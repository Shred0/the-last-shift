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
 * resuming inside it travels. It blocks nothing, is invisible and never affects navigation; Volume's box extent is its
 * size (set by the area generator).
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

	/** Checks whether the player is inside in Normal time and travels if so (also called on overlap and on Resume). */
	UFUNCTION(BlueprintCallable, Category = "Area")
	void TryTravel();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UBoxComponent> Volume;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	bool bTriggered = false;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
