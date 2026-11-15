// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "TextChannel/TextDegradation.h"
#include "UI/StdChoiceListWidget.h"
#include "RunEndingWidget.generated.h"

/**
 * The ending's choice, on StdCore's choice-list screen over black: the home AI's last message above the list (degraded by
 * the run's battery, in the home AI's colour; nothing at Silent), then "Inject the harvest" and, only when the suit's
 * archive holds enough twin AI parts, "Open the structure". No title, no way back. Choosing an entry opens that ending's
 * text (the DT_Endings row, Engineer voice, in full) on top; when the text ends, this screen closes and the main menu
 * shows again. Reads the battery and the archive from the run every time it is configured; holds no copy.
 */
UCLASS()
class THELASTSHIFT_API URunEndingWidget : public UStdChoiceListWidget
{
	GENERATED_BODY()

public:
	/** Fills the screen from the run's state now. */
	void Configure();

	/** The DT_Endings row of entry Index (Inject, Open); None for no entry. */
	UFUNCTION(BlueprintPure, Category = "Scenes")
	FName GetEndingOfEntry(int32 Index) const { return EntryEndings.IsValidIndex(Index) ? EntryEndings[Index] : NAME_None; }

	/** The level the home AI's last message was shown at. */
	UFUNCTION(BlueprintPure, Category = "Scenes")
	ETextDegradation GetMessageLevel() const { return MessageLevel; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnEntriesRebuilt() override;

private:
	UFUNCTION()
	void HandleChoice(UStdChoiceListWidget* Screen, int32 Index);

	TArray<FName> EntryEndings;
	ETextDegradation MessageLevel = ETextDegradation::Full;
};
