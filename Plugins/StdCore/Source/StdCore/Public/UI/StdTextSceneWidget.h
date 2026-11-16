// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UI/StdScreenWidget.h"
#include "StdTextSceneWidget.generated.h"

class FSlateFontMeasure;
class UBorder;
class UButton;
class UHorizontalBox;
class USizeBox;
class UStdKeyPromptWidget;
class UStdTextSceneWidget;
class UTextBlock;

/** One entry of a text scene: what it says and how it looks. */
USTRUCT(BlueprintType)
struct STDCORE_API FStdTextSceneEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI")
	FLinearColor TextColor = FLinearColor::White;

	/** The whole screen behind the text while this entry is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI")
	FLinearColor BackgroundColor = FLinearColor::Black;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStdOnTextSceneEntryStarted, UStdTextSceneWidget*, Screen, int32, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStdOnTextSceneFinished, UStdTextSceneWidget*, Screen, bool, bSkipped);

namespace StdTextScene
{
	/**
	 * Text with its words wrapped onto lines no wider than MaxWidth (slate units) in Font: a word that would cross the
	 * edge starts the next line; a word wider than a line stays whole on a line of its own; existing line breaks are kept.
	 */
	STDCORE_API FString BreakLines(const FString& Text, const FSlateFontInfo& Font, float MaxWidth, const TSharedRef<FSlateFontMeasure>& Measure);

	/** Confirm keys of a text scene: Enter, Space and the gamepad's bottom face button. */
	STDCORE_API bool IsConfirmKey(const FKey& Key);

	/** The keys the on-screen controls show: the keyboard's confirm key (Enter) and back key (Escape). */
	STDCORE_API FKey ContinueControlKey();
	STDCORE_API FKey SkipControlKey();
}

/**
 * A full-screen scene of text: a list of entries shown one at a time, each typed out at CharsPerSecond in its own text
 * colour over its own background colour. Confirm (a confirm key or a click) completes an entry that is still being typed,
 * and on a complete entry shows the next one; after the last one the scene finishes. Back (Escape, the gamepad's right
 * face button) finishes it at once as skipped. OnEntryStarted reports every entry as it starts, OnSceneFinished how the
 * scene ended. A finished scene shows no text over the background it ended on and ignores input; the owner closes it.
 *
 * Each entry is laid out once, when it starts: its words are wrapped to ColumnWidth with the engine font at FontSize
 * and the block is centred on screen, so typing never moves a line or a word. The tree is built in C++ (Border_Background,
 * SizeBox_Line, Text_Line), with no texture and without the menu theme (each entry sets its colours). Typing runs on
 * the widget's own tick (real time); AdvanceSeconds drives it directly (tests).
 *
 * On-screen controls (bShowControls, off by default): while an entry is on screen, two controls in the bottom right
 * corner, one that goes on (the confirm key's prompt and ContinueLabel) and one that skips (the back key's prompt and
 * SkipLabel), in the entry's text colour at ControlsOpacity, ControlsMargin from the corner. A click on one does what its
 * key does, once; they never take the keyboard focus, so the keys keep working whatever the mouse did. With the option
 * off they are collapsed and the screen is as without them. They are part of the tree built in C++ (Box_Controls,
 * Button_Continue holding KeyPrompt_Continue, Button_Skip holding KeyPrompt_Skip).
 */
UCLASS()
class STDCORE_API UStdTextSceneWidget : public UStdScreenWidget
{
	GENERATED_BODY()

public:
	UStdTextSceneWidget(const FObjectInitializer& ObjectInitializer);

	/** Replaces the entries; the scene waits for StartScene. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetEntries(const TArray<FStdTextSceneEntry>& InEntries);

	/** Shows the first entry (a scene with no entry finishes at once, not skipped). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void StartScene();

	/** Completes the entry being typed; on a complete entry shows the next, or finishes after the last. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void Confirm();

	/** Finishes the scene at once, as skipped. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void Skip();

	/** Advances the typing by DeltaSeconds (the tick calls it with the frame time). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void AdvanceSeconds(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	int32 GetEntryCount() const { return Entries.Num(); }

	/** The entry on screen; INDEX_NONE before the start. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	int32 GetCurrentIndex() const { return CurrentIndex; }

	/** The current entry is still being typed. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	bool IsTyping() const;

	/** The part of the current entry shown so far (with the line breaks of its layout). */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FString GetVisibleText() const;

	/** The current entry as laid out: its whole text with the line breaks the wrap put in. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FString GetLaidOutText() const { return LaidOut; }

	/** Lines of the current entry's layout. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	int32 GetLineCount() const;

	/** Size of the current entry's laid-out block, slate units (its widest line, all its lines). */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FVector2D GetBlockSize() const { return BlockSize; }

	/** Colours on screen now: the text and the background. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FLinearColor GetShownTextColor() const;

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FLinearColor GetShownBackgroundColor() const;

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	bool WasSkipped() const { return bSkipped; }

	/** Typing speed, characters per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float CharsPerSecond = 40.f;

	/** Width of the column the text wraps in, slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float ColumnWidth = 960.f;

	/** Size of the engine font the entries are drawn with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI", meta = (ClampMin = "8", ClampMax = "96"))
	int32 FontSize = 26;

	/** A left click confirms like a confirm key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI")
	bool bClickConfirms = true;

	/** Shows the two on-screen controls (go on, skip) while an entry is on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetShowControls, Category = "Std Core|UI|Controls")
	bool bShowControls = false;

	/** Label of the control that goes on, after its key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI|Controls")
	FText ContinueLabel;

	/** Label of the control that skips, after its key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI|Controls")
	FText SkipLabel;

	/** Opacity of the controls, times the alpha of the entry's text colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI|Controls", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ControlsOpacity = 0.5f;

	/** Distance of the controls from the bottom and right edges of the screen, slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI|Controls", meta = (ClampMin = "0.0", ClampMax = "400.0"))
	float ControlsMargin = 40.f;

	UFUNCTION(BlueprintCallable, Category = "Std Core|UI|Controls")
	void SetShowControls(bool bInShowControls);

	/** Pushes the controls' option, labels, opacity, margin and the entry's colour to them (call after changing them at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI|Controls")
	void RefreshControls();

	/** The controls are on screen now (the option is on and an entry is shown). */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI|Controls")
	bool AreControlsShown() const;

	/** Colour the controls are drawn in now. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI|Controls")
	FLinearColor GetControlsColor() const;

	UPROPERTY(BlueprintAssignable, Category = "Std Core|UI")
	FStdOnTextSceneEntryStarted OnEntryStarted;

	UPROPERTY(BlueprintAssignable, Category = "Std Core|UI")
	FStdOnTextSceneFinished OnSceneFinished;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool NativeHandleBackAction() override;

	/** Builds the widget tree when the class has none. */
	virtual void BuildDefaultTree();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UBorder> Border_Background;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<USizeBox> SizeBox_Line;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UTextBlock> Text_Line;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI|Controls")
	TObjectPtr<UHorizontalBox> Box_Controls;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI|Controls")
	TObjectPtr<UButton> Button_Continue;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI|Controls")
	TObjectPtr<UButton> Button_Skip;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI|Controls")
	TObjectPtr<UStdKeyPromptWidget> KeyPrompt_Continue;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI|Controls")
	TObjectPtr<UStdKeyPromptWidget> KeyPrompt_Skip;

private:
	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleSkipClicked();

	UButton* BuildControl(FName ButtonName, FName PromptName, TObjectPtr<UStdKeyPromptWidget>& OutPrompt, const FKey& Key);

	void ShowEntry(int32 Index);
	void Finish(bool bInSkipped);
	void RefreshText();
	FSlateFontInfo GetFont() const;

	TArray<FStdTextSceneEntry> Entries;
	int32 CurrentIndex = INDEX_NONE;
	FString LaidOut;
	FVector2D BlockSize = FVector2D::ZeroVector;
	double Elapsed = 0.0;
	int32 ShownChars = 0;
	bool bFinished = false;
	bool bSkipped = false;
};
