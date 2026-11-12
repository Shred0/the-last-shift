#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Flow/StdTransitionProgress.h"
#include "Misc/Attribute.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StdFlowSubsystem.generated.h"

class FStdInputBlocker;
class SWidget;
class UGameViewportClient;
class UWorld;
struct FActorsInitializedParams;
struct FSlateColor;

UENUM(BlueprintType)
enum class EStdFlowState : uint8
{
	Idle,
	FadingOut,
	Loading,
	WaitingToFadeIn,
	FadingIn
};

/** Options of one map transition. */
USTRUCT(BlueprintType)
struct STDCORE_API FStdTravelOptions
{
	GENERATED_BODY()

	/** Short text shown in the middle of the covered screen until the fade in starts. Empty = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Flow")
	FText Label;

	/** The screen stays fully covered at least this long, counted from the end of the fade out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Flow", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float MinCoveredSeconds = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStdFlowStateChanged, EStdFlowState, NewState);

/**
 * Moving between maps with a fade: fade out, load, fade in.
 *
 * The fade is a Slate widget added to the game viewport client, not a UMG widget: the viewport client outlives
 * the world, so the overlay stays on screen while the map loads (UMG widgets are removed with their world).
 * While a transition runs, player input is blocked and further travel requests are ignored.
 * Fades run on real time, so they work with the game paused and ignore time dilation.
 *
 * Cover holds: anything that needs the screen to stay covered after the new map has loaded (content still being
 * built, for example) adds a named hold and removes it when done; the fade in waits for every hold, for the
 * transition's MinCoveredSeconds and for FadeInDelay. Adding a hold while no transition runs covers the screen at
 * once. Holds are cleared when a new map arrives and when TravelTimeoutSeconds is exceeded (with an error).
 *
 * Progress bar and marker (both off by default, see UStdProjectSettings): the bar shows the largest progress reported
 * in the current covered period. The transition reports 0 when it covers the screen, TransitionMapLoadedProgress when the
 * new map has arrived and 1 when the destination is ready (map arrived, no cover hold left); a project reports the steps
 * in between with ReportTransitionProgress. The marker is the fraction of the transition's MinCoveredSeconds that has
 * passed since the screen became fully covered. Both run on real time.
 */
UCLASS()
class STDCORE_API UStdFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Fades out, opens Level and fades in. Returns false, without starting anything, when a transition is already
	 * running, when Level is empty or when its package does not exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	bool TravelToLevel(TSoftObjectPtr<UWorld> Level);

	/** TravelToLevel with a label on the covered screen and a minimum covered time. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	bool TravelToLevelWithOptions(TSoftObjectPtr<UWorld> Level, const FStdTravelOptions& Options);

	/** Travels to UStdProjectSettings::MainMenuMap. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	bool ReturnToMainMenu();

	/** Keeps the screen covered until RemoveCoverHold(Reason). Covers it at once when no transition is running. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	void AddCoverHold(FName Reason);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	void RemoveCoverHold(FName Reason);

	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	bool IsCoverHeld() const { return CoverHolds.Num() > 0; }

	/** Seconds the screen has been fully covered in the current transition; 0 when it is not covered. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow", meta = (Units = "s"))
	float GetCoveredSeconds() const;

	/**
	 * Reports loading progress of the current transition, 0..1 (clamped): the bar shows the largest fraction reported so
	 * far and never goes back. Ignored when no transition runs. Report real steps only (content built, data ready).
	 */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	void ReportTransitionProgress(float Fraction);

	/** Progress of the current covered period, 0..1 (0 when no transition runs). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	float GetTransitionProgress() const { return IsTransitioning() ? Progress.GetBarFraction() : 0.f; }

	/** Horizontal position of the marker, 0 (left edge) to 1 (right edge): the fraction of MinCoveredSeconds that has passed. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	float GetTransitionMarkerFraction() const { return IsTransitioning() ? Progress.GetMarkerFraction() : 0.f; }

	/** The progress bar is drawn now (switched on and the overlay visible). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	bool IsTransitionProgressBarShown() const;

	/** The marker is drawn now (switched on, the overlay visible and a minimum covered time greater than zero). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	bool IsTransitionMarkerShown() const;

	/** The minimum covered time of the current transition (0 when none runs). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow", meta = (Units = "s"))
	float GetMinCoveredSeconds() const { return IsTransitioning() ? MinCoveredSeconds : 0.f; }

	/** Opacity of the covered screen, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	float GetOverlayAlpha() const { return Alpha; }

	/** Every state change, on the game thread. */
	UPROPERTY(BlueprintAssignable, Category = "Std Core|Flow")
	FOnStdFlowStateChanged OnStateChanged;

	/** Travels to UStdProjectSettings::GameplayMap. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	bool StartGameplay();

	/** Fades out, then quits the game (ends the session in Play In Editor). Ignored during a transition. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Flow")
	void QuitGame();

	/** True from the start of a fade out to the end of the fade in. Pause and travel are refused meanwhile. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	bool IsTransitioning() const { return State != EStdFlowState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Std Core|Flow")
	EStdFlowState GetState() const { return State; }

	/**
	 * The label of the covered screen: the project's transition font, centred, wrapped onto several lines beyond
	 * UStdProjectSettings::TransitionLabelMaxWidth.
	 */
	static TSharedRef<SWidget> MakeLabelWidget(const TAttribute<FText>& InText, const TAttribute<FSlateColor>& InColor);

private:
	bool Tick(float DeltaTime);

	void BeginFadeOut();
	void HandleFadeOutFinished();
	void HandleWorldInitialized(const FActorsInitializedParams& Params);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void FinishTransition();
	void AbortTransition(const FString& Reason);
	void SetState(EStdFlowState NewState);

	void EnsureOverlay();
	void RemoveOverlay();
	void SetInputBlocked(bool bBlocked);

	FLinearColor GetOverlayColor() const;
	FSlateColor GetLabelColor() const;
	bool IsOverlayVisible() const { return Alpha > 0.f; }

	/** True when the fade in may start: delay elapsed, minimum covered time reached, no holds. */
	bool CanStartFadeIn() const;

	/** Covers the screen at once (used by AddCoverHold outside a transition). */
	void CoverNow();

	/** The destination is ready when the new map has arrived and no cover hold is left: the bar is full. */
	void UpdateReadyProgress();

	/** Bar and marker of the current covered period. */
	FStdTransitionProgress Progress;

	/** Brush of UStdProjectSettings::TransitionMarkerImage (no resource: plain rectangle). Keeps the image loaded. */
	UPROPERTY(Transient)
	FSlateBrush MarkerBrush;

	TSet<FName> CoverHolds;

	/** Label and minimum covered time of the current transition. */
	FText Label;
	float MinCoveredSeconds = 0.f;

	/** FPlatformTime when the screen became fully covered; negative when it is not. */
	double CoveredSince = -1.0;

	EStdFlowState State = EStdFlowState::Idle;

	/** Opacity of the overlay, 0..1. */
	float Alpha = 0.f;

	/** Seconds left in WaitingToFadeIn. */
	float FadeInDelayRemaining = 0.f;

	/** FPlatformTime when the current state began, for the travel timeout. */
	double StateStartTime = 0.0;

	bool bQuitAfterFadeOut = false;

	TSoftObjectPtr<UWorld> PendingLevel;

	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;
	TSharedPtr<FStdInputBlocker> InputBlocker;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldInitializedHandle;
	FDelegateHandle TravelFailureHandle;
};
