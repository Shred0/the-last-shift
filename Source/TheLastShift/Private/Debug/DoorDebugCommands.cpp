// SPDX-License-Identifier: MIT

// Brief 46, every door moves. tls.DoorCheck (an area map or a hand-built map; PIE; not with its Quit argument) checks, for
// every ADoorDevice of the map (each one opened at once before) and for one spawned ADoorDevice, what the player sees of
// the panel, frame by frame:
//
// - It travels: opening and closing each take DoorMoveSeconds of the run's clock, within two of the run's longest frames,
//   at the settings' value and at 2.0 s set for the session; the panel starts at its shut place (within 1 mm of the place
//   it had before the brief: the Blocker's centre for the cube, its bottom centre for a modelled panel) and ends with
//   nothing of it in the opening.
// - It does not jump: sampled every frame, the panel never steps back, and no step is larger than three times the
//   travel's share of that frame's run time.
// - It is seen in between: from 4 m in front (a depth capture with the panel and one without it, the pawn left out), the
//   panel's visible pixels at half the motion are between a third and two thirds of those shut.
// - It is not seen where it should not be: open, from 4 m on each side, no pixel of the panel; the pixels a risen panel
//   would show there if it were not hidden are logged (the doors hidden once risen).
// - The sound (criterion 5): the Motion slot asked for once at each motion's start at pitch 1, one looping sound, still
//   playing at half the motion, stopped within MotionFadeOutSeconds after its end; no Door slot.
// - The clock (criterion 2), on the first door: a Stop at half a motion holds the panel for 2 s (within 1 mm), a scan taken
//   then sees it there (the scan's own origin's depth capture, with and without the panel), a sweep of the pawn's capsule
//   meets it there; after the Resume the motion ends in the share of time that was left. The same behind the pause menu.
// - In an area (criteria 3, 4 and 6), on the area's first gate: the powered time (3 s for the session) counted from the
//   start of the opening, held while Stopped, then the closing; a time jump while open (the door closes on the return to
//   Normal time); a payment while closing (opens again from where it stands, the sound reversing); the doorway (the pawn
//   in it holds the door open 3 s past its time, the door closes once it has left; a pawn that enters while it closes
//   opens it again; the pawn never moves by more than 1 cm with no input and is never inside the panel's collision); the
//   layout hash against the reference and the validation at the first seed.
// - Shots to Saved/Screenshots/Brief46: in an area, the first gate from 4 m with the flashlight at a quarter, a half and
//   three quarters of its opening and open, and in a scan Stopped at half its motion; at home, the print room's door at
//   half its motion and open.
//
// It puts back the settings, the powered times, the run's home state, the pawn and every door (shut), and destroys the
// door it spawned. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Audio/GameAudioSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Energy/DoorDevice.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Prologue/PrologueDeparture.h"
#include "Prologue/PrologueEntryDoor.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Sound/SoundWave.h"
#include "TextureResource.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace DoorCheck
{
	using TimeStateDebug::ResolveGameWorld;

	static const TCHAR* Name() { return TEXT("tls.DoorCheck"); }

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	/** One motion under measure, sampled every frame by the ticker. */
	struct FMotion
	{
		bool bActive = false;
		TWeakObjectPtr<ADoorDevice> Door;
		bool bOpening = true;
		FString Label;
		float Seconds = 0.8f;
		double StartRun = -1.0;
		double EndRun = -1.0;
		double PrevRun = -1.0;
		float PrevRise = 0.f;
		float TotalRise = 0.f;
		double LongestFrame = 0.0;
		double StoppedRun = 0.0;
		float WorstStepRatio = 0.f;
		int32 BackSteps = 0;
		int32 Frames = 0;
		bool bHalfDone = false;
		bool bFirstSoundChecked = false;
		bool bHalfSoundOk = false;
		bool bStartSoundOk = false;
		FString StartSound;
		int32 Mark = 0;
		TFunction<void()> AtHalf;
	};

	struct FDoorResult
	{
		FString Name;
		float Open = -1.f, Close = -1.f, Open2 = -1.f, Close2 = -1.f, StepRatio = 0.f, Cover = -1.f;
		int32 WouldShowFront = -1, WouldShowBack = -1;
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
		FTSTicker::FDelegateHandle Ticker;
		FString Map;
		bool bArea = false;

		TArray<TWeakObjectPtr<ADoorDevice>> Doors;
		TWeakObjectPtr<ADoorDevice> Spawned;
		TMap<TWeakObjectPtr<ADoorDevice>, float> SavedPowered;
		TMap<FString, FDoorResult> Results;
		float SavedMoveSeconds = 0.8f;
		FVector StartLocation = FVector::ZeroVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		TWeakObjectPtr<AActor> Departure;

		FMotion Motion;
		int32 ShutPixels = 0;
		FVector HeldPanel = FVector::ZeroVector;
		float HeldFraction = 0.f;
		double MarkRun = 0.0;
		double MarkRun2 = 0.0;
		double StopAtRun = 0.0;
		float PrevFraction = 0.f;
		FVector PawnHeld = FVector::ZeroVector;
		bool bWatchPawn = false;
		float PawnDrift = 0.f;
		int32 PawnInPanel = 0;
		bool bShotReady = true;
	};

	static FRun GRun;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name(), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name(), *What);
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

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static ACharacter* Pawn() { const APlayerController* C = PC(); return C ? Cast<ACharacter>(C->GetPawn()) : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { return Pawn() ? Pawn()->FindComponentByClass<UScanDirectorComponent>() : nullptr; }
	static UGameAudioSubsystem* Audio() { return UGameAudioSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static double RunNow() { const UTimeStateSubsystem* T = TS(); return T ? T->GetRunSeconds() : 0.0; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static void StopTime() { if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped()) { T->Stop(); } }
	static void ResumeTime() { if (UTimeStateSubsystem* T = TS(); T && T->IsStopped()) { T->Resume(); } }
	static float MoveSeconds() { return ADoorDevice::GetMoveSeconds(); }
	static ADoorDevice* DoorAt(int32 I) { return GRun.Doors.IsValidIndex(I) ? GRun.Doors[I].Get() : nullptr; }
	static ADoorDevice* FirstDoor() { return DoorAt(0); }

	static AAreaGenerator* Generator()
	{
		UWorld* World = W();
		for (TActorIterator<AAreaGenerator> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static int64 ReferenceHash(int32 Act)
	{
		// The layout hashes of the four areas at their first seeds (tls.WallCheck's and tls.BackdropCheck's reference).
		switch (Act)
		{
		case 1: return static_cast<int64>(0x7f1032152054f0a3ull);
		case 2: return static_cast<int64>(0xf2a87c443cf7c500ull);
		case 3: return static_cast<int64>(0xf7db963f34395881ull);
		case 4: return static_cast<int64>(0x6cf097ae1c4679cfull);
		default: return 0;
		}
	}

	// --- A door's frame of reference ---

	/** The opening's centre, its forward (through the door), right (across), up, and half extent (unscaled box, world scale 1). */
	static FTransform OpeningFrame(const ADoorDevice* D)
	{
		const FBox Box = D ? D->GetOpeningBox() : FBox(ForceInit);
		const FQuat Q = D ? D->GetActorQuat() : FQuat::Identity;
		return FTransform(Q, Box.IsValid ? Box.GetCenter() : (D ? D->GetActorLocation() : FVector::ZeroVector));
	}

	static FVector HalfSize(const ADoorDevice* D) { return D ? D->GetDoorSize() * 0.5f : FVector::ZeroVector; }

	static float FloorZ(const ADoorDevice* D) { return OpeningFrame(D).GetLocation().Z - HalfSize(D).Z; }

	/** The panel's place in the door's own frame: its rise above its shut place, read from the rendered panel. */
	static float PanelRiseSeen(const ADoorDevice* D)
	{
		const UStaticMeshComponent* P = D ? D->GetPanel() : nullptr;
		if (!P)
		{
			return 0.f;
		}
		const FVector Local = OpeningFrame(D).InverseTransformPosition(P->GetComponentLocation());
		// Shut: the cube's centre at the opening's centre (0), a modelled panel's pivot at its bottom (-half height).
		const bool bModelled = D->PanelMesh != nullptr;
		return static_cast<float>(Local.Z + (bModelled ? HalfSize(D).Z : 0.0));
	}

	/** Where the panel stood shut before Brief 46, in world space: the Blocker's centre (cube) or its bottom centre (modelled). */
	static FVector OldShutPlace(const ADoorDevice* D)
	{
		const FTransform F = OpeningFrame(D);
		return D && D->PanelMesh ? F.TransformPosition(FVector(0.f, 0.f, -HalfSize(D).Z)) : F.GetLocation();
	}

	static void StandAt(const FVector& At, const FVector& Look, float Floor)
	{
		ACharacter* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		P->SetActorLocation(FVector(At.X, At.Y, Floor + Half + 1.f), false, nullptr, ETeleportType::TeleportPhysics);
		P->GetCharacterMovement()->StopMovementImmediately();
		const UCameraComponent* Camera = P->FindComponentByClass<UCameraComponent>();
		const FVector Eye = Camera ? Camera->GetComponentLocation() : P->GetActorLocation() + FVector(0.f, 0.f, 64.f);
		C->SetControlRotation((Look - Eye).Rotation());
	}

	/** The pawn 4 m (Distance) in front of the door on Side (+1 its forward, -1 behind), looking at the opening's centre. */
	static void StandBefore(const ADoorDevice* D, float Side, float Distance = 400.f)
	{
		const FTransform F = OpeningFrame(D);
		StandAt(F.TransformPosition(FVector(Side * Distance, 0.f, 0.f)), F.GetLocation(), FloorZ(D));
	}

	static void SetFlashlight(bool bOn)
	{
		if (const APawn* P = Pawn())
		{
			TArray<USpotLightComponent*> Lights;
			P->GetComponents(Lights);
			for (USpotLightComponent* Light : Lights) { Light->SetVisibility(bOn); }
		}
	}

	// --- Captures: what the player sees of the panel ---

	static bool CaptureDepth(const FVector& Eye, const FRotator& Rotation, int32 Width, int32 Height, const UPrimitiveComponent* Hidden, TArray<float>& Out)
	{
		Out.Reset();
		UWorld* World = W();
		ACharacter* P = Pawn();
		if (!World || !P)
		{
			return false;
		}
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->RenderTargetFormat = RTF_RGBA32f;
		Target->ClearColor = FLinearColor(1.0e9f, 0.f, 0.f, 0.f);
		Target->InitAutoFormat(Width, Height);
		Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(P);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = SCS_SceneDepth;
		Capture->FOVAngle = 90.f;
		Capture->TextureTarget = Target;
		Capture->HiddenActors.Add(P);
		if (Hidden)
		{
			Capture->HiddenComponents.Add(const_cast<UPrimitiveComponent*>(Hidden));
		}
		Capture->RegisterComponentWithWorld(World);
		Capture->SetWorldLocationAndRotation(Eye, Rotation);
		Capture->CaptureScene();
		FlushRenderingCommands();
		TArray<FLinearColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bOk = Resource && Resource->ReadLinearColorPixels(Pixels) && Pixels.Num() == Width * Height;
		Capture->DestroyComponent();
		if (!bOk)
		{
			return false;
		}
		Out.SetNumUninitialized(Pixels.Num());
		for (int32 I = 0; I < Pixels.Num(); ++I)
		{
			Out[I] = Pixels[I].R;
		}
		return true;
	}

	/**
	 * Pixels of the door's panel seen from Eye looking at the opening's centre: nearer with the panel than without it.
	 * bInOpening: only those whose ray crosses the door's plane inside the opening's rectangle (what the panel covers of
	 * the opening, whether or not a wall above hides the rest of it).
	 */
	static int32 PanelPixelsFrom(const ADoorDevice* D, const FVector& Eye, bool bInOpening = false)
	{
		const UStaticMeshComponent* Panel = D ? D->GetPanel() : nullptr;
		if (!Panel)
		{
			return -1;
		}
		constexpr int32 Width = 320;
		constexpr int32 Height = 240;
		const FRotator Rotation = (OpeningFrame(D).GetLocation() - Eye).Rotation();
		TArray<float> With, Without;
		if (!CaptureDepth(Eye, Rotation, Width, Height, nullptr, With) || !CaptureDepth(Eye, Rotation, Width, Height, Panel, Without))
		{
			return -1;
		}
		const FTransform F = OpeningFrame(D);
		const FVector Half = HalfSize(D);
		const FVector LocalEye = F.InverseTransformPosition(Eye);
		const FRotationMatrix M(Rotation);
		const double Tan = FMath::Tan(FMath::DegreesToRadians(45.0));
		int32 N = 0;
		for (int32 I = 0; I < With.Num(); ++I)
		{
			if (!(With[I] < Without[I] - 1.f))
			{
				continue;
			}
			if (bInOpening)
			{
				const int32 X = I % Width;
				const int32 Y = I / Width;
				const double U = (2.0 * (X + 0.5) / Width - 1.0) * Tan;
				const double V = (1.0 - 2.0 * (Y + 0.5) / Height) * Tan * Height / Width;
				const FVector Ray = F.InverseTransformVectorNoScale((M.GetUnitAxis(EAxis::X) + M.GetUnitAxis(EAxis::Y) * U + M.GetUnitAxis(EAxis::Z) * V).GetSafeNormal());
				if (FMath::Abs(Ray.X) < 1.e-6)
				{
					continue;
				}
				const double T = -LocalEye.X / Ray.X;
				const FVector At = LocalEye + Ray * T;
				if (T <= 0.0 || FMath::Abs(At.Y) > Half.Y || FMath::Abs(At.Z) > Half.Z)
				{
					continue;
				}
			}
			++N;
		}
		return N;
	}

	static FVector EyeBefore(const ADoorDevice* D, float Side)
	{
		const FTransform F = OpeningFrame(D);
		const FVector Foot = F.TransformPosition(FVector(Side * 400.f, 0.f, 0.f));
		return FVector(Foot.X, Foot.Y, FloorZ(D) + 165.f);
	}

	// --- Shots ---

	static FString ShotFolder() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief46"))); }

	static void Shot(const FString& ShotName)
	{
		const FString Path = FPaths::Combine(ShotFolder(), GRun.Map + TEXT("_") + ShotName + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	// --- The sound record ---

	static TArray<FGameSoundRequest> MotionSince(const AActor* Mover, int32 From, bool bStops)
	{
		TArray<FGameSoundRequest> Out;
		const UGameAudioSubsystem* A = Audio();
		if (!A || !Mover)
		{
			return Out;
		}
		for (int32 I = From; I < A->GetRecordEnd(); ++I)
		{
			const FGameSoundRequest* R = A->GetRequest(I);
			if (R && R->Sound == EGameSound::Motion && R->bStop == bStops && R->Source.StartsWith(Mover->GetName() + TEXT(" ")))
			{
				Out.Add(*R);
			}
		}
		return Out;
	}

	static bool IsLoopingSound(const UAudioComponent* C)
	{
		const USoundWave* Wave = C ? Cast<USoundWave>(C->Sound) : nullptr;
		return Wave && Wave->bLooping;
	}

	// --- The motion sampler ---

	static void SampleMotion()
	{
		FMotion& M = GRun.Motion;
		ADoorDevice* D = M.Door.Get();
		if (!M.bActive || !D)
		{
			return;
		}
		const double Now = RunNow();
		const float Rise = PanelRiseSeen(D);
		const bool bHeld = IsStopped() || (StdPC() && StdPC()->IsPausedByMenu());
		if (M.PrevRun >= 0.0 && !bHeld)
		{
			const double Dt = Now - M.PrevRun;
			M.LongestFrame = FMath::Max(M.LongestFrame, Dt);
			const float Step = (Rise - M.PrevRise) * (M.bOpening ? 1.f : -1.f);
			if (Step < -0.01f)
			{
				++M.BackSteps;
			}
			const double Share = Dt / M.Seconds * M.TotalRise;
			if (Share > 1.e-3 && D->IsMoving())
			{
				M.WorstStepRatio = FMath::Max(M.WorstStepRatio, static_cast<float>(FMath::Abs(Step) / Share));
			}
		}
		else if (M.PrevRun >= 0.0 && bHeld)
		{
			M.StoppedRun += Now - M.PrevRun;
		}
		M.PrevRun = Now;
		M.PrevRise = Rise;
		++M.Frames;
		if (!M.bFirstSoundChecked && Audio())
		{
			M.bFirstSoundChecked = true;
			const TArray<FGameSoundRequest> Starts = MotionSince(D, M.Mark, false);
			const UAudioComponent* C = Audio()->GetMotionSound(D);
			M.bStartSoundOk = Starts.Num() == 1 && FMath::IsNearlyEqual(Starts[0].Pitch, 1.f) && Starts[0].bHadSound && Starts[0].bInWorld
				&& Starts[0].Source.EndsWith(M.bOpening ? TEXT("opens") : TEXT("closes")) && Audio()->IsMotionSoundPlaying(D) && IsLoopingSound(C);
			M.StartSound = FString::Printf(TEXT("%d request(s), pitch %.2f, playing %d, looping %d"), Starts.Num(), Starts.Num() > 0 ? Starts[0].Pitch : 0.f,
				Audio()->IsMotionSoundPlaying(D) ? 1 : 0, IsLoopingSound(C) ? 1 : 0);
		}
		const float Share = M.bOpening ? D->GetOpenFraction() : 1.f - D->GetOpenFraction();
		if (!M.bHalfDone && Share >= 0.5f && D->IsMoving())
		{
			M.bHalfDone = true;
			M.bHalfSoundOk = Audio() && Audio()->IsMotionSoundPlaying(D) && MotionSince(D, M.Mark, false).Num() == 1 && MotionSince(D, M.Mark, true).Num() == 0;
			if (M.AtHalf)
			{
				M.AtHalf();
			}
		}
		if (M.EndRun < 0.0 && !D->IsMoving())
		{
			M.EndRun = Now;
		}
	}

	static void SampleWatch()
	{
		if (!GRun.bWatchPawn || !Pawn())
		{
			return;
		}
		const ACharacter* P = Pawn();
		GRun.PawnDrift = FMath::Max(GRun.PawnDrift, static_cast<float>(FVector::Dist(P->GetActorLocation(), GRun.PawnHeld)));
		const ADoorDevice* D = FirstDoor();
		const UBoxComponent* Box = D ? D->GetPanelCollision() : nullptr;
		if (Box && Box->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			const UCapsuleComponent* Cap = P->GetCapsuleComponent();
			const FCollisionShape Shape = FCollisionShape::MakeCapsule(Cap->GetScaledCapsuleRadius() - 1.f, Cap->GetScaledCapsuleHalfHeight() - 1.f);
			GRun.PawnInPanel += const_cast<UBoxComponent*>(Box)->OverlapComponent(P->GetActorLocation(), P->GetActorQuat(), Shape) ? 1 : 0;
		}
	}

	/**
	 * One motion of Door (Opening, or closing), started by Start, measured to its end: its time against Seconds (the
	 * settings' DoorMoveSeconds when 0), its steps, its end place, its sound. AtHalf runs at half the motion (captures).
	 */
	static void AddMotion(TArray<FStep>& S, const FString& Label, TFunction<ADoorDevice*()> Door, bool bOpening, TFunction<void(ADoorDevice*)> Start,
		TFunction<void()> AtHalf = nullptr, bool bSound = true, TFunction<void(float)> Took = nullptr)
	{
		S.Add(Do(Label + TEXT(": start"), [Label, Door, bOpening, Start, AtHalf]()
		{
			ADoorDevice* D = Door();
			if (!D)
			{
				Check(false, Label + TEXT(": the door"));
				return -1.f;
			}
			FMotion& M = GRun.Motion;
			M = FMotion();
			M.Door = D;
			M.bOpening = bOpening;
			M.Label = Label;
			M.Seconds = MoveSeconds();
			M.TotalRise = D->GetDoorSize().Z / FMath::Max(D->GetActorScale3D().Z, 0.01) + 1.f;
			M.Mark = Audio() ? Audio()->GetRecordEnd() : 0;
			M.AtHalf = AtHalf;
			M.StartRun = RunNow();
			M.PrevRun = M.StartRun;
			M.PrevRise = PanelRiseSeen(D);
			M.bActive = true;
			Start(D);
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the motion ends"), [Door]() { const ADoorDevice* D = Door(); return GRun.Motion.EndRun >= 0.0 || (D && !D->IsMoving() && GRun.Motion.Frames > 1); },
			MoveSeconds() * 3.f + 10.f, [Label, Door, bOpening, bSound, Took]()
		{
			FMotion& M = GRun.Motion;
			M.bActive = false;
			ADoorDevice* D = Door();
			const double TookRun = (M.EndRun >= 0.0 ? M.EndRun : RunNow()) - M.StartRun - M.StoppedRun;
			const double Margin = 2.0 * FMath::Max(M.LongestFrame, 1.0 / 240.0);
			Check(D && FMath::Abs(TookRun - M.Seconds) <= Margin,
				FString::Printf(TEXT("%s: the travel took %.3f s of the run's clock (DoorMoveSeconds %.2f, within two of its longest frames %.3f s)"), *Label, TookRun, M.Seconds, Margin));
			Check(M.BackSteps == 0 && M.WorstStepRatio <= 3.f,
				FString::Printf(TEXT("%s: %d frames, never a step back (%d), the largest step %.2f times the travel's share of its frame (at most 3)"), *Label, M.Frames, M.BackSteps,
					M.WorstStepRatio));
			if (D)
			{
				const FBox Opening = D->GetOpeningBox();
				const UStaticMeshComponent* P = D->GetPanel();
				if (bOpening)
				{
					const FBox PanelBox = P ? P->Bounds.GetBox() : FBox(ForceInit);
					// The panel risen along the door's up: its lowest point at or above the opening's top, or hidden.
					const float Bottom = PanelRiseSeen(D) - HalfSize(D).Z * 2.f / FMath::Max(D->GetActorScale3D().Z, 0.01);
					Check(D->IsFullyOpen() && (!P->IsVisible() || Bottom >= -0.1f),
						FString::Printf(TEXT("%s: open, nothing of the panel in the opening (its bottom %.1f cm above the opening's top%s; panel box %s, opening %s)"), *Label, Bottom,
							P->IsVisible() ? TEXT("") : TEXT(", hidden"), *PanelBox.ToString(), *Opening.ToString()));
				}
				else
				{
					const float Off = static_cast<float>(FVector::Dist(P->GetComponentLocation(), OldShutPlace(D)));
					Check(!D->IsOpen() && P->IsVisible() && Off <= 0.1f, FString::Printf(TEXT("%s: shut, the panel at its place before Brief 46 (%.3f mm off)"), *Label, Off * 10.f));
				}
			}
			if (bSound)
			{
				Check(M.bStartSoundOk, FString::Printf(TEXT("%s: the Motion slot asked for once at the start, pitch 1, one looping sound playing (%s)"), *Label, *M.StartSound));
				Check(M.bHalfSoundOk, FString::Printf(TEXT("%s: at half the motion the same sound still plays, no second request, not stopped"), *Label));
			}
			if (Took)
			{
				Took(static_cast<float>(TookRun));
			}
			FDoorResult& R = GRun.Results.FindOrAdd(GetNameSafe(D));
			R.Name = GetNameSafe(D);
			R.StepRatio = FMath::Max(R.StepRatio, M.WorstStepRatio);
			return bSound ? UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds() + 0.1f : 0.f;
		}));
		if (bSound)
		{
			S.Add(Do(Label + TEXT(": the sound after the end"), [Label, Door]()
			{
				const ADoorDevice* D = Door();
				const TArray<FGameSoundRequest> Stops = MotionSince(D, GRun.Motion.Mark, true);
				Check(Audio() && !Audio()->IsMotionSoundPlaying(D) && Stops.Num() == 1 && Stops[0].Source.Contains(TEXT("motion ends")) && Audio()->GetMotionPlays(D) == 1,
					FString::Printf(TEXT("%s: %.2f s after the end the sound has stopped (fade %.2f s), played once (%d stop(s))"), *Label,
						UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds() + 0.1f, UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds(), Stops.Num()));
				return 0.f;
			}));
		}
	}

	static void Open(ADoorDevice* D) { D->PowerOn(); }
	static void Close(ADoorDevice* D) { D->PowerOff(); }

	// --- Criterion 1 and 5: each door ---

	static void AddDoor(TArray<FStep>& S, TFunction<ADoorDevice*()> Door, const FString& Label, bool bShotsAtHome)
	{
		S.Add(Do(Label + TEXT(": shut, the pawn 4 m in front"), [Door, Label]()
		{
			ADoorDevice* D = Door();
			if (!D)
			{
				Check(false, Label + TEXT(": the door"));
				return -1.f;
			}
			ResumeTime();
			StandBefore(D, 1.f);
			const float Off = static_cast<float>(FVector::Dist(D->GetPanel()->GetComponentLocation(), OldShutPlace(D)));
			Check(!D->IsOpen() && !D->IsMoving() && D->GetPanel()->IsVisible() && Off <= 0.1f,
				FString::Printf(TEXT("%s (%s, %s, %s): shut, the panel at its place before Brief 46 (%.3f mm off); opening %s, doorway extent %s"), *Label, *D->GetName(), *D->GetClass()->GetName(),
					D->PanelMesh ? *D->PanelMesh->GetName() : TEXT("cube"), Off * 10.f, *D->GetDoorSize().ToCompactString(), *D->GetDoorway()->GetScaledBoxExtent().ToCompactString()));
			GRun.ShutPixels = PanelPixelsFrom(D, EyeBefore(D, 1.f), true);
			Note(FString::Printf(TEXT("%s: shut, %d pixels of the panel in the opening from 4 m in front (%d of it in all)"), *Label, GRun.ShutPixels, PanelPixelsFrom(D, EyeBefore(D, 1.f))));
			return 0.3f;
		}));
		AddMotion(S, Label + TEXT(": opening"), Door, true, Open, [Door, Label, bShotsAtHome]()
		{
			const ADoorDevice* D = Door();
			const int32 Half = PanelPixelsFrom(D, EyeBefore(D, 1.f), true);
			const float Cover = GRun.ShutPixels > 0 ? static_cast<float>(Half) / GRun.ShutPixels : -1.f;
			Check(Cover >= 1.f / 3.f && Cover <= 2.f / 3.f, FString::Printf(TEXT("%s: at half the motion (%.3f open) the panel covers %.2f of the opening it covers shut (%d of %d pixels from 4 m; %d of it in all)"),
				*Label, D->GetOpenFraction(), Cover, Half, GRun.ShutPixels, PanelPixelsFrom(D, EyeBefore(D, 1.f))));
			GRun.Results.FindOrAdd(GetNameSafe(D)).Cover = Cover;
			if (bShotsAtHome)
			{
				Shot(TEXT("PrintRoomDoor_Half"));
			}
		}, true, [Door](float T) { GRun.Results.FindOrAdd(GetNameSafe(Door())).Open = T; });
		S.Add(Do(Label + TEXT(": open, from each side"), [Door, Label, bShotsAtHome]()
		{
			ADoorDevice* D = Door();
			const int32 Front = PanelPixelsFrom(D, EyeBefore(D, 1.f));
			const int32 Back = PanelPixelsFrom(D, EyeBefore(D, -1.f));
			Check(Front == 0 && Back == 0, FString::Printf(TEXT("%s: open, no pixel of the panel from 4 m on each side (%d, %d)"), *Label, Front, Back));
			// Where a risen panel would show if it were not hidden: the panel shown at its open place for this measure only.
			UStaticMeshComponent* P = D->GetPanel();
			const bool bWas = P->IsVisible();
			P->SetVisibility(true);
			const int32 WouldFront = PanelPixelsFrom(D, EyeBefore(D, 1.f));
			const int32 WouldBack = PanelPixelsFrom(D, EyeBefore(D, -1.f));
			P->SetVisibility(bWas);
			FDoorResult& R = GRun.Results.FindOrAdd(GetNameSafe(D));
			R.WouldShowFront = WouldFront;
			R.WouldShowBack = WouldBack;
			Note(FString::Printf(TEXT("%s: RISEN a risen panel shown at its open place would show %d pixels from 4 m in front and %d behind: %s"), *Label, WouldFront, WouldBack,
				WouldFront + WouldBack > 0 ? TEXT("hidden once risen (it would show past the wall above)") : TEXT("the wall above hides it (hidden once risen as well)")));
			if (bShotsAtHome)
			{
				Shot(TEXT("PrintRoomDoor_Open"));
			}
			return 0.3f;
		}));
		AddMotion(S, Label + TEXT(": closing"), Door, false, Close, nullptr, true, [Door](float T) { GRun.Results.FindOrAdd(GetNameSafe(Door())).Close = T; });
		S.Add(Do(Label + TEXT(": DoorMoveSeconds 2.0 for the session"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->DoorMoveSeconds = 2.f;
			return 0.f;
		}));
		AddMotion(S, Label + TEXT(": opening at 2.0 s"), Door, true, Open, nullptr, false, [Door](float T) { GRun.Results.FindOrAdd(GetNameSafe(Door())).Open2 = T; });
		AddMotion(S, Label + TEXT(": closing at 2.0 s"), Door, false, Close, nullptr, false, [Door](float T) { GRun.Results.FindOrAdd(GetNameSafe(Door())).Close2 = T; });
		S.Add(Do(Label + TEXT(": DoorMoveSeconds back"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->DoorMoveSeconds = GRun.SavedMoveSeconds;
			return 0.f;
		}));
	}

	// --- Criterion 2: the clock ---

	static void AddClock(TArray<FStep>& S, bool bMenu)
	{
		const FString L = bMenu ? TEXT("2 pause menu") : TEXT("2 Stop");
		S.Add(Do(L + TEXT(": the first door opens, the pawn 4 m in front"), [L]()
		{
			ADoorDevice* D = FirstDoor();
			StandBefore(D, 1.f);
			GRun.MarkRun = RunNow();
			D->PowerOn();
			return 0.f;
		}));
		S.Add(WaitFor(L + TEXT(": half the motion"), []() { return FirstDoor() && FirstDoor()->GetOpenFraction() >= 0.5f; }, MoveSeconds() + 3.f, [bMenu, L]()
		{
			ADoorDevice* D = FirstDoor();
			GRun.HeldFraction = D->GetOpenFraction();
			GRun.HeldPanel = D->GetPanel()->GetComponentLocation();
			if (bMenu)
			{
				Check(StdPC() && StdPC()->PauseGame(), L + TEXT(": the pause menu opens"));
			}
			else
			{
				StopTime();
			}
			GRun.StopAtRun = RunNow();
			Note(FString::Printf(TEXT("%s: held at %.3f open"), *L, GRun.HeldFraction));
			return 2.f;
		}));
		S.Add(Do(L + TEXT(": 2 s later"), [bMenu, L]()
		{
			ADoorDevice* D = FirstDoor();
			const float Moved = static_cast<float>(FVector::Dist(D->GetPanel()->GetComponentLocation(), GRun.HeldPanel));
			Check(Moved <= 0.1f && FMath::IsNearlyEqual(D->GetOpenFraction(), GRun.HeldFraction, 1.e-4f) && D->IsMoving(),
				FString::Printf(TEXT("%s: the panel stood still for 2 s (%.3f mm, %.4f -> %.4f open), its motion not ended"), *L, Moved * 10.f, GRun.HeldFraction, D->GetOpenFraction()));
			if (!bMenu)
			{
				// A scan taken then: what a capture from the scan's origin sees of the panel, and the capsule's sweep.
				const UScanDirectorComponent* Dir = Director();
				const TArray<FLidarScanContext> Origins = Dir ? Dir->GetRecentScanOrigins() : TArray<FLidarScanContext>();
				const int32 Seen = Origins.Num() > 0 ? PanelPixelsFrom(D, Origins.Last().CaptureOrigin) : -1;
				Check(Dir && Dir->GetScansThisStop() >= 1 && Seen > 0, FString::Printf(TEXT("%s: a scan taken while it stands (%d this Stop) sees the panel there (%d pixels from its origin)"), *L,
					Dir ? Dir->GetScansThisStop() : 0, Seen));
				const FTransform F = OpeningFrame(D);
				const float Bottom = HalfSize(D).Z * (2.f * D->GetOpenFraction() - 1.f);
				const UCapsuleComponent* Cap = Pawn()->GetCapsuleComponent();
				const FVector From = F.TransformPosition(FVector(-150.f, 0.f, Bottom + 30.f));
				const FVector To = F.TransformPosition(FVector(150.f, 0.f, Bottom + 30.f));
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsDoorSweep), false, Pawn());
				const bool bHit = W()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Cap->GetScaledCapsuleRadius(), Cap->GetScaledCapsuleHalfHeight()), Params);
				Check(bHit && Hit.GetComponent() == D->GetPanelCollision(), FString::Printf(TEXT("%s: a sweep of the pawn's capsule through the opening meets the panel there (%s.%s)"), *L,
					*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent())));
				if (GRun.bArea)
				{
					Shot(TEXT("Gate_Scan_Stopped_Half"));
				}
				return 0.3f;
			}
			return 0.f;
		}));
		S.Add(Do(L + TEXT(": resume"), [bMenu, L]()
		{
			if (bMenu)
			{
				Check(StdPC() && StdPC()->ResumeGame(), L + TEXT(": the pause menu closes"));
			}
			else
			{
				ResumeTime();
			}
			GRun.MarkRun2 = RunNow();
			return 0.f;
		}));
		S.Add(WaitFor(L + TEXT(": the motion ends"), []() { return FirstDoor() && !FirstDoor()->IsMoving(); }, MoveSeconds() + 3.f, [L]()
		{
			const double Took = RunNow() - GRun.MarkRun2;
			const double Want = (1.0 - GRun.HeldFraction) * MoveSeconds();
			Check(FMath::Abs(Took - Want) <= 2.0 / 30.0 && FirstDoor()->IsFullyOpen(),
				FString::Printf(TEXT("%s: after the return the motion ended in %.3f s of the run's clock, the share left %.3f s"), *L, Took, Want));
			FirstDoor()->PowerOff();
			return 0.f;
		}));
		S.Add(WaitFor(L + TEXT(": shut again"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, MoveSeconds() + 3.f));
	}

	// --- Criterion 3: a paid door's time ---

	static void AddPowered(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("3 powered: 3 s for the session, opened"), []()
		{
			ADoorDevice* D = FirstDoor();
			GRun.SavedPowered.Add(D, D->PoweredSeconds);
			D->PoweredSeconds = 3.f;
			StandBefore(D, 1.f);
			GRun.MarkRun = RunNow();
			GRun.Motion = FMotion();
			D->PowerOn();
			return 1.5f;
		}));
		S.Add(Do(TEXT("3 powered: a Stop of 1 s in the powered time"), []()
		{
			StopTime();
			GRun.StopAtRun = RunNow();
			return 1.f;
		}));
		S.Add(Do(TEXT("3 powered: resume"), []()
		{
			ResumeTime();
			GRun.MarkRun2 = RunNow() - GRun.StopAtRun;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 powered: the closing starts"), []() { return FirstDoor() && FirstDoor()->GetMotionDirection() < 0; }, 8.f, []()
		{
			const double Normal = RunNow() - GRun.MarkRun - GRun.MarkRun2;
			Check(FMath::Abs(Normal - 3.0) <= 2.0 / 30.0 && !FirstDoor()->IsPowered(),
				FString::Printf(TEXT("3 powered: the closing starts %.3f s of Normal time after the opening started (PoweredSeconds 3; a Stop of %.2f s stood still)"), Normal, GRun.MarkRun2));
			GRun.MarkRun = RunNow();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 powered: shut"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, MoveSeconds() + 3.f, []()
		{
			const double Took = RunNow() - GRun.MarkRun;
			Check(FMath::Abs(Took - MoveSeconds()) <= 2.0 / 30.0, FString::Printf(TEXT("3 powered: shut %.3f s after the closing started (DoorMoveSeconds %.2f)"), Took, MoveSeconds()));
			// A time jump while open.
			FirstDoor()->PowerOn();
			return MoveSeconds() + 0.3f;
		}));
		S.Add(Do(TEXT("3 jump: Stop with the door open"), []()
		{
			Check(FirstDoor()->IsFullyOpen() && FirstDoor()->IsPowered(), TEXT("3 jump: the door open and powered before the Stop"));
			GRun.SavedBattery = Home() ? Home()->GetBatteryFraction() : 1.0;
			GRun.SavedHome = Home() ? Home()->GetHomeSecondsElapsed() : 0.0;
			StopTime();
			return 0.5f;
		}));
		S.Add(Do(TEXT("3 jump: the Stop ends with a jump"), []()
		{
			Check(Director() && Director()->ForceExit(EStopExitReason::Jumped), TEXT("3 jump: the Stop ends with a time jump (ForceExit Jumped)"));
			const ADoorDevice* D = FirstDoor();
			Check(!IsStopped() && !D->IsPowered() && D->GetMotionDirection() < 0,
				FString::Printf(TEXT("3 jump: on the return to Normal time the door is switched off and starts to close from where it stands (%.3f open)"), D->GetOpenFraction()));
			GRun.MarkRun = RunNow();
			if (Home())
			{
				Home()->RestoreState(GRun.SavedBattery, GRun.SavedHome);
			}
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 jump: shut"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, MoveSeconds() + 3.f, []()
		{
			const double Took = RunNow() - GRun.MarkRun;
			Check(FMath::Abs(Took - MoveSeconds()) <= 2.0 / 30.0, FString::Printf(TEXT("3 jump: shut %.3f s after the return (DoorMoveSeconds %.2f)"), Took, MoveSeconds()));
			FirstDoor()->PowerOn();
			return MoveSeconds() + 0.3f;
		}));
		S.Add(Do(TEXT("3 payment while closing: closing"), []()
		{
			GRun.Motion = FMotion();
			GRun.Motion.Mark = Audio() ? Audio()->GetRecordEnd() : 0;
			FirstDoor()->PowerOff();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 payment while closing: 40% closed"), []() { return FirstDoor() && FirstDoor()->GetOpenFraction() <= 0.6f; }, MoveSeconds() + 3.f, []()
		{
			ADoorDevice* D = FirstDoor();
			const float Before = D->GetOpenFraction();
			const FVector Panel = D->GetPanel()->GetComponentLocation();
			D->PowerOn();
			const float Moved = static_cast<float>(FVector::Dist(D->GetPanel()->GetComponentLocation(), Panel));
			const TArray<FGameSoundRequest> Stops = MotionSince(D, GRun.Motion.Mark, true);
			const TArray<FGameSoundRequest> Starts = MotionSince(D, GRun.Motion.Mark, false);
			Check(D->GetMotionDirection() > 0 && Moved <= 0.1f, FString::Printf(TEXT("3 payment while closing: it opens again from where it stands (%.3f open, the panel moved %.3f mm)"), Before, Moved * 10.f));
			Check(Stops.Num() == 1 && Stops[0].Source.Contains(TEXT("motion reverses")) && Starts.Num() == 2 && Starts[1].Source.EndsWith(TEXT("opens")) && Audio()->IsMotionSoundPlaying(D),
				FString::Printf(TEXT("3 payment while closing: the first sound fades out, the next starts (%d start(s), %d stop(s))"), Starts.Num(), Stops.Num()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 payment while closing: open"), []() { return FirstDoor() && FirstDoor()->IsFullyOpen(); }, MoveSeconds() + 3.f, []()
		{
			FirstDoor()->PowerOff();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3: shut"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, MoveSeconds() + 3.f));
	}

	// --- Criterion 4: the doorway ---

	static void PawnInDoorway(float AlongCm)
	{
		const ADoorDevice* D = FirstDoor();
		const FTransform F = OpeningFrame(D);
		StandAt(F.TransformPosition(FVector(AlongCm, 0.f, 0.f)), F.TransformPosition(FVector(AlongCm + 300.f, 0.f, 0.f)), FloorZ(D));
	}

	static void AddDoorway(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("4 doorway: opened for 3 s"), []()
		{
			ADoorDevice* D = FirstDoor();
			D->PoweredSeconds = 3.f;
			StandBefore(D, 1.f);
			GRun.MarkRun = RunNow();
			D->PowerOn();
			return MoveSeconds() + 0.2f;
		}));
		S.Add(Do(TEXT("4 doorway: the pawn in the doorway"), []()
		{
			PawnInDoorway(0.f);
			return 0.4f;
		}));
		S.Add(Do(TEXT("4 doorway: the pawn settled"), []()
		{
			GRun.PawnHeld = Pawn()->GetActorLocation();
			GRun.PawnDrift = 0.f;
			GRun.PawnInPanel = 0;
			GRun.bWatchPawn = true;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("4 doorway: the powered time ends"), []() { return FirstDoor() && !FirstDoor()->IsPowered(); }, 6.f, []() { return 3.f; }));
		S.Add(Do(TEXT("4 doorway: 3 s past its time"), []()
		{
			const ADoorDevice* D = FirstDoor();
			Check(D->IsFullyOpen() && !D->IsMoving() && D->IsClosePending() && D->IsDoorwayOccupied(),
				FString::Printf(TEXT("4 doorway: 3 s after its time ended, the pawn in the doorway keeps it open (%.3f open, pending %d)"), D->GetOpenFraction(), D->IsClosePending() ? 1 : 0));
			Check(GRun.PawnDrift <= 1.f && GRun.PawnInPanel == 0, FString::Printf(TEXT("4 doorway: with no input the pawn moved %.2f cm, %d frame(s) inside the panel's collision"), GRun.PawnDrift, GRun.PawnInPanel));
			GRun.bWatchPawn = false;
			StandBefore(D, 1.f);
			GRun.MarkRun = RunNow();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("4 doorway: the pawn left, the door closes"), []() { return FirstDoor() && FirstDoor()->GetMotionDirection() < 0; }, 2.f, []()
		{
			const double Took = RunNow() - GRun.MarkRun;
			Check(Took <= 2.0 / 30.0 + 1.e-3, FString::Printf(TEXT("4 doorway: the door starts to close %.3f s after the pawn left the doorway"), Took));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("4 doorway: 40% closed"), []() { return FirstDoor() && FirstDoor()->GetOpenFraction() <= 0.6f; }, MoveSeconds() + 3.f, []()
		{
			// Into the doorway, clear of the panel (60 cm from its plane), as a pawn walking in reaches it.
			GRun.PrevFraction = FirstDoor()->GetOpenFraction();
			PawnInDoorway(60.f);
			GRun.PawnHeld = Pawn()->GetActorLocation();
			GRun.PawnDrift = 0.f;
			GRun.PawnInPanel = 0;
			GRun.bWatchPawn = true;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("4 doorway: the pawn entered, the door opens again"), []() { return FirstDoor() && FirstDoor()->GetMotionDirection() > 0; }, 2.f, []()
		{
			// From where it stands: no more than the travel of the two frames between the teleport and this reading.
			const float TwoFrames = static_cast<float>(2.0 / 30.0 / MoveSeconds());
			Check(FMath::Abs(FirstDoor()->GetOpenFraction() - GRun.PrevFraction) <= TwoFrames,
				FString::Printf(TEXT("4 doorway: a pawn that entered while it closed opens it again from where it stands (%.3f -> %.3f open, two frames' travel %.3f)"),
				GRun.PrevFraction, FirstDoor()->GetOpenFraction(), TwoFrames));
			return 0.3f;
		}));
		S.Add(Do(TEXT("4 doorway: the pawn settled"), []()
		{
			// The drift is measured from the settled place (StandAt leaves the pawn 1 cm above the floor); the panel's
			// collision is watched from the teleport on.
			GRun.PawnHeld = Pawn()->GetActorLocation();
			GRun.PawnDrift = 0.f;
			return MoveSeconds() + 0.3f;
		}));
		S.Add(Do(TEXT("4 doorway: held open"), []()
		{
			const ADoorDevice* D = FirstDoor();
			Check(D->IsFullyOpen() && D->IsClosePending(), FString::Printf(TEXT("4 doorway: open again and held for the pawn (%.3f open)"), D->GetOpenFraction()));
			Check(GRun.PawnDrift <= 1.f && GRun.PawnInPanel == 0, FString::Printf(TEXT("4 doorway: with no input the pawn moved %.2f cm, %d frame(s) inside the panel's collision"),
				GRun.PawnDrift, GRun.PawnInPanel));
			GRun.bWatchPawn = false;
			StandBefore(D, 1.f);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("4 doorway: shut once the pawn left"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, MoveSeconds() + 3.f, []()
		{
			ADoorDevice* D = FirstDoor();
			if (const float* Saved = GRun.SavedPowered.Find(D))
			{
				D->PoweredSeconds = *Saved;
			}
			return 0.f;
		}));
	}

	// --- Criterion 8: the gate's shots ---

	static void AddGateShots(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("8 shots: the first gate opening over 8 s, the flashlight on"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->DoorMoveSeconds = 8.f;
			StandBefore(FirstDoor(), 1.f);
			SetFlashlight(true);
			FirstDoor()->PowerOn();
			return 0.f;
		}));
		for (const float Share : { 0.25f, 0.5f, 0.75f })
		{
			S.Add(WaitFor(FString::Printf(TEXT("8 shots: %.2f open"), Share), [Share]() { return FirstDoor() && FirstDoor()->GetOpenFraction() >= Share; }, 12.f, [Share]()
			{
				Shot(FString::Printf(TEXT("Gate_Opening_%02d"), FMath::RoundToInt(Share * 100.f)));
				return 0.2f;
			}));
		}
		S.Add(WaitFor(TEXT("8 shots: open"), []() { return FirstDoor() && FirstDoor()->IsFullyOpen(); }, 12.f, []()
		{
			Shot(TEXT("Gate_Open"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("8 shots: shut"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->DoorMoveSeconds = GRun.SavedMoveSeconds;
			FirstDoor()->PowerOff();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("8 shots: shut again"), []() { return FirstDoor() && !FirstDoor()->IsOpen(); }, 12.f));
	}

	// --- The run ---

	static void Finish(const TCHAR* Why)
	{
		GRun.bRunning = false;
		GRun.Motion.bActive = false;
		GRun.bWatchPawn = false;
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GRun.Ticker);
			GRun.Ticker.Reset();
		}
		GetMutableDefault<UTheLastShiftSettings>()->DoorMoveSeconds = GRun.SavedMoveSeconds;
		for (const TPair<TWeakObjectPtr<ADoorDevice>, float>& Pair : GRun.SavedPowered)
		{
			if (ADoorDevice* D = Pair.Key.Get())
			{
				D->PoweredSeconds = Pair.Value;
			}
		}
		if (StdPC() && StdPC()->IsPausedByMenu())
		{
			StdPC()->ResumeGame();
		}
		ResumeTime();
		for (const TWeakObjectPtr<ADoorDevice>& Each : GRun.Doors)
		{
			if (ADoorDevice* D = Each.Get(); D && D != GRun.Spawned.Get() && D->IsPowered())
			{
				D->PowerOff();
			}
		}
		if (ADoorDevice* D = GRun.Spawned.Get())
		{
			D->Destroy();
		}
		if (AActor* Dep = GRun.Departure.Get())
		{
			Dep->SetActorTickEnabled(true);
		}
		if (ACharacter* P = Pawn())
		{
			P->SetActorLocation(GRun.StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
			if (PC())
			{
				PC()->SetControlRotation(GRun.StartRotation);
			}
		}
		for (const TPair<FString, FDoorResult>& Pair : GRun.Results)
		{
			const FDoorResult& R = Pair.Value;
			Note(FString::Printf(TEXT("DOOR %s: open %.3f s, close %.3f s (at 2.0 s: %.3f, %.3f), largest step %.2f x the frame's share, cover at half %.2f, risen panel would show %d / %d px"),
				*R.Name, R.Open, R.Close, R.Open2, R.Close2, R.StepRatio, R.Cover, R.WouldShowFront, R.WouldShowBack));
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %d passed, %d failed%s."), Name(), GRun.Passed, GRun.Failed, Why);
	}

	static bool Tick(float)
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
		SampleMotion();
		SampleWatch();
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
		GRun.WaitUntilReal = FPlatformTime::Seconds() + Delay;
		return true;
	}

	static void Start(UWorld* InWorld)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: already running."), Name());
			return;
		}
		GRun = FRun();
		UWorld* World = ResolveGameWorld(InWorld);
		if (!World || !Pawn())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: needs a play session with the player."), Name());
			return;
		}
		GRun.Map = FPaths::GetBaseFilename(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
		GRun.bArea = Generator() != nullptr && URunSubsystem::GetActIndexOf(World) > 0;
		GRun.SavedMoveSeconds = UTheLastShiftSettings::Get()->DoorMoveSeconds;
		GRun.StartLocation = Pawn()->GetActorLocation();
		GRun.StartRotation = PC()->GetControlRotation();
		const bool bHome = URunSubsystem::GetPrologueStageOf(World) == 1;

		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("setup: the map ready"), []() { const AAreaGenerator* G = Generator(); return Pawn() && (!G || G->IsReady()); }, 60.f, [bHome]()
		{
			ResumeTime();
			// The doors of the map, the first gate first (the nearest to the player's start), then a spawned one.
			TArray<ADoorDevice*> Found;
			for (TActorIterator<ADoorDevice> It(W()); It; ++It)
			{
				Found.Add(*It);
			}
			const FVector From = Pawn()->GetActorLocation();
			if (bHome)
			{
				// The print room's door first: the door nearest the start that is no entry door.
				Found.Sort([From](const ADoorDevice& A, const ADoorDevice& B)
				{
					const bool bAEntry = A.IsA<APrologueEntryDoor>();
					const bool bBEntry = B.IsA<APrologueEntryDoor>();
					return bAEntry != bBEntry ? !bAEntry : FVector::DistSquared(A.GetActorLocation(), From) < FVector::DistSquared(B.GetActorLocation(), From);
				});
				// The departure scene must not start while the check moves the pawn round the hangar.
				for (TActorIterator<APrologueDeparture> It(W()); It; ++It)
				{
					GRun.Departure = *It;
					It->SetActorTickEnabled(false);
				}
			}
			else
			{
				Found.Sort([From](const ADoorDevice& A, const ADoorDevice& B) { return FVector::DistSquared(A.GetActorLocation(), From) < FVector::DistSquared(B.GetActorLocation(), From); });
			}
			FString List;
			for (ADoorDevice* D : Found)
			{
				GRun.Doors.Add(D);
				List += FString::Printf(TEXT("%s%s (%s, %s, powered %.0f s, at %s)"), List.IsEmpty() ? TEXT("") : TEXT(", "), *D->GetName(), *D->GetClass()->GetName(),
					D->PanelMesh ? *D->PanelMesh->GetName() : TEXT("cube"), D->PoweredSeconds, *D->GetActorLocation().ToCompactString());
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Feet = Pawn()->GetActorLocation() - FVector(0.f, 0.f, Pawn()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			// The spawned door stands in the open: 5 m along the longest free line from the pawn (of 16, at eye height), away
			// from every door of the map, its front toward the pawn (so that 4 m in front of it is 1 m from the pawn).
			FVector Dir = Pawn()->GetActorForwardVector().GetSafeNormal2D();
			double Longest = -1.0;
			for (int32 K = 0; K < 16; ++K)
			{
				const FVector Try = FRotator(0.f, K * 22.5f, 0.f).Vector();
				const FVector Eye = Feet + FVector(0.f, 0.f, 165.f);
				FHitResult Hit;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(TlsDoorSpawn), false, Pawn());
				const double Free = W()->LineTraceSingleByChannel(Hit, Eye, Eye + Try * 2000.f, ECC_Visibility, Query) ? Hit.Distance : 2000.0;
				bool bNearDoor = false;
				for (const ADoorDevice* D : Found)
				{
					bNearDoor |= FVector::Dist2D(D->GetActorLocation(), Feet + Try * 500.f) < 400.f;
				}
				if (!bNearDoor && Free > Longest)
				{
					Longest = Free;
					Dir = Try;
				}
			}
			Note(FString::Printf(TEXT("setup: the spawned door 5 m along the longest free line from the pawn (%.0f cm free, yaw %.1f)"), Longest, Dir.Rotation().Yaw));
			ADoorDevice* Spawned = W()->SpawnActor<ADoorDevice>(ADoorDevice::StaticClass(), Feet + Dir * 500.f, (-Dir).Rotation(), Params);
			GRun.Spawned = Spawned;
			if (Spawned)
			{
				GRun.Doors.Add(Spawned);
			}
			Check(Found.Num() > 0 && Spawned, FString::Printf(TEXT("setup: %d door(s) in %s: %s; one spawned (%s)"), Found.Num(), *GRun.Map, *List, *GetNameSafe(Spawned)));
			bool bNoDoorSlot = true;
			for (int32 I = 0; I < static_cast<int32>(EGameSound::Count); ++I)
			{
				bNoDoorSlot &= FCString::Stricmp(LexToString(static_cast<EGameSound>(I)), TEXT("Door")) != 0;
			}
			Check(bNoDoorSlot, TEXT("5: no Door slot among the audio's slots"));
			Note(FString::Printf(TEXT("DoorMoveSeconds %.2f (the settings), the Motion slot %s, MotionFadeOutSeconds %.2f"), MoveSeconds(),
				*UTheLastShiftSettings::Get()->Audio.GetSound(EGameSound::Motion).ToString(), UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds()));
			return Found.Num() > 0 ? 0.f : -1.f;
		}));
		if (GRun.bArea)
		{
			S.Add(Do(TEXT("6 the area's plan"), []()
			{
				const AAreaGenerator* G = Generator();
				const int32 Act = URunSubsystem::GetActIndexOf(W());
				const int64 Reference = ReferenceHash(Act);
				Check(G && (Reference == 0 || G->GetLayoutHash() == Reference), FString::Printf(TEXT("6: the layout hash %016llx (reference %016llx)"),
					G ? static_cast<uint64>(G->GetLayoutHash()) : 0ull, static_cast<uint64>(Reference)));
				Check(G && G->bLastValidationPassed && G->LastSeedsTried == 1, FString::Printf(TEXT("6: the validation passed at the first try (%s, seeds tried %d)"),
					G && G->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED"), G ? G->LastSeedsTried : -1));
				return 0.f;
			}));
		}
		// Criterion 1 and 5 for every door; the spawned one last.
		const int32 Count = [World]() { int32 N = 0; for (TActorIterator<ADoorDevice> It(World); It; ++It) { ++N; } return N; }() + 1;
		for (int32 I = 0; I < Count; ++I)
		{
			AddDoor(S, [I]() { return DoorAt(I); }, FString::Printf(TEXT("1 door %d"), I), bHome && I == 0);
		}
		AddClock(S, false);
		AddClock(S, true);
		if (GRun.bArea)
		{
			AddPowered(S);
			AddDoorway(S);
			AddGateShots(S);
		}
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.f);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: started in %s (%s), %d steps."), Name(), *GRun.Map, GRun.bArea ? TEXT("an area") : TEXT("a hand-built map"), S.Num());
	}

	static FAutoConsoleCommandWithWorldAndArgs GDoorCheckCommand(
		TEXT("tls.DoorCheck"),
		TEXT("tls.DoorCheck (an area map or a hand-built map; PIE): every door of the map and a spawned one rise open and come down shut over DoorMoveSeconds (times, steps, the panel's cover at half its motion, nothing seen past the frame, the motion's sound), the clock (a Stop and the pause menu at half a motion), and in an area the powered time, a time jump, a payment while closing, the doorway and the layout hash; shots to Saved/Screenshots/Brief46."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld) { Start(InWorld); }));
}

#endif // !UE_BUILD_SHIPPING
