// SPDX-License-Identifier: MIT

#include "Area/AreaValidator.h"

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Energy/DoorDevice.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/PlayerStart.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"

namespace AreaValidation
{
	static constexpr int32 MaxDetails = 8;
	static const FVector ProjectExtent(100.f, 100.f, 300.f);

	/** Editor label when there is one (editor builds), else the object name. */
	static FString Label(const AActor* Actor)
	{
#if WITH_EDITOR
		return Actor->GetActorLabel();
#else
		return Actor->GetName();
#endif
	}

	static FString Where(const FVector& P) { return FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), P.X, P.Y, P.Z); }

	static void AddDetail(FAreaRuleResult& R, const FString& Text)
	{
		if (R.Details.Num() < MaxDetails)
		{
			R.Details.Add(Text);
		}
		else if (R.Details.Num() == MaxDetails)
		{
			R.Details.Add(TEXT("..."));
		}
	}

	template <class T>
	static TArray<T*> All(UWorld* World)
	{
		TArray<T*> Out;
		for (TActorIterator<T> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		Out.Sort([](const T& A, const T& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	struct FContext
	{
		UWorld* World = nullptr;
		AAreaGenerator* Generator = nullptr;
		const FAreaRecipe* Recipe = nullptr;
		AAreaInfo* Info = nullptr;
		UNavigationSystemV1* NavSys = nullptr;
		ARecastNavMesh* Nav = nullptr;
		AEntity* Entity = nullptr;
		FAreaPawnMetrics Pawn;
		FVector Start = FVector::ZeroVector;
		FVector Exit = FVector::ZeroVector;
		NavNodeRef StartPoly = INVALID_NAVNODEREF;
		NavNodeRef ExitPoly = INVALID_NAVNODEREF;
		TSet<NavNodeRef> Reach;

		bool Project(const FVector& P, FVector& Out) const
		{
			FNavLocation Location;
			if (NavSys->ProjectPointToNavigation(P, Location, ProjectExtent, Nav))
			{
				Out = Location.Location;
				return true;
			}
			return false;
		}

		NavNodeRef PolyAt(const FVector& P) const { return Nav->FindNearestPoly(P, ProjectExtent); }

		/** Complete navigation path length from A to B; false when there is none (or only a partial one). */
		bool PathLength(const FVector& A, const FVector& B, double& OutLength) const
		{
			FPathFindingQuery Query(Info, *Nav, A, B);
			const FPathFindingResult Result = NavSys->FindPathSync(Query);
			if (!Result.IsSuccessful() || Result.IsPartial() || !Result.Path.IsValid())
			{
				return false;
			}
			OutLength = Result.Path->GetLength();
			return true;
		}

		void Flood(NavNodeRef From, const TSet<NavNodeRef>& Blocked, TSet<NavNodeRef>& Out) const
		{
			if (From == INVALID_NAVNODEREF || Blocked.Contains(From))
			{
				return;
			}
			TArray<NavNodeRef> Queue;
			Queue.Add(From);
			Out.Add(From);
			TArray<NavNodeRef> Neighbors;
			for (int32 I = 0; I < Queue.Num(); ++I)
			{
				Neighbors.Reset();
				Nav->GetPolyNeighbors(Queue[I], Neighbors);
				for (const NavNodeRef N : Neighbors)
				{
					if (!Blocked.Contains(N) && !Out.Contains(N))
					{
						Out.Add(N);
						Queue.Add(N);
					}
				}
			}
		}

		bool Reachable(const FVector& P, FVector* OutOnNav = nullptr) const
		{
			FVector OnNav;
			if (!Project(P, OnNav))
			{
				return false;
			}
			if (OutOnNav)
			{
				*OutOnNav = OnNav;
			}
			return Reach.Contains(PolyAt(OnNav));
		}

		float RouteDistance(const FVector& P) const
		{
			int32 Segment = -1;
			float Offset = 0.f;
			return Info->ProjectToRoute(P, Segment, Offset);
		}
	};

	static FVector TerminalFront(const APowerTerminal* Terminal)
	{
		return Terminal->GetActorLocation() + Terminal->GetActorForwardVector() * 90.f + FVector(0.f, 0.f, 50.f);
	}

	static APowerTerminal* TerminalOf(UWorld* World, const APoweredDevice* Device)
	{
		for (APowerTerminal* Terminal : All<APowerTerminal>(World))
		{
			for (const APoweredDevice* Target : Terminal->Targets)
			{
				if (Target == Device)
				{
					return Terminal;
				}
			}
		}
		return nullptr;
	}

	static FAreaRuleResult RulePath(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 1;
		R.Name = TEXT("start-to-exit path");
		double Length = 0.0;
		const bool bPath = C.PathLength(C.Start, C.Exit, Length);
		const float Min = C.Recipe->RouteLengthMin;
		const float Max = C.Recipe->RouteLengthMax;
		R.bPass = bPath && Length >= Min && Length <= Max;
		R.Summary = bPath
			? FString::Printf(TEXT("path %.1f m (range %.0f-%.0f m)"), Length / 100.0, Min / 100.f, Max / 100.f)
			: FString::Printf(TEXT("no complete path from the PlayerStart %s to the exit %s"), *Where(C.Start), *Where(C.Exit));
		return R;
	}

	static FAreaRuleResult RuleReachable(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 2;
		R.Name = TEXT("reachability and counts");
		UWorld* World = C.World;
		int32 Unreachable = 0;
		int32 Residues = 0;
		int32 Regenerating = 0;
		for (const AResidue* Residue : All<AResidue>(World))
		{
			++Residues;
			Regenerating += Residue->bRegenerates ? 1 : 0;
			if (!C.Reachable(Residue->GetActorLocation()))
			{
				++Unreachable;
				AddDetail(R, FString::Printf(TEXT("residue %s unreachable at %s"), *Label(Residue), *Where(Residue->GetActorLocation())));
			}
		}
		const TArray<APowerTerminal*> Terminals = All<APowerTerminal>(World);
		for (const APowerTerminal* Terminal : Terminals)
		{
			if (!C.Reachable(TerminalFront(Terminal)))
			{
				++Unreachable;
				AddDetail(R, FString::Printf(TEXT("terminal %s unreachable at %s"), *Label(Terminal), *Where(TerminalFront(Terminal))));
			}
		}
		const TArray<ADoorDevice*> Doors = All<ADoorDevice>(World);
		for (const ADoorDevice* Door : Doors)
		{
			for (const float Side : { 150.f, -150.f })
			{
				const FVector P = Door->GetActorTransform().TransformPosition(FVector(Side, 0.f, 50.f));
				if (!C.Reachable(P))
				{
					++Unreachable;
					AddDetail(R, FString::Printf(TEXT("door %s side %+.0f unreachable at %s"), *Label(Door), Side, *Where(P)));
				}
			}
		}
		const int32 Gates = C.Recipe->GetGateCount();
		const int32 LightGroups = All<ALightGroupDevice>(World).Num();
		const int32 Entities = All<AEntity>(World).Num();
		const int32 Starts = All<APlayerStart>(World).Num();
		const int32 Exits = All<AAreaExit>(World).Num();
		const bool bCounts = Residues == C.Recipe->ResidueCount && Regenerating == Gates && Doors.Num() == Gates
			&& Terminals.Num() == Gates + C.Recipe->LightGroupCount && LightGroups == C.Recipe->LightGroupCount
			&& Entities == 1 && Starts == 1 && Exits == 1;
		if (!bCounts)
		{
			AddDetail(R, FString::Printf(TEXT("counts: residues %d/%d (regenerating %d/%d), doors %d/%d, terminals %d/%d, light groups %d/%d, entities %d/1, starts %d/1, exits %d/1"),
				Residues, C.Recipe->ResidueCount, Regenerating, Gates, Doors.Num(), Gates, Terminals.Num(), Gates + C.Recipe->LightGroupCount,
				LightGroups, C.Recipe->LightGroupCount, Entities, Starts, Exits));
		}
		R.bPass = Unreachable == 0 && bCounts;
		R.Summary = FString::Printf(TEXT("%d residues (%d regenerating), %d terminals, %d doors; unreachable %d; counts %s"),
			Residues, Regenerating, Terminals.Num(), Doors.Num(), Unreachable, bCounts ? TEXT("match") : TEXT("DIFFER"));
		return R;
	}

	static FAreaRuleResult RuleGates(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 3;
		R.Name = TEXT("gates solvable, not skippable");
		UWorld* World = C.World;
		const TArray<ADoorDevice*> Doors = All<ADoorDevice>(World);
		const TArray<AResidue*> Residues = All<AResidue>(World);
		int32 Failures = 0;
		TArray<FString> Lines;
		for (const ADoorDevice* Door : Doors)
		{
			const FString Name = Label(Door);
			const APowerTerminal* Terminal = TerminalOf(World, Door);
			if (!Terminal)
			{
				++Failures;
				AddDetail(R, FString::Printf(TEXT("%s: no terminal powers it"), *Name));
				continue;
			}

			// Not skippable: flood from the start with the door's polygons removed.
			const FVector Size = Door->GetDoorSize();
			const FBox Local(FVector(-Size.X * 0.5f - 30.f, -Size.Y * 0.5f, -50.f), FVector(Size.X * 0.5f + 30.f, Size.Y * 0.5f, Size.Z + 50.f));
			const FBox DoorBox = Local.TransformBy(Door->GetActorTransform());
			TArray<FNavPoly> DoorPolys;
			C.Nav->GetPolysInBox(DoorBox, DoorPolys);
			TSet<NavNodeRef> Blocked;
			for (const FNavPoly& Poly : DoorPolys)
			{
				Blocked.Add(Poly.Ref);
			}
			TSet<NavNodeRef> Before;
			C.Flood(C.StartPoly, Blocked, Before);
			const bool bSkippable = Before.Contains(C.ExitPoly);
			if (bSkippable)
			{
				++Failures;
				AddDetail(R, FString::Printf(TEXT("%s: the exit is reachable without crossing the door (%d door polygons removed)"), *Name, DoorPolys.Num()));
			}

			// The door seals its passage: just beyond each edge there is solid geometry, no room for a pawn.
			for (const float Side : { 1.f, -1.f })
			{
				for (const float Z : { 60.f, 150.f })
				{
					const FVector P = Door->GetActorTransform().TransformPosition(FVector(0.f, Side * (Size.Y * 0.5f + C.Pawn.CapsuleRadius + 1.f), Z));
					FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaDoorSeal), false, Door);
					if (!World->OverlapAnyTestByChannel(P, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(C.Pawn.CapsuleRadius * 0.9f), Params))
					{
						++Failures;
						AddDetail(R, FString::Printf(TEXT("%s: open gap beside the door at %s"), *Name, *Where(P)));
					}
				}
			}

			// Walk from the terminal to the door longer than the door stays open.
			FVector From;
			FVector To;
			double Walk = 0.0;
			const bool bWalk = C.Project(TerminalFront(Terminal), From) && C.Project(Door->GetActorLocation() + FVector(0.f, 0.f, 50.f), To) && C.PathLength(From, To, Walk);
			const float WalkSeconds = bWalk ? static_cast<float>(Walk / FMath::Max(1.f, C.Pawn.WalkSpeed)) : 0.f;
			if (!bWalk || WalkSeconds <= Door->PoweredSeconds)
			{
				++Failures;
				AddDetail(R, FString::Printf(TEXT("%s: terminal-to-door walk %.1f m = %.2f s at %.0f cm/s, door open %.2f s"),
					*Name, Walk / 100.0, WalkSeconds, C.Pawn.WalkSpeed, Door->PoweredSeconds));
			}

			// A regenerating residue before the terminal, and enough energy before the door.
			const float TerminalS = C.RouteDistance(Terminal->GetActorLocation());
			bool bRegen = false;
			int32 EnergyBefore = 0;
			for (const AResidue* Residue : Residues)
			{
				FVector OnNav;
				if (!C.Project(Residue->GetActorLocation(), OnNav) || !Before.Contains(C.PolyAt(OnNav)))
				{
					continue;
				}
				EnergyBefore += FMath::Clamp(Residue->EnergyValue, 1, 10);
				bRegen |= Residue->bRegenerates && C.RouteDistance(Residue->GetActorLocation()) < TerminalS;
			}
			int32 CostBefore = FMath::Clamp(Terminal->Cost, 0, 99);
			for (const ADoorDevice* Other : Doors)
			{
				FVector OnNav;
				const APowerTerminal* OtherTerminal = Other != Door ? TerminalOf(World, Other) : nullptr;
				if (OtherTerminal && C.Project(Other->GetActorLocation() + FVector(0.f, 0.f, 50.f), OnNav) && Before.Contains(C.PolyAt(OnNav)))
				{
					CostBefore += FMath::Clamp(OtherTerminal->Cost, 0, 99);
				}
			}
			if (!bRegen)
			{
				++Failures;
				AddDetail(R, FString::Printf(TEXT("%s: no reachable regenerating residue before its terminal"), *Name));
			}
			if (EnergyBefore < CostBefore)
			{
				++Failures;
				AddDetail(R, FString::Printf(TEXT("%s: energy before the door %d < cost %d"), *Name, EnergyBefore, CostBefore));
			}
			Lines.Add(FString::Printf(TEXT("%s: walk %.1f m = %.2f s > %.2f s, energy before %d >= %d, regen %d, skippable %d"),
				*Name, Walk / 100.0, WalkSeconds, Door->PoweredSeconds, EnergyBefore, CostBefore, bRegen ? 1 : 0, bSkippable ? 1 : 0));
		}
		R.bPass = Failures == 0;
		R.Summary = FString::Printf(TEXT("%d gates, %d failures%s%s"), Doors.Num(), Failures, Lines.Num() > 0 ? TEXT("; ") : TEXT(""), *FString::Join(Lines, TEXT("; ")));
		return R;
	}

	static FAreaRuleResult RuleHiding(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 4;
		R.Name = TEXT("hiding places in closed segments");
		const float BandCenter = C.Entity->MinSpawnDistance;
		const float Tolerance = FMath::Clamp(C.Entity->SpawnDistanceTolerance, 0.f, 0.5f) * BandCenter;
		const float BandMin = BandCenter - Tolerance;
		const float BandMax = BandCenter + Tolerance;
		const float Spacing = FMath::Clamp(C.Generator->HidingSampleSpacing, 100.f, 5000.f);
		const float Eye = C.Pawn.GetEyeAboveFloor();
		int32 Samples = 0;
		int32 Passed = 0;
		const float Step = 100.f;
		const int32 Steps = FMath::CeilToInt(BandMax / Step);
		for (const FAreaSegmentRecord& Segment : C.Info->Segments)
		{
			if (!Segment.bClosed)
			{
				continue;
			}
			for (float S = Segment.RouteStart; S < Segment.RouteEnd; S += Spacing)
			{
				int32 SegmentIndex = -1;
				const FVector Point = C.Info->GetRoutePointAtDistance(S, SegmentIndex);
				FVector Sample;
				if (!C.Project(Point + FVector(0.f, 0.f, 50.f), Sample))
				{
					++Samples;
					AddDetail(R, FString::Printf(TEXT("sample at %.0f m %s is off the navmesh"), S / 100.f, *Where(Point)));
					continue;
				}
				++Samples;
				const FVector EyePoint = Sample + FVector(0.f, 0.f, Eye);
				bool bFound = false;
				for (int32 DY = -Steps; DY <= Steps && !bFound; ++DY)
				{
					for (int32 DX = -Steps; DX <= Steps && !bFound; ++DX)
					{
						const FVector Offset(DX * Step, DY * Step, 0.f);
						const float Straight = static_cast<float>(Offset.Size2D());
						if (Straight > BandMax || Straight < 1.f)
						{
							continue;
						}
						FVector Candidate;
						if (!C.Project(Sample + Offset + FVector(0.f, 0.f, 50.f), Candidate) || FVector::Dist2D(Candidate, Sample + Offset) > 60.f)
						{
							continue;
						}
						if (!C.Reach.Contains(C.PolyAt(Candidate)) || C.Entity->CanBeSeenFrom(EyePoint, Candidate))
						{
							continue;
						}
						double Length = 0.0;
						bFound = C.PathLength(Sample, Candidate, Length) && Length >= BandMin && Length <= BandMax;
					}
				}
				if (bFound)
				{
					++Passed;
				}
				else
				{
					AddDetail(R, FString::Printf(TEXT("no hiding place from the sample at %.0f m %s (%s)"), S / 100.f, *Where(Sample), LexToString(Segment.Type)));
				}
			}
		}
		const float Share = Samples > 0 ? float(Passed) / Samples : 1.f;
		const float Needed = FMath::Clamp(C.Generator->HidingPassShare, 0.f, 1.f);
		R.bPass = Share >= Needed;
		R.Summary = FString::Printf(TEXT("%d/%d samples (%.0f%%, need %.0f%%) have an unseen navmesh point at a path distance of %.0f-%.0f cm"),
			Passed, Samples, Share * 100.f, Needed * 100.f, BandMin, BandMax);
		return R;
	}

	static FAreaRuleResult RuleCurbs(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 5;
		R.Name = TEXT("curbs on open edges");
		UWorld* World = C.World;
		const float Spacing = FMath::Clamp(C.Generator->CurbSampleSpacing, 10.f, 500.f);
		const float TestZ = C.Info->FloorZ + C.Pawn.MaxStepHeight + 1.f;
		int32 Probes = 0;
		int32 Joined = 0;
		int32 Missing = 0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaCurb), false);
		for (const FAreaFloorRect& Rect : C.Info->FloorRects)
		{
			if (!Rect.bOpen)
			{
				continue;
			}
			const FVector2D Min(Rect.Box.Min.X, Rect.Box.Min.Y);
			const FVector2D Max(Rect.Box.Max.X, Rect.Box.Max.Y);
			struct FSide { FVector2D A; FVector2D B; FVector2D N; };
			const FSide Sides[4] = {
				{ FVector2D(Min.X, Min.Y), FVector2D(Max.X, Min.Y), FVector2D(0.0, -1.0) },
				{ FVector2D(Max.X, Min.Y), FVector2D(Max.X, Max.Y), FVector2D(1.0, 0.0) },
				{ FVector2D(Max.X, Max.Y), FVector2D(Min.X, Max.Y), FVector2D(0.0, 1.0) },
				{ FVector2D(Min.X, Max.Y), FVector2D(Min.X, Min.Y), FVector2D(-1.0, 0.0) } };
			for (const FSide& Side : Sides)
			{
				const double Length = FVector2D::Distance(Side.A, Side.B);
				const int32 Count = FMath::Max(1, FMath::FloorToInt(Length / Spacing));
				for (int32 K = 0; K < Count; ++K)
				{
					const FVector2D P = FMath::Lerp(Side.A, Side.B, (K + 0.5) / Count);
					const FVector2D Beyond = P + Side.N * 30.0;
					++Probes;
					FHitResult Hit;
					const bool bFloorBeyond = World->LineTraceSingleByChannel(Hit, FVector(Beyond.X, Beyond.Y, C.Info->FloorZ + 30.f),
						FVector(Beyond.X, Beyond.Y, C.Info->FloorZ - 30.f), ECC_Visibility, Params) && FMath::Abs(Hit.ImpactPoint.Z - C.Info->FloorZ) < 5.f;
					if (bFloorBeyond)
					{
						++Joined;
						continue;
					}
					const FVector2D In = P - Side.N * 25.0;
					const FVector2D Out = P + Side.N * 25.0;
					if (!World->LineTraceTestByChannel(FVector(In.X, In.Y, TestZ), FVector(Out.X, Out.Y, TestZ), ECC_Pawn, Params))
					{
						++Missing;
						AddDetail(R, FString::Printf(TEXT("open edge without a curb at (%.0f, %.0f)"), P.X, P.Y));
					}
				}
			}
		}
		R.bPass = Missing == 0;
		R.Summary = FString::Printf(TEXT("%d probes on open floor edges (%d continue into floor), edges without a curb: %d (curb test at %.0f cm)"),
			Probes, Joined, Missing, TestZ);
		return R;
	}

	static FAreaRuleResult RuleNavmesh(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 6;
		R.Name = TEXT("no navmesh outside the route");
		TArray<FNavTileRef> Tiles;
		C.Nav->GetAllNavMeshTiles(Tiles);
		int32 Polys = 0;
		int32 OffFloor = 0;
		int32 Disconnected = 0;
		TArray<FNavPoly> TilePolys;
		for (const FNavTileRef& Tile : Tiles)
		{
			TilePolys.Reset();
			C.Nav->GetPolysInTile(Tile, TilePolys);
			for (const FNavPoly& Poly : TilePolys)
			{
				++Polys;
				const bool bOnFloor = C.Info->FindFloorRect(Poly.Center, 5.f) >= 0 && FMath::Abs(Poly.Center.Z - C.Info->FloorZ) < 60.f;
				if (!bOnFloor)
				{
					++OffFloor;
					AddDetail(R, FString::Printf(TEXT("navmesh off the generated floor at %s"), *Where(Poly.Center)));
				}
				else if (!C.Reach.Contains(Poly.Ref))
				{
					++Disconnected;
					AddDetail(R, FString::Printf(TEXT("navmesh island not connected to the start at %s"), *Where(Poly.Center)));
				}
			}
		}
		R.bPass = Polys > 0 && OffFloor == 0 && Disconnected == 0;
		R.Summary = FString::Printf(TEXT("%d polygons in %d tiles: off the floor %d, disconnected %d"), Polys, Tiles.Num(), OffFloor, Disconnected);
		return R;
	}
}

bool FAreaValidationResult::AllPassed() const
{
	if (!FatalError.IsEmpty() || Rules.Num() == 0)
	{
		return false;
	}
	for (const FAreaRuleResult& Rule : Rules)
	{
		if (!Rule.bPass)
		{
			return false;
		}
	}
	return true;
}

void FAreaValidationResult::Log(const TCHAR* Prefix, bool bDetails) const
{
	if (!FatalError.IsEmpty())
	{
		UE_LOG(LogArea, Error, TEXT("%s cannot validate: %s"), Prefix, *FatalError);
		return;
	}
	for (const FAreaRuleResult& Rule : Rules)
	{
		UE_LOG(LogArea, Display, TEXT("%s rule %d %s: %s - %s"), Prefix, Rule.Rule, *Rule.Name, Rule.bPass ? TEXT("PASS") : TEXT("FAIL"), *Rule.Summary);
		if (bDetails)
		{
			for (const FString& Detail : Rule.Details)
			{
				UE_LOG(LogArea, Display, TEXT("%s     %s"), Prefix, *Detail);
			}
		}
	}
	UE_LOG(LogArea, Display, TEXT("%s %s in %.2f s"), Prefix, AllPassed() ? TEXT("ALL RULES PASS") : TEXT("FAILED"), Seconds);
}

FAreaValidationResult AreaValidator::Validate(UWorld* World)
{
	using namespace AreaValidation;
	FAreaValidationResult Result;
	const double T0 = FPlatformTime::Seconds();

	FContext C;
	C.World = World;
	if (World)
	{
		C.Generator = AAreaGenerator::Find(World);
		C.Recipe = C.Generator ? C.Generator->GetRecipe() : nullptr;
		C.Info = AAreaInfo::Find(World);
		C.NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		C.Nav = C.NavSys ? Cast<ARecastNavMesh>(C.NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
		for (TActorIterator<AEntity> It(World); It; ++It) { C.Entity = *It; break; }
	}
	const TArray<APlayerStart*> Starts = World ? All<APlayerStart>(World) : TArray<APlayerStart*>();
	const TArray<AAreaExit*> Exits = World ? All<AAreaExit>(World) : TArray<AAreaExit*>();
	if (!World || !C.Generator || !C.Recipe || !C.Info || !C.Nav || !C.Entity || Starts.Num() == 0 || Exits.Num() == 0)
	{
		Result.FatalError = FString::Printf(TEXT("needs a generator (%d) with a recipe (%d), an area info (%d), a recast navmesh (%d), an entity (%d), a PlayerStart (%d) and an exit (%d)"),
			C.Generator ? 1 : 0, C.Recipe ? 1 : 0, C.Info ? 1 : 0, C.Nav ? 1 : 0, C.Entity ? 1 : 0, Starts.Num(), Exits.Num());
		return Result;
	}
	C.Pawn = GetAreaPawnMetrics(World);
	if (!C.Project(Starts[0]->GetActorLocation(), C.Start) || !C.Project(Exits[0]->GetActorLocation(), C.Exit))
	{
		Result.FatalError = FString::Printf(TEXT("the PlayerStart %s or the exit %s is not on the navmesh"),
			*Where(Starts[0]->GetActorLocation()), *Where(Exits[0]->GetActorLocation()));
		// Rule 1 still reports the failure, so a broken start/exit is attributed to it.
		FAreaRuleResult R;
		R.Rule = 1;
		R.Name = TEXT("start-to-exit path");
		R.Summary = Result.FatalError;
		Result.Rules.Add(R);
		Result.FatalError.Reset();
		Result.Seconds = FPlatformTime::Seconds() - T0;
		return Result;
	}
	C.StartPoly = C.PolyAt(C.Start);
	C.ExitPoly = C.PolyAt(C.Exit);
	C.Flood(C.StartPoly, TSet<NavNodeRef>(), C.Reach);

	Result.Rules.Add(RulePath(C));
	Result.Rules.Add(RuleReachable(C));
	Result.Rules.Add(RuleGates(C));
	Result.Rules.Add(RuleHiding(C));
	Result.Rules.Add(RuleCurbs(C));
	Result.Rules.Add(RuleNavmesh(C));
	Result.Seconds = FPlatformTime::Seconds() - T0;
	return Result;
}

