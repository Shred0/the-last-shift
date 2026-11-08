// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "TimeState.generated.h"

/** Whether time is flowing or stopped by the player. Never called "pause": that word belongs to the menu. */
UENUM(BlueprintType)
enum class ETimeState : uint8
{
	Normal,
	Stopped
};
