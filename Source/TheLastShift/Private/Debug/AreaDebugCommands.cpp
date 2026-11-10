// SPDX-License-Identifier: MIT

// Debug console commands for generated areas, acting on the game (PIE) world where the area is generated at load:
// regenerate, clear, validate (also over many seeds), report, the determinism and regeneration checks, deliberate
// rule breaks, a top-down map dump, a teleport along the route, and travel between areas. Generation, PCG and the
// navmesh are asynchronous, so the multi-step commands wait for AAreaGenerator::OnGenerationFinished.
// Compiled out of Shipping.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Area/AreaValidator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace AreaDebug
{
	static const EAreaPieceKind AllKinds[] = { EAreaPieceKind::Floors, EAreaPieceKind::Walls, EAreaPieceKind::Curbs,
		EAreaPieceKind::Pillars, EAreaPieceKind::Ceilings, EAreaPieceKind::Backdrop };

	static AAreaGenerator* Generator(UWorld* InWorld, const TCHAR* Command)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		AAreaGenerator* Gen = World && World->IsGameWorld() ? AAreaGenerator::Find(World) : nullptr;
		if (!Gen)
		{
			UE_LOG(LogArea, Warning, TEXT("%s: needs a game (PIE) world with an area generator."), Command);
		}
		return Gen;
	}

	static bool IsBusy(const AAreaGenerator* Gen, const TCHAR* Command)
	{
		const EAreaGenerationState State = Gen->GetGenerationState();
		if (State == EAreaGenerationState::BuildingWorld || State == EAreaGenerationState::BuildingNavigation)
		{
			UE_LOG(LogArea, Warning, TEXT("%s: the area is still being generated; try again when it is ready."), Command);
			return true;
		}
		return false;
	}

	static int32 CountGenerated(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count += It->ActorHasTag(AAreaInfo::GeneratedTag) ? 1 : 0;
		}
		return Count;
	}

	/** Calls Then once when Gen next finishes a generation or revalidation (on the next tick, outside the broadcast). */
	static void OnceFinished(AAreaGenerator* Gen, TFunction<void(AAreaGenerator*, bool)> Then)
	{
		// The callback state lives outside the delegate: the binding is removed on the next tick, never from inside its
		// own broadcast (that would free the lambda while it runs).
		struct FState
		{
			FDelegateHandle Handle;
			TFunction<void(AAreaGenerator*, bool)> Then;
			bool bFired = false;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		State->Then = MoveTemp(Then);
		TWeakObjectPtr<AAreaGenerator> WeakGen = Gen;
		State->Handle = Gen->OnGenerationFinished.AddLambda([State, WeakGen](AAreaGenerator* Finished, bool bPassed)
		{
			if (State->bFired)
			{
				return;
			}
			State->bFired = true;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, WeakGen, bPassed](float)
			{
				if (AAreaGenerator* G = WeakGen.Get())
				{
					G->OnGenerationFinished.Remove(State->Handle);
					State->Then(G, bPassed);
				}
				return false;
			}));
		});
	}

	static FString FailedRules(const AAreaGenerator* Gen, TArray<FString>* OutKeys = nullptr)
	{
		const FAreaValidationResult& V = Gen->GetLastValidation();
		FString Out;
		for (const FAreaRuleResult& Rule : V.Rules)
		{
			if (!Rule.bPass)
			{
				const FString Key = FString::Printf(TEXT("rule %d %s"), Rule.Rule, *Rule.Name);
				if (OutKeys) { OutKeys->Add(Key); }
				Out += FString::Printf(TEXT(" [rule %d: %s%s]"), Rule.Rule, *Rule.Summary, Rule.Details.Num() > 0 ? *FString::Printf(TEXT("; first: %s"), *Rule.Details[0]) : TEXT(""));
			}
		}
		if (!V.FatalError.IsEmpty())
		{
			if (OutKeys) { OutKeys->Add(TEXT("fatal")); }
			Out += FString::Printf(TEXT(" [fatal: %s]"), *V.FatalError);
		}
		return Out;
	}

	// --- Seed sweep ---

	struct FSweep
	{
		bool bActive = false;
		TArray<int32> Seeds;
		int32 Next = 0;
		int32 OriginalSeed = 0;
		int32 Passed = 0;
		TMap<FString, int32> FailuresByRule;
		TArray<FString> Lines;
		double ReadySum = 0.0;
		double ReadyMax = 0.0;
	};
	static FSweep GSweep;

	static void SweepNext(AAreaGenerator* Gen);

	static void SweepRecord(AAreaGenerator* Gen, bool bPassed)
	{
		const int32 Seed = Gen->GetSeed();
		FString Failed;
		if (!bPassed)
		{
			TArray<FString> Keys;
			if (Gen->LastError.Contains(TEXT("validation failed")))
			{
				Failed = FailedRules(Gen, &Keys);
			}
			else
			{
				Keys = { Gen->LastError.Contains(TEXT("PCG did not")) ? TEXT("pcg") : TEXT("generation") };
				Failed = FString::Printf(TEXT(" [%s]"), *Gen->LastError);
			}
			for (const FString& Key : Keys)
			{
				GSweep.FailuresByRule.FindOrAdd(Key) += 1;
			}
		}
		GSweep.Passed += bPassed ? 1 : 0;
		GSweep.ReadySum += Gen->GetTimeToReadySeconds();
		GSweep.ReadyMax = FMath::Max(GSweep.ReadyMax, double(Gen->GetTimeToReadySeconds()));
		GSweep.Lines.Add(FString::Printf(TEXT("seed %d: %s, route %.0f m, %d PCG instances, ready in %.2f s (plan %.2f, PCG %.2f, navmesh %.2f, validation %.2f), hash %016llx%s"),
			Seed, bPassed ? TEXT("PASS") : TEXT("FAIL"), Gen->GetRouteLength() / 100.f, Gen->GetPCGInstanceCount(), Gen->GetTimeToReadySeconds(),
			Gen->LastGenerationSeconds, Gen->LastPCGSeconds, Gen->LastNavigationSeconds, Gen->LastValidationSeconds, static_cast<uint64>(Gen->GetLayoutHash()), *Failed));
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: %s"), *GSweep.Lines.Last());
		SweepNext(Gen);
	}

	static void SweepNext(AAreaGenerator* Gen)
	{
		if (GSweep.Next < GSweep.Seeds.Num())
		{
			const int32 Seed = GSweep.Seeds[GSweep.Next++];
			OnceFinished(Gen, [](AAreaGenerator* G, bool bPassed) { SweepRecord(G, bPassed); });
			Gen->Regenerate(Seed, /*bTryNextSeeds=*/false);
			return;
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: ===== act %d: %d of %d seeds pass all rules (time to ready mean %.2f s, max %.2f s) ====="),
			Gen->GetAct(), GSweep.Passed, GSweep.Seeds.Num(), GSweep.Seeds.Num() > 0 ? GSweep.ReadySum / GSweep.Seeds.Num() : 0.0, GSweep.ReadyMax);
		TArray<FString> Keys;
		GSweep.FailuresByRule.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: failures by rule: %s: %d"), *Key, GSweep.FailuresByRule[Key]);
		}
		GSweep.bActive = false;
		const int32 Original = GSweep.OriginalSeed;
		OnceFinished(Gen, [Original](AAreaGenerator* G, bool bPassed)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: restored seed %d (%s), hash %016llx."), Original, bPassed ? TEXT("valid") : TEXT("INVALID"), static_cast<uint64>(G->GetLayoutHash()));
		});
		Gen->Regenerate(Original, /*bTryNextSeeds=*/false);
	}

	// --- Repeated generation (determinism, regeneration) ---

	struct FRepeatRun { int32 Seed = 0; int64 Hash = 0; int32 Instances = 0; int32 Generated = 0; int32 OrphansAfterClear = 0; bool bPassed = false; float Ready = 0.f; };
	struct FRepeat
	{
		bool bActive = false;
		FString Name;
		TArray<int32> Seeds;
		int32 Next = 0;
		TArray<FRepeatRun> Runs;
		TFunction<void(const TArray<FRepeatRun>&)> Summary;
	};
	static FRepeat GRepeat;

	static void RepeatNext(AAreaGenerator* Gen)
	{
		if (GRepeat.Next >= GRepeat.Seeds.Num())
		{
			GRepeat.bActive = false;
			GRepeat.Summary(GRepeat.Runs);
			return;
		}
		const int32 Seed = GRepeat.Seeds[GRepeat.Next++];
		// Clear first and count what is left: nothing generated may survive a clear.
		Gen->ClearArea();
		const int32 Orphans = CountGenerated(Gen->GetWorld()) + Gen->GetPCGInstanceCount();
		OnceFinished(Gen, [Seed, Orphans](AAreaGenerator* G, bool bPassed)
		{
			FRepeatRun Run;
			Run.Seed = Seed;
			Run.Hash = G->GetLayoutHash();
			Run.Instances = G->GetPCGInstanceCount();
			Run.Generated = CountGenerated(G->GetWorld());
			Run.OrphansAfterClear = Orphans;
			Run.bPassed = bPassed;
			Run.Ready = G->GetTimeToReadySeconds();
			GRepeat.Runs.Add(Run);
			UE_LOG(LogArea, Display, TEXT("%s: run %d: seed %d %s, hash %016llx, %d PCG instances, %d generated actors, left after clear %d, ready in %.2f s"),
				*GRepeat.Name, GRepeat.Runs.Num(), Seed, bPassed ? TEXT("valid") : TEXT("INVALID"), static_cast<uint64>(Run.Hash), Run.Instances, Run.Generated, Orphans, Run.Ready);
			RepeatNext(G);
		});
		Gen->Regenerate(Seed, /*bTryNextSeeds=*/false);
	}

	static void StartRepeat(AAreaGenerator* Gen, const FString& Name, const TArray<int32>& Seeds, TFunction<void(const TArray<FRepeatRun>&)> Summary)
	{
		GRepeat = FRepeat();
		GRepeat.bActive = true;
		GRepeat.Name = Name;
		GRepeat.Seeds = Seeds;
		GRepeat.Summary = MoveTemp(Summary);
		RepeatNext(Gen);
	}

	static bool AnyTestRunning(const TCHAR* Command)
	{
		if (GSweep.bActive || GRepeat.bActive)
		{
			UE_LOG(LogArea, Warning, TEXT("%s: a sweep or repeat test is already running."), Command);
			return true;
		}
		return false;
	}

	// --- Report ---

	static void LogReport(UWorld* World)
	{
		AAreaGenerator* Gen = AAreaGenerator::Find(World);
		AAreaInfo* Info = AAreaInfo::Find(World);
		if (!Gen || !Info)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaReport: needs a generated area (generator and area info)."));
			return;
		}
		const FAreaPawnMetrics Pawn = GetAreaPawnMetrics(World);
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		double PathLength = -1.0;
		APlayerStart* Start = nullptr;
		AAreaExit* Exit = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Start = *It; break; }
		for (TActorIterator<AAreaExit> It(World); It; ++It) { Exit = *It; break; }
		if (NavSys && Start && Exit)
		{
			FNavLocation A;
			FNavLocation B;
			const FVector Extent(100.f, 100.f, 300.f);
			if (NavSys->ProjectPointToNavigation(Start->GetActorLocation(), A, Extent) && NavSys->ProjectPointToNavigation(Exit->GetActorLocation(), B, Extent))
			{
				NavSys->GetPathLength(A.Location, B.Location, PathLength);
			}
		}
		TMap<EAreaSegmentType, int32> ByType;
		int32 Spurs = 0;
		int32 Niches = 0;
		for (const FAreaSegmentRecord& S : Info->Segments)
		{
			ByType.FindOrAdd(S.Type) += 1;
			Spurs += S.SpursPlaced;
			Niches += S.NichesPlaced;
		}
		FString Types;
		for (const EAreaSegmentType T : { EAreaSegmentType::Corridor, EAreaSegmentType::Room, EAreaSegmentType::Catwalk, EAreaSegmentType::Hall, EAreaSegmentType::Gate })
		{
			Types += FString::Printf(TEXT("%s%s %d"), Types.IsEmpty() ? TEXT("") : TEXT(", "), LexToString(T), ByType.FindRef(T));
		}
		FString Instances;
		for (const EAreaPieceKind Kind : AllKinds)
		{
			Instances += FString::Printf(TEXT("%s%s %d (%d components)"), Instances.IsEmpty() ? TEXT("") : TEXT(", "), LexToString(Kind), Gen->GetPCGInstanceCountOf(Kind), Gen->GetPCGComponents(Kind).Num());
		}
		int32 Tiles = 0;
		if (const ARecastNavMesh* Nav = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr)
		{
			Tiles = Nav->GetNumActiveTiles();
		}
		FString Why;
		const bool bMatch = Gen->DoesPCGMatchPlan(Why);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: act %d, map %s, state %d, seed %d (%d tried), hash %016llx, %d hashed items, %d generated actors"),
			Gen->GetAct(), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), static_cast<int32>(Gen->GetGenerationState()), Gen->GetSeed(), Info->SeedsTried,
			static_cast<uint64>(Gen->GetLayoutHash()), Gen->LastActorCount, CountGenerated(World));
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: route centreline %.1f m, navigation path start->exit %.1f m, expected walk %.0f s at %.0f cm/s (%s)"),
			Info->GetRouteLength() / 100.f, PathLength / 100.0, PathLength > 0.0 ? PathLength / Pawn.WalkSpeed : -1.0, Pawn.WalkSpeed, *Pawn.Source);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: segments: %s; spurs/dead ends %d, niches %d"), *Types, Spurs, Niches);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: PCG instances %d: %s; PCG matches the plan: %s%s"), Gen->GetPCGInstanceCount(), *Instances,
			bMatch ? TEXT("yes") : TEXT("NO"), bMatch ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Why));
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: time to ready %.2f s (last attempt: plan %.3f s, PCG %.3f s, navmesh %.3f s, validation %.3f s), %d active navmesh tiles"),
			Gen->GetTimeToReadySeconds(), Gen->LastGenerationSeconds, Gen->LastPCGSeconds, Gen->LastNavigationSeconds, Gen->LastValidationSeconds, Tiles);
		for (int32 Index = 0; Index < Info->Segments.Num(); ++Index)
		{
			const FAreaSegmentRecord& S = Info->Segments[Index];
			UE_LOG(LogArea, Display, TEXT("tls.AreaReport:   segment %d %s: route %.1f-%.1f m, bounds %.0f x %.0f m, spurs %d, niches %d"), Index, LexToString(S.Type),
				S.RouteStart / 100.f, S.RouteEnd / 100.f, S.Bounds.GetSize().X / 100.f, S.Bounds.GetSize().Y / 100.f, S.SpursPlaced, S.NichesPlaced);
		}
	}

	/** Route distance of a named pose: start, corridor (10 m in), room, hall (the hall's centre), gate, exit, or metres. */
	static bool PoseDistance(const AAreaInfo* Info, const FString& What, float& OutDistance)
	{
		if (What.IsNumeric())
		{
			OutDistance = FCString::Atof(*What) * 100.f;
			return true;
		}
		if (What == TEXT("start")) { OutDistance = 150.f; return true; }
		if (What == TEXT("corridor")) { OutDistance = 1000.f; return true; }
		if (What == TEXT("exit")) { OutDistance = Info->GetRouteLength() - 300.f; return true; }
		const EAreaSegmentType Type = What == TEXT("hall") ? EAreaSegmentType::Hall : What == TEXT("gate") ? EAreaSegmentType::Gate
			: What == TEXT("catwalk") ? EAreaSegmentType::Catwalk : EAreaSegmentType::Room;
		const int32 Index = Info->FindSegmentOfType(Type);
		if (Index < 0)
		{
			return false;
		}
		const FAreaSegmentRecord& S = Info->Segments[Index];
		if (Type != EAreaSegmentType::Hall)
		{
			OutDistance = (S.RouteStart + S.RouteEnd) * 0.5f;
			return true;
		}
		// The route point of the hall closest to the hall's centre.
		const FVector Center = S.Bounds.GetCenter();
		float Best = TNumericLimits<float>::Max();
		for (float D = S.RouteStart; D <= S.RouteEnd; D += 100.f)
		{
			int32 Segment = -1;
			const float Dist = static_cast<float>(FVector::Dist2D(Info->GetRoutePointAtDistance(D, Segment), Center));
			if (Dist < Best)
			{
				Best = Dist;
				OutDistance = D;
			}
		}
		return true;
	}

	static UInstancedStaticMeshComponent* FirstComponent(const AAreaGenerator* Gen, EAreaPieceKind Kind)
	{
		const TArray<UInstancedStaticMeshComponent*> Components = Gen->GetPCGComponents(Kind);
		return Components.Num() > 0 ? Components[0] : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaGenerateCommand(
	TEXT("tls.AreaGenerate"),
	TEXT("tls.AreaGenerate [Seed]: (PIE) clears and regenerates the area of the map's act at load-time (plan, PCG, navmesh, validation), with Seed or the current seed; does not try further seeds."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaGenerate"));
		if (!Gen || AreaDebug::IsBusy(Gen, TEXT("tls.AreaGenerate")) || AreaDebug::AnyTestRunning(TEXT("tls.AreaGenerate")))
		{
			return;
		}
		const int32 Seed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : Gen->GetSeed();
		AreaDebug::OnceFinished(Gen, [](AAreaGenerator* G, bool bPassed)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaGenerate: seed %d %s, hash %016llx, %d PCG instances, route %.1f m, ready in %.2f s%s"), G->GetSeed(),
				bPassed ? TEXT("valid") : TEXT("INVALID"), static_cast<uint64>(G->GetLayoutHash()), G->GetPCGInstanceCount(), G->GetRouteLength() / 100.f,
				G->GetTimeToReadySeconds(), bPassed ? TEXT("") : *FString::Printf(TEXT(": %s"), *G->LastError));
		});
		Gen->Regenerate(Seed, false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaClearCommand(
	TEXT("tls.AreaClear"),
	TEXT("tls.AreaClear: (PIE) removes every generated area actor and the PCG output."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		if (AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaClear")))
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaClear: removed %d actors; %d PCG instances left."), Gen->ClearArea(), Gen->GetPCGInstanceCount());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaValidateCommand(
	TEXT("tls.AreaValidate"),
	TEXT("tls.AreaValidate [Rebuild=0]: (PIE) checks the six area rules (path, reachability and counts, gates, hiding places, curbs, stray navmesh) on the world as it is; Rebuild=1 rebuilds the navmesh first."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaValidate"));
		if (!Gen || AreaDebug::IsBusy(Gen, TEXT("tls.AreaValidate")))
		{
			return;
		}
		UWorld* World = Gen->GetWorld();
		const AAreaInfo* Info = AAreaInfo::Find(World);
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate: %s, act %d, seed %d, hash %016llx"), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Gen->GetAct(),
			Info ? Info->Seed : -1, static_cast<uint64>(Gen->GetLayoutHash()));
		if (Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0)
		{
			AreaDebug::OnceFinished(Gen, [](AAreaGenerator* G, bool) { G->GetLastValidation().Log(TEXT("tls.AreaValidate:"), true); });
			Gen->RevalidateAfterNavigationRebuild();
			return;
		}
		AreaValidator::Validate(World).Log(TEXT("tls.AreaValidate:"), true);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaValidateSeedsCommand(
	TEXT("tls.AreaValidate.Seeds"),
	TEXT("tls.AreaValidate.Seeds [N=20] [FirstSeed=1]: (PIE) regenerates and validates seeds FirstSeed..FirstSeed+N-1 with the map's recipe at runtime, one after the other; reports the pass rate and the failures by rule, then regenerates the map's own seed."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaValidate.Seeds"));
		if (!Gen || AreaDebug::IsBusy(Gen, TEXT("tls.AreaValidate.Seeds")) || AreaDebug::AnyTestRunning(TEXT("tls.AreaValidate.Seeds")))
		{
			return;
		}
		const int32 N = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 500) : 20;
		const int32 First = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1;
		AreaDebug::GSweep = AreaDebug::FSweep();
		AreaDebug::GSweep.bActive = true;
		AreaDebug::GSweep.OriginalSeed = Gen->GetSeed();
		for (int32 I = 0; I < N; ++I)
		{
			AreaDebug::GSweep.Seeds.Add(First + I);
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: act %d, %d seeds from %d (map seed %d)."), Gen->GetAct(), N, First, Gen->GetSeed());
		AreaDebug::SweepNext(Gen);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaReportCommand(
	TEXT("tls.AreaReport"),
	TEXT("tls.AreaReport: (PIE) act, map, seed, hash, route length, expected walking time, segments by type, PCG instance counts and whether they match the plan, time to ready and its parts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		if (AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaReport")))
		{
			AreaDebug::LogReport(Gen->GetWorld());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaRegenTestCommand(
	TEXT("tls.AreaRegenTest"),
	TEXT("tls.AreaRegenTest [Runs=3]: (PIE) clears and regenerates the current seed Runs times: nothing generated left after each clear, same hash, PCG instance count and actor count every time."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaRegenTest"));
		if (!Gen || AreaDebug::IsBusy(Gen, TEXT("tls.AreaRegenTest")) || AreaDebug::AnyTestRunning(TEXT("tls.AreaRegenTest")))
		{
			return;
		}
		const int32 Runs = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 50) : 3;
		TArray<int32> Seeds;
		Seeds.Init(Gen->GetSeed(), Runs);
		AreaDebug::StartRepeat(Gen, TEXT("tls.AreaRegenTest"), Seeds, [](const TArray<AreaDebug::FRepeatRun>& R)
		{
			int32 Problems = 0;
			for (const AreaDebug::FRepeatRun& Run : R)
			{
				Problems += (Run.OrphansAfterClear == 0 && Run.Hash == R[0].Hash && Run.Instances == R[0].Instances && Run.Generated == R[0].Generated) ? 0 : 1;
			}
			UE_LOG(LogArea, Display, TEXT("tls.AreaRegenTest: ===== %s: %d runs, %d with leftovers or a different hash, instance or actor count ====="),
				Problems == 0 ? TEXT("PASS") : TEXT("FAIL"), R.Num(), Problems);
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaDeterminismCommand(
	TEXT("tls.AreaDeterminism"),
	TEXT("tls.AreaDeterminism: (PIE) regenerates the current seed twice, seed+1 once and the seed again; compares layout hashes and PCG instance counts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaDeterminism"));
		if (!Gen || AreaDebug::IsBusy(Gen, TEXT("tls.AreaDeterminism")) || AreaDebug::AnyTestRunning(TEXT("tls.AreaDeterminism")))
		{
			return;
		}
		const int32 Seed = Gen->GetSeed();
		AreaDebug::StartRepeat(Gen, TEXT("tls.AreaDeterminism"), { Seed, Seed, Seed + 1, Seed }, [](const TArray<AreaDebug::FRepeatRun>& R)
		{
			const bool bSame = R.Num() == 4 && R[0].Hash == R[1].Hash && R[0].Hash == R[3].Hash && R[0].Instances == R[1].Instances && R[0].Instances == R[3].Instances;
			const bool bDiffers = R.Num() == 4 && R[2].Hash != R[0].Hash;
			UE_LOG(LogArea, Display, TEXT("tls.AreaDeterminism: same seed -> same hash and PCG instance count: %s; seed+1 -> different hash: %s"),
				bSame ? TEXT("PASS") : TEXT("FAIL"), bDiffers ? TEXT("PASS") : TEXT("FAIL"));
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaBreakCommand(
	TEXT("tls.AreaBreak"),
	TEXT("tls.AreaBreak path|residue|terminal|hiding|curb|navmesh|mass: (PIE) breaks one validator rule on purpose (exit into the void, a residue off the navmesh, the gate terminal 3 m from its door, walls and pillars removed, one curb removed, a stray floor slab, a solid PCG mass across the route), rebuilds the navmesh and validates. tls.AreaGenerate afterwards restores the area."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaBreak"));
		UWorld* World = Gen ? Gen->GetWorld() : nullptr;
		AAreaInfo* Info = AAreaInfo::Find(World);
		if (!Gen || !Info || Args.Num() == 0 || AreaDebug::IsBusy(Gen, TEXT("tls.AreaBreak")))
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaBreak: needs a generated area and a rule name."));
			return;
		}
		const FString What = Args[0].ToLower();
		auto OffFloor = [Info](const FVector& From)
		{
			for (const float R : { 3000.f, 5000.f, 8000.f })
			{
				for (const FVector Dir : { FVector(0, 1, 0), FVector(0, -1, 0), FVector(1, 0, 0), FVector(-1, 0, 0) })
				{
					const FVector P = From + Dir * R;
					bool bClear = true;
					for (const FAreaFloorRect& Rect : Info->FloorRects)
					{
						bClear &= Rect.Box.ExpandBy(FVector(800.f, 800.f, 0.f)).IsInsideXY(P) == false;
					}
					if (bClear) { return P; }
				}
			}
			return From + FVector(0.f, 0.f, 3000.f);
		};
		FString Done;
		if (What == TEXT("path"))
		{
			for (TActorIterator<AAreaExit> It(World); It; ++It)
			{
				const FVector P = OffFloor(It->GetActorLocation());
				It->SetActorLocation(P);
				Done = FString::Printf(TEXT("moved the exit into the void at (%.0f, %.0f)"), P.X, P.Y);
				break;
			}
		}
		else if (What == TEXT("residue"))
		{
			TArray<AResidue*> List;
			for (TActorIterator<AResidue> It(World); It; ++It) { if (!It->bRegenerates) { List.Add(*It); } }
			List.Sort([](const AResidue& A, const AResidue& B) { return A.GetName() < B.GetName(); });
			if (List.Num() > 0)
			{
				const FVector P = OffFloor(List[0]->GetActorLocation());
				List[0]->SetActorLocation(P);
				Done = FString::Printf(TEXT("moved residue %s off the navmesh to (%.0f, %.0f, %.0f)"), *List[0]->GetName(), P.X, P.Y, P.Z);
			}
		}
		else if (What == TEXT("terminal"))
		{
			for (TActorIterator<ADoorDevice> It(World); It; ++It)
			{
				for (TActorIterator<APowerTerminal> T(World); T; ++T)
				{
					if (T->Targets.Contains(*It))
					{
						const FVector Back = It->GetActorTransform().TransformPosition(FVector(-300.f, 120.f, 0.f));
						T->SetActorLocation(Back);
						T->SetActorRotation((It->GetActorLocation() - Back).GetSafeNormal2D().Rotation());
						Done = FString::Printf(TEXT("moved %s 3 m from %s (door open %.1f s)"), *T->GetName(), *It->GetName(), It->PoweredSeconds);
						break;
					}
				}
				break;
			}
		}
		else if (What == TEXT("hiding"))
		{
			int32 Removed = 0;
			for (const EAreaPieceKind Kind : { EAreaPieceKind::Walls, EAreaPieceKind::Pillars })
			{
				for (UInstancedStaticMeshComponent* Component : Gen->GetPCGComponents(Kind))
				{
					Removed += Component->GetInstanceCount();
					Component->ClearInstances();
				}
			}
			Done = FString::Printf(TEXT("removed %d PCG wall and pillar instances"), Removed);
		}
		else if (What == TEXT("curb"))
		{
			const int32 Hall = Info->FindSegmentOfType(EAreaSegmentType::Hall);
			const int32 Catwalk = Info->FindSegmentOfType(EAreaSegmentType::Catwalk);
			const int32 Open = Hall >= 0 ? Hall : Catwalk;
			const FVector Target = Open >= 0 ? Info->Segments[Open].Bounds.GetCenter() : Info->RoutePoints[Info->RoutePoints.Num() / 2];
			UInstancedStaticMeshComponent* Curbs = AreaDebug::FirstComponent(Gen, EAreaPieceKind::Curbs);
			int32 Best = -1;
			double BestDist = TNumericLimits<double>::Max();
			for (int32 I = 0; Curbs && I < Curbs->GetInstanceCount(); ++I)
			{
				FTransform T;
				Curbs->GetInstanceTransform(I, T, true);
				const double D = FVector::Dist2D(T.GetLocation(), Target);
				if (D < BestDist) { BestDist = D; Best = I; }
			}
			if (Best >= 0)
			{
				FTransform T;
				Curbs->GetInstanceTransform(Best, T, true);
				Curbs->RemoveInstance(Best);
				Done = FString::Printf(TEXT("removed PCG curb instance %d at (%.0f, %.0f), %.0f cm long"), Best, T.GetLocation().X, T.GetLocation().Y, FMath::Max(T.GetScale3D().X, T.GetScale3D().Y) * 100.f);
			}
		}
		else if (What == TEXT("navmesh"))
		{
			const int32 Hall = Info->FindSegmentOfType(EAreaSegmentType::Hall);
			const FVector Center = Hall >= 0 ? Info->Segments[Hall].Bounds.GetCenter() : Info->RoutePoints[0];
			FVector Spot = Center;
			for (float R = 0.f; R < 20000.f; R += 500.f)
			{
				bool bFound = false;
				for (const FVector Dir : { FVector(1, 1, 0), FVector(1, -1, 0), FVector(-1, 1, 0), FVector(-1, -1, 0) })
				{
					const FVector P = Center + Dir * R;
					bool bClear = true;
					for (const FAreaFloorRect& Rect : Info->FloorRects)
					{
						bClear &= !Rect.Box.ExpandBy(FVector(700.f, 700.f, 0.f)).IsInsideXY(P);
					}
					if (bClear) { Spot = P; bFound = true; break; }
				}
				if (bFound) { break; }
			}
			if (UInstancedStaticMeshComponent* Floors = AreaDebug::FirstComponent(Gen, EAreaPieceKind::Floors))
			{
				Floors->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Spot.X, Spot.Y, -5.f), FVector(6.f, 6.f, 0.1f)), /*bWorldSpace=*/true);
				Done = FString::Printf(TEXT("added a stray 6 x 6 m PCG floor slab at (%.0f, %.0f)"), Spot.X, Spot.Y);
			}
		}
		else if (What == TEXT("mass"))
		{
			// A solid block across the first corridor, 10 m in: as if a graph had placed a mass on the route.
			const int32 Corridor = Info->FindSegmentOfType(EAreaSegmentType::Corridor);
			const float At = Corridor >= 0 ? Info->Segments[Corridor].RouteStart + 1000.f : 1000.f;
			int32 Segment = -1;
			const FVector P = Info->GetRoutePointAtDistance(At, Segment);
			const FVector Dir = Info->GetRouteDirectionAtDistance(At);
			if (UInstancedStaticMeshComponent* Pillars = AreaDebug::FirstComponent(Gen, EAreaPieceKind::Pillars))
			{
				const FRotator Yaw = Dir.Rotation();
				Pillars->AddInstance(FTransform(Yaw, FVector(P.X, P.Y, Gen->WallHeight * 0.5f - Gen->FloorThickness * 0.5f),
					FVector(4.f, 12.f, (Gen->WallHeight + Gen->FloorThickness) / 100.f)), /*bWorldSpace=*/true);
				Done = FString::Printf(TEXT("added a solid 4 x 12 m PCG mass across the route at %.0f m (%.0f, %.0f)"), At / 100.f, P.X, P.Y);
			}
			else
			{
				Done = TEXT("no PCG pillar component to add the mass to (no rooms in this area)");
			}
		}
		if (Done.IsEmpty())
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaBreak: unknown rule '%s' (path|residue|terminal|hiding|curb|navmesh|mass), or nothing to break."), *What);
			return;
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaBreak %s: %s; rebuilding the navmesh and validating."), *What, *Done);
		AreaDebug::OnceFinished(Gen, [What](AAreaGenerator* G, bool bPassed)
		{
			G->GetLastValidation().Log(*FString::Printf(TEXT("tls.AreaBreak %s:"), *What), true);
			UE_LOG(LogArea, Display, TEXT("tls.AreaBreak %s: ===== %s (navmesh %.2f s) ====="), *What, bPassed ? TEXT("NOT CAUGHT: all rules pass") : TEXT("CAUGHT"), G->LastNavigationSeconds);
		});
		Gen->RevalidateAfterNavigationRebuild();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaMapCommand(
	TEXT("tls.AreaMap"),
	TEXT("tls.AreaMap: (PIE) writes Saved/AreaMap.json: floors, walls, curbs, pillars, backdrop footprints (from the PCG output), route, gameplay actors and navmesh polygons (connected to the start or not), for a top-down map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaMap"));
		UWorld* World = Gen ? Gen->GetWorld() : nullptr;
		const AAreaInfo* Info = AAreaInfo::Find(World);
		if (!Info)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaMap: needs a generated area."));
			return;
		}
		FString Json = FString::Printf(TEXT("{\"act\":%d,\"map\":\"%s\",\"seed\":%d,\"hash\":\"%016llx\",\"floorZ\":%.1f,\"pcgInstances\":%d,\n"), Gen->GetAct(),
			*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Info->Seed, static_cast<uint64>(Gen->GetLayoutHash()), Info->FloorZ, Gen->GetPCGInstanceCount());
		auto Box = [](const FBox& B) { return FString::Printf(TEXT("[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]"), B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z); };
		auto Instances = [&](EAreaPieceKind Kind)
		{
			TArray<FString> Items;
			for (const UInstancedStaticMeshComponent* Component : Gen->GetPCGComponents(Kind))
			{
				for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
				{
					FTransform T;
					Component->GetInstanceTransform(I, T, true);
					const FVector Half = T.GetScale3D().GetAbs() * 50.f;
					Items.Add(Box(FBox(T.GetLocation() - Half, T.GetLocation() + Half)));
				}
			}
			return FString::Join(Items, TEXT(","));
		};
		TArray<FString> Floors;
		for (const FAreaFloorRect& R : Info->FloorRects)
		{
			Floors.Add(FString::Printf(TEXT("{\"box\":%s,\"seg\":%d,\"open\":%d}"), *Box(R.Box), R.Segment, R.bOpen ? 1 : 0));
		}
		Json += FString::Printf(TEXT("\"floors\":[%s],\n"), *FString::Join(Floors, TEXT(",")));
		Json += FString::Printf(TEXT("\"walls\":[%s],\n\"curbs\":[%s],\n\"pillars\":[%s],\n\"backdrop\":[%s],\n"),
			*Instances(EAreaPieceKind::Walls), *Instances(EAreaPieceKind::Curbs), *Instances(EAreaPieceKind::Pillars), *Instances(EAreaPieceKind::Backdrop));
		TArray<FString> Route;
		for (int32 I = 0; I < Info->RoutePoints.Num(); ++I)
		{
			Route.Add(FString::Printf(TEXT("[%.0f,%.0f,%d]"), Info->RoutePoints[I].X, Info->RoutePoints[I].Y, Info->RoutePointSegments[I]));
		}
		Json += FString::Printf(TEXT("\"route\":[%s],\n"), *FString::Join(Route, TEXT(",")));
		TArray<FString> Actors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const bool bListed = It->ActorHasTag(AAreaInfo::GeneratedTag) || It->IsA<APlayerStart>() || It->IsA<AEntity>();
			if (!bListed || It->IsA<AAreaInfo>())
			{
				continue;
			}
			const FVector L = It->GetActorLocation();
			const FVector F = It->GetActorForwardVector();
			Actors.Add(FString::Printf(TEXT("{\"label\":\"%s\",\"class\":\"%s\",\"pos\":[%.0f,%.0f,%.0f],\"fwd\":[%.2f,%.2f]}"),
				*It->GetName(), *It->GetClass()->GetName(), L.X, L.Y, L.Z, F.X, F.Y));
		}
		Json += FString::Printf(TEXT("\"actors\":[%s],\n"), *FString::Join(Actors, TEXT(",")));

		// Navmesh polygons, flagged when not connected to the PlayerStart's polygon.
		TArray<FString> Polys;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ARecastNavMesh* Nav = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
		APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Start = *It; break; }
		if (Nav && Start)
		{
			TSet<NavNodeRef> Reach;
			const NavNodeRef First = Nav->FindNearestPoly(Start->GetActorLocation(), FVector(100.f, 100.f, 300.f));
			TArray<NavNodeRef> Queue;
			if (First != INVALID_NAVNODEREF) { Queue.Add(First); Reach.Add(First); }
			TArray<NavNodeRef> Neighbors;
			for (int32 I = 0; I < Queue.Num(); ++I)
			{
				Neighbors.Reset();
				Nav->GetPolyNeighbors(Queue[I], Neighbors);
				for (const NavNodeRef N : Neighbors) { if (!Reach.Contains(N)) { Reach.Add(N); Queue.Add(N); } }
			}
			TArray<FNavTileRef> Tiles;
			Nav->GetAllNavMeshTiles(Tiles);
			TArray<FNavPoly> TilePolys;
			for (const FNavTileRef& Tile : Tiles)
			{
				TilePolys.Reset();
				Nav->GetPolysInTile(Tile, TilePolys);
				for (const FNavPoly& Poly : TilePolys)
				{
					TArray<FVector> Verts;
					Nav->GetPolyVerts(Poly.Ref, Verts);
					TArray<FString> V;
					for (const FVector& P : Verts) { V.Add(FString::Printf(TEXT("[%.0f,%.0f]"), P.X, P.Y)); }
					Polys.Add(FString::Printf(TEXT("{\"c\":%d,\"v\":[%s]}"), Reach.Contains(Poly.Ref) ? 1 : 0, *FString::Join(V, TEXT(","))));
				}
			}
		}
		Json += FString::Printf(TEXT("\"navpolys\":[%s]}\n"), *FString::Join(Polys, TEXT(",\n")));
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AreaMap.json");
		FFileHelper::SaveStringToFile(Json, *Path);
		UE_LOG(LogArea, Display, TEXT("tls.AreaMap: wrote %s (%d floors, %d navmesh polygons)."), *FPaths::ConvertRelativePathToFull(Path), Floors.Num(), Polys.Num());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaHashCommand(
	TEXT("tls.AreaHash"),
	TEXT("tls.AreaHash [Dump=0]: (PIE) recomputes the layout hash and compares it with the one stored at ready; also says whether PCG built exactly the plan. Dump=1 writes the hashed lines to Saved/AreaHashLines.txt."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(InWorld, TEXT("tls.AreaHash"));
		if (!Gen)
		{
			return;
		}
		int32 Lines = 0;
		TArray<FString> Text;
		const int64 Hash = Gen->ComputeLayoutHash(Lines, &Text);
		if (Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("AreaHashLines.txt");
			FFileHelper::SaveStringToFile(FString::Join(Text, LINE_TERMINATOR) + LINE_TERMINATOR, *Path);
			UE_LOG(LogArea, Display, TEXT("tls.AreaHash: wrote %d lines to %s."), Text.Num(), *FPaths::ConvertRelativePathToFull(Path));
		}
		FString Why;
		const bool bMatch = Gen->DoesPCGMatchPlan(Why);
		UE_LOG(LogArea, Display, TEXT("tls.AreaHash: recomputed %016llx (%d items), stored %016llx: %s; PCG matches the plan: %s%s"),
			static_cast<uint64>(Hash), Lines, static_cast<uint64>(Gen->GetLayoutHash()), Hash == Gen->GetLayoutHash() ? TEXT("SAME") : TEXT("DIFFERENT"),
			bMatch ? TEXT("yes") : TEXT("NO"), bMatch ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Why));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaGoCommand(
	TEXT("tls.AreaGo"),
	TEXT("tls.AreaGo start|corridor|room|gate|catwalk|hall|exit|Metres [YawOffset=0]: (PIE) puts the pawn on the route centreline at that pose, facing along the route plus YawOffset degrees."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		const AAreaInfo* Info = AAreaInfo::Find(World);
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
		float Distance = 0.f;
		const bool bPose = Info && Args.Num() > 0 && AreaDebug::PoseDistance(Info, Args[0].ToLower(), Distance);
		if (!Character || !bPose)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaGo: needs PIE, an area info and a pose."));
			return;
		}
		int32 Segment = -1;
		const FVector P = Info->GetRoutePointAtDistance(Distance, Segment);
		const FVector Dir = Info->GetRouteDirectionAtDistance(Distance);
		const float Yaw = static_cast<float>(Dir.Rotation().Yaw) + (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f);
		const FVector Location = P + FVector(0.f, 0.f, Character->GetDefaultHalfHeight() + 2.f);
		Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		Controller->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		UE_LOG(LogArea, Display, TEXT("tls.AreaGo: %s = %.1f m along the route (segment %d %s) at (%.0f, %.0f, %.0f), yaw %.0f."), *Args[0], Distance / 100.f, Segment,
			Info->Segments.IsValidIndex(Segment) ? LexToString(Info->Segments[Segment].Type) : TEXT("-"), Location.X, Location.Y, Location.Z, Yaw);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTravelNextCommand(
	TEXT("tls.Travel.Next"),
	TEXT("tls.Travel.Next: (PIE) travels to the next area as the exit would (after the last area: the main menu). Refused while Stopped."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		URunSubsystem* Run = URunSubsystem::Get(World);
		if (!Run)
		{
			UE_LOG(LogRun, Warning, TEXT("tls.Travel.Next: needs a game world."));
			return;
		}
		UE_LOG(LogRun, Display, TEXT("tls.Travel.Next: from act %d: %s."), Run->GetActIndex(), Run->TravelToNextArea() ? TEXT("travelling") : TEXT("refused"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTravelToCommand(
	TEXT("tls.Travel.To"),
	TEXT("tls.Travel.To <1-4>: (PIE) travels to the area of that act. Refused while Stopped."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		URunSubsystem* Run = URunSubsystem::Get(World);
		if (!Run || Args.Num() == 0)
		{
			UE_LOG(LogRun, Warning, TEXT("tls.Travel.To: needs a game world and an act (1-4)."));
			return;
		}
		const int32 Act = FCString::Atoi(*Args[0]);
		UE_LOG(LogRun, Display, TEXT("tls.Travel.To %d: from act %d: %s."), Act, Run->GetActIndex(), Run->TravelToArea(Act) ? TEXT("travelling") : TEXT("refused"));
	}));

#endif // !UE_BUILD_SHIPPING
