// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Flow/StdFlowSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RunSubsystem.generated.h"

class UWorld;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogRun, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAreaEntered, int32, ActIndex);

/**
 * The run across maps: which act the current map is, and the one-way travel to the next area through StdCore's
 * level transition (black screen with UTheLastShiftSettings::TravelLabel, held until the next area is ready and for
 * at least TravelHoldSeconds). The act is the current map's position in UTheLastShiftSettings::AreaMaps; this
 * subsystem reads the settings and copies nothing. After the last area, travel goes back to the main menu.
 */
UCLASS()
class THELASTSHIFT_API URunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The run subsystem of WorldContext's game instance, or null. */
	static URunSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Act of the current map (1-4), 0 outside the map list. */
	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetActIndex() const;

	/** Act of World's map (1-4), 0 outside the map list. */
	static int32 GetActIndexOf(const UWorld* World);

	/**
	 * Travels to the map after the current one (outside the list: to the first area); after the last area, to the main
	 * menu. Refused (false) while time is Stopped or while a transition is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToNextArea();

	/** Travels to the area of Act (1-4). Refused while time is Stopped or while a transition is already running. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	bool TravelToArea(int32 Act);

	/** Called by the area generator when the current area is ready; broadcasts OnAreaEntered. */
	void NotifyAreaReady(UWorld* World);

	/** The current area is generated, validated and playable. */
	UPROPERTY(BlueprintAssignable, Category = "Run")
	FOnAreaEntered OnAreaEntered;

	/** Real-time seconds of the last travel: request to black, request to area ready, request to fade in (-1 = not yet). */
	float GetLastTravelToCoveredSeconds() const { return LastTravelCovered; }
	float GetLastTravelToReadySeconds() const { return LastTravelReady; }
	float GetLastTravelToFadeInSeconds() const { return LastTravelFadeIn; }

private:
	bool TravelTo(const TSoftObjectPtr<UWorld>& Level, bool bMainMenu, const FString& What);

	UFUNCTION()
	void HandleFlowStateChanged(EStdFlowState NewState);

	/** FPlatformTime of the last travel request; negative when none is pending. */
	double TravelRequestTime = -1.0;
	float LastTravelCovered = -1.f;
	float LastTravelReady = -1.f;
	float LastTravelFadeIn = -1.f;
};
