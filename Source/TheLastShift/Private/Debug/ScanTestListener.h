// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ScanTestListener.generated.h"

class UScanDirectorComponent;

/**
 * tls.ScanTest helper: binds to a scan director's dynamic delegates and records when each event happened,
 * in seconds since the Stop, together with the live tunable that governs what comes next (ScanCooldown at each scan,
 * BeatInterval at each beat), so the checks hold when the values are edited during the test. Debug only, never
 * referenced by game code.
 */
UCLASS(Transient)
class UTlsScanTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UScanDirectorComponent* InDirector);
	void Unbind();

	TArray<float> ScanTimes;
	TArray<float> ReadyTimes;
	TArray<float> BeatTimes;
	TArray<int32> BeatIndices;

	/** Clamped ScanCooldown read when each scan completed (the frame after it was issued and latched). */
	TArray<float> ScanCooldowns;

	/** Clamped BeatInterval read at each beat: the director arms the next beat with it in the same frame. */
	TArray<float> BeatIntervals;

private:
	UFUNCTION()
	void HandleScanCompleted(int32 ScanIndex, float Timestamp);

	UFUNCTION()
	void HandleScanReady();

	UFUNCTION()
	void HandleBeat(int32 BeatIndex);

	float GetStopElapsed() const;

	TWeakObjectPtr<UScanDirectorComponent> Director;
};
