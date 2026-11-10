// SPDX-License-Identifier: MIT

// Debug console commands for generated areas: generate, clear, validate (also over many seeds), report, the
// determinism and regeneration checks, deliberate rule breaks, and a teleport along the route for PIE measurements.
// Generation and validation act on the editor world (not during PIE); tls.AreaGo acts on the PIE world.
// Editor builds only, compiled out of Shipping.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaGeometry.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Area/AreaValidator.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Residue/Residue.h"

#if WITH_EDITOR && !UE_BUILD_SHIPPING

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Editor.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "GameFramework/PlayerStart.h"

namespace AreaDebug
{
	static UWorld* EditorWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		if (GEditor->PlayWorld)
		{
			UE_LOG(LogArea, Warning, TEXT("Area commands act on the editor world: stop PIE first."));
			return nullptr;
		}
		return GEditor->GetEditorWorldContext().World();
	}

	static AAreaGenerator* Generator(UWorld* World)
	{
		if (World)
		{
			for (TActorIterator<AAreaGenerator> It(World); It; ++It)
			{
				return *It;
			}
		}
		UE_LOG(LogArea, Warning, TEXT("No AAreaGenerator in %s."), *GetNameSafe(World));
		return nullptr;
	}

	static int32 CountActors(UWorld* World, int32& OutGenerated)
	{
		int32 Total = 0;
		OutGenerated = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++Total;
			OutGenerated += It->ActorHasTag(AAreaInfo::GeneratedTag) ? 1 : 0;
		}
		return Total;
	}

	static bool GenerateSeed(AAreaGenerator* Gen, int32 Seed, FString& OutError)
	{
		Gen->Seed = Seed;
		return Gen->GenerateArea(OutError, true);
	}

	// --- Seed sweep ---

	struct FSweep
	{
		bool bActive = false;
		TWeakObjectPtr<AAreaGenerator> Gen;
		TArray<int32> Seeds;
		int32 Next = 0;
		int32 OriginalSeed = 0;
		int64 OriginalHash = 0;
		int32 Passed = 0;
		TMap<FString, int32> FailuresByRule;
		TArray<FString> Lines;
		double GenSecondsSum = 0.0;
		double GenSecondsMax = 0.0;
	};
	static FSweep GSweep;

	static bool TickSweep(float)
	{
		AAreaGenerator* Gen = GSweep.Gen.Get();
		if (!GSweep.bActive || !Gen)
		{
			GSweep.bActive = false;
			return false;
		}
		if (GSweep.Next < GSweep.Seeds.Num())
		{
			const int32 Seed = GSweep.Seeds[GSweep.Next++];
			FString Error;
			if (!GenerateSeed(Gen, Seed, Error))
			{
				GSweep.FailuresByRule.FindOrAdd(TEXT("generation")) += 1;
				GSweep.Lines.Add(FString::Printf(TEXT("seed %d: GENERATION FAILED: %s"), Seed, *Error));
				UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: seed %d generation failed: %s"), Seed, *Error);
				return true;
			}
			GSweep.GenSecondsSum += Gen->LastGenerationSeconds;
			GSweep.GenSecondsMax = FMath::Max(GSweep.GenSecondsMax, double(Gen->LastGenerationSeconds));
			const FAreaValidationResult Result = AreaValidator::Validate(Gen->GetWorld());
			FString Failed;
			for (const FAreaRuleResult& Rule : Result.Rules)
			{
				if (!Rule.bPass)
				{
					const FString Key = FString::Printf(TEXT("rule %d %s"), Rule.Rule, *Rule.Name);
					GSweep.FailuresByRule.FindOrAdd(Key) += 1;
					Failed += FString::Printf(TEXT(" [rule %d: %s%s]"), Rule.Rule, *Rule.Summary,
						Rule.Details.Num() > 0 ? *FString::Printf(TEXT("; first: %s"), *Rule.Details[0]) : TEXT(""));
				}
			}
			if (!Result.FatalError.IsEmpty())
			{
				GSweep.FailuresByRule.FindOrAdd(TEXT("fatal")) += 1;
				Failed += FString::Printf(TEXT(" [fatal: %s]"), *Result.FatalError);
			}
			const bool bPass = Result.AllPassed();
			GSweep.Passed += bPass ? 1 : 0;
			const AAreaInfo* Info = AAreaInfo::Find(Gen->GetWorld());
			GSweep.Lines.Add(FString::Printf(TEXT("seed %d: %s, route %.0f m, generation %.2f s, hash %016llx%s"), Seed, bPass ? TEXT("PASS") : TEXT("FAIL"),
				Info ? Info->GetRouteLength() / 100.f : 0.f, Gen->LastGenerationSeconds, static_cast<uint64>(Gen->LastLayoutHash), *Failed));
			UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: %s"), *GSweep.Lines.Last());
			return true;
		}

		// Done: restore the map's own seed.
		FString Error;
		GenerateSeed(Gen, GSweep.OriginalSeed, Error);
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: ===== %d of %d seeds pass all rules (generation mean %.2f s, max %.2f s) ====="),
			GSweep.Passed, GSweep.Seeds.Num(), GSweep.Seeds.Num() > 0 ? GSweep.GenSecondsSum / GSweep.Seeds.Num() : 0.0, GSweep.GenSecondsMax);
		TArray<FString> Keys;
		GSweep.FailuresByRule.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: failures by rule: %s: %d"), *Key, GSweep.FailuresByRule[Key]);
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: restored seed %d, hash %016llx (before the sweep %016llx, %s)."), GSweep.OriginalSeed,
			static_cast<uint64>(Gen->LastLayoutHash), static_cast<uint64>(GSweep.OriginalHash), Gen->LastLayoutHash == GSweep.OriginalHash ? TEXT("same") : TEXT("DIFFERENT"));
		GSweep.bActive = false;
		return false;
	}

	// --- Regeneration test ---

	struct FRegen
	{
		bool bActive = false;
		TWeakObjectPtr<AAreaGenerator> Gen;
		int32 Runs = 0;
		int32 Done = 0;
		int32 BaselineActors = 0;
		int32 FirstCount = -1;
		int64 FirstHash = 0;
		int32 Problems = 0;
	};
	static FRegen GRegen;

	static bool TickRegen(float)
	{
		AAreaGenerator* Gen = GRegen.Gen.Get();
		if (!GRegen.bActive || !Gen)
		{
			GRegen.bActive = false;
			return false;
		}
		UWorld* World = Gen->GetWorld();
		const int32 Removed = Gen->ClearArea();
		int32 Generated = 0;
		const int32 AfterClear = CountActors(World, Generated);
		const bool bClean = Generated == 0 && AfterClear == GRegen.BaselineActors;
		FString Error;
		const bool bOk = Gen->GenerateArea(Error, true);
		int32 GeneratedNow = 0;
		const int32 Total = CountActors(World, GeneratedNow);
		if (GRegen.FirstCount < 0)
		{
			GRegen.FirstCount = GeneratedNow;
			GRegen.FirstHash = Gen->LastLayoutHash;
		}
		const bool bSame = GeneratedNow == GRegen.FirstCount && Gen->LastLayoutHash == GRegen.FirstHash;
		GRegen.Problems += (bClean && bOk && bSame) ? 0 : 1;
		++GRegen.Done;
		UE_LOG(LogArea, Display, TEXT("tls.AreaRegenTest: run %d/%d: cleared %d, after Clear %d actors (%d tagged; baseline %d) %s; generated %d tagged, level %d actors, hash %016llx, %.2f s %s%s"),
			GRegen.Done, GRegen.Runs, Removed, AfterClear, Generated, GRegen.BaselineActors, bClean ? TEXT("clean") : TEXT("ORPHANS"), GeneratedNow, Total,
			static_cast<uint64>(Gen->LastLayoutHash), Gen->LastGenerationSeconds, bSame ? TEXT("same") : TEXT("DIFFERENT"), bOk ? TEXT("") : *FString::Printf(TEXT(" FAILED: %s"), *Error));
		if (GRegen.Done >= GRegen.Runs)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaRegenTest: ===== %s: %d runs, %d with orphans, a different count or hash, or a failure ====="),
				GRegen.Problems == 0 ? TEXT("PASS") : TEXT("FAIL"), GRegen.Runs, GRegen.Problems);
			GRegen.bActive = false;
			return false;
		}
		return true;
	}

	static void LogReport(UWorld* World, bool bRebuild)
	{
		AAreaGenerator* Gen = Generator(World);
		AAreaInfo* Info = AAreaInfo::Find(World);
		if (!Gen || !Info)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaReport: needs a generator and an area info."));
			return;
		}
		float NavSeconds = Info->NavBuildSeconds;
		if (bRebuild)
		{
			NavSeconds = Gen->BuildNavigation();
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
		int32 TotalInstances = 0;
		for (TActorIterator<AAreaGeometry> It(World); It; ++It)
		{
			for (const UInstancedStaticMeshComponent* Component : It->GetAllComponents())
			{
				Instances += FString::Printf(TEXT("%s%s %d"), Instances.IsEmpty() ? TEXT("") : TEXT(", "), *Component->GetName(), Component->GetInstanceCount());
				TotalInstances += Component->GetInstanceCount();
			}
		}
		int32 Tiles = 0;
		if (const ARecastNavMesh* Nav = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr)
		{
			Tiles = Nav->GetNumActiveTiles();
		}
		int32 Generated = 0;
		const int32 Total = CountActors(World, Generated);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: seed %d, hash %016llx, %d generated actors (level %d)"), Info->Seed, static_cast<uint64>(Info->LayoutHash), Generated, Total);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: route centreline %.1f m, navigation path start->exit %.1f m, expected walk %.0f s at %.0f cm/s (%s)"),
			Info->GetRouteLength() / 100.f, PathLength / 100.0, PathLength > 0.0 ? PathLength / Pawn.WalkSpeed : -1.0, Pawn.WalkSpeed, *Pawn.Source);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: segments: %s; spurs/dead ends %d, niches %d"), *Types, Spurs, Niches);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: instances %d: %s"), TotalInstances, *Instances);
		UE_LOG(LogArea, Display, TEXT("tls.AreaReport: generation %.3f s, navmesh build %.3f s%s, %d active navmesh tiles"),
			Info->GenerationSeconds, NavSeconds, bRebuild ? TEXT(" (rebuilt now)") : TEXT(" (at generation)"), Tiles);
		for (int32 Index = 0; Index < Info->Segments.Num(); ++Index)
		{
			const FAreaSegmentRecord& S = Info->Segments[Index];
			UE_LOG(LogArea, Display, TEXT("tls.AreaReport:   segment %d %s: route %.1f-%.1f m, bounds %.0f x %.0f m, spurs %d, niches %d"), Index, LexToString(S.Type),
				S.RouteStart / 100.f, S.RouteEnd / 100.f, S.Bounds.GetSize().X / 100.f, S.Bounds.GetSize().Y / 100.f, S.SpursPlaced, S.NichesPlaced);
		}
	}

	/** Route distance of a named pose: start, corridor (10 m in), hall (the hall's centre), gate, exit, or metres. */
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
		const EAreaSegmentType Type = What == TEXT("hall") ? EAreaSegmentType::Hall : What == TEXT("gate") ? EAreaSegmentType::Gate : EAreaSegmentType::Room;
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
}

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaGenerateCommand(
	TEXT("tls.AreaGenerate"),
	TEXT("tls.AreaGenerate [Seed]: (editor world) clears and bakes the area of the map's AAreaGenerator, optionally with a new Seed, and builds the navmesh."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = AreaDebug::EditorWorld();
		AAreaGenerator* Gen = AreaDebug::Generator(World);
		if (!Gen)
		{
			return;
		}
		if (Args.Num() > 0)
		{
			Gen->Seed = FCString::Atoi(*Args[0]);
		}
		FString Error;
		if (!Gen->GenerateArea(Error, true))
		{
			UE_LOG(LogArea, Error, TEXT("tls.AreaGenerate: seed %d failed: %s"), Gen->Seed, *Error);
			return;
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaGenerate: seed %d, %d actors, hash %016llx, %.3f s."), Gen->Seed, Gen->LastActorCount, static_cast<uint64>(Gen->LastLayoutHash), Gen->LastGenerationSeconds);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaClearCommand(
	TEXT("tls.AreaClear"),
	TEXT("tls.AreaClear: (editor world) removes every generated area actor."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		if (AAreaGenerator* Gen = AreaDebug::Generator(AreaDebug::EditorWorld()))
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaClear: removed %d actors."), Gen->ClearArea());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaValidateCommand(
	TEXT("tls.AreaValidate"),
	TEXT("tls.AreaValidate [Rebuild=0]: (editor world) checks the six area rules (path, reachability and counts, gates, hiding places, curbs, stray navmesh); Rebuild=1 rebuilds the navmesh first."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = AreaDebug::EditorWorld();
		AAreaGenerator* Gen = AreaDebug::Generator(World);
		if (!Gen)
		{
			return;
		}
		if (Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0)
		{
			UE_LOG(LogArea, Display, TEXT("tls.AreaValidate: navmesh rebuilt in %.3f s."), Gen->BuildNavigation());
		}
		const FAreaValidationResult Result = AreaValidator::Validate(World);
		const AAreaInfo* Info = AAreaInfo::Find(World);
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate: %s, seed %d, hash %016llx"), *World->GetMapName(), Info ? Info->Seed : -1, Info ? static_cast<uint64>(Info->LayoutHash) : 0ull);
		Result.Log(TEXT("tls.AreaValidate:"), true);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaValidateSeedsCommand(
	TEXT("tls.AreaValidate.Seeds"),
	TEXT("tls.AreaValidate.Seeds [N=20] [FirstSeed=1]: (editor world, not saved) generates and validates seeds FirstSeed..FirstSeed+N-1 with the map's recipe, one after the other; reports the pass rate and the failures by rule, then regenerates the map's own seed."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(AreaDebug::EditorWorld());
		if (!Gen || AreaDebug::GSweep.bActive || AreaDebug::GRegen.bActive)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaValidate.Seeds: no generator, or a sweep is already running."));
			return;
		}
		const int32 N = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 500) : 20;
		const int32 First = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1;
		AreaDebug::GSweep = AreaDebug::FSweep();
		AreaDebug::GSweep.bActive = true;
		AreaDebug::GSweep.Gen = Gen;
		AreaDebug::GSweep.OriginalSeed = Gen->Seed;
		int32 Count = 0;
		AreaDebug::GSweep.OriginalHash = AAreaInfo::ComputeLayoutHash(Gen->GetWorld(), Count);
		for (int32 I = 0; I < N; ++I)
		{
			AreaDebug::GSweep.Seeds.Add(First + I);
		}
		UE_LOG(LogArea, Display, TEXT("tls.AreaValidate.Seeds: %d seeds from %d (map seed %d)."), N, First, Gen->Seed);
		// Synchronous: a navmesh Build() called from the core ticker never completes (editor deadlock).
		while (AreaDebug::TickSweep(0.f)) {}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaReportCommand(
	TEXT("tls.AreaReport"),
	TEXT("tls.AreaReport [Rebuild=0]: (editor world) route length, expected walking time, segments by type, instance counts, generation and navmesh build time (Rebuild=1 times a fresh build)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		AreaDebug::LogReport(AreaDebug::EditorWorld(), Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaRegenTestCommand(
	TEXT("tls.AreaRegenTest"),
	TEXT("tls.AreaRegenTest [Runs=10]: (editor world) Clear() then Generate() Runs times: no orphan actor after Clear, same actor count and hash every time."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = AreaDebug::EditorWorld();
		AAreaGenerator* Gen = AreaDebug::Generator(World);
		if (!Gen || AreaDebug::GSweep.bActive || AreaDebug::GRegen.bActive)
		{
			return;
		}
		AreaDebug::GRegen = AreaDebug::FRegen();
		AreaDebug::GRegen.bActive = true;
		AreaDebug::GRegen.Gen = Gen;
		AreaDebug::GRegen.Runs = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 100) : 10;
		int32 Generated = 0;
		const int32 Total = AreaDebug::CountActors(World, Generated);
		AreaDebug::GRegen.BaselineActors = Total - Generated;
		UE_LOG(LogArea, Display, TEXT("tls.AreaRegenTest: %d runs; level has %d actors, %d of them generated."), AreaDebug::GRegen.Runs, Total, Generated);
		while (AreaDebug::TickRegen(0.f)) {}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaDeterminismCommand(
	TEXT("tls.AreaDeterminism"),
	TEXT("tls.AreaDeterminism: (editor world) generates the map's seed twice and seed+1 once, compares layout hashes and actor counts, then regenerates the map's seed."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		AAreaGenerator* Gen = AreaDebug::Generator(AreaDebug::EditorWorld());
		if (!Gen)
		{
			return;
		}
		const int32 Seed = Gen->Seed;
		struct FRun { int32 Seed; int64 Hash; int32 Count; float Seconds; };
		TArray<FRun> Runs;
		for (const int32 S : { Seed, Seed, Seed + 1, Seed })
		{
			FString Error;
			AreaDebug::GenerateSeed(Gen, S, Error);
			Runs.Add({ S, Gen->LastLayoutHash, Gen->LastActorCount, Gen->LastGenerationSeconds });
			UE_LOG(LogArea, Display, TEXT("tls.AreaDeterminism: seed %d: hash %016llx, %d actors, %.3f s%s"), S, static_cast<uint64>(Gen->LastLayoutHash),
				Gen->LastActorCount, Gen->LastGenerationSeconds, Error.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" FAILED: %s"), *Error));
		}
		const bool bSame = Runs[0].Hash == Runs[1].Hash && Runs[0].Count == Runs[1].Count && Runs[0].Hash == Runs[3].Hash;
		const bool bDiffers = Runs[2].Hash != Runs[0].Hash;
		UE_LOG(LogArea, Display, TEXT("tls.AreaDeterminism: same seed -> same hash and count: %s; seed+1 -> different hash: %s"),
			bSame ? TEXT("PASS") : TEXT("FAIL"), bDiffers ? TEXT("PASS") : TEXT("FAIL"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaBreakCommand(
	TEXT("tls.AreaBreak"),
	TEXT("tls.AreaBreak path|residue|terminal|hiding|curb|navmesh: (editor world) breaks one validator rule on purpose (exit into the void, a residue off the navmesh, the gate terminal 3 m from its door, walls and pillars removed, one curb removed, a stray floor slab), rebuilds the navmesh and validates. Regenerate afterwards (tls.AreaGenerate)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = AreaDebug::EditorWorld();
		AAreaGenerator* Gen = AreaDebug::Generator(World);
		AAreaInfo* Info = AAreaInfo::Find(World);
		AAreaGeometry* Geo = nullptr;
		for (TActorIterator<AAreaGeometry> It(World); It; ++It) { Geo = *It; break; }
		if (!Gen || !Info || !Geo || Args.Num() == 0)
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
				Done = FString::Printf(TEXT("moved residue %s off the navmesh to (%.0f, %.0f, %.0f)"), *List[0]->GetActorLabel(), P.X, P.Y, P.Z);
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
						Done = FString::Printf(TEXT("moved %s 3 m from %s"), *T->GetActorLabel(), *It->GetActorLabel());
						break;
					}
				}
				break;
			}
		}
		else if (What == TEXT("hiding"))
		{
			Done = FString::Printf(TEXT("removed %d wall and %d pillar instances"), Geo->Walls->GetInstanceCount(), Geo->Pillars->GetInstanceCount());
			Geo->Walls->ClearInstances();
			Geo->Pillars->ClearInstances();
		}
		else if (What == TEXT("curb"))
		{
			const int32 Hall = Info->FindSegmentOfType(EAreaSegmentType::Hall);
			const FVector Target = Hall >= 0 ? Info->Segments[Hall].Bounds.GetCenter() : Info->RoutePoints[Info->RoutePoints.Num() / 2];
			int32 Best = -1;
			double BestDist = TNumericLimits<double>::Max();
			for (int32 I = 0; I < Geo->Curbs->GetInstanceCount(); ++I)
			{
				FTransform T;
				Geo->Curbs->GetInstanceTransform(I, T, true);
				const double D = FVector::Dist2D(T.GetLocation(), Target);
				if (D < BestDist) { BestDist = D; Best = I; }
			}
			if (Best >= 0)
			{
				FTransform T;
				Geo->Curbs->GetInstanceTransform(Best, T, true);
				Geo->Curbs->RemoveInstance(Best);
				Done = FString::Printf(TEXT("removed curb instance %d at (%.0f, %.0f), %.0f cm long"), Best, T.GetLocation().X, T.GetLocation().Y, FMath::Max(T.GetScale3D().X, T.GetScale3D().Y) * 100.f);
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
			Geo->Floors->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Spot.X, Spot.Y, -5.f), FVector(6.f, 6.f, 0.1f)));
			Done = FString::Printf(TEXT("added a stray 6 x 6 m floor slab at (%.0f, %.0f)"), Spot.X, Spot.Y);
		}
		if (Done.IsEmpty())
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaBreak: unknown rule '%s' (path|residue|terminal|hiding|curb|navmesh)."), *What);
			return;
		}
		World->MarkPackageDirty();
		UE_LOG(LogArea, Display, TEXT("tls.AreaBreak %s: %s; navmesh rebuilt in %.3f s."), *What, *Done, Gen->BuildNavigation());
		AreaValidator::Validate(World).Log(*FString::Printf(TEXT("tls.AreaBreak %s:"), *What), true);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaMapCommand(
	TEXT("tls.AreaMap"),
	TEXT("tls.AreaMap: (editor world) writes Saved/AreaMap.json: floors, walls, curbs, pillars, backdrop footprints, route, gameplay actors and navmesh polygons (connected to the start or not), for a top-down map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		UWorld* World = AreaDebug::EditorWorld();
		const AAreaInfo* Info = AAreaInfo::Find(World);
		AAreaGeometry* Geo = nullptr;
		for (TActorIterator<AAreaGeometry> It(World); It; ++It) { Geo = *It; break; }
		if (!Info || !Geo)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.AreaMap: needs a generated area."));
			return;
		}
		FString Json = FString::Printf(TEXT("{\"seed\":%d,\"hash\":\"%016llx\",\"floorZ\":%.1f,\n"), Info->Seed, static_cast<uint64>(Info->LayoutHash), Info->FloorZ);
		auto Box = [](const FBox& B) { return FString::Printf(TEXT("[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]"), B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z); };
		auto Instances = [&](const UInstancedStaticMeshComponent* Component)
		{
			TArray<FString> Items;
			for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
			{
				FTransform T;
				Component->GetInstanceTransform(I, T, true);
				const FVector Half = T.GetScale3D() * 50.f;
				Items.Add(Box(FBox(T.GetLocation() - Half, T.GetLocation() + Half)));
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
			*Instances(Geo->Walls), *Instances(Geo->Curbs), *Instances(Geo->Pillars), *Instances(Geo->Backdrop));
		TArray<FString> Route;
		for (int32 I = 0; I < Info->RoutePoints.Num(); ++I)
		{
			Route.Add(FString::Printf(TEXT("[%.0f,%.0f,%d]"), Info->RoutePoints[I].X, Info->RoutePoints[I].Y, Info->RoutePointSegments[I]));
		}
		Json += FString::Printf(TEXT("\"route\":[%s],\n"), *FString::Join(Route, TEXT(",")));
		TArray<FString> Actors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(AAreaInfo::GeneratedTag) || It->IsA<AAreaGeometry>() || It->IsA<AAreaInfo>())
			{
				continue;
			}
			const FVector L = It->GetActorLocation();
			const FVector F = It->GetActorForwardVector();
			Actors.Add(FString::Printf(TEXT("{\"label\":\"%s\",\"class\":\"%s\",\"pos\":[%.0f,%.0f,%.0f],\"fwd\":[%.2f,%.2f]}"),
				*It->GetActorLabel(), *It->GetClass()->GetName(), L.X, L.Y, L.Z, F.X, F.Y));
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

#endif // WITH_EDITOR && !UE_BUILD_SHIPPING

#if !UE_BUILD_SHIPPING

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaHashCommand(
	TEXT("tls.AreaHash"),
	TEXT("tls.AreaHash: (PIE world if running, else the editor world) recomputes the layout hash and generated actor count and compares them with the ones stored in the area info at generation; also says whether an area generator exists in that world."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (!World)
		{
			World = InWorld;
		}
		const AAreaInfo* Info = AAreaInfo::Find(World);
		int32 Count = 0;
		const int64 Hash = AAreaInfo::ComputeLayoutHash(World, Count);
		int32 Generators = 0;
		for (TActorIterator<AAreaGenerator> It(World); It; ++It) { ++Generators; }
		const bool bSame = Info && Info->LayoutHash == Hash && Info->GeneratedActorCount == Count;
		UE_LOG(LogArea, Display, TEXT("tls.AreaHash: %s world %s: recomputed hash %016llx, %d generated actors; stored %016llx, %d: %s; area generators in this world: %d"),
			World && World->IsGameWorld() ? TEXT("game") : TEXT("editor"), *GetNameSafe(World), static_cast<uint64>(Hash), Count,
			Info ? static_cast<uint64>(Info->LayoutHash) : 0ull, Info ? Info->GeneratedActorCount : 0, bSame ? TEXT("SAME") : TEXT("DIFFERENT"), Generators);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaGoCommand(
	TEXT("tls.AreaGo"),
	TEXT("tls.AreaGo start|corridor|room|gate|hall|exit|Metres [YawOffset=0]: (PIE) puts the pawn on the route centreline at that pose, facing along the route plus YawOffset degrees."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		const AAreaInfo* Info = AAreaInfo::Find(World);
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
		float Distance = 0.f;
#if WITH_EDITOR
		const bool bPose = Info && Args.Num() > 0 && AreaDebug::PoseDistance(Info, Args[0].ToLower(), Distance);
#else
		const bool bPose = Info && Args.Num() > 0 && Args[0].IsNumeric() && ((Distance = FCString::Atof(*Args[0]) * 100.f), true);
#endif
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

#endif // !UE_BUILD_SHIPPING
