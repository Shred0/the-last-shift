// SPDX-License-Identifier: MIT

#include "TextChannel/Typewriter.h"

void FTypewriter::Start(const FString& InText, float InCharsPerSecond)
{
	Text = InText;
	CharsPerSecond = FMath::Max(InCharsPerSecond, 1.f);
	Restart();
}

void FTypewriter::Restart()
{
	Elapsed = 0.0;
	Visible.Reset();
}

void FTypewriter::Reset()
{
	Text.Reset();
	Restart();
}

const FString& FTypewriter::Advance(float DeltaSeconds)
{
	Elapsed += FMath::Max(DeltaSeconds, 0.f);
	Refresh();
	return Visible;
}

void FTypewriter::Refresh()
{
	// A small tolerance, so that frame times summing to a whole second (60 x 1/60) reach the last character.
	const int32 Chars = FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(Elapsed * CharsPerSecond + 1.e-4)), 0, Text.Len());
	if (Chars != Visible.Len())
	{
		Visible = Text.Left(Chars);
	}
}
