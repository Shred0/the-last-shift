// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Time/HomeStateSubsystem.h"
#include "UObject/Object.h"
#include "HomeTestListener.generated.h"

/** Counts the home state's events for tls.HomeTest (reflected, so it exists in every configuration; used in development builds only). */
UCLASS(Transient)
class UTlsHomeTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UHomeStateSubsystem* Home)
	{
		Unbind();
		if (Home)
		{
			Bound = Home;
			Home->OnHomeJump.AddUniqueDynamic(this, &UTlsHomeTestListener::HandleJump);
			Home->OnRunLost.AddUniqueDynamic(this, &UTlsHomeTestListener::HandleLost);
		}
	}

	void Unbind()
	{
		if (UHomeStateSubsystem* Home = Bound.Get())
		{
			Home->OnHomeJump.RemoveDynamic(this, &UTlsHomeTestListener::HandleJump);
			Home->OnRunLost.RemoveDynamic(this, &UTlsHomeTestListener::HandleLost);
		}
		Bound.Reset();
	}

	int32 Jumps = 0;
	int32 Losses = 0;
	FHomeJump LastJump;

private:
	UFUNCTION()
	void HandleJump(const FHomeJump& Jump)
	{
		++Jumps;
		LastJump = Jump;
	}

	UFUNCTION()
	void HandleLost()
	{
		++Losses;
	}

	TWeakObjectPtr<UHomeStateSubsystem> Bound;
};
