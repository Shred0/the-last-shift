// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "UI/StdScreenWidget.h"
#include "StdChoiceListWidget.generated.h"

class UStdChoiceListWidget;
class UTextBlock;
class UVerticalBox;

/** One entry of a choice list. */
USTRUCT(BlueprintType)
struct STDCORE_API FStdChoiceEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI")
	FText Label;

	/** A disabled entry is shown but cannot be chosen or focused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI")
	bool bEnabled = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStdOnChoiceSelected, UStdChoiceListWidget*, Screen, int32, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStdOnChoiceBack, UStdChoiceListWidget*, Screen);

/** A button that reports its index in the list it belongs to. */
UCLASS()
class STDCORE_API UStdChoiceButton : public UButton
{
	GENERATED_BODY()

public:
	/** Binds the click to Owner->ChooseEntry(Index). */
	void InitChoice(UStdChoiceListWidget* InOwner, int32 Index);

	int32 GetChoiceIndex() const { return ChoiceIndex; }

private:
	int32 ChoiceIndex = INDEX_NONE;
	TWeakObjectPtr<UStdChoiceListWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * A screen with a title, an optional body text and a list of labelled entries, optionally followed by a back entry.
 * The widget tree is built in C++ (no Widget Blueprint needed), with the names the theme styles: Border_Background,
 * Text_Title, Text_Body, Button_* and their labels, so it looks like the other Std Core menus. Keyboard and gamepad navigation and the focus highlight are
 * the screen base's.
 *
 * Choosing an entry broadcasts OnChoiceSelected with its index; the screen stays open (the owner decides). The back
 * entry, Escape and the gamepad's right face button broadcast OnBack and close the screen when it was opened from
 * another one; with bAllowBack false the back keys do nothing and no back entry is shown.
 */
UCLASS()
class STDCORE_API UStdChoiceListWidget : public UStdScreenWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetTitle(const FText& InTitle);

	/** Optional text shown between the title and the entries, wrapped to the column width. Empty = none. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetBody(const FText& InBody);

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FText GetBody() const { return Body; }

	/** Replaces the entries and focuses the first enabled one. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetEntries(const TArray<FStdChoiceEntry>& InEntries);

	/** Label of the back entry shown after the list; empty = no back entry (the back keys still work if allowed). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetBackLabel(const FText& InLabel);

	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void SetAllowBack(bool bInAllowBack);

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	int32 GetEntryCount() const { return Entries.Num(); }

	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	FText GetEntryLabel(int32 Index) const { return Entries.IsValidIndex(Index) ? Entries[Index].Label : FText::GetEmpty(); }

	/** The button of entry Index (INDEX_NONE: the back entry), or null. */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI")
	UButton* GetEntryButton(int32 Index) const;

	/** What a click on entry Index does (also used by the entry buttons). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI")
	void ChooseEntry(int32 Index);

	UPROPERTY(BlueprintAssignable, Category = "Std Core|UI")
	FStdOnChoiceSelected OnChoiceSelected;

	UPROPERTY(BlueprintAssignable, Category = "Std Core|UI")
	FStdOnChoiceBack OnBack;

protected:
	virtual void NativeOnInitialized() override;
	virtual UWidget* GetDefaultFocusWidget() const override;
	virtual bool NativeHandleBackAction() override;

	/** Builds the widget tree when the class has none (a native class, or a Blueprint left empty). */
	virtual void BuildDefaultTree();

	/** Called after the entry buttons were rebuilt and the theme applied to them. */
	virtual void NativeOnEntriesRebuilt() {}

	/** Width of the column of entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Std Core|UI", meta = (ClampMin = "100.0", ClampMax = "2000.0"))
	float ColumnWidth = 420.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UTextBlock> Text_Title;

	/** The body text above the entries; collapsed while the body is empty. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UTextBlock> Text_Body;

	/** Holds the entry buttons and the back entry. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Std Core|UI")
	TObjectPtr<UVerticalBox> Box_Entries;

private:
	void RebuildButtons();
	UStdChoiceButton* MakeButton(int32 Index, const FText& Label, bool bEnabled);

	TArray<FStdChoiceEntry> Entries;
	FText Title;
	FText Body;
	FText BackLabel;
	bool bAllowBack = true;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStdChoiceButton>> EntryButtons;

	UPROPERTY(Transient)
	TObjectPtr<UStdChoiceButton> BackButton;
};
