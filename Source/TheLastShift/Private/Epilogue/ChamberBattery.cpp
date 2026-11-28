// SPDX-License-Identifier: MIT

#include "Epilogue/ChamberBattery.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/EpilogueChamber.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChamberBattery)

namespace ChamberBatteryImpl
{
	/** tls.Chamber.Battery's level for the session (below 0: none, the run's). */
	static float GSessionBattery = -1.f;

	static bool IsGlowMesh(const UStaticMesh* Mesh, bool& bOutSpine)
	{
		const FString Name = GetNameSafe(Mesh);
		bOutSpine = Name == TEXT("SM_BatterySpineGlow");
		return bOutSpine || Name == TEXT("SM_BatteryTierGlow") || Name == TEXT("SM_BatteryTierBGlow");
	}

	/** Smooth from 0 to 1 (the aftermath's curves: no step at its start or end). */
	static float Ease(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}
}

AChamberBattery::AChamberBattery()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// After the chamber's tick (the aftermath it reports is this frame's).
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/TheLastShift/Art/Materials/MI_BatteryGlow.MI_BatteryGlow"));
	GlowMaterial = Glow.Succeeded() ? Glow.Object : nullptr;
}

AChamberBattery* AChamberBattery::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AChamberBattery> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void AChamberBattery::SetSessionBattery(float Fraction)
{
	ChamberBatteryImpl::GSessionBattery = Fraction < 0.f ? -1.f : FMath::Clamp(Fraction, 0.f, 1.f);
}

float AChamberBattery::GetSessionBattery()
{
	return ChamberBatteryImpl::GSessionBattery;
}

float AChamberBattery::GetBatteryShown() const
{
	if (ChamberBatteryImpl::GSessionBattery >= 0.f)
	{
		return ChamberBatteryImpl::GSessionBattery;
	}
	const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this);
	return Home ? FMath::Clamp(static_cast<float>(Home->GetBatteryFraction()), 0.f, 1.f) : 1.f;
}

FLinearColor AChamberBattery::GetGlowColor() const
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const FLinearColor Tint = Settings->GetSurfaceVariant(true).Tint;
	FLinearColor Color = FMath::Lerp(Tint, FLinearColor::White, Settings->GetBatteryLook().GlowTintShare);
	Color.A = 1.f;
	return Color;
}

FName AChamberBattery::GetAftermathEnding() const
{
	return Chamber.IsValid() ? Chamber->GetAftermathEnding() : NAME_None;
}

float AChamberBattery::GetAftermathAlpha() const
{
	return Chamber.IsValid() ? Chamber->GetAftermathAlpha() : 0.f;
}

float AChamberBattery::GetGaugeLevel() const
{
	const float Level = GetBatteryShown();
	// Full at the aftermath's end, not before.
	return GetAftermathEnding() == TEXT("Inject") ? FMath::Lerp(Level, 1.f, ChamberBatteryImpl::Ease(GetAftermathAlpha())) : Level;
}

float AChamberBattery::GetScreenOutShare() const
{
	// The monitors go out one after another over the whole aftermath, the last at its end; the hold keeps the wall black.
	return GetAftermathEnding() == TEXT("Open") ? FMath::Clamp(GetAftermathAlpha(), 0.f, 1.f) : 0.f;
}

void AChamberBattery::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &AChamberBattery::HandleTimeStateChanged);
	}
	Chamber = AEpilogueChamber::Find(World);
	Collect();
	LastRun = -1.0;
	PulsePhase = 0.0;
	UpdateVisibility();
	Apply();
}

void AChamberBattery::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	UpdateVisibility();
}

void AChamberBattery::Collect()
{
	Glows.Reset();
	Rings.Reset();
	RingBase.Reset();
	RingTurns.Reset();
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (GlowMaterial)
	{
		TierMid = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		SpineMid = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		// The spines' pulses run in the world's height, so that the stacked spines read as one strip up the column.
		SpineMid->SetScalarParameterValue(TEXT("UseWorldZ"), 1.f);
		TierMid->SetScalarParameterValue(TEXT("UseWorldZ"), 0.f);
	}
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* C = *It;
		if (!C || C->GetWorld() != World || !C->IsRegistered() || !C->GetStaticMesh())
		{
			continue;
		}
		bool bSpine = false;
		if (ChamberBatteryImpl::IsGlowMesh(C->GetStaticMesh(), bSpine))
		{
			// Light, not a surface: no collision, no shadow, never in a scan's capture, no custom depth.
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->bHiddenInSceneCapture = true;
			C->SetRenderCustomDepth(false);
			C->bAffectDistanceFieldLighting = false;
			C->bAffectDynamicIndirectLighting = false;
			C->SetCanEverAffectNavigation(false);
			if (UMaterialInstanceDynamic* Mid = bSpine ? SpineMid : TierMid)
			{
				C->SetMaterial(0, Mid);
			}
			C->MarkRenderStateDirty();
			Glows.Add(C);
		}
		else if (C->GetStaticMesh()->GetName() == TEXT("SM_BatteryRing"))
		{
			Rings.Add(C);
		}
	}
	Rings.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetComponentLocation().Z < B.GetComponentLocation().Z; });
	// The rings move: their surface (the mesh's own family, the battery's) is mapped in their own space, as the doors' and
	// hatches' panels are, so that every detail of it turns with the ring instead of standing still while it turns.
	RingMid = nullptr;
	UMaterialInterface* RingSurface = Rings.Num() > 0 && Rings[0]->GetStaticMesh() ? Rings[0]->GetStaticMesh()->GetMaterial(0) : nullptr;
	if (RingSurface)
	{
		RingMid = UMaterialInstanceDynamic::Create(RingSurface, this);
		RingMid->SetScalarParameterValue(TEXT("ObjectSpace"), 1.f);
	}
	for (UStaticMeshComponent* Ring : Rings)
	{
		if (RingMid)
		{
			Ring->SetMaterial(0, RingMid);
		}
		RingBase.Add(Ring->GetComponentQuat());
		RingTurns.Add(0.0);
		if (Ring->Mobility != EComponentMobility::Movable)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: ring %s is not movable: it cannot turn."), *GetName(), *GetNameSafe(Ring->GetOwner()));
		}
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the battery: %d glow strips, %d rings, battery shown %.3f."), *GetName(), Glows.Num(), Rings.Num(), GetBatteryShown());
}

void AChamberBattery::UpdateVisibility()
{
	const bool bStopped = TimeState && TimeState->IsStopped();
	const bool bShow = !bHiddenForTest && (bShownWhileStoppedForTest || !bStopped);
	bGlowsShown = bShow;
	for (UStaticMeshComponent* Glow : Glows)
	{
		if (Glow)
		{
			Glow->SetVisibility(bShow);
		}
	}
}

void AChamberBattery::SetShownWhileStoppedForTest(bool bShown)
{
	bShownWhileStoppedForTest = bShown;
	UpdateVisibility();
}

void AChamberBattery::SetHiddenForTest(bool bInHidden)
{
	bHiddenForTest = bInHidden;
	UpdateVisibility();
}

void AChamberBattery::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Chamber.IsValid())
	{
		Chamber = AEpilogueChamber::Find(GetWorld());
	}
	// The run's clock stands still behind the pause menu; while Stopped nothing of the battery moves.
	const double Now = TimeState ? TimeState->GetRunSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const bool bStopped = TimeState && TimeState->IsStopped();
	const double Delta = LastRun >= 0.0 && !bStopped ? FMath::Max(Now - LastRun, 0.0) : 0.0;
	LastRun = Now;

	const FBatteryLook Look = UTheLastShiftSettings::Get()->GetBatteryLook();
	const float Surge = UTheLastShiftSettings::Get()->GetAftermathSurge();
	const float B = GetBatteryShown();
	const FName Ending = GetAftermathEnding();
	const float A = ChamberBatteryImpl::Ease(GetAftermathAlpha());
	float Level = FMath::Lerp(Look.GlowLevelEmpty, Look.GlowLevelFull, B);
	float Speed = FMath::Lerp(Look.PulseSpeedEmpty, Look.PulseSpeedFull, B);
	float Rings01 = 1.f;
	if (Ending == TEXT("Inject"))
	{
		// Fed: faster and faster, the glow to its full level and past it.
		Level = FMath::Lerp(Level, Look.GlowLevelFull * (1.f + Surge), A);
		const float Raw = FMath::Clamp(GetAftermathAlpha(), 0.f, 1.f);
		Speed = FMath::Lerp(Speed, Look.PulseSpeedFull * (1.f + 3.f * Surge), Raw * Raw);
	}
	else if (Ending == TEXT("Open"))
	{
		// Let go: the glow sinks to nothing, the pulses stop, the rings slow to a stand.
		Level *= 1.f - A;
		Speed *= 1.f - A;
		Rings01 = 1.f - A;
	}
	GlowLevel = Level;
	PulseSpeed = Speed;
	RingSpeed = Look.RingDegreesPerSecond * Rings01;
	PulsePhase += Speed * Delta;

	for (int32 I = 0; I < Rings.Num(); ++I)
	{
		UStaticMeshComponent* Ring = Rings[I];
		if (!Ring || Ring->Mobility != EComponentMobility::Movable)
		{
			continue;
		}
		// Each the other way from the one below it.
		const double Dir = (I % 2 == 0) ? 1.0 : -1.0;
		RingTurns[I] += Dir * RingSpeed * Delta;
		if (Delta > 0.0)
		{
			const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(RingTurns[I]));
			Ring->SetWorldRotation(Turn * RingBase[I]);
		}
	}
	Apply();
}

void AChamberBattery::Apply()
{
	const FBatteryLook Look = UTheLastShiftSettings::Get()->GetBatteryLook();
	const FLinearColor Color = GetGlowColor();
	for (UMaterialInstanceDynamic* Mid : { TierMid.Get(), SpineMid.Get() })
	{
		if (!Mid)
		{
			continue;
		}
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Level"), GlowLevel);
		// The pattern repeats every PulseSpacing: the phase is kept within it (no jump, no float drift).
		Mid->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(FMath::Fmod(PulsePhase, static_cast<double>(Look.PulseSpacingM))));
		Mid->SetScalarParameterValue(TEXT("PulseLength"), Look.PulseLengthM);
		Mid->SetScalarParameterValue(TEXT("PulseSpacing"), Look.PulseSpacingM);
	}
}
