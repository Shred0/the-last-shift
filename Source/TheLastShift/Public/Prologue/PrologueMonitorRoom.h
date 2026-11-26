// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "PrologueMonitorRoom.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTimeStateSubsystem;

/**
 * The monitor room of Prologue_Home (Brief 27), one placed in the map. When play begins it finds every component of the
 * map that shows MonitorWallMesh or RackMesh and:
 *  - lays a black plate (the engine's plane, UTheLastShiftSettings::ScreenMaterial) over each monitor of each monitor
 *    wall, as a component of the actor that owns the wall: the monitors show nothing;
 *  - puts small indicator lights on each server's front of each rack (RackLights in UTheLastShiftSettings, the one owner
 *    of their values), each blinking on its own rhythm on the run's clock. Most in home's tint, a few in a second colour.
 *    They show in Normal time only, are hidden while Stopped, are never captured by a scan and stand still behind the
 *    pause menu, like the hologram of the room. They light nothing.
 * The monitors' and servers' places are those of the two meshes (their own frames); nothing of either mesh changes.
 */
UCLASS()
class THELASTSHIFT_API APrologueMonitorRoom : public AActor
{
	GENERATED_BODY()

public:
	APrologueMonitorRoom();

	virtual void Tick(float DeltaSeconds) override;

	/** The mesh whose monitors get a plate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	TObjectPtr<UStaticMesh> MonitorWallMesh;

	/** The mesh whose servers get lights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	TObjectPtr<UStaticMesh> RackMesh;

	/** The lights' look: unlit, its "Color" parameter the emissive colour (the hologram's material). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	TObjectPtr<UMaterialInterface> LightMaterial;

	/** Plates laid over monitors. */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	int32 GetPlateCount() const { return Plates.Num(); }

	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	TArray<UStaticMeshComponent*> GetPlates() const;

	/** Lights, both colours. */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	int32 GetLightCount() const { return Lights.Num(); }

	/** Lights on now. */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	int32 GetLightsOnCount() const;

	/** The lights are shown (Normal time). */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	bool AreLightsShown() const;

	/** World place, colour index (0 home's tint, 1 the second), period and phase of light Index (tests and reports). */
	bool GetLight(int32 Index, FVector& OutLocation, int32& OutColour, float& OutPeriod, float& OutPhase, bool& bOutOn) const;

	/** Builds the plates and the lights again from the settings now (tests, after changing the settings for a session). */
	UFUNCTION(BlueprintCallable, Category = "Monitor Room")
	void Rebuild();

	/** Test only: the lights shown while Stopped too (to prove a scan does not capture them). */
	void SetShownWhileStoppedForTest(bool bShown);

	/** Test only: the lights hidden in every state (a scan's points with and without them). */
	void SetHiddenForTest(bool bHidden);

	/** The two light components (home's tint, the second colour). */
	UInstancedStaticMeshComponent* GetLightComponent(int32 Colour) const { return Colour == 0 ? LightsHome : LightsSecond; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FLight
	{
		FTransform World;
		int32 Colour = 0;
		int32 Instance = INDEX_NONE;
		float Period = 1.f;
		float Phase = 0.f;
		bool bOn = true;
	};

	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	void BuildPlates();
	void BuildLights();
	void UpdateBlink(bool bForce);
	void UpdateVisibility();
	bool IsLightOn(const FLight& Light, double RunSeconds) const;

	UPROPERTY(VisibleAnywhere, Category = "Monitor Room")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Monitor Room")
	TObjectPtr<UInstancedStaticMeshComponent> LightsHome;

	UPROPERTY(VisibleAnywhere, Category = "Monitor Room")
	TObjectPtr<UInstancedStaticMeshComponent> LightsSecond;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Plates;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HomeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SecondMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TArray<FLight> Lights;
	float OnShare = 0.5f;
	bool bShownWhileStoppedForTest = false;
	bool bHiddenForTest = false;
};
