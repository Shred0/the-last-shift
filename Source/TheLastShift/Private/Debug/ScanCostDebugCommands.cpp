// SPDX-License-Identifier: MIT

// tls.ScanCost: the cost of one scan and the points it produces, from the pose the player stands at. Holds a Stop,
// fires Samples scans directly on the player's lidar (cooldown bypassed), each optionally inside a ProfileGPU frame,
// and logs per scan the game-thread time of ULidarScanComponent::Scan(), the captured and injected particles of that
// scan's slot and the injection counters. Development builds only.

#include "Containers/Ticker.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "LidarScanComponent.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#if !UE_BUILD_SHIPPING

// Frame timing globals for tls.ScanBurst (RHI and RenderCore are dependencies of non-Shipping builds only).
#include "DynamicRHI.h"
#include "RenderTimer.h"

namespace ScanCostDebug
{
	struct FPending
	{
		int32 ScanIndex = INDEX_NONE;
		double CpuMs = 0.0;
		FLidarInjectionStats Injection;
		double ReadAt = 0.0;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		int32 Samples = 20;
		float Interval = 0.6f;
		/** 0 = no ProfileGPU, 1 = ProfileGPU in the frame of the scan, 2 = ProfileGPU one frame before the scan. */
		int32 Profile = 1;
		/** 0 = waiting for the director's first scan, 1 = sampling, 2 = holding the Stop. */
		int32 Phase = 0;
		int32 ScanBase = 0;
		int32 Fired = 0;
		bool bScanNextTick = false;
		double NextTime = 0.0;
		TArray<FPending> Pending;
		TArray<double> Cpu;
		TArray<int32> Captured;
		TArray<int32> Injected;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FState G;

	static double Median(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 N = Values.Num();
		return N % 2 ? Values[N / 2] : 0.5 * (Values[N / 2 - 1] + Values[N / 2]);
	}

	static void KeepEntityDormant(UWorld* World, UTimeStateSubsystem* TimeState)
	{
		if (!TimeState || TimeState->GetCumulativeStoppedSeconds() < 10.f)
		{
			return;
		}
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			if (It->GetState() != EEntityState::Dormant)
			{
				return;
			}
		}
		TimeState->ResetCumulativeStoppedSeconds();
	}

	static void FireScan(UWorld* World, ULidarScanComponent* Lidar, double Now)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("ScanCost: ---- sample %d, scan %d ----"), G.Fired + 1, Lidar->GetScanCount());
		const double Start = FPlatformTime::Seconds();
		Lidar->Scan();
		FPending& P = G.Pending.AddDefaulted_GetRef();
		P.CpuMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		P.ScanIndex = Lidar->GetScanCount() - 1;
		P.Injection = Lidar->GetLastInjectionStats();
		P.ReadAt = Now + G.Interval * 0.8;
		++G.Fired;
	}

	static void ReadPending(ULidarScanComponent* Lidar, double Now)
	{
		for (int32 Index = G.Pending.Num() - 1; Index >= 0; --Index)
		{
			const FPending& P = G.Pending[Index];
			if (Now < P.ReadAt)
			{
				continue;
			}
			int32 Captured = -1;
			int32 Injected = -1;
			for (const FLidarScanSlotStats& S : Lidar->GetScanSlotStats())
			{
				if (S.ScanIndex == P.ScanIndex)
				{
					Captured = S.CapturedParticles;
					Injected = S.InjectedParticles;
				}
			}
			UE_LOG(LogTheLastShift, Log, TEXT("ScanCost: scan %d: game thread %.3f ms, captured %d, injected %d (sources %d, gathered %d, outside coverage %d, over cap %d, uploaded %d)."),
				P.ScanIndex, P.CpuMs, Captured, Injected, P.Injection.Sources, P.Injection.Gathered, P.Injection.OutsideCoverage,
				P.Injection.OverCap, P.Injection.Uploaded);
			G.Cpu.Add(P.CpuMs);
			G.Captured.Add(Captured);
			G.Injected.Add(Injected);
			G.Pending.RemoveAt(Index);
		}
	}

	static bool Tick(float)
	{
		UWorld* World = G.World.Get();
		UTimeStateSubsystem* TimeState = World && !World->bIsTearingDown ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		if (!TimeState || !Lidar || !TimeState->IsStopped())
		{
			if (G.Phase != 2)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("ScanCost: the Stop ended (or PIE stopped) after %d of %d samples."), G.Fired, G.Samples);
			}
			G.Ticker.Reset();
			return false;
		}
		KeepEntityDormant(World, TimeState);
		const double Now = FPlatformTime::Seconds();

		if (G.Phase == 0)
		{
			if (Lidar->GetScanCount() > G.ScanBase && G.NextTime <= 0.0)
			{
				G.NextTime = Now + G.Interval;
			}
			if (G.NextTime > 0.0 && Now >= G.NextTime)
			{
				G.Phase = 1;
			}
			return true;
		}
		if (G.Phase == 2)
		{
			return true;
		}

		ReadPending(Lidar, Now);
		if (G.bScanNextTick)
		{
			G.bScanNextTick = false;
			FireScan(World, Lidar, Now);
			G.NextTime = Now + G.Interval;
			return true;
		}
		if (G.Fired >= G.Samples)
		{
			if (G.Pending.IsEmpty())
			{
				TArray<double> Captured;
				TArray<double> Injected;
				int32 MinC = MAX_int32, MaxC = MIN_int32, MinI = MAX_int32, MaxI = MIN_int32;
				for (int32 Index = 0; Index < G.Captured.Num(); ++Index)
				{
					Captured.Add(G.Captured[Index]);
					Injected.Add(G.Injected[Index]);
					MinC = FMath::Min(MinC, G.Captured[Index]);
					MaxC = FMath::Max(MaxC, G.Captured[Index]);
					MinI = FMath::Min(MinI, G.Injected[Index]);
					MaxI = FMath::Max(MaxI, G.Injected[Index]);
				}
				TArray<double> Sorted = G.Cpu;
				Sorted.Sort();
				UE_LOG(LogTheLastShift, Display, TEXT("ScanCost: ===== %d scans: game thread median %.3f ms (min %.3f, max %.3f); captured median %.0f (min %d, max %d); injected median %.0f (min %d, max %d). Still Stopped (tls.Resume ends). ====="),
					G.Cpu.Num(), Median(G.Cpu), Sorted.IsEmpty() ? 0.0 : Sorted[0], Sorted.IsEmpty() ? 0.0 : Sorted.Last(),
					Median(Captured), MinC, MaxC, Median(Injected), MinI, MaxI);
				G.Phase = 2;
			}
			return true;
		}
		if (Now < G.NextTime)
		{
			return true;
		}
		if (G.Profile == 1)
		{
			GEngine->Exec(World, TEXT("ProfileGPU"));
			FireScan(World, Lidar, Now);
			G.NextTime = Now + G.Interval;
		}
		else if (G.Profile == 2)
		{
			GEngine->Exec(World, TEXT("ProfileGPU"));
			G.bScanNextTick = true;
		}
		else
		{
			FireScan(World, Lidar, Now);
			G.NextTime = Now + G.Interval;
		}
		return true;
	}
}

namespace ScanBurstDebug
{
	/** Per-frame timings of one window. */
	struct FWindow
	{
		TArray<double> Gpu;
		TArray<double> Render;
		TArray<double> Game;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		int32 Rounds = 4;
		int32 Frames = 30;
		/** Frames skipped at the start of a window (the stats lag the frame by one or two). */
		int32 Settle = 4;
		int32 Round = 0;
		/** false = idle window, true = a scan every frame. */
		bool bScanning = false;
		int32 FrameInWindow = 0;
		bool bStarted = false;
		double StartAt = 0.0;
		FWindow Idle;
		FWindow Scan;
		int32 Scans = 0;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FState G;

	static double Median(TArray<double> Values)
	{
		return ScanCostDebug::Median(MoveTemp(Values));
	}

	static void Finish()
	{
		const double GpuIdle = Median(G.Idle.Gpu), GpuScan = Median(G.Scan.Gpu);
		const double RtIdle = Median(G.Idle.Render), RtScan = Median(G.Scan.Render);
		const double GtIdle = Median(G.Idle.Game), GtScan = Median(G.Scan.Game);
		UE_LOG(LogTheLastShift, Display, TEXT("ScanBurst: ===== %d rounds x %d frames, %d scans. Median per frame, idle -> one scan every frame (delta = cost of one scan): GPU %.3f -> %.3f ms (+%.3f), render thread %.3f -> %.3f ms (+%.3f), game thread %.3f -> %.3f ms (+%.3f). Still Stopped. ====="),
			G.Rounds, G.Frames, G.Scans, GpuIdle, GpuScan, GpuScan - GpuIdle, RtIdle, RtScan, RtScan - RtIdle, GtIdle, GtScan, GtScan - GtIdle);
	}

	static bool Tick(float)
	{
		UWorld* World = G.World.Get();
		UTimeStateSubsystem* TimeState = World && !World->bIsTearingDown ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		if (!TimeState || !Lidar || !TimeState->IsStopped())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("ScanBurst: the Stop ended (or PIE stopped) in round %d."), G.Round);
			G.Ticker.Reset();
			return false;
		}
		ScanCostDebug::KeepEntityDormant(World, TimeState);
		if (!G.bStarted)
		{
			// Wait for the director's first scan and its slots to settle.
			if (Lidar->GetScanCount() == 0 || FPlatformTime::Seconds() < G.StartAt)
			{
				return true;
			}
			G.bStarted = true;
		}

		// Timings of the frame that just completed.
		if (G.FrameInWindow >= G.Settle)
		{
			FWindow& Window = G.bScanning ? G.Scan : G.Idle;
			Window.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			Window.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			Window.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		}
		++G.FrameInWindow;
		if (G.FrameInWindow >= G.Frames + G.Settle)
		{
			G.FrameInWindow = 0;
			if (G.bScanning)
			{
				++G.Round;
			}
			G.bScanning = !G.bScanning;
			if (G.Round >= G.Rounds)
			{
				Finish();
				G.Ticker.Reset();
				return false;
			}
		}
		if (G.bScanning)
		{
			Lidar->Scan();
			++G.Scans;
		}
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsScanBurstCommand(
	TEXT("tls.ScanBurst"),
	TEXT("tls.ScanBurst [Rounds=4] [Frames=30]: stops time if needed, then alternates Rounds times a window of Frames idle frames and a window of Frames frames with one scan on the player's lidar every frame (cooldown bypassed), from the current pose. Logs the median GPU, render-thread and game-thread time per frame of each kind of window and the difference: the cost of one scan. Keeps a dormant entity dormant and holds the Stop."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace ScanBurstDebug;
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!TimeState || !TimeStateDebug::FindPlayerDirector(World))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanBurst: no game world with a player lidar."));
			return;
		}
		if (G.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(G.Ticker);
		}
		G = FState();
		G.World = World;
		G.Rounds = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 50) : 4;
		G.Frames = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 5, 600) : 30;
		G.StartAt = FPlatformTime::Seconds() + 1.5;
		if (!TimeState->IsStopped())
		{
			TimeState->Stop();
		}
		UE_LOG(LogTheLastShift, Display, TEXT("ScanBurst: %d rounds x %d frames."), G.Rounds, G.Frames);
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsScanCostCommand(
	TEXT("tls.ScanCost"),
	TEXT("tls.ScanCost [Samples=20] [Interval=0.6] [Profile=1]: stops time if needed, waits for the director's first scan, then fires Samples scans on the player's lidar from the current pose, Interval s apart, and logs for each the game-thread time of Scan(), the captured and injected particles of its slot and the injection counters, then the medians. Profile 1 issues ProfileGPU in the frame of each scan, 2 one frame before, 0 never. Keeps a dormant entity dormant and holds the Stop when done."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace ScanCostDebug;
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* Lidar = Director ? Director->GetLidarScan() : nullptr;
		if (!TimeState || !Lidar)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanCost: no game world with a player lidar."));
			return;
		}
		if (G.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(G.Ticker);
			G.Ticker.Reset();
		}
		G = FState();
		G.World = World;
		G.Samples = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 20;
		G.Interval = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.2f, 5.f) : 0.6f;
		G.Profile = Args.Num() > 2 ? FMath::Clamp(FCString::Atoi(*Args[2]), 0, 2) : 1;
		G.ScanBase = Lidar->GetScanCount();
		if (TimeState->IsStopped())
		{
			// Already stopped: start sampling after one interval.
			G.NextTime = FPlatformTime::Seconds() + G.Interval;
		}
		else
		{
			TimeState->Stop();
		}
		UE_LOG(LogTheLastShift, Display, TEXT("ScanCost: %d samples, %.2f s apart, profile mode %d, CaptureResolution %d, MaxRange %.0f cm."),
			G.Samples, G.Interval, G.Profile, Lidar->CaptureResolution, Lidar->MaxRange);
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}));

#endif // !UE_BUILD_SHIPPING
