// SPDX-License-Identifier: MIT

#include "Area/AreaValidator.h"

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Components/BoxComponent.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Energy/PowerTerminal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Light.h"
#include "Engine/OverlapResult.h"
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

	/** The generator's TerminalStandDistance (one owner), read at the start of each validation. */
	static float GStandDistance = 90.f;

	static FVector TerminalFront(const APowerTerminal* Terminal)
	{
		return Terminal->GetActorLocation() + Terminal->GetActorForwardVector() * GStandDistance + FVector(0.f, 0.f, 50.f);
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
		// The recipe's light groups; beacons are light groups too, counted by rule 8 (their terminals are counted here).
		int32 LightGroups = 0;
		for (const ALightGroupDevice* Group : All<ALightGroupDevice>(World))
		{
			LightGroups += Group->ActorHasTag(AAreaInfo::BeaconTag) ? 0 : 1;
		}
		const int32 Beacons = C.Recipe->BeaconCount;
		const int32 Entities = All<AEntity>(World).Num();
		const int32 Starts = All<APlayerStart>(World).Num();
		const int32 Exits = All<AAreaExit>(World).Num();
		// The optional terminals against a wall (rule 7 checks their own counts) are terminals too.
		const int32 Optional = C.Recipe->LogTerminalCount + C.Recipe->TwinAITerminalCount;
		const bool bCounts = Residues == C.Recipe->ResidueCount && Regenerating == Gates && Doors.Num() == Gates
			&& Terminals.Num() == Gates + C.Recipe->LightGroupCount + Optional + Beacons && LightGroups == C.Recipe->LightGroupCount
			&& Entities == 1 && Starts == 1 && Exits == 1;
		if (!bCounts)
		{
			AddDetail(R, FString::Printf(TEXT("counts: residues %d/%d (regenerating %d/%d), doors %d/%d, terminals %d/%d, light groups %d/%d, entities %d/1, starts %d/1, exits %d/1"),
				Residues, C.Recipe->ResidueCount, Regenerating, Gates, Doors.Num(), Gates, Terminals.Num(), Gates + C.Recipe->LightGroupCount + Optional + Beacons,
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
			// The exit corridor holds no hiding place by design (Brief 26; rule 9 checks it).
			if (!Segment.bClosed || Segment.bExitCorridor)
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

	/** A free place for the pawn at the navmesh point OnNav: its capsule overlaps nothing that blocks a pawn. */
	static bool IsFreeForPawn(const FContext& C, const FVector& OnNav, FString* OutBlocker = nullptr)
	{
		const float Radius = C.Pawn.CapsuleRadius * 0.9f;
		const float HalfHeight = FMath::Max(Radius, C.Pawn.CapsuleHalfHeight * 0.9f);
		const FVector Centre = OnNav + FVector(0.f, 0.f, C.Pawn.CapsuleHalfHeight + 5.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaStandFree), false);
		TArray<FOverlapResult> Overlaps;
		C.World->OverlapMultiByChannel(Overlaps, Centre, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Overlap.bBlockingHit)
			{
				if (OutBlocker)
				{
					*OutBlocker = FString::Printf(TEXT("%s.%s"), *GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
				}
				return false;
			}
		}
		return true;
	}

	/**
	 * A place to stand for Terminal at Wanted: on the navmesh within 30 cm, connected to the start, within the terminal's
	 * reach and free for the pawn. OutWhere is the navmesh point.
	 */
	static bool HasStandingPlace(const FContext& C, const APowerTerminal* Terminal, const FVector& Wanted, FVector& OutWhere, FString& OutWhy)
	{
		FNavLocation Location;
		if (!C.NavSys->ProjectPointToNavigation(Wanted, Location, FVector(30.f, 30.f, 150.f), C.Nav))
		{
			OutWhy = FString::Printf(TEXT("no navmesh at %s"), *Where(Wanted));
			return false;
		}
		OutWhere = Location.Location;
		if (!C.Reach.Contains(C.PolyAt(OutWhere)))
		{
			OutWhy = FString::Printf(TEXT("%s not connected to the start"), *Where(OutWhere));
			return false;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(OutWhere, Terminal->GetActorLocation()));
		if (Distance > Terminal->GetInteractRadius())
		{
			OutWhy = FString::Printf(TEXT("%s out of reach (%.0f > %.0f cm)"), *Where(OutWhere), Distance, Terminal->GetInteractRadius());
			return false;
		}
		FString Blocker;
		if (!IsFreeForPawn(C, OutWhere, &Blocker))
		{
			OutWhy = FString::Printf(TEXT("%s not free for the pawn (%s)"), *Where(OutWhere), *Blocker);
			return false;
		}
		return true;
	}

	static int32 SegmentAt(const FContext& C, const FVector& P, float& OutRouteDistance)
	{
		int32 Segment = -1;
		float Offset = 0.f;
		OutRouteDistance = C.Info->ProjectToRoute(P, Segment, Offset);
		return Segment;
	}

	/**
	 * Rule 7 (Brief 8b): the recipe's log terminals and twin AI terminal. Counts match; each terminal has a standing place
	 * on the navmesh within its reach, free for the pawn; a log terminal's is its reading place, 1 to 1.5 m in front of the
	 * screen with nothing between the eye and the screen, in a closed corridor or room of its own, one per equal part of
	 * the route; the twin AI's terminal stands in the last closed segment; no place is within reach of two terminals (any
	 * kind); and the exit is reached without paying any of them (they power nothing and no door depends on them).
	 */
	static FAreaRuleResult RuleOptionalTerminals(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 7;
		R.Name = TEXT("log and twin AI terminals");
		UWorld* World = C.World;
		const TArray<ALogTerminal*> Logs = All<ALogTerminal>(World);
		const TArray<ATwinAITerminal*> Twins = All<ATwinAITerminal>(World);
		const TArray<APowerTerminal*> Terminals = All<APowerTerminal>(World);
		const TArray<ADoorDevice*> Doors = All<ADoorDevice>(World);
		int32 Failures = 0;
		TArray<FString> Lines;

		const bool bCounts = Logs.Num() == C.Recipe->LogTerminalCount && Twins.Num() == C.Recipe->TwinAITerminalCount;
		if (!bCounts)
		{
			++Failures;
			AddDetail(R, FString::Printf(TEXT("counts: log terminals %d/%d, twin AI terminals %d/%d"), Logs.Num(), C.Recipe->LogTerminalCount, Twins.Num(), C.Recipe->TwinAITerminalCount));
		}

		const float RouteLength = C.Info->GetRouteLength();
		const float Reading = FMath::Clamp(C.Generator->LogReadingDistance, 100.f, 150.f);
		// Log terminals in route order, so that the first stands in the first part of the route.
		TArray<TPair<float, ALogTerminal*>> Ordered;
		for (ALogTerminal* Log : Logs)
		{
			float RouteS = 0.f;
			SegmentAt(C, Log->GetActorLocation(), RouteS);
			Ordered.Emplace(RouteS, Log);
		}
		Ordered.Sort([](const TPair<float, ALogTerminal*>& A, const TPair<float, ALogTerminal*>& B) { return A.Key < B.Key; });
		TSet<int32> LogSegments;
		for (int32 Index = 0; Index < Ordered.Num(); ++Index)
		{
			const ALogTerminal* Log = Ordered[Index].Value;
			const FString Name = Label(Log);
			float RouteS = 0.f;
			const int32 Segment = SegmentAt(C, Log->GetActorLocation(), RouteS);
			FVector Stand = FVector::ZeroVector;
			FString Why;
			const FVector Forward = Log->GetActorForwardVector().GetSafeNormal2D();
			const FVector Screen = Log->GetScreenCentre();
			const FVector Wanted = FVector(Screen.X, Screen.Y, Log->GetActorLocation().Z + 50.f) + Forward * Reading;
			const bool bStand = HasStandingPlace(C, Log, Wanted, Stand, Why);
			bool bView = false;
			float ReadDistance = 0.f;
			if (bStand)
			{
				ReadDistance = static_cast<float>(FVector::DotProduct(Stand - Screen, Forward));
				const FVector Eye = Stand + FVector(0.f, 0.f, C.Pawn.GetEyeAboveFloor());
				// Against the triangles (complex): the console's simple collision fills its frame's opening.
				FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaReadingView), true);
				FHitResult Hit;
				bView = !World->LineTraceSingleByChannel(Hit, Eye, Screen + Forward * 3.f, ECC_Visibility, Params);
				if (!bView)
				{
					Why = FString::Printf(TEXT("the view from %s to the screen is blocked by %s"), *Where(Eye), *GetNameSafe(Hit.GetActor()));
				}
				else if (ReadDistance < 99.f || ReadDistance > 151.f)
				{
					bView = false;
					Why = FString::Printf(TEXT("the reading place is %.0f cm from the screen (100-150)"), ReadDistance);
				}
			}
			const bool bSegmentOk = C.Info->Segments.IsValidIndex(Segment) && C.Info->Segments[Segment].bClosed && C.Info->Segments[Segment].Type != EAreaSegmentType::Gate
				&& !LogSegments.Contains(Segment);
			const int32 Parts = FMath::Max(1, Ordered.Num());
			const int32 Part = RouteLength > 0.f ? FMath::Clamp(FMath::FloorToInt(RouteS / RouteLength * Parts), 0, Parts - 1) : 0;
			const bool bPartOk = Part == Index;
			LogSegments.Add(Segment);
			const bool bTargets = Log->Targets.IsEmpty();
			if (!bStand || !bView || !bSegmentOk || !bPartOk || !bTargets)
			{
				++Failures;
				FString Text = Name + TEXT(":");
				if (!bStand || !bView) { Text += FString::Printf(TEXT(" reading place: %s;"), *Why); }
				if (!bSegmentOk) { Text += FString::Printf(TEXT(" segment %d is not a closed corridor or room of its own;"), Segment); }
				if (!bPartOk) { Text += FString::Printf(TEXT(" route %.1f m is in part %d of the route, expected part %d;"), RouteS / 100.f, Part, Index); }
				if (!bTargets) { Text += TEXT(" it powers a device;"); }
				AddDetail(R, Text);
			}
			Lines.Add(FString::Printf(TEXT("%s segment %d at %.1f m, reading place %.0f cm from the screen"), *Name, Segment, RouteS / 100.f, ReadDistance));
		}

		// The twin AI's room when the area has one (Brief 26), else the last closed segment before the exit corridor.
		int32 LastClosed = -1;
		for (int32 Index = C.Info->Segments.Num() - 1; Index >= 0 && LastClosed < 0; --Index)
		{
			LastClosed = C.Info->Segments[Index].bTwinAIRoom ? Index : -1;
		}
		for (int32 Index = C.Info->Segments.Num() - 1; Index >= 0 && LastClosed < 0; --Index)
		{
			LastClosed = C.Info->Segments[Index].bClosed && !C.Info->Segments[Index].bExitCorridor ? Index : -1;
		}
		for (const ATwinAITerminal* Twin : Twins)
		{
			const FString Name = Label(Twin);
			float RouteS = 0.f;
			const int32 Segment = SegmentAt(C, Twin->GetActorLocation(), RouteS);
			FVector Stand = FVector::ZeroVector;
			FString Why;
			const FVector Wanted = Twin->GetActorLocation() + Twin->GetActorForwardVector().GetSafeNormal2D() * GStandDistance + FVector(0.f, 0.f, 50.f);
			const bool bStand = HasStandingPlace(C, Twin, Wanted, Stand, Why);
			const bool bSegmentOk = Segment == LastClosed;
			const bool bTargets = Twin->Targets.IsEmpty();
			if (!bStand || !bSegmentOk || !bTargets)
			{
				++Failures;
				FString Text = Name + TEXT(":");
				if (!bStand) { Text += FString::Printf(TEXT(" standing place: %s;"), *Why); }
				if (!bSegmentOk) { Text += FString::Printf(TEXT(" segment %d is not the twin AI's room or the last closed segment %d;"), Segment, LastClosed); }
				if (!bTargets) { Text += TEXT(" it powers a device;"); }
				AddDetail(R, Text);
			}
			Lines.Add(FString::Printf(TEXT("%s segment %d at %.1f m"), *Name, Segment, RouteS / 100.f));
		}

		// No place within reach of two terminals (any kind): their reach circles do not meet.
		int32 Overlaps = 0;
		for (int32 I = 0; I < Terminals.Num(); ++I)
		{
			for (int32 J = I + 1; J < Terminals.Num(); ++J)
			{
				const float Distance = static_cast<float>(FVector::Dist2D(Terminals[I]->GetActorLocation(), Terminals[J]->GetActorLocation()));
				if (Distance <= Terminals[I]->GetInteractRadius() + Terminals[J]->GetInteractRadius())
				{
					++Overlaps;
					AddDetail(R, FString::Printf(TEXT("%s and %s are %.0f cm apart: a place is within reach of both"), *Label(Terminals[I]), *Label(Terminals[J]), Distance));
				}
			}
		}
		Failures += Overlaps;

		// The exit without paying any of them: they power nothing and no door depends on them, so the start-to-exit path
		// (rule 1) and the gates' energy (rule 3 counts only the doors' terminals) hold whether they are paid or not.
		bool bExitFree = true;
		for (const ADoorDevice* Door : Doors)
		{
			const APowerTerminal* Terminal = TerminalOf(World, Door);
			bExitFree &= !Terminal || (!Terminal->IsA<ALogTerminal>() && !Terminal->IsA<ATwinAITerminal>());
		}
		double PathLength = 0.0;
		bExitFree &= C.PathLength(C.Start, C.Exit, PathLength);
		if (!bExitFree)
		{
			++Failures;
			AddDetail(R, TEXT("the exit cannot be reached without paying a log or twin AI terminal"));
		}

		R.bPass = Failures == 0;
		R.Summary = FString::Printf(TEXT("%d log terminals (recipe %d), %d twin AI terminals (recipe %d), reach overlaps %d, exit without paying them %s%s%s"),
			Logs.Num(), C.Recipe->LogTerminalCount, Twins.Num(), C.Recipe->TwinAITerminalCount, Overlaps, bExitFree ? TEXT("yes") : TEXT("NO"),
			Lines.Num() > 0 ? TEXT("; ") : TEXT(""), *FString::Join(Lines, TEXT("; ")));
		return R;
	}

	/** The boxes PCG built for the area, every kind, from their instances: what stands there, with collision or without (the backdrop has none). */
	static TArray<FBox> BuiltBoxes(const FContext& C)
	{
		TArray<FBox> Out;
		for (int32 Kind = 0; Kind <= static_cast<int32>(EAreaPieceKind::Backdrop); ++Kind)
		{
			for (const UInstancedStaticMeshComponent* Instances : C.Generator->GetPCGComponents(static_cast<EAreaPieceKind>(Kind)))
			{
				for (int32 Index = 0; Index < Instances->GetInstanceCount(); ++Index)
				{
					FTransform T;
					Instances->GetInstanceTransform(Index, T, /*bWorldSpace=*/true);
					const FVector Half = T.GetScale3D().GetAbs() * 50.0;
					Out.Add(FBox(T.GetLocation() - Half, T.GetLocation() + Half));
				}
			}
		}
		return Out;
	}

	/** Rule 8: how far below BeaconHeight the lowest point of a beacon may come over a floor (the head hangs below its mount). */
	static constexpr float BeaconClearanceMargin = 100.f;

	/**
	 * Rule 8 (Brief 10): the recipe's beacons. Counts match (light groups with the beacon tag, bodies); one beacon per Hall
	 * segment in route order; each hangs above its segment, its lowest point at least BeaconHeight less
	 * BeaconClearanceMargin above every floor or catwalk under it and above the segment's highest floor; one spot light,
	 * aimed at the point where the route leaves the segment, with the range to reach it, and nothing on the line from the
	 * light to BeaconClearPathHeight above that point (built geometry included, collision or not); the rod
	 * ends in the first geometry above the mount, or has the maximum length where there is none; the body blocks nothing,
	 * changes no navigation and renders no custom depth; its terminal stands at the entrance of the space (from 6 m before
	 * to 4 m after the point where the route enters it) with a standing place on the navmesh, connected to the start and
	 * free for the pawn, and no place within reach of another terminal; the exit is reached without paying any beacon.
	 */
	static FAreaRuleResult RuleBeacons(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 8;
		R.Name = TEXT("beacons");
		UWorld* World = C.World;
		const int32 Wanted = C.Recipe->BeaconCount;
		TArray<ALightGroupDevice*> Devices;
		for (ALightGroupDevice* Device : All<ALightGroupDevice>(World))
		{
			if (Device->ActorHasTag(AAreaInfo::BeaconTag)) { Devices.Add(Device); }
		}
		TArray<ABeaconBody*> Bodies = All<ABeaconBody>(World);
		Bodies.Sort([](const ABeaconBody& A, const ABeaconBody& B) { return A.Segment < B.Segment; });
		const TArray<APowerTerminal*> Terminals = All<APowerTerminal>(World);
		int32 Failures = 0;
		TArray<FString> Lines;

		TArray<int32> Halls;
		for (int32 Index = 0; Index < C.Info->Segments.Num(); ++Index)
		{
			if (C.Info->Segments[Index].Type == EAreaSegmentType::Hall) { Halls.Add(Index); }
		}
		const bool bCounts = Devices.Num() == Wanted && Bodies.Num() == Wanted && Wanted <= Halls.Num();
		if (!bCounts)
		{
			++Failures;
			AddDetail(R, FString::Printf(TEXT("counts: beacon light groups %d, bodies %d, recipe %d, Hall segments %d"), Devices.Num(), Bodies.Num(), Wanted, Halls.Num()));
		}

		const float Height = FMath::Clamp(C.Generator->BeaconHeight, 300.f, 3000.f);
		const float RodMax = FMath::Clamp(C.Generator->BeaconRodMaxLength, 100.f, 20000.f);
		const TArray<FBox> Built = BuiltBoxes(C);
		const float ClearHeight = FMath::Clamp(C.Generator->BeaconClearPathHeight, 0.f, 400.f);
		for (int32 K = 0; K < Bodies.Num(); ++K)
		{
			const ABeaconBody* Body = Bodies[K];
			const FString Name = Label(Body);
			TArray<FString> Why;
			float TightestGap = TNumericLimits<float>::Max();
			FString TightestOver;
			const int32 Segment = Body->Segment;
			const FAreaSegmentRecord* Seg = C.Info->Segments.IsValidIndex(Segment) ? &C.Info->Segments[Segment] : nullptr;
			if (!Halls.IsValidIndex(K) || Halls[K] != Segment)
			{
				Why.Add(FString::Printf(TEXT("hangs over segment %d, expected Hall segment %d"), Segment, Halls.IsValidIndex(K) ? Halls[K] : -1));
			}
			const FVector Mount = Body->GetActorLocation();
			if (!Seg || !Seg->Bounds.IsInsideXY(Mount))
			{
				Why.Add(TEXT("not above its segment"));
			}

			// The light.
			const ALightGroupDevice* Device = Body->Device;
			const ALight* Light = Device && Device->Lights.Num() == 1 ? Device->Lights[0].Get() : nullptr;
			const USpotLightComponent* Spot = Light ? Cast<USpotLightComponent>(Light->GetLightComponent()) : nullptr;
			if (!Device || !Devices.Contains(Device) || !Spot)
			{
				Why.Add(TEXT("its light group is not a beacon light group with one spot light"));
			}
			const FVector LightAt = Spot ? Spot->GetComponentLocation() : Mount;

			// Clear of every floor and catwalk below it.
			float Lowest = FMath::Min(static_cast<float>(Mount.Z), static_cast<float>(LightAt.Z));
			const FBox BodyBox = Body->GetBodyBounds();
			FBox Footprint(Mount - FVector(50.f), Mount + FVector(50.f));
			if (BodyBox.IsValid)
			{
				Lowest = FMath::Min(Lowest, static_cast<float>(BodyBox.Min.Z));
				Footprint += BodyBox;
			}
			float SegmentTop = -TNumericLimits<float>::Max();
			float Clearance = TNumericLimits<float>::Max();
			for (const FAreaFloorRect& Rect : C.Info->FloorRects)
			{
				const bool bUnder = Rect.Box.Min.X <= Footprint.Max.X && Rect.Box.Max.X >= Footprint.Min.X && Rect.Box.Min.Y <= Footprint.Max.Y && Rect.Box.Max.Y >= Footprint.Min.Y;
				if (Rect.Segment == Segment)
				{
					SegmentTop = FMath::Max(SegmentTop, static_cast<float>(Rect.Box.Max.Z));
				}
				if (bUnder)
				{
					Clearance = FMath::Min(Clearance, Lowest - static_cast<float>(Rect.Box.Max.Z));
				}
			}
			if (SegmentTop > -1.0e30f)
			{
				Clearance = FMath::Min(Clearance, Lowest - SegmentTop);
			}
			if (Clearance < Height - BeaconClearanceMargin)
			{
				Why.Add(FString::Printf(TEXT("its lowest point is %.0f cm above a floor (needs %.0f)"), Clearance, Height - BeaconClearanceMargin));
			}

			// Aimed at the point where the route leaves the segment, reaching it, nothing between.
			int32 ExitSegment = -1;
			const FVector Leave = Seg ? C.Info->GetRoutePointAtDistance(Seg->RouteEnd, ExitSegment) : FVector::ZeroVector;
			const FVector Aim = Body->GetAimPoint();
			const float AimDistance = static_cast<float>(FVector::Dist(LightAt, Aim));
			if (!Seg || FVector::Dist2D(Aim, Leave) > 50.f)
			{
				Why.Add(FString::Printf(TEXT("aimed at %s, the route leaves the segment at %s"), *Where(Aim), *Where(Leave)));
			}
			if (Spot)
			{
				const float Cos = static_cast<float>(FVector::DotProduct(Spot->GetForwardVector(), (Aim - LightAt).GetSafeNormal()));
				if (Cos < FMath::Cos(FMath::DegreesToRadians(1.f)))
				{
					Why.Add(FString::Printf(TEXT("the light points %.1f degrees off its aim"), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f)))));
				}
				if (Spot->AttenuationRadius < AimDistance)
				{
					Why.Add(FString::Printf(TEXT("range %.0f m short of its aim %.0f m away"), Spot->AttenuationRadius / 100.f, AimDistance / 100.f));
				}
			}
			{
				// To BeaconClearPathHeight above the point where the route leaves (the beacon itself aims at the floor point).
				const FVector End = Aim + FVector(0.f, 0.f, ClearHeight);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaBeaconView), false, Body);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, LightAt, End, ECC_Visibility, Params))
				{
					Why.Add(FString::Printf(TEXT("its light is blocked by %s at %s on the way to %s"), *GetNameSafe(Hit.GetActor()), *Where(Hit.ImpactPoint), *Where(End)));
				}
				// The built geometry too, collision or not.
				for (const FBox& Box : Built)
				{
					if (FMath::LineBoxIntersection(Box, LightAt, End, End - LightAt))
					{
						Why.Add(FString::Printf(TEXT("its light crosses built geometry %s-%s on the way to %s"), *Where(Box.Min), *Where(Box.Max), *Where(End)));
						break;
					}
				}
				// How far the path passes above each terminal within a metre of it (in plan): the tightest is reported.
				const FVector2D A2(LightAt.X, LightAt.Y);
				const FVector2D B2(End.X, End.Y);
				const double Len2 = FVector2D::DistSquared(A2, B2);
				for (const APowerTerminal* Other : Terminals)
				{
					const FVector P = Other->GetActorLocation();
					const double T = Len2 > 0.0 ? FMath::Clamp(FVector2D::DotProduct(FVector2D(P.X, P.Y) - A2, B2 - A2) / Len2, 0.0, 1.0) : 0.0;
					const FVector2D Closest = A2 + (B2 - A2) * T;
					if (FVector2D::Distance(Closest, FVector2D(P.X, P.Y)) > 100.0)
					{
						continue;
					}
					const FBox OtherBox = Other->GetComponentsBoundingBox(/*bNonColliding=*/true);
					const float PathZ = static_cast<float>(FMath::Lerp(LightAt.Z, End.Z, T));
					const float Gap = OtherBox.IsValid ? PathZ - static_cast<float>(OtherBox.Max.Z) : PathZ - static_cast<float>(P.Z);
					if (Gap < TightestGap)
					{
						TightestGap = Gap;
						TightestOver = FString::Printf(TEXT("%s (top %.0f cm, path %.0f cm)"), *Label(Other), OtherBox.IsValid ? OtherBox.Max.Z : P.Z, PathZ);
					}
				}
			}

			// The rod: to the first geometry above the mount, or its maximum length.
			{
				// The first geometry above: what blocks a trace, and every box PCG built (the backdrop has no collision).
				FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaBeaconRod), false, Body);
				FHitResult Hit;
				float Above = TNumericLimits<float>::Max();
				FString What;
				if (World->LineTraceSingleByChannel(Hit, Mount, Mount + FVector(0.f, 0.f, RodMax + 1.f), ECC_Visibility, Params))
				{
					Above = Hit.bStartPenetrating ? 0.f : static_cast<float>(Hit.Distance);
					What = GetNameSafe(Hit.GetActor());
				}
				for (const FBox& Box : Built)
				{
					if (Mount.X >= Box.Min.X && Mount.X <= Box.Max.X && Mount.Y >= Box.Min.Y && Mount.Y <= Box.Max.Y && Box.Max.Z > Mount.Z)
					{
						const float Distance = static_cast<float>(FMath::Max(0.0, Box.Min.Z - Mount.Z));
						if (Distance < Above)
						{
							Above = Distance;
							What = FString::Printf(TEXT("built box %s-%s"), *Where(Box.Min), *Where(Box.Max));
						}
					}
				}
				const bool bHit = Above <= RodMax;
				const float Expected = bHit ? Above : RodMax;
				if (bHit && Above < 1.f)
				{
					Why.Add(FString::Printf(TEXT("its mount is inside geometry (%s)"), *What));
				}
				if (FMath::Abs(Body->GetRodLength() - Expected) > 10.f || Body->DoesRodReachGeometry() != bHit)
				{
					Why.Add(FString::Printf(TEXT("rod %.0f cm (%s), the first geometry above is %s"), Body->GetRodLength(), Body->DoesRodReachGeometry() ? TEXT("reaching") : TEXT("maximum"),
						bHit ? *FString::Printf(TEXT("%.0f cm up (%s)"), Above, *What) : TEXT("not within the maximum length")));
				}
			}

			// Ordinary geometry: blocks nothing, no navigation, no custom depth.
			TArray<UPrimitiveComponent*> Parts;
			Body->GetComponents(Parts);
			for (const UPrimitiveComponent* Part : Parts)
			{
				if (Part->IsCollisionEnabled() || Part->CanEverAffectNavigation() || Part->bRenderCustomDepth)
				{
					Why.Add(FString::Printf(TEXT("%s has collision, navigation or custom depth"), *Part->GetName()));
				}
			}

			// Its terminal, at the entrance, paid before the space is crossed.
			const APowerTerminal* Terminal = Device ? TerminalOf(World, Device) : nullptr;
			float TerminalS = 0.f;
			if (!Terminal)
			{
				Why.Add(TEXT("no terminal powers it"));
			}
			else
			{
				FVector Stand = FVector::ZeroVector;
				FString StandWhy;
				if (!HasStandingPlace(C, Terminal, TerminalFront(Terminal), Stand, StandWhy))
				{
					Why.Add(FString::Printf(TEXT("terminal standing place: %s"), *StandWhy));
				}
				SegmentAt(C, Terminal->GetActorLocation(), TerminalS);
				if (!Seg || TerminalS < Seg->RouteStart - 600.f || TerminalS > Seg->RouteStart + 400.f)
				{
					Why.Add(FString::Printf(TEXT("terminal at route %.1f m, the space starts at %.1f m (allowed %.1f-%.1f m)"), TerminalS / 100.f, Seg ? Seg->RouteStart / 100.f : 0.f,
						Seg ? (Seg->RouteStart - 600.f) / 100.f : 0.f, Seg ? (Seg->RouteStart + 400.f) / 100.f : 0.f));
				}
				for (const APowerTerminal* Other : Terminals)
				{
					const float Distance = Other != Terminal ? static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), Terminal->GetActorLocation())) : TNumericLimits<float>::Max();
					if (Distance <= Other->GetInteractRadius() + Terminal->GetInteractRadius())
					{
						Why.Add(FString::Printf(TEXT("terminal %.0f cm from %s: a place is within reach of both"), Distance, *Label(Other)));
					}
				}
			}

			if (Why.Num() > 0)
			{
				++Failures;
				AddDetail(R, Name + TEXT(": ") + FString::Join(Why, TEXT("; ")));
			}
			Lines.Add(FString::Printf(TEXT("%s over segment %d at %s, lowest point %.0f cm above the floor, rod %.0f cm (%s), aimed %.1f m at %s, clear path to %.0f cm above it%s, terminal at %.1f m (space from %.1f m)"),
				*Name, Segment, *Where(Mount), Clearance, Body->GetRodLength(), Body->DoesRodReachGeometry() ? TEXT("to the geometry above") : TEXT("maximum"),
				AimDistance / 100.f, *Where(Aim), ClearHeight,
				TightestOver.IsEmpty() ? TEXT(" (no terminal within 1 m of it)") : *FString::Printf(TEXT(", passing %.0f cm above %s"), TightestGap, *TightestOver),
				TerminalS / 100.f, Seg ? Seg->RouteStart / 100.f : 0.f));
		}

		// The exit without paying any: no door is powered by a beacon's terminal, and the start-to-exit path exists.
		bool bExitFree = true;
		for (const ADoorDevice* Door : All<ADoorDevice>(World))
		{
			const APowerTerminal* Terminal = TerminalOf(World, Door);
			bExitFree &= !Terminal || !Terminal->ActorHasTag(AAreaInfo::BeaconTag);
		}
		double PathLength = 0.0;
		bExitFree &= C.PathLength(C.Start, C.Exit, PathLength);
		if (!bExitFree)
		{
			++Failures;
			AddDetail(R, TEXT("the exit cannot be reached without paying a beacon"));
		}

		R.bPass = Failures == 0;
		R.Summary = FString::Printf(TEXT("%d beacons (recipe %d, %d Hall segments), exit without paying them %s%s%s"), Bodies.Num(), Wanted, Halls.Num(),
			bExitFree ? TEXT("yes") : TEXT("NO"), Lines.Num() > 0 ? TEXT("; ") : TEXT(""), *FString::Join(Lines, TEXT("; ")));
		return R;
	}

	/**
	 * Rule 9 (Brief 26): the way out. Exactly one exit corridor, the route's last segment, straight (one leg of the route,
	 * its floor one rectangle as wide as the corridor), closed (a ceiling over it); the exit lies in it at the recipe's
	 * depth from its mouth, across its whole section; the corridor runs the recipe's run past it and holds nothing (no
	 * niche, side passage, residue, terminal, door, light or beacon). With the recipe's twin AI's room: the room is the
	 * segment right before the exit corridor, the corridor starts from it and the twin AI's terminal stands in it, on the
	 * wall that faces the room's entrance, facing it.
	 */
	static FAreaRuleResult RuleWayOut(FContext& C)
	{
		FAreaRuleResult R;
		R.Rule = 9;
		R.Name = TEXT("the way out");
		UWorld* World = C.World;
		const TArray<FAreaSegmentRecord>& Segs = C.Info->Segments;
		TArray<int32> Corridors;
		for (int32 I = 0; I < Segs.Num(); ++I)
		{
			if (Segs[I].bExitCorridor) { Corridors.Add(I); }
		}
		int32 Failures = 0;
		auto Fail = [&](const FString& Why) { ++Failures; AddDetail(R, Why); };
		if (Corridors.Num() != 1)
		{
			Fail(FString::Printf(TEXT("%d exit corridors (want 1)"), Corridors.Num()));
			R.Summary = FString::Printf(TEXT("%d exit corridors"), Corridors.Num());
			return R;
		}
		const int32 Index = Corridors[0];
		const FAreaSegmentRecord& Seg = Segs[Index];
		const FVector Dir = Seg.Direction.GetSafeNormal2D();
		const FVector Lateral(-Dir.Y, Dir.X, 0.f);
		const float Depth = FMath::Clamp(C.Recipe->ExitTriggerDepthM, 2.f, 50.f) * 100.f;
		const float Run = FMath::Clamp(C.Recipe->ExitRunLengthM, 10.f, 200.f) * 100.f;
		const float Length = Seg.RouteEnd - Seg.RouteStart;
		if (Index != Segs.Num() - 1 || !Seg.bClosed || Seg.Type != EAreaSegmentType::Corridor) { Fail(FString::Printf(TEXT("segment %d is not the route's last, closed corridor"), Index)); }
		// Straight: one piece of the route, and its floor one rectangle as wide as the corridor along it.
		int32 Pieces = 0;
		for (int32 I = 0; I + 1 < C.Info->RoutePoints.Num(); ++I) { Pieces += C.Info->RoutePointSegments.IsValidIndex(I) && C.Info->RoutePointSegments[I] == Index ? 1 : 0; }
		int32 FloorRects = 0;
		FBox Floor(ForceInit);
		for (const FAreaFloorRect& Rect : C.Info->FloorRects)
		{
			if (Rect.Segment == Index) { ++FloorRects; Floor += Rect.Box; }
		}
		const FVector Size = Floor.IsValid ? Floor.GetSize() : FVector::ZeroVector;
		const float Across = static_cast<float>(FMath::Abs(FVector::DotProduct(Size, Lateral)));
		const float Along = static_cast<float>(FMath::Abs(FVector::DotProduct(Size, Dir)));
		if (Pieces != 1 || FloorRects != 1 || FMath::Abs(Across - Seg.Width) > 1.f || FMath::Abs(Along - Length) > 1.f)
		{
			Fail(FString::Printf(TEXT("not straight: %d route pieces, %d floor rectangles, %.0f x %.0f cm (want %.0f x %.0f)"), Pieces, FloorRects, Along, Across, Length, Seg.Width));
		}
		if (FMath::Abs(Length - (Depth + Run)) > 51.f) { Fail(FString::Printf(TEXT("%.0f cm long, want %.0f + %.0f"), Length, Depth, Run)); }
		if (Seg.NichesPlaced > 0 || Seg.SpursPlaced > 0) { Fail(FString::Printf(TEXT("%d niches and %d side passages"), Seg.NichesPlaced, Seg.SpursPlaced)); }
		// Closed: the ceiling's boxes cover it.
		double Covered = 0.0;
		for (const FBox& Box : C.Generator->GetPublishedPieces(EAreaPieceKind::Ceilings))
		{
			const FBox Over = Box.Overlap(FBox(FVector(Floor.Min.X, Floor.Min.Y, -1.0), FVector(Floor.Max.X, Floor.Max.Y, 1.0e5)));
			Covered += Over.IsValid ? Over.GetSize().X * Over.GetSize().Y : 0.0;
		}
		const double Area = Size.X * Size.Y;
		if (Area <= 0.0 || Covered < Area * 0.999) { Fail(FString::Printf(TEXT("its ceiling covers %.1f%% of it"), Area > 0.0 ? 100.0 * Covered / Area : 0.0)); }
		// The exit: at the depth, across the whole section.
		const AAreaExit* Exit = nullptr;
		for (TActorIterator<AAreaExit> It(World); It; ++It) { Exit = *It; break; }
		float ExitDepth = -1.f;
		if (Exit)
		{
			const FVector P = Exit->GetActorLocation();
			ExitDepth = static_cast<float>(FVector::DotProduct(P - Seg.Mouth, Dir));
			const float Off = static_cast<float>(FMath::Abs(FVector::DotProduct(P - Seg.Mouth, Lateral)));
			const FVector Extent = Exit->GetVolume()->GetScaledBoxExtent();
			const float WallHeight = C.Generator->WallHeight;
			const bool bSection = Extent.Y >= 0.5f * Seg.Width - 1.f && P.Z - Extent.Z <= 1.f && P.Z + Extent.Z >= WallHeight - 1.f;
			const bool bFacing = FVector::DotProduct(Exit->GetActorForwardVector().GetSafeNormal2D(), Dir) > 0.999f;
			if (FMath::Abs(ExitDepth - Depth) > 51.f || Off > 1.f || !bSection || !bFacing)
			{
				Fail(FString::Printf(TEXT("the exit %s: %.0f cm in (want %.0f), %.0f cm off the axis, extent %s (section %s), facing along %s"), *Where(P), ExitDepth, Depth, Off,
					*Extent.ToCompactString(), bSection ? TEXT("whole") : TEXT("NOT whole"), bFacing ? TEXT("yes") : TEXT("NO")));
			}
		}
		else
		{
			Fail(TEXT("no exit"));
		}
		// Nothing placed in it.
		const FBox Inside(Floor.Min - FVector(0.0, 0.0, 10.0), FVector(Floor.Max.X, Floor.Max.Y, C.Generator->WallHeight));
		TArray<FString> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* A = *It;
			const bool bKind = A->IsA<AResidue>() || A->IsA<APowerTerminal>() || A->IsA<ADoorDevice>() || A->IsA<ALightGroupDevice>() || A->IsA<ALight>() || A->IsA<ABeaconBody>();
			if (bKind && Inside.IsInsideOrOn(A->GetActorLocation())) { Found.Add(Label(A)); }
		}
		if (Found.Num() > 0) { Fail(TEXT("it holds ") + FString::Join(Found, TEXT(", "))); }
		// The twin AI's room.
		FString RoomLine;
		if (C.Recipe->bTwinAIRoom)
		{
			const int32 RoomIndex = Index - 1;
			const FAreaSegmentRecord* Room = Segs.IsValidIndex(RoomIndex) ? &Segs[RoomIndex] : nullptr;
			if (!Room || !Room->bTwinAIRoom || !Room->bClosed)
			{
				Fail(TEXT("the segment before the exit corridor is not the twin AI's room"));
			}
			else
			{
				const FVector In = Room->Direction.GetSafeNormal2D();
				const FBox2D RoomBox(FVector2D(Room->Bounds.Min), FVector2D(Room->Bounds.Max));
				const bool bFromRoom = RoomBox.ExpandBy(1.0).IsInside(FVector2D(Seg.Mouth)) && FMath::Abs(FVector::DotProduct(Dir, In)) < 0.01f;
				if (!bFromRoom) { Fail(FString::Printf(TEXT("the exit corridor's mouth %s is not in a side wall of the room"), *Where(Seg.Mouth))); }
				int32 InRoom = 0;
				for (TActorIterator<ATwinAITerminal> It(World); It; ++It)
				{
					const FVector P = It->GetActorLocation();
					int32 S = -1;
					float Offset = 0.f;
					C.Info->ProjectToRoute(P, S, Offset);
					const bool bIn = Room->Bounds.ExpandBy(FVector(30.0, 30.0, 10.0)).IsInsideOrOn(P);
					// On the far wall: facing back toward the entrance, its back within half a wall box of the far edge.
					const double FarEdge = FVector::DotProduct(Room->Bounds.GetCenter() - Room->Mouth, In) + 0.5 * FMath::Abs(FVector::DotProduct(Room->Bounds.GetSize(), In));
					const double At = FVector::DotProduct(P - Room->Mouth, In);
					const bool bFacing = FVector::DotProduct(It->GetActorForwardVector().GetSafeNormal2D(), -In) > 0.999f;
					const double Centred = FMath::Abs(FVector::DotProduct(P - Room->Bounds.GetCenter(), FVector(-In.Y, In.X, 0.0)));
					if (!bIn || !bFacing || FarEdge - At > 100.0 || Centred > 1.0)
					{
						Fail(FString::Printf(TEXT("%s at %s: in the room %s, facing the entrance %s, %.0f cm from the far wall's edge, %.0f cm off its middle"), *Label(*It), *Where(P),
							bIn ? TEXT("yes") : TEXT("NO"), bFacing ? TEXT("yes") : TEXT("NO"), FarEdge - At, Centred));
					}
					else
					{
						++InRoom;
					}
				}
				if (InRoom != C.Recipe->TwinAITerminalCount) { Fail(FString::Printf(TEXT("%d twin AI terminals in the room (recipe %d)"), InRoom, C.Recipe->TwinAITerminalCount)); }
				RoomLine = FString::Printf(TEXT("; twin AI's room segment %d, %.0f x %.0f cm, %d terminal(s) on its far wall"), RoomIndex, Room->Bounds.GetSize().X, Room->Bounds.GetSize().Y, InRoom);
			}
		}
		R.bPass = Failures == 0;
		R.Summary = FString::Printf(TEXT("exit corridor segment %d (last of %d), %.0f cm long, %.0f wide, exit %.0f cm in (want %.0f), %.0f cm past it (want %.0f)%s"),
			Index, Segs.Num(), Length, Seg.Width, ExitDepth, Depth, Length - ExitDepth, Run, *RoomLine);
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
	GStandDistance = FMath::Clamp(C.Generator->TerminalStandDistance, 50.f, 140.f);
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
	Result.Rules.Add(RuleOptionalTerminals(C));
	Result.Rules.Add(RuleBeacons(C));
	Result.Rules.Add(RuleWayOut(C));
	Result.Seconds = FPlatformTime::Seconds() - T0;
	return Result;
}

