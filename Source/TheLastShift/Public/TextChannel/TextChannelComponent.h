// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Narrative/NarrativeRows.h"
#include "TextChannel/TextDegradation.h"
#include "TextChannel/TextTokens.h"
#include "TextChannel/Typewriter.h"
#include "TextChannelComponent.generated.h"

class UDataTable;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogTextChannel, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTextMessageStarted, FName, RowName, ETextDegradation, Level);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextUpdated, const FString&, VisibleText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextMessageFinished, FName, RowName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextVisibilityChanged, bool, bVisible);

/**
 * The home AI's text channel: a queue of messages, each degraded by the battery, filled with its numbers and typed out.
 *
 * It owns neither the battery nor the time state. Each push carries a context (FTextChannelContext) with the numbers
 * the message shows; the battery fraction in it fixes the level and the typing speed when the message starts, and they
 * do not change while it is shown. At Silent the message is discarded. Whoever owns the component suspends it
 * (SetSuspended): while suspended nothing is typed or shown and the current message's timers stand still; when the
 * suspension ends the current message restarts from its first character. The queue is kept.
 *
 * A message is typed at a speed between CharsPerSecondEmpty and CharsPerSecondFull (by the battery), stays fully typed
 * for HoldSeconds + HoldSecondsPerChar per character, finishes, and the next one starts GapSeconds later.
 *
 * Messages come from the table given with SetMessageTable (FAIMessageRow rows). Everything runs on unscaled real
 * time: the owner's tick gives real seconds, and AdvanceSeconds can be called directly (tests).
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UTextChannelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTextChannelComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Levels ---

	/** Below this battery fraction a message loses its articles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Levels", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoArticlesBelow = 0.70f;

	/** Below this battery fraction a message is lowercase and abbreviated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Levels", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AbbreviatedBelow = 0.40f;

	/** Below this battery fraction only fragments of a message are left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Levels", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScatteredBelow = 0.10f;

	/** Below this battery fraction a message is discarded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Levels", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SilentBelow = 0.03f;

	// --- Timing ---

	/** Typing speed at a full battery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Timing", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float CharsPerSecondFull = 40.f;

	/** Typing speed at an empty battery; in between it follows the battery fraction linearly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Timing", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float CharsPerSecondEmpty = 8.f;

	/** A fully typed message stays this long, plus HoldSecondsPerChar per character, before it finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Timing", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	float HoldSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Timing", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float HoldSecondsPerChar = 0.04f;

	/** Between the end of one message and the start of the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Timing", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float GapSeconds = 0.8f;

	/** Messages waiting to start, at most; a push beyond it is refused with a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|Queue", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxQueued = 8;

	// --- Actions ---

	/** The table the rows come from (FAIMessageRow). Given by the owner; null = no messages. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void SetMessageTable(UDataTable* InTable);

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	UDataTable* GetMessageTable() const { return MessageTable; }

	/** Queues the row RowName. False (with a warning) when it does not exist or the queue is full. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	bool PushRow(FName RowName, const FTextChannelContext& Context);

	/**
	 * Queues one row of Trigger and Act: the first call the lowest Order, each call the next, and the last again once
	 * they run out. No row: no message (JumpReturn and Spend write a warning).
	 */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	bool PushTrigger(ETextTrigger Trigger, int32 Act, const FTextChannelContext& Context);

	/** Queues every row of Trigger and Act, in ascending Order. Returns how many were queued. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	int32 PushSequence(ETextTrigger Trigger, int32 Act, const FTextChannelContext& Context);

	/** Queues a text that is no row (debug: tls.Text.Say), degraded automatically. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	bool PushText(const FString& Text, const FTextChannelContext& Context);

	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void SetSuspended(bool bInSuspended);

	/** Drops the current message and the queue, and hides the channel. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void ClearAll();

	/** Advances the channel by DeltaSeconds of real time (the tick calls it). */
	void AdvanceSeconds(float DeltaSeconds);

	// --- Queries ---

	/** The part of the current message typed so far; empty when nothing is shown. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	FString GetVisibleText() const { return bVisible ? Typewriter.GetVisible() : FString(); }

	/** A message is shown, waits to restart, or is queued. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsBusy() const { return bHasCurrent || Queue.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsSuspended() const { return bSuspended; }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsVisible() const { return bVisible; }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	int32 GetQueuedCount() const { return Queue.Num(); }

	/** Row of the current message (None for a pushed text or when there is none). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	FName GetCurrentRowName() const { return bHasCurrent ? Current.RowName : NAME_None; }

	/** Level of the current message (Silent when there is none). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	ETextDegradation GetCurrentLevel() const { return bHasCurrent ? CurrentLevel : ETextDegradation::Silent; }

	/** The full text of the current message, as it is being typed. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	FString GetCurrentText() const { return bHasCurrent ? Typewriter.GetText() : FString(); }

	/** Level of a battery fraction with this channel's thresholds. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	ETextDegradation GetLevelForBattery(double Fraction) const;

	/** Typing speed at a battery fraction. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	float GetCharsPerSecond(double Fraction) const;

	/** How long a fully typed text of CharCount characters stays: HoldSeconds + CharCount x HoldSecondsPerChar. */
	UFUNCTION(BlueprintPure, Category = "TextChannel", meta = (Units = "s"))
	float GetHoldSecondsFor(int32 CharCount) const;

	/** The text of Row at Level, with the context's numbers: the row's variant for that level if set, else the automatic one. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static FString ComposeMessage(const FAIMessageRow& Row, ETextDegradation Level, const FTextChannelContext& Context);

	/** Rows of Trigger and Act in Table, in ascending Order (ties by row name). */
	static TArray<FName> FindRows(const UDataTable* Table, ETextTrigger Trigger, int32 Act);

	// --- Events ---

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextMessageStarted OnMessageStarted;

	/** The visible text changed (also to empty when a message restarts or the channel hides). */
	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextUpdated OnTextUpdated;

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextMessageFinished OnMessageFinished;

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextVisibilityChanged OnVisibilityChanged;

private:
	struct FQueued
	{
		FName RowName;
		/** Set for a pushed text (no row). */
		FString RawText;
		bool bRaw = false;
		FTextChannelContext Context;
	};

	enum class EPhase : uint8
	{
		Idle,
		Typing,
		Holding,
		Gap
	};

	bool Enqueue(FQueued&& Item, const TCHAR* What);
	/** Starts queued messages until one is shown or the queue is empty. */
	void StartNext();
	void SetVisible(bool bInVisible);
	void BroadcastText();
	void FinishCurrent();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MessageTable;

	TArray<FQueued> Queue;
	FQueued Current;
	bool bHasCurrent = false;
	ETextDegradation CurrentLevel = ETextDegradation::Full;
	FTypewriter Typewriter;
	EPhase Phase = EPhase::Idle;
	/** Seconds left in Holding or Gap. */
	float PhaseLeft = 0.f;
	bool bSuspended = false;
	bool bVisible = false;
	FString LastBroadcastText;

	/** Next row index per trigger and act (PushTrigger). */
	TMap<uint32, int32> NextIndex;

	double LastRealTime = -1.0;
};
