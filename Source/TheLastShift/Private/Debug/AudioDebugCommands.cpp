// SPDX-License-Identifier: MIT

// Brief 24, the game's minimal audio. The tests check what the audio was asked to play (its record: the slot, how many
// times, where, at which pitch), never what a sound is.
//
// tls.AudioTest local [Quit] (an area map, Area1_Hull; PIE or a package): stopped time (the stop sound, the scan sound at
// the automatic first scan and at a manual one, the ready signal once at the end of a cooldown and never before the first
// scan, nothing for a press during the cooldown, no ready signal after a Stop that ended before its cooldown); a residue
// collected; the text channel's three voices, a restart after a Stop and after the pause menu, a log on its terminal's
// surface; the pause menu (nothing asked while it is open, the run's sounds after it); a terminal of each kind of
// payment, a free one, reading a log again and a refusal; the area's gate door and a spawned door opening and closing,
// each a timed motion (Brief 46);
// an empty slot; the commands; then every exit from stopped time (a Resume, a contact with each response, a graze).
// tls.AudioTest menu [Quit] (MainMenu; PIE or a package; clears the save slot): the menu's music (once, looping, again
// after a travel back), the text scenes' lines and their interruption, New game (the music fades as the opening scene
// opens), the prologue's terminals, doors and hatches, the arrival's hatch and exit door, area 1 (no music), Continue and
// the act selection (the music fades), the ending (no music under it, back after it).
// tls.Audio.Report logs the slots and the record; tls.Audio.Play <Slot> plays a slot; tls.Audio.Set changes a slot, the
// hatches' pitch, the falloff and the music's fades for the session. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Debug/EntityFormTestScope.h"
#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/StdMusicSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TextTestListener.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Prologue/PrologueBriefingTerminal.h"
#include "Prologue/PrologueDeparture.h"
#include "Prologue/PrologueEntryDoor.h"
#include "Prologue/PrologueHatch.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Residue/Residue.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Settings/StdProjectSettings.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdMainMenuWidget.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace AudioDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Session values (tls.Audio.Set): the settings' values before the first change, to put back ---

	static TOptional<FGameAudio> GSavedAudio;

	static FGameAudio& MutableAudio()
	{
		FGameAudio& Audio = GetMutableDefault<UTheLastShiftSettings>()->Audio;
		if (!GSavedAudio.IsSet())
		{
			GSavedAudio = Audio;
		}
		return Audio;
	}

	static void RestoreAudio()
	{
		if (GSavedAudio.IsSet())
		{
			GetMutableDefault<UTheLastShiftSettings>()->Audio = GSavedAudio.GetValue();
			GSavedAudio.Reset();
		}
	}

	static bool ParseSound(const FString& Name, EGameSound& Out)
	{
		for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
		{
			if (Name.Equals(LexToString(static_cast<EGameSound>(I)), ESearchCase::IgnoreCase))
			{
				Out = static_cast<EGameSound>(I);
				return true;
			}
		}
		return false;
	}

	static FString DescribeRequest(const FGameSoundRequest& R)
	{
		return FString::Printf(TEXT("%s%s%s pitch %.2f vol %.2f%s [%s] (%s)"), LexToString(R.Sound), R.bStop ? TEXT(" stop") : TEXT(""),
			R.bInWorld ? *FString::Printf(TEXT(" at %s"), *R.Location.ToCompactString()) : TEXT(" (suit)"), R.Pitch, R.Volume, R.bHadSound ? TEXT("") : TEXT(" EMPTY"),
			*R.Source, *R.Map);
	}

	// --- The test runner ---

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
		FString Part;
		FTSTicker::FDelegateHandle Ticker;

		int32 Mark = 0;
		/** Where the record stood when the pause menu last opened (the menu's own mark, apart from the steps' Mark). */
		int32 MenuMark = 0;
		int32 ListenerMark = 0;
		TStrongObjectPtr<UTlsTextTestListener> Text;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TMap<EPaymentKind, TWeakObjectPtr<APowerTerminal>> Terminals;
		TWeakObjectPtr<APowerTerminal> FreeTerminal;
		TWeakObjectPtr<APowerTerminal> RefusingTerminal;
		TWeakObjectPtr<AResidue> Residue;
		TWeakObjectPtr<ADoorDevice> GateDoor;
		TWeakObjectPtr<APowerTerminal> GateTerminal;
		int32 SavedBanked = 0;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		float SavedGraze = 400.f;
		float SavedThreshold = 18.f;
		FVector StartLocation = FVector::ZeroVector;
		float SavedSpeed = 66.6667f;
		EEntityContactResponse SavedResponse = EEntityContactResponse::Rewind;
		bool bEntitySaved = false;
		bool bMenuWasOpen = false;
		int32 RequestsWhileMenu = 0;
		int32 ErrorsDuringEmpty = 0;
		int32 LinesBeforeScene = 0;
		double MotionStartReal = 0.0;
		double MotionEndReal = 0.0;
		TWeakObjectPtr<APrologueSlidingPanel> Panel;
		TWeakObjectPtr<UAudioComponent> FirstMotionSound;
		TWeakObjectPtr<UAudioComponent> SecondMotionSound;
	};
	static FRun GRun;

	static const TCHAR* Name() { return TEXT("tls.AudioTest"); }

	/** Counts warnings and errors logged while a slot is empty (criterion 7: an empty slot is no error). */
	struct FCountWarnings : public FOutputDevice
	{
		int32 Count = 0;
		bool bAdded = false;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			Count += Verbosity <= ELogVerbosity::Warning && Category != TEXT("LogTheLastShift") ? 1 : 0;
		}
	};
	static FCountWarnings GWarnings;

	static void StopCountingWarnings()
	{
		if (GWarnings.bAdded)
		{
			GLog->RemoveOutputDevice(&GWarnings);
			GWarnings.bAdded = false;
		}
	}

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
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UTextChannelComponent* Channel() { return Pawn() ? Pawn()->FindComponentByClass<UTextChannelComponent>() : nullptr; }
	static UHomeAIComponent* HomeAI() { return Pawn() ? Pawn()->FindComponentByClass<UHomeAIComponent>() : nullptr; }
	static UScanDirectorComponent* Director() { return Pawn() ? Pawn()->FindComponentByClass<UScanDirectorComponent>() : nullptr; }
	static UGameAudioSubsystem* Audio() { return UGameAudioSubsystem::Get(W()); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static URunSceneSubsystem* Scenes() { return URunSceneSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static FString MapName() { const UWorld* World = W(); return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString(); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }
	static int32 Stage() { return URunSubsystem::GetPrologueStageOf(W()); }

	static UStdFlowSubsystem* Flow()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}
	static bool Transitioning() { const UStdFlowSubsystem* F = Flow(); return F && F->IsTransitioning(); }

	static UStdMusicSubsystem* Music()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdMusicSubsystem>() : nullptr;
	}

	static UStdMainMenuWidget* MainMenu() { return Cast<UStdMainMenuWidget>(TimeStateDebug::TopScreen(W())); }
	static bool InMenu() { return W() && MapName().EndsWith(TEXT("/MainMenu")) && MainMenu() && !Transitioning(); }
	static URunTextSceneWidget* TopScene() { return Cast<URunTextSceneWidget>(TimeStateDebug::TopScreen(W())); }

	template <typename T>
	static T* First()
	{
		UWorld* World = W();
		for (TActorIterator<T> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	template <typename T>
	static TArray<T*> All()
	{
		TArray<T*> Found;
		UWorld* World = W();
		for (TActorIterator<T> It(World); World && It; ++It)
		{
			Found.Add(*It);
		}
		return Found;
	}

	static AEntity* Entity() { return First<AEntity>(); }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static void StopTime() { if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped()) { T->Stop(); } }
	static void ResumeTime() { if (UTimeStateSubsystem* T = TS(); T && T->IsStopped()) { T->Resume(); } }

	static void Teleport(const FVector& Where, bool bKeepZ = true)
	{
		if (APawn* P = Pawn())
		{
			P->SetActorLocation(bKeepZ ? FVector(Where.X, Where.Y, P->GetActorLocation().Z) : Where, false, nullptr, ETeleportType::TeleportPhysics);
			if (ACharacter* C = Cast<ACharacter>(P))
			{
				C->GetCharacterMovement()->StopMovementImmediately();
			}
		}
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

	// --- The record ---

	static void MarkRecord() { GRun.Mark = Audio() ? Audio()->GetRecordEnd() : 0; GRun.ListenerMark = GRun.Text ? GRun.Text->Started.Num() : 0; }

	static TArray<FGameSoundRequest> Since(int32 From = -1)
	{
		TArray<FGameSoundRequest> Out;
		const UGameAudioSubsystem* A = Audio();
		if (!A)
		{
			return Out;
		}
		for (int32 I = From < 0 ? GRun.Mark : From; I < A->GetRecordEnd(); ++I)
		{
			if (const FGameSoundRequest* R = A->GetRequest(I))
			{
				Out.Add(*R);
			}
		}
		return Out;
	}

	static TArray<FGameSoundRequest> SinceOf(EGameSound Sound, bool bWithStops = false)
	{
		TArray<FGameSoundRequest> Out;
		for (const FGameSoundRequest& R : Since())
		{
			if (R.Sound == Sound && (bWithStops || !R.bStop))
			{
				Out.Add(R);
			}
		}
		return Out;
	}

	static int32 Count(EGameSound Sound) { return SinceOf(Sound).Num(); }

	static FString Describe(const TArray<FGameSoundRequest>& List)
	{
		FString Out;
		for (const FGameSoundRequest& R : List)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT("; ")) + DescribeRequest(R);
		}
		return Out.IsEmpty() ? FString(TEXT("none")) : Out;
	}

	/** Every request since the mark is one of Allowed (and nothing else was asked). */
	static bool OnlyOf(std::initializer_list<EGameSound> Allowed)
	{
		for (const FGameSoundRequest& R : Since())
		{
			bool bOk = false;
			for (const EGameSound S : Allowed)
			{
				bOk |= R.Sound == S;
			}
			if (!bOk)
			{
				return false;
			}
		}
		return true;
	}

	static bool Near(const FVector& A, const FVector& B) { return FVector::Dist(A, B) < 1.0; }

	// --- Finish ---

	/** Brief 30: this run switched the entity's walls off (EntityWallsTest), to put back at its end. */
	static bool GWallsOff = false;

	static void RestoreStaticValues();

	static void Finish(const TCHAR* Why)
	{
		// Brief 35: the static's values back to the settings', when a case set them for the session.
		RestoreStaticValues();
		// Brief 30: the walls go back to the session's switch.
		if (GWallsOff)
		{
			EntityWallsTest::Restore(TEXT("tls.AudioTest"));
			GWallsOff = false;
		}
		GRun.bRunning = false;
		StopCountingWarnings();
		for (const TWeakObjectPtr<AActor>& Actor : GRun.Spawned)
		{
			if (AActor* A = Actor.Get())
			{
				A->Destroy();
			}
		}
		GRun.Spawned.Reset();
		if (AEntity* E = Entity(); E && GRun.bEntitySaved)
		{
			E->GrazeRadius = GRun.SavedGraze;
			E->Speed = GRun.SavedSpeed;
			E->AwakenThresholdSeconds = GRun.SavedThreshold;
			E->ContactResponse = GRun.SavedResponse;
		}
		if (GRun.Part == TEXT("local"))
		{
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(GRun.SavedBanked);
			}
			if (UHomeStateSubsystem* H = Home())
			{
				H->RestoreState(GRun.SavedBattery, GRun.SavedHome);
			}
		}
		if (AStdPlayerController* C = StdPC(); C && C->IsPausedByMenu())
		{
			C->ResumeGame();
		}
		if (GRun.Text)
		{
			GRun.Text->Unbind();
		}
		RestoreAudio();
		if (UWorld* World = W())
		{
			NarrativeFixture::Restore(World);
		}
		if (GRun.Part == TEXT("menu"))
		{
			if (URunSubsystem* R = Run())
			{
				R->ClearSave();
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s %s summary (%s): %d checks passed, %d failed%s."), Name(), *GRun.Part,
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
		// Criterion 8: while the pause menu is open, nothing of the run asks for a sound.
		if (const AStdPlayerController* C = StdPC(); C && C->IsPausedByMenu())
		{
			if (!GRun.bMenuWasOpen)
			{
				GRun.bMenuWasOpen = true;
				GRun.MenuMark = Audio() ? Audio()->GetRecordEnd() : 0;
			}
			GRun.RequestsWhileMenu = Since(GRun.MenuMark).Num();
		}
		else
		{
			GRun.bMenuWasOpen = false;
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

	static void BindText()
	{
		if (!GRun.Text.IsValid())
		{
			GRun.Text.Reset(NewObject<UTlsTextTestListener>());
		}
		GRun.Text->bUseRealClock = true;
		GRun.Text->Bind(Channel());
	}

	static FStep WaitIdle(const FString& Label, float Timeout = 120.f)
	{
		return WaitFor(Label + TEXT(": the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, Timeout);
	}

	// ================================================================================================================
	// Part local (an area map)
	// ================================================================================================================

	/** A terminal spawned 3 m from the player at Angle degrees, in reach, its device 30 m below. */
	static APowerTerminal* SpawnTerminal(EPaymentKind Kind, float Angle, int32 Cost = 1)
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
		T->Cost = Cost;
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
			if (ABeaconBody* Body = World->SpawnActor<ABeaconBody>(ABeaconBody::StaticClass(), Far + FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, Params))
			{
				Body->Device = Cast<ALightGroupDevice>(Device);
				GRun.Spawned.Add(Body);
			}
		}
		return T;
	}

	static ADoorDevice* DoorOf(const APowerTerminal* T)
	{
		return T && T->Targets.Num() > 0 ? Cast<ADoorDevice>(T->Targets[0]) : nullptr;
	}

	/** One terminal sound since the mark, at T's own position, pitch 1. */
	static void CheckTerminalSound(const FString& Label, const APowerTerminal* T, int32 Want = 1)
	{
		const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Terminal);
		bool bOk = List.Num() == Want;
		for (const FGameSoundRequest& R : List)
		{
			bOk &= T && R.bInWorld && Near(R.Location, T->GetActorLocation()) && FMath::IsNearlyEqual(R.Pitch, 1.f) && R.bHadSound;
		}
		Check(bOk, FString::Printf(TEXT("%s: the terminal sound %d time(s) at %s, pitch 1: %s"), *Label, Want, T ? *T->GetActorLocation().ToCompactString() : TEXT("-"), *Describe(List)));
	}

	/**
	 * Motion sounds since the mark (Brief 46: every door moves over time, the Door slot is gone): exactly Movers, in order,
	 * each at its mover's position and the pitch of its kind.
	 */
	static void CheckDoorSounds(const FString& Label, const TArray<TPair<const AActor*, bool>>& Movers, EGameSound Slot = EGameSound::Motion)
	{
		const TArray<FGameSoundRequest> List = SinceOf(Slot);
		const float Hatch = UTheLastShiftSettings::Get()->Audio.GetHatchPitch();
		bool bOk = List.Num() == Movers.Num();
		FString Want;
		for (int32 I = 0; I < Movers.Num(); ++I)
		{
			const AActor* M = Movers[I].Key;
			const float Pitch = M && M->IsA<ADoorDevice>() ? 1.f : Hatch;
			Want += FString::Printf(TEXT("%s%s %s pitch %.2f"), Want.IsEmpty() ? TEXT("") : TEXT(", "), *GetNameSafe(M), Movers[I].Value ? TEXT("opens") : TEXT("closes"), Pitch);
			if (List.IsValidIndex(I))
			{
				const FGameSoundRequest& R = List[I];
				bOk &= M && R.bInWorld && Near(R.Location, M->GetActorLocation()) && FMath::IsNearlyEqual(R.Pitch, Pitch, 1.e-3f)
					&& R.Source.EndsWith(Movers[I].Value ? TEXT("opens") : TEXT("closes")) && R.Source.StartsWith(M->GetName());
			}
		}
		Check(bOk, FString::Printf(TEXT("%s: the %s sound for %s: %s"), *Label, Slot == EGameSound::Motion ? TEXT("motion's") : TEXT("door"), Want.IsEmpty() ? TEXT("nothing") : *Want,
			*Describe(List)));
	}

	/** Motion requests (not stops) since the mark whose source names Mover. */
	static int32 DoorStartsOf(const AActor* Mover, EGameSound Slot = EGameSound::Motion)
	{
		int32 N = 0;
		for (const FGameSoundRequest& R : SinceOf(Slot))
		{
			N += Mover && R.Source.StartsWith(Mover->GetName() + TEXT(" ")) ? 1 : 0;
		}
		return N;
	}

	/** Motion stops since the mark whose source names Mover. */
	static TArray<FGameSoundRequest> DoorStopsOf(const AActor* Mover, EGameSound Slot = EGameSound::Motion)
	{
		TArray<FGameSoundRequest> Out;
		for (const FGameSoundRequest& R : SinceOf(Slot, true))
		{
			if (R.bStop && Mover && R.Source.StartsWith(Mover->GetName() + TEXT(" ")))
			{
				Out.Add(R);
			}
		}
		return Out;
	}

	/** Brief 46: a door's motion as AddTimedMotion reads it: its share of the travel opening (bOpening) or closing. */
	static float DoorProgress(const ADoorDevice* D, bool bOpening)
	{
		if (!D)
		{
			return 0.f;
		}
		if (bOpening)
		{
			return D->GetMotionDirection() > 0 || D->IsFullyOpen() ? FMath::Max(D->GetOpenFraction(), 0.01f) : 0.f;
		}
		return D->GetMotionDirection() < 0 || !D->IsOpen() ? FMath::Max(1.f - D->GetOpenFraction(), 0.01f) : 0.f;
	}

	/** Brief 46: a door's motion ended: fully open (bOpening) or shut. */
	static bool DoorEnded(const ADoorDevice* D, bool bOpening)
	{
		return D && !D->IsMoving() && (bOpening ? D->IsFullyOpen() : !D->IsOpen());
	}

	/** Brief 45: Component plays a sound wave that loops (one sound for as long as it is held, never asked for again). */
	static bool IsLooping(const UAudioComponent* Component)
	{
		const USoundWave* Wave = Component ? Cast<USoundWave>(Component->Sound) : nullptr;
		return Wave && Wave->bLooping;
	}

	static FString DescribeLoop(const UAudioComponent* Component)
	{
		const USoundWave* Wave = Component ? Cast<USoundWave>(Component->Sound) : nullptr;
		return Wave ? FString::Printf(TEXT("%s, %.2f s%s"), *Wave->GetName(), Wave->Duration, Wave->bLooping ? TEXT(", looping") : TEXT(", NOT looping")) : FString(TEXT("no sound"));
	}

	/**
	 * A timed motion of Mover (Progress 0 to 1, Ended at its end, about Seconds long): Brief 45, the motion's sound (its own
	 * slot, a file made to loop) asked for once at the start at the pitch of its kind, one looping sound, the same one still
	 * playing at half the motion with no second request, and stopped within the fade after the end (Brief 46: every door's
	 * motion too; the door's short sound is gone).
	 */
	static void AddTimedMotion(TArray<FStep>& S, const FString& Label, TFunction<AActor*()> Mover, TFunction<float()> Progress, TFunction<bool()> Ended, float Seconds)
	{
		S.Add(WaitFor(Label + TEXT(": the motion starts"), [Progress]() { return Progress() > 0.f; }, Seconds + 15.f, [Label, Mover]()
		{
			GRun.MotionStartReal = Real();
			UAudioComponent* Component = Audio()->GetMotionSound(Mover());
			GRun.FirstMotionSound = Component;
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Motion);
			const float Pitch = Mover() && Mover()->IsA<ADoorDevice>() ? 1.f : UTheLastShiftSettings::Get()->Audio.GetHatchPitch();
			bool bPitch = false;
			for (const FGameSoundRequest& R : List)
			{
				bPitch |= Mover() && R.Source.StartsWith(Mover()->GetName() + TEXT(" ")) && FMath::IsNearlyEqual(R.Pitch, Pitch, 1.e-3f) && R.bHadSound;
			}
			Check(DoorStartsOf(Mover(), EGameSound::Motion) == 1 && bPitch && Audio()->IsMotionSoundPlaying(Mover()) && IsLooping(Component)
				&& Audio()->GetMotionPlays(Mover()) == 1,
				FString::Printf(TEXT("%s: the motion's sound asked for once at the start at pitch %.2f, one looping sound, playing (%s; played %d): %s"), *Label, Pitch,
					*DescribeLoop(Component), Audio()->GetMotionPlays(Mover()), *Describe(List)));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": half the motion"), [Progress]() { return Progress() >= 0.5f; }, Seconds + 5.f, [Label, Mover]()
		{
			const UAudioComponent* Component = Audio()->GetMotionSound(Mover());
			Check(Audio()->IsMotionSoundPlaying(Mover()) && Component && Component == GRun.FirstMotionSound.Get() && DoorStartsOf(Mover(), EGameSound::Motion) == 1
				&& DoorStopsOf(Mover(), EGameSound::Motion).Num() == 0 && Audio()->GetMotionPlays(Mover()) == 1,
				FString::Printf(TEXT("%s: at half the motion (%.2f s) the same sound still plays, no second request, not stopped (played %d time(s))"), *Label,
					Real() - GRun.MotionStartReal, Audio()->GetMotionPlays(Mover())));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the motion ends"), [Ended]() { return Ended(); }, Seconds + 5.f, []()
		{
			GRun.MotionEndReal = Real();
			return UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds() + 0.15f;
		}));
		S.Add(Do(Label + TEXT(": after the fade"), [Label, Mover, Seconds]()
		{
			const TArray<FGameSoundRequest> Stops = DoorStopsOf(Mover(), EGameSound::Motion);
			const float Fade = UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds();
			double Started = -1.0;
			for (const FGameSoundRequest& R : SinceOf(EGameSound::Motion))
			{
				Started = Mover() && R.Source.StartsWith(Mover()->GetName() + TEXT(" ")) ? R.RealSeconds : Started;
			}
			const double Held = Stops.Num() > 0 && Started >= 0.0 ? Stops[0].RealSeconds - Started : -1.0;
			const UAudioComponent* First = GRun.FirstMotionSound.Get();
			Check(!Audio()->IsMotionSoundPlaying(Mover()) && (!First || !First->IsPlaying()) && Stops.Num() == 1 && Stops[0].Source.Contains(TEXT("motion ends"))
				&& DoorStartsOf(Mover(), EGameSound::Motion) == 1 && Audio()->GetMotionPlays(Mover()) == 1 && FMath::Abs(Held - Seconds) <= 0.25,
				FString::Printf(TEXT("%s: held from the motion's start to its end %.2f s (the motion: %.1f s), played once; %.2f s after the end (fade %.2f s) the sound has stopped: %s"),
					*Label, Held, Seconds, Real() - GRun.MotionEndReal, Fade, *Describe(Stops)));
			return 0.f;
		}));
	}

	/** Exactly one exit sound since the mark: Want (TimeResume or Slip). */
	static void CheckExit(const FString& Label, EGameSound Want)
	{
		const int32 Resume = Count(EGameSound::TimeResume);
		const int32 Slip = Count(EGameSound::Slip);
		Check((Want == EGameSound::TimeResume ? Resume == 1 && Slip == 0 : Slip == 1 && Resume == 0) && Count(EGameSound::TimeStop) == 1,
			FString::Printf(TEXT("%s: one stop sound and exactly one exit sound, %s (resume %d, slip %d): %s"), *Label, LexToString(Want), Resume, Slip,
				*Describe(Since())));
	}

	static void AddLocal(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("setup: an area with the player, its lidar, its text channel and an entity"), []()
		{
			return W() && Act() > 0 && AAreaGenerator::IsAreaReady(W()) && Pawn() && Director() && Channel() && HomeAI() && Entity() && Audio() && !Transitioning();
		}, 90.f, []()
		{
			NarrativeFixture::Install(W());
			BindText();
			GRun.SavedBanked = Energy()->GetBankedEnergy();
			GRun.SavedBattery = Home()->GetBatteryFraction();
			GRun.SavedHome = Home()->GetHomeSecondsElapsed();
			AEntity* E = Entity();
			GRun.SavedGraze = E->GrazeRadius;
			GRun.SavedSpeed = E->Speed;
			GRun.SavedResponse = E->ContactResponse;
			GRun.SavedThreshold = E->AwakenThresholdSeconds;
			GRun.bEntitySaved = true;
			// Dormant until the exits' part awakens it (SpawnNow): the Stops before it must not meet it.
			E->AwakenThresholdSeconds = 100000.f;
			GRun.StartLocation = Pawn()->GetActorLocation();
			const FGameAudio& A = UTheLastShiftSettings::Get()->Audio;
			bool bAll = true;
			FString Slots;
			for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
			{
				const TSoftObjectPtr<USoundBase>& Sound = A.GetSound(static_cast<EGameSound>(I));
				bAll &= Sound.LoadSynchronous() != nullptr;
				Slots += FString::Printf(TEXT("%s%s=%s"), Slots.IsEmpty() ? TEXT("") : TEXT(", "), LexToString(static_cast<EGameSound>(I)), *Sound.GetAssetName());
			}
			Check(bAll, FString::Printf(TEXT("setup: map %s (act %d), every slot of Audio has its sound: %s"), *MapName(), Act(), *Slots));
			Check(!E->IsManifested() && TS()->GetCumulativeStoppedSeconds() < 1.f, FString::Printf(TEXT("setup: the entity is dormant (stopped %.1f s)"), TS()->GetCumulativeStoppedSeconds()));
			return 0.5f;
		}));

		// --- Criterion 2: stopped time ---
		S.Add(WaitIdle(TEXT("stop: the area's start lines said")));
		S.Add(Do(TEXT("stop: Stop"), []()
		{
			MarkRecord();
			StopTime();
			return 0.3f;
		}));
		S.Add(Do(TEXT("stop: before the first scan"), []()
		{
			const TArray<FGameSoundRequest> Stop = SinceOf(EGameSound::TimeStop);
			Check(Stop.Num() == 1 && !Stop[0].bInWorld && Stop[0].bHadSound && Count(EGameSound::Scan) == 0 && Count(EGameSound::ScanReady) == 0 && Director()->GetScansThisStop() == 0,
				FString::Printf(TEXT("stop: the Stop asks for the stop sound once, the suit's; no scan sound and no ready signal before the first scan: %s"), *Describe(Since())));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("stop: the automatic first scan"), []() { return Director()->GetScansThisStop() >= 1; }, 5.f, []()
		{
			const TArray<FGameSoundRequest> Scan = SinceOf(EGameSound::Scan);
			Check(Scan.Num() == 1 && !Scan[0].bInWorld && Count(EGameSound::ScanReady) == 0,
				FString::Printf(TEXT("stop: the automatic first scan asks for the scan sound once (the frame it starts), no ready signal: %s"), *Describe(Since())));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("stop: the cooldown ends"), []() { return Director()->IsScanReady(); }, 8.f, []()
		{
			Check(Count(EGameSound::ScanReady) == 1, FString::Printf(TEXT("stop: the ready signal once when the cooldown ends: %s"), *Describe(SinceOf(EGameSound::ScanReady))));
			return 0.5f;
		}));
		S.Add(Do(TEXT("stop: a manual scan"), []()
		{
			Check(Count(EGameSound::ScanReady) == 1, TEXT("stop: still one ready signal 0.5 s later"));
			Check(Director()->RequestScan(), TEXT("stop: the manual scan is issued"));
			return 0.1f;
		}));
		S.Add(Do(TEXT("stop: a press during the cooldown"), []()
		{
			Check(Count(EGameSound::Scan) == 2, FString::Printf(TEXT("stop: the manual scan asks for the scan sound again (2 in this Stop): %s"), *Describe(SinceOf(EGameSound::Scan))));
			GRun.LinesBeforeScene = Audio()->GetRecordEnd();
			Check(!Director()->RequestScan(), TEXT("stop: a press during the cooldown issues nothing"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("stop: Resume before the cooldown ends"), []()
		{
			Check(Since(GRun.LinesBeforeScene).Num() == 0, FString::Printf(TEXT("stop: the press during the cooldown asks for nothing: %s"), *Describe(Since(GRun.LinesBeforeScene))));
			ResumeTime();
			return 0.2f;
		}));
		S.Add(Do(TEXT("stop: the voluntary Resume"), []()
		{
			CheckExit(TEXT("exit: a voluntary Resume"), EGameSound::TimeResume);
			GRun.LinesBeforeScene = Audio()->GetRecordEnd();
			return 6.f;
		}));
		S.Add(Do(TEXT("stop: no ready signal after the Stop"), []()
		{
			Check(Count(EGameSound::ScanReady) == 1 && Since(GRun.LinesBeforeScene).Num() == 0,
				FString::Printf(TEXT("stop: a Stop that ended before its cooldown asks for no ready signal (6 s later): %s"), *Describe(Since(GRun.LinesBeforeScene))));
			return 0.f;
		}));

		// --- Criterion 8: behind the pause menu ---
		S.Add(Do(TEXT("menu: Stop, the first scan, then the pause menu during the cooldown"), []() { MarkRecord(); StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("menu: the first scan"), []() { return Director()->GetScansThisStop() >= 1; }, 5.f, []()
		{
			Check(StdPC() && StdPC()->PauseGame(), TEXT("menu: the pause menu opens 0 s after the first scan, the cooldown running"));
			return 6.f;
		}));
		S.Add(Do(TEXT("menu: 6 s behind the pause menu"), []()
		{
			Check(GRun.bMenuWasOpen && GRun.RequestsWhileMenu == 0 && Count(EGameSound::ScanReady) == 0,
				FString::Printf(TEXT("menu: 6 s behind the pause menu (longer than the cooldown) nothing of the run asks for a sound: %s"), *Describe(Since(GRun.MenuMark))));
			Check(StdPC()->ResumeGame(), TEXT("menu: the pause menu closes"));
			MarkRecord();
			return 0.f;
		}));
		// The director's cooldown ends on the run's clock; the ready signal goes out from the director's own tick, which may
		// come after this test's in the frame the cooldown ends: the check waits for the end of the director's cooldown, then
		// a margin of a quarter of a second (several frames) after it.
		S.Add(WaitFor(TEXT("menu: the cooldown goes on after the menu"), []() { return Director()->IsScanReady(); }, 8.f, []()
		{
			Note(FString::Printf(TEXT("menu: the director's cooldown has ended (%.3f s left); the check waits 0.25 s more"), Director()->GetCooldownRemaining()));
			return 0.25f;
		}));
		S.Add(Do(TEXT("menu: the ready signal after the cooldown"), []()
		{
			Check(Count(EGameSound::ScanReady) == 1, FString::Printf(TEXT("menu: the run's sounds come back with it: the ready signal once the cooldown ends after the menu (checked 0.25 s after the director's cooldown ended): %s"),
				*Describe(Since())));
			return 0.f;
		}));

		// --- Criterion 5: a residue collected (still in that Stop, the entity dormant) ---
		S.Add(Do(TEXT("energy: a residue at the player's eye"), []()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AResidue* R = W()->SpawnActor<AResidue>(AResidue::StaticClass(), Pawn()->GetPawnViewLocation() + Pawn()->GetActorForwardVector() * 30.f, FRotator::ZeroRotator, Params);
			GRun.Residue = R;
			GRun.Spawned.Add(R);
			MarkRecord();
			Check(R && R->BeginCollect(Pawn()), TEXT("energy: the hold starts"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("energy: the hold completes"), []() { return GRun.Residue.IsValid() && GRun.Residue->GetState() == EResidueState::Collected; }, 8.f, []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Energy);
			Check(List.Num() == 1 && !List[0].bInWorld && List[0].bHadSound, FString::Printf(TEXT("energy: the residue collected asks for the energy signal once, the suit's: %s"), *Describe(List)));
			ResumeTime();
			return 0.5f;
		}));

		// --- Criterion 4: the text channel ---
		S.Add(WaitIdle(TEXT("lines: ready")));
		S.Add(Do(TEXT("lines: a home AI message"), []()
		{
			MarkRecord();
			Channel()->PushText(TEXT("Fixture audio message from the home AI, long enough to be typed for a while on the channel."), HomeAI()->MakeContext(), TEXT("AudioHome_1"));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("lines: the home AI message said"), 30.f));
		S.Add(Do(TEXT("lines: the home AI's line sound"), []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Line);
			Check(List.Num() == 1 && List[0].Source == TEXT("channel Home AudioHome_1") && !List[0].bInWorld && OnlyOf({ EGameSound::Line }),
				FString::Printf(TEXT("lines: one line sound for the home AI's message, not one per character: %s"), *Describe(Since())));
			MarkRecord();
			// A row no event of the area has said yet: its engineer line comes before it.
			const FName Row = *FString::Printf(TEXT("A%d_EntityFound_1"), FMath::Max(Act(), 1));
			NarrativeFixture::SetEngineerLines(W(), { { Row, 1 } });
			Channel()->PushRow(Row, HomeAI()->MakeContext());
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("lines: the engineer and the home AI said"), 60.f));
		S.Add(Do(TEXT("lines: the engineer's and the home AI's line sounds"), []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Line);
			const bool bOrder = List.Num() == 2 && List[0].Source.StartsWith(TEXT("channel Engineer E_")) && List[1].Source.StartsWith(TEXT("channel Home A"));
			Check(bOrder && GRun.Text->Started.Num() - GRun.ListenerMark == 2, FString::Printf(TEXT("lines: one line sound for the engineer's line and one for the home AI's row after it: %s"), *Describe(List)));
			NarrativeFixture::SetEngineerLines(W(), {});
			MarkRecord();
			Channel()->PushTwinPart(1);
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("lines: the twin AI's part said"), 90.f));
		S.Add(Do(TEXT("lines: the twin AI's line sounds"), []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Line);
			const int32 Started = GRun.Text->Started.Num() - GRun.ListenerMark;
			bool bTwin = List.Num() > 0;
			for (const FGameSoundRequest& R : List)
			{
				bTwin &= R.Source.StartsWith(TEXT("channel Twin "));
			}
			Check(bTwin && List.Num() == Started && Started >= 2, FString::Printf(TEXT("lines: one line sound for each of the twin AI's %d messages: %s"), Started, *Describe(List)));
			MarkRecord();
			Channel()->PushText(TEXT("Fixture audio message that a Stop interrupts: it is long, so that it is still being typed when time stops, and it starts again after."),
				HomeAI()->MakeContext(), TEXT("AudioHome_2"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("lines: the message is being typed"), []() { return Channel()->GetCurrentRowName() == TEXT("AudioHome_2") && Channel()->GetVisibleText().Len() > 10; }, 10.f, []()
		{
			StopTime();
			return 1.f;
		}));
		S.Add(Do(TEXT("lines: Resume"), []()
		{
			Check(Count(EGameSound::Line) == 1, FString::Printf(TEXT("lines: while Stopped no line sound (the channel hidden): %s"), *Describe(SinceOf(EGameSound::Line))));
			ResumeTime();
			return 0.3f;
		}));
		S.Add(Do(TEXT("lines: the restart after the Stop"), []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Line);
			Check(List.Num() == 2 && List[1].Source == TEXT("channel Home AudioHome_2 restart"),
				FString::Printf(TEXT("lines: the message restarts after the Stop: one more line sound: %s"), *Describe(List)));
			Check(StdPC() && StdPC()->PauseGame(), TEXT("lines: the pause menu opens during the message"));
			return 1.f;
		}));
		S.Add(Do(TEXT("lines: the pause menu closes"), []()
		{
			Check(GRun.RequestsWhileMenu == 0, TEXT("lines: nothing asked behind the pause menu"));
			Check(StdPC()->ResumeGame(), TEXT("lines: the pause menu closes"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("lines: the restart after the pause menu"), []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Line);
			Check(List.Num() == 3 && List[2].Source == TEXT("channel Home AudioHome_2 restart"),
				FString::Printf(TEXT("lines: it restarts after the pause menu: one more line sound: %s"), *Describe(List)));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("lines: the interrupted message said"), 60.f));

		// --- Criterion 5: terminals ---
		S.Add(Do(TEXT("terminals: one of each kind, a free one and a refusing one"), []()
		{
			const float Angles[] = { 0.f, 50.f, 100.f, 150.f, 200.f, 250.f };
			const EPaymentKind Kinds[] = { EPaymentKind::Door, EPaymentKind::Lights, EPaymentKind::Beacon, EPaymentKind::Log, EPaymentKind::Twin, EPaymentKind::Other };
			for (int32 I = 0; I < 6; ++I)
			{
				GRun.Terminals.Add(Kinds[I], SpawnTerminal(Kinds[I], Angles[I]));
			}
			GRun.FreeTerminal = SpawnTerminal(EPaymentKind::Door, 300.f, 0);
			GRun.RefusingTerminal = SpawnTerminal(EPaymentKind::Lights, 330.f, 5);
			bool bKinds = GRun.FreeTerminal.IsValid() && GRun.RefusingTerminal.IsValid();
			for (const EPaymentKind K : Kinds)
			{
				bKinds &= GRun.Terminals.FindRef(K).IsValid() && GRun.Terminals.FindRef(K)->GetPaymentKind() == K;
			}
			Check(bKinds, TEXT("terminals: spawned: Door, Lights, Beacon, Log, Twin, Other, a free terminal and one that costs 5"));
			return 0.5f;
		}));
		for (const EPaymentKind Kind : { EPaymentKind::Door, EPaymentKind::Lights, EPaymentKind::Beacon, EPaymentKind::Log, EPaymentKind::Twin, EPaymentKind::Other })
		{
			const FString Label = FString::Printf(TEXT("terminals: %s"), LexToString(Kind));
			S.Add(WaitIdle(Label));
			S.Add(Do(Label + TEXT(": pay"), [Label, Kind]()
			{
				APowerTerminal* T = GRun.Terminals.FindRef(Kind).Get();
				Energy()->AddBankedEnergy(T ? T->GetCost() : 1);
				MarkRecord();
				Check(T && T->TryActivate(Pawn()), Label + TEXT(": paid"));
				return 0.2f;
			}));
			S.Add(Do(Label + TEXT(": the sound"), [Label, Kind]()
			{
				APowerTerminal* T = GRun.Terminals.FindRef(Kind).Get();
				CheckTerminalSound(Label, T);
				if (Kind == EPaymentKind::Door)
				{
					CheckDoorSounds(Label + TEXT(": its door"), { { DoorOf(T), true } });
				}
				if (Kind == EPaymentKind::Log)
				{
					// The log is typed on the terminal's surface: no line sound for it (the home AI's spending comment is the channel's).
					bool bOnlyChannel = true;
					for (const FGameSoundRequest& R : SinceOf(EGameSound::Line))
					{
						bOnlyChannel &= R.Source.StartsWith(TEXT("channel "));
					}
					Check(bOnlyChannel, FString::Printf(TEXT("%s: no line sound for the log on its surface: %s"), *Label, *Describe(SinceOf(EGameSound::Line))));
				}
				return 0.f;
			}));
		}
		S.Add(WaitIdle(TEXT("terminals: read again")));
		S.Add(Do(TEXT("terminals: reading the log terminal again"), []()
		{
			ALogTerminal* Log = Cast<ALogTerminal>(GRun.Terminals.FindRef(EPaymentKind::Log).Get());
			MarkRecord();
			Check(Log && Log->IsUnlocked() && Log->TryActivate(Pawn()), TEXT("terminals: the unlocked log terminal shows its next entry"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("terminals: the read's sound"), []()
		{
			CheckTerminalSound(TEXT("terminals: reading a log again"), GRun.Terminals.FindRef(EPaymentKind::Log).Get());
			MarkRecord();
			APowerTerminal* Free = GRun.FreeTerminal.Get();
			const int32 Banked = Energy()->GetBankedEnergy();
			Check(Free && Free->TryActivate(Pawn()) && Energy()->GetBankedEnergy() == Banked, TEXT("terminals: the free terminal works, nothing spent"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("terminals: the free activation's sound"), []()
		{
			CheckTerminalSound(TEXT("terminals: a free activation"), GRun.FreeTerminal.Get());
			CheckDoorSounds(TEXT("terminals: the free terminal's door"), { { DoorOf(GRun.FreeTerminal.Get()), true } });
			Energy()->RestoreBankedEnergy(0);
			MarkRecord();
			Check(GRun.RefusingTerminal.IsValid() && !GRun.RefusingTerminal->TryActivate(Pawn()), TEXT("terminals: with no energy the terminal refuses"));
			Energy()->AddBankedEnergy(10);
			StopTime();
			Check(!GRun.RefusingTerminal->TryActivate(Pawn()), TEXT("terminals: while Stopped the terminal refuses"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("terminals: the refusals"), []()
		{
			Check(Count(EGameSound::Terminal) == 0, FString::Printf(TEXT("terminals: a refused payment is silent: %s"), *Describe(SinceOf(EGameSound::Terminal))));
			ResumeTime();
			Energy()->RestoreBankedEnergy(0);
			return 0.3f;
		}));

		// --- Criterion 5: doors, a gate's and a spawned one closing ---
		S.Add(Do(TEXT("doors: the spawned door closes"), []()
		{
			ADoorDevice* Door = DoorOf(GRun.Terminals.FindRef(EPaymentKind::Door).Get());
			Check(Door && Door->IsFullyOpen() && !Door->IsMoving(), TEXT("doors: the spawned door is fully open before it closes"));
			MarkRecord();
			if (Door)
			{
				Door->PowerOff();
			}
			return 0.f;
		}));
		// Brief 46: the door moves over DoorMoveSeconds (before: it closed at once, with the door's short sound).
		AddTimedMotion(S, TEXT("doors: the spawned door closing"), []() -> AActor* { return DoorOf(GRun.Terminals.FindRef(EPaymentKind::Door).Get()); },
			[]() { return DoorProgress(DoorOf(GRun.Terminals.FindRef(EPaymentKind::Door).Get()), false); },
			[]() { return DoorEnded(DoorOf(GRun.Terminals.FindRef(EPaymentKind::Door).Get()), false); }, ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("doors: the spawned door's close"), []()
		{
			ADoorDevice* Door = DoorOf(GRun.Terminals.FindRef(EPaymentKind::Door).Get());
			Check(Door && !Door->IsOpen(), TEXT("doors: the spawned door is closed"));
			CheckDoorSounds(TEXT("doors: a door that starts to close"), { { Door, false } });
			// The area's first gate: its terminal is the nearest one that powers a door the generator placed.
			APowerTerminal* Best = nullptr;
			double BestDist = TNumericLimits<double>::Max();
			for (APowerTerminal* T : All<APowerTerminal>())
			{
				ADoorDevice* D = DoorOf(T);
				if (!D || GRun.Spawned.Contains(T) || D->IsOpen())
				{
					continue;
				}
				const double Dist = FVector::Dist(T->GetActorLocation(), Pawn()->GetActorLocation());
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Best = T;
				}
			}
			GRun.GateTerminal = Best;
			GRun.GateDoor = DoorOf(Best);
			Check(Best != nullptr, FString::Printf(TEXT("doors: the area's gate terminal %s and its door %s"), *GetNameSafe(Best), *GetNameSafe(DoorOf(Best))));
			if (!Best)
			{
				return -1.f;
			}
			Teleport(Best->GetActorLocation() + Best->GetActorForwardVector() * 100.f + FVector(0.f, 0.f, 100.f), false);
			Energy()->AddBankedEnergy(Best->GetCost());
			return 0.5f;
		}));
		S.Add(WaitIdle(TEXT("doors: the gate")));
		S.Add(Do(TEXT("doors: pay the gate"), []()
		{
			MarkRecord();
			Check(GRun.GateTerminal.IsValid() && GRun.GateTerminal->TryActivate(Pawn()), TEXT("doors: the gate is paid"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("doors: the gate opens"), []()
		{
			CheckTerminalSound(TEXT("doors: the gate's terminal"), GRun.GateTerminal.Get());
			CheckDoorSounds(TEXT("doors: the gate's door starts to open"), { { GRun.GateDoor.Get(), true } });
			return 0.f;
		}));
		// Brief 46: the gate's door rises over DoorMoveSeconds (before: it opened at once, with the door's short sound).
		AddTimedMotion(S, TEXT("doors: the gate's door opening"), []() -> AActor* { return GRun.GateDoor.Get(); },
			[]() { return DoorProgress(GRun.GateDoor.Get(), true); }, []() { return DoorEnded(GRun.GateDoor.Get(), true); }, ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("doors: the gate's door closes"), []()
		{
			// Out of the gate's doorway first, so that the door closes when its power goes.
			Teleport(GRun.StartLocation, false);
			MarkRecord();
			GRun.GateDoor->PowerOff();
			return 0.f;
		}));
		AddTimedMotion(S, TEXT("doors: the gate's door closing"), []() -> AActor* { return GRun.GateDoor.Get(); },
			[]() { return DoorProgress(GRun.GateDoor.Get(), false); }, []() { return DoorEnded(GRun.GateDoor.Get(), false); }, ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("doors: the gate is shut"), []()
		{
			Check(GRun.GateDoor.IsValid() && !GRun.GateDoor->IsOpen(), TEXT("doors: the gate's door is shut"));
			CheckDoorSounds(TEXT("doors: the gate's door starts to close"), { { GRun.GateDoor.Get(), false } });
			return 0.f;
		}));

		// --- Follow-up: a panel that moves over time and reverses before it ends (Brief 45: the motion's own looping sound) ---
		S.Add(Do(TEXT("motion: a sliding panel starts to open"), []()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APrologueSlidingPanel* P = W()->SpawnActor<APrologueSlidingPanel>(APrologueSlidingPanel::StaticClass(),
				Pawn()->GetActorLocation() + Pawn()->GetActorForwardVector() * 500.f, FRotator::ZeroRotator, Params);
			GRun.Panel = P;
			GRun.Spawned.Add(P);
			MarkRecord();
			if (!P)
			{
				return -1.f;
			}
			P->SetOpenFraction(0.3f);
			GRun.FirstMotionSound = Audio()->GetMotionSound(P);
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Motion);
			Check(List.Num() == 1 && List[0].Source.EndsWith(TEXT("opens")) && Near(List[0].Location, P->GetActorLocation())
				&& FMath::IsNearlyEqual(List[0].Pitch, UTheLastShiftSettings::Get()->Audio.GetHatchPitch()) && Audio()->IsMotionSoundPlaying(P)
				&& IsLooping(GRun.FirstMotionSound.Get()),
				FString::Printf(TEXT("motion: the panel starts to open: the motion's sound once, at the panel, at the hatches' pitch, one looping sound (%s): %s"),
					*DescribeLoop(GRun.FirstMotionSound.Get()), *Describe(List)));
			P->SetOpenFraction(0.6f);
			Check(DoorStartsOf(P, EGameSound::Motion) == 1 && Audio()->GetMotionPlays(P) == 1, TEXT("motion: going on opening asks for nothing more"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("motion: the panel reverses"), []()
		{
			APrologueSlidingPanel* P = GRun.Panel.Get();
			P->SetOpenFraction(0.4f);
			const TArray<FGameSoundRequest> Stops = DoorStopsOf(P, EGameSound::Motion);
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Motion);
			GRun.SecondMotionSound = Audio()->GetMotionSound(P);
			Check(Stops.Num() == 1 && Stops[0].Source.Contains(TEXT("motion reverses")) && List.Num() == 2 && List[1].Source.EndsWith(TEXT("closes"))
				&& Audio()->IsMotionSoundPlaying(P) && GRun.SecondMotionSound.Get() != GRun.FirstMotionSound.Get() && IsLooping(GRun.SecondMotionSound.Get()),
				FString::Printf(TEXT("motion: a reversal before the end fades the first sound out and starts the next: %s"), *Describe(Since())));
			// Past the length of a short sound (1 s) at the hatches' pitch: Brief 46, the door's short sound whose length this
			// waited for before is gone with its slot; a fixed second stands in for it.
			GRun.MotionStartReal = Real();
			return FMath::Max(1.f / UTheLastShiftSettings::Get()->Audio.GetHatchPitch() + 0.4f,
				UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds() + 0.15f);
		}));
		S.Add(Do(TEXT("motion: past the short sound's length into the closing"), []()
		{
			APrologueSlidingPanel* P = GRun.Panel.Get();
			const UAudioComponent* First = GRun.FirstMotionSound.Get();
			Check(!First || !First->IsPlaying(), FString::Printf(TEXT("motion: the first sound has faded out (fade %.2f s)"), UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds()));
			Check(Audio()->IsMotionSoundPlaying(P) && Audio()->GetMotionSound(P) == GRun.SecondMotionSound.Get() && Audio()->GetMotionPlays(P) == 1
				&& DoorStartsOf(P, EGameSound::Motion) == 2,
				FString::Printf(TEXT("motion: %.2f s into the closing the same looping sound plays on, played once, no request since the reversal (%s)"),
					Real() - GRun.MotionStartReal, *DescribeLoop(GRun.SecondMotionSound.Get())));
			P->SetOpenFraction(0.f);
			GRun.MotionEndReal = Real();
			return UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds() + 0.15f;
		}));
		S.Add(Do(TEXT("motion: the panel has closed"), []()
		{
			APrologueSlidingPanel* P = GRun.Panel.Get();
			const TArray<FGameSoundRequest> Stops = DoorStopsOf(P, EGameSound::Motion);
			Check(!Audio()->IsMotionSoundPlaying(P) && Stops.Num() == 2 && Stops[1].Source.Contains(TEXT("motion ends")),
				FString::Printf(TEXT("motion: %.2f s after the end (fade %.2f s) the sound has stopped: %s"), Real() - GRun.MotionEndReal,
					UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds(), *Describe(Stops)));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("doors: the spending comments said")));

		// --- Criterion 7: one owner, an empty slot, the commands ---
		S.Add(Do(TEXT("owner: the Scan slot emptied, a Stop"), []()
		{
			GWarnings.Count = 0;
			GLog->AddOutputDevice(&GWarnings);
			GWarnings.bAdded = true;
			GEngine->Exec(W(), TEXT("tls.Audio.Set Scan Empty"));
			Check(UTheLastShiftSettings::Get()->Audio.GetSound(EGameSound::Scan).IsNull(), TEXT("owner: tls.Audio.Set Scan Empty empties the Scan slot for the session"));
			MarkRecord();
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("owner: the first scan with the empty slot"), []() { return Director()->GetScansThisStop() >= 1; }, 5.f, []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::Scan);
			Check(List.Num() == 1 && !List[0].bHadSound, FString::Printf(TEXT("owner: the scan with its slot empty is asked for and plays nothing: %s"), *Describe(List)));
			ResumeTime();
			return 0.2f;
		}));
		S.Add(Do(TEXT("owner: no error for the empty slot"), []()
		{
			StopCountingWarnings();
			Check(GWarnings.Count == 0, FString::Printf(TEXT("owner: the empty slot logged no warning and no error (%d)"), GWarnings.Count));
			GEngine->Exec(W(), TEXT("tls.Audio.Set Scan Restore"));
			Check(!UTheLastShiftSettings::Get()->Audio.GetSound(EGameSound::Scan).IsNull(), TEXT("owner: tls.Audio.Set Scan Restore gives the slot its sound back"));
			// Every request since the test began came from the settings' slots: the sound each one played is its slot's.
			bool bOwner = true;
			for (const FGameSoundRequest& R : Since(0))
			{
				bOwner &= !R.bHadSound || !UTheLastShiftSettings::Get()->Audio.GetSound(R.Sound).IsNull();
			}
			Check(bOwner, TEXT("owner: every sound asked for so far comes from its slot of Audio"));
			return 0.f;
		}));
		S.Add(Do(TEXT("owner: the commands"), []()
		{
			MarkRecord();
			// Brief 46: the Door slot is gone (no slot of that name); the commands are checked on the terminal's slot instead.
			bool bNoDoorSlot = true;
			for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
			{
				bNoDoorSlot &= FCString::Stricmp(LexToString(static_cast<EGameSound>(I)), TEXT("Door")) != 0;
			}
			Check(bNoDoorSlot, TEXT("owner: no Door slot among the audio's slots (the door's short sound is gone)"));
			GEngine->Exec(W(), TEXT("tls.Audio.Set Terminal Volume 0.5"));
			GEngine->Exec(W(), TEXT("tls.Audio.Set HatchPitch 0.7"));
			GEngine->Exec(W(), TEXT("tls.Audio.Set FalloffCm 1500"));
			const FGameAudio& A = UTheLastShiftSettings::Get()->Audio;
			Check(FMath::IsNearlyEqual(A.Terminal.GetVolume(), 0.5f) && FMath::IsNearlyEqual(A.GetHatchPitch(), 0.7f) && FMath::IsNearlyEqual(A.GetWorldFalloffCm(), 1500.f),
				FString::Printf(TEXT("owner: tls.Audio.Set sets the terminal's volume %.2f, the hatches' pitch %.2f and the falloff %.0f cm for the session"), A.Terminal.GetVolume(),
					A.GetHatchPitch(), A.GetWorldFalloffCm()));
			GEngine->Exec(W(), TEXT("tls.Audio.Play Terminal"));
			GEngine->Exec(W(), TEXT("tls.Audio.Report"));
			const TArray<FGameSoundRequest> List = Since();
			Check(List.Num() == 1 && List[0].Sound == EGameSound::Terminal && FMath::IsNearlyEqual(List[0].Volume, 0.5f) && List[0].bHadSound,
				FString::Printf(TEXT("owner: tls.Audio.Play Terminal plays the terminal's slot at its session volume; tls.Audio.Report asks for nothing: %s"), *Describe(List)));
			GEngine->Exec(W(), TEXT("tls.Audio.Set All Restore"));
			Check(FMath::IsNearlyEqual(UTheLastShiftSettings::Get()->Audio.Terminal.GetVolume(), 1.f) || !GSavedAudio.IsSet(), TEXT("owner: tls.Audio.Set All Restore puts the values back"));
			return 0.f;
		}));

		// --- Criterion 3: the exits, the entity awake ---
		auto Contact = [&S](EEntityContactResponse Response, EGameSound Want)
		{
			const FString Label = FString::Printf(TEXT("exit: a contact with %s"), LexToString(Response));
			S.Add(WaitIdle(Label));
			S.Add(Do(Label + TEXT(": Stop, the player on the figure"), [Response]()
			{
				AEntity* E = Entity();
				Home()->RestoreState(GRun.SavedBattery, GRun.SavedHome);
				E->ContactResponse = Response;
				E->GrazeRadius = 0.f;
				MarkRecord();
				StopTime();
				E->SpawnNow(E->MinSpawnDistance);
				E->Speed = 0.f;
				APawn* P = Pawn();
				const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
				P->SetActorLocation(E->GetFigureLocation() + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
				return 0.f;
			}));
			S.Add(WaitFor(Label + TEXT(": the contact ends the Stop"), []() { return !IsStopped(); }, 10.f, [Label, Want]()
			{
				CheckExit(Label, Want);
				Home()->RestoreState(GRun.SavedBattery, GRun.SavedHome);
				return 0.5f;
			}));
		};
		Contact(EEntityContactResponse::Rewind, EGameSound::TimeResume);
		Contact(EEntityContactResponse::Jump, EGameSound::Slip);
		Contact(EEntityContactResponse::RewindAndJump, EGameSound::Slip);
		S.Add(WaitIdle(TEXT("exit: a graze")));
		S.Add(Do(TEXT("exit: a graze: Stop, the player 3 m along the figure's path"), []()
		{
			AEntity* E = Entity();
			E->GrazeRadius = 400.f;
			MarkRecord();
			StopTime();
			E->SpawnNow(E->MinSpawnDistance);
			E->Speed = 0.f;
			const TArray<FVector> Path = E->GetCurrentPath();
			FVector Target = Path.Num() > 0 ? Path.Last() : E->GetFigureLocation();
			float Left = 300.f;
			for (int32 I = 1; I < Path.Num(); ++I)
			{
				const float Leg = FVector::Dist(Path[I - 1], Path[I]);
				if (Leg >= Left)
				{
					Target = FMath::Lerp(Path[I - 1], Path[I], Leg > 0.f ? Left / Leg : 0.f);
					break;
				}
				Left -= Leg;
			}
			APawn* P = Pawn();
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(Target + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			return 0.3f;
		}));
		S.Add(Do(TEXT("exit: a graze: Resume"), []()
		{
			ResumeTime();
			Check(Entity()->WasLastExitGraze(), TEXT("exit: the Resume was a graze"));
			CheckExit(TEXT("exit: a Resume with a graze"), EGameSound::Slip);
			Entity()->GrazeRadius = 0.f;
			Home()->RestoreState(GRun.SavedBattery, GRun.SavedHome);
			return 0.f;
		}));
	}

	// ================================================================================================================
	// Part menu (MainMenu)
	// ================================================================================================================

	static USoundBase* MusicAsset() { return UTheLastShiftSettings::Get()->Audio.MenuMusic.LoadSynchronous(); }

	static bool MusicPlaying() { const UStdMusicSubsystem* M = Music(); return M && M->GetCurrentMusic() != nullptr; }

	/** In a map that is not the menu: no music, nothing asked to play since the mark. */
	static void CheckNoMusic(const FString& Label)
	{
		int32 Plays = 0;
		for (const FGameSoundRequest& R : SinceOf(EGameSound::MenuMusic, true))
		{
			Plays += R.bStop ? 0 : 1;
		}
		Check(!MusicPlaying() && Audio() && !Audio()->IsMusicOn() && Plays == 0,
			FString::Printf(TEXT("%s: in %s no music plays and none was asked for: %s"), *Label, *MapName(), *Describe(SinceOf(EGameSound::MenuMusic, true))));
	}

	/** Arrived in the main menu: the music asked to play once since the mark, and playing. */
	static void CheckMusicOnce(const FString& Label)
	{
		const TArray<FGameSoundRequest> List = SinceOf(EGameSound::MenuMusic, true);
		int32 Plays = 0;
		for (const FGameSoundRequest& R : List)
		{
			Plays += R.bStop ? 0 : 1;
		}
		const USoundWave* Wave = Cast<USoundWave>(MusicAsset());
		Check(Plays == 1 && Audio()->IsMusicOn() && Music() && Music()->GetCurrentMusic() == MusicAsset() && Wave && Wave->bLooping,
			FString::Printf(TEXT("%s: the music asked to play once, playing %s, looping %d: %s"), *Label, *GetNameSafe(Music() ? Music()->GetCurrentMusic() : nullptr),
				Wave && Wave->bLooping ? 1 : 0, *Describe(List)));
	}

	/** One music stop request since the mark, and the music off. */
	static void CheckMusicStopped(const FString& Label)
	{
		const TArray<FGameSoundRequest> List = SinceOf(EGameSound::MenuMusic, true);
		Check(List.Num() == 1 && List[0].bStop && !MusicPlaying() && !Audio()->IsMusicOn(),
			FString::Printf(TEXT("%s: the music fades out (%.1f s): %s"), *Label, UTheLastShiftSettings::Get()->Audio.GetMusicFadeOutSeconds(), *Describe(List)));
	}

	static void AddBackToMenu(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": back to the main menu"), []()
		{
			MarkRecord();
			Run()->ReturnToMainMenu();
			return 1.f;
		}));
		S.Add(WaitFor(Label + TEXT(": in the main menu"), []() { return InMenu(); }, 60.f, [Label]()
		{
			CheckMusicOnce(Label + TEXT(": in the menu again"));
			return 0.5f;
		}));
	}

	/** Shows a scene of Voices over the main menu (as an ending's text, so that it closes itself) and reads it through. */
	static void AddScene(TArray<FStep>& S, const FString& Label, FName Scene, TArray<ESceneVoice> Voices, int32 WantInterruptAt)
	{
		S.Add(Do(Label + TEXT(": open"), [Scene, Voices]()
		{
			TArray<FRunSceneLine> Lines;
			for (int32 I = 0; I < Voices.Num(); ++I)
			{
				FRunSceneLine& Line = Lines.AddDefaulted_GetRef();
				Line.RowName = *FString::Printf(TEXT("%s_%d"), *Scene.ToString(), I + 1);
				Line.Voice = Voices[I];
				Line.Text = FString::Printf(TEXT("Fixture line %d of the audio scene."), I + 1);
			}
			MarkRecord();
			UStdMainMenuWidget* Menu = MainMenu();
			URunTextSceneWidget* Screen = Menu ? Cast<URunTextSceneWidget>(Menu->OpenChildScreen(URunTextSceneWidget::StaticClass())) : nullptr;
			if (!Screen)
			{
				return -1.f;
			}
			Screen->Play(ERunSceneKind::EndingText, Scene, Lines);
			return 0.2f;
		}));
		S.Add(WaitFor(Label + TEXT(": read through"), []()
		{
			if (URunTextSceneWidget* Scene = TopScene())
			{
				Scene->Confirm();
				return false;
			}
			return InMenu();
		}, 30.f, [Label, Voices, WantInterruptAt]()
		{
			const TArray<FGameSoundRequest> Lines = SinceOf(EGameSound::Line);
			const TArray<FGameSoundRequest> Cuts = SinceOf(EGameSound::Interruption);
			bool bCut = WantInterruptAt < 0 ? Cuts.Num() == 0 : Cuts.Num() == 1 && Cuts[0].Source.EndsWith(FString::Printf(TEXT("line %d"), WantInterruptAt));
			Check(Lines.Num() == Voices.Num() && bCut, FString::Printf(TEXT("%s: one line sound for each of its %d lines, the interruption %s: %s"), *Label, Voices.Num(),
				WantInterruptAt < 0 ? TEXT("never") : *FString::Printf(TEXT("once, at line %d"), WantInterruptAt), *Describe(Since())));
			return 0.2f;
		}));
	}

	/** Brief 35: the interruption's noise held with the static of a fixture scene's cut, on the static's two values. */
	struct FStaticSound
	{
		double StartReal = 0.0;
		float Hold = 0.f;
		float Fade = 0.f;
		float SavedHold = 0.f;
		float SavedFade = 0.f;
		bool bSaved = false;
		/** Brief 45: the noise's one component, from the cut. */
		TWeakObjectPtr<UAudioComponent> Sound;
	};
	static FStaticSound GStatic;

	static void RestoreStaticValues()
	{
		if (GStatic.bSaved)
		{
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			Settings->StaticSeconds = GStatic.SavedHold;
			Settings->StaticFadeSeconds = GStatic.SavedFade;
			GStatic.bSaved = false;
		}
	}

	static void AddStaticSound(TArray<FStep>& S, const FString& Label, float Hold, float Fade, bool bSkip)
	{
		S.Add(Do(Label + TEXT(": open"), [Hold, Fade]()
		{
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			if (Hold >= 0.f)
			{
				GStatic.SavedHold = Settings->StaticSeconds;
				GStatic.SavedFade = Settings->StaticFadeSeconds;
				GStatic.bSaved = true;
				Settings->StaticSeconds = Hold;
				Settings->StaticFadeSeconds = Fade;
			}
			TArray<FRunSceneLine> Lines;
			const ESceneVoice Voices[] = { ESceneVoice::Paradise, ESceneVoice::HomeAI, ESceneVoice::HomeAI };
			for (int32 I = 0; I < 3; ++I)
			{
				FRunSceneLine& Line = Lines.AddDefaulted_GetRef();
				Line.RowName = *FString::Printf(TEXT("AudioStatic_%d"), I + 1);
				Line.Voice = Voices[I];
				Line.Text = FString::Printf(TEXT("Fixture line %d of the static's scene."), I + 1);
			}
			MarkRecord();
			UStdMainMenuWidget* Menu = MainMenu();
			URunTextSceneWidget* Screen = Menu ? Cast<URunTextSceneWidget>(Menu->OpenChildScreen(URunTextSceneWidget::StaticClass())) : nullptr;
			if (!Screen)
			{
				return -1.f;
			}
			Screen->Play(ERunSceneKind::EndingText, TEXT("AudioStatic"), Lines);
			return 0.2f;
		}));
		S.Add(WaitFor(Label + TEXT(": the cut from white to black (line 2)"), []()
		{
			URunTextSceneWidget* Scene = TopScene();
			if (Scene && Scene->GetCurrentIndex() < 1)
			{
				Scene->Confirm();
				return false;
			}
			return Scene && Scene->GetCurrentIndex() == 1;
		}, 10.f, [Label, bSkip]()
		{
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			GStatic.StartReal = FPlatformTime::Seconds();
			GStatic.Hold = Settings->GetStaticSeconds();
			GStatic.Fade = Settings->GetStaticFadeSeconds();
			const TArray<FGameSoundRequest> Cuts = SinceOf(EGameSound::Interruption);
			Check(Cuts.Num() == 1 && Audio()->IsInterruptionHeld() && Audio()->IsInterruptionPlaying(),
				FString::Printf(TEXT("%s: the interruption's noise starts with the static (StaticSeconds %.2f, StaticFadeSeconds %.2f): held %d, playing %d, %s"), *Label, GStatic.Hold,
					GStatic.Fade, Audio()->IsInterruptionHeld() ? 1 : 0, Audio()->IsInterruptionPlaying() ? 1 : 0, *Describe(Cuts)));
			// Brief 45: one sound, looping, at once at its volume (the cut is sudden: no fade in).
			GStatic.Sound = Audio()->GetInterruptionComponent();
			Check(IsLooping(GStatic.Sound.Get()) && Audio()->GetInterruptionPlays() == 1 && Audio()->GetInterruptionShare() >= 0.99f,
				FString::Printf(TEXT("%s: the noise is one looping sound, played once, at its full volume from the cut (%s; share %.2f)"), *Label, *DescribeLoop(GStatic.Sound.Get()),
					Audio()->GetInterruptionShare()));
			return bSkip ? 0.5f : FMath::Max(GStatic.Hold - 0.25f, 0.05f);
		}));
		if (bSkip)
		{
			S.Add(Do(Label + TEXT(": skipped during the static"), [Label]()
			{
				URunTextSceneWidget* Scene = TopScene();
				Check(Scene && Audio()->IsInterruptionPlaying(), FString::Printf(TEXT("%s: 0.5 s into the static the noise still plays"), *Label));
				if (Scene)
				{
					Scene->Skip();
				}
				return 0.2f;
			}));
			S.Add(Do(Label + TEXT(": after the skip"), [Label]()
			{
				const TArray<FGameSoundRequest> Stops = SinceOf(EGameSound::Interruption, true);
				Check(!Audio()->IsInterruptionHeld() && !Audio()->IsInterruptionPlaying() && Stops.Num() == 2 && Stops[1].bStop,
					FString::Printf(TEXT("%s: skipping the scene during the static stops the noise with it (0.2 s later: held %d, playing %d): %s"), *Label, Audio()->IsInterruptionHeld() ? 1 : 0,
						Audio()->IsInterruptionPlaying() ? 1 : 0, *Describe(Stops)));
				return 0.f;
			}));
		}
		else
		{
			S.Add(Do(Label + TEXT(": at the end of the hold"), [Label]()
			{
				const double At = FPlatformTime::Seconds() - GStatic.StartReal;
				Check(Audio()->IsInterruptionHeld() && Audio()->IsInterruptionPlaying() && Audio()->GetInterruptionShare() >= 0.99f
					&& Audio()->GetInterruptionComponent() == GStatic.Sound.Get() && Audio()->GetInterruptionPlays() == 1,
					FString::Printf(TEXT("%s: %.2f s in (StaticSeconds %.2f) the noise still plays at full strength (share %.2f, played %d times)"), *Label, At, GStatic.Hold,
						Audio()->GetInterruptionShare(), Audio()->GetInterruptionPlays()));
				return 0.25f + 0.5f * GStatic.Fade;
			}));
			S.Add(Do(Label + TEXT(": in the fade"), [Label]()
			{
				const double At = FPlatformTime::Seconds() - GStatic.StartReal;
				const float Share = Audio()->GetInterruptionShare();
				Check(GStatic.Fade <= 0.f || (Audio()->IsInterruptionPlaying() && Share > 0.05f && Share < 0.95f),
					FString::Printf(TEXT("%s: %.2f s in, halfway through the fade (StaticFadeSeconds %.2f), the noise fades with the static (share %.2f)"), *Label, At, GStatic.Fade, Share));
				return 0.5f * GStatic.Fade + 0.3f;
			}));
			S.Add(Do(Label + TEXT(": after the fade"), [Label]()
			{
				const double At = FPlatformTime::Seconds() - GStatic.StartReal;
				const TArray<FGameSoundRequest> Stops = SinceOf(EGameSound::Interruption, true);
				const USoundBase* Sound = UTheLastShiftSettings::Get()->Audio.GetSound(EGameSound::Interruption).LoadSynchronous();
				// Brief 46: the sound wave's own length (USoundBase::GetDuration gives 10000 s for a looping wave).
				const USoundWave* Wave = Cast<USoundWave>(Sound);
				const float Length = Wave ? Wave->Duration : 0.f;
				Check(!Audio()->IsInterruptionHeld() && !Audio()->IsInterruptionPlaying() && Stops.Num() == 2 && Stops[1].bStop && SinceOf(EGameSound::Interruption).Num() == 1,
					FString::Printf(TEXT("%s: %.2f s in, past StaticSeconds + StaticFadeSeconds (%.2f) by 0.3 s, the noise has stopped; asked for once: %s"), *Label, At, GStatic.Hold + GStatic.Fade,
						*Describe(Stops)));
				Check(Length <= 0.f || (Length >= GStatic.Hold + GStatic.Fade && Audio()->GetInterruptionPlays() == 1),
					FString::Printf(TEXT("%s: one sound (%.2f s, the static %.2f s) for the whole static, never played again: played %d time(s)"), *Label, Length, GStatic.Hold + GStatic.Fade,
						Audio()->GetInterruptionPlays()));
				return 0.f;
			}));
		}
		S.Add(WaitFor(Label + TEXT(": the scene closed"), []()
		{
			if (URunTextSceneWidget* Scene = TopScene())
			{
				Scene->Confirm();
				return false;
			}
			return InMenu();
		}, 20.f, []()
		{
			RestoreStaticValues();
			return 0.2f;
		}));
	}

	static void AddMenu(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("setup: the main menu"), []() { return InMenu() && Audio(); }, 30.f, []()
		{
			NarrativeFixture::Install(W());
			Run()->ClearSave();
			Check(UTheLastShiftSettings::Get()->ArePrologueMapsOn(), TEXT("setup: the prologue's maps are on"));
			Check(MusicAsset() != nullptr, FString::Printf(TEXT("setup: the music's slot holds %s"), *UTheLastShiftSettings::Get()->Audio.MenuMusic.ToString()));
			return 0.5f;
		}));

		// --- Criterion 6: the menu's music ---
		S.Add(Do(TEXT("music: a travel from the menu to the menu"), []()
		{
			Check(Audio()->IsMusicOn() && MusicPlaying(), TEXT("music: the menu's music plays in the main menu"));
			MarkRecord();
			Check(Run()->ReturnToMainMenu(), TEXT("music: the travel starts"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("music: leaving"), []()
		{
			CheckMusicStopped(TEXT("music: a travel leaving the menu"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("music: in the menu again"), []() { return InMenu(); }, 60.f, []()
		{
			const TArray<FGameSoundRequest> List = SinceOf(EGameSound::MenuMusic, true);
			Check(List.Num() == 2 && List[0].bStop && !List[1].bStop, FString::Printf(TEXT("music: off on leaving, on again on arrival: %s"), *Describe(List)));
			return 3.f;
		}));
		S.Add(Do(TEXT("music: 3 s in the menu"), []()
		{
			Check(Audio()->IsMusicOn() && Music()->GetCurrentMusic() == MusicAsset() && SinceOf(EGameSound::MenuMusic, true).Num() == 2,
				TEXT("music: after 3 s in the menu still the one track, asked for once since arrival"));
			return 0.f;
		}));

		// --- Criterion 4: the text scenes ---
		AddScene(S, TEXT("scenes: white, white, black, black"), TEXT("AudioCut"), { ESceneVoice::Paradise, ESceneVoice::EngineerParadise, ESceneVoice::HomeAI, ESceneVoice::Engineer }, 3);
		AddScene(S, TEXT("scenes: white, black, white, black"), TEXT("AudioBack"), { ESceneVoice::Paradise, ESceneVoice::HomeAI, ESceneVoice::EngineerParadise, ESceneVoice::HomeAI }, 2);
		AddScene(S, TEXT("scenes: black only"), TEXT("AudioBlack"), { ESceneVoice::HomeAI, ESceneVoice::Engineer, ESceneVoice::HomeAI }, -1);

		// --- Brief 35, criterion 5: the interruption's noise lasts as long as the static ---
		AddStaticSound(S, TEXT("static"), -1.f, -1.f, false);
		AddStaticSound(S, TEXT("static, 1 s and 1 s for the session"), 1.f, 1.f, false);
		AddStaticSound(S, TEXT("static, the scene skipped"), -1.f, -1.f, true);

		// --- New game: the music fades as the opening scene opens; the scene's lines and its interruption ---
		S.Add(Do(TEXT("new game: New game"), []()
		{
			// The home's briefing and the arrival's lines left out: their terminals and doors work at once.
			NarrativeFixture::SetPrologueGroups({ TEXT("Hologram"), TEXT("Light"), TEXT("Hatch"), TEXT("Door") });
			MarkRecord();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_NewGame"), false) && TopScene() && TopScene()->GetKind() == ERunSceneKind::Prologue,
				TEXT("new game: New game opens the opening scene"));
			return 0.1f;
		}));
		S.Add(Do(TEXT("new game: the music"), []()
		{
			CheckMusicStopped(TEXT("new game: the opening scene opens"));
			GRun.LinesBeforeScene = TopScene() ? TopScene()->GetLines().Num() : 0;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("new game: the opening scene read through"), []()
		{
			if (URunTextSceneWidget* Scene = TopScene())
			{
				Scene->Confirm();
			}
			return Transitioning() || Stage() == 1;
		}, 60.f, []()
		{
			const TArray<FGameSoundRequest> Lines = SinceOf(EGameSound::Line);
			const TArray<FGameSoundRequest> Cuts = SinceOf(EGameSound::Interruption);
			Check(Lines.Num() == GRun.LinesBeforeScene && Cuts.Num() == 1 && Cuts[0].Source.EndsWith(TEXT("line 3")),
				FString::Printf(TEXT("new game: the opening scene: one line sound for each of its %d lines and the interruption once, at its cut from white to black (line 3): %s"),
					GRun.LinesBeforeScene, *Describe(Since())));
			MarkRecord();
			return 0.f;
		}));

		// --- The prologue's home: terminals, doors, hatches ---
		S.Add(WaitFor(TEXT("home: in Prologue_Home"), []() { return Stage() == 1 && Pawn() && !Transitioning() && First<APrologueDeparture>(); }, 90.f, []()
		{
			CheckNoMusic(TEXT("home"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("home: the first terminal"), []()
		{
			TArray<APowerTerminal*> Terminals = All<APowerTerminal>();
			APowerTerminal* T = nullptr;
			for (APowerTerminal* Each : Terminals)
			{
				if (!Each->IsA<APrologueBriefingTerminal>() && DoorOf(Each) && !DoorOf(Each)->IsOpen())
				{
					T = Each;
					break;
				}
			}
			GRun.GateTerminal = T;
			if (!T)
			{
				Check(false, TEXT("home: a free terminal with its door"));
				return -1.f;
			}
			Teleport(T->GetActorLocation() + T->GetActorForwardVector() * 100.f);
			return 0.3f;
		}));
		S.Add(Do(TEXT("home: Interact at it"), []()
		{
			MarkRecord();
			Check(GRun.GateTerminal->TryActivate(Pawn()), TEXT("home: the free terminal works"));
			return 0.f;
		}));
		// Brief 46: the print room's door rises over DoorMoveSeconds (before: its panel was hidden at once, the door's short sound).
		AddTimedMotion(S, TEXT("home: the print room's door opening"), []() -> AActor* { return DoorOf(GRun.GateTerminal.Get()); },
			[]() { return DoorProgress(DoorOf(GRun.GateTerminal.Get()), true); }, []() { return DoorEnded(DoorOf(GRun.GateTerminal.Get()), true); }, ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("home: its sounds"), []()
		{
			CheckTerminalSound(TEXT("home: a prologue terminal's free activation"), GRun.GateTerminal.Get());
			CheckDoorSounds(TEXT("home: a prologue door starts to open"), { { DoorOf(GRun.GateTerminal.Get()), true } });
			const APrologueLineTrigger* Trigger = nullptr;
			for (const APrologueLineTrigger* T : All<APrologueLineTrigger>())
			{
				Trigger = T->Group == TEXT("Hologram") ? T : Trigger;
			}
			if (Trigger)
			{
				const FVector C = Trigger->GetActorLocation();
				Teleport(C.Y - Trigger->Extent.Y > 100.f ? FVector(C.X, C.Y - Trigger->Extent.Y + 300.f, 0.f) : FVector(C.X - Trigger->Extent.X + 300.f, C.Y, 0.f));
			}
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("home: the briefing terminal works (no Hologram row)"), []() { const APrologueBriefingTerminal* B = First<APrologueBriefingTerminal>(); return B && B->IsBriefed(); }, 10.f, []()
		{
			APrologueBriefingTerminal* B = First<APrologueBriefingTerminal>();
			Teleport(B->GetActorLocation() + B->GetActorForwardVector() * 100.f);
			return 0.3f;
		}));
		S.Add(Do(TEXT("home: the hangar's terminal"), []()
		{
			MarkRecord();
			Check(First<APrologueBriefingTerminal>()->TryActivate(Pawn()), TEXT("home: the hangar's terminal works"));
			return 0.f;
		}));
		// Brief 46: the entry door rises open over DoorMoveSeconds (before: its panel was hidden at once).
		AddTimedMotion(S, TEXT("home: the entry door opening"), []() -> AActor* { return First<APrologueEntryDoor>(); },
			[]() { return DoorProgress(First<APrologueEntryDoor>(), true); }, []() { return DoorEnded(First<APrologueEntryDoor>(), true); }, ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("home: its sounds; into the hangar"), []()
		{
			APrologueBriefingTerminal* B = First<APrologueBriefingTerminal>();
			CheckTerminalSound(TEXT("home: the hangar's terminal"), B);
			CheckDoorSounds(TEXT("home: the entry door starts to open"), { { First<APrologueEntryDoor>(), true } });
			const APrologueLineTrigger* Trigger = nullptr;
			for (const APrologueLineTrigger* T : All<APrologueLineTrigger>())
			{
				Trigger = T->Group == TEXT("Hangar") ? T : Trigger;
			}
			MarkRecord();
			if (Trigger)
			{
				const FVector C = Trigger->GetActorLocation();
				Teleport(FVector(C.X - Trigger->Extent.X + 500.f, C.Y, 0.f));
			}
			return 0.5f;
		}));
		{
			const UTheLastShiftSettings* Set = UTheLastShiftSettings::Get();
			AddTimedMotion(S, TEXT("home: the entry door closing behind the player"), []() -> AActor* { return First<APrologueEntryDoor>(); },
				[]() { const APrologueEntryDoor* E = First<APrologueEntryDoor>(); return E && (E->IsClosingBehind() || E->HasClosedBehind()) ? FMath::Max(E->GetClosedFraction(), 0.01f) : 0.f; },
				[]() { const APrologueEntryDoor* E = First<APrologueEntryDoor>(); return E && E->HasClosedBehind(); }, Set->GetHomeDoorCloseSeconds());
			AddTimedMotion(S, TEXT("home: the hangar's hatch opening"), []() -> AActor* { return First<APrologueDeparture>()->HangarHatch.Get(); },
				[]() { const APrologueSlidingPanel* H = First<APrologueDeparture>()->HangarHatch.Get(); return H ? H->GetOpenFraction() : 0.f; },
				[]() { const APrologueSlidingPanel* H = First<APrologueDeparture>()->HangarHatch.Get(); return H && H->IsFullyOpen(); }, Set->GetHomeHatchOpenSeconds());
			AddTimedMotion(S, TEXT("home: the ship's hatch opening"), []() -> AActor* { return First<APrologueDeparture>()->ShipHatch.Get(); },
				[]() { const APrologueSlidingPanel* H = First<APrologueDeparture>()->ShipHatch.Get(); return H ? H->GetOpenFraction() : 0.f; },
				[]() { const APrologueSlidingPanel* H = First<APrologueDeparture>()->ShipHatch.Get(); return H && H->IsFullyOpen(); }, Set->GetShipHatchOpenSeconds());
		}
		S.Add(WaitFor(TEXT("home: the departure scene"), []() { const APrologueDeparture* D = First<APrologueDeparture>(); return D && D->IsBoardable(); }, 30.f, []()
		{
			const APrologueDeparture* D = First<APrologueDeparture>();
			TArray<APrologueSlidingPanel*> Panels = All<APrologueSlidingPanel>();
			// The hangar's hatch opens first, then the ship's: the panels in the order of their sounds.
			TArray<TPair<const AActor*, bool>> Want = { { First<APrologueEntryDoor>(), false } };
			TArray<FGameSoundRequest> Doors = SinceOf(EGameSound::Motion);
			for (int32 I = 1; I < Doors.Num(); ++I)
			{
				for (APrologueSlidingPanel* P : Panels)
				{
					if (Doors[I].Source.StartsWith(P->GetName()))
					{
						Want.Add({ P, true });
					}
				}
			}
			Check(Panels.Num() == 2 && Want.Num() == 3, FString::Printf(TEXT("home: the two hatches (%d sliding panels) both sounded"), Panels.Num()));
			CheckDoorSounds(TEXT("home: the entry door starts to close behind the player, then the hangar's hatch and the ship's hatch open"), Want, EGameSound::Motion);
			Teleport(D->ShipExit->GetActorLocation());
			MarkRecord();
			return 0.f;
		}));

		// --- The arrival: its hatch closes, its exit door opens ---
		S.Add(WaitFor(TEXT("arrival: in Prologue_Arrival"), []() { const APrologueHatch* H = First<APrologueHatch>(); return Stage() == 2 && Pawn() && !Transitioning() && H && H->IsInsideShip(Pawn()); }, 90.f, []()
		{
			CheckNoMusic(TEXT("arrival"));
			bool bLabel = true;
			for (const FGameSoundRequest& R : SinceOf(EGameSound::Line))
			{
				bLabel &= !R.Source.Contains(TEXT("Flight"));
			}
			Check(bLabel, TEXT("arrival: no line sound for the flight's label"));
			MarkRecord();
			const FBox Ship = First<APrologueHatch>()->GetShipBox();
			Teleport(FVector(Ship.Max.X + 300.f, Ship.GetCenter().Y, 0.f));
			return 0.f;
		}));
		AddTimedMotion(S, TEXT("arrival: the hangar's hatch closing"), []() -> AActor* { return First<APrologueHatch>(); },
			[]() { const APrologueHatch* H = First<APrologueHatch>(); return H && H->GetHatchState() != EPrologueHatchState::Open ? FMath::Max(H->GetClosedFraction(), 0.01f) : 0.f; },
			[]() { const APrologueHatch* H = First<APrologueHatch>(); return H && H->GetHatchState() != EPrologueHatchState::Open && H->GetHatchState() != EPrologueHatchState::Closing; },
			UTheLastShiftSettings::Get()->GetHatchCloseSeconds());
		S.Add(WaitFor(TEXT("arrival: the exit door opens"), []() { const APrologueHatch* H = First<APrologueHatch>(); return H && H->GetHatchState() == EPrologueHatchState::DoorOpened; }, 40.f, []()
		{
			const APrologueHatch* H = First<APrologueHatch>();
			// Brief 46: the exit door's opening is a motion too (before: the door's short sound).
			CheckDoorSounds(TEXT("arrival: the hatch starts to close, then the exit door starts to open (the motion's sound)"), { { H, false }, { H->ExitDoor.Get(), true } });
			return 0.f;
		}));
		AddTimedMotion(S, TEXT("arrival: the exit door opening"), []() -> AActor* { return First<APrologueHatch>()->ExitDoor.Get(); },
			[]() { return DoorProgress(First<APrologueHatch>()->ExitDoor.Get(), true); }, []() { return DoorEnded(First<APrologueHatch>()->ExitDoor.Get(), true); },
			ADoorDevice::GetMoveSeconds());
		S.Add(Do(TEXT("arrival: travel on"), []()
		{
			MarkRecord();
			Check(Run()->TravelToNextArea(), TEXT("arrival: the travel to area 1 starts"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 1: arrived"), []() { return Act() == 1 && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }, 120.f, []()
		{
			CheckNoMusic(TEXT("area 1"));
			bool bLabel = true;
			for (const FGameSoundRequest& R : SinceOf(EGameSound::Line))
			{
				bLabel &= !R.Source.Contains(TEXT("AreaEntry"));
			}
			Check(bLabel, FString::Printf(TEXT("area 1: no line sound for the travel screen's label: %s"), *Describe(SinceOf(EGameSound::Line))));
			return 1.f;
		}));
		AddBackToMenu(S, TEXT("from area 1"));

		// --- Continue and the act selection ---
		S.Add(Do(TEXT("continue: Continue"), []()
		{
			MarkRecord();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_Continue"), false), TEXT("continue: Continue clicked"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("continue: the music"), []() { CheckMusicStopped(TEXT("continue: Continue leaves the menu")); MarkRecord(); return 0.f; }));
		S.Add(WaitFor(TEXT("continue: in area 1"), []() { return Act() == 1 && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }, 120.f, []()
		{
			CheckNoMusic(TEXT("continue: area 1"));
			return 0.5f;
		}));
		AddBackToMenu(S, TEXT("from Continue"));
		S.Add(Do(TEXT("act selection: Select act, the first act"), []()
		{
			MarkRecord();
			Check(TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"), false), TEXT("act selection: Select act clicked"));
			Check(Audio()->IsMusicOn() && SinceOf(EGameSound::MenuMusic, true).Num() == 0, TEXT("act selection: the act list over the menu keeps the music"));
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), TEXT("act selection: the first act chosen"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("act selection: the music"), []() { CheckMusicStopped(TEXT("act selection: the act selection leaves the menu")); MarkRecord(); return 0.f; }));
		S.Add(WaitFor(TEXT("act selection: in the area"), []() { return Act() > 0 && AAreaGenerator::IsAreaReady(W()) && Pawn() && !Transitioning(); }, 120.f, []()
		{
			CheckNoMusic(TEXT("act selection: the area"));
			return 0.5f;
		}));

		// --- The ending: no music under it, back after it ---
		S.Add(Do(TEXT("ending: the travel to the ending"), []()
		{
			MarkRecord();
			Check(Run()->TravelToEnding(), TEXT("ending: the travel to the ending starts (as out of the last area)"));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("ending: the ending over the menu"), []() { return W() && MapName().EndsWith(TEXT("/MainMenu")) && !Transitioning() && Cast<URunEndingWidget>(TimeStateDebug::TopScreen(W())); }, 60.f, []()
		{
			return 2.f;
		}));
		S.Add(Do(TEXT("ending: 2 s under the ending"), []()
		{
			CheckNoMusic(TEXT("ending: under the ending's choice"));
			MarkRecord();
			Check(TimeStateDebug::ClickChoiceEntry(W(), 0), TEXT("ending: the first ending chosen"));
			return 0.2f;
		}));
		S.Add(WaitFor(TEXT("ending: its text read through"), []()
		{
			if (URunTextSceneWidget* Scene = TopScene())
			{
				Scene->Confirm();
				return false;
			}
			return InMenu();
		}, 30.f, []()
		{
			const TArray<FGameSoundRequest> Lines = SinceOf(EGameSound::Line);
			Check(Lines.Num() >= 1 && Count(EGameSound::Interruption) == 0, FString::Printf(TEXT("ending: one line sound for its text, no interruption: %s"), *Describe(Lines)));
			CheckMusicOnce(TEXT("ending: back in the menu after it"));
			return 0.f;
		}));
	}

	static void StartTest(const FString& Part, bool bQuit)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		// Brief 30: no entity wall for the run (it awakens the entity and measures contacts and grazes).
		if (!GWallsOff)
		{
			EntityWallsTest::SwitchOff(TEXT("tls.AudioTest"));
			GWallsOff = true;
		}
		GRun.bQuit = bQuit;
		GRun.Part = Part;
		if (Part == TEXT("local"))
		{
			AddLocal(GRun.Steps);
		}
		else if (Part == TEXT("menu"))
		{
			AddMenu(GRun.Steps);
		}
		else
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: unknown part '%s' (local | menu)."), Name(), *Part);
			return;
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s %s: %d steps."), Name(), *Part, GRun.Steps.Num());
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	static FAutoConsoleCommandWithWorldAndArgs GAudioTestCommand(
		TEXT("tls.AudioTest"),
		TEXT("tls.AudioTest <local|menu> [Quit]: Brief 24, the audio's requests. local: an area map (Area1_Hull). menu: MainMenu (clears the save slot)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartTest(Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("local")), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0);
		}));

	static FAutoConsoleCommandWithWorldAndArgs GAudioReportCommand(
		TEXT("tls.Audio.Report"),
		TEXT("tls.Audio.Report [Last]: logs every slot of Audio (sound, loaded, volume), the hatches' pitch, the falloff, the music's fades and state, and the last requests (default 20)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* InWorld)
		{
			const FGameAudio& A = UTheLastShiftSettings::Get()->Audio;
			for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
			{
				const EGameSound Which = static_cast<EGameSound>(I);
				const TSoftObjectPtr<USoundBase>& Ref = A.GetSound(Which);
				const USoundBase* Sound = Ref.LoadSynchronous();
				const USoundWave* Wave = Cast<USoundWave>(Sound);
				UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Report: %-12s %s%s, %s%s"), LexToString(Which),
					Ref.IsNull() ? TEXT("(empty)") : *Ref.ToString(), Ref.IsNull() || Sound ? TEXT("") : TEXT(" (MISSING)"),
					A.FindEffect(Which) ? *FString::Printf(TEXT("volume %.2f"), A.GetVolume(Which)) : TEXT("no volume (the file's level, under StdCore's Music volume)"),
					Wave ? *FString::Printf(TEXT(", %d ch, %d Hz, %.3f s%s"), Wave->NumChannels, static_cast<int32>(Wave->GetSampleRateForCurrentPlatform()), Wave->Duration,
						Wave->bLooping ? TEXT(", looping") : TEXT("")) : TEXT(""));
			}
			UWorld* World = ResolveGameWorld(InWorld);
			UGameAudioSubsystem* Sub = UGameAudioSubsystem::Get(World);
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Report: hatch pitch %.2f, falloff %.0f cm, music fades in %.1f s / out %.1f s; music %s."), A.GetHatchPitch(),
				A.GetWorldFalloffCm(), A.GetMusicFadeInSeconds(), A.GetMusicFadeOutSeconds(), Sub && Sub->IsMusicOn() ? TEXT("on") : TEXT("off"));
			if (!Sub)
			{
				return;
			}
			const int32 Last = Args.Num() > 0 ? FMath::Max(FCString::Atoi(*Args[0]), 1) : 20;
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Report: %d requests recorded (%d dropped); the last %d:"), Sub->GetRecord().Num(), Sub->GetRecordOffset(), Last);
			for (int32 I = FMath::Max(Sub->GetRecord().Num() - Last, 0); I < Sub->GetRecord().Num(); ++I)
			{
				UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Report:   #%d %s"), Sub->GetRecordOffset() + I, *DescribeRequest(Sub->GetRecord()[I]));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GAudioPlayCommand(
		TEXT("tls.Audio.Play"),
		TEXT("tls.Audio.Play <Slot> [Pitch]: plays a slot of Audio, the suit's (Scan, ScanReady, TimeStop, TimeResume, Slip, Line, Terminal, Door, Energy, Interruption, MenuMusic)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* InWorld)
		{
			EGameSound Sound;
			UGameAudioSubsystem* Sub = UGameAudioSubsystem::Get(ResolveGameWorld(InWorld));
			if (Args.Num() < 1 || !ParseSound(Args[0], Sound) || !Sub)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Audio.Play: usage: tls.Audio.Play <Slot> [Pitch] (in a game world)."));
				return;
			}
			if (Args.Num() > 1)
			{
				const APlayerController* C = ResolveGameWorld(InWorld)->GetFirstPlayerController();
				const APawn* P = C ? C->GetPawn() : nullptr;
				Sub->PlaySoundAt(Sound, P ? P->GetActorLocation() + P->GetActorForwardVector() * 200.f : FVector::ZeroVector, FCString::Atof(*Args[1]), TEXT("tls.Audio.Play"));
			}
			else
			{
				Sub->PlaySound(Sound, TEXT("tls.Audio.Play"));
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Play: %s."), *DescribeRequest(Sub->GetRecord().Last()));
		}));

	static FAutoConsoleCommandWithWorldAndArgs GAudioSetCommand(
		TEXT("tls.Audio.Set"),
		TEXT("tls.Audio.Set <Slot|All> Volume <V> | <Slot|All> Empty | <Slot|All> Restore | HatchPitch <P> | FalloffCm <Cm> | MusicFadeIn <s> | MusicFadeOut <s>: changes Audio for the session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Audio.Set: usage: see the command's help."));
				return;
			}
			const FString What = Args[0];
			const float Value = FCString::Atof(*Args[1]);
			if (What.Equals(TEXT("HatchPitch"), ESearchCase::IgnoreCase)) { MutableAudio().HatchPitch = Value; }
			else if (What.Equals(TEXT("FalloffCm"), ESearchCase::IgnoreCase)) { MutableAudio().WorldFalloffCm = Value; }
			else if (What.Equals(TEXT("MusicFadeIn"), ESearchCase::IgnoreCase)) { MutableAudio().MusicFadeInSeconds = Value; }
			else if (What.Equals(TEXT("MusicFadeOut"), ESearchCase::IgnoreCase)) { MutableAudio().MusicFadeOutSeconds = Value; }
			else
			{
				TArray<EGameSound> Sounds;
				EGameSound One;
				if (What.Equals(TEXT("All"), ESearchCase::IgnoreCase))
				{
					for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
					{
						Sounds.Add(static_cast<EGameSound>(I));
					}
				}
				else if (ParseSound(What, One))
				{
					Sounds.Add(One);
				}
				else
				{
					UE_LOG(LogTheLastShift, Warning, TEXT("tls.Audio.Set: unknown slot or value '%s'."), *What);
					return;
				}
				const FString Op = Args[1];
				for (const EGameSound Sound : Sounds)
				{
					if (Op.Equals(TEXT("Restore"), ESearchCase::IgnoreCase))
					{
						if (GSavedAudio.IsSet())
						{
							MutableAudio().GetSound(Sound) = GSavedAudio.GetValue().GetSound(Sound);
							if (FGameSoundSlot* Slot = MutableAudio().FindEffect(Sound))
							{
								Slot->Volume = GSavedAudio.GetValue().FindEffect(Sound)->Volume;
							}
						}
					}
					else if (Op.Equals(TEXT("Empty"), ESearchCase::IgnoreCase))
					{
						MutableAudio().GetSound(Sound).Reset();
					}
					else if (Op.Equals(TEXT("Volume"), ESearchCase::IgnoreCase) && Args.Num() > 2)
					{
						if (FGameSoundSlot* Slot = MutableAudio().FindEffect(Sound))
						{
							Slot->Volume = FCString::Atof(*Args[2]);
						}
						else
						{
							UE_LOG(LogTheLastShift, Warning, TEXT("tls.Audio.Set: %s has no volume (the file's level, under StdCore's Music volume)."), LexToString(Sound));
						}
					}
				}
				if (What.Equals(TEXT("All"), ESearchCase::IgnoreCase) && Op.Equals(TEXT("Restore"), ESearchCase::IgnoreCase))
				{
					RestoreAudio();
				}
			}
			const FGameAudio& A = UTheLastShiftSettings::Get()->Audio;
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Audio.Set %s: hatch pitch %.2f, falloff %.0f cm, fades %.1f / %.1f s; slots: %s"), *FString::Join(Args, TEXT(" ")), A.GetHatchPitch(),
				A.GetWorldFalloffCm(), A.GetMusicFadeInSeconds(), A.GetMusicFadeOutSeconds(), *[&A]()
				{
					FString Out;
					for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
					{
						const EGameSound Which = static_cast<EGameSound>(I);
						const TSoftObjectPtr<USoundBase>& Ref = A.GetSound(Which);
						Out += FString::Printf(TEXT("%s%s=%s@%.2f"), Out.IsEmpty() ? TEXT("") : TEXT(", "), LexToString(Which),
							Ref.IsNull() ? TEXT("empty") : *Ref.GetAssetName(), A.GetVolume(Which));
					}
					return Out;
				}());
		}));
}

#endif // !UE_BUILD_SHIPPING
