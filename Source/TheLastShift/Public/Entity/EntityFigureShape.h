// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Procedural humanoid point cloud for the entity's figure: points sampled uniformly on the surface of a set of
 * capsules (head, neck, chest, pelvis, upper and lower arms and legs, feet). Local space: feet at the origin, facing
 * +X, +Y to the figure's left side, Z up, about 180 cm tall. Deterministic for a given seed.
 */
struct THELASTSHIFT_API FEntityFigureShape
{
	/** Height of the head centre above the feet: the pivot the refraction copies rotate around. */
	static constexpr float HeadHeight = 166.f;

	static FVector GetHeadPivot() { return FVector(0.f, 0.f, HeadHeight); }

	/** Replaces OutPoints with Count surface points. */
	static void Build(int32 Count, int32 Seed, TArray<FVector>& OutPoints);
};
