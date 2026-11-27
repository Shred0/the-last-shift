// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "PrologueMonitorRoom.generated.h"

class AChamberBattery;
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
 *
 * Brief 36B, with bBatteryScreens (the battery's hall): the two screens of every control bank (ControlBankMesh) get a
 * black plate too, and every monitor and bank screen gets a lit plate in front of its black one (ScreenLitMaterial: bars,
 * columns and segmented gauges, four layouts shared out by the monitor's place, no word and no digit), filled to the
 * level the hall's battery shows (AChamberBattery::GetGaugeLevel) in the glow's colour, the one owner of both. A lit
 * plate is light: shown in Normal time only (while Stopped the black plate under it is the screen), never captured by a
 * scan, lighting nothing, still behind the pause menu. In Open's aftermath the lit plates go out one after another across
 * the wall (AChamberBattery::GetScreenOutShare), leaving the black plates.
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

	/** Brief 36B: the battery's hall: lit plates showing the battery's level on the monitors and the control banks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	bool bBatteryScreens = false;

	/** Brief 36B: the mesh whose two screens (the desk's and the upright's) get plates with bBatteryScreens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	TObjectPtr<UStaticMesh> ControlBankMesh;

	/** Brief 36B: the lit screens' look (M_BatteryScreen: unlit; Layout, Level, Color, Brightness). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monitor Room")
	TObjectPtr<UMaterialInterface> ScreenLitMaterial;

	/** Brief 36B: the lit plates (bBatteryScreens), in their order across the wall. */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	TArray<UStaticMeshComponent*> GetLitPlates() const;

	/** Brief 36B: the layout (0 to 3) of lit plate Index, and whether it sits on a control bank. */
	int32 GetLitLayout(int32 Index) const { return LitLayouts.IsValidIndex(Index) ? LitLayouts[Index] : -1; }
	bool IsLitOnBank(int32 Index) const { return LitOnBank.IsValidIndex(Index) && LitOnBank[Index]; }

	/** Brief 36B: the level the lit screens show now (0 to 1), and the lit plates shown now. */
	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	float GetScreenLevel() const { return ScreenLevel; }

	UFUNCTION(BlueprintPure, Category = "Monitor Room")
	int32 GetLitShownCount() const;

	/** Brief 36B: the material of each layout (tests read its parameters). */
	UMaterialInstanceDynamic* GetLayoutMaterial(int32 Layout) const { return LayoutMids.IsValidIndex(Layout) ? LayoutMids[Layout].Get() : nullptr; }

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
	void BuildLitPlate(UStaticMeshComponent* Owner, int32 Index, const FVector& Centre, const FVector& Normal, const FVector& Up, const FVector2D& Size, bool bBank);
	void UpdateScreens();
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
	TArray<TObjectPtr<UStaticMeshComponent>> LitPlates;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LayoutMids;

	TArray<int32> LitLayouts;
	TArray<bool> LitOnBank;
	/** Each lit plate's place across the wall, 0 at one end and 1 at the other (the order in which they go out). */
	TArray<float> LitOrder;
	TWeakObjectPtr<AChamberBattery> Battery;
	float ScreenLevel = 0.f;

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
