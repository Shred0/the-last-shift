// SPDX-License-Identifier: MIT

// Brief 13, the cost of the neighbour lines without a frame cap: tls.Links.CostRun runs in Area2_ServerHalls (PIE or a
// package; a package runs uncapped). It stands at the hall view of beacon 0, takes 16 scans with 0 neighbours, measures
// 1000 frames there and 1000 frames from 25 m behind, then the same with the settings' line values (neighbours from the
// command, logging links per scan, links dropped by the cap and line particles), then the frame of a scan and the worst
// of the 12 frames that follow, lines off against on. Development builds only.
// Brief 16 added a pose: "corridor" stands 15 m into the current area's first corridor, looking back at its start (so the
// position 25 m behind is further along the same corridor), in any area map.
// Brief 25 added "memory" (fourth argument): both runs with the settings' line values, the first with the scan memory off
// (ratio 0), the second with the settings' ratio (2 when they have 0); every measure also logs the points and line particles
// alive.
// Built with TLS_COST_BASELINE defined to 1 (the Step 1 baseline copy, without lines), it measures 0 neighbours only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LidarScanComponent.h"
#include "Misc/App.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#if !UE_BUILD_SHIPPING

#include "DynamicRHI.h"
#include "RenderTimer.h"

#ifndef TLS_COST_BASELINE
#define TLS_COST_BASELINE 0
#endif

namespace LinksCostDebug
{
	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }

	static double Median(TArray<double> V)
	{
		if (V.IsEmpty())
		{
			return 0.0;
		}
		V.Sort();
		return V[V.Num() / 2];
	}

	enum class EPhase : uint8 { WaitWorld, Pose, Scans, MeasureHall, MeasureBehind, ScanFrames, Done };

	struct FCostRun
	{
		EPhase Phase = EPhase::WaitWorld;
		int32 Setting = 0; // 0 = 0 neighbours, 1 = the settings' line values
		double PhaseStart = 0.0;
		bool bQuit = false;
		bool bCorridor = false;
		FVector HallLocation = FVector::ZeroVector;
		FRotator HallRotation = FRotator::ZeroRotator;
		TArray<double> Frame, Gpu, Game;
		FString Results;
		// Scan frames.
		int32 Round = 0;
		bool bLinesOn = false;
		int32 FrameInRound = -1;
		int32 IdleFrames = 0;
		double RoundMax = 0.0;
		TArray<double> First[2], Worst[2];
		FTSTicker::FDelegateHandle Ticker;
#if !TLS_COST_BASELINE
		FScanLinks SavedLinks;
		FScanMemory SavedMemory;
		bool bMemory = false;
		int32 Neighbours = 2;
		int32 LastStatsScan = INDEX_NONE;
		TArray<FLidarLinkStats> ScanStats;
#endif
	};
	static FCostRun G;

	static void Exec(const TCHAR* Command)
	{
		if (APlayerController* C = PC())
		{
			C->ConsoleCommand(Command);
		}
	}

	static void SetLines(bool bOn)
	{
#if !TLS_COST_BASELINE
		FScanLinks Values = G.SavedLinks;
		Values.Neighbours = (bOn || G.bMemory) ? G.Neighbours : 0;
		GetMutableDefault<UTheLastShiftSettings>()->ScanLinks = Values;
		if (G.bMemory)
		{
			// Brief 25: lines on in both runs; the memory off (ratio 0) against the settings' ratio.
			FScanMemory Memory = G.SavedMemory;
			Memory.Ratio = bOn ? (G.SavedMemory.Ratio > 0.f ? G.SavedMemory.Ratio : 2.f) : 0.f;
			GetMutableDefault<UTheLastShiftSettings>()->ScanMemory = Memory;
		}
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanLinks();
			D->ApplyScanMemory();
		}
#endif
	}

	static void KeepDormant()
	{
		if (UTimeStateSubsystem* T = TS())
		{
			T->ResetCumulativeStoppedSeconds();
		}
	}

	static void PlaceAt(const FVector& Location, const FRotator& Rotation)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		P->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		C->SetControlRotation(Rotation);
	}

	/** Records the link counters of each scan whose search finishes (lines on). */
	static void PollLinkStats(const ULidarScanComponent* L)
	{
#if !TLS_COST_BASELINE
		if (!L || G.Setting != 1)
		{
			return;
		}
		const FLidarLinkStats Stats = L->GetLastLinkStats();
		if (Stats.ScanIndex != INDEX_NONE && Stats.ScanIndex != G.LastStatsScan)
		{
			G.LastStatsScan = Stats.ScanIndex;
			G.ScanStats.Add(Stats);
		}
#endif
	}

	/** Links per scan, links dropped by the cap and line particles of the alive scans, for the lines-on run. */
	static FString LinkSummary(const ULidarScanComponent* L)
	{
#if !TLS_COST_BASELINE
		TArray<double> Links, Points, Search, DroppedRows;
		int64 Dropped = 0;
		int32 MaxDegree = 0;
		int32 Grid = 0;
		const int32 Width = L ? FMath::Max(L->CaptureResolution, 1) : 1;
		for (const FLidarLinkStats& S : G.ScanStats)
		{
			Links.Add(S.Links);
			Points.Add(S.Points);
			Search.Add(S.SearchMilliseconds);
			Dropped += S.Dropped;
			MaxDegree = FMath::Max(MaxDegree, S.MaxDegree);
			Grid += S.Mode == ELidarLinkMode::Grid ? 1 : 0;
			if (S.FirstDroppedTexel >= 0)
			{
				// Brief 23: the cap drops the links of the capture's rows from this one down (the bottom of the view).
				DroppedRows.Add(S.FirstDroppedTexel / Width);
			}
		}
		int64 LineParticles = 0;
		int32 Slots = 0;
		if (L)
		{
			for (const FLidarScanSlotStats& Slot : L->GetScanSlotStats())
			{
				if (Slot.ScanIndex != INDEX_NONE)
				{
					LineParticles += Slot.LineParticles;
					++Slots;
				}
			}
		}
		Links.Sort();
		return FString::Printf(TEXT("%s mode (%d of %d searches on the grid), %d neighbours, %.1f cm, cap %d: %d searches; links per scan median %.0f (min %.0f, max %.0f) for median %.0f points; %lld links dropped by the cap in all, in %d scans, from row %.0f of %d down (median; the bottom %.0f%% of the view); max degree %d; search %.1f ms median; line particles of the %d alive scans %lld"),
			L && L->LinkMode == ELidarLinkMode::Grid ? TEXT("grid") : TEXT("nearest"), Grid, G.ScanStats.Num(), G.Neighbours, L ? L->LinkMaxDistance : -1.f, L ? L->MaxLinksPerScan : -1, G.ScanStats.Num(),
			Median(Links), Links.IsEmpty() ? 0.0 : Links[0], Links.IsEmpty() ? 0.0 : Links.Last(),
			Median(Points), Dropped, DroppedRows.Num(), DroppedRows.IsEmpty() ? -1.0 : Median(DroppedRows), Width,
			DroppedRows.IsEmpty() ? 0.0 : 100.0 * (Width - Median(DroppedRows)) / Width, MaxDegree, Median(Search), Slots, LineParticles);
#else
		return FString();
#endif
	}

	/** Brief 25: the captured points and line particles alive in every slot. */
	static FString Alive(const ULidarScanComponent* L)
	{
		int64 Points = 0, Lines = 0;
		for (const FLidarScanSlotStats& Slot : L ? L->GetScanSlotStats() : TArray<FLidarScanSlotStats>())
		{
			Points += Slot.CapturedParticles;
			Lines += Slot.LineParticles;
		}
		return FString::Printf(TEXT("%lld points and %lld line particles alive"), Points, Lines);
	}

	static const TCHAR* SettingName(int32 Setting)
	{
#if !TLS_COST_BASELINE
		if (G.bMemory)
		{
			return Setting == 1 ? TEXT("memory on") : TEXT("memory off");
		}
#endif
		return Setting == 1 ? TEXT("lines on") : TEXT("0 neighbours");
	}

	static void Begin(EPhase Phase, double Now)
	{
		G.Phase = Phase;
		G.PhaseStart = Now;
		G.Frame.Reset();
		G.Gpu.Reset();
		G.Game.Reset();
	}

	static bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		ULidarScanComponent* L = Lidar();
		KeepDormant();
		PollLinkStats(L);
		const double FrameMs = FApp::GetDeltaTime() * 1000.0;
		switch (G.Phase)
		{
		case EPhase::WaitWorld:
			if (L && Pawn() && Now - G.PhaseStart > 4.0)
			{
				Exec(TEXT("t.MaxFPS 0"));
				Exec(TEXT("r.VSync 0"));
				Exec(TEXT("r.VSyncEditor 0"));
				Begin(EPhase::Pose, Now);
			}
			else if (Now - G.PhaseStart > 60.0)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("CostRun: no world with the player's lidar."));
				G.Phase = EPhase::Done;
			}
			return G.Phase != EPhase::Done;
		case EPhase::Pose:
			if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
			{
				T->Resume();
			}
			SetLines(G.Setting == 1);
			if (G.bCorridor)
			{
				// 15 m into the area's first corridor, looking back at its start.
				const UWorld* World = Pawn() ? Pawn()->GetWorld() : nullptr;
				const AAreaInfo* Info = AAreaInfo::Find(World);
				bool bPlaced = false;
				for (int32 I = 0; Info && I < Info->Segments.Num() && !bPlaced; ++I)
				{
					if (Info->Segments[I].Type != EAreaSegmentType::Corridor)
					{
						continue;
					}
					int32 Unused = 0;
					const FVector From = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 1500.f, Unused);
					const FVector To = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 150.f, Unused);
					Exec(*FString::Printf(TEXT("tls.StandAt %.0f %.0f %.0f %.0f"), From.X, From.Y, To.X, To.Y));
					bPlaced = true;
				}
				if (!bPlaced)
				{
					UE_LOG(LogTheLastShift, Warning, TEXT("CostRun: no corridor in this map."));
				}
			}
			else
			{
				Exec(TEXT("tls.Beacon.View 0 middle"));
			}
			Begin(EPhase::Scans, Now);
			G.FrameInRound = -2;
			return true;
		case EPhase::Scans:
			if (G.FrameInRound == -2 && Now - G.PhaseStart > 0.6)
			{
				if (const APawn* P = Pawn())
				{
					G.HallLocation = P->GetActorLocation();
					G.HallRotation = PC()->GetControlRotation();
				}
				Exec(TEXT("tls.ScanSlotTest 16 0 100 0.3"));
				G.FrameInRound = -1;
				G.PhaseStart = Now;
			}
			if (G.FrameInRound == -1 && L && L->GetAliveScanCount() >= 16 && Now - G.PhaseStart > 7.0)
			{
				PlaceAt(G.HallLocation, G.HallRotation);
				Begin(EPhase::MeasureHall, Now);
			}
			return true;
		case EPhase::MeasureHall:
		case EPhase::MeasureBehind:
			if (Now - G.PhaseStart < 0.5)
			{
				return true;
			}
			G.Frame.Add(FrameMs);
			G.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			G.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
			if (G.Frame.Num() >= 1000)
			{
				const TCHAR* Where = G.Phase == EPhase::MeasureHall ? TEXT("down the hall") : TEXT("25 m behind");
				const FString Line = FString::Printf(TEXT("%s, %s: frame %.3f ms, GPU %.3f ms, game thread %.3f ms (medians of 1000 frames, %d alive scans; %s)"),
					SettingName(G.Setting), Where, Median(G.Frame), Median(G.Gpu), Median(G.Game), L ? L->GetAliveScanCount() : -1, *Alive(L));
				UE_LOG(LogTheLastShift, Display, TEXT("CostRun: %s"), *Line);
				G.Results += TEXT("\n  ") + Line;
				if (G.Phase == EPhase::MeasureHall && G.Setting == 1 && !TLS_COST_BASELINE)
				{
					const FString Summary = LinkSummary(L);
					UE_LOG(LogTheLastShift, Display, TEXT("CostRun: %s"), *Summary);
					G.Results += TEXT("\n  ") + Summary;
				}
				if (G.Phase == EPhase::MeasureHall)
				{
					const FVector Back = G.HallRotation.Vector().GetSafeNormal2D() * 2500.0;
					PlaceAt(G.HallLocation - Back, G.HallRotation);
					Begin(EPhase::MeasureBehind, Now);
				}
				else if (G.Setting == 0 && !TLS_COST_BASELINE)
				{
					G.Setting = 1;
					Begin(EPhase::Pose, Now);
				}
				else
				{
					PlaceAt(G.HallLocation, G.HallRotation);
					Begin(TLS_COST_BASELINE ? EPhase::Done : EPhase::ScanFrames, Now);
					G.Round = 0;
					G.bLinesOn = false;
					G.FrameInRound = -1;
					G.IdleFrames = 0;
				}
			}
			return true;
		case EPhase::ScanFrames:
		{
			if (!L)
			{
				G.Phase = EPhase::Done;
				return true;
			}
			const int32 Kind = G.bLinesOn ? 1 : 0;
			if (G.FrameInRound >= 0)
			{
				// Frame 0 issued the scan; the next tick's delta is that frame's time.
				if (G.FrameInRound == 1)
				{
					G.First[Kind].Add(FrameMs);
				}
				if (G.FrameInRound >= 1)
				{
					G.RoundMax = FMath::Max(G.RoundMax, FrameMs);
				}
				if (++G.FrameInRound > 12)
				{
					G.Worst[Kind].Add(G.RoundMax);
					G.FrameInRound = -1;
					G.bLinesOn = !G.bLinesOn;
					G.Round += G.bLinesOn ? 0 : 1;
				}
				return true;
			}
			G.Frame.Add(FrameMs);
			if (G.Round >= 20)
			{
				const FString Line = FString::Printf(TEXT("scan frames (20 each, 16 alive scans): idle frame %.3f ms; frame of the scan: %s %.3f ms, %s %.3f ms; worst of the 12 frames from the scan: %.3f ms, %.3f ms (medians; %s)"),
					Median(G.Frame), SettingName(0), Median(G.First[0]), SettingName(1), Median(G.First[1]), Median(G.Worst[0]), Median(G.Worst[1]), *Alive(L));
				UE_LOG(LogTheLastShift, Display, TEXT("CostRun: %s"), *Line);
				G.Results += TEXT("\n  ") + Line;
				G.Phase = EPhase::Done;
				return true;
			}
			if (++G.IdleFrames % 30 == 0)
			{
				SetLines(G.bLinesOn);
				L->Scan();
				G.FrameInRound = 0;
				G.RoundMax = 0.0;
			}
			return true;
		}
		case EPhase::Done:
		default:
#if !TLS_COST_BASELINE
			GetMutableDefault<UTheLastShiftSettings>()->ScanLinks = G.SavedLinks;
			GetMutableDefault<UTheLastShiftSettings>()->ScanMemory = G.SavedMemory;
			if (UScanDirectorComponent* D = Director())
			{
				D->ApplyScanLinks();
				D->ApplyScanMemory();
			}
#endif
			UE_LOG(LogTheLastShift, Display, TEXT("CostRun: ===== done (%s build)%s ====="), TLS_COST_BASELINE ? TEXT("Step 1 baseline") : TEXT("Brief 13"), *G.Results);
			if (G.bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("tls.Links.CostRun"));
			}
			return false;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksCostRunCommand(
	TEXT("tls.Links.CostRun"),
	TEXT("tls.Links.CostRun [Quit=0] [Neighbours, 0 = the settings'] [hall|corridor] [memory]: (memory, Brief 25: both runs with lines on, the scan memory off against the settings' ratio; every measure logs the points and line particles alive) (PIE or a package; hall in Area2_ServerHalls, corridor in any area: 15 m into its first corridor, looking back at the start) uncaps the frame rate, then at beacon 0's hall view takes 16 scans with 0 neighbours and measures 1000 frames there and 1000 from 25 m behind, then the same with the settings' line values and Neighbours (default the settings', at least 2), logging links per scan, links dropped by the cap and line particles, then the frame of a scan and the worst of the 12 frames after it, lines off against on. Logs medians."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksCostDebug;
		if (G.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(G.Ticker);
		}
		G = FCostRun();
		G.bQuit = Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0;
		G.bCorridor = Args.Num() > 2 && Args[2].Equals(TEXT("corridor"), ESearchCase::IgnoreCase);
#if !TLS_COST_BASELINE
		G.Neighbours = Args.Num() > 1 && FCString::Atoi(*Args[1]) > 0 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 8) : FMath::Max(UTheLastShiftSettings::GetScanLinks().Neighbours, 2);
#endif
		G.PhaseStart = FPlatformTime::Seconds();
#if !TLS_COST_BASELINE
		G.SavedLinks = UTheLastShiftSettings::GetScanLinks();
		G.SavedMemory = UTheLastShiftSettings::GetScanMemory();
		G.bMemory = Args.Num() > 3 && Args[3].Equals(TEXT("memory"), ESearchCase::IgnoreCase);
#endif
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}));

#endif
