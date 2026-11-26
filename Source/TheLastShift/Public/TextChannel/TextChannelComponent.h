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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTextCellsUpdated, int32, ShownCells, int32, TranslatedCells);

/** Who speaks a message of the channel. */
UENUM(BlueprintType)
enum class ETextVoice : uint8
{
	/** The home AI: degraded by the battery, typed at the battery's speed, discarded at Silent. */
	Home,
	/** The twin AI: barcode characters typed first and translated a few characters behind; never degraded or discarded. */
	Twin,
	/**
	 * The engineer: rows of the message table whose trigger is the engineer's (EntitySeen), and the engineer's lines before
	 * a row (the engineer table, FEngineerLineRow). Never degraded or discarded at any battery level: its full text, typed
	 * at the full battery's speed.
	 */
	Engineer
};

/** A message starts to be typed from its first character: its first start (bRestart false) or a restart after a suspension. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTextTypingStarted, FName, RowName, ETextVoice, Voice, bool, bRestart);

/**
 * The suit's text channel: one queue for three voices. The home AI's messages are degraded by the battery, filled with
 * their numbers and typed out. The engineer's messages (rows of the message table whose trigger GetVoiceForTrigger gives
 * the engineer: EntitySeen) are filled with their numbers and typed out in full at every battery level, Silent included,
 * at the full battery's speed: the battery's degradation is the home AI's. The twin AI's messages (ETextVoice::Twin, rows of the twin table) are never degraded and
 * never discarded at Silent; they are typed at the fixed TwinCharsPerSecond of the project settings as barcode
 * characters, and the translation follows TranslationLagChars behind and catches up after the last barcode
 * (GetShownCells / GetTranslatedCells, OnCellsUpdated). Every other rule (Normal time only, suspensions, restart from
 * the first character, hold and gap) is the same for every voice.
 *
 * It owns neither the battery nor the time state. Each push carries a context (FTextChannelContext) with the numbers
 * the message shows; the battery fraction in it fixes the level and the typing speed when the message starts, and they
 * do not change while it is shown. At Silent the message is discarded. Whoever owns the component suspends it
 * (SetSuspended): while suspended nothing is typed or shown and the current message's timers stand still; when the
 * suspension ends the current message restarts from its first character. The queue is kept.
 *
 * The battery thresholds of the levels and the typing speeds are not the channel's: their one owner is
 * UTheLastShiftSettings::TextDegradation (Config/DefaultGame.ini), read when a message starts. A message is typed at the
 * settings' speed for its battery, stays fully typed for HoldSeconds + HoldSecondsPerChar per character, finishes, and
 * the next one starts GapSeconds later.
 *
 * Messages come from the table given with SetMessageTable (FAIMessageRow rows). Everything runs on unscaled real
 * time: the owner's tick gives real seconds, and AdvanceSeconds can be called directly (tests).
 *
 * The engineer answers before any row: the engineer table (SetEngineerTable, FEngineerLineRow rows) names, for a row of
 * the message table or a prologue line (a PushText with the line's row name), the engineer's lines said before it. When
 * such a row is pushed, its engineer lines are queued first, in ascending Order, then the row; the first time only that
 * the row is pushed on this channel (the channel lives with the pawn of one map: once in the act, or in the prologue
 * map). Nothing of it is saved. The lines are the engineer's voice, so at Silent the row after them is dropped and they
 * are still said.
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UTextChannelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTextChannelComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Timing ---

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

	/**
	 * Queues the row RowName, after the engineer's lines before it the first time it is pushed here. False (with a warning)
	 * when it does not exist or the queue is full.
	 */
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

	/** The twin AI's table (FTwinAIRow rows). Given by the owner; null = the twin AI says nothing. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void SetTwinTable(UDataTable* InTable);

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	UDataTable* GetTwinTable() const { return TwinTable; }

	/** Queues every row of the twin AI's Part, in ascending Order, as the twin voice. Returns how many were queued. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	int32 PushTwinPart(int32 Part);

	/**
	 * Queues a text that is no row of the message table (a prologue line, debug: tls.Text.Say), degraded automatically
	 * and with the context's tokens. Name is what GetCurrentRowName reports for it (None: nothing); a prologue line passes
	 * its row name, and the engineer's lines before that row come first, as for PushRow.
	 */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	bool PushText(const FString& Text, const FTextChannelContext& Context, FName Name = NAME_None);

	/** The engineer's lines (FEngineerLineRow rows). Given by the owner; null = the engineer says nothing before a row. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void SetEngineerTable(UDataTable* InTable);

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	UDataTable* GetEngineerTable() const { return EngineerTable; }

	/** True once the row Before has been pushed here with its engineer lines (whether or not it has any). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool HasSaidEngineerLinesBefore(FName Before) const { return EngineerSaidBefore.Contains(Before); }

	/** Forgets which rows' engineer lines were said, as at the start of a map (tests: several cases in one map). */
	UFUNCTION(BlueprintCallable, Category = "TextChannel")
	void ResetSaidEngineerLines() { EngineerSaidBefore.Reset(); }

	/** The engineer's lines before the row Before in Table, in ascending Order (ties by row name). */
	static TArray<FName> FindEngineerLines(const UDataTable* Table, FName Before);

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

	/**
	 * Everything pushed so far has been typed out: nothing is queued, and the current message, if any, shows its last
	 * character (it is held, or it has finished). A message that restarts after a suspension is being typed again.
	 */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsTypedOut() const { return Queue.Num() == 0 && (!bHasCurrent || Phase == EPhase::Holding); }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsSuspended() const { return bSuspended; }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsVisible() const { return bVisible; }

	UFUNCTION(BlueprintPure, Category = "TextChannel")
	int32 GetQueuedCount() const { return Queue.Num(); }

	/** A message named RowName waits in the queue (not the current one). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	bool IsQueued(FName RowName) const;

	/** Row of the current message (None for a pushed text or when there is none). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	FName GetCurrentRowName() const { return bHasCurrent ? Current.RowName : NAME_None; }

	/** Level of the current message (Silent when there is none). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	ETextDegradation GetCurrentLevel() const { return bHasCurrent ? CurrentLevel : ETextDegradation::Silent; }

	/** Voice of the current message (Home when there is none). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	ETextVoice GetCurrentVoice() const { return bHasCurrent ? Current.Voice : ETextVoice::Home; }

	/** Cells of the current message shown so far: its typed characters (as barcodes for the twin voice). 0 when hidden. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	int32 GetShownCells() const { return bVisible ? Typewriter.GetVisibleChars() : 0; }

	/** Of the shown cells, how many are translated: all of them for the home voice, the lagging count for the twin voice. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	int32 GetTranslatedCells() const;

	/** The full text of the current message, as it is being typed. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	FString GetCurrentText() const { return bHasCurrent ? Typewriter.GetText() : FString(); }

	/** Level of a battery fraction, with the thresholds of the project settings (UTheLastShiftSettings::GetTextLevel). */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	ETextDegradation GetLevelForBattery(double Fraction) const;

	/** Typing speed at a battery fraction, from the project settings (UTheLastShiftSettings::GetTextCharsPerSecond). */
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

	/** Who speaks a row of the message table with Trigger: the engineer for EntitySeen, the home AI for every other. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static ETextVoice GetVoiceForTrigger(ETextTrigger Trigger);

	// --- Events ---

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextMessageStarted OnMessageStarted;

	/**
	 * A message starts to be typed from its first character, for every voice: with OnMessageStarted, and again each time
	 * it restarts when the channel is no longer suspended (after a Stop, the pause menu or a travel screen).
	 */
	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextTypingStarted OnTypingStarted;

	/** The visible text changed (also to empty when a message restarts or the channel hides). */
	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextUpdated OnTextUpdated;

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextMessageFinished OnMessageFinished;

	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextVisibilityChanged OnVisibilityChanged;

	/** A twin message's shown or translated cells changed (also to 0 when it restarts or the channel hides). */
	UPROPERTY(BlueprintAssignable, Category = "TextChannel")
	FOnTextCellsUpdated OnCellsUpdated;

private:
	struct FQueued
	{
		FName RowName;
		/** Set for a pushed text (no row). */
		FString RawText;
		bool bRaw = false;
		ETextVoice Voice = ETextVoice::Home;
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
	/** The first time Before is pushed: queues its engineer lines in order. Returns how many were queued. */
	int32 PushEngineerLinesBefore(FName Before, const FTextChannelContext& Context);
	/** Starts queued messages until one is shown or the queue is empty. */
	void StartNext();
	void SetVisible(bool bInVisible);
	void BroadcastText();
	void FinishCurrent();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MessageTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> TwinTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> EngineerTable;

	/** Rows already pushed with their engineer lines on this channel (this map). */
	TSet<FName> EngineerSaidBefore;

	/** Twin voice: seconds typed since the message (re)started, and the lag fixed when it started. */
	double TwinElapsed = 0.0;
	int32 TwinLag = 0;
	int32 LastShownCells = -1;
	int32 LastTranslatedCells = -1;

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
