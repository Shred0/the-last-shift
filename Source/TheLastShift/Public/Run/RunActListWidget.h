// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UI/StdChoiceListWidget.h"
#include "RunActListWidget.generated.h"

UENUM(BlueprintType)
enum class ERunActListMode : uint8
{
	/** From the main menu: the acts reached, then Back. */
	SelectAct,
	/** After the battery reached zero: fades in over black, the acts reached, then Main menu; no way back to the game. */
	RunLost
};

/**
 * The list of acts reached (one entry per run snapshot, "Area N — B%" with the battery at that act's entry), on StdCore's
 * choice-list screen. In RunLost mode the body above the list is the Loss row of DT_Endings, in full. Choosing an act restarts the run from it (URunSubsystem::RestartFromAct). Reads the snapshots
 * from the run subsystem every time it is configured; holds no copy of them.
 */
UCLASS()
class THELASTSHIFT_API URunActListWidget : public UStdChoiceListWidget
{
	GENERATED_BODY()

public:
	/** Fills the screen for Mode from the run subsystem's current snapshots. */
	UFUNCTION(BlueprintCallable, Category = "Run|UI")
	void Configure(ERunActListMode InMode);

	UFUNCTION(BlueprintPure, Category = "Run|UI")
	ERunActListMode GetMode() const { return Mode; }

	/** Act restarted by entry Index; 0 for the Main menu entry; -1 for none. */
	UFUNCTION(BlueprintPure, Category = "Run|UI")
	int32 GetActOfEntry(int32 Index) const { return EntryActs.IsValidIndex(Index) ? EntryActs[Index] : -1; }

	/** RunLost: time over which the screen fades in over black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run|UI", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float LossFadeSeconds = 1.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnEntriesRebuilt() override;

private:
	UFUNCTION()
	void HandleChoice(UStdChoiceListWidget* Screen, int32 Index);

	ERunActListMode Mode = ERunActListMode::SelectAct;

	/** Act of each entry, same order (0 = Main menu). */
	TArray<int32> EntryActs;

	float FadeElapsed = 0.f;
};
