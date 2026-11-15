// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdPauseMenuWidget.h"
#include "RunPauseMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

/**
 * The game's pause menu, set in StdCore's Pause Menu Widget Class setting: StdCore's pause menu with the game's own
 * entry "Archive" after Resume, shown only while the suit's archive holds at least one item
 * (URunSubsystem::HasArchiveItems); it opens UArchiveScreenWidget as a child screen, so closing it returns here and
 * closing the menu restores game input as StdCore does. The widget tree is built in C++ in the layout of StdCore's
 * WBP_PauseMenu, with the names its theme styles; StdCore's base class binds Resume, Settings, Main menu and Quit.
 */
UCLASS()
class THELASTSHIFT_API URunPauseMenuWidget : public UStdPauseMenuWidget
{
	GENERATED_BODY()

public:
	/** The "Archive" entry (collapsed while the archive is empty). */
	UButton* GetArchiveButton() const { return Button_Archive; }

	/** Short UI label of the entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|UI")
	FText ArchiveLabel = NSLOCTEXT("TheLastShift", "PauseArchive", "Archive");

	/** The menu's title, StdCore's pause menu title (a short UI label: StdCore keeps it only in its widget asset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|UI")
	FText TitleLabel = NSLOCTEXT("TheLastShift", "PauseTitle", "Paused");

	/** The title (Text_Title, styled by StdCore's theme like StdCore's own). */
	UTextBlock* GetTitleText() const { return Text_Title; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UButton> Button_Archive;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Run|UI")
	TObjectPtr<UTextBlock> Text_Title;

private:
	void BuildTree();
	UButton* AddButton(UVerticalBox* Box, const TCHAR* Name, const FText& Label, bool bLast = false);

	UFUNCTION()
	void RefreshEntries();

	UFUNCTION()
	void HandleArchiveClicked();
};
