// SPDX-License-Identifier: MIT

// Brief 30: the entity's walls (EntityWall.h). AEntityWall is one wall (its strands of points and the volume that blocks the player while
// Stopped); the AEntity members below choose the Stop's walls among the area's passages and raise and clear them.

#include "Entity/EntityWall.h"

#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "Entity/Entity.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EntityWall)

TOptional<bool> AEntity::SessionWalls;
TOptional<int32> AEntity::SessionWallCount;

// ---------------------------------------------------------------------------------------------------------------------
// One strand
// ---------------------------------------------------------------------------------------------------------------------

FVector2D FEntityWallStrand::At(double T) const
{
	const FVector2D Chord = End - Start;
	const double C = Chord.Size();
	if (C < UE_KINDA_SMALL_NUMBER)
	{
		return Start;
	}
	const FVector2D E2 = Chord / C;
	const double S = FMath::Abs(Sagitta);
	if (S < 0.5)
	{
		return FMath::Lerp(Start, End, T);
	}
	// A circular arc through Start and End whose middle stands S off the chord, on the side Sagitta's sign says.
	const FVector2D E1 = FVector2D(-E2.Y, E2.X) * (Sagitta >= 0.0 ? 1.0 : -1.0);
	const double Radius = (C * C * 0.25 + S * S) / (2.0 * S);
	const double Half = FMath::Asin(FMath::Clamp(C * 0.5 / Radius, 0.0, 1.0));
	const FVector2D Centre = (Start + End) * 0.5 - E1 * (Radius - S);
	const double Phi = -Half + 2.0 * Half * T;
	return Centre + (E1 * FMath::Cos(Phi) + E2 * FMath::Sin(Phi)) * Radius;
}

// ---------------------------------------------------------------------------------------------------------------------
// AEntityWall
// ---------------------------------------------------------------------------------------------------------------------

AEntityWall::AEntityWall()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	// The player's pawn only: no trace channel (so no scan, sight or ground test sees it), no other object, no navigation.
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionObjectType(ECC_WorldDynamic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Volume->SetGenerateOverlapEvents(false);
	Volume->SetCanEverAffectNavigation(false);
	Volume->CanCharacterStepUpOn = ECB_No;
	Volume->SetHiddenInGame(true);
	Volume->SetVisibility(false);
	Volume->SetCastShadow(false);
	Volume->SetMobility(EComponentMobility::Movable);
}

FVector AEntityWall::ToWorld(const FVector2D& UV, double Depth) const
{
	return Passage.Centre + Passage.Across * UV.X + FVector::UpVector * UV.Y + Passage.Along * Depth;
}

void AEntityWall::Setup(int32 InPassageIndex, const FAreaPassage& InPassage, float InHeight, float InBlockDepth, int32 InStrands, float InBow, float InThickness, int32 InPoints,
	bool bInStrategic, FRandomStream& Rng)
{
	PassageIndex = InPassageIndex;
	Passage = InPassage;
	Height = FMath::Clamp(InHeight, 100.f, 2000.f);
	Thickness = FMath::Clamp(InThickness, 0.f, 50.f);
	PointCount = FMath::Clamp(InPoints, 0, 1000);
	bStrategic = bInStrategic;

	// The volume: the whole opening and a little into its sides (no gap at a wall or a curb), from the floor to the top.
	const float Depth = FMath::Clamp(InBlockDepth, 5.f, 200.f);
	Volume->SetBoxExtent(FVector(Depth * 0.5f, Passage.HalfWidth + 15.f, Height * 0.5f), false);
	SetActorLocationAndRotation(Passage.Centre + FVector(0.f, 0.f, Height * 0.5f), Passage.Along.Rotation());

	// The strands: across from edge to edge, from the floor to the top, from one side to the other; in that order, again.
	const int32 Count = FMath::Clamp(InStrands, 1, 32);
	const double HW = FMath::Max(10.0, Passage.HalfWidth - 3.0);
	const double V0 = 3.0;
	const double V1 = Height - 3.0;
	const float Bow = FMath::Clamp(InBow, 0.f, 0.5f);
	Strands.Reset();
	for (int32 K = 0; K < Count; ++K)
	{
		FEntityWallStrand Strand;
		const double Flip = Rng.FRand() < 0.5f ? -1.0 : 1.0;
		switch (K % 3)
		{
		case 0: // edge to edge, at a height, a little slanted
		{
			const double V = FMath::Lerp(V0, V1, Rng.FRandRange(0.1f, 0.9f));
			Strand.Start = FVector2D(-HW * Flip, V);
			Strand.End = FVector2D(HW * Flip, FMath::Clamp(V + Rng.FRandRange(-0.15f, 0.15f) * Height, V0, V1));
			break;
		}
		case 1: // floor to ceiling, across at a place, a little slanted
		{
			const double U = Rng.FRandRange(-0.85f, 0.85f) * HW;
			Strand.Start = FVector2D(U, Flip > 0.0 ? V0 : V1);
			Strand.End = FVector2D(FMath::Clamp(U + Rng.FRandRange(-0.3f, 0.3f) * HW, -HW, HW), Flip > 0.0 ? V1 : V0);
			break;
		}
		default: // side to side, from low on one edge to high on the other
		{
			Strand.Start = FVector2D(-HW * Flip, FMath::Lerp(V0, V1, Rng.FRandRange(0.f, 0.35f)));
			Strand.End = FVector2D(HW * Flip, FMath::Lerp(V0, V1, Rng.FRandRange(0.65f, 1.f)));
			break;
		}
		}
		const double Length = FVector2D::Distance(Strand.Start, Strand.End);
		Strand.Sagitta = Bow * Length * Rng.FRandRange(0.7f, 1.f) * (Rng.FRand() < 0.5f ? -1.0 : 1.0);
		Strand.Points = PointCount / Count + (K < PointCount % Count ? 1 : 0);
		Strands.Add(Strand);
	}
}

void AEntityWall::BuildPoints(int32 ScanIndex, TArray<FLidarInjectedPoint>& OutPoints) const
{
	// New jitter for every scan, as the trail's: each strand a line of points at an irregular step, thick by Thickness.
	FRandomStream Rng(static_cast<int32>(HashCombineFast(GetTypeHash(ScanIndex), HashCombineFast(GetTypeHash(GetUniqueID()), GetTypeHash(PassageIndex)))));
	const double HW = Passage.HalfWidth;
	OutPoints.Reserve(OutPoints.Num() + PointCount);
	for (const FEntityWallStrand& Strand : Strands)
	{
		for (int32 I = 0; I < Strand.Points; ++I)
		{
			const double T = FMath::Clamp((I + 0.5 + Rng.FRandRange(-0.45f, 0.45f)) / FMath::Max(Strand.Points, 1), 0.0, 1.0);
			FVector2D UV = Strand.At(T) + FVector2D(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)) * Thickness;
			UV.X = FMath::Clamp(UV.X, -HW, HW);
			UV.Y = FMath::Clamp(UV.Y, 0.0, static_cast<double>(Height));
			OutPoints.Emplace(ToWorld(UV, Rng.FRandRange(-1.f, 1.f) * Thickness));
		}
	}
}

bool AEntityWall::IsInsideOpening(const FVector& Point, float Tolerance) const
{
	const FVector D = Point - Passage.Centre;
	return FMath::Abs(FVector::DotProduct(D, Passage.Across)) <= Passage.HalfWidth + Tolerance && D.Z >= -Tolerance && D.Z <= Height + Tolerance
		&& FMath::Abs(FVector::DotProduct(D, Passage.Along)) <= Thickness + Tolerance;
}

double AEntityWall::GetSignedDistance(const FVector& Point) const
{
	return FVector::DotProduct(Point - Passage.Centre, Passage.Along);
}

// ---------------------------------------------------------------------------------------------------------------------
// AEntity: the walls
// ---------------------------------------------------------------------------------------------------------------------

namespace EntityWallTuning
{
	/** A crossing counts within this height below the opening's floor (the navmesh lies a little above or below it). */
	static constexpr float BelowFloor = 150.f;

	/** The point beyond a way out of the player's segment that the way-out check walks to. */
	static constexpr float BeyondWayOut = 150.f;

	/** A target (a residue, a terminal) counts as ahead of the player when it lies at least this far on along the route. */
	static constexpr float AheadMargin = 50.f;

	static float Distance2DToOpening(const FVector& Point, const FAreaPassage& P)
	{
		const FVector A = P.Centre - P.Across * P.HalfWidth;
		const FVector B = P.Centre + P.Across * P.HalfWidth;
		return static_cast<float>(FMath::PointDistToSegment(FVector(Point.X, Point.Y, 0.0), FVector(A.X, A.Y, 0.0), FVector(B.X, B.Y, 0.0)));
	}
}

void AEntity::SetSessionWalls(TOptional<bool> bOn)
{
	SessionWalls = bOn;
}

TOptional<bool> AEntity::GetSessionWalls()
{
	return SessionWalls;
}

void AEntity::SetSessionWallCount(TOptional<int32> Count)
{
	SessionWallCount = Count;
}

TOptional<int32> AEntity::GetSessionWallCount()
{
	return SessionWallCount;
}

int32 AEntity::GetWallCountNow() const
{
	if (SessionWallCount.IsSet())
	{
		return FMath::Clamp(SessionWallCount.GetValue(), 0, 4);
	}
	FActTuning Tuning;
	return UTheLastShiftSettings::Get()->GetActTuning(URunSubsystem::GetActIndexOf(GetWorld()), Tuning) ? Tuning.WallCount : 0;
}

int32 AEntity::GetWallPointsNow() const
{
	FActTuning Tuning;
	return UTheLastShiftSettings::Get()->GetActTuning(URunSubsystem::GetActIndexOf(GetWorld()), Tuning) ? Tuning.WallPoints : 250;
}

bool AEntity::AreWallsOn() const
{
	const bool bSwitch = SessionWalls.IsSet() ? SessionWalls.GetValue() : UTheLastShiftSettings::Get()->EntityForms.bEntityWalls;
	return bSwitch && GetWallCountNow() > 0;
}

float AEntity::GetWallHeight() const
{
	const AAreaGenerator* Generator = AAreaGenerator::Find(GetWorld());
	return Generator ? FMath::Clamp(Generator->WallHeight, 250.f, 2000.f) : 400.f;
}

TArray<AEntityWall*> AEntity::GetWalls() const
{
	TArray<AEntityWall*> Out;
	for (const TObjectPtr<AEntityWall>& Wall : Walls)
	{
		if (IsValid(Wall))
		{
			Out.Add(Wall);
		}
	}
	return Out;
}

bool AEntity::PathCrossesPassage(const TArray<FVector>& Path, const FAreaPassage& Passage, float Height, float* OutAlong)
{
	const FVector2D C(Passage.Centre.X, Passage.Centre.Y);
	const FVector2D Across(Passage.Across.X, Passage.Across.Y);
	const FVector2D Q0 = C - Across * Passage.HalfWidth;
	const FVector2D Q1 = C + Across * Passage.HalfWidth;
	const FVector2D S = Q1 - Q0;
	double Walked = 0.0;
	for (int32 I = 1; I < Path.Num(); ++I)
	{
		const FVector2D P0(Path[I - 1].X, Path[I - 1].Y);
		const FVector2D P1(Path[I].X, Path[I].Y);
		const FVector2D R = P1 - P0;
		const double Piece = FVector::Dist(Path[I - 1], Path[I]);
		const double Denominator = FVector2D::CrossProduct(R, S);
		if (FMath::Abs(Denominator) > UE_KINDA_SMALL_NUMBER)
		{
			const FVector2D QP = Q0 - P0;
			const double T = FVector2D::CrossProduct(QP, S) / Denominator;
			const double U = FVector2D::CrossProduct(QP, R) / Denominator;
			if (T >= 0.0 && T <= 1.0 && U >= 0.0 && U <= 1.0)
			{
				const double Z = FMath::Lerp(Path[I - 1].Z, Path[I].Z, T) - Passage.Centre.Z;
				if (Z >= -EntityWallTuning::BelowFloor && Z <= Height)
				{
					if (OutAlong)
					{
						*OutAlong = static_cast<float>(Walked + T * Piece);
					}
					return true;
				}
			}
		}
		Walked += Piece;
	}
	return false;
}

void AEntity::RefreshWallCandidates()
{
	UWorld* World = GetWorld();
	const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
	if (!Generator || !Generator->IsReady())
	{
		WallCandidates.Reset();
		bWallCandidatesBuilt = false;
		return;
	}
	const TArray<FAreaPassage>& Passages = Generator->GetPassages();
	if (bWallCandidatesBuilt && WallCandidatesLayout == Generator->GetLayoutHash() && WallCandidatesPassages == Passages.Num())
	{
		return;
	}
	bWallCandidatesBuilt = true;
	WallCandidatesLayout = Generator->GetLayoutHash();
	WallCandidatesPassages = Passages.Num();
	WallCandidates.Reset();

	const AAreaInfo* Info = AAreaInfo::Find(World);
	const float Clear = FMath::Clamp(UTheLastShiftSettings::Get()->EntityForms.WallClearOfDevices, 0.f, 2000.f);
	TArray<FVector> Terminals;
	for (TActorIterator<APowerTerminal> It(World); It; ++It)
	{
		Terminals.Add(It->GetActorLocation());
	}
	TArray<FVector> Doors;
	for (TActorIterator<ADoorDevice> It(World); It; ++It)
	{
		Doors.Add(It->GetActorLocation());
	}
	auto IsExitCorridor = [Info](int32 Segment)
	{
		return Info && Info->Segments.IsValidIndex(Segment) && Info->Segments[Segment].bExitCorridor;
	};
	int32 Excluded = 0;
	for (int32 Index = 0; Index < Passages.Num(); ++Index)
	{
		FEntityWallCandidate Candidate;
		Candidate.Passage = Index;
		Candidate.Where = Passages[Index];
		const FAreaPassage& P = Candidate.Where;
		if (IsExitCorridor(P.Segment) || IsExitCorridor(P.OtherSegment))
		{
			Candidate.bExcluded = true;
			Candidate.Why = TEXT("the exit corridor or its mouth");
		}
		for (const FVector& Door : Doors)
		{
			if (!Candidate.bExcluded && FMath::Abs(Door.Z - P.Centre.Z) < 600.f && EntityWallTuning::Distance2DToOpening(Door, P) < Clear)
			{
				Candidate.bExcluded = true;
				Candidate.Why = TEXT("a gate's door");
			}
		}
		for (const FVector& Terminal : Terminals)
		{
			if (!Candidate.bExcluded && FMath::Abs(Terminal.Z - P.Centre.Z) < 600.f && EntityWallTuning::Distance2DToOpening(Terminal, P) < Clear)
			{
				Candidate.bExcluded = true;
				Candidate.Why = FString::Printf(TEXT("a terminal within %.0f cm"), Clear);
			}
		}
		Excluded += Candidate.bExcluded ? 1 : 0;
		WallCandidates.Add(MoveTemp(Candidate));
	}
	UE_LOG(LogEntity, Log, TEXT("%s: walls: %d candidate passages, %d excluded (exit corridor, gate doors, terminals within %.0f cm)."), *GetName(), WallCandidates.Num(), Excluded, Clear);
}

const TArray<FEntityWallCandidate>& AEntity::GetWallCandidates()
{
	RefreshWallCandidates();
	return WallCandidates;
}

void AEntity::ClearWalls(const TCHAR* Why)
{
	int32 Removed = 0;
	for (TObjectPtr<AEntityWall>& Wall : Walls)
	{
		if (IsValid(Wall))
		{
			Wall->Destroy();
			++Removed;
		}
	}
	Walls.Reset();
	if (Removed > 0 && Why)
	{
		UE_LOG(LogEntity, Log, TEXT("%s: walls: %d removed (%s)."), *GetName(), Removed, Why);
	}
}

AEntityWall* AEntity::SpawnWall(int32 Candidate, bool bStrategic)
{
	UWorld* World = GetWorld();
	if (!World || !WallCandidates.IsValidIndex(Candidate))
	{
		return nullptr;
	}
	const FAreaPassage& Passage = WallCandidates[Candidate].Where;
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AEntityWall* Wall = World->SpawnActor<AEntityWall>(AEntityWall::StaticClass(), FTransform(Passage.Along.Rotation(), Passage.Centre), Params);
	if (!Wall)
	{
		return nullptr;
	}
	// The look of the act's form (EntityForms); the procedural figure, or a form with no entry, takes the defaults.
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const FEntityFormTuning* Form = Settings->FindEntityForm(GetRequestedForm());
	const FEntityFormTuning Look = Form ? *Form : FEntityFormTuning();
	if (!bWallRngInitialized)
	{
		WallRng.Initialize(static_cast<int32>(HashCombineFast(GetTypeHash(SpawnSeed), 0x57A11u)));
		bWallRngInitialized = true;
	}
	Wall->Setup(Candidate, Passage, GetWallHeight(), Settings->EntityForms.WallBlockDepth, Look.WallStrands, Look.WallBow, Look.WallThickness, GetWallPointsNow(), bStrategic, WallRng);
	Walls.Add(Wall);
	return Wall;
}

void AEntity::GatherWallPoints(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints)
{
	LastGatheredWallPoints = 0;
	if (!TimeState || !TimeState->IsStopped())
	{
		return;
	}
	const int32 First = OutPoints.Num();
	for (const TObjectPtr<AEntityWall>& Wall : Walls)
	{
		if (IsValid(Wall))
		{
			Wall->BuildPoints(Context.ScanIndex, OutPoints);
		}
	}
	LastGatheredWallPoints = OutPoints.Num() - First;
}

bool AEntity::RaiseWallsAtForTest(const TArray<int32>& Candidates)
{
	if (!TimeState || !TimeState->IsStopped())
	{
		return false;
	}
	RefreshWallCandidates();
	ClearWalls(TEXT("raised for a test"));
	bWallsChosenThisStop = true;
	FEntityWallReport Report;
	Report.StopSerial = StopSerial;
	Report.Why = TEXT("test");
	Report.Wanted = Candidates.Num();
	Report.Candidates = WallCandidates.Num();
	for (const int32 Candidate : Candidates)
	{
		if (SpawnWall(Candidate, false))
		{
			Report.Passages.Add(Candidate);
			Report.PathDistances.Add(-1.f);
		}
	}
	Report.Raised = Report.Passages.Num();
	LastWallReport = Report;
	return Report.Raised == Candidates.Num();
}

int32 AEntity::RaiseWalls(const TCHAR* Why)
{
	bWallsChosenThisStop = true;
	ClearWalls(TEXT("a new choice"));
	FEntityWallReport Report;
	Report.StopSerial = StopSerial;
	Report.Why = Why;
	Report.Wanted = GetWallCountNow();
	const int32 PreviousStrategic = LastStrategicPassage;
	LastStrategicPassage = INDEX_NONE;

	UWorld* World = GetWorld();
	const AAreaInfo* Info = World ? AAreaInfo::Find(World) : nullptr;
	const APawn* Pawn = GetPlayerPawn();
	FVector Player;
	if (State != EEntityState::Awake || !AreWallsOn() || !Info || !Pawn || !GetPlayerNavLocation(Player))
	{
		Report.StrategicWhy = State != EEntityState::Awake ? TEXT("dormant") : !AreWallsOn() ? TEXT("walls off") : TEXT("no area or no player");
		LastWallReport = Report;
		return 0;
	}
	RefreshWallCandidates();
	Report.Candidates = WallCandidates.Num();
	if (WallCandidates.Num() == 0)
	{
		Report.StrategicWhy = TEXT("no candidate passage (no area plan)");
		LastWallReport = Report;
		return 0;
	}
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const float MinDistance = FMath::Clamp(Settings->EntityForms.WallMinDistanceM, 0.f, 100.f) * 100.f;
	const float Range = FMath::Clamp(Settings->EntityForms.WallRangeM, 1.f, 500.f) * 100.f;
	const float Height = GetWallHeight();
	const float Depth = FMath::Clamp(Settings->EntityForms.WallBlockDepth, 5.f, 200.f);
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float Radius = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;

	auto OnNav = [NavSys](const FVector& Point, FVector& Out)
	{
		FNavLocation Location;
		if (NavSys && NavSys->ProjectPointToNavigation(Point, Location, FVector(120.f, 120.f, 300.f)))
		{
			Out = Location.Location;
			return true;
		}
		Out = Point;
		return false;
	};
	auto PathTo = [this, &Player, &OnNav](const FVector& Target, TArray<FVector>& OutPath, float& OutLength)
	{
		FVector Goal;
		OnNav(Target, Goal);
		return FindPath(Player, Goal, OutPath, OutLength);
	};

	// Where the player stands: the segment of the floor under them, its route distance.
	float Offset = 0.f;
	int32 PlayerSegment = INDEX_NONE;
	const float PlayerS = Info->ProjectToRoute(Player, PlayerSegment, Offset);
	if (const int32 Rect = Info->FindFloorRect(Player, 30.f); Info->FloorRects.IsValidIndex(Rect))
	{
		PlayerSegment = Info->FloorRects[Rect].Segment;
	}
	Report.PlayerSegment = PlayerSegment;
	Report.PlayerRouteDistance = PlayerS;

	// The limit of the regenerating residues: none on the player's way to that of a gate still ahead.
	TArray<float> DoorS;
	for (TActorIterator<ADoorDevice> It(World); It; ++It)
	{
		int32 Unused = 0;
		float UnusedOffset = 0.f;
		DoorS.Add(Info->ProjectToRoute(It->GetActorLocation(), Unused, UnusedOffset));
	}
	TArray<TArray<FVector>> ProtectedPaths;
	for (TActorIterator<AResidue> It(World); It; ++It)
	{
		if (!It->bRegenerates)
		{
			continue;
		}
		int32 Unused = 0;
		float UnusedOffset = 0.f;
		const float ResidueS = Info->ProjectToRoute(It->GetActorLocation(), Unused, UnusedOffset);
		float GateS = -1.f;
		for (const float S : DoorS)
		{
			if (S > ResidueS && (GateS < 0.f || S < GateS))
			{
				GateS = S;
			}
		}
		TArray<FVector> Path;
		float Length = 0.f;
		if (GateS > PlayerS && PathTo(It->GetActorLocation(), Path, Length))
		{
			ProtectedPaths.Add(MoveTemp(Path));
		}
	}

	// The ways out of the player's segment: past each of its joints, a little into the next segment.
	TArray<TArray<FVector>> WayPaths;
	int32 Ways = 0;
	if (const AAreaGenerator* Generator = AAreaGenerator::Find(World))
	{
		for (const FAreaPassage& P : Generator->GetPassages())
		{
			if (!P.IsJoint() || (P.Segment != PlayerSegment && P.OtherSegment != PlayerSegment))
			{
				continue;
			}
			++Ways;
			const FVector Beyond = P.Centre + P.Along * (P.Segment == PlayerSegment ? EntityWallTuning::BeyondWayOut : -EntityWallTuning::BeyondWayOut);
			TArray<FVector> Path;
			float Length = 0.f;
			if (PathTo(Beyond, Path, Length))
			{
				WayPaths.Add(MoveTemp(Path));
			}
		}
	}
	Report.WaysOut = Ways;
	auto KeepsWayOut = [this, &WayPaths, Ways, Height](const TArray<int32>& Set)
	{
		if (Ways == 0)
		{
			return true;
		}
		for (const TArray<FVector>& Path : WayPaths)
		{
			bool bOpen = true;
			for (const int32 Candidate : Set)
			{
				bOpen &= !PathCrossesPassage(Path, WallCandidates[Candidate].Where, Height);
			}
			if (bOpen)
			{
				return true;
			}
		}
		return false;
	};

	// Each candidate: usable, protected, the player's path distance to it (only where it may matter), not where the player stands.
	struct FDynamic
	{
		bool bUsable = false;
		bool bProtected = false;
		bool bUnderPlayer = false;
		bool bMeasured = false;
		float PathDistance = -1.f;
	};
	TArray<FDynamic> Dynamic;
	Dynamic.SetNum(WallCandidates.Num());
	auto Measure = [&](int32 Candidate) -> float
	{
		FDynamic& D = Dynamic[Candidate];
		if (!D.bMeasured)
		{
			D.bMeasured = true;
			TArray<FVector> Path;
			float Length = 0.f;
			D.PathDistance = PathTo(WallCandidates[Candidate].Where.Centre, Path, Length) ? Length : -1.f;
		}
		return D.PathDistance;
	};
	for (int32 Candidate = 0; Candidate < WallCandidates.Num(); ++Candidate)
	{
		FDynamic& D = Dynamic[Candidate];
		const FEntityWallCandidate& C = WallCandidates[Candidate];
		D.bUsable = !C.bExcluded;
		if (!D.bUsable)
		{
			continue;
		}
		++Report.Usable;
		for (const TArray<FVector>& Path : ProtectedPaths)
		{
			D.bProtected |= PathCrossesPassage(Path, C.Where, Height);
		}
		const FVector Local = Player - C.Where.Centre;
		D.bUnderPlayer = FMath::Abs(FVector::DotProduct(Local, C.Where.Along)) <= Depth * 0.5f + Radius + 10.f
			&& FMath::Abs(FVector::DotProduct(Local, C.Where.Across)) <= C.Where.HalfWidth + Radius + 30.f;
		Report.Protected += D.bProtected ? 1 : 0;
	}
	auto Allowed = [&](int32 Candidate, bool bStrategic, const TArray<int32>& Chosen) -> bool
	{
		const FDynamic& D = Dynamic[Candidate];
		if (!D.bUsable || D.bProtected || D.bUnderPlayer || Chosen.Contains(Candidate) || (bStrategic && Candidate == PreviousStrategic))
		{
			return false;
		}
		const float Distance = Measure(Candidate);
		if (Distance < 0.f || Distance < MinDistance || (!bStrategic && Distance > Range))
		{
			return false;
		}
		TArray<int32> With = Chosen;
		With.Add(Candidate);
		if (!KeepsWayOut(With))
		{
			++Report.RejectedWayOut;
			return false;
		}
		return true;
	};

	TArray<int32> Chosen;

	// The strategic wall: the nearest thing not taken ahead along the route (else behind), and the last passage the player's
	// path to it crosses that the limits allow.
	if (Report.Wanted > 0)
	{
		const AActor* Target = nullptr;
		float TargetS = 0.f;
		bool bAhead = false;
		auto Consider = [&](const AActor* Actor)
		{
			int32 Unused = 0;
			float UnusedOffset = 0.f;
			const float S = Info->ProjectToRoute(Actor->GetActorLocation(), Unused, UnusedOffset);
			const bool bIsAhead = S >= PlayerS + EntityWallTuning::AheadMargin;
			if (!Target || (bIsAhead && !bAhead) || (bIsAhead == bAhead && (bIsAhead ? S < TargetS : S > TargetS)))
			{
				Target = Actor;
				TargetS = S;
				bAhead = bIsAhead;
			}
		};
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			// The regenerating residues are protected by the limit above: never a target.
			if (!It->bRegenerates && It->GetState() == EResidueState::Available)
			{
				Consider(*It);
			}
		}
		for (TActorIterator<APowerTerminal> It(World); It; ++It)
		{
			if (It->OffersPayment())
			{
				Consider(*It);
			}
		}
		if (Target)
		{
			Report.StrategicTarget = Target->GetName();
			Report.StrategicTargetLocation = Target->GetActorLocation();
			Report.bStrategicAhead = bAhead;
			TArray<FVector> Path;
			float Length = 0.f;
			if (!PathTo(Target->GetActorLocation(), Path, Length))
			{
				Report.StrategicWhy = TEXT("no path to the target");
			}
			else
			{
				TArray<TPair<float, int32>> Crossed;
				for (int32 Candidate = 0; Candidate < WallCandidates.Num(); ++Candidate)
				{
					float Along = 0.f;
					if (Dynamic[Candidate].bUsable && PathCrossesPassage(Path, WallCandidates[Candidate].Where, Height, &Along))
					{
						Crossed.Emplace(Along, Candidate);
					}
				}
				Crossed.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
				Report.StrategicCrossed = Crossed.Num();
				for (int32 K = Crossed.Num() - 1; K >= 0; --K)
				{
					if (Allowed(Crossed[K].Value, true, Chosen))
					{
						Chosen.Add(Crossed[K].Value);
						Report.StrategicPassage = Crossed[K].Value;
						LastStrategicPassage = Crossed[K].Value;
						break;
					}
				}
				if (Report.StrategicPassage == INDEX_NONE)
				{
					Report.StrategicWhy = Crossed.Num() == 0 ? TEXT("no candidate passage between the player and the target") : TEXT("the limits leave no passage on the way");
				}
			}
		}
		else
		{
			Report.StrategicWhy = TEXT("nothing left to take");
		}
	}

	// The others, at random from the entity's wall stream, among the candidates within WallRangeM.
	TArray<int32> Pool;
	for (int32 Candidate = 0; Candidate < WallCandidates.Num(); ++Candidate)
	{
		const FDynamic& D = Dynamic[Candidate];
		if (!D.bUsable || D.bProtected || D.bUnderPlayer || Chosen.Contains(Candidate)
			|| FVector::Dist(Player, WallCandidates[Candidate].Where.Centre) > Range)
		{
			continue;
		}
		const float Distance = Measure(Candidate);
		if (Distance < 0.f || Distance < MinDistance)
		{
			Report.TooNear += Distance >= 0.f ? 1 : 0;
			continue;
		}
		if (Distance <= Range)
		{
			Pool.Add(Candidate);
		}
	}
	Report.InRange = Pool.Num();
	if (!bWallRngInitialized)
	{
		WallRng.Initialize(static_cast<int32>(HashCombineFast(GetTypeHash(SpawnSeed), 0x57A11u)));
		bWallRngInitialized = true;
	}
	while (Chosen.Num() < Report.Wanted && Pool.Num() > 0)
	{
		const int32 Pick = WallRng.RandRange(0, Pool.Num() - 1);
		const int32 Candidate = Pool[Pick];
		Pool.RemoveAt(Pick);
		if (Allowed(Candidate, false, Chosen))
		{
			Chosen.Add(Candidate);
		}
	}

	for (int32 K = 0; K < Chosen.Num(); ++K)
	{
		if (SpawnWall(Chosen[K], Chosen[K] == Report.StrategicPassage))
		{
			Report.Passages.Add(Chosen[K]);
			Report.PathDistances.Add(Dynamic[Chosen[K]].PathDistance);
		}
	}
	Report.Raised = Report.Passages.Num();
	LastWallReport = Report;

	FString List;
	for (int32 K = 0; K < Report.Passages.Num(); ++K)
	{
		const FAreaPassage& P = WallCandidates[Report.Passages[K]].Where;
		List += FString::Printf(TEXT("%s#%d %s at route %.0f m, %.1f m of path%s"), K > 0 ? TEXT("; ") : TEXT(""), Report.Passages[K], LexToString(P.Kind), P.RouteDistance / 100.f,
			Report.PathDistances[K] / 100.f, Report.Passages[K] == Report.StrategicPassage ? TEXT(" (strategic)") : TEXT(""));
	}
	UE_LOG(LogEntity, Log, TEXT("%s: walls (%s, Stop %d): %d of %d raised [%s]; player at route %.0f m in segment %d (%d ways out); strategic target %s%s%s; %d usable, %d protected, %d in range, %d left out for the way out."),
		*GetName(), Why, StopSerial, Report.Raised, Report.Wanted, *List, PlayerS / 100.f, PlayerSegment, Ways, Report.StrategicTarget.IsEmpty() ? TEXT("none") : *Report.StrategicTarget,
		Report.bStrategicAhead ? TEXT(" (ahead)") : TEXT(" (behind)"), Report.StrategicWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *Report.StrategicWhy),
		Report.Usable, Report.Protected, Report.InRange, Report.RejectedWayOut);
	return Report.Raised;
}
