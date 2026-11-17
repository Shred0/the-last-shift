// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PrologueLineTrigger.generated.h"

class UBoxComponent;
class UWorld;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAnyPrologueLine, UWorld* /*World*/, FName /*Group*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPrologueLineFired, FName, Group);

/**
 * A volume of the prologue's maps that asks the home AI for a group of the prologue table (DT_Prologue) the first time
 * the player's pawn enters it, in either time state: the monitor room (Hologram) and the hangar (Hangar). The home AI is
 * the one place that pushes the lines (UHomeAIComponent listens to OnAnyLine); this volume only says when.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueLineTrigger : public AActor
{
	GENERATED_BODY()

public:
	APrologueLineTrigger();

	/** The group of DT_Prologue asked for (Hologram, Hangar, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue")
	FName Group;

	/** Half size of the volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector Extent = FVector(300.f, 300.f, 200.f);

	UFUNCTION(BlueprintPure, Category = "Prologue")
	bool HasFired() const { return bFired; }

	/** Every prologue group asked for in any world (a trigger entered, the arrival's hatch); listeners filter by world. */
	static FOnAnyPrologueLine OnAnyLine;

	/** Asks for Group in WorldContext's world (what a trigger does when entered). */
	static void Say(const UObject* WorldContext, FName Group);

	UPROPERTY(BlueprintAssignable, Category = "Prologue")
	FOnPrologueLineFired OnFired;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category = "Prologue")
	TObjectPtr<UBoxComponent> Volume;

	bool bFired = false;
};
