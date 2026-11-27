// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdScreenWidget.h"
#include "ChoiceDialogWidget.generated.h"

class AChoiceTerminal;
class APlayerController;
class UBorder;
class UButton;
class UProgressBar;
class UStdKeyPromptWidget;
class UTextBlock;
class UTimeStateSubsystem;
class UVerticalBox;

/** What holds a button of the choice's dialog down. */
UENUM(BlueprintType)
enum class EChoiceHoldSource : uint8
{
	None,
	/** The left mouse button, pressed on the button and kept on it. */
	Mouse,
	/** The confirm key, with the button focused. */
	Key
};

/**
 * The choice's dialog of the battery's chamber, opened by its terminal (AChoiceTerminal) on a press of Interact: a panel
 * at the centre of the screen, over the game's view, in StdCore's style (Border_Panel and Button_* themed by name, the
 * labels in the theme's button font), with one button for each ending the terminal offers (AChoiceTerminal::GetEndings,
 * labelled by URunSceneSubsystem::GetEndingLabel, the list's own) and nothing else to read but the on-screen back control
 * (the back key's prompt and "Back", as the text scenes show theirs). The tree is built in C++.
 *
 * - Opening: no button is focused (the screen itself holds the focus; the cursor is put below the panel, so the mouse
 *   moves the focus only when it reaches a button); the confirm key does nothing until a button is reached with the keys
 *   (the navigation keys and Tab reach the first or the last one; then the menus' navigation) or the mouse. Input goes to
 *   the UI only, the cursor is shown and the player controller's pause is held (bCanPause): the pawn does not move, look,
 *   scan, Stop or use the flashlight, and the pause menu does not open. Time stays Normal and goes on.
 * - A button chooses when held for UTheLastShiftSettings::ChoiceHoldSeconds of the run's clock: the left mouse button kept
 *   down on it, or the confirm key kept down while it is focused. While held it fills from its left to its right. Letting
 *   go, the cursor off the button (a mouse hold), the focus leaving it (a key hold) or the back key before the end cancel
 *   the hold: the fill empties, nothing is chosen, and the button must be pressed again. A click or a tap chooses nothing.
 * - The back key (and a click on the back control) closes the dialog when no hold is in progress; during a hold it cancels
 *   the hold only. Closed, the input, the cursor and the pause are given back as they were in game, and the terminal
 *   offers again.
 * - Held through: the terminal is told (AChoiceTerminal::Choose) and the dialog closes.
 */
UCLASS()
class THELASTSHIFT_API UChoiceDialogWidget : public UStdScreenWidget
{
	GENERATED_BODY()

public:
	UChoiceDialogWidget(const FObjectInitializer& ObjectInitializer);

	/** Opens the dialog of Terminal for Player: null when either is missing or the terminal offers no ending. */
	static UChoiceDialogWidget* Open(APlayerController* Player, AChoiceTerminal* Terminal);

	/** Closes the dialog and gives the input back (Why is logged). Does nothing once closed. */
	UFUNCTION(BlueprintCallable, Category = "Choice Dialog")
	void Close(const FString& Why);

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	bool IsClosed() const { return bClosed; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	int32 GetButtonCount() const { return Buttons.Num(); }

	/** The DT_Endings row of button Index (None for no button). */
	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	FName GetEndingOfButton(int32 Index) const { return Endings.IsValidIndex(Index) ? Endings[Index] : NAME_None; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UButton* GetButton(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UTextBlock* GetButtonLabel(int32 Index) const;

	/** The fill of button Index (its percent is the hold's fraction). */
	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UProgressBar* GetButtonFill(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UBorder* GetPanel() const { return Border_Panel; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UButton* GetBackControl() const { return Button_Back; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	UStdKeyPromptWidget* GetBackPrompt() const { return KeyPrompt_Back; }

	/** The button that holds the keyboard focus now (INDEX_NONE: none). */
	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	int32 GetFocusedButton() const;

	/** The button held now (INDEX_NONE: none), what holds it and how far the hold is (0..1). */
	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	int32 GetHeldButton() const { return HeldIndex; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	EChoiceHoldSource GetHoldSource() const { return HoldSource; }

	UFUNCTION(BlueprintPure, Category = "Choice Dialog")
	float GetHoldFraction() const;

	/** Holds started and cancelled so far, and why the last one was cancelled (tests). */
	int32 GetHoldsStarted() const { return HoldsStarted; }
	int32 GetHoldsCancelled() const { return HoldsCancelled; }
	const FString& GetLastCancelReason() const { return LastCancelReason; }

	/** The ending chosen when a hold went through (None before). */
	FName GetChosenEnding() const { return ChosenEnding; }

	/** The on-screen back control's label: a short UI label, its one owner. */
	static FText GetBackLabel();

	/** Width of the column of buttons, Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice Dialog", meta = (ClampMin = "200.0", ClampMax = "1200.0"))
	float ColumnWidth = 460.f;

	/** Space inside the panel around the buttons and the back control, Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice Dialog", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float PanelPadding = 32.f;

	/** Opacity of the fill over a held button, in the style's text colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice Dialog", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float FillOpacity = 0.45f;

	/** Opacity of the back control, in the style's text colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice Dialog", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BackControlOpacity = 0.6f;

	/** Z order of the dialog in the viewport (under the pause menu's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice Dialog", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 DialogZOrder = 40;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual UWidget* GetDefaultFocusWidget() const override { return nullptr; }
	virtual bool NativeHandleBackAction() override;
	virtual void NativeOnScreenClosing() override;

private:
	void BuildTree();
	void Configure(AChoiceTerminal* InTerminal);
	void StyleAfterTheme();
	void TakeInput(APlayerController* Player);
	void GiveInputBack();
	void PutCursorBelowPanel();
	void StartHold(int32 Index, EChoiceHoldSource Source);
	void CancelHold(const TCHAR* Why);
	void SetFill(int32 Index, float Fraction);
	double RunNow() const;

	UFUNCTION()
	void HandleBackClicked();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Border_Panel;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Box_Buttons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> Button_Back;

	UPROPERTY(Transient)
	TObjectPtr<UStdKeyPromptWidget> KeyPrompt_Back;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Buttons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Labels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProgressBar>> Fills;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TArray<FName> Endings;
	/** A button whose press was cancelled: it holds again only once released. */
	TArray<bool> PressSpent;

	TWeakObjectPtr<AChoiceTerminal> Terminal;
	TWeakObjectPtr<APlayerController> Owner;
	bool bCanPauseBefore = true;
	bool bInputTaken = false;
	bool bClosed = false;
	bool bCursorPlaced = false;

	int32 HeldIndex = INDEX_NONE;
	EChoiceHoldSource HoldSource = EChoiceHoldSource::None;
	double HoldStartRun = 0.0;
	int32 HoldsStarted = 0;
	int32 HoldsCancelled = 0;
	FString LastCancelReason;
	FName ChosenEnding;
};
