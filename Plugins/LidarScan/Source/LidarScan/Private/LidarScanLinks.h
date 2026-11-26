// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"

/**
 * Neighbour search of one scan, run on a worker thread on the samples the point system spawned, read back from the GPU
 * (the sample target, layout in the contract of LidarScanComponent.cpp). Two ways to find the links:
 *  - nearest (bGrid false): each live sample keeps its Neighbours nearest live samples of the same channel within
 *    MaxDistance and within the normal angle limit, looked for among the samples whose texels are close in the capture
 *    image; a link joins two samples when each is among the other's nearest (mutual nearest neighbours), so no sample
 *    has more than Neighbours links;
 *  - grid (bGrid true): each live sample is linked to the next live sample to its right in its row of the capture and
 *    to the next live sample below it in its column, looked for over at most GridReach texels, when that sample is of
 *    the same channel, within MaxDistance and within the normal angle limit; Neighbours only says that lines are on.
 *    No sample has more than 4 links.
 * In both, each link is listed once, from its lower texel, in texel order, and the cap keeps the first MaxLinks.
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
	/** The search on the capture's grid instead of the mutual nearest neighbours. */
	bool bGrid = false;
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
	/** Texel of the first point whose link the cap dropped (the lower texel of that link), -1 when none was dropped. */
	int32 FirstDroppedTexel = INDEX_NONE;
	int32 MaxDegree = 0;
	float Longest = 0.f;
	double Milliseconds = 0.0;
};

namespace LidarScanLinks
{
	/** The value the sample target is cleared to before each scan: a W below every channel and below "no point" (-1). */
	inline constexpr float UnwrittenW = -2.f;

	/** Most texels the grid search looks along a row or a column for the next live sample. */
	inline constexpr int32 MaxGridReach = 64;

	/**
	 * Texels the grid search looks along a row or a column from a sample at SampleDepth: 1 (the next texel only, a missing
	 * sample there is a hole) where the near thinning keeps every sample, and 1 + 3 (Thin^2 - 1) nearer, Thin =
	 * NearThinningDistance / SampleDepth, since the thinning keeps one sample in Thin^2 there; at most MaxGridReach.
	 */
	int32 GridReach(float SampleDepth, float NearThinningDistance);

	/** Runs the search. Thread safe: reads only Input, writes only Output. */
	void Find(const FLidarLinkSearchInput& Input, FLidarLinkSearchOutput& Output);
}
