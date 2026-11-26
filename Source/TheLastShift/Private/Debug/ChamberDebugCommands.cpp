// SPDX-License-Identifier: MIT

// The battery's chamber. tls.ChamberCheck [Quit] (run from MainMenu, in PIE; clears the save slot) checks, with its own
// fixture rows: the way in from the way home's corridor (the Passage label, the marker, no ending, the run's state kept, no
// entry of its own, the input); the room (its size, the doorway shut behind the player, the column, the racks and their
// lights, the two places facing each other, the lights, home's look); the lines (an engineer line, the Chamber row and the
// home AI's last message on the channel, shortened by the battery); the wait before the places offer (the last row typed
// out, dropped at Silent, no row at all); the intake (its label, the hold with the collection's ring, a release cancelling,
// the Inject text alone, then the main menu); the outer hatch (its label with every twin AI part, the panel opening, the Open
// text alone; with one part less nothing at any distance and nothing on Interact); the run after an ending; quitting from the
// chamber and Continue; the two switches; the column's mesh. Shots to Saved/Screenshots/Brief31B. tls.Run.Chamber [Battery]
// [TwinParts] puts the run on its way home and travels to the chamber (the developer's check). Development builds only.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Epilogue/EpilogueChamber.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdMenuPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "PhysicsEngine/BodySetup.h"
#include "Prologue/PrologueMonitorRoom.h"
#include "Prologue/PrologueReturn.h"
#include "Prologue/PrologueSlidingPanel.h"
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
#include "UI/InteractPromptWidget.h"
#include "UI/StdMainMenuWidget.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

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

		// Sampled every frame.
		TArray<FLine> Lines;
		FName LastRow;
		bool bMarkerShown = false;
		bool bMarkerRightToLeft = true;
		float MarkerFirst = -1.f;
		float MarkerLast = -1.f;
		int32 EarlyOffers = 0;
		int32 RingFrames = 0;
		int32 PromptFramesNearHatch = 0;
		bool bHatchShotTaken = false;
		double HatchOpenRun = -1.0;
		float HatchMidFraction = -1.f;
		int32 LightsOnMin = TNumericLimits<int32>::Max();
		int32 LightsOnMax = -1;

		// Scratch.
		FState AtExit;
		double Battery = 0.5;
		double SilentBattery = 0.01;
		ETextDegradation Level = ETextDegradation::Full;
		FText ExpectedLabel;
		int32 SaveWritesAtReturn = -1;
		double PressRun = -1.0;
		int32 TerminalEvents = 0;
		FDelegateHandle TerminalHandle;
		FKey InteractKey;
		int32 HoldsBefore = 0;
		int32 LinesBefore = 0;
		int32 ChamberVisits = 0;
		int32 VisitsBefore = 0;
		bool bWasInChamber = false;
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

	static void Shot(const FString& ShotName)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief31B"), ShotName + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot %s"), *Path));
	}

	static void Finish(const TCHAR* Why)
	{
		UTheLastShiftSettings* Mutable = GetMutableDefault<UTheLastShiftSettings>();
		Mutable->bReturnHome = GRun.bSavedReturn;
		Mutable->bPrologueMaps = GRun.bSavedMaps;
		Mutable->bChoiceChamber = GRun.bSavedChamber;
		APowerTerminal::OnAnyActivated.Remove(GRun.TerminalHandle);
		if (PC() && GRun.InteractKey.IsValid())
		{
			PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Released, 0.f));
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
		// Neither place offers anything before the last message is typed out.
		if (const AEpilogueChamber* C = Chamber(); C && URunSubsystem::IsChamberOf(W()))
		{
			const bool bBefore = C->GetChamberState() == EEpilogueChamberState::Waiting || C->GetChamberState() == EEpilogueChamberState::Lines;
			if (bBefore && ((C->Intake && C->Intake->OffersChoice()) || (C->HatchTerminal && C->HatchTerminal->OffersChoice())))
			{
				++GRun.EarlyOffers;
			}
			if (const UResidueCollectCueWidget* Cue = FindWidget<UResidueCollectCueWidget>(); Cue && Cue->IsRingShown())
			{
				++GRun.RingFrames;
			}
			if (const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>(); Room && Room->AreLightsShown())
			{
				GRun.LightsOnMin = FMath::Min(GRun.LightsOnMin, Room->GetLightsOnCount());
				GRun.LightsOnMax = FMath::Max(GRun.LightsOnMax, Room->GetLightsOnCount());
			}
			// The first frame the outer hatch is seen fully open, in any state of the chamber.
			if (GRun.HatchOpenRun < 0.0 && C->OuterHatch && C->OuterHatch->GetOpenFraction() >= 1.f)
			{
				GRun.HatchOpenRun = Now;
			}
			// The hatch open on the dark: shot once it has (nearly) opened, before the travel covers the screen.
			if (C->GetChamberState() == EEpilogueChamberState::HatchOpening && C->OuterHatch)
			{
				const float Open = C->OuterHatch->GetOpenFraction();
				if (GRun.HatchMidFraction < 0.f && C->GetChosenRunSeconds() >= 0.f && Now - C->GetChosenRunSeconds() >= Settings()->GetChamberHatchOpenSeconds() * 0.5)
				{
					GRun.HatchMidFraction = Open;
				}
				if (!GRun.bHatchShotTaken && Open >= 0.97f)
				{
					GRun.bHatchShotTaken = true;
					Shot(TEXT("8_Hatch_OpenOnTheDark"));
				}
			}
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
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

	static float Trace(const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("ChamberCheck"), false, Pawn());
		return W()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) ? static_cast<float>(Hit.Distance) : -1.f;
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

	static UInteractPromptWidget* Prompt() { return FindWidget<UInteractPromptWidget>(); }
	static UResidueCollectCueWidget* Cue() { return FindWidget<UResidueCollectCueWidget>(); }

	static void PressInteract()
	{
		GRun.PressRun = RunNow();
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Pressed, 1.f));
	}

	static void ReleaseInteract()
	{
		PC()->InputKey(FInputKeyEventArgs::CreateSimulated(GRun.InteractKey, IE_Released, 0.f));
	}

	/** The room's centre (the column's foot), from the chamber actor's two places. */
	static FVector RoomCentre()
	{
		const AEpilogueChamber* C = Chamber();
		if (!C || !C->Intake || !C->HatchTerminal)
		{
			return FVector::ZeroVector;
		}
		const FVector Mid = (C->Intake->GetActorLocation() + C->HatchTerminal->GetActorLocation()) * 0.5f;
		return FVector(0.f, 0.f, Mid.Z);
	}

	/** Stands the pawn Distance in front of a place (toward the room's centre), looking at it. */
	static void FacePlace(const AActor* Place, float Distance)
	{
		if (!Place)
		{
			return;
		}
		const FVector Out = (RoomCentre() - Place->GetActorLocation()).GetSafeNormal2D();
		Teleport(Place->GetActorLocation() + Out * Distance);
		LookAt(Place->GetActorLocation() + FVector(0.f, 0.f, 100.f));
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

	/** The column's radius at mid height, by a trace from the doorway's side toward the centre. */
	static float ColumnRadius()
	{
		const AEpilogueChamber* C = Chamber();
		const FVector Centre = RoomCentre() + FVector(0.f, 0.f, 350.f);
		const FVector In = C && C->EntryDoor ? (RoomCentre() - C->EntryDoor->GetActorLocation()).GetSafeNormal2D() : FVector::XAxisVector;
		const float Hit = Trace(Centre - In * 600.f, Centre);
		return Hit >= 0.f ? 600.f - Hit : -1.f;
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
			UHomeAIComponent::BuildPassageTravelLabel(W(), GRun.ExpectedLabel, GRun.HatchMidFraction);
			GRun.HatchMidFraction = -1.f;
			Teleport(Ret()->CorridorExit->GetActorLocation());
			return 0.f;
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

	/** Holds Interact at Place until its ending's travel starts. */
	static void AddHoldThrough(TArray<FStep>& S, const FString& L, TFunction<AChoiceTerminal*()> Place)
	{
		S.Add(Do(L + TEXT(": hold Interact"), [Place]()
		{
			FacePlace(Place(), 100.f);
			GRun.RingFrames = 0;
			PressInteract();
			return 0.f;
		}));
		S.Add(WaitFor(L + TEXT(": held through, the choice made"), [Place]() { return Place() && Place()->HasChosen(); }, 10.f, [L, Place]()
		{
			ReleaseInteract();
			const AEpilogueChamber* C = Chamber();
			const float Took = C->GetChosenRunSeconds() - static_cast<float>(GRun.PressRun);
			Check(FMath::Abs(Took - Settings()->GetChoiceHoldSeconds()) <= 0.15f && GRun.RingFrames > 0,
				FString::Printf(TEXT("%s: the hold took %.2f s of the run's clock (ChoiceHoldSeconds %.2f), the ring shown %d frames"), *L, Took, Settings()->GetChoiceHoldSeconds(), GRun.RingFrames));
			Check(C->GetChosenEnding() == Place()->Ending && !C->Intake->OffersChoice() && !C->HatchTerminal->OffersChoice(),
				FString::Printf(TEXT("%s: %s chosen; nothing in the room answers any more"), *L, *C->GetChosenEnding().ToString()));
			Check(Place()->BeginHold(Pawn()) == false, FString::Printf(TEXT("%s: a second hold after the choice is refused"), *L));
			return 0.f;
		}));
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
			Note(FString::Printf(TEXT("battery for the run %.3f (%s), Silent battery %.3f; ChoiceHoldSeconds %.2f, ChamberHatchOpenSeconds %.2f, OpenEndingTwinParts %d"), GRun.Battery,
				LexToString(GRun.Level), GRun.SilentBattery, Settings()->GetChoiceHoldSeconds(), Settings()->GetChamberHatchOpenSeconds(), Settings()->GetOpenEndingTwinParts()));
			Check(Settings()->IsChoiceChamberOn() && Settings()->IsChamberMap(Settings()->ChamberMap.ToSoftObjectPath().GetLongPackageName()) && Settings()->IsHomeLookMap(Settings()->ChamberMap.ToSoftObjectPath().GetLongPackageName()),
				FString::Printf(TEXT("setup: the chamber on, its map %s, in home's look"), *Settings()->ChamberMap.ToString()));
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

		// --- Criterion 1: the way in ---
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

		// --- Criterion 2: the room ---
		S.Add(Do(TEXT("room: from the doorway"), []()
		{
			const AEpilogueChamber* C = Chamber();
			const FVector Centre = RoomCentre();
			const FVector P0 = Centre + FVector(-450.f, -450.f, 450.f);
			const float SizeX = Trace(P0, P0 + FVector(3000.f, 0.f, 0.f)) + Trace(P0, P0 - FVector(3000.f, 0.f, 0.f));
			const float SizeY = Trace(P0, P0 + FVector(0.f, 3000.f, 0.f)) + Trace(P0, P0 - FVector(0.f, 3000.f, 0.f));
			const FVector Low = Centre + FVector(-450.f, -450.f, 50.f);
			const float Height = Trace(Low, Low + FVector(0.f, 0.f, 3000.f)) + Trace(Low, Low - FVector(0.f, 0.f, 300.f));
			Check(SizeX >= 1700.f && SizeX <= 1900.f && SizeY >= 1700.f && SizeY <= 1900.f && Height >= 650.f && Height <= 750.f,
				FString::Printf(TEXT("room: %.0f x %.0f cm, %.0f cm high (about 18 x 18 x 7 m)"), SizeX, SizeY, Height));
			// Closed: no way out at the player's height around the room.
			int32 Open = 0;
			for (int32 I = 0; I < 36; ++I)
			{
				const FVector Dir = FRotator(0.f, I * 10.f, 0.f).Vector();
				Open += Trace(Centre + FVector(0.f, 0.f, 150.f) + Dir * 200.f, Centre + FVector(0.f, 0.f, 150.f) + Dir * 3000.f) < 0.f ? 1 : 0;
			}
			Check(Open == 0, FString::Printf(TEXT("room: closed all round at 1.5 m (%d of 36 rays out)"), Open));
			const APawn* P = Pawn();
			const FVector ToColumn = (Centre - P->GetActorLocation()).GetSafeNormal2D();
			const float Facing = FVector::DotProduct(PC()->GetControlRotation().Vector().GetSafeNormal2D(), ToColumn);
			Check(C->EntryDoor && !C->EntryDoor->IsOpen() && FVector::Dist2D(P->GetActorLocation(), C->EntryDoor->GetActorLocation()) < 300.f && Facing > 0.95f,
				FString::Printf(TEXT("room: the player just inside the doorway (%.0f cm), its door shut behind, looking at the column (%.2f)"), FVector::Dist2D(P->GetActorLocation(), C->EntryDoor->GetActorLocation()), Facing));
			const FVector DoorToCentre = Centre - C->EntryDoor->GetActorLocation();
			const float DoorSide = FMath::Min(FMath::Abs(FMath::Abs(DoorToCentre.X) - 900.f), FMath::Abs(FMath::Abs(DoorToCentre.Y) - 900.f));
			Check(DoorSide < 50.f && FMath::Min(FMath::Abs(DoorToCentre.X), FMath::Abs(DoorToCentre.Y)) < 60.f,
				FString::Printf(TEXT("room: the doorway in the middle of a wall (%s from the centre)"), *DoorToCentre.ToCompactString()));
			const UStaticMeshComponent* Column = ColumnComponent();
			const FBox Box = Column ? Column->Bounds.GetBox() : FBox(ForceInit);
			const float Radius = ColumnRadius();
			Check(Column && FVector::Dist2D(Column->GetComponentLocation(), Centre) < 30.f && Box.Min.Z <= Centre.Z + 5.f && Box.Max.Z >= Centre.Z + Height - 20.f
				&& Radius >= 110.f && Radius <= 200.f,
				FString::Printf(TEXT("room: the battery's column at the centre, floor to ceiling, %.0f cm across at mid height (%s)"), Radius * 2.f, *Box.ToString()));
			// The racks on the wall facing the entry and on the entry's own wall, their lights.
			int32 Far = 0;
			int32 Near = 0;
			const FVector InDir = ToColumn;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == W() && It->GetStaticMesh() && It->GetStaticMesh()->GetName() == TEXT("SM_ComputeRack"))
				{
					const float Along = FVector::DotProduct(It->GetComponentLocation() - Centre, InDir);
					Far += Along > 700.f ? 1 : 0;
					Near += Along < -700.f ? 1 : 0;
				}
			}
			const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>();
			Check(Far > 0 && Near > 0 && Room && Room->GetLightCount() > 0 && Room->AreLightsShown(),
				FString::Printf(TEXT("room: racks on the far wall (%d) and the entry's wall (%d), %d lights by RackLights"), Far, Near, Room ? Room->GetLightCount() : 0));
			const UInstancedStaticMeshComponent* Lights = Room ? Room->GetLightComponent(0) : nullptr;
			Check(Lights && Lights->bHiddenInSceneCapture && Lights->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Lights->CastShadow,
				TEXT("room: the rack lights never in a scan's capture, colliding with nothing, casting nothing"));
			// The two places, in the middle of the side walls, facing each other across the column.
			const FVector Right = FVector::CrossProduct(FVector::UpVector, InDir);
			const FVector I = C->Intake ? C->Intake->GetActorLocation() - Centre : FVector::ZeroVector;
			const FVector H = C->HatchTerminal ? C->HatchTerminal->GetActorLocation() - Centre : FVector::ZeroVector;
			const FVector Hatch = C->OuterHatch ? C->OuterHatch->GetActorLocation() - Centre : FVector::ZeroVector;
			Check(C->Intake && C->Intake->Ending == TEXT("Inject") && FVector::DotProduct(I, Right) < -700.f && FMath::Abs(FVector::DotProduct(I, InDir)) < 100.f,
				FString::Printf(TEXT("room: the intake in the middle of the left wall (%s)"), *I.ToCompactString()));
			Check(C->HatchTerminal && C->HatchTerminal->Ending == TEXT("Open") && C->OuterHatch && FVector::DotProduct(Hatch, Right) > 800.f && FMath::Abs(FVector::DotProduct(Hatch, InDir)) < 60.f
				&& FVector::Dist2D(H, Hatch) < 400.f && !C->OuterHatch->IsFullyOpen() && C->OuterHatch->IsBlocking(),
				FString::Printf(TEXT("room: the outer hatch shut in the middle of the right wall (%s), its terminal beside it (%s)"), *Hatch.ToCompactString(), *H.ToCompactString()));
			const float DI = I.Size2D() - Radius;
			const float DH = Hatch.Size2D() - Radius;
			Check(DI > 550.f && DI < 850.f && DH > 550.f && DH < 850.f, FString::Printf(TEXT("room: each place about 7 m from the column (%.0f, %.0f cm)"), DI, DH));
			int32 LightCount = 0;
			for (ALight* Light : All<ALight>())
			{
				LightCount += Light ? 1 : 0;
			}
			Check(LightCount >= 1 && LightCount <= 4, FString::Printf(TEXT("room: %d lights (at most 4)"), LightCount));
			Check(Settings()->IsHomeLookMap(MapName()), TEXT("room: the map shows home's look"));
			Shot(TEXT("1_Room_FromDoorway"));
			return 0.5f;
		}));

		// --- Criteria 3 and 4: the lines and the wait ---
		S.Add(WaitFor(TEXT("lines: the places offer"), []() { return Offering(); }, 60.f, []()
		{
			// The key bound to Interact, read where there is a pawn.
			GRun.InteractKey = TimeStateDebug::BoundKey(W(), TEXT("InteractAction"));
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
			Check(GRun.EarlyOffers == 0 && End && End->TypedRun >= 0.0 && C->GetOfferedRunSeconds() >= End->TypedRun - 0.05,
				FString::Printf(TEXT("wait: no place offered before the last message was typed out (%d early frames; typed %.2f, offered %.2f)"), GRun.EarlyOffers, End ? End->TypedRun : -1.0,
					C->GetOfferedRunSeconds()));
			FacePlace(C->Intake, 100.f);
			return 0.5f;
		}));

		// --- Criterion 12: the shots; the prompts ---
		S.Add(Do(TEXT("intake: its prompt"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			if (!GRun.InteractKey.IsValid() && P)
			{
				GRun.InteractKey = P->GetShownKey();
			}
			Note(FString::Printf(TEXT("the Interact key: %s"), *GRun.InteractKey.ToString()));
			const FString Want = FString::Printf(TEXT("] %s"), *URunSceneSubsystem::GetEndingLabel(TEXT("Inject")).ToString());
			Check(P && P->GetMode() == EInteractPromptMode::Choice && P->GetShownText().EndsWith(Want) && P->GetShownKey() == GRun.InteractKey,
				FString::Printf(TEXT("intake: the prompt shows the Interact key and the list's label (\"%s\")"), P ? *P->GetShownText() : TEXT("-")));
			FacePlace(Chamber()->HatchTerminal, 100.f);
			return 0.5f;
		}));
		S.Add(Do(TEXT("hatch: its prompt"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			const FString Want = FString::Printf(TEXT("] %s"), *URunSceneSubsystem::GetEndingLabel(TEXT("Open")).ToString());
			Check(P && P->GetMode() == EInteractPromptMode::Choice && P->GetShownText().EndsWith(Want),
				FString::Printf(TEXT("hatch: with every twin AI part its terminal offers with the list's other label (\"%s\")"), P ? *P->GetShownText() : TEXT("-")));
			Shot(TEXT("5_Hatch_WithPrompt"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("shots: the column from 4 m"), []()
		{
			const FVector Centre = RoomCentre();
			const FVector In = (Centre - Chamber()->EntryDoor->GetActorLocation()).GetSafeNormal2D();
			Teleport(Centre - In * (ColumnRadius() + 400.f));
			LookAt(Centre + FVector(0.f, 0.f, 300.f));
			return 0.5f;
		}));
		S.Add(Do(TEXT("shots: the column"), []() { Shot(TEXT("2_Column_From4m")); return 0.4f; }));
		S.Add(Do(TEXT("shots: the intake from 3 m"), []() { FacePlace(Chamber()->Intake, 300.f); return 0.5f; }));
		S.Add(Do(TEXT("shots: the intake"), []() { Shot(TEXT("3_Intake_From3m")); return 0.4f; }));
		S.Add(Do(TEXT("shots: the hatch from 3 m"), []() { FacePlace(Chamber()->OuterHatch, 300.f); return 0.5f; }));
		S.Add(Do(TEXT("shots: the hatch"), []()
		{
			Shot(TEXT("4_Hatch_From3m"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("scan: back to the doorway, Stop"), []()
		{
			const AEpilogueChamber* C = Chamber();
			const FVector In = (RoomCentre() - C->EntryDoor->GetActorLocation()).GetSafeNormal2D();
			Teleport(C->EntryDoor->GetActorLocation() + In * 150.f);
			LookAt(RoomCentre() + FVector(0.f, 0.f, 200.f));
			Check(GRun.LightsOnMax > GRun.LightsOnMin, FString::Printf(TEXT("room: the rack lights blink (%d to %d on)"), GRun.LightsOnMin, GRun.LightsOnMax));
			Time()->Toggle();
			return 0.2f;
		}));
		S.Add(WaitFor(TEXT("scan: the first scan from the doorway"), []()
		{
			const UScanDirectorComponent* D = TimeStateDebug::FindPlayerDirector(W());
			return D && D->GetRecentScanOrigins().Num() > 0;
		}, 10.f, []()
		{
			const APrologueMonitorRoom* Room = First<APrologueMonitorRoom>();
			Check(Time()->IsStopped() && Room && !Room->AreLightsShown(), TEXT("scan: the rack lights hidden while Stopped"));
			const AEpilogueChamber* C = Chamber();
			Check(!C->Intake->OffersChoice() && !C->HatchTerminal->OffersChoice(), TEXT("scan: neither place offers while Stopped (Normal time only)"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("scan: shot"), []() { Shot(TEXT("7_Room_Scan_FromDoorway")); return 0.5f; }));

		// --- Criterion 8: left and resumed ---
		S.Add(Do(TEXT("quit: back to Normal time, the pause menu's Main menu"), []()
		{
			Time()->Toggle();
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
		S.Add(WaitFor(TEXT("again: the places offer"), []() { return Offering(); }, 90.f, []()
		{
			Check(GRun.EarlyOffers == 0 && LineOf(TEXT("Ending_1")) != nullptr, FString::Printf(TEXT("again: the lines said again, nothing offered before (%s)"), *LinesSaid()));
			FacePlace(Chamber()->Intake, 100.f);
			GRun.RingFrames = 0;
			GRun.HoldsBefore = Chamber()->Intake->GetHoldsCancelled();
			PressInteract();
			return 1.0f;
		}));

		// --- Criterion 5: the intake ---
		S.Add(Do(TEXT("intake: halfway through the hold"), []()
		{
			const AChoiceTerminal* I = Chamber()->Intake;
			const UResidueCollectCueWidget* C = Cue();
			const float Half = 1.0f / Settings()->GetChoiceHoldSeconds();
			Check(I->IsHolding() && FMath::Abs(I->GetHoldFraction() - Half) < 0.2f && C && C->IsRingShown() && FMath::Abs(C->GetRingProgress() - I->GetHoldFraction()) < 0.05f,
				FString::Printf(TEXT("intake: holding, %.2f of the hold after 1 s, the collection's ring at %.2f"), I->GetHoldFraction(), C ? C->GetRingProgress() : -1.f));
			Check(Prompt() && Prompt()->GetMode() == EInteractPromptMode::Hidden, TEXT("intake: the prompt gives way to the ring while held"));
			ReleaseInteract();
			return 0.5f;
		}));
		S.Add(Do(TEXT("intake: let go"), []()
		{
			const AChoiceTerminal* I = Chamber()->Intake;
			Check(!I->IsHolding() && !I->HasChosen() && I->GetHoldFraction() == 0.f && I->GetHoldsCancelled() == GRun.HoldsBefore + 1 && Cue() && !Cue()->IsRingShown()
				&& Chamber()->GetChamberState() == EEpilogueChamberState::Offering && !Transitioning() && I->OffersChoice(),
				TEXT("intake: letting go cancels the hold; nothing happens, the place offers again"));
			return 0.3f;
		}));
		AddHoldThrough(S, TEXT("intake"), []() { return Chamber() ? Chamber()->Intake.Get() : nullptr; });
		AddChooseAndBack(S, TEXT("intake"), TEXT("Inject"));

		// --- Criterion 6: the outer hatch ---
		S.Add(Do(TEXT("hatch: Continue"), []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("hatch: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("hatch: in the ship"), []() { return InReturn(); }, 60.f, []()
		{
			Check(Run()->TravelToChamber(), TEXT("hatch: to the chamber"));
			ResetLines();
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("hatch: the places offer"), []() { return Offering(); }, 90.f, []()
		{
			FacePlace(Chamber()->OuterHatch, 400.f);
			return 0.3f;
		}));
		AddHoldThrough(S, TEXT("hatch"), []() { return Chamber() ? Chamber()->HatchTerminal.Get() : nullptr; });
		S.Add(Do(TEXT("hatch: watch the panel"), []()
		{
			FacePlace(Chamber()->OuterHatch, 400.f);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("hatch: the panel has opened"), []() { return Chamber() && Chamber()->GetChamberState() == EEpilogueChamberState::Ended; }, 30.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			Check(GRun.HatchMidFraction > 0.2f && GRun.HatchMidFraction < 0.8f && C->OuterHatch->IsFullyOpen(),
				FString::Printf(TEXT("hatch: the panel slides open over its time (%.2f open halfway)"), GRun.HatchMidFraction));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("hatch: then the ending's travel"), []() { return Transitioning() || InMenu(); }, 10.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			if (C)
			{
				// The panel's own time, on the run's clock (one frame of it may hold a screenshot), and the ending right after.
				const double Opening = C->OuterHatch->GetOpenFraction() >= 1.f && GRun.HatchOpenRun >= 0.0 ? GRun.HatchOpenRun - C->GetChosenRunSeconds() : -1.0;
				const double After = C->GetEndingRunSeconds() - GRun.HatchOpenRun;
				Check(Opening >= Settings()->GetChamberHatchOpenSeconds() - 0.02 && Opening <= Settings()->GetChamberHatchOpenSeconds() + 0.25 && FMath::Abs(After) <= 0.1,
					FString::Printf(TEXT("hatch: the panel opened over %.2f s of the run's clock (ChamberHatchOpenSeconds %.2f), the ending asked when it opened (%.3f s from the frame it was seen open)"),
						Opening, Settings()->GetChamberHatchOpenSeconds(), After));
			}
			Check(GRun.bHatchShotTaken, TEXT("hatch: the shot of the hatch open on the dark"));
			return 0.f;
		}));
		AddChooseAndBack(S, TEXT("hatch"), TEXT("Open"));

		// --- Criterion 6: with one part less, nothing ---
		S.Add(Do(TEXT("one part less: Continue"), []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("one part less: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("one part less: in the ship"), []() { return InReturn(); }, 60.f, []()
		{
			Run()->ClearArchive();
			for (int32 Part = 1; Part < Settings()->GetOpenEndingTwinParts(); ++Part)
			{
				Run()->StoreTwinPart(Part);
			}
			Check(Run()->TravelToChamber(), FString::Printf(TEXT("one part less: to the chamber with %d twin parts"), Run()->GetArchive().TwinPartsPlayed));
			ResetLines();
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("one part less: the places offer"), []() { return Offering(); }, 90.f, []()
		{
			GRun.PromptFramesNearHatch = 0;
			FacePlace(Chamber()->HatchTerminal, 300.f);
			return 0.3f;
		}));
		for (const float Distance : { 300.f, 150.f, 60.f })
		{
			S.Add(Do(FString::Printf(TEXT("one part less: from %.0f cm"), Distance), [Distance]()
			{
				FacePlace(Chamber()->HatchTerminal, Distance);
				return 0.3f;
			}));
			S.Add(Do(FString::Printf(TEXT("one part less: the prompt from %.0f cm"), Distance), [Distance]()
			{
				const UInteractPromptWidget* P = Prompt();
				Check(P && P->GetMode() == EInteractPromptMode::Hidden && !Chamber()->HatchTerminal->OffersChoice(),
					FString::Printf(TEXT("one part less: no prompt from %.0f cm (%s)"), Distance, P ? *P->GetShownText() : TEXT("-")));
				if (Distance < 100.f)
				{
					Shot(TEXT("6_Hatch_WithoutPrompt"));
					GRun.TerminalEvents = 0;
					GRun.LinesBefore = GRun.Lines.Num();
					GRun.HoldsBefore = Chamber()->HatchTerminal->GetHoldsStarted();
					PressInteract();
				}
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("one part less: Interact held"), []() { return 1.0f; }));
		S.Add(Do(TEXT("one part less: nothing on Interact"), []()
		{
			ReleaseInteract();
			const AEpilogueChamber* C = Chamber();
			const UTextChannelComponent* Text = Channel();
			Check(C->HatchTerminal->GetHoldsStarted() == GRun.HoldsBefore && !C->HatchTerminal->HasChosen() && GRun.TerminalEvents == 0 && Cue() && !Cue()->IsRingShown()
				&& Text && GRun.Lines.Num() == GRun.LinesBefore && C->GetChamberState() == EEpilogueChamberState::Offering && Prompt()->GetMode() == EInteractPromptMode::Hidden,
				FString::Printf(TEXT("one part less: nothing on Interact: no hold, no sound, no line, no refusal (%d terminal events)"), GRun.TerminalEvents));
			return 0.2f;
		}));
		AddHoldThrough(S, TEXT("one part less, intake"), []() { return Chamber() ? Chamber()->Intake.Get() : nullptr; });
		AddChooseAndBack(S, TEXT("one part less"), TEXT("Inject"));

		// --- Criterion 4: the last row dropped at Silent ---
		S.Add(Do(TEXT("silent: to the chamber"), []()
		{
			Home()->RestoreState(GRun.SilentBattery, Home()->GetHomeSecondsElapsed());
			Run()->SetReturningHome(true, TEXT("tls.ChamberCheck"));
			ResetLines();
			Check(Run()->TravelToChamber(), TEXT("silent: to the chamber at a Silent battery"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("silent: the places offer"), []() { return Offering(); }, 90.f, []()
		{
			const FLine* E = LineOf(NarrativeFixture::GetEngineerLineName(TEXT("Chamber_1"), 1));
			Check(E && E->TypedRun >= 0.0 && !LineOf(TEXT("Chamber_1")) && !LineOf(TEXT("Ending_1")) && Chamber()->GetOfferedRunSeconds() >= E->TypedRun - 0.05 && GRun.EarlyOffers == 0,
				FString::Printf(TEXT("silent: the home AI's rows dropped, the engineer's line still said; the wait ends once it is typed out (%s)"), *LinesSaid()));
			NarrativeFixture::SetEpilogueGroups({ EpilogueGroups::Chamber });
			NarrativeFixture::SetEndingRows(false);
			Check(Flow()->ReturnToMainMenu(), TEXT("silent: back to the main menu"));
			return 0.5f;
		}));

		// --- Criterion 4: no row at all ---
		S.Add(WaitFor(TEXT("no row: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Run()->SetReturningHome(true, TEXT("tls.ChamberCheck"));
			ResetLines();
			Check(Run()->TravelToChamber(), TEXT("no row: to the chamber with no Chamber row and no last message"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("no row: the places offer"), []() { return Offering(); }, 60.f, []()
		{
			const AEpilogueChamber* C = Chamber();
			Check(GRun.Lines.IsEmpty() && C->GetLastRow().IsNone() && FMath::Abs(C->GetOfferedRunSeconds() - C->GetLinesStartRunSeconds()) < 0.05f,
				FString::Printf(TEXT("no row: the places offer at once (%.3f s, %s)"), C->GetOfferedRunSeconds() - C->GetLinesStartRunSeconds(), *LinesSaid()));
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
			// --- Criterion 11: the column's mesh ---
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
				FString::Printf(TEXT("mesh: SM_BatteryColumn, %d triangles (at most 8,000), %d slot, %s, collision %d boxes and %d convex, %lld bytes, bounds %s"), Tris,
					Mesh ? Mesh->GetStaticMaterials().Num() : 0, *MatPath, Boxes, Convex, Bytes, Mesh ? *Mesh->GetBounds().GetBox().ToString() : TEXT("-")));
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
		GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = true;
		GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = true;
		GetMutableDefault<UTheLastShiftSettings>()->bChoiceChamber = true;
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
		TEXT("The battery's chamber: the way in, the room, the lines and the wait, the two places, the endings, the switches, the column's mesh (fixture rows; shots to Saved/Screenshots/Brief31B). Run in MainMenu, PIE. Args: [Quit]. Clears the save slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTest));

	static FAutoConsoleCommandWithWorldAndArgs GRunChamberCmd(
		TEXT("tls.Run.Chamber"),
		TEXT("Puts the run on its way home (battery and twin AI parts given, else the run's own) and travels to the battery's chamber. Any map. Args: [Battery] [TwinParts]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunChamber));
}

#endif // !UE_BUILD_SHIPPING
