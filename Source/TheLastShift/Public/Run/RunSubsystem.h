// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Flow/StdFlowSubsystem.h"
#include "Run/RunSaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RunSubsystem.generated.h"

class URunActListWidget;
class UWorld;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogRun, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAreaEntered, int32, ActIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunSnapshotsChanged);

/**
 * The run across maps: which act the current map is, and the one-way travel to the next area through StdCore's
 * level transition (black screen with UTheLastShiftSettings::TravelLabel, held until the next area is ready and for
 * at least TravelHoldSeconds). The act is the current map's position in UTheLastShiftSettings::AreaMaps; this
 * subsystem reads the settings and copies nothing. After the last area, travel goes back to the main menu. A run
 * starts in the first entry of AreaMaps (StartNewRun), never in StdCore's Gameplay Map.
 *
 * Sole owner of the run snapshots: on every area entry, before the player can act, the run state (battery and home
 * time from UHomeStateSubsystem, banked energy from UEnergySubsystem) is recorded for that act, the snapshots of that
 * act and later ones are replaced, and the list is written to one save slot (URunSaveGame), which is read at startup.
 * Restarting from act k restores snapshot k, discards the later ones, saves and travels to area k; act 1 is a new run.
 *
 * On UHomeStateSubsystem::OnRunLost it shows the loss screen (URunActListWidget, RunLost): game input off, the pause
 * menu refused, UI input on the screen.
 */
UCLASS()
class THELASTSHIFT_API URunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The run subsystem of WorldContext's game instance, or null. */
	static URunSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Act of the current map (1-4), 0 outside the map list. */
	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetActIndex() const;

	/** Act of World's map (1-4), 0 outside the map list. */
	static int32 GetActIndexOf(const UWorld* World);

	/**
	 * Travels to the map after the current one (outside the list: to the first area); after the last area, to the main
	 * menu. Refused (false) while time is Stopped or while a transition is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToNextArea();

	/** Travels to the area of Act (1-4). Refused while time is Stopped or while a transition is already running. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToArea(int32 Act);

	/** Called by the area generator when the current area is ready: takes the act's snapshot, saves, broadcasts OnAreaEntered. */
	void NotifyAreaReady(UWorld* World);

	// --- Snapshots and save ---

	/** One snapshot per act reached, in act order. */
	UFUNCTION(BlueprintPure, Category = "Run")
	const TArray<FAreaSnapshot>& GetSnapshots() const { return Snapshots; }

	/** True when the save holds at least one snapshot. */
	UFUNCTION(BlueprintPure, Category = "Run")
	bool HasSavedRun() const { return Snapshots.Num() > 0; }

	/**
	 * Restores the snapshot of Act, discards the later snapshots, saves and travels to area Act through the travel
	 * screen. Act 1 is StartNewRun(). Refused (false, nothing changed) without a snapshot for Act, while time is
	 * Stopped or while a transition runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool RestartFromAct(int32 Act);

	/** Clears the snapshots and the save, resets home state and banked energy, and travels to the first area. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool StartNewRun();

	/** RestartFromAct of the latest act reached. False when there is no saved run. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool ContinueRun();

	/** Travels to the main menu (the loss screen's Main menu). The save is kept. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool ReturnToMainMenu();

	/** Deletes the snapshots and the save slot (debug: tls.Run.ClearSave). */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void ClearSave();

	/** Re-reads the save slot into the snapshots (startup; debug). */
	bool LoadSave();

	/** The snapshots changed (area entry, restart, new run, clear, load). */
	UPROPERTY(BlueprintAssignable, Category = "Run")
	FOnRunSnapshotsChanged OnSnapshotsChanged;

	/** The loss screen, while it is shown. */
	URunActListWidget* GetLossScreen() const { return LossScreen.Get(); }

	/** Writes to the save slot since startup, and whether the last one succeeded. */
	int32 GetSaveWrites() const { return SaveWrites; }
	bool DidLastSaveSucceed() const { return bLastSaveOk; }

	static const TCHAR* SaveSlotName() { return TEXT("Run"); }

	/** The current area is generated, validated and playable. */
	UPROPERTY(BlueprintAssignable, Category = "Run")
	FOnAreaEntered OnAreaEntered;

	/** Real-time seconds of the last travel: request to black, request to area ready, request to fade in (-1 = not yet). */
	float GetLastTravelToCoveredSeconds() const { return LastTravelCovered; }
	float GetLastTravelToReadySeconds() const { return LastTravelReady; }
	float GetLastTravelToFadeInSeconds() const { return LastTravelFadeIn; }

private:
	bool TravelTo(const TSoftObjectPtr<UWorld>& Level, bool bMainMenu, const FString& What);

	UFUNCTION()
	void HandleFlowStateChanged(EStdFlowState NewState);

	UFUNCTION()
	void HandleRunLost();

	/** No transition running and time not Stopped in the current world. */
	bool CanTravelNow(const TCHAR* What) const;

	void TakeSnapshot(int32 Act);
	void WriteSave();

	TArray<FAreaSnapshot> Snapshots;
	int32 SaveWrites = 0;
	bool bLastSaveOk = false;

	TWeakObjectPtr<URunActListWidget> LossScreen;

	/** FPlatformTime of the last travel request; negative when none is pending. */
	double TravelRequestTime = -1.0;
	float LastTravelCovered = -1.f;
	float LastTravelReady = -1.f;
	float LastTravelFadeIn = -1.f;
};
