// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"

/**
 * Neighbour search of one scan, run on a worker thread on the samples the point system spawned, read back from the GPU
 * (the sample target, layout in the contract of LidarScanComponent.cpp). Each live sample keeps its Neighbours nearest
 * live samples of the same channel within MaxDistance and within the normal angle limit, looked for among the samples
 * whose texels are close in the capture image; a link joins two samples when each is among the other's nearest (mutual
 * nearest neighbours), so no sample has more than Neighbours links.
 */
struct FLidarLinkSearchInput
{
	int32 Width = 0;
	int32 Height = 0;
	/** Per texel: the sample's surface position (xyz) and its channel (w), w < 0 when the texel spawned no live point. */
	TArray<FVector4f> Samples;
	/** Per texel: the sample's surface normal. */
	TArray<FVector3f> Normals;

	FVector Origin = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float NearThinningDistance = 0.f;

	int32 Neighbours = 0;
	float MaxDistance = 0.f;
	/** Cosine of the largest angle between the two points' normals; below -1 = no limit. */
	float MinNormalDot = -2.f;
	int32 MaxLinks = 0;
};

struct FLidarLinkSearchOutput
{
	/** Per link: the two points' surface positions (no draw offset) and normals with their channel in A. */
	TArray<FVector> Starts;
	TArray<FVector> Ends;
	TArray<FLinearColor> StartNormals;
	TArray<FLinearColor> EndNormals;
	/** Per link: texel index of each end (diagnostics). */
	TArray<int32> StartTexels;
	TArray<int32> EndTexels;

	/** Per texel: depth of the point along the capture's forward axis, -1 where the texel has no live point. */
	TArray<float> PointDepths;
	/** Per texel: links of the point (0 where there is no point). */
	TArray<uint8> Degrees;

	int32 Points = 0;
	/** Texels the spawn did not write (their sample still holds the clear value). */
	int32 Unwritten = 0;
	/** Links found, kept and dropped over the cap. */
	int32 Found = 0;
	int32 Dropped = 0;
	int32 MaxDegree = 0;
	float Longest = 0.f;
	double Milliseconds = 0.0;
};

namespace LidarScanLinks
{
	/** The value the sample target is cleared to before each scan: a W below every channel and below "no point" (-1). */
	inline constexpr float UnwrittenW = -2.f;

	/** Runs the search. Thread safe: reads only Input, writes only Output. */
	void Find(const FLidarLinkSearchInput& Input, FLidarLinkSearchOutput& Output);
}
