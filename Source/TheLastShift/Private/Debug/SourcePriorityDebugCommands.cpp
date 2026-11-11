// SPDX-License-Identifier: MIT

// tls.SourcePriorityTest: the lidar's injected-point cap cuts the lowest point-source priorities, whatever the
// registration order (Brief 2l). tls.Lidar.SourcePriorities: re-registers the entity and the residues at priority 0 or
// at the director's priorities, for A/B counts. Development builds only.

#include "Containers/Ticker.h"
#include "Debug/LidarTestSource.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "LidarScanComponent.h"
#include "Residue/Residue.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace SourcePriorityDebug
{
	/** Points of the debug source: more than the lidar's cap on its own. */
	static constexpr int32 DebugPoints = 5000;

	struct FCase
	{
		FString Name;
		/** Debug source priority; INT_MAX = one above the entity's. */
		int32 Priority = 0;
		/** Register the debug source before the entity and the residues (they are re-registered after it). */
		bool bBeforeOthers = false;
		/** The debug source is served first: every entity and residue point must be cut. */
		bool bDebugFirst = false;
	};

	struct FSample
	{
		int32 ScanIndex = INDEX_NONE;
		FLidarInjectionStats Stats;
		int32 Dropped = 0;
		int32 Particles = -1;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TStrongObjectPtr<UTlsLidarTestSource> Source;
		TArray<FCase> Cases;
		int32 Case = 0;
		/** 0 = settle after Stop, 1 = scan, 2 = read particles. */
		int32 Phase = 0;
		double NextTime = 0.0;
		FSample Control;
		FSample Test;
		int32 Passed = 0;
		int32 Failed = 0;
		int32 Cap = 4096;
		bool bPlaced = false;
		/** Entity speeds before the test (the entity is frozen so that consecutive scans see the same figure). */
		TArray<TPair<TWeakObjectPtr<AEntity>, float>> SavedSpeeds;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FState G;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? G.Passed : G.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.SourcePriorityTest   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static UScanDirectorComponent* Director()
	{
		UWorld* World = G.World.Get();
		return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
	}

	static TArray<UObject*> GameSources(UWorld* World)
	{
		TArray<UObject*> Out;
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		return Out;
	}

	/** Re-registers the entity and the residues through the director (their game priorities), after everything else. */
	static void ReRegisterGameSources(UScanDirectorComponent* D)
	{
		for (UObject* Object : GameSources(D->GetWorld()))
		{
			D->UnregisterPointSource(TScriptInterface<ILidarPointSource>(Object));
			D->RegisterPointSource(TScriptInterface<ILidarPointSource>(Object));
		}
	}

	static int32 ReadParticles(const ULidarScanComponent* L, int32 ScanIndex)
	{
		for (const FLidarScanSlotStats& S : L->GetScanSlotStats())
		{
			if (S.ScanIndex == ScanIndex)
			{
				return S.InjectedParticles;
			}
		}
		return -1;
	}

	static FSample ScanNow(ULidarScanComponent* L)
	{
		FSample S;
		L->Scan();
		S.ScanIndex = L->GetScanCount() - 1;
		S.Stats = L->GetLastInjectionStats();
		S.Dropped = L->GetLastDroppedInjectedPoints();
		return S;
	}

	/**
	 * Puts the debug cube (60 cm) 2 m behind the surface the newest scan captured at its centre texel, on that texel's ray:
	 * inside the capture, hidden by what the capture saw. False when there is no scan to read.
	 */
	static bool PlaceDebugCube(ULidarScanComponent* L)
	{
		TArray<float> Depth;
		int32 Width = 0, Height = 0;
		FLidarScanContext Context;
		if (!G.Source || !L || !L->ReadNewestScanDepth(Depth, Width, Height, Context))
		{
			return false;
		}
		const float Centre = Depth[(Height / 2) * Width + Width / 2];
		if (Centre <= 1.f || Centre >= Context.MaxRange - 300.f)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.SourcePriorityTest: no surface at the centre of the view (depth %.0f cm): face a wall."), Centre);
			return false;
		}
		const double U = ((Width / 2 + 0.5) / Width) * 2.0 - 1.0;
		const double V = 1.0 - ((Height / 2 + 0.5) / Height) * 2.0;
		const FVector Ray = Context.CaptureForward + U * Context.TanHalfFOV.X * Context.CaptureRight + V * Context.TanHalfFOV.Y * Context.CaptureUp;
		G.Source->Center = Context.CaptureOrigin + Ray * (Centre + 200.f);
		G.Source->Size = 60.f;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.SourcePriorityTest: debug cube of %d points at %s, 2 m behind the captured surface at %.1f m."),
			DebugPoints, *G.Source->Center.ToCompactString(), Centre / 100.f);
		return true;
	}

	static void Finish(const TCHAR* Why)
	{
		for (const TPair<TWeakObjectPtr<AEntity>, float>& Saved : G.SavedSpeeds)
		{
			if (AEntity* Entity = Saved.Key.Get())
			{
				Entity->Speed = Saved.Value;
			}
		}
		G.SavedSpeeds.Reset();
		if (UScanDirectorComponent* D = Director(); D && G.Source)
		{
			D->UnregisterPointSource(TScriptInterface<ILidarPointSource>(G.Source.Get()));
		}
		G.Source.Reset();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.SourcePriorityTest summary (%s): %d checks passed, %d failed%s."),
			G.Failed == 0 && !*Why ? TEXT("PASS") : TEXT("FAIL"), G.Passed, G.Failed, Why);
		G.Ticker.Reset();
	}

	static bool Tick(float)
	{
		UWorld* World = G.World.Get();
		UTimeStateSubsystem* TimeState = World && !World->bIsTearingDown ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* D = Director();
		ULidarScanComponent* L = D ? D->GetLidarScan() : nullptr;
		if (!TimeState || !L || !TimeState->IsStopped())
		{
			Finish(TEXT(" - the Stop ended or PIE stopped"));
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < G.NextTime)
		{
			return true;
		}
		if (G.Case >= G.Cases.Num())
		{
			Finish(TEXT(""));
			return false;
		}
		if (G.Case == 0 && G.Phase == 0 && !G.bPlaced)
		{
			// The director's first scan is alive: place the cube from what it captured.
			if (!PlaceDebugCube(L))
			{
				Finish(TEXT(" - could not place the debug cube"));
				return false;
			}
			G.bPlaced = true;
		}
		const FCase& C = G.Cases[G.Case];
		if (G.Phase == 0)
		{
			// Register the debug source as the case says.
			UTlsLidarTestSource* Source = G.Source.Get();
			D->UnregisterPointSource(TScriptInterface<ILidarPointSource>(Source));
			const int32 EntityPriority = UScanDirectorComponent::GetPointSourcePriority(GameSources(World).Num() > 0 ? GameSources(World)[0] : nullptr);
			const int32 Priority = C.Priority == MAX_int32 ? FMath::Max(EntityPriority, 2) + 1 : C.Priority;
			D->RegisterPointSourceAtPriorityForTest(TScriptInterface<ILidarPointSource>(Source), Priority);
			if (C.bBeforeOthers)
			{
				ReRegisterGameSources(D);
			}
			FString Order;
			for (UObject* Object : GameSources(World))
			{
				Order += FString::Printf(TEXT("%s%s=%d"), Order.IsEmpty() ? TEXT("") : TEXT(", "), *Object->GetName(), L->GetPointSourcePriority(TScriptInterface<ILidarPointSource>(Object)));
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.SourcePriorityTest: case '%s': debug source at priority %d, registered %s the game sources (%s)."),
				*C.Name, Priority, C.bBeforeOthers ? TEXT("before") : TEXT("after"), *Order);

			// Same pose, entity frozen: a control scan with the debug source empty, read while it is the newest scan (the
			// entity's points last only until the next scan), then the test scan, read the same way.
			Source->PointCount = 0;
			G.Control = ScanNow(L);
			G.Phase = 1;
			G.NextTime = Now + 0.8;
			return true;
		}
		if (G.Phase == 1)
		{
			G.Control.Particles = ReadParticles(L, G.Control.ScanIndex);
			G.Source->PointCount = DebugPoints;
			G.Test = ScanNow(L);
			G.Phase = 2;
			G.NextTime = Now + 0.8;
			return true;
		}

		G.Test.Particles = ReadParticles(L, G.Test.ScanIndex);
		const FLidarInjectionStats& Cs = G.Control.Stats;
		const FLidarInjectionStats& Ts = G.Test.Stats;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.SourcePriorityTest: case '%s': control scan %d: %d sources, %d gathered, %d outside coverage, %d uploaded, %d dropped, %d drawn; test scan %d: %d sources, %d gathered, %d outside coverage, %d uploaded, %d dropped, %d drawn."),
			*C.Name, G.Control.ScanIndex, Cs.Sources, Cs.Gathered, Cs.OutsideCoverage, Cs.Uploaded, G.Control.Dropped, G.Control.Particles,
			G.Test.ScanIndex, Ts.Sources, Ts.Gathered, Ts.OutsideCoverage, Ts.Uploaded, G.Test.Dropped, G.Test.Particles);

		// The test scan's game points in coverage (its gathered points minus the debug ones, all inside the capture, minus
		// those outside coverage), plus the debug points: what the scan wanted; the excess over the cap must be dropped.
		const int32 GamePoints = Ts.Gathered - DebugPoints - Ts.OutsideCoverage;
		const int32 Excess = FMath::Max(GamePoints + DebugPoints - G.Cap, 0);
		Check(Cs.Uploaded > 0 && G.Control.Dropped == 0, FString::Printf(TEXT("%s: the entity and the residues give %d points in coverage, under the cap"), *C.Name, Cs.Uploaded));
		Check(Ts.Uploaded == G.Cap, FString::Printf(TEXT("%s: the test scan uploads exactly the cap (%d of %d)"), *C.Name, Ts.Uploaded, G.Cap));
		Check(G.Test.Dropped == Excess && Ts.OverCap == Excess, FString::Printf(TEXT("%s: dropped %d = the excess %d (%d game + %d debug - cap %d)"),
			*C.Name, G.Test.Dropped, Excess, GamePoints, DebugPoints, G.Cap));
		if (C.bDebugFirst)
		{
			// The debug points (all behind a wall, so all removed by the GPU depth test) took the whole cap.
			Check(G.Test.Particles == 0, FString::Printf(TEXT("%s: the entity and the residues are cut instead: no point drawn (%d; control %d)"),
				*C.Name, G.Test.Particles, G.Control.Particles));
		}
		else
		{
			// The debug points are cut and the ones uploaded are all hidden: what is drawn is the game sources', as in the control.
			const int32 Tolerance = FMath::Max(5, G.Control.Particles / 50);

			Check(G.Control.Particles > 0 && FMath::Abs(G.Test.Particles - G.Control.Particles) <= Tolerance,
				FString::Printf(TEXT("%s: every entity and residue point is present: drawn %d, control without the debug source %d (tolerance %d: the entity's per-scan jitter)"),
					*C.Name, G.Test.Particles, G.Control.Particles, Tolerance));
		}
		D->UnregisterPointSource(TScriptInterface<ILidarPointSource>(G.Source.Get()));
		++G.Case;
		G.Phase = 0;
		G.NextTime = Now + 0.3;
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsSourcePriorityTestCommand(
	TEXT("tls.SourcePriorityTest"),
	TEXT("tls.SourcePriorityTest: stops time (entity awakened first, so it has points), puts a 5000-point debug source 2 m behind the surface the first scan captured at the centre of the view (inside the capture, all removed by the GPU depth test) and, for three cases (priority 0 registered before the entity and the residues, priority 0 registered after, priority above the entity), fires a control scan without its points and then a test scan with them, each read while it is the newest scan (the entity is frozen for the run). Checks: the test scan uploads exactly the cap and drops exactly the excess; at priority 0 every entity and residue point is drawn as in the control; at the top priority nothing else is. Logs passed and failed counts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace SourcePriorityDebug;
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* D = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* L = D ? D->GetLidarScan() : nullptr;
		if (!TimeState || !L)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.SourcePriorityTest: no game world with a player lidar."));
			return;
		}
		if (G.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(G.Ticker);
		}
		G = FState();
		G.World = World;
		G.Cap = FMath::Clamp(L->MaxInjectedPointsPerScan, 0, 65536);
		G.Source.Reset(NewObject<UTlsLidarTestSource>(GetTransientPackage()));
		G.Source->PointCount = 0;
		G.Source->Settings.Color = FLinearColor::White;
		G.Source->Settings.FadeMode = ELidarFadeMode::ScanFade;
		G.Source->Settings.bOccludeByCapture = true;
		G.Cases = {
			{ TEXT("priority 0, registered before the entity and the residues"), 0, true, false },
			{ TEXT("priority 0, registered after the entity and the residues"), 0, false, false },
			{ TEXT("priority above the entity"), MAX_int32, false, true },
		};
		GEngine->Exec(World, TEXT("tls.Entity.Awake"));
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			G.SavedSpeeds.Emplace(*It, It->Speed);
			It->Speed = 0.f;
		}
		if (!TimeState->IsStopped())
		{
			TimeState->Stop();
		}
		G.NextTime = FPlatformTime::Seconds() + 1.6;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.SourcePriorityTest started: cap %d, %d cases."), G.Cap, G.Cases.Num());
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarSourcePrioritiesCommand(
	TEXT("tls.Lidar.SourcePriorities"),
	TEXT("tls.Lidar.SourcePriorities zero|game: re-registers the entity and every residue with the player's lidar at priority 0 (in that order, as before Brief 2l), or through the director at their game priorities. For A/B injected counts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace SourcePriorityDebug;
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UScanDirectorComponent* D = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		if (!D)
		{
			return;
		}
		const bool bZero = Args.Num() > 0 && Args[0].Equals(TEXT("zero"), ESearchCase::IgnoreCase);
		int32 Count = 0;
		for (UObject* Object : GameSources(World))
		{
			D->UnregisterPointSource(TScriptInterface<ILidarPointSource>(Object));
			if (bZero)
			{
				D->RegisterPointSourceAtPriorityForTest(TScriptInterface<ILidarPointSource>(Object), 0);
			}
			else
			{
				D->RegisterPointSource(TScriptInterface<ILidarPointSource>(Object));
			}
			++Count;
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.SourcePriorities: %d sources re-registered at %s."), Count, bZero ? TEXT("priority 0") : TEXT("their game priorities"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarChannelsCommand(
	TEXT("tls.Lidar.Channels"),
	TEXT("tls.Lidar.Channels 0|1: turns the player's lidar colour channels off (0, as a component without channels) or back on (1, the game's setting) from the next scan on; idle scan slots switch their capture targets at once."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UScanDirectorComponent* D = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		ULidarScanComponent* L = D ? D->GetLidarScan() : nullptr;
		const bool bOn = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
		UScanDirectorComponent::bChannelsOffForTest = !bOn;
		if (L)
		{
			L->bUseChannels = bOn;
			L->ApplyLiveSettings();
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.Channels: channels %s (active for the next scan: %s)."), bOn ? TEXT("on") : TEXT("off"),
			L && L->AreChannelsActive() ? TEXT("yes") : TEXT("no"));
	}));

#endif // !UE_BUILD_SHIPPING
