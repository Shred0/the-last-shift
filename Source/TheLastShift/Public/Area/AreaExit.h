// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AreaExit.generated.h"

class APawn;
class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAreaExitEntered, APawn*, Pawn);

/**
 * The end of an area's route: a plain volume that fires OnPlayerEntered when a player-controlled pawn walks into it.
 * What happens then (travel to the next map) is not this actor's business. It blocks nothing, is invisible and never
 * affects navigation; Volume's box extent is its size (set by the area generator).
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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UBoxComponent> Volume;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
