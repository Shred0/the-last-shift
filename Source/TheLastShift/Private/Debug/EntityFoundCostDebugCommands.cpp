// SPDX-License-Identifier: MIT

// Brief 20, the cost of the game's check of the entity's points in the frame of a scan. tls.EntityFound.Cost [Quit]
// [Rounds] runs in an area map (Area1_Hull in the reports), in PIE or a package (uncapped there). It uses only calls that
// the build of Brief 19 has too, so that the same command measures a build before Brief 20 and one after it.
//
// Control: Rounds Stops with the entity dormant, the player 15 m into the area's first corridor looking back at its
// start (the pose of tls.Links.CostRun corridor); in each, the automatic scan and two manual scans.
// Awake, not found: Rounds Stops with the entity awake (GrazeRadius 0, Speed 0, the found state cleared before each Stop),
// each from the same corridor pose with the engine's random stream and the spawn history reset to the round's seed, so
// that both builds place the figure at the same point; at the Stop the player is stood 4 to 9 m from the figure where it
// is in sight, looking at it, before the automatic scan. For every scan: the frame time of the scan (the larger of the two
// frame deltas around it), the largest GPU frame time of the five frames after it, the game thread time, and the
// entity's check of that scan (traces, milliseconds, its result), with the figure's points gathered. Logs medians per
// scan of the Stop and "===== done". Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Debug/EntityFormTestScope.h"
#include "Debug/FrameCapsTestScope.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LidarScanComponent.h"
#include "Misc/App.h"
#include "NavigationSystem.h"
#include "Run/RunSubsystem.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"

#if !UE_BUILD_SHIPPING

#include "DynamicRHI.h"
#include "RenderTimer.h"

namespace EntityFoundCostDebug
{
	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static double Median(TArray<double> V)
	{
		if (V.IsEmpty())
		{
			return 0.0;
		}
		V.Sort();
		return V[V.Num() / 2];
	}

	static double Max(const TArray<double>& V)
	{
		double M = 0.0;
		for (const double X : V)
		{
			M = FMath::Max(M, X);
		}
		return M;
	}

	static constexpr int32 ScansPerStop = 3;

	enum class EPhase : uint8 { WaitWorld, Pose, Stop, Scans, Done };

	struct FScanRecord
	{
		double FrameMs = 0.0;
		double GpuMs = 0.0;
		double GameMs = 0.0;
		int32 Traces = -1;
		double CheckMs = -1.0;
		bool bKept = false;
		int32 FigurePoints = 0;
	};

	struct FCost
	{
		EPhase Phase = EPhase::WaitWorld;
		bool bQuit = false;
		bool bAwake = false;
		int32 Rounds = 10;
		int32 Round = 0;
		double PhaseStart = 0.0;
		FVector PoseLocation = FVector::ZeroVector;
		bool bHavePose = false;
		float SavedSpeed = 0.f;
		float SavedGraze = 0.f;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		// The scan being measured.
		int32 ScanInStop = 0;
		int32 LastScanCount = 0;
		int32 FramesSinceScan = -1;
		double PrevDelta = 0.0;
		double NextScanAt = 0.0;
		FScanRecord Current;
		TArray<double> GpuWindow;
		TArray<FScanRecord> Records[2][ScansPerStop];
		FString Results;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FCost G;

	static void Exec(const FString& Command)
	{
		if (APlayerController* C = PC())
		{
			C->ConsoleCommand(Command);
		}
	}

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	/** The first corridor's pose of tls.Links.CostRun corridor: 15 m in, looking back at its start. */
	static bool CorridorPose()
	{
		const AAreaInfo* Info = AAreaInfo::Find(W());
		for (int32 I = 0; Info && I < Info->Segments.Num(); ++I)
		{
			if (Info->Segments[I].Type != EAreaSegmentType::Corridor)
			{
				continue;
			}
			int32 Unused = 0;
			const FVector From = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 1500.f, Unused);
			const FVector To = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 150.f, Unused);
			Exec(FString::Printf(TEXT("tls.StandAt %.0f %.0f %.0f %.0f"), From.X, From.Y, To.X, To.Y));
			return true;
		}
		return false;
	}

	/** A navmesh point 400..900 cm from the figure from which an eye at the pawn's height sees it (the spawn's own test). */
	static bool StandWhereSeen(const AEntity* Entity)
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Entity || !Nav || !Character)
		{
			return false;
		}
		const FVector Figure = Entity->GetFigureLocation();
		const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float EyeAbove = Half + 2.f + (EyeOf(Character).Z - Character->GetActorLocation().Z);
		for (float Radius = 400.f; Radius <= 900.f; Radius += 50.f)
		{
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Angle = Step * (2.f * UE_PI / 24.f);
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, OnNav, FVector(40.f, 40.f, 300.f)))
				{
					continue;
				}
				if (FVector::Dist2D(OnNav.Location, Figure) < 399.f || FMath::Abs(OnNav.Location.Z - Figure.Z) > 150.f
					|| !Entity->CanBeSeenFrom(OnNav.Location + FVector(0.f, 0.f, EyeAbove), Figure))
				{
					continue;
				}
				Character->SetActorLocation(OnNav.Location + FVector(0.f, 0.f, Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
				Character->GetCharacterMovement()->StopMovementImmediately();
				if (APlayerController* C = PC())
				{
					C->SetControlRotation((Figure + FVector(0.f, 0.f, 90.f) - EyeOf(Character)).Rotation());
				}
				return true;
			}
		}
		return false;
	}

	static void Begin(EPhase Phase, double Now)
	{
		G.Phase = Phase;
		G.PhaseStart = Now;
	}

	static void Summarise(int32 Kind)
	{
		for (int32 Scan = 0; Scan < ScansPerStop; ++Scan)
		{
			TArray<double> Frame, Gpu, Game, Check, Traces;
			int32 Kept = 0;
			int32 Checked = 0;
			int32 Points = 0;
			for (const FScanRecord& R : G.Records[Kind][Scan])
			{
				Frame.Add(R.FrameMs);
				Gpu.Add(R.GpuMs);
				Game.Add(R.GameMs);
				if (R.Traces >= 0)
				{
					Check.Add(R.CheckMs);
					Traces.Add(R.Traces);
					++Checked;
					Kept += R.bKept ? 1 : 0;
				}
				Points = FMath::Max(Points, R.FigurePoints);
			}
			const FString Line = FString::Printf(TEXT("%s, scan %d of the Stop (%d Stops): frame %.3f ms (max %.3f), GPU %.3f ms (max %.3f), game thread %.3f ms; check ran in %d (kept %d): %.0f traces, %.3f ms (medians; max %.3f ms); figure points gathered %d"),
				Kind == 0 ? TEXT("dormant (control)") : TEXT("awake, not found"), Scan + 1, G.Records[Kind][Scan].Num(), Median(Frame), Max(Frame), Median(Gpu), Max(Gpu),
				Median(Game), Checked, Kept, Median(Traces), Median(Check), Max(Check), Points);
			UE_LOG(LogTheLastShift, Display, TEXT("EntityFoundCost: %s"), *Line);
			G.Results += TEXT("\n  ") + Line;
		}
	}

	static void Finish()
	{
		EntityFormTest::Restore(TEXT("tls.EntityFound.Cost"));
		FrameCapsTest::Restore(W(), TEXT("tls.EntityFound.Cost"));
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume();
		}
		if (AEntity* E = FindEntity())
		{
			E->Speed = G.SavedSpeed;
			E->GrazeRadius = G.SavedGraze;
		}
		if (UHomeStateSubsystem* H = UHomeStateSubsystem::Get(W()))
		{
			H->RestoreState(G.SavedBattery, G.SavedHome);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("EntityFoundCost: ===== done%s"), *G.Results);
		G.Phase = EPhase::Done;
		if (G.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.EntityFound.Cost"));
		}
	}

	static bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		const double DeltaMs = FApp::GetDeltaTime() * 1000.0;
		ULidarScanComponent* L = Lidar();
		UScanDirectorComponent* D = Director();
		AEntity* E = FindEntity();
		UTimeStateSubsystem* T = TS();
		if (!G.bAwake && T)
		{
			T->ResetCumulativeStoppedSeconds();
		}
		switch (G.Phase)
		{
		case EPhase::WaitWorld:
			if (L && D && E && T && Pawn() && Now - G.PhaseStart > 4.0)
			{
				// No cap for the run: the player's frame-rate limit and V-Sync are lifted, and put back at the end.
				FrameCapsTest::Lift(W(), TEXT("tls.EntityFound.Cost"));
				Exec(TEXT("r.VSyncEditor 0"));
				G.SavedSpeed = E->Speed;
				G.SavedGraze = E->GrazeRadius;
				if (const UHomeStateSubsystem* H = UHomeStateSubsystem::Get(W()))
				{
					G.SavedBattery = H->GetBatteryFraction();
					G.SavedHome = H->GetHomeSecondsElapsed();
				}
				UE_LOG(LogTheLastShift, Display, TEXT("EntityFoundCost: %s, entity %s %s, %d rounds per phase."), *W()->GetOutermost()->GetName(), *E->GetName(),
					E->GetState() == EEntityState::Dormant ? TEXT("dormant") : TEXT("AWAKE (the control needs it dormant: start on a fresh map)"), G.Rounds);
				Begin(EPhase::Pose, Now);
			}
			else if (Now - G.PhaseStart > 90.0)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("EntityFoundCost: no world with the player's lidar and an entity."));
				Finish();
			}
			return G.Phase != EPhase::Done;

		case EPhase::Pose:
			if (T->IsStopped())
			{
				T->Resume();
			}
			if (URunSubsystem* Run = URunSubsystem::Get(W()))
			{
				Run->SetEntityFound(false, TEXT("tls.EntityFound.Cost"));
			}
			if (UHomeStateSubsystem* H = UHomeStateSubsystem::Get(W()))
			{
				H->RestoreState(G.SavedBattery, G.SavedHome);
			}
			if (!CorridorPose())
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("EntityFoundCost: no corridor in this map."));
				Finish();
				return false;
			}
			Begin(EPhase::Stop, Now);
			return true;

		case EPhase::Stop:
			if (Now - G.PhaseStart < 0.6)
			{
				return true;
			}
			if (G.bAwake)
			{
				E->Speed = 0.f;
				E->GrazeRadius = 0.f;
				FMath::RandInit(2000 + G.Round);
				E->ResetSpawnHistory();
				E->SetSpawnDistanceForNextStop(800.f);
			}
			T->Stop();
			if (G.bAwake)
			{
				const bool bSeen = E->IsManifested() && StandWhereSeen(E);
				UE_LOG(LogTheLastShift, Display, TEXT("EntityFoundCost: round %d awake: figure at %s, %s; player at %s."), G.Round, *E->GetFigureLocation().ToString(),
					bSeen ? TEXT("stood where it is in sight") : TEXT("NO POSE THAT SEES IT"), *Pawn()->GetActorLocation().ToString());
			}
			G.ScanInStop = 0;
			G.LastScanCount = L->GetScanCount();
			G.FramesSinceScan = -1;
			G.NextScanAt = 0.0;
			Begin(EPhase::Scans, Now);
			return true;

		case EPhase::Scans:
		{
			const int32 Kind = G.bAwake ? 1 : 0;
			// The lidar's own count: the director counts only the scans it fires itself, not Scan() called on the lidar.
			const int32 Count = L->GetScanCount();
			if (G.FramesSinceScan < 0 && Count > G.LastScanCount)
			{
				// The scan ran in the frame this tick follows (or the one before it): measure both deltas.
				G.LastScanCount = Count;
				G.FramesSinceScan = 0;
				G.Current = FScanRecord();
				G.Current.FrameMs = FMath::Max(G.PrevDelta, DeltaMs);
				G.Current.GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
				G.GpuWindow.Reset();
				const int32 ScanIndex = L->GetLastInjectionStats().ScanIndex;
				if (E->GetLastKeptCheckScan() == ScanIndex && ScanIndex != INDEX_NONE)
				{
					G.Current.Traces = E->GetLastKeptCheckTraces();
					G.Current.CheckMs = E->GetLastKeptCheckMs();
					G.Current.bKept = E->GetLastKeptCheckResult();
				}
				G.Current.FigurePoints = E->IsManifested() ? E->GetLastGatheredFigurePoints() : 0;
			}
			else if (G.FramesSinceScan >= 0)
			{
				if (G.FramesSinceScan == 0)
				{
					G.Current.FrameMs = FMath::Max(G.Current.FrameMs, DeltaMs);
				}
				G.GpuWindow.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
				if (++G.FramesSinceScan >= 5)
				{
					G.Current.GpuMs = Max(G.GpuWindow);
					if (G.ScanInStop < ScansPerStop)
					{
						G.Records[Kind][G.ScanInStop].Add(G.Current);
					}
					UE_LOG(LogTheLastShift, Log, TEXT("EntityFoundCost: %s round %d scan %d: frame %.3f ms, GPU %.3f ms, game %.3f ms, check %d traces %.3f ms %s, figure points %d."),
						G.bAwake ? TEXT("awake") : TEXT("dormant"), G.Round, G.ScanInStop + 1, G.Current.FrameMs, G.Current.GpuMs, G.Current.GameMs, G.Current.Traces,
						G.Current.CheckMs, G.Current.Traces < 0 ? TEXT("(no check)") : (G.Current.bKept ? TEXT("kept") : TEXT("none kept")), G.Current.FigurePoints);
					++G.ScanInStop;
					G.FramesSinceScan = -1;
					G.NextScanAt = Now + 0.8;
				}
			}
			G.PrevDelta = DeltaMs;
			if (G.FramesSinceScan < 0 && G.ScanInStop >= ScansPerStop)
			{
				T->Resume();
				if (++G.Round >= G.Rounds)
				{
					if (!G.bAwake)
					{
						G.bAwake = true;
						G.Round = 0;
						E->Awaken();
					}
					else
					{
						Summarise(0);
						Summarise(1);
						Finish();
						return false;
					}
				}
				Begin(EPhase::Pose, Now);
				return true;
			}
			// Scans 2 and 3 are the player's, 0.8 s after the one before (the first is the automatic scan).
			if (G.FramesSinceScan < 0 && G.ScanInStop >= 1 && G.NextScanAt > 0.0 && Now >= G.NextScanAt)
			{
				G.NextScanAt = 0.0;
				L->Scan();
			}
			if (Now - G.PhaseStart > 20.0)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("EntityFoundCost: round %d: no scan within 20 s."), G.Round);
				Finish();
				return false;
			}
			return true;
		}
		case EPhase::Done:
		default:
			return false;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityFoundCostCommand(
	TEXT("tls.EntityFound.Cost"),
	TEXT("tls.EntityFound.Cost [Quit=0] [Rounds=10]: (an area map, PIE or a package; Area1_Hull in the reports) the frame of a scan, frame and GPU time, with the entity dormant (control, 15 m into the first corridor) and then awake and not found, stood 4 to 9 m from the figure where it is in sight (the same points in every build: seeded spawns), with the traces and milliseconds of the game's check of its points. Logs medians."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace EntityFoundCostDebug;
		if (G.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(G.Ticker);
		}
		EntityFormTest::Restore(TEXT("tls.EntityFound.Cost"));
		EntityFormTest::ForceAreaOneForm(TEXT("tls.EntityFound.Cost"));
		G = FCost();
		G.bQuit = Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0;
		G.Rounds = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 100) : 10;
		G.PhaseStart = FPlatformTime::Seconds();
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}));

#endif
