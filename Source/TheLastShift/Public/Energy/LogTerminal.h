// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PowerTerminal.h"
#include "LogTerminal.generated.h"

class ULogDisplayWidget;
class UStaticMesh;
class UWidgetComponent;

/**
 * A terminal of the twin structure that holds the other humanity's logs. Paid once (Cost, Normal time, like any
 * terminal), it stays unlocked for the rest of the act: a time jump does not lock it. The payment deals it a group of
 * entries: the entries of DT_Logs, newest first, in equal consecutive groups to the log terminals of the map, in the
 * order the player pays them (the first terminal paid gets the newest group; TwinWriting::DealLogGroup), and stores the
 * whole group in the suit's archive at once.
 *
 * Unlocked, it shows its entries one at a time, newest first, on its surface (Screen) or in a suit panel on screen
 * (UTheLastShiftSettings::LogDisplay): the whole entry as barcodes, the translation typed over it at LogCharsPerSecond
 * of run time. Interact shows the next entry (after the last, the first again), for free. Hidden while Stopped; the
 * typing stands still behind the pause menu and while hidden, and the terminal keeps the entry it is on. Logs never go
 * through the text channel and are never shortened.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API ALogTerminal : public APowerTerminal
{
	GENERATED_BODY()

public:
	ALogTerminal();

	/** Locked: pays like any terminal. Unlocked: in Normal time and in range, shows the next entry, for free. */
	virtual bool TryActivate(APawn* InstigatorPawn) override;

	virtual bool AcceptsPayment() const override { return !bUnlocked; }
	virtual bool OffersPayment() const override { return !bUnlocked; }
	virtual bool OffersRead() const override { return bUnlocked && Entries.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	bool IsUnlocked() const { return bUnlocked; }

	/** Orders of the entries dealt to this terminal, newest first (empty while locked). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	const TArray<int32>& GetEntries() const { return Entries; }

	/** Which group of the map's deal this terminal got (0 = the newest; -1 while locked). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	int32 GetGroupIndex() const { return GroupIndex; }

	/** Index in GetEntries() of the entry shown now. */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	int32 GetCurrentIndex() const { return CurrentIndex; }

	/** Order of the entry shown now (0 when none). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	int32 GetCurrentOrder() const { return Entries.IsValidIndex(CurrentIndex) ? Entries[CurrentIndex] : 0; }

	/** Characters of the entry shown now. */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	int32 GetCurrentLength() const { return CurrentText.Len(); }

	/** Cells of the entry shown now that are translated (the rest are barcodes). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	int32 GetTranslatedCells() const;

	/** The entry is on its display now (the surface, or the panel while the player is in range). */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	bool IsEntryShown() const { return bShown; }

	/** Moves to the next entry (after the last, the first) and starts its translation again. */
	UFUNCTION(BlueprintCallable, Category = "Terminal|Logs")
	void ShowNextEntry();

	/** Back to locked, with no group and nothing shown, as at BeginPlay (used by the tests to deal the groups again). */
	void Relock();

	/** The widget on the terminal's surface. */
	ULogDisplayWidget* GetSurfaceWidget() const;

	/** The suit panel (null until it was first needed). */
	ULogDisplayWidget* GetPanelWidget() const { return Panel; }

	/** Size of the surface's widget, in widget pixels; the world size is this times the Screen's scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal|Logs", meta = (ClampMin = "100.0", ClampMax = "4000.0"))
	FVector2D SurfaceDrawSize = FVector2D(1000.0, 720.0);

	/**
	 * The body: the log console (SM_LogConsole, Assets zone), its back at the actor's origin, standing against a wall and
	 * facing +X, the surface in its frame's opening. Without it the body is the Brief 8 slab (1 x 1.8 m), also with its
	 * back at the origin.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Terminal|Logs")
	TObjectPtr<UStaticMesh> BodyMesh;

	/**
	 * The space the body takes in front of its wall, from the actor's origin: depth along +X, width, height. The area
	 * planner keeps it free; the body must fit in it (BeginPlay warns otherwise).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Terminal|Logs", meta = (ClampMin = "10.0", ClampMax = "300.0", Units = "cm"))
	FVector Footprint = FVector(50.0, 110.0, 190.0);

	/** Centre of the surface, in world space. */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs")
	FVector GetScreenCentre() const;

	/** Distance of the surface in front of the actor's origin (the wall), along +X. */
	UFUNCTION(BlueprintPure, Category = "Terminal|Logs", meta = (Units = "cm"))
	float GetScreenOffset() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandlePaid(APawn* InstigatorPawn) override;

	/** The terminal's surface: a world-space widget on its front face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal|Logs")
	TObjectPtr<UWidgetComponent> Screen;

private:
	/** Puts the body (BodyMesh, or the slab without it) and the surface in place, the back at the actor's origin. */
	void ApplyBodyLayout();
	void StartEntry();
	void UpdateDisplays(bool bShowSurface, bool bShowPanel);
	ULogDisplayWidget* EnsurePanel();

	UPROPERTY(Transient)
	TObjectPtr<ULogDisplayWidget> Panel;

	bool bUnlocked = false;
	bool bShown = false;
	int32 GroupIndex = -1;
	TArray<int32> Entries;
	int32 CurrentIndex = 0;
	FString CurrentText;
	/** Run seconds the current entry has been on display since it (re)started. */
	double TypedSeconds = 0.0;
	int32 LastPushedTranslated = -1;
	FLinearColor LastPushedColor = FLinearColor(-1.f, -1.f, -1.f, -1.f);
};
