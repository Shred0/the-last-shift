// SPDX-License-Identifier: MIT

#pragma once

// Brief 29: the tests that measure the entity's figure force area 1's form for their run and put the session's form back
// at the end, so that their poses and counts keep meaning what they meant before the forms (area 1's form keeps the
// procedural figure's 1000 points). Development builds only.

#include "CoreMinimal.h"
#include "Entity/Entity.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"

#if !UE_BUILD_SHIPPING

// Brief 30: the tests that measure the entity or walk a route while Stopped switch the entity's walls off for their run and
// put the session's switch back at the end (ForceAreaOneForm and Restore do it too), so that their poses and counts keep
// their meaning.
namespace EntityWallsTest
{
	struct FState
	{
		int32 Depth = 0;
		TOptional<bool> Saved;
	};

	inline FState& State()
	{
		static FState Instance;
		return Instance;
	}

	/** Every entity raises no wall until Restore; nested calls keep the first saved switch. */
	inline void SwitchOff(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth++ == 0)
		{
			S.Saved = AEntity::GetSessionWalls();
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's walls are off for the run."), Test);
		}
		AEntity::SetSessionWalls(false);
	}

	/** Puts back the session's switch saved by the first SwitchOff. No-op without one. */
	inline void Restore(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth <= 0)
		{
			return;
		}
		if (--S.Depth == 0)
		{
			AEntity::SetSessionWalls(S.Saved);
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's walls are %s again."), Test, S.Saved.IsSet() ? (S.Saved.GetValue() ? TEXT("on (session)") : TEXT("off (session)")) : TEXT("as the settings say"));
			S.Saved.Reset();
		}
	}
}

// Brief 47: the tests that measure the figure hold it still: ForceAreaOneForm and Restore set the figure's tremor and pulse
// to 0 for their run and put the session's values back at the end, so that their poses, counts and levels keep their
// meaning. tls.EntityFormCheck does not use the scope's form: it measures the figure trembling, as the player sees it.
namespace EntityMotionTest
{
	struct FState
	{
		int32 Depth = 0;
		TOptional<FVector4f> Saved;
	};

	inline FState& State()
	{
		static FState Instance;
		return Instance;
	}

	/** Every entity's figure stands still until Restore; nested calls keep the first saved values. */
	inline void HoldStill(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth++ == 0)
		{
			S.Saved = AEntity::GetSessionFigureMotion();
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's figure stands still for the run (tremor and pulse 0)."), Test);
		}
		AEntity::SetSessionFigureMotion(FVector4f(0.f, 0.f, 0.f, 0.f));
	}

	/** Puts back the session's values saved by the first HoldStill. No-op without one. */
	inline void Restore(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth <= 0)
		{
			return;
		}
		if (--S.Depth == 0)
		{
			AEntity::SetSessionFigureMotion(S.Saved);
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's figure trembles and pulses %s again."), Test, S.Saved.IsSet() ? TEXT("at the session's values") : TEXT("as the settings say"));
			S.Saved.Reset();
		}
	}
}

namespace EntityFormTest
{
	struct FState
	{
		int32 Depth = 0;
		TOptional<EEntityForm> Saved;
	};

	inline FState& State()
	{
		static FState Instance;
		return Instance;
	}

	/** Every entity takes area 1's form (ActTuning's first EntityForm) until Restore; nested calls keep the first saved form. */
	inline void ForceAreaOneForm(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth++ == 0)
		{
			S.Saved = AEntity::GetSessionForm();
		}
		const EEntityForm Form = UTheLastShiftSettings::Get()->GetEntityFormOfAct(1);
		AEntity::SetSessionForm(Form);
		EntityWallsTest::SwitchOff(Test);
		EntityMotionTest::HoldStill(Test);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's figure takes area 1's form (%s) for the run."), Test, LexToString(Form));
	}

	/** Puts back the session's form saved by the first ForceAreaOneForm (unset: each act's own). No-op without one. */
	inline void Restore(const TCHAR* Test)
	{
		FState& S = State();
		if (S.Depth <= 0)
		{
			return;
		}
		EntityWallsTest::Restore(Test);
		EntityMotionTest::Restore(Test);
		if (--S.Depth == 0)
		{
			AEntity::SetSessionForm(S.Saved);
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's figure takes %s again."), Test, S.Saved.IsSet() ? LexToString(S.Saved.GetValue()) : TEXT("each act's form"));
			S.Saved.Reset();
		}
	}
}

#endif // !UE_BUILD_SHIPPING
