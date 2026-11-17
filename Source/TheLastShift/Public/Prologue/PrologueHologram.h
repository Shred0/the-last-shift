// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "PrologueHologram.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTimeStateSubsystem;

/**
 * The twin structure as a hologram in the prologue's monitor room: a still cloud of points laid on an octahedron
 * centred on the actor, its vertices on the actor's axes. Size, number of points and point size come from
 * UTheLastShiftSettings::Hologram, the colour from ScanColors.Scan (read when it is built; nothing is copied).
 *
 * It is light, not a thing: unlit points with no collision and no shadow, shown in Normal time only (hidden the moment
 * time stops, so no light is left on the black screen), and never seen by a scan: the points are hidden in scene
 * captures, which is what the lidar uses, and they are no lidar point source.
 *
 * The points are instances of one small cube (one draw), PointMaterial an unlit material with a vector parameter Color.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueHologram : public AActor
{
	GENERATED_BODY()

public:
	APrologueHologram();

	/** Unlit material of the points, with a vector parameter named Color (M_Hologram). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
	TObjectPtr<UMaterialInterface> PointMaterial;

	/** Rebuilds the points from the settings and the colour from ScanColors.Scan. */
	UFUNCTION(BlueprintCallable, Category = "Hologram")
	void Rebuild();

	/** Debug and measurement: off hides the points in every time state (on follows the time state again). */
	UFUNCTION(BlueprintCallable, Category = "Hologram")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Hologram")
	bool IsEnabled() const { return bEnabled; }

	/** The points are drawn now. */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	bool IsShown() const;

	UFUNCTION(BlueprintPure, Category = "Hologram")
	int32 GetPointCount() const;

	/** The colour the points were given (ScanColors.Scan when built). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FLinearColor GetPointColor() const { return PointColor; }

	/** The points' bounds in world space. */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FBox GetPointsBox() const;

	/** The points' component: hidden in scene captures, no collision. */
	UInstancedStaticMeshComponent* GetPointsComponent() const { return Points; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	void ApplyVisibility();

	UPROPERTY(VisibleAnywhere, Category = "Hologram")
	TObjectPtr<UInstancedStaticMeshComponent> Points;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PointMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	FLinearColor PointColor = FLinearColor::White;
	bool bEnabled = true;
};
