// SPDX-License-Identifier: MIT

// Brief 8, the other humanity's writing: tls.LogsTest (log terminals: payment, deal, surface and panel, state, readability
// shots), tls.TwinTest (the twin AI's terminal and voice, the battery, one queue, the suit's text colours, the home
// messages' timing), tls.ArchiveTest local|chain (the suit's archive: content, pause-menu entry and input; run state across
// maps, restarts, Continue and a save written before this brief) and tls.Archive.Store (stores log entries and twin AI parts
// without a terminal). The tests read their own fixture rows (Debug/NarrativeFixture), never the rows of Narrative/.
// Development builds only.

#include "Debug/NarrativeFixture.h"
#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Containers/Ticker.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LogTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "LidarScanComponent.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Run/ArchiveScreenWidget.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunPauseMenuWidget.h"
#include "Run/RunSubsystem.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TwinWriting.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UI/LogDisplayWidget.h"
#include "UI/StdBarcodeText.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace TwinWritingDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker (it keeps running while the menu pauses the world) ---

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
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		bool bQuit = false;
		FString ShotFolder = TEXT("Brief8");
		TFunction<void()> Cleanup;
		/** Called every core tick while the run is going (watchers). */
		TFunction<void()> Watch;
		FTSTicker::FDelegateHandle Ticker;
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

	static double Real() { return FPlatformTime::Seconds(); }

	static FStep Do(const FString& Label, TFunction<float()> Fn)
	{
		FStep Step;
		Step.Label = Label;
		Step.Do = MoveTemp(Fn);
		return Step;
	}

	static FStep Wait(float Seconds) { return Do(FString::Printf(TEXT("wait %.2f s"), Seconds), [Seconds]() { return Seconds; }); }

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
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static UTextChannelComponent* Channel() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr; }
	static UHomeAIComponent* HomeAI() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UHomeAIComponent>() : nullptr; }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static int32 Banked() { const UEnergySubsystem* E = Energy(); return E ? E->GetBankedEnergy() : -1; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static UTheLastShiftSettings* MutableSettings() { return GetMutableDefault<UTheLastShiftSettings>(); }

	template <class T>
	static T* FindWidget()
	{
		UWorld* World = W();
		for (TObjectIterator<T> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static UTextChannelWidget* ChannelWidget() { return FindWidget<UTextChannelWidget>(); }
	static UInteractPromptWidget* Prompt() { return FindWidget<UInteractPromptWidget>(); }

	static TArray<ALogTerminal*> LogTerminals()
	{
		TArray<ALogTerminal*> Found;
		UWorld* World = W();
		for (TActorIterator<ALogTerminal> It(World); World && It; ++It)
		{
			Found.Add(*It);
		}
		Found.Sort([](const ALogTerminal& A, const ALogTerminal& B) { return A.GetName() < B.GetName(); });
		return Found;
	}

	static ATwinAITerminal* TwinTerminal()
	{
		UWorld* World = W();
		for (TActorIterator<ATwinAITerminal> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static FString Join(const TArray<int32>& Values)
	{
		return FString::JoinBy(Values, TEXT(","), [](int32 V) { return FString::FromInt(V); });
	}

	static bool IsReady()
	{
		UWorld* World = W();
		if (!World || !StdPC() || !Pawn() || !Director() || !TS() || !Channel())
		{
			return false;
		}
		const UGameInstance* GI = World->GetGameInstance();
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		const AAreaGenerator* Generator = AAreaGenerator::Find(World);
		return (!Flow || !Flow->IsTransitioning()) && (!Generator || Generator->IsReady());
	}

	static void Shot(const FString& Label)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), GRun.ShotFolder, Label + TEXT(".png")));
		// With the UI: the suit panel, the text channel and the archive screens are UI.
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	static void SetFlashlight(bool bOn)
	{
		if (const APawn* P = Pawn())
		{
			TArray<USpotLightComponent*> Lights;
			P->GetComponents(Lights);
			for (USpotLightComponent* Light : Lights)
			{
				Light->SetVisibility(bOn);
			}
		}
	}

	/** The pawn at Distance in front of the terminal (its +X), looking at its surface's centre. */
	static void StandBefore(const AActor* Terminal, float Distance)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C || !Terminal)
		{
			return;
		}
		const FVector Forward = Terminal->GetActorForwardVector().GetSafeNormal2D();
		const FVector Target = Terminal->GetActorLocation() + Forward * Distance;
		P->SetActorLocation(FVector(Target.X, Target.Y, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		const UWidgetComponent* Screen = Terminal->FindComponentByClass<UWidgetComponent>();
		const FVector LookAt = Screen ? Screen->GetComponentLocation() : Terminal->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		const UCameraComponent* Camera = P->FindComponentByClass<UCameraComponent>();
		const FVector Eye = Camera ? Camera->GetComponentLocation() : P->GetActorLocation() + FVector(0.f, 0.f, 62.f);
		C->SetControlRotation((LookAt - Eye).Rotation());
	}

	static void Away(const AActor* Terminal, float Distance)
	{
		StandBefore(Terminal, Distance);
	}

	// --- Finish ---

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
		}
		if (AStdPlayerController* C = StdPC(); C && C->IsPausedByMenu())
		{
			C->ResumeGame();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 8 test"));
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		if (GRun.Watch)
		{
			GRun.Watch();
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

	static bool Begin(const FString& Name, bool bQuit)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.%s: a Brief 8 test (%s) is already running."), *Name, *GRun.Name);
			return false;
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuit = bQuit;
		return true;
	}

	static void Start()
	{
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *GRun.Name, GRun.Steps.Num());
	}

	// --- Pause menu helpers ---

	static UStdScreenWidget* TopScreen()
	{
		AStdPlayerController* C = StdPC();
		UStdPauseMenuWidget* Menu = C ? C->GetPauseMenu() : nullptr;
		return Menu ? Menu->GetTopScreen() : nullptr;
	}

	static URunPauseMenuWidget* GamePauseMenu()
	{
		const AStdPlayerController* C = StdPC();
		return C ? Cast<URunPauseMenuWidget>(C->GetPauseMenu()) : nullptr;
	}

	static bool IsArchiveEntryShown()
	{
		const URunPauseMenuWidget* Menu = GamePauseMenu();
		const UButton* Button = Menu ? Menu->GetArchiveButton() : nullptr;
		return Button && Button->GetVisibility() != ESlateVisibility::Collapsed;
	}

	static bool OpenMenu()
	{
		AStdPlayerController* C = StdPC();
		return C && (C->IsPausedByMenu() || C->PauseGame());
	}

	static bool CloseMenu()
	{
		AStdPlayerController* C = StdPC();
		return C && C->ResumeGame();
	}

	static bool ClickArchive()
	{
		URunPauseMenuWidget* Menu = GamePauseMenu();
		UButton* Button = Menu ? Menu->GetArchiveButton() : nullptr;
		if (!Button || Button->GetVisibility() == ESlateVisibility::Collapsed)
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		return true;
	}

	static UArchiveScreenWidget* ArchiveList() { return Cast<UArchiveScreenWidget>(TopScreen()); }
	static UArchiveItemWidget* ArchiveItem() { return Cast<UArchiveItemWidget>(TopScreen()); }

	static FString ListLabels() { return TimeStateDebug::DescribeChoiceList(W()); }

	static FString ExpectedLabels(const TArray<int32>& LogsNewestFirst, int32 Parts)
	{
		TArray<FString> Labels;
		for (const int32 Order : LogsNewestFirst)
		{
			Labels.Add(FString::Printf(TEXT("Log %d"), Order));
		}
		for (int32 Part = 1; Part <= Parts; ++Part)
		{
			Labels.Add(FString::Printf(TEXT("Transmission %d"), Part));
		}
		Labels.Add(TEXT("<back>"));
		return FString::Join(Labels, TEXT(" | "));
	}

	static bool HasBarcodeIn(const UUserWidget* Widget)
	{
		bool bFound = false;
		if (Widget && Widget->WidgetTree)
		{
			Widget->WidgetTree->ForEachWidget([&bFound](const UWidget* Child) { bFound |= Child->IsA<UStdBarcodeText>(); });
		}
		return bFound;
	}

	static bool SameColor(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 0.002f); }

	// --- State shared between steps ---

	struct FState
	{
		double Mark = 0.0;
		int32 Banked = 0;
		int32 Index = 0;
		int32 Translated = 0;
		int32 Jumps = 0;
		FString Row;
		TWeakObjectPtr<ALogTerminal> A;
		TWeakObjectPtr<ALogTerminal> B;
		ELogDisplay SavedDisplay = ELogDisplay::Surface;
		float SavedLogCps = 40.f;
		FSuitTextColors SavedColors;
		bool bFlashlight = false;
		/** Distinct (row, real time) of the channel's current message, in order, from the watcher. */
		TArray<TPair<FString, double>> Sequence;
		bool bTwinNeverAhead = true;
		bool bTwinLagExact = true;
		int32 TwinSamples = 0;
		double TwinStart = -1.0;
		double TwinShownAll = -1.0;
		double TwinTranslatedAll = -1.0;
		int32 TwinLength = 0;
		TimeStateDebug::FInputReport InputBefore;
		float CooldownBefore = 0.f;
		int32 BeatsBefore = 0;
		float BeatInBefore = 0.f;
		float CumulativeBefore = 0.f;
		bool bStoppedBefore = false;
		float CooldownAtClose = 0.f;
		int32 BeatsAtClose = 0;
		float BeatInAtClose = 0.f;
		float CumulativeAtClose = 0.f;
		FString OldSavePath;
	};
	static FState G;

	static void WatchChannel()
	{
		const UTextChannelComponent* C = Channel();
		const FString Now = C && C->IsBusy() && C->GetCurrentRowName() != NAME_None ? C->GetCurrentRowName().ToString() : FString(TEXT("-"));
		if (G.Sequence.IsEmpty() || G.Sequence.Last().Key != Now)
		{
			G.Sequence.Emplace(Now, Real());
		}
		if (C && C->GetCurrentVoice() == ETextVoice::Twin && C->IsVisible())
		{
			const int32 Len = C->GetCurrentText().Len();
			const int32 Shown = C->GetShownCells();
			const int32 Translated = C->GetTranslatedCells();
			++G.TwinSamples;
			G.bTwinNeverAhead &= Translated <= Shown;
			if (Shown < Len)
			{
				const int32 Lag = UTheLastShiftSettings::Get()->GetTranslationLagChars();
				G.bTwinLagExact &= Translated == FMath::Max(0, Shown - Lag) || Translated == FMath::Max(0, Shown - Lag - 1) || Translated == FMath::Max(0, Shown - Lag + 1);
			}
			if (G.TwinStart >= 0.0 && G.TwinShownAll < 0.0 && Shown >= Len)
			{
				G.TwinShownAll = Real();
			}
			if (G.TwinStart >= 0.0 && G.TwinTranslatedAll < 0.0 && Translated >= Len)
			{
				G.TwinTranslatedAll = Real();
			}
		}
	}

	static FString SequenceString()
	{
		return FString::JoinBy(G.Sequence, TEXT(" "), [](const TPair<FString, double>& P) { return P.Key; });
	}

	/** Row names in the watched sequence, without the gaps. */
	static TArray<FString> SequenceRows()
	{
		TArray<FString> Rows;
		for (const TPair<FString, double>& P : G.Sequence)
		{
			if (P.Key != TEXT("-"))
			{
				Rows.Add(P.Key);
			}
		}
		return Rows;
	}

	// =====================================================================================================
	// tls.LogsTest
	// =====================================================================================================

	static bool PayAt(ALogTerminal* Terminal)
	{
		if (!Terminal)
		{
			return false;
		}
		StandBefore(Terminal, 100.f);
		return Terminal->TryActivate(Pawn());
	}

	static void RelockAll()
	{
		for (ALogTerminal* T : LogTerminals())
		{
			T->Relock();
		}
	}

	static void AddDealPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("payment: refused with no energy, prompt and dark surface while locked"), []()
		{
			ALogTerminal* A = G.A.Get();
			Energy()->RestoreBankedEnergy(0);
			StandBefore(A, 100.f);
			return 0.2f;
		}));
		S.Add(Do(TEXT("payment: check the locked terminal"), []()
		{
			ALogTerminal* A = G.A.Get();
			const UInteractPromptWidget* P = Prompt();
			Check(P && P->GetMode() == EInteractPromptMode::Power && P->GetShownText().Contains(TEXT("Power — 1")),
				FString::Printf(TEXT("payment: locked, the terminal prompt shows its cost (\"%s\")"), P ? *P->GetShownText() : TEXT("no prompt")));
			const UWidgetComponent* Screen = A ? A->FindComponentByClass<UWidgetComponent>() : nullptr;
			Check(A && !A->IsUnlocked() && !A->IsEntryShown() && Screen && !Screen->IsVisible(), TEXT("payment: locked, its surface is dark (nothing shown)"));
			const bool bPaid = A && A->TryActivate(Pawn());
			Check(!bPaid && A && !A->IsUnlocked() && Banked() == 0, TEXT("payment: with no energy it is refused like any terminal (still locked, banked 0)"));
			Energy()->RestoreBankedEnergy(5);
			const bool bPaidNow = A && A->TryActivate(Pawn());
			Check(bPaidNow && A->IsUnlocked() && Banked() == 4, FString::Printf(TEXT("payment: with energy it unlocks and takes 1 (banked 5 -> %d)"), Banked()));
			return 0.2f;
		}));
		S.Add(Do(TEXT("deal: 8 entries, A paid first"), []()
		{
			ALogTerminal* A = G.A.Get();
			ALogTerminal* B = G.B.Get();
			Energy()->RestoreBankedEnergy(10);
			const bool bPaid = PayAt(B);
			Check(A && Join(A->GetEntries()) == TEXT("8,7,6,5"), FString::Printf(TEXT("deal: the first terminal paid (%s) shows 8,7,6,5 (got %s)"), *GetNameSafe(A), A ? *Join(A->GetEntries()) : TEXT("-")));
			Check(bPaid && B && Join(B->GetEntries()) == TEXT("4,3,2,1"), FString::Printf(TEXT("deal: the second (%s) shows 4,3,2,1 (got %s)"), *GetNameSafe(B), B ? *Join(B->GetEntries()) : TEXT("-")));
			RelockAll();
			const bool bB = PayAt(B);
			const bool bA = PayAt(A);
			Check(bA && bB && Join(B->GetEntries()) == TEXT("8,7,6,5") && Join(A->GetEntries()) == TEXT("4,3,2,1"),
				FString::Printf(TEXT("deal: B paid first gets 8,7,6,5 (%s) and A then 4,3,2,1 (%s): by payment order, not by place"), *Join(B->GetEntries()), *Join(A->GetEntries())));
			NarrativeFixture::SetLogCount(5);
			RelockAll();
			PayAt(A);
			PayAt(B);
			Check(Join(A->GetEntries()) == TEXT("5,4,3") && Join(B->GetEntries()) == TEXT("2,1"),
				FString::Printf(TEXT("deal: with five entries 5,4,3 (%s) and then 2,1 (%s)"), *Join(A->GetEntries()), *Join(B->GetEntries())));
			NarrativeFixture::SetLogCount(8);
			RelockAll();
			Run()->ClearArchive();
			return 0.2f;
		}));
	}

	static void AddSurfacePhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("surface: pay A, typing slowed for the all-barcode shot"), []()
		{
			// The first shot needs the entry before any character is translated: one character per second for that frame.
			MutableSettings()->LogCharsPerSecond = 1.f;
			ALogTerminal* A = G.A.Get();
			Energy()->RestoreBankedEnergy(3);
			G.Banked = 2;
			PayAt(A);
			StandBefore(A, 120.f);
			return 0.3f;
		}));
		S.Add(Do(TEXT("surface: all barcode"), []()
		{
			ALogTerminal* A = G.A.Get();
			const ULogDisplayWidget* Surface = A ? A->GetSurfaceWidget() : nullptr;
			const UStdBarcodeText* Barcode = Surface ? Surface->GetBarcode() : nullptr;
			const StdBarcode::FDrawCounts Counts = Barcode ? Barcode->GetDrawCounts() : StdBarcode::FDrawCounts();
			const UWidgetComponent* Screen = A ? A->FindComponentByClass<UWidgetComponent>() : nullptr;
			Check(A && A->IsEntryShown() && Screen && Screen->IsVisible() && A->GetCurrentOrder() == 8, FString::Printf(TEXT("surface: on unlock the first entry (log %d) is on the surface"), A ? A->GetCurrentOrder() : -1));
			Check(Barcode && Counts.Total() == A->GetCurrentLength() && Counts.Characters <= 1,
				FString::Printf(TEXT("surface: the whole entry is there as barcode from the start (%d cells: %d bars, %d characters, %d gaps)"), Counts.Total(), Counts.Bars, Counts.Characters, Counts.Gaps));
			Shot(TEXT("logs_surface_1_barcode"));
			// The long frame of the screenshot must pass before the timed restart below.
			return 1.f;
		}));
		S.Add(Do(TEXT("surface: restart the entry at the real speed"), []()
		{
			MutableSettings()->LogCharsPerSecond = G.SavedLogCps;
			ALogTerminal* A = G.A.Get();
			// Four Interacts walk back to the first entry and restart its translation.
			for (int32 I = 0; I < 4; ++I)
			{
				A->TryActivate(Pawn());
			}
			G.Mark = Real();
			Check(A->GetCurrentIndex() == 0 && A->GetTranslatedCells() == 0, FString::Printf(TEXT("surface: after the last entry the first again (index %d, log %d)"), A->GetCurrentIndex(), A->GetCurrentOrder()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("surface: half translated"), []() { const ALogTerminal* A = G.A.Get(); return A && A->GetTranslatedCells() * 2 >= A->GetCurrentLength(); }, 10.f,
			[]() { Shot(TEXT("logs_surface_2_half")); return 0.f; }));
		S.Add(WaitFor(TEXT("surface: fully translated"), []() { const ALogTerminal* A = G.A.Get(); return A && A->GetTranslatedCells() >= A->GetCurrentLength(); }, 15.f, []()
		{
			const ALogTerminal* A = G.A.Get();
			const double Took = Real() - G.Mark;
			const double Expected = A->GetCurrentLength() / UTheLastShiftSettings::Get()->GetLogCharsPerSecond();
			Check(FMath::Abs(Took - Expected) <= 0.25, FString::Printf(TEXT("surface: %d characters translated in %.2f s at %.0f chars/s (expected %.2f s)"), A->GetCurrentLength(), Took,
				UTheLastShiftSettings::Get()->GetLogCharsPerSecond(), Expected));
			const StdBarcode::FDrawCounts Counts = A->GetSurfaceWidget()->GetBarcode()->GetDrawCounts();
			Check(Counts.Bars == 0 && Counts.Characters + Counts.Gaps == A->GetCurrentLength(), FString::Printf(TEXT("surface: every cell is a character (%d characters, %d bars)"), Counts.Characters, Counts.Bars));
			Shot(TEXT("logs_surface_3_translated"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("surface: Interact walks the entries"), []()
		{
			ALogTerminal* A = G.A.Get();
			const UInteractPromptWidget* P = Prompt();
			StandBefore(A, 100.f);
			TArray<int32> Seen = { A->GetCurrentOrder() };
			for (int32 I = 0; I < 4; ++I)
			{
				A->TryActivate(Pawn());
				Seen.Add(A->GetCurrentOrder());
			}
			Check(Join(Seen) == TEXT("8,7,6,5,8"), FString::Printf(TEXT("surface: Interact shows the next entry, after the last the first again (%s)"), *Join(Seen)));
			Check(Banked() == G.Banked, FString::Printf(TEXT("surface: no energy spent after the first payment (banked %d)"), Banked()));
			return 0.2f;
		}));
		S.Add(Do(TEXT("surface: Read prompt"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			Check(P && P->GetMode() == EInteractPromptMode::Read && P->GetShownText().EndsWith(TEXT("] Read")), FString::Printf(TEXT("prompt: \"Read\" on an unlocked log terminal (\"%s\")"), P ? *P->GetShownText() : TEXT("-")));
			return 0.f;
		}));
	}

	static void AddPanelPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("panel: LogDisplay = Panel"), []()
		{
			MutableSettings()->LogDisplay = ELogDisplay::Panel;
			ALogTerminal* A = G.A.Get();
			StandBefore(A, 100.f);
			for (int32 I = 0; I < 8 && A->GetCurrentIndex() != 0; ++I)
			{
				A->TryActivate(Pawn());
			}
			// Four more walk the whole group back to its first entry and restart its translation.
			for (int32 I = 0; I < 4; ++I)
			{
				A->TryActivate(Pawn());
			}
			G.Mark = Real();
			return 0.2f;
		}));
		S.Add(Do(TEXT("panel: shown in the suit panel, not on the surface"), []()
		{
			ALogTerminal* A = G.A.Get();
			const ULogDisplayWidget* Panel = A->GetPanelWidget();
			const UWidgetComponent* Screen = A->FindComponentByClass<UWidgetComponent>();
			Check(Panel && Panel->IsInViewport() && Panel->GetVisibility() != ESlateVisibility::Collapsed && Screen && !Screen->IsVisible() && A->IsEntryShown(),
				TEXT("panel: the entry is in the suit panel and the surface is off"));
			Check(Panel && Panel->GetBarcode()->GetCellCount() == A->GetCurrentLength() && A->GetCurrentOrder() == 8, FString::Printf(TEXT("panel: the same entry (log %d)"), A->GetCurrentOrder()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("panel: fully translated at the same speed"), []() { const ALogTerminal* A = G.A.Get(); return A && A->GetTranslatedCells() >= A->GetCurrentLength(); }, 15.f, []()
		{
			const ALogTerminal* A = G.A.Get();
			const double Took = Real() - G.Mark;
			const double Expected = A->GetCurrentLength() / UTheLastShiftSettings::Get()->GetLogCharsPerSecond();
			Check(FMath::Abs(Took - Expected) <= 0.3, FString::Printf(TEXT("panel: translated in %.2f s (expected %.2f s)"), Took, Expected));
			Check(A->GetPanelWidget()->GetBarcode()->GetDrawCounts().Bars == 0, TEXT("panel: every cell a character"));
			Shot(TEXT("logs_panel"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("panel: Interact walks the entries"), []()
		{
			ALogTerminal* A = G.A.Get();
			TArray<int32> Seen = { A->GetCurrentOrder() };
			for (int32 I = 0; I < 4; ++I)
			{
				A->TryActivate(Pawn());
				Seen.Add(A->GetCurrentOrder());
			}
			Check(Join(Seen) == TEXT("8,7,6,5,8") && Banked() == G.Banked, FString::Printf(TEXT("panel: the same sequence (%s), no energy spent (banked %d)"), *Join(Seen), Banked()));
			return 0.f;
		}));
		S.Add(Do(TEXT("panel: leave the terminal"), []()
		{
			ALogTerminal* A = G.A.Get();
			A->TryActivate(Pawn()); // entry 7
			G.Index = A->GetCurrentIndex();
			Away(A, 900.f);
			return 0.5f;
		}));
		S.Add(Do(TEXT("panel: away and back"), []()
		{
			ALogTerminal* A = G.A.Get();
			const ULogDisplayWidget* Panel = A->GetPanelWidget();
			Check(!A->IsEntryShown() && Panel && Panel->GetVisibility() == ESlateVisibility::Collapsed, TEXT("panel: hidden away from the terminal"));
			StandBefore(A, 100.f);
			return 0.3f;
		}));
		S.Add(Do(TEXT("panel: back at the terminal"), []()
		{
			ALogTerminal* A = G.A.Get();
			Check(A->IsEntryShown() && A->GetCurrentIndex() == G.Index, FString::Printf(TEXT("panel: coming back shows the entry it was on (index %d, log %d)"), A->GetCurrentIndex(), A->GetCurrentOrder()));
			MutableSettings()->LogDisplay = ELogDisplay::Surface;
			return 0.3f;
		}));
	}

	static void AddStatePhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("state: Stop"), []()
		{
			ALogTerminal* A = G.A.Get();
			StandBefore(A, 120.f);
			A->TryActivate(Pawn());
			G.Index = A->GetCurrentIndex();
			TS()->Toggle();
			return 0.5f;
		}));
		S.Add(Do(TEXT("state: hidden while Stopped"), []()
		{
			ALogTerminal* A = G.A.Get();
			const UWidgetComponent* Screen = A->FindComponentByClass<UWidgetComponent>();
			Check(IsStopped() && !A->IsEntryShown() && !Screen->IsVisible(), TEXT("state: a Stop hides the entry"));
			G.Translated = A->GetTranslatedCells();
			return 1.f;
		}));
		S.Add(Do(TEXT("state: typing stands still while Stopped, then Resume"), []()
		{
			ALogTerminal* A = G.A.Get();
			Check(A->GetTranslatedCells() == G.Translated, FString::Printf(TEXT("state: no typing while hidden (%d -> %d)"), G.Translated, A->GetTranslatedCells()));
			TS()->Toggle();
			return 0.3f;
		}));
		S.Add(Do(TEXT("state: shown again after the Resume"), []()
		{
			ALogTerminal* A = G.A.Get();
			const UWidgetComponent* Screen = A->FindComponentByClass<UWidgetComponent>();
			Check(!IsStopped() && A->IsEntryShown() && Screen->IsVisible() && A->GetCurrentIndex() == G.Index && A->GetTranslatedCells() >= G.Translated,
				FString::Printf(TEXT("state: the Resume shows the same entry again (index %d, translated %d -> %d)"), A->GetCurrentIndex(), G.Translated, A->GetTranslatedCells()));
			// A time jump: a Stop that ends with a contact of response Jump.
			G.Jumps = Home()->GetJumpCount();
			TS()->Toggle();
			return 0.3f;
		}));
		S.Add(Do(TEXT("state: time jump"), []()
		{
			Director()->ForceExit(EStopExitReason::Jumped);
			return 0.3f;
		}));
		S.Add(Do(TEXT("state: still unlocked after the jump"), []()
		{
			ALogTerminal* A = G.A.Get();
			Check(Home()->GetJumpCount() == G.Jumps + 1 && !IsStopped(), FString::Printf(TEXT("state: a time jump happened (jumps %d -> %d)"), G.Jumps, Home()->GetJumpCount()));
			Check(A->IsUnlocked() && A->IsEntryShown() && A->GetCurrentIndex() == G.Index, TEXT("state: a time jump leaves the terminal unlocked, on its entry"));
			A->ShowNextEntry();
			return 0.5f;
		}));
		S.Add(Do(TEXT("state: pause menu during the typing"), []()
		{
			ALogTerminal* A = G.A.Get();
			G.Translated = A->GetTranslatedCells();
			Check(OpenMenu(), TEXT("state: pause menu opened"));
			return 2.f;
		}));
		S.Add(Do(TEXT("state: typing frozen behind the menu"), []()
		{
			ALogTerminal* A = G.A.Get();
			const int32 Now = A->GetTranslatedCells();
			Check(FMath::Abs(Now - G.Translated) <= 1 && Now < A->GetCurrentLength(), FString::Printf(TEXT("state: 2 s of pause menu, translated %d -> %d"), G.Translated, Now));
			G.Translated = Now;
			CloseMenu();
			return 0.5f;
		}));
		S.Add(Do(TEXT("state: typing continues after the menu"), []()
		{
			ALogTerminal* A = G.A.Get();
			Check(A->GetTranslatedCells() > G.Translated, FString::Printf(TEXT("state: typing continues from where it was (%d -> %d)"), G.Translated, A->GetTranslatedCells()));
			G.Index = A->GetCurrentIndex();
			Away(A, 1200.f);
			return 1.f;
		}));
		S.Add(Do(TEXT("state: away and back (surface)"), []()
		{
			ALogTerminal* A = G.A.Get();
			StandBefore(A, 100.f);
			Check(A->IsUnlocked() && A->GetCurrentIndex() == G.Index, FString::Printf(TEXT("state: leaving the terminal and coming back shows the entry it was on (log %d)"), A->GetCurrentOrder()));
			return 0.f;
		}));
	}

	static void AddReadabilityPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("readability: flashlight on, 1 m"), []()
		{
			ALogTerminal* A = G.A.Get();
			MutableSettings()->LogDisplay = ELogDisplay::Surface;
			SetFlashlight(true);
			StandBefore(A, 100.f);
			return 0.2f;
		}));
		S.Add(WaitFor(TEXT("readability: entry translated"), []() { const ALogTerminal* A = G.A.Get(); return A && A->GetTranslatedCells() >= A->GetCurrentLength(); }, 15.f,
			[]() { Shot(TEXT("logs_readability_1m_flashlight")); return 0.4f; }));
		S.Add(Do(TEXT("readability: 2 m"), []() { StandBefore(G.A.Get(), 200.f); return 0.4f; }));
		S.Add(Do(TEXT("readability: shot at 2 m"), []() { Shot(TEXT("logs_readability_2m_flashlight")); return 0.4f; }));
		S.Add(Do(TEXT("readability: flashlight off, 1 m"), []() { SetFlashlight(false); StandBefore(G.A.Get(), 100.f); return 0.4f; }));
		S.Add(Do(TEXT("readability: shot at 1 m without the flashlight"), []() { Shot(TEXT("logs_readability_1m_dark")); return 0.4f; }));
		S.Add(Do(TEXT("readability: restore the flashlight"), []() { SetFlashlight(G.bFlashlight); return 0.f; }));
	}

	static void StartLogsTest(bool bQuit)
	{
		if (!Begin(TEXT("LogsTest"), bQuit))
		{
			return;
		}
		G = FState();
		GRun.Cleanup = []()
		{
			MutableSettings()->LogDisplay = G.SavedDisplay;
			MutableSettings()->LogCharsPerSecond = G.SavedLogCps;
			NarrativeFixture::Restore(W());
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the map is playable"), []() { return IsReady(); }, 90.f, []()
		{
			G.SavedDisplay = UTheLastShiftSettings::Get()->LogDisplay;
			G.SavedLogCps = UTheLastShiftSettings::Get()->LogCharsPerSecond;
			G.bFlashlight = false;
			if (const APawn* P = Pawn())
			{
				TArray<USpotLightComponent*> Lights;
				P->GetComponents(Lights);
				G.bFlashlight = Lights.Num() > 0 && Lights[0]->IsVisible();
			}
			MutableSettings()->LogDisplay = ELogDisplay::Surface;
			NarrativeFixture::Install(W());
			NarrativeFixture::SetLogCount(8);
			const TArray<ALogTerminal*> Terminals = LogTerminals();
			Check(Terminals.Num() == 2, FString::Printf(TEXT("the map holds two log terminals (%d)"), Terminals.Num()));
			if (Terminals.Num() < 2 || IsStopped())
			{
				return -1.f;
			}
			G.A = Terminals[0];
			G.B = Terminals[1];
			RelockAll();
			Run()->ClearArchive();
			Channel()->ClearAll();
			Note(FString::Printf(TEXT("terminals A=%s at %s, B=%s at %s; %s"), *Terminals[0]->GetName(), *Terminals[0]->GetActorLocation().ToCompactString(),
				*Terminals[1]->GetName(), *Terminals[1]->GetActorLocation().ToCompactString(), *GetNameSafe(UTheLastShiftSettings::Get()->LoadLogsTable())));
			return 0.2f;
		}));
		AddDealPhase(S);
		AddSurfacePhase(S);
		AddPanelPhase(S);
		AddStatePhase(S);
		AddReadabilityPhase(S);
		Start();
	}

	// =====================================================================================================
	// tls.TwinTest
	// =====================================================================================================

	static void AddAct0SpendRow()
	{
		// TestLidar is outside the run (act 0), where the real table has no Spend row: the fixture gets one for this test.
		UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
		if (Table && !Table->GetRowMap().Contains(TEXT("A0_Spend_1")))
		{
			FAIMessageRow Row;
			Row.Trigger = ETextTrigger::Spend;
			Row.Act = 0;
			Row.Order = 1;
			Row.Text = FText::FromString(TEXT("Fixture spend line of act 0: one unit spent at a terminal, battery {battery}."));
			Table->AddRow(TEXT("A0_Spend_1"), Row);
		}
	}

	static void ResetTwinWatch()
	{
		G.Sequence.Reset();
		G.bTwinNeverAhead = true;
		G.bTwinLagExact = true;
		G.TwinSamples = 0;
		G.TwinStart = Real();
		G.TwinShownAll = -1.0;
		G.TwinTranslatedAll = -1.0;
	}

	static bool ChannelIdle() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }

	static FString TwinRows(int32 Part)
	{
		return FString::JoinBy(TwinWriting::GetTwinPartRows(UTheLastShiftSettings::Get()->LoadTwinAITable(), Part), TEXT(" "), [](const FName& N) { return N.ToString(); });
	}

	static void AddPartsPhase(TArray<FStep>& S)
	{
		for (int32 Part = 1; Part <= 3; ++Part)
		{
			S.Add(Do(FString::Printf(TEXT("parts: payment %d"), Part), [Part]()
			{
				ATwinAITerminal* T = TwinTerminal();
				StandBefore(T, 100.f);
				ResetTwinWatch();
				G.Banked = Banked();
				const bool bPaid = T->TryActivate(Pawn());
				Check(bPaid && Banked() == G.Banked - 1 && T->GetLastPlayedPart() == Part, FString::Printf(TEXT("parts: payment %d plays part %d (banked %d -> %d)"), Part, T->GetLastPlayedPart(), G.Banked, Banked()));
				Check(Run()->GetArchive().TwinPartsPlayed == Part && Channel()->IsBusy(),
					FString::Printf(TEXT("parts: part %d stored in the archive at the payment, while its messages are still to be typed"), Part));
				if (Part == 1)
				{
					Shot(TEXT("twin_0_payment"));
				}
				return 0.f;
			}));
			if (Part == 1)
			{
				S.Add(WaitFor(TEXT("voice: a twin message is half typed"), []()
				{
					const UTextChannelComponent* C = Channel();
					return C && C->GetCurrentVoice() == ETextVoice::Twin && C->GetShownCells() * 2 >= C->GetCurrentText().Len();
				}, 15.f, []()
				{
					const UTextChannelWidget* Widget = ChannelWidget();
					const UStdBarcodeText* Barcode = Widget ? Widget->GetTwinBarcode() : nullptr;
					const FSuitTextColors& Colors = UTheLastShiftSettings::GetTextColors();
					Check(Widget && Widget->IsShowingTwin() && Barcode && SameColor(Barcode->BarColor, Colors.TwinAI) && SameColor(Barcode->TextColor, Colors.TwinAI),
						TEXT("voice: a twin message is drawn as barcode text in the twin colour (bars and characters)"));
					const UTextChannelComponent* C = Channel();
					Check(Barcode && Barcode->GetShownCells() == C->GetShownCells() && Barcode->GetTranslatedCells() == C->GetTranslatedCells(),
						FString::Printf(TEXT("voice: the widget draws the channel's counts (%d shown, %d translated)"), C->GetShownCells(), C->GetTranslatedCells()));
					Shot(TEXT("twin_1_mid"));
					return 0.f;
				}));
				S.Add(WaitFor(TEXT("voice: the twin message fully translated"), []()
				{
					const UTextChannelComponent* C = Channel();
					return C && C->GetCurrentVoice() == ETextVoice::Twin && C->GetTranslatedCells() >= C->GetCurrentText().Len();
				}, 15.f, []() { Shot(TEXT("twin_2_done")); return 0.f; }));
			}
			S.Add(WaitFor(FString::Printf(TEXT("parts: part %d said"), Part), []() { return ChannelIdle(); }, 30.f, [Part]()
			{
				const FString Expected = FString::Printf(TEXT("A0_Spend_1 %s"), *TwinRows(Part));
				const FString Got = FString::Join(SequenceRows(), TEXT(" "));
				Check(Got == Expected, FString::Printf(TEXT("parts: the spending comment, then part %d's rows in order, one at a time (%s; sequence %s)"), Part, *Got, *SequenceString()));
				Check(G.bTwinNeverAhead && G.TwinSamples > 0, FString::Printf(TEXT("voice: the translation never ahead of the barcodes (%d samples)"), G.TwinSamples));
				Check(G.bTwinLagExact, FString::Printf(TEXT("voice: the translation %d characters behind while the barcodes are typed"), UTheLastShiftSettings::Get()->GetTranslationLagChars()));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("parts: a fourth payment"), []()
		{
			ATwinAITerminal* T = TwinTerminal();
			StandBefore(T, 100.f);
			G.Banked = Banked();
			const bool bPaid = T->TryActivate(Pawn());
			Check(!bPaid && Banked() == G.Banked && Run()->GetArchive().TwinPartsPlayed == 3 && !T->OffersPayment(),
				FString::Printf(TEXT("parts: a fourth payment is not taken, no energy spent (banked %d -> %d)"), G.Banked, Banked()));
			return 0.3f;
		}));
		S.Add(Do(TEXT("parts: no prompt offers it"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			Check(P && P->GetMode() == EInteractPromptMode::Hidden, FString::Printf(TEXT("parts: no prompt offers it (\"%s\")"), P ? *P->GetShownText() : TEXT("-")));
			Check(ChannelIdle(), TEXT("parts: nothing queued by the refused payment"));
			return 0.f;
		}));
	}

	static void AddBatteryPhase(TArray<FStep>& S)
	{
		for (const double Battery : { 0.35, 0.02 })
		{
			S.Add(Do(FString::Printf(TEXT("battery %.2f: home, then twin"), Battery), [Battery]()
			{
				Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
				Channel()->ClearAll();
				ResetTwinWatch();
				const bool bHome = Channel()->PushTrigger(ETextTrigger::Spend, 0, HomeAI()->MakeContext());
				const int32 Twin = Channel()->PushTwinPart(1);
				Note(FString::Printf(TEXT("battery %.2f: home pushed %d, twin rows pushed %d"), Battery, bHome ? 1 : 0, Twin));
				return 0.f;
			}));
			S.Add(WaitFor(FString::Printf(TEXT("battery %.2f: the first twin row starts"), Battery), []()
			{
				const UTextChannelComponent* C = Channel();
				return C && C->GetCurrentVoice() == ETextVoice::Twin;
			}, 20.f, [Battery]()
			{
				const UTextChannelComponent* C = Channel();
				const FTwinAIRow* Row = UTheLastShiftSettings::Get()->LoadTwinAITable()->FindRow<FTwinAIRow>(C->GetCurrentRowName(), TEXT("Test"), false);
				Check(Row && C->GetCurrentText() == Row->Text.ToString() && C->GetCurrentLevel() == ETextDegradation::Full,
					FString::Printf(TEXT("battery %.2f: the twin message is shown in full (%d characters, level %s)"), Battery, C->GetCurrentText().Len(), LexToString(C->GetCurrentLevel())));
				G.TwinStart = Real();
				G.TwinTranslatedAll = -1.0;
				G.TwinLength = C->GetCurrentText().Len();
				return 0.f;
			}));
			S.Add(WaitFor(FString::Printf(TEXT("battery %.2f: twin translated"), Battery), []() { return G.TwinTranslatedAll >= 0.0; }, 15.f, [Battery]()
			{
				const double Took = G.TwinTranslatedAll - G.TwinStart;
				const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
				const double Expected = (G.TwinLength + Settings->GetTranslationLagChars()) / Settings->GetTwinCharsPerSecond();
				Check(FMath::Abs(Took - Expected) <= 0.2, FString::Printf(TEXT("battery %.2f: at the same speed (%.2f s for %d characters and the lag, expected %.2f s)"), Battery, Took, G.TwinLength, Expected));
				return 0.f;
			}));
			S.Add(WaitFor(FString::Printf(TEXT("battery %.2f: channel idle"), Battery), []() { return ChannelIdle(); }, 30.f, [Battery]()
			{
				const TArray<FString> Rows = SequenceRows();
				if (Battery > 0.1)
				{
					Check(Rows.Num() >= 1 && Rows[0] == TEXT("A0_Spend_1"), FString::Printf(TEXT("battery %.2f: the home message was shown (%s)"), Battery, *SequenceString()));
				}
				else
				{
					Check(!Rows.Contains(TEXT("A0_Spend_1")), FString::Printf(TEXT("battery %.2f: the home message is not shown at Silent (%s)"), Battery, *SequenceString()));
				}
				Check(Rows.Contains(TEXT("T_1_1")) && Rows.Contains(TEXT("T_1_2")), FString::Printf(TEXT("battery %.2f: both twin rows shown, never dropped"), Battery));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("battery: the home level at 0.35"), []()
		{
			const UTextChannelComponent* C = Channel();
			Check(C->GetLevelForBattery(0.35) == ETextDegradation::Abbreviated && C->GetLevelForBattery(0.02) == ETextDegradation::Silent,
				TEXT("battery: a home message is Abbreviated at 0.35 and Silent at 0.02"));
			Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed());
			return 0.f;
		}));
	}

	static void AddQueuePhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("queue: a twin message, then a Stop"), []()
		{
			Channel()->ClearAll();
			ResetTwinWatch();
			Channel()->PushTwinPart(2);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("queue: 20 cells typed"), []() { const UTextChannelComponent* C = Channel(); return C && C->GetShownCells() >= 20; }, 10.f, []()
		{
			G.Row = Channel()->GetCurrentRowName().ToString();
			TS()->Toggle();
			return 0.3f;
		}));
		S.Add(Do(TEXT("queue: hidden while Stopped"), []()
		{
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = ChannelWidget();
			Check(IsStopped() && C->IsSuspended() && !C->IsVisible() && Widget && !Widget->IsShown(), TEXT("queue: a Stop during a twin message hides it"));
			TS()->Toggle();
			return 0.f;
		}));
		S.Add(Do(TEXT("queue: the Resume restarts it"), []()
		{
			const UTextChannelComponent* C = Channel();
			Check(!IsStopped() && C->GetCurrentRowName().ToString() == G.Row && C->GetShownCells() <= 1 && C->GetTranslatedCells() == 0,
				FString::Printf(TEXT("queue: the Resume restarts %s from its first character (%d shown)"), *G.Row, C->GetShownCells()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("queue: idle"), []() { return ChannelIdle(); }, 30.f));
	}

	static void AddColorsPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("colors: a log surface and a twin message on screen"), []()
		{
			G.SavedColors = UTheLastShiftSettings::GetTextColors();
			ALogTerminal* A = LogTerminals().Num() > 0 ? LogTerminals()[0] : nullptr;
			if (A && !A->IsUnlocked())
			{
				Energy()->RestoreBankedEnergy(FMath::Max(Banked(), 1));
				StandBefore(A, 100.f);
				A->TryActivate(Pawn());
			}
			Channel()->ClearAll();
			Channel()->PushTwinPart(3);
			Channel()->PushTwinPart(3);
			return 0.5f;
		}));
		S.Add(Do(TEXT("colors: change each colour"), []()
		{
			struct FReaders { FLinearColor Home, TwinBars, TwinText, LogBars, LogText; };
			const auto Read = []()
			{
				FReaders R;
				const UTextChannelWidget* Widget = ChannelWidget();
				R.Home = Widget && Widget->GetHomeTextBlock() ? Widget->GetHomeTextBlock()->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Black;
				R.TwinBars = Widget && Widget->GetTwinBarcode() ? Widget->GetTwinBarcode()->BarColor : FLinearColor::Black;
				R.TwinText = Widget && Widget->GetTwinBarcode() ? Widget->GetTwinBarcode()->TextColor : FLinearColor::Black;
				const ALogTerminal* A = LogTerminals().Num() > 0 ? LogTerminals()[0] : nullptr;
				const UStdBarcodeText* Log = A && A->GetSurfaceWidget() ? A->GetSurfaceWidget()->GetBarcode() : nullptr;
				R.LogBars = Log ? Log->BarColor : FLinearColor::Black;
				R.LogText = Log ? Log->TextColor : FLinearColor::Black;
				return R;
			};
			const FReaders Base = Read();
			const FSuitTextColors& C = UTheLastShiftSettings::GetTextColors();
			Check(SameColor(Base.Home, C.HomeAI) && SameColor(Base.TwinBars, C.TwinAI) && SameColor(Base.TwinText, C.TwinAI) && SameColor(Base.LogBars, C.LogTranslation)
				&& SameColor(Base.LogText, C.LogTranslation), TEXT("colors: every reader shows the ini colour of its voice"));
			Check(!SameColor(C.TwinAI, UTheLastShiftSettings::GetScanColors().Scan) && !SameColor(C.HomeAI, UTheLastShiftSettings::GetScanColors().Scan)
				&& !SameColor(C.LogTranslation, UTheLastShiftSettings::GetScanColors().Scan), TEXT("colors: none of the three is the scan colour"));
			Note(FString::Printf(TEXT("colors: home %s, twin %s, log %s"), *C.HomeAI.ToString(), *C.TwinAI.ToString(), *C.LogTranslation.ToString()));
			return 0.f;
		}));
		// One colour at a time, as if it had been edited in the ini; the others must not move.
		struct FCase { const TCHAR* Name; FLinearColor FSuitTextColors::* Member; FLinearColor Value; };
		static const FCase Cases[] = {
			{ TEXT("LogTranslation"), &FSuitTextColors::LogTranslation, FLinearColor(1.f, 0.f, 1.f, 1.f) },
			{ TEXT("TwinAI"), &FSuitTextColors::TwinAI, FLinearColor(0.f, 1.f, 1.f, 1.f) },
			{ TEXT("HomeAI"), &FSuitTextColors::HomeAI, FLinearColor(0.2f, 1.f, 0.2f, 1.f) },
		};
		for (const FCase& Case : Cases)
		{
			S.Add(Do(FString::Printf(TEXT("colors: %s changed"), Case.Name), [Case]()
			{
				MutableSettings()->TextColors = G.SavedColors;
				MutableSettings()->TextColors.*Case.Member = Case.Value;
				if (FCString::Strcmp(Case.Name, TEXT("HomeAI")) == 0)
				{
					// The home colour is drawn by a home message.
					Channel()->ClearAll();
					Channel()->PushText(TEXT("A neutral home test line of forty chars."), HomeAI()->MakeContext());
				}
				else if (!Channel()->IsBusy())
				{
					Channel()->PushTwinPart(3);
				}
				return 0.4f;
			}));
			S.Add(Do(FString::Printf(TEXT("colors: %s readers"), Case.Name), [Case]()
			{
				const UTextChannelWidget* Widget = ChannelWidget();
				const FLinearColor Home = Widget->GetHomeTextBlock()->GetColorAndOpacity().GetSpecifiedColor();
				const FLinearColor TwinBars = Widget->GetTwinBarcode()->BarColor;
				const FLinearColor TwinText = Widget->GetTwinBarcode()->TextColor;
				const ALogTerminal* A = LogTerminals()[0];
				const UStdBarcodeText* Log = A->GetSurfaceWidget()->GetBarcode();
				// The edited voice shows the new colour on its text and its bars; no other reader takes it.
				const bool bIsLog = FCString::Strcmp(Case.Name, TEXT("LogTranslation")) == 0;
				const bool bIsTwin = FCString::Strcmp(Case.Name, TEXT("TwinAI")) == 0;
				const bool bIsHome = FCString::Strcmp(Case.Name, TEXT("HomeAI")) == 0;
				const bool bLog = SameColor(Log->BarColor, Case.Value) == bIsLog && SameColor(Log->TextColor, Case.Value) == bIsLog;
				const bool bTwin = SameColor(TwinBars, Case.Value) == bIsTwin && SameColor(TwinText, Case.Value) == bIsTwin;
				const bool bHome = SameColor(Home, Case.Value) == bIsHome;
				Check(bLog && bTwin && bHome, FString::Printf(TEXT("colors: %s edited: that text and its bars change, nothing else (log %s/%s, twin %s/%s, home %s)"), Case.Name,
					*Log->BarColor.ToString(), *Log->TextColor.ToString(), *TwinBars.ToString(), *TwinText.ToString(), *Home.ToString()));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("colors: restore"), []()
		{
			MutableSettings()->TextColors = G.SavedColors;
			Channel()->ClearAll();
			return 0.2f;
		}));
	}

	static void AddHomeTimingPhase(TArray<FStep>& S)
	{
		for (const double Battery : { 1.0, 0.03 })
		{
			S.Add(Do(FString::Printf(TEXT("home timing: 40 characters at battery %.2f"), Battery), [Battery]()
			{
				Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
				Channel()->ClearAll();
				G.Sequence.Reset();
				FTextChannelContext Context = HomeAI()->MakeContext();
				Channel()->PushText(TEXT("abcdefghij abcdefghij abcdefghij abcdefg"), Context);
				G.Mark = Real();
				G.Row = Channel()->GetCurrentText();
				return 0.f;
			}));
			S.Add(WaitFor(FString::Printf(TEXT("home timing %.2f: typed"), Battery), []()
			{
				const UTextChannelComponent* C = Channel();
				return C && C->IsBusy() && C->GetVisibleText().Len() >= C->GetCurrentText().Len();
			}, 20.f, [Battery]()
			{
				const UTextChannelComponent* C = Channel();
				const double Took = Real() - G.Mark;
				const float Cps = C->GetCharsPerSecond(Battery);
				const double Expected = C->GetCurrentText().Len() / Cps;
				Check(FMath::Abs(Took - Expected) <= 0.2 && C->GetCurrentVoice() == ETextVoice::Home,
					FString::Printf(TEXT("home timing: battery %.2f, level %s, %d characters at %.2f chars/s in %.2f s (expected %.2f s); text \"%s\""), Battery,
						LexToString(C->GetCurrentLevel()), C->GetCurrentText().Len(), Cps, Took, Expected, *C->GetCurrentText()));
				const UTextChannelWidget* Widget = ChannelWidget();
				Check(Widget && Widget->IsShown() && !Widget->IsShowingTwin() && Widget->GetShownText() == C->GetVisibleText(), TEXT("home timing: drawn as plain text, as before"));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("home timing: restore"), []()
		{
			Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed());
			Channel()->ClearAll();
			return 0.f;
		}));
	}

	static void StartTwinTest(bool bQuit)
	{
		if (!Begin(TEXT("TwinTest"), bQuit))
		{
			return;
		}
		G = FState();
		GRun.Watch = &WatchChannel;
		GRun.Cleanup = []()
		{
			if (Home())
			{
				Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed());
			}
			MutableSettings()->TextColors = G.SavedColors;
			if (UTextChannelComponent* C = Channel())
			{
				C->ClearAll();
			}
			NarrativeFixture::Restore(W());
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the map is playable"), []() { return IsReady(); }, 90.f, []()
		{
			G.SavedColors = UTheLastShiftSettings::GetTextColors();
			NarrativeFixture::Install(W());
			AddAct0SpendRow();
			ATwinAITerminal* T = TwinTerminal();
			Check(T != nullptr, FString::Printf(TEXT("the map holds a twin AI terminal (%s)"), *GetNameSafe(T)));
			if (!T || IsStopped())
			{
				return -1.f;
			}
			Check(T->GetPartCount() == 3, FString::Printf(TEXT("the fixture's twin AI has 3 parts (%d)"), T->GetPartCount()));
			Run()->ClearArchive();
			Channel()->ClearAll();
			Home()->RestoreState(1.0, Home()->GetHomeSecondsElapsed());
			Energy()->RestoreBankedEnergy(10);
			return 0.3f;
		}));
		AddPartsPhase(S);
		AddBatteryPhase(S);
		AddQueuePhase(S);
		AddColorsPhase(S);
		AddHomeTimingPhase(S);
		Start();
	}

	// =====================================================================================================
	// tls.ArchiveTest
	// =====================================================================================================

	static void StoreWithoutTerminal(const TArray<int32>& Logs, int32 Parts)
	{
		URunSubsystem* R = Run();
		R->StoreLogEntries(Logs);
		while (R->GetArchive().TwinPartsPlayed < Parts)
		{
			R->StoreTwinPart(R->GetArchive().TwinPartsPlayed + 1);
		}
	}

	static TArray<int32> ArchiveLogsNewestFirst()
	{
		TArray<int32> Logs = Run()->GetArchive().LogOrders;
		Logs.Sort([](int32 A, int32 B) { return A > B; });
		return Logs;
	}

	/** Opens the menu, checks the Archive entry and the list, then closes everything. */
	static void AddListCheck(TArray<FStep>& S, const FString& What, TFunction<FString()> Expected, bool bShots = false)
	{
		S.Add(Do(What + TEXT(": open the menu"), [bShots]()
		{
			Check(OpenMenu() && IsArchiveEntryShown(), TEXT("archive: the pause menu has an \"Archive\" entry"));
			if (bShots)
			{
				Shot(TEXT("archive_0_pause_menu"));
			}
			return bShots ? 0.5f : 0.1f;
		}));
		S.Add(Do(What + TEXT(": open the archive"), []()
		{
			Check(ClickArchive(), TEXT("archive: \"Archive\" opens the screen"));
			return 0.3f;
		}));
		S.Add(Do(What + TEXT(": the list"), [What, Expected, bShots]()
		{
			const FString Got = ListLabels();
			const FString Want = Expected();
			Check(ArchiveList() && Got == Want, FString::Printf(TEXT("%s: the archive lists \"%s\" (want \"%s\")"), *What, *Got, *Want));
			if (bShots)
			{
				Shot(TEXT("archive_1_list"));
			}
			return bShots ? 0.4f : 0.f;
		}));
		S.Add(Do(What + TEXT(": close"), []()
		{
			StdPC()->HandlePauseBackKey();
			const bool bBack = TopScreen() == StdPC()->GetPauseMenu() && StdPC()->GetPauseMenu()->IsVisible();
			Check(bBack, TEXT("archive: closing the screen returns to the pause menu"));
			CloseMenu();
			return 0.2f;
		}));
	}

	static void AddLocalContentPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("content: nothing stored"), []()
		{
			Check(OpenMenu(), TEXT("content: the pause menu opens"));
			Check(GamePauseMenu() != nullptr, TEXT("content: the pause menu is the game's (RunPauseMenuWidget)"));
			Check(!IsArchiveEntryShown(), TEXT("content: with nothing stored the pause menu has no \"Archive\" entry"));
			CloseMenu();
			return 0.2f;
		}));
		S.Add(Do(TEXT("content: the first log terminal"), []()
		{
			const TArray<ALogTerminal*> Terminals = LogTerminals();
			if (Terminals.Num() > 0)
			{
				PayAt(Terminals[0]);
				Note(FString::Printf(TEXT("content: paid %s, its group %s; it shows entry %d only"), *Terminals[0]->GetName(), *Join(Terminals[0]->GetEntries()), Terminals[0]->GetCurrentOrder()));
			}
			else
			{
				StoreWithoutTerminal({ 8, 7, 6, 5 }, 0);
				Note(TEXT("content: no log terminal in this map: entries 8,7,6,5 stored as tls.Archive.Store does"));
			}
			return 0.2f;
		}));
		AddListCheck(S, TEXT("content: after the first terminal"), []() { return ExpectedLabels({ 8, 7, 6, 5 }, 0); });
		S.Add(Do(TEXT("content: the second log terminal"), []()
		{
			const TArray<ALogTerminal*> Terminals = LogTerminals();
			if (Terminals.Num() > 1)
			{
				PayAt(Terminals[1]);
			}
			else
			{
				StoreWithoutTerminal({ 4, 3, 2, 1 }, 0);
			}
			return 0.2f;
		}));
		AddListCheck(S, TEXT("content: after the second terminal"), []() { return ExpectedLabels({ 8, 7, 6, 5, 4, 3, 2, 1 }, 0); });
		S.Add(Do(TEXT("content: one twin payment"), []()
		{
			if (ATwinAITerminal* T = TwinTerminal())
			{
				StandBefore(T, 100.f);
				T->TryActivate(Pawn());
				Check(Run()->GetArchive().TwinPartsPlayed == 1 && Channel()->IsBusy(), TEXT("content: \"Transmission 1\" stored at the payment, its messages still being typed"));
			}
			else
			{
				StoreWithoutTerminal({}, 1);
			}
			return 0.1f;
		}));
		AddListCheck(S, TEXT("content: after one twin payment"), []() { return ExpectedLabels({ 8, 7, 6, 5, 4, 3, 2, 1 }, 1); });
		S.Add(Do(TEXT("content: two more twin payments"), []()
		{
			if (ATwinAITerminal* T = TwinTerminal())
			{
				T->TryActivate(Pawn());
				T->TryActivate(Pawn());
			}
			else
			{
				StoreWithoutTerminal({}, 3);
			}
			return 0.1f;
		}));
		AddListCheck(S, TEXT("content: after three twin payments"), []() { return ExpectedLabels({ 8, 7, 6, 5, 4, 3, 2, 1 }, 3); }, true);
		S.Add(Do(TEXT("content: read a log"), []()
		{
			OpenMenu();
			ClickArchive();
			TimeStateDebug::ClickChoiceEntry(W(), 0);
			return 0.4f;
		}));
		S.Add(Do(TEXT("content: the log in full"), []()
		{
			const UArchiveItemWidget* Item = ArchiveItem();
			const FText Want = TwinWriting::FindLogText(UTheLastShiftSettings::Get()->LoadLogsTable(), 8);
			Check(Item && Item->GetBody().ToString() == Want.ToString() && !Want.IsEmpty(), FString::Printf(TEXT("content: \"Log 8\" shows its full text (\"%s\")"), Item ? *Item->GetBody().ToString().Left(60) : TEXT("-")));
			Check(Item && !HasBarcodeIn(Item), TEXT("content: as plain translated text, no barcode"));
			Shot(TEXT("archive_2_item"));
			return 0.4f;
		}));
		S.Add(Do(TEXT("content: read a transmission"), []()
		{
			StdPC()->HandlePauseBackKey();
			Check(ArchiveList() != nullptr, TEXT("content: back from an item returns to the list"));
			const UArchiveScreenWidget* List = ArchiveList();
			const int32 Index = List ? List->GetEntryCount() - 3 : 0; // Transmission 1
			TimeStateDebug::ClickChoiceEntry(W(), Index);
			return 0.3f;
		}));
		S.Add(Do(TEXT("content: the transmission in full"), []()
		{
			const UArchiveItemWidget* Item = ArchiveItem();
			TArray<FString> Rows;
			for (const FText& Text : TwinWriting::GetTwinPartTexts(UTheLastShiftSettings::Get()->LoadTwinAITable(), 1))
			{
				Rows.Add(Text.ToString());
			}
			const FString Want = FString::Join(Rows, TEXT("\n\n"));
			Check(Item && Item->GetBody().ToString() == Want && Rows.Num() == 2, FString::Printf(TEXT("content: \"Transmission 1\" shows the rows of part 1 in order (%d rows)"), Rows.Num()));
			const int32 Count = UArchiveScreenWidget::ListItems(W()).Num();
			Check(Count == Run()->GetArchive().LogOrders.Num() + Run()->GetArchive().TwinPartsPlayed,
				FString::Printf(TEXT("content: only log entries and twin parts are listed, no home AI message (%d items)"), Count));
			StdPC()->HandlePauseBackKey();
			StdPC()->HandlePauseBackKey();
			CloseMenu();
			return 0.3f;
		}));
	}

	static void TakeInputAndTimers()
	{
		G.InputBefore = TimeStateDebug::BuildInputReport(W());
		G.bStoppedBefore = IsStopped();
		if (const UScanDirectorComponent* D = Director())
		{
			G.CooldownBefore = D->GetCooldownRemaining();
			G.BeatsBefore = D->GetBeatCount();
			G.BeatInBefore = D->GetTimeToNextBeat();
		}
		G.CumulativeBefore = TS() ? TS()->GetCumulativeStoppedSeconds() : 0.f;
	}

	static void CompareInputAndTimers(const TCHAR* Phase)
	{
		const TimeStateDebug::FInputReport Now = TimeStateDebug::BuildInputReport(W());
		const TimeStateDebug::FInputReport& Was = G.InputBefore;
		Check(Now.InputMode == Was.InputMode && Now.bViewportIgnoresInput == Was.bViewportIgnoresInput && Now.bCursorShown == Was.bCursorShown
			&& Now.bPawnInputEnabled == Was.bPawnInputEnabled && Now.bPawnContextApplied == Was.bPawnContextApplied && Now.MappingContexts == Was.MappingContexts
			&& !Now.bPausedByMenu, FString::Printf(TEXT("%s: the input section is as before the menu (mode %s -> %s, cursor %d -> %d, ignore %d -> %d, game ready %d -> %d)"), Phase,
				*Was.InputMode, *Now.InputMode, Was.bCursorShown ? 1 : 0, Now.bCursorShown ? 1 : 0, Was.bViewportIgnoresInput ? 1 : 0, Now.bViewportIgnoresInput ? 1 : 0,
				Was.IsGameReady() ? 1 : 0, Now.IsGameReady() ? 1 : 0));
		Check(Now.IsGameReady(), FString::Printf(TEXT("%s: game input ready after the menu"), Phase));
		Check(IsStopped() == G.bStoppedBefore, FString::Printf(TEXT("%s: the time state is as before (stopped %d)"), Phase, IsStopped() ? 1 : 0));
		if (G.bStoppedBefore)
		{
			// Read in the frame the menu closed: the timers run again from there.
			Check(FMath::Abs(G.CooldownAtClose - G.CooldownBefore) <= 0.1f && G.BeatsAtClose == G.BeatsBefore && FMath::Abs(G.BeatInAtClose - G.BeatInBefore) <= 0.1f
				&& FMath::Abs(G.CumulativeAtClose - G.CumulativeBefore) <= 0.1f,
				FString::Printf(TEXT("%s: the timers are where they were when the menu opened (cooldown %.2f -> %.2f s, beats %d -> %d, next beat %.2f -> %.2f s, cumulative %.2f -> %.2f s)"),
					Phase, G.CooldownBefore, G.CooldownAtClose, G.BeatsBefore, G.BeatsAtClose, G.BeatInBefore, G.BeatInAtClose, G.CumulativeBefore, G.CumulativeAtClose));
		}
	}

	static void AddMenuRoundTrip(TArray<FStep>& S, const FString& Phase)
	{
		S.Add(Do(Phase + TEXT(": open the menu and the archive"), [Phase]()
		{
			TakeInputAndTimers();
			Check(OpenMenu() && ClickArchive() && ArchiveList(), FString::Printf(TEXT("%s: the archive opens from the pause menu"), *Phase));
			TimeStateDebug::ClickChoiceEntry(W(), 0);
			return 3.f;
		}));
		S.Add(Do(Phase + TEXT(": back, back, close"), [Phase]()
		{
			Check(ArchiveItem() != nullptr, FString::Printf(TEXT("%s: an item is open"), *Phase));
			StdPC()->HandlePauseBackKey();
			StdPC()->HandlePauseBackKey();
			Check(TopScreen() == StdPC()->GetPauseMenu(), FString::Printf(TEXT("%s: closing the screen returns to the pause menu"), *Phase));
			CloseMenu();
			if (const UScanDirectorComponent* D = Director())
			{
				G.CooldownAtClose = D->GetCooldownRemaining();
				G.BeatsAtClose = D->GetBeatCount();
				G.BeatInAtClose = D->GetTimeToNextBeat();
			}
			G.CumulativeAtClose = TS() ? TS()->GetCumulativeStoppedSeconds() : 0.f;
			return 0.3f;
		}));
		S.Add(Do(Phase + TEXT(": after the menu"), [Phase]() { CompareInputAndTimers(*Phase); return 0.f; }));
	}

	static void AddLocalInputPhase(TArray<FStep>& S)
	{
		AddMenuRoundTrip(S, TEXT("input, Normal"));
		S.Add(Do(TEXT("input: Stop"), []()
		{
			G.Banked = Director()->GetLidarScan() ? Director()->GetLidarScan()->GetScanCount() : 0;
			TS()->Toggle();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("input: the first scan of the Stop"), []()
		{
			const UScanDirectorComponent* D = Director();
			return D && D->GetLidarScan() && D->GetLidarScan()->GetScanCount() > G.Banked;
		}, 5.f, []() { return 0.5f; }));
		AddMenuRoundTrip(S, TEXT("input, Stopped"));
		S.Add(Do(TEXT("input: Resume"), []() { TS()->Toggle(); return 0.3f; }));
		S.Add(Do(TEXT("input: Normal again"), []() { Check(!IsStopped(), TEXT("input: time resumes")); return 0.f; }));
	}

	static void StartArchiveLocal(bool bQuit)
	{
		if (!Begin(TEXT("ArchiveTest"), bQuit))
		{
			return;
		}
		G = FState();
		GRun.Cleanup = []() { NarrativeFixture::Restore(W()); };
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the map is playable"), []() { return IsReady(); }, 90.f, []()
		{
			if (IsStopped())
			{
				return -1.f;
			}
			NarrativeFixture::Install(W());
			RelockAll();
			Run()->ClearArchive();
			Channel()->ClearAll();
			Energy()->RestoreBankedEnergy(10);
			Note(FString::Printf(TEXT("map %s: %d log terminals, twin terminal %s"), *GetNameSafe(W()), LogTerminals().Num(), *GetNameSafe(TwinTerminal())));
			return 0.2f;
		}));
		AddLocalContentPhase(S);
		AddLocalInputPhase(S);
		Start();
	}

	// --- chain: run state across maps, restarts, Continue, an old save ---

	static bool AreaReady(int32 Act)
	{
		return IsReady() && URunSubsystem::GetActIndexOf(W()) == Act && AAreaGenerator::Find(W()) != nullptr;
	}

	static bool SameArchive(const FSuitArchive& A, const TArray<int32>& Logs, int32 Parts)
	{
		TArray<int32> Sorted = A.LogOrders;
		Sorted.Sort([](int32 X, int32 Y) { return X > Y; });
		TArray<int32> Want = Logs;
		Want.Sort([](int32 X, int32 Y) { return X > Y; });
		return Sorted == Want && A.TwinPartsPlayed == Parts;
	}

	static FString Describe(const FSuitArchive& A)
	{
		return FString::Printf(TEXT("logs %s, parts %d"), *Join(A.LogOrders), A.TwinPartsPlayed);
	}

	static const FAreaSnapshot* SnapshotOf(int32 Act)
	{
		return Run()->GetSnapshots().FindByPredicate([Act](const FAreaSnapshot& S) { return S.ActIndex == Act; });
	}

	static void StartArchiveChain(bool bQuit, const FString& OldSave)
	{
		if (!Begin(TEXT("ArchiveTest"), bQuit))
		{
			return;
		}
		G = FState();
		G.OldSavePath = OldSave.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief8"), TEXT("Run_preBrief8.sav")) : OldSave;
		GRun.Cleanup = []()
		{
			if (URunSubsystem* R = Run())
			{
				R->ClearSave();
				R->ClearArchive();
			}
			NarrativeFixture::Restore(W());
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("chain: the main menu or a map is up"), []() { return W() && PC(); }, 60.f, []()
		{
			NarrativeFixture::Install(W());
			Check(Run()->StartNewRun(), TEXT("chain: a new run starts (archive empty, save cleared)"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: area 1 ready"), []() { return AreaReady(1); }, 90.f, []()
		{
			Check(Run()->GetArchive().IsEmpty(), FString::Printf(TEXT("chain: area 1 starts with an empty archive (%s)"), *Describe(Run()->GetArchive())));
			StoreWithoutTerminal({ 8, 7 }, 1);
			Check(SameArchive(Run()->GetArchive(), { 8, 7 }, 1), FString::Printf(TEXT("chain: stored in area 1 (%s)"), *Describe(Run()->GetArchive())));
			Check(Run()->TravelToNextArea(), TEXT("chain: travel to area 2"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("chain: area 2 ready"), []() { return AreaReady(2); }, 90.f, []()
		{
			Check(SameArchive(Run()->GetArchive(), { 8, 7 }, 1), FString::Printf(TEXT("chain: the archive survives the travel (%s)"), *Describe(Run()->GetArchive())));
			const FAreaSnapshot* S2 = SnapshotOf(2);
			Check(S2 && SameArchive(S2->Archive, { 8, 7 }, 1), FString::Printf(TEXT("chain: act 2's entry snapshot holds it (%s)"), S2 ? *Describe(S2->Archive) : TEXT("no snapshot")));
			const FAreaSnapshot* S1 = SnapshotOf(1);
			Check(S1 && S1->Archive.IsEmpty(), TEXT("chain: act 1's entry snapshot is empty"));
			StoreWithoutTerminal({ 6 }, 2);
			return 0.3f;
		}));
		AddListCheck(S, TEXT("chain: in area 2"), []() { return ExpectedLabels({ 8, 7, 6 }, 2); });
		S.Add(Do(TEXT("chain: restart from act 2"), []()
		{
			Check(Run()->RestartFromAct(2), TEXT("chain: restart from act 2"));
			Check(SameArchive(Run()->GetArchive(), { 8, 7 }, 1), FString::Printf(TEXT("chain: a restart from act 2 restores the archive as on entering it (%s)"), *Describe(Run()->GetArchive())));
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("chain: area 2 ready again"), []() { return AreaReady(2); }, 90.f, []()
		{
			Check(SameArchive(Run()->GetArchive(), { 8, 7 }, 1), FString::Printf(TEXT("chain: in area 2 after the restart (%s)"), *Describe(Run()->GetArchive())));
			// As if the game had been quit and started again: the archive in memory is gone, the save is read again.
			Run()->ClearArchive();
			const bool bLoadedBack = Run()->LoadSave();
			Check(bLoadedBack && Run()->GetLoadedSaveVersion() == URunSaveGame::CurrentVersion,
				FString::Printf(TEXT("chain: the save reads back (version %d, %d snapshots)"), Run()->GetLoadedSaveVersion(), Run()->GetSnapshots().Num()));
			const bool bContinued = Run()->ContinueRun();
			Check(bContinued, TEXT("chain: Continue"));
			Check(SameArchive(Run()->GetArchive(), { 8, 7 }, 1), FString::Printf(TEXT("chain: Continue has the stored items (%s)"), *Describe(Run()->GetArchive())));
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("chain: area 2 after Continue"), []() { return AreaReady(2); }, 90.f, []() { return 0.3f; }));
		AddListCheck(S, TEXT("chain: after Continue"), []() { return ExpectedLabels({ 8, 7 }, 1); });
		S.Add(Do(TEXT("chain: restart from act 1"), []()
		{
			const bool bRestarted = Run()->RestartFromAct(1);
			Check(bRestarted && Run()->GetArchive().IsEmpty(), FString::Printf(TEXT("chain: a restart from act 1 restores its empty archive (%s)"), *Describe(Run()->GetArchive())));
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("chain: area 1 ready again"), []() { return AreaReady(1); }, 90.f, []()
		{
			OpenMenu();
			Check(!IsArchiveEntryShown(), TEXT("chain: no \"Archive\" entry in act 1 after the restart"));
			CloseMenu();
			// A save written before this brief (version 1, no archive in its snapshots).
			const FString Slot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav"));
			const bool bHave = IFileManager::Get().FileExists(*G.OldSavePath);
			Check(bHave, FString::Printf(TEXT("chain: a save written before Brief 8 is at hand (%s)"), *G.OldSavePath));
			if (!bHave)
			{
				return 0.f;
			}
			const bool bCopied = IFileManager::Get().Copy(*Slot, *G.OldSavePath, true) == COPY_OK;
			const bool bLoaded = Run()->LoadSave();
			bool bAllEmpty = true;
			for (const FAreaSnapshot& Snapshot : Run()->GetSnapshots())
			{
				bAllEmpty &= Snapshot.Archive.IsEmpty();
			}
			Check(bCopied && bLoaded && Run()->GetLoadedSaveVersion() == 1 && Run()->GetSnapshots().Num() > 0 && bAllEmpty,
				FString::Printf(TEXT("chain: the old save loads (version %d, %d snapshots, every archive empty)"), Run()->GetLoadedSaveVersion(), Run()->GetSnapshots().Num()));
			Check(Run()->ContinueRun() && Run()->GetArchive().IsEmpty(), FString::Printf(TEXT("chain: Continue from the old save has an empty archive (%s)"), *Describe(Run()->GetArchive())));
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("chain: the area of the old save is ready"), []() { return IsReady() && URunSubsystem::GetActIndexOf(W()) > 0; }, 90.f, []()
		{
			Check(Run()->GetArchive().IsEmpty(), TEXT("chain: still empty once there"));
			return 0.f;
		}));
		Start();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLogsTestCommand(
	TEXT("tls.LogsTest"),
	TEXT("tls.LogsTest [Quit=0]: (fresh PIE in TestLidar, which holds two log terminals) payment and refusal, the deal of the entries by payment order "
		"(8 and 5 entries), the surface (all barcode, half and fully translated, Interact walks the entries for free), the suit panel, Stop, time jump, "
		"pause menu and leaving, and the readability shots at 1 m and 2 m with the flashlight. Fixture rows only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		TwinWritingDebug::StartLogsTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTwinTestCommand(
	TEXT("tls.TwinTest"),
	TEXT("tls.TwinTest [Quit=0]: (fresh PIE in TestLidar, which holds the twin AI's terminal and a log terminal) three payments play parts 1-3 after the "
		"spending comment, a fourth is refused; the twin voice (barcodes first, the translation behind), the battery at 0.35 and 0.02, a Stop during a "
		"twin message, the three text colours, and the home messages' timing at battery 1.0 and 0.03. Fixture rows only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		TwinWritingDebug::StartTwinTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsArchiveTestCommand(
	TEXT("tls.ArchiveTest"),
	TEXT("tls.ArchiveTest [local|chain] [Quit=0] [OldSavePath]: local (fresh PIE in TestLidar, or a package in an area: without terminals it stores as "
		"tls.Archive.Store does) checks the pause menu's Archive entry, the list and the items, and that the menu round trip leaves the time state, the "
		"timers and the input as they were, in Normal time and Stopped. chain (fresh PIE in MainMenu; clears the save) checks the archive across a travel, "
		"restarts from acts 2 and 1, Continue after the save is read again, and a save written before Brief 8 (default Saved/Brief8/Run_preBrief8.sav)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const bool bChain = Args.Num() > 0 && Args[0].Equals(TEXT("chain"), ESearchCase::IgnoreCase);
		const bool bQuit = Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0;
		if (bChain)
		{
			TwinWritingDebug::StartArchiveChain(bQuit, Args.Num() > 2 ? Args[2] : FString());
		}
		else
		{
			TwinWritingDebug::StartArchiveLocal(bQuit);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsArchiveStoreCommand(
	TEXT("tls.Archive.Store"),
	TEXT("tls.Archive.Store logs <order>... | twin <parts> | clear | print: stores log entries (by order) or the twin AI's parts 1..<parts> in the suit's "
		"archive without a terminal, empties it, or prints it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		URunSubsystem* Run = URunSubsystem::Get(World);
		if (!Run)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Archive.Store: no run subsystem (no game world)."));
			return;
		}
		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("print"));
		if (Verb == TEXT("logs"))
		{
			TArray<int32> Orders;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				Orders.Add(FCString::Atoi(*Args[Index]));
			}
			Run->StoreLogEntries(Orders);
		}
		else if (Verb == TEXT("twin"))
		{
			const int32 Parts = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : Run->GetArchive().TwinPartsPlayed + 1;
			while (Run->GetArchive().TwinPartsPlayed < Parts && Run->StoreTwinPart(Run->GetArchive().TwinPartsPlayed + 1))
			{
			}
		}
		else if (Verb == TEXT("clear"))
		{
			Run->ClearArchive();
		}
		const FSuitArchive& Archive = Run->GetArchive();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Archive.Store: archive holds log entries [%s] and twin parts 1..%d."),
			*FString::JoinBy(Archive.LogOrders, TEXT(","), [](int32 O) { return FString::FromInt(O); }), Archive.TwinPartsPlayed);
	}));

#endif // !UE_BUILD_SHIPPING
