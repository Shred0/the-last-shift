// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "PrologueHologram.generated.h"

class APrologueBriefingTerminal;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTimeStateSubsystem;

/**
 * The twin structure as a hologram in the prologue's monitor room: a cloud of points laid on an octahedron centred on
 * the actor, its vertices on the actor's axes, that turns slowly around its vertical axis while each point drifts a
 * little along the face it lies on and comes back; inside it, the route the player will walk (four nodes, one for each
 * area, joined in order by three straight lines), drawn by the home AI's briefing; under it, a beam of faint light rising
 * from the tip of each vertical rod of its base (Base): its axis on the hologram's centre, its spread BeamSpreadShare of
 * the angle of the cone that holds the whole turning hologram, BeamLength of the way to the hologram's far side,
 * strongest at the tip and fading to nothing at its end, with a slight flicker of its level. The beams do not turn. Every value comes from UTheLastShiftSettings::Hologram; the points and the beams take the hologram's
 * colour (ScanColors.Scan), the route the entity's own colour (ScanColors.Entity), whatever the entity-found state.
 *
 * The route is drawn by the share of the briefing typed, read from the briefing terminal (APrologueBriefingTerminal::
 * GetBriefingProgress01): none before the group starts, from node 1 to node 4 while it is typed, whole once it is done.
 *
 * It is light, not a thing: unlit, no collision, no shadow, shown in Normal time only (hidden the moment time stops, so
 * no light is left on the black screen), and never seen by a scan: every part is hidden in scene captures, which is what
 * the lidar uses, and it is no lidar point source. Its motion reads the run's clock (UTimeStateSubsystem::GetRunSeconds),
 * so it stands still behind the pause menu.
 *
 * The points are instances of one small cube (one draw), PointMaterial an unlit material with a vector parameter Color;
 * the route is cubes in a second instance of the same material; the beams are instances of one cone, BeamMaterial an
 * unlit additive two-sided material with a vector parameter Color whose emissive is scaled by the per-instance custom
 * data 0 (the beam's level at its tip, flicker included) and by a fade along the cone's local Z, (1 - u)^FadePower with u
 * from 0 at its apex (scalar parameter ApexZ) to 1 at its base (LengthZ below it), both pushed from the cone mesh's bounds,
 * FadePower from the settings.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueHologram : public AActor
{
	GENERATED_BODY()

public:
	APrologueHologram();

	/** Unlit material of the points and of the route, with a vector parameter named Color (M_Hologram). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
	TObjectPtr<UMaterialInterface> PointMaterial;

	/** Unlit additive material of the beams, with a vector parameter Color, scaled by per-instance custom data 0 and faded from ApexZ over LengthZ (M_HologramBeam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
	TObjectPtr<UMaterialInterface> BeamMaterial;

	/** The base the beams rise from: its rods are described by the settings (BeamRods, BeamRodRadius, BeamRodTop) around its pivot. None: no beam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
	TObjectPtr<AActor> Base;

	/** The terminal whose briefing draws the route. None: the first briefing terminal of the world at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
	TObjectPtr<APrologueBriefingTerminal> Briefing;

	/** Rebuilds the points, the route and the beams from the settings and the colours from ScanColors. */
	UFUNCTION(BlueprintCallable, Category = "Hologram")
	void Rebuild();

	/** Debug and measurement: off hides every part in every time state (on follows the time state again). */
	UFUNCTION(BlueprintCallable, Category = "Hologram")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Hologram")
	bool IsEnabled() const { return bEnabled; }

	/**
	 * Debug and measurement only: shows every part while Stopped too, to prove that the scan's capture does not see it.
	 * The game never sets it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hologram|Debug")
	void SetShownWhileStoppedForTest(bool bInShown);

	/** Debug and measurement only: hides the beams (off: they follow the hologram again). The game never sets it. */
	UFUNCTION(BlueprintCallable, Category = "Hologram|Debug")
	void SetBeamsHiddenForTest(bool bInHidden);

	/** Debug and measurement only: hides each part on its own (all off: they follow the hologram again). The game never sets it. */
	UFUNCTION(BlueprintCallable, Category = "Hologram|Debug")
	void SetPartsHiddenForTest(bool bInPoints, bool bInRoute, bool bInBeams);

	/** Beam Index's cone that holds the whole hologram: its half-angle in degrees (its spread is BeamSpreadShare of it). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	float GetBeamHoldingSpread(int32 Index) const { return BeamHolding.IsValidIndex(Index) ? BeamHolding[Index] : 0.f; }

	/** Debug and measurement only: draws the route at this share (0..1) instead of the briefing's; below 0 follows the briefing again. */
	UFUNCTION(BlueprintCallable, Category = "Hologram|Debug")
	void SetRouteProgressOverride(float Progress01);

	/** The points are drawn now. */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	bool IsShown() const;

	UFUNCTION(BlueprintPure, Category = "Hologram")
	int32 GetPointCount() const;

	/** The colour the points and the beams were given (ScanColors.Scan when built). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FLinearColor GetPointColor() const { return PointColor; }

	/** The colour the route was given (ScanColors.Entity when built). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FLinearColor GetRouteColor() const { return RouteColor; }

	/** The points' bounds in world space. */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FBox GetPointsBox() const;

	/** The turn about the vertical axis now, in degrees from the placed rotation (0..360). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	float GetTurnDegrees() const { return TurnDegrees; }

	/** The share of the route drawn now (0..1). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	float GetRouteProgress01() const { return RouteProgress; }

	/** The route's node Index (0..3) in world space now (it turns with the hologram). */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	FVector GetRouteNodeWorld(int32 Index) const;

	/** Number of route nodes and lines drawn now. */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	int32 GetRouteNodesShown() const { return RouteNodesShown; }

	UFUNCTION(BlueprintPure, Category = "Hologram")
	int32 GetBeamCount() const;

	/**
	 * Beam Index: its start (a rod's tip) and end (on its axis) in world space, its spread (the half-angle of its cone), the
	 * distance from its start to the hologram's far side along its axis (the length at BeamLength 1), and its level at the
	 * tip now (flicker included). False when absent.
	 */
	UFUNCTION(BlueprintPure, Category = "Hologram")
	bool GetBeam(int32 Index, FVector& OutStart, FVector& OutEnd, float& OutSpreadDegrees, float& OutFarDistance, float& OutLevel) const;

	/** A beam's fade at a share (0 at its tip, 1 at its end) of its length, as M_HologramBeam draws it: (1 - share)^Power. */
	static float BeamFade(float Share01, float Power);

	/** Debug and measurement only: holds the turn, the drift and the flicker where they are (shots compared pixel by pixel). */
	UFUNCTION(BlueprintCallable, Category = "Hologram|Debug")
	void SetMotionFrozenForTest(bool bInFrozen) { bMotionFrozenForTest = bInFrozen; }

	/** Point Index at rest and now, in the points' component space (the octahedron's frame, which turns). False when absent. */
	bool GetPointLocal(int32 Index, FVector& OutRest, FVector& OutNow) const;

	/** The octahedron's half-diagonal (Size / 2). */
	float GetHalfSize() const { return HalfSize; }

	/** The parts' components: hidden in scene captures, no collision. */
	UInstancedStaticMeshComponent* GetPointsComponent() const { return Points; }
	UInstancedStaticMeshComponent* GetRouteComponent() const { return Route; }
	UInstancedStaticMeshComponent* GetBeamsComponent() const { return Beams; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	void ApplyVisibility();
	void BuildPoints();
	void BuildRoute();
	void BuildBeams();
	/** Moves the parts to the run's time Seconds: the turn, the points' drift, the beams' flicker, the route's progress. */
	void UpdateMotion(double Seconds);
	void UpdateRoute(float Progress01);
	float ReadBriefingProgress() const;

	UPROPERTY(VisibleAnywhere, Category = "Hologram")
	TObjectPtr<UInstancedStaticMeshComponent> Points;

	UPROPERTY(VisibleAnywhere, Category = "Hologram")
	TObjectPtr<UInstancedStaticMeshComponent> Route;

	UPROPERTY(VisibleAnywhere, Category = "Hologram")
	TObjectPtr<UInstancedStaticMeshComponent> Beams;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PointMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RouteMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	/** Per point: rest position, drift vector (along its face, within the face), drift period and phase. */
	TArray<FVector> RestPositions;
	TArray<FVector> DriftVectors;
	TArray<float> DriftPeriods;
	TArray<float> DriftPhases;
	TArray<FTransform> PointTransforms;

	/** The route's nodes in the points' frame, and the beams' starts, ends and flicker phases in world space. */
	TArray<FVector> RouteNodes;
	TArray<FVector> BeamStarts;
	TArray<FVector> BeamEnds;
	TArray<float> BeamSpreads;
	TArray<float> BeamHolding;
	TArray<float> BeamFars;
	TArray<FVector> BeamPhases;
	TArray<float> BeamLevels;

	FQuat PlacedRotation = FQuat::Identity;
	FLinearColor PointColor = FLinearColor::White;
	FLinearColor RouteColor = FLinearColor::White;
	float HalfSize = 200.f;
	float TurnDegrees = 0.f;
	float RouteProgress = -1.f;
	float RouteOverride = -1.f;
	float BriefingProgress = 0.f;
	int32 RouteNodesShown = 0;
	bool bEnabled = true;
	bool bShownWhileStoppedForTest = false;
	bool bBeamsHiddenForTest = false;
	bool bPointsHiddenForTest = false;
	bool bRouteHiddenForTest = false;
	bool bMotionFrozenForTest = false;
};
