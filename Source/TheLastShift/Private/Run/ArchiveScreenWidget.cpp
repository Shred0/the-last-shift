// SPDX-License-Identifier: MIT

#include "Run/ArchiveScreenWidget.h"

#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TwinWriting.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ArchiveScreenWidget)

TArray<FArchiveItem> UArchiveScreenWidget::ListItems(const UObject* WorldContext)
{
	TArray<FArchiveItem> Out;
	const URunSubsystem* Run = URunSubsystem::Get(WorldContext);
	if (!Run)
	{
		return Out;
	}
	const FSuitArchive& Archive = Run->GetArchive();
	TArray<int32> Logs = Archive.LogOrders;
	Logs.Sort([](int32 A, int32 B) { return A > B; });
	for (const int32 Order : Logs)
	{
		FArchiveItem& Item = Out.AddDefaulted_GetRef();
		Item.bLog = true;
		Item.Number = Order;
	}
	for (int32 Part = 1; Part <= Archive.TwinPartsPlayed; ++Part)
	{
		FArchiveItem& Item = Out.AddDefaulted_GetRef();
		Item.bLog = false;
		Item.Number = Part;
	}
	return Out;
}

FText UArchiveScreenWidget::GetItemLabel(const FArchiveItem& Item) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("N"), Item.Number);
	return FText::Format(Item.bLog ? LogLabel : TransmissionLabel, Args);
}

FText UArchiveScreenWidget::GetItemText(const FArchiveItem& Item)
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	if (Item.bLog)
	{
		return TwinWriting::FindLogText(Settings->LoadLogsTable(), Item.Number);
	}
	TArray<FString> Paragraphs;
	for (const FText& Text : TwinWriting::GetTwinPartTexts(Settings->LoadTwinAITable(), Item.Number))
	{
		Paragraphs.Add(Text.ToString());
	}
	return FText::FromString(FString::Join(Paragraphs, TEXT("\n\n")));
}

void UArchiveScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	OnChoiceSelected.AddUniqueDynamic(this, &UArchiveScreenWidget::HandleChoice);
	Rebuild();
}

void UArchiveScreenWidget::Rebuild()
{
	Items = ListItems(this);
	TArray<FStdChoiceEntry> ChoiceEntries;
	for (const FArchiveItem& Item : Items)
	{
		FStdChoiceEntry& Entry = ChoiceEntries.AddDefaulted_GetRef();
		Entry.Label = GetItemLabel(Item);
	}
	SetTitle(TitleLabel);
	SetBody(FText::GetEmpty());
	SetEntries(ChoiceEntries);
	SetBackLabel(BackEntryLabel);
}

void UArchiveScreenWidget::HandleChoice(UStdChoiceListWidget* Screen, int32 Index)
{
	if (!Items.IsValidIndex(Index))
	{
		return;
	}
	if (UArchiveItemWidget* ItemScreen = Cast<UArchiveItemWidget>(OpenChildScreen(UArchiveItemWidget::StaticClass())))
	{
		ItemScreen->Configure(GetItemLabel(Items[Index]), GetItemText(Items[Index]), BackEntryLabel);
	}
}

UArchiveItemWidget::UArchiveItemWidget()
{
	// Room for a few hundred characters of plain text.
	ColumnWidth = 900.f;
}

void UArchiveItemWidget::Configure(const FText& InTitle, const FText& InText, const FText& InBackLabel)
{
	SetTitle(InTitle);
	SetBody(InText);
	SetEntries({});
	SetBackLabel(InBackLabel);
}
