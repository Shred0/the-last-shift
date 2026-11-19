// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/DoorDevice.h"
#include "PrologueEntryDoor.generated.h"

/**
 * The door at the end of the home map's corridor, into the hangar: a free door like the others (its terminal powers it,
 * untimed, and it opens), that can then close behind the player and stay closed. It keeps no timer of its own:
 * APrologueDeparture starts the close, moves the panel and ends it.
 *
 * BeginCloseBehind works only while the door is open and no pawn is in its doorway. It shuts the way at once (the
 * Blocker blocks from that moment, so nothing can step into the doorway and the door never closes on anyone; the Doorway
 * volume, which keeps an unpowered door open for a pawn in it, is switched off, since nobody can be in it any more) and
 * shows the panel raised inside the wall above the doorway; SetClosedFraction lowers it; FinishCloseBehind switches the
 * power off, which closes the door at once. The panel is thinner than the wall, so raised it is hidden in it.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueEntryDoor : public ADoorDevice
{
	GENERATED_BODY()

public:
	APrologueEntryDoor();

	/** Shuts the way and shows the panel raised. False (nothing done) when the door is not open or a pawn is in the doorway. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	bool BeginCloseBehind();

	/** 0 raised (open), 1 down (closed). Only while closing behind. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void SetClosedFraction(float InFraction);

	/** The panel down; the power off, so the door closes and stays closed. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void FinishCloseBehind();

	/** Between BeginCloseBehind and FinishCloseBehind. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsClosingBehind() const { return bClosingBehind; }

	/** FinishCloseBehind has run. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool HasClosedBehind() const { return bClosedBehind; }

	/** 0 open, 1 closed, while closing behind; 1 once closed behind; 0 before. */
	UFUNCTION(BlueprintPure, Category = "Door")
	float GetClosedFraction() const { return ClosedFraction; }

	/** The Blocker blocks pawns now. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsBlocking() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void ApplyPanelFraction();

	bool bClosingBehind = false;
	bool bClosedBehind = false;
	float ClosedFraction = 0.f;
};
