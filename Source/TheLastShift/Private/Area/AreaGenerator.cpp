// SPDX-License-Identifier: MIT

#include "Area/AreaGenerator.h"
#include "HAL/IConsoleManager.h"

#include "AI/NavDataGenerator.h"
#include "Area/AreaExit.h"
#include "Area/AreaInfo.h"
#include "Area/AreaValidator.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/GameInstance.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
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

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarAreaWayOut(
	TEXT("tls.Area.WayOut"), 1,
	TEXT("1: the route ends in the twin AI's room (where the recipe asks for it) and the exit corridor (Brief 26). 0: neither, the exit at the route's end as before Brief 26 (diagnostics: the layout hash is then the one before Brief 26). Read at each generation."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAreaFloorStrips(
	TEXT("tls.Area.FloorStrips"), 1,
	TEXT("1: a strip of the floor plates' family fills the floor from each wall panel's foot to its slab (Brief 26). 0: none, the gap before Brief 26 (diagnostics). Read at each generation."),
	ECVF_Default);
#endif

/** Whether the way out (Brief 26) is built: always, but tls.Area.WayOut 0 turns it off for diagnostics. */
static bool IsAreaWayOutOn()
{
#if !UE_BUILD_SHIPPING
	return CVarAreaWayOut.GetValueOnGameThread() != 0;
#else
	return true;
#endif
}

static bool IsAreaFloorStripsOn()
{
#if !UE_BUILD_SHIPPING
	return CVarAreaFloorStrips.GetValueOnGameThread() != 0;
#else
	return true;
#endif
}

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
	LogTerminalClass = ALogTerminal::StaticClass();
	TwinAITerminalClass = ATwinAITerminal::StaticClass();
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
		/** Hall only: a web of catwalks (its Legs are the junction platforms). */
		bool bWeb = false;
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
		// The way out (Brief 26): the exit corridor and the twin AI's room, their mouth (the corridor's) or entrance (the
		// room's) and the direction they are entered in.
		bool bExitCorridor = false;
		bool bTwinRoom = false;
		FVector2D Mouth = FVector2D::ZeroVector;
		int32 EnterDir = 0;
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
		/** Cell edges (cell X, cell Y, direction) the exit corridor ends on: open on the dark, their wall collision only (Brief 26). */
		TSet<FIntVector> OpenEdges;
		/** Route length before the way out (the recipe's own route): what every placement made before it reads. */
		float LengthBeforeWayOut = 0.f;

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
		/** Where the recipe's route ends and the way it heads there. */
		FVector2D EndHead = FVector2D::ZeroVector;
		int32 EndDir = 0;

		// --- The way out (Brief 26) ---
		//
		// After the layout is chosen and everything of the recipe is placed, from the finished plan, with no random draw:
		// the twin AI's room (when the recipe asks for it) and the exit corridor after it, so no draw of the stream and no
		// placement before them changes. The corridor is straight: it goes on in the route's last direction, or, after the
		// room, from the middle of one of the room's two side walls (toward the route's start line first). A seed whose plan
		// cannot take them fails this layout.

		struct FWayOut
		{
			int32 RoomSegment = -1;
			int32 CorridorSegment = -1;
			FBox2D RoomRect = FBox2D(ForceInit);
			int32 RoomDir = 0;
			FVector2D ExitPos = FVector2D::ZeroVector;
			FVector2D ExitDir = FVector2D(1.0, 0.0);
			float HalfWidth = 0.f;
			float Length = 0.f;
		};
		FWayOut WayOut;
		FString WayOutError;

		bool PlaceWayOut(bool bEnabled)
		{
			Plan.LengthBeforeWayOut = Plan.RouteLength();
			if (!bEnabled)
			{
				return true;
			}
			const float W = SnapWidth(FMath::Clamp(G.ExitCorridorWidth, 200.f, 1000.f));
			const float Depth = SnapLen(FMath::Clamp(R.ExitTriggerDepthM, 2.f, 50.f) * 100.f);
			const float Run = SnapLen(FMath::Clamp(R.ExitRunLengthM, 10.f, 200.f) * 100.f);
			const float Len = Depth + Run;
			const FVector2D Head = EndHead;
			const int32 Dir = EndDir;
			const float PrevWidth = Plan.Segs.Num() > 0 ? Plan.Segs.Last().Width : 0.f;
			auto JointZone = [&](const FVector2D& At, float Width)
			{
				const double Reach = (Margin + 2) * Cell + 0.5 * FMath::Max(Width, PrevWidth) + 50.0;
				return FBox2D(At - FVector2D(Reach, Reach), At + FVector2D(Reach, Reach));
			};

			TArray<int32> CorridorDirs;
			FVector2D RoomCentre = FVector2D::ZeroVector;
			float Side = 0.f;
			if (R.bTwinAIRoom)
			{
				Side = SnapWidth(FMath::Clamp(G.TwinRoomSide, 1000.f, 1400.f));
				const float Passage = SnapLen(FMath::Clamp(G.TwinRoomPassageLength, 300.f, 1000.f));
				const FVector2D F = DirV(Dir);
				FSeg Room;
				Room.Type = EAreaSegmentType::Room;
				Room.bClosed = true;
				Room.bTwinRoom = true;
				Room.Width = Side;
				Room.Mouth = Head;
				Room.EnterDir = Dir;
				FLeg PassageLeg;
				PassageLeg.Rect = MakeRect(Head, Dir, 0.0, Passage, -W * 0.5, W * 0.5);
				PassageLeg.Start = Head;
				PassageLeg.Dir = Dir;
				PassageLeg.Length = Passage;
				FLeg RoomLeg;
				RoomLeg.Rect = MakeRect(Head + F * Passage, Dir, 0.0, Side, -Side * 0.5, Side * 0.5);
				RoomLeg.Start = Head + F * Passage;
				RoomLeg.Dir = Dir;
				RoomLeg.Length = Side;
				Room.Legs = { PassageLeg, RoomLeg };
				TArray<FIntPoint> Cells;
				CellsIn(PassageLeg.Rect, Cells);
				CellsIn(RoomLeg.Rect, Cells);
				const TArray<FIntPoint> Unique = TSet<FIntPoint>(Cells).Array();
				TArray<FIntPoint> Sorted = Unique;
				Sorted.Sort([](const FIntPoint& X, const FIntPoint& Y) { return X.Y != Y.Y ? X.Y < Y.Y : X.X < Y.X; });
				const int32 Index = Plan.Segs.Num();
				if (!Plan.CanPlace(Sorted, Index, false, { JointZone(Head, W) }, Margin))
				{
					WayOutError = FString::Printf(TEXT("the twin AI's room (%.0f m a side) does not fit after the route's end at (%.0f, %.0f) without touching another segment"), Side / 100.f, Head.X, Head.Y);
					return false;
				}
				Plan.Segs.Add(Room);
				Plan.Commit(Sorted, Index, EKind::Floor);
				RoomCentre = Head + F * (Passage + Side * 0.5f);
				Plan.AddRoute(RoomCentre, Index);
				WayOut.RoomSegment = Index;
				WayOut.RoomRect = RoomLeg.Rect;
				WayOut.RoomDir = Dir;
				if (Dir == 0)
				{
					const int32 Toward = Head.Y > 0.0 ? 3 : 1;
					CorridorDirs = { Toward, Opposite(Toward) };
				}
				else
				{
					CorridorDirs = { 0 };
				}
			}
			else
			{
				CorridorDirs = { Dir };
			}

			const int32 Index = Plan.Segs.Num();
			for (const int32 D : CorridorDirs)
			{
				const FVector2D Start = R.bTwinAIRoom ? RoomCentre + DirV(D) * (Side * 0.5f) : Head;
				const FBox2D Rect = MakeRect(Start, D, 0.0, Len, -W * 0.5, W * 0.5);
				TArray<FIntPoint> Cells;
				CellsIn(Rect, Cells);
				// After the room only the room may be near its mouth; else the predecessor around the joint, as any segment.
				const FBox2D Zone = R.bTwinAIRoom ? Grow(WayOut.RoomRect, Cell) : JointZone(Head, W);
				if (!Plan.CanPlace(Cells, Index, false, { Zone }, Margin))
				{
					continue;
				}
				FSeg Corridor;
				Corridor.Type = EAreaSegmentType::Corridor;
				Corridor.bClosed = true;
				Corridor.bExitCorridor = true;
				Corridor.Width = W;
				Corridor.Mouth = Start;
				Corridor.EnterDir = D;
				FLeg Leg;
				Leg.Rect = Rect;
				Leg.Start = Start;
				Leg.Dir = D;
				Leg.Length = Len;
				Corridor.Legs.Add(Leg);
				Plan.Segs.Add(Corridor);
				Plan.Commit(Cells, Index, EKind::Floor);
				if (R.bTwinAIRoom)
				{
					Plan.AddRoute(Start, Index - 1);
				}
				Plan.AddRoute(Start + DirV(D) * Len, Index);
				// The far end: open on the dark (its wall box is kept for collision only, never dressed or rendered).
				const TSet<FIntPoint> Own(Cells);
				for (const FIntPoint& C : Cells)
				{
					if (!Own.Contains(C + Dirs[D]))
					{
						Plan.OpenEdges.Add(FIntVector(C.X, C.Y, D));
					}
				}
				Plan.ComputeRouteDistances();
				WayOut.CorridorSegment = Index;
				WayOut.ExitPos = Start + DirV(D) * Depth;
				WayOut.ExitDir = DirV(D);
				WayOut.HalfWidth = W * 0.5f;
				WayOut.Length = Len;
				return true;
			}
			WayOutError = FString::Printf(TEXT("the exit corridor (%.0f m) does not fit %s without touching another segment"), Len / 100.f,
				R.bTwinAIRoom ? TEXT("from either side wall of the twin AI's room") : *FString::Printf(TEXT("straight on from the route's end at (%.0f, %.0f)"), Head.X, Head.Y));
			return false;
		}

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
			EndHead = Head;
			EndDir = Dir;
			Plan.ComputeRouteDistances();
			Plan.LengthBeforeWayOut = Plan.RouteLength();

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
			Body.Seg.bWeb = true;

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
		bool FindSideSpot(float S, float MinS, FVector2D& OutPos, FVector2D& OutFacing, float MaxWallDistance = 1000.f, int32 FixedFirstSide = 0,
			const TFunction<bool(const FVector2D&)>& Accept = nullptr)
		{
			// FixedFirstSide (1 left, -1 right) draws nothing from the stream: placements on top of a finished plan (beacons).
			const int32 FirstSide = FixedFirstSide != 0 ? FMath::Sign(FixedFirstSide) : (Rng.FRand() < 0.5f ? 1 : -1);
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
					for (float D = 0.f; D <= MaxWallDistance; D += Cell * 0.5f)
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
					if (bStraightWall && Plan.IsClear(T, 40.0) && Plan.IsClear(Stand, 60.0) && !NearUsed(T, 300.0) && (!Accept || Accept(T)))
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
			// Rooms first, the one nearest the exit first (the last room of a route is where a lit room matters most),
			// then corridors in route order.
			TArray<int32> Order;
			for (int32 Index = Plan.Segs.Num() - 1; Index >= 0; --Index)
			{
				if (Plan.Segs[Index].Type == EAreaSegmentType::Room) { Order.Add(Index); }
			}
			for (int32 Index = 0; Index < Plan.Segs.Num(); ++Index)
			{
				if (Plan.Segs[Index].Type == EAreaSegmentType::Corridor) { Order.Add(Index); }
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
				// A wide room may have no wall within 10 m of the route near its entry: then look for one up to 30 m away
				// (the terminal still stands TerminalSideOffset from the centreline).
				if (!FindSideSpot(Start, FMath::Max(50.f, S.RouteStart - 500.f), Lights.Terminal, Lights.TerminalFacing)
					&& !FindSideSpot(Start, FMath::Max(50.f, S.RouteStart - 500.f), Lights.Terminal, Lights.TerminalFacing, 3000.f))
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

	public:
		// --- Terminals against a wall (log terminals, the twin AI's terminal), Brief 8b ---
		//
		// Placed after the layout is chosen and everything else is placed, from the finished plan, with no random draw:
		// they move nothing that exists and change no draw of the stream. A rule that cannot be met leaves the terminal out
		// (the validator then fails the seed); there is no fallback position.

		/** What a wall terminal needs, in centimetres from the wall face, into the segment. */
		struct FWallSpec
		{
			/** The actor's origin from the wall face. */
			float BackOffset = 0.f;
			/** Half the body's width along the wall. */
			float HalfWidth = 50.f;
			/** The body's depth from the wall face. */
			float Depth = 50.f;
			/** The free place to stand (log terminals: to read) from the wall face. */
			float StandDistance = 120.f;
			/** The terminal's InteractRadius. */
			float Reach = 150.f;
		};

		struct FWallPlacement
		{
			int32 Segment = -1;
			FVector2D Pos = FVector2D::ZeroVector;
			FVector2D Facing = FVector2D(1.0, 0.0);
			FVector2D Stand = FVector2D::ZeroVector;
			float RouteDistance = 0.f;
		};

		TArray<FWallPlacement> LogPlacements;
		TArray<FWallPlacement> TwinPlacements;
		/** Why a wall terminal of the recipe was left out (empty when all were placed). */
		FString WallError;

		/**
		 * Places Recipe.LogTerminalCount log terminals and Recipe.TwinAITerminalCount twin AI terminals. Others: the
		 * terminals already placed (gates, light groups) with their InteractRadius. LineClear: the distance from the route
		 * centreline the front of a body keeps, as the other terminals keep it. Doors: the gates' doors.
		 */
		bool PlaceWallTerminals(const FWallSpec& Log, const FWallSpec& Twin, float OthersReach, float LineClear)
		{
			TArray<TPair<FVector2D, float>> Others;
			for (const FVector2D& P : UsedSideSpots)
			{
				Others.Emplace(P, OthersReach);
			}
			// The recipe's route: the way out (Brief 26) moves no terminal placed along it.
			const float L = Plan.LengthBeforeWayOut;
			bool bAll = true;

			// Log terminals: one per equal part of the route, each in its own closed corridor or room, in route order.
			const int32 Logs = FMath::Clamp(R.LogTerminalCount, 0, 4);
			TSet<int32> UsedSegments;
			for (int32 Part = 0; Part < Logs; ++Part)
			{
				const float PartStart = Part * L / Logs;
				const float PartEnd = (Part + 1) * L / Logs;
				FString Why = TEXT("no closed corridor or room in this part of the route");
				bool bPlaced = false;
				for (int32 Index = 0; Index < Plan.Segs.Num() && !bPlaced; ++Index)
				{
					const FSeg& S = Plan.Segs[Index];
					if ((S.Type != EAreaSegmentType::Corridor && S.Type != EAreaSegmentType::Room) || UsedSegments.Contains(Index))
					{
						continue;
					}
					const float S0 = FMath::Max(S.RouteStart + 200.f, PartStart);
					const float S1 = FMath::Min(S.RouteEnd - 200.f, PartEnd - 1.f);
					if (S1 < S0)
					{
						continue;
					}
					FWallPlacement Spot;
					FString SegmentWhy;
					if (FindWallSpot(Index, S0, S1, PartStart, PartEnd, Log, Others, LineClear, Spot, SegmentWhy))
					{
						LogPlacements.Add(Spot);
						Others.Emplace(Spot.Pos, Log.Reach);
						UsedSegments.Add(Index);
						bPlaced = true;
					}
					else
					{
						Why = FString::Printf(TEXT("segment %d (%s): %s"), Index, LexToString(S.Type), *SegmentWhy);
					}
				}
				if (!bPlaced)
				{
					WallError += FString::Printf(TEXT("%slog terminal %d (route %.0f-%.0f m): %s"), WallError.IsEmpty() ? TEXT("") : TEXT("; "), Part, PartStart / 100.f, PartEnd / 100.f, *Why);
					bAll = false;
				}
			}

			// The twin AI's terminal: in the twin AI's room when there is one (PlaceTwinInRoom), else the last closed segment
			// of the recipe's route.
			const int32 Twins = WayOut.RoomSegment >= 0 ? 0 : FMath::Clamp(R.TwinAITerminalCount, 0, 1);
			for (int32 K = 0; K < Twins; ++K)
			{
				int32 Last = -1;
				for (int32 Index = Plan.Segs.Num() - 1; Index >= 0 && Last < 0; --Index)
				{
					Last = Plan.Segs[Index].bClosed && !Plan.Segs[Index].bExitCorridor ? Index : -1;
				}
				FString Why = TEXT("the route has no closed segment");
				FWallPlacement Spot;
				bool bPlaced = false;
				if (Last >= 0)
				{
					const FSeg& S = Plan.Segs[Last];
					const float S0 = S.RouteStart + 200.f;
					const float S1 = FMath::Min(S.RouteEnd - 200.f, L - 600.f);
					bPlaced = S1 >= S0 && FindWallSpot(Last, S0, S1, 0.f, L, Twin, Others, LineClear, Spot, Why);
					if (S1 < S0)
					{
						Why = TEXT("too short");
					}
					Why = FString::Printf(TEXT("segment %d (%s): %s"), Last, LexToString(S.Type), *Why);
				}
				if (bPlaced)
				{
					TwinPlacements.Add(Spot);
					Others.Emplace(Spot.Pos, Twin.Reach);
				}
				else
				{
					WallError += FString::Printf(TEXT("%stwin AI terminal: %s"), WallError.IsEmpty() ? TEXT("") : TEXT("; "), *Why);
					bAll = false;
				}
			}
			return bAll;
		}

		/**
		 * Brief 26: the twin AI's terminal in its room, set into the middle of the wall that faces the entrance: its back
		 * (BackX, the body's back along its +X) on the wall panels' slab, facing the entrance; its standing place Stand in
		 * front of its front (FrontX). WallThickness: the wall box's (the slab is its middle). False (and WallError) when the
		 * standing place is not free or not within Reach.
		 */
		bool PlaceTwinInRoom(float BackX, float FrontX, float Stand, float Reach, float WallThickness)
		{
			if (WayOut.RoomSegment < 0 || R.TwinAITerminalCount <= 0)
			{
				return true;
			}
			const FSeg& Room = Plan.Segs[WayOut.RoomSegment];
			const FLeg& Leg = Room.Legs.Last();
			const FVector2D F = DirV(Leg.Dir);
			const FVector2D FarFace = Leg.Start + F * Leg.Length;
			FWallPlacement Spot;
			Spot.Segment = WayOut.RoomSegment;
			Spot.Facing = -F;
			Spot.Pos = FarFace + F * (0.5f * WallThickness + BackX);
			Spot.Stand = Spot.Pos - F * (FrontX + Stand);
			Spot.RouteDistance = Plan.Project(Spot.Pos);
			if (!Plan.IsClear(Spot.Stand, 60.0) || FVector2D::Distance(Spot.Pos, Spot.Stand) > Reach - 10.f)
			{
				WallError += FString::Printf(TEXT("%stwin AI terminal: its standing place in the room (%.0f, %.0f) is not free or out of reach (%.0f cm of %.0f)"),
					WallError.IsEmpty() ? TEXT("") : TEXT("; "), Spot.Stand.X, Spot.Stand.Y, FVector2D::Distance(Spot.Pos, Spot.Stand), Reach);
				return false;
			}
			TwinPlacements.Add(Spot);
			return true;
		}

		// --- Beacons (Brief 10) ---
		//
		// Placed after the wall terminals, from the finished plan, with no random draw: one per Hall segment in route order,
		// over the centre of the Hall's own body (its platforms or its floor, spurs left out), aimed at the point where the
		// route leaves it; the terminal at the entrance with the light groups' rule (FindSideSpot, left side first instead
		// of a drawn side), kept out of every other terminal's reach. A rule that cannot be met leaves the beacon out and
		// the validator fails the seed; there is no fallback position.

		struct FBeaconPlacement
		{
			int32 Segment = -1;
			FVector2D Centre = FVector2D::ZeroVector;
			FVector2D Aim = FVector2D::ZeroVector;
			FVector2D Terminal = FVector2D::ZeroVector;
			FVector2D TerminalFacing = FVector2D(1.0, 0.0);
			float TerminalRouteDistance = 0.f;
		};

		TArray<FBeaconPlacement> BeaconPlacements;
		/** Why a beacon of the recipe was left out (empty when all were placed). */
		FString BeaconError;

		/** TerminalReach: the standard terminal's InteractRadius; LogReach and TwinReach: the wall terminals' (already placed). */
		bool PlaceBeacons(float TerminalReach, float LogReach, float TwinReach)
		{
			const int32 Wanted = FMath::Clamp(R.BeaconCount, 0, 4);
			if (Wanted == 0)
			{
				return true;
			}
			TArray<TPair<FVector2D, float>> Others;
			for (const FVector2D& P : UsedSideSpots) { Others.Emplace(P, TerminalReach); }
			for (const FWallPlacement& P : LogPlacements) { Others.Emplace(P.Pos, LogReach); }
			for (const FWallPlacement& P : TwinPlacements) { Others.Emplace(P.Pos, TwinReach); }
			TArray<int32> Halls;
			for (int32 Index = 0; Index < Plan.Segs.Num(); ++Index)
			{
				if (Plan.Segs[Index].Type == EAreaSegmentType::Hall) { Halls.Add(Index); }
			}
			bool bAll = true;
			for (int32 K = 0; K < Wanted; ++K)
			{
				if (!Halls.IsValidIndex(K))
				{
					BeaconError += FString::Printf(TEXT("%sbeacon %d: the route has %d Hall segments"), BeaconError.IsEmpty() ? TEXT("") : TEXT("; "), K, Halls.Num());
					bAll = false;
					continue;
				}
				const int32 Index = Halls[K];
				const FSeg& S = Plan.Segs[Index];
				FBeaconPlacement B;
				B.Segment = Index;
				FBox2D Body(ForceInit);
				for (const FLeg& Leg : S.Legs) { Body += Leg.Rect; }
				B.Centre = Body.bIsValid ? Body.GetCenter() : S.Bounds.GetCenter();
				B.Aim = Plan.PointAt(S.RouteEnd);
				const TFunction<bool(const FVector2D&)> Accept = [&Others, TerminalReach, this](const FVector2D& T)
				{
					for (const TPair<FVector2D, float>& Other : Others)
					{
						if (FVector2D::Distance(T, Other.Key) < TerminalReach + Other.Value + 50.f) { return false; }
					}
					for (const FGatePlacement& Gate : GatePlacements)
					{
						if (FVector2D::Distance(T, Gate.Door) < 400.f) { return false; }
					}
					return true;
				};
				const float Start = S.RouteStart + 300.f;
				const float MinS = FMath::Max(50.f, S.RouteStart - 500.f);
				if (!FindSideSpot(Start, MinS, B.Terminal, B.TerminalFacing, 1000.f, 1, Accept)
					&& !FindSideSpot(Start, MinS, B.Terminal, B.TerminalFacing, 3000.f, 1, Accept))
				{
					BeaconError += FString::Printf(TEXT("%sbeacon %d (segment %d): no spot for its terminal at the entrance (route %.0f-%.0f m)"),
						BeaconError.IsEmpty() ? TEXT("") : TEXT("; "), K, Index, MinS / 100.f, Start / 100.f);
					bAll = false;
					continue;
				}
				B.TerminalRouteDistance = Plan.Project(B.Terminal);
				Others.Emplace(B.Terminal, TerminalReach);
				BeaconPlacements.Add(B);
			}
			return bAll;
		}

	private:
		static FIntPoint CellAt(const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell)); }

		bool IsFloorOf(const FVector2D& P, int32 Segment) const
		{
			const FCell* E = Plan.Cells.Find(CellAt(P));
			return E && E->Kind == EKind::Floor && E->Segment == Segment;
		}

		/**
		 * A wall spot in Segment between route distances S0 and S1, tried from the middle outwards every cell, left side
		 * first: the wall face straight and solid over the body's width and a margin (no niche, passage, opening or
		 * corner), the body's footprint and the way to the standing place on the segment's own floor (no pillar), the
		 * standing place clear for a pawn, the body's front LineClear from the centreline, the route distance inside
		 * [MinS, MaxS) and 3 m from the start, 6 m before the exit, and no point within reach of this and another terminal.
		 */
		bool FindWallSpot(int32 Segment, float S0, float S1, float MinS, float MaxS, const FWallSpec& Spec, const TArray<TPair<FVector2D, float>>& Others,
			float LineClear, FWallPlacement& Out, FString& OutWhy)
		{
			const float Mid = 0.5f * (S0 + S1);
			const int32 Steps = FMath::CeilToInt(0.5f * (S1 - S0) / Cell) + 1;
			const float MaxWall = FMath::Clamp(G.WallTerminalSearchDistance, 200.f, 5000.f);
			const float Edge = 25.f;
			int32 Tried = 0;
			int32 NoWall = 0, NearLine = 0, NotStraight = 0, NoRoom = 0, NoStand = 0, TooClose = 0, OffRange = 0;
			for (int32 K = 0; K <= 2 * Steps; ++K)
			{
				const float At = Mid + (K % 2 == 1 ? 1.f : -1.f) * ((K + 1) / 2) * Cell;
				if (At < S0 || At > S1)
				{
					continue;
				}
				int32 PointSegment = -1;
				FVector2D Dir;
				const FVector2D P = Plan.PointAt(At, &PointSegment, &Dir);
				if (PointSegment != Segment)
				{
					continue;
				}
				Dir = DirV(DirOf(Dir));
				const FVector2D Lv(-Dir.Y, Dir.X);
				for (const int32 Side : { 1, -1 })
				{
					++Tried;
					const FVector2D OutDir = Lv * Side;
					// Walk to the wall across the segment's own floor.
					bool bWall = false;
					FIntPoint LastCell(0, 0);
					bool bHasLast = false;
					for (float D = Cell * 0.25f; D <= MaxWall; D += Cell * 0.5f)
					{
						const FIntPoint C = CellAt(P + OutDir * D);
						const FCell* E = Plan.Cells.Find(C);
						if (!E)
						{
							bWall = bHasLast;
							break;
						}
						if (E->Kind != EKind::Floor || E->Segment != Segment)
						{
							break;
						}
						LastCell = C;
						bHasLast = true;
					}
					if (!bWall)
					{
						++NoWall;
						continue;
					}
					FVector2D Face = P;
					switch (DirOf(OutDir))
					{
					case 0: Face.X = (LastCell.X + 1) * Cell; break;
					case 2: Face.X = LastCell.X * Cell; break;
					case 1: Face.Y = (LastCell.Y + 1) * Cell; break;
					default: Face.Y = LastCell.Y * Cell; break;
					}
					const float Wall = static_cast<float>(FVector2D::Distance(P, Face));
					if (Wall - Spec.Depth < LineClear)
					{
						++NearLine;
						continue;
					}
					bool bStraight = true;
					for (float A = -Spec.HalfWidth - Edge; A <= Spec.HalfWidth + Edge + 1.f && bStraight; A += Edge)
					{
						bStraight = IsFloorOf(Face + Dir * A - OutDir * (Cell * 0.5f), Segment) && !Plan.Cells.Contains(CellAt(Face + Dir * A + OutDir * (Cell * 0.5f)));
					}
					if (!bStraight)
					{
						++NotStraight;
						continue;
					}
					// The body's footprint and the way to the standing place, on this segment's floor.
					bool bRoom = true;
					for (float D = Cell * 0.5f; D <= Spec.Depth + Edge + 1.f && bRoom; D += Edge)
					{
						for (float A = -Spec.HalfWidth - Edge; A <= Spec.HalfWidth + Edge + 1.f && bRoom; A += Edge)
						{
							bRoom = IsFloorOf(Face - OutDir * D + Dir * A, Segment);
						}
					}
					for (float D = Spec.Depth; D <= Spec.StandDistance + 1.f && bRoom; D += Edge)
					{
						for (const float A : { -45.f, 0.f, 45.f })
						{
							bRoom &= IsFloorOf(Face - OutDir * D + Dir * A, Segment);
						}
					}
					if (!bRoom)
					{
						++NoRoom;
						continue;
					}
					const FVector2D Pos = Face - OutDir * Spec.BackOffset;
					const FVector2D Stand = Face - OutDir * Spec.StandDistance;
					if (!Plan.IsClear(Stand, 60.0) || FVector2D::Distance(Pos, Stand) > Spec.Reach - 10.f)
					{
						++NoStand;
						continue;
					}
					const float RouteS = Plan.Project(Pos);
					const float RouteEnd = Plan.LengthBeforeWayOut;
					if (RouteS < MinS || RouteS >= MaxS || RouteS < 300.f || RouteS > RouteEnd - 600.f || Plan.Project(Stand) > RouteEnd - 600.f)
					{
						++OffRange;
						continue;
					}
					bool bFar = true;
					for (const TPair<FVector2D, float>& Other : Others)
					{
						bFar &= FVector2D::Distance(Pos, Other.Key) >= Spec.Reach + Other.Value + 50.f;
					}
					for (const FGatePlacement& Gate : GatePlacements)
					{
						bFar &= FVector2D::Distance(Pos, Gate.Door) >= 400.f;
					}
					if (!bFar)
					{
						++TooClose;
						continue;
					}
					Out.Segment = Segment;
					Out.Pos = Pos;
					Out.Facing = -OutDir;
					Out.Stand = Stand;
					Out.RouteDistance = RouteS;
					return true;
				}
			}
			OutWhy = FString::Printf(TEXT("%d spots tried: no wall %d, too near the route %d, wall not straight %d, no room %d, no standing place %d, out of range %d, within reach of another terminal %d"),
				Tried, NoWall, NearLine, NotStraight, NoRoom, NoStand, OffRange, TooClose);
			return false;
		}
	};

	// --- Geometry from the plan ---

	/** One wall or curb box as the planner laid it: the cell edges it runs along (for the dressing; the box is the plan's). */
	struct FEdgeRun
	{
		int32 D = 0;      // the floor cells' side the edge is on (0 +X, 1 +Y, 2 -X, 3 -Y)
		int32 Line = 0;   // the edge's grid line
		int32 R0 = 0;     // first and last cell coordinate along the line
		int32 R1 = 0;
		float Ext0 = 0.f; // the box's extension beyond the first and last edge (T at a concave end, 0 at a convex one)
		float Ext1 = 0.f;
	};

	/** The plan's boxes; structure kinds carry their XY footprint (their Z comes from FAreaPieceHeights). */
	struct FGeometryPlan
	{
		TArray<FBox> Floors, Walls, Curbs, Pillars, Ceilings, Backdrop;
		TArray<FAreaFloorRect> FloorRects;
		// Parallel to Walls, Curbs, Ceilings and Pillars: what the dressing needs to know of each box.
		TArray<FEdgeRun> WallRuns, CurbRuns;
		TArray<int32> CeilingSegments, PillarSegments;
		/** Parallel to Walls: the exit corridor's far end (Brief 26), open on the dark: collision only, never dressed. */
		TArray<bool> WallOpen;
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
					Out.CeilingSegments.Add(S);
				}
			}
			for (const FBox2D& P : Plan.Segs[S].Pillars)
			{
				Out.Pillars.Add(BoxTransform(FVector(P.Min.X, P.Min.Y, -FT), FVector(P.Max.X, P.Max.Y, G.WallHeight)));
				Out.PillarSegments.Add(S);
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
				if (bWall)
				{
					bool bOpen = Plan.OpenEdges.Num() > 0;
					for (int32 RC = R0; RC <= R1 && bOpen; ++RC)
					{
						const FIntPoint Floor = bAlongY ? FIntPoint(D == 0 ? Line - 1 : Line, RC) : FIntPoint(RC, D == 1 ? Line - 1 : Line);
						bOpen = Plan.OpenEdges.Contains(FIntVector(Floor.X, Floor.Y, D));
					}
					Out.WallOpen.Add(bOpen);
				}
				FEdgeRun Run;
				Run.D = D;
				Run.Line = Line;
				Run.R0 = R0;
				Run.R1 = R1;
				Run.Ext0 = Ext0;
				Run.Ext1 = Ext1;
				(bWall ? Out.WallRuns : Out.CurbRuns).Add(Run);
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
		// Brief 26: nothing stands beyond the open end of an exit corridor, in its line, out to the backdrop's reach, so that
		// no mass closes it in a scan.
		TArray<FBox2D> ViewStrips;
		for (const FSeg& S : Plan.Segs)
		{
			if (S.bExitCorridor && S.Legs.Num() > 0)
			{
				const FLeg& Leg = S.Legs[0];
				ViewStrips.Add(MakeRect(Leg.Start, Leg.Dir, Leg.Length, Leg.Length + Radius + G.BackdropClearance, -0.5 * S.Width, 0.5 * S.Width));
			}
		}
		auto Clear = [&](const FBox2D& Box)
		{
			const FBox2D B = Grow(Box, G.BackdropClearance);
			for (const FBox2D& Strip : ViewStrips)
			{
				if (B.Intersect(Strip)) { return false; }
			}
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

	// --- Dressing (Brief 16) ---

	/** Which boxes stay shown, and the kit's meshes placed over the others. Parallel flags to FGeometryPlan's arrays. */
	struct FDressing
	{
		TArray<bool> FloorHidden, WallHidden, CurbHidden, PillarHidden, CeilingHidden;
		TArray<FTransform> Panels, Plates, CeilingPlates, Posts, Beams, Conduits, Curbs, Frames, GateFrames;
		/** Brief 26: strips of the floor plates' family from each wall panel's foot to its slab (appended to Plates when published). */
		TArray<FTransform> FloorStrips;
		FString Report;
		AAreaGenerator::FDressPanelPick Squeezed, Stretched, FirstMouth;
		/** Brief 28: the open segments' parts (a hall's own parts, a catwalk's floor boxes), for the backdrop's levels of walkways. */
		struct FOpenPart { FBox2D Rect; int32 Segment = -1; bool bPlatform = false; };
		TArray<FOpenPart> OpenParts;
		/** Brief 28: every catwalk frame's lowest point, with its line along its part and its piece across it, for the pillars. */
		struct FFrameTip { FVector Tip; int32 Line = 0; int32 Lines = 1; int32 Piece = 0; int32 Pieces = 1; bool bPlatform = false; bool bAlongX = true; };
		TArray<FFrameTip> FrameTips;
	};

	/** The floor cell across a run's edge line from RunCell (the other side of the line). */
	static FIntPoint AcrossCell(const FEdgeRun& R, int32 RunCoord)
	{
		switch (R.D)
		{
		case 0: return FIntPoint(R.Line, RunCoord);
		case 2: return FIntPoint(R.Line - 1, RunCoord);
		case 1: return FIntPoint(RunCoord, R.Line);
		default: return FIntPoint(RunCoord, R.Line - 1);
		}
	}

	static FIntPoint RunCell(const FEdgeRun& R, int32 RunCoord)
	{
		switch (R.D)
		{
		case 0: return FIntPoint(R.Line - 1, RunCoord);
		case 2: return FIntPoint(R.Line, RunCoord);
		case 1: return FIntPoint(RunCoord, R.Line - 1);
		default: return FIntPoint(RunCoord, R.Line);
		}
	}

	static bool IsDressedSegment(const FPlan& Plan, int32 Segment)
	{
		return Plan.Segs.IsValidIndex(Segment) && AAreaGenerator::IsDressedSegmentType(Plan.Segs[Segment].Type);
	}

	/** True when every floor cell along the run belongs to a modelled segment; OutAllType: every one is of Type. */
	static bool IsRunDressed(const FPlan& Plan, const FEdgeRun& R, EAreaSegmentType Type, bool& OutAllType)
	{
		OutAllType = true;
		for (int32 C = R.R0; C <= R.R1; ++C)
		{
			const FCell* E = Plan.Cells.Find(RunCell(R, C));
			if (!E || !IsDressedSegment(Plan, E->Segment))
			{
				OutAllType = false;
				return false;
			}
			OutAllType &= Plan.Segs[E->Segment].Type == Type;
		}
		return true;
	}

	/** World transform of a piece of Size (its pivot at its base centre) filling the box Min..Max (XY), local +X along Facing. */
	static FTransform FitPiece(const FVector2D& Centre, double Z, int32 FacingDir, double Thickness, double Length, double Height, const FVector& Size, double Roll = 0.0)
	{
		const FVector2D F = DirV(FacingDir);
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(F.Y, F.X));
		return FTransform(FRotator(0.0, Yaw, Roll), FVector(Centre.X, Centre.Y, Z),
			FVector(Thickness / Size.X, Length / Size.Y, Height / Size.Z));
	}

	/** The smallest and largest factor a kit piece was scaled by, and where (for the report). */
	struct FFactorRange
	{
		double Min = TNumericLimits<double>::Max();
		double Max = -TNumericLimits<double>::Max();
		FVector2D MinAt = FVector2D::ZeroVector;
		FVector2D MaxAt = FVector2D::ZeroVector;
		int32 Count = 0;

		void Add(double F, const FVector2D& At)
		{
			++Count;
			if (F < Min) { Min = F; MinAt = At; }
			if (F > Max) { Max = F; MaxAt = At; }
		}

		FString ToString(const TCHAR* Name) const
		{
			return Count == 0 ? FString::Printf(TEXT("%s 0"), Name)
				: FString::Printf(TEXT("%s %d (x%.3f at (%.0f, %.0f) .. x%.3f at (%.0f, %.0f))"), Name, Count, Min, MinAt.X, MinAt.Y, Max, MaxAt.X, MaxAt.Y);
		}
	};

	static void BuildDressing(const AAreaGenerator& G, const FPlan& Plan, const FGeometryPlan& Geo, FDressing& Out)
	{
		const double FT = G.FloorThickness;
		const double T = G.WallThickness;
		const double WallTop = G.WallHeight + FT;  // walls are dressed from the floor's underside to the ceiling's top
		const FVector PanelSize = G.DressWallPanelSize.ComponentMax(FVector(1.0));
		const FVector PlateSize = G.DressFloorPlateSize.ComponentMax(FVector(1.0));
		const FVector PostSize = G.DressCornerPostSize.ComponentMax(FVector(1.0));
		const FVector CurbSize = G.DressCurbSize.ComponentMax(FVector(1.0));
		const double BeamLength = FMath::Max(1.0, static_cast<double>(G.DressBeamLength));
		const double ConduitLen = FMath::Max(1.0, static_cast<double>(G.DressConduitLength));
		// How far a conduit stands in front of its wall's face (its back is on the panels' slab, the box's middle).
		const double ConduitFront = FMath::Max(0.0, G.DressConduitDepth - 0.5 * T);
		const double FrameHalfDepth = 0.5 * FMath::Max(1.0, static_cast<double>(G.DressGateFrameDepth));
		FFactorRange PanelAlong, PlateShort, PlateLong, BeamF, CurbF, ConduitF, FrameF, Fillers;
		// The range a wall panel or a curb is scaled along by (shorter spans get a filler block).
		constexpr double KitMinFactor = 2.0 / 3.0;
		constexpr double KitMaxFactor = 4.0 / 3.0;

		// Which boxes are kept for collision only.
		Out.FloorHidden.Init(false, Geo.Floors.Num());
		for (int32 I = 0; I < Geo.FloorRects.Num() && I < Geo.Floors.Num(); ++I)
		{
			Out.FloorHidden[I] = IsDressedSegment(Plan, Geo.FloorRects[I].Segment);
		}
		Out.CeilingHidden.Init(false, Geo.Ceilings.Num());
		for (int32 I = 0; I < Geo.CeilingSegments.Num() && I < Geo.Ceilings.Num(); ++I)
		{
			Out.CeilingHidden[I] = IsDressedSegment(Plan, Geo.CeilingSegments[I]);
		}
		Out.PillarHidden.Init(false, Geo.Pillars.Num());
		for (int32 I = 0; I < Geo.PillarSegments.Num() && I < Geo.Pillars.Num(); ++I)
		{
			Out.PillarHidden[I] = IsDressedSegment(Plan, Geo.PillarSegments[I]);
		}
		Out.WallHidden.Init(false, Geo.Walls.Num());
		for (int32 I = 0; I < Geo.WallRuns.Num() && I < Geo.Walls.Num(); ++I)
		{
			bool bUnused = false;
			Out.WallHidden[I] = IsRunDressed(Plan, Geo.WallRuns[I], EAreaSegmentType::Corridor, bUnused);
		}
		int32 ShownMixed = 0;
		auto TouchesDressed = [&](const FEdgeRun& R)
		{
			for (int32 C = R.R0; C <= R.R1; ++C)
			{
				const FCell* E = Plan.Cells.Find(RunCell(R, C));
				if (E && IsDressedSegment(Plan, E->Segment)) { return true; }
			}
			return false;
		};
		for (int32 I = 0; I < Geo.WallRuns.Num() && I < Out.WallHidden.Num(); ++I)
		{
			ShownMixed += !Out.WallHidden[I] && TouchesDressed(Geo.WallRuns[I]) ? 1 : 0;
		}
		Out.CurbHidden.Init(false, Geo.Curbs.Num());
		for (int32 I = 0; I < Geo.CurbRuns.Num() && I < Geo.Curbs.Num(); ++I)
		{
			bool bUnused = false;
			Out.CurbHidden[I] = IsRunDressed(Plan, Geo.CurbRuns[I], EAreaSegmentType::Catwalk, bUnused);
		}

		// Floor plates over floors (and catwalk decks), turned over under ceilings: the long side across the box's short side.
		auto Tile = [&](const FBox& Box, double Z, bool bTurnOver, FFactorRange* LongSide = nullptr, FFactorRange* ShortSide = nullptr)
		{
			const double SX = Box.Max.X - Box.Min.X;
			const double SY = Box.Max.Y - Box.Min.Y;
			// Of the two ways to lay the plates (their long side along X or along Y), the one whose plates are scaled least.
			auto Layout = [&](bool bLongAlongX, int32& NX, int32& NY, double& Deviation)
			{
				const double PX = bLongAlongX ? PlateSize.X : PlateSize.Y;  // the plate's size along world X
				const double PY = bLongAlongX ? PlateSize.Y : PlateSize.X;
				NX = FMath::Max(1, FMath::RoundToInt(SX / PX));
				NY = FMath::Max(1, FMath::RoundToInt(SY / PY));
				Deviation = FMath::Max(FMath::Abs(SX / NX / PX - 1.0), FMath::Abs(SY / NY / PY - 1.0));
			};
			int32 NX0, NY0, NX1, NY1;
			double D0, D1;
			Layout(true, NX0, NY0, D0);
			Layout(false, NX1, NY1, D1);
			// Ties keep the long side across the box's shorter side.
			const bool bPreferX = SX <= SY;
			const bool bLongAlongX = D0 < D1 - 1e-6 ? true : (D1 < D0 - 1e-6 ? false : bPreferX);
			const int32 NX = bLongAlongX ? NX0 : NX1;
			const int32 NY = bLongAlongX ? NY0 : NY1;
			const double CX = SX / NX;
			const double CY = SY / NY;
			// The plate's own long (X) and short (Y) sides after the turn.
			const double PL = bLongAlongX ? CX : CY;
			const double PS = bLongAlongX ? CY : CX;
			const FVector2D Centre(0.5 * (Box.Min.X + Box.Max.X), 0.5 * (Box.Min.Y + Box.Max.Y));
			(LongSide ? *LongSide : PlateShort).Add(PL / PlateSize.X, Centre);
			(ShortSide ? *ShortSide : PlateLong).Add(PS / PlateSize.Y, Centre);
			for (int32 A = 0; A < NX; ++A)
			{
				for (int32 B = 0; B < NY; ++B)
				{
					const FVector2D C(Box.Min.X + (A + 0.5) * CX, Box.Min.Y + (B + 0.5) * CY);
					const FRotator R(0.0, bLongAlongX ? 0.0 : 90.0, bTurnOver ? 180.0 : 0.0);
					(bTurnOver ? Out.CeilingPlates : Out.Plates).Add(FTransform(R, FVector(C.X, C.Y, Z), FVector(PL / PlateSize.X, PS / PlateSize.Y, FT / PlateSize.Z)));
				}
			}
		};
		auto IsHall = [&Plan](int32 Segment) { return Plan.Segs.IsValidIndex(Segment) && Plan.Segs[Segment].Type == EAreaSegmentType::Hall; };
		for (int32 I = 0; I < Geo.Floors.Num(); ++I)
		{
			// Halls are tiled below, by their own parts.
			if (Out.FloorHidden[I] && !(I < Geo.FloorRects.Num() && IsHall(Geo.FloorRects[I].Segment)))
			{
				Tile(Geo.Floors[I], -FT, false);
			}
		}

		// Halls (Brief 19): their floors are tiled by the hall's own parts, not by its collision boxes (the greedy merge
		// runs a web's walkways through its platforms and cuts the platforms into slivers): first its Legs (a web's
		// junction platforms, a floor hall's whole floor), then the platforms at the ends of its spurs, then what remains
		// (the walkways and the spurs' strips), merged. The same cells, the same top: only the joints of the plates move.
		struct FHallPart { FBox Box; int32 Segment = -1; bool bPlatform = false; };
		TArray<FHallPart> HallParts;
		FFactorRange HallPlateLong, HallPlateShort;
		int32 HallSegments = 0, HallPlatforms = 0;
		for (int32 S = 0; S < Plan.Segs.Num(); ++S)
		{
			if (!IsHall(S) || !IsDressedSegment(Plan, S))
			{
				continue;
			}
			++HallSegments;
			TSet<FIntPoint> Left;
			for (const TPair<FIntPoint, FCell>& Pair : Plan.Cells)
			{
				if (Pair.Value.Segment == S && Pair.Value.Kind == EKind::Floor) { Left.Add(Pair.Key); }
			}
			auto AllLeft = [&Left](const FBox2D& R)
			{
				TArray<FIntPoint> Cells;
				CellsIn(R, Cells);
				for (const FIntPoint& C : Cells)
				{
					if (!Left.Contains(C)) { return false; }
				}
				return Cells.Num() > 0;
			};
			auto Take = [&](const FBox2D& R, bool bPlatform)
			{
				if (!AllLeft(R))
				{
					return;
				}
				TArray<FIntPoint> Cells;
				CellsIn(R, Cells);
				for (const FIntPoint& C : Cells) { Left.Remove(C); }
				HallParts.Add({ FBox(FVector(R.Min.X, R.Min.Y, -FT), FVector(R.Max.X, R.Max.Y, 0.0)), S, bPlatform });
				HallPlatforms += bPlatform ? 1 : 0;
			};
			for (const FLeg& Leg : Plan.Segs[S].Legs)
			{
				Take(Leg.Rect, true);
			}
			// A spur's platform: the largest square of the hall's cells around its centre.
			for (const FSpot& Spot : Plan.SpurEnds)
			{
				if (Spot.Segment != S || !Spot.bOpen)
				{
					continue;
				}
				double Half = 0.0;
				for (double H = Cell; H <= 3000.0 && AllLeft(FBox2D(Spot.Pos - FVector2D(H, H), Spot.Pos + FVector2D(H, H))); H += Cell)
				{
					Half = H;
				}
				if (Half > 0.0)
				{
					Take(FBox2D(Spot.Pos - FVector2D(Half, Half), Spot.Pos + FVector2D(Half, Half)), true);
				}
			}
			TArray<FIntRect> Rects;
			MergeRects(Left.Array(), Rects);
			for (const FIntRect& R : Rects)
			{
				HallParts.Add({ FBox(FVector(R.Min.X * Cell, R.Min.Y * Cell, -FT), FVector((R.Max.X + 1) * Cell, (R.Max.Y + 1) * Cell, 0.0)), S, false });
			}
		}
		for (const FHallPart& Part : HallParts)
		{
			Tile(Part.Box, -FT, false, &HallPlateLong, &HallPlateShort);
		}
		for (int32 I = 0; I < Geo.Ceilings.Num(); ++I)
		{
			if (Out.CeilingHidden[I])
			{
				// Turned over, the plate's pivot is its top: the ceiling's top.
				Tile(Geo.Ceilings[I], G.WallHeight + FT, true);
			}
		}

		// Gate frames: around each modelled gate's door; the gate's walls stop on each side of the frame.
		struct FFrame { FVector2D Door; int32 Dir = 0; double HalfWidth = 0.0; };
		TArray<FFrame> Frames;
		for (int32 S = 0; S < Plan.Segs.Num(); ++S)
		{
			const FSeg& Seg = Plan.Segs[S];
			if (Seg.Type != EAreaSegmentType::Gate || !IsDressedSegment(Plan, S))
			{
				continue;
			}
			Frames.Add({ Seg.DoorPos, Seg.DoorDir, 0.5 * Seg.Width });
			const FVector2D F = DirV(Seg.DoorDir);
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(F.Y, F.X));
			const FVector2D Size = G.DressGateFrameSize.ComponentMax(FVector2D(1.0, 1.0));
			const double Across = Seg.Width + 4.0 * T;
			FrameF.Add(Across / Size.X, Seg.DoorPos);
			Out.GateFrames.Add(FTransform(FRotator(0.0, Yaw, 0.0), FVector(Seg.DoorPos.X, Seg.DoorPos.Y, 0.0),
				FVector(1.0, Across / Size.X, (WallTop + FT) / Size.Y)));
		}
		// The parts of [A, B] along a run that a gate frame does not take.
		auto Spans = [&](bool bAlongY, double Mid, double A, double B)
		{
			TArray<FVector2D> Cuts;
			for (const FFrame& Fr : Frames)
			{
				const bool bFrameAlongY = Fr.Dir == 1 || Fr.Dir == 3;
				if (bFrameAlongY != bAlongY)
				{
					continue;
				}
				const double DoorAlong = bAlongY ? Fr.Door.Y : Fr.Door.X;
				const double DoorAcross = bAlongY ? Fr.Door.X : Fr.Door.Y;
				if (FMath::Abs(Mid - DoorAcross) <= Fr.HalfWidth + T + 1.0 && DoorAlong > A - 1.0 && DoorAlong < B + 1.0)
				{
					Cuts.Add(FVector2D(DoorAlong - FrameHalfDepth, DoorAlong + FrameHalfDepth));
				}
			}
			Cuts.Sort([](const FVector2D& X, const FVector2D& Y) { return X.X < Y.X; });
			TArray<FVector2D> Result;
			double From = A;
			for (const FVector2D& Cut : Cuts)
			{
				if (Cut.X > From) { Result.Add(FVector2D(From, FMath::Min(Cut.X, B))); }
				From = FMath::Max(From, Cut.Y);
			}
			if (B > From) { Result.Add(FVector2D(From, B)); }
			return Result;
		};

		// Corner squares: at a convex end (no extension) the run's last T belongs to the square shared with the run that
		// turns there; at a concave end the box's extension is the square. A post fills each square no shown box covers.
		struct FSquare { FBox2D Box; bool bWall = false; };
		TMap<FIntPoint, FSquare> Squares;
		auto SquareKey = [](const FBox2D& Box) { const FVector2D C = Box.GetCenter(); return FIntPoint(FMath::RoundToInt(C.X * 2.0), FMath::RoundToInt(C.Y * 2.0)); };
		auto AddSquare = [&](const FBox2D& Box, bool bWall)
		{
			FSquare& Sq = Squares.FindOrAdd(SquareKey(Box));
			Sq.Box = Box;
			Sq.bWall |= bWall;
		};
		struct FRunGeo
		{
			bool bAlongY = false;
			double Out0 = 0.0, Mid = 0.0, A = 0.0, B = 0.0;
			int32 Facing = 0;
			bool bConvex0 = false, bConvex1 = false;
			FBox2D Square0, Square1;
		};
		auto RunGeo = [&](const FEdgeRun& R)
		{
			FRunGeo Gm;
			Gm.bAlongY = R.D == 0 || R.D == 2;
			const double LineCm = R.Line * Cell;
			Gm.Out0 = (R.D == 0 || R.D == 1) ? LineCm : LineCm - T;
			Gm.Mid = Gm.Out0 + 0.5 * T;
			Gm.Facing = Opposite(R.D);  // towards the floor
			Gm.A = R.R0 * Cell;
			Gm.B = (R.R1 + 1) * Cell;
			Gm.bConvex0 = R.Ext0 <= 0.f;
			Gm.bConvex1 = R.Ext1 <= 0.f;
			auto Sq = [&](double S0, double S1)
			{
				return Gm.bAlongY ? FBox2D(FVector2D(Gm.Out0, S0), FVector2D(Gm.Out0 + T, S1)) : FBox2D(FVector2D(S0, Gm.Out0), FVector2D(S1, Gm.Out0 + T));
			};
			Gm.Square0 = Gm.bConvex0 ? Sq(Gm.A, Gm.A + T) : Sq(Gm.A - T, Gm.A);
			Gm.Square1 = Gm.bConvex1 ? Sq(Gm.B - T, Gm.B) : Sq(Gm.B, Gm.B + T);
			return Gm;
		};
		auto At = [](const FRunGeo& Gm, double Mid, double S) { return Gm.bAlongY ? FVector2D(Mid, S) : FVector2D(S, Mid); };
		// Brief 26: the wall panel's slab stands at the wall box's middle, its ribs reach the floor's edge: between the ribs
		// the floor would stop short of the wall by half the box. A strip of SM_KitFloorPlate (the floors' family) under each
		// panel fills it, from the floor's edge to 1 cm into the slab, its top at the floor's.
		const bool bStrips = IsAreaFloorStripsOn();
		const double StripWidth = 0.5 * T + 1.0;
		auto PlacePieces = [&](const FRunGeo& Gm, double A1, double B1, bool bWall, bool bStrip)
		{
			const double Len = B1 - A1;
			if (Len < 1.0)
			{
				return;
			}
			const FVector Size = bWall ? PanelSize : CurbSize;
			const double Bottom = bWall ? -FT : G.CurbHeight - CurbSize.Z;
			const double Top = bWall ? WallTop : G.CurbHeight;
			if (Len < Size.Y * KitMinFactor)
			{
				// Too short for a piece within its factors: a plain filler block (the corner post), flush with the face.
				const FVector2D C = At(Gm, Gm.Mid, 0.5 * (A1 + B1));
				Fillers.Add(Len, C);
				Out.Posts.Add(FitPiece(C, Bottom, Gm.Facing, T, Len, Top - Bottom, FVector(PostSize.X, PostSize.Y, PostSize.Z)));
				return;
			}
			// As few pieces as keep each within the factors (along: x0.667 to x1.333 of the piece).
			const int32 N = FMath::Max(1, FMath::CeilToInt(Len / (Size.Y * KitMaxFactor) - 1e-6));
			const double PieceLen = Len / N;
			for (int32 K = 0; K < N; ++K)
			{
				const FVector2D C = At(Gm, Gm.Mid, A1 + (K + 0.5) * PieceLen);
				if (bWall)
				{
					const double Factor = PieceLen / Size.Y;
					if (PanelAlong.Count == 0 || Factor < Out.Squeezed.Factor) { Out.Squeezed = { C, DirV(Gm.Facing), Factor }; }
					if (PanelAlong.Count == 0 || Factor > Out.Stretched.Factor) { Out.Stretched = { C, DirV(Gm.Facing), Factor }; }
					PanelAlong.Add(Factor, C);
					Out.Panels.Add(FitPiece(C, -FT, Gm.Facing, T, PieceLen, WallTop + FT, Size));
					if (bStrip && bStrips)
					{
						// The strip's middle: from the floor's edge (the box's face toward the floor) into the slab.
						const FVector2D Toward = DirV(Gm.Facing);
						const FVector2D SC = C + Toward * (0.5 * T - 0.5 * StripWidth);
						const double Yaw = Gm.bAlongY ? 90.0 : 0.0;  // the plate's long side (its X) along the run
						Out.FloorStrips.Add(FTransform(FRotator(0.0, Yaw, 0.0), FVector(SC.X, SC.Y, -FT),
							FVector(PieceLen / PlateSize.X, StripWidth / PlateSize.Y, FT / PlateSize.Z)));
					}
				}
				else
				{
					// The curb's top at CurbHeight, its girder hanging under the deck.
					CurbF.Add(PieceLen / Size.Y, C);
					Out.Curbs.Add(FitPiece(C, G.CurbHeight - Size.Z, Gm.Facing, T, PieceLen, Size.Z, Size));
				}
			}
		};
		auto DressRun = [&](const FEdgeRun& R, bool bWall)
		{
			const FRunGeo Gm = RunGeo(R);
			AddSquare(Gm.Square0, bWall);
			AddSquare(Gm.Square1, bWall);
			const double A1 = Gm.A + (Gm.bConvex0 ? T : 0.0);
			const double B1 = Gm.B - (Gm.bConvex1 ? T : 0.0);
			for (const FVector2D& Span : Spans(Gm.bAlongY, Gm.Mid, A1, B1))
			{
				PlacePieces(Gm, Span.X, Span.Y, bWall, bWall);
			}
		};
		auto IsOpenWall = [&Geo](int32 I) { return Geo.WallOpen.IsValidIndex(I) && Geo.WallOpen[I]; };
		for (int32 I = 0; I < Geo.WallRuns.Num(); ++I)
		{
			// The exit corridor's open end (Brief 26) is not dressed: nothing shows there.
			if (Out.WallHidden[I] && !IsOpenWall(I))
			{
				DressRun(Geo.WallRuns[I], true);
			}
		}
		for (int32 I = 0; I < Geo.CurbRuns.Num(); ++I)
		{
			if (Out.CurbHidden[I])
			{
				DressRun(Geo.CurbRuns[I], false);
			}
		}

		// Pillars: a wall panel on each face, ribs out, and a post in each corner; a plate closes the bottom (the ceiling
		// plates already pass over them).
		for (int32 I = 0; I < Geo.Pillars.Num(); ++I)
		{
			if (!Out.PillarHidden[I])
			{
				continue;
			}
			const FBox& P = Geo.Pillars[I];
			struct FFace { bool bAlongY; double Out0; int32 Facing; double A; double B; };
			const FFace Faces[4] = {
				{ true, P.Min.X, 2, P.Min.Y + T, P.Max.Y - T },
				{ true, P.Max.X - T, 0, P.Min.Y + T, P.Max.Y - T },
				{ false, P.Min.Y, 3, P.Min.X + T, P.Max.X - T },
				{ false, P.Max.Y - T, 1, P.Min.X + T, P.Max.X - T } };
			for (const FFace& Face : Faces)
			{
				FRunGeo Gm;
				Gm.bAlongY = Face.bAlongY;
				Gm.Out0 = Face.Out0;
				Gm.Mid = Face.Out0 + 0.5 * T;
				Gm.Facing = Face.Facing;
				PlacePieces(Gm, Face.A, Face.B, true, false);
			}
			for (const FVector2D& Corner : { FVector2D(P.Min.X, P.Min.Y), FVector2D(P.Max.X - T, P.Min.Y), FVector2D(P.Min.X, P.Max.Y - T), FVector2D(P.Max.X - T, P.Max.Y - T) })
			{
				AddSquare(FBox2D(Corner, Corner + FVector2D(T, T)), true);
			}
			Tile(FBox(FVector(P.Min.X, P.Min.Y, -FT), FVector(P.Max.X, P.Max.Y, 0.0)), -FT, false);
		}

		// A post in each corner square that no shown box covers, as tall as the tallest box there.
		TArray<FIntPoint> Keys;
		Squares.GetKeys(Keys);
		Keys.Sort([](const FIntPoint& X, const FIntPoint& Y) { return X.Y != Y.Y ? X.Y < Y.Y : X.X < Y.X; });
		for (const FIntPoint& Key : Keys)
		{
			const FSquare& Sq = Squares[Key];
			const FVector2D C = Sq.Box.GetCenter();
			// No post on the exit corridor's open end (Brief 26): its side walls simply stop.
			bool bOnOpenEnd = false;
			for (int32 I = 0; I < Geo.Walls.Num() && !bOnOpenEnd; ++I)
			{
				bOnOpenEnd = IsOpenWall(I) && C.X > Geo.Walls[I].Min.X - 0.1 && C.X < Geo.Walls[I].Max.X + 0.1 && C.Y > Geo.Walls[I].Min.Y - 0.1 && C.Y < Geo.Walls[I].Max.Y + 0.1;
			}
			if (bOnOpenEnd)
			{
				continue;
			}
			bool bShownCovers = false;
			bool bWallCovers = Sq.bWall;
			auto Covers = [&C](const FBox& Box) { return C.X > Box.Min.X + 0.1 && C.X < Box.Max.X - 0.1 && C.Y > Box.Min.Y + 0.1 && C.Y < Box.Max.Y - 0.1; };
			for (int32 I = 0; I < Geo.Walls.Num() && !bShownCovers; ++I)
			{
				if (Covers(Geo.Walls[I]))
				{
					bShownCovers |= !Out.WallHidden[I];
					bWallCovers = true;
				}
			}
			for (int32 I = 0; I < Geo.Curbs.Num() && !bShownCovers; ++I)
			{
				bShownCovers |= Covers(Geo.Curbs[I]) && !Out.CurbHidden[I];
			}
			for (int32 I = 0; I < Geo.Pillars.Num() && !bShownCovers; ++I)
			{
				bShownCovers |= Covers(Geo.Pillars[I]) && !Out.PillarHidden[I];
			}
			if (bShownCovers)
			{
				continue;
			}
			const double Bottom = bWallCovers ? -FT : G.CurbHeight - CurbSize.Z;
			const double Top = bWallCovers ? WallTop : G.CurbHeight;
			Out.Posts.Add(FitPiece(C, Bottom, 0, Sq.Box.GetSize().X, Sq.Box.GetSize().Y, Top - Bottom, PostSize));
		}

		// Conduits: one continuous run along each wall line of a corridor or a gate (cells inside its legs: not inside
		// niches or dead ends) at least a metre long. Where a niche or a dead end of the same segment opens in the wall,
		// the run goes straight on across its mouth, at the same height, to the wall on its far side: the walls on the
		// two sides of a mouth are one line. A line ends where its wall ends: against the wall that closes it, at the
		// edge of any other opening (the face of the wall that turns away there), or against a gate frame. Where two
		// walls that both carry one meet, the run along X meets the run along Y: at an inside corner it stops at the
		// other run's front, at an outside corner it goes on to the other run's front, so the two join.
		auto IsMainCell = [&](const FIntPoint& C)
		{
			const FCell* E = Plan.Cells.Find(C);
			if (!E || E->Kind != EKind::Floor || !Plan.Segs.IsValidIndex(E->Segment))
			{
				return false;
			}
			const FSeg& Sg = Plan.Segs[E->Segment];
			if (Sg.Type != EAreaSegmentType::Corridor && Sg.Type != EAreaSegmentType::Gate)
			{
				return false;
			}
			const FVector2D P = CellCenter(C);
			for (const FLeg& Leg : Sg.Legs)
			{
				if (Inside(Leg.Rect, P)) { return true; }
			}
			return false;
		};
		// A niche's or a dead end's mouth along a run's line: the corridor's (or gate's) floor on one side of the line and
		// a floor cell of the same segment outside its legs on the other, so no wall stands on the line there.
		auto IsMouth = [&](const FEdgeRun& R, int32 C)
		{
			const FIntPoint Floor = RunCell(R, C);
			const FIntPoint Across = AcrossCell(R, C);
			const FCell* F = Plan.Cells.Find(Floor);
			const FCell* A = Plan.Cells.Find(Across);
			return F && A && A->Kind == EKind::Floor && A->Segment == F->Segment && IsMainCell(Floor) && !IsMainCell(Across);
		};
		TArray<bool> CarriesConduit;
		CarriesConduit.Init(false, Geo.WallRuns.Num());
		TMap<FIntPoint, TArray<int32>> ConduitCorners;
		TMap<FIntVector, int32> RunStarts;  // (D, Line, R0) of each run that carries one
		for (int32 I = 0; I < Geo.WallRuns.Num(); ++I)
		{
			const FEdgeRun& R = Geo.WallRuns[I];
			if (!Out.WallHidden[I] || IsOpenWall(I))
			{
				continue;
			}
			bool bMain = true;
			for (int32 C = R.R0; C <= R.R1 && bMain; ++C)
			{
				bMain = IsMainCell(RunCell(R, C));
			}
			if (!bMain)
			{
				continue;
			}
			CarriesConduit[I] = true;
			RunStarts.Add(FIntVector(R.D, R.Line, R.R0), I);
			const FRunGeo Gm = RunGeo(R);
			ConduitCorners.FindOrAdd(SquareKey(Gm.Square0)).Add(I);
			ConduitCorners.FindOrAdd(SquareKey(Gm.Square1)).Add(I);
		}
		// Lines: runs on one wall line joined across the mouths between them, in run order.
		TArray<int32> NextRun, MouthEnd;
		NextRun.Init(INDEX_NONE, Geo.WallRuns.Num());
		MouthEnd.Init(0, Geo.WallRuns.Num());
		TArray<bool> HasPrevious;
		HasPrevious.Init(false, Geo.WallRuns.Num());
		for (int32 I = 0; I < Geo.WallRuns.Num(); ++I)
		{
			if (!CarriesConduit[I])
			{
				continue;
			}
			const FEdgeRun& R = Geo.WallRuns[I];
			int32 C = R.R1 + 1;
			while (IsMouth(R, C) && C - R.R1 <= 64)
			{
				++C;
			}
			const int32* J = C > R.R1 + 1 ? RunStarts.Find(FIntVector(R.D, R.Line, C)) : nullptr;
			if (J && *J != I)
			{
				NextRun[I] = *J;
				MouthEnd[I] = C;
				HasPrevious[*J] = true;
			}
		}
		auto Shared = [&](const FBox2D& Square)
		{
			const TArray<int32>* Runs = ConduitCorners.Find(SquareKey(Square));
			return Runs && Runs->Num() > 1;
		};
		// SM_KitConduit has one bracket, its middle a quarter of the piece's length from the piece's middle along the
		// piece's -Y (Blender's +Y is Unreal's -Y), and a fortieth of its length wide on each side.
		constexpr double BracketAt = -0.25;
		constexpr double BracketHalf = 0.025;
		constexpr double BracketMargin = 2.0;  // cm a bracket keeps from a mouth's edge
		FFactorRange ConduitCrossF;
		int32 MouthsCrossed = 0, LinesUnsolved = 0;
		struct FMouthLine { bool bAlongY; double Mid; double A; double B; };
		TArray<FMouthLine> AllMouths;
		double FirstMouthDistance = TNumericLimits<double>::Max();
		const FVector2D RouteStart = Plan.Segs.Num() > 0 && Plan.Segs[0].Legs.Num() > 0 ? FVector2D(Plan.Segs[0].Legs[0].Start) : FVector2D::ZeroVector;
		for (int32 First = 0; First < Geo.WallRuns.Num(); ++First)
		{
			if (!CarriesConduit[First] || HasPrevious[First])
			{
				continue;
			}
			int32 Last = First;
			TArray<FVector2D> Mouths;  // along the line, between a run's end and the next run's start
			for (int32 Guard = 0; NextRun[Last] != INDEX_NONE && Guard < 256; ++Guard)
			{
				const FEdgeRun& R = Geo.WallRuns[Last];
				Mouths.Add(FVector2D((R.R1 + 1) * Cell, MouthEnd[Last] * Cell));
				Last = NextRun[Last];
			}
			const FRunGeo Gm = RunGeo(Geo.WallRuns[First]);
			const FRunGeo GmLast = RunGeo(Geo.WallRuns[Last]);
			const bool bAlongX = !Gm.bAlongY;
			double S0 = Gm.A;
			double S1 = GmLast.B;
			if (S1 - S0 < 100.0)
			{
				continue;
			}
			if (bAlongX && Shared(Gm.Square0)) { S0 += Gm.bConvex0 ? -ConduitFront : ConduitFront; }
			if (bAlongX && Shared(GmLast.Square1)) { S1 += GmLast.bConvex1 ? ConduitFront : -ConduitFront; }
			for (const FVector2D& M : Mouths)
			{
				++MouthsCrossed;
				AllMouths.Add({ Gm.bAlongY, Gm.Mid, M.X, M.Y });
				const FVector2D MouthAt = At(Gm, Gm.Mid, 0.5 * (M.X + M.Y));
				const double Distance = FVector2D::Distance(MouthAt, RouteStart);
				if (Distance < FirstMouthDistance)
				{
					FirstMouthDistance = Distance;
					Out.FirstMouth = { MouthAt, DirV(Gm.Facing), M.Y - M.X };
				}
			}
			// Where the bracket of a piece from S to S + L falls along the line, by the way the piece's +Y runs.
			const FVector2D F = DirV(Gm.Facing);
			const double Sigma = Gm.bAlongY ? F.X : -F.Y;
			auto BracketClear = [&](double S, double L)
			{
				const double Middle = S + 0.5 * L + Sigma * BracketAt * L;
				const double Half = BracketHalf * L + BracketMargin;
				for (const FVector2D& M : Mouths)
				{
					if (Middle + Half > M.X && Middle - Half < M.Y) { return false; }
				}
				return true;
			};
			for (const FVector2D& Span : Spans(Gm.bAlongY, Gm.Mid, S0, S1))
			{
				const double Len = Span.Y - Span.X;
				if (Len < 1.0)
				{
					continue;
				}
				TArray<double> Lengths;
				bool bCrosses = false;
				for (const FVector2D& M : Mouths)
				{
					bCrosses |= M.X < Span.Y && M.Y > Span.X;
				}
				if (bCrosses)
				{
					// The pieces' lengths, on a 5 cm step (the last one takes what is left), that keep every bracket on a
					// wall and scale the pieces least: the sum of the squared log of each piece's factor.
					constexpr double Step = 5.0;
					const double MinL = 0.5 * ConduitLen;
					const double MaxL = 2.5 * ConduitLen;
					const int32 K = FMath::FloorToInt(Len / Step);
					TArray<double> Best;
					TArray<int32> Prev;
					Best.Init(TNumericLimits<double>::Max(), K + 2);
					Prev.Init(INDEX_NONE, K + 2);
					Best[0] = 0.0;
					auto Cost = [&](double L) { return FMath::Square(FMath::Loge(L / ConduitLen)); };
					for (int32 P = 0; P <= K; ++P)
					{
						if (Best[P] == TNumericLimits<double>::Max())
						{
							continue;
						}
						const double From = P * Step;
						for (int32 Q = P + 1; Q <= K + 1; ++Q)
						{
							const double To = Q == K + 1 ? Len : Q * Step;
							const double L = To - From;
							if (L > MaxL) { break; }
							if (L < MinL || (Q == K && Len - To < 1e-6) || !BracketClear(Span.X + From, L)) { continue; }
							const double C = Best[P] + Cost(L);
							if (C < Best[Q]) { Best[Q] = C; Prev[Q] = P; }
						}
					}
					if (Prev[K + 1] != INDEX_NONE)
					{
						for (int32 Q = K + 1; Q > 0; Q = Prev[Q])
						{
							const double To = Q == K + 1 ? Len : Q * Step;
							Lengths.Insert(To - Prev[Q] * Step, 0);
						}
					}
					else
					{
						++LinesUnsolved;
					}
				}
				if (Lengths.Num() == 0)
				{
					const int32 N = FMath::Max(1, FMath::RoundToInt(Len / ConduitLen));
					Lengths.Init(Len / N, N);
				}
				double S = Span.X;
				for (const double L : Lengths)
				{
					const FVector2D C = At(Gm, Gm.Mid, S + 0.5 * L);
					bool bOverMouth = false;
					for (const FVector2D& M : Mouths)
					{
						bOverMouth |= M.X < S + L && M.Y > S;
					}
					(bOverMouth ? ConduitCrossF : ConduitF).Add(L / ConduitLen, C);
					Out.Conduits.Add(FitPiece(C, G.DressConduitHeight, Gm.Facing, 1.0, L, 1.0, FVector(1.0, ConduitLen, 1.0)));
					S += L;
				}
			}
		}
		// Every placed bracket, from its piece's transform, against every mouth crossed (must be none).
		int32 BracketsOverMouth = 0;
		for (const FTransform& Piece : Out.Conduits)
		{
			const FVector Bracket = Piece.TransformPosition(FVector(0.5 * G.DressConduitDepth, BracketAt * ConduitLen, 0.0));
			const double Half = BracketHalf * ConduitLen * Piece.GetScale3D().Y;
			for (const FMouthLine& M : AllMouths)
			{
				const double Along = M.bAlongY ? Bracket.Y : Bracket.X;
				const double Across = M.bAlongY ? Bracket.X : Bracket.Y;
				if (FMath::Abs(Across - M.Mid) < T + G.DressConduitDepth && Along + Half > M.A && Along - Half < M.B)
				{
					++BracketsOverMouth;
				}
			}
		}

		// Ceiling beams: across corridor and gate legs every DressBeamSpacing where both ends meet a wall (not by a gate's
		// frame), and across rooms in lines of beams from wall to wall; their ends in the panels' slab.
		const double BeamSpacing = G.DressBeamSpacing;
		for (int32 S = 0; S < Plan.Segs.Num() && BeamSpacing > 0.0; ++S)
		{
			const FSeg& Seg = Plan.Segs[S];
			const bool bStrip = Seg.Type == EAreaSegmentType::Corridor || Seg.Type == EAreaSegmentType::Gate;
			if ((!bStrip && Seg.Type != EAreaSegmentType::Room) || !IsDressedSegment(Plan, S))
			{
				continue;
			}
			for (const FLeg& Leg : Seg.Legs)
			{
				const FVector2D F = DirV(Leg.Dir);
				const FVector2D L = DirV(LeftOf(Leg.Dir));
				// The leg's extent across: the strip's width around its centreline, or the room's rectangle.
				double Lat0 = -0.5 * Seg.Width;
				double Lat1 = 0.5 * Seg.Width;
				if (!bStrip)
				{
					const FVector2D C0 = Leg.Rect.Min - Leg.Start;
					const FVector2D C1 = Leg.Rect.Max - Leg.Start;
					const double D0 = FVector2D::DotProduct(C0, L);
					const double D1 = FVector2D::DotProduct(C1, L);
					Lat0 = FMath::Min(D0, D1);
					Lat1 = FMath::Max(D0, D1);
				}
				for (double Along = BeamSpacing; Along <= Leg.Length - 100.0; Along += BeamSpacing)
				{
					const FVector2D P = Leg.Start + F * Along;
					bool bWalls = true;
					for (const double Lat : { Lat0, Lat1 })
					{
						const double Side = Lat > 0.0 ? 1.0 : -1.0;
						const FVector2D InsideP = P + L * (Lat - Side * Cell * 0.5);
						const FVector2D OutsideP = P + L * (Lat + Side * Cell * 0.5);
						const FIntPoint CI(FMath::FloorToInt(InsideP.X / Cell), FMath::FloorToInt(InsideP.Y / Cell));
						const FIntPoint CO(FMath::FloorToInt(OutsideP.X / Cell), FMath::FloorToInt(OutsideP.Y / Cell));
						const FCell* EI = Plan.Cells.Find(CI);
						bWalls &= EI && EI->Segment == S && !Plan.Cells.Contains(CO);
					}
					for (const FFrame& Fr : Frames)
					{
						bWalls &= FVector2D::Distance(P, Fr.Door) > 100.0;
					}
					if (!bWalls)
					{
						continue;
					}
					// Lines of beams from wall to wall, each about the beam's length; the line's length along the beam's Y.
					const double Span = (Lat1 - Lat0) + T;
					const int32 N = FMath::Max(1, FMath::RoundToInt(Span / BeamLength));
					for (int32 K = 0; K < N; ++K)
					{
						const double Lat = Lat0 - 0.5 * T + (K + 0.5) * Span / N;
						const FVector2D C = P + L * Lat;
						BeamF.Add(Span / N / BeamLength, C);
						Out.Beams.Add(FitPiece(C, G.WallHeight, Leg.Dir, 1.0, Span / N, 1.0, FVector(1.0, BeamLength, 1.0)));
					}
				}
			}
		}

		// Frames under catwalk decks and platforms, across each floor box's short side and its curbs.
		const double FrameSpacing = G.DressFrameSpacing;
		const double FrameSpan = FMath::Max(1.0, static_cast<double>(G.DressFrameSpan));
		FFactorRange CatwalkFrameF, HallFrameF;
		int32 HallFramesSplit = 0;
		// A web hall's frame across more than this many times SM_KitCatwalkFrame's span is made of several, end to end
		// (the widest a Catwalk segment uses is its 6 m platforms, x1.88).
		constexpr double FrameMaxFactor = 1.9;
		const double FrameTipDepth = FMath::Max(0.0, static_cast<double>(G.DressFrameTipDepth));
		auto FrameBox = [&](const FBox& Box, bool bHall, bool bPlatform)
		{
			const double SX = Box.Max.X - Box.Min.X;
			const double SY = Box.Max.Y - Box.Min.Y;
			const bool bAlongX = SX >= SY;  // frames step along the long side
			const double Long = bAlongX ? SX : SY;
			const int32 N = FMath::Max(1, FMath::RoundToInt(Long / FrameSpacing));
			for (int32 K = 0; K < N; ++K)
			{
				const double S = (bAlongX ? Box.Min.X : Box.Min.Y) + (K + 0.5) * Long / N;
				double E0 = bAlongX ? Box.Min.Y : Box.Min.X;
				double E1 = bAlongX ? Box.Max.Y : Box.Max.X;
				// Out to the curbs' outer faces where the edge is open (no floor beyond).
				auto Floor = [&](double Across)
				{
					const FVector2D Q = bAlongX ? FVector2D(S, Across) : FVector2D(Across, S);
					return Plan.Cells.Contains(FIntPoint(FMath::FloorToInt(Q.X / Cell), FMath::FloorToInt(Q.Y / Cell)));
				};
				E0 -= Floor(E0 - Cell * 0.5) ? 0.0 : T;
				E1 += Floor(E1 + Cell * 0.5) ? 0.0 : T;
				const int32 Pieces = bHall ? FMath::Max(1, FMath::CeilToInt((E1 - E0) / (FrameSpan * FrameMaxFactor) - 1e-6)) : 1;
				HallFramesSplit += Pieces > 1 ? 1 : 0;
				const double Piece = (E1 - E0) / Pieces;
				for (int32 P = 0; P < Pieces; ++P)
				{
					const double Mid = E0 + (P + 0.5) * Piece;
					const FVector2D C = bAlongX ? FVector2D(S, Mid) : FVector2D(Mid, S);
					(bHall ? HallFrameF : CatwalkFrameF).Add(Piece / FrameSpan, C);
					Out.Frames.Add(FitPiece(C, -FT, bAlongX ? 0 : 1, 1.0, Piece, 1.0, FVector(1.0, FrameSpan, 1.0)));
					// Brief 28: the frame's lowest point (its pivot is the deck's underside, its tip FrameTipDepth under it).
					Out.FrameTips.Add({ FVector(C.X, C.Y, -FT - FrameTipDepth), K, N, P, Pieces, bPlatform, bAlongX });
				}
			}
		};
		// A catwalk's floor box is a platform when the platform at the end of one of its spurs lies in it.
		auto HoldsSpurPlatform = [&Plan](const FBox& Box, int32 Segment)
		{
			for (const FSpot& Spot : Plan.SpurEnds)
			{
				if (Spot.Segment == Segment && Spot.Pos.X > Box.Min.X && Spot.Pos.X < Box.Max.X && Spot.Pos.Y > Box.Min.Y && Spot.Pos.Y < Box.Max.Y)
				{
					return true;
				}
			}
			return false;
		};
		for (int32 I = 0; I < Geo.FloorRects.Num(); ++I)
		{
			const FAreaFloorRect& Rect = Geo.FloorRects[I];
			if (Out.FloorHidden[I] && Plan.Segs.IsValidIndex(Rect.Segment) && Plan.Segs[Rect.Segment].Type == EAreaSegmentType::Catwalk)
			{
				const bool bPlatform = HoldsSpurPlatform(Rect.Box, Rect.Segment);
				Out.OpenParts.Add({ FBox2D(FVector2D(Rect.Box.Min), FVector2D(Rect.Box.Max)), Rect.Segment, bPlatform });
				if (FrameSpacing > 0.0)
				{
					FrameBox(Rect.Box, false, bPlatform);
				}
			}
		}
		// Web halls: under each of the hall's parts (its platforms and walkways), as under a catwalk. Floor halls have none.
		for (const FHallPart& Part : HallParts)
		{
			Out.OpenParts.Add({ FBox2D(FVector2D(Part.Box.Min), FVector2D(Part.Box.Max)), Part.Segment, Part.bPlatform });
			if (FrameSpacing > 0.0 && Plan.Segs[Part.Segment].bWeb)
			{
				FrameBox(Part.Box, true, Part.bPlatform);
			}
		}

		Out.Report = FString::Printf(TEXT("%s, wall height x%.3f; %s, %s; %s; %s; %s; %s; posts %d; %s"),
			*PanelAlong.ToString(TEXT("wall panels along")), (WallTop + FT) / PanelSize.Z, *PlateShort.ToString(TEXT("floor and ceiling plates, their 2 m side")),
			*PlateLong.ToString(TEXT("their 1 m side")), *BeamF.ToString(TEXT("beams")), *ConduitF.ToString(TEXT("conduits")), *CurbF.ToString(TEXT("curbs")),
			*CatwalkFrameF.ToString(TEXT("catwalk frames")), Out.Posts.Num(), *FrameF.ToString(TEXT("gate frames across")));
		for (int32 I = 0; I < Geo.CurbRuns.Num() && I < Out.CurbHidden.Num(); ++I)
		{
			ShownMixed += !Out.CurbHidden[I] && TouchesDressed(Geo.CurbRuns[I]) ? 1 : 0;
		}
		int32 HiddenBoxes = 0, ShownStructure = 0;
		for (const TArray<bool>* Flags : { &Out.FloorHidden, &Out.WallHidden, &Out.CurbHidden, &Out.PillarHidden, &Out.CeilingHidden })
		{
			for (const bool bHidden : *Flags) { (bHidden ? HiddenBoxes : ShownStructure) += 1; }
		}
		Out.Report += FString::Printf(TEXT("; %s (cm); boxes collision only %d, shown %d, shown boxes touching a modelled segment %d"),
			*Fillers.ToString(TEXT("filler blocks")), HiddenBoxes, ShownStructure, ShownMixed);
		Out.Report += FString::Printf(TEXT("; halls %d, tiled in %d parts (%d platforms): %s, %s; %s, %d frame lines in several pieces"),
			HallSegments, HallParts.Num(), HallPlatforms, *HallPlateLong.ToString(TEXT("hall plates, their 2 m side")), *HallPlateShort.ToString(TEXT("their 1 m side")),
			*HallFrameF.ToString(TEXT("hall frames")), HallFramesSplit);
		Out.Report += FString::Printf(TEXT("; conduit runs across %d niche and dead-end mouths, %s, brackets over a mouth %d, lines left unsolved %d"),
			MouthsCrossed, *ConduitCrossF.ToString(TEXT("conduits across a mouth")), BracketsOverMouth, LinesUnsolved);
		Out.Report += FString::Printf(TEXT("; plates by family: floors %d, ceilings %d"), Out.Plates.Num(), Out.CeilingPlates.Num());
		int32 OpenWalls = 0;
		for (int32 I = 0; I < Geo.Walls.Num(); ++I) { OpenWalls += IsOpenWall(I) ? 1 : 0; }
		Out.Report += FString::Printf(TEXT("; floor strips to the wall panels' slab %d (%s, %.0f cm wide); exit corridor open ends %d (collision only, not dressed)"),
			Out.FloorStrips.Num(), bStrips ? TEXT("on") : TEXT("off, tls.Area.FloorStrips 0"), StripWidth, OpenWalls);
	}

	/**
	 * Brief 26: the conduits and cables of the twin AI's room, from the terminal's placement (Pos, facing Facing, its body's
	 * box Body in its own space) and the room (Room: its rectangle; the walls' slab half a wall box behind its edges): two
	 * conduits from the top of the body into the ceiling, two along the far wall from the body's sides into the side walls,
	 * two along the floor from the base that turn down into the floor, bundles of cables from the top into the ceiling and
	 * along the far wall into the side walls at two heights. Every run tiled by pieces of PieceLen scaled to fit (at most
	 * x1.25 or x0.75). No random draw; look only.
	 */
	static void BuildTwinDressing(const AAreaGenerator& G, const FVector2D& Pos, const FVector2D& Facing, const FBox& Body, const FBox2D& Room,
		TArray<FTransform>& Conduits, TArray<FTransform>& Bends, TArray<FTransform>& Cables, FString& Report)
	{
		const double PieceLen = FMath::Max(10.0, static_cast<double>(G.DressTwinPieceLength));
		const double BendR = FMath::Max(10.0, static_cast<double>(G.DressTwinBendRadius));
		const double Top = G.WallHeight + G.FloorThickness * 0.5;  // into the ceiling's box
		const FVector Fwd(Facing.X, Facing.Y, 0.0);
		const FVector Left(-Facing.Y, Facing.X, 0.0);
		const FVector Origin(Pos.X, Pos.Y, 0.0);
		auto World = [&](double X, double Y, double Z) { return Origin + Fwd * X + Left * Y + FVector(0.0, 0.0, Z); };
		// How far the side walls' slab is from the body's centre line, along Left (the room is centred on it).
		const FVector2D Centre = Room.GetCenter();
		const double HalfRoom = 0.5 * FMath::Abs(FVector2D::DotProduct(Room.GetSize(), FVector2D(Left.X, Left.Y)));
		const double OffCentre = FVector2D::DotProduct(Pos - Centre, FVector2D(Left.X, Left.Y));
		const double SideSlab = HalfRoom + 0.5 * G.WallThickness;
		const double Back = Body.Min.X;
		int32 Runs = 0;
		auto Tile = [&](TArray<FTransform>& Out, const FVector& A, const FVector& B)
		{
			const FVector D = B - A;
			const double Len = D.Size();
			if (Len < 1.0)
			{
				return;
			}
			++Runs;
			const int32 N = FMath::Max(1, FMath::RoundToInt(Len / PieceLen));
			const FRotator Rot = D.GetSafeNormal().Rotation();
			for (int32 K = 0; K < N; ++K)
			{
				Out.Add(FTransform(Rot, A + D * (double(K) / N), FVector(Len / N / PieceLen, 1.0, 1.0)));
			}
		};
		const double PipeX = Back + 30.0;  // the conduits' and cables' axis, in front of the wall
		for (const double S : { 1.0, -1.0 })
		{
			const double SideY = S * Body.Max.Y;
			const double WallY = S * SideSlab - OffCentre;
			// From the top into the ceiling.
			Tile(Conduits, World(PipeX - 10.0, S * 0.3 * Body.Max.Y, Body.Max.Z), World(PipeX - 10.0, S * 0.3 * Body.Max.Y, Top));
			// From the sides into the side walls, under the ceiling.
			Tile(Conduits, World(PipeX, SideY, Body.Max.Z * 0.77), World(PipeX, WallY, Body.Max.Z * 0.77));
			// Along the floor from the base, turning down into the floor.
			const double FloorZ = 12.0;
			const double Turn = SideY + S * 1.5 * PieceLen;
			Tile(Conduits, World(PipeX, SideY, FloorZ), World(PipeX, Turn, FloorZ));
			{
				// SM_KitTwinConduitBend turns from its +X to its +Z: +X along the run (away from the body), +Z down.
				const FVector Along = Left * S;
				const FVector Down(0.0, 0.0, -1.0);
				const FVector Side = FVector::CrossProduct(Down, Along);
				Bends.Add(FTransform(FMatrix(Along, Side, Down, World(PipeX, Turn, FloorZ))));
			}
			// Cables from the top into the ceiling, and along the far wall into the side walls at two heights.
			Tile(Cables, World(PipeX - 5.0, S * 0.08 * Body.Max.Y, Body.Max.Z), World(PipeX - 5.0, S * 0.08 * Body.Max.Y, Top));
			for (const double Z : { 0.40, 0.50 })
			{
				Tile(Cables, World(PipeX - 8.0, SideY, Body.Max.Z * Z), World(PipeX - 8.0, WallY, Body.Max.Z * Z));
			}
		}
		Report = FString::Printf(TEXT("twin AI's room: %d runs, conduits %d, bends %d, cables %d"), Runs, Conduits.Num(), Bends.Num(), Cables.Num());
	}

	// --- The backdrop's dressing (Brief 28): the masses' modules, the levels of walkways above and below, the pillars ---
	// Look only (the graphs spawn them with no collision and no navigation), computed from the finished plan with no random
	// draw: the plan, the layout hash and everything placed stay as they are.

	/** What BuildDecor places, by the generator's array (EDecorMesh order), and what it records for tls.BackdropCheck. */
	struct FDecorOut
	{
		TArray<FTransform> Arrays[AAreaGenerator::DecorMeshCount];
		TArray<AAreaGenerator::FDecorFaceRecord> Faces;
		TArray<AAreaGenerator::FDecorLevelRecord> Levels;
		TArray<AAreaGenerator::FDecorPillarRecord> Pillars;
		TArray<AAreaGenerator::FDecorShotPick> MassPicks;
		AAreaGenerator::FDecorShotPick PillarPick;
		TArray<FBox> Forbidden;
		TArray<FBox2D> ExitClearance;
		FString Report;
	};

	/** The module of a family for the cell (Column, Row) of a face: a fixed pattern, no random draw. */
	static int32 DecorModuleFor(EAreaBackdropFamily Family, int32 Column, int32 Row)
	{
		using M = AAreaGenerator;
		switch (Family)
		{
		case EAreaBackdropFamily::Hull:
			// Deep ribs every other column, from bottom to top; plating between them, a deep stringer every third row.
			return Column % 2 == 0 ? M::DecorHullRib : (Row % 3 == 2 ? M::DecorHullFrame : M::DecorHullPlate);
		case EAreaBackdropFamily::Servers:
			// Stacks of tiers, a cable riser every fourth column, some tiers of closed bays.
			return Column % 4 == 3 ? M::DecorServerRiser : (Column % 2 == 1 && Row % 2 == 1 ? M::DecorServerBays : M::DecorServerStack);
		case EAreaBackdropFamily::Machinery:
			// Banded columns, pipes, ringed columns, column by column.
			return Column % 3 == 0 ? M::DecorMachColumn : (Column % 3 == 1 ? M::DecorMachPipes : M::DecorMachRing);
		default:
			return -1;
		}
	}

	static const TCHAR* LexDecorFamily(EAreaBackdropFamily Family)
	{
		switch (Family)
		{
		case EAreaBackdropFamily::Hull: return TEXT("Hull");
		case EAreaBackdropFamily::Servers: return TEXT("Servers");
		case EAreaBackdropFamily::Machinery: return TEXT("Machinery");
		case EAreaBackdropFamily::ServersAndMachinery: return TEXT("ServersAndMachinery");
		default: return TEXT("None");
		}
	}

	/** Columns (or rows) of modules across Span and their size: within the factor range, or the nearest count clamped to it (then centred, with a gap). */
	static void FitModules(double Span, double Module, double MinF, double MaxF, int32& OutCount, double& OutSize)
	{
		OutCount = FMath::Max(1, FMath::RoundToInt(Span / Module));
		double F = Span / (OutCount * Module);
		if (F > MaxF) { ++OutCount; }
		else if (F < MinF && OutCount > 1) { --OutCount; }
		F = FMath::Clamp(Span / (OutCount * Module), MinF, MaxF);
		OutSize = F * Module;
		if (OutCount * OutSize > Span + 0.5)
		{
			// Too short for even one module at its smallest: none.
			OutCount = FMath::Max(0, FMath::FloorToInt(Span / OutSize + 1e-6));
		}
	}

	static void BuildDecor(const AAreaGenerator& G, const FPlan& Plan, const FGeometryPlan& Geo, const FDressing& Dressing, const FAreaBackdropRecipe& R, FDecorOut& Out)
	{
		const double FT = G.FloorThickness;
		const double T = G.WallThickness;
		const double Side = FMath::Max(0.0, static_cast<double>(G.DecorSideClearance));
		const double Vert = FMath::Max(0.0, static_cast<double>(G.DecorVerticalClearance));
		const double TipDepth = FMath::Max(0.0, static_cast<double>(G.DressFrameTipDepth));
		const FVector PlateSize = G.DressFloorPlateSize.ComponentMax(FVector(1.0));
		const FVector CurbSize = G.DressCurbSize.ComponentMax(FVector(1.0));
		const FVector PostSize = G.DressCornerPostSize.ComponentMax(FVector(1.0));
		const double FrameSpan = FMath::Max(1.0, static_cast<double>(G.DressFrameSpan));
		constexpr double FrameMaxFactor = 1.9;

		// The walkable floors and the space kept clear round them: 2 m off their sides, 8 m above and below them.
		TArray<FBox> Walk;
		for (const FAreaFloorRect& Rect : Geo.FloorRects)
		{
			Walk.Add(Rect.Box);
			Out.Forbidden.Add(FBox(Rect.Box.Min - FVector(Side, Side, Vert), Rect.Box.Max + FVector(Side, Side, Vert)));
		}
		// The exit corridors' clearance (Brief 26): their line past the open end out to the backdrop's reach, grown by the
		// clearance the masses keep from it. Every height.
		const double Radius = FMath::Clamp(static_cast<double>(G.BackdropRadius), 0.0, 100000.0);
		for (const FSeg& S : Plan.Segs)
		{
			if (S.bExitCorridor && S.Legs.Num() > 0)
			{
				const FLeg& Leg = S.Legs[0];
				Out.ExitClearance.Add(Grow(MakeRect(Leg.Start, Leg.Dir, Leg.Length, Leg.Length + Radius + G.BackdropClearance, -0.5 * S.Width, 0.5 * S.Width), G.BackdropClearance));
			}
		}
		auto InExitClearance = [&Out](const FBox2D& B)
		{
			for (const FBox2D& C : Out.ExitClearance)
			{
				if (B.Intersect(C)) { return true; }
			}
			return false;
		};
		auto Overlaps = [](const FBox& A, const FBox& B)
		{
			return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y && A.Min.Z < B.Max.Z && A.Max.Z > B.Min.Z;
		};
		auto InForbidden = [&](const FBox& B)
		{
			if (InExitClearance(FBox2D(FVector2D(B.Min), FVector2D(B.Max))))
			{
				return true;
			}
			for (const FBox& F : Out.Forbidden)
			{
				if (Overlaps(B, F)) { return true; }
			}
			return false;
		};
		const TArray<FBox>& Masses = Geo.Backdrop;
		int32 Totals[AAreaGenerator::DecorMeshCount] = {};
		FString MassReport = TEXT("masses off");
		FString LayerReport = TEXT("layers off");
		FString PillarReport = TEXT("pillars off");

		// --- Part A: the masses' faces seen from the route, dressed with the area's modules ---
		if (R.bMasses && R.Family != EAreaBackdropFamily::None)
		{
			const double Module = FMath::Max(50.0, static_cast<double>(G.DecorModuleSize));
			const double Depth = FMath::Max(0.0, static_cast<double>(G.DecorModuleDepth));
			const double MinF = FMath::Clamp(static_cast<double>(G.DecorModuleMinFactor), 0.1, 1.0);
			const double MaxF = FMath::Clamp(static_cast<double>(G.DecorModuleMaxFactor), 1.0, 4.0);
			const double Reach = FMath::Max(0.0, R.MassReachM * 100.0);
			int32 Toward = 0, Dressed = 0, BeyondReach = 0, CellsSkippedMass = 0, CellsSkippedClear = 0;
			FFactorRange AlongF, UpF;
			// The walkable volumes the faces are seen from: the floors up to the eye.
			TArray<FBox> Seen;
			for (const FBox& W : Walk) { Seen.Add(FBox(W.Min, FVector(W.Max.X, W.Max.Y, W.Max.Z + 200.0))); }
			double BestPick[3] = { TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max() };
			for (int32 M = 0; M < Masses.Num(); ++M)
			{
				const FBox& B = Masses[M];
				const EAreaBackdropFamily Family = R.Family == EAreaBackdropFamily::ServersAndMachinery
					? (M % 2 == 0 ? EAreaBackdropFamily::Servers : EAreaBackdropFamily::Machinery) : R.Family;
				for (int32 Face = 0; Face < 6; ++Face)
				{
					// Faces 0..5: +X, -X, +Y, -Y, +Z, -Z.
					const int32 Axis = Face / 2;
					const double Sign = Face % 2 == 0 ? 1.0 : -1.0;
					FVector N = FVector::ZeroVector;
					N[Axis] = Sign;
					const FVector Up = Axis == 2 ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 0.0, 1.0);
					const FRotator Rot = FRotationMatrix::MakeFromXZ(N, Up).Rotator();
					const FVector AxY = FRotationMatrix(Rot).GetScaledAxis(EAxis::Y);
					const FVector AxZ = FRotationMatrix(Rot).GetScaledAxis(EAxis::Z);
					const FVector Size = B.GetSize();
					const double W = FMath::Abs(FVector::DotProduct(Size, AxY));
					const double H = FMath::Abs(FVector::DotProduct(Size, AxZ));
					FVector Centre = B.GetCenter();
					Centre[Axis] = Sign > 0.0 ? B.Max[Axis] : B.Min[Axis];
					AAreaGenerator::FDecorFaceRecord Rec;
					Rec.Mass = M;
					Rec.Face = Face;
					Rec.Normal = N;
					Rec.Centre = Centre;
					Rec.Size = FVector2D(W, H);
					Rec.Family = Family;
					// Turned to the route: some walkable volume lies on the face's outer side.
					for (const FBox& V : Seen)
					{
						if (Sign > 0.0 ? V.Max[Axis] > B.Max[Axis] : V.Min[Axis] < B.Min[Axis]) { Rec.bTowardRoute = true; break; }
					}
					if (!Rec.bTowardRoute)
					{
						Out.Faces.Add(Rec);
						continue;
					}
					++Toward;
					// The walkable volumes within reach of the face's rectangle (a box round the face, grown by the reach).
					const FBox FaceBox(Centre - (AxY * W + AxZ * H).GetAbs() * 0.5, Centre + (AxY * W + AxZ * H).GetAbs() * 0.5);
					TArray<const FBox*> Near;
					for (const FBox& V : Seen)
					{
						if (FMath::Sqrt(FaceBox.ComputeSquaredDistanceToBox(V)) <= Reach) { Near.Add(&V); }
					}
					if (Near.Num() == 0)
					{
						++BeyondReach;
						Out.Faces.Add(Rec);
						continue;
					}
					int32 NCols = 0, NRows = 0;
					double CW = 0.0, CH = 0.0;
					FitModules(W, Module, MinF, MaxF, NCols, CW);
					FitModules(H, Module, MinF, MaxF, NRows, CH);
					const double Y0 = -0.5 * NCols * CW;
					const double Z0 = -0.5 * NRows * CH;
					for (int32 Row = 0; Row < NRows; ++Row)
					{
						for (int32 Col = 0; Col < NCols; ++Col)
						{
							const FVector Base = Centre + AxY * (Y0 + (Col + 0.5) * CW) + AxZ * (Z0 + Row * CH);
							const FVector Mid = Base + AxZ * (0.5 * CH);
							double Best = TNumericLimits<double>::Max();
							for (const FBox* V : Near) { Best = FMath::Min(Best, V->ComputeSquaredDistanceToPoint(Mid)); }
							if (FMath::Sqrt(Best) > Reach)
							{
								continue;
							}
							++Rec.CellsInReach;
							// The module's box: the cell on the face, out to the modules' depth.
							const FVector C0 = Base - AxY * (0.5 * CW);
							const FVector C1 = Base + AxY * (0.5 * CW) + AxZ * CH + N * Depth;
							const FBox Cell3(C0.ComponentMin(C1), C0.ComponentMax(C1));
							bool bOtherMass = false;
							for (int32 O = 0; O < Masses.Num() && !bOtherMass; ++O)
							{
								bOtherMass = O != M && Overlaps(Cell3, Masses[O]);
							}
							if (bOtherMass)
							{
								++Rec.SkippedMass;
								++CellsSkippedMass;
								continue;
							}
							if (InForbidden(Cell3))
							{
								++Rec.SkippedClear;
								++CellsSkippedClear;
								continue;
							}
							const int32 Mesh = DecorModuleFor(Family, Col, Row);
							if (Mesh < 0)
							{
								continue;
							}
							Out.Arrays[Mesh].Add(FTransform(Rot, Base, FVector(1.0, CW / Module, CH / Module)));
							++Totals[Mesh];
							++Rec.Placed;
							AlongF.Add(CW / Module, FVector2D(Base));
							UpF.Add(CH / Module, FVector2D(Base));
							// The shot of each family: a module on a side face, nearest the route, about the eye's height.
							const int32 Fam = Mesh / 3;
							const double Score = FMath::Sqrt(Best) + FMath::Abs(Mid.Z - 300.0) * 2.0;
							if (Axis != 2 && Score < BestPick[Fam])
							{
								BestPick[Fam] = Score;
								AAreaGenerator::FDecorShotPick Pick;
								Pick.Family = LexDecorFamily(Fam == 0 ? EAreaBackdropFamily::Hull : Fam == 1 ? EAreaBackdropFamily::Servers : EAreaBackdropFamily::Machinery);
								Pick.At = Mid;
								Pick.Normal = N;
								bool bFound = false;
								for (AAreaGenerator::FDecorShotPick& P : Out.MassPicks)
								{
									if (P.Family == Pick.Family) { P = Pick; bFound = true; }
								}
								if (!bFound) { Out.MassPicks.Add(Pick); }
							}
						}
					}
					Dressed += Rec.Placed > 0 ? 1 : 0;
					Out.Faces.Add(Rec);
				}
			}
			int32 Modules = 0;
			for (int32 I = 0; I <= AAreaGenerator::DecorMachRing; ++I) { Modules += Totals[I]; }
			MassReport = FString::Printf(TEXT("masses %d, family %s: faces turned to the route %d, dressed %d, beyond reach (%.0f m) %d; modules %d (hull rib %d plate %d frame %d, servers stack %d bays %d riser %d, machinery column %d pipes %d ring %d); cells left: against another mass %d, in the route's clearance %d; %s, %s"),
				Masses.Num(), LexDecorFamily(R.Family), Toward, Dressed, R.MassReachM, BeyondReach, Modules,
				Totals[0], Totals[1], Totals[2], Totals[3], Totals[4], Totals[5], Totals[6], Totals[7], Totals[8], CellsSkippedMass, CellsSkippedClear,
				*AlongF.ToString(TEXT("factor along")), *UpF.ToString(TEXT("factor up")));
		}

		// --- Part B: levels of walkways above and below every open segment ---
		struct FDecorTip { FVector Tip; int32 Line = 0; int32 Lines = 1; int32 Piece = 0; int32 Pieces = 1; bool bPlatform = false; bool bAlongX = true; };
		TArray<FDecorTip> LayerTips;
		TArray<FBox> LayerDecks;
		if (R.bLayers && (R.LayerCountAbove > 0 || R.LayerCountBelow > 0))
		{
			const double Spacing = FMath::Max(100.0, R.LayerSpacingM * 100.0);
			const double Scale = FMath::Clamp(static_cast<double>(R.LayerScale), 1.0, 4.0);
			const double PieceLen = FMath::Max(100.0, R.LayerPieceLengthM * 100.0);
			const int32 Above = FMath::Clamp(R.LayerCountAbove, 0, 4);
			const int32 Below = FMath::Clamp(R.LayerCountBelow, 0, 4);
			int32 Cut = 0;
			TArray<int32> Segments;
			for (const FDressing::FOpenPart& P : Dressing.OpenParts) { Segments.AddUnique(P.Segment); }
			Segments.Sort();
			for (const int32 S : Segments)
			{
				const FVector2D C = Plan.Segs[S].Bounds.GetCenter();
				double WalkedReach = 0.0;
				for (const FDressing::FOpenPart& P : Dressing.OpenParts)
				{
					if (P.Segment != S) { continue; }
					for (const FVector2D& Q : { P.Rect.Min, P.Rect.Max, FVector2D(P.Rect.Min.X, P.Rect.Max.Y), FVector2D(P.Rect.Max.X, P.Rect.Min.Y) })
					{
						WalkedReach = FMath::Max(WalkedReach, FVector2D::Distance(Q, C));
					}
				}
				for (int32 L = 0; L < Above + Below; ++L)
				{
					const bool bAbove = L < Above;
					const int32 K = bAbove ? L + 1 : L - Above + 1;
					const double Z = (bAbove ? 1.0 : -1.0) * K * Spacing;
					// A quarter turn more for each level: the levels above first, then those below.
					const int32 Turns = (L + 1) % 4;
					AAreaGenerator::FDecorLevelRecord Rec;
					Rec.Segment = S;
					Rec.Level = bAbove ? K : -K;
					Rec.Z = static_cast<float>(Z);
					Rec.Turns = Turns;
					Rec.WalkedReach = WalkedReach;
					Rec.Bounds = FBox2D(ForceInit);
					// The level's parts: the walked ones turned about the segment's centre and enlarged, cut into pieces.
					struct FPiece { FBox2D Box; bool bAlongX; int32 J; int32 NA; int32 K; int32 NC; bool bPlatform; };
					TArray<FPiece> Kept;
					for (const FDressing::FOpenPart& P : Dressing.OpenParts)
					{
						if (P.Segment != S) { continue; }
						FBox2D Box(ForceInit);
						for (FVector2D Q : { P.Rect.Min, P.Rect.Max })
						{
							FVector2D D = Q - C;
							for (int32 Tn = 0; Tn < Turns; ++Tn) { D = FVector2D(-D.Y, D.X); }
							Box += C + D * Scale;
						}
						const double SX = Box.Max.X - Box.Min.X;
						const double SY = Box.Max.Y - Box.Min.Y;
						const bool bAlongX = SX >= SY;
						const double Long = bAlongX ? SX : SY;
						const double Wide = bAlongX ? SY : SX;
						const int32 NA = FMath::Max(1, FMath::RoundToInt(Long / PieceLen));
						const int32 NC = FMath::Max(1, FMath::RoundToInt(Wide / PieceLen));
						for (int32 J = 0; J < NA; ++J)
						{
							for (int32 Kc = 0; Kc < NC; ++Kc)
							{
								const double A0 = J * Long / NA, A1 = (J + 1) * Long / NA;
								const double W0 = Kc * Wide / NC, W1 = (Kc + 1) * Wide / NC;
								const FBox2D Piece = bAlongX ? FBox2D(FVector2D(Box.Min.X + A0, Box.Min.Y + W0), FVector2D(Box.Min.X + A1, Box.Min.Y + W1))
									: FBox2D(FVector2D(Box.Min.X + W0, Box.Min.Y + A0), FVector2D(Box.Min.X + W1, Box.Min.Y + A1));
								// The piece with its curbs and the frames under it.
								const FBox Space(FVector(Piece.Min.X - T, Piece.Min.Y - T, Z - FT - TipDepth), FVector(Piece.Max.X + T, Piece.Max.Y + T, Z + G.CurbHeight));
								bool bBlocked = InForbidden(Space);
								for (int32 M = 0; M < Masses.Num() && !bBlocked; ++M)
								{
									bBlocked = Overlaps(Space, Masses[M].ExpandBy(FVector(G.DecorModuleDepth)));
								}
								if (bBlocked)
								{
									++Rec.PiecesCut;
									++Cut;
									continue;
								}
								Kept.Add({ Piece, bAlongX, J, NA, Kc, NC, P.bPlatform });
							}
						}
					}
					TMap<FIntPoint, TArray<FVector2D>> CurbLines;  // (side, line in cm) -> free spans along it
					for (const FPiece& P : Kept)
					{
						const FVector2D PC = P.Box.GetCenter();
						const double SX = P.Box.Max.X - P.Box.Min.X;
						const double SY = P.Box.Max.Y - P.Box.Min.Y;
						Rec.Bounds += P.Box;
						for (const FVector2D& Q : { P.Box.Min, P.Box.Max, FVector2D(P.Box.Min.X, P.Box.Max.Y), FVector2D(P.Box.Max.X, P.Box.Min.Y) })
						{
							Rec.Reach = FMath::Max(Rec.Reach, FVector2D::Distance(Q, C));
						}
						// The deck: one plate over the piece (its long side along the piece's).
						const double PL = P.bAlongX ? SX : SY;
						const double PW = P.bAlongX ? SY : SX;
						Out.Arrays[AAreaGenerator::DecorLayerPlate].Add(FTransform(FRotator(0.0, P.bAlongX ? 0.0 : 90.0, 0.0), FVector(PC.X, PC.Y, Z - FT),
							FVector(PL / PlateSize.X, PW / PlateSize.Y, FT / PlateSize.Z)));
						LayerDecks.Add(FBox(FVector(P.Box.Min, Z - FT), FVector(P.Box.Max, Z)));
						++Rec.Plates;
						// The frames: under every other piece along its part (and under the last), one line across the piece at its
						// middle, out to the curbs, in pieces of at most x1.9 (the budget: Brief 28's reduction).
						if (P.J % 2 == 0 || P.J == P.NA - 1)
						{
							const double Across0 = (P.bAlongX ? P.Box.Min.Y : P.Box.Min.X) - T;
							const double Across1 = (P.bAlongX ? P.Box.Max.Y : P.Box.Max.X) + T;
							const int32 FP = FMath::Max(1, FMath::CeilToInt((Across1 - Across0) / (FrameSpan * FrameMaxFactor) - 1e-6));
							const double FL = (Across1 - Across0) / FP;
							const int32 Line = (P.J + 1) / 2;
							const int32 Lines = (P.NA + 1) / 2 + (P.NA % 2 == 0 ? 1 : 0);
							for (int32 Q = 0; Q < FP; ++Q)
							{
								const double Mid = Across0 + (Q + 0.5) * FL;
								const FVector2D FC = P.bAlongX ? FVector2D(PC.X, Mid) : FVector2D(Mid, PC.Y);
								Out.Arrays[AAreaGenerator::DecorLayerFrame].Add(FitPiece(FC, Z - FT, P.bAlongX ? 0 : 1, 1.0, FL, 1.0, FVector(1.0, FrameSpan, 1.0)));
								LayerTips.Add({ FVector(FC.X, FC.Y, Z - FT - TipDepth), Line, Lines, P.K * FP + Q, P.NC * FP, P.bPlatform, P.bAlongX });
								++Rec.Frames;
							}
						}
						// The curbs: along each edge, where no other piece of the level meets it; gathered by line and merged
						// below, so that a curb runs on across the pieces' joints.
						for (int32 D = 0; D < 4; ++D)
						{
							const bool bEdgeAlongY = D == 0 || D == 2;  // the edge runs along Y on the +X and -X sides
							const double Line = D == 0 ? P.Box.Max.X : D == 1 ? P.Box.Max.Y : D == 2 ? P.Box.Min.X : P.Box.Min.Y;
							const double E0 = bEdgeAlongY ? P.Box.Min.Y : P.Box.Min.X;
							const double E1 = bEdgeAlongY ? P.Box.Max.Y : P.Box.Max.X;
							TArray<FVector2D> Free = { FVector2D(E0, E1) };
							for (const FPiece& O : Kept)
							{
								const double OLine = D == 0 ? O.Box.Min.X : D == 1 ? O.Box.Min.Y : D == 2 ? O.Box.Max.X : O.Box.Max.Y;
								if (&O == &P || FMath::Abs(OLine - Line) > 1.0)
								{
									continue;
								}
								const double O0 = bEdgeAlongY ? O.Box.Min.Y : O.Box.Min.X;
								const double O1 = bEdgeAlongY ? O.Box.Max.Y : O.Box.Max.X;
								TArray<FVector2D> Next;
								for (const FVector2D& F : Free)
								{
									if (O1 <= F.X || O0 >= F.Y) { Next.Add(F); continue; }
									if (O0 > F.X) { Next.Add(FVector2D(F.X, O0)); }
									if (O1 < F.Y) { Next.Add(FVector2D(O1, F.Y)); }
								}
								Free = MoveTemp(Next);
							}
							TArray<FVector2D>& Runs = CurbLines.FindOrAdd(FIntPoint(D, FMath::RoundToInt(Line)));
							for (const FVector2D& F : Free)
							{
								if (F.Y - F.X >= 1.0) { Runs.Add(F); }
							}
						}
					}
					// The curbs: each line's free spans merged where they meet, then pieces of about twice the deck's (Brief
					// 28's reduction: a curb is the costliest piece of the kit).
					TArray<FIntPoint> CurbKeys;
					CurbLines.GetKeys(CurbKeys);
					CurbKeys.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
					for (const FIntPoint& Key : CurbKeys)
					{
						TArray<FVector2D> Runs = CurbLines[Key];
						Runs.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
						TArray<FVector2D> Merged;
						for (const FVector2D& F : Runs)
						{
							if (Merged.Num() > 0 && F.X <= Merged.Last().Y + 1.0) { Merged.Last().Y = FMath::Max(Merged.Last().Y, F.Y); }
							else { Merged.Add(F); }
						}
						const int32 D = Key.X;
						const bool bEdgeAlongY = D == 0 || D == 2;
						const double Line = Key.Y;
						const FVector2D Outward = DirV(D);
						for (const FVector2D& F : Merged)
						{
							const double Len = F.Y - F.X;
							if (Len < 10.0)
							{
								continue;
							}
							const int32 NCurb = FMath::Max(1, FMath::RoundToInt(Len / (2.0 * PieceLen)));
							for (int32 Q = 0; Q < NCurb; ++Q)
							{
								const double S0 = F.X + (Q + 0.5) * Len / NCurb;
								const FVector2D OnLine = bEdgeAlongY ? FVector2D(Line, S0) : FVector2D(S0, Line);
								Out.Arrays[AAreaGenerator::DecorLayerCurb].Add(FitPiece(OnLine + Outward * (0.5 * T), Z + G.CurbHeight - CurbSize.Z, Opposite(D), T, Len / NCurb, CurbSize.Z, CurbSize));
								++Rec.Curbs;
							}
						}
					}
					Totals[AAreaGenerator::DecorLayerPlate] += Rec.Plates;
					Totals[AAreaGenerator::DecorLayerCurb] += Rec.Curbs;
					Totals[AAreaGenerator::DecorLayerFrame] += Rec.Frames;
					Out.Levels.Add(Rec);
				}
			}
			LayerReport = FString::Printf(TEXT("layers: %d open segments, %d above and %d below, %.1f m apart, x%.2f, pieces of %.1f m: %d levels, plates %d, curbs %d, frames %d, pieces cut %d"),
				Segments.Num(), Above, Below, Spacing / 100.0, Scale, PieceLen / 100.0, Out.Levels.Num(), Totals[AAreaGenerator::DecorLayerPlate],
				Totals[AAreaGenerator::DecorLayerCurb], Totals[AAreaGenerator::DecorLayerFrame], Cut);
		}

		// --- Part C: pillars from frames' lowest points down to the first geometry below ---
		if (R.bPillars)
		{
			const int32 Nth = FMath::Clamp(R.PillarEveryNthFrame, 1, 20);
			const double MaxLen = FMath::Max(100.0, R.PillarMaxLengthM * 100.0);
			const double Half = 0.5 * FMath::Max(PostSize.X, PostSize.Y);
			int32 ByFoot[3] = {}, Dropped = 0, WalkedCount = 0;
			auto Place = [&](const FVector& Tip, int32 Line, int32 Lines, int32 Piece, int32 Pieces, bool bPlatform, bool bAlongX, bool bWalked)
			{
				const bool bCorner = bPlatform && (Line == 0 || Line == Lines - 1) && (Piece == 0 || Piece == Pieces - 1);
				if (Line % Nth != 0 && !bCorner)
				{
					return;
				}
				const FBox2D Foot2(FVector2D(Tip) - FVector2D(Half, Half), FVector2D(Tip) + FVector2D(Half, Half));
				if (InExitClearance(Foot2))
				{
					++Dropped;
					return;
				}
				// The first geometry under the tip: a mass, a level's deck; for a level's pillar, also the route's clearance (cut there).
				double FootZ = -TNumericLimits<double>::Max();
				uint8 Kind = AAreaGenerator::PillarFootMaxLength;
				auto Under = [&](const FBox& B, uint8 K)
				{
					if (B.Min.X < Foot2.Max.X && B.Max.X > Foot2.Min.X && B.Min.Y < Foot2.Max.Y && B.Max.Y > Foot2.Min.Y && B.Max.Z < Tip.Z - 1.0 && B.Max.Z > FootZ)
					{
						FootZ = B.Max.Z;
						Kind = K;
					}
				};
				for (const FBox& B : Masses) { Under(B, AAreaGenerator::PillarFootGeometry); }
				for (const FBox& B : LayerDecks) { Under(B, AAreaGenerator::PillarFootGeometry); }
				for (const FBox& B : Walk) { Under(B, AAreaGenerator::PillarFootGeometry); }
				if (!bWalked)
				{
					for (const FBox& B : Out.Forbidden) { Under(B, AAreaGenerator::PillarFootCut); }
				}
				double Len = Tip.Z - FootZ;
				if (Len > MaxLen)
				{
					Len = MaxLen;
					Kind = AAreaGenerator::PillarFootMaxLength;
				}
				if (Kind == AAreaGenerator::PillarFootCut && Len < 200.0)
				{
					++Dropped;
					return;
				}
				Out.Arrays[AAreaGenerator::DecorPillar].Add(FTransform(FRotator(0.0, bAlongX ? 0.0 : 90.0, 0.0), FVector(Tip.X, Tip.Y, Tip.Z - Len),
					FVector(1.0, 1.0, Len / PostSize.Z)));
				Out.Pillars.Add({ Tip, static_cast<float>(Len), Kind, bWalked });
				++ByFoot[Kind];
				WalkedCount += bWalked ? 1 : 0;
				if (bWalked && Len >= 1000.0 && Out.PillarPick.Family.IsEmpty())
				{
					Out.PillarPick.Family = TEXT("Pillar");
					Out.PillarPick.At = Tip;
					Out.PillarPick.Normal = bAlongX ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 1.0, 0.0);
				}
			};
			for (const FDressing::FFrameTip& F : Dressing.FrameTips) { Place(F.Tip, F.Line, F.Lines, F.Piece, F.Pieces, F.bPlatform, F.bAlongX, true); }
			for (const FDecorTip& F : LayerTips) { Place(F.Tip, F.Line, F.Lines, F.Piece, F.Pieces, F.bPlatform, F.bAlongX, false); }
			Totals[AAreaGenerator::DecorPillar] = Out.Arrays[AAreaGenerator::DecorPillar].Num();
			PillarReport = FString::Printf(TEXT("pillars every %d frames and under platforms' corners: %d (%d under the walked level, %d under the levels): on geometry %d, at the length %.0f m %d, cut at the route's clearance %d; dropped %d (exit clearance, or cut under 2 m)"),
				Nth, Out.Pillars.Num(), WalkedCount, Out.Pillars.Num() - WalkedCount, ByFoot[AAreaGenerator::PillarFootGeometry], MaxLen / 100.0,
				ByFoot[AAreaGenerator::PillarFootMaxLength], ByFoot[AAreaGenerator::PillarFootCut], Dropped);
		}
		Out.Report = MassReport + TEXT("; ") + LayerReport + TEXT("; ") + PillarReport;
	}
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

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarAreaDressing(
	TEXT("tls.Area.Dressing"), 1,
	TEXT("1: the modelled segments are dressed with the kit (their boxes collision only). 0: every box shown, no dressing (diagnostics: the look before Brief 16). Read at each generation."),
	ECVF_Default);
#endif

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

bool AAreaGenerator::IsDressedSegmentType(EAreaSegmentType Type)
{
	// Brief 16: corridors, rooms, gates and catwalks; Brief 19: halls too. Every walkable segment is modelled.
	return Type == EAreaSegmentType::Corridor || Type == EAreaSegmentType::Room || Type == EAreaSegmentType::Gate
		|| Type == EAreaSegmentType::Catwalk || Type == EAreaSegmentType::Hall;
}

int32 AAreaGenerator::GetDressingInstanceCount() const
{
	static const FName Tag(TEXT("AreaDressing"));
	TArray<UInstancedStaticMeshComponent*> All;
	GetComponents(All);
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Component : All)
	{
		if (Component && Component->ComponentHasTag(Tag))
		{
			Count += Component->GetInstanceCount();
		}
	}
	return Count;
}

const TCHAR* AAreaGenerator::GetDecorArrayName(int32 Mesh)
{
	static const TCHAR* Names[DecorMeshCount] = { TEXT("DecorMassHullRib"), TEXT("DecorMassHullPlate"), TEXT("DecorMassHullFrame"),
		TEXT("DecorMassServerStack"), TEXT("DecorMassServerBays"), TEXT("DecorMassServerRiser"), TEXT("DecorMassMachColumn"),
		TEXT("DecorMassMachPipes"), TEXT("DecorMassMachRing"), TEXT("DecorLayerPlates"), TEXT("DecorLayerCurbs"), TEXT("DecorLayerFrames"), TEXT("DecorPillars") };
	return Mesh >= 0 && Mesh < DecorMeshCount ? Names[Mesh] : TEXT("");
}

TArray<FTransform>* AAreaGenerator::MutableDecorArray(int32 Mesh)
{
	TArray<FTransform>* Arrays[DecorMeshCount] = { &DecorMassHullRib, &DecorMassHullPlate, &DecorMassHullFrame, &DecorMassServerStack, &DecorMassServerBays,
		&DecorMassServerRiser, &DecorMassMachColumn, &DecorMassMachPipes, &DecorMassMachRing, &DecorLayerPlates, &DecorLayerCurbs, &DecorLayerFrames, &DecorPillars };
	return Arrays[FMath::Clamp(Mesh, 0, DecorMeshCount - 1)];
}

const TArray<FTransform>& AAreaGenerator::GetDecorArray(int32 Mesh) const
{
	return *const_cast<AAreaGenerator*>(this)->MutableDecorArray(Mesh);
}

int32 AAreaGenerator::GetDecorPart(int32 Mesh)
{
	return Mesh <= DecorMachRing ? 0 : (Mesh == DecorPillar ? 2 : 1);
}

TArray<UInstancedStaticMeshComponent*> AAreaGenerator::GetDecorComponents(int32 Mesh) const
{
	// Every instanced component of the backdrop's dressing carries "AreaDecor" and "Area<its array>".
	static const FName All(TEXT("AreaDecor"));
	const FName Tag = Mesh < 0 ? All : FName(*FString::Printf(TEXT("Area%s"), GetDecorArrayName(Mesh)));
	TArray<UInstancedStaticMeshComponent*> Components;
	GetComponents(Components);
	TArray<UInstancedStaticMeshComponent*> Out;
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(Tag))
		{
			Out.Add(Component);
		}
	}
	return Out;
}

int32 AAreaGenerator::GetDecorInstanceCount() const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Component : GetDecorComponents(-1))
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
		else if (const ABeaconBody* Body = Cast<ABeaconBody>(Actor))
		{
			const FVector Aim = Body->GetAimPoint();
			Line += FString::Printf(TEXT("|rod=%d|aim=%d,%d,%d"), FMath::RoundToInt(Body->GetRodLength()), FMath::RoundToInt(Aim.X), FMath::RoundToInt(Aim.Y), FMath::RoundToInt(Aim.Z));
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
	for (TArray<FTransform>* Plan : { &PlanFloors, &PlanWalls, &PlanCurbs, &PlanPillars, &PlanCeilings, &PlanBackdrop,
		&PlanFloorsCollision, &PlanWallsCollision, &PlanCurbsCollision, &PlanPillarsCollision, &PlanCeilingsCollision,
		&DressWallPanels, &DressFloorPlates, &DressCeilingPlates, &DressCornerPosts, &DressCeilingBeams, &DressConduits, &DressCurbs, &DressCatwalkFrames, &DressGateFrames,
		&DressTwinConduits, &DressTwinBends, &DressTwinCables })
	{
		Plan->Reset();
	}
	for (int32 Mesh = 0; Mesh < DecorMeshCount; ++Mesh)
	{
		MutableDecorArray(Mesh)->Reset();
	}
	DecorFaces.Reset();
	DecorLevels.Reset();
	DecorPillarRecords.Reset();
	DecorMassPicks.Reset();
	DecorPillarPick = FDecorShotPick();
	DecorForbidden.Reset();
	DecorExitClearance.Reset();
	LastBackdropReport.Reset();
	DressFloorStripsFrom = 0;
	OpenEndBoxes.Reset();
	NavigationBounds = FBox(ForceInit);
	RouteLength = 0.f;
	RouteLengthBeforeWayOut = 0.f;
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
			ReportLoadStep(EAreaLoadStep::RoutePlanned);
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
			ReportLoadStep(EAreaLoadStep::GeometryBuilt);
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
				ReportLoadStep(EAreaLoadStep::NavigationReady);
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
	UE_LOG(LogArea, Display, TEXT("%s: AREA READY: act %d, map %s, seed %d (%d tried), hash %016llx, route %.1f m, %d PCG instances (plan %s), plan %.3f s, PCG %.3f s, navmesh %.3f s, validation %.3f s, time to ready %.2f s; dressing %d instances."),
		*GetName(), Act, *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Seed, LastSeedsTried, static_cast<uint64>(LayoutHash), RouteLength / 100.f,
		GetPCGInstanceCount(), bLastPCGMatchedPlan ? TEXT("matched") : TEXT("NOT MATCHED"), LastGenerationSeconds, LastPCGSeconds, LastNavigationSeconds,
		LastValidationSeconds, TimeToReadySeconds, GetDressingInstanceCount());
	UE_LOG(LogArea, Display, TEXT("%s: DRESSING act %d seed %d: %s."), *GetName(), Act, Seed, *LastDressingReport);
	UE_LOG(LogArea, Display, TEXT("%s: BACKDROP act %d seed %d: %s; instances built %d."), *GetName(), Act, Seed, *LastBackdropReport, GetDecorInstanceCount());
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

void AAreaGenerator::ReportLoadStep(EAreaLoadStep Step)
{
	// A revalidation keeps the player in the area: no travel screen to fill.
	if (bRevalidateOnly)
	{
		return;
	}
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->ReportAreaLoadStep(Step);
	}
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
		// The way out (Brief 26), on top of the chosen layout: the length above is the recipe's route, as before.
		else if (!Builder->PlaceWayOut(IsAreaWayOutOn()))
		{
			Why = Builder->WayOutError;
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

	// Terminals against a wall, on top of the finished plan (no random draw, nothing else moves). Their sizes come from
	// their classes: the log terminal's footprint and screen, the twin AI terminal's standard body.
	{
		const ALogTerminal* LogDefaults = LogTerminalClass ? LogTerminalClass->GetDefaultObject<ALogTerminal>() : nullptr;
		const ATwinAITerminal* TwinDefaults = TwinAITerminalClass ? TwinAITerminalClass->GetDefaultObject<ATwinAITerminal>() : nullptr;
		const FBox StandardBody = TerminalDefaults->GetLocalBodyBox();
		const float StandardHalfDepth = StandardBody.IsValid ? static_cast<float>(StandardBody.Max.X) : 20.f;
		const float LineClear = FMath::Max(50.f, TerminalSideOffset - StandardHalfDepth);
		const float Stand = FMath::Clamp(TerminalStandDistance, 50.f, 140.f);
		FBuilder::FWallSpec Log;
		if (LogDefaults)
		{
			Log.BackOffset = 0.5f;
			Log.HalfWidth = static_cast<float>(LogDefaults->Footprint.Y) * 0.5f;
			Log.Depth = static_cast<float>(LogDefaults->Footprint.X);
			Log.StandDistance = Log.BackOffset + LogDefaults->GetScreenOffset() + FMath::Clamp(LogReadingDistance, 100.f, 150.f);
			Log.Reach = LogDefaults->GetInteractRadius();
		}
		FBuilder::FWallSpec Twin;
		if (TwinDefaults)
		{
			const FBox Body = TwinDefaults->GetLocalBodyBox();
			const float HalfDepth = Body.IsValid ? static_cast<float>(Body.Max.X) : 20.f;
			Twin.BackOffset = 5.f + HalfDepth;
			Twin.HalfWidth = Body.IsValid ? static_cast<float>(Body.GetExtent().Y) : 30.f;
			Twin.Depth = 5.f + 2.f * HalfDepth;
			Twin.StandDistance = Twin.BackOffset + Stand;
			Twin.Reach = TwinDefaults->GetInteractRadius();
		}
		if ((Recipe->LogTerminalCount > 0 && !LogDefaults) || (Recipe->TwinAITerminalCount > 0 && !TwinDefaults))
		{
			Builder.WallError = TEXT("the log or twin AI terminal class is not set");
		}
		else if (!Builder.PlaceWallTerminals(Log, Twin, TerminalDefaults->GetInteractRadius(), LineClear))
		{
			UE_LOG(LogArea, Warning, TEXT("%s: act %d seed %d: %s; the validator fails this seed."), *GetName(), Act, Seed, *Builder.WallError);
		}
		// Brief 26: in the twin AI's room the terminal shows its own body (SM_TwinTerminal), set into the far wall.
		if (TwinDefaults && Builder.WayOut.RoomSegment >= 0)
		{
			const UStaticMesh* TwinMesh = TerminalDefaults->bModelledBody ? TwinDefaults->GetModelledBodyMesh() : nullptr;
			const FBox Body = TwinMesh ? TwinMesh->GetBoundingBox() : TwinDefaults->GetLocalBodyBox();
			if (!Builder.PlaceTwinInRoom(Body.IsValid ? static_cast<float>(Body.Min.X) : -20.f, Body.IsValid ? static_cast<float>(Body.Max.X) : 20.f, Stand,
				TwinDefaults->GetInteractRadius(), WallThickness))
			{
				UE_LOG(LogArea, Warning, TEXT("%s: act %d seed %d: %s; the validator fails this seed."), *GetName(), Act, Seed, *Builder.WallError);
			}
		}
		// Beacons last, on top of everything (no random draw, nothing else moves).
		if (!Builder.PlaceBeacons(TerminalDefaults->GetInteractRadius(), Log.Reach, Twin.Reach))
		{
			UE_LOG(LogArea, Warning, TEXT("%s: act %d seed %d: %s; the validator fails this seed."), *GetName(), Act, Seed, *Builder.BeaconError);
		}
	}

	// --- Publish the plan to the PCG graphs ---
	PublishedPieces.SetNum(6);
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Floors)].Boxes = Geometry.Floors;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Walls)].Boxes = Geometry.Walls;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Curbs)].Boxes = Geometry.Curbs;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Pillars)].Boxes = Geometry.Pillars;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Ceilings)].Boxes = Geometry.Ceilings;
	PublishedPieces[static_cast<int32>(EAreaPieceKind::Backdrop)].Boxes = Geometry.Backdrop;
	{
		// The boxes of the modelled segments go to the collision arrays (spawned hidden), the others stay shown; the
		// dressing is placed over the hidden ones. Neither changes PublishedPieces, which the hash and the plan check read.
		FDressing Dressing;
#if !UE_BUILD_SHIPPING
		// tls.Area.Dressing 0 (diagnostics): every box shown and no dressing, as before Brief 16; the plan is the same.
		if (CVarAreaDressing.GetValueOnGameThread() == 0)
		{
			Dressing.FloorHidden.Init(false, Geometry.Floors.Num());
			Dressing.WallHidden.Init(false, Geometry.Walls.Num());
			Dressing.CurbHidden.Init(false, Geometry.Curbs.Num());
			Dressing.PillarHidden.Init(false, Geometry.Pillars.Num());
			Dressing.CeilingHidden.Init(false, Geometry.Ceilings.Num());
			Dressing.Report = TEXT("off (tls.Area.Dressing 0)");
		}
		else
#endif
		{
			BuildDressing(*this, Plan, Geometry, Dressing);
		}
		const FAreaPieceHeights Heights = GetPieceHeights();
		TArray<FTransform>* Shown[] = { &PlanFloors, &PlanWalls, &PlanCurbs, &PlanPillars, &PlanCeilings, &PlanBackdrop };
		TArray<FTransform>* Hidden[] = { &PlanFloorsCollision, &PlanWallsCollision, &PlanCurbsCollision, &PlanPillarsCollision, &PlanCeilingsCollision, nullptr };
		const TArray<bool>* HiddenFlags[] = { &Dressing.FloorHidden, &Dressing.WallHidden, &Dressing.CurbHidden, &Dressing.PillarHidden, &Dressing.CeilingHidden, nullptr };
		for (int32 Kind = 0; Kind < 6; ++Kind)
		{
			const TArray<FBox>& Boxes = PublishedPieces[Kind].Boxes;
			for (int32 I = 0; I < Boxes.Num(); ++I)
			{
				const bool bCollisionOnly = HiddenFlags[Kind] && HiddenFlags[Kind]->IsValidIndex(I) && (*HiddenFlags[Kind])[I];
				(bCollisionOnly ? Hidden[Kind] : Shown[Kind])->Add(Heights.MakeTransform(static_cast<EAreaPieceKind>(Kind), Boxes[I]));
			}
		}
		DressWallPanels = MoveTemp(Dressing.Panels);
		DressFloorStripsFrom = Dressing.Plates.Num();
		Dressing.Plates.Append(Dressing.FloorStrips);
		DressFloorPlates = MoveTemp(Dressing.Plates);
		DressCeilingPlates = MoveTemp(Dressing.CeilingPlates);
		DressCornerPosts = MoveTemp(Dressing.Posts);
		DressCeilingBeams = MoveTemp(Dressing.Beams);
		DressConduits = MoveTemp(Dressing.Conduits);
		DressCurbs = MoveTemp(Dressing.Curbs);
		DressCatwalkFrames = MoveTemp(Dressing.Frames);
		DressGateFrames = MoveTemp(Dressing.GateFrames);
		LastDressingReport = MoveTemp(Dressing.Report);
		DressMostSqueezedPanel = Dressing.Squeezed;
		DressMostStretchedPanel = Dressing.Stretched;
		DressFirstConduitMouth = Dressing.FirstMouth;
		OpenEndBoxes.Reset();
		for (int32 I = 0; I < Geometry.Walls.Num(); ++I)
		{
			if (Geometry.WallOpen.IsValidIndex(I) && Geometry.WallOpen[I])
			{
				OpenEndBoxes.Add(Geometry.Walls[I]);
			}
		}
		// The twin AI's room's conduits and cables (Brief 26), from its terminal's placement.
		if (Builder.WayOut.RoomSegment >= 0 && Builder.TwinPlacements.Num() > 0)
		{
			const ATwinAITerminal* TwinDefaults = TwinAITerminalClass ? TwinAITerminalClass->GetDefaultObject<ATwinAITerminal>() : nullptr;
			const UStaticMesh* TwinMesh = TwinDefaults && TerminalDefaults->bModelledBody ? TwinDefaults->GetModelledBodyMesh() : nullptr;
			if (TwinMesh)
			{
				FString TwinReport;
				BuildTwinDressing(*this, Builder.TwinPlacements[0].Pos, Builder.TwinPlacements[0].Facing, TwinMesh->GetBoundingBox(), Builder.WayOut.RoomRect,
					DressTwinConduits, DressTwinBends, DressTwinCables, TwinReport);
				LastDressingReport += TEXT("; ") + TwinReport;
			}
		}
		// The backdrop's dressing (Brief 28): the masses' modules, the levels of walkways, the pillars. Look only, from the
		// finished plan with no random draw; each part has its switch and its values in the recipe (FAreaRecipe::Backdrop).
		{
			FDecorOut Decor;
			BuildDecor(*this, Plan, Geometry, Dressing, Recipe->Backdrop, Decor);
			for (int32 Mesh = 0; Mesh < DecorMeshCount; ++Mesh)
			{
				*MutableDecorArray(Mesh) = MoveTemp(Decor.Arrays[Mesh]);
			}
			DecorFaces = MoveTemp(Decor.Faces);
			DecorLevels = MoveTemp(Decor.Levels);
			DecorPillarRecords = MoveTemp(Decor.Pillars);
			DecorMassPicks = MoveTemp(Decor.MassPicks);
			DecorPillarPick = Decor.PillarPick;
			DecorForbidden = MoveTemp(Decor.Forbidden);
			DecorExitClearance = MoveTemp(Decor.ExitClearance);
			LastBackdropReport = MoveTemp(Decor.Report);
		}
	}
	RouteLength = Plan.RouteLength();
	RouteLengthBeforeWayOut = Plan.LengthBeforeWayOut;

	// --- Spawn the gameplay actors ---
	ULevel* Level = GetLevel();
	auto Spawn = [&](UClass* Class, const FVector& Location, const FRotator& Rotation, const FString& Label, const TCHAR* Folder, TFunction<void(AActor*)> BeforeConstruction = nullptr) -> AActor*
	{
		FActorSpawnParameters Params;
		Params.OverrideLevel = Level;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = BeforeConstruction != nullptr;
		if (!bGame)
		{
			Params.ObjectFlags |= RF_Transient;
		}
		AActor* Actor = World->SpawnActor(Class, &Location, &Rotation, Params);
		if (Actor && BeforeConstruction)
		{
			// Set what the construction reads (a door's modelled panel), then finish the spawn.
			BeforeConstruction(Actor);
			Actor->FinishSpawning(FTransform(Rotation, Location));
		}
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
		Record.bExitCorridor = S.bExitCorridor;
		Record.bTwinAIRoom = S.bTwinRoom;
		if (S.bExitCorridor || S.bTwinRoom)
		{
			const FVector2D D = DirV(S.EnterDir);
			Record.Direction = FVector(D.X, D.Y, 0.0);
			Record.Mouth = At(S.Mouth, 0.f);
			Record.Width = S.Width;
		}
		Info->Segments.Add(Record);
	}

	// Start (the map's PlayerStart moves to it), exit, entity (the map's entity moves to it)
	const FVector2D StartDir = Plan.Route.Num() > 1 ? (Plan.Route[1] - Plan.Route[0]).GetSafeNormal() : FVector2D(1.0, 0.0);
	PlannedStart = At(Plan.Route[0] + StartDir * 150.0, 100.f);
	PlannedStartRotation = YawOf(StartDir);
	// Halfway along the recipe's route: the way out (Brief 26) does not move the entity.
	PlannedEntity = At(Plan.PointAt(Plan.LengthBeforeWayOut * 0.5f), 0.f);
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
	if (Builder.WayOut.CorridorSegment >= 0)
	{
		// Brief 26: inside the exit corridor, the recipe's depth in from its mouth, across its whole section (wall to wall,
		// floor to ceiling), so the player cannot walk past it.
		AAreaExit* Exit = Cast<AAreaExit>(Spawn(AAreaExit::StaticClass(), At(Builder.WayOut.ExitPos, WallHeight * 0.5f), YawOf(Builder.WayOut.ExitDir), TEXT("Area_Exit"), TEXT("Flow")));
		if (Exit)
		{
			Exit->GetVolume()->SetBoxExtent(FVector(150.f, Builder.WayOut.HalfWidth, WallHeight * 0.5f));
			// The rest of the corridor past the exit: back in Normal time a player there travels at once.
			Exit->RunPastExit = FMath::Max(0.f, Builder.WayOut.Length - static_cast<float>(FVector2D::Distance(Builder.WayOut.ExitPos, Plan.Segs[Builder.WayOut.CorridorSegment].Mouth)) - 150.f);
		}
	}
	else
	{
		const FVector2D EndDir = (Plan.Route.Last() - Plan.Route[Plan.Route.Num() - 2]).GetSafeNormal();
		AAreaExit* Exit = Cast<AAreaExit>(Spawn(AAreaExit::StaticClass(), At(Plan.Route.Last() - EndDir * 150.0, 150.f), YawOf(EndDir), TEXT("Area_Exit"), TEXT("Flow")));
		if (Exit)
		{
			const float HalfWidth = FMath::Max(100.f, Plan.Segs.Last().Width * 0.5f - 10.f);
			Exit->GetVolume()->SetBoxExtent(FVector(150.f, HalfWidth, 150.f));
		}
	}

	// Gates
	for (int32 I = 0; I < Builder.GatePlacements.Num(); ++I)
	{
		const FBuilder::FGatePlacement& Gate = Builder.GatePlacements[I];
		// The gate's door shows the modelled gate panel (look only: its blocker, scan channel and timing are the class's).
		UStaticMesh* Panel = GatePanelMesh;
		ADoorDevice* Door = Cast<ADoorDevice>(Spawn(DoorClass, At(Gate.Door, 0.f), YawOf(DirV(Gate.DoorDir)), FString::Printf(TEXT("Area_Gate%d_Door"), I), TEXT("Gates"),
			[Panel](AActor* Actor) { if (ADoorDevice* D = Cast<ADoorDevice>(Actor); D && Panel) { D->PanelMesh = Panel; } }));
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

	// Log terminals and the twin AI's terminal (optional: they power nothing)
	for (int32 I = 0; I < Builder.LogPlacements.Num(); ++I)
	{
		const FBuilder::FWallPlacement& P = Builder.LogPlacements[I];
		Spawn(LogTerminalClass, At(P.Pos, 0.f), YawOf(P.Facing), FString::Printf(TEXT("Area_LogTerminal%d"), I), TEXT("Twin"));
		UE_LOG(LogArea, Log, TEXT("%s: log terminal %d in segment %d (%s) at (%.0f, %.0f), facing yaw %.0f, route %.1f m of %.1f m, reading place (%.0f, %.0f)."), *GetName(), I, P.Segment,
			LexToString(Plan.Segs[P.Segment].Type), P.Pos.X, P.Pos.Y, YawOf(P.Facing).Yaw, P.RouteDistance / 100.f, Plan.RouteLength() / 100.f, P.Stand.X, P.Stand.Y);
	}
	for (int32 I = 0; I < Builder.TwinPlacements.Num(); ++I)
	{
		const FBuilder::FWallPlacement& P = Builder.TwinPlacements[I];
		APowerTerminal* Twin = Cast<APowerTerminal>(Spawn(TwinAITerminalClass, At(P.Pos, 0.f), YawOf(P.Facing), FString::Printf(TEXT("Area_TwinAITerminal%d"), I), TEXT("Twin")));
		// The twin AI's terminal has no body of its own: it shows the modelled body when the area's terminals do.
		if (Twin && !Twin->bModelledBody && TerminalDefaults->bModelledBody)
		{
			Twin->bModelledBody = true;
			Twin->ApplyTerminalBody();
		}
		UE_LOG(LogArea, Log, TEXT("%s: twin AI terminal in segment %d (%s) at (%.0f, %.0f), facing yaw %.0f, route %.1f m of %.1f m, standing place (%.0f, %.0f)."), *GetName(), P.Segment,
			LexToString(Plan.Segs[P.Segment].Type), P.Pos.X, P.Pos.Y, YawOf(P.Facing).Yaw, P.RouteDistance / 100.f, Plan.RouteLength() / 100.f, P.Stand.X, P.Stand.Y);
	}

	// Beacons (optional: they power no door). Each is a light group with one spot light, its terminal and its body.
	{
		const FAreaPieceHeights Heights = GetPieceHeights();
		const float Height = FMath::Clamp(BeaconHeight, 300.f, 3000.f);
		const float RodMax = FMath::Clamp(BeaconRodMaxLength, 100.f, 20000.f);
		for (int32 I = 0; I < Builder.BeaconPlacements.Num(); ++I)
		{
			const FBuilder::FBeaconPlacement& B = Builder.BeaconPlacements[I];
			// Above the segment's highest floor or catwalk, over the centre of the space; aimed at the floor where the route leaves it.
			float FloorTop = -TNumericLimits<float>::Max();
			for (const FAreaFloorRect& Rect : Geometry.FloorRects)
			{
				if (Rect.Segment == B.Segment) { FloorTop = FMath::Max(FloorTop, static_cast<float>(Rect.Box.Max.Z)); }
			}
			FloorTop = FloorTop > -1.0e30f ? FloorTop : 0.f;
			const FVector Mount = At(B.Centre, FloorTop + Height);
			const FVector Aim = At(B.Aim, FloorTop);

			// The rod: up to the first geometry of the plan above the mount, else its maximum length.
			float Rod = RodMax;
			for (int32 Kind = 0; Kind < PublishedPieces.Num(); ++Kind)
			{
				for (const FBox& Box : PublishedPieces[Kind].Boxes)
				{
					const FTransform T = Heights.MakeTransform(static_cast<EAreaPieceKind>(Kind), Box);
					const FVector Half = T.GetScale3D() * (CubeSize * 0.5);
					const FBox World3(T.GetLocation() - Half, T.GetLocation() + Half);
					if (Mount.X >= World3.Min.X && Mount.X <= World3.Max.X && Mount.Y >= World3.Min.Y && Mount.Y <= World3.Max.Y && World3.Max.Z > Mount.Z)
					{
						Rod = FMath::Min(Rod, static_cast<float>(FMath::Max(0.0, World3.Min.Z - Mount.Z)));
					}
				}
			}
			const bool bReaches = Rod < RodMax;

			ABeaconBody* Body = Cast<ABeaconBody>(Spawn(ABeaconBody::StaticClass(), Mount, FRotator::ZeroRotator, FString::Printf(TEXT("Area_Beacon%d_Body"), I), TEXT("Beacons")));
			ALightGroupDevice* Device = Cast<ALightGroupDevice>(Spawn(LightGroupClass, At(B.Terminal, 0.f), FRotator::ZeroRotator, FString::Printf(TEXT("Area_Beacon%d"), I), TEXT("Beacons")));
			APowerTerminal* Terminal = Cast<APowerTerminal>(Spawn(TerminalClass, At(B.Terminal, 0.f), YawOf(B.TerminalFacing), FString::Printf(TEXT("Area_Beacon%d_Terminal"), I), TEXT("Beacons")));
			if (!Body || !Device)
			{
				continue;
			}
			Body->Tags.Add(AAreaInfo::BeaconTag);
			Body->Build(BeaconHeadMesh, BeaconRodMesh, Aim, Rod, bReaches);
			Body->Device = Device;
			Body->Segment = B.Segment;
			Body->HeightAboveFloor = Height;
			Device->Tags.Add(AAreaInfo::BeaconTag);
			Device->PoweredSeconds = LightGroupPoweredSeconds;
			const FVector Lens = Body->GetLensLocation();
			ASpotLight* Light = Cast<ASpotLight>(Spawn(ASpotLight::StaticClass(), Lens, (Aim - Lens).Rotation(), FString::Printf(TEXT("Area_Beacon%d_Light"), I), TEXT("Beacons")));
			if (Light)
			{
				Light->Tags.Add(AAreaInfo::BeaconTag);
				Light->GetRootComponent()->SetMobility(EComponentMobility::Movable);
				// ASpotLight's root (the light) carries a -90 pitch that the spawn rotation is combined with: aim it here.
				Light->SetActorRotation((Aim - Lens).Rotation());
				if (USpotLightComponent* Component = Cast<USpotLightComponent>(Light->GetLightComponent()))
				{
					Component->SetIntensityUnits(ELightUnits::Candelas);
					Component->SetIntensity(FMath::Clamp(BeaconIntensity, 0.f, 100000000.f));
					Component->SetAttenuationRadius(FMath::Clamp(BeaconRange, 1000.f, 50000.f));
					Component->SetInnerConeAngle(0.f);
					Component->SetOuterConeAngle(FMath::Clamp(BeaconConeAngle, 1.f, 45.f));
					Component->SetCastShadows(false);
					// Dark until paid, whenever the device begins play (spawned during or after the world's BeginPlay).
					Component->SetVisibility(false);
				}
				Device->Lights.Add(Light);
			}
			if (Terminal)
			{
				Terminal->Tags.Add(AAreaInfo::BeaconTag);
				Terminal->Targets.Add(Device);
			}
			UE_LOG(LogArea, Log, TEXT("%s: beacon %d over segment %d (%s) at (%.0f, %.0f, %.0f), %.0f cm above its highest floor, aimed at (%.0f, %.0f, %.0f) %.1f m away, rod %.0f cm (%s), terminal at (%.0f, %.0f) facing yaw %.0f, route %.1f m (segment from %.1f m)."),
				*GetName(), I, B.Segment, LexToString(Plan.Segs[B.Segment].Type), Mount.X, Mount.Y, Mount.Z, Height, Aim.X, Aim.Y, Aim.Z, FVector::Dist(Mount, Aim) / 100.f,
				Rod, bReaches ? TEXT("reaches the geometry above") : TEXT("maximum length, nothing above"), B.Terminal.X, B.Terminal.Y, YawOf(B.TerminalFacing).Yaw,
				B.TerminalRouteDistance / 100.f, Plan.Segs[B.Segment].RouteStart / 100.f);
		}
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

	UE_LOG(LogArea, Log, TEXT("%s: act %d seed %d planned in %.3f s: %d segments, route %.1f m, boxes (floors %d, walls %d, curbs %d, pillars %d, ceilings %d, backdrop %d), %d residues, %d gates, %d light groups, %d log terminals, %d twin AI terminals, %d beacons; PCG started."),
		*GetName(), Act, Seed, LastGenerationSeconds, Plan.Segs.Num(), Plan.RouteLength() / 100.f,
		Geometry.Floors.Num(), Geometry.Walls.Num(), Geometry.Curbs.Num(), Geometry.Pillars.Num(), Geometry.Ceilings.Num(), Geometry.Backdrop.Num(),
		Builder.NormalResidues.Num() + Builder.GatePlacements.Num(), Builder.GatePlacements.Num(), Builder.LightPlacements.Num(),
		Builder.LogPlacements.Num(), Builder.TwinPlacements.Num(), Builder.BeaconPlacements.Num());
	if (Builder.WayOut.CorridorSegment >= 0)
	{
		const FSeg& Corridor = Plan.Segs[Builder.WayOut.CorridorSegment];
		UE_LOG(LogArea, Log, TEXT("%s: WAY OUT act %d seed %d: route %.1f m before it, %.1f m with it; %sexit corridor segment %d from (%.0f, %.0f) heading yaw %.0f, %.0f m long (exit %.0f m in, %.0f m past it), %.0f cm wide."),
			*GetName(), Act, Seed, Plan.LengthBeforeWayOut / 100.f, Plan.RouteLength() / 100.f,
			Builder.WayOut.RoomSegment >= 0 ? *FString::Printf(TEXT("twin AI's room segment %d (%.0f m a side) entered at (%.0f, %.0f); "), Builder.WayOut.RoomSegment,
				Plan.Segs[Builder.WayOut.RoomSegment].Width / 100.f, Plan.Segs[Builder.WayOut.RoomSegment].Mouth.X, Plan.Segs[Builder.WayOut.RoomSegment].Mouth.Y) : TEXT(""),
			Builder.WayOut.CorridorSegment, Corridor.Mouth.X, Corridor.Mouth.Y, YawOf(Builder.WayOut.ExitDir).Yaw, Builder.WayOut.Length / 100.f,
			FVector2D::Distance(Builder.WayOut.ExitPos, Corridor.Mouth) / 100.f, (Builder.WayOut.Length - FVector2D::Distance(Builder.WayOut.ExitPos, Corridor.Mouth)) / 100.f, Corridor.Width);
	}
	return true;
}
