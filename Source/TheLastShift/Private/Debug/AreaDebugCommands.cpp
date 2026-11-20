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
#include "Engine/StaticMesh.h"
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
#include "UObject/UObjectIterator.h"

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

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaProbeCommand(
	TEXT("tls.Area.Probe"),
	TEXT("tls.Area.Probe <X0> <Y0> <Z0> <X1> <Y1> <Z1>: traces the segment on the Visibility and Pawn channels in the game world and logs every blocking hit (component, its tags, its collision and render flags, distance)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
		}
		if (!World || Args.Num() < 6)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.Area.Probe <X0> <Y0> <Z0> <X1> <Y1> <Z1> (in a game world)"));
			return;
		}
		const FVector A(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		const FVector B(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
		for (const ECollisionChannel Channel : { ECC_Visibility, ECC_Pawn })
		{
			TArray<FHitResult> Hits;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsAreaProbe), false);
			World->LineTraceMultiByChannel(Hits, A, B, Channel, Params);
			UE_LOG(LogArea, Display, TEXT("tls.Area.Probe %s -> %s on %s: %d hits"), *A.ToString(), *B.ToString(), Channel == ECC_Visibility ? TEXT("Visibility") : TEXT("Pawn"), Hits.Num());
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* C = Hit.GetComponent();
				FString Tags;
				if (C) { for (const FName& T : C->ComponentTags) { Tags += T.ToString() + TEXT(" "); } }
				UE_LOG(LogArea, Display, TEXT("  %s hit %s (%s) tags [%s] at %s, distance %.1f, item %d; collision %d, profile %s, response %d, visible %d, main pass %d"),
					Hit.bBlockingHit ? TEXT("blocking") : TEXT("touch"), *GetNameSafe(C), C ? *C->GetClass()->GetName() : TEXT("-"), *Tags, *Hit.ImpactPoint.ToString(), Hit.Distance, Hit.Item,
					C ? static_cast<int32>(C->GetCollisionEnabled()) : -1, C ? *C->GetCollisionProfileName().ToString() : TEXT("-"), C ? static_cast<int32>(C->GetCollisionResponseToChannel(Channel)) : -1,
					C ? (C->IsVisible() ? 1 : 0) : -1, C ? (C->bRenderInMainPass ? 1 : 0) : -1);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaDressCheckCommand(
	TEXT("tls.Area.DressCheck"),
	TEXT("tls.Area.DressCheck: measures the dressing as placed (its instances and their meshes' bounds) against the plan's boxes: floor plate tops against the floor's top, wall panel and curb faces against their boxes' faces, curb tops against CurbHeight; counts dressing pieces inside the walkable space (over a floor, from 1 cm to 2.2 m above it)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
		}
		AAreaGenerator* Gen = World ? AAreaGenerator::Find(World) : nullptr;
		const AAreaInfo* Info = World ? AAreaInfo::Find(World) : nullptr;
		if (!Gen || !Info)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.Area.DressCheck: no area in a game world."));
			return;
		}
		auto Boxes = [](const TArray<FTransform>& A, const TArray<FTransform>& B)
		{
			TArray<FBox> Out;
			for (const TArray<FTransform>* L : { &A, &B })
			{
				for (const FTransform& T : *L)
				{
					const FVector Half = T.GetScale3D().GetAbs() * 50.0;
					Out.Add(FBox(T.GetLocation() - Half, T.GetLocation() + Half));
				}
			}
			return Out;
		};
		const TArray<FBox> Walls = Boxes(Gen->PlanWalls, Gen->PlanWallsCollision);
		const TArray<FBox> Curbs = Boxes(Gen->PlanCurbs, Gen->PlanCurbsCollision);
		const TArray<FBox> Pillars = Boxes(Gen->PlanPillars, Gen->PlanPillarsCollision);
		TArray<FBox> WallLike = Walls;
		WallLike.Append(Pillars);
		auto Containing = [](const TArray<FBox>& List, const FVector& P) -> const FBox*
		{
			for (const FBox& B : List)
			{
				if (P.X >= B.Min.X - 0.5 && P.X <= B.Max.X + 0.5 && P.Y >= B.Min.Y - 0.5 && P.Y <= B.Max.Y + 0.5) { return &B; }
			}
			return nullptr;
		};
		double FloorTop = 0.0, WallFace = 0.0, CurbFace = 0.0, CurbTop = 0.0;
		int32 Plates = 0, Panels = 0, CurbsN = 0, Unmatched = 0, Intrusions = 0, Pieces = 0;
		FString Worst;
		double WorstIntrusion = 0.0;
		TArray<UInstancedStaticMeshComponent*> All;
		Gen->GetComponents(All);
		for (const UInstancedStaticMeshComponent* C : All)
		{
			if (!C || !C->ComponentHasTag(TEXT("AreaDressing")) || !C->GetStaticMesh())
			{
				continue;
			}
			const FBox Local = C->GetStaticMesh()->GetBoundingBox();
			const bool bPlates = C->ComponentHasTag(TEXT("AreaDressFloorPlates"));
			const bool bPanels = C->ComponentHasTag(TEXT("AreaDressWallPanels"));
			const bool bCurbs = C->ComponentHasTag(TEXT("AreaDressCurbs"));
			for (int32 I = 0; I < C->GetInstanceCount(); ++I)
			{
				FTransform T;
				C->GetInstanceTransform(I, T, true);
				const FBox B = Local.TransformBy(T);
				++Pieces;
				if (bPlates && B.Max.Z < 50.0)
				{
					++Plates;
					FloorTop = FMath::Max(FloorTop, FMath::Abs(B.Max.Z - Info->FloorZ));
				}
				if (bPanels || bCurbs)
				{
					const FBox* Box = Containing(bPanels ? WallLike : Curbs, B.GetCenter());
					if (!Box)
					{
						++Unmatched;
					}
					else
					{
						// The piece's thin axis. Against a wall or curb box (as thin as the piece) both faces must match; against a
						// pillar the piece lines one face, so the nearer of the two.
						const bool bThinX = (B.Max.X - B.Min.X) < (B.Max.Y - B.Min.Y);
						const double D0 = bThinX ? FMath::Abs(B.Min.X - Box->Min.X) : FMath::Abs(B.Min.Y - Box->Min.Y);
						const double D1 = bThinX ? FMath::Abs(B.Max.X - Box->Max.X) : FMath::Abs(B.Max.Y - Box->Max.Y);
						const double BoxThin = bThinX ? Box->Max.X - Box->Min.X : Box->Max.Y - Box->Min.Y;
						const double D = BoxThin <= Gen->WallThickness + 1.0 ? FMath::Max(D0, D1) : FMath::Min(D0, D1);
						if (bPanels) { ++Panels; WallFace = FMath::Max(WallFace, D); }
						else { ++CurbsN; CurbFace = FMath::Max(CurbFace, D); CurbTop = FMath::Max(CurbTop, FMath::Abs(B.Max.Z - Gen->CurbHeight)); }
					}
				}
				// Inside the walkable space: over a floor rectangle (less 1 cm), from 1 cm to 2.2 m above it. A gate frame's
				// bounds span its doorway (its jambs stand outside the walls' faces, its head above 3 m): it is left out.
				if (C->ComponentHasTag(TEXT("AreaDressGateFrames")))
				{
					continue;
				}
				for (const FAreaFloorRect& R : Info->FloorRects)
				{
					const FBox Walk(FVector(R.Box.Min.X + 1.0, R.Box.Min.Y + 1.0, R.Box.Max.Z + 1.0), FVector(R.Box.Max.X - 1.0, R.Box.Max.Y - 1.0, R.Box.Max.Z + 220.0));
					if (B.Intersect(Walk))
					{
						const FBox O = B.Overlap(Walk);
						const double Depth = FMath::Min3(O.GetSize().X, O.GetSize().Y, O.GetSize().Z);
						if (Depth > 0.5)
						{
							++Intrusions;
							if (Depth > WorstIntrusion)
							{
								WorstIntrusion = Depth;
								Worst = FString::Printf(TEXT("%s instance %d at %s, %.1f cm"), *C->GetName(), I, *B.GetCenter().ToString(), Depth);
							}
						}
					}
				}
			}
		}
		UE_LOG(LogArea, Display, TEXT("tls.Area.DressCheck: %d dressing pieces; floor plates %d, largest |top - floor| %.2f cm; wall panels %d, largest face distance from their box %.2f cm; curbs %d, largest face distance %.2f cm, largest |top - CurbHeight| %.2f cm; pieces not over a box %d; pieces inside the walkable space (deeper than 0.5 cm) %d%s"),
			Pieces, Plates, FloorTop, Panels, WallFace, CurbsN, CurbFace, CurbTop, Unmatched, Intrusions, Worst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", deepest %s"), *Worst));
	}));

namespace TerminalCheck
{
	struct FPart { const TCHAR* Name; FBox Box; };

	/** SM_TerminalBody's parts (mesh cm, from its Blender source): plinth, column, head (the head's slanted top as its box). */
	static const FPart BodyParts[] = {
		{ TEXT("plinth"), FBox(FVector(-20.0, -30.0, 0.0), FVector(20.0, 30.0, 8.0)) },
		{ TEXT("column"), FBox(FVector(-17.0, -17.0, 8.0), FVector(5.0, 17.0, 78.0)) },
		{ TEXT("head"), FBox(FVector(-20.0, -30.0, 78.0), FVector(20.0, 30.0, 120.0)) },
	};

	/** SM_KitWallPanel's parts (mesh cm, from its Blender source; +X is its face side, the slab's face at X 0). */
	static const FPart PanelParts[] = {
		{ TEXT("slab"), FBox(FVector(-10.0, -42.5, 0.0), FVector(0.0, 42.5, 350.0)) },
		{ TEXT("rib"), FBox(FVector(-10.0, -50.0, 0.0), FVector(10.0, -42.5, 350.0)) },
		{ TEXT("rib"), FBox(FVector(-10.0, 42.5, 0.0), FVector(10.0, 50.0, 350.0)) },
		{ TEXT("rail"), FBox(FVector(0.0, -42.5, 100.0), FVector(5.0, 42.5, 108.0)) },
		{ TEXT("louvre"), FBox(FVector(0.0, -30.0, 40.0), FVector(3.0, 30.0, 90.0)) },
		{ TEXT("louvre slat"), FBox(FVector(3.0, -30.0, 48.0), FVector(7.0, 30.0, 54.0)) },
		{ TEXT("louvre slat"), FBox(FVector(3.0, -30.0, 62.0), FVector(7.0, 30.0, 68.0)) },
		{ TEXT("louvre slat"), FBox(FVector(3.0, -30.0, 76.0), FVector(7.0, 30.0, 82.0)) },
		{ TEXT("upper frame"), FBox(FVector(0.0, -33.0, 175.0), FVector(5.0, 33.0, 255.0)) },
		{ TEXT("upper side rib"), FBox(FVector(0.0, -38.5, 185.0), FVector(8.0, -33.5, 245.0)) },
		{ TEXT("upper side rib"), FBox(FVector(0.0, 33.5, 185.0), FVector(8.0, 38.5, 245.0)) },
	};

	static FBox Shrunk(const FBox& B, double By) { return FBox(B.Min + FVector(By), B.Max - FVector(By)); }
}

static FAutoConsoleCommandWithWorldAndArgs GTlsTerminalCheckCommand(
	TEXT("tls.TerminalCheck"),
	TEXT("tls.TerminalCheck: (any map; the game world, else the editor's) for every power terminal showing the modelled body, logs every visible mesh part its body's parts (plinth, column, head) cut into by more than 0.5 cm: a wall panel's own parts (slab, ribs, rail, louvre, upper frame) from SM_KitWallPanel's source, any other mesh by its bounds; and the distance from its back to the wall panel's slab face behind it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		using namespace TerminalCheck;
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (!World && Context.World() && Context.WorldType == EWorldType::Editor) { World = Context.World(); }
		}
		if (!World)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.TerminalCheck: no world."));
			return;
		}
		int32 Terminals = 0, Clear = 0;
		for (TActorIterator<APowerTerminal> It(World); It; ++It)
		{
			const APowerTerminal* T = *It;
			const UStaticMeshComponent* Body = nullptr;
			TArray<UStaticMeshComponent*> Own;
			T->GetComponents(Own);
			for (const UStaticMeshComponent* C : Own)
			{
				Body = C && C->GetStaticMesh() && C->GetStaticMesh()->GetName() == TEXT("SM_TerminalBody") ? C : Body;
			}
			if (!T->bModelledBody || !Body)
			{
				continue;
			}
			++Terminals;
			const FTransform BodyT = Body->GetComponentTransform();
			FBox BodyBox(ForceInit);
			TArray<FBox> BodyBoxes;
			for (const FPart& P : BodyParts)
			{
				BodyBoxes.Add(Shrunk(P.Box.TransformBy(BodyT), 0.5));
				BodyBox += BodyBoxes.Last();
			}
			const FBox Near = BodyBox.ExpandBy(60.0);
			FString Hits;
			int32 HitCount = 0;
			double BackToFace = -1.0;
			const FVector Back = BodyT.TransformPosition(FVector(-20.0, 0.0, 60.0));
			const FVector BackDir = -BodyT.GetUnitAxis(EAxis::X);
			auto Test = [&](const FString& What, const FBox& Box)
			{
				for (int32 I = 0; I < BodyBoxes.Num(); ++I)
				{
					if (BodyBoxes[I].Intersect(Box))
					{
						const FVector O = BodyBoxes[I].Overlap(Box).GetSize();
						const double Depth = FMath::Min3(O.X, O.Y, O.Z) + 0.5;  // the 0.5 cm taken off the body's side
						++HitCount;
						Hits += FString::Printf(TEXT("%s%s into the %s by %.1f cm"), Hits.IsEmpty() ? TEXT("") : TEXT("; "), *What, BodyParts[I].Name, Depth);
					}
				}
			};
			for (TObjectIterator<UStaticMeshComponent> CIt; CIt; ++CIt)
			{
				const UStaticMeshComponent* C = *CIt;
				if (!C || C->GetWorld() != World || C->GetOwner() == T || !C->GetStaticMesh() || !C->IsVisible() || !C->bRenderInMainPass || !C->IsRegistered())
				{
					continue;
				}
				const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C);
				const int32 Count = ISM ? ISM->GetInstanceCount() : 1;
				const FBox Local = C->GetStaticMesh()->GetBoundingBox();
				const bool bPanel = C->GetStaticMesh()->GetName() == TEXT("SM_KitWallPanel");
				for (int32 I = 0; I < Count; ++I)
				{
					FTransform W = C->GetComponentTransform();
					if (ISM) { ISM->GetInstanceTransform(I, W, true); }
					if (!Local.TransformBy(W).Intersect(Near))
					{
						continue;
					}
					const FString Owner = FString::Printf(TEXT("%s %s%s"), C->GetOwner() ? *C->GetOwner()->GetActorNameOrLabel() : TEXT("-"), *C->GetStaticMesh()->GetName(),
						ISM ? *FString::Printf(TEXT(" #%d"), I) : TEXT(""));
					if (!bPanel)
					{
						Test(Owner, Local.TransformBy(W));
						continue;
					}
					for (const FPart& P : PanelParts)
					{
						Test(Owner + TEXT(" ") + P.Name, P.Box.TransformBy(W));
					}
					// The panel's slab face behind the terminal's back: its distance along the panel's face direction.
					const FVector FaceN = W.GetUnitAxis(EAxis::X);
					if (FVector::DotProduct(FaceN, BackDir) < -0.9)
					{
						const FVector InPanel = W.InverseTransformPosition(Back);
						const FVector HalfAlong = W.GetScale3D() * 50.0;
						if (InPanel.Y >= -50.0 && InPanel.Y <= 50.0 && InPanel.Z >= 0.0 && InPanel.Z <= 350.0 && HalfAlong.Y > 0.0)
						{
							const double D = FVector::DotProduct(Back - W.GetLocation(), FaceN);
							BackToFace = BackToFace < 0.0 ? D : FMath::Min(BackToFace, D);
						}
					}
				}
			}
			Clear += HitCount == 0 ? 1 : 0;
			UE_LOG(LogArea, Display, TEXT("tls.TerminalCheck: %s (%s) at %s yaw %.0f: %s%s"), *T->GetActorNameOrLabel(), *T->GetClass()->GetName(),
				*T->GetActorLocation().ToCompactString(), T->GetActorRotation().Yaw, HitCount == 0 ? TEXT("clear") : *FString::Printf(TEXT("%d contacts: %s"), HitCount, *Hits),
				BackToFace >= 0.0 ? *FString::Printf(TEXT("; its back %.1f cm from the wall panel's slab face"), BackToFace) : TEXT("; no wall panel behind it"));
		}
		UE_LOG(LogArea, Display, TEXT("tls.TerminalCheck: %d terminals with the modelled body in %s, %d clear"), Terminals, *World->GetMapName(), Clear);
	}));

#endif // !UE_BUILD_SHIPPING
