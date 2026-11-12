// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Types a text out at a fixed speed: after each Advance the visible text is the prefix of the text that the elapsed
 * time has reached. A plain struct with no timing of its own: whoever owns it advances it.
 */
struct THELASTSHIFT_API FTypewriter
{
	/** Starts Text from its first character at CharsPerSecond (clamped to at least 1). */
	void Start(const FString& InText, float InCharsPerSecond);

	/** Back to the first character, same text and speed. */
	void Restart();

	/** Adds DeltaSeconds (negative ignored) and returns the visible prefix. */
	const FString& Advance(float DeltaSeconds);

	void Reset();

	const FString& GetText() const { return Text; }
	const FString& GetVisible() const { return Visible; }
	int32 GetVisibleChars() const { return Visible.Len(); }
	bool IsComplete() const { return Visible.Len() >= Text.Len(); }
	float GetCharsPerSecond() const { return CharsPerSecond; }

	/** Seconds the whole text takes at this speed. */
	double GetTypingSeconds() const { return CharsPerSecond > 0.f ? Text.Len() / static_cast<double>(CharsPerSecond) : 0.0; }

private:
	void Refresh();

	FString Text;
	FString Visible;
	float CharsPerSecond = 1.f;
	double Elapsed = 0.0;
};
