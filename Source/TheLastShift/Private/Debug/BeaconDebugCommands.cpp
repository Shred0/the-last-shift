// SPDX-License-Identifier: MIT

// Brief 10, beacons: tls.BeaconTest (the generated beacons of an area: placement, payment, timer, Stop, time jump,
// refusal, readability in scans, clear view, shots), tls.Beacon.Report, tls.Beacon.Power and tls.Beacon.View (helpers
// for frame timing and screenshots). Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Energy/BeaconBody.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "LidarPointSource.h"
#include "LidarScanComponent.h"
#include "Misc/Paths.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace BeaconDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker ---

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
		bool bShots = true;
		TFunction<void()> Cleanup;
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
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static int32 Banked() { const UEnergySubsystem* E = Energy(); return E ? E->GetBankedEnergy() : -1; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static double RunNow() { const UTimeStateSubsystem* T = TS(); return T ? T->GetRunSeconds() : 0.0; }

	/** The beacons of the world, in route order of their segments. */
	static TArray<ABeaconBody*> Bodies(UWorld* World = nullptr)
	{
		World = World ? World : W();
		TArray<ABeaconBody*> Out;
		for (TActorIterator<ABeaconBody> It(World); World && It; ++It)
		{
			Out.Add(*It);
		}
		Out.Sort([](const ABeaconBody& A, const ABeaconBody& B) { return A.Segment < B.Segment; });
		return Out;
	}

	static ABeaconBody* Body(int32 Index)
	{
		const TArray<ABeaconBody*> All = Bodies();
		return All.IsValidIndex(Index) ? All[Index] : nullptr;
	}

	static APowerTerminal* TerminalOf(const ABeaconBody* B)
	{
		UWorld* World = W();
		for (TActorIterator<APowerTerminal> It(World); World && B && It; ++It)
		{
			for (const APoweredDevice* Target : It->Targets)
			{
				if (Target && Target == B->Device)
				{
					return *It;
				}
			}
		}
		return nullptr;
	}

	static USpotLightComponent* SpotOf(const ABeaconBody* B)
	{
		const ALight* Light = B && B->Device && B->Device->Lights.Num() > 0 ? B->Device->Lights[0].Get() : nullptr;
		return Light ? Cast<USpotLightComponent>(Light->GetLightComponent()) : nullptr;
	}

	static bool IsLit(const ABeaconBody* B)
	{
		const USpotLightComponent* Spot = SpotOf(B);
		return B && B->Device && B->Device->GetLitCount() == 1 && Spot && Spot->IsVisible();
	}

	static bool IsDark(const ABeaconBody* B)
	{
		const USpotLightComponent* Spot = SpotOf(B);
		return B && B->Device && B->Device->GetLitCount() == 0 && Spot && !Spot->IsVisible();
	}

	static UInteractPromptWidget* Prompt()
	{
		UWorld* World = W();
		for (TObjectIterator<UInteractPromptWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool IsAreaReady()
	{
		UWorld* World = W();
		if (!World || !Pawn() || !Director() || !TS())
		{
			return false;
		}
		const UGameInstance* GI = World->GetGameInstance();
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		const AAreaGenerator* Generator = AAreaGenerator::Find(World);
		return (!Flow || !Flow->IsTransitioning()) && Generator && Generator->IsReady();
	}

	// --- Poses ---

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	/** The pawn at a floor point (its current height above the floor kept), looking at LookAt. */
	static void StandAt(const FVector& Floor, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		P->SetActorLocation(FVector(Floor.X, Floor.Y, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		C->SetControlRotation((LookAt - EyeOf(P)).Rotation());
	}

	static float StandDistance()
	{
		const AAreaGenerator* Gen = AAreaGenerator::Find(W());
		return Gen ? FMath::Clamp(Gen->TerminalStandDistance, 50.f, 140.f) : 90.f;
	}

	/** Where a pose stands: "entrance" (the terminal's standing place), "middle" (the route halfway across the space), "under" (the route point about 20 m from the mount). */
	static bool PoseFloor(const ABeaconBody* B, const FString& Where, FVector& OutFloor)
	{
		const AAreaInfo* Info = AAreaInfo::Find(W());
		const APowerTerminal* Terminal = TerminalOf(B);
		if (!B || !Info || !Info->Segments.IsValidIndex(B->Segment))
		{
			return false;
		}
		const FAreaSegmentRecord& Seg = Info->Segments[B->Segment];
		int32 Segment = -1;
		if (Where == TEXT("entrance"))
		{
			if (!Terminal)
			{
				return false;
			}
			OutFloor = Terminal->GetActorLocation() + Terminal->GetActorForwardVector().GetSafeNormal2D() * StandDistance();
			return true;
		}
		if (Where == TEXT("middle"))
		{
			OutFloor = Info->GetRoutePointAtDistance(0.5f * (Seg.RouteStart + Seg.RouteEnd), Segment);
			return true;
		}
		float Best = TNumericLimits<float>::Max();
		for (float S = Seg.RouteStart; S <= Seg.RouteEnd; S += 100.f)
		{
			const FVector P = Info->GetRoutePointAtDistance(S, Segment);
			const float Off = FMath::Abs(static_cast<float>(FVector::Dist2D(P, B->GetActorLocation())) - 2000.f);
			if (Off < Best)
			{
				Best = Off;
				OutFloor = P;
			}
		}
		return Best < TNumericLimits<float>::Max();
	}

	static bool StandPose(const ABeaconBody* B, const FString& Where)
	{
		FVector Floor;
		if (!PoseFloor(B, Where, Floor))
		{
			return false;
		}
		const FVector LookAt = Where == TEXT("under") ? B->GetActorLocation() : B->GetAimPoint() + FVector(0.f, 0.f, 100.f);
		StandAt(Floor, LookAt);
		return true;
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

	static void Shot(const FString& Label)
	{
		if (!GRun.bShots)
		{
			return;
		}
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief10"), Label + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	/** Every point of the segment's floors seen from Eye: true when no line of sight to them crosses the beacon's body or light. */
	static bool IsViewClear(const ABeaconBody* B, const FVector& Eye, int32& OutLines)
	{
		const AAreaInfo* Info = AAreaInfo::Find(W());
		OutLines = 0;
		if (!Info || !B)
		{
			return false;
		}
		FBox Box = B->GetBodyBounds();
		Box += B->GetActorLocation();
		if (const USpotLightComponent* Spot = SpotOf(B))
		{
			Box += Spot->GetComponentLocation();
		}
		Box = Box.ExpandBy(30.f);
		bool bClear = true;
		for (const FAreaFloorRect& Rect : Info->FloorRects)
		{
			if (Rect.Segment != B->Segment)
			{
				continue;
			}
			for (const FVector& Corner : { Rect.Box.GetCenter(), FVector(Rect.Box.Min.X, Rect.Box.Min.Y, Rect.Box.Max.Z), FVector(Rect.Box.Max.X, Rect.Box.Max.Y, Rect.Box.Max.Z) })
			{
				const FVector Target(Corner.X, Corner.Y, Rect.Box.Max.Z + 30.f);
				++OutLines;
				bClear &= !FMath::LineBoxIntersection(Box, Eye, Target, Target - Eye);
			}
		}
		// Nothing of the beacon on a floor: its whole box is high above every floor of the area.
		for (const FAreaFloorRect& Rect : Info->FloorRects)
		{
			bClear &= Box.Min.Z > Rect.Box.Max.Z + 500.f || !Rect.Box.ExpandBy(FVector(0.f, 0.f, 1.f)).IntersectXY(Box);
		}
		return bClear;
	}

	// --- Finish ---

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 10 test"));
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		if (!W())
		{
			Finish(TEXT(", the world went away"));
			return false;
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

	// =====================================================================================================
	// tls.BeaconTest
	// =====================================================================================================

	struct FState
	{
		int32 Act = 0;
		int32 Wanted = 0;
		float PoweredSeconds = 30.f;
		bool bFlashlight = true;
		int32 Sources = 0;
		// Timer measure of beacon 0.
		double PaidAt = 0.0;
		double StopStartedAt = 0.0;
		double StoppedFor = 0.0;
		float FrozenRemaining = 0.f;
		// Scans compared with the beacon dark and powered.
		double DepthMean[2] = { 0.0, 0.0 };
		int32 DepthValid[2] = { 0, 0 };
	};
	static FState G;

	static void KeepEntityDormant()
	{
		if (UTimeStateSubsystem* T = TS())
		{
			T->ResetCumulativeStoppedSeconds();
		}
	}

	static void StopTime()
	{
		KeepEntityDormant();
		if (!IsStopped())
		{
			TS()->Toggle();
		}
	}

	static void ResumeTime()
	{
		if (IsStopped())
		{
			TS()->Toggle();
		}
		KeepEntityDormant();
	}

	static bool FirstScanDone()
	{
		const UScanDirectorComponent* D = Director();
		return IsStopped() && D && D->GetScansThisStop() >= 1;
	}

	static bool ReadDepth(int32 Slot)
	{
		const UScanDirectorComponent* D = Director();
		const ULidarScanComponent* Lidar = D ? D->GetLidarScan() : nullptr;
		TArray<float> Depth;
		int32 Width = 0;
		int32 Height = 0;
		FLidarScanContext Context;
		if (!Lidar || !Lidar->ReadNewestScanDepth(Depth, Width, Height, Context))
		{
			return false;
		}
		double Sum = 0.0;
		int32 Valid = 0;
		for (const float V : Depth)
		{
			if (V > 0.f && V < 1.0e7f)
			{
				Sum += V;
				++Valid;
			}
		}
		G.DepthMean[Slot] = Valid > 0 ? Sum / Valid : 0.0;
		G.DepthValid[Slot] = Valid;
		return true;
	}

	static void AddBeaconPhase(TArray<FStep>& S, int32 Index)
	{
		const FString Name = FString::Printf(TEXT("beacon %d"), Index);
		S.Add(Do(Name + TEXT(": unpaid, at its terminal"), [Index, Name]()
		{
			ABeaconBody* B = Body(Index);
			APowerTerminal* T = TerminalOf(B);
			if (!B || !T)
			{
				Check(false, Name + TEXT(": the beacon and its terminal exist"));
				return -1.f;
			}
			B->Device->PowerOff();
			Energy()->RestoreBankedEnergy(0);
			SetFlashlight(true);
			StandPose(B, TEXT("entrance"));
			Check(T->IsInRange(Pawn()), FString::Printf(TEXT("%s: its terminal's standing place is within reach"), *Name));
			Check(IsDark(B) && !B->Device->IsPowered(), FString::Printf(TEXT("%s: unpaid, it is dark (lit %d)"), *Name, B->Device->GetLitCount()));
			Check(T->OffersPayment() && T->GetCost() == 1, FString::Printf(TEXT("%s: the terminal offers a payment of 1 (offers %d, cost %d)"), *Name, T->OffersPayment() ? 1 : 0, T->GetCost()));
			return 0.4f;
		}));
		S.Add(WaitFor(Name + TEXT(": the Interact prompt shows the power offer with its cost"), []()
		{
			const UInteractPromptWidget* P = Prompt();
			return P && P->GetMode() == EInteractPromptMode::Power && P->GetShownText().Contains(TEXT("1"));
		}, 3.f, [Name]()
		{
			const UInteractPromptWidget* P = Prompt();
			Note(FString::Printf(TEXT("%s: prompt \"%s\""), *Name, P ? *P->GetShownText() : TEXT("-")));
			return 0.f;
		}));
		S.Add(Do(Name + TEXT(": refused with no energy"), [Index, Name]()
		{
			ABeaconBody* B = Body(Index);
			APowerTerminal* T = TerminalOf(B);
			const bool bPaid = T->TryActivate(Pawn());
			Check(!bPaid && IsDark(B) && !B->Device->IsPowered() && Banked() == 0, FString::Printf(TEXT("%s: with no energy the payment is refused and it stays dark"), *Name));
			return 0.3f;
		}));
		if (Index == 0)
		{
			S.Add(Do(Name + TEXT(": shot from the entrance, dark"), [Index]()
			{
				StandPose(Body(Index), TEXT("entrance"));
				Shot(FString::Printf(TEXT("area%d_beacon%d_entrance_off"), G.Act, Index));
				return 0.6f;
			}));
		}
		S.Add(Do(Name + TEXT(": paid"), [Index, Name]()
		{
			ABeaconBody* B = Body(Index);
			APowerTerminal* T = TerminalOf(B);
			Energy()->RestoreBankedEnergy(1);
			StandPose(B, TEXT("entrance"));
			const bool bPaid = T->TryActivate(Pawn());
			G.PaidAt = RunNow();
			G.StoppedFor = 0.0;
			Check(bPaid && Banked() == 0, FString::Printf(TEXT("%s: the payment takes 1 (banked 1 -> %d)"), *Name, Banked()));
			Check(IsLit(B) && B->Device->IsPowered(), FString::Printf(TEXT("%s: paid, it is lit (lit %d)"), *Name, B->Device->GetLitCount()));
			Check(FMath::Abs(B->Device->GetRemainingSeconds() - G.PoweredSeconds) < 0.2f,
				FString::Printf(TEXT("%s: lit for PoweredSeconds (%.2f s of %.2f)"), *Name, B->Device->GetRemainingSeconds(), G.PoweredSeconds));
			Check(T->GetCost() == 1 && !T->OffersPayment(), FString::Printf(TEXT("%s: once paid the terminal offers nothing more"), *Name));
			return 0.6f;
		}));
		S.Add(Do(Name + TEXT(": shots lit, from the entrance and the middle"), [Index, Name]()
		{
			StandPose(Body(Index), TEXT("entrance"));
			Shot(FString::Printf(TEXT("area%d_beacon%d_entrance_on"), G.Act, Index));
			return 0.6f;
		}));
		S.Add(Do(Name + TEXT(": middle"), [Index]()
		{
			StandPose(Body(Index), TEXT("middle"));
			Shot(FString::Printf(TEXT("area%d_beacon%d_middle_on"), G.Act, Index));
			return 0.6f;
		}));
		S.Add(Do(Name + TEXT(": clear view from the entrance and the middle"), [Index, Name]()
		{
			const ABeaconBody* B = Body(Index);
			for (const TCHAR* Where : { TEXT("entrance"), TEXT("middle") })
			{
				StandPose(B, Where);
				int32 Lines = 0;
				const bool bClear = IsViewClear(B, EyeOf(Pawn()), Lines);
				Check(bClear && Lines > 0, FString::Printf(TEXT("%s: from the %s nothing of the beacon stands on a floor or between the eye and the %d floor points of the space"), *Name, Where, Lines));
			}
			return 0.f;
		}));
		S.Add(Do(Name + TEXT(": stop time"), [Index]()
		{
			StandPose(Body(Index), TEXT("under"));
			G.StopStartedAt = RunNow();
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(Name + TEXT(": the first scan of the Stop"), &FirstScanDone, 5.f, [Index, Name]()
		{
			const ABeaconBody* B = Body(Index);
			G.FrozenRemaining = B->Device->GetRemainingSeconds();
			Check(IsDark(B) && B->Device->IsPowered(), FString::Printf(TEXT("%s: while Stopped it is dark and still powered"), *Name));
			return 2.f;
		}));
		S.Add(Do(Name + TEXT(": the timer stood still"), [Index, Name]()
		{
			const ABeaconBody* B = Body(Index);
			const float Now = B->Device->GetRemainingSeconds();
			Check(FMath::Abs(Now - G.FrozenRemaining) < 0.02f && Now > 0.f, FString::Printf(TEXT("%s: 2 s Stopped, the remaining time stands still (%.3f -> %.3f s)"), *Name, G.FrozenRemaining, Now));
			Shot(FString::Printf(TEXT("area%d_beacon%d_scan_powered"), G.Act, Index));
			return 0.5f;
		}));
		S.Add(Do(Name + TEXT(": resume"), []()
		{
			G.StoppedFor += RunNow() - G.StopStartedAt;
			ResumeTime();
			return 0.3f;
		}));
		S.Add(Do(Name + TEXT(": lit again in Normal time"), [Index, Name]()
		{
			const ABeaconBody* B = Body(Index);
			Check(IsLit(B) && B->Device->IsPowered(), FString::Printf(TEXT("%s: after Resume it is lit again"), *Name));
			Check(B->Device->GetRemainingSeconds() <= G.FrozenRemaining + 0.01f && B->Device->GetRemainingSeconds() > G.FrozenRemaining - 1.f,
				FString::Printf(TEXT("%s: the timer resumes from where it stood (%.2f s, stood at %.2f s)"), *Name, B->Device->GetRemainingSeconds(), G.FrozenRemaining));
			return 0.f;
		}));
		if (Index == 0)
		{
			S.Add(WaitFor(Name + TEXT(": its time runs out"), [Index]() { const ABeaconBody* B = Body(Index); return B && !B->Device->IsPowered(); }, G.PoweredSeconds + 5.f, [Index, Name]()
			{
				const ABeaconBody* B = Body(Index);
				const double Normal = RunNow() - G.PaidAt - G.StoppedFor;
				Check(IsDark(B), FString::Printf(TEXT("%s: out of time, it is dark"), *Name));
				Check(FMath::Abs(Normal - G.PoweredSeconds) < 0.3, FString::Printf(TEXT("%s: it stayed lit for %.2f s of Normal time (PoweredSeconds %.2f, %.2f s Stopped not counted)"),
					*Name, Normal, G.PoweredSeconds, G.StoppedFor));
				return 0.f;
			}));
		}
		S.Add(Do(Name + TEXT(": paid again, then a Stop ended by a time jump"), [Index, Name]()
		{
			ABeaconBody* B = Body(Index);
			APowerTerminal* T = TerminalOf(B);
			B->Device->PowerOff();
			Energy()->RestoreBankedEnergy(1);
			StandPose(B, TEXT("entrance"));
			const bool bPaid = T->TryActivate(Pawn());
			Check(bPaid && IsLit(B), FString::Printf(TEXT("%s: paid again, lit"), *Name));
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(Name + TEXT(": stopped for the jump"), &FirstScanDone, 5.f, [Index, Name]()
		{
			Director()->ForceExit(EStopExitReason::Jumped);
			KeepEntityDormant();
			return 0.3f;
		}));
		S.Add(Do(Name + TEXT(": the time jump switched it off"), [Index, Name]()
		{
			const ABeaconBody* B = Body(Index);
			Check(!IsStopped() && !B->Device->IsPowered() && IsDark(B), FString::Printf(TEXT("%s: a time jump switches it off"), *Name));
			return 0.f;
		}));
	}

	/** Readability in scans (beacon 0): the body in the scan's colour, its terminal in the interactables' channel, no injected point, a lit beacon changes no scan. */
	static void AddScanPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("scans: what the beacon is made of"), []()
		{
			const ABeaconBody* B = Body(0);
			const APowerTerminal* T = TerminalOf(B);
			bool bInert = true;
			bool bNoStencil = true;
			TArray<UPrimitiveComponent*> Parts;
			B->GetComponents(Parts);
			for (const UPrimitiveComponent* Part : Parts)
			{
				bInert &= !Part->IsCollisionEnabled() && !Part->CanEverAffectNavigation();
				bNoStencil &= !Part->bRenderCustomDepth && Part->CustomDepthStencilValue == 0;
			}
			Check(bInert, TEXT("scans: the body has no collision and does not affect navigation"));
			Check(bNoStencil, TEXT("scans: the body renders no custom depth (default channel: the scan's own colour)"));
			Check(B->HasMesh(), FString::Printf(TEXT("scans: the body has its meshes (head %s, rod %s)"), *GetNameSafe(B->Head->GetStaticMesh()), *GetNameSafe(B->Rod->GetStaticMesh())));
			bool bTerminalChannel = false;
			TArray<UStaticMeshComponent*> Meshes;
			if (T)
			{
				T->GetComponents(Meshes);
			}
			for (const UStaticMeshComponent* M : Meshes)
			{
				bTerminalChannel |= M->bRenderCustomDepth && M->CustomDepthStencilValue == UTheLastShiftSettings::GetInteractableScanChannel();
			}
			Check(bTerminalChannel, FString::Printf(TEXT("scans: its terminal renders the interactables' channel %d"), UTheLastShiftSettings::GetInteractableScanChannel()));
			const bool bNoSource = !B->GetClass()->ImplementsInterface(ULidarPointSource::StaticClass()) && !B->Device->GetClass()->ImplementsInterface(ULidarPointSource::StaticClass())
				&& (B->Device->Lights.Num() == 0 || !B->Device->Lights[0]->GetClass()->ImplementsInterface(ULidarPointSource::StaticClass()));
			const UScanDirectorComponent* D = Director();
			const ULidarScanComponent* Lidar = D ? D->GetLidarScan() : nullptr;
			Check(bNoSource && Lidar && Lidar->GetPointSourceCount() == G.Sources,
				FString::Printf(TEXT("scans: the beacon is no point source (sources %d, %d at the start)"), Lidar ? Lidar->GetPointSourceCount() : -1, G.Sources));
			return 0.f;
		}));
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FString Label = Slot == 0 ? TEXT("scans: a scan with the beacon dark") : TEXT("scans: the same scan with the beacon powered");
			S.Add(Do(Label, [Slot]()
			{
				ABeaconBody* B = Body(0);
				B->Device->PowerOff();
				if (Slot == 1)
				{
					B->Device->PowerOn();
				}
				StandPose(B, TEXT("under"));
				StopTime();
				return 0.f;
			}));
			S.Add(WaitFor(Label + TEXT(": first scan"), &FirstScanDone, 5.f, []() { return 0.8f; }));
			S.Add(Do(Label + TEXT(": read"), [Slot, Label]()
			{
				Check(ReadDepth(Slot), Label + TEXT(": the scan's depth is read"));
				Check(IsDark(Body(0)), Label + TEXT(": the beacon's light is dark while Stopped"));
				Shot(FString::Printf(TEXT("area%d_beacon0_scan_%s"), G.Act, Slot == 0 ? TEXT("dark") : TEXT("powered_compare")));
				return 0.5f;
			}));
			S.Add(Do(Label + TEXT(": resume"), []() { ResumeTime(); return 0.5f; }));
		}
		S.Add(Do(TEXT("scans: lit and dark scans alike"), []()
		{
			const double Ratio = G.DepthMean[0] > 0.0 ? G.DepthMean[1] / G.DepthMean[0] : 0.0;
			const double Valid = G.DepthValid[0] > 0 ? double(G.DepthValid[1]) / G.DepthValid[0] : 0.0;
			Check(FMath::Abs(Ratio - 1.0) < 0.01 && FMath::Abs(Valid - 1.0) < 0.01,
				FString::Printf(TEXT("scans: a scan with the beacon powered matches one with it dark (mean depth %.1f vs %.1f cm, samples %d vs %d)"), G.DepthMean[1], G.DepthMean[0], G.DepthValid[1], G.DepthValid[0]));
			Body(0)->Device->PowerOff();
			return 0.f;
		}));
	}

	static void LogBeacons(const TCHAR* Prefix)
	{
		UWorld* World = W();
		const AAreaInfo* Info = AAreaInfo::Find(World);
		const TArray<ABeaconBody*> All = Bodies(World);
		UE_LOG(LogTheLastShift, Log, TEXT("%s %d beacons."), Prefix, All.Num());
		for (int32 I = 0; I < All.Num(); ++I)
		{
			const ABeaconBody* B = All[I];
			const APowerTerminal* T = TerminalOf(B);
			const USpotLightComponent* Spot = SpotOf(B);
			const FAreaSegmentRecord* Seg = Info && Info->Segments.IsValidIndex(B->Segment) ? &Info->Segments[B->Segment] : nullptr;
			int32 TS = -1;
			float Offset = 0.f;
			const float TerminalS = Info && T ? Info->ProjectToRoute(T->GetActorLocation(), TS, Offset) : 0.f;
			const FBox Box = B->GetBodyBounds();
			UE_LOG(LogTheLastShift, Log, TEXT("%s beacon %d: segment %d (%s, %.0f x %.0f m, route %.1f-%.1f m), mount %s, %.0f cm above its highest floor, body lowest point %.0f cm, rod %.0f cm (%s, upper end at %.0f cm), aimed at %s (%.1f m from the light), light %s cone %.1f deg intensity %.0f cd range %.0f m, terminal %s yaw %.0f at route %.1f m."),
				Prefix, I, B->Segment, Seg ? LexToString(Seg->Type) : TEXT("-"), Seg ? Seg->Bounds.GetSize().X / 100.f : 0.f, Seg ? Seg->Bounds.GetSize().Y / 100.f : 0.f,
				Seg ? Seg->RouteStart / 100.f : 0.f, Seg ? Seg->RouteEnd / 100.f : 0.f, *B->GetActorLocation().ToCompactString(), B->HeightAboveFloor,
				Box.IsValid ? Box.Min.Z : B->GetActorLocation().Z, B->GetRodLength(), B->DoesRodReachGeometry() ? TEXT("reaches the geometry above") : TEXT("maximum length, nothing above"),
				B->GetActorLocation().Z + B->GetRodLength(), *B->GetAimPoint().ToCompactString(), Spot ? FVector::Dist(Spot->GetComponentLocation(), B->GetAimPoint()) / 100.f : 0.f,
				Spot ? *Spot->GetComponentLocation().ToCompactString() : TEXT("-"), Spot ? Spot->OuterConeAngle : 0.f, Spot ? Spot->Intensity : 0.f, Spot ? Spot->AttenuationRadius / 100.f : 0.f,
				T ? *T->GetActorLocation().ToCompactString() : TEXT("-"), T ? T->GetActorRotation().Yaw : 0.f, TerminalS / 100.f);
		}
	}

	static void StartBeaconTest(bool bQuit, bool bShots)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.BeaconTest: a run is already going."));
			return;
		}
		GRun = FRun();
		GRun.Name = TEXT("BeaconTest");
		GRun.bQuit = bQuit;
		GRun.bShots = bShots;
		G = FState();
		GRun.Cleanup = []()
		{
			if (IsStopped())
			{
				ResumeTime();
			}
			for (ABeaconBody* B : Bodies())
			{
				if (B->Device)
				{
					B->Device->PowerOff();
				}
			}
			SetFlashlight(G.bFlashlight);
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the area is generated and playable"), &IsAreaReady, 90.f, []()
		{
			const AAreaGenerator* Gen = AAreaGenerator::Find(W());
			const FAreaRecipe* Recipe = Gen ? Gen->GetRecipe() : nullptr;
			if (!Recipe || IsStopped())
			{
				Check(false, TEXT("the map has a generated area with a recipe, in Normal time"));
				return -1.f;
			}
			G.Act = Gen->GetAct();
			G.Wanted = Recipe->BeaconCount;
			G.PoweredSeconds = FMath::Clamp(Gen->LightGroupPoweredSeconds, 0.f, 600.f);
			if (const APawn* P = Pawn())
			{
				TArray<USpotLightComponent*> Lights;
				P->GetComponents(Lights);
				G.bFlashlight = Lights.Num() > 0 && Lights[0]->IsVisible();
			}
			const UScanDirectorComponent* D = Director();
			G.Sources = D && D->GetLidarScan() ? D->GetLidarScan()->GetPointSourceCount() : -1;
			Check(Gen->bLastValidationPassed, FString::Printf(TEXT("act %d seed %d: the area passed every validator rule"), G.Act, Gen->GetSeed()));
			bool bRule8 = false;
			for (const FAreaRuleResult& Rule : Gen->GetLastValidation().Rules)
			{
				if (Rule.Rule == 8)
				{
					bRule8 = Rule.bPass;
					Note(FString::Printf(TEXT("rule 8: %s"), *Rule.Summary));
				}
			}
			Check(bRule8, TEXT("rule 8 (beacons) passed"));
			const TArray<ABeaconBody*> All = Bodies();
			Check(All.Num() == G.Wanted && G.Wanted > 0, FString::Printf(TEXT("act %d: %d beacons (recipe %d)"), G.Act, All.Num(), G.Wanted));
			LogBeacons(TEXT("BeaconTest   .."));
			if (All.Num() == 0 || All.Num() != G.Wanted)
			{
				Note(TEXT("this test runs in an area whose recipe asks for beacons (area 2)"));
				return -1.f;
			}
			bool bAllDark = true;
			for (const ABeaconBody* B : All)
			{
				bAllDark &= IsDark(B) && !B->Device->IsPowered();
			}
			Check(bAllDark, TEXT("every beacon is dark at the start of the area"));
			Check(G.PoweredSeconds > 0.f, FString::Printf(TEXT("beacons are timed: PoweredSeconds %.1f s (the light groups' own)"), G.PoweredSeconds));
			return 0.3f;
		}));
		S.Add(Do(TEXT("phases"), []()
		{
			TArray<FStep> More;
			for (int32 Index = 0; Index < G.Wanted; ++Index)
			{
				AddBeaconPhase(More, Index);
			}
			AddScanPhase(More);
			GRun.Steps.Insert(More, GRun.Next);
			return 0.f;
		}));
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started."), *GRun.Name);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsBeaconTestCommand(
	TEXT("tls.BeaconTest"),
	TEXT("tls.BeaconTest [Quit=0] [Shots=1]: (fresh PIE or a package, in Area2_ServerHalls) the generated beacons: counts and rule 8, dark until paid, the prompt, "
		"refusal with no energy, payment, PoweredSeconds of Normal time with the timer still while Stopped, a time jump switching it off, clear view from the "
		"entrance and the middle, the body in the scan's colour, its terminal in the interactables' channel, no point source, lit and dark scans alike. "
		"Shots to Saved/Screenshots/Brief10."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		BeaconDebug::StartBeaconTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsBeaconReportCommand(
	TEXT("tls.Beacon.Report"),
	TEXT("tls.Beacon.Report: (PIE) logs every beacon of the area: segment, mount, height, rod, aim, light and terminal."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		BeaconDebug::LogBeacons(TEXT("tls.Beacon.Report:"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsBeaconPowerCommand(
	TEXT("tls.Beacon.Power"),
	TEXT("tls.Beacon.Power <Index> on|off: (PIE) powers a beacon on or off without its terminal (frame timing)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		ABeaconBody* B = BeaconDebug::Body(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		if (!B || !B->Device)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Beacon.Power: no such beacon."));
			return;
		}
		const bool bOn = Args.Num() < 2 || Args[1] != TEXT("off");
		bOn ? B->Device->PowerOn() : B->Device->PowerOff();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Beacon.Power: %s %s (lit %d)."), *B->GetName(), bOn ? TEXT("on") : TEXT("off"), B->Device->GetLitCount());
	}));

// Brief 19: the beacons' cone and intensity for the session, to compare values before choosing them. Their one owner is
// the area generator (BeaconConeAngle, BeaconIntensity); the command sets them there and pushes them to the lights it spawned.
static FAutoConsoleCommandWithWorldAndArgs GTlsBeaconLightCommand(
	TEXT("tls.Beacon.Light"),
	TEXT("tls.Beacon.Light [ConeDeg] [Candelas]: (PIE or a package) sets the beacons' cone (half-angle, 1-45 deg) and intensity (candelas) on the area generator for the session "
		"and applies them to every beacon's light; with no value, logs them. Not saved: the next map load takes the generator's own values again."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		UWorld* World = BeaconDebug::W();
		AAreaGenerator* Gen = World ? AAreaGenerator::Find(World) : nullptr;
		if (!Gen)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Beacon.Light: no area generator in the game world."));
			return;
		}
		if (Args.Num() > 0)
		{
			Gen->BeaconConeAngle = FMath::Clamp(FCString::Atof(*Args[0]), 1.f, 45.f);
		}
		if (Args.Num() > 1)
		{
			Gen->BeaconIntensity = FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 100000000.f);
		}
		int32 Lights = 0;
		for (const ABeaconBody* B : BeaconDebug::Bodies(World))
		{
			if (USpotLightComponent* Spot = BeaconDebug::SpotOf(B))
			{
				Spot->SetOuterConeAngle(Gen->BeaconConeAngle);
				Spot->SetIntensity(Gen->BeaconIntensity);
				++Lights;
			}
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Beacon.Light: cone %.1f deg, intensity %.0f cd, range %.0f m, on %d beacon lights."),
			Gen->BeaconConeAngle, Gen->BeaconIntensity, Gen->BeaconRange / 100.f, Lights);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsBeaconViewCommand(
	TEXT("tls.Beacon.View"),
	TEXT("tls.Beacon.View <Index> entrance|middle|under: (PIE) puts the pawn at the beacon's entrance (its terminal's standing place) or halfway across its space, looking at "
		"where it points, or about 20 m from its mount, looking at it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const ABeaconBody* B = BeaconDebug::Body(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		const FString Where = Args.Num() > 1 ? Args[1] : FString(TEXT("entrance"));
		const bool bOk = BeaconDebug::StandPose(B, Where);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Beacon.View: %s %s: %s."), B ? *B->GetName() : TEXT("-"), *Where, bOk ? TEXT("done") : TEXT("no such beacon or pose"));
	}));

#endif
