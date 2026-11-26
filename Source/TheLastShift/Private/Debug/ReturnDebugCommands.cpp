// SPDX-License-Identifier: MIT

// Brief 31A: the way home. tls.ReturnCheck [Quit] (run from MainMenu, in PIE; clears the save slot) checks, with its own
// fixture rows: an older save (Saved/Brief31A/Run_preBrief31A.sav) loading with its four acts and no return entry, the
// list of entries without the return before it is reached; the departure as before with the way home's door shut and its
// corridor's exit off; area 4's exit travelling home behind the travel screen of the flight back (its Return label, the
// marker from right to left); the return state (the pawn in the ship, the ship turned, its hatch and the hangar's open,
// the entry door shut, no departure, no terminal, no Wake or Hangar line, act 0); the return's snapshot in memory and on
// disk (version 5) with the run state of area 4; the step out (the hatch closing, the Landing lines after an engineer line,
// tokens written out, shortened by the battery, the input); the door after ExitDoorDelaySeconds, still behind the pause
// menu; the corridor (section, length, exit, light); the ending from its exit with the run's state, "Open the structure"
// with the twin AI's parts and without; the entries after it (the return's included), Select act's Return, the pause
// menu's Main menu and Continue, the save read again from disk and Continue; the last Landing row dropped at Silent and no
// Landing row; restarting from act 2 discarding the return; the way home switched off (area 4's exit opens the ending, a
// file's return entry not offered). Shots to Saved/Screenshots/Brief31A. tls.Run.Return [Battery] [TwinParts] puts the run
// at the end of area 4 and starts the flight back (the developer's check). Development builds only.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdMenuPlayerController.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueBriefingTerminal.h"
#include "Prologue/PrologueDeparture.h"
#include "Prologue/PrologueEntryDoor.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Prologue/PrologueReturn.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Residue/Residue.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSaveGame.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdMainMenuWidget.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace ReturnDebug
{
	using TimeStateDebug::ResolveGameWorld;

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static URunSceneSubsystem* Scenes() { return URunSceneSubsystem::Get(W()); }
	static UTimeStateSubsystem* Time() { return W() ? W()->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static int32 Stage() { return URunSubsystem::GetPrologueStageOf(W()); }
	static FString MapName() { return W() ? UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()) : FString(); }
	static const UTheLastShiftSettings* Settings() { return UTheLastShiftSettings::Get(); }

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
	static UHomeAIComponent* HomeAI() { return Pawn() ? Pawn()->FindComponentByClass<UHomeAIComponent>() : nullptr; }

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
		Out.Sort([](const T& A, const T& B) { return A.GetActorLocation().X < B.GetActorLocation().X; });
		return Out;
	}

	template <typename T>
	static T* First() { const TArray<T*> Found = All<T>(); return Found.Num() > 0 ? Found[0] : nullptr; }

	static APrologueReturn* Ret() { return First<APrologueReturn>(); }
	static bool InMenu() { return MainMenu() != nullptr && !Transitioning(); }
	static bool InArea(int32 Act) { return W() && URunSubsystem::GetActIndexOf(W()) == Act && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }
	static bool InStage(int32 S) { return W() && Stage() == S && Pawn() && !Transitioning(); }
	static bool InReturn() { return InStage(1) && Ret() && Ret()->IsReturn() && Ret()->HasPlacedPawn(); }

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

	/** The run state compared across the return: battery, home time, banked energy, the archive. */
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

		// Sampled every frame.
		TArray<FLine> Lines;
		FName LastRow;
		bool bMarkerShown = false;
		bool bMarkerRightToLeft = false;
		float MarkerFirst = -1.f;
		float MarkerLast = -1.f;

		// Scratch.
		FState AtExit;
		double Battery = 0.5;
		double SilentBattery = 0.01;
		ETextDegradation Level = ETextDegradation::Full;
		FText ExpectedLabel;
		bool bCorridorTriggered = false;
		float DelayMark = 0.f;
		TArray<uint8> SaveWithReturn;
		FString HomeCorridor;
		float HomeWidth = 0.f;
		float HomeHeight = 0.f;
	};
	static FRun GRun;

	static const TCHAR* Name() { return TEXT("tls.ReturnCheck"); }

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
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief31A"), ShotName + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot %s"), *Path));
	}

	static void Finish(const TCHAR* Why)
	{
		UTheLastShiftSettings* Mutable = GetMutableDefault<UTheLastShiftSettings>();
		Mutable->bReturnHome = GRun.bSavedReturn;
		Mutable->bPrologueMaps = GRun.bSavedMaps;
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
		const double RunNow = Time() ? Time()->GetRunSeconds() : -1.0;
		if (Row != GRun.LastRow)
		{
			GRun.LastRow = Row;
			if (!Row.IsNone())
			{
				GRun.Lines.Add({ Row, RunNow, -1.0, Text->GetCurrentText() });
				Note(FString::Printf(TEXT("line %s started in %s: \"%s\""), *Row.ToString(), *MapName(), *Text->GetCurrentText()));
			}
		}
		if (Text && !Row.IsNone() && GRun.Lines.Num() > 0 && GRun.Lines.Last().Row == Row && GRun.Lines.Last().TypedRun < 0.0 && Text->GetShownCells() > 0
			&& Text->GetShownCells() >= Text->GetCurrentText().Len())
		{
			GRun.Lines.Last().TypedRun = RunNow;
			GRun.Lines.Last().Text = Text->GetCurrentText();
		}
		// The travel screen's marker: where it stood when first drawn, and last.
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
		if (const APrologueReturn* R = Ret(); R && R->CorridorExit && R->CorridorExit->HasTriggered() && !R->IsReturn())
		{
			GRun.bCorridorTriggered = true;
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

	/** Puts the pawn at Where on the floor (its Z kept unless bWithZ), stopped. */
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
		FCollisionQueryParams Params(TEXT("ReturnCheck"), false, Pawn());
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

	/** The text the channel shows for the fixture's epilogue row at the battery's level now, with the run's numbers. */
	static FString Expected(FName Group, int32 Order, ETextDegradation Level)
	{
		FAIMessageRow Row;
		Row.Text = FText::FromString(NarrativeFixture::GetEpilogueLineText(Group, Order));
		FTextChannelContext Context;
		Context.BatteryFraction = Home()->GetBatteryFraction();
		Context.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
		return UTextChannelComponent::ComposeMessage(Row, Level, Context);
	}

	/** The direction from the hangar into the way home's corridor (the dominant axis from the hangar's centre to the door). */
	static FVector CorridorDir()
	{
		const APrologueDeparture* Dep = First<APrologueDeparture>();
		const APrologueReturn* R = Ret();
		if (!Dep || !R || !R->HomeDoor)
		{
			return FVector::YAxisVector;
		}
		const FVector D = R->HomeDoor->GetActorLocation() - Dep->GetHangarBox().GetCenter();
		return FMath::Abs(D.X) > FMath::Abs(D.Y) ? FVector(FMath::Sign(D.X), 0.f, 0.f) : FVector(0.f, FMath::Sign(D.Y), 0.f);
	}

	static FString ListLabels() { return TimeStateDebug::DescribeChoiceList(W()); }

	static int32 ListIndexOf(int32 Act)
	{
		const URunActListWidget* List = TopList();
		for (int32 I = 0; List && I < List->GetEntryCount(); ++I)
		{
			if (List->GetActOfEntry(I) == Act)
			{
				return I;
			}
		}
		return INDEX_NONE;
	}

	static const URunSaveGame* ReadSlot()
	{
		return Cast<URunSaveGame>(UGameplayStatics::LoadGameFromSlot(URunSubsystem::SaveSlotName(), 0));
	}

	static FString SlotPath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav")); }

	// --- Pieces of the test ---

	/** The checks made on arriving in the hangar on the return. */
	static void CheckReturnState(const FString& L)
	{
		const APrologueReturn* R = Ret();
		const APawn* P = Pawn();
		Check(R && R->GetReturnState() == EPrologueReturnState::InShip && URunSubsystem::IsReturnStateOf(W()) && Run()->IsReturningHome(),
			FString::Printf(TEXT("%s: Prologue_Home in its return state, the player not yet out of the ship (%s)"), *L, *MapName()));
		Check(R && P && R->IsInsideShip(P), FString::Printf(TEXT("%s: the pawn starts inside the ship (%s, inside %s)"), *L, P ? *P->GetActorLocation().ToCompactString() : TEXT("-"),
			R ? *R->GetShipInsideBox().ToString() : TEXT("-")));
		const AActor* Ship = R ? R->Ship.Get() : nullptr;
		Check(Ship && FVector::Dist2D(Ship->GetActorLocation(), R->ReturnShipLocation) < 1.f && FMath::IsNearlyEqual(FRotator::NormalizeAxis(Ship->GetActorRotation().Yaw), R->ReturnShipYaw, 0.5f),
			FString::Printf(TEXT("%s: the ship in its return pose (%s yaw %.1f)"), *L, Ship ? *Ship->GetActorLocation().ToCompactString() : TEXT("-"), Ship ? Ship->GetActorRotation().Yaw : 0.f));
		const APrologueSlidingPanel* ShipHatch = R ? R->ShipHatch.Get() : nullptr;
		const APrologueSlidingPanel* Hangar = R ? R->HangarHatch.Get() : nullptr;
		Check(ShipHatch && ShipHatch->IsFullyOpen() && !ShipHatch->IsBlocking() && Hangar && Hangar->IsFullyOpen(),
			FString::Printf(TEXT("%s: the ship's hatch panel open and passable, the hangar's hatch open (%.2f, %.2f)"), *L, ShipHatch ? ShipHatch->GetOpenFraction() : -1.f,
				Hangar ? Hangar->GetOpenFraction() : -1.f));
		// The opening faces away from the hangar's hatch, toward the way home's door.
		if (ShipHatch && Hangar && R->HomeDoor && Ship)
		{
			const FVector Out = (ShipHatch->GetOpeningBox().GetCenter() - Ship->GetActorLocation()).GetSafeNormal2D();
			const FVector ToDoor = (R->HomeDoor->GetActorLocation() - Ship->GetActorLocation()).GetSafeNormal2D();
			const FVector ToHatch = (Hangar->GetActorLocation() - Ship->GetActorLocation()).GetSafeNormal2D();
			Check(FVector::DotProduct(Out, ToDoor) > 0.7f && FVector::DotProduct(Out, ToHatch) < -0.7f,
				FString::Printf(TEXT("%s: the ship's opening faces the door and turns its back on the hatch (to the door %.2f, to the hatch %.2f)"), *L,
					FVector::DotProduct(Out, ToDoor), FVector::DotProduct(Out, ToHatch)));
		}
		const APrologueEntryDoor* Entry = First<APrologueEntryDoor>();
		const APrologueBriefingTerminal* Terminal = First<APrologueBriefingTerminal>();
		const APrologueDeparture* Dep = First<APrologueDeparture>();
		Check(Entry && !Entry->IsOpen() && Terminal && Terminal->IsInertOnReturn() && !Terminal->OffersPayment() && !Terminal->AcceptsPayment() && !Terminal->OffersRead(),
			FString::Printf(TEXT("%s: the entry door shut, the hangar's terminal offers nothing"), *L));
		Check(Dep && Dep->GetDepartureState() == EPrologueDepartureState::Waiting && !Dep->IsActorTickEnabled(), FString::Printf(TEXT("%s: no departure scene"), *L));
		const APrologueLineTrigger* HangarTrigger = nullptr;
		for (const APrologueLineTrigger* T : All<APrologueLineTrigger>())
		{
			HangarTrigger = T->Group == PrologueGroups::Hangar ? T : HangarTrigger;
		}
		const UHomeAIComponent* AI = HomeAI();
		Check(AI && !AI->HasSaidPrologueGroup(PrologueGroups::Wake) && !AI->HasSaidPrologueGroup(PrologueGroups::Hangar) && HangarTrigger && !HangarTrigger->HasFired(),
			FString::Printf(TEXT("%s: no Wake and no Hangar line, the hangar's trigger silent"), *L));
		Check(R && R->ShipExit && !R->ShipExit->IsExitEnabled() && R->CorridorExit && R->CorridorExit->IsExitEnabled() && R->HomeDoor && !R->HomeDoor->IsOpen(),
			FString::Printf(TEXT("%s: the ship's exit off, the corridor's exit on, the way home's door shut"), *L));
		Check(Run()->GetActIndex() == 0 && All<AEntity>().IsEmpty() && All<AResidue>().IsEmpty() && !AAreaGenerator::Find(W()),
			FString::Printf(TEXT("%s: no act (%d), no entity, no residue, no area: the return is no act"), *L, Run()->GetActIndex()));
	}

	/** The step out of the ship on the return: teleported past its opening. */
	static void StepOut()
	{
		const APrologueReturn* R = Ret();
		if (!R || !R->ShipHatch || !R->Ship)
		{
			return;
		}
		const FVector Opening = R->ShipHatch->GetOpeningBox().GetCenter();
		const FVector Out = (Opening - R->Ship->GetActorLocation()).GetSafeNormal2D();
		Teleport(Opening + Out * 300.f);
		LookAt(R->HomeDoor ? R->HomeDoor->GetActorLocation() + FVector(0.f, 0.f, 150.f) : Opening + Out * 1000.f);
		GRun.Lines.Reset();
		GRun.LastRow = NAME_None;
	}

	/** Arrive in the return, step out, the door, the corridor's exit: the ending opens with Open offered or not. */
	static void AddPlayToEnding(TArray<FStep>& S, const FString& L, bool bExpectOpen)
	{
		S.Add(Do(L + TEXT(": step out"), []() { StepOut(); return 0.3f; }));
		S.Add(WaitFor(L + TEXT(": the door opens"), []() { return Ret() && Ret()->GetReturnState() == EPrologueReturnState::DoorOpened && Ret()->HomeDoor->IsOpen(); }, 60.f, []()
		{
			Teleport(Ret()->CorridorExit->GetActorLocation());
			return 0.2f;
		}));
		S.Add(WaitFor(L + TEXT(": the corridor's exit opens the ending over the main menu"), []() { return InMenu() && TopEnding() != nullptr; }, 60.f, [L, bExpectOpen]()
		{
			URunEndingWidget* Ending = TopEnding();
			int32 OpenIndex = INDEX_NONE;
			int32 InjectIndex = INDEX_NONE;
			for (int32 I = 0; Ending && I < Ending->GetEntryCount(); ++I)
			{
				OpenIndex = Ending->GetEndingOfEntry(I) == TEXT("Open") ? I : OpenIndex;
				InjectIndex = Ending->GetEndingOfEntry(I) == TEXT("Inject") ? I : InjectIndex;
			}
			const ETextDegradation Want = Settings()->GetTextLevel(Home()->GetBatteryFraction());
			Check(Ending && Ending->GetMessageLevel() == Want, FString::Printf(TEXT("%s: the last message at the run's battery (%.4f): %s, want %s"), *L, Home()->GetBatteryFraction(),
				Ending ? LexToString(Ending->GetMessageLevel()) : TEXT("-"), LexToString(Want)));
			Check((OpenIndex != INDEX_NONE) == bExpectOpen && InjectIndex != INDEX_NONE,
				FString::Printf(TEXT("%s: \"Open the structure\" %s with %d twin parts (%s)"), *L, bExpectOpen ? TEXT("offered") : TEXT("not offered"), Run()->GetArchive().TwinPartsPlayed,
					*TimeStateDebug::DescribeChoiceList(W())));
			Check(TimeStateDebug::ClickChoiceEntry(W(), bExpectOpen ? OpenIndex : InjectIndex), FString::Printf(TEXT("%s: %s chosen"), *L, bExpectOpen ? TEXT("Open") : TEXT("Inject")));
			URunTextSceneWidget* Text = TopScene();
			const FName Row = Text && Text->GetLines().Num() > 0 ? Text->GetLines()[0].RowName : NAME_None;
			Check(Text && Row == (bExpectOpen ? TEXT("Open") : TEXT("Inject")), FString::Printf(TEXT("%s: the chosen ending's text (%s)"), *L, *Row.ToString()));
			if (Text)
			{
				Text->Confirm();
				Text->Confirm();
			}
			return 0.5f;
		}));
		S.Add(WaitFor(L + TEXT(": back in the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f, [L]()
		{
			Check(!Run()->IsReturningHome() && !URunSceneSubsystem::Get(W())->IsEndingPending(), FString::Printf(TEXT("%s: after the ending the run is no longer on its way home"), *L));
			return 0.3f;
		}));
	}

	static TArray<FStep> BuildTest()
	{
		TArray<FStep> S;
		const float Delay = Settings()->GetExitDoorDelaySeconds();

		// --- Setup, the older file, the list before the return (criteria 9, 10, 12) ---
		S.Add(WaitFor(TEXT("setup: the main menu, no transition"), []() { return InMenu(); }, 30.f, []()
		{
			// The real table, before the fixture: DT_Epilogue with its four groups and no other.
			const UDataTable* Real = Settings()->LoadEpilogueTable();
			TSet<FName> Groups;
			bool bOnlyFour = true;
			if (Real && Real->GetRowStruct() == FPrologueRow::StaticStruct())
			{
				for (const TPair<FName, uint8*>& Pair : Real->GetRowMap())
				{
					const FName Group = reinterpret_cast<const FPrologueRow*>(Pair.Value)->Group;
					Groups.Add(Group);
					bOnlyFour &= EpilogueGroups::IsEpilogueGroup(Group);
				}
			}
			Check(Real && Real->GetRowStruct() == FPrologueRow::StaticStruct() && bOnlyFour && Groups.Contains(EpilogueGroups::Return) && Groups.Contains(EpilogueGroups::Landing)
				&& Groups.Contains(EpilogueGroups::Passage) && Groups.Contains(EpilogueGroups::Chamber),
				FString::Printf(TEXT("setup: %s loaded with the prologue's row struct and the groups Return, Landing, Passage, Chamber (%d groups)"), *GetNameSafe(Real), Groups.Num()));
			NarrativeFixture::Install(W());
			NarrativeFixture::SetEngineerLines(W(), { { TEXT("Landing_1"), 1 } });
			// Battery levels from the settings' thresholds: an Abbreviated battery for the run, a Silent one above zero.
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
			Note(FString::Printf(TEXT("battery for the run %.3f (%s), Silent battery %.3f (%s)"), GRun.Battery, LexToString(GRun.Level), GRun.SilentBattery,
				LexToString(Settings()->GetTextLevel(GRun.SilentBattery))));
			// Criterion 10: the file written before this brief.
			const FString Old = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief31A"), TEXT("Run_preBrief31A.sav"));
			const bool bCopied = IFileManager::Get().Copy(*SlotPath(), *Old) == COPY_OK;
			Run()->LoadSave();
			const TArray<FAreaSnapshot>& Snaps = Run()->GetSnapshots();
			Check(bCopied && Run()->GetLoadedSaveVersion() == 4 && Snaps.Num() == 4 && Snaps[0].ActIndex == 1 && Snaps[3].ActIndex == 4 && !Run()->HasReturnEntry(),
				FString::Printf(TEXT("older file: Run_preBrief31A.sav (version %d) loads with its %d acts and no return entry"), Run()->GetLoadedSaveVersion(), Snaps.Num()));
			Check(GetDefault<URunSaveGame>()->Version == 1 && URunSaveGame::CurrentVersion == 5,
				FString::Printf(TEXT("the class default of URunSaveGame::Version is %d, the version written %d"), GetDefault<URunSaveGame>()->Version, URunSaveGame::CurrentVersion));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("list: Select act"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("list: before the return"), []()
		{
			const FString Labels = ListLabels();
			Check(TopList() && TopList()->GetActOfEntry(3) == 4 && ListIndexOf(URunActListWidget::ReturnEntry) == INDEX_NONE && !Labels.Contains(TEXT("Return")),
				FString::Printf(TEXT("list: the four acts, no return before it has been reached (%s)"), *Labels));
			TimeStateDebug::ClickChoiceEntry(W(), INDEX_NONE);
			// Criterion 1: the departure, from New game.
			Check(Run()->StartNewGame(), TEXT("departure: New game (after the text scene)"));
			return 0.5f;
		}));

		// --- The departure (criterion 1) ---
		S.Add(WaitFor(TEXT("departure: Prologue_Home"), []() { return InStage(1) && Ret() != nullptr; }, 60.f, []()
		{
			const APrologueReturn* R = Ret();
			Check(R->GetReturnState() == EPrologueReturnState::Departure && !Run()->IsReturningHome(), TEXT("departure: the map in its departure state"));
			Check(R->HomeDoor && !R->HomeDoor->IsOpen() && !R->HomeDoor->IsPowered() && R->CorridorExit && !R->CorridorExit->IsExitEnabled(),
				TEXT("departure: the way home's door shut and unpowered, the corridor's exit off"));
			bool bTargeted = false;
			for (const APowerTerminal* T : All<APowerTerminal>())
			{
				bTargeted |= T->Targets.Contains(R->HomeDoor.Get());
			}
			Check(!bTargeted, TEXT("departure: no terminal powers the way home's door: no prompt"));
			const AActor* Ship = R->Ship.Get();
			Check(Ship && FMath::IsNearlyEqual(FRotator::NormalizeAxis(Ship->GetActorRotation().Yaw), 0.f, 0.5f) && FVector::Dist2D(Ship->GetActorLocation(), FVector(4330.f, 0.f, 0.f)) < 1.f,
				FString::Printf(TEXT("departure: the ship where it always stood (%s)"), Ship ? *Ship->GetActorLocation().ToCompactString() : TEXT("-")));
			Check(HomeAI() && HomeAI()->HasSaidPrologueGroup(PrologueGroups::Wake), TEXT("departure: the Wake lines asked for, as before"));
			// From the entry door: inside the hangar, the departure starts (its entry door was never opened).
			const APrologueEntryDoor* Entry = First<APrologueEntryDoor>();
			Teleport(Entry->GetActorLocation() + FVector(150.f, 0.f, 0.f));
			LookAt(R->HomeDoor->GetActorLocation() + FVector(0.f, 0.f, 150.f));
			return 1.0f;
		}));
		S.Add(Do(TEXT("departure: shot"), []() { Shot(TEXT("1_Departure_ShutDoor_FromEntry")); return 0.5f; }));
		S.Add(WaitFor(TEXT("departure: the ship boardable"), []() { const APrologueDeparture* D = First<APrologueDeparture>(); return D && D->IsBoardable(); }, 30.f, []()
		{
			const APrologueReturn* R = Ret();
			Check(!R->HomeDoor->IsOpen() && !R->CorridorExit->HasTriggered() && !GRun.bCorridorTriggered, TEXT("departure: the way home's door stayed shut through the departure scene, its exit never fired"));
			Teleport(R->ShipExit->GetActorLocation());
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("departure: the flight to Prologue_Arrival"), []() { return InStage(2); }, 60.f, []()
		{
			Check(!GRun.bCorridorTriggered && !Run()->IsReturningHome(), TEXT("departure: the corridor's exit never fired; the run not on its way home"));
			Check(Run()->RestartFromAct(4), TEXT("area 4: restart from act 4"));
			return 0.5f;
		}));

		// --- Area 4, the flight back (criteria 2, 7) ---
		S.Add(WaitFor(TEXT("area 4: arrived"), []() { return InArea(4); }, 180.f, []()
		{
			Home()->RestoreState(GRun.Battery, Home()->GetHomeSecondsElapsed() + 1234.0);
			Energy()->RestoreBankedEnergy(3);
			Run()->ClearArchive();
			Run()->StoreLogEntries({ 1, 2 });
			for (int32 Part = 1; Part <= 3; ++Part)
			{
				Run()->StoreTwinPart(Part);
			}
			GRun.AtExit = FState::Now();
			Note(FString::Printf(TEXT("area 4 left with %s"), *GRun.AtExit.ToString()));
			Check(!Run()->HasReturnEntry() && !Run()->IsReturningHome(), TEXT("area 4: no return entry yet, not on the way home"));
			UHomeAIComponent::BuildReturnTravelLabel(W(), GRun.ExpectedLabel, GRun.DelayMark);
			const AAreaExit* Exit = First<AAreaExit>();
			Teleport(Exit->GetActorLocation(), true);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("flight back: the travel starts from area 4's exit"), []() { return Transitioning(); }, 10.f, []()
		{
			Check(Run()->IsReturningHome() && !URunSceneSubsystem::Get(W())->IsEndingPending(), TEXT("flight back: on the way home, no ending"));
			Check(!GRun.ExpectedLabel.IsEmpty() && Run()->GetLastTravelLabel().EqualTo(GRun.ExpectedLabel),
				FString::Printf(TEXT("flight back: the label is the Return row (\"%s\")"), *Run()->GetLastTravelLabel().ToString()));
			Check(Flow()->IsTransitionMarkerRightToLeft(), TEXT("flight back: the marker crosses from right to left"));
			return 1.5f;
		}));
		S.Add(Do(TEXT("flight back: shot"), []() { Shot(TEXT("2_FlightBack_TravelScreen")); return 0.f; }));
		S.Add(WaitFor(TEXT("return: arrived in the hangar"), []() { return InReturn(); }, 120.f, []()
		{
			Check(GRun.bMarkerRightToLeft && GRun.MarkerFirst > GRun.MarkerLast + 0.2f && GRun.MarkerFirst >= 0.9f,
				FString::Printf(TEXT("flight back: the marker crossed from %.2f to %.2f (right to left)"), GRun.MarkerFirst, GRun.MarkerLast));
			CheckReturnState(TEXT("return"));
			const FState Now = FState::Now();
			Check(Now.Same(GRun.AtExit), FString::Printf(TEXT("return: the run state as it left area 4 (%s)"), *Now.ToString()));
			Check(Run()->HasReturnEntry() && FState::Of(Run()->GetReturnSnapshot()).Same(GRun.AtExit) && Run()->GetSnapshots().Num() == 4,
				FString::Printf(TEXT("return: the return's snapshot taken with that state (%s), the four acts kept"), *FState::Of(Run()->GetReturnSnapshot()).ToString()));
			const URunSaveGame* Disk = ReadSlot();
			Check(Disk && Disk->Version == 5 && Disk->bHasReturn && FState::Of(Disk->Return).Same(GRun.AtExit) && Disk->Return.ActIndex == 0 && Disk->Snapshots.Num() == 4,
				FString::Printf(TEXT("return: on disk, version %d, return entry %d, %d acts"), Disk ? Disk->Version : -1, Disk && Disk->bHasReturn ? 1 : 0, Disk ? Disk->Snapshots.Num() : -1));
			FFileHelper::LoadFileToArray(GRun.SaveWithReturn, *SlotPath());
			// The view from the ship's opening.
			const FVector Opening = Ret()->ShipHatch->GetOpeningBox().GetCenter();
			Teleport(Opening);
			LookAt(Ret()->HomeDoor->GetActorLocation() + FVector(0.f, 0.f, 150.f));
			return 0.6f;
		}));
		S.Add(Do(TEXT("return: shot"), []()
		{
			Check(Ret()->GetReturnState() == EPrologueReturnState::InShip, TEXT("return: in the ship's opening, still inside"));
			Shot(TEXT("3_Return_FromShipOpening"));
			return 0.5f;
		}));

		// --- The step out, the lines, the door (criteria 3, 4, 5, 13) ---
		S.Add(Do(TEXT("return: step out"), []() { StepOut(); return 1.0f; }));
		S.Add(Do(TEXT("return: after the step out"), []()
		{
			const APrologueReturn* R = Ret();
			Check(R->GetReturnState() >= EPrologueReturnState::Landing && R->GetHatchClosedFraction() > 0.f && R->HangarHatch->GetOpenFraction() < 1.f,
				FString::Printf(TEXT("return: the hangar's hatch closing from the step out (%.2f closed)"), R->GetHatchClosedFraction()));
			const TimeStateDebug::FInputReport Input = TimeStateDebug::BuildInputReport(W());
			TimeStateDebug::LogInputReport(W(), TEXT("tls.ReturnCheck input (from area 4)"));
			Check(Input.IsGameReady(), TEXT("input: game input after the step out on the return reached from area 4"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("return: the door's delay starts"), []() { return Ret()->GetReturnState() == EPrologueReturnState::DoorDelay; }, 60.f, []()
		{
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			GRun.DelayMark = Ret()->GetDoorDelaySeconds();
			Check(StdPC && StdPC->PauseGame(), TEXT("return: the pause menu opens during the delay"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("return: the delay behind the pause menu"), []()
		{
			Check(FMath::IsNearlyEqual(Ret()->GetDoorDelaySeconds(), GRun.DelayMark, 0.02f) && Ret()->GetReturnState() == EPrologueReturnState::DoorDelay && !Ret()->HomeDoor->IsOpen(),
				FString::Printf(TEXT("return: the delay stood still behind the pause menu (%.3f, %.3f)"), GRun.DelayMark, Ret()->GetDoorDelaySeconds()));
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			Check(StdPC && StdPC->ResumeGame(), TEXT("return: the pause menu closes"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("return: the door opens"), []() { return Ret()->GetReturnState() == EPrologueReturnState::DoorOpened; }, 30.f, [Delay]()
		{
			const APrologueReturn* R = Ret();
			const FLine* E = LineOf(NarrativeFixture::GetEngineerLineName(TEXT("Landing_1"), 1));
			const FLine* L1 = LineOf(TEXT("Landing_1"));
			const FLine* L2 = LineOf(TEXT("Landing_2"));
			Check(E && L1 && L2 && E->StartRun <= L1->StartRun && L1->StartRun <= L2->StartRun && E->StartRun >= R->GetSteppedOutRunSeconds() - 0.05,
				FString::Printf(TEXT("lines: from the step out, the engineer's line first, then Landing_1 and Landing_2 (%s)"), *LinesSaid()));
			Check(E && E->Text == NarrativeFixture::GetEngineerLineText(TEXT("Landing_1"), 1), TEXT("lines: the engineer's line in full, never shortened"));
			const FString Want1 = Expected(EpilogueGroups::Landing, 1, GRun.Level);
			const FString Want2 = Expected(EpilogueGroups::Landing, 2, GRun.Level);
			const FString Full1 = Expected(EpilogueGroups::Landing, 1, ETextDegradation::Full);
			Check(L1 && L2 && L1->Text == Want1 && L2->Text == Want2 && Want1 != Full1,
				FString::Printf(TEXT("lines: the Landing rows shortened by the run's battery (%s): \"%s\""), LexToString(GRun.Level), L1 ? *L1->Text : TEXT("-")));
			const FString Battery = UTextTokensLibrary::FormatBattery(Home()->GetBatteryFraction(), GRun.Level);
			const FString HomeTime = UTextTokensLibrary::FormatDuration(Home()->GetHomeSecondsElapsed(), GRun.Level);
			Check(L1 && !L1->Text.Contains(TEXT("{")) && L1->Text.Contains(Battery) && L1->Text.Contains(HomeTime),
				FString::Printf(TEXT("lines: {home} and {battery} written out with the run's values (%s, %s)"), *HomeTime, *Battery));
			Check(L2 && L2->TypedRun >= 0.0 && R->GetLandingDoneRunSeconds() >= L2->TypedRun - 0.05,
				FString::Printf(TEXT("door: the wait ends once the last Landing row is typed out (%.3f, typed %.3f)"), R->GetLandingDoneRunSeconds(), L2 ? L2->TypedRun : -1.0));
			const float Took = R->GetDoorOpenedRunSeconds() - R->GetLandingDoneRunSeconds();
			Check(FMath::Abs(Took - Delay) <= 0.1f && R->HomeDoor->IsOpen(), FString::Printf(TEXT("door: open %.2f s of the run's clock after the Landing lines (ExitDoorDelaySeconds %.2f)"), Took, Delay));
			Check(R->HangarHatch->GetOpenFraction() <= 0.f, FString::Printf(TEXT("return: the hangar's hatch has closed (%.2f open)"), R->HangarHatch->GetOpenFraction()));
			// The door open with the corridor behind it, 6 m before it.
			const FVector Dir = CorridorDir();
			Teleport(R->HomeDoor->GetActorLocation() - Dir * 600.f);
			LookAt(R->HomeDoor->GetActorLocation() + Dir * 1000.f + FVector(0.f, 0.f, 150.f));
			return 0.6f;
		}));
		S.Add(Do(TEXT("door: shot"), []() { Shot(TEXT("4_Return_DoorOpen_Corridor")); return 0.5f; }));
		S.Add(Do(TEXT("corridor: to its mouth"), []()
		{
			const FVector Dir = CorridorDir();
			Teleport(Ret()->HomeDoor->GetActorLocation() + Dir * 150.f);
			LookAt(Ret()->HomeDoor->GetActorLocation() + Dir * 3000.f + FVector(0.f, 0.f, 150.f));
			return 0.6f;
		}));

		// --- The corridor (criterion 6) ---
		S.Add(Do(TEXT("corridor: shot"), []() { Shot(TEXT("5_Corridor_FromMouth")); return 0.5f; }));
		S.Add(Do(TEXT("corridor: measures"), []()
		{
			const APrologueReturn* R = Ret();
			const FVector Dir = CorridorDir();
			const FVector Side(-Dir.Y, Dir.X, 0.f);
			const FVector Door = R->HomeDoor->GetActorLocation();
			const FVector Mouth = Door + Dir * 10.f;
			const FVector In = Door + Dir * 450.f + FVector(0.f, 0.f, 150.f);
			const float Width = Trace(In, In + Side * 500.f) + Trace(In, In - Side * 500.f);
			const float Height = 150.f + Trace(In, In + FVector(0.f, 0.f, 1000.f));
			// The home's own corridor, measured the same way half a panel off its joints.
			const FVector HomeIn(1360.f, 0.f, 150.f);
			const float HomeWidth = Trace(HomeIn, HomeIn + FVector(0.f, 500.f, 0.f)) + Trace(HomeIn, HomeIn - FVector(0.f, 500.f, 0.f));
			const float HomeHeight = 150.f + Trace(HomeIn, HomeIn + FVector(0.f, 0.f, 1000.f));
			Check(Width >= 240.f && FMath::Abs(Width - HomeWidth) <= 5.f && FMath::Abs(Height - HomeHeight) <= 5.f,
				FString::Printf(TEXT("corridor: the section of home's corridor: %.0f x %.0f cm (home's %.0f x %.0f)"), Width, Height, HomeWidth, HomeHeight));
			const FVector Start = Door + Dir * 40.f + FVector(0.f, 0.f, 150.f);
			const float ToEnd = Trace(Start, Start + Dir * 6000.f);
			const float Length = ToEnd >= 0.f ? ToEnd + 30.f : -1.f;
			Check(FMath::Abs(Length - 3000.f) <= 25.f, FString::Printf(TEXT("corridor: 30 m long, closed at its far end (%.0f cm from the mouth to its end door)"), Length));
			const AAreaExit* Exit = R->CorridorExit;
			const float ExitAt = FVector::DotProduct(Exit->GetActorLocation() - Mouth, Dir);
			const FVector Extent = Exit->GetVolume()->GetUnscaledBoxExtent();
			const float Rest = Length - ExitAt - Extent.X;
			Check(FMath::Abs(ExitAt - 800.f) <= 15.f && FVector::DotProduct(Exit->GetActorForwardVector(), Dir) > 0.99f && Extent.Y * 2.f >= Width && FMath::Abs(Exit->RunPastExit - Rest) <= 30.f,
				FString::Printf(TEXT("corridor: its exit %.0f cm in, across the section (%.0f cm), RunPastExit %.0f for the %.0f cm left"), ExitAt, Extent.Y * 2.f, Exit->RunPastExit, Rest));
			int32 Lights = 0;
			float LightAt = -1.f;
			for (const ALightGroupDevice* Group : All<ALightGroupDevice>())
			{
				for (const TObjectPtr<ALight>& Light : Group->Lights)
				{
					const float Along = Light ? FVector::DotProduct(Light->GetActorLocation() - Mouth, Dir) : -1.f;
					if (Light && Along > 0.f && Along < Length && FMath::Abs(FVector::DotProduct(Light->GetActorLocation() - Mouth, Side)) < 200.f)
					{
						++Lights;
						LightAt = Along;
					}
				}
			}
			Check(Lights == 1 && LightAt <= 300.f, FString::Printf(TEXT("corridor: lit at its mouth only (%d lights, at %.0f cm)"), Lights, LightAt));
			Teleport(R->CorridorExit->GetActorLocation());
			return 0.2f;
		}));
		S.Add(WaitFor(TEXT("ending: the corridor's exit opens the ending"), []() { return InMenu() && TopEnding() != nullptr; }, 60.f, []()
		{
			const FState Now = FState::Now();
			Check(Now.Same(GRun.AtExit), FString::Printf(TEXT("ending: the run state as it left area 4 (%s)"), *Now.ToString()));
			URunEndingWidget* Ending = TopEnding();
			int32 OpenIndex = INDEX_NONE;
			for (int32 I = 0; I < Ending->GetEntryCount(); ++I)
			{
				OpenIndex = Ending->GetEndingOfEntry(I) == TEXT("Open") ? I : OpenIndex;
			}
			const ETextDegradation Want = Settings()->GetTextLevel(GRun.AtExit.Battery);
			Check(Ending->GetMessageLevel() == Want, FString::Printf(TEXT("ending: the last message at the run's battery: %s"), LexToString(Ending->GetMessageLevel())));
			Check(OpenIndex != INDEX_NONE, FString::Printf(TEXT("ending: \"Open the structure\" offered with every twin part (%s)"), *TimeStateDebug::DescribeChoiceList(W())));
			Check(TimeStateDebug::ClickChoiceEntry(W(), OpenIndex), TEXT("ending: Open chosen"));
			URunTextSceneWidget* Text = TopScene();
			Check(Text && Text->GetLines().Num() > 0 && Text->GetLines()[0].RowName == TEXT("Open"), TEXT("ending: the chosen ending's text"));
			if (Text)
			{
				Text->Confirm();
				Text->Confirm();
			}
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("ending: the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f, []()
		{
			Check(!Run()->IsReturningHome(), TEXT("ending: after it the run is no longer on its way home"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("list: Select act after the ending"));
			return 0.3f;
		}));

		// --- The list, Continue, the save read again (criteria 8, 9, 13) ---
		S.Add(Do(TEXT("list: after the ending"), []()
		{
			const FString Labels = ListLabels();
			const int32 Index = ListIndexOf(URunActListWidget::ReturnEntry);
			Check(Index == 4 && TopList()->GetActOfEntry(3) == 4 && TopList()->GetEntryLabel(4).ToString().StartsWith(TEXT("Return")),
				FString::Printf(TEXT("list: every entry selectable after the ending, the return after act 4, labelled Return (%s)"), *Labels));
			Shot(TEXT("6_List_WithReturn"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("list: Return"), []()
		{
			Check(TimeStateDebug::ClickChoiceEntry(W(), ListIndexOf(URunActListWidget::ReturnEntry)), TEXT("list: Return chosen"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("list: the return from the list"), []() { return InReturn(); }, 60.f, []()
		{
			CheckReturnState(TEXT("list's return"));
			Check(FState::Now().Same(GRun.AtExit), FString::Printf(TEXT("list's return: with its snapshot's state (%s)"), *FState::Now().ToString()));
			// What the pause menu's Main menu does: StdCore's return to the main menu.
			Check(Flow()->ReturnToMainMenu(), TEXT("quit: the pause menu's Main menu from the hangar"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("quit: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Check(Run()->HasReturnEntry(), TEXT("quit: the return entry kept"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("continue: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("continue: back in the ship"), []() { return InReturn(); }, 60.f, []()
		{
			CheckReturnState(TEXT("continue"));
			Check(FState::Now().Same(GRun.AtExit), FString::Printf(TEXT("continue: with the snapshot's state (%s)"), *FState::Now().ToString()));
			StepOut();
			return 1.0f;
		}));
		S.Add(Do(TEXT("continue: after the step out"), []()
		{
			const TimeStateDebug::FInputReport Input = TimeStateDebug::BuildInputReport(W());
			TimeStateDebug::LogInputReport(W(), TEXT("tls.ReturnCheck input (Continue)"));
			Check(Input.IsGameReady(), TEXT("input: game input after the step out on the return reached by Continue"));
			Check(Flow()->ReturnToMainMenu(), TEXT("restart: back to the main menu"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("restart: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			// The game closed and started again: the save read from disk.
			Run()->LoadSave();
			Check(Run()->GetLoadedSaveVersion() == 5 && Run()->HasReturnEntry() && FState::Of(Run()->GetReturnSnapshot()).Same(GRun.AtExit) && Run()->GetSnapshots().Num() == 4,
				FString::Printf(TEXT("restart: the save read from disk (version %d) holds the four acts and the return"), Run()->GetLoadedSaveVersion()));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("restart: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("restart: back in the ship"), []() { return InReturn(); }, 60.f, []()
		{
			Check(FState::Now().Same(GRun.AtExit), FString::Printf(TEXT("restart: with the snapshot's state (%s)"), *FState::Now().ToString()));
			// "Open the structure" without the twin AI's parts.
			Run()->ClearArchive();
			return 0.2f;
		}));
		AddPlayToEnding(S, TEXT("restart"), false);

		// --- The last Landing row dropped at Silent, and no Landing row (criteria 4, 5) ---
		S.Add(Do(TEXT("silent: Continue"), []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("silent: Continue"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("silent: in the ship"), []() { return InReturn(); }, 60.f, []()
		{
			Home()->RestoreState(GRun.SilentBattery, Home()->GetHomeSecondsElapsed());
			StepOut();
			return 0.2f;
		}));
		S.Add(WaitFor(TEXT("silent: the door opens"), []() { return Ret()->GetReturnState() == EPrologueReturnState::DoorOpened; }, 60.f, [Delay]()
		{
			const APrologueReturn* R = Ret();
			const FLine* E = LineOf(NarrativeFixture::GetEngineerLineName(TEXT("Landing_1"), 1));
			Check(E && E->TypedRun >= 0.0 && !LineOf(TEXT("Landing_1")) && !LineOf(TEXT("Landing_2")),
				FString::Printf(TEXT("silent: the Landing rows dropped, the engineer's line still said (%s)"), *LinesSaid()));
			const float Took = R->GetDoorOpenedRunSeconds() - R->GetLandingDoneRunSeconds();
			Check(E && R->GetLandingDoneRunSeconds() >= E->TypedRun - 0.05 && FMath::Abs(Took - Delay) <= 0.1f,
				FString::Printf(TEXT("silent: the last row dropped ends the wait; the door %.2f s after"), Took));
			NarrativeFixture::SetEpilogueGroups({ EpilogueGroups::Landing });
			Check(Run()->RestartFromReturn(), TEXT("no Landing row: the return again"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("no Landing row: in the ship"), []() { return InReturn(); }, 60.f, []() { StepOut(); return 0.2f; }));
		S.Add(WaitFor(TEXT("no Landing row: the door opens"), []() { return Ret()->GetReturnState() == EPrologueReturnState::DoorOpened; }, 30.f, [Delay]()
		{
			const APrologueReturn* R = Ret();
			const float Took = R->GetDoorOpenedRunSeconds() - R->GetSteppedOutRunSeconds();
			Check(GRun.Lines.IsEmpty() && FMath::IsNearlyEqual(R->GetLandingDoneRunSeconds(), R->GetSteppedOutRunSeconds(), 0.05f) && FMath::Abs(Took - Delay) <= 0.1f,
				FString::Printf(TEXT("no Landing row: the delay from the step out, the door %.2f s after it (%s)"), Took, *LinesSaid()));
			NarrativeFixture::SetEpilogueGroups({});
			Check(Flow()->ReturnToMainMenu(), TEXT("act 2: to the main menu"));
			return 0.5f;
		}));

		// --- Restarting from act 2 discards the return (criterion 9) ---
		S.Add(WaitFor(TEXT("act 2: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("act 2: Select act"));
			const int32 Index = ListIndexOf(2);
			Check(Index != INDEX_NONE && TimeStateDebug::ClickChoiceEntry(W(), Index), FString::Printf(TEXT("act 2: Area 2 chosen (%s)"), *ListLabels()));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("act 2: arrived"), []() { return InArea(2); }, 180.f, []()
		{
			const URunSaveGame* Disk = ReadSlot();
			Check(!Run()->HasReturnEntry() && Run()->GetSnapshots().Num() == 2 && !Run()->IsReturningHome() && Disk && !Disk->bHasReturn && Disk->Snapshots.Num() == 2,
				FString::Printf(TEXT("act 2: the entries after it discarded, the return's included (%d acts, on disk %d and return %d)"), Run()->GetSnapshots().Num(),
					Disk ? Disk->Snapshots.Num() : -1, Disk && Disk->bHasReturn ? 1 : 0));
			Check(Flow()->ReturnToMainMenu(), TEXT("switch: to the main menu"));
			return 0.5f;
		}));

		// --- The way home switched off (criterion 11) ---
		S.Add(WaitFor(TEXT("switch: in the main menu"), []() { return InMenu(); }, 60.f, []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = false;
			const bool bWrote = FFileHelper::SaveArrayToFile(GRun.SaveWithReturn, *SlotPath());
			Run()->LoadSave();
			const URunSaveGame* Disk = ReadSlot();
			Check(bWrote && Disk && Disk->bHasReturn && !Run()->HasReturnEntry() && Run()->GetSnapshots().Num() == 4,
				TEXT("switch off: a file that holds a return entry loads with its four acts and no return entry offered"));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("switch off: Select act"));
			const FString Labels = ListLabels();
			Check(ListIndexOf(URunActListWidget::ReturnEntry) == INDEX_NONE && !Labels.Contains(TEXT("Return")), FString::Printf(TEXT("switch off: no return in the list (%s)"), *Labels));
			Check(TimeStateDebug::ClickChoiceEntry(W(), ListIndexOf(4)), TEXT("switch off: Area 4 chosen"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("switch off: area 4"), []() { return InArea(4); }, 180.f, []()
		{
			Teleport(First<AAreaExit>()->GetActorLocation(), true);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("switch off: area 4's exit opens the ending as before"), []() { return InMenu() && TopEnding() != nullptr; }, 60.f, []()
		{
			const URunSaveGame* Disk = ReadSlot();
			Check(!Run()->IsReturningHome() && Disk && !Disk->bHasReturn && !GRun.Lines.ContainsByPredicate([](const FLine& L) { return L.Row == TEXT("Landing_1"); }),
				TEXT("switch off: no way home, no return entry taken"));
			URunEndingWidget* Ending = TopEnding();
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), FString::Printf(TEXT("switch off: %s chosen"), *Ending->GetEndingOfEntry(0).ToString()));
			if (URunTextSceneWidget* Text = TopScene())
			{
				Text->Confirm();
				Text->Confirm();
			}
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("switch off: back in the main menu"), []() { return InMenu() && Top() == MainMenu(); }, 10.f));
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
		GetMutableDefault<UTheLastShiftSettings>()->bReturnHome = true;
		GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = true;
		GRun.Steps = BuildTest();
		GRun.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d steps, in %s."), Name(), GRun.Steps.Num(), *MapName());
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	/** tls.Run.Return [Battery] [TwinParts]: the run at the end of area 4, then the flight back. */
	static void RunReturn(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		URunSubsystem* R = URunSubsystem::Get(World);
		UHomeStateSubsystem* H = UHomeStateSubsystem::Get(World);
		if (!R || !H)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Run.Return: no game world."));
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
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Run.Return: battery %.3f, %d twin parts, %d log entries: %s."), H->GetBatteryFraction(), R->GetArchive().TwinPartsPlayed,
			R->GetArchive().LogOrders.Num(), R->StartReturnHome() ? TEXT("the flight back started") : TEXT("refused (see LogRun)"));
	}

	static FAutoConsoleCommandWithWorldAndArgs GReturnCheckCmd(
		TEXT("tls.ReturnCheck"),
		TEXT("Brief 31A: the way home (criteria 1 to 13 with fixture rows; shots to Saved/Screenshots/Brief31A). Run in MainMenu, PIE. Args: [Quit]. Clears the save slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTest));

	static FAutoConsoleCommandWithWorldAndArgs GRunReturnCmd(
		TEXT("tls.Run.Return"),
		TEXT("Brief 31A: puts the run at the end of area 4 (battery and twin AI parts given, else the run's own) and starts the flight back. Any map. Args: [Battery] [TwinParts]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunReturn));
}

#endif // !UE_BUILD_SHIPPING
