// SPDX-License-Identifier: MIT

// Brief 22: the engineer answers before any row of the text channel, and the spending comment follows the kind of device.
//
// tls.EngineerLinesTest [Quit] (fresh PIE or a package, MainMenu; starts a new run and clears the save): with fixture rows
// (Debug/NarrativeFixture), never the rows of Narrative/. Area 1's AreaStart sequence with an engineer line before its first
// row and two before the row in its middle; a spending comment that repeats (its line the first time only); the entity found
// once in a run (a line between EntitySeen and EntityFound; found again in the same map: no line); one line before a row of
// each prologue group said on the channel (Wake, Hologram, Hangar, Light in the middle of its group, Hatch, Door); each line
// shot and its glyphs and bar measured against TextColors.Engineer, the row after it in the home AI's colour without the bar
// and its text unchanged. Five battery levels (in full, typed at the full battery's speed; at Silent the home AI's row is
// dropped and the line said); hidden while Stopped, restarted, suspended behind the pause menu and behind the travel
// screen; no line when its row is not said (FirstEnergy never banked, a row of another act, the area entry label of the
// travel to area 2). Shots to Saved/Screenshots/Brief22.
//
// tls.SpendKindTest [Quit] (an area map, Area1_Hull; PIE or a package): terminals spawned around the player for each kind
// of payment (a door, a group of lights, a beacon, two log terminals, the twin AI's terminal, one that powers nothing).
// With today's tables (no row of any kind) every payment says the act's Spend row, as before this brief; then with fixture
// rows of the kinds: a payment says its kind's row, the next payment of that kind the next row and then the last again, two
// kinds count on their own, a kind with no row and a terminal that powers nothing say the Spend rows counted as before, an
// engineer line before a kind's row at the first payment of that kind only, the twin AI's part after its comment; then a second
// set of rows, two each for Lights, Beacon, Log and Twin: each kind its own first row, its second at a second payment and the
// second again at a third, in three interleaved rounds (the table given again so every trigger counts from its first row, the
// archive cleared so the twin AI plays its parts again, the log payments at a pair of log terminals of their own, dealt on their
// own as area 2's two, both locked again before the third). The run's
// banked energy and archive are put back. Development builds only.

#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Debug/HomeTestListener.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TextTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TwinWriting.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"

#if !UE_BUILD_SHIPPING

namespace EngineerLinesDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }
	static UTextChannelComponent* Channel() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr; }
	static UHomeAIComponent* HomeAI() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UHomeAIComponent>() : nullptr; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }

	static UStdFlowSubsystem* Flow()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	static UTextChannelComponent* ChannelOf(UWorld* World)
	{
		const APlayerController* C = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* P = C ? C->GetPawn() : nullptr;
		return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr;
	}

	static UTextChannelWidget* ChannelWidget()
	{
		TArray<UUserWidget*> Found;
		if (UWorld* World = W())
		{
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UTextChannelWidget::StaticClass(), false);
		}
		return Found.Num() > 0 ? Cast<UTextChannelWidget>(Found[0]) : nullptr;
	}

	static FTextChannelContext ContextAt(double Battery)
	{
		FTextChannelContext Context = HomeAI() ? HomeAI()->MakeContext() : FTextChannelContext();
		Context.BatteryFraction = Battery;
		return Context;
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
		TStrongObjectPtr<UTlsTextTestListener> Text;
		TFunction<void()> Cleanup;
		TWeakObjectPtr<UWorld> TravelFrom;
		TWeakObjectPtr<UTextChannelComponent> BoundChannel;
		TWeakObjectPtr<UTextChannelComponent> OldChannel;
		/** Every row started on any channel of the test, in order (the listener starts again with each pawn). */
		TArray<FName> AllStarted;
		int32 Mark = 0;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		int32 SavedBanked = 0;
		bool bArchiveWasEmpty = true;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		/** The area's own terminal the repeat case pays at (moved in reach, not spawned). */
		TWeakObjectPtr<APowerTerminal> PaidTerminal;
		TMap<EPaymentKind, TArray<TWeakObjectPtr<APowerTerminal>>> Terminals;
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
			GRun.Cleanup = nullptr;
		}
		if (GRun.Text)
		{
			GRun.Text->Unbind();
			GRun.Text.Reset();
		}
		NarrativeFixture::Restore(W());
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuitAtEnd)
		{
			FPlatformMisc::RequestExit(false, *GRun.Name);
		}
	}

	/** Keeps AllStarted: what the listener has started since the last call. */
	static int32 GLastSeenStarts = 0;
	static void Collect()
	{
		if (!GRun.Text)
		{
			return;
		}
		const TArray<FName>& Started = GRun.Text->Started;
		for (int32 I = FMath::Min(GLastSeenStarts, Started.Num()); I < Started.Num(); ++I)
		{
			GRun.AllStarted.Add(Started[I]);
		}
		GLastSeenStarts = Started.Num();
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		Collect();
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

	static void Start(const FString& Name, bool bQuit, TArray<FStep>&& Steps)
	{
		GRun = FRun();
		GLastSeenStarts = 0;
		GRun.Name = Name;
		GRun.bQuitAtEnd = bQuit;
		GRun.Steps = MoveTemp(Steps);
		GRun.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d steps, in %s."), *Name, GRun.Steps.Num(), W() ? *UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()) : TEXT("-"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	// --- The channel's rows ---

	static void BindChannel(UTextChannelComponent* C)
	{
		Collect();
		if (!GRun.Text)
		{
			GRun.Text.Reset(NewObject<UTlsTextTestListener>());
			GRun.Text->bUseRealClock = true;
		}
		GRun.Text->Bind(C);
		GLastSeenStarts = 0;
		GRun.BoundChannel = C;
	}

	static void MarkRows() { Collect(); GRun.Mark = GRun.AllStarted.Num(); }

	static FString RowsSinceMark()
	{
		Collect();
		FString Out;
		for (int32 I = GRun.Mark; I < GRun.AllStarted.Num(); ++I)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + GRun.AllStarted[I].ToString();
		}
		return Out;
	}

	/** The started rows since the mark, twin parts (T_*) left out. */
	static FString HomeRowsSinceMark()
	{
		Collect();
		FString Out;
		for (int32 I = GRun.Mark; I < GRun.AllStarted.Num(); ++I)
		{
			const FString Name = GRun.AllStarted[I].ToString();
			if (!Name.StartsWith(TEXT("T_")))
			{
				Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + Name;
			}
		}
		return Out;
	}

	static bool EverStarted(FName Row)
	{
		Collect();
		return GRun.AllStarted.Contains(Row);
	}

	static bool ChannelIdle() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }

	static FStep WaitIdle(const FString& Label, float Timeout = 90.f)
	{
		return WaitFor(Label + TEXT(": channel idle"), []() { return ChannelIdle(); }, Timeout, []() { return 0.5f; });
	}

	static bool RowTypedOut(FName Row)
	{
		const UTextChannelComponent* C = Channel();
		return C && C->GetCurrentRowName() == Row && C->IsVisible() && C->GetVisibleText().Len() > 0 && C->GetVisibleText().Len() == C->GetCurrentText().Len();
	}

	static FName Line(FName Before, int32 Order = 1) { return NarrativeFixture::GetEngineerLineName(Before, Order); }

	/** The home AI's text of a fixture message row at the battery of Context, as the channel composes it. */
	static FString HomeText(FName Row, const FTextChannelContext& Context)
	{
		const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
		const FAIMessageRow* R = Table ? Table->FindRow<FAIMessageRow>(Row, TEXT("tls"), false) : nullptr;
		return R ? UTextChannelComponent::ComposeMessage(*R, UTheLastShiftSettings::Get()->GetTextLevel(Context.BatteryFraction), Context) : FString();
	}

	// --- A shot with the UI, measured on its pixels (as tls.TextChannelTest since Brief 21) ---

	struct FUIShot
	{
		FString Label;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
		bool bReady = false;
		FBox2D Rect = FBox2D(ForceInit);
		FBox2D BarRect = FBox2D(ForceInit);
	};
	static FUIShot GUIShot;
	static FDelegateHandle GUIShotHandle;

	static void HandleUIShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(GUIShotHandle);
		GUIShotHandle.Reset();
		GUIShot.Width = Width;
		GUIShot.Height = Height;
		GUIShot.Pixels = Pixels;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief22"), GUIShot.Label + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("shot %s saved (%d x %d): %s"), *GUIShot.Label, Width, Height, *Path));
		GUIShot.bReady = true;
	}

	static void RequestUIShot(const FString& Label)
	{
		GUIShot = FUIShot();
		GUIShot.Label = Label;
		const UTextChannelWidget* Widget = ChannelWidget();
		FBox2D Rect = Widget ? Widget->GetShownRect() : FBox2D(ForceInit);
		FBox2D Bar(ForceInit);
		if (const UBorder* VoiceBar = Widget ? Widget->GetVoiceBar() : nullptr; VoiceBar && VoiceBar->IsVisible())
		{
			const FGeometry& G = VoiceBar->GetCachedGeometry();
			Bar = FBox2D(G.GetAbsolutePosition(), G.GetAbsolutePosition() + G.GetAbsoluteSize());
		}
		if (GEngine && GEngine->GameViewport)
		{
			if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
			{
				Rect = Rect.bIsValid ? Rect.ShiftBy(-Window->GetPositionInScreen()) : Rect;
				Bar = Bar.bIsValid ? Bar.ShiftBy(-Window->GetPositionInScreen()) : Bar;
			}
		}
		GUIShot.Rect = Rect;
		GUIShot.BarRect = Bar;
		if (!GUIShotHandle.IsValid())
		{
			GUIShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleUIShot);
		}
		FScreenshotRequest::RequestScreenshot(FString(), true, false);
	}

	static bool InBar(int32 X, int32 Y)
	{
		const FBox2D& B = GUIShot.BarRect;
		return B.bIsValid && X >= B.Min.X - 2 && X <= B.Max.X + 2 && Y >= B.Min.Y - 2 && Y <= B.Max.Y + 2;
	}

	static bool MeasureBar(FColor& OutMean, int32& OutCount)
	{
		const FUIShot& Shot = GUIShot;
		OutCount = 0;
		if (!Shot.BarRect.bIsValid || Shot.Pixels.Num() < Shot.Width * Shot.Height)
		{
			return false;
		}
		int64 R = 0, G = 0, B = 0;
		for (int32 Y = FMath::Max(0, FMath::CeilToInt32(Shot.BarRect.Min.Y) + 1); Y < FMath::Min(Shot.Height, FMath::FloorToInt32(Shot.BarRect.Max.Y) - 1); ++Y)
		{
			for (int32 X = FMath::Max(0, FMath::CeilToInt32(Shot.BarRect.Min.X)); X < FMath::Min(Shot.Width, FMath::FloorToInt32(Shot.BarRect.Max.X)); ++X)
			{
				const FColor& C = Shot.Pixels[Y * Shot.Width + X];
				R += C.R; G += C.G; B += C.B; ++OutCount;
			}
		}
		if (OutCount == 0)
		{
			return false;
		}
		OutMean = FColor(static_cast<uint8>(R / OutCount), static_cast<uint8>(G / OutCount), static_cast<uint8>(B / OutCount));
		return true;
	}

	static bool MeasureBox(FColor& OutMean, int32& OutCount)
	{
		const FUIShot& Shot = GUIShot;
		OutCount = 0;
		if (!Shot.Rect.bIsValid || Shot.Pixels.Num() < Shot.Width * Shot.Height)
		{
			return false;
		}
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(Shot.Rect.Min.X) + 2, 0, Shot.Width);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt32(Shot.Rect.Max.X) - 2, 0, Shot.Width);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(Shot.Rect.Min.Y) + 2, 0, Shot.Height);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(Shot.Rect.Max.Y) - 2, 0, Shot.Height);
		auto Lum = [](const FColor& C) { return C.R * 2 + C.G * 5 + C.B; };
		int32 MaxLum = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				if (!InBar(X, Y))
				{
					MaxLum = FMath::Max(MaxLum, Lum(Shot.Pixels[Y * Shot.Width + X]));
				}
			}
		}
		int64 R = 0, G = 0, B = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const FColor& C = Shot.Pixels[Y * Shot.Width + X];
				if (MaxLum > 0 && !InBar(X, Y) && Lum(C) * 100 >= MaxLum * 92)
				{
					R += C.R; G += C.G; B += C.B; ++OutCount;
				}
			}
		}
		if (OutCount == 0)
		{
			return false;
		}
		OutMean = FColor(static_cast<uint8>(R / OutCount), static_cast<uint8>(G / OutCount), static_cast<uint8>(B / OutCount));
		return true;
	}

	static FColor OnScreen(const FLinearColor& Color)
	{
		return FLinearColor(Color.R * Color.A, Color.G * Color.A, Color.B * Color.A, 1.f).ToFColor(true);
	}

	static bool Near(const FColor& A, const FColor& B, int32 Tolerance)
	{
		return FMath::Abs(A.R - B.R) <= Tolerance && FMath::Abs(A.G - B.G) <= Tolerance && FMath::Abs(A.B - B.B) <= Tolerance;
	}

	/** A shot of the box now: the glyphs (the bar left out) and the bar measured against the voice's colour of TextColors. */
	static void AddBoxShot(TArray<FStep>& S, const FString& Label, const FString& ShotName, bool bEngineer)
	{
		S.Add(Do(Label + TEXT(": shot"), [ShotName]() { RequestUIShot(ShotName); return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": shot taken"), []() { return GUIShot.bReady; }, 5.f, [Label, bEngineer]()
		{
			const FSuitTextColors& Colors = UTheLastShiftSettings::GetTextColors();
			const FLinearColor Want = bEngineer ? Colors.Engineer : Colors.HomeAI;
			const FColor Expected = OnScreen(Want);
			const FColor Other = OnScreen(bEngineer ? Colors.HomeAI : Colors.Engineer);
			FColor Mean;
			int32 Count = 0;
			const bool bMeasured = MeasureBox(Mean, Count);
			FColor BarMean(0, 0, 0);
			int32 BarCount = 0;
			const bool bBar = MeasureBar(BarMean, BarCount);
			const bool bBarOk = bEngineer ? (bBar && BarCount >= 10 && Near(BarMean, Expected, 14)) : !bBar;
			Check(bMeasured && Count >= 10 && Near(Mean, Expected, 14) && !Near(Mean, Other, 14) && bBarOk,
				FString::Printf(TEXT("%s: the brightest %d pixels of the glyphs (the bar left out) are (%d, %d, %d) on the shot: the %s colour of TextColors (on screen (%d, %d, %d)), not the %s one ((%d, %d, %d)); bar %s"),
					*Label, Count, Mean.R, Mean.G, Mean.B, bEngineer ? TEXT("Engineer") : TEXT("HomeAI"), Expected.R, Expected.G, Expected.B,
					bEngineer ? TEXT("HomeAI") : TEXT("Engineer"), Other.R, Other.G, Other.B,
					bBar ? *FString::Printf(TEXT("(%d, %d, %d) over %d pixels"), BarMean.R, BarMean.G, BarMean.B, BarCount) : TEXT("not shown")));
			return 0.f;
		}));
	}

	/**
	 * The engineer line Line typed out on the channel: in full (its fixture text), the engineer's voice at Full, the widget in
	 * the Engineer colour with its bar; then a shot when ShotName is set.
	 */
	static void AddLineTypedOut(TArray<FStep>& S, const FString& Label, FName LineName, FName Before, int32 Order, const FString& ShotName, float Timeout = 40.f)
	{
		S.Add(WaitFor(Label + TEXT(": ") + LineName.ToString() + TEXT(" typed out"), [LineName]() { return RowTypedOut(LineName); }, Timeout, [Label, LineName, Before, Order]()
		{
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = ChannelWidget();
			const FString Want = NarrativeFixture::GetEngineerLineText(Before, Order);
			const UBorder* Bar = Widget ? Widget->GetVoiceBar() : nullptr;
			Check(C->GetCurrentVoice() == ETextVoice::Engineer && C->GetCurrentLevel() == ETextDegradation::Full && C->GetCurrentText() == Want,
				FString::Printf(TEXT("%s: %s in the engineer's voice, in full: \"%s\""), *Label, *LineName.ToString(), *C->GetCurrentText()));
			Check(Widget && Widget->IsShowingEngineer() && Widget->GetHomeTextBlock()
				&& Widget->GetHomeTextBlock()->GetColorAndOpacity().GetSpecifiedColor().Equals(UTheLastShiftSettings::GetTextColors().Engineer)
				&& Bar && Bar->IsVisible() && Bar->GetBrushColor().Equals(UTheLastShiftSettings::GetTextColors().Engineer),
				FString::Printf(TEXT("%s: the widget shows it in the Engineer colour of TextColors, with its bar"), *Label));
			return 0.f;
		}));
		if (!ShotName.IsEmpty())
		{
			AddBoxShot(S, Label, ShotName, true);
		}
	}

	/** The home AI's row Row typed out: the home AI's voice, its text as the channel composes it at the battery now (unchanged), no bar. */
	static void AddRowTypedOut(TArray<FStep>& S, const FString& Label, FName Row, TFunction<FString()> WantText, float Timeout = 40.f)
	{
		S.Add(WaitFor(Label + TEXT(": ") + Row.ToString() + TEXT(" typed out"), [Row]() { return RowTypedOut(Row); }, Timeout, [Label, Row, WantText]()
		{
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = ChannelWidget();
			const FString Want = WantText();
			Check(C->GetCurrentVoice() == ETextVoice::Home && C->GetCurrentText() == Want && Widget && !Widget->IsShowingEngineer() && Widget->GetVoiceBar()
				&& !Widget->GetVoiceBar()->IsVisible(),
				FString::Printf(TEXT("%s: %s after it, the home AI's, unchanged and without the bar: \"%s\" (expected \"%s\")"), *Label, *Row.ToString(), *C->GetCurrentText(), *Want));
			return 0.f;
		}));
	}

	static FStep CheckOrder(const FString& Label, const FString& Want, bool bTwinLeftOut = false)
	{
		return Do(Label + TEXT(": order"), [Label, Want, bTwinLeftOut]()
		{
			const FString Got = bTwinLeftOut ? HomeRowsSinceMark() : RowsSinceMark();
			Check(Got == Want, FString::Printf(TEXT("%s: on the channel, in order: %s (expected %s)"), *Label, *Got, *Want));
			return 0.f;
		});
	}

	// --- tls.EngineerLinesTest ---

	static const double Batteries[] = { 1.0, 0.65, 0.35, 0.05, 0.02 };
	static const TCHAR* BatteryRows[] = { TEXT("A2_Spend_1"), TEXT("A3_Spend_1"), TEXT("A4_Spend_1"), TEXT("A2_EntityFound_1"), TEXT("A4_EntityFound_1") };

	static TArray<NarrativeFixture::FEngineerLine> TestLines()
	{
		TArray<NarrativeFixture::FEngineerLine> L = {
			{ TEXT("A1_AreaStart_1"), 1 }, { TEXT("A1_AreaStart_2"), 2 }, { TEXT("A1_AreaStart_2"), 1 }, { TEXT("A1_Spend_1"), 1 }, { TEXT("A1_EntityFound_1"), 1 },
			{ TEXT("Wake_1"), 1 }, { TEXT("Hologram_1"), 1 }, { TEXT("Hangar_1"), 1 }, { TEXT("Light_2"), 1 }, { TEXT("Hatch_1"), 1 }, { TEXT("Door_1"), 1 },
			// Never said: a row of a trigger never fired, a row of another act, and a label.
			{ TEXT("A1_FirstEnergy_1"), 1 }, { TEXT("A3_EntityFound_1"), 1 }, { TEXT("A2_AreaEntry_1"), 1 },
			// Stop and menu, then the travel.
			{ TEXT("A3_JumpReturn_1"), 1 }, { TEXT("A4_JumpReturn_1"), 1 } };
		for (const TCHAR* Row : BatteryRows)
		{
			L.Add({ Row, 1 });
		}
		return L;
	}

	static bool AreaReady(UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return (!F || !F->IsTransitioning()) && URunSubsystem::GetActIndexOf(World) > 0 && World->GetFirstPlayerController() != nullptr;
	}

	static void AddPrologueGroup(TArray<FStep>& S, FName Group, int32 Rows, int32 LineBefore)
	{
		const FString Label = FString::Printf(TEXT("prologue group %s"), *Group.ToString());
		const FName Before(*FString::Printf(TEXT("%s_%d"), *Group.ToString(), LineBefore));
		S.Add(WaitIdle(Label));
		S.Add(Do(Label + TEXT(": said"), [Group]()
		{
			MarkRows();
			const int32 Queued = HomeAI()->SayPrologueGroup(Group);
			Note(FString::Printf(TEXT("%s: %d rows queued"), *Group.ToString(), Queued));
			return 0.f;
		}));
		AddLineTypedOut(S, Label, Line(Before), Before, 1, FString::Printf(TEXT("Engineer_Before_%s"), *Before.ToString()));
		AddRowTypedOut(S, Label, Before, [Group, LineBefore]() { return NarrativeFixture::GetPrologueLineText(Group, LineBefore); });
		S.Add(WaitIdle(Label));
		FString Want;
		for (int32 Order = 1; Order <= Rows; ++Order)
		{
			const FName Row(*FString::Printf(TEXT("%s_%d"), *Group.ToString(), Order));
			Want += (Want.IsEmpty() ? TEXT("") : TEXT(" ")) + (Order == LineBefore ? Line(Row).ToString() + TEXT(" ") : FString()) + Row.ToString();
		}
		S.Add(CheckOrder(Label, Want));
	}

	static TArray<FStep> BuildEngineerLinesTest()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("main menu ready"), []()
		{
			const UStdFlowSubsystem* F = Flow();
			return F && !F->IsTransitioning() && W() && W()->GetFirstPlayerController() != nullptr && Act() == 0;
		}, 30.f, []() { return 0.5f; }));
		S.Add(Do(TEXT("setup: fixture rows with engineer lines, a new run"), []()
		{
			if (!Run() || !Home() || !Energy())
			{
				Check(false, TEXT("needs a fresh PIE or package in MainMenu with the run, home and energy subsystems"));
				return -1.f;
			}
			NarrativeFixture::Install(W());
			NarrativeFixture::SetEngineerLines(W(), TestLines());
			GRun.TravelFrom = W();
			const bool bStarted = Run()->StartNewRun();
			Check(bStarted, TEXT("a new run started (save cleared, battery 1)"));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitFor(TEXT("area 1"), []()
		{
			UWorld* World = W();
			if (!World || World == GRun.TravelFrom.Get())
			{
				return false;
			}
			// Bound as soon as the pawn exists: the area's first messages may start in the frame it becomes ready.
			if (UTextChannelComponent* C = ChannelOf(World); C && GRun.BoundChannel.Get() != C)
			{
				BindChannel(C);
				MarkRows();
			}
			return AreaReady(World);
		}, 120.f, []()
		{
			Check(Act() == 1 && Channel() && Channel()->GetEngineerTable() && Channel()->GetEngineerTable()->GetRowMap().Num() == TestLines().Num(),
				FString::Printf(TEXT("area 1: the channel has the fixture's %d engineer lines (%s)"), TestLines().Num(), *GetNameSafe(Channel() ? Channel()->GetEngineerTable() : nullptr)));
			GRun.TravelFrom.Reset();
			return 0.f;
		}));

		// --- A sequence: a line before its first row, two lines before the row in its middle (the real AreaStart trigger) ---
		const FString Seq = TEXT("AreaStart");
		const auto Full = []() { return ContextAt(1.0); };
		AddLineTypedOut(S, Seq, Line(TEXT("A1_AreaStart_1")), TEXT("A1_AreaStart_1"), 1, TEXT("Engineer_Before_A1_AreaStart_1"));
		AddRowTypedOut(S, Seq, TEXT("A1_AreaStart_1"), [Full]() { return HomeText(TEXT("A1_AreaStart_1"), Full()); });
		AddLineTypedOut(S, Seq, Line(TEXT("A1_AreaStart_2"), 1), TEXT("A1_AreaStart_2"), 1, TEXT("Engineer_Before_A1_AreaStart_2_first"));
		AddLineTypedOut(S, Seq, Line(TEXT("A1_AreaStart_2"), 2), TEXT("A1_AreaStart_2"), 2, TEXT("Engineer_Before_A1_AreaStart_2_second"));
		AddRowTypedOut(S, Seq, TEXT("A1_AreaStart_2"), [Full]() { return HomeText(TEXT("A1_AreaStart_2"), Full()); });
		AddBoxShot(S, Seq + TEXT(": the home AI's row after the lines"), TEXT("HomeAI_A1_AreaStart_2_After_Engineer"), false);
		S.Add(WaitIdle(Seq));
		S.Add(CheckOrder(Seq, TEXT("E_A1_AreaStart_1_1 A1_AreaStart_1 E_A1_AreaStart_2_1 E_A1_AreaStart_2_2 A1_AreaStart_2 A1_AreaStart_3")));

		// --- A row that repeats: a spending comment at three payments, its line the first time only ---
		const FString Rep = TEXT("repeat");
		S.Add(Do(Rep + TEXT(": the first payment"), [Rep]()
		{
			MarkRows();
			APowerTerminal* T = nullptr;
			for (TActorIterator<APowerTerminal> It(W()); It; ++It)
			{
				if (It->GetCost() > 0 && It->AcceptsPayment() && It->GetPaymentKind() == EPaymentKind::Door) { T = *It; break; }
			}
			if (!T)
			{
				Check(false, Rep + TEXT(": area 1 has a gate's terminal"));
				return -1.f;
			}
			// In range without moving the pawn: the payment is what is tested here.
			T->InteractRadius = 1000.f;
			T->SetActorLocation(Pawn()->GetActorLocation() + Pawn()->GetActorForwardVector() * 300.f - FVector(0.f, 0.f, 90.f));
			Energy()->AddBankedEnergy(3 * T->GetCost());
			Check(T->TryActivate(Pawn()), FString::Printf(TEXT("%s: paid at %s (kind %s; the fixture has no SpendDoor row: the Spend row)"), *Rep, *T->GetName(), LexToString(T->GetPaymentKind())));
			GRun.PaidTerminal = T;
			return 0.f;
		}));
		AddLineTypedOut(S, Rep, Line(TEXT("A1_Spend_1")), TEXT("A1_Spend_1"), 1, TEXT("Engineer_Before_A1_Spend_1"));
		AddRowTypedOut(S, Rep, TEXT("A1_Spend_1"), [Full]() { return HomeText(TEXT("A1_Spend_1"), Full()); });
		for (int32 Pay = 2; Pay <= 3; ++Pay)
		{
			S.Add(WaitIdle(Rep));
			S.Add(Do(FString::Printf(TEXT("%s: payment %d"), *Rep, Pay), [Rep, Pay]()
			{
				APowerTerminal* T = GRun.PaidTerminal.Get();
				Check(T && T->TryActivate(Pawn()), FString::Printf(TEXT("%s: payment %d"), *Rep, Pay));
				return 0.f;
			}));
		}
		S.Add(WaitIdle(Rep));
		S.Add(CheckOrder(Rep, TEXT("E_A1_Spend_1_1 A1_Spend_1 A1_Spend_1 A1_Spend_1")));

		// --- A row said once in a run: the entity found by a sighting (a line between EntitySeen and EntityFound) ---
		const FString Once = TEXT("once in a run");
		S.Add(Do(Once + TEXT(": the entity found by a sighting"), [Once]()
		{
			MarkRows();
			Run()->SetEntityFound(false, TEXT("tls.EngineerLinesTest"));
			Check(Run()->MarkEntityFound(TEXT("tls.EngineerLinesTest"), EEntityFoundBy::Sighting), Once + TEXT(": found (OnEntityFound)"));
			return 0.f;
		}));
		AddLineTypedOut(S, Once, Line(TEXT("A1_EntityFound_1")), TEXT("A1_EntityFound_1"), 1, TEXT("Engineer_Before_A1_EntityFound_1"));
		AddRowTypedOut(S, Once, TEXT("A1_EntityFound_1"), [Full]() { return HomeText(TEXT("A1_EntityFound_1"), Full()); });
		S.Add(WaitIdle(Once));
		S.Add(CheckOrder(Once, TEXT("A1_EntitySeen_1 E_A1_EntityFound_1_1 A1_EntityFound_1")));
		S.Add(Do(Once + TEXT(": found again in the same map"), [Once]()
		{
			MarkRows();
			Check(!Run()->MarkEntityFound(TEXT("tls.EngineerLinesTest"), EEntityFoundBy::Sighting), Once + TEXT(": already found: nothing"));
			Run()->SetEntityFound(false, TEXT("tls.EngineerLinesTest: found again"));
			Run()->MarkEntityFound(TEXT("tls.EngineerLinesTest"), EEntityFoundBy::Sighting);
			return 0.5f;
		}));
		S.Add(WaitIdle(Once));
		S.Add(CheckOrder(Once + TEXT(" (found again)"), TEXT("A1_EntitySeen_1 A1_EntityFound_1")));

		// --- One row of each prologue group said on the channel (in this map: each group once) ---
		AddPrologueGroup(S, PrologueGroups::Wake, 2, 1);
		AddPrologueGroup(S, PrologueGroups::Hologram, 1, 1);
		AddPrologueGroup(S, PrologueGroups::Hangar, 1, 1);
		AddPrologueGroup(S, PrologueGroups::Light, 2, 2);
		AddPrologueGroup(S, PrologueGroups::Hatch, 2, 1);
		AddPrologueGroup(S, PrologueGroups::Door, 2, 1);

		// --- Five battery levels: the line in full at the full battery's speed; at Silent the row dropped, the line said ---
		for (int32 I = 0; I < 5; ++I)
		{
			const double Battery = Batteries[I];
			const FName Row(BatteryRows[I]);
			const FString Label = FString::Printf(TEXT("battery %.2f"), Battery);
			S.Add(WaitIdle(Label));
			S.Add(Do(Label + TEXT(": push ") + Row.ToString(), [Battery, Row]()
			{
				MarkRows();
				Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
				Channel()->PushRow(Row, ContextAt(Battery));
				return 0.f;
			}));
			AddLineTypedOut(S, Label, Line(Row), Row, 1, FMath::IsNearlyEqual(Battery, 0.35) || Battery < 0.03 ? FString::Printf(TEXT("Engineer_Battery_%03d"), FMath::RoundToInt(Battery * 100.0)) : FString());
			S.Add(Do(Label + TEXT(": typing speed"), [Label]()
			{
				const double Seconds = GRun.Text->LastTypingSeconds;
				const double Rate = Seconds > 0.0 ? GRun.Text->StartedTexts.Last().Len() / Seconds : 0.0;
				const double Full = Channel()->GetCharsPerSecond(1.0);
				Check(Rate > Full * 0.75 && Rate < Full * 1.3, FString::Printf(TEXT("%s: typed at the full battery's speed (%.1f chars/s, %.1f at full)"), *Label, Rate, Full));
				return 0.f;
			}));
			if (Battery >= 0.03)
			{
				AddRowTypedOut(S, Label, Row, [Row, Battery]() { return HomeText(Row, ContextAt(Battery)); });
				S.Add(Do(Label + TEXT(": the row's level"), [Label, Battery]()
				{
					const ETextDegradation Level = Channel()->GetCurrentLevel();
					Check(Battery >= 0.70 ? Level == ETextDegradation::Full : Level != ETextDegradation::Full,
						FString::Printf(TEXT("%s: the home AI's row after it at %s"), *Label, LexToString(Level)));
					return 0.f;
				}));
			}
			S.Add(WaitIdle(Label));
			S.Add(CheckOrder(Label, Battery >= 0.03 ? Line(Row).ToString() + TEXT(" ") + Row.ToString() : Line(Row).ToString()));
		}

		// --- Hidden while Stopped, restarted after the Stop, suspended behind the pause menu ---
		const FString Sus = TEXT("Stop and menu");
		S.Add(WaitIdle(Sus));
		S.Add(Do(Sus + TEXT(": push"), []()
		{
			Home()->RestoreState(0.6, Home()->GetHomeSecondsElapsed());
			Channel()->PushRow(TEXT("A3_JumpReturn_1"), ContextAt(0.6));
			return 0.f;
		}));
		S.Add(WaitFor(Sus + TEXT(": the line being typed"), []() { const UTextChannelComponent* C = Channel(); return C && C->GetCurrentRowName() == Line(TEXT("A3_JumpReturn_1")) && C->GetVisibleText().Len() >= 12; }, 10.f, [Sus]()
		{
			TS()->Stop();
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = ChannelWidget();
			Check(!C->IsVisible() && C->GetVisibleText().IsEmpty() && Widget && !Widget->IsShown(), Sus + TEXT(": a Stop hides the engineer's line at once (channel and widget)"));
			return 1.0f;
		}));
		S.Add(Do(Sus + TEXT(": Resume"), [Sus]()
		{
			const UTextChannelComponent* C = Channel();
			Check(IsStopped() && !C->IsVisible() && C->GetCurrentRowName() == Line(TEXT("A3_JumpReturn_1")), Sus + TEXT(": 1 s Stopped, nothing shown, the line kept"));
			TS()->Resume(EStopExitReason::Resumed);
			Check(C->IsVisible() && C->GetVisibleText().Len() <= 1 && C->GetCurrentVoice() == ETextVoice::Engineer,
				FString::Printf(TEXT("%s: after the Stop the line restarts from its first character (\"%s\")"), *Sus, *C->GetVisibleText()));
			return 0.f;
		}));
		S.Add(WaitFor(Sus + TEXT(": typed again"), []() { const UTextChannelComponent* C = Channel(); return C && C->GetVisibleText().Len() >= 12; }, 10.f, [Sus]()
		{
			AStdPlayerController* SPC = Cast<AStdPlayerController>(PC());
			Check(SPC && SPC->PauseGame(), Sus + TEXT(": the pause menu opens during the line"));
			return 1.0f;
		}));
		S.Add(Do(Sus + TEXT(": close the menu"), [Sus]()
		{
			const UTextChannelComponent* C = Channel();
			Check(C->IsSuspended() && !C->IsVisible() && C->GetVisibleText().IsEmpty(), Sus + TEXT(": behind the pause menu the channel is suspended, nothing typed or shown"));
			AStdPlayerController* SPC = Cast<AStdPlayerController>(PC());
			return SPC && SPC->ResumeGame() ? 0.1f : -1.f;
		}));
		S.Add(Do(Sus + TEXT(": after the menu"), [Sus]()
		{
			const UTextChannelComponent* C = Channel();
			Check(!C->IsSuspended() && C->IsVisible() && C->GetCurrentVoice() == ETextVoice::Engineer && C->GetVisibleText().Len() < 12,
				FString::Printf(TEXT("%s: the menu closed, the line restarts from its first character (\"%s\")"), *Sus, *C->GetVisibleText()));
			return 0.f;
		}));
		S.Add(WaitIdle(Sus));

		// --- No line when its row is not said; behind the travel screen; the area entry label takes none ---
		S.Add(Do(TEXT("not said: FirstEnergy never banked, a row of another act"), []()
		{
			Check(!EverStarted(Line(TEXT("A1_FirstEnergy_1"))) && !EverStarted(TEXT("A1_FirstEnergy_1")) && !EverStarted(Line(TEXT("A3_EntityFound_1"))),
				TEXT("not said: no engineer line before A1_FirstEnergy_1 (never said) or A3_EntityFound_1 (another act)"));
			return 0.f;
		}));
		const FString Tr = TEXT("travel");
		S.Add(Do(Tr + TEXT(": push a row with a line before it"), []()
		{
			Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed());
			MarkRows();
			GRun.OldChannel = Channel();
			Channel()->PushRow(TEXT("A4_JumpReturn_1"), ContextAt(1.0));
			return 0.f;
		}));
		// After the gap that follows the last message: the line is on the channel (not waiting in the queue) when the travel starts.
		S.Add(WaitFor(Tr + TEXT(": the line on the channel"), []() { const UTextChannelComponent* C = Channel(); return C && C->GetCurrentRowName() == Line(TEXT("A4_JumpReturn_1")) && C->IsVisible(); }, 10.f,
			[Tr]()
		{
			GRun.TravelFrom = W();
			const bool bStarted = Run()->TravelToNextArea();
			const FString Label = Run()->GetLastTravelLabel().ToString();
			FTextChannelContext C;
			C.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
			C.BatteryFraction = Home()->GetBatteryFraction();
			const FString Want = HomeText(TEXT("A2_AreaEntry_1"), C);
			Check(bStarted && Label == Want, FString::Printf(TEXT("%s: the travel screen's label is the A2_AreaEntry_1 row alone, no engineer line in it: \"%s\""), *Tr, *Label));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitFor(Tr + TEXT(": the screen covered"), []()
		{
			const UStdFlowSubsystem* F = Flow();
			return !GRun.OldChannel.IsValid() || (F && F->GetCoveredSeconds() > 0.f);
		}, 30.f, [Tr]()
		{
			const UTextChannelComponent* Old = GRun.OldChannel.Get();
			Check(Old && Old->IsSuspended() && !Old->IsVisible() && Old->GetCurrentVoice() == ETextVoice::Engineer,
				FString::Printf(TEXT("%s: behind the travel screen the channel is suspended and the engineer's line hidden (%s)"), *Tr, Old ? TEXT("the old pawn's channel") : TEXT("gone")));
			return 0.f;
		}));
		S.Add(WaitFor(Tr + TEXT(": area 2"), []()
		{
			UWorld* World = W();
			if (!World || World == GRun.TravelFrom.Get())
			{
				return false;
			}
			if (UTextChannelComponent* C = ChannelOf(World); C && GRun.BoundChannel.Get() != C)
			{
				BindChannel(C);
			}
			return AreaReady(World);
		}, 120.f, []() { return 3.0f; }));
		S.Add(Do(Tr + TEXT(": nothing said for the label"), [Tr]()
		{
			Check(Act() == 2 && !EverStarted(Line(TEXT("A2_AreaEntry_1"))) && !EverStarted(TEXT("A2_AreaEntry_1")),
				FString::Printf(TEXT("%s: in area 2, the area entry row (a label) and its engineer line never said on the channel (rows since the travel: %s)"), *Tr, *RowsSinceMark()));
			return 0.f;
		}));
		return S;
	}

	// --- tls.SpendKindTest ---

	static ETextTrigger KindTrigger(EPaymentKind Kind) { return UHomeAIComponent::GetSpendTrigger(Kind); }

	/** A terminal of Kind spawned 3 m from the player at Angle degrees, in reach, with its device 30 m away. */
	static APowerTerminal* SpawnKind(EPaymentKind Kind, float Angle)
	{
		UWorld* World = W();
		APawn* P = Pawn();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Feet = P->GetNavAgentLocation();
		const FVector At = Feet + FRotator(0.f, Angle, 0.f).RotateVector(FVector(300.f, 0.f, 0.f));
		const FRotator Facing = (Feet - At).Rotation();
		const FVector Far = Feet + FVector(0.f, 0.f, -3000.f) + FRotator(0.f, Angle, 0.f).RotateVector(FVector(500.f, 0.f, 0.f));
		APowerTerminal* T = nullptr;
		switch (Kind)
		{
		case EPaymentKind::Log: T = World->SpawnActor<ALogTerminal>(ALogTerminal::StaticClass(), At, Facing, Params); break;
		case EPaymentKind::Twin: T = World->SpawnActor<ATwinAITerminal>(ATwinAITerminal::StaticClass(), At, Facing, Params); break;
		default: T = World->SpawnActor<APowerTerminal>(APowerTerminal::StaticClass(), At, Facing, Params); break;
		}
		if (!T)
		{
			return nullptr;
		}
		T->InteractRadius = 1000.f;
		GRun.Spawned.Add(T);
		APoweredDevice* Device = nullptr;
		if (Kind == EPaymentKind::Door)
		{
			Device = World->SpawnActor<ADoorDevice>(ADoorDevice::StaticClass(), Far, FRotator::ZeroRotator, Params);
		}
		else if (Kind == EPaymentKind::Lights || Kind == EPaymentKind::Beacon)
		{
			Device = World->SpawnActor<ALightGroupDevice>(ALightGroupDevice::StaticClass(), Far, FRotator::ZeroRotator, Params);
		}
		if (Device)
		{
			GRun.Spawned.Add(Device);
			T->Targets.Add(Device);
		}
		if (Kind == EPaymentKind::Beacon)
		{
			ABeaconBody* Body = World->SpawnActor<ABeaconBody>(ABeaconBody::StaticClass(), Far + FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, Params);
			if (Body)
			{
				Body->Device = Cast<ALightGroupDevice>(Device);
				GRun.Spawned.Add(Body);
			}
		}
		GRun.Terminals.FindOrAdd(Kind).Add(T);
		return T;
	}

	static APowerTerminal* TerminalOf(EPaymentKind Kind, int32 Index = 0)
	{
		const TArray<TWeakObjectPtr<APowerTerminal>>* List = GRun.Terminals.Find(Kind);
		return List && List->IsValidIndex(Index) ? (*List)[Index].Get() : nullptr;
	}

	/** One payment at the Index-th terminal of Kind; waits for the channel to be idle after it. */
	static void AddPay(TArray<FStep>& S, const FString& Label, EPaymentKind Kind, int32 Index = 0)
	{
		S.Add(WaitIdle(Label, 120.f));
		S.Add(Do(Label + TEXT(": pay"), [Label, Kind, Index]()
		{
			APowerTerminal* T = TerminalOf(Kind, Index);
			Energy()->AddBankedEnergy(T ? T->GetCost() : 1);
			const bool bPaid = T && T->TryActivate(Pawn());
			Check(bPaid && T->GetPaymentKind() == Kind, FString::Printf(TEXT("%s: paid at %s, its kind %s"), *Label, *GetNameSafe(T), T ? LexToString(T->GetPaymentKind()) : TEXT("-")));
			return bPaid ? 0.3f : -1.f;
		}));
	}

	static TArray<FStep> BuildSpendKindTest()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("setup: an area with the player"), []() { return W() && Act() > 0 && AAreaGenerator::IsAreaReady(W()) && Pawn() && Channel() && HomeAI(); }, 60.f, []()
		{
			NarrativeFixture::Install(W());
			BindChannel(Channel());
			GRun.SavedBanked = Energy()->GetBankedEnergy();
			GRun.bArchiveWasEmpty = !Run()->HasArchiveItems();
			GRun.Cleanup = []()
			{
				for (const TWeakObjectPtr<AActor>& Actor : GRun.Spawned)
				{
					if (AActor* A = Actor.Get())
					{
						A->Destroy();
					}
				}
				GRun.Spawned.Reset();
				if (UEnergySubsystem* E = Energy())
				{
					E->RestoreBankedEnergy(GRun.SavedBanked);
				}
				if (URunSubsystem* R = Run(); R && GRun.bArchiveWasEmpty)
				{
					R->ClearArchive();
				}
			};
			// Spawned away from the map's own terminals: every payment below is at one of these.
			SpawnKind(EPaymentKind::Door, 0.f);
			SpawnKind(EPaymentKind::Lights, 50.f);
			SpawnKind(EPaymentKind::Beacon, 100.f);
			SpawnKind(EPaymentKind::Log, 150.f);
			SpawnKind(EPaymentKind::Log, 200.f);
			SpawnKind(EPaymentKind::Twin, 250.f);
			SpawnKind(EPaymentKind::Other, 300.f);
			FString Kinds;
			for (const EPaymentKind K : { EPaymentKind::Door, EPaymentKind::Lights, EPaymentKind::Beacon, EPaymentKind::Log, EPaymentKind::Twin, EPaymentKind::Other })
			{
				const APowerTerminal* T = TerminalOf(K);
				Kinds += FString::Printf(TEXT("%s%s -> %s (%s)"), Kinds.IsEmpty() ? TEXT("") : TEXT(", "), LexToString(K), T ? LexToString(T->GetPaymentKind()) : TEXT("none"),
					LexToString(KindTrigger(K)));
			}
			bool bKinds = true;
			for (const EPaymentKind K : { EPaymentKind::Door, EPaymentKind::Lights, EPaymentKind::Beacon, EPaymentKind::Log, EPaymentKind::Twin, EPaymentKind::Other })
			{
				bKinds &= TerminalOf(K) && TerminalOf(K)->GetPaymentKind() == K;
			}
			Check(bKinds, FString::Printf(TEXT("setup: in act %d, one terminal of each kind of payment and its trigger: %s"), Act(), *Kinds));
			return 0.5f;
		}));

		auto Row = [](const TCHAR* Trigger, int32 Order) { return FString::Printf(TEXT("A%d_%s_%d"), Act(), Trigger, Order); };

		// --- Today's tables: no row of any kind: every payment says the act's Spend row, as before this brief ---
		// The area's own start lines first: the payments' rows are counted from an idle channel.
		S.Add(WaitIdle(TEXT("today: the area's start lines said"), 120.f));
		S.Add(Do(TEXT("today: mark"), []() { MarkRows(); return 0.f; }));
		AddPay(S, TEXT("today: a door"), EPaymentKind::Door);
		AddPay(S, TEXT("today: a group of lights"), EPaymentKind::Lights);
		AddPay(S, TEXT("today: a beacon"), EPaymentKind::Beacon);
		AddPay(S, TEXT("today: a log terminal"), EPaymentKind::Log, 0);
		AddPay(S, TEXT("today: the twin AI"), EPaymentKind::Twin);
		AddPay(S, TEXT("today: a terminal that powers nothing"), EPaymentKind::Other);
		S.Add(WaitIdle(TEXT("today"), 120.f));
		S.Add(Do(TEXT("today: the rows"), [Row]()
		{
			const FString Spend = Row(TEXT("Spend"), 1);
			const FString Want = FString::Printf(TEXT("%s %s %s %s %s %s"), *Spend, *Spend, *Spend, *Spend, *Spend, *Spend);
			Check(HomeRowsSinceMark() == Want, FString::Printf(TEXT("today: with no row of any kind every payment says %s, as before (%s)"), *Spend, *HomeRowsSinceMark()));
			const ALogTerminal* Log = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, 0));
			Check(Log && Log->IsUnlocked() && Log->GetEntries().Num() > 0, TEXT("today: the log terminal paid shows its entries, as before"));
			Check(RowsSinceMark().Contains(TEXT("T_1_1 T_1_2")), FString::Printf(TEXT("today: the twin AI's part 1 after its comment (%s)"), *RowsSinceMark()));
			return 0.f;
		}));

		// --- Rows of the kinds: Door 2, Log 2, Beacon 1, Twin 1, none for Lights; Spend 2; a line before the first SpendDoor row ---
		S.Add(Do(TEXT("kinds: fixture rows"), []()
		{
			const int32 A = Act();
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendDoor, A, 1);
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendDoor, A, 2);
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendLog, A, 1);
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendLog, A, 2);
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendBeacon, A, 1);
			NarrativeFixture::AddMessageRow(ETextTrigger::SpendTwin, A, 1);
			NarrativeFixture::AddMessageRow(ETextTrigger::Spend, A, 2);
			NarrativeFixture::SetEngineerLines(W(), { { FName(*FString::Printf(TEXT("A%d_SpendDoor_1"), A)), 1 } });
			Note(FString::Printf(TEXT("kinds: A%d_SpendDoor_1..2, A%d_SpendLog_1..2, A%d_SpendBeacon_1, A%d_SpendTwin_1, A%d_Spend_2 and a line before A%d_SpendDoor_1"), A, A, A, A, A, A));
			MarkRows();
			return 0.f;
		}));
		AddPay(S, TEXT("kinds: door 1"), EPaymentKind::Door);
		S.Add(WaitFor(TEXT("kinds: the engineer line before the first SpendDoor row"), [Row]() { return RowTypedOut(FName(*(TEXT("E_") + Row(TEXT("SpendDoor"), 1) + TEXT("_1")))); }, 30.f,
			[]() { return 0.f; }));
		AddBoxShot(S, TEXT("kinds: the engineer line before A_SpendDoor_1"), TEXT("Engineer_Before_SpendDoor_1"), true);
		AddPay(S, TEXT("kinds: door 2"), EPaymentKind::Door);
		AddPay(S, TEXT("kinds: log 1 (a second log terminal)"), EPaymentKind::Log, 1);
		AddPay(S, TEXT("kinds: door 3"), EPaymentKind::Door);
		AddPay(S, TEXT("kinds: lights (no row of its kind)"), EPaymentKind::Lights);
		AddPay(S, TEXT("kinds: beacon 1"), EPaymentKind::Beacon);
		AddPay(S, TEXT("kinds: beacon 2"), EPaymentKind::Beacon);
		AddPay(S, TEXT("kinds: the twin AI"), EPaymentKind::Twin);
		AddPay(S, TEXT("kinds: a terminal that powers nothing"), EPaymentKind::Other);
		AddPay(S, TEXT("kinds: lights again"), EPaymentKind::Lights);
		S.Add(WaitIdle(TEXT("kinds"), 120.f));
		S.Add(Do(TEXT("kinds: the rows"), [Row]()
		{
			const FString Want = FString::Printf(TEXT("E_%s_1 %s %s %s %s %s %s %s %s %s %s"), *Row(TEXT("SpendDoor"), 1), *Row(TEXT("SpendDoor"), 1), *Row(TEXT("SpendDoor"), 2),
				*Row(TEXT("SpendLog"), 1), *Row(TEXT("SpendDoor"), 2), *Row(TEXT("Spend"), 1), *Row(TEXT("SpendBeacon"), 1), *Row(TEXT("SpendBeacon"), 1), *Row(TEXT("SpendTwin"), 1),
				*Row(TEXT("Spend"), 2), *Row(TEXT("Spend"), 2));
			Check(HomeRowsSinceMark() == Want, FString::Printf(TEXT("kinds: each payment its kind's row, the next of its kind, then the last again; kinds counted on their own; Lights (no row) and a terminal that powers nothing on the Spend rows, counted as before; the engineer line at the first door payment only: %s (expected %s)"),
				*HomeRowsSinceMark(), *Want));
			const FString All = RowsSinceMark();
			const FString Twin = Row(TEXT("SpendTwin"), 1);
			const int32 At = All.Find(Twin);
			Check(At != INDEX_NONE && All.Mid(At).StartsWith(Twin + TEXT(" T_2_1 T_2_2")), FString::Printf(TEXT("kinds: the twin AI's part 2 follows its comment, as before (%s)"), *All));
			const ALogTerminal* Log = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, 1));
			Check(Log && Log->IsUnlocked() && Log->GetEntries().Num() > 0, TEXT("kinds: the second log terminal shows its entries, as before"));
			return 0.f;
		}));

		// --- A kind with no row in an act with no Spend row either: nothing, as before this brief ---
		S.Add(Do(TEXT("no Spend row: the act's Spend rows taken out of the fixture"), [Row]()
		{
			UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
			Table->RemoveRow(FName(*Row(TEXT("Spend"), 1)));
			Table->RemoveRow(FName(*Row(TEXT("Spend"), 2)));
			Check(UTextChannelComponent::FindRows(Table, ETextTrigger::Spend, Act()).IsEmpty(), FString::Printf(TEXT("no Spend row: the fixture %s has no Spend row of act %d now"), *GetNameSafe(Table), Act()));
			MarkRows();
			return 0.f;
		}));
		AddPay(S, TEXT("no Spend row: lights"), EPaymentKind::Lights);
		S.Add(Do(TEXT("no Spend row: wait"), []() { return 2.5f; }));
		S.Add(Do(TEXT("no Spend row: nothing said"), []()
		{
			Check(RowsSinceMark().IsEmpty() && ChannelIdle(), FString::Printf(TEXT("no Spend row: a kind with no row in an act with no Spend row says nothing, as before (%s)"), *RowsSinceMark()));
			return 0.f;
		}));

		// --- A second set: Lights, Beacon, Log and Twin with two rows each: own first row, second row, the last repeated ---
		S.Add(Do(TEXT("own rows: a second set of rows (two of each kind), counted from the start"), []()
		{
			const int32 A = Act();
			UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
			for (const ETextTrigger T : { ETextTrigger::SpendDoor, ETextTrigger::SpendLights, ETextTrigger::SpendBeacon, ETextTrigger::SpendLog, ETextTrigger::SpendTwin })
			{
				for (int32 Order = 1; Order <= 2; ++Order)
				{
					Table->RemoveRow(FName(*FString::Printf(TEXT("A%d_%s_%d"), A, LexToString(T), Order)));
				}
			}
			for (const ETextTrigger T : { ETextTrigger::SpendLights, ETextTrigger::SpendBeacon, ETextTrigger::SpendLog, ETextTrigger::SpendTwin })
			{
				NarrativeFixture::AddMessageRow(T, A, 1);
				NarrativeFixture::AddMessageRow(T, A, 2);
			}
			// The table given again, as a new map's channel gets it: every trigger counts from its first row.
			Channel()->SetMessageTable(Table);
			// The twin AI's parts start again (the archive is put back at the end). The log payments get a pair of their own, as
			// area 2's recipe places (two): the earlier phases' log terminals leave the map, so the pair is dealt on its own.
			Run()->ClearArchive();
			for (int32 I = 0; I < 2; ++I)
			{
				if (APowerTerminal* Old = TerminalOf(EPaymentKind::Log, I))
				{
					Old->Destroy();
				}
			}
			SpawnKind(EPaymentKind::Log, 25.f);
			SpawnKind(EPaymentKind::Log, 75.f);
			bool bRows = true;
			for (const ETextTrigger T : { ETextTrigger::SpendLights, ETextTrigger::SpendBeacon, ETextTrigger::SpendLog, ETextTrigger::SpendTwin })
			{
				bRows &= UTextChannelComponent::FindRows(Table, T, A).Num() == 2;
			}
			Check(bRows && UTextChannelComponent::FindRows(Table, ETextTrigger::SpendDoor, A).IsEmpty() && TerminalOf(EPaymentKind::Log, 3) && !TerminalOf(EPaymentKind::Log, 0) && !TerminalOf(EPaymentKind::Log, 1)
				&& !Run()->HasArchiveItems(),
				FString::Printf(TEXT("own rows: two rows each of SpendLights, SpendBeacon, SpendLog and SpendTwin in act %d, a pair of log terminals of its own, the archive empty"), A));
			MarkRows();
			return 0.f;
		}));
		// Three rounds, the kinds in a different order each round: each kind counts on its own.
		AddPay(S, TEXT("own rows: lights 1"), EPaymentKind::Lights);
		AddPay(S, TEXT("own rows: beacon 1"), EPaymentKind::Beacon);
		AddPay(S, TEXT("own rows: log 1"), EPaymentKind::Log, 2);
		AddPay(S, TEXT("own rows: twin 1"), EPaymentKind::Twin);
		AddPay(S, TEXT("own rows: log 2"), EPaymentKind::Log, 3);
		AddPay(S, TEXT("own rows: lights 2"), EPaymentKind::Lights);
		AddPay(S, TEXT("own rows: twin 2"), EPaymentKind::Twin);
		AddPay(S, TEXT("own rows: beacon 2"), EPaymentKind::Beacon);
		AddPay(S, TEXT("own rows: twin 3"), EPaymentKind::Twin);
		AddPay(S, TEXT("own rows: beacon 3"), EPaymentKind::Beacon);
		AddPay(S, TEXT("own rows: lights 3"), EPaymentKind::Lights);
		S.Add(Do(TEXT("own rows: the pair's entries, then both locked again for a new deal"), []()
		{
			const ALogTerminal* First = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, 2));
			const ALogTerminal* Second = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, 3));
			const auto Orders = [](const ALogTerminal* T) { return T ? FString::JoinBy(T->GetEntries(), TEXT(","), [](int32 O) { return FString::FromInt(O); }) : FString(TEXT("-")); };
			Check(First && Second && First->GetGroupIndex() == 0 && Second->GetGroupIndex() == 1 && First->GetEntries().Num() > 0 && Second->GetEntries().Num() > 0
				&& First->GetEntries().Num() + Second->GetEntries().Num() == TwinWriting::GetLogOrdersNewestFirst(UTheLastShiftSettings::Get()->LoadLogsTable()).Num(),
				FString::Printf(TEXT("own rows: the pair is dealt on its own, as in area 2: group 0 (%s) and group 1 (%s), every entry dealt, none empty"), *Orders(First), *Orders(Second)));
			for (int32 I = 2; I <= 3; ++I)
			{
				if (ALogTerminal* T = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, I)))
				{
					T->Relock();
				}
			}
			return 0.f;
		}));
		AddPay(S, TEXT("own rows: log 3 (the pair dealt again)"), EPaymentKind::Log, 2);
		S.Add(WaitIdle(TEXT("own rows"), 120.f));
		S.Add(Do(TEXT("own rows: the rows"), [Row]()
		{
			const FString Want = FString::Join(TArray<FString>{
				Row(TEXT("SpendLights"), 1), Row(TEXT("SpendBeacon"), 1), Row(TEXT("SpendLog"), 1), Row(TEXT("SpendTwin"), 1),
				Row(TEXT("SpendLog"), 2), Row(TEXT("SpendLights"), 2), Row(TEXT("SpendTwin"), 2), Row(TEXT("SpendBeacon"), 2),
				Row(TEXT("SpendTwin"), 2), Row(TEXT("SpendBeacon"), 2), Row(TEXT("SpendLights"), 2), Row(TEXT("SpendLog"), 2) }, TEXT(" "));
			Check(HomeRowsSinceMark() == Want, FString::Printf(TEXT("own rows: Lights, Beacon, Log and Twin each say their own first row, their second at a second payment and the second again at a third, each kind counting on its own: %s (expected %s)"),
				*HomeRowsSinceMark(), *Want));
			const FString All = RowsSinceMark();
			const FString Twin1 = Row(TEXT("SpendTwin"), 1);
			const FString Twin2 = Row(TEXT("SpendTwin"), 2);
			Check(All.Contains(Twin1 + TEXT(" T_1_1 T_1_2")) && All.Contains(Twin2 + TEXT(" T_2_1 T_2_2")) && All.Contains(Twin2 + TEXT(" T_3_1 T_3_2")),
				FString::Printf(TEXT("own rows: each twin AI payment's part follows its comment, as before (%s)"), *All));
			const ALogTerminal* Log = Cast<ALogTerminal>(TerminalOf(EPaymentKind::Log, 2));
			Check(Log && Log->IsUnlocked() && Log->GetGroupIndex() == 0 && Log->GetEntries().Num() > 0,
				FString::Printf(TEXT("own rows: the log terminal paid in the new deal shows its entries, as before (group %d, %d entries)"), Log ? Log->GetGroupIndex() : -1, Log ? Log->GetEntries().Num() : 0));
			return 0.f;
		}));
		return S;
	}
}

// The commands sit inside the namespace: a file-scope using directive would reach the next file of a unity build.
namespace EngineerLinesDebug
{

static FAutoConsoleCommandWithWorldAndArgs GTlsEngineerLinesTestCommand(
	TEXT("tls.EngineerLinesTest"),
	TEXT("tls.EngineerLinesTest [Quit] (fresh PIE or a package, MainMenu; starts a new run and clears the save; fixture rows): Brief 22, the engineer's lines before a row of the channel: a sequence's first and middle rows (two lines in order), a row that repeats, a row said once in a run, a row of each prologue group, five battery levels (Silent: the row dropped, the line said), Stop, pause menu and travel screen, no line when its row is not said or is a label. Shots to Saved/Screenshots/Brief22."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.EngineerLinesTest: a test is already running."));
			return;
		}
		Start(TEXT("tls.EngineerLinesTest"), Args.Num() > 0 && (Args[0] == TEXT("1") || Args[0].Equals(TEXT("Quit"), ESearchCase::IgnoreCase)), BuildEngineerLinesTest());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsSpendKindTestCommand(
	TEXT("tls.SpendKindTest"),
	TEXT("tls.SpendKindTest [Quit] (an area map, Area1_Hull; PIE or a package; fixture rows): Brief 22, the spending comment by kind of device: terminals spawned for a door, lights, a beacon, logs, the twin AI and one that powers nothing; today's tables (Spend for every payment), then rows of the kinds (each kind counted on its own, a kind with no row on the Spend rows, an engineer line before a kind's row at its first payment only, the twin AI's part after its comment; a second set with two rows each for Lights, Beacon, Log and Twin: own first row, second row, the second repeated)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.SpendKindTest: a test is already running."));
			return;
		}
		Start(TEXT("tls.SpendKindTest"), Args.Num() > 0 && (Args[0] == TEXT("1") || Args[0].Equals(TEXT("Quit"), ESearchCase::IgnoreCase)), BuildSpendKindTest());
	}));

} // namespace EngineerLinesDebug

#endif // !UE_BUILD_SHIPPING
