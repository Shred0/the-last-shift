// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "AreaExit.generated.h"

class APawn;
class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAreaExitEntered, APawn*, Pawn);

/**
 * The end of an area's route. When a player-controlled pawn is inside it in Normal time, once the area is ready, it
 * fires OnPlayerEntered and calls URunSubsystem::TravelToNextArea, once. While time is Stopped it does nothing;
 * resuming inside it travels, and so does resuming past it (Brief 26): a player who walked past it while Stopped,
 * inside its section and within RunPastExit of its far face, travels at once on the return to Normal time, as if they
 * had walked into it, so no player stands past it in Normal time. It blocks nothing, is invisible and never affects
 * navigation; Volume's box extent is its size (set by the area generator).
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AAreaExit : public AActor
{
	GENERATED_BODY()

public:
	AAreaExit();

	UPROPERTY(BlueprintAssignable, Category = "Area|Events")
	FOnAreaExitEntered OnPlayerEntered;

	UFUNCTION(BlueprintPure, Category = "Area")
	UBoxComponent* GetVolume() const { return Volume; }

	/** True once this exit has asked for the travel. */
	UFUNCTION(BlueprintPure, Category = "Area")
	bool HasTriggered() const { return bTriggered; }

	/**
	 * Brief 31A: switches the exit on or off (on by default). Off, it never travels (the ship's exit on the way home, the
	 * corridor's exit in the home map's departure state).
	 */
	UFUNCTION(BlueprintCallable, Category = "Area")
	void SetExitEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Area")
	bool IsExitEnabled() const { return bExitEnabled; }

	/** Checks whether the player is inside (or past it) in Normal time and travels if so (also called on overlap and on Resume). */
	UFUNCTION(BlueprintCallable, Category = "Area")
	void TryTravel();

	/**
	 * Brief 26: how far the exit corridor runs past the exit's far face. On the return to Normal time a player standing
	 * there, inside the exit's section, travels at once. Set by the area generator (the exit corridor's run past the exit);
	 * set by hand in a hand-built map (Prologue_Arrival). 0 = the exit's own box only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "100000.0", Units = "cm"))
	float RunPastExit = 0.f;

	/** True when Pawn stands in the exit's box or past it along its forward, within RunPastExit and inside the box's section. */
	UFUNCTION(BlueprintPure, Category = "Area")
	bool IsAtOrPastExit(const APawn* Pawn) const;

#if !UE_BUILD_SHIPPING
	/** Tests only: while set, a travel the exit would start is counted in TestTravelRequests instead of started. */
	bool bTestHoldTravel = false;
	int32 TestTravelRequests = 0;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UBoxComponent> Volume;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	bool bTriggered = false;
	bool bExitEnabled = true;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
