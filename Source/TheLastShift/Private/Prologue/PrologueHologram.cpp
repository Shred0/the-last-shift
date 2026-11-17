// SPDX-License-Identifier: MIT

#include "Prologue/PrologueHologram.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueHologram)

namespace PrologueHologramImpl
{
	/** Size of the engine's basic cube mesh used for a point. */
	static constexpr float CubeMeshSize = 100.f;
	/** The cloud is the same every time it is built. */
	static constexpr int32 Seed = 1111;

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
}

APrologueHologram::APrologueHologram()
{
	PrimaryActorTick.bCanEverTick = false;
	Points = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Points"));
	SetRootComponent(Points);
	// Movable: the points are rebuilt at BeginPlay and shown or hidden with the time state; unlit, they need no baked light.
	Points->SetMobility(EComponentMobility::Movable);
	Points->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Points->SetCollisionResponseToAllChannels(ECR_Ignore);
	Points->SetGenerateOverlapEvents(false);
	Points->SetCanEverAffectNavigation(false);
	Points->SetCastShadow(false);
	Points->bHiddenInSceneCapture = true;
	Points->SetRenderCustomDepth(false);
	Points->bAffectDistanceFieldLighting = false;
	Points->SetCullDistances(0, 0);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Points->SetStaticMesh(CubeMesh.Object);
	}
}

void APrologueHologram::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void APrologueHologram::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &APrologueHologram::HandleTimeStateChanged);
	}
	ApplyVisibility();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %d points, %.0f cm across, colour %s (ScanColors.Scan), hidden in scene captures, no collision."), *GetName(),
		GetPointCount(), UTheLastShiftSettings::Get()->GetHologramTuning().Size, *PointColor.ToString());
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
	using namespace PrologueHologramImpl;
	const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
	PointColor = UTheLastShiftSettings::GetScanColors().Scan;

	if (PointMaterial)
	{
		if (!PointMaterialInstance || PointMaterialInstance->Parent != PointMaterial)
		{
			PointMaterialInstance = UMaterialInstanceDynamic::Create(PointMaterial, this);
		}
		PointMaterialInstance->SetVectorParameterValue(TEXT("Color"), PointColor);
		Points->SetMaterial(0, PointMaterialInstance);
	}

	TArray<FVector> Vertices;
	TArray<TPair<int32, int32>> Edges;
	TArray<FIntVector> Faces;
	Octahedron(Tuning.Size * 0.5f, Vertices, Edges, Faces);

	FRandomStream Rng(Seed);
	const int32 EdgePoints = FMath::RoundToInt(Tuning.Points * Tuning.EdgeShare);
	const FVector Scale(Tuning.PointSize / CubeMeshSize);
	TArray<FTransform> Transforms;
	Transforms.Reserve(Tuning.Points);
	for (int32 I = 0; I < Tuning.Points; ++I)
	{
		FVector P;
		if (I < EdgePoints)
		{
			// Evenly along each edge, with a little jitter so the edges do not read as dotted lines.
			const TPair<int32, int32>& Edge = Edges[I % Edges.Num()];
			const int32 PerEdge = FMath::Max(EdgePoints / Edges.Num(), 1);
			const float T = (FMath::Min(I / Edges.Num(), PerEdge - 1) + Rng.FRand()) / PerEdge;
			P = FMath::Lerp(Vertices[Edge.Key], Vertices[Edge.Value], T);
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
			P = Vertices[Face.X] + U * (Vertices[Face.Y] - Vertices[Face.X]) + V * (Vertices[Face.Z] - Vertices[Face.X]);
		}
		Transforms.Emplace(FQuat::Identity, P, Scale);
	}
	Points->ClearInstances();
	Points->AddInstances(Transforms, false, false, false);
}

void APrologueHologram::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	ApplyVisibility();
}

void APrologueHologram::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	ApplyVisibility();
}

void APrologueHologram::ApplyVisibility()
{
	const bool bShow = bEnabled && !(TimeState && TimeState->IsStopped());
	Points->SetVisibility(bShow);
}

bool APrologueHologram::IsShown() const
{
	return Points && Points->IsVisible() && GetPointCount() > 0;
}

int32 APrologueHologram::GetPointCount() const
{
	return Points ? Points->GetInstanceCount() : 0;
}

FBox APrologueHologram::GetPointsBox() const
{
	return Points ? Points->Bounds.GetBox() : FBox(ForceInit);
}
