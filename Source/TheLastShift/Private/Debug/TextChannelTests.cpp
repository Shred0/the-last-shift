// SPDX-License-Identifier: MIT

// Automation tests of the text channel that need no world (Brief 6, criteria 2 to 7): TheLastShift.TextChannel.*.
// Run with: Automation RunTests TheLastShift.TextChannel. Development editor builds only.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Debug/TextTestListener.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TextDegradation.h"
#include "TextChannel/TextTokens.h"
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
	Channel->NoArticlesBelow = 0.80f;
	TestEqual(TEXT("NoArticlesBelow 0.80: battery 0.75 is NoArticles"), Channel->GetLevelForBattery(0.75), ETextDegradation::NoArticles);
	Channel->SilentBelow = 0.05f;
	TestEqual(TEXT("SilentBelow 0.05: battery 0.03 is Silent"), Channel->GetLevelForBattery(0.03), ETextDegradation::Silent);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextChannelCsvTest, "TheLastShift.TextChannel.Csv", TextChannelTests::Flags)

bool FTextChannelCsvTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	struct FFile { const TCHAR* Name; UScriptStruct* Struct; TArray<FName> TextColumns; };
	const FFile Files[] = {
		{ TEXT("DT_AIMessages.csv"), FAIMessageRow::StaticStruct(), { TEXT("Text"), TEXT("TextNoArticles"), TEXT("TextAbbreviated"), TEXT("TextScattered") } },
		{ TEXT("DT_Endings.csv"), FEndingRow::StaticStruct(), { TEXT("Text") } },
	};
	for (const FFile& File : Files)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Narrative"), File.Name);
		FString Csv;
		if (!TestTrue(FString::Printf(TEXT("%s is read"), File.Name), FFileHelper::LoadFileToString(Csv, *Path)))
		{
			continue;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = File.Struct;
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		TestEqual(FString::Printf(TEXT("%s parses with zero problems (%s)"), File.Name, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s has rows (%d)"), File.Name, Table->GetRowMap().Num()), Table->GetRowMap().Num() > 0);

		int32 Cells = 0;
		int32 Placeholder = 0;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			for (const FName& Column : File.TextColumns)
			{
				const FTextProperty* Property = CastField<FTextProperty>(File.Struct->FindPropertyByName(Column));
				if (!Property)
				{
					AddError(FString::Printf(TEXT("%s: no text column %s"), File.Name, *Column.ToString()));
					continue;
				}
				const FString Cell = Property->GetPropertyValue_InContainer(Pair.Value).ToString();
				if (!Cell.IsEmpty())
				{
					++Cells;
					Placeholder += Cell.StartsWith(TEXT("[PLACEHOLDER]"), ESearchCase::CaseSensitive) ? 1 : 0;
					if (!Cell.StartsWith(TEXT("[PLACEHOLDER]"), ESearchCase::CaseSensitive))
					{
						AddError(FString::Printf(TEXT("%s, row %s, %s does not start with [PLACEHOLDER]"), File.Name, *Pair.Key.ToString(), *Column.ToString()));
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d rows, %d text cells, %d start with [PLACEHOLDER]."), File.Name, Table->GetRowMap().Num(), Cells, Placeholder));
		TestEqual(FString::Printf(TEXT("%s: every text cell starts with [PLACEHOLDER]"), File.Name), Placeholder, Cells);
	}

	// The AI messages cover what the brief asks for.
	const FString AIPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Narrative"), TEXT("DT_AIMessages.csv"));
	FString Csv;
	FFileHelper::LoadFileToString(Csv, *AIPath);
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = FAIMessageRow::StaticStruct();
	Table->CreateTableFromCSVString(Csv);
	for (int32 Act = 1; Act <= 4; ++Act)
	{
		const TArray<FName> Returns = UTextChannelComponent::FindRows(Table, ETextTrigger::JumpReturn, Act);
		TestEqual(FString::Printf(TEXT("Act %d: two JumpReturn rows"), Act), Returns.Num(), 2);
		for (const FName& Name : Returns)
		{
			const FString Text = Table->FindRow<FAIMessageRow>(Name, TEXT("Test"))->Text.ToString();
			TestTrue(FString::Printf(TEXT("%s uses all four tokens"), *Name.ToString()),
				Text.Contains(TEXT("{still}")) && Text.Contains(TEXT("{jump}")) && Text.Contains(TEXT("{home}")) && Text.Contains(TEXT("{battery}")));
		}
		TestEqual(FString::Printf(TEXT("Act %d: one AreaEntry row"), Act), UTextChannelComponent::FindRows(Table, ETextTrigger::AreaEntry, Act).Num(), 1);
		TestEqual(FString::Printf(TEXT("Act %d: one Spend row"), Act), UTextChannelComponent::FindRows(Table, ETextTrigger::Spend, Act).Num(), 1);
		TestEqual(FString::Printf(TEXT("Act %d: AreaStart rows"), Act), UTextChannelComponent::FindRows(Table, ETextTrigger::AreaStart, Act).Num(), Act == 1 ? 3 : 0);
		TestEqual(FString::Printf(TEXT("Act %d: FirstEnergy rows"), Act), UTextChannelComponent::FindRows(Table, ETextTrigger::FirstEnergy, Act).Num(), Act == 1 ? 1 : 0);
	}
#endif
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
