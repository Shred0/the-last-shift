// SPDX-License-Identifier: MIT

// Automation tests of the text channel that need no world (Brief 6, criteria 2 to 7; Brief 7: the structure of the
// narrative CSV files, never their wording, number or length; Brief 8: DT_Logs and DT_TwinAI too, the deal of the logs
// to the terminals and the twin voice's counts): TheLastShift.TextChannel.*, TheLastShift.TwinWriting.*.
// Run with: Automation RunTests TheLastShift.TextChannel. Development editor builds only.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Debug/TextTestListener.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TextDegradation.h"
#include "TextChannel/TextTokens.h"
#include "TextChannel/TwinWriting.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TextChannelTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Tick = 1.f / 60.f;
	constexpr double SecondsPerYear = 365.25 * 86400.0;

	/** Five neutral sentences with articles, digits and a token. */
	static const TCHAR* Sentences[] = {
		TEXT("The cat sat on the mat for 12 minutes and then the dog came in at {still}."),
		TEXT("A train left the station at 7 and an hour later the rain started over the hills {jump}."),
		TEXT("The quick brown fox jumps over a lazy dog 3 times before {battery} of the day is gone."),
		TEXT("An apple, a pear and the last orange cost 250 coins at the market on a Tuesday {home}."),
		TEXT("The report lists 40 rooms, an elevator and a long corridor; the count is {still}."),
	};

	/** Whole words of S, lowercase. */
	static TArray<FString> Words(const FString& S)
	{
		TArray<FString> Out;
		FString Word;
		for (const TCHAR C : S)
		{
			if (FChar::IsAlpha(C))
			{
				Word.AppendChar(FChar::ToLower(C));
			}
			else if (!Word.IsEmpty())
			{
				Out.Add(Word);
				Word.Reset();
			}
		}
		if (!Word.IsEmpty())
		{
			Out.Add(Word);
		}
		return Out;
	}

	/** Runs of digits and {tokens} of S, in order. */
	static TArray<FString> Protected(const FString& S)
	{
		TArray<FString> Out;
		for (int32 I = 0; I < S.Len(); )
		{
			if (FChar::IsDigit(S[I]))
			{
				int32 J = I;
				while (J < S.Len() && FChar::IsDigit(S[J])) { ++J; }
				Out.Add(S.Mid(I, J - I));
				I = J;
			}
			else if (S[I] == TEXT('{'))
			{
				const int32 J = S.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
				if (J == INDEX_NONE) { break; }
				Out.Add(S.Mid(I, J - I + 1));
				I = J + 1;
			}
			else
			{
				++I;
			}
		}
		return Out;
	}

	static UTextChannelComponent* NewChannel()
	{
		return NewObject<UTextChannelComponent>(GetTransientPackage());
	}

	/** Advances the channel by one tick at a time until Done or MaxSeconds of simulated time; returns the time spent. */
	static double RunUntil(UTextChannelComponent* Channel, UTlsTextTestListener* Listener, TFunctionRef<bool()> Done, double MaxSeconds)
	{
		double Spent = 0.0;
		while (!Done() && Spent < MaxSeconds)
		{
			Channel->AdvanceSeconds(Tick);
			Spent += Tick;
			Listener->Clock += Tick;
		}
		return Spent;
	}

	static FAIMessageRow MakeRow(ETextTrigger Trigger, int32 Act, int32 Order, const TCHAR* Text)
	{
		FAIMessageRow Row;
		Row.Trigger = Trigger;
		Row.Act = Act;
		Row.Order = Order;
		Row.Text = FText::FromString(Text);
		return Row;
	}

	static FTextChannelContext Context(double Battery)
	{
		FTextChannelContext C;
		C.BatteryFraction = Battery;
		C.StoppedSeconds = 131.0;
		C.JumpSeconds = 600.0;
		C.HomeSecondsElapsed = 212.0 * SecondsPerYear;
		return C;
	}
}

// --- Criterion 2 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelLevelTest, "TheLastShift.TextChannel.Level", TextChannelTests::Flags)

bool FTextChannelLevelTest::RunTest(const FString& Parameters)
{
	UTextChannelComponent* Channel = TextChannelTests::NewChannel();
	struct FCase { double Battery; ETextDegradation Expected; };
	const FCase Cases[] = {
		{ 1.00, ETextDegradation::Full }, { 0.70, ETextDegradation::Full },
		{ 0.69, ETextDegradation::NoArticles }, { 0.40, ETextDegradation::NoArticles },
		{ 0.39, ETextDegradation::Abbreviated }, { 0.10, ETextDegradation::Abbreviated },
		{ 0.09, ETextDegradation::Scattered }, { 0.03, ETextDegradation::Scattered },
		{ 0.02, ETextDegradation::Silent },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("Battery %.2f is %s"), Case.Battery, LexToString(Case.Expected)),
			Channel->GetLevelForBattery(Case.Battery), Case.Expected);
	}
	// The thresholds are the project settings' (one owner): a change there moves the channel's level, and is put back.
	FTextDegradationTuning& Tuning = GetMutableDefault<UTheLastShiftSettings>()->TextDegradation;
	const FTextDegradationTuning Saved = Tuning;
	Tuning.NoArticlesBelow = 0.80f;
	TestEqual(TEXT("NoArticlesBelow 0.80 in the settings: battery 0.75 is NoArticles"), Channel->GetLevelForBattery(0.75), ETextDegradation::NoArticles);
	Tuning.SilentBelow = 0.05f;
	TestEqual(TEXT("SilentBelow 0.05 in the settings: battery 0.03 is Silent"), Channel->GetLevelForBattery(0.03), ETextDegradation::Silent);
	Tuning.CharsPerSecondFull = 60.f;
	TestEqual(TEXT("CharsPerSecondFull 60 in the settings: the channel types at 60 at battery 1"), Channel->GetCharsPerSecond(1.0), 60.f);
	Tuning = Saved;
	TestEqual(TEXT("Settings put back: battery 0.75 is Full again"), Channel->GetLevelForBattery(0.75), ETextDegradation::Full);
	return true;
}

// --- Criterion 3 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelDegradationTest, "TheLastShift.TextChannel.Degradation", TextChannelTests::Flags)

bool FTextChannelDegradationTest::RunTest(const FString& Parameters)
{
	using namespace TextChannelTests;
	const ETextDegradation Levels[] = { ETextDegradation::Full, ETextDegradation::NoArticles, ETextDegradation::Abbreviated, ETextDegradation::Scattered };
	for (const TCHAR* Sentence : Sentences)
	{
		const FString Full = Sentence;
		const FString NoArticles = UTextDegradationLibrary::Degrade(Full, ETextDegradation::NoArticles);
		const FString Abbreviated = UTextDegradationLibrary::Degrade(Full, ETextDegradation::Abbreviated);
		const FString Scattered = UTextDegradationLibrary::Degrade(Full, ETextDegradation::Scattered);
		AddInfo(FString::Printf(TEXT("Full: %s | NoArticles: %s | Abbreviated: %s | Scattered: %s"), *Full, *NoArticles, *Abbreviated, *Scattered));

		TestEqual(TEXT("Full is unchanged"), UTextDegradationLibrary::Degrade(Full, ETextDegradation::Full), Full);
		const TArray<FString> Remaining = Words(NoArticles);
		TestFalse(FString::Printf(TEXT("NoArticles has no whole-word a, an, the: %s"), *NoArticles),
			Remaining.Contains(TEXT("a")) || Remaining.Contains(TEXT("an")) || Remaining.Contains(TEXT("the")));
		TestEqual(TEXT("Abbreviated is lowercase"), Abbreviated, Abbreviated.ToLower());
		TestTrue(FString::Printf(TEXT("Abbreviated (%d) is not longer than NoArticles (%d)"), Abbreviated.Len(), NoArticles.Len()), Abbreviated.Len() <= NoArticles.Len());
		TestTrue(FString::Printf(TEXT("Scattered (%d) is at most half of Full (%d)"), Scattered.Len(), Full.Len()), Scattered.Len() * 2 <= Full.Len());
		TestTrue(TEXT("Silent is empty"), UTextDegradationLibrary::Degrade(Full, ETextDegradation::Silent).IsEmpty());

		const TArray<FString> Kept = Protected(Full);
		for (const ETextDegradation Level : Levels)
		{
			const FString Out = UTextDegradationLibrary::Degrade(Full, Level);
			TestEqual(FString::Printf(TEXT("Digits and tokens intact at %s"), LexToString(Level)), Protected(Out), Kept);
			bool bSame = true;
			for (int32 I = 0; I < 100; ++I)
			{
				bSame &= UTextDegradationLibrary::Degrade(Full, Level) == Out;
			}
			TestTrue(FString::Printf(TEXT("100 calls give the same output at %s"), LexToString(Level)), bSame);
		}
	}
	return true;
}

// --- Criterion 4 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelVariantsTest, "TheLastShift.TextChannel.Variants", TextChannelTests::Flags)

bool FTextChannelVariantsTest::RunTest(const FString& Parameters)
{
	FAIMessageRow Row = TextChannelTests::MakeRow(ETextTrigger::JumpReturn, 1, 1, TEXT("The level of the cell is {battery} after a wait of {still}."));
	Row.TextAbbreviated = FText::FromString(TEXT("explicit variant {battery} {still}"));
	FTextChannelContext Context = TextChannelTests::Context(0.31);
	const FString Abbreviated = UTextChannelComponent::ComposeMessage(Row, ETextDegradation::Abbreviated, Context);
	TestEqual(TEXT("Abbreviated uses the variant, tokens replaced"), Abbreviated, FString(TEXT("explicit variant 31% 2 min")));
	for (const ETextDegradation Level : { ETextDegradation::Full, ETextDegradation::NoArticles, ETextDegradation::Scattered })
	{
		const FString Expected = UTextTokensLibrary::ReplaceTokens(UTextDegradationLibrary::Degrade(Row.Text.ToString(), Level), Context, Level);
		TestEqual(FString::Printf(TEXT("%s is the automatic text"), LexToString(Level)), UTextChannelComponent::ComposeMessage(Row, Level, Context), Expected);
	}
	TestTrue(TEXT("Silent is empty even with variants"), UTextChannelComponent::ComposeMessage(Row, ETextDegradation::Silent, Context).IsEmpty());
	return true;
}

// --- Criterion 5 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelDurationsTest, "TheLastShift.TextChannel.Durations", TextChannelTests::Flags)

bool FTextChannelDurationsTest::RunTest(const FString& Parameters)
{
	using TextChannelTests::SecondsPerYear;
	TestEqual(TEXT("131 s at Full"), UTextTokensLibrary::FormatDuration(131.0, ETextDegradation::Full), FString(TEXT("2 minutes and 11 seconds")));
	TestEqual(TEXT("4 years at Full"), UTextTokensLibrary::FormatDuration(4.0 * SecondsPerYear, ETextDegradation::Full), FString(TEXT("4 years")));
	TestEqual(TEXT("212 years at Abbreviated"), UTextTokensLibrary::FormatDuration(212.0 * SecondsPerYear, ETextDegradation::Abbreviated), FString(TEXT("212 yrs")));
	TestEqual(TEXT("1000 years at Scattered"), UTextTokensLibrary::FormatDuration(1000.0 * SecondsPerYear, ETextDegradation::Scattered), FString(TEXT("1k y")));
	TestEqual(TEXT("Battery 0.31 at Full"), UTextTokensLibrary::FormatBattery(0.31, ETextDegradation::Full), FString(TEXT("31%")));
	TestEqual(TEXT("Battery 0.31 at Scattered"), UTextTokensLibrary::FormatBattery(0.31, ETextDegradation::Scattered), FString(TEXT("31")));
	// Neighbours of the rules above.
	TestEqual(TEXT("600 s at NoArticles"), UTextTokensLibrary::FormatDuration(600.0, ETextDegradation::NoArticles), FString(TEXT("10 minutes")));
	TestEqual(TEXT("Half a year at Full: two largest units"), UTextTokensLibrary::FormatDuration(15778800.0, ETextDegradation::Full), FString(TEXT("182 days and 15 hours")));
	TestEqual(TEXT("1 year at Full"), UTextTokensLibrary::FormatDuration(SecondsPerYear, ETextDegradation::Full), FString(TEXT("1 year")));
	TestEqual(TEXT("131 s at Scattered"), UTextTokensLibrary::FormatDuration(131.0, ETextDegradation::Scattered), FString(TEXT("2 m")));
	TestEqual(TEXT("0 s at Full"), UTextTokensLibrary::FormatDuration(0.0, ETextDegradation::Full), FString(TEXT("0 seconds")));
	TestEqual(TEXT("Battery 0.31 at Abbreviated"), UTextTokensLibrary::FormatBattery(0.31, ETextDegradation::Abbreviated), FString(TEXT("31%")));
	return true;
}

// --- Criterion 6 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelTypewriterTest, "TheLastShift.TextChannel.TypewriterQueue", TextChannelTests::Flags)

bool FTextChannelTypewriterTest::RunTest(const FString& Parameters)
{
	using namespace TextChannelTests;
	// Digits only: the same 40 characters at every level, so the speed alone is measured.
	const FString Forty = TEXT("1234567890 1234567890 1234567890 1234567");
	TestEqual(TEXT("The message has 40 characters"), Forty.Len(), 40);

	UTextChannelComponent* Channel = NewChannel();
	TStrongObjectPtr<UTlsTextTestListener> Listener(NewObject<UTlsTextTestListener>());
	Listener->Bind(Channel);

	TestTrue(TEXT("Push at battery 1.0"), Channel->PushText(Forty, Context(1.0)));
	TestTrue(TEXT("Push a second message"), Channel->PushText(TEXT("second message"), Context(1.0)));
	TestEqual(TEXT("The first starts at once"), Listener->Started.Num(), 1);
	const double Typed = RunUntil(Channel, Listener.Get(), [&]() { return Channel->GetVisibleText().Len() >= 40; }, 10.0);
	AddInfo(FString::Printf(TEXT("40 characters at battery 1.0 typed in %.3f s (%.1f chars/s)."), Typed, Channel->GetCharsPerSecond(1.0)));
	TestTrue(TEXT("40 characters at battery 1.0 are typed in 1.0 s (one tick of tolerance)"), Typed <= 1.0 + Tick * 0.5 && Typed >= 1.0 - Tick * 1.5);
	TestTrue(TEXT("The visible text was always a prefix of the final text"), Listener->bAllPrefixes);
	TestEqual(TEXT("The final text is the message"), Channel->GetVisibleText(), Forty);

	const double Hold = RunUntil(Channel, Listener.Get(), [&]() { return Listener->Finished.Num() > 0; }, 20.0);
	const double ExpectedHold = Channel->HoldSeconds + 40 * Channel->HoldSecondsPerChar;
	AddInfo(FString::Printf(TEXT("Finished %.3f s after fully typed (expected %.3f)."), Hold, ExpectedHold));
	TestTrue(TEXT("Finishes HoldSeconds + 40 x HoldSecondsPerChar after fully typed"), FMath::Abs(Hold - ExpectedHold) <= Tick * 1.5);
	TestEqual(TEXT("OnMessageFinished fired once"), Listener->Finished.Num(), 1);
	TestFalse(TEXT("Hidden between messages"), Channel->IsVisible());

	const double Gap = RunUntil(Channel, Listener.Get(), [&]() { return Listener->Started.Num() > 1; }, 10.0);
	AddInfo(FString::Printf(TEXT("Second message started %.3f s after the first finished (GapSeconds %.2f)."), Gap, Channel->GapSeconds));
	TestTrue(TEXT("The second starts GapSeconds after the first finishes"), FMath::Abs(Gap - Channel->GapSeconds) <= Tick * 1.5);
	RunUntil(Channel, Listener.Get(), [&]() { return !Channel->IsBusy(); }, 20.0);
	TestEqual(TEXT("OnMessageFinished fired once per message"), Listener->Finished.Num(), 2);

	// Low battery: slower.
	Listener->Reset();
	Channel->PushText(Forty, Context(0.03));
	// Timed from the start of the message: the gap after the previous one comes first.
	RunUntil(Channel, Listener.Get(), [&]() { return Listener->Started.Num() > 0; }, 5.0);
	const double Slow = RunUntil(Channel, Listener.Get(), [&]() { return Channel->GetVisibleText().Len() >= 40; }, 20.0);
	AddInfo(FString::Printf(TEXT("40 characters at battery 0.03 typed in %.3f s (%.2f chars/s)."), Slow, Channel->GetCharsPerSecond(0.03)));
	TestTrue(TEXT("40 characters at battery 0.03 take more than 4 s"), Slow > 4.0);
	TestTrue(TEXT("Prefixes at low battery too"), Listener->bAllPrefixes);
	Channel->ClearAll();

	// Queue limit: nothing starts while suspended.
	Channel->SetSuspended(true);
	int32 Accepted = 0;
	for (int32 I = 0; I < 8; ++I)
	{
		Accepted += Channel->PushText(FString::Printf(TEXT("queued %d"), I), Context(1.0)) ? 1 : 0;
	}
	TestEqual(TEXT("Eight messages are queued"), Accepted, 8);
	AddExpectedMessage(TEXT("the queue is full"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	TestFalse(TEXT("A ninth queued message is refused"), Channel->PushText(TEXT("ninth"), Context(1.0)));
	TestEqual(TEXT("Still eight queued"), Channel->GetQueuedCount(), 8);

	// Suspension: nothing shown, restart from the first character.
	Channel->ClearAll();
	Channel->SetSuspended(false);
	Listener->Reset();
	Channel->PushText(Forty, Context(1.0));
	RunUntil(Channel, Listener.Get(), [&]() { return Channel->GetVisibleText().Len() >= 20; }, 5.0);
	Channel->SetSuspended(true);
	TestTrue(TEXT("Suspended: nothing visible"), Channel->GetVisibleText().IsEmpty() && !Channel->IsVisible());
	RunUntil(Channel, Listener.Get(), [&]() { return false; }, 1.0);
	TestTrue(TEXT("Suspended: nothing typed"), Channel->GetVisibleText().IsEmpty());
	Channel->SetSuspended(false);
	TestTrue(TEXT("Resumed: visible again, from the first character"), Channel->IsVisible() && Channel->GetVisibleText().IsEmpty());
	Channel->AdvanceSeconds(Tick);
	TestTrue(TEXT("Resumed: one tick types one character"), Channel->GetVisibleText().Len() <= 1);
	TestEqual(TEXT("Resumed: the same message, not a new one"), Listener->Started.Num(), 1);

	Listener->Unbind();
	return true;
}

// --- Criterion 7 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelSelectionTest, "TheLastShift.TextChannel.Selection", TextChannelTests::Flags)

bool FTextChannelSelectionTest::RunTest(const FString& Parameters)
{
	using namespace TextChannelTests;
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = FAIMessageRow::StaticStruct();
	// Added out of order: the Order column decides.
	Table->AddRow(TEXT("T_AreaStart_3"), MakeRow(ETextTrigger::AreaStart, 1, 3, TEXT("third line")));
	Table->AddRow(TEXT("T_AreaStart_1"), MakeRow(ETextTrigger::AreaStart, 1, 1, TEXT("first line")));
	Table->AddRow(TEXT("T_AreaStart_2"), MakeRow(ETextTrigger::AreaStart, 1, 2, TEXT("second line")));
	Table->AddRow(TEXT("T_Spend_1"), MakeRow(ETextTrigger::Spend, 2, 1, TEXT("other act")));

	UTextChannelComponent* Channel = NewChannel();
	Channel->SetMessageTable(Table);
	TStrongObjectPtr<UTlsTextTestListener> Listener(NewObject<UTlsTextTestListener>());
	Listener->Bind(Channel);
	Channel->SetSuspended(true);

	for (int32 I = 0; I < 4; ++I)
	{
		TestTrue(FString::Printf(TEXT("PushTrigger call %d"), I + 1), Channel->PushTrigger(ETextTrigger::AreaStart, 1, Context(1.0)));
	}
	Channel->SetSuspended(false);
	RunUntil(Channel, Listener.Get(), [&]() { return !Channel->IsBusy(); }, 120.0);
	const FString Got = FString::JoinBy(Listener->Started, TEXT(","), [](const FName& N) { return N.ToString(); });
	TestEqual(TEXT("Four PushTrigger calls: rows in order, then the last again"), Got, FString(TEXT("T_AreaStart_1,T_AreaStart_2,T_AreaStart_3,T_AreaStart_3")));

	Listener->Reset();
	TestEqual(TEXT("PushSequence queues three"), Channel->PushSequence(ETextTrigger::AreaStart, 1, Context(1.0)), 3);
	RunUntil(Channel, Listener.Get(), [&]() { return !Channel->IsBusy(); }, 120.0);
	TestEqual(TEXT("PushSequence: all three in order"), FString::JoinBy(Listener->Started, TEXT(","), [](const FName& N) { return N.ToString(); }),
		FString(TEXT("T_AreaStart_1,T_AreaStart_2,T_AreaStart_3")));

	Listener->Reset();
	TestFalse(TEXT("A trigger with no row: no message"), Channel->PushTrigger(ETextTrigger::FirstEnergy, 1, Context(1.0)));
	TestEqual(TEXT("A sequence with no row: nothing"), Channel->PushSequence(ETextTrigger::AreaEntry, 3, Context(1.0)), 0);
	AddExpectedMessage(TEXT("no JumpReturn row for act 4"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	TestFalse(TEXT("JumpReturn with no row: no message (one warning)"), Channel->PushTrigger(ETextTrigger::JumpReturn, 4, Context(1.0)));
	RunUntil(Channel, Listener.Get(), [&]() { return false; }, 1.0);
	TestEqual(TEXT("Nothing started"), Listener->Started.Num(), 0);

	Listener->Reset();
	Channel->PushRow(TEXT("T_AreaStart_1"), Context(0.02));
	RunUntil(Channel, Listener.Get(), [&]() { return false; }, 1.0);
	TestEqual(TEXT("A Silent message is discarded"), Listener->Started.Num(), 0);
	Listener->Unbind();
	return true;
}

// --- Structure of the narrative CSV files (Brief 7): never their wording, their number or their length ---

#if WITH_EDITOR
namespace NarrativeCsv
{
	static const TCHAR* AllowedTokens[] = { TEXT("{still}"), TEXT("{jump}"), TEXT("{home}"), TEXT("{battery}") };
	static const FName AIMessageTextColumns[] = { TEXT("Text"), TEXT("TextNoArticles"), TEXT("TextAbbreviated"), TEXT("TextScattered") };

	static FString Load(const TCHAR* FileName)
	{
		FString Csv;
		FFileHelper::LoadFileToString(Csv, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Narrative"), FileName));
		return Csv;
	}

	/** Non-empty physical lines of Csv minus the header: equal to the row count unless a cell spans lines. */
	static int32 CountDataLines(const FString& Csv)
	{
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines, true);
		int32 Count = 0;
		for (const FString& Line : Lines)
		{
			Count += Line.TrimStartAndEnd().IsEmpty() ? 0 : 1;
		}
		return FMath::Max(Count - 1, 0);
	}

	static UDataTable* Parse(const FString& Csv, UScriptStruct* Struct, TArray<FString>& OutProblems, const TCHAR* File)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = Struct;
		for (const FString& Problem : Table->CreateTableFromCSVString(Csv))
		{
			OutProblems.Add(FString::Printf(TEXT("%s does not parse against %s: %s"), File, *Struct->GetName(), *Problem));
		}
		if (CountDataLines(Csv) != Table->GetRowMap().Num())
		{
			OutProblems.Add(FString::Printf(TEXT("%s: %d data lines for %d rows, a cell has a line break"), File, CountDataLines(Csv), Table->GetRowMap().Num()));
		}
		return Table;
	}

	static FString TextCell(const UScriptStruct* Struct, const uint8* Row, FName Column)
	{
		const FTextProperty* Property = CastField<FTextProperty>(Struct->FindPropertyByName(Column));
		return Property ? Property->GetPropertyValue_InContainer(Row).ToString() : FString();
	}

	static void CheckLineBreaks(const TCHAR* File, const FName& RowName, FName Column, const FString& Cell, TArray<FString>& OutProblems)
	{
		if (Cell.Contains(TEXT("\n")) || Cell.Contains(TEXT("\r")))
		{
			OutProblems.Add(FString::Printf(TEXT("%s, row %s, %s: the cell has a line break"), File, *RowName.ToString(), *Column.ToString()));
		}
	}

	/** Whole words of S, lowercase, with every {token} removed first. */
	static TArray<FString> WordsWithoutTokens(const FString& S)
	{
		FString Plain;
		for (int32 I = 0; I < S.Len(); ++I)
		{
			if (S[I] == TEXT('{'))
			{
				const int32 Close = S.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
				if (Close != INDEX_NONE)
				{
					Plain.AppendChar(TEXT(' '));
					I = Close;
					continue;
				}
			}
			Plain.AppendChar(S[I]);
		}
		return TextChannelTests::Words(Plain);
	}

	/** Every problem of DT_AIMessages.csv (empty: the structure is right). */
	static TArray<FString> CheckAIMessages(const FString& Csv, const TCHAR* File = TEXT("DT_AIMessages.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FAIMessageRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		if (Table->GetRowMap().IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s has no rows"), File));
		}
		const UEnum* Triggers = StaticEnum<ETextTrigger>();
		const FRegexPattern NamePattern(TEXT("^A([1-4])_([A-Za-z]+)_([0-9]+)$"));
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Name = Pair.Key.ToString();
			const FAIMessageRow& Row = *reinterpret_cast<const FAIMessageRow*>(Pair.Value);
			const int64 TriggerValue = static_cast<int64>(Row.Trigger);
			if (!Triggers || !Triggers->IsValidEnumValue(TriggerValue) || Triggers->GetNameStringByValue(TriggerValue).EndsWith(TEXT("_MAX")))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: invalid trigger"), File, *Name));
			}
			if (Row.Act < 1 || Row.Act > 4)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: act %d is not 1-4"), File, *Name, Row.Act));
			}
			if (Row.Order < 0 || Row.Order > 999)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: order %d is not 0-999"), File, *Name, Row.Order));
			}
			FRegexMatcher Match(NamePattern, Name);
			if (!Match.FindNext())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not follow A<act>_<Trigger>_<order>"), File, *Name));
			}
			else if (FCString::Atoi(*Match.GetCaptureGroup(1)) != Row.Act || Match.GetCaptureGroup(2) != LexToString(Row.Trigger)
				|| FCString::Atoi(*Match.GetCaptureGroup(3)) != Row.Order)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not match its columns (act %d, trigger %s, order %d)"), File, *Name, Row.Act,
					LexToString(Row.Trigger), Row.Order));
			}

			for (const FName& Column : AIMessageTextColumns)
			{
				const FString Cell = TextCell(Struct, Pair.Value, Column);
				CheckLineBreaks(File, Pair.Key, Column, Cell, Problems);
				for (int32 I = 0; I < Cell.Len(); ++I)
				{
					if (Cell[I] != TEXT('{'))
					{
						continue;
					}
					const int32 Close = Cell.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
					const FString Token = Close == INDEX_NONE ? Cell.Mid(I) : Cell.Mid(I, Close - I + 1);
					bool bKnown = false;
					for (const TCHAR* Allowed : AllowedTokens)
					{
						bKnown |= Token.Equals(Allowed, ESearchCase::CaseSensitive);
					}
					if (!bKnown)
					{
						Problems.Add(FString::Printf(TEXT("%s, row %s, %s: unknown token %s (allowed: {still}, {jump}, {home}, {battery})"), File, *Name, *Column.ToString(), *Token));
					}
					else if (Token == TEXT("{jump}") && Row.Trigger != ETextTrigger::JumpReturn)
					{
						Problems.Add(FString::Printf(TEXT("%s, row %s, %s: {jump} in a %s row ({jump} belongs to JumpReturn rows only)"), File, *Name, *Column.ToString(),
							LexToString(Row.Trigger)));
					}
					I = Close == INDEX_NONE ? Cell.Len() : Close;
				}
			}
			const FString Text = Row.Text.ToString();
			if (Row.Trigger == ETextTrigger::JumpReturn && !Text.StartsWith(TEXT("[PLACEHOLDER]"), ESearchCase::CaseSensitive)
				&& !WordsWithoutTokens(Text).Contains(TEXT("still")))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: a JumpReturn text without the word \"still\" (the {still} token does not count)"), File, *Name));
			}
		}
		return Problems;
	}

	/** Every problem of DT_Endings.csv (empty: the structure is right). */
	static TArray<FString> CheckEndings(const FString& Csv, const TCHAR* File = TEXT("DT_Endings.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FEndingRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		for (const TCHAR* Required : { TEXT("Inject"), TEXT("Open"), TEXT("Loss") })
		{
			if (!Table->GetRowMap().Contains(FName(Required)))
			{
				Problems.Add(FString::Printf(TEXT("%s: no row %s (the rows are Inject, Open and Loss)"), File, Required));
			}
		}
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			CheckLineBreaks(File, Pair.Key, TEXT("Text"), TextCell(Struct, Pair.Value, TEXT("Text")), Problems);
		}
		return Problems;
	}

	/** Every { in Cell, as a problem: the other humanity's writing has no tokens. */
	static void CheckNoTokens(const TCHAR* File, const FName& RowName, const FString& Cell, TArray<FString>& OutProblems)
	{
		const int32 Open = Cell.Find(TEXT("{"));
		if (Open != INDEX_NONE)
		{
			OutProblems.Add(FString::Printf(TEXT("%s, row %s: a token (%s) in a text that takes none"), File, *RowName.ToString(), *Cell.Mid(Open, 12)));
		}
	}

	/** Every problem of DT_Logs.csv (empty: the structure is right): rows L_<order>, orders 1-999 matching their names, unique. */
	static TArray<FString> CheckLogs(const FString& Csv, const TCHAR* File = TEXT("DT_Logs.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FLogRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		if (Table->GetRowMap().IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s has no rows"), File));
		}
		const FRegexPattern NamePattern(TEXT("^L_([0-9]+)$"));
		TSet<int32> Orders;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Name = Pair.Key.ToString();
			const FLogRow& Row = *reinterpret_cast<const FLogRow*>(Pair.Value);
			FRegexMatcher Match(NamePattern, Name);
			if (!Match.FindNext())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not follow L_<order>"), File, *Name));
			}
			else if (FCString::Atoi(*Match.GetCaptureGroup(1)) != Row.Order)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not match its order %d"), File, *Name, Row.Order));
			}
			if (Row.Order < 1 || Row.Order > 999)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: order %d is not 1-999"), File, *Name, Row.Order));
			}
			if (Orders.Contains(Row.Order))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: order %d appears twice"), File, *Name, Row.Order));
			}
			Orders.Add(Row.Order);
			const FString Text = TextCell(Struct, Pair.Value, TEXT("Text"));
			CheckLineBreaks(File, Pair.Key, TEXT("Text"), Text, Problems);
			CheckNoTokens(File, Pair.Key, Text, Problems);
			if (Text.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: empty text"), File, *Name));
			}
		}
		return Problems;
	}

	/** Every problem of DT_TwinAI.csv (empty: the structure is right): rows T_<part>_<order> matching their columns, parts 1..N with none missing. */
	static TArray<FString> CheckTwinAI(const FString& Csv, const TCHAR* File = TEXT("DT_TwinAI.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FTwinAIRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		if (Table->GetRowMap().IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s has no rows"), File));
		}
		const FRegexPattern NamePattern(TEXT("^T_([0-9]+)_([0-9]+)$"));
		TSet<int32> Parts;
		int32 MaxPart = 0;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Name = Pair.Key.ToString();
			const FTwinAIRow& Row = *reinterpret_cast<const FTwinAIRow*>(Pair.Value);
			FRegexMatcher Match(NamePattern, Name);
			if (!Match.FindNext())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not follow T_<part>_<order>"), File, *Name));
			}
			else if (FCString::Atoi(*Match.GetCaptureGroup(1)) != Row.Part || FCString::Atoi(*Match.GetCaptureGroup(2)) != Row.Order)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not match its columns (part %d, order %d)"), File, *Name, Row.Part, Row.Order));
			}
			if (Row.Part < 1 || Row.Part > 99 || Row.Order < 1 || Row.Order > 999)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: part %d or order %d out of range"), File, *Name, Row.Part, Row.Order));
			}
			Parts.Add(Row.Part);
			MaxPart = FMath::Max(MaxPart, Row.Part);
			const FString Text = TextCell(Struct, Pair.Value, TEXT("Text"));
			CheckLineBreaks(File, Pair.Key, TEXT("Text"), Text, Problems);
			CheckNoTokens(File, Pair.Key, Text, Problems);
			if (Text.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: empty text"), File, *Name));
			}
		}
		for (int32 Part = 1; Part <= MaxPart; ++Part)
		{
			if (!Parts.Contains(Part))
			{
				Problems.Add(FString::Printf(TEXT("%s: part %d has no row (parts are played 1 to %d in order)"), File, Part, MaxPart));
			}
		}
		return Problems;
	}

	/**
	 * Every problem of DT_Scenes.csv (empty: the structure is right): it parses, rows <Scene>_<order> matching their Scene
	 * and Order columns, (Scene, Order) unique, every Voice one of Paradise, HomeAI and Engineer, no line break, no empty text.
	 */
	static TArray<FString> CheckScenes(const FString& Csv, const TCHAR* File = TEXT("DT_Scenes.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FSceneRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		if (Table->GetRowMap().IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s has no rows"), File));
		}
		// The Voice cell as written: an unknown name would import as the enum's first value without a word.
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines, true);
		const UEnum* VoiceEnum = StaticEnum<ESceneVoice>();
		for (int32 I = 1; I < Lines.Num(); ++I)
		{
			TArray<FString> Cells;
			Lines[I].ParseIntoArray(Cells, TEXT(","), false);
			if (Cells.Num() < 6 || Cells[0].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const FString Voice = Cells[3].TrimStartAndEnd().TrimQuotes();
			if (VoiceEnum->GetValueByNameString(Voice) == INDEX_NONE)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: voice '%s' is not Paradise, HomeAI or Engineer"), File, *Cells[0], *Voice));
			}
		}
		const FRegexPattern NamePattern(TEXT("^([A-Za-z]+)_([0-9]+)$"));
		TSet<FString> Keys;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Name = Pair.Key.ToString();
			const FSceneRow& Row = *reinterpret_cast<const FSceneRow*>(Pair.Value);
			FRegexMatcher Match(NamePattern, Name);
			if (!Match.FindNext())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not follow <Scene>_<order>"), File, *Name));
			}
			else if (FName(*Match.GetCaptureGroup(1)) != Row.Scene || FCString::Atoi(*Match.GetCaptureGroup(2)) != Row.Order)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not match its columns (scene %s, order %d)"), File, *Name, *Row.Scene.ToString(), Row.Order));
			}
			const FString Key = FString::Printf(TEXT("%s/%d"), *Row.Scene.ToString(), Row.Order);
			if (Keys.Contains(Key))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: scene %s order %d appears twice"), File, *Name, *Row.Scene.ToString(), Row.Order));
			}
			Keys.Add(Key);
			const FString Text = TextCell(Struct, Pair.Value, TEXT("Text"));
			CheckLineBreaks(File, Pair.Key, TEXT("Text"), Text, Problems);
			if (Text.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: empty text"), File, *Name));
			}
		}
		return Problems;
	}

	/**
	 * Every problem of DT_Prologue.csv (empty: the structure is right): it parses, rows <Group>_<order> matching their Group
	 * and Order columns, every Group one of Wake, Hologram, Hangar, Flight, Light, Hatch and Door, (Group, Order) unique, no line
	 * break, no empty text.
	 */
	static TArray<FString> CheckPrologue(const FString& Csv, const TCHAR* File = TEXT("DT_Prologue.csv"))
	{
		TArray<FString> Problems;
		UScriptStruct* Struct = FPrologueRow::StaticStruct();
		const UDataTable* Table = Parse(Csv, Struct, Problems, File);
		if (Table->GetRowMap().IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s has no rows"), File));
		}
		const TSet<FName> Groups = { PrologueGroups::Wake, PrologueGroups::Hologram, PrologueGroups::Hangar, PrologueGroups::Flight, PrologueGroups::Light, PrologueGroups::Hatch, PrologueGroups::Door };
		const FRegexPattern NamePattern(TEXT("^([A-Za-z]+)_([0-9]+)$"));
		TSet<FString> Keys;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Name = Pair.Key.ToString();
			const FPrologueRow& Row = *reinterpret_cast<const FPrologueRow*>(Pair.Value);
			FRegexMatcher Match(NamePattern, Name);
			if (!Match.FindNext())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not follow <Group>_<order>"), File, *Name));
			}
			else if (FName(*Match.GetCaptureGroup(1)) != Row.Group || FCString::Atoi(*Match.GetCaptureGroup(2)) != Row.Order)
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: the name does not match its columns (group %s, order %d)"), File, *Name, *Row.Group.ToString(), Row.Order));
			}
			if (!Groups.Contains(Row.Group))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: group '%s' is not Wake, Hologram, Hangar, Flight, Light, Hatch or Door"), File, *Name, *Row.Group.ToString()));
			}
			const FString Key = FString::Printf(TEXT("%s/%d"), *Row.Group.ToString(), Row.Order);
			if (Keys.Contains(Key))
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: group %s order %d appears twice"), File, *Name, *Row.Group.ToString(), Row.Order));
			}
			Keys.Add(Key);
			const FString Text = TextCell(Struct, Pair.Value, TEXT("Text"));
			CheckLineBreaks(File, Pair.Key, TEXT("Text"), Text, Problems);
			if (Text.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("%s, row %s: empty text"), File, *Name));
			}
		}
		return Problems;
	}

	/** Csv with one more data line, on a line of its own. */
	static FString WithLine(const FString& Csv, const FString& Line)
	{
		return Csv + (Csv.EndsWith(TEXT("\n")) ? TEXT("") : TEXT("\n")) + Line + TEXT("\n");
	}
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelCsvTest, "TheLastShift.TextChannel.Csv", TextChannelTests::Flags)

bool FTextChannelCsvTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	using namespace NarrativeCsv;
	const FString Messages = Load(TEXT("DT_AIMessages.csv"));
	const FString Endings = Load(TEXT("DT_Endings.csv"));
	const FString Logs = Load(TEXT("DT_Logs.csv"));
	const FString Twin = Load(TEXT("DT_TwinAI.csv"));
	TestFalse(TEXT("Narrative/DT_AIMessages.csv is read"), Messages.IsEmpty());
	TestFalse(TEXT("Narrative/DT_Endings.csv is read"), Endings.IsEmpty());
	TestFalse(TEXT("Narrative/DT_Logs.csv is read"), Logs.IsEmpty());
	TestFalse(TEXT("Narrative/DT_TwinAI.csv is read"), Twin.IsEmpty());
	const TArray<FString> LogProblems = CheckLogs(Logs);
	const TArray<FString> TwinProblems = CheckTwinAI(Twin);
	for (const FString& Problem : LogProblems) { AddError(Problem); }
	for (const FString& Problem : TwinProblems) { AddError(Problem); }
	AddInfo(FString::Printf(TEXT("DT_Logs.csv: %d data lines, %d problems; DT_TwinAI.csv: %d data lines, %d problems."), CountDataLines(Logs), LogProblems.Num(),
		CountDataLines(Twin), TwinProblems.Num()));
	TestEqual(TEXT("DT_Logs.csv: structure right (parse, L_<order> names, orders, line breaks, no token)"), LogProblems.Num(), 0);
	TestEqual(TEXT("DT_TwinAI.csv: structure right (parse, T_<part>_<order> names, parts in sequence, line breaks, no token)"), TwinProblems.Num(), 0);
	const TArray<FString> MessageProblems = CheckAIMessages(Messages);
	const TArray<FString> EndingProblems = CheckEndings(Endings);
	for (const FString& Problem : MessageProblems) { AddError(Problem); }
	for (const FString& Problem : EndingProblems) { AddError(Problem); }
	AddInfo(FString::Printf(TEXT("DT_AIMessages.csv: %d data lines, %d problems; DT_Endings.csv: %d data lines, %d problems."), CountDataLines(Messages),
		MessageProblems.Num(), CountDataLines(Endings), EndingProblems.Num()));
	TestEqual(TEXT("DT_AIMessages.csv: structure right (parse, names, triggers, acts, tokens, {jump}, line breaks, still)"), MessageProblems.Num(), 0);
	TestEqual(TEXT("DT_Endings.csv: structure right (parse, Inject, Open, Loss, line breaks)"), EndingProblems.Num(), 0);
	const FString Scenes = Load(TEXT("DT_Scenes.csv"));
	TestFalse(TEXT("Narrative/DT_Scenes.csv is read"), Scenes.IsEmpty());
	const TArray<FString> SceneProblems = CheckScenes(Scenes);
	for (const FString& Problem : SceneProblems) { AddError(Problem); }
	AddInfo(FString::Printf(TEXT("DT_Scenes.csv: %d data lines, %d problems."), CountDataLines(Scenes), SceneProblems.Num()));
	TestEqual(TEXT("DT_Scenes.csv: structure right (parse, <Scene>_<order> names, voices, line breaks)"), SceneProblems.Num(), 0);
	const FString Prologue = Load(TEXT("DT_Prologue.csv"));
	TestFalse(TEXT("Narrative/DT_Prologue.csv is read"), Prologue.IsEmpty());
	const TArray<FString> PrologueProblems = CheckPrologue(Prologue);
	for (const FString& Problem : PrologueProblems) { AddError(Problem); }
	AddInfo(FString::Printf(TEXT("DT_Prologue.csv: %d data lines, %d problems."), CountDataLines(Prologue), PrologueProblems.Num()));
	TestEqual(TEXT("DT_Prologue.csv: structure right (parse, <Group>_<order> names, groups, line breaks)"), PrologueProblems.Num(), 0);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelCsvCheckTest, "TheLastShift.TextChannel.CsvCheck", TextChannelTests::Flags)

bool FTextChannelCsvCheckTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	using namespace NarrativeCsv;
	// Each case adds one bad row to a copy of the committed file, so it holds whatever the file holds.
	const FString Base = Load(TEXT("DT_AIMessages.csv"));
	TestEqual(TEXT("The committed DT_AIMessages.csv has no problem before a bad row is added"), CheckAIMessages(Base).Num(), 0);
	struct FCase { const TCHAR* What; const TCHAR* Line; const TCHAR* Expected; };
	const FCase Cases[] = {
		{ TEXT("an unknown token"), TEXT("A1_Spend_901,Spend,1,901,\"A test line with {bogus} in it.\",,,"), TEXT("unknown token {bogus}") },
		{ TEXT("{jump} in a Spend row"), TEXT("A1_Spend_902,Spend,1,902,\"A test line with {jump} in it.\",,,"), TEXT("{jump} in a Spend row") },
		{ TEXT("a line break in a cell"), TEXT("A1_Spend_903,Spend,1,903,\"A test line,\nbroken in two.\",,,"), TEXT("line break") },
		{ TEXT("a JumpReturn text without \"still\""), TEXT("A1_JumpReturn_904,JumpReturn,1,904,\"A return after {still} with a jump of {jump}.\",,,"),
			TEXT("without the word \"still\"") },
	};
	for (const FCase& Case : Cases)
	{
		const TArray<FString> Problems = CheckAIMessages(WithLine(Base, Case.Line), TEXT("DT_AIMessages.csv (copy)"));
		const FString All = FString::Join(Problems, TEXT(" | "));
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *All));
		TestTrue(FString::Printf(TEXT("%s fails with a clear message (\"%s\" in: %s)"), Case.What, Case.Expected, *All),
			Problems.Num() > 0 && All.Contains(Case.Expected, ESearchCase::CaseSensitive));
	}
	// The two tables of the other humanity's writing.
	const FCase WritingCases[] = {
		{ TEXT("a log row with a bad name"), TEXT("Log_901,901,\"A test line.\""), TEXT("does not follow L_<order>") },
		{ TEXT("a log row with a token"), TEXT("L_902,902,\"A test line with {still} in it.\""), TEXT("a token") },
		{ TEXT("a log row with a line break"), TEXT("L_903,903,\"A test line,\nbroken in two.\""), TEXT("line break") },
	};
	const FString LogBase = Load(TEXT("DT_Logs.csv"));
	TestEqual(TEXT("The committed DT_Logs.csv has no problem before a bad row is added"), CheckLogs(LogBase).Num(), 0);
	for (const FCase& Case : WritingCases)
	{
		const FString All = FString::Join(CheckLogs(WithLine(LogBase, Case.Line), TEXT("DT_Logs.csv (copy)")), TEXT(" | "));
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *All));
		TestTrue(FString::Printf(TEXT("%s fails with a clear message (\"%s\" in: %s)"), Case.What, Case.Expected, *All), All.Contains(Case.Expected, ESearchCase::CaseSensitive));
	}
	const FString TwinBase = Load(TEXT("DT_TwinAI.csv"));
	TestEqual(TEXT("The committed DT_TwinAI.csv has no problem before a bad row is added"), CheckTwinAI(TwinBase).Num(), 0);
	const FCase TwinCases[] = {
		{ TEXT("a twin row whose name does not match its part"), TEXT("T_1_901,2,901,\"A test line.\""), TEXT("does not match its columns") },
		{ TEXT("a twin row with a token"), TEXT("T_1_902,1,902,\"A test line with {home} in it.\""), TEXT("a token") },
		{ TEXT("a twin part with a gap before it"), TEXT("T_9_1,9,1,\"A test line.\""), TEXT("has no row") },
	};
	for (const FCase& Case : TwinCases)
	{
		const FString All = FString::Join(CheckTwinAI(WithLine(TwinBase, Case.Line), TEXT("DT_TwinAI.csv (copy)")), TEXT(" | "));
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *All));
		TestTrue(FString::Printf(TEXT("%s fails with a clear message (\"%s\" in: %s)"), Case.What, Case.Expected, *All), All.Contains(Case.Expected, ESearchCase::CaseSensitive));
	}

	const FString SceneBase = Load(TEXT("DT_Scenes.csv"));
	TestEqual(TEXT("The committed DT_Scenes.csv has no problem before a bad row is added"), CheckScenes(SceneBase).Num(), 0);
	const FCase SceneCases[] = {
		{ TEXT("a scene row with a bad name"), TEXT("Prologue-901,Prologue,901,HomeAI,,\"A test line.\""), TEXT("does not follow <Scene>_<order>") },
		{ TEXT("a scene row whose name does not match its order"), TEXT("Prologue_902,Prologue,903,HomeAI,,\"A test line.\""), TEXT("does not match its columns") },
		{ TEXT("a scene row with an unknown voice"), TEXT("Prologue_904,Prologue,904,Narrator,,\"A test line.\""), TEXT("is not Paradise, HomeAI or Engineer") },
		{ TEXT("a scene row with a line break"), TEXT("Prologue_905,Prologue,905,HomeAI,,\"A test line,\nbroken in two.\""), TEXT("line break") },
	};
	for (const FCase& Case : SceneCases)
	{
		const FString All = FString::Join(CheckScenes(WithLine(SceneBase, Case.Line), TEXT("DT_Scenes.csv (copy)")), TEXT(" | "));
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *All));
		TestTrue(FString::Printf(TEXT("%s fails with a clear message (\"%s\" in: %s)"), Case.What, Case.Expected, *All), All.Contains(Case.Expected, ESearchCase::CaseSensitive));
	}

	const FString NoLoss = Load(TEXT("DT_Endings.csv")).Replace(TEXT("\nLoss,"), TEXT("\nLost,"));
	TestTrue(TEXT("DT_Endings.csv without the Loss row fails"),
		FString::Join(CheckEndings(NoLoss, TEXT("DT_Endings.csv (copy)")), TEXT(" | ")).Contains(TEXT("no row Loss")));
#endif
	return true;
}

// --- The other humanity's writing (Brief 8) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinWritingDealTest, "TheLastShift.TwinWriting.Deal", TextChannelTests::Flags)

bool FTwinWritingDealTest::RunTest(const FString& Parameters)
{
	const auto Join = [](const TArray<int32>& Orders) { return FString::JoinBy(Orders, TEXT(","), [](int32 O) { return FString::FromInt(O); }); };
	const TArray<int32> Eight = { 8, 7, 6, 5, 4, 3, 2, 1 };
	TestEqual(TEXT("8 entries, 2 terminals: the first paid gets 8,7,6,5"), Join(TwinWriting::DealLogGroup(Eight, 2, 0)), FString(TEXT("8,7,6,5")));
	TestEqual(TEXT("8 entries, 2 terminals: the second paid gets 4,3,2,1"), Join(TwinWriting::DealLogGroup(Eight, 2, 1)), FString(TEXT("4,3,2,1")));
	const TArray<int32> Five = { 5, 4, 3, 2, 1 };
	TestEqual(TEXT("5 entries, 2 terminals: 5,4,3"), Join(TwinWriting::DealLogGroup(Five, 2, 0)), FString(TEXT("5,4,3")));
	TestEqual(TEXT("5 entries, 2 terminals: then 2,1"), Join(TwinWriting::DealLogGroup(Five, 2, 1)), FString(TEXT("2,1")));
	TestEqual(TEXT("8 entries, 3 terminals: 3, 3 and 2 entries"), FString::Printf(TEXT("%d %d %d"), TwinWriting::DealLogGroup(Eight, 3, 0).Num(),
		TwinWriting::DealLogGroup(Eight, 3, 1).Num(), TwinWriting::DealLogGroup(Eight, 3, 2).Num()), FString(TEXT("3 3 2")));
	TestEqual(TEXT("Past the last group: nothing"), TwinWriting::DealLogGroup(Eight, 2, 2).Num(), 0);
	TestEqual(TEXT("One terminal gets every entry"), Join(TwinWriting::DealLogGroup(Five, 1, 0)), FString(TEXT("5,4,3,2,1")));

	// The twin voice: barcodes first, the translation 10 behind, never ahead, catching up after the end.
	int32 Shown = 0;
	int32 Translated = 0;
	TwinWriting::GetTwinCounts(60, 4.0, 10, Shown, Translated);
	TestTrue(TEXT("Twin counts at 4 typed: 4 shown, 0 translated"), Shown == 4 && Translated == 0);
	TwinWriting::GetTwinCounts(60, 25.0, 10, Shown, Translated);
	TestTrue(TEXT("Twin counts at 25 typed: 25 shown, 15 translated"), Shown == 25 && Translated == 15);
	TwinWriting::GetTwinCounts(60, 64.0, 10, Shown, Translated);
	TestTrue(TEXT("Twin counts at 64 typed: every barcode shown, 54 translated"), Shown == 60 && Translated == 54);
	TwinWriting::GetTwinCounts(60, 70.0, 10, Shown, Translated);
	TestTrue(TEXT("Twin counts at 70 typed: every cell translated"), Shown == 60 && Translated == 60);
	bool bNeverAhead = true;
	for (double Units = 0.0; Units < 80.0; Units += 0.37)
	{
		TwinWriting::GetTwinCounts(60, Units, 10, Shown, Translated);
		bNeverAhead &= Translated <= Shown && Translated == FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(Units + 1.e-4)) - 10, 0, Shown);
	}
	TestTrue(TEXT("Twin counts: the translation is never ahead of the barcodes"), bNeverAhead);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
