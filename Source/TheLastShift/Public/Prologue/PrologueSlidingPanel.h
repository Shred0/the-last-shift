// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PrologueSlidingPanel.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Where a sliding panel goes when it opens. */
UENUM(BlueprintType)
enum class EProloguePanelSlide : uint8
{
	/** Up into the wall above the opening. */
	Up,
	/** Down into the floor below the opening. */
	Down
};

/**
 * A greybox panel that closes an opening of the prologue's home map and slides out of it: the hangar's hatch (up into
 * the wall above) and the ship's hatch (down into the floor). It starts closed and is moved only by SetOpenFraction:
 * it keeps no timer of its own; APrologueDeparture, which owns the order and the times of the departure scene, sets it.
 *
 * The panel has no collision, so it never moves into a pawn. Blocker fills the opening: it blocks while the panel is not
 * fully open, and also when open if bBlockWhenOpen (the hangar's hatch: nothing beyond it is ever reachable).
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueSlidingPanel : public AActor
{
	GENERATED_BODY()

public:
	APrologueSlidingPanel();

	/** Size of the opening the panel closes (X thickness, Y width, Z height); the actor's origin is the opening's bottom centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector OpeningSize = FVector(20.f, 1600.f, 700.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	EProloguePanelSlide Slide = EProloguePanelSlide::Up;

	/** The panel reaches this far into the frame on each side and on the side it slides to, so no light shows at its edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "cm"))
	float PanelOverlap = 20.f;

	/**
	 * Optional modelled panel, made at the panel's size (the opening's width plus PanelOverlap on each side, its height plus
	 * PanelOverlap, 75% of its depth) with its pivot at its bottom centre: when set, Panel shows it at scale 1 instead of
	 * the engine's cube scaled to that size. Only the look changes: it moves and blocks as before. Empty (the default):
	 * nothing changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	TObjectPtr<UStaticMesh> PanelMesh;

	/** The opening stays blocked when the panel is open (true: the void beyond the hangar's hatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	bool bBlockWhenOpen = false;

	/** 0 closed, 1 fully open (clamped). The Blocker opens only at 1, and only without bBlockWhenOpen. */
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void SetOpenFraction(float InFraction);

	UFUNCTION(BlueprintPure, Category = "Panel")
	float GetOpenFraction() const { return OpenFraction; }

	UFUNCTION(BlueprintPure, Category = "Panel")
	bool IsFullyOpen() const { return OpenFraction >= 1.f; }

	/** The Blocker blocks pawns now. */
	UFUNCTION(BlueprintPure, Category = "Panel")
	bool IsBlocking() const;

	/** The opening in world space (the Blocker's box). */
	UFUNCTION(BlueprintPure, Category = "Panel")
	FBox GetOpeningBox() const;

	UStaticMeshComponent* GetPanel() const { return Panel; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	void ApplyPanel();
	FVector GetOpening() const;

	UPROPERTY(VisibleAnywhere, Category = "Panel")
	TObjectPtr<UStaticMeshComponent> Panel;

	UPROPERTY(VisibleAnywhere, Category = "Panel")
	TObjectPtr<UBoxComponent> Blocker;

	float OpenFraction = 0.f;
};
