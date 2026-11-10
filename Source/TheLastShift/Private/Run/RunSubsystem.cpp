// SPDX-License-Identifier: MIT

#include "Run/RunSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Run/TheLastShiftSettings.h"
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
}

void URunSubsystem::Deinitialize()
{
	if (UStdFlowSubsystem* Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr)
	{
		Flow->OnStateChanged.RemoveDynamic(this, &URunSubsystem::HandleFlowStateChanged);
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
	OnAreaEntered.Broadcast(Act);
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
