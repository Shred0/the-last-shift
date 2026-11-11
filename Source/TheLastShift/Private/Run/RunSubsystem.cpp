// SPDX-License-Identifier: MIT

#include "Run/RunSubsystem.h"

#include "Energy/EnergySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Run/RunActListWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunSubsystem)

DEFINE_LOG_CATEGORY(LogRun);

URunSubsystem* URunSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<URunSubsystem>() : nullptr;
}

void URunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UStdFlowSubsystem* Flow = Collection.InitializeDependency<UStdFlowSubsystem>())
	{
		Flow->OnStateChanged.AddUniqueDynamic(this, &URunSubsystem::HandleFlowStateChanged);
	}
	Collection.InitializeDependency<UEnergySubsystem>();
	if (UHomeStateSubsystem* Home = Collection.InitializeDependency<UHomeStateSubsystem>())
	{
		Home->OnRunLost.AddUniqueDynamic(this, &URunSubsystem::HandleRunLost);
	}
	LoadSave();
}

void URunSubsystem::Deinitialize()
{
	if (UStdFlowSubsystem* Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr)
	{
		Flow->OnStateChanged.RemoveDynamic(this, &URunSubsystem::HandleFlowStateChanged);
	}
	if (UHomeStateSubsystem* Home = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHomeStateSubsystem>() : nullptr)
	{
		Home->OnRunLost.RemoveDynamic(this, &URunSubsystem::HandleRunLost);
	}
	Super::Deinitialize();
}

int32 URunSubsystem::GetActIndexOf(const UWorld* World)
{
	return World ? UTheLastShiftSettings::Get()->GetActOfMap(World->GetOutermost()->GetName()) : 0;
}

int32 URunSubsystem::GetActIndex() const
{
	return GetActIndexOf(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

bool URunSubsystem::TravelToNextArea()
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const int32 Act = GetActIndex();
	if (Act >= Settings->AreaMaps.Num() && Act > 0)
	{
		return TravelTo(TSoftObjectPtr<UWorld>(), true, FString::Printf(TEXT("act %d is the last: main menu"), Act));
	}
	const int32 Next = Act + 1;
	if (!Settings->AreaMaps.IsValidIndex(Next - 1))
	{
		UE_LOG(LogRun, Error, TEXT("TravelToNextArea: no map for act %d (AreaMaps has %d)."), Next, Settings->AreaMaps.Num());
		return false;
	}
	return TravelTo(Settings->AreaMaps[Next - 1], false, FString::Printf(TEXT("act %d -> act %d"), Act, Next));
}

bool URunSubsystem::TravelToArea(int32 Act)
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	if (!Settings->AreaMaps.IsValidIndex(Act - 1))
	{
		UE_LOG(LogRun, Error, TEXT("TravelToArea: no map for act %d (AreaMaps has %d)."), Act, Settings->AreaMaps.Num());
		return false;
	}
	return TravelTo(Settings->AreaMaps[Act - 1], false, FString::Printf(TEXT("act %d -> act %d"), GetActIndex(), Act));
}

bool URunSubsystem::TravelTo(const TSoftObjectPtr<UWorld>& Level, bool bMainMenu, const FString& What)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	if (!World || !Flow)
	{
		UE_LOG(LogRun, Error, TEXT("Travel (%s) refused: no world or no StdCore flow."), *What);
		return false;
	}
	if (const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>(); Time && Time->IsStopped())
	{
		UE_LOG(LogRun, Warning, TEXT("Travel (%s) refused: time is Stopped (an area is left in Normal time only)."), *What);
		return false;
	}
	bool bStarted = false;
	if (bMainMenu)
	{
		bStarted = Flow->ReturnToMainMenu();
	}
	else
	{
		const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
		FStdTravelOptions Options;
		Options.Label = Settings->TravelLabel;
		Options.MinCoveredSeconds = FMath::Clamp(Settings->TravelHoldSeconds, 0.f, 30.f);
		bStarted = Flow->TravelToLevelWithOptions(Level, Options);
	}
	if (bStarted)
	{
		TravelRequestTime = FPlatformTime::Seconds();
		LastTravelCovered = LastTravelReady = LastTravelFadeIn = -1.f;
	}
	UE_LOG(LogRun, Log, TEXT("Travel (%s): %s."), *What, bStarted ? TEXT("started") : TEXT("refused by the flow"));
	return bStarted;
}

void URunSubsystem::NotifyAreaReady(UWorld* World)
{
	const int32 Act = GetActIndexOf(World);
	if (TravelRequestTime >= 0.0)
	{
		LastTravelReady = static_cast<float>(FPlatformTime::Seconds() - TravelRequestTime);
	}
	UE_LOG(LogRun, Log, TEXT("Area entered: act %d (%s)%s."), Act, *GetNameSafe(World),
		LastTravelReady >= 0.f ? *FString::Printf(TEXT(", %.2f s after the travel request"), LastTravelReady) : TEXT(""));
	if (Act > 0)
	{
		TakeSnapshot(Act);
	}
	OnAreaEntered.Broadcast(Act);
}

// --- Snapshots and save ---

void URunSubsystem::TakeSnapshot(int32 Act)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UHomeStateSubsystem* Home = GameInstance ? GameInstance->GetSubsystem<UHomeStateSubsystem>() : nullptr;
	const UEnergySubsystem* Energy = GameInstance ? GameInstance->GetSubsystem<UEnergySubsystem>() : nullptr;
	FAreaSnapshot Snapshot;
	Snapshot.ActIndex = Act;
	Snapshot.BatteryFraction = Home ? Home->GetBatteryFraction() : 1.0;
	Snapshot.HomeSecondsElapsed = Home ? Home->GetHomeSecondsElapsed() : 0.0;
	Snapshot.BankedEnergy = Energy ? Energy->GetBankedEnergy() : 0;

	const int32 Discarded = Snapshots.RemoveAll([Act](const FAreaSnapshot& Old) { return Old.ActIndex >= Act; });
	Snapshots.Add(Snapshot);
	UE_LOG(LogRun, Log, TEXT("Snapshot: act %d, battery %.4f, home %.0f s, banked %d (%d replaced or discarded, %d snapshots)."),
		Act, Snapshot.BatteryFraction, Snapshot.HomeSecondsElapsed, Snapshot.BankedEnergy, Discarded, Snapshots.Num());
	WriteSave();
	OnSnapshotsChanged.Broadcast();
}

void URunSubsystem::WriteSave()
{
	URunSaveGame* Save = Cast<URunSaveGame>(UGameplayStatics::CreateSaveGameObject(URunSaveGame::StaticClass()));
	if (!Save)
	{
		bLastSaveOk = false;
		return;
	}
	Save->Snapshots = Snapshots;
	bLastSaveOk = UGameplayStatics::SaveGameToSlot(Save, SaveSlotName(), 0);
	++SaveWrites;
	UE_LOG(LogRun, Log, TEXT("Save: %d snapshots written to slot '%s': %s."), Snapshots.Num(), SaveSlotName(), bLastSaveOk ? TEXT("ok") : TEXT("FAILED"));
}

bool URunSubsystem::LoadSave()
{
	Snapshots.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName(), 0))
	{
		if (const URunSaveGame* Save = Cast<URunSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName(), 0)))
		{
			const int32 ActCount = UTheLastShiftSettings::Get()->AreaMaps.Num();
			for (const FAreaSnapshot& Snapshot : Save->Snapshots)
			{
				// One per act, in act order, acts that exist.
				if (Snapshot.ActIndex >= 1 && Snapshot.ActIndex <= ActCount && (Snapshots.IsEmpty() || Snapshot.ActIndex > Snapshots.Last().ActIndex))
				{
					Snapshots.Add(Snapshot);
				}
			}
		}
		else
		{
			UE_LOG(LogRun, Warning, TEXT("Save: slot '%s' exists but does not load as a run save; ignored."), SaveSlotName());
		}
	}
	UE_LOG(LogRun, Log, TEXT("Save: %d snapshots read from slot '%s'."), Snapshots.Num(), SaveSlotName());
	OnSnapshotsChanged.Broadcast();
	return Snapshots.Num() > 0;
}

void URunSubsystem::ClearSave()
{
	Snapshots.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName(), 0))
	{
		UGameplayStatics::DeleteGameInSlot(SaveSlotName(), 0);
	}
	UE_LOG(LogRun, Log, TEXT("Save: slot '%s' cleared."), SaveSlotName());
	OnSnapshotsChanged.Broadcast();
}

bool URunSubsystem::CanTravelNow(const TCHAR* What) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	if (!World || !Flow || Flow->IsTransitioning())
	{
		UE_LOG(LogRun, Warning, TEXT("%s refused: no world or flow, or a transition is running."), What);
		return false;
	}
	if (const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>(); Time && Time->IsStopped())
	{
		UE_LOG(LogRun, Warning, TEXT("%s refused: time is Stopped."), What);
		return false;
	}
	return true;
}

bool URunSubsystem::StartNewRun()
{
	if (!CanTravelNow(TEXT("New run")) || UTheLastShiftSettings::Get()->GetFirstAreaMap().IsNull())
	{
		return false;
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (UHomeStateSubsystem* Home = GameInstance->GetSubsystem<UHomeStateSubsystem>())
	{
		Home->ResetRun();
	}
	if (UEnergySubsystem* Energy = GameInstance->GetSubsystem<UEnergySubsystem>())
	{
		Energy->RestoreBankedEnergy(0);
	}
	ClearSave();
	UE_LOG(LogRun, Log, TEXT("New run: battery 1, home 0 s, banked 0, save cleared."));
	return TravelToArea(1);
}

bool URunSubsystem::RestartFromAct(int32 Act)
{
	if (Act == 1)
	{
		return StartNewRun();
	}
	const FAreaSnapshot* Snapshot = Snapshots.FindByPredicate([Act](const FAreaSnapshot& S) { return S.ActIndex == Act; });
	if (!Snapshot)
	{
		UE_LOG(LogRun, Warning, TEXT("Restart from act %d refused: no snapshot for it (%d snapshots)."), Act, Snapshots.Num());
		return false;
	}
	if (!CanTravelNow(*FString::Printf(TEXT("Restart from act %d"), Act)))
	{
		return false;
	}
	const FAreaSnapshot Restored = *Snapshot;
	UGameInstance* GameInstance = GetGameInstance();
	if (UHomeStateSubsystem* Home = GameInstance->GetSubsystem<UHomeStateSubsystem>())
	{
		Home->RestoreState(Restored.BatteryFraction, Restored.HomeSecondsElapsed);
	}
	if (UEnergySubsystem* Energy = GameInstance->GetSubsystem<UEnergySubsystem>())
	{
		Energy->RestoreBankedEnergy(Restored.BankedEnergy);
	}
	const int32 Discarded = Snapshots.RemoveAll([Act](const FAreaSnapshot& S) { return S.ActIndex > Act; });
	UE_LOG(LogRun, Log, TEXT("Restart from act %d: battery %.4f, home %.0f s, banked %d; %d later snapshots discarded."),
		Act, Restored.BatteryFraction, Restored.HomeSecondsElapsed, Restored.BankedEnergy, Discarded);
	WriteSave();
	OnSnapshotsChanged.Broadcast();
	return TravelToArea(Act);
}

bool URunSubsystem::ContinueRun()
{
	if (Snapshots.IsEmpty())
	{
		UE_LOG(LogRun, Warning, TEXT("Continue refused: no saved run."));
		return false;
	}
	return RestartFromAct(Snapshots.Last().ActIndex);
}

bool URunSubsystem::ReturnToMainMenu()
{
	return TravelTo(TSoftObjectPtr<UWorld>(), true, TEXT("main menu"));
}

// --- Loss ---

void URunSubsystem::HandleRunLost()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogRun, Warning, TEXT("Run lost: no player controller to show the loss screen on."));
		return;
	}
	if (AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC))
	{
		StdPC->ResumeGame();
		StdPC->bCanPause = false;
	}
	// Time resumes if a debug jump ended the run while Stopped (the home subsystem ignores that exit).
	if (const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>(); Time && Time->IsStopped())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UScanDirectorComponent* Director = Pawn->FindComponentByClass<UScanDirectorComponent>())
			{
				Director->ForceExit(EStopExitReason::Resumed);
			}
		}
	}
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->DisableInput(PC);
	}
	PC->FlushPressedKeys();

	URunActListWidget* Screen = CreateWidget<URunActListWidget>(PC, URunActListWidget::StaticClass());
	if (!Screen)
	{
		return;
	}
	Screen->Configure(ERunActListMode::RunLost);
	Screen->ShowAsRoot(60);
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);
	Screen->FocusDefaultWidget();
	LossScreen = Screen;
	UE_LOG(LogRun, Log, TEXT("Run lost in act %d: loss screen shown with %d acts."), GetActIndexOf(World), Snapshots.Num());
}

void URunSubsystem::HandleFlowStateChanged(EStdFlowState NewState)
{
	if (TravelRequestTime < 0.0)
	{
		return;
	}
	const float Since = static_cast<float>(FPlatformTime::Seconds() - TravelRequestTime);
	if (NewState == EStdFlowState::Loading)
	{
		LastTravelCovered = Since;
	}
	else if (NewState == EStdFlowState::FadingIn)
	{
		LastTravelFadeIn = Since;
		UE_LOG(LogRun, Log, TEXT("Travel: black at %.2f s, area ready at %.2f s, fade in at %.2f s after the request (black for %.2f s)."),
			LastTravelCovered, LastTravelReady, LastTravelFadeIn, LastTravelFadeIn - LastTravelCovered);
		TravelRequestTime = -1.0;
	}
	else if (NewState == EStdFlowState::Idle && LastTravelFadeIn < 0.f)
	{
		TravelRequestTime = -1.0;
	}
}
