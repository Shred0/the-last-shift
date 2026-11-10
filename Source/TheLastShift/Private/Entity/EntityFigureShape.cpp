// SPDX-License-Identifier: MIT

#include "Entity/EntityFigureShape.h"

#include "Math/RandomStream.h"

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
