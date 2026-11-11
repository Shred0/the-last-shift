// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "StdKeyPromptWidget.generated.h"

class UBorder;
class UInputAction;
class UStdInputSubsystem;
class UTextBlock;

/**
 * A key prompt: the key currently bound to an input action, in an outlined key cap, followed by an optional label
 * ("[E] Open"). The key is read from the owning player's live mappings, so a remap made in the Controls tab shows at
 * once, without a restart (UStdInputSubsystem::OnKeyMappingsChanged).
 *
 * Keyboard and mouse keys are shown unless bPreferGamepad; an action with no key of the preferred kind shows its other
 * key. Std Core does not track which device the player used last, so whoever owns the prompt sets bPreferGamepad when
 * it knows. "Unbound" when the action has no key.
 *
 * The tree (Border_Key holding Text_Key, then Text_Label) is built in C++ when the class is used without a widget
 * Blueprint; a Blueprint subclass may provide its own widgets with those names. Fonts come from the Std Core UI theme.
 * Purely visual: never takes focus or clicks.
 */
UCLASS(meta = (DisplayName = "Key Prompt"))
class STDCORE_API UStdKeyPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Action whose key is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetInputAction, Category = "Std Core|Key Prompt")
	TObjectPtr<const UInputAction> InputAction;

	/** Text after the key cap. Empty hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetLabel, Category = "Std Core|Key Prompt")
	FText Label;

	/** Show the gamepad key instead of the keyboard/mouse key when the action has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPreferGamepad, Category = "Std Core|Key Prompt")
	bool bPreferGamepad = false;

	/** Size of the key and label text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Key Prompt", meta = (ClampMin = "8", ClampMax = "64"))
	int32 FontSize = 18;

	/** Colour of the key, its cap outline and the label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Key Prompt")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.85f);

	/** Outline width of the key cap; 0 hides the cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Key Prompt", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float CapOutline = 1.f;

	/** Space between the key cap and the label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|Key Prompt", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float LabelGap = 10.f;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Key Prompt")
	void SetInputAction(const UInputAction* InAction);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Key Prompt")
	void SetLabel(const FText& InLabel);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Key Prompt")
	void SetPreferGamepad(bool bInPreferGamepad);

	/** Reads the bound key again and updates the cap (called on its own after a remap). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Key Prompt")
	void RefreshKey();

	/** Pushes FontSize, Color, CapOutline and LabelGap to the widgets (call after changing them at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Key Prompt")
	void ApplyStyle();

	/** The key the cap shows now (invalid when unbound). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Key Prompt")
	FKey GetDisplayedKey() const { return DisplayedKey; }

	/** The text the cap shows now. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Key Prompt")
	FText GetKeyText() const;

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|Key Prompt", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Border_Key;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|Key Prompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Key;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|Key Prompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Label;

private:
	UFUNCTION()
	void HandleKeyMappingsChanged();

	void BindInputSubsystem();
	void UnbindInputSubsystem();
	UStdInputSubsystem* FindInputSubsystem() const;

	FKey DisplayedKey;

	/** Frames left during which the key is read again: a remap rebuilds the player's mappings on the next tick. */
	int32 PendingRefreshFrames = 0;

	TWeakObjectPtr<UStdInputSubsystem> BoundSubsystem;
};
