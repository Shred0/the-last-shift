// SPDX-License-Identifier: MIT

// Brief 11: the prologue's two hand-built maps. tls.PrologueTest [Quit] (run from MainMenu, in PIE and in a package):
// C) the prologue's maps switched off: "New game" goes from the text scene to area 1, as before; the input and the first
// snapshot there are the baseline. A) "New game" read through: the text scene, Prologue_Home (outside the run: no act,
// entity, residue or snapshot, the save untouched; the input as in area 1; lights lit in Normal time and dark while
// Stopped; the two free terminals and their doors; the hologram: points, size, colour, hidden while Stopped and in scans,
// no collision, frame time with and without it; the walk from the start to the ship), the flight with the first Flight
// line, Prologue_Arrival (the ship's lights, the hatch started by leaving the ship and closing in HatchCloseSeconds of
// Normal time, still while Stopped and behind the pause menu, the Hatch lines, the Door lines, then the exit door after
// ExitDoorDelaySeconds of Normal time, still while Stopped and behind the pause menu), and area 1 with the same run state
// and first snapshot as C. The flashlight: off and unavailable at home (its key does nothing), available and off at the
// start of the arrival, as without the prologue's maps in area 1. The home's two corridors: measured, walked, shot.
// B) skipped, with no Hangar and no Hatch rows: they say nothing and block nothing; the door's delay measured without
// a Stop. D) Continue and Select act never load a prologue map. E) with a saved run in act 2, New game, quit to the main
// menu from inside Prologue_Home: nothing saved or deleted, and Continue restores that saved run. F) no Door rows: the
// door opens the delay after the last Hatch row. G) neither group: the delay after the hatch has closed. The lines are
// the test's own fixture rows. tls.Prologue.Report logs the prologue map's doors, terminals, lights, hatch, hologram,
// triggers, corridors and flashlight. Development builds only.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TestLidarCharacter.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/StdMenuPlayerController.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueHatch.h"
#include "Prologue/PrologueHologram.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Residue/Residue.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UI/StdMainMenuWidget.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace PrologueDebug
{
	using TimeStateDebug::ResolveGameWorld;

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static UTimeStateSubsystem* Time() { return W() ? W()->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static int32 Stage() { return URunSubsystem::GetPrologueStageOf(W()); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }
	static FString MapName() { return W() ? UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()) : FString(); }

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
	static UScanDirectorComponent* Director() { return Pawn() ? Pawn()->FindComponentByClass<UScanDirectorComponent>() : nullptr; }

	static UStdMainMenuWidget* MainMenu()
	{
		const AStdMenuPlayerController* MenuPC = W() ? Cast<AStdMenuPlayerController>(W()->GetFirstPlayerController()) : nullptr;
		return MenuPC ? MenuPC->GetMainMenu() : nullptr;
	}

	static bool InMenu() { return MainMenu() != nullptr && !Transitioning(); }
	static bool InStage(int32 InStage) { return W() && Stage() == InStage && Pawn() && !Transitioning(); }
	static bool InArea(int32 InAct) { return W() && Act() == InAct && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }
	static URunTextSceneWidget* TopScene() { return Cast<URunTextSceneWidget>(TimeStateDebug::TopScreen(W())); }

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

	static UInteractPromptWidget* Prompt()
	{
		for (TObjectIterator<UInteractPromptWidget> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && It->GetWorld() == W())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static int32 LitLights(int32* OutTotal = nullptr)
	{
		int32 Lit = 0;
		int32 Total = 0;
		for (const ALightGroupDevice* Group : All<ALightGroupDevice>())
		{
			Lit += Group->GetLitCount();
			Total += Group->Lights.Num();
		}
		if (OutTotal)
		{
			*OutTotal = Total;
		}
		return Lit;
	}

	static bool SendKey(const FKey& Key)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		if (UStdScreenWidget* Screen = TimeStateDebug::TopScreen(W()))
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

	static bool PressPawnKey(FName Action) { return TimeStateDebug::PressKey(W(), TimeStateDebug::BoundKey(W(), Action)); }

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

	/**
	 * The input state that decides whether real keys arrive (SceneDebug's signature), with the area fields read as "no area
	 * holds the player": the prologue's maps have no area generator.
	 */
	static FString InputSignature()
	{
		const TimeStateDebug::FInputReport R = TimeStateDebug::BuildInputReport(W());
		const bool bAreaFree = !R.bHasArea || (R.bAreaReady && !R.bHeldByArea);
		return FString::Printf(TEXT("verdict=%s controller=%s mode=%s ignores=%d capture=%s viewportFocus=%d mouseCaptured=%d cursor=%d contexts=[%s] pawnContext=%d keys=[%s] unmapped=%d pawnInput=%d canMove=%d areaFree=%d transitioning=%d paused=%d"),
			R.IsGameReady() ? TEXT("game") : R.IsMenuReady() ? TEXT("menu") : TEXT("none"), *R.ControllerClass, *R.InputMode, R.bViewportIgnoresInput ? 1 : 0,
			*R.MouseCaptureMode, R.bViewportFocused ? 1 : 0, R.bMouseCaptured ? 1 : 0, R.bCursorShown ? 1 : 0, *FString::Join(R.MappingContexts, TEXT(", ")),
			R.bPawnContextApplied ? 1 : 0, *FString::Join(R.PawnActionKeys, TEXT(", ")), R.UnmappedPawnActions, R.bPawnInputEnabled ? 1 : 0, R.bPawnCanMove ? 1 : 0,
			bAreaFree ? 1 : 0, R.bTransitioning ? 1 : 0, R.bPausedByMenu ? 1 : 0);
	}

	// --- Step runner (core ticker, survives map changes) ---

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
		FString Map;
		double Real = 0.0;
		/** The run's clock when it started, and when it showed its last character (-1: not yet). */
		double StartRun = -1.0;
		double TypedRun = -1.0;
	};

	struct FRun
	{
		bool bQuitAtEnd = false;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		bool bSavedMaps = true;

		// Sampled every frame.
		TArray<FLine> Lines;
		FName LastRow;
		TArray<FString> Maps;
		double LastReal = -1.0;
		bool bWalking = false;
		FVector WalkTarget = FVector::ZeroVector;
		double WalkStartReal = 0.0;
		double WalkSeconds = 0.0;
		TArray<float> FrameMs;
		int32 FrameSamplesWanted = 0;
		int32 LastScansThisStop = 0;
		bool bHologramShownAtScan = false;
		int32 ScansSeen = 0;
		EPrologueHatchState LastHatch = EPrologueHatchState::Open;
		bool bDoorOpenBeforeClosed = false;
		double HatchNormalSeconds = 0.0;
		double HatchClosedReal = -1.0;
		double DoorOpenedReal = -1.0;
		double HatchClosedRun = -1.0;
		double DoorOpenedRun = -1.0;
		double DelayNormalSeconds = 0.0;
		int32 LinesAtDoor = 0;
		int32 SaveWritesBefore = 0;

		// Scratch.
		FString BaselineInput;
		FAreaSnapshot BaselineSnapshot;
		int32 BaselineSnapshots = 0;
		FString HashBefore;
		int32 SnapshotsBefore = 0;
		int32 MapsAtMark = 0;
		double MarkReal = 0.0;
		float Fraction = 0.f;
		float FrameOnMs = 0.f;
		float FrameOffMs = 0.f;
		int32 BankedBefore = 0;
		bool bAreaFlashlightAvailable = false;
		bool bAreaFlashlightOn = false;
		float DelayMark = 0.f;
	};
	static FRun GRun;

	static const TCHAR* Name() { return TEXT("tls.PrologueTest"); }

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name(), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name(), *What);
	}

	static void Finish(const TCHAR* Why)
	{
		GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = GRun.bSavedMaps;
		NarrativeFixture::Restore(W());
		GRun.bRunning = false;
		GRun.bWalking = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), Name(),
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuitAtEnd)
		{
			FPlatformMisc::RequestExit(false, Name());
		}
	}

	static void Sample(double Now)
	{
		const float Delta = GRun.LastReal >= 0.0 ? static_cast<float>(Now - GRun.LastReal) : 0.f;
		GRun.LastReal = Now;
		const FString Map = MapName();
		if (!Map.IsEmpty() && (GRun.Maps.IsEmpty() || GRun.Maps.Last() != Map))
		{
			GRun.Maps.Add(Map);
			Note(FString::Printf(TEXT("map %s"), *Map));
		}
		// Every message the channel starts (a restart after a suspension is the same message).
		const UTextChannelComponent* Text = Channel();
		const FName Row = Text ? Text->GetCurrentRowName() : NAME_None;
		if (Row != GRun.LastRow)
		{
			GRun.LastRow = Row;
			if (!Row.IsNone())
			{
				GRun.Lines.Add({ Row, Map, Now, Time() ? Time()->GetRunSeconds() : -1.0, -1.0 });
				Note(FString::Printf(TEXT("line %s started in %s"), *Row.ToString(), *Map));
			}
		}
		// The moment the current message shows its last character (a restart after a Stop keeps the first one).
		if (Text && !Row.IsNone() && GRun.Lines.Num() > 0 && GRun.Lines.Last().Row == Row && GRun.Lines.Last().TypedRun < 0.0
			&& Text->GetShownCells() > 0 && Text->GetShownCells() >= Text->GetCurrentText().Len())
		{
			GRun.Lines.Last().TypedRun = Time() ? Time()->GetRunSeconds() : -1.0;
			Note(FString::Printf(TEXT("line %s typed out (run clock %.3f s)"), *Row.ToString(), GRun.Lines.Last().TypedRun));
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
		if (GRun.FrameSamplesWanted > 0 && Delta > 0.f)
		{
			GRun.FrameMs.Add(Delta * 1000.f);
			--GRun.FrameSamplesWanted;
		}
		// A scan starting: the hologram must not be there to be captured.
		if (const UScanDirectorComponent* D = Director())
		{
			const int32 Scans = D->GetScansThisStop();
			if (Scans > GRun.LastScansThisStop)
			{
				++GRun.ScansSeen;
				if (const APrologueHologram* Holo = First<APrologueHologram>())
				{
					GRun.bHologramShownAtScan |= Holo->IsShown();
				}
			}
			GRun.LastScansThisStop = Scans;
		}
		// The hatch and the exit door.
		if (const APrologueHatch* Hatch = First<APrologueHatch>())
		{
			const EPrologueHatchState State = Hatch->GetHatchState();
			const UTimeStateSubsystem* T = Time();
			if (State == EPrologueHatchState::Closing && T && !T->IsStopped() && !T->IsRunFrozen())
			{
				GRun.HatchNormalSeconds += Delta;
			}
			if (State == EPrologueHatchState::DoorDelay && T && !T->IsStopped() && !T->IsRunFrozen())
			{
				GRun.DelayNormalSeconds += Delta;
			}
			if (Hatch->ExitDoor && Hatch->ExitDoor->IsOpen() && State < EPrologueHatchState::DoorOpened)
			{
				GRun.bDoorOpenBeforeClosed = true;
			}
			// The hatch may go from Closed to DoorOpened between two samples: both moments are taken at the first frame seen.
			if (State >= EPrologueHatchState::Closed && GRun.HatchClosedReal < 0.0)
			{
				GRun.HatchClosedReal = Now;
				GRun.HatchClosedRun = T ? T->GetRunSeconds() : -1.0;
			}
			if (State == EPrologueHatchState::DoorOpened && GRun.DoorOpenedReal < 0.0)
			{
				GRun.DoorOpenedReal = Now;
				GRun.DoorOpenedRun = T ? T->GetRunSeconds() : -1.0;
				GRun.LinesAtDoor = GRun.Lines.Num();
			}
			GRun.LastHatch = State;
		}
		else
		{
			GRun.LastHatch = EPrologueHatchState::Open;
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		Sample(Now);
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

	static void Shot(const FString& ShotName)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief11"), ShotName + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot %s"), *Path));
	}

	static void LookAt(const FVector& Target)
	{
		if (APawn* P = Pawn())
		{
			const FVector Eye = P->GetPawnViewLocation();
			PC()->SetControlRotation((Target - Eye).Rotation());
		}
	}

	static void Teleport(const FVector& Where)
	{
		if (APawn* P = Pawn())
		{
			P->SetActorLocation(FVector(Where.X, Where.Y, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	static float Dist2D(const FVector& A, const FVector& B) { return static_cast<float>(FVector::Dist2D(A, B)); }

	/** Walks the pawn (movement input every frame) to Target(), within Radius; the legs add up to the walk time. */
	static void AddWalk(TArray<FStep>& S, const FString& Label, TFunction<FVector()> Target, float Radius = 60.f, float Timeout = 30.f)
	{
		S.Add(Do(Label + TEXT(": walk"), [Target]()
		{
			GRun.WalkTarget = Target();
			GRun.bWalking = true;
			GRun.WalkStartReal = FPlatformTime::Seconds();
			return 0.f;
		}));
		// An exit reached on the way ends the leg too (the travel has started).
		S.Add(WaitFor(Label + TEXT(": reached"), [Radius]() { return Transitioning() || (Pawn() && Dist2D(Pawn()->GetActorLocation(), GRun.WalkTarget) <= Radius); }, Timeout, [Label]()
		{
			GRun.bWalking = false;
			const double Took = FPlatformTime::Seconds() - GRun.WalkStartReal;
			GRun.WalkSeconds += Took;
			Note(FString::Printf(TEXT("%s: walked in %.2f s (%.2f s so far)"), *Label, Took, GRun.WalkSeconds));
			if (ACharacter* C = Cast<ACharacter>(Pawn()))
			{
				C->GetCharacterMovement()->StopMovementImmediately();
			}
			return 0.2f;
		}));
	}

	/** A point 100 cm from the terminal, on the side of the player start: within its InteractRadius. */
	static FVector StandAt(const APowerTerminal* Terminal)
	{
		const FVector T = Terminal->GetActorLocation();
		FVector Dir = Terminal->GetActorForwardVector();
		Dir.Z = 0.f;
		return T + Dir.GetSafeNormal() * 100.f;
	}

	/** Beside the arrival's ship, 11 m from the hatch wall: the hatch in view, the ship to one side. */
	static FVector HatchView()
	{
		const FBox Ship = First<APrologueHatch>()->GetShipBox();
		return FVector(Ship.Min.X + 400.f, Ship.Min.Y - 800.f, 0.f);
	}

	static ATestLidarCharacter* Char() { return Cast<ATestLidarCharacter>(Pawn()); }

	static FString FlashlightState()
	{
		const ATestLidarCharacter* C = Char();
		return C ? FString::Printf(TEXT("%s, %s"), C->IsFlashlightAvailable() ? TEXT("available") : TEXT("unavailable"), C->IsFlashlightOn() ? TEXT("on") : TEXT("off"))
			: FString(TEXT("no pawn"));
	}

	static const APrologueLineTrigger* TriggerOf(FName Group)
	{
		for (const APrologueLineTrigger* T : All<APrologueLineTrigger>())
		{
			if (T->Group == Group)
			{
				return T;
			}
		}
		return nullptr;
	}

	/** Inside cm into the room whose entrance holds Group's line trigger, on the trigger's centre line. */
	static FVector RoomEntry(FName Group, float Inside)
	{
		const APrologueLineTrigger* T = TriggerOf(Group);
		return T ? FVector(T->GetActorLocation().X - T->Extent.X + Inside, T->GetActorLocation().Y, 0.f) : FVector::ZeroVector;
	}

	/** A corridor of the home map, measured: its pieces are the actors tagged Corridor<Index>. */
	struct FCorridor
	{
		bool bFound = false;
		int32 Pieces = 0;
		/** Along X: the side walls' extent. Across and up: traces at the corridor's middle, wall to wall and floor to ceiling. */
		float Length = 0.f;
		float Width = 0.f;
		float Height = 0.f;
		float StartX = 0.f;
		float EndX = 0.f;
		int32 Lights = 0;
	};

	static FCorridor MeasureCorridor(int32 Index)
	{
		FCorridor C;
		UWorld* World = W();
		if (!World)
		{
			return C;
		}
		const FName Tag(*FString::Printf(TEXT("Corridor%d"), Index));
		FBox Walls(ForceInit);
		FBox AllBox(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(Tag))
			{
				continue;
			}
			++C.Pieces;
			const FBox B = It->GetComponentsBoundingBox(true);
			AllBox += B;
			if (B.GetSize().Y < 100.f && B.GetSize().Z > 200.f)
			{
				Walls += B;
			}
		}
		if (C.Pieces == 0 || !Walls.IsValid)
		{
			return C;
		}
		C.bFound = true;
		C.Length = static_cast<float>(Walls.GetSize().X);
		C.StartX = static_cast<float>(Walls.Min.X);
		C.EndX = static_cast<float>(Walls.Max.X);
		const FVector Mid(Walls.GetCenter().X, Walls.GetCenter().Y, 150.f);
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Pawn());
		FHitResult L, R, D, U;
		const bool bL = World->LineTraceSingleByChannel(L, Mid, Mid - FVector(0.f, 1000.f, 0.f), ECC_Visibility, Params);
		const bool bR = World->LineTraceSingleByChannel(R, Mid, Mid + FVector(0.f, 1000.f, 0.f), ECC_Visibility, Params);
		const bool bD = World->LineTraceSingleByChannel(D, Mid, Mid - FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params);
		const bool bU = World->LineTraceSingleByChannel(U, Mid, Mid + FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params);
		C.Width = bL && bR ? L.Distance + R.Distance : -1.f;
		C.Height = bD && bU ? D.Distance + U.Distance : -1.f;
		for (const ALightGroupDevice* Group : All<ALightGroupDevice>())
		{
			for (const TObjectPtr<ALight>& Light : Group->Lights)
			{
				C.Lights += Light && Light->GetActorLocation().X > C.StartX && Light->GetActorLocation().X < C.EndX
					&& FMath::Abs(Light->GetActorLocation().Y - Walls.GetCenter().Y) < 200.f ? 1 : 0;
			}
		}
		return C;
	}

	/** The arrival's last line that started at or after FromIndex, or null. */
	static const FLine* LastLineFrom(int32 FromIndex)
	{
		return GRun.Lines.Num() > FromIndex ? &GRun.Lines.Last() : nullptr;
	}

	static bool RowSeen(FName Row, int32 FromIndex = 0)
	{
		for (int32 I = FromIndex; I < GRun.Lines.Num(); ++I)
		{
			if (GRun.Lines[I].Row == Row)
			{
				return true;
			}
		}
		return false;
	}

	static FString RowsFrom(int32 FromIndex)
	{
		FString Out;
		for (int32 I = FromIndex; I < GRun.Lines.Num(); ++I)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + GRun.Lines[I].Row.ToString();
		}
		return Out;
	}

	static bool ChannelIdle() { return Channel() && !Channel()->IsBusy(); }

	static void AddBackToMenu(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": back to the main menu"), []() { return Run()->ReturnToMainMenu() ? 0.5f : -1.f; }));
		S.Add(WaitFor(Label + TEXT(": in the main menu"), []() { return InMenu(); }, 60.f, []() { return 0.5f; }));
	}

	/** The checks of "outside the run" in a prologue map. */
	static void CheckOutsideRun(const FString& Label)
	{
		Check(Act() == 0, FString::Printf(TEXT("%s: act 0 (%d)"), *Label, Act()));
		Check(All<AEntity>().IsEmpty() && All<AResidue>().IsEmpty() && !AAreaGenerator::Find(W()),
			FString::Printf(TEXT("%s: no entity (%d), no residue (%d), no area generator"), *Label, All<AEntity>().Num(), All<AResidue>().Num()));
		Check(Home()->GetBatteryFraction() == 1.0 && Home()->GetHomeSecondsElapsed() == 0.0 && !Home()->IsRunLost(),
			FString::Printf(TEXT("%s: a new run's home state in memory: battery %.4f, home %.0f s"), *Label, Home()->GetBatteryFraction(), Home()->GetHomeSecondsElapsed()));
		Check(Energy()->GetBankedEnergy() == 0 && Run()->GetArchive().IsEmpty(), FString::Printf(TEXT("%s: banked %d, archive empty"), *Label, Energy()->GetBankedEnergy()));
		Check(Run()->GetSnapshots().Num() == GRun.SnapshotsBefore && SaveHash() == GRun.HashBefore && Run()->GetSaveWrites() == GRun.SaveWritesBefore,
			FString::Printf(TEXT("%s: no snapshot taken, nothing saved, the save as it was at New game (%d snapshots, %d writes, %s)"), *Label,
				Run()->GetSnapshots().Num(), Run()->GetSaveWrites() - GRun.SaveWritesBefore, *SaveHash()));
	}

	static void CheckInput(const FString& Label)
	{
		const FString Now = InputSignature();
		TimeStateDebug::LogInputReport(W(), Name());
		Check(Now == GRun.BaselineInput, FString::Printf(TEXT("%s: the input is the same as in area 1 entered without the prologue's maps: %s"), *Label, *Now));
	}

	static void CheckLightsLit(const FString& Label)
	{
		int32 Total = 0;
		const int32 Lit = LitLights(&Total);
		Check(Total > 0 && Lit == Total, FString::Printf(TEXT("%s: %d of %d lights lit in Normal time"), *Label, Lit, Total));
	}

	// --- Stop and Resume ---

	static void AddStopResume(TArray<FStep>& S, const FString& Label, bool bHologram, const FString& ScanShot)
	{
		S.Add(Do(Label + TEXT(": Stop"), []()
		{
			GRun.ScansSeen = 0;
			GRun.bHologramShownAtScan = false;
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("Stop key pressed"));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": time is Stopped"), []() { return Time() && Time()->IsStopped(); }, 3.f, [Label, bHologram]()
		{
			int32 Total = 0;
			const int32 Lit = LitLights(&Total);
			Check(Lit == 0 && Total > 0, FString::Printf(TEXT("%s: every light dark while Stopped (%d of %d lit)"), *Label, Lit, Total));
			if (bHologram)
			{
				const APrologueHologram* Holo = First<APrologueHologram>();
				Check(Holo && !Holo->IsShown(), FString::Printf(TEXT("%s: the hologram is hidden while Stopped"), *Label));
			}
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the first scan"), []() { return GRun.ScansSeen >= 1; }, 5.f, [Label, bHologram, ScanShot]()
		{
			if (bHologram)
			{
				Check(!GRun.bHologramShownAtScan, FString::Printf(TEXT("%s: the hologram was hidden when the scan started (nothing for the capture to see)"), *Label));
			}
			if (!ScanShot.IsEmpty())
			{
				Shot(ScanShot);
			}
			return 1.5f;
		}));
		S.Add(Do(Label + TEXT(": Resume"), []()
		{
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("Resume key pressed"));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": Normal again"), []() { return Time() && !Time()->IsStopped(); }, 3.f, [Label, bHologram]()
		{
			CheckLightsLit(Label + TEXT(" after Resume"));
			if (bHologram)
			{
				const APrologueHologram* Holo = First<APrologueHologram>();
				Check(Holo && Holo->IsShown(), FString::Printf(TEXT("%s: the hologram shows again in Normal time"), *Label));
			}
			return 0.3f;
		}));
	}

	// --- Terminals ---

	static void AddTerminal(TArray<FStep>& S, const FString& Label, int32 Index, bool bWalk)
	{
		auto Terminal = [Index]() { const TArray<APowerTerminal*> T = All<APowerTerminal>(); return T.IsValidIndex(Index) ? T[Index] : nullptr; };
		if (bWalk)
		{
			AddWalk(S, Label, [Terminal]() { return Terminal() ? StandAt(Terminal()) : Pawn()->GetActorLocation(); }, 40.f);
		}
		else
		{
			S.Add(Do(Label + TEXT(": teleport"), [Terminal]() { if (Terminal()) { Teleport(StandAt(Terminal())); } return 0.4f; }));
		}
		S.Add(Do(Label + TEXT(": the prompt and Interact"), [Label, Terminal]()
		{
			APowerTerminal* T = Terminal();
			Check(T && T->IsInRange(Pawn()), FString::Printf(TEXT("%s: at the terminal %s"), *Label, *GetNameSafe(T)));
			if (!T)
			{
				return -1.f;
			}
			const UInteractPromptWidget* P = Prompt();
			const FString Shown = P ? P->GetShownText() : FString();
			bool bDigit = false;
			for (const TCHAR C : Shown)
			{
				bDigit |= FChar::IsDigit(C);
			}
			Check(T->GetCost() == 0 && P && P->GetMode() == EInteractPromptMode::Power && !Shown.IsEmpty() && !bDigit && !Shown.Contains(TEXT("energy")),
				FString::Printf(TEXT("%s: free terminal (cost %d), the prompt shows no cost: \"%s\""), *Label, T->GetCost(), *Shown));
			const ADoorDevice* Door = T->Targets.Num() > 0 ? Cast<ADoorDevice>(T->Targets[0]) : nullptr;
			Check(Door && !Door->IsOpen(), FString::Printf(TEXT("%s: its door %s is closed"), *Label, *GetNameSafe(Door)));
			GRun.BankedBefore = Energy()->GetBankedEnergy();
			GRun.MapsAtMark = GRun.Lines.Num();
			Check(PressPawnKey(TEXT("InteractAction")), FString::Printf(TEXT("%s: Interact pressed"), *Label));
			return 0.3f;
		}));
		S.Add(Do(Label + TEXT(": the door opened"), [Label, Terminal]()
		{
			APowerTerminal* T = Terminal();
			const ADoorDevice* Door = T && T->Targets.Num() > 0 ? Cast<ADoorDevice>(T->Targets[0]) : nullptr;
			Check(Door && Door->IsOpen() && Door->IsPowered() && !Door->IsTimed(), FString::Printf(TEXT("%s: the door opens at the first Interact, untimed"), *Label));
			if (Door)
			{
				const FVector Size = Door->GetDoorSize();
				Check(FMath::IsNearlyEqual(Size.Y, 200.f, 1.f) && FMath::IsNearlyEqual(Size.Z, 300.f, 1.f),
					FString::Printf(TEXT("%s: the door is %.0f cm wide and %.0f cm high"), *Label, Size.Y, Size.Z));
			}
			Check(Energy()->GetBankedEnergy() == GRun.BankedBefore, FString::Printf(TEXT("%s: nothing spent (banked %d)"), *Label, Energy()->GetBankedEnergy()));
			return 2.f;
		}));
		S.Add(Do(Label + TEXT(": the door stays open"), [Label, Terminal]()
		{
			APowerTerminal* T = Terminal();
			const ADoorDevice* Door = T && T->Targets.Num() > 0 ? Cast<ADoorDevice>(T->Targets[0]) : nullptr;
			Check(Door && Door->IsOpen(), FString::Printf(TEXT("%s: the door is still open 2 s later"), *Label));
			bool bSpend = false;
			for (int32 I = GRun.MapsAtMark; I < GRun.Lines.Num(); ++I)
			{
				bSpend |= GRun.Lines[I].Row.ToString().Contains(TEXT("Spend"));
			}
			Check(!bSpend, FString::Printf(TEXT("%s: no spending comment for a free terminal"), *Label));
			return 0.f;
		}));
	}

	// --- Corridors ---

	/**
	 * Through door DoorIndex and down corridor Index to the room of NextGroup's trigger: measured (20 m x 3 m x 3.5 m), its
	 * lights in the home's light group, shot from its start, walked; the next room's line not said in the corridor.
	 */
	static void AddCorridor(TArray<FStep>& S, int32 Index, int32 DoorIndex, FName NextGroup)
	{
		const FString Label = FString::Printf(TEXT("A: corridor %d"), Index);
		AddWalk(S, FString::Printf(TEXT("A: through door %d"), DoorIndex + 1), [DoorIndex]() { return All<ADoorDevice>()[DoorIndex]->GetActorLocation() + FVector(-150.f, 0.f, 0.f); }, 60.f);
		AddWalk(S, Label + TEXT(": its start"), [DoorIndex]() { return All<ADoorDevice>()[DoorIndex]->GetActorLocation() + FVector(120.f, 0.f, 0.f); }, 40.f);
		S.Add(Do(Label + TEXT(": measured"), [Label, Index, DoorIndex, NextGroup]()
		{
			const FCorridor C = MeasureCorridor(Index);
			const ADoorDevice* Door = All<ADoorDevice>()[DoorIndex];
			const APrologueLineTrigger* Next = TriggerOf(NextGroup);
			Check(C.bFound && FMath::IsNearlyEqual(C.Length, 2000.f, 2.f) && FMath::IsNearlyEqual(C.Width, 300.f, 2.f) && FMath::IsNearlyEqual(C.Height, 350.f, 2.f),
				FString::Printf(TEXT("%s: %.0f cm long, %.0f cm wide, %.0f cm high (%d pieces, X %.0f..%.0f)"), *Label, C.Length, C.Width, C.Height, C.Pieces, C.StartX, C.EndX));
			Check(Door && FMath::Abs(Door->GetActorLocation().X - C.StartX) <= 30.f,
				FString::Printf(TEXT("%s: its door stands at its start (door X %.0f, corridor from %.0f)"), *Label, Door ? Door->GetActorLocation().X : 0.f, C.StartX));
			Check(Next && Next->GetActorLocation().X - Next->Extent.X >= C.EndX - 30.f && !Next->HasFired(),
				FString::Printf(TEXT("%s: the %s trigger starts at the room past its end (trigger from %.0f), not fired yet"), *Label, *NextGroup.ToString(),
					Next ? Next->GetActorLocation().X - Next->Extent.X : 0.f));
			Check(C.Lights > 0, FString::Printf(TEXT("%s: %d lights of the home's light group in it"), *Label, C.Lights));
			if (APawn* P = Pawn())
			{
				LookAt(P->GetPawnViewLocation() + FVector(1000.f, 0.f, 0.f));
			}
			return 0.4f;
		}));
		S.Add(Do(Label + TEXT(": shot"), [Index]()
		{
			Shot(FString::Printf(TEXT("Home_Corridor_%d"), Index));
			return 0.3f;
		}));
	}

	/** The door's delay after the last line said in the arrival (or the hatch closing, with no line): its Normal-time length. */
	static void CheckDoorDelay(const FString& Label, int32 FromLine, FName WantLast, bool bClean)
	{
		const APrologueHatch* Hatch = First<APrologueHatch>();
		const float Want = UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds();
		const FLine* Last = LastLineFrom(FromLine);
		Check(Hatch && Hatch->ExitDoor && Hatch->ExitDoor->IsOpen() && !Hatch->ExitDoor->IsTimed() && !GRun.bDoorOpenBeforeClosed,
			FString::Printf(TEXT("%s: the exit door is open, untimed, and was closed until now"), *Label));
		if (WantLast.IsNone())
		{
			const double Gap = GRun.DoorOpenedRun - GRun.HatchClosedRun;
			Check(!Last && FMath::Abs(Gap - Want) <= 0.1,
				FString::Printf(TEXT("%s: no line; the door opened %.3f s of the run's clock after the hatch had closed (ExitDoorDelaySeconds %.2f)"), *Label, Gap, Want));
		}
		else
		{
			const double Gap = Last && Last->TypedRun >= 0.0 ? GRun.DoorOpenedRun - Last->TypedRun : -1.0;
			Check(Last && Last->Row == WantLast && GRun.LinesAtDoor == GRun.Lines.Num() && Last->TypedRun >= 0.0 && Last->TypedRun < GRun.DoorOpenedRun,
				FString::Printf(TEXT("%s: the last line before the door is %s, typed out at %.3f s, the door at %.3f s (run clock)"), *Label,
					Last ? *Last->Row.ToString() : TEXT("none"), Last ? Last->TypedRun : -1.0, GRun.DoorOpenedRun));
			if (bClean)
			{
				Check(FMath::Abs(Gap - Want) <= 0.1, FString::Printf(TEXT("%s: the door opened %.3f s after %s was typed out (ExitDoorDelaySeconds %.2f)"), *Label, Gap, *WantLast.ToString(), Want));
			}
			else
			{
				Note(FString::Printf(TEXT("%s: the door opened %.3f s of the run's clock after %s was typed out, with a Stop in between"), *Label, Gap, *WantLast.ToString()));
			}
		}
		Check(Hatch && FMath::Abs(Hatch->GetDoorDelaySeconds() - Want) <= 0.1f && FMath::Abs(GRun.DelayNormalSeconds - Want) <= 0.25,
			FString::Printf(TEXT("%s: the delay was %.2f s of the run's Normal time, %.2f s of unfrozen Normal real time (ExitDoorDelaySeconds %.2f)"), *Label,
				Hatch ? Hatch->GetDoorDelaySeconds() : -1.f, GRun.DelayNormalSeconds, Want));
	}

	/** New game skipped with the groups in Without left out of the fixture, the home crossed by teleports, then the arrival's hatch closing in front of the player. */
	static void AddSkipToArrival(TArray<FStep>& S, const FString& Label, TArray<FName> Without)
	{
		S.Add(Do(Label + TEXT(": New game, skipped"), [Label, Without]()
		{
			NarrativeFixture::SetPrologueGroups(Without);
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), Label + TEXT(": New game clicked"));
			Check(SendKey(BackKey()) && TopScene() && TopScene()->WasSkipped(), Label + TEXT(": back skips the text"));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": in Prologue_Home"), []() { return InStage(1); }, 60.f, []()
		{
			Teleport(First<AAreaExit>()->GetActorLocation());
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": in Prologue_Arrival"), []() { return InStage(2); }, 90.f, []()
		{
			GRun.HatchClosedReal = GRun.DoorOpenedReal = GRun.HatchClosedRun = GRun.DoorOpenedRun = -1.0;
			GRun.DelayNormalSeconds = 0.0;
			GRun.bDoorOpenBeforeClosed = false;
			GRun.MapsAtMark = GRun.Lines.Num();
			Teleport(HatchView());
			return 0.f;
		}));
	}

	// --- The test ---

	static TArray<FStep> BuildTest()
	{
		TArray<FStep> S;

		S.Add(WaitFor(TEXT("setup: the main menu is shown and no transition runs"), []() { return InMenu(); }, 30.f, []()
		{
			NarrativeFixture::Install(W());
			Run()->ClearSave();
			Check(UTheLastShiftSettings::Get()->ArePrologueMapsOn(), TEXT("setup: the prologue's maps are on in the settings (bPrologueMaps, both maps set)"));
			GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = false;
			Note(TEXT("fixture installed, save cleared, the prologue's maps switched off for part C"));
			return 0.5f;
		}));

		// --- C: switched off, as before this brief ---
		S.Add(Do(TEXT("C: New game, the prologue's maps off"), []()
		{
			GRun.MapsAtMark = GRun.Maps.Num();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), TEXT("C: New game clicked"));
			Check(TopScene() != nullptr, TEXT("C: the text scene opens"));
			Check(SendKey(BackKey()) && TopScene() && TopScene()->WasSkipped(), TEXT("C: back skips it"));
			Check(Transitioning(), TEXT("C: the travel starts at once"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("C: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			bool bPrologueMap = false;
			FString Visited;
			for (int32 I = GRun.MapsAtMark; I < GRun.Maps.Num(); ++I)
			{
				bPrologueMap |= GRun.Maps[I].Contains(TEXT("Prologue_"));
				Visited += (Visited.IsEmpty() ? TEXT("") : TEXT(", ")) + GRun.Maps[I];
			}
			Check(!bPrologueMap, FString::Printf(TEXT("C: from the text scene straight to area 1 (maps: %s)"), *Visited));
			GRun.BaselineInput = InputSignature();
			TimeStateDebug::LogInputReport(W(), Name());
			Check(TimeStateDebug::BuildInputReport(W()).IsGameReady(), TEXT("C: game input in area 1 (the baseline)"));
			GRun.BaselineSnapshots = Run()->GetSnapshots().Num();
			if (GRun.BaselineSnapshots > 0)
			{
				GRun.BaselineSnapshot = Run()->GetSnapshots()[0];
			}
			Check(GRun.BaselineSnapshots == 1 && GRun.BaselineSnapshot.ActIndex == 1 && GRun.BaselineSnapshot.BatteryFraction == 1.0,
				FString::Printf(TEXT("C: one snapshot, act 1, battery %.4f, home %.0f s, banked %d (the baseline)"), GRun.BaselineSnapshot.BatteryFraction,
					GRun.BaselineSnapshot.HomeSecondsElapsed, GRun.BaselineSnapshot.BankedEnergy));
			GRun.bAreaFlashlightAvailable = Char() && Char()->IsFlashlightAvailable();
			GRun.bAreaFlashlightOn = Char() && Char()->IsFlashlightOn();
			Check(GRun.bAreaFlashlightAvailable, FString::Printf(TEXT("C: the flashlight in area 1 (the baseline): %s"), *FlashlightState()));
			GetMutableDefault<UTheLastShiftSettings>()->bPrologueMaps = GRun.bSavedMaps;
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("C"));

		// --- A: read through, walked ---
		S.Add(Do(TEXT("A: New game with leftovers of an earlier run, read through"), []()
		{
			Check(UTheLastShiftSettings::Get()->ArePrologueMapsOn(), TEXT("A: the prologue's maps on"));
			// An earlier run left a low battery and some energy: New game starts from a new run's state.
			Home()->RestoreState(0.35, 1000.0);
			Energy()->RestoreBankedEnergy(2);
			GRun.HashBefore = SaveHash();
			GRun.SnapshotsBefore = Run()->GetSnapshots().Num();
			GRun.SaveWritesBefore = Run()->GetSaveWrites();
			Note(FString::Printf(TEXT("A: before New game: battery 0.35, banked 2, %d snapshots, save %s"), GRun.SnapshotsBefore, *GRun.HashBefore));
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), TEXT("A: New game clicked"));
			Check(TopScene() && TopScene()->GetKind() == ERunSceneKind::Prologue, TEXT("A: the text scene opens"));
			GRun.MarkReal = 0.0;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the text scene read through with confirm"), []()
		{
			URunTextSceneWidget* Scene = TopScene();
			if (!Scene || Scene->IsFinished())
			{
				return Scene == nullptr || Scene->IsFinished();
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - GRun.MarkReal > 0.15)
			{
				GRun.MarkReal = Now;
				SendKey(EKeys::Enter);
			}
			return false;
		}, 60.f, []()
		{
			URunTextSceneWidget* Scene = TopScene();
			Check(Scene && Scene->IsFinished() && !Scene->WasSkipped(), TEXT("A: the text scene ends, read through"));
			Check(Transitioning(), TEXT("A: a travel starts"));
			Check(Run()->GetLastTravelLabel().IsEmpty(), TEXT("A: no line on the travel screen to the home"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: arrived in Prologue_Home"), []() { return InStage(1); }, 60.f, []()
		{
			Check(MapName().EndsWith(TEXT("Prologue_Home")), FString::Printf(TEXT("A: the map is %s"), *MapName()));
			CheckOutsideRun(TEXT("A: home"));
			CheckInput(TEXT("A: menu to home"));
			const APlayerStart* Start = First<APlayerStart>();
			const TArray<ADoorDevice*> Doors = All<ADoorDevice>();
			Check(Start && Doors.Num() == 2 && Start->GetActorLocation().X < Doors[0]->GetActorLocation().X,
				FString::Printf(TEXT("A: the player starts in the print room, before the first of %d doors"), Doors.Num()));
			CheckLightsLit(TEXT("A: home"));
			Check(Char() && !Char()->IsFlashlightAvailable() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: home: the flashlight is off and unavailable (%s)"), *FlashlightState()));
			if (Start)
			{
				LookAt(Start->GetActorLocation() + FVector(800.f, 0.f, 0.f));
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: home: the flashlight key"), []()
		{
			Check(PressPawnKey(TEXT("FlashlightAction")), TEXT("A: home: the flashlight key pressed"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: home: the flashlight stays off"), []()
		{
			Check(Char() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: home: the flashlight stays off after its key (%s)"), *FlashlightState()));
			Check(PressPawnKey(TEXT("FlashlightAction")), TEXT("A: home: the flashlight key pressed again"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: home: still off, the input as it was"), []()
		{
			Check(Char() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: home: still off after a second press (%s)"), *FlashlightState()));
			CheckInput(TEXT("A: home after the flashlight key (the action stays mapped)"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: Wake_1 is said in the print room"), []() { return RowSeen(TEXT("Wake_1")); }, 15.f, []()
		{
			Shot(TEXT("Home_1_PrintRoom"));
			return 0.f;
		}));
		AddStopResume(S, TEXT("A: home, print room"), true, FString());
		S.Add(Do(TEXT("A: home: the flashlight after a Stop"), []()
		{
			Check(Char() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: home: the flashlight is still off after a Stop and a Resume (%s)"), *FlashlightState()));
			return 0.f;
		}));
		AddTerminal(S, TEXT("A: terminal 1 (print room)"), 0, true);
		AddCorridor(S, 1, 0, PrologueGroups::Hologram);
		AddWalk(S, TEXT("A: into the monitor room"), []() { return RoomEntry(PrologueGroups::Hologram, 300.f); }, 60.f);
		S.Add(WaitFor(TEXT("A: the Hologram line on entering the monitor room"), []() { return RowSeen(TEXT("Hologram_1")); }, 30.f, []()
		{
			const APrologueHologram* Holo = First<APrologueHologram>();
			const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
			Check(Holo && Holo->IsShown(), TEXT("A: the hologram is shown in Normal time"));
			if (!Holo)
			{
				return -1.f;
			}
			const FBox Box = Holo->GetPointsBox();
			const FVector Size = Box.GetSize();
			Check(Holo->GetPointCount() == Tuning.Points, FString::Printf(TEXT("A: hologram points %d (setting %d)"), Holo->GetPointCount(), Tuning.Points));
			Check(FMath::Abs(Size.X - Tuning.Size) <= Tuning.PointSize * 2.f && FMath::Abs(Size.Y - Tuning.Size) <= Tuning.PointSize * 2.f && FMath::Abs(Size.Z - Tuning.Size) <= Tuning.PointSize * 2.f,
				FString::Printf(TEXT("A: hologram %.0f x %.0f x %.0f cm (setting %.0f)"), Size.X, Size.Y, Size.Z, Tuning.Size));
			FHitResult Floor;
			const FVector Centre = Box.GetCenter();
			const bool bFloor = W()->LineTraceSingleByChannel(Floor, Centre, Centre - FVector(0.f, 0.f, 2000.f), ECC_Visibility);
			Check(bFloor && FMath::IsNearlyEqual(Floor.Distance, 250.f, 15.f), FString::Printf(TEXT("A: its centre %.0f cm above the floor"), bFloor ? Floor.Distance : -1.f));
			Check(Holo->GetPointColor().Equals(UTheLastShiftSettings::GetScanColors().Scan, 0.001f),
				FString::Printf(TEXT("A: its colour %s is ScanColors.Scan of the settings"), *Holo->GetPointColor().ToString()));
			const UInstancedStaticMeshComponent* Points = Holo->GetPointsComponent();
			FHitResult Hit;
			const FVector A = Centre - FVector(Size.X * 0.5f + 50.f, 0.f, 0.f);
			const FVector B = Centre + FVector(Size.X * 0.5f + 50.f, 0.f, 0.f);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Pawn());
			const bool bHit = W()->LineTraceSingleByChannel(Hit, A, B, ECC_Visibility, Params) || W()->LineTraceSingleByChannel(Hit, A, B, ECC_Pawn, Params);
			Check(Points && Points->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !bHit,
				FString::Printf(TEXT("A: no collision: a trace through its centre hits %s"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing")));
			Check(Points && Points->bHiddenInSceneCapture && !Points->bRenderCustomDepth && !Points->CastShadow,
				TEXT("A: hidden in scene captures (the lidar's), no custom depth, no shadow"));
			LookAt(Centre);
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: the monitor room, the hologram"), []()
		{
			Shot(TEXT("Home_2_MonitorRoom_Hologram"));
			return 1.f;
		}));
		// A screenshot frame inflates the next frame's time: sampling starts a second after it.
		S.Add(Do(TEXT("A: frame time sampling"), []()
		{
			GRun.FrameMs.Reset();
			GRun.FrameSamplesWanted = 240;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: frame time with the hologram"), []() { return GRun.FrameSamplesWanted == 0; }, 20.f, []()
		{
			GRun.FrameMs.Sort();
			GRun.FrameOnMs = GRun.FrameMs[GRun.FrameMs.Num() / 2];
			First<APrologueHologram>()->SetEnabled(false);
			GRun.FrameMs.Reset();
			GRun.FrameSamplesWanted = 240;
			return 1.f;
		}));
		S.Add(WaitFor(TEXT("A: frame time without the hologram"), []() { return GRun.FrameSamplesWanted == 0; }, 20.f, []()
		{
			GRun.FrameMs.Sort();
			GRun.FrameOffMs = GRun.FrameMs[GRun.FrameMs.Num() / 2];
			First<APrologueHologram>()->SetEnabled(true);
			Note(FString::Printf(TEXT("A: frame time in the monitor room, median of 240 frames: %.2f ms with the hologram, %.2f ms without (%+.2f ms)"),
				GRun.FrameOnMs, GRun.FrameOffMs, GRun.FrameOnMs - GRun.FrameOffMs));
			Check(GRun.FrameOnMs > 0.f && GRun.FrameOffMs > 0.f, TEXT("A: frame times measured"));
			LookAt(First<APrologueHologram>()->GetPointsBox().GetCenter());
			return 0.3f;
		}));
		AddStopResume(S, TEXT("A: home, monitor room"), true, TEXT("Home_3_MonitorRoom_Scan"));
		AddTerminal(S, TEXT("A: terminal 2 (monitor room)"), 1, true);
		AddCorridor(S, 2, 1, PrologueGroups::Hangar);
		AddWalk(S, TEXT("A: into the hangar"), []() { return RoomEntry(PrologueGroups::Hangar, 400.f); }, 60.f);
		S.Add(WaitFor(TEXT("A: the Hangar line on entering the hangar"), []() { return RowSeen(TEXT("Hangar_1")); }, 30.f, []()
		{
			if (const AAreaExit* Exit = First<AAreaExit>())
			{
				LookAt(Exit->GetActorLocation());
			}
			return 0.4f;
		}));
		S.Add(Do(TEXT("A: the hangar and the ship"), []()
		{
			Shot(TEXT("Home_4_Hangar_Ship"));
			return 0.5f;
		}));
		AddWalk(S, TEXT("A: into the ship"), []() { return First<AAreaExit>()->GetActorLocation(); }, 60.f, 30.f);
		S.Add(WaitFor(TEXT("A: the ship starts the flight"), []() { return Transitioning(); }, 10.f, []()
		{
			GRun.bWalking = false;
			Note(FString::Printf(TEXT("A: walk from the start to the ship: %.2f s at walk speed %.0f cm/s (terminal stops left out)"), GRun.WalkSeconds,
				Cast<ACharacter>(Pawn()) ? Cast<ACharacter>(Pawn())->GetCharacterMovement()->MaxWalkSpeed : 0.f));
			Check(GRun.WalkSeconds < 45.0, FString::Printf(TEXT("A: the home map is walked in under 45 s (%.2f s)"), GRun.WalkSeconds));
			const FString Label = Run()->GetLastTravelLabel().ToString();
			Check(Label == NarrativeFixture::GetPrologueLineText(PrologueGroups::Flight, 1),
				FString::Printf(TEXT("A: the travel screen of the flight carries the first Flight row: \"%s\""), *Label));
			FString Order;
			for (const FLine& Line : GRun.Lines)
			{
				if (Line.Map.EndsWith(TEXT("Prologue_Home")))
				{
					Order += (Order.IsEmpty() ? TEXT("") : TEXT(" ")) + Line.Row.ToString();
				}
			}
			Check(Order == TEXT("Wake_1 Wake_2 Hologram_1 Hangar_1"), FString::Printf(TEXT("A: the home's lines, once each, in order: %s"), *Order));
			GRun.WalkSeconds = 0.0;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: arrived in Prologue_Arrival"), []() { return InStage(2); }, 60.f, []()
		{
			Check(MapName().EndsWith(TEXT("Prologue_Arrival")), FString::Printf(TEXT("A: the map is %s"), *MapName()));
			CheckOutsideRun(TEXT("A: arrival"));
			CheckInput(TEXT("A: home to arrival"));
			CheckLightsLit(TEXT("A: arrival (the ship's lights)"));
			int32 Total = 0;
			LitLights(&Total);
			Check(Total == 2, FString::Printf(TEXT("A: the arrival has the ship's two lights only (%d)"), Total));
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch && Hatch->GetHatchState() == EPrologueHatchState::Open && Hatch->IsInsideShip(Pawn()), TEXT("A: the hatch is open, the player inside the ship"));
			Check(Hatch && Hatch->ExitDoor && !Hatch->ExitDoor->IsOpen(), TEXT("A: the exit door is closed"));
			return 0.f;
		}));
		S.Add(Do(TEXT("A: arrival: the flashlight at the start"), []()
		{
			Check(Char() && Char()->IsFlashlightAvailable() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: arrival: the flashlight is available and off at the start (%s)"), *FlashlightState()));
			Check(PressPawnKey(TEXT("FlashlightAction")), TEXT("A: arrival: the flashlight key pressed"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: arrival: the flashlight on"), []()
		{
			Check(Char() && Char()->IsFlashlightOn(), FString::Printf(TEXT("A: arrival: its key switches it on (%s)"), *FlashlightState()));
			return 0.f;
		}));
		AddStopResume(S, TEXT("A: arrival, in the ship"), false, FString());
		S.Add(Do(TEXT("A: arrival: the flashlight after a Stop"), []()
		{
			Check(Char() && Char()->IsFlashlightOn(), FString::Printf(TEXT("A: arrival: back on after a Stop and a Resume, as in the areas (%s)"), *FlashlightState()));
			Check(PressPawnKey(TEXT("FlashlightAction")), TEXT("A: arrival: the flashlight key pressed again"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: arrival: the flashlight off"), []()
		{
			Check(Char() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("A: arrival: its key switches it off (%s)"), *FlashlightState()));
			return 0.f;
		}));
		S.Add(Do(TEXT("A: staying in the ship"), []() { return 2.f; }));
		S.Add(Do(TEXT("A: the hatch waits while the player is in the ship"), []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch && Hatch->GetHatchState() == EPrologueHatchState::Open, TEXT("A: the hatch is still open after 2 s and a Stop in the ship"));
			GRun.HatchNormalSeconds = 0.0;
			GRun.DelayNormalSeconds = 0.0;
			GRun.bDoorOpenBeforeClosed = false;
			GRun.HatchClosedReal = GRun.DoorOpenedReal = GRun.HatchClosedRun = GRun.DoorOpenedRun = -1.0;
			return 0.f;
		}));
		AddWalk(S, TEXT("A: out of the ship"), []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			const FBox Ship = Hatch->GetShipBox();
			return FVector(Ship.Max.X + 300.f, Ship.GetCenter().Y, 0.f);
		}, 60.f);
		S.Add(WaitFor(TEXT("A: leaving the ship starts the hatch"), []() { const APrologueHatch* H = First<APrologueHatch>(); return H && H->GetHatchState() == EPrologueHatchState::Closing; }, 3.f, []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(!Hatch->IsInsideShip(Pawn()), TEXT("A: the player is outside the ship"));
			// Beside the ship: from its open side the ship hides the hatch.
			Teleport(HatchView());
			LookAt(Hatch->GetActorLocation() + FVector(0.f, 0.f, 350.f));
			return 0.f;
		}));
		S.Add(Do(TEXT("A: the hatch closing"), []()
		{
			Note(FString::Printf(TEXT("A: hatch at %.3f closed when shot"), First<APrologueHatch>()->GetClosedFraction()));
			Shot(TEXT("Arrival_2_HatchClosing"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the hatch closing, past a third"), []() { return First<APrologueHatch>()->GetClosedFraction() >= 0.35f; }, 10.f, []()
		{
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("A: Stop key pressed while the hatch closes"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: Stopped: mark"), []()
		{
			Check(Time()->IsStopped(), TEXT("A: Stopped"));
			GRun.Fraction = First<APrologueHatch>()->GetClosedFraction();
			return 1.5f;
		}));
		S.Add(Do(TEXT("A: the hatch stands still while Stopped"), []()
		{
			const float Now = First<APrologueHatch>()->GetClosedFraction();
			Check(Now == GRun.Fraction, FString::Printf(TEXT("A: the hatch stood still for 1.5 s while Stopped (%.4f -> %.4f)"), GRun.Fraction, Now));
			Shot(TEXT("Arrival_3_HatchClosing_Scan"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: Resume"), []()
		{
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("A: Resume key pressed"));
			return 0.4f;
		}));
		S.Add(Do(TEXT("A: the pause menu"), []()
		{
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			Check(!Time()->IsStopped() && StdPC && StdPC->PauseGame(), TEXT("A: the pause menu opens while the hatch closes"));
			GRun.Fraction = First<APrologueHatch>()->GetClosedFraction();
			return 1.5f;
		}));
		S.Add(Do(TEXT("A: the hatch stands still behind the pause menu"), []()
		{
			const float Now = First<APrologueHatch>()->GetClosedFraction();
			Check(Now == GRun.Fraction, FString::Printf(TEXT("A: the hatch stood still for 1.5 s behind the pause menu (%.4f -> %.4f)"), GRun.Fraction, Now));
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			Check(StdPC && StdPC->ResumeGame(), TEXT("A: the pause menu closes"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the hatch closed"), []() { return First<APrologueHatch>()->GetHatchState() >= EPrologueHatchState::Closed; }, 15.f, []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			const float Want = UTheLastShiftSettings::Get()->GetHatchCloseSeconds();
			Check(FMath::Abs(Hatch->GetClosingSeconds() - Want) <= 0.1f && FMath::Abs(GRun.HatchNormalSeconds - Want) <= 0.25,
				FString::Printf(TEXT("A: closed in %.2f s of the run's Normal time, %.2f s of unfrozen Normal real time (HatchCloseSeconds %.2f)"),
					Hatch->GetClosingSeconds(), GRun.HatchNormalSeconds, Want));
			Check(!GRun.bDoorOpenBeforeClosed && Hatch->ExitDoor && (!Hatch->ExitDoor->IsOpen() || Hatch->GetHatchState() == EPrologueHatchState::DoorOpened),
				TEXT("A: the exit door stayed closed until the hatch had closed"));
			GRun.MapsAtMark = GRun.Lines.Num();
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: the hatch closed, shot"), []()
		{
			Shot(TEXT("Arrival_4_HatchClosed"));
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch->GetHatchState() == EPrologueHatchState::Closed && !Hatch->ExitDoor->IsOpen() && RowSeen(TEXT("Hatch_1")),
				FString::Printf(TEXT("A: the Hatch lines are said, the door waits for them (%s)"), *RowsFrom(0)));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the Door lines after the Hatch lines"), []() { return RowSeen(TEXT("Door_1"), GRun.MapsAtMark); }, 30.f, []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			const FLine* Hatch2 = nullptr;
			for (const FLine& Line : GRun.Lines)
			{
				Hatch2 = Line.Row == TEXT("Hatch_2") ? &Line : Hatch2;
			}
			Check(Hatch2 && Hatch2->TypedRun >= 0.0 && Hatch->GetHatchState() == EPrologueHatchState::DoorLines && !Hatch->ExitDoor->IsOpen(),
				FString::Printf(TEXT("A: the Door lines are asked for once Hatch_2 is typed out (at %.3f s); the door is still closed"), Hatch2 ? Hatch2->TypedRun : -1.0));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the door's delay starts"), []() { return First<APrologueHatch>()->GetHatchState() == EPrologueHatchState::DoorDelay; }, 30.f, []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			const FLine* Last = LastLineFrom(GRun.MapsAtMark);
			Check(Last && Last->Row == TEXT("Door_2") && Last->TypedRun >= 0.0 && !Hatch->ExitDoor->IsOpen(),
				FString::Printf(TEXT("A: the delay starts when the last Door row is typed out (%s), the door still closed"), Last ? *Last->Row.ToString() : TEXT("none")));
			return 0.6f;
		}));
		S.Add(Do(TEXT("A: the delay runs in Normal time, then Stop"), []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch->GetDoorDelaySeconds() > 0.4f && Hatch->GetHatchState() == EPrologueHatchState::DoorDelay && !Hatch->ExitDoor->IsOpen(),
				FString::Printf(TEXT("A: the delay runs in Normal time (%.3f s after 0.6 s), the door closed"), Hatch->GetDoorDelaySeconds()));
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("A: Stop key pressed during the door's delay"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("A: the delay, Stopped: mark"), []()
		{
			Check(Time()->IsStopped(), TEXT("A: Stopped during the delay"));
			GRun.DelayMark = First<APrologueHatch>()->GetDoorDelaySeconds();
			return 1.5f;
		}));
		S.Add(Do(TEXT("A: the delay stands still while Stopped"), []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch->GetDoorDelaySeconds() == GRun.DelayMark && Hatch->GetHatchState() == EPrologueHatchState::DoorDelay && !Hatch->ExitDoor->IsOpen(),
				FString::Printf(TEXT("A: the delay stood still for 1.5 s while Stopped (%.3f -> %.3f s), the door closed"), GRun.DelayMark, Hatch->GetDoorDelaySeconds()));
			Check(PressPawnKey(TEXT("TimeStopAction")), TEXT("A: Resume key pressed"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("A: the delay, the pause menu"), []()
		{
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			Check(!Time()->IsStopped() && StdPC && StdPC->PauseGame(), TEXT("A: the pause menu opens during the delay"));
			GRun.DelayMark = First<APrologueHatch>()->GetDoorDelaySeconds();
			return 1.5f;
		}));
		S.Add(Do(TEXT("A: the delay stands still behind the pause menu"), []()
		{
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Check(Hatch->GetDoorDelaySeconds() == GRun.DelayMark && Hatch->GetHatchState() == EPrologueHatchState::DoorDelay && !Hatch->ExitDoor->IsOpen(),
				FString::Printf(TEXT("A: the delay stood still for 1.5 s behind the pause menu (%.3f -> %.3f s), the door closed"), GRun.DelayMark, Hatch->GetDoorDelaySeconds()));
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			Check(StdPC && StdPC->ResumeGame(), TEXT("A: the pause menu closes"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: the exit door opens by itself"), []() { return First<APrologueHatch>()->GetHatchState() == EPrologueHatchState::DoorOpened; }, 30.f, []()
		{
			CheckDoorDelay(TEXT("A"), GRun.MapsAtMark, TEXT("Door_2"), false);
			FString Order;
			for (const FLine& Line : GRun.Lines)
			{
				if (Line.Map.EndsWith(TEXT("Prologue_Arrival")))
				{
					Order += (Order.IsEmpty() ? TEXT("") : TEXT(" ")) + Line.Row.ToString();
				}
			}
			Check(Order == TEXT("Hatch_1 Hatch_2 Door_1 Door_2"), FString::Printf(TEXT("A: the arrival's lines, once each, in order, all before the door opened: %s"), *Order));
			LookAt(First<APrologueHatch>()->ExitDoor->GetActorLocation() + FVector(0.f, 0.f, 150.f));
			return 0.6f;
		}));
		S.Add(Do(TEXT("A: the exit door open, shot"), []()
		{
			Shot(TEXT("Arrival_5_ExitDoorOpen"));
			return 0.5f;
		}));
		AddWalk(S, TEXT("A: through the exit door"), []() { return First<APrologueHatch>()->ExitDoor->GetActorLocation() + FVector(-150.f, 0.f, 0.f); }, 60.f);
		AddWalk(S, TEXT("A: to the area exit"), []() { return First<AAreaExit>()->GetActorLocation(); }, 60.f);
		S.Add(WaitFor(TEXT("A: the travel to area 1"), []() { return Transitioning(); }, 10.f, []()
		{
			GRun.bWalking = false;
			Note(FString::Printf(TEXT("A: walk in the arrival map from the ship to the exit: %.2f s"), GRun.WalkSeconds));
			Check(GRun.WalkSeconds < 45.0, FString::Printf(TEXT("A: the arrival map is walked in under 45 s (%.2f s)"), GRun.WalkSeconds));
			const FString Label = Run()->GetLastTravelLabel().ToString();
			Check(Label.Contains(TEXT("Fixture entry line of act 1")), FString::Printf(TEXT("A: the travel screen carries area 1's entry line, as New game: \"%s\""), *Label));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("A: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			CheckInput(TEXT("A: arrival to area 1"));
			const TArray<FAreaSnapshot>& Snaps = Run()->GetSnapshots();
			const bool bSame = Snaps.Num() == GRun.BaselineSnapshots && Snaps.Num() == 1 && Snaps[0].ActIndex == GRun.BaselineSnapshot.ActIndex
				&& Snaps[0].BatteryFraction == GRun.BaselineSnapshot.BatteryFraction && Snaps[0].HomeSecondsElapsed == GRun.BaselineSnapshot.HomeSecondsElapsed
				&& Snaps[0].BankedEnergy == GRun.BaselineSnapshot.BankedEnergy && Snaps[0].Archive.IsEmpty() && GRun.BaselineSnapshot.Archive.IsEmpty();
			Check(bSame, FString::Printf(TEXT("A: the first snapshot is the same as without the prologue's maps: %d snapshot(s), act %d, battery %.4f, home %.0f s, banked %d"),
				Snaps.Num(), Snaps.Num() > 0 ? Snaps[0].ActIndex : 0, Snaps.Num() > 0 ? Snaps[0].BatteryFraction : -1.0, Snaps.Num() > 0 ? Snaps[0].HomeSecondsElapsed : -1.0,
				Snaps.Num() > 0 ? Snaps[0].BankedEnergy : -1));
			Check(Home()->GetBatteryFraction() == 1.0 && Energy()->GetBankedEnergy() == 0 && Run()->GetArchive().IsEmpty(), TEXT("A: the run state in area 1 is a new run's"));
			Check(Char() && Char()->IsFlashlightAvailable() == GRun.bAreaFlashlightAvailable && Char()->IsFlashlightOn() == GRun.bAreaFlashlightOn,
				FString::Printf(TEXT("A: the flashlight in area 1 after the prologue is as without it (%s)"), *FlashlightState()));
			Check(Run()->GetSaveWrites() > GRun.SaveWritesBefore && Run()->DidLastSaveSucceed(),
				FString::Printf(TEXT("A: the save is written on entering area 1, not before (%d writes, %s)"), Run()->GetSaveWrites() - GRun.SaveWritesBefore, *SaveHash()));
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("A"));

		// --- B: skipped, the Hangar and Hatch groups with no row ---
		S.Add(Do(TEXT("B: New game, skipped"), []()
		{
			NarrativeFixture::SetPrologueGroups({ PrologueGroups::Hangar, PrologueGroups::Hatch });
			GRun.HashBefore = SaveHash();
			GRun.SnapshotsBefore = Run()->GetSnapshots().Num();
			GRun.SaveWritesBefore = Run()->GetSaveWrites();
			GRun.MapsAtMark = GRun.Lines.Num();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), TEXT("B: New game clicked"));
			SendKey(EKeys::Enter);
			Check(SendKey(BackKey()) && TopScene() && TopScene()->IsFinished() && TopScene()->WasSkipped(), TEXT("B: back skips the text"));
			Check(Transitioning(), TEXT("B: the travel starts"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: arrived in Prologue_Home after the skip"), []() { return InStage(1); }, 60.f, []()
		{
			CheckOutsideRun(TEXT("B: home"));
			CheckInput(TEXT("B: menu to home after a skip"));
			return 0.f;
		}));
		AddTerminal(S, TEXT("B: terminal 1"), 0, false);
		S.Add(Do(TEXT("B: into the monitor room"), []() { Teleport(RoomEntry(PrologueGroups::Hologram, 300.f)); return 0.5f; }));
		AddTerminal(S, TEXT("B: terminal 2"), 1, false);
		S.Add(Do(TEXT("B: into the hangar"), []()
		{
			GRun.MapsAtMark = GRun.Lines.Num();
			Teleport(RoomEntry(PrologueGroups::Hangar, 400.f));
			return 1.f;
		}));
		S.Add(Do(TEXT("B: the Hangar group has no row"), []()
		{
			const APrologueLineTrigger* Hangar = nullptr;
			for (const APrologueLineTrigger* T : All<APrologueLineTrigger>())
			{
				Hangar = T->Group == PrologueGroups::Hangar ? T : Hangar;
			}
			Check(Hangar && Hangar->HasFired() && HomeAI() && HomeAI()->HasSaidPrologueGroup(PrologueGroups::Hangar) && !RowSeen(TEXT("Hangar_1"), GRun.MapsAtMark),
				TEXT("B: entering the hangar asks for Hangar, which has no row: nothing is said"));
			Teleport(First<AAreaExit>()->GetActorLocation());
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: the flight starts"), []() { return Transitioning(); }, 10.f, []()
		{
			Check(Run()->GetLastTravelLabel().ToString() == NarrativeFixture::GetPrologueLineText(PrologueGroups::Flight, 1), TEXT("B: the flight's line"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: arrived in Prologue_Arrival"), []() { return InStage(2); }, 60.f, []()
		{
			CheckInput(TEXT("B: home to arrival"));
			GRun.HatchClosedReal = GRun.DoorOpenedReal = GRun.HatchClosedRun = GRun.DoorOpenedRun = -1.0;
			GRun.DelayNormalSeconds = 0.0;
			GRun.bDoorOpenBeforeClosed = false;
			GRun.MapsAtMark = GRun.Lines.Num();
			Check(Char() && Char()->IsFlashlightAvailable() && !Char()->IsFlashlightOn(), FString::Printf(TEXT("B: arrival: the flashlight is available and off (%s)"), *FlashlightState()));
			// Out of the ship and beside it in one move: the hatch is shot open, in the frame it starts to close.
			const APrologueHatch* Hatch = First<APrologueHatch>();
			Teleport(HatchView());
			LookAt(Hatch->GetActorLocation() + FVector(0.f, 0.f, 350.f));
			Note(FString::Printf(TEXT("B: hatch at %.3f closed when shot"), Hatch->GetClosedFraction()));
			Shot(TEXT("Arrival_1_HatchOpen"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: the hatch closes"), []() { return First<APrologueHatch>()->GetHatchState() >= EPrologueHatchState::Closed; }, 15.f));
		S.Add(WaitFor(TEXT("B: the Door lines"), []() { return RowSeen(TEXT("Door_1"), GRun.MapsAtMark); }, 10.f, []()
		{
			const double Gap = GRun.Lines.Last().Real - GRun.HatchClosedReal;
			Check(GRun.HatchClosedReal > 0.0 && Gap >= 0.0 && Gap < 0.2 && !RowSeen(TEXT("Hatch_1"), GRun.MapsAtMark),
				FString::Printf(TEXT("B: with no Hatch row the Door lines start right after the hatch has closed (%.3f s): an empty group blocks nothing"), Gap));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: the exit door opens"), []() { return First<APrologueHatch>()->GetHatchState() == EPrologueHatchState::DoorOpened; }, 30.f, []()
		{
			CheckDoorDelay(TEXT("B"), GRun.MapsAtMark, TEXT("Door_2"), true);
			Check(RowsFrom(GRun.MapsAtMark) == TEXT("Door_1 Door_2"), FString::Printf(TEXT("B: the arrival's lines: %s"), *RowsFrom(GRun.MapsAtMark)));
			Teleport(First<AAreaExit>()->GetActorLocation());
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: arrived in area 1"), []() { return InArea(1); }, 90.f, []()
		{
			CheckInput(TEXT("B: arrival to area 1"));
			Check(Run()->GetSnapshots().Num() == 1, TEXT("B: one snapshot (act 1)"));
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("B"));

		// --- D: Continue and Select act never pass through the prologue's maps ---
		S.Add(Do(TEXT("D: Continue"), []()
		{
			GRun.MapsAtMark = GRun.Maps.Num();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("D: Continue clicked"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("D: arrived in area 1 (Continue)"), []() { return InArea(1); }, 90.f, []()
		{
			bool bPrologue = false;
			for (int32 I = GRun.MapsAtMark; I < GRun.Maps.Num(); ++I)
			{
				bPrologue |= GRun.Maps[I].Contains(TEXT("Prologue_"));
			}
			Check(!bPrologue, TEXT("D: Continue loads no prologue map"));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("D"));
		S.Add(Do(TEXT("D: Select act, act 1"), []()
		{
			GRun.MapsAtMark = GRun.Maps.Num();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("D: Select act clicked"));
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), TEXT("D: act 1 chosen"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("D: arrived in area 1 (Select act)"), []() { return InArea(1); }, 90.f, []()
		{
			bool bPrologue = false;
			for (int32 I = GRun.MapsAtMark; I < GRun.Maps.Num(); ++I)
			{
				bPrologue |= GRun.Maps[I].Contains(TEXT("Prologue_"));
			}
			Check(!bPrologue, TEXT("D: Select act loads no prologue map"));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("D"));

		// --- E: a save of a run that differs from a new one; New game, then quit from inside the prologue; Continue ---
		S.Add(Do(TEXT("E: a saved run in act 2 (battery 0.55, home 5000 s, banked 3)"), []()
		{
			Home()->RestoreState(0.55, 5000.0);
			Energy()->RestoreBankedEnergy(3);
			return Run()->TravelToArea(2) ? 0.f : -1.f;
		}));
		S.Add(WaitFor(TEXT("E: arrived in area 2, its entry snapshot saved"), []() { return InArea(2); }, 90.f, []()
		{
			const TArray<FAreaSnapshot>& Snaps = Run()->GetSnapshots();
			Check(Snaps.Num() == 2 && Snaps.Last().ActIndex == 2 && FMath::IsNearlyEqual(Snaps.Last().BatteryFraction, 0.55) && Snaps.Last().BankedEnergy == 3,
				FString::Printf(TEXT("E: the save holds %d snapshots, the last act %d, battery %.4f, banked %d"), Snaps.Num(), Snaps.Num() > 0 ? Snaps.Last().ActIndex : 0,
					Snaps.Num() > 0 ? Snaps.Last().BatteryFraction : -1.0, Snaps.Num() > 0 ? Snaps.Last().BankedEnergy : -1));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("E"));
		S.Add(Do(TEXT("E: New game with that save, skipped"), []()
		{
			GRun.HashBefore = SaveHash();
			GRun.SnapshotsBefore = Run()->GetSnapshots().Num();
			GRun.SaveWritesBefore = Run()->GetSaveWrites();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false), TEXT("E: New game clicked"));
			Check(SendKey(BackKey()) && TopScene() && TopScene()->WasSkipped(), TEXT("E: back skips the text"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("E: arrived in Prologue_Home"), []() { return InStage(1); }, 60.f, []()
		{
			CheckOutsideRun(TEXT("E: home"));
			Note(TEXT("E: quitting to the main menu from inside Prologue_Home (what the pause menu's Main menu does)"));
			return Run()->ReturnToMainMenu() ? 0.5f : -1.f;
		}));
		S.Add(WaitFor(TEXT("E: in the main menu after quitting the prologue"), []() { return InMenu(); }, 60.f, []()
		{
			Check(SaveHash() == GRun.HashBefore && Run()->GetSaveWrites() == GRun.SaveWritesBefore && Run()->GetSnapshots().Num() == GRun.SnapshotsBefore,
				FString::Printf(TEXT("E: quitting inside the prologue saved and deleted nothing (%d snapshots, %d writes, %s)"), Run()->GetSnapshots().Num(),
					Run()->GetSaveWrites() - GRun.SaveWritesBefore, *SaveHash()));
			Check(Run()->HasSavedRun(), TEXT("E: the main menu still has a saved run"));
			GRun.MapsAtMark = GRun.Maps.Num();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("E: Continue clicked"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("E: Continue arrives in the saved run's act"), []() { return InArea(2); }, 90.f, []()
		{
			bool bPrologue = false;
			for (int32 I = GRun.MapsAtMark; I < GRun.Maps.Num(); ++I)
			{
				bPrologue |= GRun.Maps[I].Contains(TEXT("Prologue_"));
			}
			Check(!bPrologue, TEXT("E: Continue loads no prologue map"));
			Check(FMath::IsNearlyEqual(Home()->GetBatteryFraction(), 0.55) && FMath::IsNearlyEqual(Home()->GetHomeSecondsElapsed(), 5000.0) && Energy()->GetBankedEnergy() == 3,
				FString::Printf(TEXT("E: Continue restores the saved run, not the prologue's new one: act %d, battery %.4f, home %.0f s, banked %d"), Act(),
					Home()->GetBatteryFraction(), Home()->GetHomeSecondsElapsed(), Energy()->GetBankedEnergy()));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("E"));

		// --- F: no Door rows: the door opens the delay after the last Hatch row ---
		AddSkipToArrival(S, TEXT("F"), { PrologueGroups::Door });
		S.Add(WaitFor(TEXT("F: the exit door opens"), []() { return First<APrologueHatch>()->GetHatchState() == EPrologueHatchState::DoorOpened; }, 40.f, []()
		{
			CheckDoorDelay(TEXT("F"), GRun.MapsAtMark, TEXT("Hatch_2"), true);
			Check(RowsFrom(GRun.MapsAtMark) == TEXT("Hatch_1 Hatch_2"), FString::Printf(TEXT("F: no Door line, the arrival's lines: %s"), *RowsFrom(GRun.MapsAtMark)));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("F"));

		// --- G: neither group: the door opens the delay after the hatch has closed ---
		AddSkipToArrival(S, TEXT("G"), { PrologueGroups::Hatch, PrologueGroups::Door });
		S.Add(WaitFor(TEXT("G: the exit door opens"), []() { return First<APrologueHatch>()->GetHatchState() == EPrologueHatchState::DoorOpened; }, 40.f, []()
		{
			CheckDoorDelay(TEXT("G"), GRun.MapsAtMark, NAME_None, true);
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("G"));
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
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: run it in MainMenu (PIE or a package)."), Name());
			return;
		}
		GRun = FRun();
		GRun.bQuitAtEnd = Args.Contains(TEXT("Quit")) || Args.Contains(TEXT("quit"));
		GRun.bSavedMaps = UTheLastShiftSettings::Get()->bPrologueMaps;
		GRun.Steps = BuildTest();
		GRun.bRunning = true;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d steps, in %s."), Name(), GRun.Steps.Num(), *MapName());
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	static void Report(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		if (!World)
		{
			return;
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Prologue.Report: %s, prologue stage %d, act %d, prologue maps %s."), *MapName(), Stage(), Act(),
			UTheLastShiftSettings::Get()->ArePrologueMapsOn() ? TEXT("on") : TEXT("off"));
		for (const ADoorDevice* Door : All<ADoorDevice>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  door %s at %s, %s, size %s, powered %d timed %d"), *Door->GetName(), *Door->GetActorLocation().ToCompactString(),
				Door->IsOpen() ? TEXT("open") : TEXT("closed"), *Door->GetDoorSize().ToCompactString(), Door->IsPowered() ? 1 : 0, Door->IsTimed() ? 1 : 0);
		}
		for (const APowerTerminal* Terminal : All<APowerTerminal>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  terminal %s at %s, cost %d, %d targets (%s)"), *Terminal->GetName(), *Terminal->GetActorLocation().ToCompactString(),
				Terminal->GetCost(), Terminal->Targets.Num(), Terminal->Targets.Num() > 0 ? *GetNameSafe(Terminal->Targets[0]) : TEXT("-"));
		}
		for (const ALightGroupDevice* Group : All<ALightGroupDevice>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  light group %s: %d of %d lit, powered at start %d, powered %d"), *Group->GetName(), Group->GetLitCount(), Group->Lights.Num(),
				Group->bPoweredAtStart ? 1 : 0, Group->IsPowered() ? 1 : 0);
		}
		for (const APrologueLineTrigger* Trigger : All<APrologueLineTrigger>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  line trigger %s: group %s at %s, extent %s, fired %d"), *Trigger->GetName(), *Trigger->Group.ToString(),
				*Trigger->GetActorLocation().ToCompactString(), *Trigger->Extent.ToCompactString(), Trigger->HasFired() ? 1 : 0);
		}
		for (const APrologueHatch* Hatch : All<APrologueHatch>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  hatch %s at %s: %s, closed %.3f, opening %s, ship box %s, exit door %s"), *Hatch->GetName(), *Hatch->GetActorLocation().ToCompactString(),
				*StaticEnum<EPrologueHatchState>()->GetNameStringByValue(static_cast<int64>(Hatch->GetHatchState())), Hatch->GetClosedFraction(),
				*Hatch->OpeningSize.ToCompactString(), *Hatch->GetShipBox().ToString(), *GetNameSafe(Hatch->ExitDoor));
		}
		for (const APrologueHologram* Holo : All<APrologueHologram>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  hologram %s: %d points, box %s, colour %s, shown %d"), *Holo->GetName(), Holo->GetPointCount(), *Holo->GetPointsBox().ToString(),
				*Holo->GetPointColor().ToString(), Holo->IsShown() ? 1 : 0);
		}
		for (const AAreaExit* Exit : All<AAreaExit>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  exit %s at %s"), *Exit->GetName(), *Exit->GetActorLocation().ToCompactString());
		}
		if (const APlayerStart* Start = First<APlayerStart>())
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  player start at %s, yaw %.0f"), *Start->GetActorLocation().ToCompactString(), Start->GetActorRotation().Yaw);
		}
		for (int32 Index = 1; Index <= 2; ++Index)
		{
			const FCorridor C = MeasureCorridor(Index);
			if (C.bFound)
			{
				UE_LOG(LogTheLastShift, Log, TEXT("  corridor %d: %d pieces, X %.0f..%.0f, %.0f cm long, %.0f cm wide, %.0f cm high, %d lights"), Index, C.Pieces, C.StartX, C.EndX,
					C.Length, C.Width, C.Height, C.Lights);
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("  flashlight: %s; exit door delay %.2f s"), *FlashlightState(), UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds());
	}

	static FAutoConsoleCommandWithWorldAndArgs GPrologueTestCmd(
		TEXT("tls.PrologueTest"),
		TEXT("Brief 11: New game through the text scene, Prologue_Home, the flight, Prologue_Arrival and into area 1 (read, skipped, switched off), Continue and Select act, quit inside the prologue, the exit door without Door or Hatch rows. Run in MainMenu. Args: [Quit]. Clears the save."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTest));

	static FAutoConsoleCommandWithWorldAndArgs GPrologueReportCmd(
		TEXT("tls.Prologue.Report"),
		TEXT("Logs the prologue map's doors, terminals, lights, line triggers, hatch, hologram, exit, corridors and flashlight."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Report));
}

#endif // !UE_BUILD_SHIPPING
