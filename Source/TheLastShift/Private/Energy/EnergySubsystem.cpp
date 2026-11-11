// SPDX-License-Identifier: MIT

#include "Energy/EnergySubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Scan/ScanDirectorComponent.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnergySubsystem)

DEFINE_LOG_CATEGORY(LogEnergy);

UEnergySubsystem* UEnergySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEnergySubsystem>() : nullptr;
}

void UEnergySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BeganPlayHandle = UScanDirectorComponent::OnDirectorBeganPlay.AddUObject(this, &UEnergySubsystem::HandleDirectorBeganPlay);
	EndingPlayHandle = UScanDirectorComponent::OnDirectorEndingPlay.AddUObject(this, &UEnergySubsystem::HandleDirectorEndingPlay);

	// A director that began play before this subsystem existed (not expected: game-instance subsystems start first).
	for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
	{
		if (It->HasBegunPlay() && IsOurWorld(*It))
		{
			Bind(*It);
			break;
		}
	}
}

void UEnergySubsystem::Deinitialize()
{
	Unbind();
	UScanDirectorComponent::OnDirectorBeganPlay.Remove(BeganPlayHandle);
	UScanDirectorComponent::OnDirectorEndingPlay.Remove(EndingPlayHandle);
	Super::Deinitialize();
}

bool UEnergySubsystem::IsOurWorld(const UScanDirectorComponent* Director) const
{
	const UWorld* World = Director ? Director->GetWorld() : nullptr;
	return World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance();
}

void UEnergySubsystem::HandleDirectorBeganPlay(UScanDirectorComponent* Director)
{
	if (!IsOurWorld(Director))
	{
		return;
	}
	if (const UScanDirectorComponent* Current = BoundDirector.Get(); Current && Current != Director)
	{
		UE_LOG(LogEnergy, Warning, TEXT("Energy: a second scan director (%s) began play; staying bound to %s."),
			*Director->GetPathName(), *Current->GetPathName());
		return;
	}
	Bind(Director);
}

void UEnergySubsystem::HandleDirectorEndingPlay(UScanDirectorComponent* Director)
{
	if (Director && Director == BoundDirector.Get())
	{
		Unbind();

		// The world is going away (map change): session energy belongs to it. Nothing is broadcast here, the listeners
		// of the old world are ending play too; the next director's bind broadcasts the state to the new world.
		if (SessionEnergy != 0)
		{
			UE_LOG(LogEnergy, Log, TEXT("Energy: session energy %d dropped with the world (banked %d kept)."), SessionEnergy, BankedEnergy);
			SessionEnergy = 0;
		}
	}
}

void UEnergySubsystem::Bind(UScanDirectorComponent* Director)
{
	if (!Director || Director == BoundDirector.Get())
	{
		return;
	}
	Unbind();
	BoundDirector = Director;
	Director->OnStopExited.AddUniqueDynamic(this, &UEnergySubsystem::HandleStopExited);
	UE_LOG(LogEnergy, Log, TEXT("Energy: bound to %s in world %s (banked %d, session %d)."),
		*Director->GetPathName(), *GetNameSafe(Director->GetWorld()), BankedEnergy, SessionEnergy);
	BroadcastChanged();
}

void UEnergySubsystem::Unbind()
{
	if (UScanDirectorComponent* Director = BoundDirector.Get())
	{
		Director->OnStopExited.RemoveDynamic(this, &UEnergySubsystem::HandleStopExited);
		UE_LOG(LogEnergy, Log, TEXT("Energy: unbound from %s."), *Director->GetPathName());
	}
	BoundDirector.Reset();
}

void UEnergySubsystem::HandleStopExited(EStopExitReason Reason, float StoppedSeconds)
{
	const int32 Session = SessionEnergy;
	SessionEnergy = 0;

	if (Reason == EStopExitReason::Resumed)
	{
		BankedEnergy += Session;
		UE_LOG(LogEnergy, Log, TEXT("Energy: Resumed after %.2f s, %d banked (banked %d)."), StoppedSeconds, Session, BankedEnergy);
		OnSessionBanked.Broadcast(Session);
	}
	else
	{
		UE_LOG(LogEnergy, Log, TEXT("Energy: %s after %.2f s, session energy %d lost (banked %d)."),
			LexToString(Reason), StoppedSeconds, Session, BankedEnergy);
		OnSessionLost.Broadcast(Session, Reason);
	}
	BroadcastChanged();
}

bool UEnergySubsystem::Spend(int32 Cost)
{
	if (!CanSpend(Cost))
	{
		UE_LOG(LogEnergy, Log, TEXT("Energy: cannot spend %d (banked %d)."), Cost, BankedEnergy);
		return false;
	}
	BankedEnergy -= Cost;
	UE_LOG(LogEnergy, Log, TEXT("Energy: spent %d (banked %d)."), Cost, BankedEnergy);
	BroadcastChanged();
	return true;
}

void UEnergySubsystem::AddSessionEnergy(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	SessionEnergy += Amount;
	UE_LOG(LogEnergy, Log, TEXT("Energy: +%d session (session %d, banked %d)."), Amount, SessionEnergy, BankedEnergy);
	BroadcastChanged();
}

void UEnergySubsystem::AddBankedEnergy(int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}
	BankedEnergy = FMath::Max(BankedEnergy + Amount, 0);
	UE_LOG(LogEnergy, Log, TEXT("Energy: %+d banked (banked %d)."), Amount, BankedEnergy);
	BroadcastChanged();
}

void UEnergySubsystem::RestoreBankedEnergy(int32 Banked)
{
	BankedEnergy = FMath::Max(Banked, 0);
	SessionEnergy = 0;
	UE_LOG(LogEnergy, Log, TEXT("Energy: restored, banked %d, session 0."), BankedEnergy);
	BroadcastChanged();
}

void UEnergySubsystem::BroadcastChanged()
{
	OnEnergyChanged.Broadcast(BankedEnergy, SessionEnergy);
}
