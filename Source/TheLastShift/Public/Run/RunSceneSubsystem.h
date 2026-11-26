// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Narrative/NarrativeRows.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TextChannel/TextDegradation.h"
#include "TextChannel/TextTokens.h"
#include "RunSceneSubsystem.generated.h"

class UDataTable;

/** Brief 27: what decides the interruption at a text scene's cut, for the sound and for the static alike. */
namespace RunScenes
{
	/** A line of Voice is shown on the paradise's white background (Paradise, EngineerParadise). */
	THELASTSHIFT_API bool IsOnWhite(ESceneVoice Voice);

	/**
	 * The interruption: the line at Index is the scene's first change from a line on white to a line on black (its line
	 * before on white, it on black, and no such change before it).
	 */
	THELASTSHIFT_API bool IsInterruptionLine(const TArray<ESceneVoice>& Voices, int32 Index);
}
class URunEndingWidget;
class URunTextSceneWidget;
class UStdScreenWidget;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogScenes, Log, All);

/** One line of a text scene as it is shown: its text already degraded and filled (HomeAI) or as written (the others). */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FRunSceneLine
{
	GENERATED_BODY()

	/** The DT_Scenes row (or the DT_Endings row for an ending's text). */
	UPROPERTY(BlueprintReadOnly, Category = "Scenes")
	FName RowName;

	UPROPERTY(BlueprintReadOnly, Category = "Scenes")
	ESceneVoice Voice = ESceneVoice::HomeAI;

	/** The row's cue name (None: none). */
	UPROPERTY(BlueprintReadOnly, Category = "Scenes")
	FName Cue;

	UPROPERTY(BlueprintReadOnly, Category = "Scenes")
	FString Text;

	/** The level the text was shown at: the battery's for HomeAI, Full for the other voices. */
	UPROPERTY(BlueprintReadOnly, Category = "Scenes")
	ETextDegradation Level = ETextDegradation::Full;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnSceneLineStarted, FName, Scene, int32, Index, ESceneVoice, Voice, FName, Cue);

/** The prologue's or the ending's screen opened over the main menu (bOpen) or closed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSceneScreenChanged, FName, Scene, bool, bOpen);

/**
 * The run's full-screen text scenes: the prologue before area 1 and the ending after area 4, both on StdCore's text scene
 * screen, both without a pawn.
 *
 * - A scene is built from the DT_Scenes rows of one Scene, in ascending Order. Each line gets the colours of its voice
 *   (UTheLastShiftSettings::TextColors). HomeAI lines are degraded by the battery of the context with the text channel's
 *   function and the settings' thresholds, and their tokens are filled; a HomeAI line at Silent is left out. The other
 *   voices are shown as written.
 * - Prologue: "New game" opens it over the main menu (battery full: it is a new run) and, once it has finished or was
 *   skipped, starts the new run exactly as "New game" did before. No Prologue row: no scene.
 * - Ending: the main menu opens it when the run's travel out of the last area arrives there (SetEndingPending). The home
 *   AI's last message (the Ending rows, degraded by the run's battery now) above StdCore's choice list; "Inject the
 *   harvest" always, "Open the structure" only with at least OpenEndingTwinParts twin AI parts in the suit's archive; the
 *   chosen DT_Endings row in the Engineer voice, in full; then the main menu. The save is not touched.
 * - Every line that starts is reported by OnSceneLineStarted with its cue name; nothing listens to it yet.
 */
UCLASS()
class THELASTSHIFT_API URunSceneSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static URunSceneSubsystem* Get(const UObject* WorldContext);

	static FName PrologueScene() { return TEXT("Prologue"); }
	static FName EndingScene() { return TEXT("Ending"); }

	/** Rows of Scene in Table (FSceneRow), in ascending Order (ties by row name). */
	static TArray<FName> FindSceneRows(const UDataTable* Table, FName Scene);

	/** The lines of Scene from the settings' scenes table, with the numbers and the battery of Context. */
	TArray<FRunSceneLine> BuildScene(FName Scene, const FTextChannelContext& Context) const;

	/** The context of a new run: battery full, no home time. */
	static FTextChannelContext MakeNewRunContext();

	/** The context of the run now: home time and battery of the home state. */
	FTextChannelContext MakeRunContext() const;

	// --- Prologue ---

	/**
	 * Opens the prologue as a child screen of Parent (the main menu); when it finishes or is skipped the new run starts
	 * (URunSubsystem::StartNewRun). False, with nothing opened, when the scene has no line: the caller starts the run.
	 */
	bool OpenPrologue(UStdScreenWidget* Parent);

	// --- Ending ---

	/** The home AI's last message before the choice, at the run's battery now; empty at Silent or without Ending rows. */
	FText BuildEndingMessage(ETextDegradation* OutLevel = nullptr) const;

	/** "Open the structure" is offered: the suit's archive holds at least the settings' OpenEndingTwinParts twin AI parts. */
	UFUNCTION(BlueprintPure, Category = "Scenes")
	bool IsOpenEndingOffered() const;

	/** Opens the ending (the choice, then the chosen text) as a child screen of Parent (the main menu). */
	URunEndingWidget* OpenEnding(UStdScreenWidget* Parent);

	/**
	 * What the main menu opens for a pending ending: with an ending chosen in the battery's chamber (SetChosenEnding), that
	 * ending's text alone (no last message, no list); else the ending's list (OpenEnding). Null when nothing was opened.
	 */
	UStdScreenWidget* OpenPendingEnding(UStdScreenWidget* Parent);

	/**
	 * The label of an ending (Inject: "Inject the harvest", Open: "Open the structure"; empty for any other): the one owner of
	 * the two labels, read by the ending's list and by the chamber's two places.
	 */
	static FText GetEndingLabel(FName Ending);

	/** The ending chosen in the chamber, opened alone by the main menu with the pending ending (None: the list). */
	void SetChosenEnding(FName Ending);

	UFUNCTION(BlueprintPure, Category = "Scenes")
	FName GetChosenEnding() const { return ChosenEnding; }

	/** The ending's text for row Ending (Inject, Open) as a one-line Engineer scene; empty when the row has no text. */
	TArray<FRunSceneLine> BuildEndingText(FName Ending) const;

	/** Set by the run when its travel out of the last area starts; the main menu opens the ending on arrival. */
	void SetEndingPending(bool bPending);

	UFUNCTION(BlueprintPure, Category = "Scenes")
	bool IsEndingPending() const { return bEndingPending; }

	/** True once, when an ending is pending (the main menu then opens it). */
	bool ConsumePendingEnding();

	// --- Lines ---

	/** Called by the scene screen when a line starts: logs it and broadcasts OnSceneLineStarted. */
	void NotifyLineStarted(FName Scene, int32 Index, const FRunSceneLine& Line);

	/** A line of a scene started on screen, with its cue name (None: none). The hook for the prologue's sound and effects. */
	UPROPERTY(BlueprintAssignable, Category = "Scenes")
	FOnSceneLineStarted OnSceneLineStarted;

	/** OpenPrologue or OpenEnding opened its screen (bOpen), or that screen closed (back to the main menu). */
	UPROPERTY(BlueprintAssignable, Category = "Scenes")
	FOnSceneScreenChanged OnSceneScreenChanged;

	/** A screen opened by OpenPrologue or OpenEnding is still shown (it has neither closed nor gone with its world). */
	UFUNCTION(BlueprintPure, Category = "Scenes")
	bool IsSceneScreenOpen() const;

private:
	/** Tracks Screen until it closes, and says it opened. */
	void TrackScreen(UStdScreenWidget* Screen, FName Scene);

	UFUNCTION()
	void HandleSceneScreenClosed(UStdScreenWidget* Screen);

	bool bEndingPending = false;

	/** The ending chosen in the chamber, with the pending ending; cleared with it and once opened. */
	FName ChosenEnding;

	/** The screens OpenPrologue and OpenEnding opened, and their scene, until they close. */
	TMap<TWeakObjectPtr<UStdScreenWidget>, FName> OpenScreens;
};
