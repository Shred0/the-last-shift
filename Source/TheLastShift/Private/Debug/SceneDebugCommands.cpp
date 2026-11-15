// SPDX-License-Identifier: MIT

// Brief 9: the run's text scenes. tls.SceneTest (the prologue on "New game": its lines, voices, typing, confirm and back,
// the travel to area 1 after it, when it is shown and when not, the input in area 1 after it), tls.EndingTest local|chain
// (the home AI's last message at three batteries, which endings are offered, each ending's text, the save untouched;
// chain: the whole run to the ending, the loss and the pause menu's Main menu not opening it, every act selectable after
// it), tls.Ending.Open (opens the ending with the run's state now: in the main menu at once, from an area through the
// travel the last area's exit makes) and tls.Scene.LevelCheck (the level of the home AI's text at a battery in the three
// places that degrade it). The tests read their own fixture rows (Debug/NarrativeFixture), never the rows of Narrative/.
// Development builds only.

#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/SceneTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/StdMenuPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TwinWriting.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "UI/StdMainMenuWidget.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace SceneDebug
{
	using TimeStateDebug::ResolveGameWorld;

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static URunSceneSubsystem* Scenes() { return URunSceneSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }
	static UStdScreenWidget* Top() { return TimeStateDebug::TopScreen(W()); }

	static UStdFlowSubsystem* Flow()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	static bool Transitioning() { return Flow() && Flow()->IsTransitioning(); }

	static UStdMainMenuWidget* MainMenu()
	{
		const UWorld* World = W();
		const AStdMenuPlayerController* MenuPC = World ? Cast<AStdMenuPlayerController>(World->GetFirstPlayerController()) : nullptr;
		return MenuPC ? MenuPC->GetMainMenu() : nullptr;
	}

	static bool InMenu() { return MainMenu() != nullptr && !Transitioning(); }
	static bool InArea(int32 InAct) { return W() && Act() == InAct && AAreaGenerator::IsAreaReady(W()) && !Transitioning(); }
	static URunTextSceneWidget* TopScene() { return Cast<URunTextSceneWidget>(Top()); }
	static URunEndingWidget* TopEnding() { return Cast<URunEndingWidget>(Top()); }

	/** A real key through Slate, to the topmost screen (focused first: the console box holds the focus after a command). */
	static bool SendKey(const FKey& Key)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		if (UStdScreenWidget* Screen = Top())
		{
			Screen->FocusDefaultWidget();
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		const bool bHandled = Slate.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		return bHandled;
	}

	/** Escape is the editor's Stop in PIE: the gamepad's right face button stands for it there. */
	static FKey BackKey() { return GIsEditor ? EKeys::Gamepad_FaceButton_Right : EKeys::Escape; }

	static FString SaveHash()
	{
		const FString Slot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav"));
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Slot))
		{
			return TEXT("<no save file>");
		}
		return FString::Printf(TEXT("%s (%d bytes)"), *FMD5::HashBytes(Bytes.GetData(), Bytes.Num()), Bytes.Num());
	}

	/** The input state that decides whether real keys arrive, without the pawn's name. */
	static FString InputSignature()
	{
		const TimeStateDebug::FInputReport R = TimeStateDebug::BuildInputReport(W());
		return FString::Printf(TEXT("verdict=%s controller=%s mode=%s ignores=%d capture=%s viewportFocus=%d mouseCaptured=%d cursor=%d contexts=[%s] pawnContext=%d keys=[%s] unmapped=%d pawnInput=%d canMove=%d area=%d held=%d transitioning=%d paused=%d"),
			R.IsGameReady() ? TEXT("game") : R.IsMenuReady() ? TEXT("menu") : TEXT("none"), *R.ControllerClass, *R.InputMode, R.bViewportIgnoresInput ? 1 : 0,
			*R.MouseCaptureMode, R.bViewportFocused ? 1 : 0, R.bMouseCaptured ? 1 : 0, R.bCursorShown ? 1 : 0, *FString::Join(R.MappingContexts, TEXT(", ")),
			R.bPawnContextApplied ? 1 : 0, *FString::Join(R.PawnActionKeys, TEXT(", ")), R.UnmappedPawnActions, R.bPawnInputEnabled ? 1 : 0, R.bPawnCanMove ? 1 : 0,
			R.bAreaReady ? 1 : 0, R.bHeldByArea ? 1 : 0, R.bTransitioning ? 1 : 0, R.bPausedByMenu ? 1 : 0);
	}

	// --- Step runner (core ticker, survives map changes) ---

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FRun
	{
		FString Name;
		bool bQuitAtEnd = false;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		TStrongObjectPtr<UTlsSceneTestListener> Listener;
		TFunction<void()> Cleanup;
		// Shared scratch of the steps.
		double MarkReal = 0.0;
		FString InputFirst;
		FString HashA;
		int32 LinesSeen = 0;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		FSuitArchive SavedArchive;
		int32 SavedOpenParts = 0;
		float SavedSnapshotBattery = -1.f;
	};
	static FRun GRun;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), *GRun.Name, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), *GRun.Name, *What);
	}

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
		}
		if (GRun.Listener)
		{
			GRun.Listener->Unbind();
		}
		GRun.Listener.Reset();
		NarrativeFixture::Restore(W());
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuitAtEnd)
		{
			FPlatformMisc::RequestExit(false, *GRun.Name);
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < GRun.WaitUntilReal)
		{
			return true;
		}
		if (GRun.Next >= GRun.Steps.Num())
		{
			Finish(TEXT(""));
			return false;
		}
		FStep& Step = GRun.Steps[GRun.Next];
		if (Step.Until)
		{
			if (GRun.ConditionStartReal < 0.0)
			{
				GRun.ConditionStartReal = Now;
			}
			const bool bMet = Step.Until();
			const bool bTimedOut = Now - GRun.ConditionStartReal > Step.Timeout;
			if (!bMet && !bTimedOut)
			{
				return true;
			}
			Check(bMet, FString::Printf(TEXT("%s (waited %.2f s%s)"), *Step.Label, Now - GRun.ConditionStartReal, bTimedOut && !bMet ? TEXT(", TIMEOUT") : TEXT("")));
			GRun.ConditionStartReal = -1.0;
			if (!bMet)
			{
				Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
				return false;
			}
		}
		++GRun.Next;
		const float Delay = Step.Do ? Step.Do() : 0.f;
		if (Delay < 0.f)
		{
			Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
			return false;
		}
		GRun.WaitUntilReal = Now + Delay;
		return true;
	}

	static FStep Do(const FString& Label, TFunction<float()> Fn)
	{
		FStep Step;
		Step.Label = Label;
		Step.Do = MoveTemp(Fn);
		return Step;
	}

	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep Step;
		Step.Label = Label;
		Step.Until = MoveTemp(Until);
		Step.Timeout = Timeout;
		Step.Do = MoveTemp(Then);
		return Step;
	}

	static void Shot(const FString& Name)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief9"), Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot %s"), *Path));
	}

	static void Start(const FString& Name, bool bQuit, TArray<FStep>&& Steps)
	{
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuitAtEnd = bQuit;
		GRun.Steps = MoveTemp(Steps);
		GRun.bRunning = true;
		GRun.Listener.Reset(NewObject<UTlsSceneTestListener>());
		GRun.Listener->Bind(Scenes());
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d steps, in %s."), *Name, GRun.Steps.Num(), *UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	static int32 LineCount() { return GRun.Listener ? GRun.Listener->Lines.Num() : 0; }

	static const UTlsSceneTestListener::FLine* LastLine()
	{
		return GRun.Listener && GRun.Listener->Lines.Num() > 0 ? &GRun.Listener->Lines.Last() : nullptr;
	}

	static FString FixtureSceneText(FName RowName)
	{
		const UDataTable* Table = UTheLastShiftSettings::Get()->LoadScenesTable();
		const FSceneRow* Row = Table ? Table->FindRow<FSceneRow>(RowName, TEXT("tls"), false) : nullptr;
		return Row ? Row->Text.ToString() : FString();
	}

	static bool Near(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 0.002f); }

	// --- tls.SceneTest ---

	/** Checks the line on screen: its index, voice colours, the listener's report and its cue. */
	static void CheckLine(int32 Index, ESceneVoice Voice, FName Cue)
	{
		URunTextSceneWidget* Scene = TopScene();
		FLinearColor Text, Back;
		UTheLastShiftSettings::Get()->GetSceneVoiceColors(Voice, Text, Back);
		const UTlsSceneTestListener::FLine* Last = LastLine();
		Check(Scene && Scene->GetCurrentIndex() == Index, FString::Printf(TEXT("line %d on screen (%d)"), Index + 1, Scene ? Scene->GetCurrentIndex() + 1 : -1));
		Check(Last && Last->Scene == URunSceneSubsystem::PrologueScene() && Last->Index == Index && Last->Voice == Voice,
			FString::Printf(TEXT("line %d reported as it starts: scene %s, index %d, voice %s"), Index + 1, Last ? *Last->Scene.ToString() : TEXT("-"),
				Last ? Last->Index + 1 : -1, Last ? LexToString(Last->Voice) : TEXT("-")));
		Check(Last && Last->Cue == Cue, FString::Printf(TEXT("line %d carries cue %s (reported %s)"), Index + 1, *Cue.ToString(), Last ? *Last->Cue.ToString() : TEXT("-")));
		Check(Scene && Near(Scene->GetShownTextColor(), Text) && Near(Scene->GetShownBackgroundColor(), Back),
			FString::Printf(TEXT("line %d in the %s colours: text %s on %s"), Index + 1, LexToString(Voice), Scene ? *Scene->GetShownTextColor().ToString() : TEXT("-"),
				Scene ? *Scene->GetShownBackgroundColor().ToString() : TEXT("-")));
	}

	static void AddBackToMenu(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": back to the main menu"), []() { return Run()->ReturnToMainMenu() ? 0.5f : -1.f; }));
		S.Add(WaitFor(Label + TEXT(": in the main menu"), []() { return InMenu(); }, 60.f, []() { return 0.5f; }));
	}

	static TArray<FStep> BuildSceneTest()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("setup: the main menu is shown and no transition runs"), []() { return InMenu(); }, 30.f, []()
		{
			NarrativeFixture::Install(W());
			Run()->ClearSave();
			Note(FString::Printf(TEXT("fixture installed, save cleared; scenes table %s"), *GetNameSafe(UTheLastShiftSettings::Get()->LoadScenesTable())));
			return 0.5f;
		}));

		// --- A: without a save, read through with the confirm key ---
		S.Add(Do(TEXT("A: New game without a save"), []()
		{
			Check(!Run()->HasSavedRun(), TEXT("A: no saved run"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), TEXT("A: New game clicked"));
			URunTextSceneWidget* Scene = TopScene();
			Check(Scene && Scene->GetKind() == ERunSceneKind::Prologue && Scene->GetEntryCount() == 6,
				FString::Printf(TEXT("A: the prologue opens over the main menu with the 6 Prologue rows (%s, %d lines)"), *GetNameSafe(Top()), Scene ? Scene->GetEntryCount() : 0));
			Check(!Transitioning(), TEXT("A: no travel while the prologue is shown"));
			Check(MainMenu() && !MainMenu()->IsVisible(), TEXT("A: the main menu is hidden behind it"));
			if (Scene)
			{
				FString Order;
				for (const FRunSceneLine& Line : Scene->GetLines())
				{
					Order += FString::Printf(TEXT("%s%s(%s)"), Order.IsEmpty() ? TEXT("") : TEXT(" "), *Line.RowName.ToString(), LexToString(Line.Voice));
				}
				Check(Order == TEXT("Prologue_1(Paradise) Prologue_2(Paradise) Prologue_3(HomeAI) Prologue_4(HomeAI) Prologue_5(HomeAI) Prologue_6(HomeAI)"),
					FString::Printf(TEXT("A: rows in order with their voices: %s"), *Order));
				Check(FMath::IsNearlyEqual(Scene->CharsPerSecond, UTheLastShiftSettings::Get()->GetSceneCharsPerSecond()),
					FString::Printf(TEXT("A: typed at SceneCharsPerSecond %.1f"), Scene->CharsPerSecond));
			}
			CheckLine(0, ESceneVoice::Paradise, NAME_None);
			GRun.MarkReal = LastLine() ? LastLine()->RealTime : FPlatformTime::Seconds();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: line 1 typed out"), []() { return TopScene() && !TopScene()->IsTyping(); }, 10.f, []()
		{
			URunTextSceneWidget* Scene = TopScene();
			const float Took = static_cast<float>(FPlatformTime::Seconds() - GRun.MarkReal);
			const int32 Len = Scene->GetLaidOutText().Len();
			const float Expected = Len / UTheLastShiftSettings::Get()->GetSceneCharsPerSecond();
			Check(Took >= Expected - 0.05f && Took <= Expected + 0.3f, FString::Printf(TEXT("A: %d characters typed in %.2f s (%.2f s at %.0f per second)"), Len, Took, Expected,
				UTheLastShiftSettings::Get()->GetSceneCharsPerSecond()));
			Check(Scene->GetVisibleText().Replace(TEXT("\n"), TEXT(" ")) == FixtureSceneText(TEXT("Prologue_1")), TEXT("A: line 1 shown whole, as written"));
			Shot(TEXT("Prologue_1_Paradise"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: confirm on a whole line shows the next"), []()
		{
			Check(SendKey(EKeys::Enter), TEXT("A: Enter handled by the prologue"));
			CheckLine(1, ESceneVoice::Paradise, NAME_None);
			URunTextSceneWidget* Scene = TopScene();
			Check(Scene && Scene->IsTyping() && Scene->GetVisibleText().Len() < 5, TEXT("A: line 2 starts typing"));
			Check(SendKey(EKeys::Enter), TEXT("A: Enter on line 2 while it is typed"));
			Check(Scene && Scene->GetCurrentIndex() == 1 && !Scene->IsTyping() && Scene->GetVisibleText().Replace(TEXT("\n"), TEXT(" ")) == FixtureSceneText(TEXT("Prologue_2")),
				TEXT("A: confirm on a line being typed shows it whole and stays on it"));
			Check(LineCount() == 2, FString::Printf(TEXT("A: no line started by that confirm (%d reported)"), LineCount()));
			const FLinearColor White = Scene ? Scene->GetShownBackgroundColor() : FLinearColor::Black;
			Check(SendKey(EKeys::Enter), TEXT("A: Enter on line 2 whole"));
			CheckLine(2, ESceneVoice::HomeAI, NAME_None);
			Check(Scene && Near(White, UTheLastShiftSettings::GetTextColors().ParadiseBackground) && Near(Scene->GetShownBackgroundColor(), UTheLastShiftSettings::GetTextColors().SceneBackground),
				FString::Printf(TEXT("A: the cut from white to black between line 2 and line 3 (%s -> %s)"), *White.ToString(), Scene ? *Scene->GetShownBackgroundColor().ToString() : TEXT("-")));
			Check(Scene && Scene->GetLineCount() >= 2 && Scene->GetBlockSize().X <= Scene->ColumnWidth + 0.5f,
				FString::Printf(TEXT("A: the three-sentence line wraps inside the column: %d lines, %.0f of %.0f wide"), Scene ? Scene->GetLineCount() : 0,
					Scene ? Scene->GetBlockSize().X : 0.f, Scene ? Scene->ColumnWidth : 0.f));
			Check(Scene && Scene->GetLines().IsValidIndex(2) && Scene->GetLines()[2].Level == ETextDegradation::Full
				&& Scene->GetLaidOutText().Replace(TEXT("\n"), TEXT(" ")) == FixtureSceneText(TEXT("Prologue_3")),
				TEXT("A: a HomeAI line of the prologue is shown in full (a new run's battery is full)"));
			GRun.MarkReal = FPlatformTime::Seconds();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: line 3 half typed"), []()
		{
			const URunTextSceneWidget* Scene = TopScene();
			return Scene && Scene->GetVisibleText().Len() >= Scene->GetLaidOutText().Len() / 2;
		}, 10.f, []()
		{
			const URunTextSceneWidget* Scene = TopScene();
			Check(Scene && Scene->IsTyping(), FString::Printf(TEXT("A: line 3 still being typed: %d of %d characters"), Scene ? Scene->GetVisibleText().Len() : 0,
				Scene ? Scene->GetLaidOutText().Len() : 0));
			Shot(TEXT("Prologue_3_HomeAI_HalfTyped"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: lines 4 to 6"), []()
		{
			SendKey(EKeys::Enter);
			SendKey(EKeys::Enter);
			CheckLine(3, ESceneVoice::HomeAI, TEXT("FixtureCue"));
			SendKey(EKeys::Enter);
			SendKey(EKeys::Enter);
			CheckLine(4, ESceneVoice::HomeAI, NAME_None);
			SendKey(EKeys::SpaceBar);
			SendKey(EKeys::SpaceBar);
			CheckLine(5, ESceneVoice::HomeAI, NAME_None);
			bool bCuesOk = true;
			for (const UTlsSceneTestListener::FLine& Line : GRun.Listener->Lines)
			{
				bCuesOk &= Line.Index == 3 ? Line.Cue == TEXT("FixtureCue") : Line.Cue.IsNone();
			}
			Check(bCuesOk, TEXT("A: only the row with a cue name carries it; the others carry none"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the last line typed out"), []() { return TopScene() && !TopScene()->IsTyping(); }, 10.f, []()
		{
			Check(TopScene()->GetVisibleText().Replace(TEXT("\n"), TEXT(" ")) == FixtureSceneText(TEXT("Prologue_6")), TEXT("A: the last line shown whole"));
			Shot(TEXT("Prologue_6_LastLine"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: confirm after the last line"), []()
		{
			URunTextSceneWidget* Scene = TopScene();
			SendKey(EKeys::Enter);
			Check(Scene && Scene->IsFinished() && !Scene->WasSkipped(), TEXT("A: the prologue ends, read through"));
			Check(Transitioning(), TEXT("A: the travel to area 1 starts"));
			const FString Label = Run()->GetLastTravelLabel().ToString();
			Check(Label.Contains(TEXT("Fixture entry line of act 1")), FString::Printf(TEXT("A: the travel screen carries the area entry line: \"%s\""), *Label));
			Check(LineCount() == 6, FString::Printf(TEXT("A: six lines reported (%d)"), LineCount()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			GRun.InputFirst = InputSignature();
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(TimeStateDebug::BuildInputReport(W()).IsGameReady(), TEXT("A: game input in area 1 after the prologue read through"));
			Check(Home()->GetBatteryFraction() == 1.0 && Run()->GetSnapshots().Num() == 1, TEXT("A: a new run: battery full, one snapshot (act 1)"));
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("A"));

		// --- B: with a save, skipped with the back key ---
		S.Add(Do(TEXT("B: New game with a save"), []()
		{
			Check(Run()->HasSavedRun(), TEXT("B: a saved run exists"));
			GRun.LinesSeen = LineCount();
			TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false);
			URunTextSceneWidget* Scene = TopScene();
			Check(Scene && Scene->GetKind() == ERunSceneKind::Prologue && Scene->GetCurrentIndex() == 0, TEXT("B: the prologue opens again, from line 1"));
			SendKey(EKeys::Enter);
			SendKey(EKeys::Enter);
			SendKey(EKeys::Enter);
			SendKey(EKeys::Enter);
			Check(Scene && Scene->GetCurrentIndex() == 2, TEXT("B: on line 3"));
			const int32 Before = LineCount();
			Check(SendKey(BackKey()), FString::Printf(TEXT("B: back (%s) handled"), *BackKey().ToString()));
			Check(Scene && Scene->IsFinished() && Scene->WasSkipped(), TEXT("B: back skips the whole prologue"));
			Check(Transitioning(), TEXT("B: the travel to area 1 starts at once"));
			Check(LineCount() == Before, TEXT("B: no line started after the skip"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			const FString Now = InputSignature();
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(Now == GRun.InputFirst, FString::Printf(TEXT("B: input in area 1 after a skip is the same as after the prologue read through: %s"), *Now));
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("B"));

		// --- C: Continue and Select act never show it ---
		S.Add(Do(TEXT("C: Continue"), []()
		{
			GRun.LinesSeen = LineCount();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("C: Continue clicked"));
			Check(!TopScene() && Transitioning() && LineCount() == GRun.LinesSeen, TEXT("C: Continue travels at once, no prologue"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("C: arrived in area 1 (Continue)"), []() { return InArea(1); }, 90.f, []() { return 0.5f; }));
		AddBackToMenu(S, TEXT("C"));
		S.Add(Do(TEXT("C: Select act, act 1"), []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("C: Select act clicked"));
			Check(Cast<URunActListWidget>(Top()) != nullptr, FString::Printf(TEXT("C: the act list (%s)"), *TimeStateDebug::DescribeChoiceList(W())));
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), TEXT("C: act 1 chosen"));
			Check(!TopScene() && Transitioning() && LineCount() == GRun.LinesSeen, TEXT("C: Select act travels at once, no prologue"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("C: arrived in area 1 (Select act)"), []() { return InArea(1); }, 90.f, []() { return 0.5f; }));
		AddBackToMenu(S, TEXT("C"));

		// --- D: no Prologue row ---
		S.Add(Do(TEXT("D: New game without Prologue rows"), []()
		{
			NarrativeFixture::SetPrologueRows(false);
			TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false);
			Check(!TopScene() && Transitioning() && LineCount() == GRun.LinesSeen, TEXT("D: New game goes straight to the travel to area 1"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("D: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			const FString Now = InputSignature();
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(Now == GRun.InputFirst, FString::Printf(TEXT("D: input in area 1 without a prologue (as New game before Brief 9) is the same as after it: %s"), *Now));
			return 0.f;
		}));
		return S;
	}

	// --- tls.EndingTest ---

	static void SetBattery(double Battery)
	{
		Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
	}

	static void SetTwinParts(int32 Parts)
	{
		Run()->ClearArchive();
		for (int32 Part = 1; Part <= Parts; ++Part)
		{
			Run()->StoreTwinPart(Part);
		}
	}

	static URunEndingWidget* OpenEndingNow()
	{
		if (URunEndingWidget* Old = TopEnding())
		{
			Old->CloseScreen();
		}
		return Scenes()->OpenEnding(MainMenu());
	}

	static bool BodyCollapsed(const URunEndingWidget* Ending)
	{
		const UWidget* Body = Ending && Ending->WidgetTree ? Ending->WidgetTree->FindWidget(TEXT("Text_Body")) : nullptr;
		return Body && Body->GetVisibility() == ESlateVisibility::Collapsed;
	}

	static void AddMessageCase(TArray<FStep>& S, double Battery, ETextDegradation Expected, const FString& ShotName)
	{
		S.Add(Do(FString::Printf(TEXT("message at battery %.2f"), Battery), [Battery, Expected, ShotName]()
		{
			SetBattery(Battery);
			SetTwinParts(0);
			URunEndingWidget* Ending = OpenEndingNow();
			const FString Full = NarrativeFixture::GetEndingMessageText();
			FTextChannelContext Context = Scenes()->MakeRunContext();
			FAIMessageRow Row;
			Row.Text = FText::FromString(Full);
			const FString Want = UTextChannelComponent::ComposeMessage(Row, Expected, Context);
			const FString Shown = Ending ? Ending->GetBody().ToString() : TEXT("<no ending>");
			Note(FString::Printf(TEXT("battery %.2f: level %s, text shown: \"%s\""), Battery, Ending ? LexToString(Ending->GetMessageLevel()) : TEXT("-"), *Shown));
			Check(Ending && Ending->GetMessageLevel() == Expected, FString::Printf(TEXT("battery %.2f: the last message is at %s"), Battery, LexToString(Expected)));
			Check(Ending && Shown == Want, FString::Printf(TEXT("battery %.2f: the text is the fixture row at that level (\"%s\")"), Battery, *Want));
			if (Expected == ETextDegradation::Full)
			{
				Check(Shown == Full, TEXT("battery 1.00: shown in full"));
			}
			else if (Expected == ETextDegradation::Silent)
			{
				Check(Shown.IsEmpty() && BodyCollapsed(Ending), TEXT("battery 0.02: nothing above the list"));
			}
			else
			{
				Check(!Shown.IsEmpty() && Shown != Full, TEXT("battery 0.35: shorter than the full text"));
			}
			Check(Ending && Ending->GetEntryCount() == 1 && Ending->GetEntryLabel(0).ToString() == TEXT("Inject the harvest"),
				FString::Printf(TEXT("battery %.2f, no twin part: one entry (%s)"), Battery, *TimeStateDebug::DescribeChoiceList(W())));
			Check(Scenes()->IsEndingPending() == false, TEXT("opening it here leaves no ending pending"));
			return 0.5f;
		}));
		S.Add(Do(FString::Printf(TEXT("message at battery %.2f: screenshot"), Battery), [ShotName]()
		{
			Shot(ShotName);
			return 0.6f;
		}));
	}

	static void AddPartsCase(TArray<FStep>& S, int32 Parts, int32 Setting, int32 ExpectedEntries, const FString& ShotName)
	{
		S.Add(Do(FString::Printf(TEXT("%d twin parts, OpenEndingTwinParts %d"), Parts, Setting), [Parts, Setting, ExpectedEntries, ShotName]()
		{
			GetMutableDefault<UTheLastShiftSettings>()->OpenEndingTwinParts = Setting;
			SetBattery(1.0);
			SetTwinParts(Parts);
			URunEndingWidget* Ending = OpenEndingNow();
			const FString List = TimeStateDebug::DescribeChoiceList(W());
			const FString Want = ExpectedEntries == 1 ? TEXT("Inject the harvest") : TEXT("Inject the harvest | Open the structure");
			Check(Ending && Ending->GetEntryCount() == ExpectedEntries && List == Want, FString::Printf(TEXT("%d of %d required parts (setting %d): entries \"%s\""), Parts,
				UTheLastShiftSettings::Get()->GetOpenEndingTwinParts(), Setting, *List));
			return ShotName.IsEmpty() ? 0.1f : 0.5f;
		}));
		if (!ShotName.IsEmpty())
		{
			S.Add(Do(TEXT("screenshot"), [ShotName]()
			{
				Shot(ShotName);
				return 0.6f;
			}));
		}
	}

	static void AddEndingTextCase(TArray<FStep>& S, int32 Entry, FName Ending, bool bSkip, const FString& ShotName)
	{
		S.Add(Do(FString::Printf(TEXT("ending %s: chosen"), *Ending.ToString()), [Entry, Ending]()
		{
			GetMutableDefault<UTheLastShiftSettings>()->OpenEndingTwinParts = 0;
			SetBattery(0.02);
			SetTwinParts(TwinWriting::GetTwinPartCount(UTheLastShiftSettings::Get()->LoadTwinAITable()));
			URunEndingWidget* Choice = OpenEndingNow();
			Check(Choice && Choice->GetEndingOfEntry(Entry) == Ending, FString::Printf(TEXT("entry %d is %s"), Entry, *Ending.ToString()));
			Check(TimeStateDebug::ClickChoiceEntry(W(), Entry), FString::Printf(TEXT("entry %d chosen"), Entry));
			URunTextSceneWidget* Text = TopScene();
			FLinearColor Want, Back;
			UTheLastShiftSettings::Get()->GetSceneVoiceColors(ESceneVoice::Engineer, Want, Back);
			const FString Full = UTheLastShiftSettings::Get()->GetEndingText(Ending).ToString();
			Check(Text && Text->GetKind() == ERunSceneKind::EndingText && Text->GetEntryCount() == 1, FString::Printf(TEXT("%s: its text opens on top (%s)"), *Ending.ToString(), *GetNameSafe(Top())));
			Check(Text && Text->GetLaidOutText().Replace(TEXT("\n"), TEXT(" ")) == Full && !Full.IsEmpty(),
				FString::Printf(TEXT("%s: the DT_Endings row in full at battery 0.02: \"%s\""), *Ending.ToString(), *Full));
			Check(Text && Text->GetLines()[0].Voice == ESceneVoice::Engineer && Near(Text->GetShownTextColor(), Want) && Near(Text->GetShownBackgroundColor(), Back),
				FString::Printf(TEXT("%s: in the Engineer colour %s on %s"), *Ending.ToString(), *Want.ToString(), *Back.ToString()));
			const UTlsSceneTestListener::FLine* Last = LastLine();
			Check(Last && Last->Scene == Ending && Last->Voice == ESceneVoice::Engineer, TEXT("the ending's line is reported as it starts"));
			return 0.f;
		}));
		S.Add(WaitFor(FString::Printf(TEXT("ending %s: typed out"), *Ending.ToString()), []() { return TopScene() && !TopScene()->IsTyping(); }, 15.f, [ShotName]()
		{
			Shot(ShotName);
			return 0.6f;
		}));
		S.Add(Do(FString::Printf(TEXT("ending %s: after the text"), *Ending.ToString()), [bSkip, Ending]()
		{
			Check(SendKey(bSkip ? BackKey() : EKeys::Enter), FString::Printf(TEXT("%s: %s handled"), *Ending.ToString(), bSkip ? *BackKey().ToString() : TEXT("Enter")));
			UStdMainMenuWidget* Menu = MainMenu();
			Check(Menu && Top() == Menu && Menu->IsVisible(), FString::Printf(TEXT("%s: after its text, the main menu (%s)"), *Ending.ToString(), *GetNameSafe(Top())));
			Check(TimeStateDebug::BuildInputReport(W()).IsMenuReady(), FString::Printf(TEXT("%s: menu input on the main menu"), *Ending.ToString()));
			return 0.3f;
		}));
	}

	static TArray<FStep> BuildEndingLocal()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("setup: the main menu is shown and no transition runs"), []() { return InMenu(); }, 30.f, []()
		{
			NarrativeFixture::Install(W());
			GRun.SavedBattery = Home()->GetBatteryFraction();
			GRun.SavedHome = Home()->GetHomeSecondsElapsed();
			GRun.SavedArchive = Run()->GetArchive();
			GRun.SavedOpenParts = UTheLastShiftSettings::Get()->OpenEndingTwinParts;
			GRun.HashA = SaveHash();
			GRun.Cleanup = []()
			{
				GetMutableDefault<UTheLastShiftSettings>()->OpenEndingTwinParts = GRun.SavedOpenParts;
				if (URunEndingWidget* Ending = TopEnding())
				{
					Ending->CloseScreen();
				}
				if (Home())
				{
					Home()->RestoreState(GRun.SavedBattery, GRun.SavedHome);
				}
				if (Run())
				{
					SetTwinParts(GRun.SavedArchive.TwinPartsPlayed);
					Run()->StoreLogEntries(GRun.SavedArchive.LogOrders);
				}
			};
			Note(FString::Printf(TEXT("fixture installed; run state kept to put back: battery %.3f, archive %d logs, %d twin parts; save %s"), GRun.SavedBattery,
				GRun.SavedArchive.LogOrders.Num(), GRun.SavedArchive.TwinPartsPlayed, *GRun.HashA));
			return 0.3f;
		}));
		AddMessageCase(S, 1.0, ETextDegradation::Full, TEXT("Ending_Message_Battery100_OneEntry"));
		AddMessageCase(S, 0.35, ETextDegradation::Abbreviated, TEXT("Ending_Message_Battery035"));
		AddMessageCase(S, 0.02, ETextDegradation::Silent, TEXT("Ending_Message_Battery002"));
		const int32 Parts = 3;
		AddPartsCase(S, 0, 0, 1, FString());
		AddPartsCase(S, Parts - 1, 0, 1, FString());
		AddPartsCase(S, Parts, 0, 2, TEXT("Ending_Choice_TwoEntries"));
		AddPartsCase(S, 1, 1, 2, FString());
		AddPartsCase(S, 0, 1, 1, FString());
		AddEndingTextCase(S, 0, TEXT("Inject"), false, TEXT("Ending_Text_Inject"));
		AddEndingTextCase(S, 1, TEXT("Open"), true, TEXT("Ending_Text_Open"));
		S.Add(Do(TEXT("state: the save after the endings"), []()
		{
			const FString Now = SaveHash();
			Check(Now == GRun.HashA, FString::Printf(TEXT("the save on disk is the same before and after the endings: %s"), *Now));
			return 0.f;
		}));
		return S;
	}

	/** The travel line of the last travel, as Brief 6 to 8 built it without a pawn: the text channel's class defaults (0.70, 0.40, 0.10, 0.03). */
	static FString TravelLineBefore(int32 Act, double Battery, double HomeSeconds)
	{
		const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
		const TArray<FName> Rows = UTextChannelComponent::FindRows(Table, ETextTrigger::AreaEntry, Act);
		const FAIMessageRow* Row = Rows.Num() > 0 ? Table->FindRow<FAIMessageRow>(Rows[0], TEXT("tls"), false) : nullptr;
		if (!Row)
		{
			return FString();
		}
		FTextChannelContext Context;
		Context.BatteryFraction = Battery;
		Context.HomeSecondsElapsed = HomeSeconds;
		const ETextDegradation Level = UTextDegradationLibrary::LevelFromBattery(Battery, 0.70f, 0.40f, 0.10f, 0.03f);
		return UTextChannelComponent::ComposeMessage(*Row, Level, Context);
	}

	static TArray<FStep> BuildTravelLineTest()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("setup: the main menu is shown and no transition runs"), []() { return InMenu(); }, 30.f, []()
		{
			Run()->ClearSave();
			Check(TimeStateDebug::ClickMainMenuPlay(W()), TEXT("New game (the prologue skipped)"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("arrived in area 1"), []() { return InArea(1); }, 120.f, []()
		{
			Home()->RestoreState(0.35, Home()->GetHomeSecondsElapsed());
			return Run()->TravelToNextArea() ? 0.5f : -1.f;
		}));
		S.Add(WaitFor(TEXT("arrived in area 2 with battery 0.35"), []() { return InArea(2); }, 120.f, []()
		{
			Check(Run()->GetSnapshots().Num() == 2 && FMath::IsNearlyEqual(Run()->GetSnapshots()[1].BatteryFraction, 0.35, 1.e-6),
				TEXT("act 2's entry snapshot has battery 0.35"));
			return Run()->ReturnToMainMenu() ? 0.5f : -1.f;
		}));
		S.Add(WaitFor(TEXT("in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Check(!W()->GetFirstPlayerController()->GetPawn() || !W()->GetFirstPlayerController()->GetPawn()->FindComponentByClass<UTextChannelComponent>(),
				TEXT("no text channel in the main menu (no pawn with one)"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("Continue (into act 2, entry battery 0.35)"));
			const FString After = Run()->GetLastTravelLabel().ToString();
			const FString Before = TravelLineBefore(2, 0.35, Home()->GetHomeSecondsElapsed());
			const ETextDegradation Level = UTheLastShiftSettings::Get()->GetTextLevel(0.35);
			Note(FString::Printf(TEXT("travel line now (settings, %s): \"%s\""), LexToString(Level), *After));
			Note(FString::Printf(TEXT("travel line before Brief 9 (the channel's class defaults, %s): \"%s\""),
				LexToString(UTextDegradationLibrary::LevelFromBattery(0.35, 0.70f, 0.40f, 0.10f, 0.03f)), *Before));
			Check(Level == UTheLastShiftSettings::Get()->GetTextLevel(0.35) && GetDefault<UTextChannelComponent>()->GetLevelForBattery(0.35) == Level,
				FString::Printf(TEXT("the travel line is at the level a message has at 0.35 in an area (%s)"), LexToString(Level)));
			FTextChannelContext Context;
			Context.BatteryFraction = 0.35;
			Context.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
			const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
			const TArray<FName> Rows = UTextChannelComponent::FindRows(Table, ETextTrigger::AreaEntry, 2);
			const FAIMessageRow* Row = Rows.Num() > 0 ? Table->FindRow<FAIMessageRow>(Rows[0], TEXT("tls"), false) : nullptr;
			const FString Want = Row ? UTextChannelComponent::ComposeMessage(*Row, Level, Context) : FString();
			Check(After == Want, TEXT("the travel line is act 2's AreaEntry row at that level, tokens filled"));
			Check(After == Before, TEXT("the same text as before Brief 9 (the settings hold the channel's old defaults)"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("arrived in area 2 (Continue)"), []() { return InArea(2); }, 120.f, []() { return 0.f; }));
		return S;
	}

	static void AddTravelNext(TArray<FStep>& S, int32 To)
	{
		S.Add(Do(FString::Printf(TEXT("chain: travel to area %d"), To), []() { return Run()->TravelToNextArea() ? 0.5f : -1.f; }));
		S.Add(WaitFor(FString::Printf(TEXT("chain: arrived in area %d"), To), [To]() { return InArea(To); }, 120.f, [To]()
		{
			Check(!Scenes()->IsEndingPending(), FString::Printf(TEXT("chain: the exit into area %d opens no ending"), To));
			return 0.5f;
		}));
	}

	static TArray<FStep> BuildEndingChain()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("setup: the main menu is shown and no transition runs"), []() { return InMenu(); }, 30.f, []()
		{
			NarrativeFixture::Install(W());
			Run()->ClearSave();
			Check(TimeStateDebug::ClickMainMenuPlay(W()), TEXT("chain: New game (the prologue skipped)"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: arrived in area 1"), []() { return InArea(1); }, 120.f, []() { return 0.5f; }));
		AddTravelNext(S, 2);
		AddTravelNext(S, 3);
		AddTravelNext(S, 4);
		S.Add(Do(TEXT("chain: the pause menu's Main menu from area 4"), []()
		{
			// What the pause menu's Main menu entry does: StdCore's return to the main menu.
			return Flow()->ReturnToMainMenu() ? 0.5f : -1.f;
		}));
		S.Add(WaitFor(TEXT("chain: in the main menu (pause menu's Main menu)"), []() { return InMenu(); }, 60.f, []()
		{
			Check(Top() == MainMenu() && !Scenes()->IsEndingPending(), FString::Printf(TEXT("chain: the pause menu's Main menu opens no ending (%s)"), *GetNameSafe(Top())));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("chain: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: back in area 4"), []() { return InArea(4); }, 120.f, []()
		{
			// A loss in area 4: the loss screen, as before, and no ending.
			Home()->RestoreState(0.01, Home()->GetHomeSecondsElapsed());
			Home()->ApplyJump(EHomeJumpCause::Contact, 4, 1.f);
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: the loss screen in area 4"), []() { return Run()->GetLossScreen() != nullptr && Run()->GetLossScreen()->IsInViewport(); }, 10.f, []()
		{
			Check(!Scenes()->IsEndingPending(), TEXT("chain: a loss opens no ending"));
			Note(FString::Printf(TEXT("loss screen entries: %s"), *TimeStateDebug::DescribeChoiceList(W())));
			const int32 MainMenuEntry = Run()->GetLossScreen()->GetEntryCount() - 1;
			Check(TimeStateDebug::ClickChoiceEntry(W(), MainMenuEntry), TEXT("chain: the loss screen's Main menu"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: in the main menu after the loss"), []() { return InMenu(); }, 60.f, []()
		{
			Check(Top() == MainMenu(), FString::Printf(TEXT("chain: after a loss the main menu, no ending (%s)"), *GetNameSafe(Top())));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("chain: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: in area 4 again"), []() { return InArea(4); }, 120.f, []()
		{
			GRun.HashA = SaveHash();
			GRun.SavedSnapshotBattery = Run()->GetSnapshots().Num() == 4 ? static_cast<float>(Run()->GetSnapshots()[3].BatteryFraction) : -1.f;
			Check(Run()->GetSnapshots().Num() == 4, FString::Printf(TEXT("chain: four snapshots (%d), save %s"), Run()->GetSnapshots().Num(), *GRun.HashA));
			// The run leaves area 4 with another battery than its entry snapshot's: the ending reads the run now, Select act restores the snapshot.
			Home()->RestoreState(0.5, Home()->GetHomeSecondsElapsed());
			return Run()->TravelToNextArea() ? 0.5f : -1.f;
		}));
		S.Add(WaitFor(TEXT("chain: the exit of area 4 arrives in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			URunEndingWidget* Ending = TopEnding();
			Check(Ending != nullptr, FString::Printf(TEXT("chain: the exit of area 4 opens the ending over the main menu (%s: %s)"), *GetNameSafe(Top()),
				*TimeStateDebug::DescribeChoiceList(W())));
			Check(MainMenu() && !MainMenu()->IsVisible(), TEXT("chain: the main menu is hidden behind it"));
			Check(Ending && Ending->GetMessageLevel() == ETextDegradation::NoArticles && !Ending->GetBody().IsEmpty(),
				FString::Printf(TEXT("chain: the last message at the run's battery now (0.50): %s \"%s\""), Ending ? LexToString(Ending->GetMessageLevel()) : TEXT("-"),
					Ending ? *Ending->GetBody().ToString() : TEXT("")));
			Check(SaveHash() == GRun.HashA, TEXT("chain: the save is unchanged by the travel to the ending"));
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), TEXT("chain: Inject the harvest"));
			if (URunTextSceneWidget* Text = TopScene())
			{
				Text->Confirm();
				Text->Confirm();
			}
			Check(Top() == MainMenu() && MainMenu()->IsVisible(), TEXT("chain: after the ending's text, the main menu"));
			Check(SaveHash() == GRun.HashA, FString::Printf(TEXT("chain: the save on disk is the same before and after the ending: %s"), *SaveHash()));
			const FString Menu = TimeStateDebug::DescribeMainMenu(W());
			Check(Menu.StartsWith(TEXT("Continue | Select act | New game")), FString::Printf(TEXT("chain: the main menu offers Continue and Select act (%s)"), *Menu));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("chain: Select act"));
			const FString Acts = TimeStateDebug::DescribeChoiceList(W());
			Check(Cast<URunActListWidget>(Top()) && Cast<URunActListWidget>(Top())->GetActOfEntry(3) == 4 && Acts.Contains(TEXT("Area 4")),
				FString::Printf(TEXT("chain: all four acts selectable (%s)"), *Acts));
			Check(TimeStateDebug::ClickChoiceEntry(W(), 3), TEXT("chain: the fourth act chosen"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: area 4 from Select act"), []() { return InArea(4); }, 120.f, []()
		{
			Check(FMath::IsNearlyEqual(static_cast<float>(Home()->GetBatteryFraction()), GRun.SavedSnapshotBattery, 1.e-5f),
				FString::Printf(TEXT("chain: area 4 starts with its entry snapshot (battery %.4f, snapshot %.4f)"), Home()->GetBatteryFraction(), GRun.SavedSnapshotBattery));
			return 0.f;
		}));
		return S;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsSceneTestCommand(
	TEXT("tls.SceneTest"),
	TEXT("tls.SceneTest [Quit=0]: (fresh PIE or package, MainMenu; clears the save) with fixture rows: New game without a save opens the prologue's six lines, read through with Enter and Space (typing speed, confirm on a line being typed and on a whole one, each line's voice colours, the cut from white to black, the long line's wrap, the cue of line 4, three screenshots to Saved/Screenshots/Brief9), then the travel to area 1 with its entry line; New game with a save, skipped with back on line 3; Continue and Select act without it; New game without Prologue rows; the input in area 1 the same each time."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (SceneDebug::GRun.bRunning || !SceneDebug::MainMenu())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.SceneTest: needs the main menu, or a test is already running."));
			return;
		}
		SceneDebug::Start(TEXT("tls.SceneTest"), Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, SceneDebug::BuildSceneTest());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEndingTestCommand(
	TEXT("tls.EndingTest"),
	TEXT("tls.EndingTest [local|chain] [Quit=0]: (fresh PIE or package, MainMenu) with fixture rows. local: the home AI's last message at battery 1.0, 0.35 and 0.02; the entries with 0, all but one and all twin AI parts, and with OpenEndingTwinParts 1; each ending's text in the Engineer colour, in full, then the main menu; the save untouched; screenshots to Saved/Screenshots/Brief9; the run's state is put back. chain (clears the save): New game, areas 1 to 4, the pause menu's Main menu and a loss in area 4 open no ending, the exit of area 4 does; the save the same before and after; Select act offers all four acts and the fourth starts with its entry snapshot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (SceneDebug::GRun.bRunning || !SceneDebug::MainMenu())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EndingTest: needs the main menu, or a test is already running."));
			return;
		}
		const bool bChain = Args.Num() > 0 && Args[0].Equals(TEXT("chain"), ESearchCase::IgnoreCase);
		const bool bQuit = Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0;
		SceneDebug::Start(bChain ? TEXT("tls.EndingTest chain") : TEXT("tls.EndingTest local"), bQuit, bChain ? SceneDebug::BuildEndingChain() : SceneDebug::BuildEndingLocal());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTravelLineTestCommand(
	TEXT("tls.TravelLineTest"),
	TEXT("tls.TravelLineTest [Quit=0]: (fresh PIE or package, MainMenu; clears the save) with the game's own AreaEntry rows: New game, battery 0.35 in area 1, travel to area 2 (its entry snapshot at 0.35), back to the main menu, Continue: the travel line is the act 2 row at the level of a message at 0.35, and the same text as the line built before Brief 9."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (SceneDebug::GRun.bRunning || !SceneDebug::MainMenu())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.TravelLineTest: needs the main menu, or a test is already running."));
			return;
		}
		SceneDebug::Start(TEXT("tls.TravelLineTest"), Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, SceneDebug::BuildTravelLineTest());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEndingOpenCommand(
	TEXT("tls.Ending.Open"),
	TEXT("tls.Ending.Open: opens the ending with the run's state now (battery, home time, the suit's archive). In the main menu at once; from an area through the travel to the main menu that the last area's exit makes (Normal time only). The save is not touched."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(World);
		URunSubsystem* Run = URunSubsystem::Get(World);
		if (!World || !Scenes || !Run)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Ending.Open: no game world."));
			return;
		}
		const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(World->GetFirstPlayerController());
		if (UStdMainMenuWidget* Menu = MenuPC ? MenuPC->GetMainMenu() : nullptr)
		{
			const URunEndingWidget* Ending = Scenes->OpenEnding(Menu);
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Ending.Open: %s (battery %.3f, %d twin parts)."), Ending ? TEXT("the ending is open over the main menu") : TEXT("could not open the ending"),
				Scenes->MakeRunContext().BatteryFraction, Run->GetArchive().TwinPartsPlayed);
			return;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Ending.Open: from act %d: %s."), Run->GetActIndex(), Run->TravelToEnding() ? TEXT("travelling to the ending") : TEXT("refused"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsSceneLevelCheckCommand(
	TEXT("tls.Scene.LevelCheck"),
	TEXT("tls.Scene.LevelCheck [Battery=0.35] [Act=1]: sets the home battery to Battery for a moment and logs the level of the home AI's text there in the three places that degrade it: a message of the text channel (the pawn's, if any), the travel screen's line to Act, and the ending's last message; then puts the battery back."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(World);
		URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(World);
		if (!World || !Home || !Scenes)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Scene.LevelCheck: no game world."));
			return;
		}
		const double Battery = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 0.35;
		const int32 Act = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1;
		const double OldBattery = Home->GetBatteryFraction();
		Home->RestoreState(Battery, Home->GetHomeSecondsElapsed());
		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UTextChannelComponent* Channel = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
		const ETextDegradation ChannelLevel = (Channel ? Channel : GetDefault<UTextChannelComponent>())->GetLevelForBattery(Battery);
		FText Label;
		float Reading = 0.f;
		UHomeAIComponent::BuildTravelLabel(World, Act, Label, Reading);
		ETextDegradation EndingLevel = ETextDegradation::Full;
		const FText Message = Scenes->BuildEndingMessage(&EndingLevel);
		const FTextDegradationTuning& T = UTheLastShiftSettings::Get()->TextDegradation;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Scene.LevelCheck: battery %.3f, thresholds %.3f / %.3f / %.3f / %.3f: channel message %s (%s), travel line to act %d at %s \"%s\", ending message %s \"%s\"."),
			Battery, T.NoArticlesBelow, T.AbbreviatedBelow, T.ScatteredBelow, T.SilentBelow, LexToString(ChannelLevel), Channel ? TEXT("the pawn's channel") : TEXT("no pawn: the channel's class"),
			Act, LexToString(UTheLastShiftSettings::Get()->GetTextLevel(Battery)), *Label.ToString(), LexToString(EndingLevel), *Message.ToString());
		Home->RestoreState(OldBattery, Home->GetHomeSecondsElapsed());
	}));

#endif // !UE_BUILD_SHIPPING
