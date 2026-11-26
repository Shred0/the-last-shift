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
 * 7. Log and twin AI terminals: their counts match the recipe; each has a standing place on the navmesh within its
 *    reach, free for the pawn (a log terminal: its reading place 1 to 1.5 m in front of the screen, nothing between the
 *    eye and the screen, in a closed corridor or room of its own, one per equal part of the route; the twin AI's
 *    terminal: in the last closed segment); no place is within reach of two terminals of any kind; the exit is reached
 *    without paying any of them.
 * 8. Beacons: their count matches the recipe, one per Hall segment in route order; each hangs above its segment, clear
 *    of every floor and catwalk by at least BeaconHeight less a margin; its one spot light is aimed at the point where the
 *    route leaves the segment and reaches it, and nothing stands on the line from the light to BeaconClearPathHeight
 *    above that point; its rod ends in the first geometry above or has
 *    the maximum length; the body blocks nothing and changes no navigation; its terminal stands at the entrance and can
 *    be reached and paid before the space is crossed, within reach of no other terminal; the exit is reached without
 *    paying any beacon.
 * 9. The way out (Brief 26): exactly one exit corridor, the route's last segment, straight and closed; the exit lies in it
 *    at the recipe's ExitTriggerDepthM from its mouth across its whole section, the corridor runs ExitRunLengthM past it
 *    and holds nothing; with the recipe's twin AI's room, the room is the segment right before it, the corridor starts
 *    from one of its side walls and the twin AI's terminal stands in it on the wall that faces its entrance. Rule 4 leaves
 *    the exit corridor out, rule 7 reads the twin AI's terminal in that room.
 */
namespace AreaValidator
{
	THELASTSHIFT_API FAreaValidationResult Validate(UWorld* World);
}

