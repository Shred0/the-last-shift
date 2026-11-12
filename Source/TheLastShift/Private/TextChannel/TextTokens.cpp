// SPDX-License-Identifier: MIT

#include "TextChannel/TextTokens.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextTokens)

namespace TextTokensImpl
{
	constexpr double SecondsPerYear = 365.25 * 86400.0;

	struct FUnit
	{
		int64 Seconds;
		/** Full and NoArticles: singular, plural. */
		const TCHAR* One;
		const TCHAR* Many;
		/** Abbreviated: singular, plural. */
		const TCHAR* ShortOne;
		const TCHAR* ShortMany;
		/** Scattered. */
		const TCHAR* Shortest;
	};

	// The unit labels are the only words in this code.
	static const FUnit Units[] = {
		{ 86400, TEXT("day"), TEXT("days"), TEXT("day"), TEXT("days"), TEXT("d") },
		{ 3600, TEXT("hour"), TEXT("hours"), TEXT("hr"), TEXT("hrs"), TEXT("h") },
		{ 60, TEXT("minute"), TEXT("minutes"), TEXT("min"), TEXT("min"), TEXT("m") },
		{ 1, TEXT("second"), TEXT("seconds"), TEXT("sec"), TEXT("sec"), TEXT("s") },
	};
	static const FUnit Year = { 0, TEXT("year"), TEXT("years"), TEXT("yr"), TEXT("yrs"), TEXT("y") };

	static FString Written(int64 Value, const FUnit& Unit, ETextDegradation Level)
	{
		switch (Level)
		{
		case ETextDegradation::Full:
		case ETextDegradation::NoArticles:
			return FString::Printf(TEXT("%lld %s"), Value, Value == 1 ? Unit.One : Unit.Many);
		case ETextDegradation::Abbreviated:
			return FString::Printf(TEXT("%lld %s"), Value, Value == 1 ? Unit.ShortOne : Unit.ShortMany);
		case ETextDegradation::Scattered:
			return Value >= 1000
				? FString::Printf(TEXT("%lldk %s"), Value / 1000, Unit.Shortest)
				: FString::Printf(TEXT("%lld %s"), Value, Unit.Shortest);
		default:
			return FString();
		}
	}
}

FString UTextTokensLibrary::FormatDuration(double Seconds, ETextDegradation Level)
{
	using namespace TextTokensImpl;
	if (Level == ETextDegradation::Silent)
	{
		return FString();
	}
	const double Clamped = FMath::IsFinite(Seconds) ? FMath::Max(Seconds, 0.0) : 0.0;
	if (Clamped >= SecondsPerYear)
	{
		return Written(static_cast<int64>(FMath::FloorToDouble(Clamped / SecondsPerYear)), Year, Level);
	}

	int64 Left = static_cast<int64>(FMath::FloorToDouble(Clamped));
	TArray<FString> Parts;
	const int32 MaxParts = (Level == ETextDegradation::Full || Level == ETextDegradation::NoArticles) ? 2 : 1;
	for (const FUnit& Unit : Units)
	{
		const int64 Value = Left / Unit.Seconds;
		Left -= Value * Unit.Seconds;
		if (Value > 0 && Parts.Num() < MaxParts)
		{
			Parts.Add(Written(Value, Unit, Level));
		}
	}
	if (Parts.IsEmpty())
	{
		Parts.Add(Written(0, Units[UE_ARRAY_COUNT(Units) - 1], Level));
	}
	return FString::Join(Parts, TEXT(" and "));
}

FString UTextTokensLibrary::FormatBattery(double Fraction, ETextDegradation Level)
{
	if (Level == ETextDegradation::Silent)
	{
		return FString();
	}
	const int32 Percent = FMath::RoundToInt(FMath::Clamp(Fraction, 0.0, 1.0) * 100.0);
	return Level == ETextDegradation::Scattered ? FString::Printf(TEXT("%d"), Percent) : FString::Printf(TEXT("%d%%"), Percent);
}

FString UTextTokensLibrary::ReplaceTokens(const FString& Text, const FTextChannelContext& Context, ETextDegradation Level)
{
	FString Out = Text;
	Out.ReplaceInline(TEXT("{still}"), *FormatDuration(Context.StoppedSeconds, Level), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("{jump}"), *FormatDuration(Context.JumpSeconds, Level), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("{home}"), *FormatDuration(Context.HomeSecondsElapsed, Level), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("{battery}"), *FormatBattery(Context.BatteryFraction, Level), ESearchCase::CaseSensitive);
	return Out;
}
