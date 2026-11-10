// SPDX-License-Identifier: MIT

// Debug console commands for the entity and the lidar point-source path, so threshold, spawn rule, motion, contact and
// rewind can be checked without a keyboard (agents through the editor console, or the developer in PIE).
// Development builds only.

#include "Components/SpotLightComponent.h"
#include "Debug/EntityTestListener.h"
#include "Debug/LidarTestSource.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LidarScanComponent.h"
#include "Math/RandomStream.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"


// --- UTlsLidarTestSource (reflected, so it exists in every configuration; only used by tls.LidarTestSource) ---

void UTlsLidarTestSource::GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints)
{
	// Fixed seed: the same cube in every scan.
	FRandomStream Rng(4242);
	OutPoints.Reserve(OutPoints.Num() + PointCount);
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const FVector Offset(Rng.FRandRange(-0.5f, 0.5f), Rng.FRandRange(-0.5f, 0.5f), Rng.FRandRange(-0.5f, 0.5f));
		OutPoints.Emplace(Center + Offset * Size);
	}
}

// --- UTlsEntityTestListener (reflected, so it exists in every configuration; only used by the tls.Entity* commands) ---

void UTlsEntityTestListener::Bind(UScanDirectorComponent* InDirector, AEntity* InEntity)
{
	Unbind();
	Director = InDirector;
	Entity = InEntity;
	UWorld* World = InDirector ? InDirector->GetWorld() : (InEntity ? InEntity->GetWorld() : nullptr);
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;

	if (InDirector)
	{
		InDirector->OnBeat.AddDynamic(this, &UTlsEntityTestListener::HandleBeat);
		InDirector->OnScanCompleted.AddDynamic(this, &UTlsEntityTestListener::HandleScanCompleted);
		InDirector->OnStopExited.AddDynamic(this, &UTlsEntityTestListener::HandleStopExited);
	}
	if (UTimeStateSubsystem* Time = TimeState.Get())
	{
		Time->OnTimeStateChanged.AddDynamic(this, &UTlsEntityTestListener::HandleTimeStateChanged);
	}
	if (InEntity)
	{
		InEntity->OnAwakened.AddDynamic(this, &UTlsEntityTestListener::HandleAwakened);
		InEntity->OnSpawned.AddDynamic(this, &UTlsEntityTestListener::HandleSpawned);
		InEntity->OnContact.AddDynamic(this, &UTlsEntityTestListener::HandleContact);
	}
}

void UTlsEntityTestListener::Unbind()
{
	if (UScanDirectorComponent* Current = Director.Get())
	{
		Current->OnBeat.RemoveDynamic(this, &UTlsEntityTestListener::HandleBeat);
		Current->OnScanCompleted.RemoveDynamic(this, &UTlsEntityTestListener::HandleScanCompleted);
		Current->OnStopExited.RemoveDynamic(this, &UTlsEntityTestListener::HandleStopExited);
	}
	if (UTimeStateSubsystem* Time = TimeState.Get())
	{
		Time->OnTimeStateChanged.RemoveDynamic(this, &UTlsEntityTestListener::HandleTimeStateChanged);
	}
	if (AEntity* Current = Entity.Get())
	{
		Current->OnAwakened.RemoveDynamic(this, &UTlsEntityTestListener::HandleAwakened);
		Current->OnSpawned.RemoveDynamic(this, &UTlsEntityTestListener::HandleSpawned);
		Current->OnContact.RemoveDynamic(this, &UTlsEntityTestListener::HandleContact);
	}
	Director.Reset();
	Entity.Reset();
	TimeState.Reset();
}

void UTlsEntityTestListener::Clear()
{
	Beats.Reset();
	Spawns.Reset();
	Scans.Reset();
	AwakenTimes.Reset();
	ContactTimes.Reset();
	bHasContactPose = false;
	TimeStateExitReasons.Reset();
	StopExitReasons.Reset();
	StopExitSeconds.Reset();
}

float UTlsEntityTestListener::GetStopElapsed() const
{
	const UTimeStateSubsystem* Time = TimeState.Get();
	return Time ? Time->GetCurrentStopDuration() : 0.f;
}

float UTlsEntityTestListener::GetCumulative() const
{
	const UTimeStateSubsystem* Time = TimeState.Get();
	return Time ? Time->GetCumulativeStoppedSeconds() : 0.f;
}

static const TCHAR* ReasonName(EStopExitReason Reason)
{
	return LexToString(Reason);
}

void UTlsEntityTestListener::HandleBeat(int32 BeatIndex)
{
	FBeat& Beat = Beats.AddDefaulted_GetRef();
	Beat.Index = BeatIndex;
	Beat.Time = GetStopElapsed();
	Beat.Cumulative = GetCumulative();
	if (const AEntity* Current = Entity.Get())
	{
		Beat.bAwake = Current->GetState() == EEntityState::Awake;
		Beat.bManifested = Current->IsManifested();
		Beat.PathDistance = Current->GetPathDistanceToPlayer();
	}
}

void UTlsEntityTestListener::HandleScanCompleted(int32 ScanIndex, float Timestamp)
{
	FScan& Scan = Scans.AddDefaulted_GetRef();
	Scan.Index = ScanIndex;
	Scan.Time = GetStopElapsed();
	if (const UScanDirectorComponent* Current = Director.Get())
	{
		if (const ULidarScanComponent* Lidar = Current->GetLidarScan())
		{
			const FLidarInjectionStats Stats = Lidar->GetLastInjectionStats();
			if (Stats.ScanIndex == ScanIndex)
			{
				Scan.Uploaded = Stats.Uploaded;
				Scan.Gathered = Stats.Gathered;
				Scan.OutsideCoverage = Stats.OutsideCoverage;
				Scan.BehindDepth = Stats.BehindCapturedDepth;
			}
		}
	}
	if (const AEntity* Current = Entity.Get())
	{
		Scan.FigurePoints = Current->GetLastGatheredFigurePoints();
		Scan.TrailPoints = Current->GetLastGatheredTrailPoints();
	}
	if (bVerbose)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest %7.3f s  scan %d: entity gathered figure %d + trail %d; lidar gathered %d, outside coverage %d, uploaded %d, behind depth %d"),
			Scan.Time, ScanIndex, Scan.FigurePoints, Scan.TrailPoints, Scan.Gathered, Scan.OutsideCoverage, Scan.Uploaded, Scan.BehindDepth);
	}
}

void UTlsEntityTestListener::HandleStopExited(EStopExitReason Reason, float StoppedSeconds)
{
	StopExitReasons.Add(Reason);
	StopExitSeconds.Add(StoppedSeconds);
	if (bVerbose)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest          OnStopExited(%s, %.3f s)"), ReasonName(Reason), StoppedSeconds);
	}
}

void UTlsEntityTestListener::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	if (NewState == ETimeState::Normal && OldState == ETimeState::Stopped)
	{
		TimeStateExitReasons.Add(Reason);
		if (bVerbose)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("EntityTest          OnTimeStateChanged(Normal, Stopped, %s)"), ReasonName(Reason));
		}
	}
}

void UTlsEntityTestListener::HandleAwakened()
{
	AwakenTimes.Add(GetStopElapsed());
	if (bVerbose)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest %7.3f s  OnAwakened (cumulative %.3f s)"), GetStopElapsed(), GetCumulative());
	}
}

void UTlsEntityTestListener::HandleSpawned(FVector Location, bool bHadLineOfSight)
{
	FSpawn& Spawn = Spawns.AddDefaulted_GetRef();
	Spawn.Time = GetStopElapsed();
	Spawn.Cumulative = GetCumulative();
	Spawn.Location = Location;
	Spawn.bHadLineOfSight = bHadLineOfSight;
	if (bVerbose)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest %7.3f s  OnSpawned at %s (line of sight %d, cumulative %.3f s)"),
			Spawn.Time, *Location.ToString(), bHadLineOfSight ? 1 : 0, Spawn.Cumulative);
	}
}

void UTlsEntityTestListener::HandleContact()
{
	ContactTimes.Add(GetStopElapsed());
	if (!bHasContactPose)
	{
		// Broadcast before the director ends the Stop: this is the pose a Jump must keep.
		const UWorld* World = TimeState.IsValid() ? TimeState->GetWorld() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			bHasContactPose = true;
			ContactPawnTransform = Pawn->GetActorTransform();
			ContactViewRotation = PC->GetControlRotation();
		}
	}
	if (bVerbose)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest %7.3f s  OnContact"), GetStopElapsed());
	}
}

#if !UE_BUILD_SHIPPING

namespace EntityDebug
{
	using namespace TimeStateDebug;

	static AEntity* FindEntity(UWorld* World)
	{
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static APawn* GetPlayerPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	static bool IsFlashlightOn(const APawn* Pawn)
	{
		const USpotLightComponent* Light = Pawn ? Pawn->FindComponentByClass<USpotLightComponent>() : nullptr;
		return Light && Light->IsVisible();
	}

	/** Same sample heights as the entity's own checks: shins, waist, head. */
	static bool IsVisibleFrom(UWorld* World, const FVector& Eye, const FVector& FigureBase, const AActor* Ignore)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsEntityVisibility), false, Ignore);
		for (const float Height : { 30.f, 100.f, 165.f })
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Eye, FigureBase + FVector(0.f, 0.f, Height), ECC_Visibility, Params))
			{
				return true;
			}
		}
		return false;
	}

	static bool IsVisibleFromCamera(UWorld* World, const FVector& FigureBase)
	{
		APawn* Pawn = GetPlayerPawn(World);
		const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
		if (!Controller)
		{
			return false;
		}
		FVector Eye;
		FRotator Rotation;
		Controller->GetPlayerViewPoint(Eye, Rotation);
		return IsVisibleFrom(World, Eye, FigureBase, Pawn);
	}

	static bool IsVisibleFromAnyScan(UWorld* World, const UScanDirectorComponent* Director, const FVector& FigureBase)
	{
		if (!Director)
		{
			return false;
		}
		for (const FLidarScanContext& Scan : Director->GetRecentScanOrigins())
		{
			bool bInCoverage = false;
			for (const float Height : { 30.f, 100.f, 165.f })
			{
				double Depth = 0.0;
				bInCoverage |= Scan.IsInCoverage(FigureBase + FVector(0.f, 0.f, Height), Depth);
			}
			if (bInCoverage && IsVisibleFrom(World, Scan.CaptureOrigin, FigureBase, GetPlayerPawn(World)))
			{
				return true;
			}
		}
		return false;
	}

	static float AngleBetween(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(static_cast<float>(A.AngularDistance(B)));
	}

	/**
	 * Checks the exit after a contact: the reason matches the entity's ContactResponse on both events, the pawn is on
	 * the Stop anchor (rewind reasons) or where it stood at contact (Jumped), and nothing of the Stop is left behind.
	 * Returns true on success.
	 */
	static bool CheckContactExit(UWorld* World, const UScanDirectorComponent* Director, const AEntity* Entity, const UTlsEntityTestListener* Listener,
		bool bFlashlightBefore, const TCHAR* Prefix)
	{
		const APawn* Pawn = GetPlayerPawn(World);
		const FReport R = BuildReport(World);
		const EStopExitReason Expected = GetExitReasonForContact(Entity ? Entity->ContactResponse : EEntityContactResponse::Rewind);
		const bool bRewind = IsRewindExit(Expected);
		const FVector TargetLocation = bRewind ? Director->GetStopAnchor().GetLocation() : Listener->ContactPawnTransform.GetLocation();
		const FQuat TargetRotation = bRewind ? Director->GetStopAnchor().GetRotation() : Listener->ContactPawnTransform.GetRotation();
		const FRotator TargetView = bRewind ? Director->GetStopAnchorViewRotation() : Listener->ContactViewRotation;
		const bool bHasTarget = bRewind || Listener->bHasContactPose;
		const float LocationError = Pawn && bHasTarget ? static_cast<float>(FVector::Dist(Pawn->GetActorLocation(), TargetLocation)) : -1.f;
		const float RotationError = Pawn && bHasTarget ? AngleBetween(Pawn->GetActorQuat(), TargetRotation) : -1.f;
		const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
		const float ViewError = Controller ? AngleBetween(Controller->GetControlRotation().Quaternion(), TargetView.Quaternion()) : 0.f;
		const float FromAnchor = Pawn ? static_cast<float>(FVector::Dist(Pawn->GetActorLocation(), Director->GetStopAnchor().GetLocation())) : -1.f;
		const bool bTimeStateReason = Listener->TimeStateExitReasons.Num() > 0 && Listener->TimeStateExitReasons.Last() == Expected;
		const bool bExitReason = Listener->StopExitReasons.Num() > 0 && Listener->StopExitReasons.Last() == Expected;
		const bool bFlashlight = R.bFlashlightOn == bFlashlightBefore;
		const bool bEntityGone = Entity && !Entity->IsManifested();
		const bool bNextAtMin = Entity && FMath::IsNearlyEqual(Entity->GetSpawnDistanceForNextStop(), FMath::Clamp(Entity->MinSpawnDistance, 0.f, Entity->InitialSpawnDistance), 0.5f);

		const bool bOk = LocationError >= 0.f && LocationError <= 1.f && RotationError >= 0.f && RotationError <= 0.5f && ViewError <= 0.5f
			&& R.State == ETimeState::Normal && bTimeStateReason && bExitReason && bFlashlight && R.AliveScans == 0 && !R.bCycleActive
			&& !R.bTimersPending && R.ActiveNiagaraComponents == 0 && bEntityGone && bNextAtMin;

		UE_LOG(LogTheLastShift, Log,
			TEXT("%s contact exit check %s: response %s, expected exit %s, pose target %s, position error %.3f cm, rotation error %.3f deg, view error %.3f deg, %.1f cm from the Stop anchor, state %s, OnTimeStateChanged %s, OnStopExited %s, flashlight %s (before stop %s), alive scans %d, cycle %d, timers %d, active niagara %d, entity manifested %d, next spawn %.1f cm"),
			Prefix, bOk ? TEXT("OK") : TEXT("FAIL"), Entity ? LexToString(Entity->ContactResponse) : TEXT("-"), LexToString(Expected),
			bRewind ? TEXT("Stop anchor") : TEXT("pose at contact"), LocationError, RotationError, ViewError, FromAnchor,
			R.State == ETimeState::Stopped ? TEXT("Stopped") : TEXT("Normal"),
			Listener->TimeStateExitReasons.Num() ? ReasonName(Listener->TimeStateExitReasons.Last()) : TEXT("none"),
			Listener->StopExitReasons.Num() ? ReasonName(Listener->StopExitReasons.Last()) : TEXT("none"),
			R.bFlashlightOn ? TEXT("on") : TEXT("off"), bFlashlightBefore ? TEXT("on") : TEXT("off"), R.AliveScans,
			R.bCycleActive ? 1 : 0, R.bTimersPending ? 1 : 0, R.ActiveNiagaraComponents, Entity && Entity->IsManifested() ? 1 : 0,
			Entity ? Entity->GetSpawnDistanceForNextStop() : -1.f);
		return bOk;
	}

	static bool AnyTestRunning();

	// ------------------------------------------------------------------------------------------------------------
	// tls.EntityTest: one Stop, per-beat log, motion samples, contact and rewind check.

	struct FEntityTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UScanDirectorComponent> Director;
		TWeakObjectPtr<AEntity> Entity;
		TStrongObjectPtr<UTlsEntityTestListener> Listener;
		float Duration = 30.f;
		/** Request a scan whenever the director allows one, so every beat interval has fresh entity points. */
		bool bAutoScan = true;
		bool bFlashlightBefore = false;
		int32 LoggedBeats = 0;
		TArray<FVector2D> Samples; // (time since stop, path distance)
		FTimerHandle SampleTimer;
		FTimerHandle EndTimer;
		bool bRunning = false;
		bool bFinishing = false;
	};
	static FEntityTest GEntityTest;

	static void EntityTestSummary();

	static void EntityTestFinish()
	{
		UWorld* World = GEntityTest.World.Get();
		if (!World || GEntityTest.bFinishing)
		{
			return;
		}
		GEntityTest.bFinishing = true;
		World->GetTimerManager().ClearTimer(GEntityTest.SampleTimer);
		World->GetTimerManager().ClearTimer(GEntityTest.EndTimer);

		if (UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>(); TimeState && TimeState->IsStopped())
		{
			TimeState->Resume();
		}

		const UScanDirectorComponent* Director = GEntityTest.Director.Get();
		const float Settle = (Director ? FMath::Clamp(Director->ResumeFadeOut, 0.f, 2.f) : 0.4f) + 0.15f;
		FTimerHandle SettleTimer;
		World->GetTimerManager().SetTimer(SettleTimer, FTimerDelegate::CreateStatic(&EntityTestSummary), Settle, false);
	}

	static void EntityTestSample()
	{
		UWorld* World = GEntityTest.World.Get();
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		AEntity* Entity = GEntityTest.Entity.Get();
		UTlsEntityTestListener* L = GEntityTest.Listener.Get();
		if (!TimeState || !Entity || !L)
		{
			GEntityTest.bRunning = false;
			return;
		}
		if (!TimeState->IsStopped())
		{
			// Contact rewound the player (or something else resumed).
			EntityTestFinish();
			return;
		}

		const float T = TimeState->GetCurrentStopDuration();
		if (Entity->IsManifested())
		{
			GEntityTest.Samples.Emplace(T, Entity->GetPathDistanceToPlayer());
		}
		if (UScanDirectorComponent* Director = GEntityTest.Director.Get(); GEntityTest.bAutoScan && Director && Director->IsScanReady())
		{
			Director->RequestScan();
		}

		// One line per beat, as soon as it has been recorded.
		while (GEntityTest.LoggedBeats < L->Beats.Num())
		{
			const UTlsEntityTestListener::FBeat& Beat = L->Beats[GEntityTest.LoggedBeats++];
			const FVector Figure = Entity->GetFigureLocation();
			const bool bManifested = Entity->IsManifested();
			const int32 LastUploaded = L->Scans.Num() ? L->Scans.Last().Uploaded : 0;
			const int32 LastEntityPoints = L->Scans.Num() ? L->Scans.Last().FigurePoints + L->Scans.Last().TrailPoints : 0;
			UE_LOG(LogTheLastShift, Log,
				TEXT("EntityTest %7.3f s  beat %d: cumulative %.3f s, state %s, manifested %d, figure %s, LOS from camera %d, visible from a scan origin %d, path distance %.1f cm, last scan points (entity %d, uploaded %d), contact %s"),
				Beat.Time, Beat.Index, Beat.Cumulative, Beat.bAwake ? TEXT("Awake") : TEXT("Dormant"), bManifested ? 1 : 0,
				bManifested ? *Figure.ToString() : TEXT("-"), bManifested && IsVisibleFromCamera(World, Figure) ? 1 : 0,
				bManifested && IsVisibleFromAnyScan(World, GEntityTest.Director.Get(), Figure) ? 1 : 0, Beat.PathDistance,
				LastEntityPoints, LastUploaded, L->ContactTimes.Num() ? *FString::Printf(TEXT("%.3f s"), L->ContactTimes[0]) : TEXT("none"));
		}
	}

	/** Path distance sampled closest to time T (within MaxGap), or -1. */
	static float SampleAt(float T, float MaxGap = 0.06f)
	{
		float Best = -1.f;
		float BestGap = MaxGap;
		for (const FVector2D& Sample : GEntityTest.Samples)
		{
			const float Gap = FMath::Abs(static_cast<float>(Sample.X) - T);
			if (Gap <= BestGap && Sample.Y >= 0.0)
			{
				BestGap = Gap;
				Best = static_cast<float>(Sample.Y);
			}
		}
		return Best;
	}

	static void EntityTestSummary()
	{
		UWorld* World = GEntityTest.World.Get();
		UScanDirectorComponent* Director = GEntityTest.Director.Get();
		AEntity* Entity = GEntityTest.Entity.Get();
		UTlsEntityTestListener* L = GEntityTest.Listener.Get();
		GEntityTest.bRunning = false;
		if (!World || !Director || !Entity || !L)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("EntityTest aborted: world, director or entity gone."));
			GEntityTest.Listener.Reset();
			return;
		}
		L->Unbind();

		const float Speed = FMath::Clamp(Entity->Speed, 0.f, 1000.f);
		const float Radius = FMath::Clamp(Entity->ContactRadius, 10.f, 1000.f);

		// Threshold and spawn.
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest summary: OnAwakened %d time(s)%s, spawns %d, scans %d (with entity points %d), contact %s"),
			L->AwakenTimes.Num(), L->AwakenTimes.Num() ? *FString::Printf(TEXT(" at %.3f s"), L->AwakenTimes[0]) : TEXT(""),
			L->Spawns.Num(), L->Scans.Num(),
			L->Scans.FilterByPredicate([](const UTlsEntityTestListener::FScan& S) { return S.FigurePoints + S.TrailPoints > 0; }).Num(),
			L->ContactTimes.Num() ? *FString::Printf(TEXT("at %.3f s"), L->ContactTimes[0]) : TEXT("none"));

		float SpawnTime = -1.f;
		float SpawnDistance = -1.f;
		if (L->Spawns.Num())
		{
			const FEntitySpawnReport Report = Entity->GetLastSpawnReport();
			SpawnTime = L->Spawns[0].Time;
			SpawnDistance = Report.PathDistance;
			UE_LOG(LogTheLastShift, Log, TEXT("  spawn at %.3f s (cumulative %.3f s): %s, path distance %.1f cm (target %.1f, %+.1f%%), %d queries, %d candidates, noLOS=%d onDistance=%d hiddenFromScans=%d forward=%d fallback=%d"),
				SpawnTime, L->Spawns[0].Cumulative, *Report.Location.ToString(), Report.PathDistance, Report.TargetDistance,
				Report.TargetDistance > 0.f ? 100.f * (Report.PathDistance - Report.TargetDistance) / Report.TargetDistance : 0.f,
				Report.Attempts, Report.Candidates, Report.bNoLineOfSight ? 1 : 0, Report.bDistanceInTolerance ? 1 : 0,
				Report.bHiddenFromScans ? 1 : 0, Report.bInForwardHemisphere ? 1 : 0, Report.bFallback ? 1 : 0);
		}

		// Motion: continuous at Speed. Least-squares slope over all samples, per-beat-interval drop, and no step at beats.
		const TArray<FVector2D>& S = GEntityTest.Samples;
		if (S.Num() >= 4)
		{
			double SumT = 0, SumD = 0, SumTT = 0, SumTD = 0;
			int32 N = 0;
			for (const FVector2D& Sample : S)
			{
				if (Sample.Y < Radius + 50.0)
				{
					continue; // ignore the last stretch: contact is a feet distance, not a path distance
				}
				SumT += Sample.X; SumD += Sample.Y; SumTT += Sample.X * Sample.X; SumTD += Sample.X * Sample.Y; ++N;
			}
			const double Slope = N > 1 ? (N * SumTD - SumT * SumD) / FMath::Max(N * SumTT - SumT * SumT, 1e-6) : 0.0;
			const bool bSlopeOk = Speed <= 0.f || FMath::Abs(-Slope - Speed) <= 0.05 * Speed;
			UE_LOG(LogTheLastShift, Log, TEXT("  motion: %d samples, fitted speed %.2f cm/s (Speed %.2f, %+.1f%%) %s"),
				N, -Slope, Speed, Speed > 0.f ? 100.0 * (-Slope - Speed) / Speed : 0.0, bSlopeOk ? TEXT("OK") : TEXT("OFF"));

			for (int32 i = 0; i < L->Beats.Num(); ++i)
			{
				const float Tb = L->Beats[i].Time;
				const float Before = SampleAt(Tb - 0.1f);
				const float After = SampleAt(Tb + 0.1f);
				FString Interval;
				if (i + 1 < L->Beats.Num())
				{
					const float Next = SampleAt(L->Beats[i + 1].Time - 0.1f);
					const float Expected = Speed * (L->Beats[i + 1].Time - Tb - 0.2f);
					if (After >= 0.f && Next >= 0.f && Next > Radius + 50.f)
					{
						Interval = FString::Printf(TEXT(", to next beat -%.1f cm (expected -%.1f, %+.1f%%)"), After - Next, Expected,
							Expected > 0.f ? 100.f * ((After - Next) - Expected) / Expected : 0.f);
					}
				}
				UE_LOG(LogTheLastShift, Log, TEXT("  beat %d at %.3f s: path distance %.1f -> %.1f cm across the beat (+-0.1 s, expected -%.1f)%s"),
					L->Beats[i].Index, Tb, Before, After, Speed * 0.2f, *Interval);
			}
		}

		if (L->ContactTimes.Num() && SpawnTime >= 0.f && Speed > 0.f)
		{
			const float Predicted = SpawnTime + (SpawnDistance - Radius) / Speed;
			UE_LOG(LogTheLastShift, Log, TEXT("  contact at %.3f s; straight-path prediction (spawn %.3f s + (%.1f - %.1f) / %.2f) %.3f s, %+.3f s"),
				L->ContactTimes[0], SpawnTime, SpawnDistance, Radius, Speed, Predicted, L->ContactTimes[0] - Predicted);
			CheckContactExit(World, Director, Entity, L, GEntityTest.bFlashlightBefore, TEXT("EntityTest"));
		}
		else
		{
			LogReport(World, TEXT("EntityTest after resume:"));
		}
		UE_LOG(LogTheLastShift, Log, TEXT("  contact response %s, exit reason OnTimeStateChanged %s, OnStopExited %s"),
			LexToString(Entity->ContactResponse),
			L->TimeStateExitReasons.Num() ? ReasonName(L->TimeStateExitReasons.Last()) : TEXT("none"),
			L->StopExitReasons.Num() ? ReasonName(L->StopExitReasons.Last()) : TEXT("none"));
		GEntityTest.Listener.Reset();
	}

	// ------------------------------------------------------------------------------------------------------------
	// tls.EntitySpawnTest: N appearances from the current pose, spawn rule statistics.

	struct FSpawnTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AEntity> Entity;
		int32 Runs = 20;
		float Distance = 0.f;
		int32 Done = 0;
		int32 NoLineOfSight = 0;
		int32 InTolerance = 0;
		int32 Hidden = 0;
		int32 Forward = 0;
		int32 Fallbacks = 0;
		int32 NoSpawn = 0;
		int32 NoValid = 0;
		/** Consecutive spawns closer than SpawnRepeatRadius, and those excused because every valid candidate was. */
		int32 Repeats = 0;
		int32 ExcusedRepeats = 0;
		int32 TotalCandidates = 0;
		int32 TotalValid = 0;
		int32 TotalAttempts = 0;
		TArray<FVector> Locations;
		/** Every Stop starts from this pose (actor transform and view rotation), recorded at the start. */
		FTransform StartPose;
		FRotator StartView = FRotator::ZeroRotator;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FSpawnTest GSpawnTest;

	/**
	 * Agglomerative clustering: every point starts as its own cluster; the two clusters with the closest centroids
	 * merge while those centroids are closer than Separation. Returned centres are all at least Separation apart.
	 */
	static TArray<FVector> ClusterCentres(const TArray<FVector>& Points, float Separation, TArray<int32>& OutCounts)
	{
		TArray<FVector> Sums(Points);
		OutCounts.Init(1, Points.Num());
		for (;;)
		{
			int32 BestA = INDEX_NONE;
			int32 BestB = INDEX_NONE;
			double BestDistance = Separation;
			for (int32 A = 0; A < Sums.Num(); ++A)
			{
				for (int32 B = A + 1; B < Sums.Num(); ++B)
				{
					const double Distance = FVector::Dist(Sums[A] / OutCounts[A], Sums[B] / OutCounts[B]);
					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						BestA = A;
						BestB = B;
					}
				}
			}
			if (BestA == INDEX_NONE)
			{
				break;
			}
			Sums[BestA] += Sums[BestB];
			OutCounts[BestA] += OutCounts[BestB];
			Sums.RemoveAt(BestB);
			OutCounts.RemoveAt(BestB);
		}
		TArray<FVector> Centres;
		for (int32 Index = 0; Index < Sums.Num(); ++Index)
		{
			Centres.Add(Sums[Index] / OutCounts[Index]);
		}
		return Centres;
	}

	static void SpawnTestRestorePose(UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!Pawn)
		{
			return;
		}
		Pawn->SetActorLocationAndRotation(GSpawnTest.StartPose.GetLocation(), GSpawnTest.StartPose.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(GSpawnTest.StartView);
		}
	}

	static void SpawnTestSummary(AEntity* Entity)
	{
		const float Separation = 400.f;
		TArray<int32> Counts;
		const TArray<FVector> Centres = ClusterCentres(GSpawnTest.Locations, Separation, Counts);
		for (int32 Index = 0; Index < Centres.Num(); ++Index)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  cluster %d: %d spawn(s) around %s"), Index + 1, Counts[Index], *Centres[Index].ToString());
		}

		const int32 Runs = GSpawnTest.Runs;
		const bool bPass = GSpawnTest.NoSpawn == 0 && GSpawnTest.NoLineOfSight == Runs && GSpawnTest.InTolerance == Runs
			&& Centres.Num() >= 3 && GSpawnTest.Repeats == GSpawnTest.ExcusedRepeats;
		UE_LOG(LogTheLastShift, Log,
			TEXT("SpawnTest summary (%s): %d runs, no appearance %d, no line of sight %d, path distance within %.0f%% %d, hidden from scans %d, forward %d, no valid candidate %d, fallbacks %d, distinct clusters (centres > %.0f cm apart) %d, consecutive within SpawnRepeatRadius %.0f cm: %d (%d with only near candidates), avg %.1f queries / %.1f candidates / %.1f valid per spawn"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), Runs, GSpawnTest.NoSpawn, GSpawnTest.NoLineOfSight,
			FMath::Clamp(Entity->SpawnDistanceTolerance, 0.f, 0.5f) * 100.f, GSpawnTest.InTolerance, GSpawnTest.Hidden, GSpawnTest.Forward,
			GSpawnTest.NoValid, GSpawnTest.Fallbacks, Separation, Centres.Num(), FMath::Clamp(Entity->SpawnRepeatRadius, 0.f, 5000.f),
			GSpawnTest.Repeats, GSpawnTest.ExcusedRepeats,
			static_cast<float>(GSpawnTest.TotalAttempts) / Runs, static_cast<float>(GSpawnTest.TotalCandidates) / Runs,
			static_cast<float>(GSpawnTest.TotalValid) / Runs);
	}

	static void SpawnTestStep()
	{
		UWorld* World = GSpawnTest.World.Get();
		AEntity* Entity = GSpawnTest.Entity.Get();
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!TimeState || !Entity)
		{
			GSpawnTest.bRunning = false;
			return;
		}

		const float Target = GSpawnTest.Distance > 0.f ? GSpawnTest.Distance : Entity->InitialSpawnDistance;

		if (TimeState->IsStopped())
		{
			// Record the appearance of this Stop, then leave.
			const bool bManifested = Entity->IsManifested();
			const FEntitySpawnReport R = bManifested ? Entity->GetLastSpawnReport() : FEntitySpawnReport();
			++GSpawnTest.Done;
			GSpawnTest.NoSpawn += bManifested ? 0 : 1;
			GSpawnTest.NoLineOfSight += R.bNoLineOfSight ? 1 : 0;
			const float Tolerance = FMath::Clamp(Entity->SpawnDistanceTolerance, 0.f, 0.5f);
			const bool bWithin = R.TargetDistance > 0.f && FMath::Abs(R.PathDistance - R.TargetDistance) <= Tolerance * R.TargetDistance;
			GSpawnTest.InTolerance += bWithin ? 1 : 0;
			GSpawnTest.Hidden += R.bHiddenFromScans ? 1 : 0;
			GSpawnTest.Forward += R.bInForwardHemisphere ? 1 : 0;
			GSpawnTest.Fallbacks += R.bFallback ? 1 : 0;
			GSpawnTest.NoValid += (bManifested && R.ValidCandidates == 0) ? 1 : 0;
			GSpawnTest.TotalCandidates += R.Candidates;
			GSpawnTest.TotalValid += R.ValidCandidates;
			GSpawnTest.TotalAttempts += R.Attempts;

			float FromPrevious = -1.f;
			bool bRepeat = false;
			if (bManifested)
			{
				if (GSpawnTest.Locations.Num())
				{
					FromPrevious = static_cast<float>(FVector::Dist(R.Location, GSpawnTest.Locations.Last()));
					bRepeat = FromPrevious < FMath::Clamp(Entity->SpawnRepeatRadius, 0.f, 5000.f);
					GSpawnTest.Repeats += bRepeat ? 1 : 0;
					GSpawnTest.ExcusedRepeats += (bRepeat && R.bOnlyNearPreviousSpawn) ? 1 : 0;
				}
				GSpawnTest.Locations.Add(R.Location);
			}
			UE_LOG(LogTheLastShift, Log, TEXT("SpawnTest %2d/%d: %s path %.1f cm (target %.1f, %+.1f%%), %d queries, %d candidates, %d valid (%d near previous); %s weight %.2f/%.2f; noLOS=%d within=%d hiddenFromScans=%d forward=%d fallback=%d; %.0f cm from previous spawn%s"),
				GSpawnTest.Done, GSpawnTest.Runs, *R.Location.ToString(), R.PathDistance, R.TargetDistance,
				R.TargetDistance > 0.f ? 100.f * (R.PathDistance - R.TargetDistance) / R.TargetDistance : 0.f, R.Attempts, R.Candidates,
				R.ValidCandidates, R.ValidNearPreviousSpawn, R.bRandomPick ? TEXT("random") : TEXT("best score"), R.PickWeight, R.TotalWeight,
				R.bNoLineOfSight ? 1 : 0, bWithin ? 1 : 0, R.bHiddenFromScans ? 1 : 0, R.bInForwardHemisphere ? 1 : 0, R.bFallback ? 1 : 0,
				FromPrevious, bRepeat ? (R.bOnlyNearPreviousSpawn ? TEXT(" (REPEAT, only near candidates)") : TEXT(" (REPEAT)")) : TEXT(""));
			TimeState->Resume();

			if (GSpawnTest.Done >= GSpawnTest.Runs)
			{
				GSpawnTest.bRunning = false;
				Entity->SetSpawnDistanceForNextStop(Entity->InitialSpawnDistance);
				SpawnTestSummary(Entity);
				return;
			}
			World->GetTimerManager().SetTimer(GSpawnTest.Timer, FTimerDelegate::CreateStatic(&SpawnTestStep), 0.5f, false);
			return;
		}

		// Every run aims for the same distance from the same pose.
		SpawnTestRestorePose(World);
		Entity->SetSpawnDistanceForNextStop(Target);
		TimeState->Stop();
		World->GetTimerManager().SetTimer(GSpawnTest.Timer, FTimerDelegate::CreateStatic(&SpawnTestStep), 0.2f, false);
	}

	// ------------------------------------------------------------------------------------------------------------
	// tls.TransientTest: entity points full shortly after their scan, gone a few seconds later, never extended.

	struct FTransientTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UScanDirectorComponent> Director;
		TWeakObjectPtr<AEntity> Entity;
		/** Fire a second scan at +SecondScanAt (0 = single scan). */
		float SecondScanAt = 0.f;
		bool bScreenshots = true;
		float SavedSpeed = 0.f;
		int32 FirstScanIndex = INDEX_NONE;
		int32 SecondScanIndex = INDEX_NONE;
		double FirstScanRealTime = 0.0;
		double WaitStart = 0.0;
		int32 Phase = 0;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FTransientTest GTransientTest;

	static void TransientTestStep();

	/** The figure's waist is within 30 degrees of the view direction. */
	static bool IsInView(UWorld* World, const FVector& FigureBase)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			return false;
		}
		FVector Eye;
		FRotator Rotation;
		PC->GetPlayerViewPoint(Eye, Rotation);
		const FVector ToFigure = (FigureBase + FVector(0.f, 0.f, 100.f) - Eye).GetSafeNormal();
		return FVector::DotProduct(ToFigure, Rotation.Vector()) >= FMath::Cos(FMath::DegreesToRadians(30.f));
	}

	static void TransientSchedule(float Delay)
	{
		if (UWorld* World = GTransientTest.World.Get())
		{
			World->GetTimerManager().SetTimer(GTransientTest.Timer, FTimerDelegate::CreateStatic(&TransientTestStep), Delay, false);
		}
	}

	/** Expected alpha factor of a transient point TimeSinceScan seconds after its scan (plugin formula). */
	static float TransientAlpha(float TimeSinceScan, float Hold, float Vanish)
	{
		return FMath::Clamp(1.f - (TimeSinceScan - Hold) / FMath::Max(Vanish, 0.001f), 0.f, 1.f);
	}

	static void TransientCheckpoint(UWorld* World, const TCHAR* Label)
	{
		const AEntity* Entity = GTransientTest.Entity.Get();
		const UScanDirectorComponent* Director = GTransientTest.Director.Get();
		const ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		const double Now = World->GetRealTimeSeconds();
		const float Hold = Entity ? FMath::Clamp(Entity->PointHoldSeconds, 0.f, 30.f) : 0.f;
		const float Vanish = Entity ? FMath::Clamp(Entity->PointVanishSeconds, 0.f, 30.f) : 0.f;
		const float SinceFirst = static_cast<float>(Now - GTransientTest.FirstScanRealTime);
		FString Second;
		if (GTransientTest.SecondScanIndex != INDEX_NONE)
		{
			const float SinceSecond = SinceFirst - GTransientTest.SecondScanAt;
			Second = FString::Printf(TEXT(", scan %d (+%.3f s) entity alpha %.3f"), GTransientTest.SecondScanIndex, SinceSecond, TransientAlpha(SinceSecond, Hold, Vanish));
		}
		UE_LOG(LogTheLastShift, Log, TEXT("TransientTest %s: +%.3f s after scan %d, its entity alpha %.3f (hold %.2f s, vanish %.2f s)%s; alive scans %d, figure %s, visible from camera %d"),
			Label, SinceFirst, GTransientTest.FirstScanIndex, TransientAlpha(SinceFirst, Hold, Vanish), Hold, Vanish, *Second,
			Lidar ? Lidar->GetAliveScanCount() : -1, Entity ? *Entity->GetFigureLocation().ToString() : TEXT("-"),
			Entity && IsVisibleFromCamera(World, Entity->GetFigureLocation()) ? 1 : 0);
		if (GTransientTest.bScreenshots)
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("HighResShot 1"));
			}
		}
	}

	static void TransientTestEnd(const TCHAR* Why)
	{
		UWorld* World = GTransientTest.World.Get();
		if (World)
		{
			World->GetTimerManager().ClearTimer(GTransientTest.Timer);
		}
		if (AEntity* Entity = GTransientTest.Entity.Get())
		{
			Entity->Speed = GTransientTest.SavedSpeed;
		}
		if (UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; TimeState && TimeState->IsStopped())
		{
			TimeState->Resume();
		}
		GTransientTest.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("TransientTest done: %s."), Why);
	}

	static void TransientTestStep()
	{
		UWorld* World = GTransientTest.World.Get();
		AEntity* Entity = GTransientTest.Entity.Get();
		UScanDirectorComponent* Director = GTransientTest.Director.Get();
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!World || !Entity || !Lidar || !TimeState || !TimeState->IsStopped())
		{
			TransientTestEnd(TEXT("aborted (world, entity or lidar gone, or time resumed)"));
			return;
		}

		const double Since = World->GetRealTimeSeconds() - GTransientTest.FirstScanRealTime;
		switch (GTransientTest.Phase)
		{
		case 0:
			// Wait until the figure is in view, then freeze it so every scan shows it in the same place.
			if (Entity->IsManifested() && IsVisibleFromCamera(World, Entity->GetFigureLocation()) && IsInView(World, Entity->GetFigureLocation()))
			{
				Entity->Speed = 0.f;
				// Bypass the director's cooldown: clear every scan and fire the plugin directly.
				Lidar->ClearScans(0.f);
				GTransientTest.FirstScanIndex = Lidar->GetScanCount();
				Lidar->Scan();
				GTransientTest.FirstScanRealTime = World->GetRealTimeSeconds();
				UE_LOG(LogTheLastShift, Log, TEXT("TransientTest: figure visible at %s, path distance %.1f cm; scan %d fired (cooldown bypassed), figure frozen."),
					*Entity->GetFigureLocation().ToString(), Entity->GetPathDistanceToPlayer(), GTransientTest.FirstScanIndex);
				GTransientTest.Phase = 1;
				TransientSchedule(0.4f);
				return;
			}
			if (World->GetRealTimeSeconds() - GTransientTest.WaitStart > 30.0)
			{
				TransientTestEnd(TEXT("figure never came into view within 30 s"));
				return;
			}
			TransientSchedule(0.05f);
			return;
		case 1:
			TransientCheckpoint(World, TEXT("checkpoint A (expect full)"));
			GTransientTest.Phase = 2;
			if (GTransientTest.SecondScanAt > 0.f)
			{
				TransientSchedule(FMath::Max(GTransientTest.SecondScanAt - static_cast<float>(Since), 0.f));
				return;
			}
			TransientSchedule(FMath::Max(3.0f - static_cast<float>(Since), 0.f));
			GTransientTest.Phase = 3;
			return;
		case 2:
			GTransientTest.SecondScanIndex = Lidar->GetScanCount();
			Lidar->Scan();
			UE_LOG(LogTheLastShift, Log, TEXT("TransientTest: second scan %d fired at +%.3f s (cooldown bypassed)."), GTransientTest.SecondScanIndex, Since);
			GTransientTest.Phase = 3;
			TransientSchedule(FMath::Max(3.0f - static_cast<float>(Since), 0.f));
			return;
		case 3:
			TransientCheckpoint(World, TEXT("checkpoint B (expect first scan's entity points gone, captured points still shown)"));
			GTransientTest.Phase = 4;
			TransientSchedule(1.0f); // let the screenshot land before resuming
			return;
		default:
			TransientTestEnd(TEXT("complete"));
			return;
		}
	}

	// ------------------------------------------------------------------------------------------------------------
	// tls.EntityCycleTest: escalation over Stop -> Resume cycles, then a rewind, then the spawn after it.

	struct FCycleEscalation
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UScanDirectorComponent> Director;
		TWeakObjectPtr<AEntity> Entity;
		TStrongObjectPtr<UTlsEntityTestListener> Listener;
		int32 Cycles = 3;
		float StopSeconds = 5.f;
		int32 Stops = 0;
		float ExpectedSpawn = 0.f;
		int32 Failures = 0;
		bool bFlashlightBefore = false;
		/** 0 = regular cycles, 1 = waiting for contact, 2 = stop after the rewind. */
		int32 Phase = 0;
		double PhaseStart = 0.0;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FCycleEscalation GEscalation;

	static void EscalationStep();

	static void EscalationSchedule(float Delay)
	{
		if (UWorld* World = GEscalation.World.Get())
		{
			World->GetTimerManager().SetTimer(GEscalation.Timer, FTimerDelegate::CreateStatic(&EscalationStep), Delay, false);
		}
	}

	static void EscalationCheckSpawn(AEntity* Entity)
	{
		const FEntitySpawnReport R = Entity->GetLastSpawnReport();
		const bool bTargetOk = FMath::IsNearlyEqual(R.TargetDistance, GEscalation.ExpectedSpawn, 0.5f);
		const bool bPathOk = FMath::Abs(R.PathDistance - R.TargetDistance) <= 0.1f * R.TargetDistance;
		GEscalation.Failures += (bTargetOk && bPathOk) ? 0 : 1;
		UE_LOG(LogTheLastShift, Log, TEXT("EntityCycleTest stop %d: spawn target %.1f cm (expected %.1f) %s, path distance %.1f cm %s"),
			GEscalation.Stops, R.TargetDistance, GEscalation.ExpectedSpawn, bTargetOk ? TEXT("OK") : TEXT("FAIL"), R.PathDistance,
			bPathOk ? TEXT("within 10%") : TEXT("OFF TARGET"));
	}

	static void EscalationStep()
	{
		UWorld* World = GEscalation.World.Get();
		AEntity* Entity = GEscalation.Entity.Get();
		UScanDirectorComponent* Director = GEscalation.Director.Get();
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!TimeState || !Entity || !Director)
		{
			GEscalation.bRunning = false;
			GEscalation.Listener.Reset();
			return;
		}

		const float Initial = FMath::Clamp(Entity->InitialSpawnDistance, 100.f, 10000.f);
		const float Min = FMath::Clamp(Entity->MinSpawnDistance, 0.f, Initial);

		if (GEscalation.Phase == 1)
		{
			// Waiting for the figure to reach the standing player.
			if (TimeState->IsStopped())
			{
				if (World->GetTimeSeconds() - GEscalation.PhaseStart > 60.0)
				{
					UE_LOG(LogTheLastShift, Warning, TEXT("EntityCycleTest: no contact after 60 s, giving up."));
					++GEscalation.Failures;
					TimeState->Resume();
					GEscalation.bRunning = false;
					GEscalation.Listener.Reset();
					return;
				}
				EscalationSchedule(0.1f);
				return;
			}
			// Contact ended the Stop: check the exit and the next spawn distance after the fade-out has settled.
			GEscalation.Phase = 2;
			GEscalation.ExpectedSpawn = Min;
			const float Settle = FMath::Clamp(Director->ResumeFadeOut, 0.f, 2.f) + 0.15f;
			World->GetTimerManager().SetTimer(GEscalation.Timer, FTimerDelegate::CreateLambda([]()
			{
				UWorld* W = GEscalation.World.Get();
				if (W && GEscalation.Director.IsValid() && GEscalation.Entity.IsValid() && GEscalation.Listener)
				{
					GEscalation.Failures += CheckContactExit(W, GEscalation.Director.Get(), GEscalation.Entity.Get(), GEscalation.Listener.Get(),
						GEscalation.bFlashlightBefore, TEXT("EntityCycleTest")) ? 0 : 1;
				}
				EscalationSchedule(0.1f);
			}), Settle, false);
			return;
		}

		if (!TimeState->IsStopped())
		{
			// Start the next Stop and check where it appeared.
			++GEscalation.Stops;
			TimeState->Stop();
			EscalationCheckSpawn(Entity);
			GEscalation.PhaseStart = World->GetTimeSeconds();

			if (GEscalation.Phase == 2)
			{
				// Spawn after the rewind checked: done.
				TimeState->Resume();
				GEscalation.bRunning = false;
				UE_LOG(LogTheLastShift, Log, TEXT("EntityCycleTest summary (%s): %d stops, %d failures."),
					GEscalation.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), GEscalation.Stops, GEscalation.Failures);
				GEscalation.Listener.Reset();
				return;
			}
			if (GEscalation.Stops > GEscalation.Cycles)
			{
				GEscalation.Phase = 1; // stand still until contact
				EscalationSchedule(0.1f);
				return;
			}
			EscalationSchedule(GEscalation.StopSeconds);
			return;
		}

		// Leave the Stop at whatever distance the figure has reached.
		TimeState->Resume();
		const float Exit = Entity->GetLastExitPathDistance();
		const float Expected = FMath::Clamp(Exit - FMath::Max(Entity->ApproachPerExit, 0.f), Min, Initial);
		const float Next = Entity->GetSpawnDistanceForNextStop();
		const bool bOk = FMath::IsNearlyEqual(Next, Expected, 0.5f);
		GEscalation.Failures += bOk ? 0 : 1;
		UE_LOG(LogTheLastShift, Log, TEXT("EntityCycleTest exit %d: path distance at exit d = %.1f cm, next spawn %.1f cm, clamp(d - %.0f, %.0f, %.0f) = %.1f %s"),
			GEscalation.Stops, Exit, Next, Entity->ApproachPerExit, Min, Initial, Expected, bOk ? TEXT("OK") : TEXT("FAIL"));
		GEscalation.ExpectedSpawn = Expected;
		EscalationSchedule(FMath::Clamp(Director->ResumeFadeOut, 0.f, 2.f) + 0.3f);
	}

	// ------------------------------------------------------------------------------------------------------------
	// tls.EntityMoveTest: continuous motion at Speed while Stopped, no motion at all in Normal time.

	struct FMoveTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AEntity> Entity;
		float Seconds = 10.f;
		/** 0 = Stopped phase, 1 = Normal phase. */
		int32 Phase = 0;
		double PhaseStart = 0.0;
		TArray<FVector2D> StoppedSamples; // (time since Stop, path distance)
		FVector NormalStartLocation = FVector::ZeroVector;
		float NormalStartPath = -1.f;
		float NormalMaxMove = 0.f;
		float NormalMaxPathChange = 0.f;
		int32 NormalSamples = 0;
		int32 NormalManifestedSamples = 0;
		bool bStoppedOk = false;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FMoveTest GMoveTest;

	/** Navigation path length from a point to the player (same query as the entity's), or -1. */
	static float PathLengthToPlayer(UWorld* World, const FVector& From)
	{
		APawn* Pawn = GetPlayerPawn(World);
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Pawn || !NavSys)
		{
			return -1.f;
		}
		FVector Goal = Pawn->GetNavAgentLocation();
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Goal, Projected, FVector(100.f, 100.f, 250.f)))
		{
			Goal = Projected.Location;
		}
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, Goal, Pawn);
		return Path && Path->IsValid() && !Path->IsPartial() ? static_cast<float>(Path->GetPathLength()) : -1.f;
	}

	static void MoveTestStep()
	{
		UWorld* World = GMoveTest.World.Get();
		AEntity* Entity = GMoveTest.Entity.Get();
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!TimeState || !Entity)
		{
			GMoveTest.bRunning = false;
			return;
		}
		const double Elapsed = World->GetRealTimeSeconds() - GMoveTest.PhaseStart;
		const float Speed = FMath::Clamp(Entity->Speed, 0.f, 1000.f);

		if (GMoveTest.Phase == 0)
		{
			if (!TimeState->IsStopped())
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("EntityMoveTest: the Stop ended early (contact?) after %.3f s; FAIL."), Elapsed);
				GMoveTest.bRunning = false;
				return;
			}
			GMoveTest.StoppedSamples.Emplace(TimeState->GetCurrentStopDuration(), Entity->GetPathDistanceToPlayer());
			if (Elapsed < GMoveTest.Seconds)
			{
				World->GetTimerManager().SetTimer(GMoveTest.Timer, FTimerDelegate::CreateStatic(&MoveTestStep), 0.1f, false);
				return;
			}

			// Least-squares slope and net drop over the Stop.
			double SumT = 0, SumD = 0, SumTT = 0, SumTD = 0;
			int32 N = 0;
			for (const FVector2D& S : GMoveTest.StoppedSamples)
			{
				if (S.Y < 0.0)
				{
					continue;
				}
				SumT += S.X; SumD += S.Y; SumTT += S.X * S.X; SumTD += S.X * S.Y; ++N;
			}
			const double Slope = N > 1 ? (N * SumTD - SumT * SumD) / FMath::Max(N * SumTT - SumT * SumT, 1e-6) : 0.0;
			const FVector2D First = GMoveTest.StoppedSamples[0];
			const FVector2D Last = GMoveTest.StoppedSamples.Last();
			const double Span = Last.X - First.X;
			const double NetRate = Span > 0.0 ? (First.Y - Last.Y) / Span : 0.0;
			const bool bSlopeOk = Speed > 0.f && FMath::Abs(-Slope - Speed) <= 0.05 * Speed;
			const bool bNetOk = Speed > 0.f && FMath::Abs(NetRate - Speed) <= 0.05 * Speed;
			GMoveTest.bStoppedOk = N >= 50 && bSlopeOk && bNetOk;
			UE_LOG(LogTheLastShift, Log,
				TEXT("EntityMoveTest Stopped %s: %d samples over %.3f s, path distance %.1f -> %.1f cm; fitted speed %.2f cm/s (%+.2f%%), net %.2f cm/s (%+.2f%%), Speed %.2f cm/s, tolerance 5%%"),
				GMoveTest.bStoppedOk ? TEXT("OK") : TEXT("FAIL"), N, Span, First.Y, Last.Y, -Slope,
				Speed > 0.f ? 100.0 * (-Slope - Speed) / Speed : 0.0, NetRate, Speed > 0.f ? 100.0 * (NetRate - Speed) / Speed : 0.0, Speed);

			// Normal phase: resume in place and watch the figure where it was left.
			GMoveTest.NormalStartLocation = Entity->GetFigureLocation();
			TimeState->Resume();
			GMoveTest.NormalStartPath = PathLengthToPlayer(World, GMoveTest.NormalStartLocation);
			GMoveTest.Phase = 1;
			GMoveTest.PhaseStart = World->GetRealTimeSeconds();
			World->GetTimerManager().SetTimer(GMoveTest.Timer, FTimerDelegate::CreateStatic(&MoveTestStep), 0.1f, false);
			return;
		}

		// Normal phase.
		if (TimeState->IsStopped())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("EntityMoveTest: time was stopped during the Normal phase; FAIL."));
			GMoveTest.bRunning = false;
			return;
		}
		++GMoveTest.NormalSamples;
		GMoveTest.NormalManifestedSamples += Entity->IsManifested() ? 1 : 0;
		const FVector Location = Entity->GetFigureLocation();
		GMoveTest.NormalMaxMove = FMath::Max(GMoveTest.NormalMaxMove, static_cast<float>(FVector::Dist(Location, GMoveTest.NormalStartLocation)));
		const float PathNow = PathLengthToPlayer(World, Location);
		if (PathNow >= 0.f && GMoveTest.NormalStartPath >= 0.f)
		{
			GMoveTest.NormalMaxPathChange = FMath::Max(GMoveTest.NormalMaxPathChange, FMath::Abs(PathNow - GMoveTest.NormalStartPath));
		}
		if (Elapsed < GMoveTest.Seconds)
		{
			World->GetTimerManager().SetTimer(GMoveTest.Timer, FTimerDelegate::CreateStatic(&MoveTestStep), 0.1f, false);
			return;
		}

		const bool bNormalOk = GMoveTest.NormalSamples >= 50 && GMoveTest.NormalManifestedSamples == 0 && GMoveTest.NormalMaxMove < 0.01f
			&& GMoveTest.NormalStartPath >= 0.f && GMoveTest.NormalMaxPathChange < 0.5f;
		UE_LOG(LogTheLastShift, Log,
			TEXT("EntityMoveTest Normal %s: %d samples over %.3f s, figure at %s, max displacement %.3f cm, path distance %.1f cm, max change %.3f cm, manifested in %d samples, entity ticking %d"),
			bNormalOk ? TEXT("OK") : TEXT("FAIL"), GMoveTest.NormalSamples, Elapsed, *GMoveTest.NormalStartLocation.ToString(),
			GMoveTest.NormalMaxMove, GMoveTest.NormalStartPath, GMoveTest.NormalMaxPathChange, GMoveTest.NormalManifestedSamples,
			Entity->IsActorTickEnabled() ? 1 : 0);
		UE_LOG(LogTheLastShift, Log, TEXT("EntityMoveTest summary (%s)"), GMoveTest.bStoppedOk && bNormalOk ? TEXT("PASS") : TEXT("FAIL"));
		GMoveTest.bRunning = false;
	}

	static bool AnyTestRunning()
	{
		return GEntityTest.bRunning || GSpawnTest.bRunning || GEscalation.bRunning || GTransientTest.bRunning || GMoveTest.bRunning;
	}

	// ------------------------------------------------------------------------------------------------------------
	// tls.LidarTestSource

	static TStrongObjectPtr<UTlsLidarTestSource> GTestSource;
	static TWeakObjectPtr<UScanDirectorComponent> GTestSourceDirector;
}

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityTestCommand(
	TEXT("tls.EntityTest"),
	TEXT("tls.EntityTest [Seconds=30] [AutoScan=1]: stops time and stands still (requesting a scan whenever one is ready); logs every beat (cumulative stopped time, state, spawn, LOS from camera, visibility from scan origins, path distance, injected points), samples the path distance every 0.1 s, and on contact checks the exit for the entity's ContactResponse (Rewind / Jump / RewindAndJump: reason on both events, pose, flashlight, scans, timers, next spawn at MinSpawnDistance)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		if (!TimeState || !Director || !Entity)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EntityTest: no game world, time state, scan director or entity."));
			return;
		}
		if (AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EntityTest: an entity test is already running."));
			return;
		}
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}

		GEntityTest = FEntityTest();
		GEntityTest.World = World;
		GEntityTest.Director = Director;
		GEntityTest.Entity = Entity;
		GEntityTest.Duration = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 1.f, 600.f) : 30.f;
		GEntityTest.bAutoScan = Args.Num() > 1 ? FCString::Atoi(*Args[1]) != 0 : true;
		GEntityTest.bFlashlightBefore = IsFlashlightOn(GetPlayerPawn(World));
		GEntityTest.Listener.Reset(NewObject<UTlsEntityTestListener>());
		GEntityTest.Listener->Bind(Director, Entity);
		GEntityTest.bRunning = true;

		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest started: %.1f s, cumulative stopped %.3f s, entity %s, threshold %.1f s, next spawn %.1f cm, Speed %.2f cm/s, ContactRadius %.1f cm, ContactResponse %s (exit %s), BeatInterval %.2f s."),
			GEntityTest.Duration, TimeState->GetCumulativeStoppedSeconds(), Entity->GetState() == EEntityState::Awake ? TEXT("Awake") : TEXT("Dormant"),
			Entity->AwakenThresholdSeconds, Entity->GetSpawnDistanceForNextStop(), Entity->Speed, Entity->ContactRadius,
			LexToString(Entity->ContactResponse), LexToString(GetExitReasonForContact(Entity->ContactResponse)), Director->BeatInterval);
		TimeState->Stop();

		FTimerManager& Timers = World->GetTimerManager();
		Timers.SetTimer(GEntityTest.SampleTimer, FTimerDelegate::CreateStatic(&EntityTestSample), 0.1f, true);
		Timers.SetTimer(GEntityTest.EndTimer, FTimerDelegate::CreateStatic(&EntityTestFinish), GEntityTest.Duration, false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntitySpawnTestCommand(
	TEXT("tls.EntitySpawnTest"),
	TEXT("tls.EntitySpawnTest [Runs=20] [Distance=InitialSpawnDistance]: awakens the entity, forgets the previous spawn, and makes it appear Runs times from the current pose (restored before every Stop) at Distance; logs every pick and a summary: no line of sight, within SpawnDistanceTolerance, distinct clusters (centres > 4 m apart), consecutive spawns within SpawnRepeatRadius."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		if (!TimeState || !Entity || AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EntitySpawnTest: no game world / entity, or a test is running."));
			return;
		}
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}
		Entity->Awaken();
		Entity->ResetSpawnHistory();

		GSpawnTest = FSpawnTest();
		GSpawnTest.World = World;
		GSpawnTest.Entity = Entity;
		GSpawnTest.Runs = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 20;
		GSpawnTest.Distance = Args.Num() > 1 ? FMath::Max(FCString::Atof(*Args[1]), 0.f) : 0.f;
		if (const APawn* Pawn = GetPlayerPawn(World))
		{
			GSpawnTest.StartPose = Pawn->GetActorTransform();
			GSpawnTest.StartView = Pawn->GetController() ? Pawn->GetController()->GetControlRotation() : Pawn->GetActorRotation();
		}
		GSpawnTest.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("SpawnTest started: %d runs at %.1f cm from %s (view %s), tolerance %.2f, repeat radius %.0f cm, weights hidden %.2f forward %.2f repeat %.2f, seed %d."),
			GSpawnTest.Runs, GSpawnTest.Distance > 0.f ? GSpawnTest.Distance : Entity->InitialSpawnDistance, *GSpawnTest.StartPose.GetLocation().ToString(),
			*GSpawnTest.StartView.ToString(), Entity->SpawnDistanceTolerance, Entity->SpawnRepeatRadius, Entity->SpawnHiddenWeight,
			Entity->SpawnForwardWeight, Entity->SpawnRepeatWeight, Entity->SpawnSeed);
		SpawnTestStep();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityMoveTestCommand(
	TEXT("tls.EntityMoveTest"),
	TEXT("tls.EntityMoveTest [Seconds=10] [Distance=InitialSpawnDistance]: awakens the entity and stops time with the player standing still; samples the path distance every 0.1 s for Seconds and checks it falls at Speed +-5% (fitted and net); then resumes and checks for Seconds that the figure does not move and its path distance to the player does not change in Normal time."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		if (!TimeState || !Entity || AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EntityMoveTest: no game world / entity, or a test is running."));
			return;
		}
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}
		Entity->Awaken();
		const float Distance = Args.Num() > 1 ? FMath::Max(FCString::Atof(*Args[1]), 0.f) : Entity->InitialSpawnDistance;
		Entity->SetSpawnDistanceForNextStop(Distance);

		GMoveTest = FMoveTest();
		GMoveTest.World = World;
		GMoveTest.Entity = Entity;
		GMoveTest.Seconds = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 1.f, 60.f) : 10.f;
		GMoveTest.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("EntityMoveTest started: %.1f s Stopped then %.1f s Normal, spawn at %.1f cm, Speed %.2f cm/s, ContactRadius %.1f cm."),
			GMoveTest.Seconds, GMoveTest.Seconds, Distance, Entity->Speed, Entity->ContactRadius);
		TimeState->Stop();
		GMoveTest.PhaseStart = World->GetRealTimeSeconds();
		MoveTestStep();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityCycleTestCommand(
	TEXT("tls.EntityCycleTest"),
	TEXT("tls.EntityCycleTest [Cycles=3] [StopSeconds=5]: awakens the entity, runs Stop -> Resume cycles checking next spawn = clamp(d - ApproachPerExit, Min, Initial), then stands still until contact, checks the ContactResponse exit and that the next spawn is at MinSpawnDistance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		if (!TimeState || !Director || !Entity || AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EntityCycleTest: no game world / director / entity, or a test is running."));
			return;
		}
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}
		Entity->Awaken();
		Entity->SetSpawnDistanceForNextStop(Entity->InitialSpawnDistance);

		GEscalation = FCycleEscalation();
		GEscalation.World = World;
		GEscalation.Director = Director;
		GEscalation.Entity = Entity;
		GEscalation.Cycles = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 20) : 3;
		GEscalation.StopSeconds = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.5f, 60.f) : 5.f;
		GEscalation.ExpectedSpawn = Entity->GetSpawnDistanceForNextStop();
		GEscalation.bFlashlightBefore = IsFlashlightOn(GetPlayerPawn(World));
		GEscalation.Listener.Reset(NewObject<UTlsEntityTestListener>());
		GEscalation.Listener->bVerbose = false;
		GEscalation.Listener->Bind(Director, Entity);
		GEscalation.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("EntityCycleTest started: %d cycles of %.1f s, then contact."), GEscalation.Cycles, GEscalation.StopSeconds);
		EscalationStep();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsStopCommand(
	TEXT("tls.Stop"),
	TEXT("Stops time if it is flowing (no-op when already stopped)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; TimeState && !TimeState->IsStopped())
		{
			TimeState->Stop();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsResumeCommand(
	TEXT("tls.Resume"),
	TEXT("Resumes time if it is stopped (no-op in Normal time)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; TimeState && TimeState->IsStopped())
		{
			TimeState->Resume();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityAwakeCommand(
	TEXT("tls.Entity.Awake"),
	TEXT("Awakens the entity now (it appears at once if time is stopped)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (AEntity* Entity = World ? EntityDebug::FindEntity(World) : nullptr)
		{
			Entity->Awaken();
			TimeStateDebug::LogReport(World, TEXT("tls.Entity.Awake:"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntitySpawnCommand(
	TEXT("tls.Entity.Spawn"),
	TEXT("tls.Entity.Spawn [Distance]: sets the next spawn distance; if time is stopped, the entity appears there now."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		AEntity* Entity = World ? EntityDebug::FindEntity(World) : nullptr;
		const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!Entity || !TimeState)
		{
			return;
		}
		const float Distance = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		if (TimeState->IsStopped())
		{
			Entity->SpawnNow(Distance);
		}
		else if (Distance > 0.f)
		{
			Entity->SetSpawnDistanceForNextStop(Distance);
		}
		TimeStateDebug::LogReport(World, TEXT("tls.Entity.Spawn:"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityDrawCommand(
	TEXT("tls.Entity.Draw"),
	TEXT("tls.Entity.Draw [0/1]: debug lines for the figure origin, contact radius, path and trail reach while stopped."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (AEntity* Entity = World ? EntityDebug::FindEntity(World) : nullptr)
		{
			Entity->bDebugDraw = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Entity->bDebugDraw;
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Draw: %d"), Entity->bDebugDraw ? 1 : 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarTestSourceCommand(
	TEXT("tls.LidarTestSource"),
	TEXT("tls.LidarTestSource [On=1] [FadeMode=1] [Occlude=1] [X Y Z] [Size=100] [Hold=0.5] [Vanish=2]: registers (or with 0 removes) a 500-point cube as a lidar point source, at X Y Z or 4 m in front of the camera. FadeMode 0 = ScanFade, 1 = None, 2 = Transient."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		if (!Director)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.LidarTestSource: no scan director."));
			return;
		}

		if (GTestSource)
		{
			if (UScanDirectorComponent* Old = GTestSourceDirector.Get())
			{
				Old->UnregisterPointSource(TScriptInterface<ILidarPointSource>(GTestSource.Get()));
			}
			GTestSource.Reset();
		}

		const bool bOn = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : true;
		if (!bOn)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("tls.LidarTestSource: removed."));
			return;
		}

		UTlsLidarTestSource* Source = NewObject<UTlsLidarTestSource>();
		Source->Settings.Color = FLinearColor(0.35f, 1.f, 0.35f, 1.f);
		Source->Settings.FadeMode = static_cast<ELidarFadeMode>(FMath::Clamp(Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1, 0, 2));
		Source->Settings.HoldSeconds = Args.Num() > 7 ? FMath::Clamp(FCString::Atof(*Args[7]), 0.f, 30.f) : 0.5f;
		Source->Settings.VanishSeconds = Args.Num() > 8 ? FMath::Clamp(FCString::Atof(*Args[8]), 0.f, 30.f) : 2.f;
		Source->Settings.bOccludeByCapture = Args.Num() > 2 ? FCString::Atoi(*Args[2]) != 0 : true;
		if (Args.Num() > 5)
		{
			Source->Center = FVector(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), FCString::Atof(*Args[5]));
		}
		else if (const APawn* Pawn = GetPlayerPawn(World); Pawn && Pawn->GetController())
		{
			FVector Eye;
			FRotator Rotation;
			Pawn->GetController()->GetPlayerViewPoint(Eye, Rotation);
			Source->Center = Eye + Rotation.Vector() * 400.f;
		}
		Source->Size = Args.Num() > 6 ? FMath::Clamp(FCString::Atof(*Args[6]), 1.f, 10000.f) : 100.f;

		GTestSource.Reset(Source);
		GTestSourceDirector = Director;
		Director->RegisterPointSource(TScriptInterface<ILidarPointSource>(Source));
		UE_LOG(LogTheLastShift, Log, TEXT("tls.LidarTestSource: %d points, cube %.0f cm at %s, fade mode %s (hold %.2f s, vanish %.2f s), occlude %d."),
			Source->PointCount, Source->Size, *Source->Center.ToString(), *UEnum::GetValueAsString(Source->Settings.FadeMode),
			Source->Settings.HoldSeconds, Source->Settings.VanishSeconds, Source->Settings.bOccludeByCapture ? 1 : 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityContactResponseCommand(
	TEXT("tls.Entity.ContactResponse"),
	TEXT("tls.Entity.ContactResponse [0/1/2]: sets the entity's ContactResponse (0 = Rewind, 1 = Jump, 2 = RewindAndJump); without an argument, logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (AEntity* Entity = World ? EntityDebug::FindEntity(World) : nullptr)
		{
			if (Args.Num() > 0)
			{
				Entity->ContactResponse = static_cast<EEntityContactResponse>(FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2));
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.ContactResponse: %s (exit %s)"), LexToString(Entity->ContactResponse),
				LexToString(GetExitReasonForContact(Entity->ContactResponse)));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTransientTestCommand(
	TEXT("tls.TransientTest"),
	TEXT("tls.TransientTest [SecondScanAt=0] [Screenshots=1]: stops time, makes the entity appear at MinSpawnDistance, waits until the figure is in view, freezes it, clears the scans and fires one scan directly on the lidar (cooldown bypassed), optionally a second one at +SecondScanAt s; logs the expected entity alpha and takes a HighResShot at +0.4 s and +3.0 s, then resumes."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		if (!TimeState || !Director || !Director->GetLidarScan() || !Entity || AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.TransientTest: no game world / director / lidar / entity, or a test is running."));
			return;
		}
		if (!TimeState->IsStopped())
		{
			TimeState->Stop();
		}

		GTransientTest = FTransientTest();
		GTransientTest.World = World;
		GTransientTest.Director = Director;
		GTransientTest.Entity = Entity;
		GTransientTest.SecondScanAt = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 2.9f) : 0.f;
		GTransientTest.bScreenshots = Args.Num() > 1 ? FCString::Atoi(*Args[1]) != 0 : true;
		GTransientTest.SavedSpeed = Entity->Speed;
		GTransientTest.WaitStart = World->GetRealTimeSeconds();
		GTransientTest.bRunning = true;
		if (!Entity->IsManifested())
		{
			Entity->SpawnNow(Entity->MinSpawnDistance);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("TransientTest started: second scan at %.2f s, hold %.2f s, vanish %.2f s, figure at %s."),
			GTransientTest.SecondScanAt, Entity->PointHoldSeconds, Entity->PointVanishSeconds, *Entity->GetFigureLocation().ToString());
		TransientTestStep();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarVerifyOcclusionCommand(
	TEXT("tls.Lidar.VerifyOcclusion"),
	TEXT("tls.Lidar.VerifyOcclusion [0/1]: CPU readback count of injected points behind the captured depth, logged per scan (costs a GPU sync per scan)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		const UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		if (ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr)
		{
			Lidar->bVerifyInjectedOcclusion = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Lidar->bVerifyInjectedOcclusion;
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.VerifyOcclusion: %d"), Lidar->bVerifyInjectedOcclusion ? 1 : 0);
		}
	}));

#endif // !UE_BUILD_SHIPPING
