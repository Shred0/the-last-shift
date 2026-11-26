// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/**
 * The entity's figure as a set of points in its local space: feet (the pivot) at the origin, facing +X, +Y to the
 * figure's left side, Z up. Two sources: the procedural humanoid (Build: points sampled uniformly on the surface of a set
 * of capsules, about 180 cm tall, the fallback) and a modelled form's mesh (BuildFromMesh, Brief 29: points spread evenly
 * by area over its surface). Both are deterministic for a given seed.
 */
struct THELASTSHIFT_API FEntityFigureShape
{
	/** Height of the procedural figure's head centre above the feet: the pivot the refraction copies rotate around. */
	static constexpr float HeadHeight = 166.f;

	static FVector GetHeadPivot() { return FVector(0.f, 0.f, HeadHeight); }

	/** Replaces OutPoints with Count surface points of the procedural humanoid. */
	static void Build(int32 Count, int32 Seed, TArray<FVector>& OutPoints);

	/**
	 * Replaces OutPoints with Count points spread evenly by area over Mesh's surface (LOD 0, the mesh's own space in cm):
	 * candidates drawn by area with Seed, then thinned by farthest-point sampling, so that the same mesh, count and seed
	 * always give the same points. Computed once and kept for later calls. False (OutPoints empty) when the mesh's
	 * triangles cannot be read (no mesh, no render data, or its CPU copy discarded in a cooked build).
	 */
	static bool BuildFromMesh(const UStaticMesh* Mesh, int32 Count, int32 Seed, TArray<FVector>& OutPoints);

	/** LOD 0's vertices and triangle indices of Mesh in its own space (cm); false when they cannot be read. */
	static bool ReadMeshTriangles(const UStaticMesh* Mesh, TArray<FVector>& OutVertices, TArray<uint32>& OutIndices);

	/** Distance from Point to the nearest triangle of a mesh read by ReadMeshTriangles (tests). */
	static float DistanceToMesh(const FVector& Point, const TArray<FVector>& Vertices, const TArray<uint32>& Indices);
};
