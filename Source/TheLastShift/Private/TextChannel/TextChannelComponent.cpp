// SPDX-License-Identifier: MIT

#include "TextChannel/TextChannelComponent.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextChannelComponent)

DEFINE_LOG_CATEGORY(LogTextChannel);

namespace TextChannelImpl
{
	/** A hitch (a map load, a breakpoint) never skips a whole message. */
	constexpr float MaxStep = 0.25f;

	static uint32 Key(ETextTrigger Trigger, int32 Act)
	{
		return (static_cast<uint32>(Trigger) << 16) | static_cast<uint32>(Act & 0xFFFF);
	}
}

UTextChannelComponent::UTextChannelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Keeps the real-time clock current while the menu pauses the world (the owner suspends the channel meanwhile).
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UTextChannelComponent::BeginPlay()
{
	Super::BeginPlay();
	LastRealTime = -1.0;
}

void UTextChannelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	const float Step = LastRealTime >= 0.0 ? static_cast<float>(Now - LastRealTime) : 0.f;
	LastRealTime = Now;
	AdvanceSeconds(FMath::Clamp(Step, 0.f, TextChannelImpl::MaxStep));
}

void UTextChannelComponent::SetMessageTable(UDataTable* InTable)
{
	if (InTable && (!InTable->GetRowStruct() || !InTable->GetRowStruct()->IsChildOf(FAIMessageRow::StaticStruct())))
	{
		UE_LOG(LogTextChannel, Warning, TEXT("%s: table %s does not hold FAIMessageRow rows; ignored."), *GetNameSafe(GetOwner()), *GetNameSafe(InTable));
		InTable = nullptr;
	}
	MessageTable = InTable;
	NextIndex.Reset();
}

// --- Levels and text ---

ETextDegradation UTextChannelComponent::GetLevelForBattery(double Fraction) const
{
	return UTextDegradationLibrary::LevelFromBattery(Fraction, FMath::Clamp(NoArticlesBelow, 0.f, 1.f), FMath::Clamp(AbbreviatedBelow, 0.f, 1.f),
		FMath::Clamp(ScatteredBelow, 0.f, 1.f), FMath::Clamp(SilentBelow, 0.f, 1.f));
}

float UTextChannelComponent::GetCharsPerSecond(double Fraction) const
{
	const float Empty = FMath::Clamp(CharsPerSecondEmpty, 1.f, 500.f);
	const float Full = FMath::Clamp(CharsPerSecondFull, 1.f, 500.f);
	return FMath::Lerp(Empty, Full, static_cast<float>(FMath::Clamp(Fraction, 0.0, 1.0)));
}

float UTextChannelComponent::GetHoldSecondsFor(int32 CharCount) const
{
	return FMath::Clamp(HoldSeconds, 0.f, 30.f) + FMath::Max(CharCount, 0) * FMath::Clamp(HoldSecondsPerChar, 0.f, 1.f);
}

FString UTextChannelComponent::ComposeMessage(const FAIMessageRow& Row, ETextDegradation Level, const FTextChannelContext& Context)
{
	if (Level == ETextDegradation::Silent)
	{
		return FString();
	}
	const FText* Variant = nullptr;
	switch (Level)
	{
	case ETextDegradation::NoArticles: Variant = &Row.TextNoArticles; break;
	case ETextDegradation::Abbreviated: Variant = &Row.TextAbbreviated; break;
	case ETextDegradation::Scattered: Variant = &Row.TextScattered; break;
	default: break;
	}
	const FString Base = Variant && !Variant->IsEmpty() ? Variant->ToString() : UTextDegradationLibrary::Degrade(Row.Text.ToString(), Level);
	return UTextTokensLibrary::ReplaceTokens(Base, Context, Level);
}

TArray<FName> UTextChannelComponent::FindRows(const UDataTable* Table, ETextTrigger Trigger, int32 Act)
{
	TArray<TPair<int32, FName>> Found;
	if (Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FAIMessageRow::StaticStruct()))
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FAIMessageRow* Row = reinterpret_cast<const FAIMessageRow*>(Pair.Value);
			if (Row && Row->Trigger == Trigger && Row->Act == Act)
			{
				Found.Emplace(Row->Order, Pair.Key);
			}
		}
	}
	Found.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B)
	{
		return A.Key != B.Key ? A.Key < B.Key : A.Value.LexicalLess(B.Value);
	});
	TArray<FName> Names;
	for (const TPair<int32, FName>& Pair : Found)
	{
		Names.Add(Pair.Value);
	}
	return Names;
}

// --- Pushes ---

bool UTextChannelComponent::Enqueue(FQueued&& Item, const TCHAR* What)
{
	const int32 Max = FMath::Clamp(MaxQueued, 1, 64);
	if (Queue.Num() >= Max)
	{
		UE_LOG(LogTextChannel, Warning, TEXT("%s: %s refused, the queue is full (%d waiting)."), *GetNameSafe(GetOwner()), What, Queue.Num());
		return false;
	}
	Queue.Add(MoveTemp(Item));
	if (Phase == EPhase::Idle && !bSuspended)
	{
		StartNext();
	}
	return true;
}

bool UTextChannelComponent::PushRow(FName RowName, const FTextChannelContext& Context)
{
	if (!MessageTable || !MessageTable->GetRowMap().Contains(RowName))
	{
		UE_LOG(LogTextChannel, Warning, TEXT("%s: no row '%s' in %s."), *GetNameSafe(GetOwner()), *RowName.ToString(), *GetNameSafe(MessageTable));
		return false;
	}
	FQueued Item;
	Item.RowName = RowName;
	Item.Context = Context;
	return Enqueue(MoveTemp(Item), *FString::Printf(TEXT("row '%s'"), *RowName.ToString()));
}

bool UTextChannelComponent::PushTrigger(ETextTrigger Trigger, int32 Act, const FTextChannelContext& Context)
{
	const TArray<FName> Rows = FindRows(MessageTable, Trigger, Act);
	if (Rows.IsEmpty())
	{
		// Most acts have no AreaStart, FirstEnergy or AreaEntry row: silence is normal there.
		if (Trigger == ETextTrigger::JumpReturn || Trigger == ETextTrigger::Spend)
		{
			UE_LOG(LogTextChannel, Warning, TEXT("%s: no %s row for act %d in %s."), *GetNameSafe(GetOwner()), LexToString(Trigger), Act, *GetNameSafe(MessageTable));
		}
		return false;
	}
	const uint32 Key = TextChannelImpl::Key(Trigger, Act);
	const int32 Index = FMath::Clamp(NextIndex.FindRef(Key), 0, Rows.Num() - 1);
	if (!PushRow(Rows[Index], Context))
	{
		return false;
	}
	NextIndex.Add(Key, FMath::Min(Index + 1, Rows.Num() - 1));
	return true;
}

int32 UTextChannelComponent::PushSequence(ETextTrigger Trigger, int32 Act, const FTextChannelContext& Context)
{
	int32 Queued = 0;
	for (const FName& RowName : FindRows(MessageTable, Trigger, Act))
	{
		Queued += PushRow(RowName, Context) ? 1 : 0;
	}
	return Queued;
}

bool UTextChannelComponent::PushText(const FString& Text, const FTextChannelContext& Context)
{
	FQueued Item;
	Item.RawText = Text;
	Item.bRaw = true;
	Item.Context = Context;
	return Enqueue(MoveTemp(Item), TEXT("a text"));
}

// --- Running ---

void UTextChannelComponent::StartNext()
{
	while (Queue.Num() > 0)
	{
		FQueued Item = Queue[0];
		Queue.RemoveAt(0);

		const ETextDegradation Level = GetLevelForBattery(Item.Context.BatteryFraction);
		const FString What = Item.bRaw ? FString(TEXT("text")) : FString::Printf(TEXT("'%s'"), *Item.RowName.ToString());
		if (Level == ETextDegradation::Silent)
		{
			UE_LOG(LogTextChannel, Log, TEXT("%s: %s discarded: battery %.3f is Silent."), *GetNameSafe(GetOwner()), *What, Item.Context.BatteryFraction);
			continue;
		}
		FString Text;
		if (Item.bRaw)
		{
			FAIMessageRow Row;
			Row.Text = FText::FromString(Item.RawText);
			Text = ComposeMessage(Row, Level, Item.Context);
		}
		else if (const FAIMessageRow* Row = MessageTable ? MessageTable->FindRow<FAIMessageRow>(Item.RowName, TEXT("TextChannel"), false) : nullptr)
		{
			Text = ComposeMessage(*Row, Level, Item.Context);
		}
		if (Text.IsEmpty())
		{
			UE_LOG(LogTextChannel, Log, TEXT("%s: %s discarded: nothing left at %s."), *GetNameSafe(GetOwner()), *What, LexToString(Level));
			continue;
		}

		Current = MoveTemp(Item);
		bHasCurrent = true;
		CurrentLevel = Level;
		Typewriter.Start(Text, GetCharsPerSecond(Current.Context.BatteryFraction));
		Phase = EPhase::Typing;
		PhaseLeft = 0.f;
		UE_LOG(LogTextChannel, Log, TEXT("%s: message %s started at %s (battery %.3f, %.1f chars/s): \"%s\""), *GetNameSafe(GetOwner()), *What,
			LexToString(Level), Current.Context.BatteryFraction, Typewriter.GetCharsPerSecond(), *Text);
		OnMessageStarted.Broadcast(Current.RowName, Level);
		SetVisible(true);
		BroadcastText();
		return;
	}
	Phase = EPhase::Idle;
}

void UTextChannelComponent::FinishCurrent()
{
	const FName RowName = Current.RowName;
	bHasCurrent = false;
	Current = FQueued();
	Typewriter.Reset();
	Phase = EPhase::Gap;
	PhaseLeft = FMath::Clamp(GapSeconds, 0.f, 10.f);
	SetVisible(false);
	BroadcastText();
	UE_LOG(LogTextChannel, Log, TEXT("%s: message '%s' finished (%d queued)."), *GetNameSafe(GetOwner()), *RowName.ToString(), Queue.Num());
	OnMessageFinished.Broadcast(RowName);
}

void UTextChannelComponent::AdvanceSeconds(float DeltaSeconds)
{
	if (bSuspended)
	{
		return;
	}
	const float Delta = FMath::Max(DeltaSeconds, 0.f);
	switch (Phase)
	{
	case EPhase::Idle:
		StartNext();
		break;
	case EPhase::Gap:
		PhaseLeft -= Delta;
		if (PhaseLeft <= 0.f)
		{
			Phase = EPhase::Idle;
			StartNext();
		}
		break;
	case EPhase::Typing:
		Typewriter.Advance(Delta);
		BroadcastText();
		if (Typewriter.IsComplete())
		{
			Phase = EPhase::Holding;
			PhaseLeft = GetHoldSecondsFor(Typewriter.GetText().Len());
		}
		break;
	case EPhase::Holding:
		PhaseLeft -= Delta;
		if (PhaseLeft <= 0.f)
		{
			FinishCurrent();
		}
		break;
	}
}

void UTextChannelComponent::SetSuspended(bool bInSuspended)
{
	if (bSuspended == bInSuspended)
	{
		return;
	}
	bSuspended = bInSuspended;
	if (bSuspended)
	{
		SetVisible(false);
		BroadcastText();
		UE_LOG(LogTextChannel, Log, TEXT("%s: suspended%s."), *GetNameSafe(GetOwner()), bHasCurrent ? TEXT(" during a message") : TEXT(""));
		return;
	}
	UE_LOG(LogTextChannel, Log, TEXT("%s: resumed%s."), *GetNameSafe(GetOwner()), bHasCurrent ? TEXT(": the message restarts") : TEXT(""));
	if (bHasCurrent)
	{
		// The message starts over from its first character, at the level and speed it started with.
		Typewriter.Restart();
		Phase = EPhase::Typing;
		PhaseLeft = 0.f;
		SetVisible(true);
		BroadcastText();
	}
	else if (Phase == EPhase::Idle)
	{
		StartNext();
	}
}

void UTextChannelComponent::ClearAll()
{
	const int32 Dropped = Queue.Num() + (bHasCurrent ? 1 : 0);
	Queue.Reset();
	bHasCurrent = false;
	Current = FQueued();
	Typewriter.Reset();
	Phase = EPhase::Idle;
	PhaseLeft = 0.f;
	SetVisible(false);
	BroadcastText();
	UE_LOG(LogTextChannel, Log, TEXT("%s: cleared (%d messages dropped)."), *GetNameSafe(GetOwner()), Dropped);
}

void UTextChannelComponent::SetVisible(bool bInVisible)
{
	if (bVisible == bInVisible)
	{
		return;
	}
	bVisible = bInVisible;
	OnVisibilityChanged.Broadcast(bVisible);
}

void UTextChannelComponent::BroadcastText()
{
	const FString Now = GetVisibleText();
	if (Now != LastBroadcastText)
	{
		LastBroadcastText = Now;
		OnTextUpdated.Broadcast(Now);
	}
}
