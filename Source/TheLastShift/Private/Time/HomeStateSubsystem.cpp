// SPDX-License-Identifier: MIT

#include "Time/HomeStateSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HomeStateSubsystem)

DEFINE_LOG_CATEGORY(LogHome);

UHomeStateSubsystem* UHomeStateSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UHomeStateSubsystem>() : nullptr;
}

void UHomeStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BeganPlayHandle = UScanDirectorComponent::OnDirectorBeganPlay.AddUObject(this, &UHomeStateSubsystem::HandleDirectorBeganPlay);
	EndingPlayHandle = UScanDirectorComponent::OnDirectorEndingPlay.AddUObject(this, &UHomeStateSubsystem::HandleDirectorEndingPlay);

	for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
	{
		if (It->HasBegunPlay() && IsOurWorld(*It))
		{
			Bind(*It);
			break;
		}
	}
}

void UHomeStateSubsystem::Deinitialize()
{
	Unbind();
	UScanDirectorComponent::OnDirectorBeganPlay.Remove(BeganPlayHandle);
	UScanDirectorComponent::OnDirectorEndingPlay.Remove(EndingPlayHandle);
	Super::Deinitialize();
}

bool UHomeStateSubsystem::IsOurWorld(const UScanDirectorComponent* Director) const
{
	const UWorld* World = Director ? Director->GetWorld() : nullptr;
	return World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance();
}

void UHomeStateSubsystem::HandleDirectorBeganPlay(UScanDirectorComponent* Director)
{
	if (!IsOurWorld(Director))
	{
		return;
	}
	if (const UScanDirectorComponent* Current = BoundDirector.Get(); Current && Current != Director)
	{
		UE_LOG(LogHome, Warning, TEXT("Home: a second scan director (%s) began play; staying bound to %s."), *Director->GetPathName(), *Current->GetPathName());
		return;
	}
	Bind(Director);
}

void UHomeStateSubsystem::HandleDirectorEndingPlay(UScanDirectorComponent* Director)
{
	if (Director && Director == BoundDirector.Get())
	{
		Unbind();
	}
}

void UHomeStateSubsystem::Bind(UScanDirectorComponent* Director)
{
	if (!Director || Director == BoundDirector.Get())
	{
		return;
	}
	Unbind();
	BoundDirector = Director;
	Director->OnStopExited.AddUniqueDynamic(this, &UHomeStateSubsystem::HandleStopExited);
	UE_LOG(LogHome, Log, TEXT("Home: bound to %s in world %s (battery %.4f, home %.0f s%s)."), *Director->GetPathName(),
		*GetNameSafe(Director->GetWorld()), BatteryFraction, HomeSecondsElapsed, bRunLost ? TEXT(", run lost") : TEXT(""));
}

void UHomeStateSubsystem::Unbind()
{
	if (UScanDirectorComponent* Director = BoundDirector.Get())
	{
		Director->OnStopExited.RemoveDynamic(this, &UHomeStateSubsystem::HandleStopExited);
	}
	BoundDirector.Reset();
}

void UHomeStateSubsystem::HandleStopExited(EStopExitReason Reason, float StoppedSeconds)
{
	if (bRunLost)
	{
		UE_LOG(LogHome, Log, TEXT("Home: stop exited (%s) after the run was lost: ignored."), LexToString(Reason));
		return;
	}
	const UScanDirectorComponent* Director = BoundDirector.Get();
	UWorld* World = Director ? Director->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const int32 Act = UTheLastShiftSettings::Get()->GetActOfMap(World->GetOutermost()->GetName());

	if (IsJumpExit(Reason))
	{
		ApplyJump(EHomeJumpCause::Contact, Act, StoppedSeconds);
		return;
	}
	if (Reason != EStopExitReason::Resumed)
	{
		return;
	}
	for (TActorIterator<AEntity> It(World); It; ++It)
	{
		// Records this Stop's exit now if the entity has not done it yet: the same single measurement either way.
		It->RecordStopExit(Reason);
		if (It->WasLastExitGraze())
		{
			ApplyJump(EHomeJumpCause::Graze, Act, StoppedSeconds);
		}
		break;
	}
}

bool UHomeStateSubsystem::ApplyJump(EHomeJumpCause Cause, int32 Act, float StoppedSeconds)
{
	if (bRunLost)
	{
		UE_LOG(LogHome, Log, TEXT("Home: %s jump in act %d ignored: the run is lost."), LexToString(Cause), Act);
		return false;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	FActTuning Tuning;
	if (!Settings->GetActTuning(Act, Tuning))
	{
		UE_LOG(LogHome, Warning, TEXT("Home: no ActTuning for act %d (%d entries): no jump."), Act, Settings->ActTuning.Num());
		return false;
	}
	const double Fraction = Cause == EHomeJumpCause::Contact ? 1.0 : static_cast<double>(Settings->GetGrazeJumpFraction());

	FHomeJump Jump;
	Jump.Cause = Cause;
	Jump.ActIndex = Act;
	Jump.StoppedSeconds = StoppedSeconds;
	Jump.HomeSecondsAdded = Tuning.JumpHomeSeconds * Fraction;
	Jump.BatteryBefore = BatteryFraction;
	Jump.BatteryAfter = FMath::Max(BatteryFraction - Tuning.JumpBatteryDrop * Fraction, 0.0);

	HomeSecondsElapsed += Jump.HomeSecondsAdded;
	BatteryFraction = Jump.BatteryAfter;
	++JumpCount;
	UE_LOG(LogHome, Log, TEXT("Home: %s jump in act %d after %.2f s stopped: home +%.0f s (JumpHomeSeconds %.0f x %.2f) = %.0f s, battery %.4f -> %.4f (JumpBatteryDrop %.4f x %.2f)."),
		LexToString(Cause), Act, StoppedSeconds, Jump.HomeSecondsAdded, Tuning.JumpHomeSeconds, Fraction, HomeSecondsElapsed,
		Jump.BatteryBefore, Jump.BatteryAfter, Tuning.JumpBatteryDrop, Fraction);
	OnHomeJump.Broadcast(Jump);

	if (BatteryFraction <= 0.0)
	{
		bRunLost = true;
		UE_LOG(LogHome, Log, TEXT("Home: battery at 0 in act %d: the run is lost."), Act);
		OnRunLost.Broadcast();
	}
	return true;
}

void UHomeStateSubsystem::RestoreState(double Battery, double HomeSeconds)
{
	BatteryFraction = FMath::Clamp(Battery, 0.0, 1.0);
	HomeSecondsElapsed = FMath::Max(HomeSeconds, 0.0);
	bRunLost = false;
	UE_LOG(LogHome, Log, TEXT("Home: state set: battery %.4f, home %.0f s."), BatteryFraction, HomeSecondsElapsed);
}
