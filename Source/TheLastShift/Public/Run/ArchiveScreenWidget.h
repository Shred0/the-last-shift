// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdChoiceListWidget.h"
#include "ArchiveScreenWidget.generated.h"

/** One item of the suit's archive. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FArchiveItem
{
	GENERATED_BODY()

	/** True for a log entry, false for a part of the twin AI. */
	UPROPERTY(BlueprintReadOnly, Category = "Run|Archive")
	bool bLog = true;

	/** The log entry's Order, or the twin AI's part. */
	UPROPERTY(BlueprintReadOnly, Category = "Run|Archive")
	int32 Number = 0;
};

/**
 * The suit's archive, full screen, opened from the pause menu (URunPauseMenuWidget): StdCore's choice-list screen with
 * one entry per stored item, the log entries newest first ("Log <n>", n = the entry's order), then the twin AI's parts
 * in the order they were played ("Transmission <n>"), then Back. Choosing an item opens UArchiveItemWidget with its full
 * text. Reads the archive from the run subsystem and the text from the narrative tables every time it is built; holds
 * no copy of either. Back returns to the pause menu.
 */
UCLASS()
class THELASTSHIFT_API UArchiveScreenWidget : public UStdChoiceListWidget
{
	GENERATED_BODY()

public:
	/** The items of the archive as this screen lists them. */
	static TArray<FArchiveItem> ListItems(const UObject* WorldContext);

	/** The label of Item ("Log 8", "Transmission 1"). */
	FText GetItemLabel(const FArchiveItem& Item) const;

	/** The full text of Item, from the tables: a log entry, or the rows of a part in order, one paragraph each. */
	static FText GetItemText(const FArchiveItem& Item);

	/** Item of entry Index. */
	UFUNCTION(BlueprintPure, Category = "Run|Archive")
	FArchiveItem GetItem(int32 Index) const { return Items.IsValidIndex(Index) ? Items[Index] : FArchiveItem(); }

	/** Short UI labels; {N} is the number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|Archive")
	FText TitleLabel = NSLOCTEXT("TheLastShift", "ArchiveTitle", "Archive");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|Archive")
	FText LogLabel = NSLOCTEXT("TheLastShift", "ArchiveLog", "Log {N}");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|Archive")
	FText TransmissionLabel = NSLOCTEXT("TheLastShift", "ArchiveTransmission", "Transmission {N}");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|Archive")
	FText BackEntryLabel = NSLOCTEXT("TheLastShift", "ArchiveBack", "Back");

protected:
	virtual void NativeOnInitialized() override;

private:
	void Rebuild();

	UFUNCTION()
	void HandleChoice(UStdChoiceListWidget* Screen, int32 Index);

	TArray<FArchiveItem> Items;
};

/** One item of the archive, in full, as plain translated text: no barcode, no typing. Back returns to the list. */
UCLASS()
class THELASTSHIFT_API UArchiveItemWidget : public UStdChoiceListWidget
{
	GENERATED_BODY()

public:
	UArchiveItemWidget();

	UFUNCTION(BlueprintCallable, Category = "Run|Archive")
	void Configure(const FText& InTitle, const FText& InText, const FText& InBackLabel);
};
