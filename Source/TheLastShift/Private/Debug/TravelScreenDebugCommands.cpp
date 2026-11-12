// SPDX-License-Identifier: MIT

// tls.TravelScreenTest (Brief 6b): the travel screen's progress bar and walk marker on every kind of travel the run makes.
// It samples StdCore's flow every frame on the core ticker (so it survives map changes): bar and marker never going back,
// both at their end when the screen uncovers, the longest interval without a frame, and the run subsystem's record of
// each real step of an area build. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Flow/StdTransitionProgress.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Settings/StdProjectSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

#if !UE_BUILD_SHIPPING

namespace TravelScreenDebug
{
	using TimeStateDebug::ResolveGameWorld;

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }

	static UStdFlowSubsystem* Flow()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	// --- Frame sampler: what the travel screen showed, frame after frame ---

	struct FWatch
	{
		bool bArmed = false;
		bool bSawTransition = false;
		double LastFrame = -1.0;
		float MaxFrameGap = 0.f;
		EStdFlowState MaxGapState = EStdFlowState::Idle;
		/** Seconds since the travel started (Arm) at the end of the longest gap, and the gap around the map load. */
		float MaxGapAt = 0.f;
		float LoadGap = 0.f;
		double ArmTime = 0.0;
		bool bLoadSeen = false;
		float LastBar = 0.f;
		float LastMarker = 0.f;
		bool bBarBack = false;
		bool bMarkerBack = false;
		bool bBarShown = false;
		bool bMarkerShown = false;
		bool bBarShownWhileCovered = false;
		float BarAtFadeIn = -1.f;
		float MarkerAtFadeIn = -1.f;
		float MarkerBeforeFadeIn = -1.f;
		float CoveredBeforeFadeIn = -1.f;
		float LastFrameDelta = 0.f;
		int32 Frames = 0;
	};

	// --- Step runner (core ticker) ---

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
		FString ShotTag;
		bool bQuitAtEnd = false;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		TWeakObjectPtr<UWorld> TravelFrom;
		FWatch Watch;
		TArray<FString> Kinds;
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
		GRun.bRunning = false;
		for (const FString& Kind : GRun.Kinds)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   kind  %s"), *GRun.Name, *Kind);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuitAtEnd)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.TravelScreenTest"));
		}
	}

	static void SampleFrame()
	{
		FWatch& Wt = GRun.Watch;
		const double Now = FPlatformTime::Seconds();
		const UStdFlowSubsystem* F = Flow();
		if (!Wt.bArmed || !F)
		{
			Wt.LastFrame = Now;
			return;
		}
		const float Gap = Wt.LastFrame > 0.0 ? static_cast<float>(Now - Wt.LastFrame) : 0.f;
		Wt.LastFrame = Now;
		if (!F->IsTransitioning())
		{
			return;
		}
		Wt.bSawTransition = true;
		++Wt.Frames;
		if (Gap > Wt.MaxFrameGap)
		{
			Wt.MaxFrameGap = Gap;
			Wt.MaxGapState = F->GetState();
			Wt.MaxGapAt = static_cast<float>(Now - Wt.ArmTime);
		}
		// The first frame after the new map has arrived ends the interval that holds the blocking load.
		if (!Wt.bLoadSeen && F->GetState() == EStdFlowState::WaitingToFadeIn)
		{
			Wt.bLoadSeen = true;
			Wt.LoadGap = Gap;
		}
		const float Bar = F->GetTransitionProgress();
		const float Marker = F->GetTransitionMarkerFraction();
		Wt.bBarBack |= Bar + KINDA_SMALL_NUMBER < Wt.LastBar;
		Wt.bMarkerBack |= Marker + KINDA_SMALL_NUMBER < Wt.LastMarker;
		Wt.LastBar = Bar;
		Wt.LastMarker = Marker;
		Wt.bBarShown |= F->IsTransitionProgressBarShown();
		Wt.bMarkerShown |= F->IsTransitionMarkerShown();
		Wt.bBarShownWhileCovered |= F->IsTransitionProgressBarShown() && F->GetOverlayAlpha() >= 1.f;
		if (F->GetState() == EStdFlowState::WaitingToFadeIn)
		{
			Wt.MarkerBeforeFadeIn = Marker;
			Wt.CoveredBeforeFadeIn = F->GetCoveredSeconds();
			Wt.LastFrameDelta = Gap;
		}
		if (F->GetState() == EStdFlowState::FadingIn && Wt.BarAtFadeIn < 0.f)
		{
			Wt.BarAtFadeIn = Bar;
			Wt.MarkerAtFadeIn = Marker;
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		SampleFrame();
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
		const FString File = GRun.ShotTag.IsEmpty() ? Name : FString::Printf(TEXT("%s_%s"), *Name, *GRun.ShotTag);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief6b"), File + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot %s"), *Path));
	}

	// --- The covered screen's widgets ---

	static TSharedPtr<SWidget> FindByType(const TSharedRef<SWidget>& Root, FName Type, int32 Depth = 0)
	{
		if (Root->GetType() == Type)
		{
			return Root;
		}
		if (Depth > 200)
		{
			return nullptr;
		}
		FChildren* Children = Root->GetAllChildren();
		for (int32 I = 0; Children && I < Children->Num(); ++I)
		{
			if (TSharedPtr<SWidget> Found = FindByType(Children->GetChildAt(I), Type, Depth + 1))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static void CollectTexts(const TSharedRef<SWidget>& Root, TArray<TSharedRef<STextBlock>>& Out)
	{
		if (Root->GetType() == FName(TEXT("STextBlock")))
		{
			Out.Add(StaticCastSharedRef<STextBlock>(Root));
		}
		FChildren* Children = Root->GetAllChildren();
		for (int32 I = 0; Children && I < Children->Num(); ++I)
		{
			CollectTexts(Children->GetChildAt(I), Out);
		}
	}

	/**
	 * The covered screen as last painted: every non-empty text of StdCore's overlay, and whether the label, the bar and
	 * the marker overlap. False when the overlay is not found.
	 */
	static bool InspectCoveredScreen(const FString& Label, const FString& ExpectedText)
	{
		UWorld* World = W();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		TSharedPtr<SWidget> Root = Viewport ? StaticCastSharedPtr<SWidget>(Viewport->GetGameViewportWidget()) : nullptr;
		if (!Root.IsValid() && Viewport && Viewport->GetWindow().IsValid())
		{
			Root = Viewport->GetWindow();
		}
		const TSharedPtr<SWidget> Progress = Root ? FindByType(Root.ToSharedRef(), TEXT("SStdTransitionProgress")) : nullptr;
		const TSharedPtr<SWidget> Overlay = Progress ? Progress->GetParentWidget() : nullptr;
		if (!Progress || !Overlay)
		{
			Check(false, Label + TEXT(": StdCore's covered screen found in the viewport"));
			return false;
		}
		TArray<TSharedRef<STextBlock>> Texts;
		CollectTexts(Overlay.ToSharedRef(), Texts);
		TArray<FString> Shown;
		TSharedPtr<STextBlock> LabelBlock;
		for (const TSharedRef<STextBlock>& T : Texts)
		{
			if (!T->GetText().IsEmpty())
			{
				Shown.Add(T->GetText().ToString());
				LabelBlock = T;
			}
		}
		const bool bTextOk = ExpectedText.IsEmpty() ? Shown.Num() == 0 : (Shown.Num() == 1 && Shown[0] == ExpectedText);
		Check(bTextOk, FString::Printf(TEXT("%s: the covered screen shows %d texts [%s] (expected %s)"), *Label, Shown.Num(),
			*FString::Join(Shown, TEXT(" | ")), ExpectedText.IsEmpty() ? TEXT("none") : TEXT("only the entry line")));

		const FGeometry& ScreenGeometry = Progress->GetPaintSpaceGeometry();
		const FVector2D LocalSize = ScreenGeometry.GetLocalSize();
		const FVector2f ScreenSize(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
		const UStdFlowSubsystem* F = Flow();
		FStdTransitionProgress Now;
		Now.Reset(F ? F->GetMinCoveredSeconds() : 0.f);
		Now.Report(F ? F->GetTransitionProgress() : 0.f);
		Now.UpdateCovered(F ? F->GetTransitionMarkerFraction() * F->GetMinCoveredSeconds() : 0.f);
		const FStdTransitionLayout Layout = FStdTransitionLayout::Compute(*UStdProjectSettings::Get(), ScreenSize, Now, 1.f);
		const float BarTop = ScreenSize.Y - UStdProjectSettings::Get()->TransitionProgressBarHeight;
		const float MarkerTop = Layout.bShowMarker ? Layout.MarkerPosition.Y : BarTop;
		float LabelBottom = -1.f;
		if (LabelBlock)
		{
			const FGeometry& G = LabelBlock->GetPaintSpaceGeometry();
			const FVector2D Top = ScreenGeometry.AbsoluteToLocal(G.GetAbsolutePosition());
			const FVector2D Size = G.GetAbsoluteSize();
			LabelBottom = static_cast<float>(Top.Y + Size.Y / FMath::Max(ScreenGeometry.Scale, 0.01f));
		}
		Check(Layout.bShowBar && (!Layout.bShowMarker || Layout.MarkerPosition.Y + Layout.MarkerSize.Y <= BarTop + 0.01f) && LabelBottom < MarkerTop,
			FString::Printf(TEXT("%s: no overlap on a %.0fx%.0f screen (scale %.2f): label bottom %.1f, marker %.1f-%.1f, bar %.1f-%.1f"), *Label,
				ScreenSize.X, ScreenSize.Y, ScreenGeometry.Scale, LabelBottom, MarkerTop, MarkerTop + Layout.MarkerSize.Y, BarTop, ScreenSize.Y));
		return true;
	}

	// --- Travels ---

	static bool Arrived()
	{
		UWorld* World = W();
		if (!World || World == GRun.TravelFrom.Get())
		{
			return false;
		}
		const UStdFlowSubsystem* F = Flow();
		const AAreaGenerator* Gen = AAreaGenerator::Find(World);
		return F && !F->IsTransitioning() && (!Gen || Gen->IsReady()) && World->GetFirstPlayerController() != nullptr;
	}

	static void Arm()
	{
		GRun.Watch = FWatch();
		GRun.Watch.bArmed = true;
		GRun.Watch.ArmTime = FPlatformTime::Seconds();
		GRun.TravelFrom = W();
	}

	/** Checks the bar, the marker and the steps of the travel just finished. bArea: the destination is an area. */
	static void CheckTravel(const FString& Label, bool bArea, float ExpectedCovered)
	{
		const FWatch& Wt = GRun.Watch;
		const bool bMarkerExpected = ExpectedCovered > 0.f;
		Check(Wt.bSawTransition && Wt.bBarShownWhileCovered, FString::Printf(TEXT("%s: the bar is on the covered screen (%d frames)"), *Label, Wt.Frames));
		Check(Wt.bMarkerShown == bMarkerExpected, FString::Printf(TEXT("%s: covered time %.2f s, marker %s"), *Label, ExpectedCovered, Wt.bMarkerShown ? TEXT("shown") : TEXT("not shown")));
		Check(!Wt.bBarBack && !Wt.bMarkerBack, FString::Printf(TEXT("%s: frame after frame neither the bar nor the marker went back"), *Label));
		Check(FMath::IsNearlyEqual(Wt.BarAtFadeIn, 1.f), FString::Printf(TEXT("%s: the bar is full when the screen uncovers (%.3f)"), *Label, Wt.BarAtFadeIn));
		if (bMarkerExpected)
		{
			const float OneFrame = Wt.LastFrameDelta / ExpectedCovered;
			Check(FMath::IsNearlyEqual(Wt.MarkerAtFadeIn, 1.f) && Wt.MarkerBeforeFadeIn >= 1.f - OneFrame - 0.02f,
				FString::Printf(TEXT("%s: the marker reaches the right edge when the screen uncovers (%.3f; %.3f the frame before, after %.2f of %.2f s covered)"),
					*Label, Wt.MarkerAtFadeIn, Wt.MarkerBeforeFadeIn, Wt.CoveredBeforeFadeIn, ExpectedCovered));
		}
		Note(FString::Printf(TEXT("%s: longest interval without a frame %.3f s (state %s, ending %.3f s after the request); the frame interval holding the map load %.3f s; %d frames covered"),
			*Label, Wt.MaxFrameGap, *StaticEnum<EStdFlowState>()->GetNameStringByValue(static_cast<int64>(Wt.MaxGapState)), Wt.MaxGapAt, Wt.LoadGap, Wt.Frames));

		const TArray<FTravelStepRecord>& Steps = Run()->GetLastTravelSteps();
		TArray<FString> Names;
		float Covered = 0.f;
		for (const FTravelStepRecord& R : Steps)
		{
			if (R.Step == FName(TEXT("Covered")))
			{
				Covered = R.Seconds;
			}
		}
		bool bMonotonic = true;
		float Previous = -1.f;
		float AtReady = -1.f;
		float AtFadeIn = -1.f;
		float MarkerAtFadeIn = -1.f;
		for (const FTravelStepRecord& R : Steps)
		{
			Names.Add(R.Step.ToString());
			Note(FString::Printf(TEXT("%s: step %-15s at %.3f s after the request (%+.3f s from covered): bar %.2f, marker %.2f"), *Label, *R.Step.ToString(), R.Seconds,
				R.Seconds - Covered, R.Progress, R.Marker));
			bMonotonic &= R.Progress + KINDA_SMALL_NUMBER >= Previous;
			Previous = R.Progress;
			if (R.Step == FName(TEXT("AreaReady"))) { AtReady = R.Progress; }
			if (R.Step == FName(TEXT("FadeIn"))) { AtFadeIn = R.Progress; MarkerAtFadeIn = R.Marker; }
		}
		const FString Order = FString::Join(Names, TEXT(","));
		const FString Want = bArea ? TEXT("Covered,MapLoaded,RoutePlanned,GeometryBuilt,NavigationReady,AreaReady,FadeIn") : TEXT("Covered,MapLoaded,FadeIn");
		Check(Order == Want, FString::Printf(TEXT("%s: steps in order: %s"), *Label, *Order));
		Check(bMonotonic, FString::Printf(TEXT("%s: the bar rises step after step"), *Label));
		if (bArea)
		{
			Check(FMath::IsNearlyEqual(AtReady, 1.f), FString::Printf(TEXT("%s: the bar is full when the player is released (%.2f)"), *Label, AtReady));
		}
		Check(FMath::IsNearlyEqual(AtFadeIn, 1.f) && (!bMarkerExpected || FMath::IsNearlyEqual(MarkerAtFadeIn, 1.f)),
			FString::Printf(TEXT("%s: at the fade in bar %.2f, marker %.2f"), *Label, AtFadeIn, MarkerAtFadeIn));
		GRun.Kinds.Add(FString::Printf(TEXT("%-34s covered %.2f s, marker %s, bar full at uncover %s"), *Label, ExpectedCovered,
			Wt.bMarkerShown ? TEXT("shown") : TEXT("not shown"), FMath::IsNearlyEqual(Wt.BarAtFadeIn, 1.f) ? TEXT("yes") : TEXT("NO")));
	}

	/** Expected covered time of a travel into Act at the current battery: max(TravelHoldSeconds, reading time of its entry line). */
	static float ExpectedCoveredInto(int32 ToAct, FText& OutLabel)
	{
		float Reading = 0.f;
		UHomeAIComponent::BuildTravelLabel(W(), ToAct, OutLabel, Reading);
		return FMath::Clamp(FMath::Max(UTheLastShiftSettings::Get()->TravelHoldSeconds, Reading), 0.f, 60.f);
	}

	/** One travel into an area, started by Start; checks it on arrival. A screenshot at ShotAt s covered when given. */
	static void AddAreaTravel(TArray<FStep>& S, const FString& Label, int32 ToAct, TFunction<bool()> Start, float ShotAt = -1.f, const FString& ShotName = FString())
	{
		TSharedRef<float> Expected = MakeShared<float>(0.f);
		TSharedRef<FString> LabelText = MakeShared<FString>();
		S.Add(Do(Label + TEXT(": start"), [Label, ToAct, Start, Expected, LabelText]()
		{
			FText Line;
			*Expected = ExpectedCoveredInto(ToAct, Line);
			*LabelText = Line.ToString();
			Arm();
			const bool bStarted = Start();
			Check(bStarted && FMath::IsNearlyEqual(Run()->GetLastTravelMinCoveredSeconds(), *Expected, 0.01f),
				FString::Printf(TEXT("%s: started, covered at least %.2f s (expected %.2f), line \"%s\""), *Label, Run()->GetLastTravelMinCoveredSeconds(), *Expected, **LabelText));
			return bStarted ? 0.f : -1.f;
		}));
		if (ShotAt >= 0.f)
		{
			S.Add(WaitFor(Label + TEXT(": covered for the screenshot"), [ShotAt]()
			{
				const UStdFlowSubsystem* F = Flow();
				return F && F->GetCoveredSeconds() >= ShotAt;
			}, 30.f, [Label, ShotName, LabelText]()
			{
				InspectCoveredScreen(Label, *LabelText);
				Shot(ShotName);
				return 0.f;
			}));
		}
		S.Add(WaitFor(Label + TEXT(": arrival"), []() { return Arrived(); }, 120.f, [Label, ToAct, Expected]()
		{
			Check(Act() == ToAct, FString::Printf(TEXT("%s: arrived in act %d (expected %d)"), *Label, Act(), ToAct));
			CheckTravel(Label, true, *Expected);
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(TimeStateDebug::BuildInputReport(W()).IsGameReady(), FString::Printf(TEXT("%s: input after the travel: game ready"), *Label));
			GRun.Watch.bArmed = false;
			return 1.0f;
		}));
	}

	static void Build(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("main menu ready"), []()
		{
			const UStdFlowSubsystem* F = Flow();
			return F && !F->IsTransitioning() && W() && W()->GetFirstPlayerController() != nullptr;
		}, 30.f, []() { return 0.5f; }));
		S.Add(Do(TEXT("setup"), []()
		{
			const UStdProjectSettings* Std = UStdProjectSettings::Get();
			if (!Run() || !Home() || Act() != 0)
			{
				Check(false, TEXT("needs a fresh PIE (or a package) in MainMenu, act 0"));
				return -1.f;
			}
			Check(Std->bTransitionProgressBar && Std->bTransitionMarker && Std->TransitionMarkerImage.IsNull(),
				FString::Printf(TEXT("the game switches the bar and the marker on (bar %d, marker %d), placeholder marker (no image)"),
					Std->bTransitionProgressBar ? 1 : 0, Std->bTransitionMarker ? 1 : 0));
			const FTravelProgressFractions& P = UTheLastShiftSettings::Get()->TravelProgress;
			Note(FString::Printf(TEXT("fractions: map loaded %.2f (StdCore), route planned %.2f, geometry built %.2f, navigation ready %.2f, player released 1"),
				Std->TransitionMapLoadedProgress, P.RoutePlanned, P.GeometryBuilt, P.NavigationReady));
			return 0.f;
		}));

		AddAreaTravel(S, TEXT("new run: main menu -> area 1"), 1, []() { return Run()->StartNewRun(); });

		// Criteria 6, 7 and 8: area 1 -> 2 with the entry line at full battery.
		S.Add(Do(TEXT("battery 1"), []() { Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed()); return 0.f; }));
		AddAreaTravel(S, TEXT("next area: area 1 -> 2 (battery 1)"), 2, []() { return Run()->TravelToNextArea(); }, 2.0f, TEXT("Travel_1to2_Full"));

		// Back to area 1, then 1 -> 2 again with no line.
		AddAreaTravel(S, TEXT("new run from area 2 -> area 1"), 1, []() { return Run()->StartNewRun(); });
		S.Add(Do(TEXT("battery 0.02"), []() { Home()->RestoreState(0.02, Home()->GetHomeSecondsElapsed()); return 0.f; }));
		AddAreaTravel(S, TEXT("next area: area 1 -> 2 (battery 0.02)"), 2, []() { return Run()->TravelToNextArea(); }, 2.0f, TEXT("Travel_1to2_NoLine"));

		// A restart after a loss, through the loss screen's entry of act 2.
		S.Add(Do(TEXT("loss"), []()
		{
			Home()->ApplyJump(EHomeJumpCause::Contact, 2, 1.f);
			return 1.0f;
		}));
		AddAreaTravel(S, TEXT("restart after a loss: area 2 -> area 2"), 2, []()
		{
			Check(Home()->IsRunLost() && Run()->GetLossScreen() != nullptr, FString::Printf(TEXT("the run is lost, loss screen [%s]"), *TimeStateDebug::DescribeChoiceList(W())));
			const int32 Index = Run()->GetSnapshots().IndexOfByPredicate([](const FAreaSnapshot& Snap) { return Snap.ActIndex == 2; });
			return Index != INDEX_NONE && TimeStateDebug::ClickChoiceEntry(W(), Index);
		}, 1.0f, TEXT("Travel_Restart_Area2"));

		// To the main menu: no covered time, no marker; the bar still fills.
		S.Add(Do(TEXT("main menu: start"), []()
		{
			Arm();
			const bool bStarted = Run()->ReturnToMainMenu();
			Check(bStarted && Run()->GetLastTravelMinCoveredSeconds() == 0.f, TEXT("travel to the main menu started, no minimum covered time"));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitFor(TEXT("main menu: covered"), []() { const UStdFlowSubsystem* F = Flow(); return F && F->GetOverlayAlpha() >= 1.f; }, 10.f, []()
		{
			InspectCoveredScreen(TEXT("main menu"), FString());
			Shot(TEXT("Travel_MainMenu"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("main menu: arrival"), []() { return Arrived(); }, 60.f, []()
		{
			Check(Act() == 0, FString::Printf(TEXT("main menu: arrived in act %d"), Act()));
			CheckTravel(TEXT("return to the main menu"), false, 0.f);
			GRun.Watch.bArmed = false;
			return 0.f;
		}));
		S.Add(Do(TEXT("kinds not run here"), []()
		{
			GRun.Kinds.Add(FString::Printf(TEXT("%-34s covered %.2f s, marker shown (same path as next area)"), TEXT("continue / select act -> area k"), UTheLastShiftSettings::Get()->TravelHoldSeconds));
			GRun.Kinds.Add(TEXT("pause menu Main menu / after area 4     covered 0.00 s, marker not shown (StdCore ReturnToMainMenu)"));
			GRun.Kinds.Add(TEXT("startup fade in (first map)            covered 0.00 s, marker not shown"));
			return 0.f;
		}));
	}

	static bool StartRun(const FString& Name, const TArray<FString>& Args)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: already running."), *Name);
			return false;
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.ShotTag = Args.IsValidIndex(0) ? Args[0] : FString();
		GRun.bQuitAtEnd = Args.IsValidIndex(1) && FCString::Atoi(*Args[1]) != 0;
		Build(GRun.Steps);
		GRun.bRunning = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *Name, GRun.Steps.Num());
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsTravelScreenTestCommand(
	TEXT("tls.TravelScreenTest"),
	TEXT("tls.TravelScreenTest [ShotTag] [Quit=0] (fresh PIE or package in MainMenu; starts a new run and clears the save): the travel screen's bar and marker on a new run, area 1 -> 2 at battery 1 and 0.02, a new run from area 2, a restart after a loss and the return to the main menu: steps in order with their times, bar full when the player is released, marker at the right edge as the screen uncovers, nothing going back, the longest interval without a frame, no overlap and no text but the entry line; screenshots in Saved/Screenshots/Brief6b."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		TravelScreenDebug::StartRun(TEXT("TravelScreenTest"), Args);
	}));

#endif // !UE_BUILD_SHIPPING
