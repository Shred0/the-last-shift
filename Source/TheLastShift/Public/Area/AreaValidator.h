// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Outcome of one validator rule. */
struct THELASTSHIFT_API FAreaRuleResult
{
	int32 Rule = 0;
	FString Name;
	bool bPass = false;
	FString Summary;
	/** Failures with their locations (capped). */
	TArray<FString> Details;
};

struct THELASTSHIFT_API FAreaValidationResult
{
	TArray<FAreaRuleResult> Rules;
	/** Set when the area cannot be validated at all (no generator, info, navmesh, start or exit). */
	FString FatalError;
	double Seconds = 0.0;

	bool AllPassed() const;
	void Log(const TCHAR* Prefix, bool bDetails) const;
};

/**
 * Checks a generated area in World against the rules a hand-built level must meet, reading the recipe through the
 * map's AAreaGenerator (from UTheLastShiftSettings), the layout from its AAreaInfo, and the rest from the actors and the navmesh themselves:
 *
 * 1. A navigation path exists from the PlayerStart to the exit, with a length in the recipe's range.
 * 2. Every residue, terminal and door is reachable on the navmesh, and the counts match the recipe.
 * 3. Every gate is solvable and cannot be skipped: no navmesh route to the exit avoids the door and the door seals its
 *    passage; the terminal-to-door walk takes longer than the door's PoweredSeconds at the pawn's walk speed; a
 *    regenerating residue sits before the terminal; the energy reachable before the door pays for it.
 * 4. Closed segments: at a route sample every HidingSampleSpacing, a navmesh point with no line of sight from the
 *    pawn's eye exists at a path distance in the entity's spawn band (MinSpawnDistance +- SpawnDistanceTolerance);
 *    passes at HidingPassShare of the samples.
 * 5. Open segments: every floor edge is closed by a curb above the pawn's step height, or continues into floor.
 * 6. No walkable navmesh outside the generated route: every navmesh polygon lies on a generated floor and is
 *    connected to the start.
 */
namespace AreaValidator
{
	THELASTSHIFT_API FAreaValidationResult Validate(UWorld* World);
}

