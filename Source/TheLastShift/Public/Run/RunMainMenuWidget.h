// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdMainMenuWidget.h"
#include "RunMainMenuWidget.generated.h"

class UButton;
class UMenuBackdropWidget;

/**
 * The game's main menu, set in StdCore's Main Menu Widget Class setting: StdCore's main menu with the run's entries.
 * "Continue" and "Select act" when a saved run exists, "New game" always, then StdCore's own entries (Settings,
 * Credits, Quit); StdCore's Play entry is not built. The widget tree is built in C++ in the layout of StdCore's menu
 * and with the names its theme styles; StdCore's base class binds its own entries. Continue / Select act follow
 * URunSubsystem::OnSnapshotsChanged. "New game" opens the prologue over the menu (URunSceneSubsystem), which starts the
 * new run when it ends; without a prologue line it starts the run at once. On its first tick after the run's travel out
 * of the last area, the menu opens the ending over itself. With MenuBackdrop.bMenuBackdrop on it adds the moving background
 * (UMenuBackdropWidget) to the viewport under itself and removes it with itself.
 */
UCLASS()
class THELASTSHIFT_API URunMainMenuWidget : public UStdMainMenuWidget
{
	GENERATED_BODY()

public:
	/** The menu button named Name (Button_Continue, Button_SelectAct, Button_NewGame, Button_Settings, ...), or null. */
	UButton* FindMenuButton(FName Name) const;

	/** Adds the moving background under the menu when MenuBackdrop.bMenuBackdrop is on, removes it when off. */
	void RefreshBackdrop();

	/** The moving background, or null when it is off. */
	UMenuBackdropWidget* GetBackdrop() const { return Backdrop; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual UWidget* GetDefaultFocusWidget() const override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_Continue;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_SelectAct;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_NewGame;

	UPROPERTY(Transient)
	TObjectPtr<UMenuBackdropWidget> Backdrop;

private:
	void BuildTree();
	UButton* AddButton(class UVerticalBox* Box, const TCHAR* Name, const FText& Label, bool bLast = false);

	UFUNCTION()
	void RefreshEntries();

	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleSelectActClicked();

	UFUNCTION()
	void HandleNewGameClicked();
};
