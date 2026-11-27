// SPDX-License-Identifier: MIT

#pragma once

// The tests that measure a frame's time or a cost with no cap (tls.Views, tls.Links.CostRun, tls.EntityFound.Cost) lift
// the player's frame-rate limit and V-Sync for their run through StdCore's settings (a suspension that is never saved)
// and put them back at the end. Development builds only.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Settings/StdSettingsSubsystem.h"
#include "TheLastShift.h"

namespace FrameCapsTest
{
	inline UStdSettingsSubsystem* Settings(UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UStdSettingsSubsystem>() : nullptr;
	}

	inline int32 EngineInt(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetInt() : -1;
	}

	/** No frame-rate limit and no V-Sync for the test's run; the player's settings are not changed. */
	inline void Lift(UWorld* World, const TCHAR* Test)
	{
		if (UStdSettingsSubsystem* S = Settings(World))
		{
			S->SetFrameCapsSuspended(true);
			UE_LOG(LogTheLastShift, Display, TEXT("%s: the frame-rate limit is lifted for the run (the player's: %d%s, V-Sync %s; engine now: t.MaxFPS %d, r.VSync %d)."), Test,
				S->GetFrameRateLimit(), S->IsFrameRateUnlimited() ? TEXT(", unlimited") : TEXT(""), S->IsVSyncEnabled() ? TEXT("on") : TEXT("off"),
				EngineInt(TEXT("t.MaxFPS")), EngineInt(TEXT("r.VSync")));
		}
		else
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: no StdCore settings subsystem: the frame-rate limit is not lifted."), Test);
		}
	}

	/** The player's frame-rate limit and V-Sync back. */
	inline void Restore(UWorld* World, const TCHAR* Test)
	{
		UStdSettingsSubsystem* S = Settings(World);
		if (S && S->AreFrameCapsSuspended())
		{
			S->SetFrameCapsSuspended(false);
			UE_LOG(LogTheLastShift, Display, TEXT("%s: the player's frame-rate limit is back (t.MaxFPS %d, r.VSync %d)."), Test,
				EngineInt(TEXT("t.MaxFPS")), EngineInt(TEXT("r.VSync")));
		}
	}
}

#endif // !UE_BUILD_SHIPPING
