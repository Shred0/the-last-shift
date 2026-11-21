// SPDX-License-Identifier: MIT

#include "Prologue/PrologueHologram.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Prologue/PrologueBriefingTerminal.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueHologram)

namespace PrologueHologramImpl
{
	/** Size of the engine's basic cube mesh used for a point, a node and a line. */
	static constexpr float CubeMeshSize = 100.f;
	/** The cloud is the same every time it is built. */
	static constexpr int32 Seed = 1111;
	/** The drift and the flicker have their own streams, so the cloud stays what it was before they existed. */
	static constexpr int32 DriftSeed = 2222;
	static constexpr int32 BeamSeed = 3333;

	/** The six vertices of a regular octahedron of half-diagonal R, and its twelve edges and eight faces. */
	static void Octahedron(float R, TArray<FVector>& OutVertices, TArray<TPair<int32, int32>>& OutEdges, TArray<FIntVector>& OutFaces)
	{
		OutVertices = { FVector(R, 0, 0), FVector(-R, 0, 0), FVector(0, R, 0), FVector(0, -R, 0), FVector(0, 0, R), FVector(0, 0, -R) };
		const int32 Equator[4] = { 0, 2, 1, 3 };
		for (int32 I = 0; I < 4; ++I)
		{
			const int32 A = Equator[I];
			const int32 B = Equator[(I + 1) % 4];
			OutEdges.Emplace(A, B);
			OutEdges.Emplace(A, 4);
			OutEdges.Emplace(A, 5);
			OutFaces.Emplace(A, B, 4);
			OutFaces.Emplace(A, B, 5);
		}
	}

	/** How far P can move along the unit direction D, in the plane of triangle (A, B, C), before it leaves the triangle. */
	static float RoomInTriangle(const FVector& P, const FVector& D, const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const FVector Corners[3] = { A, B, C };
		float Room = TNumericLimits<float>::Max();
		for (int32 I = 0; I < 3; ++I)
		{
			const FVector E0 = Corners[I];
			const FVector E1 = Corners[(I + 1) % 3];
			const FVector Opposite = Corners[(I + 2) % 3];
			// The edge's normal in the plane, pointing out of the triangle.
			FVector Out = FVector::CrossProduct(E1 - E0, N).GetSafeNormal();
			if (FVector::DotProduct(Opposite - E0, Out) > 0.f)
			{
				Out = -Out;
			}
			const float Toward = FVector::DotProduct(D, Out);
			if (Toward > KINDA_SMALL_NUMBER)
			{
				Room = FMath::Min(Room, FMath::Max(0.f, FVector::DotProduct(E0 - P, Out)) / Toward);
			}
		}
		return Room;
	}
}

APrologueHologram::APrologueHologram()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	const auto Setup = [](UInstancedStaticMeshComponent* C)
	{
		// Movable: shown or hidden with the time state and moved every frame; unlit, they need no baked light.
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->bHiddenInSceneCapture = true;
		C->SetRenderCustomDepth(false);
		C->bAffectDistanceFieldLighting = false;
		C->SetCullDistances(0, 0);
	};

	Points = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Points"));
	SetRootComponent(Points);
	Setup(Points);
	Route = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Route"));
	Route->SetupAttachment(Points);
	Setup(Route);
	// The beams rise from the still base: they keep the world's frame while the points (the root) turn.
	Beams = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Beams"));
	Beams->SetupAttachment(Points);
	Beams->SetUsingAbsoluteLocation(true);
	Beams->SetUsingAbsoluteRotation(true);
	Beams->SetUsingAbsoluteScale(true);
	Setup(Beams);
	Beams->NumCustomDataFloats = 1;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Points->SetStaticMesh(CubeMesh.Object);
		Route->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (Cone.Succeeded())
	{
		Beams->SetStaticMesh(Cone.Object);
	}
}

void APrologueHologram::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PlacedRotation = Points->GetComponentQuat();
	Rebuild();
}

void APrologueHologram::BeginPlay()
{
	Super::BeginPlay();
	PlacedRotation = Points->GetComponentQuat();
	TurnDegrees = 0.f;
	RouteProgress = -1.f;
	RouteOverride = -1.f;
	BriefingProgress = 0.f;
	bShownWhileStoppedForTest = false;
	Rebuild();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &APrologueHologram::HandleTimeStateChanged);
	}
	if (!Briefing && GetWorld())
	{
		for (TActorIterator<APrologueBriefingTerminal> It(GetWorld()); It; ++It)
		{
			Briefing = *It;
			break;
		}
	}
	if (Briefing)
	{
		// The terminal updates the briefing's share in its tick: read it in the same frame, after it.
		AddTickPrerequisiteActor(Briefing);
	}
	ApplyVisibility();
	UpdateMotion(TimeState ? TimeState->GetRunSeconds() : 0.0);
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %d points, %.0f cm across, colour %s (ScanColors.Scan), one turn in %.1f s, drift %.1f cm over %.1f s; route %d nodes in %s (ScanColors.Entity), drawn by %s; %d beams from %s (spread %.1f deg, %.2f of the %.1f deg that hold the hologram, length %.3f of %.0f cm to the far side, level %.3f at the tip, fade power %.1f); hidden in scene captures, no collision."),
		*GetName(), GetPointCount(), Tuning.Size, *PointColor.ToString(), Tuning.TurnSeconds, Tuning.DriftDistance, Tuning.DriftSeconds,
		RouteNodes.Num(), *RouteColor.ToString(), *GetNameSafe(Briefing), GetBeamCount(), *GetNameSafe(Base),
		BeamSpreads.Num() ? BeamSpreads[0] : 0.f, Tuning.BeamSpreadShare, BeamHolding.Num() ? BeamHolding[0] : 0.f, Tuning.BeamLength, BeamFars.Num() ? BeamFars[0] : 0.f,
		Tuning.BeamLevel, Tuning.BeamFadePower);
}

void APrologueHologram::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &APrologueHologram::HandleTimeStateChanged);
		TimeState = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void APrologueHologram::Rebuild()
{
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	HalfSize = Tuning.Size * 0.5f;
	PointColor = UTheLastShiftSettings::GetScanColors().Scan;
	RouteColor = UTheLastShiftSettings::GetScanColors().Entity;

	const auto Instance = [this](UMaterialInterface* Parent, TObjectPtr<UMaterialInstanceDynamic>& Mid, UInstancedStaticMeshComponent* Component, const FLinearColor& Color)
	{
		if (!Parent)
		{
			return;
		}
		if (!Mid || Mid->Parent != Parent)
		{
			Mid = UMaterialInstanceDynamic::Create(Parent, this);
		}
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Mid);
	};
	Instance(PointMaterial, PointMaterialInstance, Points, PointColor);
	Instance(PointMaterial, RouteMaterialInstance, Route, RouteColor);
	Instance(BeamMaterial, BeamMaterialInstance, Beams, PointColor);

	BuildPoints();
	BuildRoute();
	BuildBeams();
}

void APrologueHologram::BuildPoints()
{
	using namespace PrologueHologramImpl;
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();

	TArray<FVector> Vertices;
	TArray<TPair<int32, int32>> Edges;
	TArray<FIntVector> Faces;
	Octahedron(HalfSize, Vertices, Edges, Faces);

	FRandomStream Rng(Seed);
	FRandomStream DriftRng(DriftSeed);
	const int32 EdgePoints = FMath::RoundToInt(Tuning.Points * Tuning.EdgeShare);
	const FVector Scale(Tuning.PointSize / CubeMeshSize);
	RestPositions.Reset(Tuning.Points);
	DriftVectors.Reset(Tuning.Points);
	DriftPeriods.Reset(Tuning.Points);
	DriftPhases.Reset(Tuning.Points);
	PointTransforms.Reset(Tuning.Points);
	for (int32 I = 0; I < Tuning.Points; ++I)
	{
		FVector P;
		FVector Drift = FVector::ZeroVector;
		if (I < EdgePoints)
		{
			// Evenly along each edge, with a little jitter so the edges do not read as dotted lines.
			const TPair<int32, int32>& Edge = Edges[I % Edges.Num()];
			const int32 PerEdge = FMath::Max(EdgePoints / Edges.Num(), 1);
			const float T = (FMath::Min(I / Edges.Num(), PerEdge - 1) + Rng.FRand()) / PerEdge;
			const FVector A = Vertices[Edge.Key];
			const FVector B = Vertices[Edge.Value];
			P = FMath::Lerp(A, B, T);
			// Along its edge (it lies on the two faces that share it), never past the edge's ends.
			const float Length = FVector::Dist(A, B);
			const float Room = FMath::Min(T, 1.f - T) * Length;
			Drift = (B - A).GetSafeNormal() * FMath::Min(Tuning.DriftDistance, Room);
		}
		else
		{
			// Uniform over the face's area.
			const FIntVector& Face = Faces[Rng.RandHelper(Faces.Num())];
			float U = Rng.FRand();
			float V = Rng.FRand();
			if (U + V > 1.f)
			{
				U = 1.f - U;
				V = 1.f - V;
			}
			const FVector A = Vertices[Face.X];
			const FVector B = Vertices[Face.Y];
			const FVector C = Vertices[Face.Z];
			P = A + U * (B - A) + V * (C - A);
			// A direction in the face's plane, and no further than the face allows either way.
			const FVector E1 = (B - A).GetSafeNormal();
			const FVector E2 = FVector::CrossProduct(FVector::CrossProduct(B - A, C - A).GetSafeNormal(), E1);
			const float Angle = DriftRng.FRandRange(0.f, 2.f * PI);
			const FVector D = E1 * FMath::Cos(Angle) + E2 * FMath::Sin(Angle);
			const float Room = FMath::Min(RoomInTriangle(P, D, A, B, C), RoomInTriangle(P, -D, A, B, C));
			Drift = D * FMath::Min(Tuning.DriftDistance, Room);
		}
		RestPositions.Add(P);
		DriftVectors.Add(Drift);
		DriftPeriods.Add(Tuning.DriftSeconds * (1.f + DriftRng.FRandRange(-Tuning.DriftPeriodSpread, Tuning.DriftPeriodSpread)));
		DriftPhases.Add(DriftRng.FRandRange(0.f, 2.f * PI));
		PointTransforms.Emplace(FQuat::Identity, P, Scale);
	}
	Points->ClearInstances();
	Points->AddInstances(PointTransforms, false, false, false);
}

void APrologueHologram::BuildRoute()
{
	using namespace PrologueHologramImpl;
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	RouteNodes = { Tuning.RouteNode1 * HalfSize, Tuning.RouteNode2 * HalfSize, Tuning.RouteNode3 * HalfSize, Tuning.RouteNode4 * HalfSize };
	Route->ClearInstances();
	TArray<FTransform> Hidden;
	Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), RouteNodes.Num() * 2 - 1);
	Route->AddInstances(Hidden, false, false, false);
	RouteNodesShown = 0;
	RouteProgress = -1.f;
	UpdateRoute(RouteOverride >= 0.f ? RouteOverride : BriefingProgress);
}

void APrologueHologram::BuildBeams()
{
	using namespace PrologueHologramImpl;
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	Beams->ClearInstances();
	Beams->SetNumCustomDataFloats(1);
	BeamStarts.Reset();
	BeamEnds.Reset();
	BeamSpreads.Reset();
	BeamHolding.Reset();
	BeamFars.Reset();
	BeamPhases.Reset();
	BeamLevels.Reset();
	Beams->SetWorldTransform(FTransform::Identity);
	const UStaticMesh* Cone = Beams->GetStaticMesh();
	if (!Base || Tuning.BeamRods <= 0 || !Cone)
	{
		return;
	}
	const FBox ConeBox = Cone->GetBoundingBox();
	const FVector ConeSize = ConeBox.GetSize();
	if (ConeSize.X <= 0.0 || ConeSize.Z <= 0.0)
	{
		return;
	}
	const FTransform BaseTransform = Base->GetActorTransform();
	const FVector Centre = GetActorLocation();
	// The hologram turns about its own up axis: over a turn its equator's vertices sweep a circle, its poles stay put. The
	// beam must hold all of it at any moment, so it holds that orbit, grown by the margin and by half a point.
	const FVector Up = PlacedRotation.GetUpVector();
	const FVector Side = PlacedRotation.GetForwardVector();
	const FVector Side2 = PlacedRotation.GetRightVector();
	const float Outer = HalfSize + Tuning.BeamMargin + Tuning.PointSize;
	TArray<FVector> Orbit = { Centre + Up * Outer, Centre - Up * Outer };
	for (int32 K = 0; K < 180; ++K)
	{
		const float A = 2.f * PI * K / 180.f;
		Orbit.Add(Centre + (Side * FMath::Cos(A) + Side2 * FMath::Sin(A)) * Outer);
	}
	FRandomStream Rng(BeamSeed);
	TArray<FTransform> Transforms;
	for (int32 I = 0; I < Tuning.BeamRods; ++I)
	{
		const float Angle = FMath::DegreesToRadians(Tuning.BeamRodAngle + 360.f * I / Tuning.BeamRods);
		const FVector Local(Tuning.BeamRodRadius * FMath::Cos(Angle), Tuning.BeamRodRadius * FMath::Sin(Angle), Tuning.BeamRodTop);
		const FVector Start = BaseTransform.TransformPosition(Local);
		const FVector Axis = (Centre - Start).GetSafeNormal();
		const float ToCentre = FVector::Dist(Start, Centre);
		if (ToCentre < Outer + 1.f)
		{
			continue;  // a rod inside the hologram's orbit: no cone from it can hold the hologram
		}
		// The spread: the widest angle, seen from the tip, between the axis and any point of the orbit.
		float Spread = 0.f;
		for (const FVector& P : Orbit)
		{
			Spread = FMath::Max(Spread, FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (P - Start).GetSafeNormal()), -1.f, 1.f)));
		}
		Spread = FMath::Min(Spread, FMath::DegreesToRadians(80.f));
		const float Holding = Spread;
		Spread *= Tuning.BeamSpreadShare;
		// The far side: the farthest the orbit reaches along the axis.
		const float AlongUp = FMath::Abs(FVector::DotProduct(Axis, Up));
		const float Far = ToCentre + Outer * FMath::Max(AlongUp, FMath::Sqrt(FMath::Max(0.f, 1.f - AlongUp * AlongUp)));
		const float Length = Tuning.BeamLength * Far;
		const FVector End = Start + Axis * Length;
		const float Width = 2.f * Length * FMath::Tan(Spread);
		// The cone's apex (+Z of the mesh) on the rod's tip, its base toward the hologram.
		const FQuat Rotation = FRotationMatrix::MakeFromZ(Start - End).ToQuat();
		const FVector Scale(Width / ConeSize.X, Width / ConeSize.Y, Length / ConeSize.Z);
		const FVector Mid = (Start + End) * 0.5;
		const FVector Location = Mid - Rotation.RotateVector(Scale * ConeBox.GetCenter());
		Transforms.Emplace(Rotation, Location, Scale);
		BeamStarts.Add(Start);
		BeamEnds.Add(End);
		BeamSpreads.Add(FMath::RadiansToDegrees(Spread));
		BeamHolding.Add(FMath::RadiansToDegrees(Holding));
		BeamFars.Add(Far);
		BeamPhases.Add(FVector(Rng.FRandRange(0.f, 2.f * PI), Rng.FRandRange(0.f, 2.f * PI), Rng.FRandRange(0.f, 2.f * PI)));
		BeamLevels.Add(Tuning.BeamLevel);
	}
	Beams->AddInstances(Transforms, false, false, false);
	for (int32 I = 0; I < BeamLevels.Num(); ++I)
	{
		Beams->SetCustomDataValue(I, 0, BeamLevels[I], false);
	}
	// The fade runs along the cone mesh's own axis: from its apex (the top of its bounds) over its height.
	if (BeamMaterialInstance)
	{
		BeamMaterialInstance->SetScalarParameterValue(TEXT("ApexZ"), ConeBox.Max.Z);
		BeamMaterialInstance->SetScalarParameterValue(TEXT("LengthZ"), ConeSize.Z);
		BeamMaterialInstance->SetScalarParameterValue(TEXT("FadePower"), Tuning.BeamFadePower);
	}
	Beams->MarkRenderStateDirty();
}

void APrologueHologram::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BriefingProgress = ReadBriefingProgress();
	if (Points->IsVisible() && !bMotionFrozenForTest)
	{
		UpdateMotion(TimeState ? TimeState->GetRunSeconds() : GetWorld()->GetTimeSeconds());
	}
	else
	{
		UpdateRoute(RouteOverride >= 0.f ? RouteOverride : BriefingProgress);
	}
}

void APrologueHologram::UpdateMotion(double Seconds)
{
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();

	// The turn: constant speed, from the placed rotation.
	TurnDegrees = static_cast<float>(FMath::Fmod(360.0 * Seconds / Tuning.TurnSeconds, 360.0));
	Points->SetWorldRotation(PlacedRotation * FQuat(FVector::UpVector, FMath::DegreesToRadians(TurnDegrees)));

	// The drift: each point out along its face and back, in its own period and phase.
	if (PointTransforms.Num() == RestPositions.Num() && PointTransforms.Num() > 0 && Tuning.DriftDistance > 0.f)
	{
		for (int32 I = 0; I < PointTransforms.Num(); ++I)
		{
			const double S = FMath::Sin(2.0 * PI * Seconds / DriftPeriods[I] + DriftPhases[I]);
			PointTransforms[I].SetTranslation(RestPositions[I] + DriftVectors[I] * S);
		}
		Points->BatchUpdateInstancesTransforms(0, PointTransforms, false, true, false);
	}

	// The beams' flicker: three sines per beam in its own phases, a smooth wavering of the level.
	for (int32 I = 0; I < BeamLevels.Num(); ++I)
	{
		const double F = Tuning.BeamFlickerHz;
		const FVector& P = BeamPhases[I];
		const double N = 0.5 * FMath::Sin(2.0 * PI * F * Seconds + P.X) + 0.3 * FMath::Sin(2.0 * PI * F * 2.31 * Seconds + P.Y)
			+ 0.2 * FMath::Sin(2.0 * PI * F * 3.73 * Seconds + P.Z);
		BeamLevels[I] = FMath::Max(0.f, Tuning.BeamLevel * static_cast<float>(1.0 + Tuning.BeamFlicker * N));
		Beams->SetCustomDataValue(I, 0, BeamLevels[I], I == BeamLevels.Num() - 1);
	}

	UpdateRoute(RouteOverride >= 0.f ? RouteOverride : BriefingProgress);
}

void APrologueHologram::UpdateRoute(float Progress01)
{
	using namespace PrologueHologramImpl;
	Progress01 = FMath::Clamp(Progress01, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Progress01, RouteProgress) || RouteNodes.Num() < 2 || Route->GetInstanceCount() != RouteNodes.Num() * 2 - 1)
	{
		return;
	}
	RouteProgress = Progress01;
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	float Total = 0.f;
	TArray<float> Lengths;
	for (int32 I = 0; I + 1 < RouteNodes.Num(); ++I)
	{
		Lengths.Add(FVector::Dist(RouteNodes[I], RouteNodes[I + 1]));
		Total += Lengths.Last();
	}
	// Nothing before the briefing starts; node 1 the moment it does; then the lines and nodes in order.
	float Drawn = Progress01 * Total;
	const FTransform None(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	TArray<FTransform> Transforms;
	const FVector NodeScale(Tuning.RouteNodeSize / CubeMeshSize);
	RouteNodesShown = 0;
	Transforms.Add(Progress01 > 0.f ? FTransform(FQuat::Identity, RouteNodes[0], NodeScale) : None);
	RouteNodesShown += Progress01 > 0.f ? 1 : 0;
	for (int32 I = 0; I < Lengths.Num(); ++I)
	{
		const float Part = FMath::Clamp(Drawn, 0.f, Lengths[I]);
		Drawn -= Lengths[I];
		const FVector A = RouteNodes[I];
		const FVector B = RouteNodes[I + 1];
		if (Progress01 > 0.f && Part > 0.f)
		{
			const FVector Dir = (B - A).GetSafeNormal();
			const FVector End = A + Dir * Part;
			const FQuat Rotation = FRotationMatrix::MakeFromX(Dir).ToQuat();
			Transforms.Emplace(Rotation, (A + End) * 0.5, FVector(Part / CubeMeshSize, Tuning.RouteLineWidth / CubeMeshSize, Tuning.RouteLineWidth / CubeMeshSize));
		}
		else
		{
			Transforms.Add(None);
		}
		const bool bReached = Progress01 > 0.f && Part >= Lengths[I] - KINDA_SMALL_NUMBER;
		Transforms.Add(bReached ? FTransform(FQuat::Identity, B, NodeScale) : None);
		RouteNodesShown += bReached ? 1 : 0;
	}
	Route->BatchUpdateInstancesTransforms(0, Transforms, false, true, false);
}

float APrologueHologram::ReadBriefingProgress() const
{
	// With no briefing terminal in the world, nothing draws the route: it is shown whole.
	return Briefing ? Briefing->GetBriefingProgress01() : 1.f;
}

void APrologueHologram::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	ApplyVisibility();
}

void APrologueHologram::SetShownWhileStoppedForTest(bool bInShown)
{
	bShownWhileStoppedForTest = bInShown;
	ApplyVisibility();
}

void APrologueHologram::SetBeamsHiddenForTest(bool bInHidden)
{
	bBeamsHiddenForTest = bInHidden;
	ApplyVisibility();
}

void APrologueHologram::SetPartsHiddenForTest(bool bInPoints, bool bInRoute, bool bInBeams)
{
	bPointsHiddenForTest = bInPoints;
	bRouteHiddenForTest = bInRoute;
	bBeamsHiddenForTest = bInBeams;
	ApplyVisibility();
}

float APrologueHologram::BeamFade(float Share01, float Power)
{
	return FMath::Pow(1.f - FMath::Clamp(Share01, 0.f, 1.f), FMath::Max(Power, 1.f));
}

void APrologueHologram::SetRouteProgressOverride(float Progress01)
{
	RouteOverride = Progress01 >= 0.f ? FMath::Min(Progress01, 1.f) : -1.f;
	UpdateRoute(RouteOverride >= 0.f ? RouteOverride : BriefingProgress);
}

void APrologueHologram::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	ApplyVisibility();
}

void APrologueHologram::ApplyVisibility()
{
	const bool bShow = bEnabled && (bShownWhileStoppedForTest || !(TimeState && TimeState->IsStopped()));
	Points->SetVisibility(bShow && !bPointsHiddenForTest);
	Route->SetVisibility(bShow && !bRouteHiddenForTest);
	Beams->SetVisibility(bShow && !bBeamsHiddenForTest);
}

bool APrologueHologram::IsShown() const
{
	return Points && (Points->IsVisible() || Route->IsVisible() || Beams->IsVisible()) && GetPointCount() > 0;
}

int32 APrologueHologram::GetPointCount() const
{
	return Points ? Points->GetInstanceCount() : 0;
}

int32 APrologueHologram::GetBeamCount() const
{
	return BeamStarts.Num();
}

FBox APrologueHologram::GetPointsBox() const
{
	return Points ? Points->Bounds.GetBox() : FBox(ForceInit);
}

FVector APrologueHologram::GetRouteNodeWorld(int32 Index) const
{
	return RouteNodes.IsValidIndex(Index) ? Points->GetComponentTransform().TransformPosition(RouteNodes[Index]) : FVector::ZeroVector;
}

bool APrologueHologram::GetBeam(int32 Index, FVector& OutStart, FVector& OutEnd, float& OutSpreadDegrees, float& OutFarDistance, float& OutLevel) const
{
	if (!BeamStarts.IsValidIndex(Index))
	{
		return false;
	}
	OutStart = BeamStarts[Index];
	OutEnd = BeamEnds[Index];
	OutSpreadDegrees = BeamSpreads[Index];
	OutFarDistance = BeamFars[Index];
	OutLevel = BeamLevels[Index];
	return true;
}

bool APrologueHologram::GetPointLocal(int32 Index, FVector& OutRest, FVector& OutNow) const
{
	if (!RestPositions.IsValidIndex(Index) || !PointTransforms.IsValidIndex(Index))
	{
		return false;
	}
	OutRest = RestPositions[Index];
	OutNow = PointTransforms[Index].GetTranslation();
	return true;
}
