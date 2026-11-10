// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Time/TimeState.h"
#include "UObject/Object.h"
#include "EntityTestListener.generated.h"

class AEntity;
class UScanDirectorComponent;
class UTimeStateSubsystem;

/**
 * tls.Entity* helper: binds to the scan director, the time state and an entity, and records every event with its
 * time since the Stop and the state that matters for the checks. Debug only, never referenced by game code.
 */
UCLASS(Transient)
class UTlsEntityTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UScanDirectorComponent* InDirector, AEntity* InEntity);
	void Unbind();
	void Clear();

	struct FBeat
	{
		int32 Index = 0;
		float Time = 0.f;
		float Cumulative = 0.f;
		bool bAwake = false;
		bool bManifested = false;
		float PathDistance = -1.f;
	};

	struct FSpawn
	{
		float Time = 0.f;
		float Cumulative = 0.f;
		FVector Location = FVector::ZeroVector;
		bool bHadLineOfSight = false;
	};

	struct FScan
	{
		int32 Index = 0;
		float Time = 0.f;
		int32 Uploaded = 0;
		int32 Gathered = 0;
		int32 OutsideCoverage = 0;
		int32 BehindDepth = -1;
		int32 FigurePoints = 0;
		int32 TrailPoints = 0;
	};

	TArray<FBeat> Beats;
	TArray<FSpawn> Spawns;
	TArray<FScan> Scans;
	TArray<float> AwakenTimes;
	TArray<float> ContactTimes;

	/** Player pose (actor transform and controller view rotation) when the first contact was broadcast. */
	bool bHasContactPose = false;
	FTransform ContactPawnTransform;
	FRotator ContactViewRotation = FRotator::ZeroRotator;

	/** Reasons seen on OnTimeStateChanged into Normal and on the director's OnStopExited. */
	TArray<EStopExitReason> TimeStateExitReasons;
	TArray<EStopExitReason> StopExitReasons;
	TArray<float> StopExitSeconds;

	/** Log every event as it happens. */
	bool bVerbose = true;

private:
	UFUNCTION()
	void HandleBeat(int32 BeatIndex);

	UFUNCTION()
	void HandleScanCompleted(int32 ScanIndex, float Timestamp);

	UFUNCTION()
	void HandleStopExited(EStopExitReason Reason, float StoppedSeconds);

	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	UFUNCTION()
	void HandleAwakened();

	UFUNCTION()
	void HandleSpawned(FVector Location, bool bHadLineOfSight);

	UFUNCTION()
	void HandleContact();

	float GetStopElapsed() const;
	float GetCumulative() const;

	TWeakObjectPtr<UScanDirectorComponent> Director;
	TWeakObjectPtr<AEntity> Entity;
	TWeakObjectPtr<UTimeStateSubsystem> TimeState;
};
