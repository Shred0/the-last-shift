// SPDX-License-Identifier: MIT

// Brief 17, the flashlight on arrival. tls.ArrivalHintTest [Quit] (start in TestLidar in PIE, or in MainMenu in a
// package): the hint is never shown in the map it starts in; Prologue_Arrival with no Light rows: the hint appears as the
// player steps out of the ship, shows the key bound to Flashlight, follows a rebinding, goes when the flashlight is
// switched on and does not come back; the time from stepping out to the exit door opening; Prologue_Arrival with the
// fixture's Light rows: the Light lines from the step out, the hint when the last one is typed out (not before, hidden
// while Stopped), then the Hatch and Door lines and the exit door, and the same time, longer by the Light lines; the hint
// never shown in area 1 and in Prologue_Home. tls.Hint.Report logs the hint. The lines are the test's fixture rows.
// Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TestLidarCharacter.h"
#include "Debug/TextTestListener.h"
#include "Energy/DoorDevice.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Input/StdInputSubsystem.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueHatch.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/FlashlightHintWidget.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace ArrivalHintDebug
{
	using TimeStateDebug::ResolveGameWorld;

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FRun
	{
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		bool bQuit = false;
		bool bWalking = false;
		FVector WalkTarget = FVector::ZeroVector;
		FTSTicker::FDelegateHandle Ticker;

		TStrongObjectPtr<UTlsTextTestListener> Text;
		TArray<FStdKeyBindingRow> SavedBindings;
		FKey FreeKey;
		double DoorOpenedRun = -1.0;
		double BeforeSeconds = -1.0;
		double AfterSeconds = -1.0;
		double HintShownRun = -1.0;
		int32 StartedMark = 0;
	};
	static FRun GRun;

	static const TCHAR* Name() { return TEXT("tls.ArrivalHintTest"); }

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name(), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name(), *What);
	}

	static double Real() { return FPlatformTime::Seconds(); }

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

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static ATestLidarCharacter* Char() { return Cast<ATestLidarCharacter>(Pawn()); }
	static UFlashlightHintWidget* Hint() { const ATestLidarCharacter* C = Char(); return C ? C->GetFlashlightHint() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UTextChannelComponent* Channel() { return Pawn() ? Pawn()->FindComponentByClass<UTextChannelComponent>() : nullptr; }
	static FString MapName() { const UWorld* World = W(); return World ? World->GetOutermost()->GetName() : FString(); }
	static double RunNow() { const UTimeStateSubsystem* T = TS(); return T ? T->GetRunSeconds() : -1.0; }

	static APrologueHatch* Hatch()
	{
		UWorld* World = W();
		for (TActorIterator<APrologueHatch> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static bool Transitioning()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return Flow && Flow->IsTransitioning();
	}

	static bool TravelTo(const TSoftObjectPtr<UWorld>& Level)
	{
		const UWorld* World = W();
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return Flow && Flow->TravelToLevelWithOptions(Level, FStdTravelOptions());
	}

	static FString HintText()
	{
		const UFlashlightHintWidget* H = Hint();
		if (!H)
		{
			return TEXT("no hint widget");
		}
		const TCHAR* State = H->GetHintState() == EFlashlightHintState::Idle ? TEXT("Idle") : H->GetHintState() == EFlashlightHintState::Active ? TEXT("Active") : TEXT("Done");
		return FString::Printf(TEXT("hint %s, %s \"%s\""), State, H->IsShown() ? TEXT("shown") : TEXT("hidden"), *H->GetShownText());
	}

	static FString StartedRows(int32 Mark)
	{
		FString Out;
		const UTlsTextTestListener* L = GRun.Text.Get();
		for (int32 I = Mark; L && I < L->Started.Num(); ++I)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + L->Started[I].ToString();
		}
		return Out;
	}

	// --- Runner ---

	static void Finish(const TCHAR* Why)
	{
		GRun.bRunning = false;
		GRun.bWalking = false;
		if (UWorld* World = W())
		{
			if (GRun.SavedBindings.Num() > 0)
			{
				TimeStateDebug::RestoreBindings(World, GRun.SavedBindings);
			}
			NarrativeFixture::Restore(World);
		}
		if (GRun.Text)
		{
			GRun.Text->Unbind();
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), Name(),
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, Name());
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		if (GRun.bWalking)
		{
			if (APawn* P = Pawn())
			{
				FVector Dir = GRun.WalkTarget - P->GetActorLocation();
				Dir.Z = 0.f;
				P->AddMovementInput(Dir.GetSafeNormal(), 1.f);
			}
		}
		if (const APrologueHatch* H = Hatch(); H && GRun.DoorOpenedRun < 0.0 && H->GetHatchState() == EPrologueHatchState::DoorOpened)
		{
			GRun.DoorOpenedRun = RunNow();
		}
		if (const UFlashlightHintWidget* H = Hint(); H && H->IsShown() && GRun.HintShownRun < 0.0)
		{
			GRun.HintShownRun = RunNow();
		}
		const double Now = Real();
		if (Now < GRun.WaitUntilReal)
		{
			return true;
		}
		if (GRun.Next >= GRun.Steps.Num())
		{
			Finish(TEXT(""));
			return false;
		}
		const FStep Step = GRun.Steps[GRun.Next];
		if (Step.Until)
		{
			if (GRun.ConditionStartReal < 0.0)
			{
				GRun.ConditionStartReal = Now;
			}
			const bool bMet = W() && Step.Until();
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
		GRun.WaitUntilReal = Real() + Delay;
		return true;
	}

	// --- Steps ---

	/** Nothing in this map shows the hint: idle and hidden for two seconds. */
	static void AddNeverShown(TArray<FStep>& S, const FString& Where)
	{
		S.Add(Do(Where + TEXT(": wait"), []() { return 2.f; }));
		S.Add(Do(Where + TEXT(": the hint"), [Where]()
		{
			const UFlashlightHintWidget* H = Hint();
			Check(H && H->GetHintState() == EFlashlightHintState::Idle && !H->IsShown() && H->GetFirstShownRealSeconds() < 0.0 && !Hatch(),
				FString::Printf(TEXT("%s (%s): no arrival hatch, the hint never shown (%s)"), *Where, *MapName(), *HintText()));
			return 0.f;
		}));
	}

	/** Travels to the arrival with the fixture's prologue rows less Without, then walks out of the ship. */
	static void AddArrival(TArray<FStep>& S, const FString& Label, TArray<FName> Without)
	{
		S.Add(Do(Label + TEXT(": to Prologue_Arrival"), [Label, Without]()
		{
			NarrativeFixture::SetPrologueGroups(Without);
			Check(TravelTo(UTheLastShiftSettings::Get()->PrologueArrivalMap), Label + TEXT(": the travel starts"));
			return 1.f;
		}));
		S.Add(WaitFor(Label + TEXT(": in Prologue_Arrival"), []() { return !Transitioning() && Hatch() && Char() && Hint() && Channel() && Hatch()->IsInsideShip(Pawn()); }, 90.f, [Label]()
		{
			GRun.Text.Reset(NewObject<UTlsTextTestListener>());
			GRun.Text->Bind(Channel());
			GRun.DoorOpenedRun = GRun.HintShownRun = -1.0;
			const UFlashlightHintWidget* H = Hint();
			Check(H->GetHintState() == EFlashlightHintState::Idle && !H->IsShown() && !Char()->IsFlashlightOn() && Char()->IsFlashlightAvailable(),
				FString::Printf(TEXT("%s: in the ship, the flashlight available and off, no hint (%s)"), *Label, *HintText()));
			return 1.f;
		}));
		S.Add(Do(Label + TEXT(": still in the ship"), [Label]()
		{
			Check(Hatch()->GetHatchState() == EPrologueHatchState::Open && !Hint()->IsShown(), FString::Printf(TEXT("%s: after 1 s in the ship, no hint"), *Label));
			const FBox Ship = Hatch()->GetShipBox();
			GRun.WalkTarget = FVector(Ship.Max.X + 300.f, Ship.GetCenter().Y, 0.f);
			GRun.bWalking = true;
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": out of the ship"), []() { return Hatch()->GetHatchState() != EPrologueHatchState::Open; }, 20.f, []()
		{
			GRun.bWalking = false;
			return 0.f;
		}));
	}

	static void AddDoorOpened(TArray<FStep>& S, const FString& Label, double* OutSeconds)
	{
		S.Add(WaitFor(Label + TEXT(": the exit door opens"), []() { return GRun.DoorOpenedRun >= 0.0; }, 90.f, [Label, OutSeconds]()
		{
			const APrologueHatch* H = Hatch();
			*OutSeconds = GRun.DoorOpenedRun - H->GetSteppedOutRunSeconds();
			Check(H->ExitDoor && H->ExitDoor->IsOpen(), FString::Printf(TEXT("%s: the exit door is open, %.2f s of the run's clock after stepping out (hatch closing %.2f s, delay %.2f s)"), *Label,
				*OutSeconds, UTheLastShiftSettings::Get()->GetHatchCloseSeconds(), UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds()));
			Note(FString::Printf(TEXT("%s: lines %s"), *Label, *StartedRows(0)));
			return 0.f;
		}));
	}

	static void StartTest(bool bQuit)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.bQuit = bQuit;
		TArray<FStep>& S = GRun.Steps;

		S.Add(WaitFor(TEXT("setup: a game world"), []() { return W() && !Transitioning(); }, 30.f, []()
		{
			UWorld* World = W();
			Note(FString::Printf(TEXT("started in %s"), *MapName()));
			NarrativeFixture::Install(World);
			GRun.SavedBindings = TimeStateDebug::SaveBindings(World);
			return 0.2f;
		}));
		// The map the test starts in (TestLidar in PIE): no hint there.
		S.Add(Do(TEXT("start map"), []()
		{
			if (!Char())
			{
				Note(FString::Printf(TEXT("%s has no player pawn: nothing to check there"), *MapName()));
				return 0.f;
			}
			return 2.f;
		}));
		S.Add(Do(TEXT("start map: the hint"), []()
		{
			if (Char())
			{
				const UFlashlightHintWidget* H = Hint();
				Check(H && H->GetHintState() == EFlashlightHintState::Idle && !H->IsShown() && !Hatch(), FString::Printf(TEXT("%s: the hint never shown (%s)"), *MapName(), *HintText()));
			}
			return 0.f;
		}));

		// --- Before: no Light rows ---
		AddArrival(S, TEXT("no Light rows"), { PrologueGroups::Light });
		S.Add(WaitFor(TEXT("no Light rows: the hint at the step out"), []() { const UFlashlightHintWidget* H = Hint(); return H && H->IsShown(); }, 1.f, []()
		{
			const FKey Bound = TimeStateDebug::BoundKey(W(), TEXT("FlashlightAction"));
			Check(Bound.IsValid() && Hint()->GetShownKey() == Bound && Hatch()->AreLightLinesDone() && FMath::Abs(Hatch()->GetLightDoneRunSeconds() - Hatch()->GetSteppedOutRunSeconds()) < 0.05f,
				FString::Printf(TEXT("no Light rows: the hint as the player steps out, with the key bound to Flashlight (%s; bound %s)"), *HintText(), *Bound.ToString()));
			return 0.f;
		}));
		AddDoorOpened(S, TEXT("no Light rows"), &GRun.BeforeSeconds);
		S.Add(Do(TEXT("no Light rows: lines and hint at the door"), []()
		{
			const FString Rows = StartedRows(0);
			Check(Rows == TEXT("Hatch_1 Hatch_2 Door_1 Door_2"), FString::Printf(TEXT("no Light rows: the Hatch and Door lines as before (%s)"), *Rows));
			Check(Hint()->IsShown() && Hint()->GetHintState() == EFlashlightHintState::Active, FString::Printf(TEXT("no Light rows: the flashlight off, the hint stays (%s)"), *HintText()));
			// A rebinding: the hint follows it at once.
			GRun.FreeKey = TimeStateDebug::FreeKey(W(), { TimeStateDebug::BoundKey(W(), TEXT("FlashlightAction")) });
			FText Message;
			UStdInputSubsystem* Input = TimeStateDebug::StdInputOf(W());
			const EStdRemapResult Result = Input ? Input->RemapKey(TEXT("Flashlight"), EPlayerMappableKeySlot::First, GRun.FreeKey, Message) : EStdRemapResult::Rejected;
			Note(FString::Printf(TEXT("Flashlight rebound to %s: result %d, \"%s\""), *GRun.FreeKey.ToString(), static_cast<int32>(Result), *Message.ToString()));
			return 0.5f;
		}));
		S.Add(Do(TEXT("rebinding: the hint's key"), []()
		{
			const FKey Bound = TimeStateDebug::BoundKey(W(), TEXT("FlashlightAction"));
			Check(GRun.FreeKey.IsValid() && Bound == GRun.FreeKey && Hint()->GetShownKey() == GRun.FreeKey,
				FString::Printf(TEXT("rebinding: the hint shows the new key (%s; bound %s)"), *HintText(), *Bound.ToString()));
			Check(TimeStateDebug::PressKey(W(), GRun.FreeKey), TEXT("rebinding: the new key pressed"));
			return 0.4f;
		}));
		S.Add(Do(TEXT("the flashlight on: the hint goes"), []()
		{
			Check(Char()->IsFlashlightOn() && Hint()->GetHintState() == EFlashlightHintState::Done && !Hint()->IsShown(),
				FString::Printf(TEXT("the flashlight on: the hint gone (%s)"), *HintText()));
			Check(TimeStateDebug::PressKey(W(), GRun.FreeKey), TEXT("the flashlight key again: off"));
			return 2.f;
		}));
		S.Add(Do(TEXT("the hint does not come back"), []()
		{
			Check(!Char()->IsFlashlightOn() && Hint()->GetHintState() == EFlashlightHintState::Done && !Hint()->IsShown(),
				FString::Printf(TEXT("the flashlight off again, 2 s later: the hint does not come back (%s)"), *HintText()));
			TimeStateDebug::RestoreBindings(W(), GRun.SavedBindings);
			return 0.f;
		}));

		// --- After: the fixture's Light rows ---
		AddArrival(S, TEXT("Light rows"), {});
		S.Add(Do(TEXT("Light rows: the step out"), []()
		{
			const UTextChannelComponent* C = Channel();
			Check(C && (C->GetCurrentRowName() == TEXT("Light_1") || StartedRows(0).StartsWith(TEXT("Light_1"))) && !Hint()->IsShown(),
				FString::Printf(TEXT("Light rows: stepping out starts the Light lines, no hint yet (now %s; %s)"), C ? *C->GetCurrentRowName().ToString() : TEXT("-"), *HintText()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("Light rows: the hint"), []() { const UFlashlightHintWidget* H = Hint(); return H && H->IsShown(); }, 40.f, []()
		{
			const FString Rows = StartedRows(0);
			const UTextChannelComponent* C = Channel();
			const FKey Bound = TimeStateDebug::BoundKey(W(), TEXT("FlashlightAction"));
			// The hatch asks for the Hatch lines in the same frame, so the channel may already hold them: Light_2 shows its last character.
			Check(Rows == TEXT("Light_1 Light_2") && C && C->GetCurrentRowName() == TEXT("Light_2") && C->GetShownCells() >= C->GetCurrentText().Len(),
				FString::Printf(TEXT("Light rows: the hint when Light_2 is typed out, before any Hatch line (%s; now %s, %d of %d characters)"), *Rows,
					C ? *C->GetCurrentRowName().ToString() : TEXT("-"), C ? C->GetShownCells() : -1, C ? C->GetCurrentText().Len() : -1));
			Check(Hint()->GetShownKey() == Bound, FString::Printf(TEXT("Light rows: the key bound to Flashlight (%s)"), *HintText()));
			Note(FString::Printf(TEXT("Light rows: the hint %.2f s of the run's clock after stepping out"), GRun.HintShownRun - Hatch()->GetSteppedOutRunSeconds()));
			return 0.f;
		}));
		AddDoorOpened(S, TEXT("Light rows"), &GRun.AfterSeconds);
		S.Add(Do(TEXT("Light rows: lines and hint at the door"), []()
		{
			const FString Rows = StartedRows(0);
			Check(Rows == TEXT("Light_1 Light_2 Hatch_1 Hatch_2 Door_1 Door_2"), FString::Printf(TEXT("Light rows: Light, then Hatch and Door lines, once each (%s)"), *Rows));
			Check(Hint()->IsShown(), FString::Printf(TEXT("Light rows: the flashlight still off, the hint stays (%s)"), *HintText()));
			Note(FString::Printf(TEXT("stepping out -> exit door: %.2f s without Light rows, %.2f s with the fixture's two Light rows (+%.2f s)"),
				GRun.BeforeSeconds, GRun.AfterSeconds, GRun.AfterSeconds - GRun.BeforeSeconds));
			Check(GRun.AfterSeconds > GRun.BeforeSeconds, TEXT("the wait in the hangar grows with the Light lines"));
			Check(TimeStateDebug::PressKey(W(), TimeStateDebug::BoundKey(W(), TEXT("TimeStopAction"))), TEXT("Light rows: Stop"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("Light rows: Stopped"), []()
		{
			Check(TS()->IsStopped() && !Hint()->IsShown() && Hint()->GetHintState() == EFlashlightHintState::Active,
				FString::Printf(TEXT("Light rows: hidden while Stopped, still active (%s)"), *HintText()));
			Check(TimeStateDebug::PressKey(W(), TimeStateDebug::BoundKey(W(), TEXT("TimeStopAction"))), TEXT("Light rows: Resume"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("Light rows: Resumed"), []()
		{
			Check(!TS()->IsStopped() && Hint()->IsShown(), FString::Printf(TEXT("Light rows: shown again in Normal time (%s)"), *HintText()));
			Check(TimeStateDebug::PressKey(W(), TimeStateDebug::BoundKey(W(), TEXT("FlashlightAction"))), TEXT("Light rows: the flashlight key"));
			return 0.4f;
		}));
		S.Add(Do(TEXT("Light rows: the flashlight on"), []()
		{
			Check(Char()->IsFlashlightOn() && Hint()->GetHintState() == EFlashlightHintState::Done && !Hint()->IsShown(), FString::Printf(TEXT("Light rows: on, the hint gone (%s)"), *HintText()));
			Check(TravelTo(UTheLastShiftSettings::Get()->AreaMaps[0]), TEXT("to area 1"));
			return 1.f;
		}));

		// --- Never elsewhere ---
		S.Add(WaitFor(TEXT("area 1 ready"), []()
		{
			const AAreaGenerator* G = AAreaGenerator::Find(W());
			return !Transitioning() && G && G->IsReady() && Char() && Hint();
		}, 90.f));
		AddNeverShown(S, TEXT("area 1"));
		S.Add(Do(TEXT("to Prologue_Home"), []()
		{
			Check(TravelTo(UTheLastShiftSettings::Get()->PrologueHomeMap), TEXT("to Prologue_Home"));
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("in Prologue_Home"), []() { return !Transitioning() && Char() && Hint() && URunSubsystem::GetPrologueStageOf(W()) == 1; }, 90.f));
		AddNeverShown(S, TEXT("Prologue_Home"));

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started (%d steps)."), Name(), GRun.Steps.Num());
	}

	static FAutoConsoleCommandWithWorldAndArgs GArrivalHintTestCommand(
		TEXT("tls.ArrivalHintTest"),
		TEXT("tls.ArrivalHintTest [Quit]: Brief 17, the arrival's Light lines and the flashlight hint (start in TestLidar in PIE, MainMenu in a package)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}));

	static FAutoConsoleCommandWithWorldAndArgs GHintReportCommand(
		TEXT("tls.Hint.Report"),
		TEXT("tls.Hint.Report: logs the flashlight hint (state, shown, key) and the arrival hatch's Light lines."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* InWorld)
		{
			UWorld* World = ResolveGameWorld(InWorld);
			const APlayerController* C = World ? World->GetFirstPlayerController() : nullptr;
			const ATestLidarCharacter* Character = C ? Cast<ATestLidarCharacter>(C->GetPawn()) : nullptr;
			const UFlashlightHintWidget* H = Character ? Character->GetFlashlightHint() : nullptr;
			const APrologueHatch* Found = nullptr;
			for (TActorIterator<APrologueHatch> It(World); World && It; ++It)
			{
				Found = *It;
				break;
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Hint.Report: %s: flashlight %s; hint %s, %s, key %s, \"%s\"; hatch %s%s."), World ? *World->GetOutermost()->GetName() : TEXT("-"),
				Character && Character->IsFlashlightOn() ? TEXT("on") : TEXT("off"),
				!H ? TEXT("none") : H->GetHintState() == EFlashlightHintState::Idle ? TEXT("Idle") : H->GetHintState() == EFlashlightHintState::Active ? TEXT("Active") : TEXT("Done"),
				H && H->IsShown() ? TEXT("shown") : TEXT("hidden"), H ? *H->GetShownKey().ToString() : TEXT("-"), H ? *H->GetShownText() : TEXT(""),
				*GetNameSafe(Found), Found ? *FString::Printf(TEXT(" (Light lines %s, stepped out at %.2f s, done at %.2f s)"), Found->AreLightLinesDone() ? TEXT("done") : TEXT("not done"),
					Found->GetSteppedOutRunSeconds(), Found->GetLightDoneRunSeconds()) : TEXT(""));
		}));
}

#endif // !UE_BUILD_SHIPPING
