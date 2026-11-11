// SPDX-License-Identifier: MIT

// Debug console commands for the entity and the lidar point-source path, so threshold, spawn rule, motion, contact and
// rewind can be checked without a keyboard (agents through the editor console, or the developer in PIE).
// Development builds only.

#include "Components/SpotLightComponent.h"
#include "Containers/Ticker.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
#include "Residue/Residue.h"
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

		// Verdicts, totalled at the end as passed and failed counts.
		int32 Passed = 0;
		int32 Failed = 0;
		auto Verdict = [&Passed, &Failed](bool bOk) { (bOk ? Passed : Failed) += 1; };
		Verdict(L->AwakenTimes.Num() == 1);
		Verdict(L->Spawns.Num() >= 1);

		float SpawnTime = -1.f;
		float SpawnDistance = -1.f;
		if (L->Spawns.Num())
		{
			const FEntitySpawnReport Report = Entity->GetLastSpawnReport();
			Verdict(Report.bNoLineOfSight);
			Verdict(Report.bDistanceInTolerance);
			Verdict(!Report.bFallback);
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
			Verdict(bSlopeOk);
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
			Verdict(CheckContactExit(World, Director, Entity, L, GEntityTest.bFlashlightBefore, TEXT("EntityTest")));
		}
		else
		{
			// No contact within the run: the contact exit check did not run (regressions use 60 s so that it does).
			Verdict(false);
			LogReport(World, TEXT("EntityTest after resume:"));
		}
		UE_LOG(LogTheLastShift, Log, TEXT("  contact response %s, exit reason OnTimeStateChanged %s, OnStopExited %s"),
			LexToString(Entity->ContactResponse),
			L->TimeStateExitReasons.Num() ? ReasonName(L->TimeStateExitReasons.Last()) : TEXT("none"),
			L->StopExitReasons.Num() ? ReasonName(L->StopExitReasons.Last()) : TEXT("none"));
		UE_LOG(LogTheLastShift, Log, TEXT("EntityTest result (%s): %d checks passed, %d failed (awakened once, spawned, spawn without line of sight, on distance, without fallback, fitted speed within 5%%, contact exit)."),
			Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Passed, Failed);
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
		/** Spawns the camera could see although a candidate it could not see existed. */
		int32 SeenWithHiddenCandidate = 0;
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
		// Generated areas: the layout decides how many hidden pockets lie at the target distance, so clusters and
		// fallbacks are reported only; the one rule is never to appear in sight while a hidden candidate existed.
		// Hand-built maps (TestLidar) keep the full rule set, three distinct clusters included.
		const bool bGenerated = AAreaGenerator::Find(Entity->GetWorld()) != nullptr;
		const bool bPass = bGenerated
			? GSpawnTest.SeenWithHiddenCandidate == 0
			: GSpawnTest.NoSpawn == 0 && GSpawnTest.NoLineOfSight == Runs && GSpawnTest.InTolerance == Runs
				&& Centres.Num() >= 3 && GSpawnTest.Repeats == GSpawnTest.ExcusedRepeats;
		UE_LOG(LogTheLastShift, Log, TEXT("SpawnTest rule set: %s; spawns in sight while a hidden candidate existed: %d"),
			bGenerated ? TEXT("generated area (pass = never in sight while a hidden candidate existed; clusters and fallbacks reported only)")
				: TEXT("hand-built map (all rules, at least 3 distinct clusters)"), GSpawnTest.SeenWithHiddenCandidate);
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
			GSpawnTest.SeenWithHiddenCandidate += (bManifested && !R.bNoLineOfSight && R.NoLineOfSightCandidates > 0) ? 1 : 0;

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
	// tls.LatestScanTest: entity points (lidar fade mode UntilNextScan) stay full while their scan is the newest and
	// are gone within PointVanishSeconds of the next scan; captured and residue points of the older scan stay.

	struct FLatestScanTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UScanDirectorComponent> Director;
		TWeakObjectPtr<AEntity> Entity;
		TWeakObjectPtr<AResidue> Residue;
		/** Before scan B the figure is moved where the camera cannot see it (trail off for the whole test). */
		bool bHiddenAtSecondScan = false;
		bool bScreenshots = true;
		float SavedSpeed = 0.f;
		float SavedTrailReach = 0.f;
		FVector SavedResidueLocation = FVector::ZeroVector;
		/** Every other available residue, moved out of the way for the test so the counts hold the test residue only. */
		TArray<TPair<TWeakObjectPtr<AResidue>, FVector>> MovedResidues;
		/** Real time the test's own Stop began (0 = time was already stopped): scan A waits for the automatic first scan. */
		double StopRealTime = 0.0;
		int32 ScanA = INDEX_NONE;
		int32 ScanB = INDEX_NONE;
		double ScanARealTime = 0.0;
		double ScanBRealTime = 0.0;
		double WaitStart = 0.0;
		/** Residue points gathered for scan A and for scan B, and everything uploaded for each. */
		int32 ResidueA = 0;
		int32 ResidueB = 0;
		int32 UploadedA = 0;
		int32 UploadedB = 0;
		/** Scan A's slot counts at the first checkpoint (all of its injected points alive). */
		int32 InjectedA0 = -1;
		int32 CapturedA0 = -1;
		int32 Phase = 0;
		int32 Failures = 0;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FLatestScanTest GLatestScanTest;

	static void LatestScanTestStep();

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

	static void LatestScanSchedule(float Delay)
	{
		if (UWorld* World = GLatestScanTest.World.Get())
		{
			World->GetTimerManager().SetTimer(GLatestScanTest.Timer, FTimerDelegate::CreateStatic(&LatestScanTestStep), FMath::Max(Delay, 0.01f), false);
		}
	}

	/** Captured / injected particles of the slot holding ScanIndex (-1 / -1 when no slot holds it). */
	static void SlotCounts(const ULidarScanComponent* Lidar, int32 ScanIndex, int32& OutCaptured, int32& OutInjected)
	{
		OutCaptured = -1;
		OutInjected = -1;
		for (const FLidarScanSlotStats& S : Lidar->GetScanSlotStats())
		{
			if (S.ScanIndex == ScanIndex)
			{
				OutCaptured = S.CapturedParticles;
				OutInjected = S.InjectedParticles;
			}
		}
	}

	static void LatestScanCheck(bool bOk, const FString& What)
	{
		GLatestScanTest.Failures += bOk ? 0 : 1;
		UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	}

	static void LatestScanShot(UWorld* World, const TCHAR* Label)
	{
		if (!GLatestScanTest.bScreenshots)
		{
			return;
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest screenshot: %s"), Label);
			PC->ConsoleCommand(TEXT("HighResShot 1"));
		}
	}

	/** Checkpoint while scan A is the newest: every injected point of A still alive, nothing of A removed. */
	static void LatestScanCheckpointA(UWorld* World, const ULidarScanComponent* Lidar, const TCHAR* Label)
	{
		const float Since = static_cast<float>(World->GetRealTimeSeconds() - GLatestScanTest.ScanARealTime);
		int32 Captured = 0;
		int32 Injected = 0;
		SlotCounts(Lidar, GLatestScanTest.ScanA, Captured, Injected);
		if (GLatestScanTest.InjectedA0 < 0)
		{
			GLatestScanTest.InjectedA0 = Injected;
			GLatestScanTest.CapturedA0 = Captured;
		}
		const int32 EntityA = Injected - GLatestScanTest.ResidueA;
		LatestScanCheck(Injected == GLatestScanTest.InjectedA0 && Captured == GLatestScanTest.CapturedA0 && EntityA > 0,
			FString::Printf(TEXT("%s +%.2f s, scan %d newest: captured %d, injected %d (residue %d + entity %d, all alive), alive scans %d"),
				Label, Since, GLatestScanTest.ScanA, Captured, Injected, GLatestScanTest.ResidueA, EntityA, Lidar->GetAliveScanCount()));
		LatestScanShot(World, Label);
	}

	static void LatestScanTestEnd(const TCHAR* Why)
	{
		UWorld* World = GLatestScanTest.World.Get();
		if (World)
		{
			World->GetTimerManager().ClearTimer(GLatestScanTest.Timer);
		}
		if (AEntity* Entity = GLatestScanTest.Entity.Get())
		{
			Entity->Speed = GLatestScanTest.SavedSpeed;
			Entity->TrailReach = GLatestScanTest.SavedTrailReach;
		}
		if (AResidue* Residue = GLatestScanTest.Residue.Get())
		{
			Residue->SetActorLocation(GLatestScanTest.SavedResidueLocation);
		}
		for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : GLatestScanTest.MovedResidues)
		{
			if (AResidue* Other = Moved.Key.Get())
			{
				Other->SetActorLocation(Moved.Value);
			}
		}
		GLatestScanTest.MovedResidues.Reset();
		if (UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; TimeState && TimeState->IsStopped())
		{
			TimeState->Resume();
		}
		GLatestScanTest.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest done: %s, %d failure(s)."), Why, GLatestScanTest.Failures);
	}

	static void LatestScanTestStep()
	{
		UWorld* World = GLatestScanTest.World.Get();
		AEntity* Entity = GLatestScanTest.Entity.Get();
		UScanDirectorComponent* Director = GLatestScanTest.Director.Get();
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		AResidue* Residue = GLatestScanTest.Residue.Get();
		if (!World || !Entity || !Lidar || !TimeState || !TimeState->IsStopped())
		{
			LatestScanTestEnd(TEXT("aborted (world, entity or lidar gone, or time resumed)"));
			return;
		}

		const double Now = World->GetRealTimeSeconds();
		const float VanishSeconds = FMath::Clamp(Entity->PointVanishSeconds, 0.f, 30.f);
		switch (GLatestScanTest.Phase)
		{
		case 0:
			// Wait until the figure is in view, then freeze it so the scans show it in one place. A Stop this test began
			// first lets the director's automatic first scan go by (about 1 s), so that scan cannot supersede scan A.
			if (GLatestScanTest.StopRealTime > 0.0 && Now - GLatestScanTest.StopRealTime < 1.6)
			{
				LatestScanSchedule(0.05f);
				return;
			}
			if (Entity->IsManifested() && IsVisibleFromCamera(World, Entity->GetFigureLocation()) && IsInView(World, Entity->GetFigureLocation()))
			{
				Entity->Speed = 0.f;
				if (Residue)
				{
					// A residue in plain view, 3 m ahead and to the side, so its points (ScanFade) are on screen too.
					const APlayerController* PC = World->GetFirstPlayerController();
					FVector Eye;
					FRotator View;
					PC->GetPlayerViewPoint(Eye, View);
					const FVector ToFigure = (Entity->GetFigureLocation() - Eye).GetSafeNormal2D();
					const FVector Side = FVector(-ToFigure.Y, ToFigure.X, 0.f);
					Residue->SetActorLocation(Eye + View.Vector() * 300.f + Side * 120.f - FVector(0.f, 0.f, 30.f));
				}
				// Bypass the director's cooldown: clear every scan and fire the plugin directly.
				Lidar->ClearScans(0.f);
				GLatestScanTest.ScanA = Lidar->GetScanCount();
				Lidar->Scan();
				GLatestScanTest.ScanARealTime = World->GetRealTimeSeconds();
				GLatestScanTest.ResidueA = Residue ? Residue->GetLastGatheredPoints() : 0;
				GLatestScanTest.UploadedA = Lidar->GetLastInjectionStats().Uploaded;
				UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest: figure visible at %s, path distance %.1f cm, frozen; scan A = %d fired (cooldown bypassed): %d injected points uploaded, %d of them residue (figure %d, trail %d gathered)."),
					*Entity->GetFigureLocation().ToString(), Entity->GetPathDistanceToPlayer(), GLatestScanTest.ScanA,
					GLatestScanTest.UploadedA, GLatestScanTest.ResidueA, Entity->GetLastGatheredFigurePoints(), Entity->GetLastGatheredTrailPoints());
				GLatestScanTest.Phase = 1;
				LatestScanSchedule(0.4f);
				return;
			}
			if (Entity->IsManifested() && IsVisibleFromCamera(World, Entity->GetFigureLocation()))
			{
				// In line of sight but outside the view (often behind the camera): face it before it reaches the player.
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					FVector Eye;
					FRotator View;
					PC->GetPlayerViewPoint(Eye, View);
					PC->SetControlRotation((Entity->GetFigureLocation() + FVector(0.f, 0.f, 100.f) - Eye).Rotation());
				}
			}
			if (Now - GLatestScanTest.WaitStart > 30.0)
			{
				LatestScanTestEnd(TEXT("figure never came into view within 30 s"));
				return;
			}
			LatestScanSchedule(0.05f);
			return;
		case 1:
			LatestScanCheckpointA(World, Lidar, TEXT("checkpoint +0.4 s"));
			GLatestScanTest.Phase = 2;
			LatestScanSchedule(3.0f - static_cast<float>(Now - GLatestScanTest.ScanARealTime));
			return;
		case 2:
			LatestScanCheckpointA(World, Lidar, TEXT("checkpoint +3.0 s"));
			GLatestScanTest.Phase = 3;
			LatestScanSchedule(4.4f - static_cast<float>(Now - GLatestScanTest.ScanARealTime));
			return;
		case 3:
			LatestScanCheckpointA(World, Lidar, TEXT("checkpoint +4.4 s"));
			if (GLatestScanTest.bHiddenAtSecondScan)
			{
				// A new appearance where the camera cannot see it: scan B must not show the figure.
				Entity->SpawnNow(Entity->MinSpawnDistance);
				Entity->Speed = 0.f;
				UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest: figure moved out of sight to %s (visible from camera %d)."),
					*Entity->GetFigureLocation().ToString(), IsVisibleFromCamera(World, Entity->GetFigureLocation()) ? 1 : 0);
			}
			GLatestScanTest.Phase = 4;
			LatestScanSchedule(4.5f - static_cast<float>(Now - GLatestScanTest.ScanARealTime));
			return;
		case 4:
			GLatestScanTest.ScanB = Lidar->GetScanCount();
			Lidar->Scan();
			GLatestScanTest.ScanBRealTime = World->GetRealTimeSeconds();
			GLatestScanTest.ResidueB = Residue ? Residue->GetLastGatheredPoints() : 0;
			GLatestScanTest.UploadedB = Lidar->GetLastInjectionStats().Uploaded;
			UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest: scan B = %d fired at +%.3f s after A (cooldown bypassed): %d injected points uploaded, %d of them residue; figure visible from camera %d."),
				GLatestScanTest.ScanB, GLatestScanTest.ScanBRealTime - GLatestScanTest.ScanARealTime, GLatestScanTest.UploadedB,
				GLatestScanTest.ResidueB, IsVisibleFromCamera(World, Entity->GetFigureLocation()) ? 1 : 0);
			GLatestScanTest.Phase = 5;
			LatestScanSchedule(0.1f);
			return;
		case 5:
		case 6:
		case 7:
		{
			// Samples after B: at +0.1 s and +0.2 s (inside the vanish) only logged, at +VanishSeconds + 0.1 s checked.
			const float SinceB = static_cast<float>(Now - GLatestScanTest.ScanBRealTime);
			int32 CapturedA = 0, InjectedA = 0, CapturedB = 0, InjectedB = 0;
			SlotCounts(Lidar, GLatestScanTest.ScanA, CapturedA, InjectedA);
			SlotCounts(Lidar, GLatestScanTest.ScanB, CapturedB, InjectedB);
			const FString Line = FString::Printf(TEXT("+%.2f s after B: scan A captured %d (was %d), injected %d (residue %d, was %d); scan B captured %d, injected %d (residue %d, entity %d)"),
				SinceB, CapturedA, GLatestScanTest.CapturedA0, InjectedA, GLatestScanTest.ResidueA, GLatestScanTest.InjectedA0,
				CapturedB, InjectedB, GLatestScanTest.ResidueB, InjectedB - GLatestScanTest.ResidueB);
			if (GLatestScanTest.Phase < 7)
			{
				UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest sample %s"), *Line);
				GLatestScanTest.Phase += 1;
				LatestScanSchedule(GLatestScanTest.Phase == 6 ? 0.1f : VanishSeconds + 0.1f - SinceB);
				return;
			}
			LatestScanCheck(InjectedA == GLatestScanTest.ResidueA,
				FString::Printf(TEXT("scan A's entity points gone within VanishSeconds %.2f, its residue points kept: %s"), VanishSeconds, *Line));
			LatestScanCheck(CapturedA == GLatestScanTest.CapturedA0,
				FString::Printf(TEXT("scan A's captured points unchanged (%d -> %d)"), GLatestScanTest.CapturedA0, CapturedA));
			const int32 EntityB = InjectedB - GLatestScanTest.ResidueB;
			if (GLatestScanTest.bHiddenAtSecondScan)
			{
				LatestScanCheck(EntityB == 0, FString::Printf(TEXT("scan B cannot see the figure: no entity points on screen (%d)"), EntityB));
			}
			else
			{
				LatestScanCheck(EntityB > 0, FString::Printf(TEXT("scan B sees the figure: exactly one snapshot on screen, scan B's %d entity points (scan A's %d gone)"),
					EntityB, GLatestScanTest.InjectedA0 - GLatestScanTest.ResidueA));
			}
			LatestScanCheck(!Residue || (GLatestScanTest.ResidueA > 0 && ILidarPointSource::Execute_GetLidarSourceSettings(Residue).FadeMode == ELidarFadeMode::ScanFade),
				FString::Printf(TEXT("residue %s in both scans (%d / %d points), fade mode ScanFade"), Residue ? *Residue->GetName() : TEXT("-"),
					GLatestScanTest.ResidueA, GLatestScanTest.ResidueB));
			LatestScanShot(World, TEXT("after B"));
			GLatestScanTest.Phase = 8;
			LatestScanSchedule(1.5f - SinceB);
			return;
		}
		case 8:
		{
			// Report only: superseded entity particles are invisible (alpha 0); if the count still holds them, they die
			// when the slot is reused or cleared.
			int32 CapturedA = 0, InjectedA = 0;
			SlotCounts(Lidar, GLatestScanTest.ScanA, CapturedA, InjectedA);
			UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest +%.2f s after B: scan A injected %d (residue %d, so %d entity particles still counted), captured %d"),
				Now - GLatestScanTest.ScanBRealTime, InjectedA, GLatestScanTest.ResidueA, InjectedA - GLatestScanTest.ResidueA, CapturedA);
			GLatestScanTest.Phase = 9;
			LatestScanSchedule(0.5f);
			return;
		}
		default:
			LatestScanTestEnd(TEXT("complete"));
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

	/**
	 * A test runs on its world's timers, which die with the world: if PIE stopped mid-test, nothing will ever clear
	 * bRunning. A test whose world is gone or tearing down is therefore not running, and is marked so here.
	 */
	static bool IsLive(bool& bRunning, const TWeakObjectPtr<UWorld>& World)
	{
		if (bRunning)
		{
			const UWorld* TestWorld = World.Get();
			if (!TestWorld || TestWorld->bIsTearingDown)
			{
				bRunning = false;
			}
		}
		return bRunning;
	}

	static bool AnyTestRunning()
	{
		// Every test is checked (no short-circuit), so stale flags are all cleared at once.
		bool bAny = IsLive(GEntityTest.bRunning, GEntityTest.World);
		bAny |= IsLive(GSpawnTest.bRunning, GSpawnTest.World);
		bAny |= IsLive(GEscalation.bRunning, GEscalation.World);
		bAny |= IsLive(GLatestScanTest.bRunning, GLatestScanTest.World);
		bAny |= IsLive(GMoveTest.bRunning, GMoveTest.World);
		return bAny;
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
	TEXT("tls.LidarTestSource [On=1] [FadeMode=1] [Occlude=1] [X Y Z] [Size=100] [Hold=0.5] [Vanish=2]: registers (or with 0 removes) a 500-point cube as a lidar point source, at X Y Z or 4 m in front of the camera. FadeMode 0 = ScanFade, 1 = None, 2 = Transient, 3 = UntilNextScan."),
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
		Source->Settings.FadeMode = static_cast<ELidarFadeMode>(FMath::Clamp(Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1, 0, 3));
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

static FAutoConsoleCommandWithWorldAndArgs GTlsLatestScanTestCommand(
	TEXT("tls.LatestScanTest"),
	TEXT("tls.LatestScanTest [Hidden=0] [Screenshots=1]: stops time, makes the entity appear at MinSpawnDistance, waits until the figure is in view, freezes it, moves the nearest available residue 3 m in front of the camera and every other one out of the way, waits for the automatic first scan, clears the scans and fires scan A directly on the lidar (cooldown bypassed). Checks at +0.4, +3.0 and +4.4 s that every injected point of A is alive; fires scan B at +4.5 s (with Hidden=1 the figure first reappears out of sight and the trail is off); checks at +PointVanishSeconds + 0.1 s that A's entity points are gone and its captured and residue points kept, and that B holds entity points (Hidden=0) or none (Hidden=1); logs A's injected count again at +1.5 s after B. HighResShot at every checkpoint, then resumes and restores the residue."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace EntityDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		AEntity* Entity = World ? FindEntity(World) : nullptr;
		APawn* Pawn = World ? GetPlayerPawn(World) : nullptr;
		if (!TimeState || !Director || !Director->GetLidarScan() || !Entity || !Pawn || AnyTestRunning())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.LatestScanTest: no game world / director / lidar / entity / pawn, or a test is running."));
			return;
		}
		const bool bOwnStop = !TimeState->IsStopped();
		if (bOwnStop)
		{
			TimeState->Stop();
		}

		GLatestScanTest = FLatestScanTest();
		GLatestScanTest.StopRealTime = bOwnStop ? World->GetRealTimeSeconds() : 0.0;
		GLatestScanTest.World = World;
		GLatestScanTest.Director = Director;
		GLatestScanTest.Entity = Entity;
		GLatestScanTest.bHiddenAtSecondScan = Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0;
		GLatestScanTest.bScreenshots = Args.Num() > 1 ? FCString::Atoi(*Args[1]) != 0 : true;
		GLatestScanTest.SavedSpeed = Entity->Speed;
		GLatestScanTest.SavedTrailReach = Entity->TrailReach;
		if (GLatestScanTest.bHiddenAtSecondScan)
		{
			Entity->TrailReach = 0.f;
		}
		float Nearest = TNumericLimits<float>::Max();
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation());
			if (It->GetState() == EResidueState::Available && Distance < Nearest)
			{
				Nearest = Distance;
				GLatestScanTest.Residue = *It;
				GLatestScanTest.SavedResidueLocation = It->GetActorLocation();
			}
		}
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			if (*It != GLatestScanTest.Residue.Get() && It->GetState() == EResidueState::Available)
			{
				GLatestScanTest.MovedResidues.Emplace(*It, It->GetActorLocation());
				It->SetActorLocation(It->GetActorLocation() - FVector(0.f, 0.f, 100000.f));
			}
		}
		GLatestScanTest.WaitStart = World->GetRealTimeSeconds();
		GLatestScanTest.bRunning = true;
		if (!Entity->IsManifested())
		{
			Entity->SpawnNow(Entity->MinSpawnDistance);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("LatestScanTest started: hidden at scan B %d, vanish %.2f s, figure at %s, residue %s."),
			GLatestScanTest.bHiddenAtSecondScan ? 1 : 0, Entity->PointVanishSeconds, *Entity->GetFigureLocation().ToString(),
			GLatestScanTest.Residue.IsValid() ? *GLatestScanTest.Residue->GetName() : TEXT("none"));
		LatestScanTestStep();
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

namespace RenderSetupDebug
{
	// tls.RenderSetup: a fixed scene for scene-render timing (ProfileGPU by hand while it holds).
	struct FState
	{
		bool bActive = false;
		int32 Phase = 0;
		int32 Scans = 3;
		int32 Fired = 0;
		int32 SpawnTries = 0;
		double PhaseReal = 0.0;
		float SavedSpeed = -1.f;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AEntity> Entity;
	};
	static FState G;

	static void End(const TCHAR* Why)
	{
		if (AEntity* Entity = G.Entity.Get(); Entity && G.SavedSpeed >= 0.f)
		{
			Entity->Speed = G.SavedSpeed;
		}
		G.SavedSpeed = -1.f;
		G.bActive = false;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.RenderSetup: ended (%s)."), Why);
	}

	static void Face(UWorld* World, const FVector& FigureBase)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Eye;
			FRotator View;
			PC->GetPlayerViewPoint(Eye, View);
			PC->SetControlRotation((FigureBase + FVector(0.f, 0.f, 100.f) - Eye).Rotation());
		}
	}

	static bool Tick(float)
	{
		if (!G.bActive)
		{
			return false;
		}
		UWorld* World = G.World.Get();
		AEntity* Entity = G.Entity.Get();
		UTimeStateSubsystem* Time = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		const double Now = FPlatformTime::Seconds();
		if (!World || !Entity || !Time || !Lidar || !Time->IsStopped())
		{
			End(TEXT("world, entity or lidar gone, or time resumed"));
			return false;
		}
		switch (G.Phase)
		{
		case 0:
			// Let the director's automatic first scan go by, then wake the entity (it appears at once while Stopped).
			if (Now - G.PhaseReal < 1.6)
			{
				return true;
			}
			Entity->Awaken();
			G.Phase = 1;
			return true;
		case 1:
			if (!Entity->IsManifested() || !EntityDebug::IsVisibleFromCamera(World, Entity->GetFigureLocation()))
			{
				if (++G.SpawnTries > 30)
				{
					End(TEXT("FAIL: no spawn in line of sight in 30 tries"));
					return false;
				}
				Entity->SpawnNow(1000.f + (G.SpawnTries % 5) * 500.f);
				return true;
			}
			G.SavedSpeed = Entity->Speed;
			Entity->Speed = 0.f;
			Face(World, Entity->GetFigureLocation());
			Lidar->ClearScans(0.f);
			G.Phase = 2;
			G.PhaseReal = Now;
			return true;
		case 2:
			if (Now - G.PhaseReal < 0.3)
			{
				return true;
			}
			Lidar->Scan();
			G.PhaseReal = Now;
			G.Phase = ++G.Fired >= G.Scans ? 3 : 2;
			return true;
		case 3:
		{
			if (Now - G.PhaseReal < 1.0)
			{
				return true;
			}
			const APlayerController* PC = World->GetFirstPlayerController();
			FVector Eye;
			FRotator View;
			PC->GetPlayerViewPoint(Eye, View);
			const AAreaGenerator* Gen = AAreaGenerator::Find(World);
			UE_LOG(LogTheLastShift, Log, TEXT("tls.RenderSetup: READY in %s: %d alive scans, entity frozen at %.1f m from the eye (in line of sight %d, in view %d, %d figure + %d trail points in the last scan), PCG instances %d; holding until tls.Resume."),
				*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Lidar->GetAliveScanCount(), FVector::Dist(Eye, Entity->GetFigureLocation()) / 100.f,
				EntityDebug::IsVisibleFromCamera(World, Entity->GetFigureLocation()) ? 1 : 0, EntityDebug::IsInView(World, Entity->GetFigureLocation()) ? 1 : 0,
				Entity->GetLastGatheredFigurePoints(), Entity->GetLastGatheredTrailPoints(), Gen ? Gen->GetPCGInstanceCount() : 0);
			G.Phase = 4;
			return true;
		}
		default:
			return true;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsRenderSetupCommand(
	TEXT("tls.RenderSetup"),
	TEXT("tls.RenderSetup [Scans=3]: (PIE, Normal time) a fixed scene for scene-render timing: in a generated area the pawn goes to the middle of the largest open segment (elsewhere it stays), time stops, the entity wakes and is respawned until it is in line of sight, frozen and faced; the scans are cleared and Scans scans fired. Logs READY and holds; take ProfileGPU samples, then tls.Resume (restores the entity's speed)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		AEntity* Entity = World ? EntityDebug::FindEntity(World) : nullptr;
		UTimeStateSubsystem* Time = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		if (!Entity || !Time || !Character || Time->IsStopped() || RenderSetupDebug::G.bActive)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.RenderSetup: needs PIE in Normal time with an entity and a player character, and no setup running."));
			return;
		}
		FString Where = TEXT("where the pawn stands");
		if (const AAreaInfo* Info = AAreaInfo::Find(World))
		{
			int32 Best = -1;
			double BestArea = 0.0;
			for (int32 Index = 0; Index < Info->Segments.Num(); ++Index)
			{
				const FAreaSegmentRecord& S = Info->Segments[Index];
				const FVector Size = S.Bounds.GetSize();
				if (!S.bClosed && Size.X * Size.Y > BestArea)
				{
					BestArea = Size.X * Size.Y;
					Best = Index;
				}
			}
			if (Best >= 0)
			{
				const FAreaSegmentRecord& S = Info->Segments[Best];
				const float Distance = 0.5f * (S.RouteStart + S.RouteEnd);
				int32 Segment = -1;
				const FVector P = Info->GetRoutePointAtDistance(Distance, Segment);
				const FVector Dir = Info->GetRouteDirectionAtDistance(Distance);
				Character->SetActorLocation(P + FVector(0.f, 0.f, Character->GetDefaultHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
				Character->GetCharacterMovement()->StopMovementImmediately();
				PC->SetControlRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
				Where = FString::Printf(TEXT("segment %d %s (%.0f x %.0f m), %.1f m along the route"), Best, LexToString(S.Type), S.Bounds.GetSize().X / 100.f,
					S.Bounds.GetSize().Y / 100.f, Distance / 100.f);
			}
		}
		RenderSetupDebug::G = RenderSetupDebug::FState();
		RenderSetupDebug::G.bActive = true;
		RenderSetupDebug::G.Scans = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 16) : 3;
		RenderSetupDebug::G.World = World;
		RenderSetupDebug::G.Entity = Entity;
		RenderSetupDebug::G.PhaseReal = FPlatformTime::Seconds();
		Time->Stop();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.RenderSetup: %s; stopped, %d scans to fire."), *Where, RenderSetupDebug::G.Scans);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RenderSetupDebug::Tick));
	}));

#endif // !UE_BUILD_SHIPPING
