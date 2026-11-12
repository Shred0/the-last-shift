// SPDX-License-Identifier: MIT

#include "Run/HomeAIComponent.h"

#include "Area/AreaGenerator.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/NarrativeRows.h"
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
	}
	if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
	{
		Energy->OnSessionBanked.AddUniqueDynamic(this, &UHomeAIComponent::HandleSessionBanked);
	}
	TerminalHandle = APowerTerminal::OnAnyActivated.AddUObject(this, &UHomeAIComponent::HandleTerminalActivated);

	UpdateSuspension();
	UE_LOG(LogTextChannel, Log, TEXT("%s: home AI on %s, act %d, table %s."), *GetName(), *GetNameSafe(Owner), GetAct(), *GetNameSafe(Text->GetMessageTable()));

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
	}
	if (UEnergySubsystem* Energy = UEnergySubsystem::Get(this))
	{
		Energy->OnSessionBanked.RemoveDynamic(this, &UHomeAIComponent::HandleSessionBanked);
	}
	APowerTerminal::OnAnyActivated.Remove(TerminalHandle);
	TerminalHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHomeAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// The pause menu and the map transitions have no event the channel needs to react to within the frame.
	UpdateSuspension();
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

void UHomeAIComponent::HandleTerminalActivated(APowerTerminal* Terminal, int32 Cost)
{
	UTextChannelComponent* Text = Channel.Get();
	if (!Text || !Terminal || Terminal->GetWorld() != GetWorld())
	{
		return;
	}
	Text->PushTrigger(ETextTrigger::Spend, GetAct(), MakeContext());
}

// --- Travel label ---

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

	// The thresholds and hold rule have one owner: the channel on the player's pawn, or its defaults where there is none (the main menu).
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
	const ETextDegradation Level = Rules->GetLevelForBattery(Context.BatteryFraction);
	const FString Text = UTextChannelComponent::ComposeMessage(*Row, Level, Context);
	UE_LOG(LogTextChannel, Log, TEXT("Travel label for act %d: '%s' at %s (battery %.3f): \"%s\"."), Act, *Rows[0].ToString(), LexToString(Level),
		Context.BatteryFraction, *Text);
	if (Text.IsEmpty())
	{
		return false;
	}
	OutLabel = FText::FromString(Text);
	OutReadingSeconds = Rules->GetHoldSecondsFor(Text.Len());
	return true;
}
