// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdMainMenuWidget.h"
#include "RunMainMenuWidget.generated.h"

class UButton;

/**
 * The game's main menu, set in StdCore's Main Menu Widget Class setting: StdCore's main menu with the run's entries.
 * "Continue" and "Select act" when a saved run exists, "New game" always, then StdCore's own entries (Settings,
 * Credits, Quit); StdCore's Play entry is not built. The widget tree is built in C++ in the layout of StdCore's menu
 * and with the names its theme styles; StdCore's base class binds its own entries. Continue / Select act follow
 * URunSubsystem::OnSnapshotsChanged.
 */
UCLASS()
class THELASTSHIFT_API URunMainMenuWidget : public UStdMainMenuWidget
{
	GENERATED_BODY()

public:
	/** The menu button named Name (Button_Continue, Button_SelectAct, Button_NewGame, Button_Settings, ...), or null. */
	UButton* FindMenuButton(FName Name) const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual UWidget* GetDefaultFocusWidget() const override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_Continue;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_SelectAct;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_NewGame;

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
