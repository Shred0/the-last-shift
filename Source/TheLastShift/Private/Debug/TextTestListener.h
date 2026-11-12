// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "TextChannel/TextChannelComponent.h"
#include "UObject/Object.h"
#include "TextTestListener.generated.h"

/**
 * Records a text channel's events for the TheLastShift.TextChannel automation tests and tls.TextChannelTest
 * (reflected, so it exists in every configuration; used in development builds only).
 */
UCLASS(Transient)
class UTlsTextTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UTextChannelComponent* InChannel)
	{
		Unbind();
		Reset();
		if (InChannel)
		{
			Channel = InChannel;
			InChannel->OnMessageStarted.AddUniqueDynamic(this, &UTlsTextTestListener::HandleStarted);
			InChannel->OnTextUpdated.AddUniqueDynamic(this, &UTlsTextTestListener::HandleText);
			InChannel->OnMessageFinished.AddUniqueDynamic(this, &UTlsTextTestListener::HandleFinished);
			InChannel->OnVisibilityChanged.AddUniqueDynamic(this, &UTlsTextTestListener::HandleVisibility);
		}
	}

	void Unbind()
	{
		if (UTextChannelComponent* Old = Channel.Get())
		{
			Old->OnMessageStarted.RemoveDynamic(this, &UTlsTextTestListener::HandleStarted);
			Old->OnTextUpdated.RemoveDynamic(this, &UTlsTextTestListener::HandleText);
			Old->OnMessageFinished.RemoveDynamic(this, &UTlsTextTestListener::HandleFinished);
			Old->OnVisibilityChanged.RemoveDynamic(this, &UTlsTextTestListener::HandleVisibility);
		}
		Channel.Reset();
	}

	void Reset()
	{
		Started.Reset();
		StartedLevels.Reset();
		StartedTexts.Reset();
		Finished.Reset();
		Visibility.Reset();
		TextUpdates = 0;
		LastText.Reset();
		bAllPrefixes = true;
		LastStartClock = LastCompleteClock = LastFinishClock = -1.0;
		LastTypingSeconds = -1.0;
	}

	/** The test's own clock (simulated seconds in the unit tests, real seconds in PIE); stamps every event. */
	double Clock = 0.0;
	bool bUseRealClock = false;

	TArray<FName> Started;
	TArray<ETextDegradation> StartedLevels;
	TArray<FString> StartedTexts;
	TArray<FName> Finished;
	TArray<bool> Visibility;
	int32 TextUpdates = 0;
	FString LastText;
	/** Every visible text so far was a prefix of the full text of the message being typed. */
	bool bAllPrefixes = true;
	double LastStartClock = -1.0;
	double LastCompleteClock = -1.0;
	double LastFinishClock = -1.0;
	/** Start to fully typed, of the last message that was fully typed. */
	double LastTypingSeconds = -1.0;

private:
	double Now() const { return bUseRealClock ? FPlatformTime::Seconds() : Clock; }

	UFUNCTION()
	void HandleStarted(FName RowName, ETextDegradation Level)
	{
		Started.Add(RowName);
		StartedLevels.Add(Level);
		StartedTexts.Add(Channel.IsValid() ? Channel->GetCurrentText() : FString());
		LastStartClock = Now();
		LastCompleteClock = -1.0;
	}

	UFUNCTION()
	void HandleText(const FString& VisibleText)
	{
		++TextUpdates;
		LastText = VisibleText;
		const UTextChannelComponent* C = Channel.Get();
		if (C && !VisibleText.IsEmpty())
		{
			const FString Full = C->GetCurrentText();
			if (!Full.StartsWith(VisibleText, ESearchCase::CaseSensitive))
			{
				bAllPrefixes = false;
			}
			if (VisibleText.Len() == Full.Len() && LastCompleteClock < 0.0)
			{
				LastCompleteClock = Now();
				LastTypingSeconds = LastStartClock >= 0.0 ? LastCompleteClock - LastStartClock : -1.0;
			}
		}
	}

	UFUNCTION()
	void HandleFinished(FName RowName)
	{
		Finished.Add(RowName);
		LastFinishClock = Now();
	}

	UFUNCTION()
	void HandleVisibility(bool bVisible)
	{
		Visibility.Add(bVisible);
	}

	TWeakObjectPtr<UTextChannelComponent> Channel;
};
