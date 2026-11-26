// SPDX-License-Identifier: MIT

#include "Run/HomeAIComponent.h"

#include "Area/AreaGenerator.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HomeAIComponent)

UHomeAIComponent::UHomeAIComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// The pause menu pauses the world: the channel must be seen as suspended meanwhile and released after.
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

UTextChannelComponent* UHomeAIComponent::GetChannel() const
{
	return Channel.Get();
}

int32 UHomeAIComponent::GetAct() const
{
	return URunSubsystem::GetActIndexOf(GetWorld());
}

void UHomeAIComponent::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	Channel = Owner ? Owner->FindComponentByClass<UTextChannelComponent>() : nullptr;
	UTextChannelComponent* Text = Channel.Get();
	if (!Text || !World || !World->IsGameWorld())
	{
		if (!Text)
		{
			UE_LOG(LogTextChannel, Warning, TEXT("%s: no text channel next to the home AI on %s; the AI says nothing."), *GetName(), *GetNameSafe(Owner));
		}
		return;
	}
	Text->SetMessageTable(UTheLastShiftSettings::Get()->LoadAIMessagesTable());
	Text->SetTwinTable(UTheLastShiftSettings::Get()->LoadTwinAITable());
	Text->SetEngineerTable(UTheLastShiftSettings::Get()->LoadEngineerLinesTable());

	if (UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>())
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &UHomeAIComponent::HandleTimeStateChanged);
	}
	if (UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this))
	{
		Home->OnHomeJump.AddUniqueDynamic(this, &UHomeAIComponent::HandleHomeJump);
		Home->OnRunLost.AddUniqueDynamic(this, &UHomeAIComponent::HandleRunLost);
	}
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnAreaEntered.AddUniqueDynamic(this, &UHomeAIComponent::HandleAreaEntered);
		Run->OnEntityFound.AddUniqueDynamic(this, &UHomeAIComponent::HandleEntityFound);
		Run->OnFirstContact.AddUniqueDynamic(this, &UHomeAIComponent::HandleFirstContact);
	}
	if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
	{
		Energy->OnSessionBanked.AddUniqueDynamic(this, &UHomeAIComponent::HandleSessionBanked);
	}
	TerminalHandle = APowerTerminal::OnAnyActivated.AddUObject(this, &UHomeAIComponent::HandleTerminalActivated);
	PrologueHandle = APrologueLineTrigger::OnAnyLine.AddUObject(this, &UHomeAIComponent::HandlePrologueLine);

	UpdateSuspension();
	const int32 PrologueStage = URunSubsystem::GetPrologueStageOf(World);
	UE_LOG(LogTextChannel, Log, TEXT("%s: home AI on %s, act %d, table %s, twin table %s, engineer lines %s%s."), *GetName(), *GetNameSafe(Owner), GetAct(),
		*GetNameSafe(Text->GetMessageTable()), *GetNameSafe(Text->GetTwinTable()), *GetNameSafe(Text->GetEngineerTable()),
		PrologueStage > 0 ? *FString::Printf(TEXT(", prologue map %d, prologue table %s"), PrologueStage, *GetNameSafe(UTheLastShiftSettings::Get()->LoadPrologueTable())) : TEXT(""));

	// Waking in the print room: the home map's first lines (said when the travel screen has gone, the channel's rule).
	// Brief 31A: not on the way home, where the player arrives in the hangar inside the ship.
	if (PrologueStage == 1 && !URunSubsystem::IsReturnStateOf(World))
	{
		SayPrologueGroup(PrologueGroups::Wake);
	}

	// An area that became ready before this pawn began play has already released the player.
	if (const AAreaGenerator* Generator = AAreaGenerator::Find(World); Generator && Generator->IsReady())
	{
		PushAreaStart(GetAct());
	}
}

void UHomeAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>())
		{
			TimeState->OnTimeStateChanged.RemoveDynamic(this, &UHomeAIComponent::HandleTimeStateChanged);
		}
	}
	if (UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this))
	{
		Home->OnHomeJump.RemoveDynamic(this, &UHomeAIComponent::HandleHomeJump);
		Home->OnRunLost.RemoveDynamic(this, &UHomeAIComponent::HandleRunLost);
	}
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnAreaEntered.RemoveDynamic(this, &UHomeAIComponent::HandleAreaEntered);
		Run->OnEntityFound.RemoveDynamic(this, &UHomeAIComponent::HandleEntityFound);
		Run->OnFirstContact.RemoveDynamic(this, &UHomeAIComponent::HandleFirstContact);
	}
	if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
	{
		Energy->OnSessionBanked.RemoveDynamic(this, &UHomeAIComponent::HandleSessionBanked);
	}
	APowerTerminal::OnAnyActivated.Remove(TerminalHandle);
	TerminalHandle.Reset();
	APrologueLineTrigger::OnAnyLine.Remove(PrologueHandle);
	PrologueHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHomeAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// The pause menu and the map transitions have no event the channel needs to react to within the frame.
	UpdateSuspension();
	PushEntityLines();
}

void UHomeAIComponent::UpdateSuspension()
{
	UTextChannelComponent* Text = Channel.Get();
	const UWorld* World = GetWorld();
	if (!Text || !World)
	{
		return;
	}
	const UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>();
	const UGameInstance* GameInstance = World->GetGameInstance();
	const UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this);

	const bool bStopped = TimeState && TimeState->IsStopped();
	// The pause menu: asked to the one place that knows the run is frozen.
	const bool bMenu = TimeState && TimeState->IsRunFrozen();
	const bool bCovered = Flow && Flow->IsTransitioning();
	const bool bLost = Home && Home->IsRunLost();
	Text->SetSuspended(bStopped || bMenu || bCovered || bLost);
}

FTextChannelContext UHomeAIComponent::MakeContext() const
{
	FTextChannelContext Context;
	if (const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this))
	{
		Context.HomeSecondsElapsed = Home->GetHomeSecondsElapsed();
		Context.BatteryFraction = Home->GetBatteryFraction();
	}
	if (const UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr)
	{
		Context.StoppedSeconds = TimeState->GetLastStopDuration();
	}
	return Context;
}

// --- Triggers ---

void UHomeAIComponent::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	// At once: a Stop hides the message in the same frame.
	UpdateSuspension();
}

void UHomeAIComponent::HandleHomeJump(const FHomeJump& Jump)
{
	UTextChannelComponent* Text = Channel.Get();
	if (!Text)
	{
		return;
	}
	FTextChannelContext Context = MakeContext();
	Context.StoppedSeconds = Jump.StoppedSeconds;
	Context.JumpSeconds = Jump.HomeSecondsAdded;
	Context.BatteryFraction = Jump.BatteryAfter;
	UE_LOG(LogTextChannel, Log, TEXT("%s: %s jump in act %d: still %.1f s, jump %.0f s, home %.0f s, battery %.4f."), *GetName(), LexToString(Jump.Cause),
		Jump.ActIndex, Context.StoppedSeconds, Context.JumpSeconds, Context.HomeSecondsElapsed, Context.BatteryFraction);
	UpdateSuspension();
	Text->PushTrigger(ETextTrigger::JumpReturn, Jump.ActIndex, Context);
}

void UHomeAIComponent::HandleRunLost()
{
	if (UTextChannelComponent* Text = Channel.Get())
	{
		Text->ClearAll();
	}
	UpdateSuspension();
}

void UHomeAIComponent::HandleAreaEntered(int32 ActIndex)
{
	if (ActIndex == GetAct())
	{
		PushAreaStart(ActIndex);
	}
}

void UHomeAIComponent::PushAreaStart(int32 Act)
{
	UTextChannelComponent* Text = Channel.Get();
	if (!Text || bAreaStartDone)
	{
		return;
	}
	bAreaStartDone = true;
	const int32 Queued = Text->PushSequence(ETextTrigger::AreaStart, Act, MakeContext());
	UE_LOG(LogTextChannel, Log, TEXT("%s: area start in act %d: %d messages queued."), *GetName(), Act, Queued);
}

void UHomeAIComponent::HandleSessionBanked(int32 BankedAmount)
{
	UTextChannelComponent* Text = Channel.Get();
	const UEnergySubsystem* Energy = UEnergySubsystem::Get(this);
	if (!Text || !Energy || bFirstEnergyDone || BankedAmount <= 0 || Energy->GetBankedEnergy() - BankedAmount > 0)
	{
		return;
	}
	bFirstEnergyDone = true;
	UpdateSuspension();
	const int32 Queued = Text->PushSequence(ETextTrigger::FirstEnergy, GetAct(), MakeContext());
	UE_LOG(LogTextChannel, Log, TEXT("%s: first energy banked in act %d (%d): %d messages queued."), *GetName(), GetAct(), BankedAmount, Queued);
}

void UHomeAIComponent::HandleEntityFound(EEntityFoundBy By)
{
	// Found inside the Stop's exit, before or after the time-state listeners that push the return message: wait a frame.
	bEntityFoundPending = true;
	bFoundBySighting = By == EEntityFoundBy::Sighting;
	UE_LOG(LogTextChannel, Log, TEXT("%s: the entity is found by %s in act %d: its rows follow on the next frame."), *GetName(), LexToString(By), GetAct());
}

void UHomeAIComponent::HandleFirstContact()
{
	bFirstContactPending = true;
	UE_LOG(LogTextChannel, Log, TEXT("%s: the first contact with the entity in act %d: its rows follow on the next frame."), *GetName(), GetAct());
}

void UHomeAIComponent::PushEntityLines()
{
	if (!bEntityFoundPending && !bFirstContactPending)
	{
		return;
	}
	const bool bFound = bEntityFoundPending;
	const bool bContact = bFirstContactPending;
	bEntityFoundPending = false;
	bFirstContactPending = false;
	UTextChannelComponent* Text = Channel.Get();
	const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(this);
	if (!Text || (Home && Home->IsRunLost()))
	{
		UE_LOG(LogTextChannel, Log, TEXT("%s: the entity: nothing said (%s)."), *GetName(), Text ? TEXT("the run is lost") : TEXT("no channel"));
		return;
	}
	const int32 Act = GetAct();
	const FTextChannelContext Context = MakeContext();
	const int32 Before = Text->GetQueuedCount() + (Text->IsBusy() ? 1 : 0);
	// The first contact: the home AI's lines about the entity, then the red marking if the contact found it. A clear
	// sighting (in a Stop without a contact): the engineer's rows, then the home AI switches the red marking on.
	const int32 Contact = bContact ? Text->PushSequence(ETextTrigger::EntityContact, Act, Context) : 0;
	const int32 Seen = bFound && !bContact && bFoundBySighting ? Text->PushSequence(ETextTrigger::EntitySeen, Act, Context) : 0;
	const int32 Found = bFound ? Text->PushSequence(ETextTrigger::EntityFound, Act, Context) : 0;
	UE_LOG(LogTextChannel, Log, TEXT("%s: the entity in act %d (%s%s): %d EntityContact, %d EntitySeen and %d EntityFound rows queued (%d messages before them)."),
		*GetName(), Act, bContact ? TEXT("first contact") : TEXT("no contact"), bFound ? (bFoundBySighting && !bContact ? TEXT(", found by a sighting") : TEXT(", found by the contact")) : TEXT(", found before"),
		Contact, Seen, Found, Before);
}

void UHomeAIComponent::HandleTerminalActivated(APowerTerminal* Terminal, int32 Cost)
{
	UTextChannelComponent* Text = Channel.Get();
	if (!Text || !Terminal || Terminal->GetWorld() != GetWorld())
	{
		return;
	}
	// A terminal that costs nothing (the prologue's free terminals) takes no payment: no spending comment.
	if (Cost <= 0 && !Terminal->IsA<ATwinAITerminal>())
	{
		UE_LOG(LogTextChannel, Log, TEXT("%s: %s used for free: no spending comment."), *GetName(), *Terminal->GetName());
		return;
	}
	// The comment of the payment's kind when the act has rows of it (each kind counts on its own), else Spend as before.
	const int32 Act = GetAct();
	const EPaymentKind Kind = Terminal->GetPaymentKind();
	const ETextTrigger KindTrigger = GetSpendTrigger(Kind);
	const bool bKindRows = KindTrigger != ETextTrigger::Spend && !UTextChannelComponent::FindRows(Text->GetMessageTable(), KindTrigger, Act).IsEmpty();
	const ETextTrigger Trigger = bKindRows ? KindTrigger : ETextTrigger::Spend;
	const bool bSaid = Text->PushTrigger(Trigger, Act, MakeContext());
	UE_LOG(LogTextChannel, Log, TEXT("%s: payment at %s (kind %s) in act %d: %s %s."), *GetName(), *Terminal->GetName(), LexToString(Kind), Act, LexToString(Trigger),
		bSaid ? TEXT("queued") : TEXT("has no row"));
	// The twin AI speaks after the home AI's comment on the same payment, in the same queue.
	if (const ATwinAITerminal* Twin = Cast<ATwinAITerminal>(Terminal))
	{
		const int32 Part = Twin->GetLastPlayedPart();
		const int32 Queued = Part > 0 ? Text->PushTwinPart(Part) : 0;
		UE_LOG(LogTextChannel, Log, TEXT("%s: twin AI part %d at %s: %d messages queued."), *GetName(), Part, *Terminal->GetName(), Queued);
	}
}

ETextTrigger UHomeAIComponent::GetSpendTrigger(EPaymentKind Kind)
{
	switch (Kind)
	{
	case EPaymentKind::Door: return ETextTrigger::SpendDoor;
	case EPaymentKind::Lights: return ETextTrigger::SpendLights;
	case EPaymentKind::Beacon: return ETextTrigger::SpendBeacon;
	case EPaymentKind::Log: return ETextTrigger::SpendLog;
	case EPaymentKind::Twin: return ETextTrigger::SpendTwin;
	case EPaymentKind::Other:
	default: return ETextTrigger::Spend;
	}
}

// --- The prologue's lines ---

TArray<FName> UHomeAIComponent::FindPrologueRows(const UDataTable* Table, FName Group)
{
	TArray<TPair<int32, FName>> Found;
	if (Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FPrologueRow::StaticStruct()))
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FPrologueRow* Row = reinterpret_cast<const FPrologueRow*>(Pair.Value);
			if (Row && Row->Group == Group)
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

int32 UHomeAIComponent::SayPrologueGroup(FName Group)
{
	UTextChannelComponent* Text = Channel.Get();
	if (!Text || Group.IsNone() || SaidPrologueGroups.Contains(Group))
	{
		return 0;
	}
	SaidPrologueGroups.Add(Group);
	// The prologue's groups from the prologue table, the way home's from the epilogue table (Brief 31A).
	const UDataTable* Table = UTheLastShiftSettings::Get()->LoadGroupTable(Group);
	const FTextChannelContext Context = MakeContext();
	int32 Queued = 0;
	for (const FName& RowName : FindPrologueRows(Table, Group))
	{
		const FPrologueRow* Row = Table->FindRow<FPrologueRow>(RowName, TEXT("Prologue"), false);
		if (Row && Text->PushText(Row->Text.ToString(), Context, RowName))
		{
			++Queued;
		}
	}
	UE_LOG(LogTextChannel, Log, TEXT("%s: prologue group %s: %d lines queued from %s."), *GetName(), *Group.ToString(), Queued, *GetNameSafe(Table));
	return Queued;
}

void UHomeAIComponent::HandlePrologueLine(UWorld* World, FName Group)
{
	if (World == GetWorld())
	{
		SayPrologueGroup(Group);
	}
}

// --- Travel label ---

namespace HomeAIImpl
{
	/**
	 * Row at the battery's level now (the thresholds of the project settings, the same with or without a pawn), tokens
	 * replaced, never typed out; OutReadingSeconds is the channel's hold time for that text. False when nothing is left.
	 */
	static bool ComposeTravelLine(const UObject* WorldContext, const FAIMessageRow& Row, FName RowName, const FString& What, FText& OutLabel, float& OutReadingSeconds)
	{
		// The hold rule is the channel's: the one on the player's pawn, or its defaults where there is none (the main menu).
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UTextChannelComponent* Rules = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
		if (!Rules)
		{
			Rules = GetDefault<UTextChannelComponent>();
		}

		FTextChannelContext Context;
		if (const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(WorldContext))
		{
			Context.HomeSecondsElapsed = Home->GetHomeSecondsElapsed();
			Context.BatteryFraction = Home->GetBatteryFraction();
		}
		const ETextDegradation Level = UTheLastShiftSettings::Get()->GetTextLevel(Context.BatteryFraction);
		const FString Text = UTextChannelComponent::ComposeMessage(Row, Level, Context);
		UE_LOG(LogTextChannel, Log, TEXT("Travel label for %s: '%s' at %s (battery %.3f): \"%s\"."), *What, *RowName.ToString(), LexToString(Level),
			Context.BatteryFraction, *Text);
		if (Text.IsEmpty())
		{
			return false;
		}
		OutLabel = FText::FromString(Text);
		OutReadingSeconds = Rules->GetHoldSecondsFor(Text.Len());
		return true;
	}
}

bool UHomeAIComponent::BuildTravelLabel(const UObject* WorldContext, int32 Act, FText& OutLabel, float& OutReadingSeconds)
{
	OutLabel = FText::GetEmpty();
	OutReadingSeconds = 0.f;
	const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
	const TArray<FName> Rows = UTextChannelComponent::FindRows(Table, ETextTrigger::AreaEntry, Act);
	const FAIMessageRow* Row = Rows.Num() > 0 ? Table->FindRow<FAIMessageRow>(Rows[0], TEXT("TravelLabel"), false) : nullptr;
	if (!Row)
	{
		return false;
	}
	return HomeAIImpl::ComposeTravelLine(WorldContext, *Row, Rows[0], FString::Printf(TEXT("act %d"), Act), OutLabel, OutReadingSeconds);
}

bool UHomeAIComponent::BuildPrologueTravelLabel(const UObject* WorldContext, FText& OutLabel, float& OutReadingSeconds)
{
	OutLabel = FText::GetEmpty();
	OutReadingSeconds = 0.f;
	const UDataTable* Table = UTheLastShiftSettings::Get()->LoadPrologueTable();
	const TArray<FName> Rows = FindPrologueRows(Table, PrologueGroups::Flight);
	const FPrologueRow* Row = Rows.Num() > 0 ? Table->FindRow<FPrologueRow>(Rows[0], TEXT("TravelLabel"), false) : nullptr;
	if (!Row)
	{
		UE_LOG(LogTextChannel, Log, TEXT("Travel label for the flight: no Flight row in %s."), *GetNameSafe(Table));
		return false;
	}
	// The line degrades like any home AI message: from its full text, with the channel's tokens.
	FAIMessageRow Message;
	Message.Text = Row->Text;
	return HomeAIImpl::ComposeTravelLine(WorldContext, Message, Rows[0], TEXT("the flight"), OutLabel, OutReadingSeconds);
}

bool UHomeAIComponent::BuildReturnTravelLabel(const UObject* WorldContext, FText& OutLabel, float& OutReadingSeconds)
{
	OutLabel = FText::GetEmpty();
	OutReadingSeconds = 0.f;
	const UDataTable* Table = UTheLastShiftSettings::Get()->LoadEpilogueTable();
	const TArray<FName> Rows = FindPrologueRows(Table, EpilogueGroups::Return);
	const FPrologueRow* Row = Rows.Num() > 0 ? Table->FindRow<FPrologueRow>(Rows[0], TEXT("TravelLabel"), false) : nullptr;
	if (!Row)
	{
		UE_LOG(LogTextChannel, Log, TEXT("Travel label for the flight back: no Return row in %s."), *GetNameSafe(Table));
		return false;
	}
	// Degraded like any home AI message: from its full text, with the channel's tokens.
	FAIMessageRow Message;
	Message.Text = Row->Text;
	return HomeAIImpl::ComposeTravelLine(WorldContext, Message, Rows[0], TEXT("the flight back"), OutLabel, OutReadingSeconds);
}
