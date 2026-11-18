// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#include "LidarScanLinks.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"

namespace LidarScanLinks
{
	/** One captured sample, as the point system spawns it. */
	struct FSample
	{
		FVector Surface = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		float Depth = 0.f;
		uint8 Channel = 0;
		bool bAlive = false;
	};

	/** Maximum neighbours a sample can keep (the component clamps Neighbours to this). */
	static constexpr int32 MaxNeighbours = 8;

	/** Window half-size in texels; widened near the capture, where the near thinning keeps few samples. */
	static int32 WindowRadius(float SampleDepth, float NearThinningDistance)
	{
		constexpr int32 Base = 3;
		if (NearThinningDistance <= 0.f || SampleDepth >= NearThinningDistance)
		{
			return Base;
		}
		return FMath::Clamp(FMath::CeilToInt32(Base * NearThinningDistance / FMath::Max(SampleDepth, 1.f)), Base, MaxNeighbours);
	}

}

void LidarScanLinks::Find(const FLidarLinkSearchInput& In, FLidarLinkSearchOutput& Out)
{
	const double Start = FPlatformTime::Seconds();
	Out = FLidarLinkSearchOutput();
	const int32 W = In.Width;
	const int32 H = In.Height;
	const int32 Count = W * H;
	const int32 K = FMath::Clamp(In.Neighbours, 0, MaxNeighbours);
	if (W <= 0 || H <= 0 || In.Samples.Num() < Count || In.Normals.Num() < Count || K <= 0 || In.MaxDistance <= 0.f)
	{
		return;
	}

	// The samples as the point system spawned them (read back from its sample target).
	TArray<FSample> Samples;
	Samples.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector4f& Sample = In.Samples[Index];
		FSample& S = Samples[Index];
		if (Sample.W <= LidarScanLinks::UnwrittenW + 0.5f)
		{
			++Out.Unwritten;
			continue;
		}
		S.bAlive = Sample.W >= 0.f;
		if (!S.bAlive)
		{
			continue;
		}
		S.Surface = FVector(Sample.X, Sample.Y, Sample.Z);
		S.Normal = FVector(In.Normals[Index]);
		S.Channel = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Sample.W), 0, 255));
		S.Depth = static_cast<float>(FVector::DotProduct(S.Surface - In.Origin, In.Forward));
	}

	// The K nearest samples of each sample (same channel, within MaxDistance and the normal limit), nearest first.
	TArray<int32> Nearest;
	Nearest.Init(INDEX_NONE, Count * K);
	const double MaxDistSq = FMath::Square(static_cast<double>(In.MaxDistance));
	ParallelFor(H, [&In, &Samples, &Nearest, W, H, K, MaxDistSq](int32 Row)
	{
		double BestDist[MaxNeighbours];
		int32 Best[MaxNeighbours];
		for (int32 X = 0; X < W; ++X)
		{
			const int32 A = Row * W + X;
			const FSample& SA = Samples[A];
			if (!SA.bAlive)
			{
				continue;
			}
			int32 Found = 0;
			const int32 R = WindowRadius(SA.Depth, In.NearThinningDistance);
			for (int32 Y = FMath::Max(Row - R, 0); Y <= FMath::Min(Row + R, H - 1); ++Y)
			{
				for (int32 BX = FMath::Max(X - R, 0); BX <= FMath::Min(X + R, W - 1); ++BX)
				{
					const int32 B = Y * W + BX;
					const FSample& SB = Samples[B];
					if (B == A || !SB.bAlive || SB.Channel != SA.Channel)
					{
						continue;
					}
					const double DistSq = FVector::DistSquared(SA.Surface, SB.Surface);
					if (DistSq > MaxDistSq || FVector::DotProduct(SA.Normal, SB.Normal) < In.MinNormalDot)
					{
						continue;
					}
					// Insert, nearest first; ties keep the lower texel index first (deterministic).
					int32 Slot = Found;
					while (Slot > 0 && (DistSq < BestDist[Slot - 1] || (DistSq == BestDist[Slot - 1] && B < Best[Slot - 1])))
					{
						if (Slot < K)
						{
							BestDist[Slot] = BestDist[Slot - 1];
							Best[Slot] = Best[Slot - 1];
						}
						--Slot;
					}
					if (Slot < K)
					{
						BestDist[Slot] = DistSq;
						Best[Slot] = B;
						Found = FMath::Min(Found + 1, K);
					}
				}
			}
			for (int32 Index = 0; Index < Found; ++Index)
			{
				Nearest[A * K + Index] = Best[Index];
			}
		}
	});

	Out.PointDepths.Init(-1.f, Count);
	Out.Degrees.Init(0, Count);
	for (int32 A = 0; A < Count; ++A)
	{
		if (Samples[A].bAlive)
		{
			++Out.Points;
			Out.PointDepths[A] = Samples[A].Depth;
		}
	}

	// Mutual nearest neighbours, each link once (from its lower texel), in texel order; the cap cuts the rest.
	const int32 Cap = FMath::Max(In.MaxLinks, 0);
	for (int32 A = 0; A < Count; ++A)
	{
		for (int32 I = 0; I < K; ++I)
		{
			const int32 B = Nearest[A * K + I];
			if (B == INDEX_NONE)
			{
				break;
			}
			if (B < A)
			{
				continue;
			}
			bool bMutual = false;
			for (int32 J = 0; J < K && !bMutual; ++J)
			{
				bMutual = Nearest[B * K + J] == A;
			}
			if (!bMutual)
			{
				continue;
			}
			++Out.Found;
			if (Out.Starts.Num() >= Cap)
			{
				++Out.Dropped;
				continue;
			}
			const FSample& SA = Samples[A];
			const FSample& SB = Samples[B];
			Out.Starts.Add(SA.Surface);
			Out.Ends.Add(SB.Surface);
			Out.StartNormals.Add(FLinearColor(SA.Normal.X, SA.Normal.Y, SA.Normal.Z, SA.Channel));
			Out.EndNormals.Add(FLinearColor(SB.Normal.X, SB.Normal.Y, SB.Normal.Z, SB.Channel));
			Out.StartTexels.Add(A);
			Out.EndTexels.Add(B);
			Out.Longest = FMath::Max(Out.Longest, static_cast<float>(FVector::Dist(SA.Surface, SB.Surface)));
			Out.MaxDegree = FMath::Max(Out.MaxDegree, static_cast<int32>(++Out.Degrees[A]));
			Out.MaxDegree = FMath::Max(Out.MaxDegree, static_cast<int32>(++Out.Degrees[B]));
		}
	}
	Out.Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
}
