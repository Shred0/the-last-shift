// SPDX-License-Identifier: MIT

#include "Area/AreaGenerator.h"

#include "AI/NavDataGenerator.h"
#include "Area/AreaExit.h"
#include "Area/AreaInfo.h"
#include "Area/AreaValidator.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/GameInstance.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "UObject/ObjectSaveContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaGenerator)

const TCHAR* LexToString(EAreaPieceKind Kind)
{
	switch (Kind)
	{
	case EAreaPieceKind::Floors: return TEXT("Floors");
	case EAreaPieceKind::Walls: return TEXT("Walls");
	case EAreaPieceKind::Curbs: return TEXT("Curbs");
	case EAreaPieceKind::Pillars: return TEXT("Pillars");
	case EAreaPieceKind::Ceilings: return TEXT("Ceilings");
	case EAreaPieceKind::Backdrop: return TEXT("Backdrop");
	}
	return TEXT("Unknown");
}

FTransform FAreaPieceHeights::MakeTransform(EAreaPieceKind Kind, const FBox& Box) const
{
	FVector Min = Box.Min;
	FVector Max = Box.Max;
	switch (Kind)
	{
	case EAreaPieceKind::Floors: Min.Z = -FloorThickness; Max.Z = 0.0; break;
	case EAreaPieceKind::Walls:
	case EAreaPieceKind::Pillars: Min.Z = -FloorThickness; Max.Z = WallHeight; break;
	case EAreaPieceKind::Curbs: Min.Z = -FloorThickness; Max.Z = CurbHeight; break;
	case EAreaPieceKind::Ceilings: Min.Z = WallHeight; Max.Z = WallHeight + FloorThickness; break;
	case EAreaPieceKind::Backdrop: break;
	}
	return FTransform(FRotator::ZeroRotator, (Min + Max) * 0.5, (Max - Min) / 100.0);
}

AAreaGenerator::AAreaGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	PCGBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PCGBounds"));
	PCGBounds->SetupAttachment(GetRootComponent());
	PCGBounds->SetBoxExtent(FVector(150000.0, 150000.0, 30000.0));
	PCGBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PCGBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	PCGBounds->SetGenerateOverlapEvents(false);
	PCGBounds->SetCanEverAffectNavigation(false);
	PCGBounds->SetHiddenInGame(true);
	PCGBounds->SetVisibility(false);

	StructurePCG = CreateDefaultSubobject<UPCGComponent>(TEXT("StructurePCG"));
	StructurePCG->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
	BackdropPCG = CreateDefaultSubobject<UPCGComponent>(TEXT("BackdropPCG"));
	BackdropPCG->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;

	ResidueClass = AResidue::StaticClass();
	TerminalClass = APowerTerminal::StaticClass();
	DoorClass = ADoorDevice::StaticClass();
	LightGroupClass = ALightGroupDevice::StaticClass();
}

AAreaGenerator* AAreaGenerator::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AAreaGenerator> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AAreaGenerator::IsAreaReady(const UWorld* World)
{
	const AAreaGenerator* Generator = Find(World);
	return !Generator || Generator->IsReady();
}

const FAreaRecipe* AAreaGenerator::GetRecipe() const
{
	return UTheLastShiftSettings::Get()->FindRecipe(Act);
}

int32 AAreaGenerator::GetGateCount() const
{
	const FAreaRecipe* Recipe = GetRecipe();
	return Recipe ? Recipe->GetGateCount() : 0;
}

FAreaPieceHeights AAreaGenerator::GetPieceHeights() const
{
	FAreaPieceHeights Heights;
	Heights.FloorThickness = FloorThickness;
	Heights.WallHeight = WallHeight;
	Heights.CurbHeight = CurbHeight;
	return Heights;
}

const TArray<FBox>& AAreaGenerator::GetPublishedPieces(EAreaPieceKind Kind) const
{
	static const TArray<FBox> Empty;
	const int32 Index = static_cast<int32>(Kind);
	return PublishedPieces.IsValidIndex(Index) ? PublishedPieces[Index].Boxes : Empty;
}

namespace AreaGen
{
	/** Grid cell. Widths snap to two cells so every centreline sits on a grid line. */
	static constexpr float Cell = 50.f;
	/** Size of the engine cube the instances scale. */
	static constexpr float CubeSize = 100.f;
	/** Coarse block (in cells) of the placement acceleration grid; at least the clearance in cells. */
	static constexpr int32 BlockCells = 8;

	enum class EKind : uint8 { Floor, Pillar };

	struct FCell
	{
		int32 Segment = -1;
		EKind Kind = EKind::Floor;
	};

	// Directions: 0 = +X, 1 = +Y, 2 = -X, 3 = -Y.
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };
	static FVector2D DirV(int32 D) { return FVector2D(Dirs[D].X, Dirs[D].Y); }
	static int32 LeftOf(int32 D) { return (D + 1) % 4; }
	static int32 Opposite(int32 D) { return (D + 2) % 4; }
	static int32 DirOf(const FVector2D& V)
	{
		if (FMath::Abs(V.X) >= FMath::Abs(V.Y)) { return V.X >= 0.0 ? 0 : 2; }
		return V.Y >= 0.0 ? 1 : 3;
	}

	static float SnapLen(float V) { return FMath::Max(Cell, FMath::RoundToFloat(V / Cell) * Cell); }
	static float SnapWidth(float V) { return FMath::Max(4.f * Cell, FMath::RoundToFloat(V / (2.f * Cell)) * 2.f * Cell); }
	static FVector2D SnapPoint(const FVector2D& P) { return FVector2D(FMath::RoundToDouble(P.X / Cell) * Cell, FMath::RoundToDouble(P.Y / Cell) * Cell); }

	/** Axis-aligned box from local coordinates around Origin: Along on D, Lateral on D's left. */
	static FBox2D MakeRect(const FVector2D& Origin, int32 D, double Along0, double Along1, double Lat0, double Lat1)
	{
		const FVector2D F = DirV(D);
		const FVector2D L = DirV(LeftOf(D));
		const FVector2D A = Origin + F * Along0 + L * Lat0;
		const FVector2D B = Origin + F * Along1 + L * Lat1;
		return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	}

	static FVector2D CellCenter(const FIntPoint& C) { return FVector2D((C.X + 0.5) * Cell, (C.Y + 0.5) * Cell); }

	static void CellsIn(const FBox2D& Box, TArray<FIntPoint>& Out)
	{
		const int32 I0 = FMath::CeilToInt(Box.Min.X / Cell - 0.5 + 1e-3);
		const int32 I1 = FMath::FloorToInt(Box.Max.X / Cell - 0.5 - 1e-3);
		const int32 J0 = FMath::CeilToInt(Box.Min.Y / Cell - 0.5 + 1e-3);
		const int32 J1 = FMath::FloorToInt(Box.Max.Y / Cell - 0.5 - 1e-3);
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				Out.Add(FIntPoint(I, J));
			}
		}
	}

	static bool Inside(const FBox2D& Box, const FVector2D& P)
	{
		return P.X >= Box.Min.X && P.X <= Box.Max.X && P.Y >= Box.Min.Y && P.Y <= Box.Max.Y;
	}

	static FBox2D Grow(const FBox2D& Box, double By) { return FBox2D(Box.Min - FVector2D(By, By), Box.Max + FVector2D(By, By)); }

	static double DistToBox(const FVector2D& P, const FBox2D& Box)
	{
		const double DX = FMath::Max3(Box.Min.X - P.X, 0.0, P.X - Box.Max.X);
		const double DY = FMath::Max3(Box.Min.Y - P.Y, 0.0, P.Y - Box.Max.Y);
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	static double DistBoxToSegment(const FBox2D& Box, const FVector2D& A, const FVector2D& B)
	{
		// Sampled: boxes and pieces are short compared with the clearances involved.
		double Best = TNumericLimits<double>::Max();
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / 25.0));
		for (int32 S = 0; S <= Steps; ++S)
		{
			Best = FMath::Min(Best, DistToBox(FMath::Lerp(A, B, double(S) / Steps), Box));
		}
		return Best;
	}

	struct FLeg
	{
		FBox2D Rect;
		FVector2D Start;
		int32 Dir = 0;
		float Length = 0.f;
	};

	struct FSpurCandidate
	{
		FVector2D Attach;  // on the parent's edge
		int32 Dir = 0;     // outward
		float MaxLength = 0.f;  // strip + platform
		FBox2D Parent;     // parent rect (allowed contact)
	};

	struct FSeg
	{
		EAreaSegmentType Type = EAreaSegmentType::Corridor;
		bool bClosed = true;
		float Width = 0.f;
		int32 SpurTarget = 0;
		TArray<FLeg> Legs;
		TArray<FSpurCandidate> SpurCandidates;
		TArray<FBox2D> Pillars;
		FBox2D Bounds = FBox2D(ForceInit);
		float RouteStart = 0.f;
		float RouteEnd = 0.f;
		int32 Spurs = 0;
		int32 Niches = 0;
		// Gate
		FVector2D DoorPos = FVector2D::ZeroVector;
		int32 DoorDir = 0;
		float PoweredSeconds = 0.f;
	};

	struct FSpot
	{
		FVector2D Pos;
		int32 Segment = -1;
		bool bOpen = false;
		float RouteDistance = 0.f;
	};

	struct FCandidateBody
	{
		TArray<FIntPoint> FloorCells;
		TArray<FIntPoint> PillarCells;
		TArray<FVector2D> Route;  // points after the head
		FVector2D Head;
		int32 Dir = 0;
		FSeg Seg;
	};

	/** The whole layout in plan, before anything is spawned. */
	struct FPlan
	{
		TMap<FIntPoint, FCell> Cells;
		TMap<FIntPoint, TArray<int32>> Blocks;  // block -> segments with cells in it
		TArray<FSeg> Segs;
		TArray<FVector2D> Route;
		TArray<int32> RouteSeg;
		TArray<float> RouteDist;
		TArray<FSpot> SpurEnds;

		bool IsFloor(const FIntPoint& C) const { const FCell* E = Cells.Find(C); return E && E->Kind == EKind::Floor; }
		bool IsPillar(const FIntPoint& C) const { const FCell* E = Cells.Find(C); return E && E->Kind == EKind::Pillar; }

		static FIntPoint BlockOf(const FIntPoint& C) { return FIntPoint(FMath::FloorToInt(float(C.X) / BlockCells), FMath::FloorToInt(float(C.Y) / BlockCells)); }

		void Commit(const TArray<FIntPoint>& NewCells, int32 Segment, EKind Kind)
		{
			for (const FIntPoint& C : NewCells)
			{
				FCell& E = Cells.FindOrAdd(C);
				E.Segment = Segment;
				E.Kind = Kind;
				Blocks.FindOrAdd(BlockOf(C)).AddUnique(Segment);
				Segs[Segment].Bounds += CellCenter(C) - FVector2D(Cell * 0.5, Cell * 0.5);
				Segs[Segment].Bounds += CellCenter(C) + FVector2D(Cell * 0.5, Cell * 0.5);
			}
		}

		/**
		 * True when NewCells overlap nothing and keep Margin cells from every other cell, except cells of Segment
		 * itself (unless bCheckSelf) and cells whose centres lie in one of AllowZones.
		 */
		bool CanPlace(const TArray<FIntPoint>& NewCells, int32 Segment, bool bCheckSelf, const TArray<FBox2D>& AllowZones, int32 Margin) const
		{
			for (const FIntPoint& C : NewCells)
			{
				if (Cells.Contains(C))
				{
					return false;
				}
				// Coarse reject-free test: no block around holds another segment's cells.
				const FIntPoint B = BlockOf(C);
				bool bNear = false;
				for (int32 BY = -1; BY <= 1 && !bNear; ++BY)
				{
					for (int32 BX = -1; BX <= 1 && !bNear; ++BX)
					{
						if (const TArray<int32>* InBlock = Blocks.Find(B + FIntPoint(BX, BY)))
						{
							for (const int32 S : *InBlock)
							{
								bNear |= bCheckSelf || S != Segment;
							}
						}
					}
				}
				if (!bNear)
				{
					continue;
				}
				for (int32 DY = -Margin; DY <= Margin; ++DY)
				{
					for (int32 DX = -Margin; DX <= Margin; ++DX)
					{
						const FIntPoint N = C + FIntPoint(DX, DY);
						const FCell* E = Cells.Find(N);
						if (!E || (!bCheckSelf && E->Segment == Segment))
						{
							continue;
						}
						const FVector2D NC = CellCenter(N);
						bool bAllowed = false;
						for (const FBox2D& Zone : AllowZones)
						{
							bAllowed |= Inside(Zone, NC);
						}
						if (!bAllowed)
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		bool IsClear(const FVector2D& P, double Radius) const
		{
			TArray<FIntPoint> Around;
			CellsIn(FBox2D(P - FVector2D(Radius, Radius), P + FVector2D(Radius, Radius)), Around);
			for (const FIntPoint& C : Around)
			{
				if (!IsFloor(C))
				{
					return false;
				}
			}
			return Around.Num() > 0;
		}

		void AddRoute(const FVector2D& P, int32 Segment)
		{
			if (Route.Num() > 0 && Route.Last().Equals(P, 0.1))
			{
				RouteSeg.Last() = Segment;
				return;
			}
			if (RouteSeg.Num() > 0)
			{
				RouteSeg.Last() = Segment;
			}
			Route.Add(P);
			RouteSeg.Add(Segment);
		}

		void ComputeRouteDistances()
		{
			RouteDist.SetNum(Route.Num());
			double D = 0.0;
			for (int32 I = 0; I < Route.Num(); ++I)
			{
				if (I > 0)
				{
					D += FVector2D::Distance(Route[I - 1], Route[I]);
				}
				RouteDist[I] = static_cast<float>(D);
			}
			for (FSeg& S : Segs)
			{
				S.RouteStart = TNumericLimits<float>::Max();
				S.RouteEnd = 0.f;
			}
			for (int32 I = 0; I + 1 < Route.Num(); ++I)
			{
				FSeg& S = Segs[RouteSeg[I]];
				S.RouteStart = FMath::Min(S.RouteStart, RouteDist[I]);
				S.RouteEnd = FMath::Max(S.RouteEnd, RouteDist[I + 1]);
			}
			for (FSeg& S : Segs)
			{
				if (S.RouteStart > S.RouteEnd)
				{
					S.RouteStart = S.RouteEnd;
				}
			}
		}

		float RouteLength() const { return RouteDist.Num() > 0 ? RouteDist.Last() : 0.f; }

		FVector2D PointAt(float S, int32* OutSegment = nullptr, FVector2D* OutDir = nullptr) const
		{
			for (int32 I = 1; I < Route.Num(); ++I)
			{
				if (RouteDist[I] >= S || I == Route.Num() - 1)
				{
					const float Piece = RouteDist[I] - RouteDist[I - 1];
					const float Alpha = Piece > 0.f ? FMath::Clamp((S - RouteDist[I - 1]) / Piece, 0.f, 1.f) : 0.f;
					if (OutSegment) { *OutSegment = RouteSeg[I - 1]; }
					if (OutDir) { *OutDir = (Route[I] - Route[I - 1]).GetSafeNormal(); }
					return FMath::Lerp(Route[I - 1], Route[I], Alpha);
				}
			}
			if (OutSegment) { *OutSegment = RouteSeg.Num() > 0 ? RouteSeg[0] : -1; }
			if (OutDir) { *OutDir = FVector2D(1.0, 0.0); }
			return Route.Num() > 0 ? Route[0] : FVector2D::ZeroVector;
		}

		float Project(const FVector2D& P) const
		{
			double Best = TNumericLimits<double>::Max();
			float BestS = 0.f;
			for (int32 I = 1; I < Route.Num(); ++I)
			{
				const FVector A(Route[I - 1].X, Route[I - 1].Y, 0.0);
				const FVector B(Route[I].X, Route[I].Y, 0.0);
				const FVector C = FMath::ClosestPointOnSegment(FVector(P.X, P.Y, 0.0), A, B);
				const double D = FVector2D::Distance(P, FVector2D(C.X, C.Y));
				if (D < Best)
				{
					Best = D;
					BestS = RouteDist[I - 1] + static_cast<float>(FVector::Dist(A, C));
				}
			}
			return BestS;
		}
	};

	/** Builds the plan from the recipe; everything random comes from Rng in a fixed order. */
	class FBuilder
	{
	public:
		FBuilder(const AAreaGenerator& InG, const FAreaRecipe& InRecipe, int32 InSeed, int32 InLayoutAttempt, float InDoorWidth, float InWalkSpeed, int32 InTerminalCost)
			: G(InG), R(InRecipe), Rng(InLayoutAttempt == 0 ? InSeed : int32(HashCombine(GetTypeHash(InSeed), GetTypeHash(InLayoutAttempt)))),
			  DoorWidth(InDoorWidth), WalkSpeed(InWalkSpeed), TerminalCost(InTerminalCost)
		{
			Margin = FMath::Max(1, FMath::CeilToInt(G.SegmentClearance / Cell));
		}

		const AAreaGenerator& G;
		const FAreaRecipe& R;
		FRandomStream Rng;
		float DoorWidth;
		float WalkSpeed;
		int32 TerminalCost;
		int32 Margin = 6;
		FPlan Plan;
		FString Error;

		// Placements in plan space.
		struct FGatePlacement { int32 Segment = -1; FVector2D Door; int32 DoorDir = 0; FVector2D Terminal; FVector2D TerminalFacing; FVector2D Regen; float PoweredSeconds = 0.f; };
		struct FLightPlacement { int32 Segment = -1; FVector2D Terminal; FVector2D TerminalFacing; TArray<FVector2D> Lights; };
		TArray<FGatePlacement> GatePlacements;
		TArray<FLightPlacement> LightPlacements;
		TArray<FVector2D> NormalResidues;
		TArray<FVector2D> UsedSideSpots;

		bool Build()
		{
			if (R.Segments.Num() == 0)
			{
				Error = TEXT("the recipe has no segments");
				return false;
			}
			FVector2D Head(0.0, 0.0);
			int32 Dir = 0;
			Plan.AddRoute(Head, 0);
			for (int32 Index = 0; Index < R.Segments.Num(); ++Index)
			{
				if (!PlaceSegment(Index, Head, Dir))
				{
					return false;
				}
			}
			Plan.ComputeRouteDistances();

			for (int32 Index = 0; Index < Plan.Segs.Num(); ++Index)
			{
				FSeg& S = Plan.Segs[Index];
				if (S.Type == EAreaSegmentType::Corridor)
				{
					PlaceNiches(Index);
					PlaceDeadEnds(Index);
				}
				else if (S.Type == EAreaSegmentType::Catwalk || S.Type == EAreaSegmentType::Hall)
				{
					PlaceOpenSpurs(Index);
				}
			}
			for (FSpot& Spot : Plan.SpurEnds)
			{
				Spot.RouteDistance = Plan.Project(Spot.Pos);
			}
			return PlaceGates() && PlaceLightGroups() && PlaceResidues();
		}

	private:
		int32 ChooseTurn(int32 D, const FVector2D& Pos)
		{
			if (D != 0)
			{
				return 0;
			}
			const int32 Toward = Pos.Y > 0.0 ? 3 : 1;
			return Rng.FRand() < 0.7f ? Toward : Opposite(Toward);
		}

		float RandRange(float A, float B) { return Rng.FRandRange(FMath::Min(A, B), FMath::Max(A, B)); }

		bool PlaceSegment(int32 Index, FVector2D& Head, int32& Dir)
		{
			const FAreaSegmentSpec& Spec = R.Segments[Index];
			const int32 Attempts = FMath::Clamp(G.PlacementAttempts, 1, 64);
			for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
			{
				FCandidateBody Body;
				Body.Seg.Type = Spec.Type;
				Body.Seg.bClosed = IsClosedAreaSegment(Spec.Type);
				Body.Seg.SpurTarget = Spec.SpurCount;
				bool bBuilt = false;
				switch (Spec.Type)
				{
				case EAreaSegmentType::Corridor:
				case EAreaSegmentType::Catwalk: bBuilt = BuildStrip(Spec, Head, Dir, Body); break;
				case EAreaSegmentType::Room: bBuilt = BuildRoom(Spec, Head, Dir, Body); break;
				case EAreaSegmentType::Gate: bBuilt = BuildGate(Spec, Head, Dir, Body); break;
				case EAreaSegmentType::Hall: bBuilt = Spec.bWeb ? BuildWebHall(Spec, Head, Dir, Body, Attempt) : BuildFloorHall(Spec, Head, Dir, Body); break;
				}
				if (!bBuilt)
				{
					return false;
				}

				TSet<FIntPoint> Unique;
				TArray<FIntPoint> All;
				for (const FIntPoint& C : Body.FloorCells) { if (!Unique.Contains(C)) { Unique.Add(C); All.Add(C); } }
				for (const FIntPoint& C : Body.PillarCells) { if (!Unique.Contains(C)) { Unique.Add(C); All.Add(C); } }

				// The predecessor may touch the new segment only around the joint.
				const double Reach = (Margin + 2) * Cell + 0.5 * FMath::Max(Body.Seg.Width, Index > 0 ? Plan.Segs[Index - 1].Width : 0.f) + 50.0;
				TArray<FBox2D> Zones;
				Zones.Add(FBox2D(Head - FVector2D(Reach, Reach), Head + FVector2D(Reach, Reach)));
				if (!Plan.CanPlace(All, Index, false, Zones, Margin))
				{
					continue;
				}

				Plan.Segs.Add(MoveTemp(Body.Seg));
				TArray<FIntPoint> Floors;
				TSet<FIntPoint> PillarSet(Body.PillarCells);
				for (const FIntPoint& C : All)
				{
					if (!PillarSet.Contains(C))
					{
						Floors.Add(C);
					}
				}
				Plan.Commit(Floors, Index, EKind::Floor);
				Plan.Commit(Body.PillarCells, Index, EKind::Pillar);
				for (const FVector2D& P : Body.Route)
				{
					Plan.AddRoute(P, Index);
				}
				Head = Body.Head;
				Dir = Body.Dir;
				return true;
			}
			Error = FString::Printf(TEXT("segment %d (%s) could not be placed without touching another segment in %d attempts"),
				Index, LexToString(Spec.Type), Attempts);
			return false;
		}

		bool BuildStrip(const FAreaSegmentSpec& Spec, const FVector2D& Head, int32 Dir, FCandidateBody& Body)
		{
			const float L = SnapLen(RandRange(Spec.LengthMin, Spec.LengthMax));
			const float W = SnapWidth(RandRange(Spec.WidthMin, Spec.WidthMax));
			Body.Seg.Width = W;
			const float MinLeg = W + 4.f * Cell;
			int32 Bends = FMath::Clamp(Spec.Bends, 0, 8);
			while (Bends > 0 && L < (Bends + 1) * MinLeg)
			{
				--Bends;
			}
			// Every leg gets MinLeg plus a random share of the rest; the last leg takes the remainder.
			TArray<float> Weights;
			float Sum = 0.f;
			for (int32 I = 0; I <= Bends; ++I)
			{
				Weights.Add(Rng.FRandRange(0.6f, 1.4f));
				Sum += Weights.Last();
			}
			const float Extra = FMath::Max(0.f, L - (Bends + 1) * MinLeg);
			TArray<float> Lengths;
			float Used = 0.f;
			for (int32 I = 0; I <= Bends; ++I)
			{
				const float Len = I == Bends ? FMath::Max(MinLeg, L - Used) : MinLeg + FMath::FloorToFloat(Extra * Weights[I] / Sum / Cell) * Cell;
				Lengths.Add(Len);
				Used += Len;
			}

			FVector2D A = Head;
			int32 D = Dir;
			for (int32 I = 0; I <= Bends; ++I)
			{
				const FVector2D B = A + DirV(D) * Lengths[I];
				const double Along0 = I == 0 ? 0.0 : -W * 0.5;
				const double Along1 = Lengths[I] + (I == Bends ? 0.0 : W * 0.5);
				FLeg Leg;
				Leg.Rect = MakeRect(A, D, Along0, Along1, -W * 0.5, W * 0.5);
				Leg.Start = A;
				Leg.Dir = D;
				Leg.Length = Lengths[I];
				CellsIn(Leg.Rect, Body.FloorCells);
				Body.Seg.Legs.Add(Leg);
				Body.Route.Add(B);
				if (I < Bends)
				{
					D = ChooseTurn(D, B);
				}
				A = B;
			}
			Body.Head = A;
			Body.Dir = D;

			if (!Body.Seg.bClosed)
			{
				// Spur candidates: both sides of every leg, away from its ends.
				for (const FLeg& Leg : Body.Seg.Legs)
				{
					for (float Along = W + 200.f; Along <= Leg.Length - W - 200.f; Along += 400.f)
					{
						for (const int32 Side : { 1, -1 })
						{
							FSpurCandidate C;
							C.Dir = Side > 0 ? LeftOf(Leg.Dir) : (LeftOf(Leg.Dir) + 2) % 4;
							C.Attach = SnapPoint(Leg.Start + DirV(Leg.Dir) * Along + DirV(C.Dir) * (W * 0.5));
							C.MaxLength = G.SpurLengthMax + G.SpurPlatformMax;
							C.Parent = Leg.Rect;
							Body.Seg.SpurCandidates.Add(C);
						}
					}
				}
			}
			return true;
		}

		bool BuildRoom(const FAreaSegmentSpec& Spec, const FVector2D& Head, int32 Dir, FCandidateBody& Body)
		{
			const float Lr = SnapWidth(RandRange(Spec.LengthMin, Spec.LengthMax));
			const float Wr = SnapWidth(RandRange(Spec.WidthMin, Spec.WidthMax));
			Body.Seg.Width = Wr;
			const float EdgeMargin = 300.f;
			const float E = Wr > 2.f * EdgeMargin ? SnapLen(RandRange(EdgeMargin, Wr - EdgeMargin)) : SnapLen(Wr * 0.5f);
			const FBox2D Rect = MakeRect(Head, Dir, 0.0, Lr, -E, Wr - E);
			FLeg Leg;
			Leg.Rect = Rect;
			Leg.Start = Head;
			Leg.Dir = Dir;
			Leg.Length = Lr;
			Body.Seg.Legs.Add(Leg);
			CellsIn(Rect, Body.FloorCells);

			const FVector2D F = DirV(Dir);
			const FVector2D Lv = DirV(LeftOf(Dir));
			const bool bTurn = Rng.FRand() < (Dir == 0 ? 0.4f : 0.7f);
			if (!bTurn)
			{
				const float Lo = -E + EdgeMargin;
				const float Hi = Wr - E - EdgeMargin;
				const float X = Hi > Lo ? SnapLen(RandRange(Lo, Hi)) : 0.f;
				const float Mid = SnapLen(Lr * 0.5f);
				Body.Route.Add(Head + F * Mid);
				Body.Route.Add(Head + F * Mid + Lv * X);
				Body.Head = Head + F * Lr + Lv * X;
				Body.Dir = Dir;
			}
			else
			{
				const int32 NewDir = ChooseTurn(Dir, Head);
				const float Lateral = NewDir == LeftOf(Dir) ? Wr - E : -E;
				const float Along = SnapLen(RandRange(Lr * 0.4f, Lr - EdgeMargin));
				Body.Route.Add(Head + F * Along);
				Body.Head = Head + F * Along + Lv * Lateral;
				Body.Dir = NewDir;
			}
			Body.Route.Add(Body.Head);

			// Pillars on a grid anchored at the entry, so the first ones flank the route close to the doorway (hiding
			// places at the entity's spawn distance from the route); clear of the walls, the openings and the route.
			const float PS = SnapWidth(G.PillarSize);
			const float Spacing = SnapWidth(G.PillarSpacing);
			const float Phase = SnapLen(Spacing * 0.75f);
			TArray<FVector2D> RouteHere;
			RouteHere.Add(Head);
			RouteHere.Append(Body.Route);
			TArray<float> Laterals;
			for (float L = Phase; L <= Wr; L += Spacing)
			{
				Laterals.Add(L);
				Laterals.Add(-L);
			}
			for (float A = Phase; A + PS * 0.5f <= Lr - 150.f; A += Spacing)
			{
				for (const float L : Laterals)
				{
					if (L - PS * 0.5f < -E + 150.f || L + PS * 0.5f > Wr - E - 150.f)
					{
						continue;
					}
					const FBox2D Pillar = MakeRect(Head, Dir, A - PS * 0.5, A + PS * 0.5, L - PS * 0.5, L + PS * 0.5);
					bool bClear = DistToBox(Head, Pillar) > 150.0 && DistToBox(Body.Head, Pillar) > 250.0;
					for (int32 I = 1; I < RouteHere.Num() && bClear; ++I)
					{
						bClear &= DistBoxToSegment(Pillar, RouteHere[I - 1], RouteHere[I]) >= G.PillarRouteClearance;
					}
					if (bClear)
					{
						Body.Seg.Pillars.Add(Pillar);
						CellsIn(Pillar, Body.PillarCells);
					}
				}
			}
			return true;
		}

		bool BuildGate(const FAreaSegmentSpec& Spec, const FVector2D& Head, int32 Dir, FCandidateBody& Body)
		{
			const float W = DoorWidth;
			if (W < 4.f * Cell || !FMath::IsNearlyZero(FMath::Fmod(W, 2.f * Cell), 0.5f))
			{
				Error = FString::Printf(TEXT("the door is %.0f cm wide; a gate needs a door width that is a multiple of %.0f cm (at least %.0f)"),
					W, 2.f * Cell, 4.f * Cell);
				return false;
			}
			const float L = SnapWidth(RandRange(Spec.LengthMin, Spec.LengthMax));
			Body.Seg.Width = W;
			FLeg Leg;
			Leg.Rect = MakeRect(Head, Dir, 0.0, L, -W * 0.5, W * 0.5);
			Leg.Start = Head;
			Leg.Dir = Dir;
			Leg.Length = L;
			Body.Seg.Legs.Add(Leg);
			CellsIn(Leg.Rect, Body.FloorCells);
			Body.Seg.DoorPos = Head + DirV(Dir) * SnapLen(L * 0.5f);
			Body.Seg.DoorDir = Dir;
			Body.Seg.PoweredSeconds = FMath::Clamp(Spec.PoweredSeconds, 0.f, 120.f);
			Body.Head = Head + DirV(Dir) * L;
			Body.Dir = Dir;
			Body.Route.Add(Body.Head);
			return true;
		}

		bool BuildWebHall(const FAreaSegmentSpec& Spec, const FVector2D& Head, int32 Dir, FCandidateBody& Body, int32 Attempt)
		{
			const float Lh = SnapWidth(RandRange(Spec.LengthMin, Spec.LengthMax));
			const float Wh = SnapWidth(RandRange(Spec.WidthMin, Spec.WidthMax));
			const float NS = SnapWidth(G.HallNodeSize);
			const float CW = SnapWidth(G.HallCatwalkWidth);
			const float Spacing = FMath::Max(G.HallWebSpacing, NS + 2.f * CW);
			const int32 Nx = FMath::Max(2, FMath::RoundToInt((Lh - NS) / Spacing) + 1);
			const int32 Ny = FMath::Max(1, FMath::RoundToInt((Wh - NS) / Spacing) + 1);
			const float Sx = SnapLen((Lh - NS) / (Nx - 1));
			const float Sy = Ny > 1 ? SnapLen((Wh - NS) / (Ny - 1)) : 0.f;
			Body.Seg.Width = CW;

			const int32 JEntry = Attempt == 0 ? Ny / 2 : Rng.RandRange(0, Ny - 1);
			const int32 JExit = Rng.RandRange(0, Ny - 1);
			const FVector2D F = DirV(Dir);
			const FVector2D Lv = DirV(LeftOf(Dir));
			auto Node = [&](int32 I, int32 J) { return Head + F * (NS * 0.5f + I * Sx) + Lv * ((J - JEntry) * Sy); };
			auto Id = [Nx](int32 I, int32 J) { return J * Nx + I; };

			struct FEdge { int32 A; int32 B; };
			TArray<FEdge> Edges;
			for (int32 J = 0; J < Ny; ++J)
			{
				for (int32 I = 0; I < Nx; ++I)
				{
					if (I + 1 < Nx) { Edges.Add({ Id(I, J), Id(I + 1, J) }); }
					if (J + 1 < Ny) { Edges.Add({ Id(I, J), Id(I, J + 1) }); }
				}
			}
			for (int32 I = Edges.Num() - 1; I > 0; --I)
			{
				Edges.Swap(I, Rng.RandRange(0, I));
			}
			TArray<int32> Parent;
			Parent.SetNum(Nx * Ny);
			for (int32 I = 0; I < Parent.Num(); ++I) { Parent[I] = I; }
			auto Find = [&Parent](int32 X) { while (Parent[X] != X) { Parent[X] = Parent[Parent[X]]; X = Parent[X]; } return X; };
			TSet<uint64> Kept;
			auto Key = [](int32 A, int32 B) { return (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B)); };
			for (const FEdge& E : Edges)
			{
				const int32 RA = Find(E.A);
				const int32 RB = Find(E.B);
				if (RA != RB)
				{
					Parent[RA] = RB;
					Kept.Add(Key(E.A, E.B));
				}
				else if (Rng.FRand() < G.HallLoopChance)
				{
					Kept.Add(Key(E.A, E.B));
				}
			}

			// Shortest path entry -> exit (BFS, neighbours in a fixed order).
			const int32 Start = Id(0, JEntry);
			const int32 Goal = Id(Nx - 1, JExit);
			auto ShortestPath = [&](TArray<int32>& OutPath)
			{
				TArray<int32> Prev;
				Prev.Init(-1, Nx * Ny);
				TArray<int32> Queue;
				Queue.Add(Start);
				Prev[Start] = Start;
				for (int32 Q = 0; Q < Queue.Num(); ++Q)
				{
					const int32 Cur = Queue[Q];
					const int32 CI = Cur % Nx;
					const int32 CJ = Cur / Nx;
					const FIntPoint Steps[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(0, -1), FIntPoint(-1, 0) };
					for (const FIntPoint& S : Steps)
					{
						const int32 NI = CI + S.X;
						const int32 NJ = CJ + S.Y;
						if (NI < 0 || NI >= Nx || NJ < 0 || NJ >= Ny) { continue; }
						const int32 N = Id(NI, NJ);
						if (Prev[N] >= 0 || !Kept.Contains(Key(Cur, N))) { continue; }
						Prev[N] = Cur;
						Queue.Add(N);
					}
				}
				OutPath.Reset();
				if (Prev[Goal] < 0)
				{
					return false;
				}
				for (int32 Cur = Goal; Cur != Start; Cur = Prev[Cur]) { OutPath.Insert(Cur, 0); }
				OutPath.Insert(Start, 0);
				return true;
			};
			TArray<int32> Path;
			if (!ShortestPath(Path))
			{
				Error = TEXT("web hall: the spanning tree does not connect entry and exit (bug)");
				return false;
			}
			// A random tree can wind: past HallMaxDetourEdges beyond the Manhattan distance, add a staircase of
			// catwalks (along the entry row, across at a random column, along the exit row) as a shortcut.
			const int32 Manhattan = (Nx - 1) + FMath::Abs(JExit - JEntry);
			if (Path.Num() - 1 > Manhattan + FMath::Clamp(G.HallMaxDetourEdges, 0, 20))
			{
				const int32 Turn = Rng.RandRange(0, Nx - 1);
				for (int32 I = 0; I < Turn; ++I) { Kept.Add(Key(Id(I, JEntry), Id(I + 1, JEntry))); }
				for (int32 J = FMath::Min(JEntry, JExit); J < FMath::Max(JEntry, JExit); ++J) { Kept.Add(Key(Id(Turn, J), Id(Turn, J + 1))); }
				for (int32 I = Turn; I < Nx - 1; ++I) { Kept.Add(Key(Id(I, JExit), Id(I + 1, JExit))); }
				ShortestPath(Path);
			}

			// Geometry: platforms at every node, catwalks on kept edges.
			for (int32 J = 0; J < Ny; ++J)
			{
				for (int32 I = 0; I < Nx; ++I)
				{
					FLeg Platform;
					Platform.Rect = MakeRect(Node(I, J), Dir, -NS * 0.5, NS * 0.5, -NS * 0.5, NS * 0.5);
					Platform.Start = Node(I, J);
					Platform.Dir = Dir;
					Platform.Length = NS;
					CellsIn(Platform.Rect, Body.FloorCells);
					Body.Seg.Legs.Add(Platform);
				}
			}
			for (const uint64 K : Kept)
			{
				const int32 A = int32(K >> 32);
				const int32 B = int32(K & 0xffffffffu);
				const FVector2D PA = Node(A % Nx, A / Nx);
				const FVector2D PB = Node(B % Nx, B / Nx);
				const int32 D = DirOf(PB - PA);
				const FBox2D Walk = MakeRect(PA, D, 0.0, FVector2D::Distance(PA, PB), -CW * 0.5, CW * 0.5);
				CellsIn(Walk, Body.FloorCells);
			}
			// Kept is a set: sort the cells so the plan does not depend on hashing.
			Body.FloorCells.Sort([](const FIntPoint& X, const FIntPoint& Y) { return X.Y != Y.Y ? X.Y < Y.Y : X.X < Y.X; });

			for (const int32 N : Path)
			{
				Body.Route.Add(Node(N % Nx, N / Nx));
			}
			Body.Head = Node(Nx - 1, JExit) + F * (NS * 0.5f);
			Body.Dir = Dir;
			Body.Route.Add(Body.Head);

			// Spur candidates: every node direction without a catwalk, never back to the gate or onto the way out.
			for (int32 J = 0; J < Ny; ++J)
			{
				for (int32 I = 0; I < Nx; ++I)
				{
					const int32 Here = Id(I, J);
					if (Here == Start || Here == Goal) { continue; }
					const int32 LocalDirs[4] = { Dir, LeftOf(Dir), Opposite(Dir), Opposite(LeftOf(Dir)) };
					const FIntPoint LocalSteps[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };
					for (int32 K = 0; K < 4; ++K)
					{
						const int32 NI = I + LocalSteps[K].X;
						const int32 NJ = J + LocalSteps[K].Y;
						const bool bInBounds = NI >= 0 && NI < Nx && NJ >= 0 && NJ < Ny;
						if (bInBounds && Kept.Contains(Key(Here, Id(NI, NJ)))) { continue; }
						if ((I == 0 && K == 2) || (I == Nx - 1 && K == 0)) { continue; }
						const float Gap = (K % 2 == 0 ? Sx : Sy) - NS;
						FSpurCandidate C;
						C.Dir = LocalDirs[K];
						C.Attach = Node(I, J) + DirV(C.Dir) * (NS * 0.5f);
						C.MaxLength = bInBounds ? Gap - G.SegmentClearance - Cell : G.SpurLengthMax + G.SpurPlatformMax;
						C.Parent = MakeRect(Node(I, J), Dir, -NS * 0.5, NS * 0.5, -NS * 0.5, NS * 0.5);
						Body.Seg.SpurCandidates.Add(C);
					}
				}
			}
			return true;
		}

		bool BuildFloorHall(const FAreaSegmentSpec& Spec, const FVector2D& Head, int32 Dir, FCandidateBody& Body)
		{
			const float Lh = SnapWidth(RandRange(Spec.LengthMin, Spec.LengthMax));
			const float Wh = SnapWidth(RandRange(Spec.WidthMin, Spec.WidthMax));
			Body.Seg.Width = Wh;
			const float EdgeMargin = 400.f;
			const float E = Wh > 2.f * EdgeMargin ? SnapLen(RandRange(EdgeMargin, Wh - EdgeMargin)) : SnapLen(Wh * 0.5f);
			FLeg Leg;
			Leg.Rect = MakeRect(Head, Dir, 0.0, Lh, -E, Wh - E);
			Leg.Start = Head;
			Leg.Dir = Dir;
			Leg.Length = Lh;
			Body.Seg.Legs.Add(Leg);
			CellsIn(Leg.Rect, Body.FloorCells);
			const FVector2D F = DirV(Dir);
			const FVector2D Lv = DirV(LeftOf(Dir));
			const float Lo = -E + EdgeMargin;
			const float Hi = Wh - E - EdgeMargin;
			const float X = Hi > Lo ? SnapLen(RandRange(Lo, Hi)) : 0.f;
			const float Mid = SnapLen(Lh * 0.5f);
			Body.Route.Add(Head + F * Mid);
			Body.Route.Add(Head + F * Mid + Lv * X);
			Body.Head = Head + F * Lh + Lv * X;
			Body.Dir = Dir;
			Body.Route.Add(Body.Head);
			for (float Along = 600.f; Along <= Lh - 600.f; Along += 800.f)
			{
				for (const int32 Side : { 1, -1 })
				{
					FSpurCandidate C;
					C.Dir = Side > 0 ? LeftOf(Dir) : Opposite(LeftOf(Dir));
					C.Attach = SnapPoint(Head + F * Along + Lv * (Side > 0 ? Wh - E : -E));
					C.MaxLength = G.SpurLengthMax + G.SpurPlatformMax;
					C.Parent = Leg.Rect;
					Body.Seg.SpurCandidates.Add(C);
				}
			}
			return true;
		}

		void PlaceNiches(int32 Index)
		{
			FSeg& S = Plan.Segs[Index];
			if (G.NicheSpacing <= 0.f)
			{
				return;
			}
			const float W = S.Width;
			const float NW = SnapWidth(G.NicheWidth);
			const float Depth = SnapLen(G.NicheDepth);
			const float Step = FMath::Max(SnapLen(G.NicheSpacing), NW + Cell);
			// One rhythm along the whole corridor (not restarted at each leg), so a short leg still gets niches;
			// positions inside a bend's corner square or near a joint are skipped.
			float Next = Index == 0 ? NW * 0.5f : 300.f + NW * 0.5f;
			float LegOffset = 0.f;
			int32 Side = Rng.FRand() < 0.5f ? 1 : -1;
			for (int32 LegIndex = 0; LegIndex < S.Legs.Num(); ++LegIndex)
			{
				const FLeg Leg = S.Legs[LegIndex];
				const bool bLast = LegIndex == S.Legs.Num() - 1;
				const float From = LegIndex == 0 ? 0.f : W * 0.5f + NW * 0.5f + Cell;
				const float To = Leg.Length - (bLast ? 300.f + NW * 0.5f : W * 0.5f + NW * 0.5f + Cell);
				for (; Next - LegOffset <= To; Next += Step)
				{
					const float At = SnapLen(Next - LegOffset);
					if (At < From)
					{
						continue;
					}
					for (const int32 Try : { Side, -Side })
					{
						const FBox2D Rect = Try > 0
							? MakeRect(Leg.Start, Leg.Dir, At - NW * 0.5, At + NW * 0.5, W * 0.5, W * 0.5 + Depth)
							: MakeRect(Leg.Start, Leg.Dir, At - NW * 0.5, At + NW * 0.5, -W * 0.5 - Depth, -W * 0.5);
						TArray<FIntPoint> Cells;
						CellsIn(Rect, Cells);
						if (Plan.CanPlace(Cells, Index, true, { Leg.Rect }, 2))
						{
							Plan.Commit(Cells, Index, EKind::Floor);
							++S.Niches;
							break;
						}
					}
					Side = -Side;
				}
				LegOffset += Leg.Length;
			}
		}

		void PlaceDeadEnds(int32 Index)
		{
			FSeg& S = Plan.Segs[Index];
			const float W = S.Width;
			for (int32 Want = 0; Want < S.SpurTarget; ++Want)
			{
				for (int32 Try = 0; Try < 12; ++Try)
				{
					const FLeg Leg = S.Legs[Rng.RandRange(0, S.Legs.Num() - 1)];
					const float Lo = W + 200.f;
					const float Hi = Leg.Length - W - 200.f;
					if (Hi <= Lo)
					{
						continue;
					}
					const float At = SnapLen(RandRange(Lo, Hi));
					const float DW = SnapWidth(RandRange(4.f * Cell, W));
					const float Len = SnapLen(RandRange(G.DeadEndLengthMin, G.DeadEndLengthMax));
					const int32 Side = Rng.FRand() < 0.5f ? 1 : -1;
					const FBox2D Rect = Side > 0
						? MakeRect(Leg.Start, Leg.Dir, At - DW * 0.5, At + DW * 0.5, W * 0.5, W * 0.5 + Len)
						: MakeRect(Leg.Start, Leg.Dir, At - DW * 0.5, At + DW * 0.5, -W * 0.5 - Len, -W * 0.5);
					TArray<FIntPoint> Cells;
					CellsIn(Rect, Cells);
					if (Plan.CanPlace(Cells, Index, true, { Grow(Leg.Rect, Margin * Cell) }, Margin))
					{
						Plan.Commit(Cells, Index, EKind::Floor);
						++S.Spurs;
						FSpot Spot;
						Spot.Pos = Leg.Start + DirV(Leg.Dir) * At + DirV(LeftOf(Leg.Dir)) * (Side * (W * 0.5f + Len - DW * 0.5f));
						Spot.Segment = Index;
						Spot.bOpen = false;
						Plan.SpurEnds.Add(Spot);
						break;
					}
				}
			}
		}

		void PlaceOpenSpurs(int32 Index)
		{
			FSeg& S = Plan.Segs[Index];
			TArray<FSpurCandidate> Candidates = S.SpurCandidates;
			for (int32 I = Candidates.Num() - 1; I > 0; --I)
			{
				Candidates.Swap(I, Rng.RandRange(0, I));
			}
			const float SW = SnapWidth(G.SpurWidth);
			for (const FSpurCandidate& C : Candidates)
			{
				if (S.Spurs >= S.SpurTarget)
				{
					break;
				}
				const float PS = SnapWidth(RandRange(G.SpurPlatformMin, G.SpurPlatformMax));
				float Len = SnapLen(RandRange(G.SpurLengthMin, G.SpurLengthMax));
				Len = FMath::Min(Len, SnapLen(C.MaxLength - PS));
				if (Len < 2.f * Cell)
				{
					continue;
				}
				const FBox2D Strip = MakeRect(C.Attach, C.Dir, 0.0, Len, -SW * 0.5, SW * 0.5);
				const FVector2D PlatformCenter = C.Attach + DirV(C.Dir) * (Len + PS * 0.5f);
				const FBox2D Platform = MakeRect(PlatformCenter, C.Dir, -PS * 0.5, PS * 0.5, -PS * 0.5, PS * 0.5);
				TArray<FIntPoint> Cells;
				CellsIn(Strip, Cells);
				CellsIn(Platform, Cells);
				TSet<FIntPoint> Unique(Cells);
				TArray<FIntPoint> UniqueCells = Unique.Array();
				UniqueCells.Sort([](const FIntPoint& X, const FIntPoint& Y) { return X.Y != Y.Y ? X.Y < Y.Y : X.X < Y.X; });
				if (Plan.CanPlace(UniqueCells, Index, true, { Grow(C.Parent, Margin * Cell) }, Margin))
				{
					Plan.Commit(UniqueCells, Index, EKind::Floor);
					++S.Spurs;
					FSpot Spot;
					Spot.Pos = PlatformCenter;
					Spot.Segment = Index;
					Spot.bOpen = true;
					Plan.SpurEnds.Add(Spot);
				}
			}
		}

		bool NearUsed(const FVector2D& P, double Radius) const
		{
			for (const FVector2D& U : UsedSideSpots)
			{
				if (FVector2D::Distance(U, P) < Radius) { return true; }
			}
			return false;
		}

		/**
		 * A spot beside the route at or before route distance S (stepping back to MinS), with its back against a
		 * straight wall: solid behind it over its whole width and a little beyond, so it never stands in front of a
		 * niche, a side passage or a doorway (where it would block the opening on the navmesh); facing the centreline.
		 */
		bool FindSideSpot(float S, float MinS, FVector2D& OutPos, FVector2D& OutFacing)
		{
			const int32 FirstSide = Rng.FRand() < 0.5f ? 1 : -1;
			for (float At = S; At >= MinS; At -= 100.f)
			{
				FVector2D Dir;
				const FVector2D P = Plan.PointAt(At, nullptr, &Dir);
				const FVector2D Lv(-Dir.Y, Dir.X);
				for (const int32 Side : { FirstSide, -FirstSide })
				{
					const FVector2D Out = Lv * Side;
					// Distance from the centreline to the wall on this side.
					float Wall = -1.f;
					for (float D = 0.f; D <= 1000.f; D += Cell * 0.5f)
					{
						if (!Plan.IsFloor(FIntPoint(FMath::FloorToInt((P.X + Out.X * D) / Cell), FMath::FloorToInt((P.Y + Out.Y * D) / Cell))))
						{
							Wall = D;
							break;
						}
					}
					const float Offset = FMath::Min(G.TerminalSideOffset, Wall - 45.f);
					if (Wall < 0.f || Offset < 60.f)
					{
						continue;
					}
					const FVector2D T = P + Out * Offset;
					bool bStraightWall = true;
					for (const float Along : { -150.f, -75.f, 0.f, 75.f, 150.f })
					{
						const FVector2D Behind = T + Dir * Along + Out * (Wall - Offset + Cell * 0.5f);
						const FVector2D Front = T + Dir * Along + Out * (Wall - Offset - Cell * 0.5f);
						const FIntPoint BehindCell(FMath::FloorToInt(Behind.X / Cell), FMath::FloorToInt(Behind.Y / Cell));
						const FIntPoint FrontCell(FMath::FloorToInt(Front.X / Cell), FMath::FloorToInt(Front.Y / Cell));
						bStraightWall &= !Plan.Cells.Contains(BehindCell) && Plan.IsFloor(FrontCell);
					}
					const FVector2D Stand = T - Out * 90.f;
					if (bStraightWall && Plan.IsClear(T, 40.0) && Plan.IsClear(Stand, 60.0) && !NearUsed(T, 300.0))
					{
						OutPos = T;
						OutFacing = -Out;
						UsedSideSpots.Add(T);
						return true;
					}
				}
			}
			return false;
		}

		bool FindCentreSpot(float S, float MinS, FVector2D& OutPos)
		{
			for (int32 K = 0; K < 60; ++K)
			{
				const float At = S - (K % 2 == 0 ? 1.f : -1.f) * (K / 2) * 100.f;
				if (At < MinS || At > Plan.RouteLength()) { continue; }
				const FVector2D P = Plan.PointAt(At);
				if (Plan.IsClear(P, 100.0))
				{
					OutPos = P;
					return true;
				}
			}
			return false;
		}

		bool PlaceGates()
		{
			for (int32 Index = 0; Index < Plan.Segs.Num(); ++Index)
			{
				const FSeg& S = Plan.Segs[Index];
				if (S.Type != EAreaSegmentType::Gate)
				{
					continue;
				}
				FGatePlacement Gate;
				Gate.Segment = Index;
				Gate.Door = S.DoorPos;
				Gate.DoorDir = S.DoorDir;
				Gate.PoweredSeconds = S.PoweredSeconds;
				const float DoorS = Plan.Project(S.DoorPos);
				// A timed door: the walk from the terminal must outlast it. A door that stays open: a short, fixed distance.
				const float Needed = S.PoweredSeconds > 0.f
					? S.PoweredSeconds * WalkSpeed * FMath::Clamp(G.TerminalDistanceFactor, 1.05f, 5.f)
					: FMath::Clamp(G.UntimedTerminalDistance, 200.f, 5000.f);
				const float TerminalS = DoorS - Needed;
				if (TerminalS < 150.f || !FindSideSpot(TerminalS, 100.f, Gate.Terminal, Gate.TerminalFacing))
				{
					Error = FString::Printf(TEXT("gate %d: the terminal needs %.0f m of route before the door (%.1f s at %.0f cm/s x %.2f), the route has %.0f m"),
						Index, Needed / 100.f, S.PoweredSeconds, WalkSpeed, G.TerminalDistanceFactor, DoorS / 100.f);
					return false;
				}
				const float TermS = Plan.Project(Gate.Terminal);
				if (!FindCentreSpot(FMath::Max(100.f, TermS - G.RegenResidueOffset), 50.f, Gate.Regen) || Plan.Project(Gate.Regen) >= TermS)
				{
					Error = FString::Printf(TEXT("gate %d: no clear spot for the regenerating residue before the terminal"), Index);
					return false;
				}
				GatePlacements.Add(Gate);
			}
			return true;
		}

		bool PlaceLightGroups()
		{
			TArray<int32> Order;
			for (const EAreaSegmentType Preferred : { EAreaSegmentType::Room, EAreaSegmentType::Corridor })
			{
				for (int32 Index = 0; Index < Plan.Segs.Num(); ++Index)
				{
					if (Plan.Segs[Index].Type == Preferred) { Order.Add(Index); }
				}
			}
			for (int32 Group = 0; Group < R.LightGroupCount; ++Group)
			{
				if (!Order.IsValidIndex(Group))
				{
					Error = FString::Printf(TEXT("light group %d: no closed room or corridor left to light"), Group);
					return false;
				}
				const FSeg& S = Plan.Segs[Order[Group]];
				FLightPlacement Lights;
				Lights.Segment = Order[Group];
				const float Start = S.RouteStart + 300.f;
				if (!FindSideSpot(Start, FMath::Max(50.f, S.RouteStart - 500.f), Lights.Terminal, Lights.TerminalFacing))
				{
					Error = FString::Printf(TEXT("light group %d: no spot for its terminal"), Group);
					return false;
				}
				for (int32 K = 0; K < FMath::Clamp(G.LightsPerGroup, 1, 12); ++K)
				{
					Lights.Lights.Add(Plan.PointAt(FMath::Min(Start + 200.f + K * G.LightSpacing, Plan.RouteLength())));
				}
				LightPlacements.Add(Lights);
			}
			return true;
		}

		bool PlaceResidues()
		{
			const int32 Gates = GatePlacements.Num();
			const int32 Normal = R.ResidueCount - Gates;
			if (Normal < 0)
			{
				Error = FString::Printf(TEXT("ResidueCount %d is below the gate count %d (each gate has a regenerating residue)"), R.ResidueCount, Gates);
				return false;
			}
			TArray<FVector2D> Placed;
			for (const FGatePlacement& Gate : GatePlacements)
			{
				Placed.Add(Gate.Regen);
			}
			auto SpacedOk = [&](const FVector2D& P)
			{
				for (const FVector2D& Q : Placed)
				{
					if (FVector2D::Distance(P, Q) < G.MinResidueSpacing) { return false; }
				}
				return true;
			};

			struct FCand { FVector2D Pos; float S; bool bOpen; bool bSpurEnd; };
			TArray<FCand> Cands;
			const float L = Plan.RouteLength();
			for (float S = 100.f; S <= L - 100.f; S += 100.f)
			{
				int32 Seg = -1;
				const FVector2D P = Plan.PointAt(S, &Seg);
				if (Plan.IsClear(P, 100.0))
				{
					Cands.Add({ P, S, Plan.Segs.IsValidIndex(Seg) && !Plan.Segs[Seg].bClosed, false });
				}
			}
			for (const FSpot& Spot : Plan.SpurEnds)
			{
				if (Plan.IsClear(Spot.Pos, 100.0))
				{
					Cands.Add({ Spot.Pos, Spot.RouteDistance, Spot.bOpen, true });
				}
			}

			// Even targets along the route, then enough of them before every gate to pay for it.
			TArray<float> Targets;
			TArray<float> MaxS;
			for (int32 J = 0; J < Normal; ++J)
			{
				Targets.Add((J + 0.5f) * L / FMath::Max(Normal, 1));
				MaxS.Add(L);
			}
			int32 CostSoFar = 0;
			for (const FGatePlacement& Gate : GatePlacements)
			{
				CostSoFar += TerminalCost;
				const float DoorS = Plan.Project(Gate.Door) - 200.f;
				int32 Before = 0;
				for (const float T : Targets) { Before += T < DoorS ? 1 : 0; }
				for (int32 J = 0; J < Targets.Num() && Before < CostSoFar; ++J)
				{
					if (Targets[J] >= DoorS)
					{
						Targets[J] = DoorS * Rng.FRandRange(0.2f, 0.8f);
						++Before;
					}
				}
				if (Before < CostSoFar)
				{
					Error = FString::Printf(TEXT("not enough residues to pay gate %d before reaching it"), Gate.Segment);
					return false;
				}
				for (int32 J = 0; J < Targets.Num(); ++J)
				{
					if (Targets[J] < DoorS) { MaxS[J] = FMath::Min(MaxS[J], DoorS); }
				}
			}
			TArray<int32> Order;
			for (int32 J = 0; J < Targets.Num(); ++J) { Order.Add(J); }
			Order.Sort([&Targets](int32 A, int32 B) { return Targets[A] < Targets[B]; });

			const float Window = 0.5f * L / FMath::Max(Normal, 1);
			for (const int32 J : Order)
			{
				const float T = Targets[J];
				int32 Seg = -1;
				Plan.PointAt(T, &Seg);
				const bool bOpenHere = Plan.Segs.IsValidIndex(Seg) && !Plan.Segs[Seg].bClosed;
				int32 Best = -1;
				float BestCost = TNumericLimits<float>::Max();
				for (int32 Pass = 0; Pass < 3 && Best < 0; ++Pass)
				{
					for (int32 C = 0; C < Cands.Num(); ++C)
					{
						const FCand& K = Cands[C];
						if (K.S > MaxS[J] || !SpacedOk(K.Pos)) { continue; }
						const float Off = FMath::Abs(K.S - T);
						if (Pass == 0 && !(bOpenHere && K.bOpen && K.bSpurEnd && Off <= Window)) { continue; }
						if (Pass == 1 && Off > Window) { continue; }
						if (Off < BestCost) { BestCost = Off; Best = C; }
					}
				}
				if (Best < 0)
				{
					Error = FString::Printf(TEXT("residue %d: no clear spot %.0f m from every other residue"), J, G.MinResidueSpacing / 100.f);
					return false;
				}
				Placed.Add(Cands[Best].Pos);
				NormalResidues.Add(Cands[Best].Pos);
			}
			return true;
		}
	};

	// --- Geometry from the plan ---

	/** The plan's boxes; structure kinds carry their XY footprint (their Z comes from FAreaPieceHeights). */
	struct FGeometryPlan
	{
		TArray<FBox> Floors, Walls, Curbs, Pillars, Ceilings, Backdrop;
		TArray<FAreaFloorRect> FloorRects;
	};

	static FBox BoxTransform(const FVector& Min, const FVector& Max)
	{
		return FBox(Min, Max);
	}

	/** Greedy merge of a cell set into rectangles (row-major, deterministic). */
	static void MergeRects(const TArray<FIntPoint>& InCells, TArray<FIntRect>& Out)
	{
		TSet<FIntPoint> Set(InCells);
		TArray<FIntPoint> Sorted = InCells;
		Sorted.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
		TSet<FIntPoint> Used;
		for (const FIntPoint& C : Sorted)
		{
			if (Used.Contains(C)) { continue; }
			int32 X1 = C.X;
			while (Set.Contains(FIntPoint(X1 + 1, C.Y)) && !Used.Contains(FIntPoint(X1 + 1, C.Y))) { ++X1; }
			int32 Y1 = C.Y;
			for (;;)
			{
				bool bRow = true;
				for (int32 X = C.X; X <= X1 && bRow; ++X)
				{
					const FIntPoint N(X, Y1 + 1);
					bRow = Set.Contains(N) && !Used.Contains(N);
				}
				if (!bRow) { break; }
				++Y1;
			}
			for (int32 Y = C.Y; Y <= Y1; ++Y)
			{
				for (int32 X = C.X; X <= X1; ++X) { Used.Add(FIntPoint(X, Y)); }
			}
			Out.Add(FIntRect(C.X, C.Y, X1, Y1));  // inclusive max
		}
	}

	static void BuildGeometry(const AAreaGenerator& G, const FPlan& Plan, FRandomStream& Rng, FGeometryPlan& Out)
	{
		const float FT = G.FloorThickness;
		const float T = G.WallThickness;

		// Floors and ceilings, per segment.
		TArray<TArray<FIntPoint>> SegFloor;
		TArray<TArray<FIntPoint>> SegAll;
		SegFloor.SetNum(Plan.Segs.Num());
		SegAll.SetNum(Plan.Segs.Num());
		for (const TPair<FIntPoint, FCell>& Pair : Plan.Cells)
		{
			SegAll[Pair.Value.Segment].Add(Pair.Key);
			if (Pair.Value.Kind == EKind::Floor)
			{
				// No floor under pillars: a floor face inside a pillar becomes a navmesh island.
				SegFloor[Pair.Value.Segment].Add(Pair.Key);
			}
		}
		for (int32 S = 0; S < Plan.Segs.Num(); ++S)
		{
			TArray<FIntRect> Rects;
			MergeRects(SegFloor[S], Rects);
			for (const FIntRect& R : Rects)
			{
				const FVector Min(R.Min.X * Cell, R.Min.Y * Cell, -FT);
				const FVector Max((R.Max.X + 1) * Cell, (R.Max.Y + 1) * Cell, 0.0);
				Out.Floors.Add(BoxTransform(Min, Max));
				FAreaFloorRect Rect;
				Rect.Box = FBox(Min, Max);
				Rect.Segment = S;
				Rect.bOpen = !Plan.Segs[S].bClosed;
				Out.FloorRects.Add(Rect);
			}
			if (Plan.Segs[S].bClosed)
			{
				TArray<FIntRect> CeilingRects;
				MergeRects(SegAll[S], CeilingRects);
				for (const FIntRect& R : CeilingRects)
				{
					Out.Ceilings.Add(BoxTransform(FVector(R.Min.X * Cell - T, R.Min.Y * Cell - T, G.WallHeight),
						FVector((R.Max.X + 1) * Cell + T, (R.Max.Y + 1) * Cell + T, G.WallHeight + FT)));
				}
			}
			for (const FBox2D& P : Plan.Segs[S].Pillars)
			{
				Out.Pillars.Add(BoxTransform(FVector(P.Min.X, P.Min.Y, -FT), FVector(P.Max.X, P.Max.Y, G.WallHeight)));
			}
		}

		// Walls (closed) and curbs (open) on every edge between floor and nothing.
		TMap<FIntVector, TArray<int32>> Runs;  // (Dir, Line, Wall) -> run coordinates
		TArray<FIntVector> KeyOrder;
		for (const TPair<FIntPoint, FCell>& Pair : Plan.Cells)
		{
			if (Pair.Value.Kind != EKind::Floor) { continue; }
			const FIntPoint C = Pair.Key;
			for (int32 D = 0; D < 4; ++D)
			{
				const FIntPoint N = C + Dirs[D];
				if (Plan.Cells.Contains(N)) { continue; }
				const bool bWall = Plan.Segs[Pair.Value.Segment].bClosed;
				const int32 Line = D == 0 ? C.X + 1 : D == 2 ? C.X : D == 1 ? C.Y + 1 : C.Y;
				const int32 RunCoord = (D == 0 || D == 2) ? C.Y : C.X;
				const FIntVector Key(D, Line, bWall ? 1 : 0);
				TArray<int32>* List = Runs.Find(Key);
				if (!List)
				{
					List = &Runs.Add(Key);
					KeyOrder.Add(Key);
				}
				List->Add(RunCoord);
			}
		}
		KeyOrder.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z; });
		for (const FIntVector& Key : KeyOrder)
		{
			TArray<int32> Coords = Runs[Key];
			Coords.Sort();
			const int32 D = Key.X;
			const int32 Line = Key.Y;
			const bool bWall = Key.Z != 0;
			const float Height = bWall ? G.WallHeight : G.CurbHeight;
			const bool bAlongY = D == 0 || D == 2;
			// The cell column/row just outside the edge.
			const int32 Outside = (D == 0 || D == 1) ? Line : Line - 1;
			auto DiagonalIsFloor = [&](int32 RunCoord)
			{
				const FIntPoint Diag = bAlongY ? FIntPoint(Outside, RunCoord) : FIntPoint(RunCoord, Outside);
				return Plan.IsFloor(Diag);
			};
			int32 R0 = Coords[0];
			for (int32 I = 1; I <= Coords.Num(); ++I)
			{
				if (I < Coords.Num() && Coords[I] == Coords[I - 1] + 1) { continue; }
				const int32 R1 = Coords[I - 1];
				const float Ext0 = DiagonalIsFloor(R0 - 1) ? 0.f : T;
				const float Ext1 = DiagonalIsFloor(R1 + 1) ? 0.f : T;
				const float LineCm = Line * Cell;
				const float Out0 = (D == 0 || D == 1) ? LineCm : LineCm - T;
				const float Out1 = Out0 + T;
				const float Run0 = R0 * Cell - Ext0;
				const float Run1 = (R1 + 1) * Cell + Ext1;
				const FVector Min = bAlongY ? FVector(Out0, Run0, -FT) : FVector(Run0, Out0, -FT);
				const FVector Max = bAlongY ? FVector(Out1, Run1, Height) : FVector(Run1, Out1, Height);
				(bWall ? Out.Walls : Out.Curbs).Add(BoxTransform(Min, Max));
				if (I < Coords.Num()) { R0 = Coords[I]; }
			}
		}

		// Backdrop: towers and slabs around open segments, out to BackdropRadius, clear of every floor.
		const float Density = FMath::Clamp(G.BackdropDensity, 0.f, 20.f);
		const float Radius = FMath::Clamp(G.BackdropRadius, 0.f, 100000.f);
		TArray<FBox2D> Open;
		for (const FSeg& S : Plan.Segs)
		{
			if (!S.bClosed && S.Bounds.bIsValid) { Open.Add(S.Bounds); }
		}
		if (Density <= 0.f || Radius <= 0.f || Open.Num() == 0)
		{
			return;
		}
		constexpr float CoarseSize = 500.f;
		TSet<FIntPoint> Coarse;
		for (const TPair<FIntPoint, FCell>& Pair : Plan.Cells)
		{
			const FVector2D P = CellCenter(Pair.Key);
			Coarse.Add(FIntPoint(FMath::FloorToInt(P.X / CoarseSize), FMath::FloorToInt(P.Y / CoarseSize)));
		}
		auto Clear = [&](const FBox2D& Box)
		{
			const FBox2D B = Grow(Box, G.BackdropClearance);
			for (int32 Y = FMath::FloorToInt(B.Min.Y / CoarseSize); Y <= FMath::FloorToInt(B.Max.Y / CoarseSize); ++Y)
			{
				for (int32 X = FMath::FloorToInt(B.Min.X / CoarseSize); X <= FMath::FloorToInt(B.Max.X / CoarseSize); ++X)
				{
					if (Coarse.Contains(FIntPoint(X, Y))) { return false; }
				}
			}
			return true;
		};
		FBox2D Region(ForceInit);
		for (const FBox2D& B : Open) { Region += Grow(B, Radius); }
		const float GridStep = FMath::Sqrt(1.0e8f / Density);  // 1 ha = 1e8 cm^2
		const int32 MaxMasses = 6000;
		for (float Y = Region.Min.Y; Y < Region.Max.Y && Out.Backdrop.Num() < MaxMasses; Y += GridStep)
		{
			for (float X = Region.Min.X; X < Region.Max.X && Out.Backdrop.Num() < MaxMasses; X += GridStep)
			{
				const FVector2D Q(X + Rng.FRand() * GridStep, Y + Rng.FRand() * GridStep);
				const bool bSlab = Rng.FRand() < G.BackdropSlabShare;
				const float SizeA = bSlab ? Rng.FRandRange(2000.f, 5000.f) : Rng.FRandRange(500.f, 2000.f);
				const float SizeB = bSlab ? Rng.FRandRange(2000.f, 5000.f) : SizeA * Rng.FRandRange(0.6f, 1.4f);
				const float ZA = Rng.FRandRange(0.f, 1.f);
				const float ZB = Rng.FRandRange(0.f, 1.f);
				double Nearest = TNumericLimits<double>::Max();
				for (const FBox2D& B : Open) { Nearest = FMath::Min(Nearest, DistToBox(Q, B)); }
				if (Nearest > Radius) { continue; }
				const FBox2D Foot(Q - FVector2D(SizeA, SizeB), Q + FVector2D(SizeA, SizeB));
				if (bSlab)
				{
					// Far above or far below the floor: no horizontal clearance needed.
					const bool bAbove = ZA < 0.5f;
					const float Thick = FMath::Lerp(300.f, 800.f, ZB);
					const float Z0 = bAbove ? FMath::Lerp(2500.f, 8000.f, ZB) : -FMath::Lerp(2000.f, G.BackdropDepth * 0.6f, ZB);
					Out.Backdrop.Add(BoxTransform(FVector(Foot.Min.X, Foot.Min.Y, Z0), FVector(Foot.Max.X, Foot.Max.Y, Z0 + Thick)));
				}
				else if (Clear(Foot))
				{
					const float Top = FMath::Lerp(1000.f, 12000.f, ZA);
					Out.Backdrop.Add(BoxTransform(FVector(Foot.Min.X, Foot.Min.Y, -G.BackdropDepth), FVector(Foot.Max.X, Foot.Max.Y, Top)));
				}
			}
		}
	}

	static FRotator YawOf(const FVector2D& Dir) { return FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0); }
}

namespace AreaHash
{
	static void AddTransform(FString& Out, const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		const FVector S = T.GetScale3D();
		Out += FString::Printf(TEXT("|%d,%d,%d|%d,%d,%d|%d,%d,%d"),
			FMath::RoundToInt(L.X), FMath::RoundToInt(L.Y), FMath::RoundToInt(L.Z),
			FMath::RoundToInt(R.Pitch * 10.0), FMath::RoundToInt(R.Yaw * 10.0), FMath::RoundToInt(R.Roll * 10.0),
			FMath::RoundToInt(S.X * 100.0), FMath::RoundToInt(S.Y * 100.0), FMath::RoundToInt(S.Z * 100.0));
	}

	static FString TransformKey(const FTransform& T)
	{
		FString Out;
		AddTransform(Out, T);
		return Out;
	}

	static uint64 Fnv1a(const FString& Text, uint64 Hash)
	{
		for (const TCHAR Char : Text)
		{
			Hash ^= static_cast<uint64>(Char);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	static constexpr EAreaPieceKind Kinds[] = { EAreaPieceKind::Floors, EAreaPieceKind::Walls, EAreaPieceKind::Curbs,
		EAreaPieceKind::Pillars, EAreaPieceKind::Ceilings, EAreaPieceKind::Backdrop };
}

// --- Queries ---

TArray<UInstancedStaticMeshComponent*> AAreaGenerator::GetPCGComponents(EAreaPieceKind Kind) const
{
	const FName Tag(*FString::Printf(TEXT("Area%s"), LexToString(Kind)));
	TArray<UInstancedStaticMeshComponent*> All;
	GetComponents(All);
	TArray<UInstancedStaticMeshComponent*> Out;
	for (UInstancedStaticMeshComponent* Component : All)
	{
		if (Component && Component->ComponentHasTag(Tag))
		{
			Out.Add(Component);
		}
	}
	Out.Sort([](const UInstancedStaticMeshComponent& A, const UInstancedStaticMeshComponent& B) { return A.GetName() < B.GetName(); });
	return Out;
}

int32 AAreaGenerator::GetPCGInstanceCountOf(EAreaPieceKind Kind) const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Component : GetPCGComponents(Kind))
	{
		Count += Component->GetInstanceCount();
	}
	return Count;
}

int32 AAreaGenerator::GetPCGInstanceCount() const
{
	int32 Count = 0;
	for (const EAreaPieceKind Kind : AreaHash::Kinds)
	{
		Count += GetPCGInstanceCountOf(Kind);
	}
	return Count;
}

int64 AAreaGenerator::ComputeLayoutHash(int32& OutLines, TArray<FString>* OutText) const
{
	using namespace AreaHash;
	OutLines = 0;
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	TArray<FString> Lines;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor || !Actor->ActorHasTag(AAreaInfo::GeneratedTag) || Actor->IsA<AAreaInfo>())
		{
			continue;
		}
		FString Line = Actor->GetClass()->GetName();
		AddTransform(Line, Actor->GetActorTransform());
		if (const AResidue* Residue = Cast<AResidue>(Actor))
		{
			Line += FString::Printf(TEXT("|regen=%d"), Residue->bRegenerates ? 1 : 0);
		}
		else if (const ADoorDevice* Door = Cast<ADoorDevice>(Actor))
		{
			Line += FString::Printf(TEXT("|powered=%d"), FMath::RoundToInt(Door->PoweredSeconds * 100.f));
		}
		else if (const APowerTerminal* Terminal = Cast<APowerTerminal>(Actor))
		{
			Line += FString::Printf(TEXT("|targets=%d"), Terminal->Targets.Num());
		}
		Lines.Add(MoveTemp(Line));
	}

	// The plan's boxes, in plan order, as the 4b-1 geometry actor (at the origin, one component per kind) hashed them.
	FString Geometry = TEXT("AreaGeometry");
	AddTransform(Geometry, FTransform::Identity);
	const FAreaPieceHeights Heights = GetPieceHeights();
	for (const EAreaPieceKind Kind : Kinds)
	{
		const TArray<FBox>& Boxes = GetPublishedPieces(Kind);
		Geometry += FString::Printf(TEXT("|%s:%d"), LexToString(Kind), Boxes.Num());
		for (const FBox& Box : Boxes)
		{
			AddTransform(Geometry, Heights.MakeTransform(Kind, Box));
		}
	}
	Lines.Add(MoveTemp(Geometry));

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FString Line = It->GetClass()->GetName();
		AddTransform(Line, It->GetActorTransform());
		Lines.Add(MoveTemp(Line));
		break;
	}
	for (TActorIterator<AEntity> It(World); It; ++It)
	{
		FString Line = It->GetClass()->GetName();
		AddTransform(Line, FTransform(It->GetActorRotation(), PlannedEntity));
		Lines.Add(MoveTemp(Line));
		break;
	}
	if (NavigationBounds.IsValid)
	{
		FString Line = TEXT("NavMeshBoundsVolume");
		AddTransform(Line, FTransform(FRotator::ZeroRotator, NavigationBounds.GetCenter()));
		Lines.Add(MoveTemp(Line));
	}
	if (const AAreaInfo* Info = AAreaInfo::Find(World))
	{
		Lines.Add(FString::Printf(TEXT("AreaInfo|seed=%d|segments=%d|route=%d"), Info->Seed, Info->Segments.Num(), FMath::RoundToInt(Info->GetRouteLength())));
	}
	Lines.Sort();
	OutLines = Lines.Num();
	if (OutText)
	{
		*OutText = Lines;
	}
	uint64 Hash = 14695981039346656037ull;
	for (const FString& Line : Lines)
	{
		Hash = Fnv1a(Line, Hash);
		Hash = Fnv1a(TEXT("\n"), Hash);
	}
	return static_cast<int64>(Hash);
}

bool AAreaGenerator::DoesPCGMatchPlan(FString& OutWhy) const
{
	using namespace AreaHash;
	const FAreaPieceHeights Heights = GetPieceHeights();
	TArray<FString> Problems;
	for (const EAreaPieceKind Kind : Kinds)
	{
		TArray<FString> Planned;
		for (const FBox& Box : GetPublishedPieces(Kind))
		{
			Planned.Add(TransformKey(Heights.MakeTransform(Kind, Box)));
		}
		TArray<FString> Built;
		for (const UInstancedStaticMeshComponent* Component : GetPCGComponents(Kind))
		{
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform T;
				Component->GetInstanceTransform(Index, T, /*bWorldSpace=*/true);
				Built.Add(TransformKey(T));
			}
		}
		if (Planned.Num() != Built.Num())
		{
			Problems.Add(FString::Printf(TEXT("%s: planned %d, built %d"), LexToString(Kind), Planned.Num(), Built.Num()));
			continue;
		}
		Planned.Sort();
		Built.Sort();
		int32 Different = 0;
		FString First;
		for (int32 Index = 0; Index < Planned.Num(); ++Index)
		{
			if (Planned[Index] != Built[Index])
			{
				if (Different++ == 0)
				{
					First = FString::Printf(TEXT(" (first: planned %s, built %s)"), *Planned[Index], *Built[Index]);
				}
			}
		}
		if (Different > 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: %d of %d transforms differ%s"), LexToString(Kind), Different, Planned.Num(), *First));
		}
	}
	OutWhy = FString::Join(Problems, TEXT("; "));
	return Problems.Num() == 0;
}

// --- Clearing ---

int32 AAreaGenerator::ClearArea()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(AAreaInfo::GeneratedTag) && It->GetLevel() == GetLevel())
		{
			ToRemove.Add(*It);
		}
	}
	for (AActor* Actor : ToRemove)
	{
#if WITH_EDITOR
		if (!World->IsGameWorld())
		{
			World->EditorDestroyActor(Actor, true);
			continue;
		}
#endif
		Actor->Destroy();
	}
	for (UPCGComponent* Component : { StructurePCG.Get(), BackdropPCG.Get() })
	{
		if (Component && Component->IsRegistered())
		{
			Component->CleanupLocalImmediate(/*bRemoveComponents=*/true);
		}
	}
	PublishedPieces.Reset();
	for (TArray<FTransform>* Plan : { &PlanFloors, &PlanWalls, &PlanCurbs, &PlanPillars, &PlanCeilings, &PlanBackdrop })
	{
		Plan->Reset();
	}
	NavigationBounds = FBox(ForceInit);
	RouteLength = 0.f;
	return ToRemove.Num();
}

#if WITH_EDITOR
void AAreaGenerator::Clear()
{
	const int32 Removed = ClearArea();
	UE_LOG(LogArea, Log, TEXT("%s: cleared the preview (%d actors and the PCG output)."), *GetName(), Removed);
}

void AAreaGenerator::Generate()
{
	UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		UE_LOG(LogArea, Warning, TEXT("%s: Generate() is the editor preview; at runtime the area is generated at load (tls.AreaGenerate regenerates it)."), *GetName());
		return;
	}
	Act = URunSubsystem::GetActIndexOf(World);
	const int32 PreviewSeed = UTheLastShiftSettings::Get()->GetSeed(Act);
	for (UPCGComponent* Component : { StructurePCG.Get(), BackdropPCG.Get() })
	{
		// The preview's PCG output is never saved with the map.
		Component->SetEditingMode(EPCGEditorDirtyMode::Preview, EPCGEditorDirtyMode::Preview);
	}
	FString Error;
	if (!StartAttempt(PreviewSeed, Error))
	{
		UE_LOG(LogArea, Error, TEXT("%s: preview of act %d seed %d failed: %s"), *GetName(), Act, PreviewSeed, *Error);
	}
	State = EAreaGenerationState::Idle;
}
#endif

// --- Lifecycle ---

void AAreaGenerator::BeginPlay()
{
	Super::BeginPlay();
	for (UPCGComponent* Component : { StructurePCG.Get(), BackdropPCG.Get() })
	{
		Component->OnPCGGraphGeneratedDelegate.AddUObject(this, &AAreaGenerator::HandlePCGGenerated);
	}
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	Act = URunSubsystem::GetActIndexOf(World);
	const int32 ActSeed = UTheLastShiftSettings::Get()->GetSeed(Act);
	UE_LOG(LogArea, Log, TEXT("%s: %s is act %d; generating from seed %d."), *GetName(), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Act, ActSeed);
	StartGeneration(ActSeed, true);
}

void AAreaGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetCoverHold(false);
	for (UPCGComponent* Component : { StructurePCG.Get(), BackdropPCG.Get() })
	{
		if (Component)
		{
			Component->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AAreaGenerator::Regenerate(int32 InSeed, bool bInTryNextSeeds)
{
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		StartGeneration(InSeed, bInTryNextSeeds);
	}
}

void AAreaGenerator::RevalidateAfterNavigationRebuild()
{
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	bRevalidateOnly = true;
	GenerationStartTime = FPlatformTime::Seconds();
	StartNavigationRebuild();
}

void AAreaGenerator::StartGeneration(int32 InSeed, bool bInTryNextSeeds)
{
	FirstSeed = InSeed;
	bTryNextSeeds = bInTryNextSeeds;
	bRevalidateOnly = false;
	LastSeedsTried = 0;
	LayoutHash = 0;
	TimeToReadySeconds = 0.f;
	GenerationStartTime = FPlatformTime::Seconds();
	State = EAreaGenerationState::BuildingWorld;
	SetCoverHold(true);
	HoldPlayer();
	if (InSeed < 0 || !GetRecipe() || GetRecipe()->Segments.Num() == 0)
	{
		LastError = FString::Printf(TEXT("act %d has no recipe or seed in UTheLastShiftSettings (Config/DefaultGame.ini)"), Act);
		GiveUp();
		return;
	}
	TryAttempt(InSeed);
}

void AAreaGenerator::TryAttempt(int32 InSeed)
{
	for (;;)
	{
		++LastSeedsTried;
		FString Error;
		if (StartAttempt(InSeed, Error))
		{
			State = EAreaGenerationState::BuildingWorld;
			PhaseStartTime = FPlatformTime::Seconds();
			return;
		}
		LastError = FString::Printf(TEXT("seed %d: %s"), InSeed, *Error);
		UE_LOG(LogArea, Warning, TEXT("%s: act %d seed %d cannot be built: %s"), *GetName(), Act, InSeed, *Error);
		if (!bTryNextSeeds || LastSeedsTried >= FMath::Clamp(SeedAttempts, 1, 32))
		{
			GiveUp();
			return;
		}
		++InSeed;
	}
}

void AAreaGenerator::FailAttempt(const FString& Why)
{
	LastError = FString::Printf(TEXT("seed %d: %s"), Seed, *Why);
	UE_LOG(LogArea, Warning, TEXT("%s: act %d seed %d failed: %s"), *GetName(), Act, Seed, *Why);
	if (bRevalidateOnly)
	{
		bRevalidateOnly = false;
		State = EAreaGenerationState::Ready;
		OnGenerationFinished.Broadcast(this, false);
		return;
	}
	if (bTryNextSeeds && LastSeedsTried < FMath::Clamp(SeedAttempts, 1, 32))
	{
		TryAttempt(Seed + 1);
		return;
	}
	GiveUp();
}

void AAreaGenerator::GiveUp()
{
	State = EAreaGenerationState::Failed;
	TimeToReadySeconds = static_cast<float>(FPlatformTime::Seconds() - GenerationStartTime);
	UE_LOG(LogArea, Error, TEXT("%s: act %d: no valid area after %d seed(s) from %d (last error: %s). The player is released on an invalid area."),
		*GetName(), Act, LastSeedsTried, FirstSeed, *LastError);
	ReleasePlayer();
	SetCoverHold(false);
	OnGenerationFinished.Broadcast(this, false);
}

void AAreaGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const bool bBuilding = State == EAreaGenerationState::BuildingWorld || State == EAreaGenerationState::BuildingNavigation;
	if (bBuilding && !bRevalidateOnly)
	{
		HoldPlayer();
	}
	const double Now = FPlatformTime::Seconds();
	const float Timeout = FMath::Clamp(BuildTimeoutSeconds, 1.f, 120.f);
	if (State == EAreaGenerationState::BuildingWorld)
	{
		if (bStructureDone && bBackdropDone)
		{
			LastPCGSeconds = static_cast<float>(Now - PhaseStartTime);
			FString Why;
			bLastPCGMatchedPlan = DoesPCGMatchPlan(Why);
			if (!bLastPCGMatchedPlan)
			{
				FailAttempt(FString::Printf(TEXT("PCG did not build the plan: %s"), *Why));
				return;
			}
			StartNavigationRebuild();
		}
		else if (Now - PhaseStartTime > Timeout)
		{
			FailAttempt(FString::Printf(TEXT("PCG did not finish within %.0f s (structure %d, backdrop %d)"), Timeout, bStructureDone ? 1 : 0, bBackdropDone ? 1 : 0));
		}
	}
	else if (State == EAreaGenerationState::BuildingNavigation)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!NavSys)
		{
			FailAttempt(TEXT("the world has no navigation system"));
			return;
		}
		if (!bNavigationRequested)
		{
			ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
			if (NavData)
			{
				if (FNavDataGenerator* Generator = NavData->GetGenerator())
				{
					// Not ANavigationData::RebuildAll(): in the editor it first waits for every pending shader
					// compilation. The tiles rebuilt are the same; the build is asynchronous.
					Generator->OnNavigationBoundsChanged();
					Generator->RebuildAll();
				}
				else
				{
					NavData->RebuildAll();
				}
				bNavigationRequested = true;
				NavigationIdleTicks = 0;
			}
		}
		else if (!NavSys->IsNavigationBuildInProgress())
		{
			if (++NavigationIdleTicks >= 3)
			{
				LastNavigationSeconds = static_cast<float>(Now - PhaseStartTime);
				FinishWithValidation();
				return;
			}
		}
		else
		{
			NavigationIdleTicks = 0;
		}
		if (State == EAreaGenerationState::BuildingNavigation && Now - PhaseStartTime > Timeout)
		{
			FailAttempt(FString::Printf(TEXT("the navmesh was not built within %.0f s (navigation data %s)"), Timeout, bNavigationRequested ? TEXT("rebuilding") : TEXT("missing")));
		}
	}
}

void AAreaGenerator::HandlePCGGenerated(UPCGComponent* Component)
{
	if (Component == StructurePCG)
	{
		bStructureDone = true;
	}
	else if (Component == BackdropPCG)
	{
		bBackdropDone = true;
	}
	UE_LOG(LogArea, Verbose, TEXT("%s: PCG %s done (%d instances so far)."), *GetName(), *GetNameSafe(Component), GetPCGInstanceCount());
}

void AAreaGenerator::RunPCG(UPCGComponent* Component, UPCGGraphInterface* Graph)
{
	const bool bStructure = Component == StructurePCG;
	if (!Component || !Graph)
	{
		UE_LOG(LogArea, Error, TEXT("%s: no %s graph assigned: that part of the area is not built."), *GetName(), bStructure ? TEXT("structure") : TEXT("backdrop"));
		(bStructure ? bStructureDone : bBackdropDone) = true;
		return;
	}
	if (Component->GetGraph() != Graph)
	{
		Component->SetGraphLocal(Graph);
	}
	Component->Seed = Seed;
	Component->GenerateLocal(/*bForce=*/true);
}

void AAreaGenerator::StartNavigationRebuild()
{
	State = EAreaGenerationState::BuildingNavigation;
	PhaseStartTime = FPlatformTime::Seconds();
	bNavigationRequested = false;
	NavigationIdleTicks = 0;
	FitNavigationBounds();
}

void AAreaGenerator::FinishWithValidation()
{
	const double T0 = FPlatformTime::Seconds();
	LastValidation = AreaValidator::Validate(GetWorld());
	const FAreaValidationResult& Result = LastValidation;
	LastValidationSeconds = static_cast<float>(FPlatformTime::Seconds() - T0);
	bLastValidationPassed = Result.AllPassed();
	Result.Log(*FString::Printf(TEXT("%s act %d seed %d:"), *GetName(), Act, Seed), !bLastValidationPassed);
	if (bRevalidateOnly)
	{
		bRevalidateOnly = false;
		State = EAreaGenerationState::Ready;
		OnGenerationFinished.Broadcast(this, bLastValidationPassed);
		return;
	}
	if (bLastValidationPassed)
	{
		BecomeReady();
		return;
	}
	TArray<FString> Failed;
	for (const FAreaRuleResult& Rule : Result.Rules)
	{
		if (!Rule.bPass)
		{
			Failed.Add(FString::Printf(TEXT("rule %d %s: %s"), Rule.Rule, *Rule.Name, *Rule.Summary));
		}
	}
	if (!Result.FatalError.IsEmpty())
	{
		Failed.Add(FString::Printf(TEXT("fatal: %s"), *Result.FatalError));
	}
	FailAttempt(FString::Printf(TEXT("validation failed: %s"), *FString::Join(Failed, TEXT("; "))));
}

void AAreaGenerator::BecomeReady()
{
	UWorld* World = GetWorld();
	int32 Lines = 0;
	LayoutHash = ComputeLayoutHash(Lines);
	LastActorCount = Lines;
	TimeToReadySeconds = static_cast<float>(FPlatformTime::Seconds() - GenerationStartTime);
	if (AAreaInfo* Info = AAreaInfo::Find(World))
	{
		Info->LayoutHash = LayoutHash;
		Info->GeneratedActorCount = Lines;
		Info->Act = Act;
		Info->SeedsTried = LastSeedsTried;
		Info->PCGInstanceCount = GetPCGInstanceCount();
		Info->PCGSeconds = LastPCGSeconds;
		Info->NavBuildSeconds = LastNavigationSeconds;
		Info->ValidationSeconds = LastValidationSeconds;
		Info->TimeToReadySeconds = TimeToReadySeconds;
	}
	State = EAreaGenerationState::Ready;
	ReleasePlayer();
	SetCoverHold(false);
	UE_LOG(LogArea, Display, TEXT("%s: AREA READY: act %d, map %s, seed %d (%d tried), hash %016llx, route %.1f m, %d PCG instances (plan %s), plan %.3f s, PCG %.3f s, navmesh %.3f s, validation %.3f s, time to ready %.2f s."),
		*GetName(), Act, *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Seed, LastSeedsTried, static_cast<uint64>(LayoutHash), RouteLength / 100.f,
		GetPCGInstanceCount(), bLastPCGMatchedPlan ? TEXT("matched") : TEXT("NOT MATCHED"), LastGenerationSeconds, LastPCGSeconds, LastNavigationSeconds,
		LastValidationSeconds, TimeToReadySeconds);
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->NotifyAreaReady(World);
	}
	OnAreaReady.Broadcast();
	OnGenerationFinished.Broadcast(this, true);
}

// --- Player and screen ---

void AAreaGenerator::HoldPlayer()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || HeldPawn.Get() == Pawn)
	{
		return;
	}
	Pawn->DisableInput(Controller);
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->DisableMovement();
	}
	HeldPawn = Pawn;
	UE_LOG(LogArea, Log, TEXT("%s: holding %s (no movement, no input) until the area is ready."), *GetName(), *Pawn->GetName());
}

void AAreaGenerator::ReleasePlayer()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = HeldPawn.Get();
	if (!Pawn && Controller)
	{
		Pawn = Controller->GetPawn();
	}
	if (!Pawn)
	{
		return;
	}
	Pawn->SetActorLocationAndRotation(PlannedStart, PlannedStartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (Controller)
	{
		Controller->SetControlRotation(PlannedStartRotation);
	}
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	Pawn->EnableInput(Controller);
	HeldPawn.Reset();
}

void AAreaGenerator::SetCoverHold(bool bHold)
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World && World->IsGameWorld() ? World->GetGameInstance() : nullptr;
	UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	if (!Flow || bHold == bCoverHeld)
	{
		return;
	}
	if (bHold)
	{
		Flow->AddCoverHold(TEXT("AreaGenerator"));
	}
	else
	{
		Flow->RemoveCoverHold(TEXT("AreaGenerator"));
	}
	bCoverHeld = bHold;
}

void AAreaGenerator::FitNavigationBounds()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !NavigationBounds.IsValid)
	{
		return;
	}
	ANavMeshBoundsVolume* Volume = nullptr;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		Volume = *It;
		break;
	}
	if (!Volume || !Volume->GetBrushComponent())
	{
		UE_LOG(LogArea, Error, TEXT("%s: the map has no NavMeshBoundsVolume to fit to the route."), *GetName());
		return;
	}
	// The map's volume keeps its brush; scaling the actor fits it to the route.
	const FBox Local = Volume->GetBrushComponent()->CalcBounds(FTransform::Identity).GetBox();
	const FVector LocalSize = Local.GetSize().ComponentMax(FVector(1.0));
	const FVector Scale = NavigationBounds.GetSize() / LocalSize;
	const FVector Location = NavigationBounds.GetCenter() - Local.GetCenter() * Scale;
	Volume->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	Volume->SetActorTransform(FTransform(FRotator::ZeroRotator, Location, Scale));
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSys->OnNavigationBoundsUpdated(Volume);
	}
}

// --- One attempt ---

bool AAreaGenerator::StartAttempt(int32 InSeed, FString& OutError)
{
	using namespace AreaGen;
	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("no world");
		return false;
	}
	const bool bGame = World->IsGameWorld();
	const double T0 = FPlatformTime::Seconds();
	ClearArea();
	Seed = InSeed;
	bStructureDone = false;
	bBackdropDone = false;

	const FAreaRecipe* Recipe = GetRecipe();
	if (!Recipe || Recipe->Segments.Num() == 0)
	{
		OutError = FString::Printf(TEXT("act %d has no recipe"), Act);
		return false;
	}
	const FAreaPawnMetrics Pawn = GetAreaPawnMetrics(World);
	const ADoorDevice* DoorDefaults = DoorClass ? DoorClass->GetDefaultObject<ADoorDevice>() : nullptr;
	const APowerTerminal* TerminalDefaults = TerminalClass ? TerminalClass->GetDefaultObject<APowerTerminal>() : nullptr;
	if (!DoorDefaults || !TerminalDefaults || !ResidueClass || !LightGroupClass)
	{
		OutError = TEXT("a gameplay class is not set");
		return false;
	}
	if (CurbHeight <= Pawn.MaxStepHeight)
	{
		UE_LOG(LogArea, Warning, TEXT("%s: CurbHeight %.0f cm is not above the pawn's MaxStepHeight %.0f cm: curbs will not block."), *GetName(), CurbHeight, Pawn.MaxStepHeight);
	}

	// The layout is redrawn (from a seed derived from Seed and the attempt) until it fits and its route centreline is
	// inside the recipe's route length range, with RouteCornerCut for the corners the navigation path cuts.
	TUniquePtr<FBuilder> Built;
	FString AttemptErrors;
	const int32 Layouts = FMath::Clamp(LayoutAttempts, 1, 32);
	// The walked path cuts corners: the centreline must be long enough for the path to reach the recipe's minimum.
	const float MinCentreline = Recipe->RouteLengthMin / (1.f - FMath::Clamp(RouteCornerCut, 0.f, 0.3f));
	for (int32 Attempt = 0; Attempt < Layouts && !Built; ++Attempt)
	{
		TUniquePtr<FBuilder> Builder = MakeUnique<FBuilder>(*this, *Recipe, Seed, Attempt, DoorDefaults->GetDoorSize().Y, Pawn.WalkSpeed, TerminalDefaults->Cost);
		FString Why;
		if (!Builder->Build())
		{
			Why = Builder->Error;
		}
		else if (Builder->Plan.RouteLength() < MinCentreline || Builder->Plan.RouteLength() > Recipe->RouteLengthMax * 0.98f)
		{
			Why = FString::Printf(TEXT("route centreline %.0f m outside %.0f-%.0f m"), Builder->Plan.RouteLength() / 100.f, MinCentreline / 100.f, Recipe->RouteLengthMax * 0.98f / 100.f);
		}
		if (Why.IsEmpty())
		{
			Built = MoveTemp(Builder);
			if (Attempt > 0)
			{
				UE_LOG(LogArea, Log, TEXT("%s: seed %d laid out on attempt %d (%s)."), *GetName(), Seed, Attempt + 1, *AttemptErrors);
			}
		}
		else
		{
			AttemptErrors += FString::Printf(TEXT("%s#%d: %s"), AttemptErrors.IsEmpty() ? TEXT("") : TEXT("; "), Attempt + 1, *Why);
		}
	}
	if (!Built)
	{
		OutError = FString::Printf(TEXT("no layout in %d attempts: %s"), Layouts, *AttemptErrors);
		return false;
	}
	FBuilder& Builder = *Built;
	const FPlan& Plan = Builder.Plan;
	FGeometryPlan Geometry;
	BuildGeometry(*this, Plan, Builder.Rng, Geometry);

	// --- Publish the plan to the PCG graphs ---
	PublishedPieces.SetNum(6);
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Floors)].Boxes = Geometry.Floors;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Walls)].Boxes = Geometry.Walls;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Curbs)].Boxes = Geometry.Curbs;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Pillars)].Boxes = Geometry.Pillars;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Ceilings)].Boxes = Geometry.Ceilings;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Backdrop)].Boxes = Geometry.Backdrop;
	{
		const FAreaPieceHeights Heights = GetPieceHeights();
		TArray<FTransform>* Targets[] = { &PlanFloors, &PlanWalls, &PlanCurbs, &PlanPillars, &PlanCeilings, &PlanBackdrop };
		for (int32 Kind = 0; Kind < 6; ++Kind)
		{
			for (const FBox& Box : PublishedPieces[Kind].Boxes)
			{
				Targets[Kind]->Add(Heights.MakeTransform(static_cast<EAreaPieceKind>(Kind), Box));
			}
		}
	}
	RouteLength = Plan.RouteLength();

	// --- Spawn the gameplay actors ---
	ULevel* Level = GetLevel();
	auto Spawn = [&](UClass* Class, const FVector& Location, const FRotator& Rotation, const FString& Label, const TCHAR* Folder) -> AActor*
	{
		FActorSpawnParameters Params;
		Params.OverrideLevel = Level;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (!bGame)
		{
			Params.ObjectFlags |= RF_Transient;
		}
		AActor* Actor = World->SpawnActor(Class, &Location, &Rotation, Params);
		if (Actor)
		{
			Actor->Tags.Add(AAreaInfo::GeneratedTag);
#if WITH_EDITOR
			if (!bGame)
			{
				Actor->SetActorLabel(Label);
				Actor->SetFolderPath(FName(*FString::Printf(TEXT("Area/%s"), Folder)));
			}
#endif
		}
		return Actor;
	};
	auto At = [](const FVector2D& P, float Z) { return FVector(P.X, P.Y, Z); };

	// Info
	AAreaInfo* Info = Cast<AAreaInfo>(Spawn(AAreaInfo::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Area_Info"), TEXT("Info")));
	if (!Info)
	{
		OutError = TEXT("could not spawn the area info");
		return false;
	}
	Info->Seed = Seed;
	Info->FloorZ = 0.f;
	Info->FloorRects = Geometry.FloorRects;
	for (int32 I = 0; I < Plan.Route.Num(); ++I)
	{
		Info->RoutePoints.Add(At(Plan.Route[I], 0.f));
		Info->RoutePointSegments.Add(Plan.RouteSeg[I]);
	}
	for (const FSeg& S : Plan.Segs)
	{
		FAreaSegmentRecord Record;
		Record.Type = S.Type;
		Record.bClosed = S.bClosed;
		Record.Bounds = FBox(FVector(S.Bounds.Min.X, S.Bounds.Min.Y, -FloorThickness), FVector(S.Bounds.Max.X, S.Bounds.Max.Y, S.bClosed ? WallHeight : CurbHeight));
		Record.RouteStart = S.RouteStart;
		Record.RouteEnd = S.RouteEnd;
		Record.SpursPlaced = S.Spurs;
		Record.NichesPlaced = S.Niches;
		Info->Segments.Add(Record);
	}

	// Start (the map's PlayerStart moves to it), exit, entity (the map's entity moves to it)
	const FVector2D StartDir = Plan.Route.Num() > 1 ? (Plan.Route[1] - Plan.Route[0]).GetSafeNormal() : FVector2D(1.0, 0.0);
	PlannedStart = At(Plan.Route[0] + StartDir * 150.0, 100.f);
	PlannedStartRotation = YawOf(StartDir);
	PlannedEntity = At(Plan.PointAt(Plan.RouteLength() * 0.5f), 0.f);
	if (bGame)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			It->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			It->SetActorLocationAndRotation(PlannedStart, PlannedStartRotation);
			break;
		}
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			It->SetActorLocation(PlannedEntity);
			break;
		}
	}
	const FVector2D EndDir = (Plan.Route.Last() - Plan.Route[Plan.Route.Num() - 2]).GetSafeNormal();
	AAreaExit* Exit = Cast<AAreaExit>(Spawn(AAreaExit::StaticClass(), At(Plan.Route.Last() - EndDir * 150.0, 150.f), YawOf(EndDir), TEXT("Area_Exit"), TEXT("Flow")));
	if (Exit)
	{
		const float HalfWidth = FMath::Max(100.f, Plan.Segs.Last().Width * 0.5f - 10.f);
		Exit->GetVolume()->SetBoxExtent(FVector(150.f, HalfWidth, 150.f));
	}

	// Gates
	for (int32 I = 0; I < Builder.GatePlacements.Num(); ++I)
	{
		const FBuilder::FGatePlacement& Gate = Builder.GatePlacements[I];
		ADoorDevice* Door = Cast<ADoorDevice>(Spawn(DoorClass, At(Gate.Door, 0.f), YawOf(DirV(Gate.DoorDir)), FString::Printf(TEXT("Area_Gate%d_Door"), I), TEXT("Gates")));
		APowerTerminal* Terminal = Cast<APowerTerminal>(Spawn(TerminalClass, At(Gate.Terminal, 0.f), YawOf(Gate.TerminalFacing), FString::Printf(TEXT("Area_Gate%d_Terminal"), I), TEXT("Gates")));
		AResidue* Regen = Cast<AResidue>(Spawn(ResidueClass, At(Gate.Regen, ResidueHeight), FRotator::ZeroRotator, FString::Printf(TEXT("Area_Gate%d_RegenResidue"), I), TEXT("Gates")));
		if (Door)
		{
			Door->PoweredSeconds = Gate.PoweredSeconds;
		}
		if (Terminal && Door)
		{
			Terminal->Targets.Add(Door);
		}
		if (Regen)
		{
			Regen->bRegenerates = true;
			Regen->RegenerateBelowEnergy = FMath::Clamp(TerminalDefaults->Cost, 1, 99);
		}
	}

	// Light groups
	for (int32 I = 0; I < Builder.LightPlacements.Num(); ++I)
	{
		const FBuilder::FLightPlacement& Group = Builder.LightPlacements[I];
		ALightGroupDevice* Device = Cast<ALightGroupDevice>(Spawn(LightGroupClass, At(Group.Terminal, 0.f), FRotator::ZeroRotator, FString::Printf(TEXT("Area_LightGroup%d"), I), TEXT("Lights")));
		APowerTerminal* Terminal = Cast<APowerTerminal>(Spawn(TerminalClass, At(Group.Terminal, 0.f), YawOf(Group.TerminalFacing), FString::Printf(TEXT("Area_LightGroup%d_Terminal"), I), TEXT("Lights")));
		if (!Device)
		{
			continue;
		}
		Device->PoweredSeconds = LightGroupPoweredSeconds;
		for (int32 K = 0; K < Group.Lights.Num(); ++K)
		{
			APointLight* Light = Cast<APointLight>(Spawn(APointLight::StaticClass(), At(Group.Lights[K], LightHeight), FRotator::ZeroRotator,
				FString::Printf(TEXT("Area_LightGroup%d_Light%d"), I, K), TEXT("Lights")));
			if (Light)
			{
				Light->GetRootComponent()->SetMobility(EComponentMobility::Movable);
				if (UPointLightComponent* Component = Cast<UPointLightComponent>(Light->GetLightComponent()))
				{
					Component->SetIntensityUnits(ELightUnits::Lumens);
					Component->SetIntensity(LightIntensity);
					Component->SetAttenuationRadius(LightRadius);
					Component->SetCastShadows(false);
				}
				Device->Lights.Add(Light);
			}
		}
		if (Terminal)
		{
			Terminal->Targets.Add(Device);
		}
	}

	// Residues
	for (int32 I = 0; I < Builder.NormalResidues.Num(); ++I)
	{
		Spawn(ResidueClass, At(Builder.NormalResidues[I], ResidueHeight), FRotator::ZeroRotator, FString::Printf(TEXT("Area_Residue%02d"), I), TEXT("Residues"));
	}

	// Navigation bounds fitted to the walkable route (applied to the map's NavMeshBoundsVolume in game worlds).
	FBox2D Walkable(ForceInit);
	for (const FAreaFloorRect& R : Geometry.FloorRects)
	{
		Walkable += FVector2D(R.Box.Min.X, R.Box.Min.Y);
		Walkable += FVector2D(R.Box.Max.X, R.Box.Max.Y);
	}
	Walkable = Grow(Walkable, 200.0);
	const float BoundsBottom = -100.f;
	const float BoundsTop = FMath::Max(250.f, WallHeight - 50.f);
	NavigationBounds = FBox(FVector(Walkable.Min.X, Walkable.Min.Y, BoundsBottom), FVector(Walkable.Max.X, Walkable.Max.Y, BoundsTop));
	FitNavigationBounds();

	LastGenerationSeconds = static_cast<float>(FPlatformTime::Seconds() - T0);
	Info->GenerationSeconds = LastGenerationSeconds;

	// --- Build the world from the plan (asynchronous; Tick waits for both) ---
	RunPCG(StructurePCG, StructureGraph);
	RunPCG(BackdropPCG, BackdropGraph);

	UE_LOG(LogArea, Log, TEXT("%s: act %d seed %d planned in %.3f s: %d segments, route %.1f m, boxes (floors %d, walls %d, curbs %d, pillars %d, ceilings %d, backdrop %d), %d residues, %d gates, %d light groups; PCG started."),
		*GetName(), Act, Seed, LastGenerationSeconds, Plan.Segs.Num(), Plan.RouteLength() / 100.f,
		Geometry.Floors.Num(), Geometry.Walls.Num(), Geometry.Curbs.Num(), Geometry.Pillars.Num(), Geometry.Ceilings.Num(), Geometry.Backdrop.Num(),
		Builder.NormalResidues.Num() + Builder.GatePlacements.Num(), Builder.GatePlacements.Num(), Builder.LightPlacements.Num());
	return true;
}
