// SPDX-License-Identifier: MIT

#include "Residue/Residue.h"

#include "Energy/EnergySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Residue)

DEFINE_LOG_CATEGORY(LogResidue);

namespace ResidueTuning
{
	// Mirrors of the UPROPERTY clamps: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static float Radius(float V) { return FMath::Clamp(V, 20.f, 1000.f); }
	static float Seconds(float V) { return FMath::Clamp(V, 0.1f, 10.f); }
	static int32 Value(int32 V) { return FMath::Clamp(V, 1, 10); }
	static int32 Threshold(int32 V) { return FMath::Clamp(V, 1, 99); }
	static int32 Points(int32 V) { return FMath::Clamp(V, 1, 2000); }
	static float Cluster(float V) { return FMath::Clamp(V, 1.f, 500.f); }
}

AResidue::AResidue()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

// --- Lifecycle ---

void AResidue::BeginPlay()
{
	Super::BeginPlay();
	EnsureBound();
	// The player pawn may not be possessed yet on the first frame.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { EnsureBound(); }));
}

void AResidue::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unbind();
	Super::EndPlay(EndPlayReason);
}

void AResidue::EnsureBound()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!TimeState)
	{
		TimeState = World->GetSubsystem<UTimeStateSubsystem>();
		if (TimeState)
		{
			TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &AResidue::HandleTimeStateChanged);
		}
	}

	if (!bEnergyBound)
	{
		if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
		{
			Energy->OnSessionBanked.AddUniqueDynamic(this, &AResidue::HandleSessionBanked);
			Energy->OnSessionLost.AddUniqueDynamic(this, &AResidue::HandleSessionLost);
			Energy->OnEnergyChanged.AddUniqueDynamic(this, &AResidue::HandleEnergyChanged);
			bEnergyBound = true;
		}
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UScanDirectorComponent* NewDirector = Pawn ? Pawn->FindComponentByClass<UScanDirectorComponent>() : nullptr;
	if (NewDirector && NewDirector != Director.Get())
	{
		if (UScanDirectorComponent* Old = Director.Get())
		{
			Old->UnregisterPointSource(TScriptInterface<ILidarPointSource>(this));
		}
		Director = NewDirector;
		NewDirector->RegisterPointSource(TScriptInterface<ILidarPointSource>(this));
	}
}

void AResidue::Unbind()
{
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &AResidue::HandleTimeStateChanged);
		TimeState = nullptr;
	}
	if (bEnergyBound)
	{
		if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
		{
			Energy->OnSessionBanked.RemoveDynamic(this, &AResidue::HandleSessionBanked);
			Energy->OnSessionLost.RemoveDynamic(this, &AResidue::HandleSessionLost);
			Energy->OnEnergyChanged.RemoveDynamic(this, &AResidue::HandleEnergyChanged);
		}
		bEnergyBound = false;
	}
	if (UScanDirectorComponent* Current = Director.Get())
	{
		Current->UnregisterPointSource(TScriptInterface<ILidarPointSource>(this));
	}
	Director.Reset();
}

// --- Collection ---

FVector AResidue::GetCollectorViewPoint(const APawn* Pawn) const
{
	return Pawn ? Pawn->GetPawnViewLocation() : FVector::ZeroVector;
}

bool AResidue::IsInCollectRange(const APawn* Pawn) const
{
	return Pawn && FVector::DistSquared(GetCollectorViewPoint(Pawn), GetActorLocation()) <= FMath::Square(ResidueTuning::Radius(CollectRadius));
}

AResidue* AResidue::FindCollectable(const APawn* Pawn)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const FVector Eye = Pawn->GetPawnViewLocation();
	AResidue* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (TActorIterator<AResidue> It(World); It; ++It)
	{
		if (It->State != EResidueState::Available || !It->IsInCollectRange(Pawn))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Eye, It->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

float AResidue::GetCollectProgress() const
{
	if (!IsCollecting())
	{
		return 0.f;
	}
	const double Elapsed = GetRunSeconds() - CollectStartTime;
	return FMath::Clamp(static_cast<float>(Elapsed / ResidueTuning::Seconds(CollectSeconds)), 0.f, 1.f);
}

bool AResidue::BeginCollect(APawn* InCollector)
{
	EnsureBound();
	if (State != EResidueState::Available || !TimeState || !TimeState->IsStopped() || TimeState->IsRunFrozen() || !IsInCollectRange(InCollector))
	{
		UE_LOG(LogResidue, Verbose, TEXT("%s: collect refused (state %s, stopped %d, in range %d)."), *GetName(), LexToString(State),
			TimeState && TimeState->IsStopped() ? 1 : 0, IsInCollectRange(InCollector) ? 1 : 0);
		return false;
	}
	if (IsCollecting())
	{
		return Collector.Get() == InCollector;
	}

	Collector = InCollector;
	CollectStartTime = GetRunSeconds();
	SetActorTickEnabled(true);
	UE_LOG(LogResidue, Log, TEXT("%s: hold started (%.2f s needed)."), *GetName(), ResidueTuning::Seconds(CollectSeconds));
	OnCollectProgress.Broadcast(0.f);
	return true;
}

void AResidue::EndCollect()
{
	// Opening the pause menu takes the keyboard focus and releases every held key: the hold stands still instead, and
	// continues from where it was when the menu closes.
	if (TimeState && TimeState->IsRunFrozen())
	{
		UE_LOG(LogResidue, Log, TEXT("%s: release ignored, the run is frozen by the pause menu (hold kept at %.2f)."), *GetName(), GetCollectProgress());
		return;
	}
	if (IsCollecting())
	{
		CancelCollect(TEXT("released"));
	}
}

void AResidue::CancelCollect(const TCHAR* Why)
{
	const float Progress = GetCollectProgress();
	Collector.Reset();
	SetActorTickEnabled(false);
	UE_LOG(LogResidue, Log, TEXT("%s: hold reset at %.2f s (%s)."), *GetName(), Progress * ResidueTuning::Seconds(CollectSeconds), Why);
	OnCollectProgress.Broadcast(0.f);
}

void AResidue::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsCollecting())
	{
		SetActorTickEnabled(false);
		return;
	}
	if (TimeState && TimeState->IsRunFrozen())
	{
		return;
	}
	if (!TimeState || !TimeState->IsStopped())
	{
		CancelCollect(TEXT("time is not stopped"));
		return;
	}
	if (!IsInCollectRange(Collector.Get()))
	{
		CancelCollect(TEXT("out of range"));
		return;
	}

	const float Progress = GetCollectProgress();
	OnCollectProgress.Broadcast(Progress);
	if (Progress >= 1.f)
	{
		CompleteCollect();
	}
}

void AResidue::CompleteCollect()
{
	const double Held = GetRunSeconds() - CollectStartTime;
	Collector.Reset();
	SetActorTickEnabled(false);
	State = EResidueState::Collected;
	UE_LOG(LogResidue, Log, TEXT("%s: collected after %.3f s held (+%d session energy)."), *GetName(), Held, ResidueTuning::Value(EnergyValue));
	OnCollected.Broadcast();

	if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
	{
		Energy->AddSessionEnergy(ResidueTuning::Value(EnergyValue));
	}
	else
	{
		UE_LOG(LogResidue, Warning, TEXT("%s: no energy subsystem, the collected energy is lost."), *GetName());
	}
}

double AResidue::GetRunSeconds() const
{
	if (TimeState)
	{
		return TimeState->GetRunSeconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void AResidue::Return(const TCHAR* Why)
{
	State = EResidueState::Available;
	UE_LOG(LogResidue, Log, TEXT("%s: back in place (%s)."), *GetName(), Why);
	OnReturned.Broadcast();
}

// --- Stop exits and energy ---

void AResidue::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	EnsureBound();
	if (NewState != ETimeState::Stopped && IsCollecting())
	{
		CancelCollect(TEXT("stop ended"));
	}
}

void AResidue::HandleSessionBanked(int32 BankedAmount)
{
	if (State == EResidueState::Collected)
	{
		State = EResidueState::Spent;
		UE_LOG(LogResidue, Log, TEXT("%s: banked, spent."), *GetName());
	}
}

void AResidue::HandleSessionLost(int32 LostEnergy, EStopExitReason Reason)
{
	if (State == EResidueState::Collected)
	{
		Return(*FString::Printf(TEXT("session lost: %s"), LexToString(Reason)));
	}
}

void AResidue::HandleEnergyChanged(int32 BankedEnergy, int32 SessionEnergy)
{
	CheckRegenerate(BankedEnergy + SessionEnergy);
}

void AResidue::CheckRegenerate(int32 TotalEnergy)
{
	if (bRegenerates && State == EResidueState::Spent && TotalEnergy < ResidueTuning::Threshold(RegenerateBelowEnergy))
	{
		Return(*FString::Printf(TEXT("regenerated: energy %d below %d"), TotalEnergy, ResidueTuning::Threshold(RegenerateBelowEnergy)));
	}
}

// --- Lidar ---

void AResidue::BuildCluster()
{
	const int32 Count = ResidueTuning::Points(PointCount);
	const float Radius = ResidueTuning::Cluster(ClusterRadius);
	if (Count == ClusterBuiltCount && Radius == ClusterBuiltRadius && ClusterSeed == ClusterBuiltSeed)
	{
		return;
	}

	// Uniform directions, radius linear in a uniform draw: the density falls off from the centre, a bright core.
	FRandomStream Rng(ClusterSeed);
	Cluster.Reset(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Cluster.Add(Rng.GetUnitVector() * (Radius * Rng.GetFraction()));
	}
	ClusterBuiltCount = Count;
	ClusterBuiltRadius = Radius;
	ClusterBuiltSeed = ClusterSeed;
}

void AResidue::GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints)
{
	LastGatheredPoints = 0;
	if (State != EResidueState::Available)
	{
		return;
	}
	BuildCluster();
	const FVector Center = GetActorLocation();
	OutPoints.Reserve(OutPoints.Num() + Cluster.Num());
	for (const FVector& Offset : Cluster)
	{
		OutPoints.Emplace(Center + Offset, 1.f);
	}
	LastGatheredPoints = Cluster.Num();
}

FLinearColor AResidue::GetResidueColor()
{
	return UTheLastShiftSettings::GetScanColors().Residue;
}

FLidarSourceSettings AResidue::GetLidarSourceSettings_Implementation() const
{
	FLidarSourceSettings Settings;
	Settings.Color = GetResidueColor();
	Settings.IntensityMultiplier = FMath::Clamp(IntensityMultiplier, 0.f, 16.f);
	Settings.FadeMode = ELidarFadeMode::ScanFade;
	Settings.bOccludeByCapture = true;
	Settings.OcclusionTolerance = FMath::Clamp(OcclusionTolerance, 0.f, 200.f);
	return Settings;
}
