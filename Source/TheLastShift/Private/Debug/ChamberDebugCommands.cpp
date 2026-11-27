// SPDX-License-Identifier: MIT

// The battery's chamber. tls.ChamberCheck [Quit] (run from MainMenu, in PIE; clears the save slot) checks, with its own
// fixture rows: the way in from the way home's corridor (the Passage label, the marker, no ending, the run's state kept, no
// entry of its own, the input); the room (its size, the doorway shut behind the player, the column, the racks and their
// lights, the lights, home's look); its one terminal (in front of the column on the line from the doorway, its front toward
// the doorway and whole on screen from the player's start, its two conduit lines to the column's foot), no hatch, no second
// terminal, no conduit elsewhere, the right wall closed; the lines and the wait before the terminal offers (the last row
// typed out, dropped at Silent, no row at all); the prompt ("Choose"); the choice's dialog (not while Stopped, its buttons
// with every twin AI part and with one part less, no button focused on opening, the keys and the mouse, a click and a tap,
// holds let go, moved off and cancelled by the back key, the pawn and the pause held while it is open, the back key and the
// back control closing it, the style's font inside its panel at the smallest supported resolution); each ending held
// through in a run of its own (the mouse at the settings' hold, the confirm key at a hold set for the session), its text
// alone, the run after it; a residue collected with its ring; quitting from the chamber and Continue; the two switches; the
// column's mesh. Shots to Saved/Screenshots/Brief36A. tls.Run.Chamber [Battery] [TwinParts] puts the run on its way home and
// travels to the chamber (the developer's check). Development builds only.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TestLidarCharacter.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Epilogue/EpilogueChamber.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/StdMenuPlayerController.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "PhysicsEngine/BodySetup.h"
#include "Prologue/PrologueMonitorRoom.h"
#include "Prologue/PrologueReturn.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Residue/Residue.h"
#include "Residue/ResidueCollectCueWidget.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "StaticMeshResources.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/ChoiceDialogWidget.h"
#include "UI/InteractPromptWidget.h"
#include "UI/StdKeyPromptWidget.h"
#include "UI/StdMainMenuWidget.h"
#include "UI/StdTextSceneWidget.h"
#include "UI/StdUIStyle.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#if !UE_BUILD_SHIPPING

namespace ChamberDebug
{
	using TimeStateDebug::ResolveGameWorld;

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static URunSceneSubsystem* Scenes() { return URunSceneSubsystem::Get(W()); }
	static UTimeStateSubsystem* Time() { return W() ? W()->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static FString MapName() { return W() ? UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()) : FString(); }
	static const UTheLastShiftSettings* Settings() { return UTheLastShiftSettings::Get(); }
	static double RunNow() { return Time() ? Time()->GetRunSeconds() : -1.0; }

	static UStdFlowSubsystem* Flow()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	static bool Transitioning() { return Flow() && Flow()->IsTransitioning(); }
	static APlayerController* PC() { return W() ? W()->GetFirstPlayerController() : nullptr; }
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { return PC() ? PC()->GetPawn() : nullptr; }
	static UTextChannelComponent* Channel() { return Pawn() ? Pawn()->FindComponentByClass<UTextChannelComponent>() : nullptr; }

	static UStdMainMenuWidget* MainMenu()
	{
		const AStdMenuPlayerController* MenuPC = W() ? Cast<AStdMenuPlayerController>(W()->GetFirstPlayerController()) : nullptr;
		return MenuPC ? MenuPC->GetMainMenu() : nullptr;
	}

	static UStdScreenWidget* Top() { return TimeStateDebug::TopScreen(W()); }
	static URunEndingWidget* TopEnding() { return Cast<URunEndingWidget>(Top()); }
	static URunTextSceneWidget* TopScene() { return Cast<URunTextSceneWidget>(Top()); }
	static URunActListWidget* TopList() { return Cast<URunActListWidget>(Top()); }

	template <typename T>
	static TArray<T*> All()
	{
		TArray<T*> Out;
		if (UWorld* World = W())
		{
			for (TActorIterator<T> It(World); It; ++It)
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	template <typename T>
	static T* First() { const TArray<T*> Found = All<T>(); return Found.Num() > 0 ? Found[0] : nullptr; }

	template <typename T>
	static T* FindWidget()
	{
		for (TObjectIterator<T> It; It; ++It)
		{
			if (It->GetWorld() == W() && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static AEpilogueChamber* Chamber() { return AEpilogueChamber::Find(W()); }
	static AChoiceTerminal* Term() { return Chamber() ? Chamber()->Terminal.Get() : nullptr; }
	static UChoiceDialogWidget* Dlg() { return Term() ? Term()->GetDialog() : nullptr; }
	static APrologueReturn* Ret() { return First<APrologueReturn>(); }
	static bool InMenu() { return MainMenu() != nullptr && !Transitioning(); }
	static bool InArea(int32 Act) { return W() && URunSubsystem::GetActIndexOf(W()) == Act && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }
	static bool InReturn() { return W() && URunSubsystem::GetPrologueStageOf(W()) == 1 && Pawn() && !Transitioning() && Ret() && Ret()->IsReturn() && Ret()->HasPlacedPawn(); }
	static bool InChamber() { return URunSubsystem::IsChamberOf(W()) && Pawn() && Chamber() && !Transitioning(); }
	static bool Offering() { return InChamber() && Chamber()->GetChamberState() == EEpilogueChamberState::Offering; }

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FLine
	{
		FName Row;
		double StartRun = -1.0;
		double TypedRun = -1.0;
		FString Text;
	};

	/** The run state compared across the way in: battery, home time, banked energy, the archive. */
	struct FState
	{
		double Battery = -1.0;
		double HomeSeconds = -1.0;
		int32 Banked = -1;
		TArray<int32> Logs;
		int32 Twin = -1;

		static FState Now()
		{
			FState S;
			S.Battery = Home() ? Home()->GetBatteryFraction() : -1.0;
			S.HomeSeconds = Home() ? Home()->GetHomeSecondsElapsed() : -1.0;
			S.Banked = Energy() ? Energy()->GetBankedEnergy() : -1;
			if (const URunSubsystem* R = Run())
			{
				S.Logs = R->GetArchive().LogOrders;
				S.Twin = R->GetArchive().TwinPartsPlayed;
			}
			return S;
		}

		static FState Of(const FAreaSnapshot& Snap)
		{
			FState S;
			S.Battery = Snap.BatteryFraction;
			S.HomeSeconds = Snap.HomeSecondsElapsed;
			S.Banked = Snap.BankedEnergy;
			S.Logs = Snap.Archive.LogOrders;
			S.Twin = Snap.Archive.TwinPartsPlayed;
			return S;
		}

		bool Same(const FState& O) const
		{
			return FMath::IsNearlyEqual(Battery, O.Battery, 1.e-6) && FMath::IsNearlyEqual(HomeSeconds, O.HomeSeconds, 1.e-3) && Banked == O.Banked && Logs == O.Logs && Twin == O.Twin;
		}

		FString ToString() const
		{
			return FString::Printf(TEXT("battery %.4f, home %.0f s, banked %d, %d log entries, %d twin parts"), Battery, HomeSeconds, Banked, Logs.Num(), Twin);
		}
	};

	struct FRun
	{
		bool bQuitAtEnd = false;
		bool bRunning = false;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bSavedReturn = true;
		bool bSavedMaps = true;
		bool bSavedChamber = true;
		float SavedHold = 3.f;
		bool bSavedSynthetic = true;

		// Sampled every frame.
		TArray<FLine> Lines;
		FName LastRow;
		bool bMarkerShown = false;
		bool bMarkerRightToLeft = true;
		float MarkerFirst = -1.f;
		float MarkerLast = -1.f;
		int32 EarlyOffers = 0;
		int32 EarlyPrompts = 0;
		int32 RingFramesWithDialog = 0;
		int32 ResidueRingFrames = 0;
		float MaxFill = 0.f;
		int32 LightsOnMin = TNumericLimits<int32>::Max();
		int32 LightsOnMax = -1;

		// Scratch.
		FState AtExit;
		double Battery = 0.5;
		double SilentBattery = 0.01;
		ETextDegradation Level = ETextDegradation::Full;
		FText ExpectedLabel;
		float ReadingSeconds = -1.f;
		int32 SaveWritesAtReturn = -1;
		double PressRun = -1.0;
		int32 TerminalEvents = 0;
		int32 EventsBefore = 0;
		FDelegateHandle TerminalHandle;
		FKey InteractKey;
		int32 HoldsStarted = 0;
		int32 HoldsCancelled = 0;
		int32 DialogsBefore = 0;
		int32 ChamberVisits = 0;
		int32 VisitsBefore = 0;
		bool bWasInChamber = false;
		FVector PawnAt = FVector::ZeroVector;
		FRotator ControlAt = FRotator::ZeroRotator;
		bool bFlashlightAt = false;
		int32 BankedBefore = 0;
		TWeakObjectPtr<AResidue> Residue;
		TSet<FKey> HeldSlateKeys;
		bool bMouseHeld = false;
		FVector2D MouseAt = FVector2D::ZeroVector;

		// Shots.
		FDelegateHandle ShotHandle;
		FString ShotName;
		bool bShotReady = false;
	};
	static FRun GRun;

	static const TCHAR* Name() { return TEXT("tls.ChamberCheck"); }

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name(), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name(), *What);
	}

	// --- Slate input: real keys and mouse events, as the player's ---

	static FKey BackKey() { return GIsEditor ? EKeys::Gamepad_FaceButton_Right : EKeys::Escape; }

	static void SlateKeyDown(const FKey& Key)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			GRun.HeldSlateKeys.Add(Key);
		}
	}

	static void SlateKeyUp(const FKey& Key)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			GRun.HeldSlateKeys.Remove(Key);
		}
	}

	static void SlateKey(const FKey& Key)
	{
		SlateKeyDown(Key);
		SlateKeyUp(Key);
	}

	static FVector2D WidgetPoint(const UWidget* Widget, const FVector2D& Fraction = FVector2D(0.5, 0.5))
	{
		if (!Widget)
		{
			return FVector2D::ZeroVector;
		}
		const FGeometry& G = Widget->GetCachedGeometry();
		return FVector2D(G.GetAbsolutePosition()) + FVector2D(G.GetAbsoluteSize()) * Fraction;
	}

	static FBox2D WidgetRect(const UWidget* Widget)
	{
		if (!Widget)
		{
			return FBox2D(ForceInit);
		}
		const FGeometry& G = Widget->GetCachedGeometry();
		const FVector2D Min(G.GetAbsolutePosition());
		const FVector2D Size(G.GetAbsoluteSize());
		return Size.IsNearlyZero() ? FBox2D(ForceInit) : FBox2D(Min, Min + Size);
	}

	/** The cursor moved to Position (desktop pixels): the platform's cursor too, so Slate's own synthesized moves agree. */
	static void MouseTo(const FVector2D& Position)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		const FVector2D Last = GRun.MouseAt.IsZero() ? Position : GRun.MouseAt;
		TSet<FKey> Pressed;
		if (GRun.bMouseHeld)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Last, Pressed, EKeys::Invalid, 0.f,
			FModifierKeysState()));
		GRun.MouseAt = Position;
	}

	static bool MouseDown(const FVector2D& Position)
	{
		MouseTo(Position);
		FSlateApplication& Slate = FSlateApplication::Get();
		const TSharedPtr<FSlateUser> User = Slate.GetCursorUser();
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows(), false, FSlateApplication::CursorUserIndex);
		Note(FString::Printf(TEXT("mouse down at (%.0f, %.0f): hits %s; pointer captured before: %d"), Position.X, Position.Y,
			Path.IsValid() ? *Path.Widgets.Last().Widget->ToString() : TEXT("<nothing>"), User && User->HasCapture(FSlateApplication::CursorPointerIndex) ? 1 : 0));
		GRun.bMouseHeld = true;
		return FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
			TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	}

	static void MouseUp()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		GRun.bMouseHeld = false;
		FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, GRun.MouseAt, GRun.MouseAt,
			TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	}

	static void Click(const FVector2D& Position)
	{
		MouseDown(Position);
		MouseUp();
	}

	static void ReleaseEverything()
	{
		if (GRun.bMouseHeld)
		{
			MouseUp();
		}
		for (const FKey& Key : TArray<FKey>(GRun.HeldSlateKeys.Array()))
		{
			SlateKeyUp(Key);
		}
	}

	/** A point below the dialog's panel, where the cursor reaches no button. */
	static FVector2D BelowPanel()
	{
		const FBox2D Panel = WidgetRect(Dlg() ? Dlg()->GetPanel() : nullptr);
		return Panel.bIsValid ? FVector2D(Panel.GetCenter().X, Panel.Max.Y + 60.0) : FVector2D::ZeroVector;
	}

	// --- Shots: the game's viewport, its UI included, cropped from the window ---

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(GRun.ShotHandle);
		GRun.ShotHandle.Reset();
		FIntRect Rect(0, 0, Width, Height);
		if (GEngine && GEngine->GameViewport)
		{
			const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
			const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
			if (Viewport.IsValid() && Window.IsValid())
			{
				const FGeometry& G = Viewport->GetCachedGeometry();
				const FVector2D Size(G.GetAbsoluteSize());
				// A capture of the whole window (PIE in the editor's viewport): the viewport's part of it.
				if (FMath::Abs(Size.X - Width) > 2.0 || FMath::Abs(Size.Y - Height) > 2.0)
				{
					const FVector2D Pos = FVector2D(G.GetAbsolutePosition()) - FVector2D(Window->GetPositionInScreen());
					Rect = FIntRect(FMath::Clamp(FMath::RoundToInt(Pos.X), 0, Width), FMath::Clamp(FMath::RoundToInt(Pos.Y), 0, Height),
						FMath::Clamp(FMath::RoundToInt(Pos.X + Size.X), 0, Width), FMath::Clamp(FMath::RoundToInt(Pos.Y + Size.Y), 0, Height));
				}
			}
		}
		TArray<FColor> Crop;
		Crop.Reserve(Rect.Area());
		for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
		{
			for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
			{
				FColor C = Pixels[Y * Width + X];
				C.A = 255;
				Crop.Add(C);
			}
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Rect.Width(), Rect.Height(), TArrayView64<const FColor>(Crop.GetData(), Crop.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief36A"), GRun.ShotName + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("shot %s (%d x %d of a %d x %d capture): %s"), *GRun.ShotName, Rect.Width(), Rect.Height(), Width, Height, *Path));
		GRun.bShotReady = true;
	}

	static void RequestShot(const FString& ShotName)
	{
		if (!GRun.ShotHandle.IsValid())
		{
			GRun.ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		GRun.ShotName = ShotName;
		GRun.bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), true, false);
	}

	static void Finish(const TCHAR* Why)
	{
		ReleaseEverything();
		if (UChoiceDialogWidget* Open = Dlg())
		{
			Open->Close(TEXT("tls.ChamberCheck finished"));
		}
		UTheLastShiftSettings* Mutable = GetMutableDefault<UTheLastShiftSettings>();
		Mutable->bReturnHome = GRun.bSavedReturn;
		Mutable->bPrologueMaps = GRun.bSavedMaps;
		Mutable->bChoiceChamber = GRun.bSavedChamber;
		Mutable->ChoiceHoldSeconds = GRun.SavedHold;
		if (IConsoleVariable* Synthetic = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableSyntheticCursorMoves")))
		{
			Synthetic->Set(GRun.bSavedSynthetic, ECVF_SetByConsole);
		}
		APowerTerminal::OnAnyActivated.Remove(GRun.TerminalHandle);
		if (GRun.ShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GRun.ShotHandle);
			GRun.ShotHandle.Reset();
		}
		if (PC() && GRun.InteractKey.IsValid())
		{
			PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Released, 0.f));
		}
		if (AResidue* R = GRun.Residue.Get())
		{
			R->Destroy();
		}
		NarrativeFixture::Restore(W());
		if (URunSubsystem* R = Run())
		{
			R->ClearSave();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), Name(),
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuitAtEnd)
		{
			FPlatformMisc::RequestExit(false, Name());
		}
	}

	static UInteractPromptWidget* Prompt() { return FindWidget<UInteractPromptWidget>(); }
	static UResidueCollectCueWidget* Cue() { return FindWidget<UResidueCollectCueWidget>(); }

	static void Sample()
	{
		const UTextChannelComponent* Text = Channel();
		const FName Row = Text ? Text->GetCurrentRowName() : NAME_None;
		const double Now = RunNow();
		if (Row != GRun.LastRow)
		{
			GRun.LastRow = Row;
			if (!Row.IsNone())
			{
				GRun.Lines.Add({ Row, Now, -1.0, Text->GetCurrentText() });
				Note(FString::Printf(TEXT("line %s started in %s: \"%s\""), *Row.ToString(), *MapName(), *Text->GetCurrentText()));
			}
		}
		if (Text && !Row.IsNone() && GRun.Lines.Num() > 0 && GRun.Lines.Last().Row == Row && GRun.Lines.Last().TypedRun < 0.0 && Text->GetShownCells() > 0
			&& Text->GetShownCells() >= Text->GetCurrentText().Len())
		{
			GRun.Lines.Last().TypedRun = Now;
			GRun.Lines.Last().Text = Text->GetCurrentText();
		}
		if (const UStdFlowSubsystem* F = Flow(); F && F->IsTransitionMarkerShown())
		{
			const float Pos = F->GetTransitionMarkerPosition();
			if (!GRun.bMarkerShown)
			{
				GRun.bMarkerShown = true;
				GRun.MarkerFirst = Pos;
				GRun.bMarkerRightToLeft = F->IsTransitionMarkerRightToLeft();
			}
			GRun.MarkerLast = Pos;
		}
		else
		{
			GRun.bMarkerShown = false;
		}
		const bool bIn = InChamber();
		GRun.ChamberVisits += bIn && !GRun.bWasInChamber ? 1 : 0;
		GRun.bWasInChamber = bIn;
		if (const AEpilogueChamber* C = Chamber(); C && URunSubsystem::IsChamberOf(W()))
		{
			// The terminal offers nothing before the last message is typed out: neither OffersChoice nor a prompt.
			const bool bBefore = C->GetChamberState() == EEpilogueChamberState::Waiting || C->GetChamberState() == EEpilogueChamberState::Lines;
			if (bBefore && C->Terminal && C->Terminal->OffersChoice())
			{
				++GRun.EarlyOffers;
			}
			if (const UInteractPromptWidget* P = Prompt(); bBefore && P && P->GetMode() != EInteractPromptMode::Hidden)
			{
				++GRun.EarlyPrompts;
			}
			const UResidueCollectCueWidget* Ring = Cue();
			if (Ring && Ring->IsRingShown() && Dlg())
			{
				++GRun.RingFramesWithDialog;
			}
			if (Ring && Ring->IsRingShown() && GRun.Residue.IsValid())
			{
				++GRun.ResidueRingFrames;
			}
			if (const UChoiceDialogWidget* D = Dlg(); D && D->GetHeldButton() != INDEX_NONE)
			{
				const UProgressBar* Fill = D->GetButtonFill(D->GetHeldButton());
				GRun.MaxFill = FMath::Max(GRun.MaxFill, Fill ? Fill->GetPercent() : 0.f);
			}
			if (const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>(); Room && Room->AreLightsShown())
			{
				GRun.LightsOnMin = FMath::Min(GRun.LightsOnMin, Room->GetLightsOnCount());
				GRun.LightsOnMax = FMath::Max(GRun.LightsOnMax, Room->GetLightsOnCount());
			}
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		// PIE stopped under the run: nothing to test any more (steps may not run without their world).
		if (!W())
		{
			GRun.Failed += 1;
			Finish(TEXT(", aborted: the game world ended"));
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		Sample();
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

	static void AddShot(TArray<FStep>& S, const FString& ShotName, float Settle = 0.4f)
	{
		S.Add(Do(ShotName + TEXT(": settle"), [Settle]() { return Settle; }));
		S.Add(Do(ShotName + TEXT(": shot"), [ShotName]() { RequestShot(ShotName); return 0.f; }));
		S.Add(WaitFor(ShotName + TEXT(": shot taken"), []() { return GRun.bShotReady; }, 5.f));
	}

	static void LookAt(const FVector& Target)
	{
		if (APawn* P = Pawn())
		{
			PC()->SetControlRotation((Target - P->GetPawnViewLocation()).Rotation());
		}
	}

	static void Teleport(const FVector& Where, bool bWithZ = false)
	{
		if (APawn* P = Pawn())
		{
			P->SetActorLocation(FVector(Where.X, Where.Y, bWithZ ? Where.Z : P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			if (ACharacter* C = Cast<ACharacter>(P))
			{
				C->GetCharacterMovement()->StopMovementImmediately();
			}
		}
	}

	static float Trace(const FVector& From, const FVector& To, UPrimitiveComponent** OutHit = nullptr)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("ChamberCheck"), false, Pawn());
		const bool bHit = W()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
		if (OutHit)
		{
			*OutHit = bHit ? Hit.GetComponent() : nullptr;
		}
		return bHit ? static_cast<float>(Hit.Distance) : -1.f;
	}

	static const FLine* LineOf(FName Row)
	{
		return GRun.Lines.FindByPredicate([Row](const FLine& L) { return L.Row == Row; });
	}

	static FString LinesSaid()
	{
		return FString::JoinBy(GRun.Lines, TEXT(", "), [](const FLine& L) { return L.Row.ToString(); });
	}

	static void ResetLines()
	{
		GRun.Lines.Reset();
		GRun.LastRow = NAME_None;
		GRun.EarlyOffers = 0;
		GRun.EarlyPrompts = 0;
	}

	/** The channel's text for a fixture text at Level, with the run's numbers now. */
	static FString Compose(const FString& Text, ETextDegradation Level)
	{
		FAIMessageRow Row;
		Row.Text = FText::FromString(Text);
		FTextChannelContext Context;
		Context.BatteryFraction = Home()->GetBatteryFraction();
		Context.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
		return UTextChannelComponent::ComposeMessage(Row, Level, Context);
	}

	static void PressInteract()
	{
		GRun.PressRun = RunNow();
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Pressed, 1.f));
	}

	static void ReleaseInteract()
	{
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Released, 0.f));
	}

	static UStaticMeshComponent* ColumnComponent()
	{
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() == W() && It->GetStaticMesh() && It->GetStaticMesh()->GetName() == TEXT("SM_BatteryColumn"))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The room's centre on the floor: the column's axis. */
	static FVector RoomCentre()
	{
		const UStaticMeshComponent* Column = ColumnComponent();
		const AEpilogueChamber* C = Chamber();
		const FVector At = Column ? Column->GetComponentLocation() : FVector::ZeroVector;
		return FVector(At.X, At.Y, C && C->EntryDoor ? C->EntryDoor->GetActorLocation().Z : At.Z);
	}

	/** From the doorway toward the column, on the floor. */
	static FVector InDir()
	{
		const AEpilogueChamber* C = Chamber();
		return C && C->EntryDoor ? (RoomCentre() - C->EntryDoor->GetActorLocation()).GetSafeNormal2D() : FVector::XAxisVector;
	}

	/** To the right of a player who comes in. */
	static FVector RightDir() { return FVector::CrossProduct(FVector::UpVector, InDir()); }

	/** The column's radius at Height above the floor, by a trace from the doorway's side toward the axis. */
	static float ColumnRadiusAt(float Height)
	{
		// On the column alone: the terminal and its conduits stand on the same line.
		UStaticMeshComponent* Column = ColumnComponent();
		const FVector Axis = RoomCentre() + FVector(0.f, 0.f, Height);
		FHitResult Hit;
		const bool bHit = Column && Column->LineTraceComponent(Hit, Axis - InDir() * 600.f, Axis, FCollisionQueryParams(TEXT("ChamberColumn"), true));
		return bHit ? 600.f - static_cast<float>(Hit.Distance) : -1.f;
	}

	/** The terminal's body box in the world: its eight corners and its faces through the actor's transform. */
	static FBox LocalBody() { return Term() ? Term()->GetLocalBodyBox() : FBox(ForceInit); }
	static FVector BodyPoint(float LX, float LY, float LZ) { return Term() ? Term()->GetActorTransform().TransformPosition(FVector(LX, LY, LZ)) : FVector::ZeroVector; }

	/** Stands the pawn Distance in front of the terminal's front face, looking at its panel. */
	static void FaceTerminal(float Distance)
	{
		const AChoiceTerminal* T = Term();
		if (!T)
		{
			return;
		}
		const FBox B = LocalBody();
		const FVector Fwd = T->GetActorForwardVector().GetSafeNormal2D();
		Teleport(BodyPoint(B.Max.X, 0.f, 0.f) + Fwd * Distance);
		LookAt(BodyPoint(B.Max.X, 0.f, B.Max.Z * 0.8f));
	}

	static void AddWayIn(TArray<FStep>& S, const FString& L)
	{
		S.Add(Do(L + TEXT(": step out of the ship"), []()
		{
			const APrologueReturn* R = Ret();
			const FVector Opening = R->ShipHatch->GetOpeningBox().GetCenter();
			const FVector Out = (Opening - R->Ship->GetActorLocation()).GetSafeNormal2D();
			Teleport(Opening + Out * 300.f);
			return 0.3f;
		}));
		S.Add(WaitFor(L + TEXT(": the way home's door opens"), []() { return Ret() && Ret()->GetReturnState() == EPrologueReturnState::DoorOpened; }, 60.f, []()
		{
			UHomeAIComponent::BuildPassageTravelLabel(W(), GRun.ExpectedLabel, GRun.ReadingSeconds);
			Teleport(Ret()->CorridorExit->GetActorLocation());
			return 0.f;
		}));
	}

	static void AddToChamber(TArray<FStep>& S, const FString& L, TFunction<void()> Before = nullptr)
	{
		S.Add(Do(L + TEXT(": Continue"), []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(L + TEXT(": in the ship"), []() { return InReturn(); }, 60.f, [L, Before]()
		{
			if (Before)
			{
				Before();
			}
			ResetLines();
			Check(Run()->TravelToChamber(), FString::Printf(TEXT("%s: to the chamber (%d twin parts)"), *L, Run()->GetArchive().TwinPartsPlayed));
			return 0.5f;
		}));
		S.Add(WaitFor(L + TEXT(": the terminal offers"), []() { return Offering(); }, 90.f, []()
		{
			FaceTerminal(80.f);
			return 0.4f;
		}));
	}

	static void AddOpenDialog(TArray<FStep>& S, const FString& L)
	{
		S.Add(Do(L + TEXT(": press Interact"), []()
		{
			GRun.DialogsBefore = Term() ? Term()->GetDialogsOpened() : 0;
			PressInteract();
			return 0.2f;
		}));
		S.Add(WaitFor(L + TEXT(": the dialog opens"), []() { return Dlg() != nullptr && Dlg()->GetCachedGeometry().GetLocalSize().X > 0.f; }, 3.f, []()
		{
			ReleaseInteract();
			if (UChoiceDialogWidget* D = Dlg())
			{
				// The console's box may hold the keyboard focus after a command: the dialog's own (no button), as on opening.
				D->FocusDefaultWidget();
				GRun.HoldsStarted = D->GetHoldsStarted();
				GRun.HoldsCancelled = D->GetHoldsCancelled();
			}
			return 0.3f;
		}));
	}

	static void AddChooseAndBack(TArray<FStep>& S, const FString& L, FName Ending)
	{
		S.Add(WaitFor(L + TEXT(": the ending's text over the main menu"), []() { return InMenu() && TopScene() != nullptr; }, 60.f, [L, Ending]()
		{
			URunTextSceneWidget* Text = TopScene();
			const bool bAlone = Text && Text->GetLines().Num() == 1 && Text->GetLines()[0].RowName == Ending && Text->GetParentScreen() == MainMenu();
			Check(bAlone, FString::Printf(TEXT("%s: the scene shows the %s text alone, over the main menu, no list (%d lines, parent %s)"), *L, *Ending.ToString(),
				Text ? Text->GetLines().Num() : -1, Text ? *GetNameSafe(Text->GetParentScreen()) : TEXT("-")));
			Check(Text && Text->GetLines().Num() > 0 && Text->GetLines()[0].Text == Settings()->GetEndingText(Ending).ToString(),
				FString::Printf(TEXT("%s: the text is the %s row, unchanged"), *L, *Ending.ToString()));
			if (Text)
			{
				Text->Confirm();
				Text->Confirm();
			}
			return 0.5f;
		}));
		S.Add(WaitFor(L + TEXT(": back in the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f, [L]()
		{
			Check(!Run()->IsReturningHome() && !Scenes()->IsEndingPending() && Scenes()->GetChosenEnding().IsNone(),
				FString::Printf(TEXT("%s: after the ending the run is no longer on its way home"), *L));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), FString::Printf(TEXT("%s: Select act"), *L));
			return 0.3f;
		}));
		S.Add(Do(L + TEXT(": the entries after it"), [L]()
		{
			const URunActListWidget* List = TopList();
			int32 ReturnIndex = INDEX_NONE;
			for (int32 I = 0; List && I < List->GetEntryCount(); ++I)
			{
				ReturnIndex = List->GetActOfEntry(I) == URunActListWidget::ReturnEntry ? I : ReturnIndex;
			}
			Check(ReturnIndex != INDEX_NONE && Run()->HasReturnEntry(), FString::Printf(TEXT("%s: every entry still selectable, the return's included (%s)"), *L,
				*TimeStateDebug::DescribeChoiceList(W())));
			TimeStateDebug::ClickChoiceEntry(W(), INDEX_NONE);
			return 0.3f;
		}));
	}

	/** After a hold went through: the choice made, the dialog closed, the input back, nothing answering any more. */
	static void CheckChosen(const FString& L, FName Ending, float Hold)
	{
		const AEpilogueChamber* C = Chamber();
		AChoiceTerminal* T = Term();
		const float Took = C->GetChosenRunSeconds() - static_cast<float>(GRun.PressRun);
		Check(FMath::Abs(Took - Hold) <= 0.15f && GRun.MaxFill >= 0.9f,
			FString::Printf(TEXT("%s: the hold took %.2f s of the run's clock (ChoiceHoldSeconds %.2f), the fill reached %.2f"), *L, Took, Hold, GRun.MaxFill));
		Check(C->GetChosenEnding() == Ending && T->HasChosen() && Dlg() == nullptr,
			FString::Printf(TEXT("%s: %s chosen, the dialog closed"), *L, *C->GetChosenEnding().ToString()));
		Check(!T->OffersChoice() && !T->TryActivate(Pawn()) && Dlg() == nullptr && Prompt() && Prompt()->GetMode() == EInteractPromptMode::Hidden,
			FString::Printf(TEXT("%s: nothing in the room answers any more (no prompt, Interact opens nothing)"), *L));
		Check(GRun.TerminalEvents == GRun.EventsBefore + 1 && GRun.RingFramesWithDialog == 0,
			FString::Printf(TEXT("%s: the terminal accepted the choice once (%d events), no ring at the terminal (%d frames)"), *L, GRun.TerminalEvents - GRun.EventsBefore,
				GRun.RingFramesWithDialog));
	}

	static TArray<UTextBlock*> TextsOf(const UUserWidget* Widget)
	{
		TArray<UTextBlock*> Out;
		if (Widget && Widget->WidgetTree)
		{
			Widget->WidgetTree->ForEachWidget([&Out](UWidget* Child)
			{
				if (UTextBlock* Text = Cast<UTextBlock>(Child))
				{
					Out.Add(Text);
				}
				else if (const UUserWidget* Nested = Cast<UUserWidget>(Child))
				{
					Out.Append(TextsOf(Nested));
				}
			});
		}
		return Out;
	}

	static bool SameFont(const FSlateFontInfo& Font) { return Font.FontObject == StdUIStyle::GetFontObject(); }

	static TArray<FStep> BuildTest()
	{
		TArray<FStep> S;

		// --- Setup ---
		S.Add(WaitFor(TEXT("setup: the main menu"), []() { return InMenu(); }, 30.f, []()
		{
			Run()->ClearSave();
			NarrativeFixture::Install(W());
			NarrativeFixture::SetEngineerLines(W(), { { TEXT("Chamber_1"), 1 } });
			for (double F = 1.0; F > 0.0; F -= 0.005)
			{
				if (Settings()->GetTextLevel(F) == ETextDegradation::Abbreviated)
				{
					GRun.Battery = F;
					break;
				}
			}
			for (double F = 0.002; F < 1.0; F += 0.002)
			{
				if (Settings()->GetTextLevel(F) == ETextDegradation::Silent)
				{
					GRun.SilentBattery = F;
					break;
				}
			}
			GRun.Level = Settings()->GetTextLevel(GRun.Battery);
			Note(FString::Printf(TEXT("battery for the run %.3f (%s), Silent battery %.3f; ChoiceHoldSeconds %.2f, OpenEndingTwinParts %d"), GRun.Battery,
				LexToString(GRun.Level), GRun.SilentBattery, Settings()->GetChoiceHoldSeconds(), Settings()->GetOpenEndingTwinParts()));
			Check(Settings()->IsChoiceChamberOn() && Settings()->IsChamberMap(Settings()->ChamberMap.ToSoftObjectPath().GetLongPackageName()) && Settings()->IsHomeLookMap(Settings()->ChamberMap.ToSoftObjectPath().GetLongPackageName()),
				FString::Printf(TEXT("setup: the chamber on, its map %s, in home's look"), *Settings()->ChamberMap.ToString()));
			Check(FMath::IsNearlyEqual(Settings()->GetChoiceHoldSeconds(), 3.f), FString::Printf(TEXT("setup: ChoiceHoldSeconds %.2f (3.0 in the ini)"), Settings()->GetChoiceHoldSeconds()));
			Home()->RestoreState(GRun.Battery, Home()->GetHomeSecondsElapsed() + 1234.0);
			Energy()->RestoreBankedEnergy(3);
			Run()->ClearArchive();
			Run()->StoreLogEntries({ 1, 2 });
			for (int32 Part = 1; Part <= Settings()->GetOpenEndingTwinParts(); ++Part)
			{
				Run()->StoreTwinPart(Part);
			}
			GRun.AtExit = FState::Now();
			GRun.TerminalHandle = APowerTerminal::OnAnyActivated.AddLambda([](APowerTerminal* T, int32) { if (T && T->GetWorld() == W()) { ++GRun.TerminalEvents; } });
			Note(FString::Printf(TEXT("the run: %s"), *GRun.AtExit.ToString()));
			Check(Run()->StartReturnHome(), TEXT("setup: the flight back"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("return: in the ship"), []() { return InReturn(); }, 120.f, []()
		{
			GRun.SaveWritesAtReturn = Run()->GetSaveWrites();
			return 0.2f;
		}));

		// --- The way in ---
		AddWayIn(S, TEXT("way in"));
		S.Add(WaitFor(TEXT("way in: the corridor's exit starts the travel"), []() { return Transitioning(); }, 10.f, []()
		{
			Check(Run()->IsReturningHome() && !Scenes()->IsEndingPending(), TEXT("way in: on the way home, the ending not pending"));
			Check(!GRun.ExpectedLabel.IsEmpty() && Run()->GetLastTravelLabel().EqualTo(GRun.ExpectedLabel),
				FString::Printf(TEXT("way in: the travel screen's label is the Passage row (\"%s\")"), *Run()->GetLastTravelLabel().ToString()));
			Check(!Flow()->IsTransitionMarkerRightToLeft(), TEXT("way in: the marker crosses as on the way into an area"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("chamber: arrived"), []() { return InChamber(); }, 60.f, []()
		{
			Check(!GRun.bMarkerRightToLeft && GRun.MarkerLast > GRun.MarkerFirst, FString::Printf(TEXT("way in: the marker crossed from %.2f to %.2f"), GRun.MarkerFirst, GRun.MarkerLast));
			const FState Now = FState::Now();
			Check(Now.Same(GRun.AtExit), FString::Printf(TEXT("way in: the run state as it left the hangar (%s)"), *Now.ToString()));
			Check(Run()->IsReturningHome() && Run()->GetActIndex() == 0 && !Scenes()->IsEndingPending() && TopEnding() == nullptr,
				TEXT("way in: in the chamber, on the way home, no act, the ending not opened"));
			Check(Run()->GetSaveWrites() == GRun.SaveWritesAtReturn && Run()->HasReturnEntry() && FState::Of(Run()->GetReturnSnapshot()).Same(GRun.AtExit),
				FString::Printf(TEXT("way in: no snapshot and no entry of its own (%d save writes since the return)"), Run()->GetSaveWrites() - GRun.SaveWritesAtReturn));
			const TimeStateDebug::FInputReport Input = TimeStateDebug::BuildInputReport(W());
			TimeStateDebug::LogInputReport(W(), TEXT("tls.ChamberCheck input (entering the chamber)"));
			Check(Input.IsGameReady(), TEXT("input: game input on entering the chamber"));
			return 0.5f;
		}));

		// --- Criterion 1: the room and its one terminal, from the player's start with no input ---
		S.Add(Do(TEXT("room: from the player's start"), []()
		{
			const AEpilogueChamber* C = Chamber();
			const FVector Centre = RoomCentre();
			const FVector In = InDir();
			const FVector Right = RightDir();
			const FVector P0 = Centre + FVector(-450.f, -450.f, 450.f);
			const float SizeX = Trace(P0, P0 + FVector(3000.f, 0.f, 0.f)) + Trace(P0, P0 - FVector(3000.f, 0.f, 0.f));
			const float SizeY = Trace(P0, P0 + FVector(0.f, 3000.f, 0.f)) + Trace(P0, P0 - FVector(0.f, 3000.f, 0.f));
			const FVector Low = Centre + FVector(-450.f, -450.f, 50.f);
			const float Height = Trace(Low, Low + FVector(0.f, 0.f, 3000.f)) + Trace(Low, Low - FVector(0.f, 0.f, 300.f));
			Check(SizeX >= 1700.f && SizeX <= 1900.f && SizeY >= 1700.f && SizeY <= 1900.f && Height >= 650.f && Height <= 750.f,
				FString::Printf(TEXT("room: %.0f x %.0f cm, %.0f cm high (about 18 x 18 x 7 m)"), SizeX, SizeY, Height));
			int32 Open = 0;
			for (int32 I = 0; I < 36; ++I)
			{
				const FVector Dir = FRotator(0.f, I * 10.f, 0.f).Vector();
				Open += Trace(Centre + FVector(0.f, 0.f, 150.f) + Dir * 200.f, Centre + FVector(0.f, 0.f, 150.f) + Dir * 3000.f) < 0.f ? 1 : 0;
			}
			Check(Open == 0, FString::Printf(TEXT("room: closed all round at 1.5 m (%d of 36 rays out)"), Open));
			const APawn* P = Pawn();
			const float Facing = FVector::DotProduct(PC()->GetControlRotation().Vector().GetSafeNormal2D(), (Centre - P->GetActorLocation()).GetSafeNormal2D());
			Check(C->EntryDoor && !C->EntryDoor->IsOpen() && FVector::Dist2D(P->GetActorLocation(), C->EntryDoor->GetActorLocation()) < 300.f && Facing > 0.95f,
				FString::Printf(TEXT("room: the player just inside the doorway (%.0f cm), its door shut behind, looking at the column (%.2f)"), FVector::Dist2D(P->GetActorLocation(), C->EntryDoor->GetActorLocation()), Facing));
			const UStaticMeshComponent* Column = ColumnComponent();
			const FBox ColumnBox = Column ? Column->Bounds.GetBox() : FBox(ForceInit);
			const float Radius = ColumnRadiusAt(350.f);
			Check(Column && FVector::Dist2D(Column->GetComponentLocation(), Centre) < 30.f && ColumnBox.Min.Z <= Centre.Z + 5.f && ColumnBox.Max.Z >= Centre.Z + Height - 20.f
				&& Radius >= 110.f && Radius <= 200.f,
				FString::Printf(TEXT("room: the battery's column at the centre, floor to ceiling, %.0f cm across at mid height"), Radius * 2.f));
			int32 Far = 0;
			int32 Near = 0;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == W() && It->GetStaticMesh() && It->GetStaticMesh()->GetName() == TEXT("SM_ComputeRack"))
				{
					const float Along = FVector::DotProduct(It->GetComponentLocation() - Centre, In);
					Far += Along > 700.f ? 1 : 0;
					Near += Along < -700.f ? 1 : 0;
				}
			}
			const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>();
			Check(Far > 0 && Near > 0 && Room && Room->GetLightCount() > 0 && Room->AreLightsShown(),
				FString::Printf(TEXT("room: racks on the far wall (%d) and the entry's wall (%d), %d lights by RackLights"), Far, Near, Room ? Room->GetLightCount() : 0));
			int32 LightCount = 0;
			for (ALight* Light : All<ALight>())
			{
				LightCount += Light ? 1 : 0;
			}
			Check(LightCount >= 1 && LightCount <= 4, FString::Printf(TEXT("room: %d lights (at most 4)"), LightCount));
			Check(Settings()->IsHomeLookMap(MapName()), TEXT("room: the map shows home's look"));

			// The one terminal: in front of the column, on the line from the doorway to its axis, its front toward the doorway.
			const AChoiceTerminal* T = Term();
			const TArray<AChoiceTerminal*> Choices = All<AChoiceTerminal>();
			const TArray<APowerTerminal*> Terminals = All<APowerTerminal>();
			Check(T && Choices.Num() == 1 && Terminals.Num() == 1 && Choices[0] == T && T->bModelledBody,
				FString::Printf(TEXT("terminal: one terminal in the room, the chamber's (%d choice terminals, %d terminals)"), Choices.Num(), Terminals.Num()));
			if (!T)
			{
				return -1.f;
			}
			const FVector Rel = T->GetActorLocation() - Centre;
			const float Lateral = FVector::DotProduct(Rel, Right);
			const float Along = FVector::DotProduct(Rel, In);
			const FVector ToDoor = (C->EntryDoor->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
			const float FrontToDoor = FVector::DotProduct(T->GetActorForwardVector().GetSafeNormal2D(), ToDoor);
			Check(FMath::Abs(Lateral) < 15.f && Along < 0.f && FrontToDoor > 0.98f,
				FString::Printf(TEXT("terminal: on the line from the doorway to the column's axis (%.1f cm off it, %.0f cm before the axis), its front toward the doorway (%.3f)"),
					Lateral, -Along, FrontToDoor));
			const FBox B = LocalBody();
			const FVector Back = BodyPoint(B.Min.X, 0.f, 0.f);
			const float GapShaft = Trace(Back + FVector(0.f, 0.f, 100.f) + In * 0.5f, Back + FVector(0.f, 0.f, 100.f) + In * 400.f);
			const float GapFoot = Trace(Back + FVector(0.f, 0.f, 10.f) + In * 0.5f, Back + FVector(0.f, 0.f, 10.f) + In * 400.f);
			Check(GapShaft >= 70.f && GapShaft <= 150.f && GapFoot >= 70.f && GapFoot <= 130.f,
				FString::Printf(TEXT("terminal: its back about 1 m from the column's surface (%.0f cm at 1 m up, %.0f cm at the foot)"), GapShaft + 0.5f, GapFoot + 0.5f));

			// Its whole front on screen from the player's start, nothing between it and the camera.
			int32 SizeVX = 0;
			int32 SizeVY = 0;
			PC()->GetViewportSize(SizeVX, SizeVY);
			const FVector Camera = PC()->PlayerCameraManager->GetCameraLocation();
			int32 OnScreen = 0;
			int32 Clear = 0;
			int32 Points = 0;
			for (const float U : { 0.03f, 0.5f, 0.97f })
			{
				for (const float V : { 0.03f, 0.5f, 0.97f })
				{
					const FVector Point = BodyPoint(B.Max.X + 1.f, FMath::Lerp(B.Min.Y, B.Max.Y, U), FMath::Lerp(B.Min.Z, B.Max.Z, V));
					FVector2D Screen;
					const bool bProjected = PC()->ProjectWorldLocationToScreen(Point, Screen, false);
					OnScreen += bProjected && Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= SizeVX && Screen.Y <= SizeVY ? 1 : 0;
					Clear += Trace(Camera, Point) < 0.f ? 1 : 0;
					++Points;
				}
			}
			Check(OnScreen == Points && Clear == Points,
				FString::Printf(TEXT("terminal: its whole front on screen from the player's start (%d of %d points) and nothing in front of it (%d of %d lines clear), %.0f cm away"),
					OnScreen, Points, Clear, Points, FVector::Dist2D(Camera, BodyPoint(B.Max.X, 0.f, 0.f))));

			// Its two conduit lines, from its back to the column's foot, and no other conduit in the room.
			const float FootRadius = ColumnRadiusAt(10.f);
			const float BackAlong = FVector::DotProduct(Back - Centre, In);
			int32 Conduits = 0;
			int32 Fit = 0;
			TArray<float> Lines;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() != W() || !It->GetStaticMesh() || !It->GetStaticMesh()->GetName().StartsWith(TEXT("SM_KitConduit")) || !It->IsVisible())
				{
					continue;
				}
				++Conduits;
				const FBox Box = It->Bounds.GetBox();
				const FVector Mid = Box.GetCenter() - Centre;
				const float Lat = FVector::DotProduct(Mid, Right);
				const FVector Ext = Box.GetExtent();
				const float HalfAlong = FMath::Abs(In.X) * Ext.X + FMath::Abs(In.Y) * Ext.Y;
				const float Start = FVector::DotProduct(Mid, In) - HalfAlong;
				const float End = FVector::DotProduct(Mid, In) + HalfAlong;
				const float FootAt = -FMath::Sqrt(FMath::Max(FMath::Square(FootRadius) - FMath::Square(Lat), 0.f));
				const bool bFits = FMath::Abs(Start - BackAlong) <= 3.f && End >= FootAt - 1.f && End <= FootAt + 15.f && Box.Min.Z <= Centre.Z + 1.f && Box.Max.Z <= Centre.Z + 40.f
					&& FMath::Abs(Lat) <= 60.f;
				Fit += bFits ? 1 : 0;
				if (!Lines.ContainsByPredicate([Lat](float O) { return FMath::Abs(O - Lat) < 20.f; }))
				{
					Lines.Add(Lat);
				}
				Note(FString::Printf(TEXT("conduit %s: from %.1f to %.1f cm along (back %.1f, the foot %.1f), %.1f cm aside, %.0f to %.0f cm high"), *GetNameSafe(It->GetOwner()),
					Start, End, BackAlong, FootAt, Lat, Box.Min.Z - Centre.Z, Box.Max.Z - Centre.Z));
			}
			Check(Conduits >= 2 && Fit == Conduits && Lines.Num() == 2,
				FString::Printf(TEXT("terminal: %d conduit pieces in the room, every one on the floor from its back to the column's foot (%d), in two lines"), Conduits, Fit));

			// No hatch, no frame but the doorway's, no conduit on the left wall (above), the right wall closed.
			int32 Frames = 0;
			int32 OtherFrames = 0;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() != W() || !It->GetStaticMesh())
				{
					continue;
				}
				const FString Mesh = It->GetStaticMesh()->GetName();
				if (Mesh == TEXT("SM_KitDoorFrame"))
				{
					++Frames;
					OtherFrames += FVector::Dist2D(It->GetComponentLocation(), C->EntryDoor->GetActorLocation()) > 50.f ? 1 : 0;
				}
				OtherFrames += Mesh.Contains(TEXT("HatchFrame")) ? 1 : 0;
			}
			Check(All<APrologueSlidingPanel>().Num() == 0 && Frames == 1 && OtherFrames == 0,
				FString::Printf(TEXT("room: no hatch, no frame but the doorway's (%d sliding panels, %d door frames, %d elsewhere)"), All<APrologueSlidingPanel>().Num(), Frames, OtherFrames));
			int32 WallRays = 0;
			int32 WallHits = 0;
			FString Odd;
			for (float A = -150.f; A <= 150.f; A += 25.f)
			{
				for (float Z = 20.f; Z <= 690.f; Z += 30.f)
				{
					const FVector From = Centre + In * A + Right * 600.f + FVector(0.f, 0.f, Z);
					UPrimitiveComponent* Hit = nullptr;
					const float D = Trace(From, From + Right * 1500.f, &Hit);
					const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Hit);
					const FString Path = Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString();
					const bool bOk = D >= 280.f && D <= 300.f && Path.StartsWith(TEXT("/Game/TheLastShift/Art/Kit/SM_KitWallPanel"));
					WallHits += bOk ? 1 : 0;
					++WallRays;
					if (!bOk && Odd.Len() < 300)
					{
						Odd += FString::Printf(TEXT(" [%.0f,%.0f: %.0f %s]"), A, Z, D, *GetNameSafe(Mesh ? Mesh->GetStaticMesh() : nullptr));
					}
				}
			}
			int32 Outside = 0;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == W() && It->IsVisible() && It->GetStaticMesh() && FVector::DotProduct(It->Bounds.GetBox().Min - Centre, Right) > 925.f
					&& FMath::Abs(FVector::DotProduct(It->Bounds.Origin - Centre, In)) < 300.f)
				{
					++Outside;
				}
			}
			Check(WallHits == WallRays && Outside == 0,
				FString::Printf(TEXT("room: the right wall closed where the hatch stood, the wall's own panels floor to ceiling (%d of %d rays), nothing outside it (%d)%s"),
					WallHits, WallRays, Outside, *Odd));
			return 0.f;
		}));
		AddShot(S, TEXT("1_Room_FromStart"), 0.1f);
		S.Add(Do(TEXT("lines: wait by the terminal"), []() { FaceTerminal(80.f); return 0.f; }));

		// --- Criterion 2: the lines, the wait and the prompt ---
		S.Add(WaitFor(TEXT("lines: the terminal offers"), []() { return Offering(); }, 60.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			const FName ERow = NarrativeFixture::GetEngineerLineName(TEXT("Chamber_1"), 1);
			const FLine* E = LineOf(ERow);
			const FLine* C1 = LineOf(TEXT("Chamber_1"));
			const FLine* End = LineOf(TEXT("Ending_1"));
			Check(E && C1 && End && E->StartRun <= C1->StartRun && C1->StartRun <= End->StartRun,
				FString::Printf(TEXT("lines: the engineer's line, Chamber_1, then Ending_1 on the channel (%s)"), *LinesSaid()));
			Check(E && E->Text == NarrativeFixture::GetEngineerLineText(TEXT("Chamber_1"), 1), TEXT("lines: the engineer's line in full"));
			const FString WantC = Compose(NarrativeFixture::GetEpilogueLineText(EpilogueGroups::Chamber, 1), GRun.Level);
			const FString WantE = Compose(NarrativeFixture::GetEndingMessageText(), GRun.Level);
			const FString FullE = Compose(NarrativeFixture::GetEndingMessageText(), ETextDegradation::Full);
			Check(C1 && End && C1->Text == WantC && End->Text == WantE && WantE != FullE,
				FString::Printf(TEXT("lines: the home AI's rows shortened by the run's battery (%s): \"%s\""), LexToString(GRun.Level), End ? *End->Text : TEXT("-")));
			Check(GRun.EarlyOffers == 0 && GRun.EarlyPrompts == 0 && End && End->TypedRun >= 0.0 && C->GetOfferedRunSeconds() >= End->TypedRun - 0.05,
				FString::Printf(TEXT("wait: nothing offered and no prompt by the terminal before the last message was typed out (%d, %d early frames; typed %.2f, offered %.2f)"),
					GRun.EarlyOffers, GRun.EarlyPrompts, End ? End->TypedRun : -1.0, C->GetOfferedRunSeconds()));
			return 0.4f;
		}));
		S.Add(Do(TEXT("prompt: by the terminal"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			GRun.InteractKey = P ? P->GetShownKey() : FKey();
			Note(FString::Printf(TEXT("the Interact key: %s"), *GRun.InteractKey.ToString()));
			const FString Want = FString::Printf(TEXT("] %s"), *URunSceneSubsystem::GetChooseLabel().ToString());
			Check(P && P->GetMode() == EInteractPromptMode::Choice && P->GetShownText().EndsWith(Want) && GRun.InteractKey.IsValid() && Term()->OffersChoice(),
				FString::Printf(TEXT("prompt: the Interact key and \"Choose\", as a terminal offers (\"%s\")"), P ? *P->GetShownText() : TEXT("-")));
			FaceTerminal(200.f);
			return 0.5f;
		}));
		S.Add(Do(TEXT("prompt: from 2 m"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			Check(P && P->GetMode() == EInteractPromptMode::Choice, FString::Printf(TEXT("prompt: still shown from 2 m in front of the terminal (\"%s\", reach %.0f cm)"),
				P ? *P->GetShownText() : TEXT("-"), Term()->GetInteractRadius()));
			return 0.f;
		}));
		AddShot(S, TEXT("2_Terminal_From2m_Prompt"), 0.2f);

		// --- Criterion 3: not while Stopped ---
		S.Add(Do(TEXT("stopped: Stop by the terminal"), []()
		{
			FaceTerminal(80.f);
			GRun.DialogsBefore = Term()->GetDialogsOpened();
			Time()->Toggle();
			return 1.2f;
		}));
		S.Add(Do(TEXT("stopped: press Interact"), []()
		{
			const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>();
			Check(Time()->IsStopped() && Room && !Room->AreLightsShown(), TEXT("stopped: the rack lights hidden while Stopped"));
			PressInteract();
			return 0.4f;
		}));
		S.Add(Do(TEXT("stopped: nothing"), []()
		{
			ReleaseInteract();
			Check(Dlg() == nullptr && Term()->GetDialogsOpened() == GRun.DialogsBefore && !Term()->OffersChoice() && Prompt() && Prompt()->GetMode() == EInteractPromptMode::Hidden,
				TEXT("stopped: no prompt and no dialog while Stopped"));
			Time()->Toggle();
			return 1.0f;
		}));

		// --- Criteria 3 to 5: the dialog with every twin AI part ---
		AddOpenDialog(S, TEXT("dialog"));
		S.Add(Do(TEXT("dialog: opened"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			if (!D)
			{
				return -1.f;
			}
			const FText Inject = URunSceneSubsystem::GetEndingLabel(TEXT("Inject"));
			const FText Open = URunSceneSubsystem::GetEndingLabel(TEXT("Open"));
			Check(D->GetButtonCount() == 2 && D->GetEndingOfButton(0) == TEXT("Inject") && D->GetEndingOfButton(1) == TEXT("Open")
				&& D->GetButtonLabel(0)->GetText().EqualTo(Inject) && D->GetButtonLabel(1)->GetText().EqualTo(Open) && D->GetButton(0)->GetIsEnabled() && D->GetButton(1)->GetIsEnabled(),
				FString::Printf(TEXT("dialog: with every twin AI part, two buttons with the list's two labels (%d buttons)"), D->GetButtonCount()));
			const TArray<UTextBlock*> Texts = TextsOf(D);
			FString Shown;
			int32 Other = 0;
			for (const UTextBlock* Text : Texts)
			{
				const FString Value = Text->GetText().ToString();
				Shown += FString::Printf(TEXT(" \"%s\""), *Value);
				const bool bAllowed = Text->GetText().EqualTo(Inject) || Text->GetText().EqualTo(Open) || Text->GetText().EqualTo(UChoiceDialogWidget::GetBackLabel())
					|| (D->GetBackPrompt() && Text->GetText().EqualTo(D->GetBackPrompt()->GetKeyText()));
				Other += bAllowed || Value.IsEmpty() ? 0 : 1;
			}
			Check(Other == 0 && D->GetBackPrompt() && D->GetBackPrompt()->GetDisplayedKey() == StdTextScene::SkipControlKey(),
				FString::Printf(TEXT("dialog: nothing else to read but the back control, its key and \"Back\" (%s)"), *Shown));
			Check(D->GetFocusedButton() == INDEX_NONE && D->HasUserFocus(PC()), TEXT("dialog: on opening no button is focused: the dialog itself holds the focus"));
			Check(PC()->ShouldShowMouseCursor() && StdPC() && !StdPC()->bCanPause && !Term()->OffersChoice() && Prompt() && Prompt()->GetMode() == EInteractPromptMode::Hidden,
				TEXT("dialog: the cursor shown, the pause held, no prompt while it is open"));
			Check(Term()->GetDialogsOpened() == GRun.DialogsBefore + 1 && GRun.TerminalEvents == 0, TEXT("dialog: opened by the press (no terminal sound yet)"));

			// Criterion 7: the style's font, inside the panel, at the smallest supported resolution.
			const FBox2D Panel = WidgetRect(D->GetPanel());
			int32 Fonts = 0;
			int32 Inside = 0;
			int32 Unclipped = 0;
			FString Fails;
			for (const UTextBlock* Text : Texts)
			{
				const FBox2D R = WidgetRect(Text);
				const bool bFont = SameFont(Text->GetFont());
				const bool bIn = Panel.bIsValid && R.bIsValid && R.Min.X >= Panel.Min.X - 0.5 && R.Min.Y >= Panel.Min.Y - 0.5 && R.Max.X <= Panel.Max.X + 0.5 && R.Max.Y <= Panel.Max.Y + 0.5;
				const FVector2D Desired = Text->GetDesiredSize();
				const FVector2D Local = Text->GetCachedGeometry().GetLocalSize();
				const bool bWhole = Desired.X <= Local.X + 1.0 && Desired.Y <= Local.Y + 1.0;
				Fonts += bFont ? 1 : 0;
				Inside += bIn ? 1 : 0;
				Unclipped += bWhole ? 1 : 0;
				if (!(bFont && bIn && bWhole))
				{
					Fails += FString::Printf(TEXT(" [\"%s\" %s %s, %.0f/%.0f]"), *Text->GetText().ToString(), *StdUIStyle::DescribeFont(Text->GetFont()), *R.ToString(), Desired.X, Local.X);
				}
			}
			Check(Texts.Num() >= 3 && Fonts == Texts.Num() && Inside == Texts.Num() && Unclipped == Texts.Num(),
				FString::Printf(TEXT("style: every text of the dialog in the style's font (%d of %d), inside its panel (%d), whole (%d)%s"), Fonts, Texts.Num(), Inside, Unclipped, *Fails));
			const FIntPoint Smallest(1280, 720);
			const float Scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Smallest);
			const FVector2D PanelSize = D->GetPanel()->GetDesiredSize();
			const FVector2D Room = FVector2D(Smallest) / FMath::Max(Scale, 0.01f);
			Check(PanelSize.X <= Room.X && PanelSize.Y <= Room.Y && PanelSize.X > 0.0,
				FString::Printf(TEXT("style: the panel (%.0f x %.0f Slate units) fits the screen at 1280 x 720 (%.0f x %.0f Slate units at its DPI scale %.3f); the layout is in Slate units, the same at every size"),
					PanelSize.X, PanelSize.Y, Room.X, Room.Y, Scale));
			return 0.f;
		}));
		AddShot(S, TEXT("3_Dialog_TwoButtons"), 0.2f);
		S.Add(Do(TEXT("confirm: the confirm key with no button focused"), []()
		{
			Dlg()->FocusDefaultWidget();
			SlateKeyDown(EKeys::Enter);
			return Settings()->GetChoiceHoldSeconds() + 0.3f;
		}));
		S.Add(Do(TEXT("confirm: nothing"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			SlateKeyUp(EKeys::Enter);
			Check(D && D->GetHoldsStarted() == GRun.HoldsStarted && !Term()->HasChosen() && D->GetFocusedButton() == INDEX_NONE,
				FString::Printf(TEXT("confirm: the confirm key held %.1f s with no button focused presses nothing, chooses nothing"), Settings()->GetChoiceHoldSeconds() + 0.3f));
			return 0.1f;
		}));
		S.Add(Do(TEXT("keys: the navigation keys"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			SlateKey(EKeys::Down);
			const int32 A = D->GetFocusedButton();
			SlateKey(EKeys::Down);
			const int32 B = D->GetFocusedButton();
			SlateKey(EKeys::Up);
			const int32 C = D->GetFocusedButton();
			D->FocusDefaultWidget();
			SlateKey(EKeys::Up);
			const int32 E = D->GetFocusedButton();
			D->FocusDefaultWidget();
			SlateKey(EKeys::Tab);
			const int32 F = D->GetFocusedButton();
			Check(A == 0 && B == 1 && C == 0 && E == 1 && F == 0,
				FString::Printf(TEXT("keys: Down reaches the first button, then the second, Up the first again; from none, Up the last and Tab the first (%d %d %d %d %d)"), A, B, C, E, F));
			Check(D->GetHoldsStarted() == GRun.HoldsStarted, TEXT("keys: moving the focus holds nothing"));
			MouseTo(WidgetPoint(D->GetButton(1)));
			return 0.25f;
		}));
		S.Add(Do(TEXT("mouse: on the second button"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			const int32 A = D->GetFocusedButton();
			MouseTo(WidgetPoint(D->GetButton(0)));
			Check(A == 1, FString::Printf(TEXT("mouse: the cursor on the second button focuses it (%d)"), A));
			return 0.25f;
		}));
		S.Add(Do(TEXT("mouse: on the first button"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			Check(D->GetFocusedButton() == 0, FString::Printf(TEXT("mouse: the cursor on the first button focuses it (%d)"), D->GetFocusedButton()));
			Click(WidgetPoint(D->GetButton(0)));
			return 0.3f;
		}));
		S.Add(Do(TEXT("click: nothing"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D && !Term()->HasChosen() && D->GetHeldButton() == INDEX_NONE && D->GetButtonFill(0)->GetPercent() == 0.f, TEXT("click: a click on a button chooses nothing"));
			SlateKey(EKeys::Enter);
			return 0.3f;
		}));
		S.Add(Do(TEXT("tap: nothing"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D && !Term()->HasChosen() && D->GetHeldButton() == INDEX_NONE && D->GetButtonFill(0)->GetPercent() == 0.f && D->GetFocusedButton() == 0,
				TEXT("tap: a tap of the confirm key on a focused button chooses nothing"));
			GRun.HoldsStarted = D->GetHoldsStarted();
			GRun.HoldsCancelled = D->GetHoldsCancelled();
			MouseDown(WidgetPoint(D->GetButton(0)));
			return Settings()->GetChoiceHoldSeconds() * 0.5f;
		}));
		S.Add(Do(TEXT("mouse hold: at half"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			const float Fill = D->GetButtonFill(0)->GetPercent();
			Check(D->GetHeldButton() == 0 && D->GetHoldSource() == EChoiceHoldSource::Mouse && FMath::Abs(Fill - 0.5f) < 0.12f && FMath::Abs(Fill - D->GetHoldFraction()) < 0.05f,
				FString::Printf(TEXT("mouse hold: the first button held with the mouse fills, %.2f at half the hold"), Fill));
			MouseUp();
			return 0.15f;
		}));
		S.Add(Do(TEXT("mouse hold: let go"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D && D->GetHoldsCancelled() == GRun.HoldsCancelled + 1 && D->GetLastCancelReason() == TEXT("let go") && D->GetButtonFill(0)->GetPercent() == 0.f && !Term()->HasChosen(),
				FString::Printf(TEXT("mouse hold: let go at half, the fill empties and nothing is chosen (%s)"), D ? *D->GetLastCancelReason() : TEXT("-")));
			GRun.HoldsCancelled = D->GetHoldsCancelled();
			MouseDown(WidgetPoint(D->GetButton(1)));
			return Settings()->GetChoiceHoldSeconds() * 0.5f;
		}));
		S.Add(Do(TEXT("mouse hold: the cursor off at half"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D->GetHeldButton() == 1 && D->GetButtonFill(1)->GetPercent() > 0.35f, FString::Printf(TEXT("mouse hold: the second button held, %.2f"), D->GetButtonFill(1)->GetPercent()));
			MouseTo(BelowPanel());
			return 0.15f;
		}));
		S.Add(Do(TEXT("mouse hold: off"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			Check(D && D->GetHoldsCancelled() == GRun.HoldsCancelled + 1 && D->GetLastCancelReason() == TEXT("the cursor off the button") && D->GetButtonFill(1)->GetPercent() == 0.f
				&& !Term()->HasChosen(), FString::Printf(TEXT("mouse hold: the cursor moved off at half, the fill empties and nothing is chosen (%s)"), D ? *D->GetLastCancelReason() : TEXT("-")));
			GRun.HoldsCancelled = D->GetHoldsCancelled();
			GRun.HoldsStarted = D->GetHoldsStarted();
			MouseTo(WidgetPoint(D->GetButton(1)));
			return 0.4f;
		}));
		S.Add(Do(TEXT("mouse hold: back on the button, still down"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			Check(D->GetHoldsStarted() == GRun.HoldsStarted && D->GetHeldButton() == INDEX_NONE, TEXT("mouse hold: back on the button with the press spent, nothing held until it is pressed again"));
			MouseUp();
			MouseTo(BelowPanel());
			D->FocusDefaultWidget();
			SlateKey(EKeys::Down);
			SlateKeyDown(EKeys::Enter);
			return Settings()->GetChoiceHoldSeconds() * 0.5f;
		}));
		S.Add(Do(TEXT("key hold: at half"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			const float Fill = D->GetButtonFill(0)->GetPercent();
			Check(D->GetHeldButton() == 0 && D->GetHoldSource() == EChoiceHoldSource::Key && FMath::Abs(Fill - 0.5f) < 0.12f,
				FString::Printf(TEXT("key hold: the focused button held with the confirm key fills, %.2f at half the hold"), Fill));
			GRun.HoldsCancelled = D->GetHoldsCancelled();
			SlateKey(BackKey());
			return 0.15f;
		}));
		S.Add(Do(TEXT("key hold: the back key"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D && D->GetHoldsCancelled() == GRun.HoldsCancelled + 1 && D->GetLastCancelReason() == TEXT("the back key") && D->GetButtonFill(0)->GetPercent() == 0.f && !Term()->HasChosen(),
				FString::Printf(TEXT("key hold: the back key at half cancels the hold, the fill empties, nothing is chosen, the dialog stays (%s)"), D ? *D->GetLastCancelReason() : TEXT("-")));
			SlateKeyUp(EKeys::Enter);
			GRun.HoldsCancelled = D ? D->GetHoldsCancelled() : 0;
			return 0.1f;
		}));
		S.Add(Do(TEXT("key hold: pressed again"), []()
		{
			SlateKeyDown(EKeys::Enter);
			return Settings()->GetChoiceHoldSeconds() * 0.5f;
		}));
		S.Add(Do(TEXT("key hold: let go at half"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			Check(D->GetHeldButton() == 0 && D->GetHoldSource() == EChoiceHoldSource::Key, TEXT("key hold: held again"));
			SlateKeyUp(EKeys::Enter);
			return 0.15f;
		}));
		S.Add(Do(TEXT("key hold: let go"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			Check(D && D->GetHoldsCancelled() == GRun.HoldsCancelled + 1 && D->GetLastCancelReason() == TEXT("let go") && D->GetButtonFill(0)->GetPercent() == 0.f && !Term()->HasChosen(),
				FString::Printf(TEXT("key hold: let go at half, the fill empties and nothing is chosen (%s)"), D ? *D->GetLastCancelReason() : TEXT("-")));
			Check(GRun.RingFramesWithDialog == 0 && Cue() && !Cue()->IsRingShown(), FString::Printf(TEXT("ring: no collection ring at the terminal (%d frames)"), GRun.RingFramesWithDialog));
			// Criterion 5: the pawn and the pause while it is open, real keys through Slate.
			D->FocusDefaultWidget();
			GRun.PawnAt = Pawn()->GetActorLocation();
			GRun.ControlAt = PC()->GetControlRotation();
			const ATestLidarCharacter* Character = Cast<ATestLidarCharacter>(Pawn());
			GRun.bFlashlightAt = Character && Character->IsFlashlightOn();
			const FKey Move = TimeStateDebug::BoundKey(W(), TEXT("MoveAction"));
			SlateKeyDown(Move.IsValid() ? Move : FKey(EKeys::W));
			for (const TCHAR* Action : { TEXT("ScanAction"), TEXT("TimeStopAction"), TEXT("FlashlightAction") })
			{
				const FKey Key = TimeStateDebug::BoundKey(W(), Action);
				Note(FString::Printf(TEXT("while open: %s's key %s"), Action, *Key.ToString()));
				if (Key.IsValid())
				{
					SlateKey(Key);
				}
			}
			SlateKey(EKeys::Gamepad_Special_Right);
			const FVector2D At = BelowPanel();
			MouseTo(At + FVector2D(120.0, 40.0));
			MouseTo(At);
			return 0.6f;
		}));
		S.Add(Do(TEXT("while open: the pawn and the pause"), []()
		{
			ReleaseEverything();
			const ATestLidarCharacter* Character = Cast<ATestLidarCharacter>(Pawn());
			const float Moved = FVector::Dist(Pawn()->GetActorLocation(), GRun.PawnAt);
			const bool bLooked = !PC()->GetControlRotation().Equals(GRun.ControlAt, 0.01f);
			Check(Moved < 1.f && !bLooked && !Time()->IsStopped() && Character && Character->IsFlashlightOn() == GRun.bFlashlightAt && Dlg() != nullptr,
				FString::Printf(TEXT("while open: the pawn does not move (%.2f cm), look (%d), Stop, scan or use the flashlight; time stays Normal"), Moved, bLooked ? 1 : 0));
			Check(StdPC() && !StdPC()->IsPausedByMenu() && !StdPC()->PauseGame() && !StdPC()->IsPausedByMenu(), TEXT("while open: the pause menu does not open (its key, and asked for)"));
			SlateKey(BackKey());
			return 0.3f;
		}));
		S.Add(Do(TEXT("back: closed"), []()
		{
			const TimeStateDebug::FInputReport Input = TimeStateDebug::BuildInputReport(W());
			TimeStateDebug::LogInputReport(W(), TEXT("tls.ChamberCheck input (the dialog closed)"));
			Check(Dlg() == nullptr && !Term()->HasChosen() && Term()->OffersChoice() && Prompt() && Prompt()->GetMode() == EInteractPromptMode::Choice,
				TEXT("back: the back key closes the dialog without a choice; the terminal offers again"));
			Check(!PC()->ShouldShowMouseCursor() && StdPC() && StdPC()->bCanPause && Input.IsGameReady(), TEXT("back: the cursor gone, the pause and game input given back"));
			GRun.PawnAt = Pawn()->GetActorLocation();
			const FKey Move = TimeStateDebug::BoundKey(W(), TEXT("MoveAction"));
			SlateKeyDown(Move.IsValid() ? Move : FKey(EKeys::W));
			return 0.5f;
		}));
		S.Add(Do(TEXT("back: the pawn answers"), []()
		{
			ReleaseEverything();
			const float Moved = FVector::Dist2D(Pawn()->GetActorLocation(), GRun.PawnAt);
			Check(Moved > 10.f, FString::Printf(TEXT("back: the pawn answers its keys again (%.0f cm)"), Moved));
			Check(StdPC() && StdPC()->PauseGame() && StdPC()->IsPausedByMenu(), TEXT("back: the pause menu opens again"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("back: resume"), []()
		{
			StdPC()->ResumeGame();
			FaceTerminal(80.f);
			return 0.4f;
		}));
		AddOpenDialog(S, TEXT("back control"));
		S.Add(Do(TEXT("back control: click"), []()
		{
			Click(WidgetPoint(Dlg()->GetBackControl()));
			return 0.3f;
		}));
		S.Add(Do(TEXT("back control: closed"), []()
		{
			Check(Dlg() == nullptr && !Term()->HasChosen() && Term()->OffersChoice(), TEXT("back control: a click on the on-screen Back closes the dialog"));
			Check(GRun.LightsOnMax > GRun.LightsOnMin, FString::Printf(TEXT("room: the rack lights blink (%d to %d on)"), GRun.LightsOnMin, GRun.LightsOnMax));
			return 0.f;
		}));

		// --- Criterion 9: left and resumed ---
		S.Add(Do(TEXT("quit: the pause menu's Main menu"), []()
		{
			Check(Flow()->ReturnToMainMenu(), TEXT("quit: the pause menu's Main menu from the chamber"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("quit: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Check(Run()->HasReturnEntry() && Run()->IsReturningHome(), TEXT("quit: the return entry kept"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("continue: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("continue: back inside the ship in the hangar"), []() { return InReturn(); }, 60.f, []()
		{
			Check(Ret()->GetReturnState() == EPrologueReturnState::InShip && Ret()->IsInsideShip(Pawn()) && FState::Now().Same(GRun.AtExit),
				FString::Printf(TEXT("continue: inside the ship, with the return's snapshot (%s)"), *FState::Now().ToString()));
			ResetLines();
			return 0.2f;
		}));
		AddWayIn(S, TEXT("again"));
		S.Add(WaitFor(TEXT("again: the terminal offers"), []() { return Offering(); }, 90.f, []()
		{
			Check(GRun.EarlyOffers == 0 && LineOf(TEXT("Ending_1")) != nullptr, FString::Printf(TEXT("again: the lines said again, nothing offered before (%s)"), *LinesSaid()));
			FaceTerminal(80.f);
			return 0.4f;
		}));

		// --- Criterion 6: Inject held through with the mouse, at the settings' hold ---
		AddOpenDialog(S, TEXT("inject"));
		S.Add(Do(TEXT("inject: hold the first button with the mouse"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			GRun.MaxFill = 0.f;
			GRun.RingFramesWithDialog = 0;
			GRun.EventsBefore = GRun.TerminalEvents;
			MouseTo(WidgetPoint(D->GetButton(0)));
			GRun.PressRun = RunNow();
			MouseDown(WidgetPoint(D->GetButton(0)));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("inject: held through"), []() { return Term() && Term()->HasChosen(); }, Settings()->GetChoiceHoldSeconds() + 5.f, []()
		{
			MouseUp();
			CheckChosen(TEXT("inject"), TEXT("Inject"), Settings()->GetChoiceHoldSeconds());
			Check(!PC()->ShouldShowMouseCursor(), TEXT("inject: the cursor gone with the dialog"));
			return 0.f;
		}));
		AddChooseAndBack(S, TEXT("inject"), TEXT("Inject"));

		// --- Criterion 6: Open held through with the confirm key, at a hold set for the session ---
		AddToChamber(S, TEXT("open"));
		AddOpenDialog(S, TEXT("open"));
		S.Add(Do(TEXT("open: hold the last button with the confirm key"), []()
		{
			UChoiceDialogWidget* D = Dlg();
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceHoldSeconds = 1.5f;
			GRun.MaxFill = 0.f;
			GRun.RingFramesWithDialog = 0;
			GRun.EventsBefore = GRun.TerminalEvents;
			SlateKey(EKeys::Up);
			Check(D->GetFocusedButton() == 1 && D->GetEndingOfButton(1) == TEXT("Open"), TEXT("open: Up reaches the last button, Open the structure"));
			GRun.PressRun = RunNow();
			SlateKeyDown(EKeys::Enter);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("open: held through"), []() { return Term() && Term()->HasChosen(); }, 6.5f, []()
		{
			SlateKeyUp(EKeys::Enter);
			CheckChosen(TEXT("open"), TEXT("Open"), 1.5f);
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceHoldSeconds = GRun.SavedHold;
			return 0.f;
		}));
		AddChooseAndBack(S, TEXT("open"), TEXT("Open"));

		// --- Criterion 3: with one part less, one button ---
		AddToChamber(S, TEXT("one part less"), []()
		{
			Run()->ClearArchive();
			for (int32 Part = 1; Part < Settings()->GetOpenEndingTwinParts(); ++Part)
			{
				Run()->StoreTwinPart(Part);
			}
		});
		AddOpenDialog(S, TEXT("one part less"));
		S.Add(Do(TEXT("one part less: the dialog"), []()
		{
			const UChoiceDialogWidget* D = Dlg();
			const FText Open = URunSceneSubsystem::GetEndingLabel(TEXT("Open"));
			int32 Named = 0;
			for (const UTextBlock* Text : TextsOf(D))
			{
				Named += Text->GetText().ToString().Contains(Open.ToString()) ? 1 : 0;
			}
			int32 Children = 0;
			if (D && D->WidgetTree)
			{
				if (const UPanelWidget* Box = Cast<UPanelWidget>(D->WidgetTree->FindWidget(TEXT("Box_Buttons"))))
				{
					Children = Box->GetChildrenCount();
				}
			}
			Check(D && D->GetButtonCount() == 1 && D->GetEndingOfButton(0) == TEXT("Inject") && D->GetButton(0)->GetIsEnabled() && Children == 1 && Named == 0,
				FString::Printf(TEXT("one part less: one button, Inject the harvest; no second label, no greyed button, no empty place (%d buttons, %d in the box, %d texts naming Open)"),
					D ? D->GetButtonCount() : -1, Children, Named));
			return 0.f;
		}));
		AddShot(S, TEXT("4_Dialog_OneButton"), 0.2f);
		S.Add(Do(TEXT("one part less: closed"), []()
		{
			SlateKey(BackKey());
			return 0.3f;
		}));

		// --- Criterion 8: a residue still collected with its ring ---
		S.Add(Do(TEXT("residue: Stop"), []()
		{
			Check(Dlg() == nullptr, TEXT("one part less: the back key closes it"));
			FaceTerminal(250.f);
			Time()->Toggle();
			return 0.4f;
		}));
		S.Add(Do(TEXT("residue: one at the player's eye, Interact held"), []()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GRun.BankedBefore = Energy()->GetBankedEnergy();
			GRun.Residue = W()->SpawnActor<AResidue>(AResidue::StaticClass(), Pawn()->GetPawnViewLocation() + Pawn()->GetActorForwardVector() * 30.f, FRotator::ZeroRotator, Params);
			GRun.ResidueRingFrames = 0;
			GRun.DialogsBefore = Term()->GetDialogsOpened();
			PressInteract();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("residue: collected"), []() { return GRun.Residue.IsValid() && GRun.Residue->GetState() == EResidueState::Collected; }, 10.f, []()
		{
			ReleaseInteract();
			Check(GRun.ResidueRingFrames > 0 && Term()->GetDialogsOpened() == GRun.DialogsBefore,
				FString::Printf(TEXT("residue: collected by holding Interact while Stopped, with the collection's ring (%d frames), no dialog"), GRun.ResidueRingFrames));
			return 0.2f;
		}));
		S.Add(Do(TEXT("residue: Resume"), []()
		{
			Check(Cue() && !Cue()->IsRingShown(), TEXT("residue: the ring gone once collected"));
			Time()->Toggle();
			return 0.3f;
		}));
		S.Add(Do(TEXT("shots: the right wall"), []()
		{
			if (AResidue* R = GRun.Residue.Get())
			{
				R->Destroy();
			}
			Energy()->RestoreBankedEnergy(GRun.BankedBefore);
			const FVector Centre = RoomCentre();
			Teleport(Centre + RightDir() * 400.f - InDir() * 120.f);
			LookAt(Centre + RightDir() * 900.f + FVector(0.f, 0.f, 250.f));
			return 0.f;
		}));
		AddShot(S, TEXT("5_RightWall_Closed"), 0.5f);
		S.Add(Do(TEXT("shots: the terminal's back from beside the column"), []()
		{
			const FVector Centre = RoomCentre();
			const FBox B = LocalBody();
			Teleport(Centre - InDir() * (ColumnRadiusAt(10.f) + 45.f) + RightDir() * 130.f);
			LookAt(BodyPoint(B.Min.X, 0.f, B.Max.Z * 0.5f));
			return 0.f;
		}));
		AddShot(S, TEXT("6_Terminal_Back_FromColumn"), 0.5f);
		S.Add(Do(TEXT("one part less: back to the main menu"), []()
		{
			Check(Flow()->ReturnToMainMenu(), TEXT("one part less: back to the main menu"));
			return 0.5f;
		}));

		// --- Criterion 2: the last row dropped at Silent ---
		S.Add(WaitFor(TEXT("silent: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Home()->RestoreState(GRun.SilentBattery, Home()->GetHomeSecondsElapsed());
			Run()->SetReturningHome(true, TEXT("tls.ChamberCheck"));
			ResetLines();
			Check(Run()->TravelToChamber(), TEXT("silent: to the chamber at a Silent battery"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("silent: the terminal offers"), []() { return Offering(); }, 90.f, []()
		{
			const FLine* E = LineOf(NarrativeFixture::GetEngineerLineName(TEXT("Chamber_1"), 1));
			Check(E && E->TypedRun >= 0.0 && !LineOf(TEXT("Chamber_1")) && !LineOf(TEXT("Ending_1")) && Chamber()->GetOfferedRunSeconds() >= E->TypedRun - 0.05 && GRun.EarlyOffers == 0,
				FString::Printf(TEXT("silent: the home AI's rows dropped, the engineer's line still said; the wait ends once it is typed out (%s)"), *LinesSaid()));
			NarrativeFixture::SetEpilogueGroups({ EpilogueGroups::Chamber });
			NarrativeFixture::SetEndingRows(false);
			Check(Flow()->ReturnToMainMenu(), TEXT("silent: back to the main menu"));
			return 0.5f;
		}));

		// --- Criterion 2: no row at all ---
		S.Add(WaitFor(TEXT("no row: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Run()->SetReturningHome(true, TEXT("tls.ChamberCheck"));
			ResetLines();
			Check(Run()->TravelToChamber(), TEXT("no row: to the chamber with no Chamber row and no last message"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("no row: the terminal offers"), []() { return Offering(); }, 60.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			Check(GRun.Lines.IsEmpty() && C->GetLastRow().IsNone() && FMath::Abs(C->GetOfferedRunSeconds() - C->GetLinesStartRunSeconds()) < 0.05f,
				FString::Printf(TEXT("no row: the terminal offers at once (%.3f s, %s)"), C->GetOfferedRunSeconds() - C->GetLinesStartRunSeconds(), *LinesSaid()));
			NarrativeFixture::SetEpilogueGroups({});
			NarrativeFixture::SetEndingRows(true);
			Check(Flow()->ReturnToMainMenu(), TEXT("no row: back to the main menu"));
			return 0.5f;
		}));

		// --- Criterion 9: the switches ---
		S.Add(WaitFor(TEXT("switch: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->bChoiceChamber = false;
			GRun.VisitsBefore = GRun.ChamberVisits;
			Home()->RestoreState(GRun.Battery, Home()->GetHomeSecondsElapsed());
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("switch off: Continue with bChoiceChamber off"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("switch off: in the ship"), []() { return InReturn(); }, 60.f));
		AddWayIn(S, TEXT("switch off"));
		S.Add(WaitFor(TEXT("switch off: the corridor's exit opens the ending's list"), []() { return InMenu() && TopEnding() != nullptr; }, 60.f, []()
		{
			Check(GRun.ChamberVisits == GRun.VisitsBefore && TopEnding()->GetEntryCount() >= 1,
				FString::Printf(TEXT("switch off: the list, as after the way home alone; the chamber never loaded (%s)"), *TimeStateDebug::DescribeChoiceList(W())));
			TimeStateDebug::ClickChoiceEntry(W(), 0);
			if (URunTextSceneWidget* Text = TopScene())
			{
				Text->Confirm();
				Text->Confirm();
			}
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("switch off: back in the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f, []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->bChoiceChamber = true;
			GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = false;
			Check(Run()->TravelToArea(4), TEXT("return off: to area 4 with bReturnHome off"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("return off: area 4"), []() { return InArea(4); }, 180.f, []()
		{
			Teleport(First<AAreaExit>()->GetActorLocation(), true);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("return off: area 4's exit opens the ending's list"), []() { return InMenu() && TopEnding() != nullptr; }, 60.f, []()
		{
			Check(!Run()->IsReturningHome(), TEXT("return off: no way home, no chamber: the list as before"));
			TimeStateDebug::ClickChoiceEntry(W(), 0);
			if (URunTextSceneWidget* Text = TopScene())
			{
				Text->Confirm();
				Text->Confirm();
			}
			GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = true;
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("return off: back in the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f, []()
		{
			// The column's mesh.
			const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/TheLastShift/Art/Prologue/SM_BatteryColumn.SM_BatteryColumn"));
			const int32 Tris = Mesh && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0 ? Mesh->GetRenderData()->LODResources[0].GetNumTriangles() : -1;
			const UMaterialInterface* Material = Mesh && Mesh->GetStaticMaterials().Num() > 0 ? Mesh->GetStaticMaterials()[0].MaterialInterface.Get() : nullptr;
			const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
			const int32 Boxes = Body ? Body->AggGeom.BoxElems.Num() : 0;
			const int32 Convex = Body ? Body->AggGeom.ConvexElems.Num() : 0;
			const FString File = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("TheLastShift/Art/Prologue/SM_BatteryColumn.uasset"));
			const int64 Bytes = IFileManager::Get().FileSize(*File);
			const FString MatPath = Material ? Material->GetPathName() : FString();
			Check(Mesh && Tris > 0 && Tris <= 8000 && Mesh->GetStaticMaterials().Num() == 1 && MatPath.StartsWith(TEXT("/Game/TheLastShift/Art/Materials/MI_Surface_")) && Boxes + Convex > 0,
				FString::Printf(TEXT("mesh: SM_BatteryColumn, %d triangles (at most 8,000), %d slot, %s, collision %d boxes and %d convex, %lld bytes"), Tris,
					Mesh ? Mesh->GetStaticMaterials().Num() : 0, *MatPath, Boxes, Convex, Bytes));
			return 0.f;
		}));
		return S;
	}

	static void RunTest(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: already running."), Name());
			return;
		}
		if (!ResolveGameWorld(InWorld) || !MainMenu())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: run it in MainMenu (PIE)."), Name());
			return;
		}
		GRun = FRun();
		GRun.bQuitAtEnd = Args.Contains(TEXT("Quit")) || Args.Contains(TEXT("quit"));
		GRun.bSavedReturn = Settings()->bReturnHome;
		GRun.bSavedMaps = Settings()->bPrologueMaps;
		GRun.bSavedChamber = Settings()->bChoiceChamber;
		GRun.SavedHold = Settings()->ChoiceHoldSeconds;
		GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = true;
		GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = true;
		GetMutableDefault<UTheLastShiftSettings>()->bChoiceChamber = true;
		// Slate moves its pointer every frame to the platform's cursor, which the test does not drive (the editor may sit
		// behind another application): the pointer events of the test stand for the player's mouse while it runs.
		if (IConsoleVariable* Synthetic = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableSyntheticCursorMoves")))
		{
			GRun.bSavedSynthetic = Synthetic->GetBool();
			Synthetic->Set(false, ECVF_SetByConsole);
		}
		GRun.Steps = BuildTest();
		GRun.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d steps, in %s."), Name(), GRun.Steps.Num(), *MapName());
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	/** tls.Run.Chamber [Battery] [TwinParts]: the run on its way home, then the battery's chamber. */
	static void RunChamber(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		URunSubsystem* R = URunSubsystem::Get(World);
		UHomeStateSubsystem* H = UHomeStateSubsystem::Get(World);
		if (!R || !H)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Run.Chamber: no game world."));
			return;
		}
		if (Args.Num() > 0)
		{
			H->RestoreState(FMath::Clamp(FCString::Atod(*Args[0]), 0.001, 1.0), H->GetHomeSecondsElapsed());
		}
		if (Args.Num() > 1)
		{
			const TArray<int32> Logs = R->GetArchive().LogOrders;
			const int32 Parts = FMath::Clamp(FCString::Atoi(*Args[1]), 0, 99);
			R->ClearArchive();
			R->StoreLogEntries(Logs);
			for (int32 Part = 1; Part <= Parts; ++Part)
			{
				R->StoreTwinPart(Part);
			}
		}
		R->SetReturningHome(true, TEXT("tls.Run.Chamber"));
		const bool bStarted = R->TravelToChamber();
		if (!bStarted)
		{
			R->SetReturningHome(false, TEXT("tls.Run.Chamber refused"));
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Run.Chamber: battery %.3f, %d twin parts (%d asked by Open the structure), %d log entries: %s."), H->GetBatteryFraction(),
			R->GetArchive().TwinPartsPlayed, UTheLastShiftSettings::Get()->GetOpenEndingTwinParts(), R->GetArchive().LogOrders.Num(),
			bStarted ? TEXT("to the battery's chamber") : TEXT("refused (see LogRun)"));
	}

	static FAutoConsoleCommandWithWorldAndArgs GChamberCheckCmd(
		TEXT("tls.ChamberCheck"),
		TEXT("The battery's chamber: the way in, the room and its one terminal, the lines and the wait, the prompt, the choice's dialog (buttons, focus, keys, mouse, holds, the pawn and the pause held, back), each ending held through, a residue, the switches, the column's mesh (fixture rows; shots to Saved/Screenshots/Brief36A). Run in MainMenu, PIE. Args: [Quit]. Clears the save slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTest));

	static FAutoConsoleCommandWithWorldAndArgs GRunChamberCmd(
		TEXT("tls.Run.Chamber"),
		TEXT("Puts the run on its way home (battery and twin AI parts given, else the run's own) and travels to the battery's chamber. Any map. Args: [Battery] [TwinParts]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunChamber));
}

#endif // !UE_BUILD_SHIPPING
