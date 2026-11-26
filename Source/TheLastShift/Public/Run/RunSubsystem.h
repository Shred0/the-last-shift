// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Flow/StdFlowSubsystem.h"
#include "Run/RunSaveGame.h"
#include "Run/TheLastShiftSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RunSubsystem.generated.h"

class URunActListWidget;
class UWorld;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogRun, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAreaEntered, int32, ActIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunSnapshotsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSuitArchiveChanged);
/** How the entity was found. */
UENUM(BlueprintType)
enum class EEntityFoundBy : uint8
{
	/** Two scans in a row of one Stop each kept more than half of its figure (AEntity::ClearSightingShare, ClearSightingScans). */
	Sighting,
	/** It touched the player (the first contact of the run). */
	Contact
};

inline const TCHAR* LexToString(EEntityFoundBy By)
{
	return By == EEntityFoundBy::Contact ? TEXT("Contact") : TEXT("Sighting");
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEntityFound, EEntityFoundBy, By);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityFirstContact);

/** One moment of the last travel started by the run: what happened, when, and where the bar and the marker stood after it. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FTravelStepRecord
{
	GENERATED_BODY()

	/** Covered, MapLoaded, RoutePlanned, GeometryBuilt, NavigationReady, AreaReady, FadeIn. */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	FName Step;

	/** Real-time seconds since the travel request. */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float Seconds = 0.f;

	/** StdCore's progress bar and marker right after the step (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float Progress = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float Marker = 0.f;
};

/**
 * The run across maps: which act the current map is, and the one-way travel to the next area through StdCore's
 * level transition (black screen with the destination act's AreaEntry line as its label, built by
 * UHomeAIComponent::BuildTravelLabel, held until the next area is ready and for at least TravelHoldSeconds and the
 * line's reading time; no label to the main menu). The act is the current map's position in UTheLastShiftSettings::AreaMaps; this
 * subsystem reads the settings and copies nothing. After the last area, travel goes to the main menu, which opens the
 * ending there (URunSceneSubsystem); no other travel opens it. A run
 * starts in the first entry of AreaMaps (StartNewRun), never in StdCore's Gameplay Map.
 *
 * Sole owner of the run snapshots: on every area entry, before the player can act, the run state (battery and home
 * time from UHomeStateSubsystem, banked energy from UEnergySubsystem) is recorded for that act, the snapshots of that
 * act and later ones are replaced, and the list is written to one save slot (URunSaveGame), which is read at startup.
 * Restarting from act k restores snapshot k, discards the later ones, saves and travels to area k; act 1 is a new run.
 *
 * Sole owner of the suit's archive (FSuitArchive: the log entries unlocked at terminals and the twin AI's parts played).
 * It is run state like battery, home time and banked energy: it survives map changes, every snapshot carries a copy,
 * restoring a snapshot restores it, a new run empties it. It stores which items, never their text.
 *
 * Sole owner of whether the entity has been found (IsEntityFound) and whether its first contact with the player has
 * happened (HasFirstContact): both false in a new run and set once, at the end of a Stop (AEntity): found at the end of
 * the first Stop with a clear sighting or a contact (MarkEntityFound), first contact at the end of the first Stop that
 * ended with a contact (MarkFirstContact). They are run state like the archive: they survive map changes, every snapshot
 * carries them, restoring a snapshot restores them, a new run clears them.
 *
 * Brief 31A, the way home (UTheLastShiftSettings::IsReturnHomeOn): after the last area the run travels to the home map in
 * its return state (StartReturnHome: the flight back, the Return label, the marker from right to left). That the run is
 * on its way home is run state owned here (IsReturningHome): set when the last area's exit fires, cleared when the
 * ending closes and by a new game or a new run; Continue and the list of entries set or clear it by the entry they
 * restore. The return is a fifth entry of the run, after the four areas': its snapshot (the same run state as an area's,
 * ActIndex 0) is taken on entering the home map in the return state (NotifyReturnEntered) and saved; entering an area or
 * restarting from an act discards it, as it discards the later acts' snapshots. It is no act: nothing keyed by the act
 * index ever sees it.
 *
 * On UHomeStateSubsystem::OnRunLost it shows the loss screen (URunActListWidget, RunLost): game input off, the pause
 * menu refused, UI input on the screen.
 *
 * The travel screen's progress bar: the one place that turns the area generator's steps (EAreaLoadStep) into StdCore
 * progress, with the fractions of UTheLastShiftSettings::TravelProgress, and keeps a record of every step of the last
 * travel it started.
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

	/** 1 in the prologue's home map, 2 in its arrival map, 0 anywhere else (UTheLastShiftSettings::GetPrologueStageOfMap). */
	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetPrologueStage() const;

	static int32 GetPrologueStageOf(const UWorld* World);

	/**
	 * "New game" after the text scene. With the prologue's maps on (UTheLastShiftSettings::ArePrologueMapsOn): the run's
	 * state in memory becomes a new run's (battery full, no home time, no banked energy, an empty archive, the entity not
	 * found, no first contact) so the home AI speaks as at the start of a run, the save is left as it is, and the travel goes to the
	 * prologue's home map. Off:
	 * StartNewRun. Refused while time is Stopped or while a transition runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool StartNewGame();

	/**
	 * Travels to the map after the current one: from the prologue's home map to its arrival map (the flight, with the
	 * first Flight line of the prologue table as the travel screen's label), from the arrival map StartNewRun (exactly
	 * what "New game" did before the prologue's maps), outside the list to the first area; after the last area, to the
	 * ending (TravelToEnding). Refused (false) while time is Stopped or while a transition is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToNextArea();

	/**
	 * Travels to the main menu, which opens the ending on arrival with the run's state as it is (what the last area's
	 * exit does). The save is not touched. Refused while time is Stopped or while a transition is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToEnding();

	/** Travels to the area of Act (1-4). Refused while time is Stopped or while a transition is already running. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToArea(int32 Act);

	/** Called by the area generator when the current area is ready: takes the act's snapshot, saves, broadcasts OnAreaEntered. */
	void NotifyAreaReady(UWorld* World);

	/** Called by the area generator after each real step of the build: reports its fraction to StdCore's travel screen. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void ReportAreaLoadStep(EAreaLoadStep Step);

	/** The steps of the last travel started through this subsystem, in order (empty before the first). */
	UFUNCTION(BlueprintPure, Category = "Run")
	const TArray<FTravelStepRecord>& GetLastTravelSteps() const { return LastTravelSteps; }

	// --- Snapshots and save ---

	/** One snapshot per act reached, in act order. */
	UFUNCTION(BlueprintPure, Category = "Run")
	const TArray<FAreaSnapshot>& GetSnapshots() const { return Snapshots; }

	/** True when the save holds at least one snapshot (an act's or the return's). */
	UFUNCTION(BlueprintPure, Category = "Run")
	bool HasSavedRun() const { return Snapshots.Num() > 0 || HasReturnEntry(); }

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

	/** The latest entry reached: RestartFromReturn when the return has been reached, else RestartFromAct of the latest act. False when there is no saved run. */
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

	// --- The suit's archive ---

	UFUNCTION(BlueprintPure, Category = "Run|Archive")
	const FSuitArchive& GetArchive() const { return Archive; }

	/** At least one log entry or twin AI part is stored (the pause menu offers the archive only then). */
	UFUNCTION(BlueprintPure, Category = "Run|Archive")
	bool HasArchiveItems() const { return !Archive.IsEmpty(); }

	/** Stores the log entries Orders (the ones already stored are skipped). Returns how many were added. */
	UFUNCTION(BlueprintCallable, Category = "Run|Archive")
	int32 StoreLogEntries(const TArray<int32>& Orders);

	/** Stores twin AI part Part: only the next one (TwinPartsPlayed + 1) is accepted. */
	UFUNCTION(BlueprintCallable, Category = "Run|Archive")
	bool StoreTwinPart(int32 Part);

	/** Empties the archive (debug: tls.Archive.Store clear). The save is not touched. */
	UFUNCTION(BlueprintCallable, Category = "Run|Archive")
	void ClearArchive();

	/** The archive changed (a store, a restore, a new run, a clear). */
	UPROPERTY(BlueprintAssignable, Category = "Run|Archive")
	FOnSuitArchiveChanged OnArchiveChanged;

	// --- The entity ---

	/** The entity has been found in this run: its points show in ScanColors.Entity, before that in ScanColors.Scan. */
	UFUNCTION(BlueprintPure, Category = "Run|Entity")
	bool IsEntityFound() const { return bEntityFound; }

	/**
	 * The entity was found now (the end of a Stop with a clear sighting or a contact), By how. True, with OnEntityFound,
	 * only the first time in a run; false when it was already found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|Entity")
	bool MarkEntityFound(const FString& Why, EEntityFoundBy By = EEntityFoundBy::Sighting);

	/** Sets the state without OnEntityFound (debug: tls.Run.EntityFound; restoring a snapshot does the same). */
	UFUNCTION(BlueprintCallable, Category = "Run|Entity")
	void SetEntityFound(bool bFound, const FString& Why);

	/** Once in a run, when the entity becomes found (MarkEntityFound), with how. The home AI says its rows on it. */
	UPROPERTY(BlueprintAssignable, Category = "Run|Entity")
	FOnEntityFound OnEntityFound;

	/** The first contact with the entity has happened in this run. */
	UFUNCTION(BlueprintPure, Category = "Run|Entity")
	bool HasFirstContact() const { return bFirstContact; }

	/**
	 * The entity touched the player now (the end of the Stop that ended with the contact). True, with OnFirstContact,
	 * only the first time in a run; false after that.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|Entity")
	bool MarkFirstContact(const FString& Why);

	/** Sets the state without OnFirstContact (debug: tls.Run.FirstContact; restoring a snapshot does the same). */
	UFUNCTION(BlueprintCallable, Category = "Run|Entity")
	void SetFirstContact(bool bHappened, const FString& Why);

	/** Once in a run, at the end of the Stop of the first contact (MarkFirstContact). The home AI says its lines on it. */
	UPROPERTY(BlueprintAssignable, Category = "Run|Entity")
	FOnEntityFirstContact OnFirstContact;

	// --- The way home (Brief 31A) ---

	/** The run is on its way home: the home map is in its return state. */
	UFUNCTION(BlueprintPure, Category = "Run|Return")
	bool IsReturningHome() const { return bReturningHome; }

	/** Sets or clears the run's way home (the ending closing clears it; debug). */
	void SetReturningHome(bool bReturning, const TCHAR* Why);

	/** World is the home map in its return state: the run on its way home and the way home switched on. */
	static bool IsReturnStateOf(const UWorld* World);

	/**
	 * The last area left: with the way home on, the run is on its way home and travels to the home map (the flight back:
	 * the travel screen's label is the first Return row of the epilogue table, its marker crosses from right to left); off,
	 * TravelToEnding. Refused while time is Stopped or while a transition runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|Return")
	bool StartReturnHome();

	/** Called when the home map begins in its return state: takes the return's snapshot and saves. */
	void NotifyReturnEntered(UWorld* World);

	/** The return has been reached and the way home is on: the return's entry is offered (after act 4) and restored by Continue. */
	UFUNCTION(BlueprintPure, Category = "Run|Return")
	bool HasReturnEntry() const;

	/** The return's snapshot (valid when HasReturnEntry). */
	UFUNCTION(BlueprintPure, Category = "Run|Return")
	const FAreaSnapshot& GetReturnSnapshot() const { return ReturnSnapshot; }

	/**
	 * Restores the return's snapshot and travels to the home map in its return state, through the travel screen of the
	 * flight back. Nothing is discarded: the return is the last entry. Refused without a return entry, while time is
	 * Stopped or while a transition runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|Return")
	bool RestartFromReturn();

	/** Version of the save file read last by LoadSave (0 when there was none). */
	int32 GetLoadedSaveVersion() const { return LoadedSaveVersion; }

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

	/** Label and minimum covered time given to the last travel that started (empty and 0 to the main menu). */
	const FText& GetLastTravelLabel() const { return LastTravelLabel; }
	float GetLastTravelMinCoveredSeconds() const { return LastTravelMinCovered; }

private:
	bool TravelTo(const TSoftObjectPtr<UWorld>& Level, bool bMainMenu, const FString& What);

	/**
	 * A travel through StdCore's travel screen with Label, covered for at least TravelHoldSeconds and ReadingSeconds (or
	 * to the main menu, without a label). Refused while time is Stopped.
	 */
	bool TravelToLevel(const TSoftObjectPtr<UWorld>& Level, bool bMainMenu, const FText& Label, float ReadingSeconds, const FString& What, bool bMarkerRightToLeft = false);

	/** The flight back to the home map (its label and marker), the run set on its way home once it has started. */
	bool TravelHome(const FString& What);

	/** Restores battery, home time, banked energy, archive and the entity's states from Snapshot. */
	void RestoreRunState(const FAreaSnapshot& Snapshot, const FString& Why);

	/** Battery full, no home time, no banked energy, an empty archive, the entity not found, no first contact (the save is not touched). */
	void ResetRunState(const TCHAR* Why);

	UFUNCTION()
	void HandleFlowStateChanged(EStdFlowState NewState);

	UFUNCTION()
	void HandleRunLost();

	/** No transition running and time not Stopped in the current world. */
	bool CanTravelNow(const TCHAR* What) const;

	void TakeSnapshot(int32 Act);
	void WriteSave();
	void SetArchive(const FSuitArchive& InArchive, const TCHAR* Why);

	/** Adds Step to LastTravelSteps with StdCore's current bar and marker, while a travel of the run is pending. */
	void RecordTravelStep(FName Step);

	TArray<FTravelStepRecord> LastTravelSteps;

	TArray<FAreaSnapshot> Snapshots;
	/** The return's snapshot, when bHasReturnSnapshot. */
	FAreaSnapshot ReturnSnapshot;
	bool bHasReturnSnapshot = false;
	bool bReturningHome = false;
	FSuitArchive Archive;
	bool bEntityFound = false;
	bool bFirstContact = false;
	int32 LoadedSaveVersion = 0;
	int32 SaveWrites = 0;
	bool bLastSaveOk = false;

	TWeakObjectPtr<URunActListWidget> LossScreen;

	/** FPlatformTime of the last travel request; negative when none is pending. */
	double TravelRequestTime = -1.0;
	float LastTravelCovered = -1.f;
	float LastTravelReady = -1.f;
	float LastTravelFadeIn = -1.f;
	FText LastTravelLabel;
	float LastTravelMinCovered = 0.f;
};
