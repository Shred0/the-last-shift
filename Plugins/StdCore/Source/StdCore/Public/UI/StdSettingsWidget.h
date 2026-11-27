#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "UI/StdScreenWidget.h"
#include "StdSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UPanelWidget;
class USlider;
class UStdKeyBindingRowWidget;
class UStdSettingsSubsystem;
class UStdInputSubsystem;
class UTextBlock;
class UWidgetSwitcher;

/**
 * Settings, shared by the main menu and the pause menu. Two tabs in a Widget Switcher:
 * index 0 "Audio and video", index 1 "Controls".
 *
 * Volume sliders apply while they move; window mode and quality apply on each press. Back (button, Escape,
 * gamepad B) saves and closes. The Controls tab is built at runtime from the remappable mappings: one category
 * header and one UStdKeyBindingRowWidget per entry. Key changes are saved as soon as they are made.
 *
 * The video options' rows are built at runtime too, after the quality row (or in Box_VideoOptions), each but the ones
 * the project hides (UStdProjectSettings::HiddenVideoOptions): the frame-rate limit (a button that moves to the next
 * limit of UStdProjectSettings::FrameRateLimits on each press, disabled while the Unlimited box beside it is ticked),
 * the anti-aliasing (a button that moves to the next method) and V-Sync (a box). Each applies at once and is saved
 * with the rest. The rows take the theme like the designer's; the tab scrolls when they do not fit, keeping the focused
 * row in view.
 */
UCLASS(Abstract)
class STDCORE_API UStdSettingsWidget : public UStdScreenWidget
{
	GENERATED_BODY()

public:
	/** Called by a row when the player picked a key for it. */
	void HandleRowKeySelected(UStdKeyBindingRowWidget* Row, const FKey& NewKey);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual UWidget* GetDefaultFocusWidget() const override;
	virtual void NativeOnScreenClosing() override;

	/** The widget placed above the rows of a category. Default: a text block. Override in Blueprint to style it. */
	UFUNCTION(BlueprintNativeEvent, Category = "Std Core|Settings")
	UWidget* CreateCategoryHeader(const FText& Category);

	// Tabs

	/** Index 0: Audio and video. Index 1: Controls. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher_Tabs;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_TabAudioVideo;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_TabControls;

	// Audio and video

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<USlider> Slider_MasterVolume;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<USlider> Slider_MusicVolume;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<USlider> Slider_SfxVolume;

	/** Optional: the master volume as a percentage. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MasterVolumeValue;

	/** Optional: the music volume as a percentage. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MusicVolumeValue;

	/** Optional: the effects volume as a percentage. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_SfxVolumeValue;

	/** Each press moves to the next window mode: fullscreen, borderless window, window. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_WindowMode;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_WindowMode;

	/** Each press moves to the next quality preset: Low, Medium, High, Epic. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_Quality;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Quality;

	/** Optional: the panel the video options' rows are added to. Default: the panel of the quality row, after it. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Box_VideoOptions;

	// The video options' rows, built at runtime (null when the project hides the row).

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UButton> Button_FrameRateLimit;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UTextBlock> Text_FrameRateLimit;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UCheckBox> CheckBox_FrameRateUnlimited;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UButton> Button_AntiAliasing;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UTextBlock> Text_AntiAliasing;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UCheckBox> CheckBox_VSync;

	// Controls

	/** Filled at runtime (a Vertical Box, or a Scroll Box for long lists). Leave it empty in the designer. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UPanelWidget> Box_KeyBindings;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_ResetDefaults;

	/** Explains swaps, refusals and resets. */
	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_KeyBindingMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Std Core|UI", meta = (BindWidget))
	TObjectPtr<UButton> Button_Back;

	// Texts

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText FullscreenText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText WindowedFullscreenText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText WindowedText;

	/** Names of quality levels 0..3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	TArray<FText> QualityNames;

	/** Shown when the saved quality matches none of the presets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText CustomQualityText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText DefaultsRestoredText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText FrameRateLimitLabel;

	/** Beside the box next to the frame-rate limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText FrameRateUnlimitedLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText AntiAliasingLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	FText VSyncLabel;

	/** Names of the anti-aliasing methods by r.AntiAliasingMethod's number: none, FXAA, TAA, MSAA, TSR. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings")
	TArray<FText> AntiAliasingNames;

	/** Volume change per gamepad/keyboard step on the sliders. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Std Core|Settings", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float VolumeStepSize = 0.05f;

public:
	UStdSettingsWidget(const FObjectInitializer& ObjectInitializer);

private:
	UStdSettingsSubsystem* GetSettingsSubsystem() const;
	UStdInputSubsystem* GetInputSubsystem() const;

	void SelectTab(int32 Index);
	void RefreshAudioVideo();
	void RefreshVolumeTexts();
	void RebuildKeyBindings();
	void RefreshKeyBindingRows();
	void SetKeyBindingMessage(const FText& Message);
	FText GetWindowModeText(EWindowMode::Type WindowMode) const;
	FText GetQualityText(int32 Level) const;
	void BuildVideoRows();
	void RefreshVideo();

	UFUNCTION()
	void HandleFrameRateLimitClicked();

	UFUNCTION()
	void HandleFrameRateUnlimitedChanged(bool bIsChecked);

	UFUNCTION()
	void HandleAntiAliasingClicked();

	UFUNCTION()
	void HandleVSyncChanged(bool bIsChecked);

	UFUNCTION()
	void HandleTabAudioVideoClicked();

	UFUNCTION()
	void HandleTabControlsClicked();

	UFUNCTION()
	void HandleMasterVolumeChanged(float Value);

	UFUNCTION()
	void HandleMusicVolumeChanged(float Value);

	UFUNCTION()
	void HandleSfxVolumeChanged(float Value);

	UFUNCTION()
	void HandleWindowModeClicked();

	UFUNCTION()
	void HandleQualityClicked();

	UFUNCTION()
	void HandleResetDefaultsClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleKeyMappingsChanged();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStdKeyBindingRowWidget>> Rows;
};
