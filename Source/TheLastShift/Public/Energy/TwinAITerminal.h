// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PowerTerminal.h"
#include "TwinAITerminal.generated.h"

/**
 * The twin AI's terminal: each payment (Cost, in Normal time, like any terminal) plays the next part of what the twin AI
 * says. The part is stored in the suit's archive at the payment (URunSubsystem::StoreTwinPart); the home AI component,
 * the only pusher of the text channel, then queues the spending comment and that part's rows as the twin voice. The
 * number of parts comes from the twin AI's table; once every part has been played the terminal takes no more payments
 * and the Interact prompt offers nothing. Which parts were played is run state, so it carries across maps and restarts.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API ATwinAITerminal : public APowerTerminal
{
	GENERATED_BODY()

public:
	ATwinAITerminal();

	/** Parts of the twin AI's table (UTheLastShiftSettings::TwinAITable). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Twin AI")
	int32 GetPartCount() const;

	/** Parts played so far in this run (the suit's archive). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Twin AI")
	int32 GetPartsPlayed() const;

	/** The part the last payment here played (0 before the first). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Twin AI")
	int32 GetLastPlayedPart() const { return LastPlayedPart; }

	virtual bool AcceptsPayment() const override;
	virtual bool OffersPayment() const override { return AcceptsPayment(); }

protected:
	virtual void HandlePaid(APawn* InstigatorPawn) override;

private:
	int32 LastPlayedPart = 0;
};
