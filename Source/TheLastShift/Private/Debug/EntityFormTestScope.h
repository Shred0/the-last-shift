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
		if (--S.Depth == 0)
		{
			AEntity::SetSessionForm(S.Saved);
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the entity's figure takes %s again."), Test, S.Saved.IsSet() ? LexToString(S.Saved.GetValue()) : TEXT("each act's form"));
			S.Saved.Reset();
		}
	}
}

#endif // !UE_BUILD_SHIPPING
