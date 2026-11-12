// SPDX-License-Identifier: MIT

// Debug console commands for the home AI's text channel (Brief 6): tls.Text.Push, tls.Text.Trigger, tls.Text.Say,
// tls.Text.Print, tls.Text.State and tls.TextChannelTest. The battery is set with tls.Home.Set. tls.TextChannelTest
// runs on the core ticker, so it survives map changes. Development builds only.

#include "Debug/HomeTestListener.h"
#include "Debug/TextTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunActListWidget.h"
#include "Run/RunSubsystem.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UI/StdRadialProgress.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace TextDebug
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

	static UTextChannelComponent* ChannelOf(UWorld* World)
	{
		const APlayerController* C = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* P = C ? C->GetPawn() : nullptr;
		return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr;
	}

	template <typename T>
	static T* FindWidget(UWorld* World)
	{
		TArray<UUserWidget*> Found;
		if (World)
		{
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, T::StaticClass(), false);
		}
		return Found.Num() > 0 ? Cast<T>(Found[0]) : nullptr;
	}

	static AEntity* Entity()
	{
		if (UWorld* World = W())
		{
			for (TActorIterator<AEntity> It(World); It; ++It) { return *It; }
		}
		return nullptr;
	}

	static APowerTerminal* Terminal()
	{
		if (UWorld* World = W())
		{
			for (TActorIterator<APowerTerminal> It(World); It; ++It)
			{
				if (It->HasUnpoweredTarget()) { return *It; }
			}
		}
		return nullptr;
	}

	/** The context of a debug push: the home AI's (home time, battery, last Stop), the act's jump size, the battery overridden when given. */
	static FTextChannelContext SampleContext(UWorld* World, TOptional<double> Battery)
	{
		FTextChannelContext Context;
		const APlayerController* C = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* P = C ? C->GetPawn() : nullptr;
		if (const UHomeAIComponent* AI = P ? P->FindComponentByClass<UHomeAIComponent>() : nullptr)
		{
			Context = AI->MakeContext();
		}
		FActTuning Tuning;
		if (UTheLastShiftSettings::Get()->GetActTuning(URunSubsystem::GetActIndexOf(World), Tuning))
		{
			Context.JumpSeconds = Tuning.JumpHomeSeconds;
		}
		if (Battery.IsSet())
		{
			Context.BatteryFraction = FMath::Clamp(Battery.GetValue(), 0.0, 1.0);
		}
		return Context;
	}

	static FString ContextLine(const FTextChannelContext& C)
	{
		return FString::Printf(TEXT("still %.1f s, jump %.0f s, home %.0f s, battery %.4f"), C.StoppedSeconds, C.JumpSeconds, C.HomeSecondsElapsed, C.BatteryFraction);
	}

	static TOptional<double> OptionalBattery(const TArray<FString>& Args, int32 Index)
	{
		return Args.IsValidIndex(Index) ? TOptional<double>(FCString::Atod(*Args[Index])) : TOptional<double>();
	}

	static bool ParseTrigger(const FString& Text, ETextTrigger& Out)
	{
		const UEnum* Enum = StaticEnum<ETextTrigger>();
		const int64 Value = Enum ? Enum->GetValueByNameString(Text, EGetByNameFlags::None) : INDEX_NONE;
		if (Value == INDEX_NONE)
		{
			return false;
		}
		Out = static_cast<ETextTrigger>(Value);
		return true;
	}

	/** Prints a row at every level with Context. */
	static void PrintRow(FName RowName, const FTextChannelContext& Context)
	{
		const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
		const FAIMessageRow* Row = Table ? Table->FindRow<FAIMessageRow>(RowName, TEXT("tls.Text.Print"), false) : nullptr;
		if (!Row)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.Print: no row '%s' in %s."), *RowName.ToString(), *GetNameSafe(Table));
			return;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Text.Print %s (%s):"), *RowName.ToString(), *ContextLine(Context));
		for (const ETextDegradation Level : { ETextDegradation::Full, ETextDegradation::NoArticles, ETextDegradation::Abbreviated, ETextDegradation::Scattered, ETextDegradation::Silent })
		{
			const FString Text = UTextChannelComponent::ComposeMessage(*Row, Level, Context);
			UE_LOG(LogTheLastShift, Display, TEXT("  %-11s \"%s\""), LexToString(Level), *Text);
		}
	}

	// --- Warnings of the channel during a test ---

	class FWarningCounter : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == LogTextChannel.GetCategoryName() && (Verbosity & ELogVerbosity::VerbosityMask) <= ELogVerbosity::Warning)
			{
				++Count;
				Lines.Add(Message);
			}
		}

		int32 Count = 0;
		TArray<FString> Lines;
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
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		TStrongObjectPtr<UTlsTextTestListener> Text;
		TStrongObjectPtr<UTlsHomeTestListener> Jumps;
		TSharedPtr<FWarningCounter> Warnings;
		TWeakObjectPtr<UWorld> TravelFrom;
		TWeakObjectPtr<UTextChannelComponent> BoundChannel;
		/** Values kept between steps. */
		int32 SavedStarts = 0;
		int32 SavedJumps = 0;
		float SavedGrazeRadius = 0.f;
		int32 SavedWarnings = 0;
		TMap<FString, double> TypingSeconds;
		TMap<FString, double> TypingRate;
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
		if (GRun.Text)
		{
			GRun.Text->Unbind();
			GRun.Text.Reset();
		}
		if (GRun.Jumps)
		{
			GRun.Jumps->Unbind();
			GRun.Jumps.Reset();
		}
		if (GRun.Warnings)
		{
			GLog->RemoveOutputDevice(GRun.Warnings.Get());
			GRun.Warnings.Reset();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
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

	// --- Helpers of the PIE test ---

	static int32 Starts() { return GRun.Text ? GRun.Text->Started.Num() : 0; }
	static FName LastStarted() { return GRun.Text && GRun.Text->Started.Num() > 0 ? GRun.Text->Started.Last() : NAME_None; }

	static int32 StartsOf(const FString& Part)
	{
		int32 N = 0;
		if (GRun.Text)
		{
			for (const FName& Name : GRun.Text->Started)
			{
				N += Name.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return N;
	}

	static void BindChannel()
	{
		UTextChannelComponent* C = Channel();
		if (!GRun.Text)
		{
			GRun.Text.Reset(NewObject<UTlsTextTestListener>());
			GRun.Text->bUseRealClock = true;
		}
		GRun.Text->Bind(C);
	}

	static void StopTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
		{
			T->Stop();
		}
	}

	static void ResumeTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume(EStopExitReason::Resumed);
		}
	}

	static void Shot(const FString& Name)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief6"), Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	static FString Expected(FName RowName, const FTextChannelContext& Context)
	{
		const UDataTable* Table = UTheLastShiftSettings::Get()->LoadAIMessagesTable();
		const FAIMessageRow* Row = Table ? Table->FindRow<FAIMessageRow>(RowName, TEXT("Test"), false) : nullptr;
		const UTextChannelComponent* C = Channel();
		return Row && C ? UTextChannelComponent::ComposeMessage(*Row, C->GetLevelForBattery(Context.BatteryFraction), Context) : FString();
	}

	static float CapsuleHalfHeight()
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	}

	static void PlacePawn(const FVector& FloorLocation, const FVector& LookAt)
	{
		APawn* P = Pawn();
		if (!P)
		{
			return;
		}
		P->SetActorLocation(FloorLocation + FVector(0.f, 0.f, CapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		if (APlayerController* Controller = PC())
		{
			Controller->SetControlRotation((LookAt - P->GetPawnViewLocation()).Rotation());
		}
	}

	static FBox2D RectOf(const UWidget* Widget)
	{
		if (!Widget || !Widget->IsVisible())
		{
			return FBox2D(ForceInit);
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D TopLeft = Geometry.GetAbsolutePosition();
		return FBox2D(TopLeft, TopLeft + Geometry.GetAbsoluteSize());
	}

	static FString RectLine(const FBox2D& Box)
	{
		return Box.bIsValid ? FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y) : FString(TEXT("none"));
	}

	/** No new message within Seconds. */
	static void AddSilence(TArray<FStep>& S, const FString& Label, float Seconds, const FString& Part = FString())
	{
		S.Add(Do(Label + TEXT(": start"), []() { GRun.SavedStarts = Starts(); return 0.f; }));
		S.Add(Do(Label + TEXT(": wait"), [Seconds]() { return Seconds; }));
		S.Add(Do(Label, [Label, Part]()
		{
			const int32 New = Starts() - GRun.SavedStarts;
			Check(New == 0, FString::Printf(TEXT("%s: no message (%d started%s)"), *Label, New, New > 0 ? *FString::Printf(TEXT(", last %s"), *LastStarted().ToString()) : TEXT("")));
			return 0.f;
		}));
	}

	static FStep WaitIdle(const FString& Label)
	{
		return WaitFor(Label + TEXT(": channel idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 90.f, []() { return 1.0f; });
	}

	/** Waits for a world other than the one the travel left, with its area ready and no transition; binds the listener to its channel as soon as it exists. */
	static FStep WaitArrival(const FString& Label, int32 ExpectedAct)
	{
		return WaitFor(Label, []()
		{
			UWorld* World = W();
			if (!World || World == GRun.TravelFrom.Get())
			{
				return false;
			}
			// Bound as soon as the new pawn exists: the area's first messages may start in the frame it becomes ready.
			if (UTextChannelComponent* C = ChannelOf(World); C && GRun.BoundChannel.Get() != C)
			{
				GRun.BoundChannel = C;
				BindChannel();
			}
			const UGameInstance* GI = World->GetGameInstance();
			const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			const AAreaGenerator* Gen = AAreaGenerator::Find(World);
			return (!F || !F->IsTransitioning()) && (!Gen || Gen->IsReady()) && World->GetFirstPlayerController() != nullptr;
		}, 120.f, [Label, ExpectedAct]()
		{
			Check(Act() == ExpectedAct, FString::Printf(TEXT("%s: arrived in %s, act %d (expected %d)"), *Label, *UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()), Act(), ExpectedAct));
			if (GRun.Jumps && Home())
			{
				GRun.Jumps->Bind(Home());
			}
			GRun.TravelFrom.Reset();
			return 0.5f;
		});
	}

	/** Travels to the next area with the battery at Battery; checks the label against the destination's AreaEntry row (or none). */
	static void AddTravel(TArray<FStep>& S, int32 FromAct, double Battery, bool bExpectLabel, const FString& ShotName)
	{
		const int32 ToAct = FromAct + 1;
		const FString Label = FString::Printf(TEXT("travel %d -> %d at battery %.2f"), FromAct, ToAct, Battery);
		S.Add(WaitIdle(Label));
		S.Add(Do(Label, [Label, ToAct, Battery, bExpectLabel]()
		{
			Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
			const FName RowName(*FString::Printf(TEXT("A%d_AreaEntry_1"), ToAct));
			FTextChannelContext Context;
			Context.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
			Context.BatteryFraction = Home()->GetBatteryFraction();
			const FString Want = bExpectLabel ? Expected(RowName, Context) : FString();
			const ETextDegradation Level = Channel() ? Channel()->GetLevelForBattery(Battery) : ETextDegradation::Full;
			GRun.TravelFrom = W();
			const bool bStarted = Run()->TravelToNextArea();
			const FString Got = Run()->GetLastTravelLabel().ToString();
			const float Hold = UTheLastShiftSettings::Get()->TravelHoldSeconds;
			const float Reading = Channel() ? Channel()->GetHoldSecondsFor(Got.Len()) : 0.f;
			Check(bStarted && Got == Want, FString::Printf(TEXT("%s: label at %s = %s row: \"%s\" (expected \"%s\")"), *Label, LexToString(Level),
				bExpectLabel ? *RowName.ToString() : TEXT("no"), *Got, *Want));
			Check(FMath::IsNearlyEqual(Run()->GetLastTravelMinCoveredSeconds(), FMath::Max(Hold, Got.IsEmpty() ? 0.f : Reading), 0.01f),
				FString::Printf(TEXT("%s: covered at least %.2f s = max(TravelHoldSeconds %.1f, reading time %.2f for %d characters)"), *Label,
					Run()->GetLastTravelMinCoveredSeconds(), Hold, Got.IsEmpty() ? 0.f : Reading, Got.Len()));
			return bStarted ? 0.f : -1.f;
		}));
		if (!ShotName.IsEmpty())
		{
			S.Add(WaitFor(Label + TEXT(": covered"), []()
			{
				const UGameInstance* GI = W() ? W()->GetGameInstance() : nullptr;
				const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
				return F && F->GetCoveredSeconds() > 1.0f;
			}, 30.f, [ShotName]() { Shot(ShotName); return 0.f; }));
		}
		S.Add(WaitArrival(Label + TEXT(": arrival"), ToAct));
		S.Add(Do(Label + TEXT(": black time"), [Label]()
		{
			const float Black = Run()->GetLastTravelToFadeInSeconds() - Run()->GetLastTravelToCoveredSeconds();
			const float Min = Run()->GetLastTravelMinCoveredSeconds();
			Check(Black + 0.05f >= Min, FString::Printf(TEXT("%s: black for %.2f s, at least %.2f s"), *Label, Black, Min));
			GRun.SavedWarnings = GRun.Warnings ? GRun.Warnings->Count : 0;
			return 0.f;
		}));
		if (ToAct > 1)
		{
			AddSilence(S, FString::Printf(TEXT("area %d start"), ToAct), 2.5f);
			S.Add(Do(FString::Printf(TEXT("area %d start: no warning"), ToAct), [ToAct]()
			{
				const int32 New = (GRun.Warnings ? GRun.Warnings->Count : 0) - GRun.SavedWarnings;
				Check(New == 0, FString::Printf(TEXT("area %d: no AreaStart row, no message and no warning (%d warnings)"), ToAct, New));
				return 0.f;
			}));
		}
	}

	/** Stop, the figure at the minimum distance and the pawn on it: the map's contact response ends the Stop. */
	static void AddContact(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": stop and contact"), [Label]()
		{
			AEntity* E = Entity();
			APawn* P = Pawn();
			if (!E || !P)
			{
				Check(false, Label + TEXT(": needs an entity and a pawn"));
				return -1.f;
			}
			GRun.SavedStarts = Starts();
			GRun.SavedJumps = GRun.Jumps->Jumps;
			StopTime();
			const bool bSpawned = E->SpawnNow(E->MinSpawnDistance);
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(E->GetFigureLocation() + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Note(FString::Printf(TEXT("%s: stopped in act %d, response %s, spawned %d"), *Label, Act(), LexToString(E->ContactResponse), bSpawned ? 1 : 0));
			return bSpawned ? 0.f : -1.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the contact ends the Stop"), []() { return !IsStopped(); }, 10.f));
	}

	/** Checks that the jump just made started its act's first JumpReturn row with the four numbers of that jump. */
	static FStep CheckJumpMessage(const FString& Label, int32 ExpectedAct)
	{
		return WaitFor(Label + TEXT(": JumpReturn message"), []() { return Starts() > GRun.SavedStarts; }, 5.f, [Label, ExpectedAct]()
		{
			const FHomeJump& J = GRun.Jumps->LastJump;
			FTextChannelContext Context;
			Context.StoppedSeconds = J.StoppedSeconds;
			Context.JumpSeconds = J.HomeSecondsAdded;
			Context.HomeSecondsElapsed = Home()->GetHomeSecondsElapsed();
			Context.BatteryFraction = J.BatteryAfter;
			const FName Row = LastStarted();
			const FString Got = GRun.Text->StartedTexts.Last();
			const FString Want = Expected(Row, Context);
			Check(GRun.Jumps->Jumps == GRun.SavedJumps + 1 && J.ActIndex == ExpectedAct && Row.ToString().StartsWith(FString::Printf(TEXT("A%d_JumpReturn_"), ExpectedAct)) && Got == Want,
				FString::Printf(TEXT("%s: %s jump in act %d (%s) shows %s: \"%s\" (expected \"%s\")"), *Label, LexToString(J.Cause), J.ActIndex, *ContextLine(Context),
					*Row.ToString(), *Got, *Want));
			return 0.f;
		});
	}

	// --- tls.TextChannelTest ---

	static void Build(TArray<FStep>& S)
	{
		// Started with -ExecCmds, the main menu is still fading in: a new run is refused until the flow is idle.
		S.Add(WaitFor(TEXT("main menu ready"), []()
		{
			const UGameInstance* GI = W() ? W()->GetGameInstance() : nullptr;
			const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return F && !F->IsTransitioning() && W()->GetFirstPlayerController() != nullptr;
		}, 30.f, []() { return 0.5f; }));
		S.Add(Do(TEXT("setup"), []()
		{
			if (!Run() || !Home() || !Energy() || Act() != 0)
			{
				Check(false, TEXT("needs a fresh PIE in MainMenu (act 0) with the run, home and energy subsystems"));
				return -1.f;
			}
			GRun.Text.Reset(NewObject<UTlsTextTestListener>());
			GRun.Text->bUseRealClock = true;
			GRun.Jumps.Reset(NewObject<UTlsHomeTestListener>());
			GRun.Warnings = MakeShared<FWarningCounter>();
			GLog->AddOutputDevice(GRun.Warnings.Get());
			Note(FString::Printf(TEXT("tables: messages %s, endings %s"), *GetNameSafe(UTheLastShiftSettings::Get()->LoadAIMessagesTable()),
				*GetNameSafe(UTheLastShiftSettings::Get()->LoadEndingsTable())));
			GRun.TravelFrom = W();
			const bool bStarted = Run()->StartNewRun();
			Check(bStarted, TEXT("new run started (save cleared, battery 1)"));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitArrival(TEXT("new run: area 1"), 1));
		S.Add(Do(TEXT("input after the travel"), []()
		{
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(TimeStateDebug::BuildInputReport(W()).IsGameReady(), TEXT("input after the travel to area 1: game ready"));
			return 0.f;
		}));

		// Criterion 10 (area start) and 9 (Normal time only).
		S.Add(WaitFor(TEXT("area 1: the first AreaStart row is being typed"), []()
		{
			const UTextChannelComponent* C = Channel();
			return C && C->GetCurrentRowName() == FName(TEXT("A1_AreaStart_1")) && C->GetVisibleText().Len() >= 12;
		}, 20.f, []()
		{
			UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = FindWidget<UTextChannelWidget>(W());
			Check(Widget && Widget->IsShown() && Widget->GetShownText() == C->GetVisibleText(), FString::Printf(TEXT("the widget shows the channel's text: \"%s\""),
				Widget ? *Widget->GetShownText() : TEXT("<no widget>")));
			GRun.SavedStarts = Starts();
			StopTime();
			Check(!C->IsVisible() && C->GetVisibleText().IsEmpty() && Widget && !Widget->IsShown(),
				TEXT("a Stop during a message hides it at once (channel and widget)"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("stopped: nothing visible"), []()
		{
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = FindWidget<UTextChannelWidget>(W());
			Check(IsStopped() && !C->IsVisible() && C->GetVisibleText().IsEmpty() && !Widget->IsShown() && C->GetCurrentRowName() == FName(TEXT("A1_AreaStart_1")),
				FString::Printf(TEXT("1 s Stopped: nothing visible, current message kept (%s), %d queued"), *C->GetCurrentRowName().ToString(), C->GetQueuedCount()));
			ResumeTime();
			Check(C->IsVisible() && C->GetVisibleText().Len() <= 1 && C->GetCurrentRowName() == FName(TEXT("A1_AreaStart_1")) && Starts() == GRun.SavedStarts,
				FString::Printf(TEXT("Resume: the same message restarts from its first character (\"%s\")"), *C->GetVisibleText()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 1: the three AreaStart rows"), []() { const UTextChannelComponent* C = Channel(); return StartsOf(TEXT("AreaStart")) >= 3 && C && !C->IsBusy(); }, 60.f, []()
		{
			const FString Got = FString::JoinBy(GRun.Text->Started, TEXT(","), [](const FName& N) { return N.ToString(); });
			Check(Got == TEXT("A1_AreaStart_1,A1_AreaStart_2,A1_AreaStart_3"), FString::Printf(TEXT("area 1 shows the three AreaStart rows in order: %s"), *Got));
			return 1.0f;
		}));

		// Criterion 9: pushed while Stopped, it waits for Normal time.
		S.Add(Do(TEXT("push while Stopped"), []()
		{
			GRun.SavedStarts = Starts();
			StopTime();
			Channel()->PushRow(TEXT("A1_Spend_1"), SampleContext(W(), TOptional<double>()));
			return 1.0f;
		}));
		S.Add(Do(TEXT("push while Stopped: waits"), []()
		{
			Check(Starts() == GRun.SavedStarts && !Channel()->IsVisible() && Channel()->GetQueuedCount() == 1,
				FString::Printf(TEXT("a message pushed while Stopped waits (%d started, %d queued)"), Starts() - GRun.SavedStarts, Channel()->GetQueuedCount()));
			ResumeTime();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("push while Stopped: shown in Normal time"), []() { return Starts() > GRun.SavedStarts; }, 3.f, []()
		{
			Check(LastStarted() == FName(TEXT("A1_Spend_1")) && !IsStopped(), TEXT("the waiting message starts in Normal time"));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("after the waiting message")));

		// Criterion 10: first energy, once.
		S.Add(Do(TEXT("first banking: stop and collect"), []()
		{
			GRun.SavedStarts = Starts();
			Check(Energy()->GetBankedEnergy() == 0, FString::Printf(TEXT("banked energy is 0 before (%d)"), Energy()->GetBankedEnergy()));
			StopTime();
			Energy()->AddSessionEnergy(1);
			return 0.3f;
		}));
		S.Add(Do(TEXT("first banking: resume"), []() { ResumeTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("first banking: FirstEnergy"), []() { return Starts() > GRun.SavedStarts; }, 3.f, []()
		{
			Check(LastStarted() == FName(TEXT("A1_FirstEnergy_1")) && StartsOf(TEXT("FirstEnergy")) == 1 && Energy()->GetBankedEnergy() == 1,
				FString::Printf(TEXT("the first banked energy shows A1_FirstEnergy_1 once (%s, banked %d)"), *LastStarted().ToString(), Energy()->GetBankedEnergy()));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("after FirstEnergy")));
		S.Add(Do(TEXT("second banking: stop and collect"), []() { StopTime(); Energy()->AddSessionEnergy(1); return 0.3f; }));
		S.Add(Do(TEXT("second banking: resume"), []() { ResumeTime(); return 0.f; }));
		AddSilence(S, TEXT("second banking in the same map"), 2.f);

		// Criterion 11 and 15: the layout at a terminal, then a payment.
		S.Add(WaitIdle(TEXT("before the terminal")));
		S.Add(Do(TEXT("terminal: place the pawn"), []()
		{
			APowerTerminal* T = Terminal();
			if (!T)
			{
				Check(false, TEXT("area 1 has a terminal with something to power"));
				return -1.f;
			}
			PlacePawn(T->GetActorLocation() + T->GetActorForwardVector() * 110.f, T->GetActorLocation() + FVector(0.f, 0.f, 60.f));
			if (Energy()->GetBankedEnergy() < T->GetCost())
			{
				Energy()->AddBankedEnergy(T->GetCost() - Energy()->GetBankedEnergy());
			}
			Channel()->PushRow(TEXT("A1_JumpReturn_1"), SampleContext(W(), TOptional<double>()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("layout: a message and the Interact prompt"), []()
		{
			const UInteractPromptWidget* Prompt = FindWidget<UInteractPromptWidget>(W());
			return Channel()->GetVisibleText().Len() >= 60 && Prompt && Prompt->GetMode() == EInteractPromptMode::Power;
		}, 10.f, []()
		{
			const UTextChannelWidget* Text = FindWidget<UTextChannelWidget>(W());
			const UInteractPromptWidget* Prompt = FindWidget<UInteractPromptWidget>(W());
			const FBox2D TextRect = Text ? Text->GetShownRect() : FBox2D(ForceInit);
			const FBox2D PromptRect = Prompt && Prompt->WidgetTree ? RectOf(Prompt->WidgetTree->FindWidget(TEXT("Border_Prompt"))) : FBox2D(ForceInit);
			FBox2D RingRect(ForceInit);
			TArray<UUserWidget*> Cues;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W(), Cues, UUserWidget::StaticClass(), false);
			for (UUserWidget* Cue : Cues)
			{
				if (Cue && Cue->WidgetTree)
				{
					Cue->WidgetTree->ForEachWidget([&RingRect](UWidget* Child)
					{
						if (Cast<UStdRadialProgress>(Child) && Child->IsVisible())
						{
							RingRect += RectOf(Child);
						}
					});
				}
			}
			const bool bPromptOverlap = TextRect.bIsValid && PromptRect.bIsValid && TextRect.Intersect(PromptRect);
			const bool bRingOverlap = TextRect.bIsValid && RingRect.bIsValid && TextRect.Intersect(RingRect);
			Check(TextRect.bIsValid && PromptRect.bIsValid && !bPromptOverlap && !bRingOverlap,
				FString::Printf(TEXT("layout: message %s, Interact prompt %s (\"%s\"), visible rings %s: no overlap"), *RectLine(TextRect), *RectLine(PromptRect),
					Prompt ? *Prompt->GetShownText() : TEXT(""), *RectLine(RingRect)));
			Shot(TEXT("Layout_Message_Prompt_Terminal"));
			return 1.0f;
		}));
		S.Add(Do(TEXT("terminal: pay"), []()
		{
			Channel()->ClearAll();
			GRun.SavedStarts = Starts();
			const bool bPaid = Terminal() && Terminal()->TryActivate(Pawn());
			Check(bPaid && Starts() == GRun.SavedStarts + 1 && LastStarted() == FName(TEXT("A1_Spend_1")),
				FString::Printf(TEXT("a successful payment shows A1_Spend_1 (paid %d, %s)"), bPaid ? 1 : 0, *LastStarted().ToString()));
			return 0.f;
		}));
		S.Add(WaitIdle(TEXT("after the payment")));
		S.Add(Do(TEXT("terminal: refused"), []()
		{
			Energy()->RestoreBankedEnergy(0);
			GRun.SavedStarts = Starts();
			APowerTerminal* T = nullptr;
			for (TActorIterator<APowerTerminal> It(W()); It; ++It) { T = *It; break; }
			const bool bPaid = T && T->TryActivate(Pawn());
			Note(FString::Printf(TEXT("refused payment at %s: %d"), *GetNameSafe(T), bPaid ? 0 : 1));
			return 0.f;
		}));
		AddSilence(S, TEXT("a refused payment"), 1.5f);

		// Criterion 8, act 1: no message on a plain Resume (all the ones above) or a rewind; one on a graze.
		S.Add(Do(TEXT("no JumpReturn so far"), []()
		{
			Check(StartsOf(TEXT("JumpReturn_")) == 1 && GRun.Jumps->Jumps == 0,
				FString::Printf(TEXT("plain Resumes: no jump and no JumpReturn but the one pushed for the layout (%d jumps)"), GRun.Jumps->Jumps));
			return 0.f;
		}));
		AddContact(S, TEXT("act 1 rewind"));
		S.Add(Do(TEXT("act 1 rewind: no jump"), []()
		{
			Check(GRun.Jumps->Jumps == GRun.SavedJumps, TEXT("an act 1 contact rewinds without a jump"));
			return 0.f;
		}));
		AddSilence(S, TEXT("act 1 rewind"), 2.f);
		S.Add(Do(TEXT("act 1 graze: stop"), []()
		{
			AEntity* E = Entity();
			GRun.SavedGrazeRadius = E->GrazeRadius;
			E->GrazeRadius = 400.f;
			StopTime();
			return E->SpawnNow(E->MinSpawnDistance) ? 0.1f : -1.f;
		}));
		S.Add(Do(TEXT("act 1 graze: place the pawn"), []()
		{
			AEntity* E = Entity();
			APawn* P = Pawn();
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
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(Target + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			GRun.SavedStarts = Starts();
			GRun.SavedJumps = GRun.Jumps->Jumps;
			return 0.05f;
		}));
		S.Add(Do(TEXT("act 1 graze: resume"), []()
		{
			ResumeTime();
			Entity()->GrazeRadius = GRun.SavedGrazeRadius;
			return 0.f;
		}));
		S.Add(CheckJumpMessage(TEXT("act 1 graze"), 1));

		// Criterion 13: the same row at four battery levels.
		for (const double Battery : { 0.65, 0.35, 0.05, 0.02 })
		{
			const FString Label = FString::Printf(TEXT("battery %.2f"), Battery);
			S.Add(WaitIdle(Label));
			S.Add(Do(Label + TEXT(": push"), [Battery]()
			{
				Home()->RestoreState(Battery, Home()->GetHomeSecondsElapsed());
				GRun.SavedStarts = Starts();
				Channel()->PushRow(TEXT("A1_JumpReturn_1"), SampleContext(W(), TOptional<double>()));
				return 0.f;
			}));
			if (Battery >= 0.03)
			{
				const ETextDegradation Want = Battery >= 0.40 ? ETextDegradation::NoArticles : Battery >= 0.10 ? ETextDegradation::Abbreviated : ETextDegradation::Scattered;
				S.Add(WaitFor(Label + TEXT(": typed"), []()
				{
					const UTextChannelComponent* C = Channel();
					return Starts() > GRun.SavedStarts && C->GetVisibleText().Len() > 0 && C->GetVisibleText().Len() == C->GetCurrentText().Len();
				}, 30.f, [Label, Want]()
				{
					const double Seconds = GRun.Text->LastTypingSeconds;
					const FString Text = GRun.Text->StartedTexts.Last();
					GRun.TypingSeconds.Add(Label, Seconds);
					GRun.TypingRate.Add(Label, Seconds > 0.0 ? Text.Len() / Seconds : 0.0);
					Check(GRun.Text->StartedLevels.Last() == Want && LastStarted() == FName(TEXT("A1_JumpReturn_1")),
						FString::Printf(TEXT("%s: A1_JumpReturn_1 at %s, %d characters typed in %.2f s (%.1f chars/s): \"%s\""), *Label,
							LexToString(GRun.Text->StartedLevels.Last()), Text.Len(), Seconds, Seconds > 0.0 ? Text.Len() / Seconds : 0.0, *Text));
					return 0.f;
				}));
			}
			else
			{
				AddSilence(S, Label + TEXT(": Silent"), 2.f);
			}
		}
		S.Add(Do(TEXT("battery: slower typing"), []()
		{
			const double Fast = GRun.TypingRate.FindRef(TEXT("battery 0.65"));
			const double Slow = GRun.TypingRate.FindRef(TEXT("battery 0.05"));
			Check(Slow > 0.0 && Fast > Slow * 1.5, FString::Printf(TEXT("typing at 0.05 (%.1f chars/s, %.2f s) is slower than at 0.65 (%.1f chars/s, %.2f s)"), Slow,
				GRun.TypingSeconds.FindRef(TEXT("battery 0.05")), Fast, GRun.TypingSeconds.FindRef(TEXT("battery 0.65"))));
			return 0.f;
		}));

		// Criterion 12: travel labels; criterion 8 in act 2; criterion 10 in areas 2 to 4.
		AddTravel(S, 1, 1.0, true, TEXT("Travel_Area1_to_Area2_Full"));
		S.Add(Do(TEXT("input after the travel to area 2"), []()
		{
			TimeStateDebug::LogInputReport(W(), *GRun.Name);
			Check(TimeStateDebug::BuildInputReport(W()).IsGameReady(), TEXT("input after the travel to area 2: game ready"));
			return 0.f;
		}));
		AddContact(S, TEXT("act 2 contact"));
		S.Add(CheckJumpMessage(TEXT("act 2 contact"), 2));
		AddTravel(S, 2, 0.35, true, TEXT("Travel_Area2_to_Area3_Abbreviated"));
		AddTravel(S, 3, 0.02, false, TEXT("Travel_Area3_to_Area4_Silent"));

		// Criterion 14: the loss.
		S.Add(WaitIdle(TEXT("before the loss")));
		S.Add(Do(TEXT("loss: queue two messages, then a jump to zero"), []()
		{
			Home()->RestoreState(0.5, Home()->GetHomeSecondsElapsed());
			Channel()->PushRow(TEXT("A4_Spend_1"), SampleContext(W(), TOptional<double>()));
			Channel()->PushRow(TEXT("A4_JumpReturn_2"), SampleContext(W(), TOptional<double>()));
			Home()->RestoreState(0.02, Home()->GetHomeSecondsElapsed());
			Note(FString::Printf(TEXT("before the loss: busy %d, %d queued"), Channel()->IsBusy() ? 1 : 0, Channel()->GetQueuedCount()));
			Home()->ApplyJump(EHomeJumpCause::Contact, 4, 1.f);
			return 0.5f;
		}));
		S.Add(Do(TEXT("loss: channel and screen"), []()
		{
			const UTextChannelComponent* C = Channel();
			const UTextChannelWidget* Widget = FindWidget<UTextChannelWidget>(W());
			const URunActListWidget* Screen = Run()->GetLossScreen();
			const FText Loss = UTheLastShiftSettings::Get()->GetEndingText(TEXT("Loss"));
			Check(Home()->IsRunLost() && C && !C->IsVisible() && !C->IsBusy() && C->GetQueuedCount() == 0 && C->IsSuspended() && Widget && !Widget->IsShown(),
				FString::Printf(TEXT("loss: the channel is hidden and empty (busy %d, %d queued, suspended %d)"), C && C->IsBusy() ? 1 : 0, C ? C->GetQueuedCount() : -1, C && C->IsSuspended() ? 1 : 0));
			Check(Screen && !Loss.IsEmpty() && Screen->GetBody().EqualTo(Loss) && Screen->GetEntryCount() > 0,
				FString::Printf(TEXT("loss screen body is the Loss row in full: \"%s\", above %d entries [%s]"), Screen ? *Screen->GetBody().ToString() : TEXT("<no screen>"),
					Screen ? Screen->GetEntryCount() : 0, *TimeStateDebug::DescribeChoiceList(W())));
			return 1.5f;
		}));
		S.Add(Do(TEXT("loss: screenshot"), []() { Shot(TEXT("Loss_Screen")); return 1.0f; }));
		S.Add(Do(TEXT("main menu: travel"), []()
		{
			GRun.TravelFrom = W();
			const bool bStarted = Run()->ReturnToMainMenu();
			Check(bStarted && Run()->GetLastTravelLabel().IsEmpty() && Run()->GetLastTravelMinCoveredSeconds() == 0.f, TEXT("travel to the main menu has no label"));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitArrival(TEXT("main menu: arrival"), 0));
		S.Add(Do(TEXT("warnings"), []()
		{
			const int32 Count = GRun.Warnings ? GRun.Warnings->Count : -1;
			Check(Count == 0, FString::Printf(TEXT("no text channel warning during the run (%d%s)"), Count,
				Count > 0 ? *FString::Printf(TEXT(": %s"), *GRun.Warnings->Lines[0]) : TEXT("")));
			return 0.f;
		}));
	}

	static bool StartRun(const FString& Name, TArray<FStep>&& Steps)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: %s is already running."), *Name, *GRun.Name);
			return false;
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.Steps = MoveTemp(Steps);
		GRun.bRunning = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *Name, GRun.Steps.Num());
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsTextPushCommand(
	TEXT("tls.Text.Push"),
	TEXT("tls.Text.Push <RowName> [battery]: queues a row of DT_AIMessages on the player's text channel, with the home AI's context (the battery overridden when given)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTextChannelComponent* C = TextDebug::ChannelOf(World);
		if (!C || Args.Num() < 1)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.Push: needs a game world with a text channel and a row name."));
			return;
		}
		const FTextChannelContext Context = TextDebug::SampleContext(World, TextDebug::OptionalBattery(Args, 1));
		const bool bOk = C->PushRow(FName(*Args[0]), Context);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Text.Push %s (%s): %s."), *Args[0], *TextDebug::ContextLine(Context), bOk ? TEXT("queued") : TEXT("refused"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTextTriggerCommand(
	TEXT("tls.Text.Trigger"),
	TEXT("tls.Text.Trigger <JumpReturn|AreaEntry|AreaStart|FirstEnergy|Spend> <act> [battery]: PushTrigger on the player's text channel, with the home AI's context (the battery overridden when given)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTextChannelComponent* C = TextDebug::ChannelOf(World);
		ETextTrigger Trigger;
		if (!C || Args.Num() < 2 || !TextDebug::ParseTrigger(Args[0], Trigger))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.Trigger: needs a game world with a text channel, a trigger and an act."));
			return;
		}
		const FTextChannelContext Context = TextDebug::SampleContext(World, TextDebug::OptionalBattery(Args, 2));
		const bool bOk = C->PushTrigger(Trigger, FCString::Atoi(*Args[1]), Context);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Text.Trigger %s %s (%s): %s."), *Args[0], *Args[1], *TextDebug::ContextLine(Context), bOk ? TEXT("queued") : TEXT("nothing"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTextSayCommand(
	TEXT("tls.Text.Say"),
	TEXT("tls.Text.Say <battery> <text...>: queues a text that is no row on the player's text channel, degraded automatically for the battery."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTextChannelComponent* C = TextDebug::ChannelOf(World);
		if (!C || Args.Num() < 2)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.Say: needs a game world with a text channel, a battery and a text."));
			return;
		}
		FString Text;
		for (int32 I = 1; I < Args.Num(); ++I)
		{
			Text += (I > 1 ? TEXT(" ") : TEXT("")) + Args[I];
		}
		const FTextChannelContext Context = TextDebug::SampleContext(World, TOptional<double>(FCString::Atod(*Args[0])));
		const bool bOk = C->PushText(Text, Context);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Text.Say (%s): %s."), *TextDebug::ContextLine(Context), bOk ? TEXT("queued") : TEXT("refused"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTextPrintCommand(
	TEXT("tls.Text.Print"),
	TEXT("tls.Text.Print <RowName> [stillSeconds jumpSeconds homeSeconds]: prints the row at the five levels (sample context 131 s, 600 s, 212 years by default)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.Print: needs a row name."));
			return;
		}
		FTextChannelContext Context;
		Context.StoppedSeconds = Args.IsValidIndex(1) ? FCString::Atod(*Args[1]) : 131.0;
		Context.JumpSeconds = Args.IsValidIndex(2) ? FCString::Atod(*Args[2]) : 600.0;
		Context.HomeSecondsElapsed = Args.IsValidIndex(3) ? FCString::Atod(*Args[3]) : 212.0 * 365.25 * 86400.0;
		Context.BatteryFraction = 0.31;
		TextDebug::PrintRow(FName(*Args[0]), Context);
	}));

static FAutoConsoleCommandWithWorld GTlsTextStateCommand(
	TEXT("tls.Text.State"),
	TEXT("tls.Text.State: the player's text channel: suspended, visible, current row and level, queue, visible text."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
	{
		const UTextChannelComponent* C = TextDebug::ChannelOf(TimeStateDebug::ResolveGameWorld(InWorld));
		if (!C)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Text.State: no text channel."));
			return;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Text.State: table %s, suspended %d, visible %d, current '%s' at %s, %d queued, text \"%s\"."),
			*GetNameSafe(C->GetMessageTable()), C->IsSuspended() ? 1 : 0, C->IsVisible() ? 1 : 0, *C->GetCurrentRowName().ToString(), LexToString(C->GetCurrentLevel()),
			C->GetQueuedCount(), *C->GetVisibleText());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsTextChannelTestCommand(
	TEXT("tls.TextChannelTest"),
	TEXT("tls.TextChannelTest (fresh PIE in MainMenu; starts a new run and clears the save): area 1's AreaStart rows, a Stop during a message, a push while Stopped, the first banked energy once, the layout at a terminal, a payment and a refusal, a rewind and a graze in act 1, the same row at batteries 0.65/0.35/0.05/0.02, travel labels 1->2 (full), 2->3 (0.35) and 3->4 (0.02), a contact in act 2, the loss and the main menu; screenshots in Saved/Screenshots/Brief6."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		TArray<TextDebug::FStep> Steps;
		TextDebug::Build(Steps);
		TextDebug::StartRun(TEXT("TextChannelTest"), MoveTemp(Steps));
	}));

#endif // !UE_BUILD_SHIPPING
