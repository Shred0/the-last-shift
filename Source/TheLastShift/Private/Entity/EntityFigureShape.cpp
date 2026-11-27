// SPDX-License-Identifier: MIT

#include "Entity/EntityFigureShape.h"

#include "Algo/BinarySearch.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"
#include "StaticMeshResources.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace EntityFigure
{
	struct FCapsule
	{
		FVector A;
		FVector B;
		float Radius;
	};

	/** Body parts, in cm. Paired limbs are mirrored on Y. */
	static void GetCapsules(TArray<FCapsule>& Out)
	{
		Out.Reset();
		Out.Add({ FVector(0.f, 0.f, 163.f), FVector(0.f, 0.f, 170.f), 10.5f });   // head
		Out.Add({ FVector(0.f, 0.f, 147.f), FVector(0.f, 0.f, 154.f), 5.f });     // neck
		for (const float Side : { -1.f, 1.f })
		{
			Out.Add({ FVector(0.f, 8.f * Side, 120.f), FVector(0.f, 8.f * Side, 140.f), 11.5f }); // chest half
			Out.Add({ FVector(0.f, 7.f * Side, 99.f), FVector(0.f, 7.f * Side, 114.f), 10.5f });  // pelvis half
			Out.Add({ FVector(0.f, 21.f * Side, 141.f), FVector(2.f, 24.f * Side, 113.f), 4.8f }); // upper arm
			Out.Add({ FVector(2.f, 24.f * Side, 113.f), FVector(6.f, 26.f * Side, 87.f), 4.f });   // forearm
			Out.Add({ FVector(0.f, 10.f * Side, 93.f), FVector(2.f, 11.f * Side, 51.f), 7.f });    // thigh
			Out.Add({ FVector(2.f, 11.f * Side, 51.f), FVector(0.f, 11.f * Side, 9.f), 5.3f });    // shin
			Out.Add({ FVector(-2.f, 11.f * Side, 4.f), FVector(14.f, 11.f * Side, 4.f), 4.f });    // foot
		}
	}

	static float GetArea(const FCapsule& Capsule)
	{
		const float Length = FVector::Dist(Capsule.A, Capsule.B);
		return 2.f * UE_PI * Capsule.Radius * Length + 4.f * UE_PI * Capsule.Radius * Capsule.Radius;
	}

	/** A point uniformly distributed on the capsule's surface. */
	static FVector SamplePoint(const FCapsule& Capsule, FRandomStream& Rng)
	{
		const FVector AxisVector = Capsule.B - Capsule.A;
		const float Length = AxisVector.Size();
		const FVector Axis = Length > UE_KINDA_SMALL_NUMBER ? AxisVector / Length : FVector::UpVector;

		const float CylinderArea = 2.f * UE_PI * Capsule.Radius * Length;
		const float SphereArea = 4.f * UE_PI * Capsule.Radius * Capsule.Radius;
		if (Rng.FRand() * (CylinderArea + SphereArea) < CylinderArea)
		{
			FVector Tangent, Bitangent;
			Axis.FindBestAxisVectors(Tangent, Bitangent);
			const float Angle = Rng.FRand() * 2.f * UE_PI;
			const FVector Radial = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);
			return Capsule.A + Axis * (Rng.FRand() * Length) + Radial * Capsule.Radius;
		}

		// Caps: a direction on the sphere; the half facing away from B sits on A, the other half on B.
		const FVector Direction = Rng.GetUnitVector();
		const FVector& Centre = FVector::DotProduct(Direction, Axis) < 0.f ? Capsule.A : Capsule.B;
		return Centre + Direction * Capsule.Radius;
	}
}

void FEntityFigureShape::Build(int32 Count, int32 Seed, TArray<FVector>& OutPoints)
{
	using namespace EntityFigure;

	OutPoints.Reset(FMath::Max(Count, 0));
	if (Count <= 0)
	{
		return;
	}

	TArray<FCapsule> Capsules;
	GetCapsules(Capsules);

	TArray<float> CumulativeArea;
	float TotalArea = 0.f;
	for (const FCapsule& Capsule : Capsules)
	{
		TotalArea += GetArea(Capsule);
		CumulativeArea.Add(TotalArea);
	}

	FRandomStream Rng(Seed);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Pick = Rng.FRand() * TotalArea;
		int32 Part = 0;
		while (Part < CumulativeArea.Num() - 1 && CumulativeArea[Part] < Pick)
		{
			++Part;
		}
		OutPoints.Add(SamplePoint(Capsules[Part], Rng));
	}
}

bool FEntityFigureShape::ReadMeshTriangles(const UStaticMesh* Mesh, TArray<FVector>& OutVertices, TArray<uint32>& OutIndices)
{
	OutVertices.Reset();
	OutIndices.Reset();
	if (!Mesh)
	{
		return false;
	}
#if WITH_EDITOR
	if (Mesh->IsCompiling())
	{
		FStaticMeshCompilingManager::Get().FinishCompilation({ const_cast<UStaticMesh*>(Mesh) });
	}
#endif
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		return false;
	}
	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
	// A cooked mesh without bAllowCPUAccess keeps no CPU copy: nothing to read.
	if (Positions.GetNumVertices() == 0 || !Positions.GetVertexData() || Indices.Num() < 3)
	{
		return false;
	}
	OutVertices.Reserve(Positions.GetNumVertices());
	for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
	{
		OutVertices.Add(FVector(Positions.VertexPosition(Index)));
	}
	OutIndices.Reserve(Indices.Num());
	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const uint32 A = Indices[Index], B = Indices[Index + 1], C = Indices[Index + 2];
		if (A < Positions.GetNumVertices() && B < Positions.GetNumVertices() && C < Positions.GetNumVertices())
		{
			OutIndices.Append({ A, B, C });
		}
	}
	return OutIndices.Num() >= 3;
}

bool FEntityFigureShape::BuildFromMesh(const UStaticMesh* Mesh, int32 Count, int32 Seed, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (!Mesh || Count <= 0)
	{
		return false;
	}

	// Computed once for a mesh, count and seed (game thread only).
	static TMap<FString, TArray<FVector>> Cache;
	const FString Key = FString::Printf(TEXT("%s|%d|%d"), *Mesh->GetPathName(), Count, Seed);
	if (const TArray<FVector>* Cached = Cache.Find(Key))
	{
		OutPoints = *Cached;
		return true;
	}

	TArray<FVector> Vertices;
	TArray<uint32> Indices;
	if (!ReadMeshTriangles(Mesh, Vertices, Indices))
	{
		return false;
	}

	TArray<double> Cumulative;
	Cumulative.Reserve(Indices.Num() / 3);
	double Total = 0.0;
	for (int32 Index = 0; Index < Indices.Num(); Index += 3)
	{
		const FVector& A = Vertices[Indices[Index]];
		Total += 0.5 * FVector::CrossProduct(Vertices[Indices[Index + 1]] - A, Vertices[Indices[Index + 2]] - A).Size();
		Cumulative.Add(Total);
	}
	if (Total <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Candidates drawn uniformly by area, several for each point kept.
	constexpr int32 Oversample = 6;
	const int32 CandidateCount = Count * Oversample;
	TArray<FVector> Candidates;
	Candidates.Reserve(CandidateCount);
	FRandomStream Rng(Seed);
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const double Pick = Rng.FRand() * Total;
		const int32 Triangle = FMath::Min(Algo::LowerBound(Cumulative, Pick), Cumulative.Num() - 1);
		const FVector& A = Vertices[Indices[Triangle * 3]];
		const FVector& B = Vertices[Indices[Triangle * 3 + 1]];
		const FVector& C = Vertices[Indices[Triangle * 3 + 2]];
		const float R1 = FMath::Sqrt(Rng.FRand());
		const float R2 = Rng.FRand();
		Candidates.Add(A * (1.f - R1) + B * (R1 * (1.f - R2)) + C * (R1 * R2));
	}

	// Farthest-point thinning: each point kept is the candidate farthest from those kept before, so the points are even.
	TArray<float> Nearest;
	Nearest.Init(TNumericLimits<float>::Max(), Candidates.Num());
	OutPoints.Reserve(Count);
	int32 Next = 0;
	for (int32 Kept = 0; Kept < Count; ++Kept)
	{
		const FVector Chosen = Candidates[Next];
		OutPoints.Add(Chosen);
		float Farthest = -1.f;
		int32 FarthestIndex = 0;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const float Distance = static_cast<float>(FVector::DistSquared(Candidates[Index], Chosen));
			if (Distance < Nearest[Index])
			{
				Nearest[Index] = Distance;
			}
			if (Nearest[Index] > Farthest)
			{
				Farthest = Nearest[Index];
				FarthestIndex = Index;
			}
		}
		Next = FarthestIndex;
	}

	Cache.Add(Key, OutPoints);
	return true;
}

float FEntityFigureShape::DistanceToMesh(const FVector& Point, const TArray<FVector>& Vertices, const TArray<uint32>& Indices)
{
	double Best = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Point, Vertices[Indices[Index]], Vertices[Indices[Index + 1]], Vertices[Indices[Index + 2]]);
		Best = FMath::Min(Best, FVector::DistSquared(Closest, Point));
	}
	return Best == TNumericLimits<double>::Max() ? -1.f : static_cast<float>(FMath::Sqrt(Best));
}
