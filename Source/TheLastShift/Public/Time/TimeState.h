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

/** Why a Stop ended. Carried by OnTimeStateChanged (meaningful when leaving Stopped) and the scan director's OnStopExited. */
UENUM(BlueprintType)
enum class EStopExitReason : uint8
{
	/** The player resumed time. */
	Resumed,
	/** The player was sent back to where the Stop began. */
	Rewound,
	/** Time resumed where the player stands, and the outside world jumps forward. */
	Jumped,
	/** The player was sent back to where the Stop began, and the outside world jumps forward. */
	RewoundAndJumped
};

/** True for the exit reasons that send the player back to the Stop anchor. */
inline bool IsRewindExit(EStopExitReason Reason)
{
	return Reason == EStopExitReason::Rewound || Reason == EStopExitReason::RewoundAndJumped;
}

/** True for the exit reasons that make the outside world jump forward. */
inline bool IsJumpExit(EStopExitReason Reason)
{
	return Reason == EStopExitReason::Jumped || Reason == EStopExitReason::RewoundAndJumped;
}

inline const TCHAR* LexToString(EStopExitReason Reason)
{
	switch (Reason)
	{
	case EStopExitReason::Resumed: return TEXT("Resumed");
	case EStopExitReason::Rewound: return TEXT("Rewound");
	case EStopExitReason::Jumped: return TEXT("Jumped");
	case EStopExitReason::RewoundAndJumped: return TEXT("RewoundAndJumped");
	}
	return TEXT("Unknown");
}
