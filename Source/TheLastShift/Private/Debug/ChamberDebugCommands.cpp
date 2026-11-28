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

#include "Algo/Reverse.h"
#include "Area/AreaExit.h"
#include "Audio/GameAudioSubsystem.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/Button.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PanelWidget.h"
#include "Components/SceneCaptureComponent2D.h"
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
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/EntityFigureShape.h"
#include "Epilogue/ChamberBattery.h"
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
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "PhysicsEngine/BodySetup.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Sound/SoundWave.h"
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
#include "Scan/ScanCueWidget.h"
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
		/** tls.Chamber.Flicker: no fixture installed, the save not touched. */
		bool bNoSaveTouch = false;
		// Brief 36B: the hall.
		TArray<FVector> Route;
		int32 RouteNext = -1;
		double LegStartReal = 0.0;
		bool bRouteStuck = false;
		bool bSawGallery = false;
		float MinAxisDistance = 1.0e6f;
		float MaxDeckError = 0.f;
		int32 FallFrames = 0;
		int32 PushIndex = 0;
		int32 PushPhase = 0;
		int32 PushesOk = 0;
		float PushDeckZ = 0.f;
		float PushMoved = 0.f;
		double PushStartReal = 0.0;
		TArray<float> RingsBefore;
		double PhaseBefore = 0.0;
		double PhaseReal = 0.0;
		TArray<FColor> ShotPixels;
		TArray<FColor> SavedPixels;
		int32 ShotW = 0;
		int32 ShotH = 0;
		float SavedMean = -1.f;
		int32 HumRecordFrom = 0;
		float HumBefore = 0.f;
		TArray<float> GpuSamples;
		int32 AftermathPrompts = 0;
		float AftermathLongestFrame = 0.f;
		double AftermathLastRun = -1.0;
		float SavedReach = 230.f;
		float SavedAftermath = 3.f;
		float SavedAftermathHold = 3.f;
		/** The hall as the aftermath left it, read when the hold began. */
		float HoldGlow = 0.f, HoldLights = 0.f, HoldHum = 0.f, HoldRings = 0.f, HoldGauge = 0.f;
		int32 HoldLit = 0;
		/** The ring test: the ring, its turn at the first pose, the pose, and its captures there. */
		int32 RingIndex = -1;
		float RingTurnStart = 0.f;
		FVector RingEye = FVector::ZeroVector;
		FRotator RingRot = FRotator::ZeroRotator;
		float RingFov = 90.f;
		TArray<FLinearColor> RingA, RingA0, RingDepthA;
		FGameSoundSlot SavedHum;
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
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief44"), GRun.ShotName + TEXT(".png")));
		if (!GRun.ShotName.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
		}
		GRun.ShotPixels = MoveTemp(Crop);
		GRun.ShotW = Rect.Width();
		GRun.ShotH = Rect.Height();
		if (!GRun.ShotName.IsEmpty())
		{
			Note(FString::Printf(TEXT("shot %s (%d x %d of a %d x %d capture): %s"), *GRun.ShotName, Rect.Width(), Rect.Height(), Width, Height, *Path));
		}
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
		Mutable->ChoiceTerminalReachCm = GRun.SavedReach;
		Mutable->ChoiceAftermathSeconds = GRun.SavedAftermath;
		Mutable->AftermathHoldSeconds = GRun.SavedAftermathHold;
		Mutable->Audio.ChamberHum = GRun.SavedHum;
		AChamberBattery::SetSessionBattery(-1.f);
		if (AChamberBattery* B = AChamberBattery::Find(W()))
		{
			B->SetHiddenForTest(false);
			B->SetShownWhileStoppedForTest(false);
		}
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
		if (!GRun.bNoSaveTouch)
		{
			NarrativeFixture::Restore(W());
			if (URunSubsystem* R = Run())
			{
				R->ClearSave();
			}
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
			const bool bBefore = C->GetChamberState() == EEpilogueChamberState::Waiting || C->GetChamberState() == EEpilogueChamberState::Lock || C->GetChamberState() == EEpilogueChamberState::Lines;
			if (bBefore && C->Terminal && C->Terminal->OffersChoice())
			{
				++GRun.EarlyOffers;
			}
			if (const UInteractPromptWidget* P = Prompt(); bBefore && P && P->GetMode() != EInteractPromptMode::Hidden)
			{
				++GRun.EarlyPrompts;
			}
			if (const UInteractPromptWidget* P = Prompt(); (C->GetChamberState() == EEpilogueChamberState::Aftermath || C->GetChamberState() == EEpilogueChamberState::Hold) && ((P && P->GetMode() != EInteractPromptMode::Hidden) || (C->Terminal && C->Terminal->OffersChoice())))
			{
				++GRun.AftermathPrompts;
			}
			// The longest frame of the run's clock from the choice to the ending's travel, the frame that crosses the aftermath's end
			// included: the aftermath ends at the first frame past its length, and the ending is asked the frame after.
			if (C->GetChosenRunSeconds() >= 0.f && (C->GetChamberState() == EEpilogueChamberState::Aftermath || C->GetChamberState() == EEpilogueChamberState::Hold || (C->GetChamberState() == EEpilogueChamberState::Ended && GRun.AftermathLastRun >= 0.0)))
			{
				if (GRun.AftermathLastRun >= 0.0)
				{
					GRun.AftermathLongestFrame = FMath::Max(GRun.AftermathLongestFrame, static_cast<float>(Now - GRun.AftermathLastRun));
				}
				GRun.AftermathLastRun = Now;
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

	/** The terminal's body box in the world: its eight corners and its faces through the actor's transform. */
	static FBox LocalBody() { return Term() ? Term()->GetLocalBodyBox() : FBox(ForceInit); }
	static FVector BodyPoint(float LX, float LY, float LZ) { return Term() ? Term()->GetActorTransform().TransformPosition(FVector(LX, LY, LZ)) : FVector::ZeroVector; }

	static void StandOn(const FVector& At, float FromZ);

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
		StandOn(BodyPoint(B.Max.X, 0.f, 0.f) + Fwd * Distance, T->GetActorLocation().Z + 250.f);
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

	/** Two triangles of one plane (normal N) overlap by more than 1 mm in it: no separating axis among their six edges' normals. */
	static bool Tri2DOverlap(const FVector& A0, const FVector& B0, const FVector& C0, const FVector& A1, const FVector& B1, const FVector& C1, const FVector& N)
	{
		const FVector U = (B0 - A0).GetSafeNormal();
		const FVector V = FVector::CrossProduct(N, U);
		const FVector2D P[2][3] = {
			{ FVector2D(FVector::DotProduct(A0, U), FVector::DotProduct(A0, V)), FVector2D(FVector::DotProduct(B0, U), FVector::DotProduct(B0, V)), FVector2D(FVector::DotProduct(C0, U), FVector::DotProduct(C0, V)) },
			{ FVector2D(FVector::DotProduct(A1, U), FVector::DotProduct(A1, V)), FVector2D(FVector::DotProduct(B1, U), FVector::DotProduct(B1, V)), FVector2D(FVector::DotProduct(C1, U), FVector::DotProduct(C1, V)) } };
		for (int32 T = 0; T < 2; ++T)
		{
			for (int32 E = 0; E < 3; ++E)
			{
				const FVector2D Edge = P[T][(E + 1) % 3] - P[T][E];
				const FVector2D Axis = FVector2D(-Edge.Y, Edge.X).GetSafeNormal();
				if (Axis.IsNearlyZero())
				{
					continue;
				}
				double Min[2] = { 1.0e9, 1.0e9 };
				double Max[2] = { -1.0e9, -1.0e9 };
				for (int32 K = 0; K < 2; ++K)
				{
					for (int32 I = 0; I < 3; ++I)
					{
						const double D = FVector2D::DotProduct(P[K][I], Axis);
						Min[K] = FMath::Min(Min[K], D);
						Max[K] = FMath::Max(Max[K], D);
					}
				}
				if (Max[0] < Min[1] + 0.1 || Max[1] < Min[0] + 0.1)
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool TriBoxOverlap(const FVector& Centre, const FVector& Half, FVector A, FVector B, FVector C)
	{
		A -= Centre;
		B -= Centre;
		C -= Centre;
		auto Separates = [&](const FVector& Axis)
		{
			if (Axis.SizeSquared() < 1.e-12)
			{
				return false;
			}
			const double P0 = FVector::DotProduct(A, Axis), P1 = FVector::DotProduct(B, Axis), P2 = FVector::DotProduct(C, Axis);
			const double R = Half.X * FMath::Abs(Axis.X) + Half.Y * FMath::Abs(Axis.Y) + Half.Z * FMath::Abs(Axis.Z);
			return FMath::Min3(P0, P1, P2) > R || FMath::Max3(P0, P1, P2) < -R;
		};
		const FVector Edges[3] = { B - A, C - B, A - C };
		const FVector Axes[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
		for (const FVector& Edge : Edges)
		{
			for (const FVector& Axis : Axes)
			{
				if (Separates(FVector::CrossProduct(Axis, Edge)))
				{
					return false;
				}
			}
		}
		for (const FVector& Axis : Axes)
		{
			if (Separates(Axis))
			{
				return false;
			}
		}
		return !Separates(FVector::CrossProduct(Edges[0], Edges[1]));
	}

	// ============================================================== Brief 36B: the battery's access hall
	static AChamberBattery* Bat() { return AChamberBattery::Find(W()); }
	static APrologueMonitorRoom* Mon() { return First<APrologueMonitorRoom>(); }
	static UGameAudioSubsystem* Aud() { return UGameAudioSubsystem::Get(W()); }
	static float HalfHeight()
	{
		const ACharacter* C = Cast<ACharacter>(Pawn());
		return C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 92.f;
	}

	/** The pawn stands on whatever floor lies under (X, Y) below FromZ. */
	static void StandOn(const FVector& At, float FromZ)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("ChamberStand"), false, Pawn());
		const FVector From(At.X, At.Y, FromZ);
		const bool bHit = W()->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 4000.f), ECC_Pawn, Params);
		Teleport(FVector(At.X, At.Y, (bHit ? Hit.ImpactPoint.Z : 0.f) + HalfHeight() + 2.f), true);
	}

	/** The deck or floor the plan puts under (X, Y): the lock and the gallery, the flights, the landing, the platform, the floor. */
	static float PlanFloorZ(const FVector& P)
	{
		if (P.Y >= 1600.f && FMath::Abs(P.X) <= 300.f) return 700.f;
		if (P.Y >= 1700.f && P.X >= -1700.f && P.X < -300.f) return 350.f + (P.X + 1700.f) * 0.25f;
		if (P.Y >= 1700.f && P.X < -1700.f) return 350.f;
		if (P.X < -1700.f && P.Y >= 300.f && P.Y < 1700.f) return (P.Y - 300.f) * 0.25f;
		if (FMath::Abs(P.X) <= 150.f && P.Y >= 422.f && P.Y <= 622.f) return 15.f;
		return 0.f;
	}

	static TArray<UStaticMeshComponent*> MeshComps(const TCHAR* Name)
	{
		TArray<UStaticMeshComponent*> Out;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() == W() && It->IsRegistered() && It->GetStaticMesh() && It->GetStaticMesh()->GetName() == Name && !Cast<UInstancedStaticMeshComponent>(*It))
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetComponentLocation().Z < B.GetComponentLocation().Z; });
		return Out;
	}

	/** A point is on the player's screen and nothing hides it from the camera (a hit within Slack of it counts as the point). */
	static bool Seen(const FVector& Point, float Slack = 40.f)
	{
		int32 SX = 0, SY = 0;
		PC()->GetViewportSize(SX, SY);
		FVector2D Screen;
		if (!PC()->ProjectWorldLocationToScreen(Point, Screen, false) || Screen.X < 0.0 || Screen.Y < 0.0 || Screen.X > SX || Screen.Y > SY)
		{
			return false;
		}
		// The railings are thin rails: seen through, though their collision is one box.
		const FVector Camera = PC()->PlayerCameraManager->GetCameraLocation();
		FCollisionQueryParams Params(TEXT("ChamberSeen"), false, Pawn());
		for (UStaticMeshComponent* R : MeshComps(TEXT("SM_KitRailing")))
		{
			Params.AddIgnoredComponent(R);
		}
		for (UStaticMeshComponent* R : MeshComps(TEXT("SM_KitRailingRamp")))
		{
			Params.AddIgnoredComponent(R);
		}
		FHitResult Hit;
		const bool bHit = W()->LineTraceSingleByChannel(Hit, Camera, Point, ECC_Visibility, Params);
		return !bHit || Hit.Distance >= FVector::Dist(Camera, Point) - Slack;
	}

	/** The mean level (0 to 255, the brightest channel) of the last shot in a square of half size R around a world point; -1 off screen. */
	static float ShotMean(const FVector& Point, int32 R)
	{
		int32 SX = 0, SY = 0;
		PC()->GetViewportSize(SX, SY);
		FVector2D Screen;
		if (GRun.ShotW <= 0 || SX <= 0 || !PC()->ProjectWorldLocationToScreen(Point, Screen, false))
		{
			return -1.f;
		}
		const int32 CX = FMath::RoundToInt(Screen.X * GRun.ShotW / SX);
		const int32 CY = FMath::RoundToInt(Screen.Y * GRun.ShotH / SY);
		double Sum = 0.0;
		int32 N = 0;
		for (int32 Y = CY - R; Y <= CY + R; ++Y)
		{
			for (int32 X = CX - R; X <= CX + R; ++X)
			{
				if (X >= 0 && Y >= 0 && X < GRun.ShotW && Y < GRun.ShotH)
				{
					const FColor& C = GRun.ShotPixels[Y * GRun.ShotW + X];
					Sum += FMath::Max3(C.R, C.G, C.B);
					++N;
				}
			}
		}
		return N > 0 ? static_cast<float>(Sum / N) : -1.f;
	}

	/** The brightest pixel of the last shot, its outer 2 pixels left out (the crop of a capture of the editor's window can keep a line of the viewport's border). */
	static int32 ShotMax()
	{
		int32 Max = 0;
		for (int32 Y = 2; Y < GRun.ShotH - 2; ++Y)
		{
			for (int32 X = 2; X < GRun.ShotW - 2; ++X)
			{
				const FColor& C = GRun.ShotPixels[Y * GRun.ShotW + X];
				Max = FMath::Max(Max, static_cast<int32>(FMath::Max3(C.R, C.G, C.B)));
			}
		}
		return Max;
	}

	// --- The rings' surface (follow-up of the battery's hall): base colour captures of one ring alone ---

	static const int32 RingCaptureW = 640;
	static const int32 RingCaptureH = 360;

	/**
	 * Captures Ring alone (the show-only list) from Eye along Rotation, FovDeg across: its base colour (no light, so the
	 * light falling on it does not count) or its scene depth (nothing drawn reads as a very large depth). Empty on failure.
	 */
	static bool CaptureRing(UStaticMeshComponent* Ring, const FVector& Eye, const FRotator& Rotation, float FovDeg, bool bDepth, TArray<FLinearColor>& Out)
	{
		Out.Reset();
		UWorld* World = W();
		APawn* P = Pawn();
		if (!World || !P || !Ring)
		{
			return false;
		}
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->RenderTargetFormat = RTF_RGBA32f;
		Target->ClearColor = bDepth ? FLinearColor(1.0e9f, 0.f, 0.f, 0.f) : FLinearColor::Black;
		Target->InitAutoFormat(RingCaptureW, RingCaptureH);
		Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(P);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = bDepth ? SCS_SceneDepth : SCS_BaseColor;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyComponents.Add(Ring);
		Capture->FOVAngle = FovDeg;
		Capture->TextureTarget = Target;
		Capture->RegisterComponentWithWorld(World);
		Capture->SetWorldLocationAndRotation(Eye, Rotation);
		Capture->CaptureScene();
		FlushRenderingCommands();
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bOk = Resource && Resource->ReadLinearColorPixels(Out) && Out.Num() == RingCaptureW * RingCaptureH;
		Capture->DestroyComponent();
		return bOk;
	}

	/** Captures the ring's base colour with its surface mapped in its own space (ObjectSpace 1) and in world space (0). */
	static bool CaptureRingBoth(UStaticMeshComponent* Ring, const FVector& Eye, const FRotator& Rotation, float FovDeg, TArray<FLinearColor>& OutOwn, TArray<FLinearColor>& OutWorld)
	{
		UMaterialInstanceDynamic* Mid = Bat() ? Bat()->GetRingMaterial() : nullptr;
		if (!Mid)
		{
			return false;
		}
		const bool bOwn = CaptureRing(Ring, Eye, Rotation, FovDeg, false, OutOwn);
		Mid->SetScalarParameterValue(TEXT("ObjectSpace"), 0.f);
		const bool bWorld = CaptureRing(Ring, Eye, Rotation, FovDeg, false, OutWorld);
		Mid->SetScalarParameterValue(TEXT("ObjectSpace"), 1.f);
		return bOwn && bWorld;
	}

	/**
	 * Mean and share above 4 levels of the difference over the ring's pixels: 8-bit levels of the base colour as shown on
	 * screen (sRGB), its largest channel. The battery's family is dark (a linear base colour near 0.035), so its details
	 * differ by a fraction of a level in linear terms and by a few once encoded for the screen.
	 */
	static void CompareRing(const TArray<FLinearColor>& A, const TArray<FLinearColor>& B, const TArray<FLinearColor>& DepthA, const TArray<FLinearColor>& DepthB, double& OutMean,
		double& OutShare, int32& OutPixels)
	{
		OutMean = OutShare = 0.0;
		OutPixels = 0;
		int32 Above = 0;
		for (int32 I = 0; I < A.Num() && I < B.Num() && I < DepthA.Num() && I < DepthB.Num(); ++I)
		{
			if (DepthA[I].R > 1.0e7f || DepthB[I].R > 1.0e7f)
			{
				continue;
			}
			const FColor SA = FLinearColor(A[I].R, A[I].G, A[I].B, 1.f).ToFColorSRGB();
			const FColor SB = FLinearColor(B[I].R, B[I].G, B[I].B, 1.f).ToFColorSRGB();
			const double D = FMath::Max3(FMath::Abs(SA.R - SB.R), FMath::Abs(SA.G - SB.G), FMath::Abs(SA.B - SB.B));
			OutMean += D;
			Above += D > 4.0 ? 1 : 0;
			++OutPixels;
		}
		if (OutPixels > 0)
		{
			OutMean /= OutPixels;
			OutShare = static_cast<double>(Above) / OutPixels;
		}
	}

	/** Eye and Rotation turned by Degrees about the vertical through Pivot. */
	static void OrbitView(const FVector& Pivot, float Degrees, FVector& InOutEye, FRotator& InOutRotation)
	{
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Degrees));
		InOutEye = Pivot + Turn.RotateVector(InOutEye - Pivot);
		InOutRotation = (Turn * InOutRotation.Quaternion()).Rotator();
	}

	/** Waits for the last requested shot to be written. */
	static void AddShotWait(TArray<FStep>& S, const FString& ShotName, TFunction<float()> Then = nullptr)
	{
		S.Add(Do(FString::Printf(TEXT("shot %s"), *ShotName), [ShotName]() { RequestShot(ShotName); return 0.f; }));
		S.Add(WaitFor(FString::Printf(TEXT("shot %s written"), *ShotName), []() { return GRun.bShotReady; }, 10.f, Then));
	}

	static float Lerp01(float A, float B, float T) { return A + (B - A) * FMath::Clamp(T, 0.f, 1.f); }

	/** The walker: sends movement input toward the route's next point; true when the route is done. */
	static bool WalkRoute()
	{
		APawn* P = Pawn();
		if (!P || GRun.RouteNext < 0 || GRun.RouteNext >= GRun.Route.Num())
		{
			return true;
		}
		const FVector At = P->GetActorLocation();
		const float Foot = At.Z - HalfHeight();
		GRun.MinAxisDistance = FMath::Min(GRun.MinAxisDistance, static_cast<float>(FVector::Dist2D(At, FVector::ZeroVector)));
		GRun.MaxDeckError = FMath::Max(GRun.MaxDeckError, FMath::Abs(Foot - PlanFloorZ(At)));
		if (const ACharacter* C = Cast<ACharacter>(P); C && C->GetCharacterMovement()->IsFalling() && C->GetVelocity().Z < -350.f)
		{
			++GRun.FallFrames;
		}
		if (const AEpilogueChamber* Ch = Chamber(); Ch && At.Y > 1700.f && FMath::Abs(At.X) < 250.f && At.Z > 600.f)
		{
			GRun.bSawGallery = true;
		}
		const FVector Goal = GRun.Route[GRun.RouteNext];
		if (FVector::Dist2D(At, Goal) < 45.f)
		{
			++GRun.RouteNext;
			GRun.LegStartReal = FPlatformTime::Seconds();
			return GRun.RouteNext >= GRun.Route.Num();
		}
		if (FPlatformTime::Seconds() - GRun.LegStartReal > 25.0)
		{
			GRun.bRouteStuck = true;
			return true;
		}
		const FVector Dir = (Goal - At).GetSafeNormal2D();
		PC()->SetControlRotation(FRotator(-5.f, Dir.Rotation().Yaw, 0.f));
		P->AddMovementInput(Dir, 1.f);
		return false;
	}

	static void StartRoute(const TArray<FVector>& Points)
	{
		GRun.Route = Points;
		GRun.RouteNext = 0;
		GRun.LegStartReal = FPlatformTime::Seconds();
		GRun.bRouteStuck = false;
		GRun.MinAxisDistance = 1.0e6f;
		GRun.MaxDeckError = 0.f;
		GRun.FallFrames = 0;
	}

	static TArray<FVector> WayDown()
	{
		return { { 0.f, 1800.f, 0.f }, { -350.f, 1845.f, 0.f }, { -1000.f, 1845.f, 0.f }, { -1600.f, 1845.f, 0.f }, { -1845.f, 1845.f, 0.f }, { -1845.f, 1600.f, 0.f },
			{ -1845.f, 1000.f, 0.f }, { -1850.f, 250.f, 0.f }, { -900.f, 440.f, 0.f }, { 0.f, 640.f, 0.f } };
	}

	static TArray<FVector> WayUp()
	{
		TArray<FVector> Up = WayDown();
		Algo::Reverse(Up);
		Up.RemoveAt(0);
		Up.Add({ 0.f, 2150.f, 0.f });
		return Up;
	}

	static void AddWalk(TArray<FStep>& S, const FString& L, bool bDown)
	{
		S.Add(Do(L + TEXT(": start"), [bDown]() { StartRoute(bDown ? WayDown() : WayUp()); return 0.f; }));
		S.Add(WaitFor(L + TEXT(": walked"), []() { return WalkRoute(); }, 180.f, [L, bDown]()
		{
			const FVector At = Pawn()->GetActorLocation();
			const FVector Goal = bDown ? FVector(0.f, 640.f, 0.f) : FVector(0.f, 2150.f, 0.f);
			Check(!GRun.bRouteStuck && FVector::Dist2D(At, Goal) < 60.f && GRun.FallFrames == 0 && GRun.MaxDeckError <= 30.f && GRun.MinAxisDistance > 450.f,
				FString::Printf(TEXT("%s: %s, every leg walked (stuck %d), no fall (%d frames), on the plan's decks all the way (largest foot error %.1f cm), never nearer the axis than %.0f cm (the parapet: 420)"),
					*L, bDown ? TEXT("from the lock to the terminal along both flights") : TEXT("back from the terminal to the lock"), GRun.bRouteStuck ? 1 : 0, GRun.FallFrames,
					GRun.MaxDeckError, GRun.MinAxisDistance));
			return 0.2f;
		}));
	}

	struct FPush
	{
		FVector From;
		FVector Dir;
		FVector Edge;   // a point of the edge pushed against (railings) or the axis (the parapet)
		bool bParapet = false;
		FString Name;
	};

	static TArray<FPush> Pushes()
	{
		TArray<FPush> Out;
		const auto Add = [&Out](const FVector& From, const FVector& Dir, const FVector& Edge, const TCHAR* Name)
		{
			FPush P;
			P.From = From;
			P.Dir = Dir.GetSafeNormal2D();
			P.Edge = Edge;
			P.Name = Name;
			Out.Add(P);
		};
		for (const float X : { -250.f, 0.f, 250.f }) Add({ X, 1660.f, 0.f }, { 0.f, -1.f, 0.f }, { X, 1600.f, 0.f }, TEXT("the gallery's front railing"));
		for (const float Y : { 1700.f, 1880.f }) Add({ 240.f, Y, 0.f }, { 1.f, 0.f, 0.f }, { 300.f, Y, 0.f }, TEXT("the gallery's right railing"));
		Add({ -240.f, 1650.f, 0.f }, { -1.f, 0.f, 0.f }, { -300.f, 1650.f, 0.f }, TEXT("the gallery's left railing"));
		for (const float X : { -1500.f, -1000.f, -500.f }) Add({ X, 1760.f, 0.f }, { 0.f, -1.f, 0.f }, { X, 1700.f, 0.f }, TEXT("the first flight's railing"));
		Add({ -1760.f, 1760.f, 0.f }, { 1.f, -1.f, 0.f }, { -1700.f, 1700.f, 0.f }, TEXT("the landing's corner"));
		for (const float Y : { 500.f, 1000.f, 1500.f }) Add({ -1760.f, Y, 0.f }, { 1.f, 0.f, 0.f }, { -1700.f, Y, 0.f }, TEXT("the second flight's railing"));
		for (int32 K = 0; K < 16; ++K)
		{
			const float A = FMath::DegreesToRadians(11.25f + 22.5f * K);
			FPush P;
			P.From = FVector(520.f * FMath::Cos(A), 520.f * FMath::Sin(A), 0.f);
			P.Dir = -P.From.GetSafeNormal2D();
			P.Edge = FVector::ZeroVector;
			P.bParapet = true;
			P.Name = TEXT("the parapet");
			// The portal's platform and posts stand against the parapet on the entry side: pushed from the platform's sides there.
			if (FMath::Abs(P.From.X) < 170.f && P.From.Y > 0.f)
			{
				continue;
			}
			Out.Add(P);
		}
		return Out;
	}

	static bool RunPushes()
	{
		const TArray<FPush> All = Pushes();
		if (GRun.PushIndex >= All.Num())
		{
			return true;
		}
		const FPush& P = All[GRun.PushIndex];
		const double Now = FPlatformTime::Seconds();
		if (GRun.PushPhase == 0)
		{
			StandOn(P.From, P.From.Y > 1600.f || P.From.X < -1700.f ? 1000.f : 200.f);
			GRun.PushDeckZ = PlanFloorZ(P.From);
			GRun.PushStartReal = Now;
			GRun.PushPhase = 1;
			return false;
		}
		if (Now - GRun.PushStartReal < 0.8)
		{
			Pawn()->AddMovementInput(P.Dir, 1.f);
			return false;
		}
		const FVector At = Pawn()->GetActorLocation();
		const float Foot = At.Z - HalfHeight();
		const bool bOnDeck = FMath::Abs(Foot - PlanFloorZ(At)) <= 30.f && FMath::Abs(Foot - GRun.PushDeckZ) <= 60.f;
		const bool bInside = P.bParapet ? FVector::Dist2D(At, FVector::ZeroVector) > 420.f : FVector::DotProduct(At - P.Edge, P.Dir) < 0.f;
		const float Moved = FVector::Dist2D(At, P.From);
		GRun.PushesOk += bOnDeck && bInside ? 1 : 0;
		if (!(bOnDeck && bInside))
		{
			Note(FString::Printf(TEXT("push %d against %s FAILED: at %s (foot %.0f, deck %.0f)"), GRun.PushIndex, *P.Name, *At.ToCompactString(), Foot, GRun.PushDeckZ));
		}
		GRun.PushMoved = FMath::Max(GRun.PushMoved, Moved);
		++GRun.PushIndex;
		GRun.PushPhase = 0;
		return GRun.PushIndex >= All.Num();
	}

	/** Ring Index's yaw in the world now. */
	static TArray<float> RingTurns()
	{
		TArray<float> Out;
		if (const AChamberBattery* B = Bat())
		{
			for (int32 I = 0; I < B->GetRings().Num(); ++I)
			{
				Out.Add(B->GetRingTurn(I));
			}
		}
		return Out;
	}

	/** The hum's linear distance level (1 within its full radius, 0 past the falloff) at a point. */
	static float HumLevelAt(const FVector& Point)
	{
		const UAudioComponent* C = Aud() ? Aud()->GetHumComponent() : nullptr;
		if (!C)
		{
			return -1.f;
		}
		const float D = FVector::Dist(C->GetComponentLocation(), Point);
		const float Inner = C->AttenuationOverrides.AttenuationShapeExtents.X;
		return 1.f - FMath::Clamp((D - Inner) / FMath::Max(C->AttenuationOverrides.FalloffDistance, 1.f), 0.f, 1.f);
	}

	/** Every check of a battery level shown for the session: the glow, the pulses, the monitors, the hum. */
	static void CheckLevel(float Battery, const FString& L)
	{
		const AChamberBattery* B = Bat();
		const APrologueMonitorRoom* M = Mon();
		const FBatteryLook Look = Settings()->GetBatteryLook();
		const FGameAudio& A = Settings()->Audio;
		const float WantGlow = Lerp01(Look.GlowLevelEmpty, Look.GlowLevelFull, Battery);
		const float WantSpeed = Lerp01(Look.PulseSpeedEmpty, Look.PulseSpeedFull, Battery);
		float MidLevel = -1.f;
		if (UMaterialInstanceDynamic* Mid = B ? B->GetGlowMaterial(true) : nullptr)
		{
			Mid->GetScalarParameterValue(FName(TEXT("Level")), MidLevel);
		}
		Check(B && FMath::IsNearlyEqual(B->GetBatteryShown(), Battery, 1.e-3f) && FMath::IsNearlyEqual(B->GetGlowLevel(), WantGlow, 1.e-3f) && FMath::IsNearlyEqual(B->GetPulseSpeed(), WantSpeed, 1.e-3f)
			&& FMath::IsNearlyEqual(MidLevel, WantGlow, 1.e-3f) && B->GetGlowLevel() > 0.f && B->GetPulseSpeed() > 0.f,
			FString::Printf(TEXT("%s: the glow at %.3f (BatteryLook: %.3f) and the pulses at %.3f m/s (%.3f), never dark and never still"), *L, B ? B->GetGlowLevel() : -1.f, WantGlow,
				B ? B->GetPulseSpeed() : -1.f, WantSpeed));
		float Layout0 = -1.f;
		if (UMaterialInstanceDynamic* Mid = M ? M->GetLayoutMaterial(0) : nullptr)
		{
			Mid->GetScalarParameterValue(FName(TEXT("Level")), Layout0);
		}
		Check(M && FMath::IsNearlyEqual(M->GetScreenLevel(), Battery, 1.e-3f) && FMath::IsNearlyEqual(Layout0, Battery, 1.e-3f) && M->GetLitShownCount() == M->GetLitPlates().Num(),
			FString::Printf(TEXT("%s: every gauge of the %d lit screens at %.3f (%d shown)"), *L, M ? M->GetLitPlates().Num() : 0, M ? M->GetScreenLevel() : -1.f, M ? M->GetLitShownCount() : 0));
		const float WantVolume = A.GetVolume(EGameSound::ChamberHum) * A.GetHumVolumeAt(Battery);
		const float WantPitch = A.GetHumPitchAt(Battery);
		Check(Aud() && Aud()->IsHumPlaying() && FMath::IsNearlyEqual(Aud()->GetHumVolume(), WantVolume, 2.e-3f) && FMath::IsNearlyEqual(Aud()->GetHumPitch(), WantPitch, 2.e-3f) && Aud()->GetHumVolume() > 0.f,
			FString::Printf(TEXT("%s: the hum at volume %.3f (%.3f) and pitch %.3f (%.3f), playing, not silent"), *L, Aud() ? Aud()->GetHumVolume() : -1.f, WantVolume, Aud() ? Aud()->GetHumPitch() : -1.f, WantPitch));
	}

	/**
	 * The column's faces of different pieces within 0.95 cm of one plane and overlapping in it by more than 1 mm (by their LOD 0
	 * triangles): Same counts the pairs that face the same way (the only ones that can fight for a pixel), Facing the pairs that
	 * face each other or stand back to back (unseen). SpineScale scales the spines and their glows across their width (local Y)
	 * about their own frame, for a fit tried without moving anything.
	 */
	struct FColumnFaces
	{
		int32 Same = 0;
		int32 Facing = 0;
		int32 Tris = 0;
		int32 Pieces = 0;
		TMap<FString, int32> SameByPair;
		TMap<FString, int32> FacingByPair;
		TArray<FString> Samples;
	};

	static FColumnFaces ColumnFaces(float SpineScale, bool bSamples, float SpineDz = 0.f, float SpineYawDeg = 0.f)
	{
		FColumnFaces Out;
		TArray<const UStaticMeshComponent*> Pieces;
		for (const TCHAR* N : { TEXT("SM_BatteryTier"), TEXT("SM_BatteryTierB"), TEXT("SM_BatteryTierGlow"), TEXT("SM_BatteryTierBGlow"), TEXT("SM_BatteryRing"), TEXT("SM_BatterySpine"),
			TEXT("SM_BatterySpineGlow"), TEXT("SM_BatteryDuct"), TEXT("SM_BatteryDuctSocket") })
		{
			for (const UStaticMeshComponent* C : MeshComps(N))
			{
				Pieces.Add(C);
			}
		}
		struct FTri { FVector A, B, C, N; int32 Piece; };
		TMultiMap<FIntVector, int32> Buckets;
		TArray<FTri> Tris;
		for (int32 P = 0; P < Pieces.Num(); ++P)
		{
			static TMap<const UStaticMesh*, TPair<TArray<FVector>, TArray<uint32>>> Read;
			TPair<TArray<FVector>, TArray<uint32>>* Cached = Read.Find(Pieces[P]->GetStaticMesh());
			if (!Cached)
			{
				Cached = &Read.Add(Pieces[P]->GetStaticMesh());
				FEntityFigureShape::ReadMeshTriangles(Pieces[P]->GetStaticMesh(), Cached->Key, Cached->Value);
			}
			const TArray<FVector>& V = Cached->Key;
			const TArray<uint32>& I = Cached->Value;
			if (I.Num() == 0)
			{
				continue;
			}
			const bool bSpine = Pieces[P]->GetStaticMesh()->GetName().StartsWith(TEXT("SM_BatterySpine"));
			const FVector Local(1.0, bSpine ? SpineScale : 1.0, 1.0);
			FTransform T = Pieces[P]->GetComponentTransform();
			if (bSpine && (SpineDz != 0.f || SpineYawDeg != 0.f))
			{
				// Turned about the column's axis (the spines' pivot is on it) and raised.
				T = T * FTransform(FRotator(0.f, SpineYawDeg, 0.f), FVector(0.f, 0.f, SpineDz));
			}
			for (int32 K = 0; K + 2 < I.Num(); K += 3)
			{
				FTri Tri;
				Tri.A = T.TransformPosition(V[I[K]] * Local);
				Tri.B = T.TransformPosition(V[I[K + 1]] * Local);
				Tri.C = T.TransformPosition(V[I[K + 2]] * Local);
				const FVector Cross = FVector::CrossProduct(Tri.B - Tri.A, Tri.C - Tri.A);
				if (Cross.Size() < 1.0)
				{
					continue;   // under half a square centimetre: no visible face
				}
				Tri.N = Cross.GetSafeNormal();
				Tri.Piece = P;
				// The plane's line (the normal up to its sign, to 3 degrees) and its distance from the origin, to 1 cm.
				const FVector Line = (Tri.N.X < -1.e-3 || (FMath::Abs(Tri.N.X) <= 1.e-3 && (Tri.N.Y < -1.e-3 || (FMath::Abs(Tri.N.Y) <= 1.e-3 && Tri.N.Z < 0.0)))) ? -Tri.N : Tri.N;
				const FIntVector Key(FMath::RoundToInt(Line.X * 20.0), FMath::RoundToInt(Line.Y * 20.0) * 1000 + FMath::RoundToInt(Line.Z * 20.0), FMath::RoundToInt(FVector::DotProduct(Line, Tri.A)));
				Buckets.Add(Key, Tris.Add(Tri));
			}
		}
		Out.Tris = Tris.Num();
		Out.Pieces = Pieces.Num();
		TSet<FIntVector> Keys;
		Buckets.GetKeys(Keys);
		for (const FIntVector& Key : Keys)
		{
			TArray<int32> Here;
			Buckets.MultiFind(Key, Here);
			TArray<int32> Near;
			for (int32 Dz = 0; Dz <= 1; ++Dz)
			{
				TArray<int32> Part;
				Buckets.MultiFind(FIntVector(Key.X, Key.Y, Key.Z + Dz), Part);
				Near.Append(Part);
			}
			for (const int32 IA : Here)
			{
				for (const int32 IB : Near)
				{
					if (IB <= IA && Buckets.FindPair(Key, IB))
					{
						continue;   // a pair of one bucket counted once
					}
					const FTri& TA = Tris[IA];
					const FTri& TB = Tris[IB];
					const double Dot = FVector::DotProduct(TA.N, TB.N);
					// Within 1 cm: closer than 0.95 cm (the glow strips stand 1.0 cm off their faces by design, the modelling session's measure).
					if (TA.Piece == TB.Piece || FMath::Abs(Dot) < 0.998 || FMath::Abs(FVector::DotProduct(TA.N, TB.A - TA.A)) >= 0.95)
					{
						continue;
					}
					FBox BA(ForceInit), BB(ForceInit);
					BA += TA.A; BA += TA.B; BA += TA.C;
					BB += TB.A; BB += TB.B; BB += TB.C;
					if (!BA.ExpandBy(0.5).Intersect(BB.ExpandBy(0.5)) || !Tri2DOverlap(TA.A, TA.B, TA.C, TB.A, TB.B, TB.C, TA.N))
					{
						continue;
					}
					const bool bSame = Dot > 0.0;
					const FString NA = Pieces[TA.Piece]->GetStaticMesh()->GetName();
					const FString NB = Pieces[TB.Piece]->GetStaticMesh()->GetName();
					const FString Pair = NA < NB ? NA + TEXT("|") + NB : NB + TEXT("|") + NA;
					int32& Count = (bSame ? Out.SameByPair : Out.FacingByPair).FindOrAdd(Pair);
					if (bSamples && bSame && Count < 4)
					{
						const FVector At = (TA.A + TA.B + TA.C) / 3.0;
						const FTransform& PT = Pieces[TA.Piece]->GetComponentTransform();
						Out.Samples.Add(FString::Printf(TEXT("%s on %s and %s on %s: at %s (%.0f cm up the column, %.0f degrees round it), normal %s (%s of the first piece's frame), %.2f cm apart"),
							*NA, *GetNameSafe(Pieces[TA.Piece]->GetOwner()), *NB, *GetNameSafe(Pieces[TB.Piece]->GetOwner()), *At.ToCompactString(), At.Z,
							FRotator::ClampAxis(FMath::RadiansToDegrees(FMath::Atan2(At.Y, At.X))), *TA.N.ToCompactString(), *PT.InverseTransformVectorNoScale(TA.N).ToCompactString(),
							FVector::DotProduct(TA.N, TB.A - TA.A)));
					}
					++Count;
					(bSame ? Out.Same : Out.Facing) += 1;
				}
			}
		}
		return Out;
	}

	static void AddHall(TArray<FStep>& S)
	{
		// --- Criterion 1: the way in ---
		S.Add(Do(TEXT("lock: the player in the lock"), []()
		{
			const AEpilogueChamber* C = Chamber();
			const FVector At = Pawn()->GetActorLocation();
			Check(C->LockPanel && C->EntryDoor && C->Battery && !C->EntryDoor->IsOpen() && C->LockPanel->GetOpenFraction() < 0.01f && At.Y > 2040.f && At.Y < 2440.f
				&& FMath::Abs(At.X) < 200.f && At.Z - HalfHeight() > 690.f,
				FString::Printf(TEXT("lock: the player in the closed lock (%s), its door shut behind, its panel shut (%.2f)"), *At.ToCompactString(), C->LockPanel ? C->LockPanel->GetOpenFraction() : -1.f));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("lock: the panel opening"), []() { return Chamber()->LockPanel->GetOpenFraction() > 0.35f; }, 10.f));
		AddShotWait(S, TEXT("01_Lock_PanelOpening"));
		S.Add(WaitFor(TEXT("lock: the panel open, the lines begun"), []() { return Chamber()->GetChamberState() != EEpilogueChamberState::Lock && Chamber()->GetChamberState() != EEpilogueChamberState::Waiting; }, 10.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			const float Delay = C->GetLockOpeningRunSeconds() - C->GetLockStartRunSeconds();
			const float Opening = C->GetLockOpenRunSeconds() - C->GetLockOpeningRunSeconds();
			Check(FMath::Abs(Delay - Settings()->GetLockOpenDelaySeconds()) <= 0.1f && FMath::Abs(Opening - Settings()->GetLockOpenSeconds()) <= 0.1f && C->LockPanel->IsFullyOpen() && !C->LockPanel->IsBlocking(),
				FString::Printf(TEXT("lock: the panel opened by itself %.2f s after the travel (LockOpenDelaySeconds %.2f) over %.2f s (LockOpenSeconds %.2f), and stays open"), Delay,
					Settings()->GetLockOpenDelaySeconds(), Opening, Settings()->GetLockOpenSeconds()));
			Check(FMath::Abs(C->GetLinesStartRunSeconds() - C->GetLockOpenRunSeconds()) <= 0.05f,
				FString::Printf(TEXT("lock: the chamber's lines begin when the panel has opened (%.2f s, open at %.2f s)"), C->GetLinesStartRunSeconds(), C->GetLockOpenRunSeconds()));
			// Three steps forward (80 cm each), no other input: the pawn on the gallery.
			const FVector Fwd = PC()->GetControlRotation().Vector().GetSafeNormal2D();
			StandOn(Pawn()->GetActorLocation() + Fwd * 240.f, 1000.f);
			return 0.3f;
		}));
		S.Add(Do(TEXT("first look: from the gallery"), []()
		{
			const FVector At = Pawn()->GetActorLocation();
			const bool bColumn = Seen(FVector(0.f, 0.f, 900.f), 400.f);
			const bool bParapet = Seen(FVector(0.f, 400.f, 121.f), 60.f);
			const bool bTerminal = Seen(Term()->GetActorLocation() + FVector(0.f, 22.f, 110.f), 30.f);
			const bool bPortal = Seen(FVector(0.f, 522.f, 300.f), 40.f);
			const bool bMonitors = Seen(FVector(1926.f, 0.f, 700.f), 60.f);
			Check(At.Y > 1600.f && At.Y < 1990.f && bColumn && bParapet && bTerminal && bPortal && bMonitors,
				FString::Printf(TEXT("first look: three steps forward, on the gallery (%s): the column %d, the parapet %d, the terminal %d under its portal %d, the monitoring side %d on screen"),
					*At.ToCompactString(), bColumn, bParapet, bTerminal, bPortal, bMonitors));
			return 0.2f;
		}));
		AddShotWait(S, TEXT("02_FromGallery"));
		S.Add(Do(TEXT("gpu: the view from the gallery, 240 frames"), []() { GRun.GpuSamples.Reset(); return 0.f; }));
		S.Add(WaitFor(TEXT("gpu: sampled"), []()
		{
			const uint32 Cycles = RHIGetGPUFrameCycles(0);
			if (Cycles > 0)
			{
				GRun.GpuSamples.Add(static_cast<float>(FPlatformTime::ToMilliseconds(Cycles)));
			}
			return GRun.GpuSamples.Num() >= 240;
		}, 30.f, []()
		{
			TArray<float> Sorted = GRun.GpuSamples;
			Sorted.Sort();
			Note(FString::Printf(TEXT("GPU time of the view from the gallery, Normal time, %d frames: median %.3f ms, 90th percentile %.3f ms (reported only)"), Sorted.Num(),
				Sorted.Num() ? Sorted[Sorted.Num() / 2] : -1.f, Sorted.Num() ? Sorted[Sorted.Num() * 9 / 10] : -1.f));
			return 0.f;
		}));

		// --- Criterion 3: the hall ---
		S.Add(Do(TEXT("hall: size, well, shaft, pieces"), []()
		{
			const FVector P0(1000.f, -1000.f, 1200.f);
			const float SizeX = Trace(P0, P0 + FVector(4000.f, 0.f, 0.f)) + Trace(P0, P0 - FVector(4000.f, 0.f, 0.f));
			const float SizeY = Trace(P0, P0 + FVector(0.f, 4000.f, 0.f)) + Trace(P0, P0 - FVector(0.f, 4000.f, 0.f));
			const FVector Low(1000.f, -1000.f, 50.f);
			const float Height = Trace(Low, Low + FVector(0.f, 0.f, 4000.f)) + 50.f;
			Check(SizeX >= 3970.f && SizeX <= 4010.f && SizeY >= 3970.f && SizeY <= 4010.f && Height >= 2090.f && Height <= 2110.f,
				FString::Printf(TEXT("hall: %.0f x %.0f cm between the panels' ribs, %.0f cm high (40 x 40 x 21 m)"), SizeX, SizeY, Height));
			// Inside the parapet's inner face (r 370), clear of the rings (330) and the tiers (300), between the spines.
			const float Down = Trace(FVector(355.f, 0.f, 10.f), FVector(355.f, 0.f, -3000.f)) - 10.f;
			const float Up = Trace(FVector(355.f, 0.f, 2095.f), FVector(355.f, 0.f, 5000.f)) - 5.f;
			int32 InWell = 0;
			int32 Lights = 0;
			for (TObjectIterator<ULightComponent> It; It; ++It)
			{
				if (It->GetWorld() == W() && It->IsRegistered() && !Cast<APawn>(It->GetOwner()))
				{
					++Lights;
					const FVector L = It->GetComponentLocation();
					InWell += FVector::Dist2D(L, FVector::ZeroVector) < 420.f && (L.Z < 0.f || L.Z > 2100.f) ? 1 : 0;
				}
			}
			Check(Down >= 1395.f && Down <= 1405.f && Up >= 695.f && Up <= 705.f && InWell == 0,
				FString::Printf(TEXT("hall: the well %.0f cm deep and the shaft %.0f cm high (14 m, 7 m), closed at their ends, no light in them (%d)"), Down, Up, InWell));
			Check(Lights >= 1 && Lights <= 10, FString::Printf(TEXT("hall: %d lights (at most 10)"), Lights));
			const int32 Tanks = MeshComps(TEXT("SM_CoolantTank")).FilterByPredicate([](const UStaticMeshComponent* C) { return C->GetComponentLocation().Y < -1500.f; }).Num();
			const int32 Racks = MeshComps(TEXT("SM_ComputeRack")).FilterByPredicate([](const UStaticMeshComponent* C) { return C->GetComponentLocation().X < -1800.f && C->GetComponentLocation().Y < 0.f; }).Num();
			const TArray<UStaticMeshComponent*> Cabinets = MeshComps(TEXT("SM_SwitchgearCabinet"));
			const int32 Entry = Cabinets.FilterByPredicate([](const UStaticMeshComponent* C) { return C->GetComponentLocation().Y > 1800.f; }).Num();
			const int32 Banks = MeshComps(TEXT("SM_ControlBank")).FilterByPredicate([](const UStaticMeshComponent* C) { return C->GetComponentLocation().X > 1800.f; }).Num();
			const int32 Walls = MeshComps(TEXT("SM_MonitorWall")).FilterByPredicate([](const UStaticMeshComponent* C) { return C->GetComponentLocation().X > 1800.f; }).Num();
			Check(Tanks == 4 && Racks == 18 && Entry >= 19 && Cabinets.Num() == Entry + 2 && Banks == 8 && Walls == 6,
				FString::Printf(TEXT("hall: 4 coolant tanks on the far wall (%d), 18 racks on the left wall's far half (%d), switchgear on the entry wall (%d) and under the second flight (%d), 8 control banks (%d) under 6 monitor walls (%d) on the right wall"),
					Tanks, Racks, Entry, Cabinets.Num() - Entry, Banks, Walls));
			const APrologueMonitorRoom* Room = Mon();
			Check(Room && Room->GetLightCount() == 18 * 6 * Settings()->GetRackLightsTuning().PerServer && Room->AreLightsShown(),
				FString::Printf(TEXT("hall: the racks' lights by RackLights (%d)"), Room ? Room->GetLightCount() : 0));
			const TArray<UStaticMeshComponent*> Covers = MeshComps(TEXT("SM_KitTrenchCover"));
			const TArray<UStaticMeshComponent*> Bollards = MeshComps(TEXT("SM_KitBollard"));
			float Nearest = 1.0e6f;
			bool bRing = true;
			for (const UStaticMeshComponent* B : Bollards)
			{
				const FVector P = B->GetComponentLocation();
				bRing &= FMath::Abs(FVector::Dist2D(P, FVector::ZeroVector) - 975.f) < 2.f;
				Nearest = FMath::Min(Nearest, static_cast<float>(FMath::PointDistToSegment(FVector(P.X, P.Y, 0.f), FVector(-1850.f, 300.f, 0.f), FVector(0.f, 650.f, 0.f))));
			}
			Check(Covers.Num() >= 30 && Bollards.Num() == 8 && bRing && Nearest >= 100.f,
				FString::Printf(TEXT("hall: %d trench covers; 8 bollards (%d) in a ring at 9.75 m, the nearest %.0f cm from the way from the walkway's foot to the terminal"), Covers.Num(), Bollards.Num(), Nearest));
			int32 Untagged = 0;
			int32 EngineMeshes = 0;
			FString Odd;
			for (TActorIterator<AActor> It(W()); It; ++It)
			{
				const bool bPiece = Cast<AStaticMeshActor>(*It) || Cast<ALight>(*It);
				if (bPiece && !It->Tags.Contains(TEXT("Chamber")))
				{
					++Untagged;
					Odd += It->GetName() + TEXT(" ");
				}
				// The map's pieces (the runtime screen plates and rack lights of the game's actors are the engine's plane and cube by design).
				const AStaticMeshActor* Piece = Cast<AStaticMeshActor>(*It);
				if (Piece && Piece->GetStaticMeshComponent()->GetStaticMesh() && Piece->GetStaticMeshComponent()->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")))
				{
					++EngineMeshes;
					Odd += It->GetName() + TEXT(" ");
				}
				const APrologueSlidingPanel* Lock = Cast<APrologueSlidingPanel>(*It);
				const UStaticMeshComponent* LockPanelMesh = Lock ? Lock->GetPanel() : nullptr;
				for (const UStaticMeshComponent* C : { LockPanelMesh })
				{
					if (C && C->GetStaticMesh() && C->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")) && C->IsVisible())
					{
						++EngineMeshes;
						Odd += FString::Printf(TEXT("%s.%s "), *It->GetName(), *C->GetName());
					}
				}
			}
			Check(Untagged == 0 && EngineMeshes == 0, FString::Printf(TEXT("hall: every piece tagged Chamber (%d not), no engine mesh shown (%d) %s"), Untagged, EngineMeshes, *Odd));
			Check(Settings()->IsHomeLookMap(MapName()), TEXT("hall: the map shows home's look"));

			// The terminal on its platform: its base on the platform's top, measured on the platform's own triangles.
			const AChoiceTerminal* T = Term();
			const TArray<UStaticMeshComponent*> Portals = MeshComps(TEXT("SM_TerminalPortal"));
			const UStaticMeshComponent* Portal = Portals.Num() ? Portals[0] : nullptr;
			TArray<FVector> V;
			TArray<uint32> I;
			float Top = -1.0e6f;
			const FVector Base = T->GetActorTransform().TransformPosition(FVector(0.f, 0.f, LocalBody().Min.Z));
			if (Portal && FEntityFigureShape::ReadMeshTriangles(Portal->GetStaticMesh(), V, I))
			{
				const FTransform PT = Portal->GetComponentTransform();
				for (int32 K = 0; K + 2 < I.Num(); K += 3)
				{
					const FVector A = PT.TransformPosition(V[I[K]]), B = PT.TransformPosition(V[I[K + 1]]), C = PT.TransformPosition(V[I[K + 2]]);
					const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
					// The platform's top: a face turned up, at or below the terminal's base (not the lintel above it).
					if (FMath::Abs(N.Z) < 0.99 || (A.Z + B.Z + C.Z) / 3.0 > Base.Z + 5.0)
					{
						continue;
					}
					// Is the base's centre over this triangle (in the plane)?
					const auto Side = [](const FVector& P, const FVector& Q, const FVector& R) { return (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X); };
					const double S0 = Side(A, B, Base), S1 = Side(B, C, Base), S2 = Side(C, A, Base);
					if ((S0 >= 0 && S1 >= 0 && S2 >= 0) || (S0 <= 0 && S1 <= 0 && S2 <= 0))
					{
						Top = FMath::Max(Top, static_cast<float>((A.Z + B.Z + C.Z) / 3.0));
					}
				}
			}
			// No part of the body inside the platform, the portal or the parapet: no triangle of theirs crosses the body's box.
			const FBox Body = LocalBody();
			const FVector Half = Body.GetExtent() - FVector(0.5f);
			const FVector Centre = Body.GetCenter();
			int32 Crossing = 0;
			TArray<const UStaticMeshComponent*> Near(Portals);
			for (const UStaticMeshComponent* P : MeshComps(TEXT("SM_ContainmentParapet")))
			{
				Near.Add(P);
			}
			const FTransform ToTerminal = T->GetActorTransform().Inverse();
			for (const UStaticMeshComponent* M : Near)
			{
				TArray<FVector> MV;
				TArray<uint32> MI;
				if (!FEntityFigureShape::ReadMeshTriangles(M->GetStaticMesh(), MV, MI))
				{
					continue;
				}
				const FTransform MT = M->GetComponentTransform();
				for (int32 K = 0; K + 2 < MI.Num(); K += 3)
				{
					const FVector A = ToTerminal.TransformPosition(MT.TransformPosition(MV[MI[K]]));
					const FVector B = ToTerminal.TransformPosition(MT.TransformPosition(MV[MI[K + 1]]));
					const FVector C = ToTerminal.TransformPosition(MT.TransformPosition(MV[MI[K + 2]]));
					Crossing += TriBoxOverlap(Centre, Half, A, B, C) ? 1 : 0;
				}
			}
			Check(Top > -1.0e5f && FMath::Abs(Base.Z - Top) <= 1.f && Crossing == 0,
				FString::Printf(TEXT("terminal: its base %.2f cm from the platform's top face (at %.2f cm, its rendered triangles), no triangle of the platform, the portal or the parapet in its body (%d)"),
					Base.Z - Top, Top, Crossing));
			const FVector Front = T->GetActorForwardVector().GetSafeNormal2D();
			Check(FVector::DotProduct(Front, FVector(0.f, 1.f, 0.f)) > 0.999f && FMath::Abs(T->GetActorLocation().X) < 1.f,
				TEXT("terminal: under the portal, its front toward the entry wall"));
			return 0.f;
		}));
		S.Add(Do(TEXT("reach: set for the session"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceTerminalReachCm = 300.f;
			return 0.2f;
		}));
		S.Add(Do(TEXT("reach: followed"), []()
		{
			Check(FMath::IsNearlyEqual(Term()->GetInteractRadius(), 300.f, 0.5f), FString::Printf(TEXT("reach: ChoiceTerminalReachCm set to 300 for the session, followed (%.0f cm)"), Term()->GetInteractRadius()));
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceTerminalReachCm = GRun.SavedReach;
			return 0.2f;
		}));
		S.Add(Do(TEXT("reach: back"), []()
		{
			Check(FMath::IsNearlyEqual(Term()->GetInteractRadius(), GRun.SavedReach, 0.5f), FString::Printf(TEXT("reach: back at %.0f cm (the ini's %.0f)"), Term()->GetInteractRadius(), GRun.SavedReach));
			return 0.f;
		}));

		// --- Criterion 4: the column ---
		S.Add(Do(TEXT("column: tiers, rings, spines, ducts, faces"), []()
		{
			TArray<UStaticMeshComponent*> Tiers = MeshComps(TEXT("SM_BatteryTier"));
			Tiers.Append(MeshComps(TEXT("SM_BatteryTierB")));
			Tiers.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetComponentLocation().Z < B.GetComponentLocation().Z; });
			bool bTurn = Tiers.Num() == 12;
			for (int32 K = 0; bTurn && K < Tiers.Num(); ++K)
			{
				const bool bA = Tiers[K]->GetStaticMesh()->GetName() == TEXT("SM_BatteryTier");
				const float Yaw = FRotator::NormalizeAxis(Tiers[K]->GetComponentRotation().Yaw);
				bTurn &= bA == (K % 2 == 0) && FMath::IsNearlyEqual(Tiers[K]->GetComponentLocation().Z, -1400.f + 350.f * K, 0.5f) && FVector::Dist2D(Tiers[K]->GetComponentLocation(), FVector::ZeroVector) < 0.5f
					&& FMath::IsNearlyEqual(Yaw, bA ? 0.f : -15.f, 0.1f);
			}
			const TArray<UStaticMeshComponent*> Rings = MeshComps(TEXT("SM_BatteryRing"));
			bool bRings = Rings.Num() == 11;
			for (int32 K = 0; bRings && K < Rings.Num(); ++K)
			{
				bRings &= FMath::IsNearlyEqual(Rings[K]->GetComponentLocation().Z, -1400.f + 350.f * (K + 1) - 25.f, 0.5f) && Rings[K]->Mobility == EComponentMobility::Movable;
			}
			TMap<int32, int32> SpineAngles;
			const TArray<UStaticMeshComponent*> Spines = MeshComps(TEXT("SM_BatterySpine"));
			for (const UStaticMeshComponent* C : Spines)
			{
				SpineAngles.FindOrAdd(FMath::RoundToInt(FRotator::ClampAxis(C->GetComponentRotation().Yaw))) += 1;
			}
			const bool bSpines = Spines.Num() == 48 && SpineAngles.FindRef(45) == 12 && SpineAngles.FindRef(135) == 12 && SpineAngles.FindRef(225) == 12 && SpineAngles.FindRef(315) == 12;
			const TArray<UStaticMeshComponent*> Sockets = MeshComps(TEXT("SM_BatteryDuctSocket"));
			const TArray<UStaticMeshComponent*> Ducts = MeshComps(TEXT("SM_BatteryDuct"));
			bool bDucts = Sockets.Num() == 2 && Ducts.Num() == 10;
			int32 East = 0;
			for (const UStaticMeshComponent* D : Ducts)
			{
				bDucts &= FMath::IsNearlyEqual(D->GetComponentLocation().Z, 1575.f, 0.5f);
				East += D->GetComponentLocation().X > 0.f ? 1 : 0;
			}
			bool bThroughL = false, bThroughR = false;
			for (const UStaticMeshComponent* D : Ducts)
			{
				const FBox B = D->Bounds.GetBox();
				bThroughR |= B.Min.X < 2000.f && B.Max.X > 2010.f;
				bThroughL |= B.Min.X < -2010.f && B.Max.X > -2000.f;
			}
			Check(bTurn, FString::Printf(TEXT("column: 12 tiers (%d), A and B in turn from the well's bottom (-14 m) to the shaft's top (+28 m), B turned 15 degrees"), Tiers.Num()));
			Check(bRings, FString::Printf(TEXT("column: a ring in the groove between every two tiers (%d), movable"), Rings.Num()));
			Check(bSpines, FString::Printf(TEXT("column: four lines of spines at 45, 135, 225 and 315 degrees up its whole height (%d)"), Spines.Num()));
			Check(bDucts && East == 5 && bThroughL && bThroughR, FString::Printf(TEXT("column: two ducts of five (%d, %d east) at 15.75 m through their sockets (%d), passing through the left (%d) and the right wall (%d)"),
				Ducts.Num(), East, Sockets.Num(), bThroughL ? 1 : 0, bThroughR ? 1 : 0));

			// No two faces of different pieces of the column within 1 cm in one plane facing the same way and overlapping (the pairs that
			// face each other or stand back to back are counted apart: nobody sees them).
			const FColumnFaces Faces = ColumnFaces(1.f, false);
			for (const TPair<FString, int32>& P : Faces.SameByPair)
			{
				Note(FString::Printf(TEXT("column: %d pairs facing the same way between %s"), P.Value, *P.Key));
			}
			Note(FString::Printf(TEXT("column: %d pairs facing each other or back to back (unseen, not counted)"), Faces.Facing));
			Check(Faces.Same == 0, FString::Printf(TEXT("column: no two faces of different pieces within 1 cm in one plane facing the same way and overlapping (%d triangles of %d pieces, %d pairs)"),
				Faces.Tris, Faces.Pieces, Faces.Same));
			return 0.f;
		}));

		// --- Criterion 2: the walk, both ways, in both time states; then the pushes ---
		S.Add(Do(TEXT("walk: from the gallery, Normal time"), []() { StandOn(FVector(0.f, 2150.f, 0.f), 1000.f); PC()->SetControlRotation(FRotator(0.f, -90.f, 0.f)); return 0.3f; }));
		AddWalk(S, TEXT("walk down, Normal time"), true);
		AddWalk(S, TEXT("walk up, Normal time"), false);
		S.Add(Do(TEXT("walk: Stop"), []() { Time()->Toggle(); return 0.3f; }));
		AddWalk(S, TEXT("walk down, Stopped"), true);
		AddWalk(S, TEXT("walk up, Stopped"), false);
		S.Add(Do(TEXT("walk: Resume"), []()
		{
			Check(Time()->IsStopped(), TEXT("walk: time stayed Stopped for the walk"));
			Time()->Toggle();
			return 0.5f;
		}));
		S.Add(Do(TEXT("push: start"), []() { GRun.PushIndex = 0; GRun.PushPhase = 0; GRun.PushesOk = 0; GRun.PushMoved = 0.f; return 0.f; }));
		S.Add(WaitFor(TEXT("push: against every railing and the parapet"), []() { return RunPushes(); }, 120.f, []()
		{
			const int32 N = Pushes().Num();
			Check(GRun.PushesOk == N && GRun.PushMoved > 5.f, FString::Printf(TEXT("push: against every railing of the gallery, the landing and the flights and all round the parapet: %d of %d pushes left the pawn on its deck and out of the well"),
				GRun.PushesOk, N));
			return 0.f;
		}));
		// Shots on the way: halfway down each flight, the walkway's foot.
		S.Add(Do(TEXT("shots: halfway down the first flight"), []() { StandOn(FVector(-1000.f, 1845.f, 0.f), 1000.f); PC()->SetControlRotation(FRotator(-10.f, 180.f, 0.f)); return 0.4f; }));
		AddShotWait(S, TEXT("03_Flight1_Halfway"));
		S.Add(Do(TEXT("shots: halfway down the second flight"), []() { StandOn(FVector(-1845.f, 1000.f, 0.f), 1000.f); PC()->SetControlRotation(FRotator(-10.f, -60.f, 0.f)); return 0.4f; }));
		AddShotWait(S, TEXT("04_Flight2_Halfway"));
		S.Add(Do(TEXT("shots: the walkway's foot"), []() { StandOn(FVector(-1850.f, 200.f, 0.f), 300.f); LookAt(Term()->GetActorLocation() + FVector(0.f, 0.f, 120.f)); return 0.4f; }));
		AddShotWait(S, TEXT("05_WalkwayFoot"));

		// --- Criteria 5 to 8: the battery's surface and light, the rings, the monitors; the hum ---
		S.Add(Do(TEXT("surface: the battery's family"), []()
		{
			// The printed circuit is gone: the battery's family is M_Surface's, with its own values (darker than the objects').
			const UMaterialInterface* MI = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TheLastShift/Art/Materials/MI_Surface_Battery.MI_Surface_Battery"));
			const UMaterial* Base = MI ? MI->GetMaterial() : nullptr;
			const bool bCircuitGone = !FPackageName::DoesPackageExist(TEXT("/Game/TheLastShift/Art/Materials/M_SurfaceBattery"));
			float WorldSpace = -1.f;
			if (MI)
			{
				MI->GetScalarParameterValue(TEXT("ObjectSpace"), WorldSpace);
			}
			Check(Base && Base->GetName() == TEXT("M_Surface") && bCircuitGone && Base->GetBlendMode() == BLEND_Opaque && !Base->HasEmissiveColorConnected() && !Base->HasVertexPositionOffsetConnected()
				&& !Base->HasPixelDepthOffsetConnected() && !Base->HasDisplacementConnected() && WorldSpace == 0.f,
				FString::Printf(TEXT("surface: MI_Surface_Battery on %s (M_SurfaceBattery and its circuit gone: %d), mapped in world space (ObjectSpace %.0f): opaque, not emissive, no vertex and no depth moved"),
					*GetNameSafe(Base), bCircuitGone ? 1 : 0, WorldSpace));
			int32 OnBattery = 0, RingsOwn = 0, Wrong = 0;
			const UMaterialInstanceDynamic* RingMid = Bat() ? Bat()->GetRingMaterial() : nullptr;
			float RingSpace = -1.f;
			if (RingMid)
			{
				RingMid->GetScalarParameterValue(TEXT("ObjectSpace"), RingSpace);
			}
			for (const TCHAR* N : { TEXT("SM_BatteryTier"), TEXT("SM_BatteryTierB"), TEXT("SM_BatteryRing"), TEXT("SM_BatterySpine"), TEXT("SM_BatteryDuct"), TEXT("SM_BatteryDuctSocket") })
			{
				const bool bRing = FCString::Strcmp(N, TEXT("SM_BatteryRing")) == 0;
				for (const UStaticMeshComponent* C : MeshComps(N))
				{
					if (bRing)
					{
						(RingMid && C->GetMaterial(0) == RingMid && RingMid->Parent == MI && RingSpace == 1.f ? RingsOwn : Wrong) += 1;
					}
					else
					{
						(C->GetMaterial(0) == MI ? OnBattery : Wrong) += 1;
					}
				}
			}
			Check(OnBattery == 12 + 48 + 10 + 2 && RingsOwn == 11 && Wrong == 0,
				FString::Printf(TEXT("surface: the tiers, spines, ducts and sockets in MI_Surface_Battery (%d); the 11 rings in it mapped in their own space (ObjectSpace %.0f: %d); %d not"),
					OnBattery, RingSpace, RingsOwn, Wrong));
			StandOn(FVector(0.f, 620.f, 0.f), 300.f);
			LookAt(FVector(0.f, 300.f, 520.f));
			return 0.5f;
		}));
		AddShotWait(S, TEXT("07_Tier_From3m"));
		S.Add(Do(TEXT("surface: from 12 m"), []() { StandOn(FVector(0.f, 1500.f, 0.f), 300.f); LookAt(FVector(0.f, 300.f, 700.f)); return 0.5f; }));
		AddShotWait(S, TEXT("08_Tier_From12m"));
		// A ring turning: its surface turns with it. The ring nearest the eye's height, from 3 m, two shots 20 degrees apart; and
		// its base colour alone (no light) from the first pose, against the same from that pose turned with the ring: mapped in
		// its own space they match; mapped in the world they do not (the control: the same captures at ObjectSpace 0).
		S.Add(Do(TEXT("ring: the pose, 3 m from a ring"), []()
		{
			const AChamberBattery* B = Bat();
			GRun.RingIndex = -1;
			float Best = TNumericLimits<float>::Max();
			for (int32 I = 0; I < B->GetRings().Num(); ++I)
			{
				const float D = FMath::Abs(static_cast<float>(B->GetRings()[I]->Bounds.Origin.Z) - 520.f);
				if (D < Best)
				{
					Best = D;
					GRun.RingIndex = I;
				}
			}
			const UStaticMeshComponent* Ring = B->GetRings().IsValidIndex(GRun.RingIndex) ? B->GetRings()[GRun.RingIndex].Get() : nullptr;
			const float Radius = Ring ? static_cast<float>(Ring->Bounds.BoxExtent.X) : 250.f;
			const float Z = Ring ? static_cast<float>(Ring->Bounds.Origin.Z) : 520.f;
			StandOn(FVector(0.f, Radius + 300.f, 0.f), 300.f);
			LookAt(FVector(0.f, Radius, Z));
			Note(FString::Printf(TEXT("ring: ring %d of %d (centre at %.0f cm, radius %.0f cm), seen from %.0f cm off its outer face"), GRun.RingIndex, B->GetRings().Num(), Z, Radius, 300.f));
			return 0.5f;
		}));
		AddShotWait(S, TEXT("09_Ring_A"), []()
		{
			const AChamberBattery* B = Bat();
			UStaticMeshComponent* Ring = B->GetRings().IsValidIndex(GRun.RingIndex) ? B->GetRings()[GRun.RingIndex].Get() : nullptr;
			const APlayerCameraManager* Cam = PC()->PlayerCameraManager;
			GRun.RingEye = Cam->GetCameraLocation();
			GRun.RingRot = Cam->GetCameraRotation();
			GRun.RingFov = Cam->GetFOVAngle();
			GRun.RingTurnStart = B->GetRingTurn(GRun.RingIndex);
			const bool bOk = Ring && CaptureRingBoth(Ring, GRun.RingEye, GRun.RingRot, GRun.RingFov, GRun.RingA, GRun.RingA0) && CaptureRing(Ring, GRun.RingEye, GRun.RingRot, GRun.RingFov, true, GRun.RingDepthA);
			Check(bOk, TEXT("ring: the base colour captures from the first pose"));
			return 0.f;
		});
		S.Add(WaitFor(TEXT("ring: turned 20 degrees"), []()
		{
			const AChamberBattery* B = Bat();
			return FMath::Abs(B->GetRingTurn(GRun.RingIndex) - GRun.RingTurnStart) >= 20.f;
		}, 30.f));
		AddShotWait(S, TEXT("09_Ring_B_20deg"), []()
		{
			const AChamberBattery* B = Bat();
			UStaticMeshComponent* Ring = B->GetRings()[GRun.RingIndex].Get();
			const float Turned = B->GetRingTurn(GRun.RingIndex) - GRun.RingTurnStart;
			FVector Eye = GRun.RingEye;
			FRotator Rotation = GRun.RingRot;
			OrbitView(Ring->GetComponentLocation(), Turned, Eye, Rotation);
			TArray<FLinearColor> Own, World, Depth, Still, StillDepth;
			const bool bOk = CaptureRingBoth(Ring, Eye, Rotation, GRun.RingFov, Own, World) && CaptureRing(Ring, Eye, Rotation, GRun.RingFov, true, Depth)
				&& CaptureRing(Ring, GRun.RingEye, GRun.RingRot, GRun.RingFov, false, Still) && CaptureRing(Ring, GRun.RingEye, GRun.RingRot, GRun.RingFov, true, StillDepth);
			double OwnMean = 0.0, OwnShare = 0.0, WorldMean = 0.0, WorldShare = 0.0, PoseMean = 0.0, PoseShare = 0.0;
			int32 OwnPixels = 0, WorldPixels = 0, PosePixels = 0;
			CompareRing(GRun.RingA, Own, GRun.RingDepthA, Depth, OwnMean, OwnShare, OwnPixels);
			CompareRing(GRun.RingA0, World, GRun.RingDepthA, Depth, WorldMean, WorldShare, WorldPixels);
			CompareRing(GRun.RingA, Still, GRun.RingDepthA, StillDepth, PoseMean, PoseShare, PosePixels);
			Check(bOk && OwnPixels > 5000 && OwnMean <= 1.0 && OwnShare <= 0.02 && WorldMean >= 0.5 && WorldMean > 4.0 * OwnMean,
				FString::Printf(TEXT("ring: turned %.1f deg, its base colour seen from the first pose turned with it matches the first capture (%d pixels, mean %.2f levels as shown, %.2f%% above 4 levels): every detail turned with the ring; mapped in the world instead (control, ObjectSpace 0) it would not (mean %.2f levels, at least 0.5 and 4 times the first, %.2f%% above 4); from the one pose, 20 degrees apart: mean %.2f levels, %.2f%% above 4"),
					Turned, OwnPixels, OwnMean, 100.0 * OwnShare, WorldMean, 100.0 * WorldShare, PoseMean, 100.0 * PoseShare));
			return 0.f;
		});
		// The light: shown, its levels, its pulses.
		S.Add(Do(TEXT("light: the glow in Normal time"), []()
		{
			const AChamberBattery* B = Bat();
			int32 Flags = 0;
			for (const UStaticMeshComponent* G : B->GetGlows())
			{
				Flags += G->bHiddenInSceneCapture && !G->CastShadow && G->GetCollisionEnabled() == ECollisionEnabled::NoCollision && G->IsVisible() ? 1 : 0;
			}
			Check(B && B->GetGlows().Num() == 12 + 48 && Flags == B->GetGlows().Num() && B->AreGlowsShown(),
				FString::Printf(TEXT("light: %d glow strips shown in Normal time, every one never in a scan's capture, casting nothing, colliding with nothing (%d)"), B ? B->GetGlows().Num() : 0, Flags));
			GRun.PhaseBefore = B->GetPulsePhase();
			GRun.PhaseReal = RunNow();
			const UMaterialInstanceDynamic* Spine = B->GetGlowMaterial(true);
			float UseZ = 0.f;
			if (Spine)
			{
				Spine->GetScalarParameterValue(FName(TEXT("UseWorldZ")), UseZ);
			}
			Check(UseZ > 0.5f, TEXT("light: the spines' pulses run in the world's height (up the stacked spines as one strip)"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("light: the pulses climb"), []()
		{
			const AChamberBattery* B = Bat();
			const double Spacing = Settings()->GetBatteryLook().PulseSpacingM;
			double D = FMath::Fmod(B->GetPulsePhase() - GRun.PhaseBefore + 10.0 * Spacing, Spacing);
			const double Want = FMath::Fmod(B->GetPulseSpeed() * (RunNow() - GRun.PhaseReal), Spacing);
			Check(B->GetPulseSpeed() > 0.f && FMath::Abs(D - Want) < 0.1 * Spacing + 0.05,
				FString::Printf(TEXT("light: the pulses travel up the strips at %.2f m/s (%.3f m in %.2f s)"), B->GetPulseSpeed(), D, RunNow() - GRun.PhaseReal));
			return 0.f;
		}));
		for (const float Level : { 1.f, 0.5f, 0.05f })
		{
			S.Add(Do(FString::Printf(TEXT("level %.2f: set"), Level), [Level]() { AChamberBattery::SetSessionBattery(Level); return 0.3f; }));
			S.Add(Do(FString::Printf(TEXT("level %.2f: followed"), Level), [Level]() { CheckLevel(Level, FString::Printf(TEXT("level %.2f"), Level)); return 0.f; }));
			if (Level != 0.5f)
			{
				S.Add(Do(FString::Printf(TEXT("level %.2f: the monitoring side from 8 m"), Level), []() { StandOn(FVector(1126.f, 0.f, 0.f), 300.f); LookAt(FVector(1926.f, 0.f, 520.f)); return 0.5f; }));
				AddShotWait(S, Level > 0.9f ? TEXT("09_Monitoring_8m_Full") : TEXT("10_Monitoring_8m_Low"));
			}
		}
		// At 0.05 the glow is still seen: the column with and without its glow.
		S.Add(Do(TEXT("level 0.05: the column"), []() { StandOn(FVector(0.f, 1100.f, 0.f), 300.f); LookAt(FVector(0.f, 0.f, 500.f)); return 0.4f; }));
		AddShotWait(S, TEXT("Low_Column_Glow"), []() { GRun.SavedPixels = GRun.ShotPixels; Bat()->SetHiddenForTest(true); return 0.3f; });
		AddShotWait(S, TEXT("Low_Column_NoGlow"), []()
		{
			int32 Lit = 0;
			for (int32 K = 0; K < GRun.ShotPixels.Num() && K < GRun.SavedPixels.Num(); ++K)
			{
				const FColor& A = GRun.SavedPixels[K];
				const FColor& B = GRun.ShotPixels[K];
				Lit += FMath::Max3(A.R, A.G, A.B) - FMath::Max3(B.R, B.G, B.B) > 12 ? 1 : 0;
			}
			Check(Lit > 200, FString::Printf(TEXT("level 0.05: the glow still seen (%d pixels lit by it)"), Lit));
			Bat()->SetHiddenForTest(false);
			AChamberBattery::SetSessionBattery(-1.f);
			return 0.3f;
		});
		// It lights nothing: the parapet's top 1 m from the strips, the same with the glow hidden.
		S.Add(Do(TEXT("light: lights nothing"), []() { StandOn(FVector(900.f, 0.f, 0.f), 300.f); LookAt(FVector(396.f, 0.f, 121.f)); return 0.4f; }));
		AddShotWait(S, TEXT("Light_Parapet_Glow"), []() { GRun.SavedMean = ShotMean(FVector(396.f, 0.f, 121.f), 3); Bat()->SetHiddenForTest(true); return 0.3f; });
		AddShotWait(S, TEXT("Light_Parapet_NoGlow"), []()
		{
			const float Without = ShotMean(FVector(396.f, 0.f, 121.f), 3);
			Check(GRun.SavedMean >= 0.f && Without >= 0.f && FMath::Abs(GRun.SavedMean - Without) <= 2.f,
				FString::Printf(TEXT("light: it lights nothing: the parapet's top beside the strips at %.1f with the glow and %.1f without"), GRun.SavedMean, Without));
			Bat()->SetHiddenForTest(false);
			return 0.f;
		});
		// The rings turn, each the other way from the one below it.
		S.Add(Do(TEXT("rings: measured"), []() { GRun.RingsBefore = RingTurns(); GRun.PhaseReal = RunNow(); return 2.0f; }));
		S.Add(Do(TEXT("rings: turned"), []()
		{
			const TArray<float> Now = RingTurns();
			const float Want = Settings()->GetBatteryLook().RingDegreesPerSecond * static_cast<float>(RunNow() - GRun.PhaseReal);
			bool bOk = Now.Num() == 11 && GRun.RingsBefore.Num() == 11;
			for (int32 I = 0; bOk && I < Now.Num(); ++I)
			{
				const float D = Now[I] - GRun.RingsBefore[I];
				bOk &= FMath::Abs(FMath::Abs(D) - Want) <= 0.05f * Want + 0.05f && (I == 0 || FMath::Sign(D) == -FMath::Sign(Now[I - 1] - GRun.RingsBefore[I - 1]));
			}
			Check(bOk, FString::Printf(TEXT("rings: each turned %.2f degrees in %.2f s (RingDegreesPerSecond %.2f), each the other way from the one below it"), Now.Num() ? FMath::Abs(Now[0] - GRun.RingsBefore[0]) : -1.f,
				RunNow() - GRun.PhaseReal, Settings()->GetBatteryLook().RingDegreesPerSecond));
			// Hum: lower in the lock than at the terminal.
			const float AtLock = HumLevelAt(FVector(0.f, 2150.f, 860.f));
			const float AtTerminal = HumLevelAt(FVector(0.f, 557.f, 170.f));
			const TArray<FGameSoundRequest>& Rec = Aud()->GetRecord();
			int32 Asked = 0;
			FVector From = FVector::ZeroVector;
			for (int32 K = GRun.HumRecordFrom - Aud()->GetRecordOffset(); K < Rec.Num(); ++K)
			{
				if (K >= 0 && Rec[K].Sound == EGameSound::ChamberHum)
				{
					++Asked;
					From = Rec[K].Location;
				}
			}
			const USoundWave* Wave = Cast<USoundWave>(Settings()->Audio.GetSound(EGameSound::ChamberHum).LoadSynchronous());
			Check(Asked == 1 && FVector::Dist(From, Bat()->GetActorLocation()) < 1.f && Aud()->IsHumPlaying() && Wave && Wave->bLooping,
				FString::Printf(TEXT("hum: asked for once when the hall started (%d), from the battery's place, looping (%d), playing"), Asked, Wave && Wave->bLooping ? 1 : 0));
			Check(AtLock >= 0.f && AtLock < AtTerminal && AtTerminal >= 0.999f, FString::Printf(TEXT("hum: lower in the lock (%.2f of its level) than at the terminal (%.2f)"), AtLock, AtTerminal));
			Check(Wave && FMath::IsNearlyEqual(Wave->Duration, 12.f, 0.01f) && Wave->GetSampleRateForCurrentPlatform() == 48000.f && Wave->NumChannels == 1,
				FString::Printf(TEXT("hum: its sound %s, %.2f s, %.0f Hz, %d channel"), *GetNameSafe(Wave), Wave ? Wave->Duration : -1.f, Wave ? Wave->GetSampleRateForCurrentPlatform() : -1.f, Wave ? Wave->NumChannels : 0));
			return 0.f;
		}));
		// While Stopped: the glow and the lit screens hidden, black before the first scan, the rings and the hum still.
		S.Add(Do(TEXT("stopped: Stop"), []()
		{
			StandOn(FVector(900.f, 300.f, 0.f), 300.f);
			LookAt(FVector(1500.f, 0.f, 500.f));
			GRun.RingsBefore = RingTurns();
			Time()->Toggle();
			// The suit's scan-ready ring is the suit's, not the hall's: out of this one shot.
			if (UScanCueWidget* Ring = FindWidget<UScanCueWidget>())
			{
				Ring->SetRenderOpacity(0.f);
			}
			return 0.2f;
		}));
		AddShotWait(S, TEXT("Stopped_BeforeScan"), []()
		{
			if (UScanCueWidget* Ring = FindWidget<UScanCueWidget>())
			{
				Ring->SetRenderOpacity(1.f);
			}
			const UScanDirectorComponent* D = Pawn()->FindComponentByClass<UScanDirectorComponent>();
			Check(ShotMax() <= 2 && D && D->GetScansThisStop() == 0, FString::Printf(TEXT("stopped: before the first scan every pixel at most %d levels (2 allowed; scans %d)"), ShotMax(), D ? D->GetScansThisStop() : -1));
			const APrologueMonitorRoom* M = Mon();
			int32 Flags = 0;
			for (const UStaticMeshComponent* P : M->GetLitPlates())
			{
				Flags += P->bHiddenInSceneCapture && !P->CastShadow ? 1 : 0;
			}
			Check(!Bat()->AreGlowsShown() && M->GetLitShownCount() == 0 && M->GetPlateCount() == M->GetLitPlates().Num() && Flags == M->GetLitPlates().Num(),
				FString::Printf(TEXT("stopped: the glow hidden, every lit screen hidden over its black plate (%d plates, %d lit), none ever in a scan's capture (%d)"), M->GetPlateCount(), M->GetLitPlates().Num(), Flags));
			return 0.25f;
		});
		S.Add(Do(TEXT("stopped: the hum and the rings"), []()
		{
			Check(Aud()->GetHumFade() == 0.f && Aud()->GetHumVolume() == 0.f, FString::Printf(TEXT("stopped: the hum silent within ChamberHumFadeSeconds (%.2f s): volume %.3f"), Settings()->Audio.GetChamberHumFadeSeconds(), Aud()->GetHumVolume()));
			return 1.5f;
		}));
		AddShotWait(S, TEXT("11_Scan_FromFloor"), []()
		{
			const TArray<float> Now = RingTurns();
			Check(Now == GRun.RingsBefore, TEXT("stopped: the rings stand still while Stopped"));
			Time()->Toggle();
			return 0.4f;
		});
		S.Add(Do(TEXT("normal again: the hum back"), []()
		{
			Check(Aud()->GetHumFade() == 1.f && Aud()->GetHumVolume() > 0.f && Bat()->AreGlowsShown() && Mon()->GetLitShownCount() == Mon()->GetLitPlates().Num(),
				FString::Printf(TEXT("normal again: the hum back (volume %.3f), the glow and the screens shown"), Aud()->GetHumVolume()));
			GRun.RingsBefore = RingTurns();
			GRun.HumBefore = Aud()->GetHumVolume();
			Check(StdPC() && StdPC()->PauseGame() && StdPC()->IsPausedByMenu(), TEXT("pause: the pause menu opens"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("pause: still behind the menu"), []()
		{
			const UAudioComponent* Hum = Aud()->GetHumComponent();
			Check(RingTurns() == GRun.RingsBefore && W()->IsPaused() && Hum && !Hum->bIsUISound && FMath::IsNearlyEqual(Aud()->GetHumVolume(), GRun.HumBefore, 1.e-4f),
				TEXT("pause: the rings still behind the pause menu, the hum paused with the game (no UI sound, its values kept)"));
			StdPC()->ResumeGame();
			return 1.0f;
		}));
		S.Add(Do(TEXT("pause: closed, the rings go on"), []()
		{
			Check(RingTurns() != GRun.RingsBefore, TEXT("pause: the rings go on after the menu closes"));
			StandOn(FVector(0.f, 1800.f, 0.f), 1000.f);
			LookAt(FVector(0.f, 0.f, 400.f));
			Time()->Toggle();
			return 2.2f;
		}));
		AddShotWait(S, TEXT("12_Scan_FromGallery"), []() { Time()->Toggle(); return 0.4f; });
		S.Add(Do(TEXT("monitors: the rows"), []()
		{
			const APrologueMonitorRoom* M = Mon();
			TSet<int32> Rows;
			int32 Banks = 0;
			TSet<int32> Layouts;
			for (int32 K = 0; K < M->GetLitPlates().Num(); ++K)
			{
				if (M->IsLitOnBank(K))
				{
					++Banks;
				}
				else
				{
					Rows.Add(FMath::RoundToInt(M->GetLitPlates()[K]->GetComponentLocation().Z / 10.f));
				}
				Layouts.Add(M->GetLitLayout(K));
			}
			Check(Banks == 16 && Rows.Num() == 6 && Layouts.Num() == 4,
				FString::Printf(TEXT("monitors: the banks' two screens (%d), the monitors in %d rows (home's monitor wall twice: six rows, not the blockout's five), %d layouts"), Banks, Rows.Num(), Layouts.Num()));
			return 0.f;
		}));
	}

	/**
	 * After a choice held through, the hall answers for ChoiceAftermathSeconds, each part reaching its end state at the end and
	 * not before (checked in the aftermath's last second); then it holds still as the aftermath left it for AftermathHoldSeconds,
	 * nothing answering the player; then the ending opens.
	 */
	static void AddAftermath(TArray<FStep>& S, const FString& L, FName Ending)
	{
		// The aftermath's last second: nothing at its end state yet.
		S.Add(WaitFor(L + TEXT(": the aftermath's last second"), []()
		{
			const float Seconds = Settings()->GetChoiceAftermathSeconds();
			return Chamber()->GetAftermathAlpha() * Seconds >= Seconds - 0.9f || Chamber()->GetChamberState() != EEpilogueChamberState::Aftermath;
		}, 40.f, [L, Ending]()
		{
			const AEpilogueChamber* C = Chamber();
			const AChamberBattery* B = Bat();
			const APrologueMonitorRoom* M = Mon();
			const FBatteryLook Look = Settings()->GetBatteryLook();
			const FGameAudio& A = Settings()->Audio;
			const float Seconds = Settings()->GetChoiceAftermathSeconds();
			const float Elapsed = C->GetAftermathAlpha() * Seconds;
			const float Surge = 1.f + Settings()->GetAftermathSurge();
			if (Ending == TEXT("Inject"))
			{
				Check(C->GetChamberState() == EEpilogueChamberState::Aftermath && B->GetGlowLevel() < Look.GlowLevelFull * Surge - 0.01f
					&& Aud()->GetHumVolume() < A.GetVolume(EGameSound::ChamberHum) * A.GetHumVolumeFull() * Surge - 0.005f,
					FString::Printf(TEXT("%s: at %.2f s of %.2f (its last second) not yet at its end: the glow %.3f below %.3f, the hum's volume %.3f below its surge"), *L, Elapsed, Seconds,
						B->GetGlowLevel(), Look.GlowLevelFull * Surge, Aud()->GetHumVolume()));
			}
			else
			{
				Check(C->GetChamberState() == EEpilogueChamberState::Aftermath && M->GetLitShownCount() > 0 && Aud()->GetHumVolume() > 0.f && B->GetGlowLevel() > 0.f && B->GetRingSpeed() > 0.f,
					FString::Printf(TEXT("%s: at %.2f s of %.2f (its last second) not yet at its end: %d monitors still lit, the hum at %.4f, the glow at %.3f, the rings at %.3f deg/s"), *L, Elapsed, Seconds,
						M->GetLitShownCount(), Aud()->GetHumVolume(), B->GetGlowLevel(), B->GetRingSpeed()));
			}
			return 0.f;
		}));
		// The aftermath's end: every part at its end state; the hold begins.
		S.Add(WaitFor(L + TEXT(": the aftermath's end, the hold begun"), []() { return Chamber()->GetChamberState() == EEpilogueChamberState::Hold || Chamber()->GetChamberState() == EEpilogueChamberState::Ended; }, 40.f, [L, Ending]()
		{
			const AEpilogueChamber* C = Chamber();
			const AChamberBattery* B = Bat();
			const APrologueMonitorRoom* M = Mon();
			const FBatteryLook Look = Settings()->GetBatteryLook();
			const FGameAudio& A = Settings()->Audio;
			const float Seconds = Settings()->GetChoiceAftermathSeconds();
			const float Took = C->GetHoldStartRunSeconds() - C->GetChosenRunSeconds();
			const float Slack = 2.f * GRun.AftermathLongestFrame + 0.02f;
			Check(C->GetChamberState() == EEpilogueChamberState::Hold && C->GetEndingRunSeconds() < 0.f && TopScene() == nullptr && GRun.AftermathPrompts == 0
				&& Took >= Seconds - 0.02f && Took <= Seconds + Slack,
				FString::Printf(TEXT("%s: the hall answered for %.3f s of the run's clock (ChoiceAftermathSeconds %.2f; within two frames, its longest %.3f s), then holds; no ending yet, nothing answered the player (%d prompt frames)"),
					*L, Took, Seconds, GRun.AftermathLongestFrame, GRun.AftermathPrompts));
			if (Ending == TEXT("Inject"))
			{
				Check(B->GetGlowLevel() > Look.GlowLevelFull && M->GetScreenLevel() >= 0.999f && M->GetLitShownCount() == M->GetLitPlates().Num(),
					FString::Printf(TEXT("%s: the glow above GlowLevelFull (%.2f > %.2f), the gauges full (%.3f)"), *L, B->GetGlowLevel(), Look.GlowLevelFull, M->GetScreenLevel()));
				Check(Aud()->GetHumVolume() > A.GetVolume(EGameSound::ChamberHum) * A.GetHumVolumeFull() && Aud()->GetHumPitch() > A.GetHumPitchFull(),
					FString::Printf(TEXT("%s: the hum above its full values (volume %.3f, pitch %.3f)"), *L, Aud()->GetHumVolume(), Aud()->GetHumPitch()));
			}
			else
			{
				Check(B->GetGlowLevel() < 0.01f * Look.GlowLevelFull && B->GetRingSpeed() < 0.01f * FMath::Max(Look.RingDegreesPerSecond, 0.01f) && M->GetLitShownCount() == 0
					&& FMath::Abs(C->GetLightShare() - Settings()->GetAftermathLightShare()) <= 0.02f && C->LockPanel->IsFullyOpen() && C->EntryDoor->IsOpen(),
					FString::Printf(TEXT("%s: the glow at nothing (%.3f), the rings still (%.3f deg/s), every monitor black (%d lit), the lights at %.3f (AftermathLightShare %.2f), the lock's panel and door open (%d, %d)"),
						*L, B->GetGlowLevel(), B->GetRingSpeed(), M->GetLitShownCount(), C->GetLightShare(), Settings()->GetAftermathLightShare(), C->LockPanel->IsFullyOpen() ? 1 : 0, C->EntryDoor->IsOpen() ? 1 : 0));
				Check(Aud()->GetHumVolume() < 0.001f, FString::Printf(TEXT("%s: the hum at silence (volume %.4f)"), *L, Aud()->GetHumVolume()));
			}
			GRun.HoldGlow = B->GetGlowLevel();
			GRun.HoldLights = C->GetLightShare();
			GRun.HoldHum = Aud()->GetHumVolume();
			GRun.HoldRings = B->GetRingSpeed();
			GRun.HoldGauge = M->GetScreenLevel();
			GRun.HoldLit = M->GetLitShownCount();
			StandOn(FVector(0.f, 1100.f, 0.f), 300.f);
			LookAt(FVector(0.f, 0.f, 700.f));
			return 0.3f;
		}));
		AddShotWait(S, Ending == TEXT("Inject") ? TEXT("13_Aftermath_Inject_End") : TEXT("14_Aftermath_Open_End"));
		// The hold: the hall as the aftermath left it, nothing answering, until its last frames.
		S.Add(WaitFor(L + TEXT(": the hold near its end"), []()
		{
			const AEpilogueChamber* C = Chamber();
			return C->GetChamberState() != EEpilogueChamberState::Hold || RunNow() - C->GetHoldStartRunSeconds() >= Settings()->GetAftermathHoldSeconds() - 0.25f;
		}, 40.f, [L]()
		{
			const AEpilogueChamber* C = Chamber();
			const AChamberBattery* B = Bat();
			const APrologueMonitorRoom* M = Mon();
			const bool bSame = FMath::IsNearlyEqual(B->GetGlowLevel(), GRun.HoldGlow, 1.e-4f) && FMath::IsNearlyEqual(C->GetLightShare(), GRun.HoldLights, 1.e-4f)
				&& FMath::IsNearlyEqual(Aud()->GetHumVolume(), GRun.HoldHum, 0.005f) && FMath::IsNearlyEqual(B->GetRingSpeed(), GRun.HoldRings, 1.e-4f)
				&& FMath::IsNearlyEqual(M->GetScreenLevel(), GRun.HoldGauge, 1.e-4f) && M->GetLitShownCount() == GRun.HoldLit;
			Check(C->GetChamberState() == EEpilogueChamberState::Hold && C->GetEndingRunSeconds() < 0.f && TopScene() == nullptr && GRun.AftermathPrompts == 0 && bSame,
				FString::Printf(TEXT("%s: through the hold the hall stays as the aftermath left it (glow %.3f, lights %.3f, hum %.3f, rings %.3f deg/s, gauges %.3f, %d monitors lit) and nothing answers (%d prompt frames, no scene)"),
					*L, B->GetGlowLevel(), C->GetLightShare(), Aud()->GetHumVolume(), B->GetRingSpeed(), M->GetScreenLevel(), M->GetLitShownCount(), GRun.AftermathPrompts));
			return 0.f;
		}));
		S.Add(Do(L + TEXT(": the hold's shot"), [Ending]() { RequestShot(Ending == TEXT("Inject") ? TEXT("15_Hold_Inject_End") : TEXT("16_Hold_Open_End")); return 0.f; }));
		S.Add(WaitFor(L + TEXT(": the ending's travel"), []() { return Chamber() && Chamber()->GetEndingRunSeconds() >= 0.f; }, 40.f, [L, Ending]()
		{
			const AEpilogueChamber* C = Chamber();
			const float Took = C->GetEndingRunSeconds() - C->GetChosenRunSeconds();
			const float Held = C->GetEndingRunSeconds() - C->GetHoldStartRunSeconds();
			const float Want = Settings()->GetChoiceAftermathSeconds() + Settings()->GetAftermathHoldSeconds();
			// The hold ends at the first frame past its length, and the ending is asked the frame after: two frames at most.
			const float Slack = 2.f * GRun.AftermathLongestFrame + 0.02f;
			Check(Took >= Want - 0.02f && Took <= Want + Slack && Held >= Settings()->GetAftermathHoldSeconds() - 0.02f - GRun.AftermathLongestFrame,
				FString::Printf(TEXT("%s: the ending asked %.3f s of the run's clock after the choice and %.3f s after the hold began (ChoiceAftermathSeconds %.2f + AftermathHoldSeconds %.2f; within two frames, its longest %.3f s)"),
					*L, Took, Held, Settings()->GetChoiceAftermathSeconds(), Settings()->GetAftermathHoldSeconds(), GRun.AftermathLongestFrame));
			GRun.AftermathLongestFrame = 0.f;
			GRun.AftermathLastRun = -1.0;
			if (Ending == TEXT("Open"))
			{
				Check(Aud()->GetHumVolume() == 0.f, FString::Printf(TEXT("%s: the hall silent before the scene opens (hum volume %.4f)"), *L, Aud()->GetHumVolume()));
			}
			return 0.f;
		}));
	}

	// ============================================================== Brief 36B follow-up: the column's flicker, tls.SeamCheck's measure

	struct FColumnFlicker
	{
		TArray<int8> Last;
		TArray<int8> BeforeLast;
		TArray<uint8> Changes;
		TArray<uint8> Uncovered;
		TArray<FColor> Hidden;
		TArray<FColor> Alone;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Spines;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Others;
		TArray<TPair<FString, FString>> SavedCVars;
		FVector Base = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		FVector Step = FVector::ZeroVector;
		int32 Width = 0;
	};
	static FColumnFlicker GFlick;

	static void ShowSpines(bool bShow)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& C : GFlick.Spines)
		{
			if (C.IsValid()) { C->SetVisibility(bShow); }
		}
	}

	static void ShowOtherPieces(bool bShow)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& C : GFlick.Others)
		{
			if (C.IsValid()) { C->SetVisibility(bShow); }
		}
	}

	static void PlaceFlickerEye(int32 K)
	{
		APawn* P = Pawn();
		P->SetActorLocation(GFlick.Base + GFlick.Step * K, false, nullptr, ETeleportType::TeleportPhysics);
		if (ACharacter* C = Cast<ACharacter>(P))
		{
			C->GetCharacterMovement()->StopMovementImmediately();
		}
		PC()->SetControlRotation((GFlick.Look + GFlick.Step * K - P->GetPawnViewLocation()).Rotation());
	}

	/**
	 * 30 frames of the column from a view, the camera moving 1 cm a frame across: each frame shot with the spines hidden, the
	 * spines alone and everything drawn. A pixel the spines alone cover is the spines' where drawn and hidden differ by 48 levels
	 * or more, another surface's where they differ by 6 or less; one that goes A, B, A three times or more, with a neighbour that
	 * does too, flickers (tls.SeamCheck's rule for a frame, its anti-aliasing, shadows and screen-space effects off).
	 */
	static void AddColumnFlicker(TArray<FStep>& S, const FString& Label, FVector Floor, float FromZ, FVector Look, bool bProfile)
	{
		S.Add(Do(Label + TEXT(": place"), [Floor, FromZ, Look, bProfile]()
		{
			GFlick = FColumnFlicker();
			StandOn(Floor, FromZ);
			GFlick.Base = Pawn()->GetActorLocation();
			GFlick.Look = Look;
			const FVector View = (Look - Pawn()->GetPawnViewLocation()).GetSafeNormal2D();
			GFlick.Step = FVector::CrossProduct(FVector::UpVector, View).GetSafeNormal();
			PlaceFlickerEye(0);
			if (bProfile)
			{
				GEngine->Exec(W(), TEXT("ProfileGPU"));
			}
			return 1.0f;
		}));
		S.Add(Do(Label + TEXT(": settings"), []()
		{
			for (const TCHAR* Name : { TEXT("r.AntiAliasingMethod"), TEXT("r.AmbientOcclusionLevels"), TEXT("r.SSR.Quality"), TEXT("r.ContactShadows"), TEXT("r.ShadowQuality") })
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					GFlick.SavedCVars.Emplace(Name, Var->GetString());
					Var->Set(0, ECVF_SetByConsole);
				}
			}
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() != W() || !It->IsRegistered() || !It->IsVisible() || !It->GetStaticMesh())
				{
					continue;
				}
				(It->GetStaticMesh()->GetName() == TEXT("SM_BatterySpine") ? GFlick.Spines : GFlick.Others).Add(*It);
			}
			return 0.3f;
		}));
		for (int32 K = 0; K < 30; ++K)
		{
			S.Add(Do(Label + TEXT(": hidden"), [K]() { PlaceFlickerEye(K); ShowSpines(false); return 0.1f; }));
			AddShotWait(S, FString(), []() { GFlick.Hidden = GRun.ShotPixels; ShowSpines(true); ShowOtherPieces(false); return 0.1f; });
			AddShotWait(S, FString(), []() { GFlick.Alone = GRun.ShotPixels; ShowOtherPieces(true); return 0.1f; });
			AddShotWait(S, K == 0 ? FString::Printf(TEXT("Flicker_%s"), *Label) : FString(), [K]()
			{
				const TArray<FColor>& Drawn = GRun.ShotPixels;
				if (GFlick.Hidden.Num() != Drawn.Num() || GFlick.Alone.Num() != Drawn.Num())
				{
					return 0.f;
				}
				if (K == 0)
				{
					GFlick.Width = GRun.ShotW;
					GFlick.Last.Init(-1, Drawn.Num());
					GFlick.BeforeLast.Init(-1, Drawn.Num());
					GFlick.Changes.Init(0, Drawn.Num());
					GFlick.Uncovered.Init(0, Drawn.Num());
				}
				for (int32 I = 0; I < Drawn.Num() && I < GFlick.Last.Num(); ++I)
				{
					const FColor& A = GFlick.Alone[I];
					if (FMath::Max3(A.R, A.G, A.B) <= 10)
					{
						GFlick.Uncovered[I] = 1;
						continue;
					}
					const FColor& D = Drawn[I];
					const FColor& H = GFlick.Hidden[I];
					if (FMath::Max3(D.R, D.G, D.B) < 12 && FMath::Max3(H.R, H.G, H.B) < 12)
					{
						continue;
					}
					const int32 Diff = FMath::Abs(D.R - H.R) + FMath::Abs(D.G - H.G) + FMath::Abs(D.B - H.B);
					const int32 Class = Diff >= 48 ? 1 : (Diff <= 6 ? 0 : -1);
					if (Class < 0)
					{
						continue;
					}
					if (GFlick.BeforeLast[I] >= 0 && GFlick.Last[I] >= 0 && GFlick.BeforeLast[I] == Class && GFlick.Last[I] != Class && GFlick.Changes[I] < 255)
					{
						++GFlick.Changes[I];
					}
					GFlick.BeforeLast[I] = GFlick.Last[I];
					GFlick.Last[I] = static_cast<int8>(Class);
				}
				return 0.f;
			});
		}
		S.Add(Do(Label + TEXT(": counted"), [Label]()
		{
			for (const TPair<FString, FString>& Saved : GFlick.SavedCVars)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
				{
					Var->Set(*Saved.Value, ECVF_SetByConsole);
				}
			}
			const int32 W0 = GFlick.Width;
			auto Flickers = [](int32 I) { return GFlick.Changes.IsValidIndex(I) && GFlick.Changes[I] >= 3 && GFlick.Uncovered[I] == 0; };
			int32 Flicker = 0, Isolated = 0, Covered = 0, Spine = 0;
			for (int32 I = 0; I < GFlick.Changes.Num(); ++I)
			{
				Covered += GFlick.Uncovered[I] == 0 ? 1 : 0;
				Spine += GFlick.Last[I] == 1 ? 1 : 0;
				if (!Flickers(I))
				{
					continue;
				}
				const int32 X = W0 > 0 ? I % W0 : 0;
				bool bNeighbour = false;
				for (int32 DY = -1; DY <= 1 && !bNeighbour; ++DY)
				{
					for (int32 DX = -1; DX <= 1 && !bNeighbour; ++DX)
					{
						bNeighbour = (DX != 0 || DY != 0) && X + DX >= 0 && X + DX < W0 && Flickers(I + DY * W0 + DX);
					}
				}
				(bNeighbour ? Flicker : Isolated) += 1;
			}
			Check(Flicker == 0, FString::Printf(TEXT("flicker %s: %d pixels alternate between the spines and another surface on 30 frames, the camera moving 1 cm a frame (%d isolated ones left out; %d pixels the spines cover, %d showing the spines)"),
				*Label, Flicker, Isolated, Covered, Spine));
			return 0.2f;
		}));
	}

	static TArray<FStep> BuildFlicker()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("flicker: in the hall, the lines said"), []() { return Offering(); }, 120.f, []()
		{
			// What moves by itself would change pixels between a frame's three shots: held still for the measure.
			if (AChamberBattery* B = Bat()) { B->SetActorTickEnabled(false); }
			if (APrologueMonitorRoom* M = Mon()) { M->SetActorTickEnabled(false); }
			return 0.5f;
		}));
		S.Add(Do(TEXT("faces: sorted"), []()
		{
			const FColumnFaces F = ColumnFaces(1.f, true);
			for (const TPair<FString, int32>& P : F.SameByPair) { Note(FString::Printf(TEXT("faces: %d pairs facing the same way and overlapping: %s"), P.Value, *P.Key)); }
			for (const TPair<FString, int32>& P : F.FacingByPair) { Note(FString::Printf(TEXT("faces: %d pairs facing each other or back to back: %s"), P.Value, *P.Key)); }
			for (const FString& Sample : F.Samples) { Note(TEXT("faces: ") + Sample); }
			Note(FString::Printf(TEXT("faces: %d triangles of %d pieces; %d pairs facing the same way, %d facing each other or back to back"), F.Tris, F.Pieces, F.Same, F.Facing));
			// The least change of placement that separates them: the spines' width (x0.9 to x1.1), their height and their turn about
			// the column's axis, tried together; the change is measured as the largest move of a spine's face, in cm.
			float BestCost = 1.0e9f, BestScale = 1.f, BestDz = 0.f, BestYaw = 0.f;
			int32 Tried = 0;
			struct FTry { float Scale, Dz, Yaw, Cost; };
			TArray<FTry> Tries;
			for (const float Scale : { 1.f, 0.98f, 1.02f, 0.95f, 1.05f, 0.9f, 1.1f })
			{
				for (const float Dz : { 0.f, 0.5f, -0.5f, 1.f, -1.f, 1.5f, -1.5f, 2.f, -2.f })
				{
					for (const float Yaw : { 0.f, 0.1f, -0.1f, 0.2f, -0.2f, 0.35f, -0.35f, 0.5f, -0.5f })
					{
						Tries.Add({ Scale, Dz, Yaw, FMath::Max3(FMath::Abs(Scale - 1.f) * 18.f, FMath::Abs(Dz), FMath::Abs(FMath::DegreesToRadians(Yaw)) * 365.f) });
					}
				}
			}
			Tries.Sort([](const FTry& A, const FTry& B) { return A.Cost < B.Cost; });
			int32 Least = INT32_MAX;
			FTry LeastAt = Tries[0];
			for (const FTry& T : Tries)
			{
				++Tried;
				const FColumnFaces Tried1 = ColumnFaces(T.Scale, false, T.Dz, T.Yaw);
				if (Tried1.Same < Least)
				{
					Least = Tried1.Same;
					LeastAt = T;
				}
				if (Tried1.Same == 0)
				{
					BestCost = T.Cost;
					BestScale = T.Scale;
					BestDz = T.Dz;
					BestYaw = T.Yaw;
					break;
				}
			}
			Note(FString::Printf(TEXT("faces: the fewest pairs facing the same way, %d, at x%.2f across, %+.1f cm up, %+.2f degrees"), Least, LeastAt.Scale, LeastAt.Dz, LeastAt.Yaw));
			if (BestCost < 1.0e8f)
			{
				const FColumnFaces T = ColumnFaces(BestScale, false, BestDz, BestYaw);
				Note(FString::Printf(TEXT("faces: FIT the spines x%.2f across, %+.1f cm up, turned %+.2f degrees about the axis (a face moves %.2f cm at most): %d pairs facing the same way, %d facing each other (%d placements tried)"),
					BestScale, BestDz, BestYaw, BestCost, T.Same, T.Facing, Tried));
			}
			else
			{
				Note(FString::Printf(TEXT("faces: NO FIT among %d placements (width x0.9 to x1.1, height -2 to +2 cm, turn -0.5 to +0.5 degrees)"), Tried));
			}
			return 0.f;
		}));
		AddColumnFlicker(S, TEXT("Gallery"), FVector(0.f, 1800.f, 0.f), 1000.f, FVector(0.f, 0.f, 600.f), true);
		AddColumnFlicker(S, TEXT("WalkwayFoot"), FVector(-1850.f, 200.f, 0.f), 300.f, FVector(0.f, 0.f, 250.f), false);
		AddColumnFlicker(S, TEXT("Terminal"), FVector(0.f, 600.f, 0.f), 300.f, FVector(0.f, 0.f, 300.f), false);
		return S;
	}

	static void RunFlicker(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (GRun.bRunning || !ResolveGameWorld(InWorld))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Chamber.Flicker: run it in Epilogue_Chamber (PIE), nothing else running."));
			return;
		}
		GRun = FRun();
		GRun.bNoSaveTouch = true;
		GRun.bQuitAtEnd = Args.Contains(TEXT("Quit")) || Args.Contains(TEXT("quit"));
		GRun.bSavedReturn = Settings()->bReturnHome;
		GRun.bSavedMaps = Settings()->bPrologueMaps;
		GRun.bSavedChamber = Settings()->bChoiceChamber;
		GRun.SavedHold = Settings()->ChoiceHoldSeconds;
		GRun.SavedReach = Settings()->ChoiceTerminalReachCm;
		GRun.SavedAftermath = Settings()->ChoiceAftermathSeconds;
		GRun.SavedAftermathHold = Settings()->AftermathHoldSeconds;
		GRun.SavedHum = Settings()->Audio.ChamberHum;
		if (IConsoleVariable* Synthetic = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableSyntheticCursorMoves")))
		{
			GRun.bSavedSynthetic = Synthetic->GetBool();
		}
		GRun.Steps = BuildFlicker();
		GRun.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Chamber.Flicker: %d steps, in %s."), GRun.Steps.Num(), *MapName());
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

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
			GRun.HumRecordFrom = Aud() ? Aud()->GetRecordEnd() : 0;
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

		AddHall(S);
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
		AddShot(S, TEXT("06_Terminal_From2m_Prompt"), 0.2f);

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
		AddAftermath(S, TEXT("inject"), TEXT("Inject"));
		AddChooseAndBack(S, TEXT("inject"), TEXT("Inject"));

		// --- Criterion 6: Open held through with the confirm key, at a hold set for the session ---
		AddToChamber(S, TEXT("open"));
		S.Add(Do(TEXT("open: the aftermath set to 2 s and the hold to 1.5 s for the session"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceAftermathSeconds = 2.f;
			GetMutableDefault<UTheLastShiftSettings>()->AftermathHoldSeconds = 1.5f;
			return 0.f;
		}));
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
		AddAftermath(S, TEXT("open"), TEXT("Open"));
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
		S.Add(Do(TEXT("residue: gone"), []()
		{
			if (AResidue* R = GRun.Residue.Get())
			{
				R->Destroy();
			}
			GetMutableDefault<UTheLastShiftSettings>()->ChoiceAftermathSeconds = GRun.SavedAftermath;
			GetMutableDefault<UTheLastShiftSettings>()->AftermathHoldSeconds = GRun.SavedAftermathHold;
			return 0.2f;
		}));
		S.Add(Do(TEXT("one part less: back to the main menu"), []()
		{
			Check(Flow()->ReturnToMainMenu(), TEXT("one part less: back to the main menu"));
			return 0.5f;
		}));

		// --- Criterion 2: the last row dropped at Silent ---
		S.Add(Do(TEXT("hum: the slot emptied for the session"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->Audio.ChamberHum.Sound.Reset();
			GRun.HumRecordFrom = Aud() ? Aud()->GetRecordEnd() : 0;
			return 0.f;
		}));
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

		S.Add(Do(TEXT("hum: with no sound"), []()
		{
			int32 Asked = 0;
			int32 Played = 0;
			const TArray<FGameSoundRequest>& Rec = Aud()->GetRecord();
			for (int32 K = GRun.HumRecordFrom - Aud()->GetRecordOffset(); K < Rec.Num(); ++K)
			{
				if (K >= 0 && Rec[K].Sound == EGameSound::ChamberHum)
				{
					++Asked;
					Played += Rec[K].bHadSound ? 1 : 0;
				}
			}
			// The visit before went on as without it (its terminal offered, above).
			Check(Asked == 1 && Played == 0, FString::Printf(TEXT("hum: with the slot emptied for the session it was asked for once in the hall (%d), nothing played (%d), and the hall went on"), Asked, Played));
			GetMutableDefault<UTheLastShiftSettings>()->Audio.ChamberHum = GRun.SavedHum;
			return 0.f;
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
			// Brief 36B: the imported pieces, each with its triangles, family, collision and bytes; SM_BatteryColumn gone.
			struct FPiece { const TCHAR* Folder; const TCHAR* Name; const TCHAR* Family; };
			const FPiece Pieces[] = {
				{ TEXT("Prologue"), TEXT("SM_BatteryTier"), TEXT("MI_Surface_Battery") }, { TEXT("Prologue"), TEXT("SM_BatteryTierGlow"), TEXT("MI_BatteryGlow") },
				{ TEXT("Prologue"), TEXT("SM_BatteryTierB"), TEXT("MI_Surface_Battery") }, { TEXT("Prologue"), TEXT("SM_BatteryTierBGlow"), TEXT("MI_BatteryGlow") },
				{ TEXT("Prologue"), TEXT("SM_BatteryRing"), TEXT("MI_Surface_Battery") }, { TEXT("Prologue"), TEXT("SM_BatterySpine"), TEXT("MI_Surface_Battery") },
				{ TEXT("Prologue"), TEXT("SM_BatterySpineGlow"), TEXT("MI_BatteryGlow") }, { TEXT("Prologue"), TEXT("SM_BatteryDuct"), TEXT("MI_Surface_Battery") },
				{ TEXT("Prologue"), TEXT("SM_BatteryDuctSocket"), TEXT("MI_Surface_Battery") }, { TEXT("Prologue"), TEXT("SM_ContainmentParapet"), TEXT("MI_Surface_Structure") },
				{ TEXT("Prologue"), TEXT("SM_ControlBank"), TEXT("MI_Surface_Object") }, { TEXT("Prologue"), TEXT("SM_CoolantTank"), TEXT("MI_Surface_Object") },
				{ TEXT("Prologue"), TEXT("SM_SwitchgearCabinet"), TEXT("MI_Surface_Object") }, { TEXT("Prologue"), TEXT("SM_TerminalPortal"), TEXT("MI_Surface_Accent") },
				{ TEXT("Kit"), TEXT("SM_KitRailing"), TEXT("MI_Surface_Structure") }, { TEXT("Kit"), TEXT("SM_KitRailingRamp"), TEXT("MI_Surface_Structure") },
				{ TEXT("Kit"), TEXT("SM_KitTrenchCover"), TEXT("MI_Surface_FloorPlate") }, { TEXT("Kit"), TEXT("SM_KitBollard"), TEXT("MI_Surface_Structure") },
			};
			int32 Good = 0;
			int64 Total = 0;
			for (const FPiece& P : Pieces)
			{
				const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/TheLastShift/Art/%s/%s.%s"), P.Folder, P.Name, P.Name));
				const int32 Tris = Mesh && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0 ? Mesh->GetRenderData()->LODResources[0].GetNumTriangles() : -1;
				const UMaterialInterface* Material = Mesh && Mesh->GetStaticMaterials().Num() > 0 ? Mesh->GetStaticMaterials()[0].MaterialInterface.Get() : nullptr;
				const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
				const int32 Boxes = Body ? Body->AggGeom.BoxElems.Num() : 0;
				const int32 Convex = Body ? Body->AggGeom.ConvexElems.Num() : 0;
				const int64 Bytes = IFileManager::Get().FileSize(*FPaths::Combine(FPaths::ProjectContentDir(), FString::Printf(TEXT("TheLastShift/Art/%s/%s.uasset"), P.Folder, P.Name)));
				Total += FMath::Max<int64>(Bytes, 0);
				const bool bOk = Mesh && Tris > 0 && Tris <= 8000 && Mesh->GetStaticMaterials().Num() == 1 && GetNameSafe(Material) == P.Family;
				Good += bOk ? 1 : 0;
				Note(FString::Printf(TEXT("mesh %s: %d triangles, slot %s, collision %d boxes and %d convex, %lld bytes%s"), P.Name, Tris, *GetNameSafe(Material), Boxes, Convex, Bytes, bOk ? TEXT("") : TEXT(" (WRONG)")));
			}
			Check(Good == UE_ARRAY_COUNT(Pieces), FString::Printf(TEXT("meshes: the %d imported pieces, each under its budget, one slot in its family (%d), %lld bytes in all"), static_cast<int32>(UE_ARRAY_COUNT(Pieces)), Good, Total));
			const FString Column = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("TheLastShift/Art/Prologue/SM_BatteryColumn.uasset"));
			Check(!IFileManager::Get().FileExists(*Column) && LoadObject<UObject>(nullptr, TEXT("/Game/TheLastShift/Art/Prologue/SM_BatteryColumn.SM_BatteryColumn"), nullptr, LOAD_NoWarn | LOAD_Quiet) == nullptr,
				TEXT("meshes: SM_BatteryColumn gone, with no redirector"));
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
		GRun.SavedReach = Settings()->ChoiceTerminalReachCm;
		GRun.SavedAftermath = Settings()->ChoiceAftermathSeconds;
		GRun.SavedAftermathHold = Settings()->AftermathHoldSeconds;
		GRun.SavedHum = Settings()->Audio.ChamberHum;
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

	static FAutoConsoleCommandWithWorldAndArgs GChamberFlickerCmd(
		TEXT("tls.Chamber.Flicker"),
		TEXT("tls.Chamber.Flicker [Quit]: (Epilogue_Chamber, PIE) sorts the column's face pairs within 1 cm of one plane (facing the same way and overlapping; facing each other or back to back), tries the spines' width from x0.9 to x1.1, and measures the pixels that alternate between the spines and another surface from the gallery, the walkway's foot and the terminal, tls.SeamCheck's way (30 frames, 1 cm a frame; a ProfileGPU from the gallery)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlicker));

	static FAutoConsoleCommandWithWorldAndArgs GChamberBatteryCmd(
		TEXT("tls.Chamber.Battery"),
		TEXT("tls.Chamber.Battery <Fraction>: the battery's hall shows that battery level for the session (its glow, pulses, monitors and hum); below 0 or no argument: the run's own again."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Fraction = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -1.f;
			AChamberBattery::SetSessionBattery(Fraction);
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Chamber.Battery: %s."), AChamberBattery::GetSessionBattery() < 0.f ? TEXT("the run's own battery") : *FString::Printf(TEXT("%.3f for the session"), AChamberBattery::GetSessionBattery()));
		}));

	static FAutoConsoleCommandWithWorldAndArgs GRunChamberCmd(
		TEXT("tls.Run.Chamber"),
		TEXT("Puts the run on its way home (battery and twin AI parts given, else the run's own) and travels to the battery's chamber. Any map. Args: [Battery] [TwinParts]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunChamber));
}

#endif // !UE_BUILD_SHIPPING
