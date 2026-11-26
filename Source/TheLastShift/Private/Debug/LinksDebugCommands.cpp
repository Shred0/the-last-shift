// SPDX-License-Identifier: MIT

// Brief 13, neighbour lines between scan points: tls.LinksTest (TestLidar: the values pushed from the settings, the mesh of
// a scan, the cap, line of sight through a wall and past a wall's edge, lines on their own surface at 3 m, 10 m and at a
// grazing angle, growth and the pause menu, life with the scan's fade, Resume and the 17th scan, width, colour and
// brightness), tls.Links.ShipCheck (Prologue_Arrival), tls.Links.ViewCheck (any map: the current view measured with and
// without lines, geometry visible and hidden), tls.Links.Set, tls.Links.Report, tls.Links.ScanFrame and
// tls.Exposure.Report. Shots are measured by the commands themselves and saved to Saved/Screenshots/Brief13.
// Brief 25, scan memory by density: tls.MemoryTest (TestLidar), tls.Memory.Set, tls.Memory.Report, tls.Memory.Shots, and the
// overlap and steps scenarios of tls.Links.DistanceCheck. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Debug/EntityFormTestScope.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Camera/CameraComponent.h"
#include "Debug/LidarTestSource.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Residue/Residue.h"
#include "Framework/StdPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarScanComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

#include "DynamicRHI.h"
#include "RenderTimer.h"

namespace LinksDebug
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

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
			GRun.Cleanup = nullptr;
		}
		EntityFormTest::Restore(*GRun.Name);
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 13 test"));
		}
	}

	static UWorld* W() { return ResolveGameWorld(nullptr); }

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

	// --- World access ---

	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static double WorldNow() { const UWorld* World = W(); return World ? World->GetTimeSeconds() : 0.0; }
	static int32 AliveScans() { const ULidarScanComponent* L = Lidar(); return L ? L->GetAliveScanCount() : 0; }
	static int32 NewestScan() { const ULidarScanComponent* L = Lidar(); return L ? L->GetScanCount() - 1 : INDEX_NONE; }

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	/** Height of the pawn standing on the floor, taken the first time StandAt runs in a test (a fall cannot carry over). */
	static double GStandZ = -1.e30;

	/** The pawn at a floor point, at the standing height of the test's start, looking at LookAt. */
	static void StandAt(const FVector& Floor, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		if (GStandZ < -1.e29)
		{
			GStandZ = P->GetActorLocation().Z;
		}
		// Stand on whatever floor is under the point (a raised floor, a ship's deck), found by a trace from 1.5 m above the
		// test's standing height; without one, the test's standing height.
		double Z = GStandZ;
		const ACharacter* Standing = Cast<ACharacter>(P);
		const double HalfHeight = Standing && Standing->GetCapsuleComponent() ? Standing->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("LinksStandAt"), false, P);
		if (UWorld* World = W(); World && World->LineTraceSingleByChannel(Hit, FVector(Floor.X, Floor.Y, GStandZ + 150.0), FVector(Floor.X, Floor.Y, GStandZ - 1000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + HalfHeight + 2.0;
		}
		P->SetActorLocation(FVector(Floor.X, Floor.Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		C->SetControlRotation((LookAt - EyeOf(P)).Rotation());
	}

	static void KeepEntityDormant()
	{
		if (UTimeStateSubsystem* T = TS())
		{
			T->ResetCumulativeStoppedSeconds();
		}
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
			T->Resume();
		}
	}

	// --- The line values: the settings are their one owner; tests change them there and the director pushes ---

	static FScanLinks GSavedLinks;
	static bool bLinksSaved = false;

	static void SaveLinks()
	{
		GSavedLinks = UTheLastShiftSettings::GetScanLinks();
		bLinksSaved = true;
	}

	static void PushLinks(const FScanLinks& Values)
	{
		GetMutableDefault<UTheLastShiftSettings>()->ScanLinks = Values;
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanLinks();
		}
	}

	static void RestoreLinks()
	{
		if (bLinksSaved)
		{
			PushLinks(GSavedLinks);
			bLinksSaved = false;
		}
	}

	// --- Brief 25, the memory values: the settings are their one owner; tests change them there and the director pushes ---

	static FScanMemory GSavedMemory;
	static bool bMemorySaved = false;

	static void PushMemorySettings(const FScanMemory& Values)
	{
		GetMutableDefault<UTheLastShiftSettings>()->ScanMemory = Values;
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanMemory();
		}
	}

	/** Sets the memory ratio for the rest of the run (the values are put back by CommonCleanup). */
	static void SetMemoryRatio(float Ratio)
	{
		if (!bMemorySaved)
		{
			GSavedMemory = UTheLastShiftSettings::GetScanMemory();
			bMemorySaved = true;
		}
		FScanMemory Values = UTheLastShiftSettings::GetScanMemory();
		Values.Ratio = Ratio;
		PushMemorySettings(Values);
	}

	static void RestoreMemory()
	{
		if (bMemorySaved)
		{
			PushMemorySettings(GSavedMemory);
			bMemorySaved = false;
			const ULidarScanComponent* L = Lidar();
			UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  scan memory back to the settings' values: %s (lidar ratio %.1f, tolerance %.1f cm)"), *GRun.Name,
				*FString::Printf(TEXT("ScanMemory=(Ratio=%.1f,ToleranceCm=%.1f)"), GSavedMemory.Ratio, GSavedMemory.ToleranceCm), L ? L->MemoryRatio : -1.f, L ? L->MemoryTolerance : -1.f);
		}
	}

	/**
	 * Brief 25 follow-up: the scan memory off (ratio 0, through the director) for the whole run of a test whose checks expect
	 * every alive scan to keep its points and lines (several scans of one place); CommonCleanup puts the settings' values back.
	 */
	static void MemoryOffForRun()
	{
		const FScanMemory Before = UTheLastShiftSettings::GetScanMemory();
		SetMemoryRatio(0.f);
		const ULidarScanComponent* L = Lidar();
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  scan memory off for this run (settings %s; lidar ratio now %.1f), put back at the end"), *GRun.Name,
			*FString::Printf(TEXT("ScanMemory=(Ratio=%.1f,ToleranceCm=%.1f)"), Before.Ratio, Before.ToleranceCm), L ? L->MemoryRatio : -1.f);
	}

	static FString MemoryIniLine(const FScanMemory& V)
	{
		return FString::Printf(TEXT("ScanMemory=(Ratio=%.1f,ToleranceCm=%.1f)"), V.Ratio, V.ToleranceCm);
	}

	/** Lines drawn (the saved intensity) or not (intensity 0): everything else of the scans stays as it is. */
	static void ShowLines(bool bShow)
	{
		FScanLinks Values = UTheLastShiftSettings::GetScanLinks();
		Values.Intensity = bShow ? GSavedLinks.Intensity : 0.f;
		PushLinks(Values);
	}

	static bool LinksReadyFor(int32 ScanIndex)
	{
		const ULidarScanComponent* L = Lidar();
		return L && ScanIndex != INDEX_NONE && L->GetLastLinkStats().ScanIndex == ScanIndex;
	}

	static FLidarScanSlotStats SlotOf(int32 ScanIndex)
	{
		const ULidarScanComponent* L = Lidar();
		for (const FLidarScanSlotStats& S : L ? L->GetScanSlotStats() : TArray<FLidarScanSlotStats>())
		{
			if (S.ScanIndex == ScanIndex)
			{
				return S;
			}
		}
		return FLidarScanSlotStats();
	}

	/** While held, AlphaFloor is 1 (no age fade), so shots taken at different moments match. */
	static float GSavedAlphaFloor = -1.f;

	static void HoldAgeFade(bool bHold)
	{
		ULidarScanComponent* L = Lidar();
		if (!L)
		{
			return;
		}
		if (bHold && GSavedAlphaFloor < 0.f)
		{
			GSavedAlphaFloor = L->AlphaFloor;
			L->AlphaFloor = 1.f;
			L->ApplyLiveSettings();
		}
		else if (!bHold && GSavedAlphaFloor >= 0.f)
		{
			L->AlphaFloor = GSavedAlphaFloor;
			GSavedAlphaFloor = -1.f;
			L->ApplyLiveSettings();
		}
	}

	// --- Shots ---

	struct FShot
	{
		FString Label;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
		double WorldTime = 0.0;
		float Growth = -1.f;
	};
	static FShot GShot;
	static bool bShotReady = false;
	/** Folder under Saved/Screenshots the shots go to (tls.Links.DistanceCheck uses Brief13b). */
	static FString GShotFolder = TEXT("Brief13");
	/** Prefix of the shots' file names (tls.Links.DistanceCheck's Tag: Before, After), none by default. */
	static FString GShotTag;
	static FDelegateHandle GShotHandle;

	static int32 Level(const FColor& C) { return FMath::Max3(C.R, C.G, C.B); }

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		GShot.WorldTime = WorldNow();
		const ULidarScanComponent* L = Lidar();
		GShot.Growth = L ? L->GetLinkGrowth(NewestScan()) : -1.f;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), GShotFolder, GShot.Label + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		bShotReady = true;
	}

	static void RequestShot(const FString& Label)
	{
		if (!GShotHandle.IsValid())
		{
			GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		GShot = FShot();
		GShot.Label = GShotTag.IsEmpty() ? Label : GShotTag + TEXT("_") + Label;
		bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), false, false);
	}

	static void ReleaseShots()
	{
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
	}

	static void AddShot(TArray<FStep>& S, const FString& Label, TFunction<void(const FShot&)> Then)
	{
		S.Add(Do(FString::Printf(TEXT("shot %s"), *Label), [Label]() { RequestShot(Label); return 0.f; }));
		S.Add(WaitFor(FString::Printf(TEXT("shot %s taken"), *Label), []() { return bShotReady; }, 5.f, [Then]()
		{
			if (Then)
			{
				Then(GShot);
			}
			return 0.f;
		}));
	}

	// --- Geometry hidden from rendering ---

	static TArray<TWeakObjectPtr<UPrimitiveComponent>> GHidden;

	static int32 HideGeometry()
	{
		GHidden.Reset();
		UWorld* World = W();
		const UClass* NiagaraClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraComponent"));
		for (TObjectIterator<UPrimitiveComponent> It; World && It; ++It)
		{
			UPrimitiveComponent* Prim = *It;
			if (!Prim || Prim->GetWorld() != World || !Prim->IsRegistered() || Prim->bHiddenInGame || !Prim->IsVisible()
				|| (NiagaraClass && Prim->IsA(NiagaraClass)))
			{
				continue;
			}
			Prim->SetHiddenInGame(true);
			GHidden.Add(Prim);
		}
		return GHidden.Num();
	}

	static void ShowGeometry()
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Prim : GHidden)
		{
			if (UPrimitiveComponent* P = Prim.Get())
			{
				P->SetHiddenInGame(false);
			}
		}
		GHidden.Reset();
	}

	// --- Measures ---

	/** A screen rectangle in pixels; empty = the whole frame. */
	struct FRect
	{
		int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
		bool IsEmpty() const { return X1 <= X0 || Y1 <= Y0; }
		bool Contains(int32 X, int32 Y) const { return IsEmpty() || (X >= X0 && X < X1 && Y >= Y0 && Y < Y1); }
	};

	/** Line pixels: lit in WithLines and brighter there than in WithoutLines by more than Tol levels. */
	static TArray<uint8> LineMask(const FShot& WithLines, const FShot& WithoutLines, int32 Tol = 3)
	{
		TArray<uint8> Mask;
		if (WithLines.Pixels.Num() != WithoutLines.Pixels.Num())
		{
			return Mask;
		}
		Mask.SetNumZeroed(WithLines.Pixels.Num());
		for (int32 Index = 0; Index < Mask.Num(); ++Index)
		{
			Mask[Index] = Level(WithLines.Pixels[Index]) > Level(WithoutLines.Pixels[Index]) + Tol ? 1 : 0;
		}
		return Mask;
	}

	struct FCount
	{
		int32 Pixels = 0;
		double Sum = 0.0;
	};

	static FCount CountMask(const TArray<uint8>& Mask, const FShot& WithLines, const FShot& WithoutLines, int32 Width, const FRect& Rect = FRect())
	{
		FCount C;
		for (int32 Index = 0; Index < Mask.Num(); ++Index)
		{
			if (Mask[Index] && Rect.Contains(Index % Width, Index / Width))
			{
				++C.Pixels;
				C.Sum += Level(WithLines.Pixels[Index]) - Level(WithoutLines.Pixels[Index]);
			}
		}
		return C;
	}

	static FCount CountLit(const FShot& Shot, const FRect& Rect = FRect())
	{
		FCount C;
		for (int32 Index = 0; Index < Shot.Pixels.Num(); ++Index)
		{
			const int32 L = Level(Shot.Pixels[Index]);
			if (L > 6 && Rect.Contains(Index % Shot.Width, Index / Shot.Width))
			{
				++C.Pixels;
				C.Sum += L;
			}
		}
		return C;
	}

	/** Interactable hue as seen on screen (green well above red and blue), as the occlusion test classifies it. */
	static bool IsInteractableHue(const FColor& C) { return C.G > 12 && C.G * 10 > C.R * 18 && C.G * 10 > C.B * 14; }

	static double Percentile(TArray<double> V, double P)
	{
		if (V.IsEmpty())
		{
			return 0.0;
		}
		V.Sort();
		return V[FMath::Clamp(FMath::FloorToInt32(P * (V.Num() - 1)), 0, V.Num() - 1)];
	}

	/** Screen position of a world point in viewport pixels, false when behind the camera or off screen. */
	static bool Project(const FVector& World, FVector2D& Out, int32 Width, int32 Height)
	{
		const APlayerController* C = PC();
		return C && C->ProjectWorldLocationToScreen(World, Out, false) && Out.X >= 0.0 && Out.Y >= 0.0 && Out.X < Width && Out.Y < Height;
	}

	/** Planar depth of a world point along the current camera's forward axis. */
	static double ViewDepth(const FVector& World)
	{
		const APlayerController* C = PC();
		if (!C || !C->PlayerCameraManager)
		{
			return 0.0;
		}
		return FVector::DotProduct(World - C->PlayerCameraManager->GetCameraLocation(), C->PlayerCameraManager->GetCameraRotation().Vector());
	}

	/**
	 * Width of the lines, in pixels, whose middle lies between MinDepth and MaxDepth (planar, cm) from the current view:
	 * across each such link of the newest scan, at a quarter, half and three quarters of its projected length, the run of
	 * line pixels through the line is counted. Returns the median and the number of samples.
	 */
	static double MeasureWidth(const TArray<uint8>& Mask, int32 Width, int32 Height, double MinDepth, double MaxDepth, int32& OutSamples, double& OutAreaWidth)
	{
		OutSamples = 0;
		OutAreaWidth = 0.0;
		const ULidarScanComponent* L = Lidar();
		TArray<FLidarLink> Links;
		TArray<float> Depths;
		TArray<int32> Degrees;
		int32 CaptureWidth = 0;
		if (!L || Mask.Num() != Width * Height || !L->GetLastScanLinks(Links, Depths, Degrees, CaptureWidth))
		{
			return 0.0;
		}
		auto At = [&Mask, Width, Height](int32 X, int32 Y) { return X >= 0 && Y >= 0 && X < Width && Y < Height && Mask[Y * Width + X] != 0; };
		TArray<double> Samples;
		// Area width: the line pixels within 2 px of the measured links, divided by their projected length.
		TArray<uint8> Near;
		Near.SetNumZeroed(Width * Height);
		double TotalLength = 0.0;
		for (const FLidarLink& Link : Links)
		{
			const double Depth = ViewDepth(0.5 * (Link.Start + Link.End));
			FVector2D A, B;
			if (Depth < MinDepth || Depth > MaxDepth || !Project(Link.Start, A, Width, Height) || !Project(Link.End, B, Width, Height))
			{
				continue;
			}
			const FVector2D AB = B - A;
			const double Length = AB.Size();
			if (Length < 8.0)
			{
				continue;
			}
			const FVector2D Across(-AB.Y / Length, AB.X / Length);
			TotalLength += Length;
			for (double T = 0.0; T <= Length; T += 0.5)
			{
				const FVector2D Q = A + AB * (T / Length);
				for (int32 DY = -2; DY <= 2; ++DY)
				{
					for (int32 DX = -2; DX <= 2; ++DX)
					{
						const int32 X = FMath::RoundToInt32(Q.X) + DX, Y = FMath::RoundToInt32(Q.Y) + DY;
						if (X >= 0 && Y >= 0 && X < Width && Y < Height)
						{
							Near[Y * Width + X] = 1;
						}
					}
				}
			}
			for (const double T : { 0.25, 0.5, 0.75 })
			{
				const FVector2D P = A + AB * T;
				// Nearest line pixel to the centre within 1 px, then the run on each side of it.
				int32 Start = INDEX_NONE;
				for (int32 K : { 0, -1, 1 })
				{
					if (At(FMath::RoundToInt32(P.X + Across.X * K), FMath::RoundToInt32(P.Y + Across.Y * K)))
					{
						Start = K;
						break;
					}
				}
				if (Start == INDEX_NONE)
				{
					continue;
				}
				int32 Run = 1;
				for (int32 K = Start + 1; K <= Start + 8 && At(FMath::RoundToInt32(P.X + Across.X * K), FMath::RoundToInt32(P.Y + Across.Y * K)); ++K)
				{
					++Run;
				}
				for (int32 K = Start - 1; K >= Start - 8 && At(FMath::RoundToInt32(P.X + Across.X * K), FMath::RoundToInt32(P.Y + Across.Y * K)); --K)
				{
					++Run;
				}
				// A run along a diagonal step of one pixel spans more than one pixel of the line's true width.
				Samples.Add(Run * FMath::Max(FMath::Abs(Across.X), FMath::Abs(Across.Y)));
			}
		}
		OutSamples = Samples.Num();
		int32 NearLine = 0;
		for (int32 Index = 0; Index < Near.Num(); ++Index)
		{
			NearLine += (Near[Index] && Mask[Index]) ? 1 : 0;
		}
		OutAreaWidth = TotalLength > 0.0 ? NearLine / TotalLength : 0.0;
		return Percentile(Samples, 0.5);
	}

	/** Share of points with 0, 1, 2 and more links, by depth band, for the last searched scan. */
	static void LogDegreeBands(const TCHAR* Prefix)
	{
		const ULidarScanComponent* L = Lidar();
		TArray<FLidarLink> Links;
		TArray<float> Depths;
		TArray<int32> Degrees;
		int32 CaptureWidth = 0;
		if (!L || !L->GetLastScanLinks(Links, Depths, Degrees, CaptureWidth))
		{
			return;
		}
		struct FBand { const TCHAR* Name; float Min; float Max; int32 Count[6] = { 0, 0, 0, 0, 0, 0 }; };
		FBand Bands[] = { { TEXT("2.5-3.5 m"), 250.f, 350.f }, { TEXT("9-11 m"), 900.f, 1100.f }, { TEXT("45-55 m"), 4500.f, 5500.f }, { TEXT("beyond 200 m"), 20000.f, 1.e9f } };
		for (int32 Texel = 0; Texel < Depths.Num(); ++Texel)
		{
			if (Depths[Texel] < 0.f)
			{
				continue;
			}
			for (FBand& Band : Bands)
			{
				if (Depths[Texel] >= Band.Min && Depths[Texel] < Band.Max)
				{
					++Band.Count[FMath::Min(Degrees[Texel], 5)];
				}
			}
		}
		for (const FBand& Band : Bands)
		{
			const int32 Total = Band.Count[0] + Band.Count[1] + Band.Count[2] + Band.Count[3] + Band.Count[4] + Band.Count[5];
			auto Share = [Total](int32 N) { return Total > 0 ? 100.0 * N / Total : 0.0; };
			UE_LOG(LogTheLastShift, Log, TEXT("%s %s: %d points, %.1f%% with 0 links, %.1f%% with 1, %.1f%% with 2, %.1f%% with 3, %.1f%% with 4, %.1f%% with more"),
				Prefix, Band.Name, Total, Share(Band.Count[0]), Share(Band.Count[1]), Share(Band.Count[2]), Share(Band.Count[3]), Share(Band.Count[4]), Share(Band.Count[5]));
		}
	}

	/**
	 * Brief 23: where the cap's dropped links are. Links are kept in texel order, so the cap drops those from the first
	 * dropped texel's row to the bottom of the capture: logs that row, the points from it down, how many lie on the floor
	 * (within 10 cm of the pawn's floor) and their depths along the capture axis.
	 */
	static void LogDroppedRows(const TCHAR* Prefix)
	{
		const ULidarScanComponent* L = Lidar();
		const FLidarLinkStats Stats = L ? L->GetLastLinkStats() : FLidarLinkStats();
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		if (!L || Stats.FirstDroppedTexel < 0 || !L->GetLastScanSamples(Positions, Channels, Width, Context) || Width <= 0)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s scan %d: the cap dropped no link."), Prefix, Stats.ScanIndex);
			return;
		}
		const int32 Height = Positions.Num() / Width;
		const int32 Row = Stats.FirstDroppedTexel / Width;
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		const double FloorZ = Character && Character->GetCapsuleComponent()
			? Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : -1.e30;
		int32 Points = 0, OnFloor = 0;
		TArray<double> Depth;
		for (int32 Texel = Stats.FirstDroppedTexel; Texel < Positions.Num(); ++Texel)
		{
			if (Channels[Texel] < 0)
			{
				continue;
			}
			++Points;
			OnFloor += FMath::Abs(Positions[Texel].Z - FloorZ) < 10.0 ? 1 : 0;
			Depth.Add(FVector::DotProduct(Positions[Texel] - Context.CaptureOrigin, Context.CaptureForward) / 100.0);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s scan %d (%s): the cap dropped %d links, those of the points from row %d of the capture's %d down (the bottom %.0f%% of the view): %d points there, %.0f%% on the floor, depth %.1f to %.1f m, median %.1f m."),
			Prefix, Stats.ScanIndex, Stats.Mode == ELidarLinkMode::Grid ? TEXT("grid") : TEXT("nearest"), Stats.Dropped, Row, Height, Height > 0 ? 100.0 * (Height - Row) / Height : 0.0,
			Points, Points > 0 ? 100.0 * OnFloor / Points : 0.0, Percentile(Depth, 0.0), Percentile(Depth, 1.0), Percentile(Depth, 0.5));
	}

	// =====================================================================================================
	// The view check: the current view with lines and without, geometry visible and hidden
	// =====================================================================================================

	struct FViewResult
	{
		FShot VisibleLines, VisibleNoLines, HiddenLines, HiddenNoLines;
	};
	static FViewResult GView;

	/**
	 * Four shots of the alive scans from the current view (age fade held): geometry visible with and without lines, then
	 * hidden with and without lines. Then hands them to Then.
	 */
	static void AddViewShots(TArray<FStep>& S, const FString& Label, TFunction<void(const FViewResult&)> Then)
	{
		S.Add(Do(Label + TEXT(": hold the age fade, lines on"), []() { HoldAgeFade(true); ShowLines(true); return 0.3f; }));
		AddShot(S, Label + TEXT("_Lines"), [](const FShot& Shot) { GView.VisibleLines = Shot; });
		S.Add(Do(Label + TEXT(": lines off"), []() { ShowLines(false); return 0.2f; }));
		AddShot(S, Label + TEXT("_NoLines"), [](const FShot& Shot) { GView.VisibleNoLines = Shot; });
		S.Add(Do(Label + TEXT(": geometry hidden"), []() { HideGeometry(); return 0.3f; }));
		AddShot(S, Label + TEXT("_Hidden_NoLines"), [](const FShot& Shot) { GView.HiddenNoLines = Shot; });
		S.Add(Do(Label + TEXT(": lines on"), []() { ShowLines(true); return 0.2f; }));
		AddShot(S, Label + TEXT("_Hidden_Lines"), [Then](const FShot& Shot)
		{
			GView.HiddenLines = Shot;
			ShowGeometry();
			HoldAgeFade(false);
			if (Then)
			{
				Then(GView);
			}
		});
	}

	/** Line pixels in view (geometry visible) and with the geometry hidden, points in view, in Rect. */
	struct FViewNumbers
	{
		FCount VisibleLines, HiddenLines, Points, HiddenPoints;
		int32 VisibleNotHidden = 0;
		TArray<uint8> HiddenMask;
	};

	static FViewNumbers Numbers(const FViewResult& V, const FRect& Rect = FRect())
	{
		FViewNumbers N;
		const int32 Width = V.VisibleLines.Width;
		const TArray<uint8> Visible = LineMask(V.VisibleLines, V.VisibleNoLines);
		N.HiddenMask = LineMask(V.HiddenLines, V.HiddenNoLines);
		N.VisibleLines = CountMask(Visible, V.VisibleLines, V.VisibleNoLines, Width, Rect);
		N.HiddenLines = CountMask(N.HiddenMask, V.HiddenLines, V.HiddenNoLines, Width, Rect);
		N.Points = CountLit(V.VisibleNoLines, Rect);
		N.HiddenPoints = CountLit(V.HiddenNoLines, Rect);
		for (int32 Index = 0; Index < Visible.Num() && Index < N.HiddenMask.Num(); ++Index)
		{
			N.VisibleNotHidden += (Visible[Index] && !N.HiddenMask[Index] && Rect.Contains(Index % Width, Index / Width)) ? 1 : 0;
		}
		return N;
	}

	static FString Describe(const FViewNumbers& N)
	{
		return FString::Printf(TEXT("line pixels %d in view / %d with the geometry hidden (%d in view and not hidden), point pixels %d in view / %d hidden"),
			N.VisibleLines.Pixels, N.HiddenLines.Pixels, N.VisibleNotHidden, N.Points.Pixels, N.HiddenPoints.Pixels);
	}

	/** Brightness of the lines against the points: line pixels (lines only) against point pixels, median and 90th percentile. */
	static FString Brightness(const FViewResult& V, double& OutRatio)
	{
		TArray<double> Line, Point;
		const TArray<uint8> Mask = LineMask(V.HiddenLines, V.HiddenNoLines);
		for (int32 Index = 0; Index < Mask.Num(); ++Index)
		{
			const int32 P = Level(V.HiddenNoLines.Pixels[Index]);
			if (Mask[Index] && P <= 6)
			{
				// A line pixel with no point under it: the line alone.
				Line.Add(Level(V.HiddenLines.Pixels[Index]));
			}
			else if (!Mask[Index] && P > 6)
			{
				Point.Add(P);
			}
		}
		const double LineP90 = Percentile(Line, 0.9);
		const double PointP90 = Percentile(Point, 0.9);
		OutRatio = PointP90 > 0.0 ? LineP90 / PointP90 : 0.0;
		return FString::Printf(TEXT("lines alone %d pixels, median %.0f, p90 %.0f levels; points %d pixels, median %.0f, p90 %.0f levels; p90 ratio %.2f (8-bit, after tonemapping)"),
			Line.Num(), Percentile(Line, 0.5), LineP90, Point.Num(), Percentile(Point, 0.5), PointP90, OutRatio);
	}

	static void StartRun(const FString& Name, bool bQuit)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuit = bQuit;
		GStandZ = -1.e30;
		GShotFolder = TEXT("Brief13");
		GShotTag.Reset();
		SaveLinks();
	}

	static void Launch()
	{
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started."), *GRun.Name);
	}

	static void FullResolution(bool bOn);

	static void CommonCleanup()
	{
		FullResolution(false);
		ShowGeometry();
		HoldAgeFade(false);
		RestoreLinks();
		RestoreMemory();
		ReleaseShots();
		if (AStdPlayerController* C = Cast<AStdPlayerController>(PC()); C && C->IsPausedByMenu())
		{
			C->ResumeGame();
		}
	}

	/** Adds: Resume (scans cleared), stand at Floor looking at LookAt, Stop, wait for the automatic scan and its links. */
	static void AddStopAndScanAt(TArray<FStep>& S, const FString& Label, const FVector& Floor, const FVector& LookAt)
	{
		S.Add(Do(Label + TEXT(": Resume, pose"), [Floor, LookAt]()
		{
			ResumeTime();
			KeepEntityDormant();
			StandAt(Floor, LookAt);
			return 0.7f;
		}));
		S.Add(Do(Label + TEXT(": Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f));
		S.Add(WaitFor(Label + TEXT(": its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return 0.2f; }));
	}

	/** Adds a scan straight on the lidar from the current pose and waits for its links. */
	static void AddScanHere(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": scan"), []() { if (ULidarScanComponent* L = Lidar()) { L->Scan(); } return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return 0.2f; }));
	}

	/** As AddStopAndScanAt, for lines off: waits for the scan and for its particle counts, not for links. */
	static void AddStopAndScanAtNoLinks(TArray<FStep>& S, const FString& Label, const FVector& Floor, const FVector& LookAt)
	{
		S.Add(Do(Label + TEXT(": Resume, pose"), [Floor, LookAt]()
		{
			ResumeTime();
			KeepEntityDormant();
			StandAt(Floor, LookAt);
			return 0.7f;
		}));
		S.Add(Do(Label + TEXT(": Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f, []() { return 0.5f; }));
	}

	static float WaitGrowthDone() { const ULidarScanComponent* L = Lidar(); return (L ? FMath::Clamp(L->LinkSeconds, 0.f, 10.f) : 1.5f) + 0.2f; }

	static const TCHAR* ModeName(ELidarLinkMode Mode) { return Mode == ELidarLinkMode::Grid ? TEXT("grid") : TEXT("nearest"); }

	/** Most texels the plugin's grid search looks along a row or a column (LidarScanLinks::MaxGridReach). */
	static constexpr int32 GridReachLimit = 64;

	/**
	 * Brief 23: the grid mesh of the last searched scan. Every link joins a point to a point after it in its row or in its
	 * column, at most GridReachLimit texels away, with no live sample between them (the read-back samples, channel -1 where
	 * a texel has no live point); none passes the angle between normals. Returns the checks' failures.
	 */
	static void CheckGridMesh(const FString& Prefix)
	{
		const ULidarScanComponent* L = Lidar();
		TArray<FLidarLink> Links;
		TArray<float> Depths;
		TArray<int32> Degrees;
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		if (!L || !L->GetLastScanLinks(Links, Depths, Degrees, Width) || !L->GetLastScanSamples(Positions, Channels, Width, Context) || Width <= 0)
		{
			Check(false, Prefix + TEXT(": the grid scan's links and samples are readable"));
			return;
		}
		const FLidarLinkStats Stats = L->GetLastLinkStats();
		Check(Stats.Mode == ELidarLinkMode::Grid, FString::Printf(TEXT("%s: the scan's links were found on the grid (mode %s)"), *Prefix, ModeName(Stats.Mode)));
		const float MaxAngle = GSavedLinks.MaxNormalAngle;
		const double MinDot = MaxAngle >= 180.f ? -2.0 : FMath::Cos(FMath::DegreesToRadians(MaxAngle));
		int32 OverAngle = 0, NotNext = 0, Right = 0, Down = 0, Sampled = 0;
		double WorstAngle = 0.0;
		for (const FLidarLink& Link : Links)
		{
			const double Dot = FVector::DotProduct(Link.StartNormal, Link.EndNormal);
			if (Dot < MinDot - 1.e-4)
			{
				++OverAngle;
			}
			WorstAngle = FMath::Max(WorstAngle, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0, 1.0))));
			const int32 A = Link.StartTexel;
			const int32 B = Link.EndTexel;
			const int32 AX = A % Width, AY = A / Width, BX = B % Width, BY = B / Width;
			const bool bRow = AY == BY && BX > AX && BX - AX <= GridReachLimit;
			const bool bColumn = AX == BX && BY > AY && BY - AY <= GridReachLimit;
			bool bNext = (bRow || bColumn) && Channels.IsValidIndex(A) && Channels.IsValidIndex(B) && Channels[A] >= 0 && Channels[B] >= 0;
			Right += bRow ? 1 : 0;
			Down += bColumn ? 1 : 0;
			// Every link is checked (the sample of the brief is all of them): no live sample between the two ends.
			const int32 Step = bRow ? 1 : Width;
			for (int32 T = A + Step; bNext && T < B; T += Step)
			{
				bNext = Channels[T] < 0;
			}
			NotNext += bNext ? 0 : 1;
			++Sampled;
		}
		Note(FString::Printf(TEXT("%s: grid mesh: %d links (%d along a row, %d along a column), widest angle between normals %.1f deg (limit %.1f)"),
			*Prefix, Links.Num(), Right, Down, WorstAngle, MaxAngle));
		Check(Stats.MaxDegree <= 4, FString::Printf(TEXT("%s: at most 4 links on one point on the grid (%d)"), *Prefix, Stats.MaxDegree));
		Check(OverAngle == 0, FString::Printf(TEXT("%s: no link passes the angle between normals (%d of %d)"), *Prefix, OverAngle, Links.Num()));
		Check(Sampled > 1000 && NotNext == 0,
			FString::Printf(TEXT("%s: the two ends of every link are neighbours on the capture's grid, in a row or a column, with no live sample between them (%d of %d are not)"), *Prefix, NotNext, Sampled));
	}

	// =====================================================================================================
	// tls.LinksTest (TestLidar)
	// =====================================================================================================

	// TestLidar: a 30 x 30 m hall split by two walls at X = -200 and X = 200 (20 cm thick) into the main room (X < -200), a
	// 4 m corridor and the east room. The corridor opens on the main room at Y 1100..1500 (the door) and on the east room at
	// Y -1500..-1100. A door terminal stands at (-1420, -1420).
	namespace Poses
	{
		static const FVector MainRoom(-850.f, 300.f, 0.f);
		static const FVector WestWall(-1500.f, 300.f, 120.f);
		static const FVector CorridorNorth(0.f, 900.f, 0.f);
		static const FVector CorridorFar(0.f, -1400.f, 100.f);
		static const FVector CorridorSouthWall(0.f, -600.f, 120.f);
		static const FVector CorridorWallFace(190.f, 300.f, 120.f);
		static const FVector CorridorFacing(-100.f, 300.f, 0.f);
		static const FVector DoorView(-100.f, -700.f, 0.f);
		static const FVector DoorTarget(700.f, -1350.f, 80.f);
		static const FVector Terminal(-1420.f, -1420.f, 100.f);
		static const FVector TerminalView(-700.f, -700.f, 0.f);
	}

	struct FState
	{
		int32 UncappedLinks = 0;
		int32 MeshScan = INDEX_NONE;
		TArray<FShot> GrowthShots;
		FShot PointsOnly;
		float PauseGrowth = -1.f;
		double PauseWorld = 0.0;
		TMap<int32, int32> LinksByScan;
		int32 FirstOfStop = INDEX_NONE;
		float SavedFade = -1.f;
		FCount FadeLinesEarly, FadePointsEarly;
		FShot FadeLinesShot;
		FTSTicker::FDelegateHandle Recorder;
		TSet<FIntVector> FirstSamples;
		int32 FirstSlotScan = INDEX_NONE;
		int32 RejectedBefore = 0;
		int32 FirstOfPair = INDEX_NONE;
		int32 DiscardedBefore = 0;
		int32 StatsBefore = INDEX_NONE;
		int32 HeldScan = INDEX_NONE;
	};
	static FState G;

	/** Records the links of every scan as they reach the point system (the 17th-scan check needs each scan's count). */
	static bool RecordLinks(float)
	{
		if (const ULidarScanComponent* L = Lidar())
		{
			const FLidarLinkStats Stats = L->GetLastLinkStats();
			if (Stats.ScanIndex != INDEX_NONE)
			{
				G.LinksByScan.Add(Stats.ScanIndex, Stats.Links);
			}
		}
		return GRun.bRunning;
	}

	/**
	 * The samples the point system spawned for the last searched scan, as read back: every texel written, the live samples
	 * equal to its captured points, each on its texel's ray at the captured depth minus the surface bias.
	 */
	static void CheckSamplesOfLastScan(const FString& Prefix)
	{
		ULidarScanComponent* L = Lidar();
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		TArray<float> Depth;
		int32 DW = 0, DH = 0;
		FLidarScanContext DepthContext;
		const bool bSamples = L && L->GetLastScanSamples(Positions, Channels, Width, Context);
		const bool bDepth = L && L->ReadNewestScanDepth(Depth, DW, DH, DepthContext);
		if (!bSamples || !bDepth || DepthContext.ScanIndex != Context.ScanIndex || DW != Width || Depth.Num() != Positions.Num())
		{
			Check(false, Prefix + FString::Printf(TEXT(": the samples and the depth of scan %d are readable (%d / %d, depth of scan %d)"),
				Context.ScanIndex, bSamples ? 1 : 0, bDepth ? 1 : 0, DepthContext.ScanIndex));
			return;
		}
		int32 Live = 0, Unwritten = 0, OutOfTexel = 0, EdgeTexel = 0, Unexplained = 0;
		double Worst = 0.0, Sum = 0.0;
		for (int32 Texel = 0; Texel < Positions.Num(); ++Texel)
		{
			if (Channels[Texel] <= -2)
			{
				++Unwritten;
				continue;
			}
			if (Channels[Texel] < 0)
			{
				continue;
			}
			++Live;
			const FVector Offset = Positions[Texel] - Context.CaptureOrigin;
			const double Along = FVector::DotProduct(Offset, Context.CaptureForward);
			const double Error = FMath::Abs(Along - (Depth[Texel] - L->SurfaceBias));
			Worst = FMath::Max(Worst, Error);
			Sum += Error;
			if (Error > 0.05)
			{
				// The GPU's nearest sampling can take the next texel when the jittered sample lies on a texel edge: then the
				// point has that texel's depth exactly. Anything else is an error.
				const int32 TX = Texel % DW, TY = Texel / DW;
				bool bNeighbour = false;
				for (int32 DY = -1; DY <= 1 && !bNeighbour; ++DY)
				{
					for (int32 DX = -1; DX <= 1 && !bNeighbour; ++DX)
					{
						const int32 NX = TX + DX, NY = TY + DY;
						bNeighbour = (DX != 0 || DY != 0) && NX >= 0 && NY >= 0 && NX < DW && NY < DH
							&& FMath::Abs(Along - (Depth[NY * DW + NX] - L->SurfaceBias)) <= 0.05;
					}
				}
				(bNeighbour ? EdgeTexel : Unexplained) += 1;
			}
			const double U = (FVector::DotProduct(Offset, Context.CaptureRight) / (Along * Context.TanHalfFOV.X) + 1.0) * 0.5 * DW;
			const double V = (1.0 - FVector::DotProduct(Offset, Context.CaptureUp) / (Along * Context.TanHalfFOV.Y)) * 0.5 * DH;
			const int32 X = Texel % DW, Y = Texel / DW;
			OutOfTexel += (U < X - 1.e-3 || U > X + 1 + 1.e-3 || V < Y - 1.e-3 || V > Y + 1 + 1.e-3) ? 1 : 0;
		}
		const int32 Captured = SlotOf(Context.ScanIndex).CapturedParticles;
		Note(Prefix + FString::Printf(TEXT(": scan %d read back: %d live samples, %d captured particles, %d texels unwritten, %d off their texel; depth error along the capture axis largest %.4f cm, mean %.5f cm; %d samples off by more than 0.05 cm, all at a neighbouring texel's depth but %d"),
			Context.ScanIndex, Live, Captured, Unwritten, OutOfTexel, Worst, Live > 0 ? Sum / Live : 0.0, EdgeTexel + Unexplained, Unexplained));
		Check(Unwritten == 0 && L->GetLastLinkStats().UnwrittenTexels == 0,
			Prefix + FString::Printf(TEXT(": the spawn writes every texel of the capture on every scan, texels with no hit included (%d unwritten)"), Unwritten));
		Check(Live == Captured && Live > 1000, Prefix + FString::Printf(TEXT(": the live samples read back are the captured points (%d / %d)"), Live, Captured));
		Check(OutOfTexel == 0 && Unexplained == 0,
			Prefix + FString::Printf(TEXT(": every read-back point lies on its texel's ray at the captured depth minus the surface bias, or a neighbouring texel's on a texel edge (largest error %.4f cm, %d at a neighbour's depth, %d unexplained, %d off their texel)"),
				Worst, EdgeTexel, Unexplained, OutOfTexel));
	}

	static void AddValuesAndMesh(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 15.f));
		S.Add(Do(TEXT("A: the values pushed from the settings"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const FScanLinks& V = GSavedLinks;
			Note(FString::Printf(TEXT("A: settings: %d neighbours, %.1f cm, %.2f px, intensity %.2f, angle %.1f deg, cap %d, mode %s (grid's distance %.1f cm)"),
				V.Neighbours, V.MaxDistanceCm, V.PixelWidth, V.Intensity, V.MaxNormalAngle, V.MaxLinksPerScan, ModeName(V.Mode), V.GridMaxDistanceCm));
			Check(L && L->LinkNeighbours == V.Neighbours && FMath::IsNearlyEqual(L->LinkMaxDistance, V.ActiveMaxDistanceCm()) && L->LinkMode == V.Mode
				&& FMath::IsNearlyEqual(L->LinePixelWidth, V.PixelWidth) && FMath::IsNearlyEqual(L->LineIntensity, V.Intensity)
				&& FMath::IsNearlyEqual(L->LinkMaxNormalAngle, V.MaxNormalAngle) && L->MaxLinksPerScan == V.MaxLinksPerScan,
				TEXT("A: the lidar holds the settings' line values (pushed by the director)"));
			Check(V.Neighbours > 0, TEXT("A: lines are on in the game"));
			return 0.f;
		}));

		// --- B: the mesh of one scan ---
		AddStopAndScanAt(S, TEXT("B"), Poses::MainRoom, Poses::WestWall);
		S.Add(Do(TEXT("B: the mesh of the scan"), []()
		{
			const ULidarScanComponent* L = Lidar();
			G.MeshScan = NewestScan();
			const FLidarLinkStats Stats = L->GetLastLinkStats();
			TArray<FLidarLink> Links;
			TArray<float> Depths;
			TArray<int32> Degrees;
			int32 CaptureWidth = 0;
			L->GetLastScanLinks(Links, Depths, Degrees, CaptureWidth);
			const TArray<FLidarScanContext> Alive = L->GetAliveScans();
			const FLidarScanContext Context = Alive.Num() > 0 ? Alive.Last() : FLidarScanContext();
			TMap<int32, int32> Degree;
			float Longest = 0.f;
			int32 CrossChannel = 0, NotThisScan = 0, Channel1 = 0;
			for (const FLidarLink& Link : Links)
			{
				Degree.FindOrAdd(Link.StartTexel)++;
				Degree.FindOrAdd(Link.EndTexel)++;
				Longest = FMath::Max(Longest, static_cast<float>(FVector::Dist(Link.Start, Link.End)));
				CrossChannel += Link.StartChannel != Link.EndChannel ? 1 : 0;
				Channel1 += Link.StartChannel > 0 ? 1 : 0;
				// Both ends are points of this scan's capture: a texel with a point, at that point's depth from this scan's origin.
				for (const TPair<int32, FVector>& End : { TPair<int32, FVector>(Link.StartTexel, Link.Start), TPair<int32, FVector>(Link.EndTexel, Link.End) })
				{
					const bool bTexel = Depths.IsValidIndex(End.Key) && Depths[End.Key] >= 0.f;
					const double Along = FVector::DotProduct(End.Value - Context.CaptureOrigin, Context.CaptureForward);
					NotThisScan += (!bTexel || FMath::Abs(Along - Depths[End.Key]) > 0.05) ? 1 : 0;
				}
			}
			int32 MaxDegree = 0;
			for (const TPair<int32, int32>& Pair : Degree)
			{
				MaxDegree = FMath::Max(MaxDegree, Pair.Value);
			}
			const FLidarScanSlotStats Slot = SlotOf(G.MeshScan);
			Note(FString::Printf(TEXT("B: scan %d: %d points (%d captured particles), %d links, %d dropped, at most %d per point, longest %.1f cm, %d links in channel 1, search %.2f ms on its worker, ready %.3f s after the scan"),
				G.MeshScan, Stats.Points, Slot.CapturedParticles, Stats.Links, Stats.Dropped, MaxDegree, Longest, Channel1, Stats.SearchMilliseconds, Stats.ReadySeconds));
			LogDegreeBands(*FString::Printf(TEXT("%s   ..  B: links per point"), *GRun.Name));
			Check(Stats.Links > 1000, FString::Printf(TEXT("B: the scan has links (%d)"), Stats.Links));
			Check(Stats.Points == Slot.CapturedParticles, FString::Printf(TEXT("B: the search rebuilt exactly the scan's points (%d found, %d particles)"), Stats.Points, Slot.CapturedParticles));
			const int32 MostLinks = GSavedLinks.Mode == ELidarLinkMode::Grid ? 4 : GSavedLinks.Neighbours;
			Check(MaxDegree <= MostLinks && Stats.MaxDegree == MaxDegree, FString::Printf(TEXT("B: at most %d links on one point (%d)"), MostLinks, MaxDegree));
			Check(Longest <= GSavedLinks.ActiveMaxDistanceCm() + 0.01f, FString::Printf(TEXT("B: the longest link is within %.1f cm (%.2f cm)"), GSavedLinks.ActiveMaxDistanceCm(), Longest));
			Check(CrossChannel == 0, FString::Printf(TEXT("B: no link joins two channels (%d)"), CrossChannel));
			Check(NotThisScan == 0, FString::Printf(TEXT("B: every link joins two captured points of its own scan: none joins two scans or an injected point (%d ends elsewhere)"), NotThisScan));

			G.UncappedLinks = Stats.Links + Stats.Dropped;
			if (GSavedLinks.Mode == ELidarLinkMode::Grid)
			{
				CheckGridMesh(TEXT("B (grid)"));
			}
			return 0.f;
		}));
		S.Add(Do(TEXT("B: the samples read back lie on their texels' rays"), []() { CheckSamplesOfLastScan(TEXT("B")); return 0.3f; }));
		S.Add(Do(TEXT("B: the line particles of the scan"), []()
		{
			const FLidarScanSlotStats Slot = SlotOf(G.MeshScan);
			const int32 Links = Lidar()->GetLastLinkStats().Links;
			Check(Slot.LineParticles == Links, FString::Printf(TEXT("B: one line particle per link once the links arrived (%d particles, %d links)"), Slot.LineParticles, Links));
			// The cap: a low cap for one scan from the same pose.
			FScanLinks Values = GSavedLinks;
			Values.MaxLinksPerScan = 500;
			PushLinks(Values);
			return 0.f;
		}));
		AddScanHere(S, TEXT("B: cap 500"));
		S.Add(Do(TEXT("B: the cap"), []()
		{
			const FLidarLinkStats Stats = Lidar()->GetLastLinkStats();
			Note(FString::Printf(TEXT("B: cap 500: %d links drawn, %d dropped, %d found; the same pose uncapped found %d"), Stats.Links, Stats.Dropped, Stats.Links + Stats.Dropped, G.UncappedLinks));
			Check(Stats.Links == 500 && Stats.Dropped > 0 && Lidar()->GetLastDroppedLinks() == Stats.Dropped,
				FString::Printf(TEXT("B: over the cap links are dropped and counted (%d drawn, %d dropped)"), Stats.Links, Stats.Dropped));
			Check(FMath::Abs(Stats.Links + Stats.Dropped - G.UncappedLinks) <= G.UncappedLinks / 10,
				FString::Printf(TEXT("B: drawn and dropped add up to what the scan found (%d + %d against %d uncapped from the same pose, another jitter)"), Stats.Links, Stats.Dropped, G.UncappedLinks));
			PushLinks(GSavedLinks);
			return 0.3f;
		}));
		S.Add(Do(TEXT("B: the capped scan's line particles"), []()
		{
			const FLidarScanSlotStats Slot = SlotOf(NewestScan());
			Check(Slot.LineParticles == 500, FString::Printf(TEXT("B: the capped scan draws 500 lines (%d particles)"), Slot.LineParticles));
			return 0.f;
		}));

		// --- B2: stale texels. Two scans in a row from different poses through the same slot (Resume frees it): no link of
		// the second ends on a sample position of the first ---
		AddStopAndScanAt(S, TEXT("B2 first"), Poses::MainRoom, Poses::WestWall);
		S.Add(Do(TEXT("B2: the first scan's samples"), []()
		{
			TArray<FVector> Positions;
			TArray<int32> Channels;
			int32 Width = 0;
			FLidarScanContext Context;
			Lidar()->GetLastScanSamples(Positions, Channels, Width, Context);
			G.FirstSamples.Reset();
			for (int32 Texel = 0; Texel < Positions.Num(); ++Texel)
			{
				if (Channels[Texel] >= 0)
				{
					G.FirstSamples.Add(FIntVector(FMath::RoundToInt32(Positions[Texel].X * 10.0), FMath::RoundToInt32(Positions[Texel].Y * 10.0), FMath::RoundToInt32(Positions[Texel].Z * 10.0)));
				}
			}
			G.FirstSlotScan = Context.ScanIndex;
			return 0.f;
		}));
		AddStopAndScanAt(S, TEXT("B2 second"), Poses::CorridorNorth, Poses::CorridorFar);
		S.Add(Do(TEXT("B2: no link of the second scan ends on the first scan's samples"), []()
		{
			TArray<FLidarLink> Links;
			TArray<float> Depths;
			TArray<int32> Degrees;
			int32 Width = 0;
			Lidar()->GetLastScanLinks(Links, Depths, Degrees, Width);
			int32 OnFirst = 0;
			for (const FLidarLink& Link : Links)
			{
				for (const FVector& End : { Link.Start, Link.End })
				{
					OnFirst += G.FirstSamples.Contains(FIntVector(FMath::RoundToInt32(End.X * 10.0), FMath::RoundToInt32(End.Y * 10.0), FMath::RoundToInt32(End.Z * 10.0))) ? 1 : 0;
				}
			}
			Note(FString::Printf(TEXT("B2: scan %d (%d samples) then scan %d (%d links) through the same slot"), G.FirstSlotScan, G.FirstSamples.Num(), NewestScan(), Links.Num()));
			Check(Links.Num() > 1000 && OnFirst == 0, FString::Printf(TEXT("B2: no link of the second scan ends on a position of the first (%d ends do)"), OnFirst));
			return 0.f;
		}));

		// --- B3: one readback per scan. Two scans in the same frame: the first gets no links, counted as rejected ---
		S.Add(Do(TEXT("B3: two scans in the same frame"), []()
		{
			ULidarScanComponent* L = Lidar();
			G.RejectedBefore = L->GetRejectedLinkScans();
			L->Scan();
			G.FirstOfPair = NewestScan();
			L->Scan();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B3: the second scan's links"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []()
		{
			const ULidarScanComponent* L = Lidar();
			const FLidarScanSlotStats First = SlotOf(G.FirstOfPair);
			Check(L->GetRejectedLinkScans() == G.RejectedBefore + 1 && L->GetLastLinkStats().Links > 1000 && First.LineParticles == 0,
				FString::Printf(TEXT("B3: the scan whose samples were not yet read back when the next started got no links (rejected %d -> %d, its lines %d, the next scan %d links)"),
					G.RejectedBefore, L->GetRejectedLinkScans(), First.LineParticles, L->GetLastLinkStats().Links));
			return 0.f;
		}));

		// --- B4: a result for a scan that is gone touches nothing (Resume while its search is held back) ---
		S.Add(Do(TEXT("B4: a scan whose result is held, then Resume"), []()
		{
			ULidarScanComponent* L = Lidar();
			G.DiscardedBefore = L->GetDiscardedLinkResults();
			G.StatsBefore = L->GetLastLinkStats().ScanIndex;
			L->LinkResultDelaySeconds = 1.f;
			L->Scan();
			G.HeldScan = NewestScan();
			return 0.3f;
		}));
		S.Add(Do(TEXT("B4: Resume"), []() { ResumeTime(); return 1.8f; }));
		S.Add(Do(TEXT("B4: the held result was dropped"), []()
		{
			ULidarScanComponent* L = Lidar();
			L->LinkResultDelaySeconds = 0.f;
			int32 Lines = 0;
			for (const FLidarScanSlotStats& Slot : L->GetScanSlotStats())
			{
				Lines += Slot.LineParticles;
			}
			Check(L->GetDiscardedLinkResults() > G.DiscardedBefore && L->GetLastLinkStats().ScanIndex == G.StatsBefore && Lines == 0 && L->GetPendingLinkSearches() == 0,
				FString::Printf(TEXT("B4: the result of scan %d, gone with Resume, was dropped and touched nothing (discarded %d -> %d, last searched scan %d, %d line particles)"),
					G.HeldScan, G.DiscardedBefore, L->GetDiscardedLinkResults(), L->GetLastLinkStats().ScanIndex, Lines));
			return 0.f;
		}));
	}

	/** Off means off: with 0 neighbours a scan queues no readback, runs no search and spawns no line particle. */
	static void AddOff(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("I: lines off"), []()
		{
			FScanLinks Values = GSavedLinks;
			Values.Neighbours = 0;
			PushLinks(Values);
			G.RejectedBefore = Lidar()->GetRejectedLinkScans();
			G.StatsBefore = Lidar()->GetLastLinkStats().ScanIndex;
			return 0.f;
		}));
		AddStopAndScanAtNoLinks(S, TEXT("I"), Poses::MainRoom, Poses::WestWall);
		S.Add(Do(TEXT("I: nothing ran for the lines"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const FLidarScanSlotStats Slot = SlotOf(NewestScan());
			Check(L->GetPendingLinkSearches() == 0 && L->GetLastLinkStats().ScanIndex == G.StatsBefore && L->GetRejectedLinkScans() == G.RejectedBefore
				&& Slot.LineParticles == 0 && Slot.CapturedParticles > 1000,
				FString::Printf(TEXT("I: with 0 neighbours no readback, no search and no line particle (%d pending, last searched scan %d, %d line particles, %d points)"),
					L->GetPendingLinkSearches(), L->GetLastLinkStats().ScanIndex, Slot.LineParticles, Slot.CapturedParticles));
			PushLinks(GSavedLinks);
			return 0.f;
		}));
	}

	static void AddLineOfSight(TArray<FStep>& S)
	{
		// --- C: a corridor scan seen from its own side and from behind the corridor's wall ---
		AddStopAndScanAt(S, TEXT("C"), Poses::CorridorNorth, Poses::CorridorFar);
		S.Add(Do(TEXT("C: wait for the growth"), []() { return WaitGrowthDone(); }));
		AddViewShots(S, TEXT("C_OwnSide"), [](const FViewResult& V)
		{
			const FViewNumbers N = Numbers(V);
			Note(FString::Printf(TEXT("C: from its own side: %s"), *Describe(N)));
			Check(N.VisibleLines.Pixels > 2000 && N.Points.Pixels > 2000, FString::Printf(TEXT("C: from its own side the scan shows points and lines (%d / %d pixels)"), N.Points.Pixels, N.VisibleLines.Pixels));
		});
		S.Add(Do(TEXT("C: behind the corridor's wall"), []() { StandAt(Poses::MainRoom, Poses::CorridorSouthWall); return 0.4f; }));
		AddViewShots(S, TEXT("C_BehindWall"), [](const FViewResult& V)
		{
			const FViewNumbers N = Numbers(V);
			Note(FString::Printf(TEXT("C: from behind the wall: %s"), *Describe(N)));
			Check(N.Points.Pixels > 2000, FString::Printf(TEXT("C: from behind the wall its points show (%d pixels)"), N.Points.Pixels));
			Check(N.HiddenLines.Pixels > 2000, FString::Printf(TEXT("C: with the wall hidden its lines would be there (%d pixels)"), N.HiddenLines.Pixels));
			Check(N.VisibleLines.Pixels == 0, FString::Printf(TEXT("C: from behind the wall no pixel of its lines shows (%d)"), N.VisibleLines.Pixels));
		});

		// --- D: lines passing behind the edge of a wall: the east room's floor and south wall by the open passage from the
		// corridor (Y -1500..-1100; the main room's door holds a closed door), scanned inside the east room, seen from the
		// corridor through the passage at an angle ---
		AddStopAndScanAt(S, TEXT("D"), FVector(800.f, -500.f, 0.f), FVector(800.f, -1450.f, 50.f));
		S.Add(Do(TEXT("D: past the passage's edge"), []() { StandAt(Poses::DoorView, Poses::DoorTarget); return WaitGrowthDone(); }));
		AddViewShots(S, TEXT("D_WallEdge"), [](const FViewResult& V)
		{
			const FViewNumbers N = Numbers(V);
			Note(FString::Printf(TEXT("D: through the passage: %s"), *Describe(N)));
			Check(N.VisibleLines.Pixels > 100, FString::Printf(TEXT("D: through the passage the east room's lines show (%d pixels)"), N.VisibleLines.Pixels));
			Check(N.HiddenLines.Pixels > N.VisibleLines.Pixels + 100, FString::Printf(TEXT("D: the parts behind the passage's edges are not drawn (%d of %d)"), N.VisibleLines.Pixels, N.HiddenLines.Pixels));
			Check(N.VisibleNotHidden <= FMath::Max(10, N.VisibleLines.Pixels / 100), FString::Printf(TEXT("D: every line pixel in view is one the hidden geometry also shows (%d are not)"), N.VisibleNotHidden));
		});
	}

	/** One surface view: a scan from Floor, then the view from a step aside; lines in view against the geometry hidden. */
	static void AddSurface(TArray<FStep>& S, const FString& Label, const FVector& Floor, const FVector& LookAt, const FVector& ViewOffset, double MinShare)
	{
		AddStopAndScanAt(S, Label, Floor, LookAt);
		S.Add(Do(Label + TEXT(": a step aside, wait for the growth"), [Floor, LookAt, ViewOffset]() { StandAt(Floor + ViewOffset, LookAt); return WaitGrowthDone(); }));
		AddViewShots(S, Label, [Label, MinShare](const FViewResult& V)
		{
			const FViewNumbers N = Numbers(V);
			const double Share = N.HiddenLines.Pixels > 0 ? static_cast<double>(N.VisibleLines.Pixels) / N.HiddenLines.Pixels : 0.0;
			Note(FString::Printf(TEXT("%s: %s; %.1f%% of the line pixels show with the geometry in place"), *Label, *Describe(N), 100.0 * Share));
			Check(N.HiddenLines.Pixels > 500 && Share >= MinShare,
				FString::Printf(TEXT("%s: its lines are not eaten by their own surface (%d of %d pixels, %.1f%%)"), *Label, N.VisibleLines.Pixels, N.HiddenLines.Pixels, 100.0 * Share));
		});
	}

	static void AddSurfaces(TArray<FStep>& S)
	{
		// --- E: a wall at 3 m, the floor and the far wall at 10 m, the corridor floor at a grazing angle ---
		AddSurface(S, TEXT("E_Wall_3m"), Poses::CorridorFacing, Poses::CorridorWallFace, FVector(0.f, 60.f, 0.f), 0.9);
		AddSurface(S, TEXT("E_Floor_10m"), FVector(-1400.f, 1000.f, 0.f), FVector(-700.f, 300.f, 0.f), FVector(40.f, 40.f, 0.f), 0.9);
		AddSurface(S, TEXT("E_Grazing"), FVector(0.f, 1300.f, 0.f), FVector(0.f, -600.f, 20.f), FVector(0.f, 80.f, 0.f), 0.8);
	}

	static void AddGrowth(TArray<FStep>& S)
	{
		// --- F: growth over the link time, older scans, the pause menu ---
		S.Add(Do(TEXT("F: Resume, pose"), []()
		{
			ResumeTime();
			KeepEntityDormant();
			StandAt(Poses::CorridorFacing, Poses::CorridorWallFace);
			G.GrowthShots.Reset();
			return 0.7f;
		}));
		S.Add(Do(TEXT("F: Stop, no age fade"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("F: the automatic scan"), []() { return AliveScans() >= 1; }, 4.f, []() { HoldAgeFade(true); return 0.f; }));
		for (const double Fraction : { 0.25, 0.5, 1.0 })
		{
			S.Add(WaitFor(FString::Printf(TEXT("F: %.0f%% of the link time"), Fraction * 100.0), [Fraction]()
			{
				const ULidarScanComponent* L = Lidar();
				return L && L->GetLinkGrowth(NewestScan()) >= Fraction - 0.03;
			}, 4.f));
			AddShot(S, FString::Printf(TEXT("F_Growth_%03d"), FMath::RoundToInt32(Fraction * 100.0)), [](const FShot& Shot) { G.GrowthShots.Add(Shot); });
		}
		S.Add(Do(TEXT("F: lines off"), []() { ShowLines(false); return 0.2f; }));
		AddShot(S, TEXT("F_Growth_NoLines"), [](const FShot& Shot)
		{
			ShowLines(true);
			G.PointsOnly = Shot;
			const ULidarScanComponent* L = Lidar();
			const int32 Full = G.GrowthShots.Num() == 3 ? CountMask(LineMask(G.GrowthShots[2], Shot), G.GrowthShots[2], Shot, Shot.Width).Pixels : 0;
			bool bOk = Full > 1000;
			for (const FShot& At : G.GrowthShots)
			{
				const int32 Pixels = CountMask(LineMask(At, Shot), At, Shot, Shot.Width).Pixels;
				const double Share = Full > 0 ? static_cast<double>(Pixels) / Full : 0.0;
				Note(FString::Printf(TEXT("F: %s at growth %.3f (%.3f s of %.2f s): %d line pixels, %.1f%% of the full lines"),
					*At.Label, At.Growth, At.Growth * (L ? L->LinkSeconds : 0.f), L ? L->LinkSeconds : 0.f, Pixels, 100.0 * Share));
				bOk &= At.Growth >= 0.f && FMath::Abs(Share - At.Growth) <= 0.15;
			}
			Check(bOk, TEXT("F: the drawn length of the lines follows the growth (within 15 points)"));
		});
		S.Add(Do(TEXT("F: a second scan"), []()
		{
			HoldAgeFade(false);
			StandAt(Poses::CorridorFacing, Poses::CorridorWallFace + FVector(0.f, -400.f, 0.f));
			if (ULidarScanComponent* L = Lidar())
			{
				L->Scan();
			}
			return 0.1f;
		}));
		S.Add(Do(TEXT("F: the older scan does not grow again"), []()
		{
			const ULidarScanComponent* L = Lidar();
			int32 Older = 0, Full = 0;
			for (const FLidarScanContext& C : L->GetAliveScans())
			{
				if (C.ScanIndex != NewestScan())
				{
					++Older;
					Full += L->GetLinkGrowth(C.ScanIndex) >= 1.f ? 1 : 0;
				}
			}
			const float NewGrowth = L->GetLinkGrowth(NewestScan());
			Check(Older > 0 && Full == Older && NewGrowth < 0.5f,
				FString::Printf(TEXT("F: 0.1 s after a new scan, the %d older scans stay grown (%d at 1) and only the new one grows (%.3f)"), Older, Full, NewGrowth));
			return 0.4f;
		}));
		S.Add(Do(TEXT("F: open the pause menu mid-growth"), []()
		{
			AStdPlayerController* C = Cast<AStdPlayerController>(PC());
			G.PauseGrowth = Lidar()->GetLinkGrowth(NewestScan());
			G.PauseWorld = WorldNow();
			Check(C && C->PauseGame(), TEXT("F: the pause menu opens"));
			return 1.5f;
		}));
		S.Add(Do(TEXT("F: still behind the menu"), []()
		{
			const float Growth = Lidar()->GetLinkGrowth(NewestScan());
			Check(G.PauseGrowth > 0.f && G.PauseGrowth < 1.f && FMath::IsNearlyEqual(Growth, G.PauseGrowth, 1.e-4f),
				FString::Printf(TEXT("F: 1.5 s behind the pause menu the growth stood still (%.3f -> %.3f)"), G.PauseGrowth, Growth));
			if (AStdPlayerController* C = Cast<AStdPlayerController>(PC()))
			{
				C->ResumeGame();
			}
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("F: after the menu the growth completes"), []() { return Lidar()->GetLinkGrowth(NewestScan()) >= 1.f; }, 4.f));
	}

	static void AddLife(TArray<FStep>& S)
	{
		// --- G: the scan's fade, Resume, the 17th scan ---
		AddStopAndScanAt(S, TEXT("G"), Poses::CorridorFacing, Poses::CorridorWallFace);
		S.Add(Do(TEXT("G: a short fade for the test"), []()
		{
			ULidarScanComponent* L = Lidar();
			G.SavedFade = L->FadeDuration;
			L->FadeDuration = 4.f;
			L->ApplyLiveSettings();
			return WaitGrowthDone() - 0.2f;
		}));
		AddShot(S, TEXT("G_Fade_Early_Lines"), [](const FShot& Shot) { G.FadeLinesShot = Shot; ShowLines(false); });
		AddShot(S, TEXT("G_Fade_Early_NoLines"), [](const FShot& Shot)
		{
			G.FadeLinesEarly = CountMask(LineMask(G.FadeLinesShot, Shot), G.FadeLinesShot, Shot, Shot.Width);
			G.FadePointsEarly = CountLit(Shot);
			ShowLines(true);
		});
		S.Add(Do(TEXT("G: wait for the fade"), []() { return 3.f; }));
		AddShot(S, TEXT("G_Fade_Late_Lines"), [](const FShot& Shot) { G.FadeLinesShot = Shot; ShowLines(false); });
		AddShot(S, TEXT("G_Fade_Late_NoLines"), [](const FShot& Shot)
		{
			const FCount Lines = CountMask(LineMask(G.FadeLinesShot, Shot), G.FadeLinesShot, Shot, Shot.Width);
			const FCount Points = CountLit(Shot);
			ShowLines(true);
			const double LineRatio = G.FadeLinesEarly.Sum > 0.0 ? Lines.Sum / G.FadeLinesEarly.Sum : 0.0;
			const double PointRatio = G.FadePointsEarly.Sum > 0.0 ? Points.Sum / G.FadePointsEarly.Sum : 0.0;
			Note(FString::Printf(TEXT("G: from %.1f s to %.1f s of age: lines %d px / sum %.0f -> %d px / sum %.0f (x%.2f), points %d px / sum %.0f -> %d px / sum %.0f (x%.2f)"),
				1.5f, 4.7f, G.FadeLinesEarly.Pixels, G.FadeLinesEarly.Sum, Lines.Pixels, Lines.Sum, LineRatio, G.FadePointsEarly.Pixels, G.FadePointsEarly.Sum, Points.Pixels, Points.Sum, PointRatio));
			Check(LineRatio > 0.0 && LineRatio < 0.8 && PointRatio < 0.8, FString::Printf(TEXT("G: the lines fade with their points (x%.2f, points x%.2f)"), LineRatio, PointRatio));
			ULidarScanComponent* L = Lidar();
			L->FadeDuration = G.SavedFade;
			L->ApplyLiveSettings();
		});
		S.Add(Do(TEXT("G: Resume"), []() { ResumeTime(); return 0.15f; }));
		S.Add(Do(TEXT("G: mid fade-out"), []()
		{
			const FLidarScanSlotStats Slot = SlotOf(NewestScan());
			Note(FString::Printf(TEXT("G: 0.15 s after Resume: scan fading out %d, %d line particles, %d points"), Slot.bFadingOut ? 1 : 0, Slot.LineParticles, Slot.CapturedParticles));
			return 0.7f;
		}));
		S.Add(Do(TEXT("G: after the fade-out"), []()
		{
			int32 Lines = 0, Points = 0;
			for (const FLidarScanSlotStats& Slot : Lidar()->GetScanSlotStats())
			{
				Lines += Slot.LineParticles;
				Points += Slot.CapturedParticles;
			}
			Check(Lines == 0 && Points == 0 && AliveScans() == 0, FString::Printf(TEXT("G: on Resume the lines clear with their points (%d line particles, %d points left)"), Lines, Points));
			return 0.f;
		}));
		// 17 scans in one Stop: the 17th reuses the slot of the first, whose lines must go with it.
		S.Add(Do(TEXT("G: Stop for 17 scans"), []()
		{
			// Results are held 3 s, so the first scan's result arrives after the 17th scan has taken its slot.
			Lidar()->LinkResultDelaySeconds = 3.f;
			G.DiscardedBefore = Lidar()->GetDiscardedLinkResults();
			G.LinksByScan.Reset();
			G.Recorder = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RecordLinks));
			KeepEntityDormant();
			StandAt(Poses::MainRoom, Poses::WestWall);
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("G: the automatic scan"), []() { return AliveScans() >= 1; }, 4.f, []() { G.FirstOfStop = NewestScan(); return 0.3f; }));
		for (int32 Index = 1; Index < 17; ++Index)
		{
			S.Add(Do(FString::Printf(TEXT("G: scan %d of the Stop"), Index + 1), [Index]()
			{
				StandAt(Poses::MainRoom, Poses::MainRoom + FRotator(0.f, Index * 22.5f, 0.f).Vector() * 1000.f + FVector(0.f, 0.f, 120.f));
				if (ULidarScanComponent* L = Lidar())
				{
					L->Scan();
				}
				return 0.12f;
			}));
		}
		S.Add(WaitFor(TEXT("G: the 17th scan's links"), []() { return LinksReadyFor(NewestScan()); }, 6.f, []() { Lidar()->LinkResultDelaySeconds = 0.f; return 0.4f; }));
		S.Add(Do(TEXT("G: the first scan is gone with its lines"), []()
		{
			const ULidarScanComponent* L = Lidar();
			int32 Mismatch = 0, Total = 0, HoldsFirst = 0;
			for (const FLidarScanSlotStats& Slot : L->GetScanSlotStats())
			{
				if (Slot.ScanIndex == INDEX_NONE)
				{
					continue;
				}
				HoldsFirst += Slot.ScanIndex == G.FirstOfStop ? 1 : 0;
				const int32* Links = G.LinksByScan.Find(Slot.ScanIndex);
				Total += Slot.LineParticles;
				Mismatch += (!Links || *Links != Slot.LineParticles) ? 1 : 0;
				if (!Links || *Links != Slot.LineParticles)
				{
					Note(FString::Printf(TEXT("G: slot with scan %d: %d line particles, %d links recorded"), Slot.ScanIndex, Slot.LineParticles, Links ? *Links : -1));
				}
			}
			Note(FString::Printf(TEXT("G: %d alive scans after 17, %d line particles in all, %d scans recorded"), L->GetAliveScanCount(), Total, G.LinksByScan.Num()));
			Check(L->GetAliveScanCount() == 16 && HoldsFirst == 0, FString::Printf(TEXT("G: the 17th scan took the first scan's slot (%d alive, first held %d)"), L->GetAliveScanCount(), HoldsFirst));
			Check(Mismatch == 0, FString::Printf(TEXT("G: every slot draws exactly its own scan's lines: the evicted scan's lines are gone (%d slots differ)"), Mismatch));
			Check(!G.LinksByScan.Contains(G.FirstOfStop) && L->GetDiscardedLinkResults() > G.DiscardedBefore,
				FString::Printf(TEXT("G: the first scan's result, held until after the 17th scan took its slot, was dropped and touched nothing (discarded %d -> %d, applied %d)"),
					G.DiscardedBefore, L->GetDiscardedLinkResults(), G.LinksByScan.Contains(G.FirstOfStop) ? 1 : 0));
			FTSTicker::RemoveTicker(G.Recorder);
			return 0.f;
		}));
		S.Add(Do(TEXT("G: the newest scan's samples with 16 scans alive"), []()
		{
			Note(FString::Printf(TEXT("G: %d scans alive"), AliveScans()));
			CheckSamplesOfLastScan(TEXT("G (16 scans alive)"));
			return 0.f;
		}));
	}

	/** The editor's scalability may render PIE below 100%; the width and brightness are measured at 100%, then it goes back. */
	static FString GSavedScreenPercentage;

	static void FullResolution(bool bOn)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		if (!CVar)
		{
			return;
		}
		if (bOn)
		{
			GSavedScreenPercentage = CVar->GetString();
			CVar->Set(TEXT("100"), ECVF_SetByConsole);
			Note(FString::Printf(TEXT("screen percentage %s -> 100 for the measure"), *GSavedScreenPercentage));
		}
		else if (!GSavedScreenPercentage.IsEmpty())
		{
			CVar->Set(*GSavedScreenPercentage, ECVF_SetByConsole);
			GSavedScreenPercentage.Reset();
		}
	}

	static void AddDisplay(TArray<FStep>& S)
	{
		// --- H: width at 3 m, the interactables' colour, brightness against the points ---
		// The width is measured across single lines: with more than 2 neighbours lines run side by side and meet at every point,
		// so the cross-sections would run into the next line. The width does not depend on the neighbours, so this one scan takes
		// at most 2 (the settings' other values unchanged), and the settings' values are pushed back after the measure.
		S.Add(Do(TEXT("H: at most 2 neighbours for the width"), []()
		{
			FScanLinks Values = GSavedLinks;
			Values.Neighbours = FMath::Min(GSavedLinks.Neighbours, 2);
			// Brief 23: on the grid every point has lines in two directions, so a cross-section runs into the line across it;
			// the lines are the same in both modes (only the search differs), so the width scan is taken in the nearest mode.
			Values.Mode = ELidarLinkMode::Nearest;
			PushLinks(Values);
			Note(FString::Printf(TEXT("H: width scan with %d neighbours (settings %d), nearest mode (settings %s)"), Values.Neighbours, GSavedLinks.Neighbours, ModeName(GSavedLinks.Mode)));
			return 0.f;
		}));
		AddStopAndScanAt(S, TEXT("H_3m"), Poses::CorridorFacing, Poses::CorridorWallFace);
		S.Add(Do(TEXT("H: wait for the growth, full resolution"), []() { FullResolution(true); return WaitGrowthDone(); }));
		AddViewShots(S, TEXT("H_Width_3m"), [](const FViewResult& V)
		{
			int32 Samples = 0;
			double AreaWidth = 0.0;
			const double Width = MeasureWidth(LineMask(V.HiddenLines, V.HiddenNoLines), V.HiddenLines.Width, V.HiddenLines.Height, 200.0, 400.0, Samples, AreaWidth);
			double Ratio = 0.0;
			const FString Bright = Brightness(V, Ratio);
			Note(FString::Printf(TEXT("H: at 3 m the lines are %.2f px wide (median of %d cross-sections; line pixels per pixel of projected length %.2f; set %.2f px); %s"), Width, Samples, AreaWidth, GSavedLinks.PixelWidth, *Bright));
			Check(Samples > 50 && Width >= GSavedLinks.PixelWidth - 0.75 && Width <= GSavedLinks.PixelWidth + 1.0,
				FString::Printf(TEXT("H: the width at 3 m is the set width within a pixel (%.2f px)"), Width));
			Check(Ratio > 0.0 && Ratio < 1.0, FString::Printf(TEXT("H: a line is dimmer than a point at intensity %.2f (p90 ratio %.2f)"), GSavedLinks.Intensity, Ratio));
			FullResolution(false);
			PushLinks(GSavedLinks);
		});
		AddStopAndScanAt(S, TEXT("H_Terminal"), Poses::TerminalView, Poses::Terminal);
		S.Add(Do(TEXT("H: wait for the growth"), []() { return WaitGrowthDone(); }));
		AddViewShots(S, TEXT("H_Interactable"), [](const FViewResult& V)
		{
			const TArray<uint8> Mask = LineMask(V.VisibleLines, V.VisibleNoLines);
			int32 Interactable = 0;
			for (int32 Index = 0; Index < Mask.Num(); ++Index)
			{
				Interactable += (Mask[Index] && IsInteractableHue(V.VisibleLines.Pixels[Index]) && Level(V.VisibleNoLines.Pixels[Index]) <= 6) ? 1 : 0;
			}
			TArray<FLidarLink> Links;
			TArray<float> Depths;
			TArray<int32> Degrees;
			int32 CaptureWidth = 0;
			Lidar()->GetLastScanLinks(Links, Depths, Degrees, CaptureWidth);
			int32 Channel1 = 0;
			for (const FLidarLink& Link : Links)
			{
				Channel1 += Link.StartChannel > 0 ? 1 : 0;
			}
			Note(FString::Printf(TEXT("H: line pixels in the interactables' colour (lines alone): %d; links in an interactable channel: %d"), Interactable, Channel1));
			Check(Channel1 > 0, FString::Printf(TEXT("H: the terminal has links of its own channel (%d)"), Channel1));
			Check(Interactable > 20, FString::Printf(TEXT("H: the terminal's lines have the interactables' colour (%d pixels)"), Interactable));
		});
	}

	static void StartLinksTest(bool bQuit)
	{
		StartRun(TEXT("LinksTest"), bQuit);
		MemoryOffForRun();
		G = FState();
		GRun.Cleanup = []()
		{
			CommonCleanup();
			if (G.Recorder.IsValid())
			{
				FTSTicker::RemoveTicker(G.Recorder);
			}
			if (ULidarScanComponent* L = Lidar())
			{
				L->LinkResultDelaySeconds = 0.f;
			}
			if (G.SavedFade > 0.f)
			{
				if (ULidarScanComponent* L = Lidar())
				{
					L->FadeDuration = G.SavedFade;
					L->ApplyLiveSettings();
				}
			}
			ResumeTime();
		};
		TArray<FStep>& S = GRun.Steps;
		AddValuesAndMesh(S);
		AddLineOfSight(S);
		AddSurfaces(S);
		AddGrowth(S);
		AddLife(S);
		AddDisplay(S);
		AddOff(S);
		Launch();
	}

	// =====================================================================================================
	// tls.Links.ShipCheck (Prologue_Arrival)
	// =====================================================================================================

	// Prologue_Arrival (Brief 15): the ship is the modelled 6 x 12 x 5 m ship (X 600..1200, Y -600..600), open in the
	// middle of its long side at X = 1200; the face measured is the outer face of its closed long side (X = 600, facing
	// the hatch's wall). (Brief 13 and 14: a 12 x 6 m box X 300..1500, Y -300..300, and its +Y long side.)
	namespace ShipPoses
	{
		// 4 m from the face: the scan covers the face and the floor strip at its foot, which the ship hides from every view.
		static const FVector OutsideRight(200.f, 0.f, 0.f);
		static const FVector RightWallOuter(600.f, 0.f, 250.f);
	}

	/** Screen rectangle of the closed long side's outer face (X 600, Y -600..600, Z 0..500) from the current view. */
	/**
	 * The closed long side's outer face on screen. Brief 18: the hull's vertical side, Y -360..360 (where its wedges begin)
	 * and Z 35..340 (from the skin's bottom to the chamfer's break) at X = 600; Brief 15 to 17 took the old box's whole face,
	 * Y -600..600, Z 0..500, which on the new hull also takes in the hangar's floor seen past the wedges and the chamfers.
	 */
	static FRect ShipRect(int32 Width, int32 Height)
	{
		FRect R;
		R.X0 = Width; R.Y0 = Height; R.X1 = 0; R.Y1 = 0;
		for (const float X : { 600.f }) for (const float Y : { -360.f, 360.f }) for (const float Z : { 35.f, 340.f })
		{
			FVector2D P;
			const APlayerController* C = PC();
			if (!C || !C->ProjectWorldLocationToScreen(FVector(X, Y, Z), P, false))
			{
				continue;
			}
			R.X0 = FMath::Min(R.X0, FMath::Clamp(FMath::FloorToInt32(P.X), 0, Width));
			R.Y0 = FMath::Min(R.Y0, FMath::Clamp(FMath::FloorToInt32(P.Y), 0, Height));
			R.X1 = FMath::Max(R.X1, FMath::Clamp(FMath::CeilToInt32(P.X), 0, Width));
			R.Y1 = FMath::Max(R.Y1, FMath::Clamp(FMath::CeilToInt32(P.Y), 0, Height));
		}
		return R;
	}

	static void AddShipView(TArray<FStep>& S, const FString& Label, const FVector& Floor, bool bWholeFrame)
	{
		S.Add(Do(Label + TEXT(": view"), [Label, Floor]()
		{
			StandAt(Floor, ShipPoses::RightWallOuter);
			Note(FString::Printf(TEXT("%s: eye at (%.0f, %.0f), %.1f m from the closed long side's outer face"), *Label, Floor.X, Floor.Y, FVector::Dist2D(Floor, ShipPoses::RightWallOuter) / 100.f));
			return 0.4f;
		}));
		AddViewShots(S, Label, [Label, bWholeFrame](const FViewResult& V)
		{
			const FRect Rect = bWholeFrame ? FRect() : ShipRect(V.VisibleLines.Width, V.VisibleLines.Height);
			const FViewNumbers N = Numbers(V, Rect);
			Note(FString::Printf(TEXT("%s: %s (%s)"), *Label, *Describe(N), bWholeFrame ? TEXT("whole frame") : *FString::Printf(TEXT("the outer face on screen x %d..%d y %d..%d"), Rect.X0, Rect.X1, Rect.Y0, Rect.Y1)));
			Check(N.Points.Pixels > 500, FString::Printf(TEXT("%s: the outer face's points show through the ship (%d pixels)"), *Label, N.Points.Pixels));
			Check(N.HiddenLines.Pixels > 500, FString::Printf(TEXT("%s: with the geometry hidden the outer face's lines are there (%d pixels)"), *Label, N.HiddenLines.Pixels));
			Check(N.VisibleLines.Pixels == 0, FString::Printf(TEXT("%s: no pixel of the outer face's lines shows (%d)"), *Label, N.VisibleLines.Pixels));
		});
	}

	static void StartShipCheck(bool bQuit)
	{
		StartRun(TEXT("LinksShipCheck"), bQuit);
		GRun.Cleanup = []() { CommonCleanup(); ResumeTime(); };
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 15.f));
		AddStopAndScanAt(S, TEXT("Ship: a scan of the closed long side's outer face"), ShipPoses::OutsideRight, ShipPoses::RightWallOuter);
		S.Add(Do(TEXT("Ship: the samples read back"), []() { CheckSamplesOfLastScan(TEXT("Ship")); return 0.f; }));
		S.Add(Do(TEXT("Ship: wait for the growth"), []()
		{
			const FLidarLinkStats Stats = Lidar()->GetLastLinkStats();
			Note(FString::Printf(TEXT("Ship: scan %d, %d points, %d links"), Stats.ScanIndex, Stats.Points, Stats.Links));
			return WaitGrowthDone();
		}));
		AddShipView(S, TEXT("Ship_Inside_5m"), FVector(1100.f, 250.f, 0.f), true);
		AddShipView(S, TEXT("Ship_BeyondOpenSide_20m"), FVector(2600.f, -400.f, 0.f), false);
		AddShipView(S, TEXT("Ship_HangarFar_34m"), FVector(3900.f, 700.f, 0.f), false);
		Launch();
	}

	// =====================================================================================================
	// tls.Links.ViewCheck (any map)
	// =====================================================================================================

	static void StartViewCheck(const FString& Label, double MinDepthM, double MaxDepthM)
	{
		StartRun(TEXT("LinksViewCheck"), false);
		GRun.Cleanup = []() { CommonCleanup(); };
		AddViewShots(GRun.Steps, Label, [Label, MinDepthM, MaxDepthM](const FViewResult& V)
		{
			const FViewNumbers N = Numbers(V);
			const double Share = N.HiddenLines.Pixels > 0 ? static_cast<double>(N.VisibleLines.Pixels) / N.HiddenLines.Pixels : 0.0;
			int32 Samples = 0;
			double AreaWidth = 0.0;
			const double Width = MeasureWidth(N.HiddenMask, V.HiddenLines.Width, V.HiddenLines.Height, MinDepthM * 100.0, MaxDepthM * 100.0, Samples, AreaWidth);
			double Ratio = 0.0;
			const FString Bright = Brightness(V, Ratio);
			const FLidarLinkStats Stats = Lidar() ? Lidar()->GetLastLinkStats() : FLidarLinkStats();
			Note(FString::Printf(TEXT("%s: %s; %.1f%% of the line pixels show with the geometry in place"), *Label, *Describe(N), 100.0 * Share));
			Note(FString::Printf(TEXT("%s: lines with their middle %.0f-%.0f m away: %.2f px wide (median of %d cross-sections; line pixels per pixel of projected length %.2f)"), *Label, MinDepthM, MaxDepthM, Width, Samples, AreaWidth));
			Note(FString::Printf(TEXT("%s: %s"), *Label, *Bright));
			if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
			{
				Note(FString::Printf(TEXT("%s: screen percentage %s, viewport %d x %d"), *Label, *CVar->GetString(), V.HiddenLines.Width, V.HiddenLines.Height));
			}
			Note(FString::Printf(TEXT("%s: last scan %d: %d points, %d links, %d dropped, longest %.1f cm"), *Label, Stats.ScanIndex, Stats.Points, Stats.Links, Stats.Dropped, Stats.LongestLink));
			LogDegreeBands(*FString::Printf(TEXT("%s   ..  %s: links per point"), *GRun.Name, *Label));
			Check(true, Label + TEXT(": measured"));
		});
		Launch();
	}

	// =====================================================================================================
	// tls.Links.TravelCheck: travel right after a scan, its result still on its way
	// =====================================================================================================

	struct FTravelCheck
	{
		int32 Phase = 0;
		double T0 = 0.0;
		TWeakObjectPtr<UWorld> OldWorld;
		TWeakObjectPtr<ULidarScanComponent> OldLidar;
		int32 HeldScan = INDEX_NONE;
		int32 Passed = 0;
		int32 Failed = 0;
		bool bQuit = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FTravelCheck GTravel;

	static void TravelCheck(bool bOk, const FString& What)
	{
		(bOk ? GTravel.Passed : GTravel.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("LinksTravelCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static bool TickTravelCheck(float)
	{
		FTravelCheck& T = GTravel;
		const double Now = Real();
		UWorld* World = W();
		ULidarScanComponent* L = Lidar();
		auto Finish = [&T]()
		{
			UE_LOG(LogTheLastShift, Log, TEXT("LinksTravelCheck summary (%s): %d checks passed, %d failed."), T.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), T.Passed, T.Failed);
			if (T.bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("tls.Links.TravelCheck"));
			}
			return false;
		};
		switch (T.Phase)
		{
		case 0:
			if (World && L && Pawn() && TS())
			{
				KeepEntityDormant();
				StopTime();
				T.T0 = Now;
				T.Phase = 1;
			}
			else if (Now - T.T0 > 15.0)
			{
				TravelCheck(false, TEXT("a game world with the player's lidar"));
				return Finish();
			}
			return true;
		case 1:
			if (L && AliveScans() >= 1 && LinksReadyFor(NewestScan()))
			{
				// A scan whose result is held 2 s, then travel in the same frame.
				L->LinkResultDelaySeconds = 2.f;
				L->Scan();
				T.HeldScan = NewestScan();
				T.OldWorld = World;
				T.OldLidar = L;
				const FString Map = UWorld::RemovePIEPrefix(World->GetMapName());
				UE_LOG(LogTheLastShift, Log, TEXT("LinksTravelCheck   ..  scan %d taken, its result held 2 s; travel to %s now"), T.HeldScan, *Map);
				PC()->ConsoleCommand(FString::Printf(TEXT("open %s"), *Map));
				T.T0 = Now;
				T.Phase = 2;
			}
			else if (Now - T.T0 > 6.0)
			{
				TravelCheck(false, TEXT("the first scan and its links"));
				return Finish();
			}
			return true;
		case 2:
			if (World && World != T.OldWorld.Get() && L && L != T.OldLidar.Get())
			{
				T.T0 = Now;
				T.Phase = 3;
			}
			else if (Now - T.T0 > 30.0)
			{
				TravelCheck(false, TEXT("the map is loaded again"));
				return Finish();
			}
			return true;
		default:
			if (Now - T.T0 < 3.0)
			{
				return true;
			}
			TravelCheck(!T.OldLidar.IsValid(), TEXT("the old world's lidar is gone"));
			TravelCheck(L && L->GetLastLinkStats().ScanIndex == INDEX_NONE && L->GetPendingLinkSearches() == 0 && L->GetDiscardedLinkResults() == 0,
				FString::Printf(TEXT("3 s after the travel, the result of scan %d reached nothing in the new world (last searched scan %d, %d pending)"),
					T.HeldScan, L ? L->GetLastLinkStats().ScanIndex : -2, L ? L->GetPendingLinkSearches() : -1));
			return Finish();
		}
	}

	// =====================================================================================================
	// tls.Links.ScanFrame: the frames around a scan, lines off and on
	// =====================================================================================================

	struct FScanFrameRun
	{
		int32 Rounds = 8;
		int32 Round = 0;
		bool bLinesOn = false;
		int32 FrameInRound = -1;
		static constexpr int32 FramesPerScan = 12;
		TArray<double> Worst[2], First[2], WorstGame[2], Idle;
		double RoundMaxMs = 0.0, RoundMaxGame = 0.0;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FScanFrameRun GFrames;

	static bool TickScanFrame(float)
	{
		ULidarScanComponent* L = Lidar();
		if (!L)
		{
			return false;
		}
		const double Ms = FApp::GetDeltaTime() * 1000.0;
		const double GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
		FScanFrameRun& R = GFrames;
		const int32 Kind = R.bLinesOn ? 1 : 0;
		if (R.FrameInRound >= 0)
		{
			// Frame 0 is the one that issued the scan; FApp's delta of the next tick is that frame's time.
			if (R.FrameInRound == 1)
			{
				R.First[Kind].Add(Ms);
			}
			if (R.FrameInRound >= 1)
			{
				R.RoundMaxMs = FMath::Max(R.RoundMaxMs, Ms);
				R.RoundMaxGame = FMath::Max(R.RoundMaxGame, GameMs);
			}
			if (++R.FrameInRound > FScanFrameRun::FramesPerScan)
			{
				R.Worst[Kind].Add(R.RoundMaxMs);
				R.WorstGame[Kind].Add(R.RoundMaxGame);
				R.FrameInRound = -1;
				R.bLinesOn = !R.bLinesOn;
				R.Round += R.bLinesOn ? 0 : 1;
			}
			return true;
		}
		R.Idle.Add(Ms);
		if (R.Round >= R.Rounds)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("ScanFrame: ===== %d scans each, %s: idle frame %.2f ms. Frame of the scan: lines off %.2f ms, on %.2f ms. Worst of the %d frames from the scan: off %.2f ms (game thread %.2f), on %.2f ms (game thread %.2f). Medians. ====="),
				R.Rounds, *FString::Printf(TEXT("%d alive scans"), L->GetAliveScanCount()), Percentile(R.Idle, 0.5), Percentile(R.First[0], 0.5), Percentile(R.First[1], 0.5),
				FScanFrameRun::FramesPerScan, Percentile(R.Worst[0], 0.5), Percentile(R.WorstGame[0], 0.5), Percentile(R.Worst[1], 0.5), Percentile(R.WorstGame[1], 0.5));
			RestoreLinks();
			return false;
		}
		if (R.Idle.Num() % 20 == 0)
		{
			FScanLinks Values = GSavedLinks;
			Values.Neighbours = R.bLinesOn ? FMath::Max(GSavedLinks.Neighbours, 2) : 0;
			PushLinks(Values);
			KeepEntityDormant();
			L->Scan();
			R.FrameInRound = 0;
			R.RoundMaxMs = 0.0;
			R.RoundMaxGame = 0.0;
		}
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksTestCommand(
	TEXT("tls.LinksTest"),
	TEXT("tls.LinksTest [Quit=0]: (fresh PIE, in TestLidar) neighbour lines: the values pushed from the settings, the mesh of a scan (links, most per point, longest, "
		"none across scans, channels or injected points), the cap, line of sight through a wall and past a door's edge, lines on their own surface at 3 m, 10 m and grazing, "
		"growth over the link time and behind the pause menu, the fade, Resume and the 17th scan, width, the interactables' colour, brightness. Shots to Saved/Screenshots/Brief13."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartLinksTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksShipCheckCommand(
	TEXT("tls.Links.ShipCheck"),
	TEXT("tls.Links.ShipCheck [Quit=0]: (PIE or a package, in Prologue_Arrival) a scan of the ship's closed long side from 4 m, then views of that outer face through the ship: from inside it (5 m), from 20 m beyond its open side and from across the hangar (about 34 m): its points show, its lines show no pixel. Shots to Saved/Screenshots/Brief13."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartShipCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksTravelCheckCommand(
	TEXT("tls.Links.TravelCheck"),
	TEXT("tls.Links.TravelCheck [Quit=0]: (PIE or a package, any map with the player's lidar) stops time, then takes a scan whose search result is held 2 s and travels to the same map in that frame; in the new world the result must reach nothing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksDebug;
		if (GTravel.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GTravel.Ticker);
		}
		GTravel = FTravelCheck();
		GTravel.bQuit = Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0;
		GTravel.T0 = Real();
		GTravel.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickTravelCheck));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksViewCheckCommand(
	TEXT("tls.Links.ViewCheck"),
	TEXT("tls.Links.ViewCheck <Label> [MinDepthM=0] [MaxDepthM=1000]: (PIE or a package, any map, scans alive) the current view with and without lines, geometry visible and hidden: line pixels in view against hidden, the width of the lines whose middle lies in the depth band, brightness against the points, links per point by depth. Shots to Saved/Screenshots/Brief13."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartViewCheck(Args.Num() > 0 ? Args[0] : FString(TEXT("View")), Args.Num() > 1 ? FCString::Atod(*Args[1]) : 0.0,
			Args.Num() > 2 ? FCString::Atod(*Args[2]) : 1000.0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksSetCommand(
	TEXT("tls.Links.Set"),
	TEXT("tls.Links.Set <Neighbours> [MaxDistanceCm] [PixelWidth] [Intensity] [MaxNormalAngle] [MaxLinksPerScan]: sets ScanLinks in the settings for this session (not saved to the ini) and has the director push it. Neighbours, distance, angle and cap apply from the next scan."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		FScanLinks& V = GetMutableDefault<UTheLastShiftSettings>()->ScanLinks;
		if (Args.Num() > 0) { V.Neighbours = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 8); }
		if (Args.Num() > 1) { V.MaxDistanceCm = FMath::Clamp(FCString::Atof(*Args[1]), 1.f, 1000.f); }
		if (Args.Num() > 2) { V.PixelWidth = FMath::Clamp(FCString::Atof(*Args[2]), 0.5f, 8.f); }
		if (Args.Num() > 3) { V.Intensity = FMath::Clamp(FCString::Atof(*Args[3]), 0.f, 4.f); }
		if (Args.Num() > 4) { V.MaxNormalAngle = FMath::Clamp(FCString::Atof(*Args[4]), 0.f, 180.f); }
		if (Args.Num() > 5) { V.MaxLinksPerScan = FMath::Clamp(FCString::Atoi(*Args[5]), 0, 262144); }
		if (UScanDirectorComponent* D = LinksDebug::Director())
		{
			D->ApplyScanLinks();
		}
		const ULidarScanComponent* L = LinksDebug::Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Set: %d neighbours, %.1f cm, %.2f px, intensity %.2f, angle %.1f deg, cap %d, mode %s (grid's distance %.1f cm); lidar %d / %.1f / %.2f / %.2f / %.1f / %d / %s."),
			V.Neighbours, V.MaxDistanceCm, V.PixelWidth, V.Intensity, V.MaxNormalAngle, V.MaxLinksPerScan, LinksDebug::ModeName(V.Mode), V.GridMaxDistanceCm,
			L ? L->LinkNeighbours : -1, L ? L->LinkMaxDistance : -1.f, L ? L->LinePixelWidth : -1.f, L ? L->LineIntensity : -1.f, L ? L->LinkMaxNormalAngle : -1.f, L ? L->MaxLinksPerScan : -1,
			L ? LinksDebug::ModeName(L->LinkMode) : TEXT("-"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksReportCommand(
	TEXT("tls.Links.Report"),
	TEXT("tls.Links.Report: logs the last searched scan's links (points, links, dropped, most per point, longest, search time), the share of points with 0, 1, 2 and more links by depth band, and the line particles of every slot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		const ULidarScanComponent* L = LinksDebug::Lidar();
		if (!L)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.Report: no player lidar."));
			return;
		}
		const FLidarLinkStats S = L->GetLastLinkStats();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Report: scan %d (%s): %d points, %d links, %d dropped, at most %d per point, longest %.1f cm, search %.2f ms, ready %.3f s after the scan; %d searches pending."),
			S.ScanIndex, LinksDebug::ModeName(S.Mode), S.Points, S.Links, S.Dropped, S.MaxDegree, S.LongestLink, S.SearchMilliseconds, S.ReadySeconds, L->GetPendingLinkSearches());
		LinksDebug::LogDegreeBands(TEXT("tls.Links.Report:"));
		LinksDebug::LogDroppedRows(TEXT("tls.Links.Report:"));
		FString Slots;
		for (const FLidarScanSlotStats& Slot : L->GetScanSlotStats())
		{
			if (Slot.ScanIndex != INDEX_NONE)
			{
				Slots += FString::Printf(TEXT(" [scan %d: %d points, %d lines]"), Slot.ScanIndex, Slot.CapturedParticles, Slot.LineParticles);
			}
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Report: slots%s"), *Slots);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksHalfDepthCommand(
	TEXT("tls.Links.HalfDepth"),
	TEXT("tls.Links.HalfDepth [BinM=2]: for the last searched scan, the share of points with no link by depth along the capture axis, in bins of BinM metres, for points on the floor (within 10 cm of the pawn's floor) and for the others (walls), and the first depth at which half of them have no link."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksDebug;
		const ULidarScanComponent* L = Lidar();
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		TArray<FLidarLink> Links;
		TArray<float> Depths;
		TArray<int32> Degrees;
		if (!L || !Character || !L->GetLastScanSamples(Positions, Channels, Width, Context) || !L->GetLastScanLinks(Links, Depths, Degrees, Width))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.HalfDepth: no searched scan or no pawn."));
			return;
		}
		const double Bin = Args.Num() > 0 ? FMath::Clamp(FCString::Atod(*Args[0]), 0.5, 50.0) * 100.0 : 200.0;
		const double FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		TMap<int32, FIntPoint> Floor, Wall; // bin -> (points, points with no link)
		for (int32 Texel = 0; Texel < Positions.Num() && Texel < Degrees.Num(); ++Texel)
		{
			if (Channels[Texel] < 0)
			{
				continue;
			}
			const int32 Key = FMath::FloorToInt32(Depths[Texel] / Bin);
			FIntPoint& Cell = (FMath::Abs(Positions[Texel].Z - FloorZ) < 10.0 ? Floor : Wall).FindOrAdd(Key);
			Cell.X += 1;
			Cell.Y += Degrees[Texel] == 0 ? 1 : 0;
		}
		for (TPair<const TCHAR*, TMap<int32, FIntPoint>*> Kind : { TPair<const TCHAR*, TMap<int32, FIntPoint>*>(TEXT("floor"), &Floor), TPair<const TCHAR*, TMap<int32, FIntPoint>*>(TEXT("walls"), &Wall) })
		{
			TArray<int32> Keys;
			Kind.Value->GetKeys(Keys);
			Keys.Sort();
			FString Row;
			double Half = -1.0;
			for (const int32 Key : Keys)
			{
				const FIntPoint Cell = (*Kind.Value)[Key];
				const double Share = Cell.X > 0 ? double(Cell.Y) / Cell.X : 0.0;
				Row += FString::Printf(TEXT(" [%.0f-%.0f m: %d, %.0f%%]"), Key * Bin / 100.0, (Key + 1) * Bin / 100.0, Cell.X, 100.0 * Share);
				if (Half < 0.0 && Cell.X >= 30 && Share >= 0.5)
				{
					Half = Key * Bin / 100.0;
				}
			}
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.HalfDepth: scan %d, %s: half the points have no link from %s;%s"), Context.ScanIndex, Kind.Key,
				Half >= 0.0 ? *FString::Printf(TEXT("%.0f m"), Half) : TEXT("nowhere in range"), *Row);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksScanFrameCommand(
	TEXT("tls.Links.ScanFrame"),
	TEXT("tls.Links.ScanFrame [Rounds=8]: while Stopped, alternates scans with lines off and on (the settings' values, at least 2 neighbours), one every 20 idle frames, and logs the median time of the scan's own frame and of the worst of the 12 frames that follow (readback and search included), off against on."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksDebug;
		if (GFrames.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GFrames.Ticker);
		}
		SaveLinks();
		GFrames = FScanFrameRun();
		GFrames.Rounds = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 100) : 8;
		GFrames.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickScanFrame));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsExposureReportCommand(
	TEXT("tls.Exposure.Report"),
	TEXT("tls.Exposure.Report: logs whether local exposure can act on the game view: r.LocalExposure, the project defaults, the game viewport's show flag, and every post-process volume or camera of the world that overrides a local exposure setting."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		auto CVar = [](const TCHAR* Name) { const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name); return V ? V->GetString() : FString(TEXT("?")); };
		const bool bFlag = GEngine && GEngine->GameViewport && GEngine->GameViewport->GetEngineShowFlags() ? GEngine->GameViewport->GetEngineShowFlags()->LocalExposure != 0 : false;
		int32 Volumes = 0, Overriding = 0;
		for (TActorIterator<APostProcessVolume> It(World); World && It; ++It)
		{
			++Volumes;
			const FPostProcessSettings& S = It->Settings;
			Overriding += (S.bOverride_LocalExposureHighlightContrastScale || S.bOverride_LocalExposureShadowContrastScale || S.bOverride_LocalExposureDetailStrength
				|| S.bOverride_LocalExposureHighlightContrastCurve || S.bOverride_LocalExposureShadowContrastCurve) ? 1 : 0;
		}
		int32 Cameras = 0, CameraOverriding = 0;
		for (TObjectIterator<UCameraComponent> It; It; ++It)
		{
			if (It->GetWorld() != World)
			{
				continue;
			}
			++Cameras;
			const FPostProcessSettings& S = It->PostProcessSettings;
			CameraOverriding += (It->PostProcessBlendWeight > 0.f && (S.bOverride_LocalExposureHighlightContrastScale || S.bOverride_LocalExposureShadowContrastScale
				|| S.bOverride_LocalExposureDetailStrength)) ? 1 : 0;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Exposure.Report: r.LocalExposure %s (0 = the engine makes every view's local exposure neutral), HighlightContrastScale default %s, ShadowContrastScale default %s, game viewport show flag LocalExposure %d; %d post-process volumes (%d override local exposure), %d cameras (%d override local exposure)."),
			*CVar(TEXT("r.LocalExposure")), *CVar(TEXT("r.DefaultFeature.LocalExposure.HighlightContrastScale")), *CVar(TEXT("r.DefaultFeature.LocalExposure.ShadowContrastScale")),
			bFlag ? 1 : 0, Volumes, Overriding, Cameras, CameraOverriding);
	}));

namespace LinksDebug
{
	// =====================================================================================================
	// Developer's check (Brief 13): tls.Links.EndsCheck and tls.Links.DistanceCheck
	// =====================================================================================================

	/** Middle of the frame: the surface a check aims at. */
	static FRect CenterRect(const FShot& Shot, double Half)
	{
		FRect R;
		R.X0 = FMath::FloorToInt32(Shot.Width * (0.5 - Half));
		R.X1 = FMath::CeilToInt32(Shot.Width * (0.5 + Half));
		R.Y0 = FMath::FloorToInt32(Shot.Height * (0.5 - Half));
		R.Y1 = FMath::CeilToInt32(Shot.Height * (0.5 + Half));
		return R;
	}

	/** The hall view of beacon 0 (area 2): eye, forward, the wall it faces and the floor under it. */
	struct FHall
	{
		FVector Eye = FVector::ZeroVector;
		FVector Floor = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector WallPoint = FVector::ZeroVector;
		double FloorZ = 0.0;
		bool bValid = false;
	};
	static FHall GHall;

	static void FindHall()
	{
		GHall = FHall();
		APawn* P = Pawn();
		APlayerController* C = PC();
		UWorld* World = W();
		if (!P || !C || !World)
		{
			return;
		}
		GHall.Eye = EyeOf(P);
		GHall.Forward = C->GetControlRotation().Vector().GetSafeNormal2D();
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("LinksHall"), false, P);
		if (World->LineTraceSingleByChannel(Hit, GHall.Eye, GHall.Eye + GHall.Forward * 30000.0, ECC_Visibility, Params))
		{
			GHall.WallPoint = Hit.ImpactPoint;
		}
		if (World->LineTraceSingleByChannel(Hit, GHall.Eye, GHall.Eye - FVector(0, 0, 1000), ECC_Visibility, Params))
		{
			GHall.FloorZ = Hit.ImpactPoint.Z;
		}
		GHall.Floor = FVector(GHall.Eye.X, GHall.Eye.Y, GHall.FloorZ);
		GHall.bValid = !GHall.WallPoint.IsZero();
		Note(FString::Printf(TEXT("hall: eye (%.0f, %.0f, %.0f), forward (%.2f, %.2f), wall %.1f m ahead, floor Z %.0f"),
			GHall.Eye.X, GHall.Eye.Y, GHall.Eye.Z, GHall.Forward.X, GHall.Forward.Y, FVector::Dist2D(GHall.Eye, GHall.WallPoint) / 100.0, GHall.FloorZ));
	}

	/** Adds: Resume, stand at Floor() looking at LookAt(), Stop, the automatic scan and, when bLinks, its links grown. */
	static void AddScanFrom(TArray<FStep>& S, const FString& Label, TFunction<FVector()> Floor, TFunction<FVector()> LookAt, bool bLinks)
	{
		S.Add(Do(Label + TEXT(": Resume, pose"), [Floor, LookAt]()
		{
			ResumeTime();
			KeepEntityDormant();
			StandAt(Floor(), LookAt());
			return 0.7f;
		}));
		S.Add(Do(Label + TEXT(": Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f, []() { return 0.2f; }));
		if (bLinks)
		{
			S.Add(WaitFor(Label + TEXT(": its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return WaitGrowthDone(); }));
		}
	}

	// --- tls.Links.EndsCheck ---

	static void StartEndsCheck(float Lift, bool bQuit)
	{
		StartRun(TEXT("LinksEndsCheck"), bQuit);
		GRun.Cleanup = []() { CommonCleanup(); ResumeTime(); };
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 30.f, []()
		{
			FullResolution(true);
			if (APlayerController* C = PC())
			{
				C->ConsoleCommand(TEXT("tls.Beacon.View 0 middle"));
			}
			return 0.8f;
		}));
		S.Add(Do(TEXT("the hall"), []() { FindHall(); return GHall.bValid ? 0.f : -1.f; }));
		for (const int32 Neighbours : { 2, 4 })
		{
			for (const float L : { 0.f, Lift })
			{
				for (const bool bWall : { true, false })
				{
					for (const double D : { 1000.0, 2000.0, 3000.0 })
					{
						const FString Label = FString::Printf(TEXT("Ends_%s_%.0fm_N%d_Lift%03d"), bWall ? TEXT("Wall") : TEXT("Floor"), D / 100.0, Neighbours, FMath::RoundToInt32(L * 100.f));
						S.Add(Do(Label + TEXT(": values"), [Neighbours, L]()
						{
							FScanLinks Values = GSavedLinks;
							Values.Neighbours = Neighbours;
							Values.LiftTexels = L;
							PushLinks(Values);
							return 0.f;
						}));
						// Wall: stand D in front of the hall's end wall, facing it. Floor: stand at the hall view, look at the floor D ahead.
						AddScanFrom(S, Label,
							[bWall, D]() { return bWall ? FVector(GHall.WallPoint.X, GHall.WallPoint.Y, GHall.FloorZ) - GHall.Forward * D : GHall.Floor; },
							[bWall, D]() { return bWall ? GHall.WallPoint : GHall.Floor + GHall.Forward * D; }, true);
						AddViewShots(S, Label, [Label, bWall, D, Neighbours, L](const FViewResult& V)
						{
							const FViewNumbers N = Numbers(V, CenterRect(V.VisibleLines, 0.1));
							const double Share = N.HiddenLines.Pixels > 0 ? 100.0 * N.VisibleLines.Pixels / N.HiddenLines.Pixels : 0.0;
							const ULidarScanComponent* Lid = Lidar();
							const FLidarLinkStats Stats = Lid ? Lid->GetLastLinkStats() : FLidarLinkStats();
							Note(FString::Printf(TEXT("%s: %s %.0f m, %d neighbours, lift %.2f texel: middle of the frame %d line pixels in view / %d with the geometry hidden (%.1f%%), %d point pixels; scan %d, %d links"),
								*Label, bWall ? TEXT("wall") : TEXT("floor"), D / 100.0, Neighbours, L, N.VisibleLines.Pixels, N.HiddenLines.Pixels, Share, N.Points.Pixels, Stats.ScanIndex, Stats.Links));
						});
					}
				}
			}
		}
		Launch();
	}

	// --- tls.Links.DistanceCheck ---

	struct FLevels
	{
		int32 Points = 0, Lines = 0, RectPixels = 0;
		double PointP50 = 0, PointP90 = 0, PointP99 = 0, LineP50 = 0, LineP90 = 0, LineP99 = 0;
		/** Share of the rectangle's pixels at 250 levels or above: points alone (lines off) and the game's look (lines on). */
		double Share250Points = 0, Share250Lines = 0;
		/** Brief 25: every lit pixel of the game's look (lines on, points and lines together): median and 99th percentile. */
		int32 Lit = 0;
		double LitP50 = 0, LitP99 = 0;
	};

	/** On-screen levels in Rect: points (lines off) and lines alone (lines on, where no point is lit). */
	static FLevels LevelsIn(const FViewResult& V, const FRect& Rect)
	{
		FLevels R;
		TArray<double> P, Ln, All;
		int32 Bright = 0, BrightLines = 0;
		const int32 Width = V.VisibleLines.Width;
		for (int32 Index = 0; Index < V.VisibleNoLines.Pixels.Num() && Index < V.VisibleLines.Pixels.Num(); ++Index)
		{
			if (!Rect.Contains(Index % Width, Index / Width))
			{
				continue;
			}
			++R.RectPixels;
			const int32 Off = Level(V.VisibleNoLines.Pixels[Index]);
			const int32 On = Level(V.VisibleLines.Pixels[Index]);
			Bright += Off >= 250 ? 1 : 0;
			BrightLines += On >= 250 ? 1 : 0;
			if (On > 6)
			{
				All.Add(On);
			}
			if (Off > 6)
			{
				P.Add(Off);
			}
			else if (On > Off + 3)
			{
				Ln.Add(On);
			}
		}
		R.Points = P.Num();
		R.Lines = Ln.Num();
		R.PointP50 = Percentile(P, 0.5);
		R.PointP90 = Percentile(P, 0.9);
		R.PointP99 = Percentile(P, 0.99);
		R.LineP50 = Percentile(Ln, 0.5);
		R.LineP90 = Percentile(Ln, 0.9);
		R.LineP99 = Percentile(Ln, 0.99);
		R.Share250Points = R.RectPixels > 0 ? 100.0 * Bright / R.RectPixels : 0.0;
		R.Share250Lines = R.RectPixels > 0 ? 100.0 * BrightLines / R.RectPixels : 0.0;
		R.Lit = All.Num();
		R.LitP50 = Percentile(All, 0.5);
		R.LitP99 = Percentile(All, 0.99);
		return R;
	}

	/**
	 * ViewPixelScale of the current view (2 tan(HFOV / 2) / viewport width: the player's camera with the horizontal FOV
	 * constraint), the same value the lidar pushes to its point system, computed here from the camera.
	 */
	static double ViewPixelScaleNow(int32 ViewWidth)
	{
		const APlayerController* C = PC();
		const double Fov = C && C->PlayerCameraManager ? C->PlayerCameraManager->GetFOVAngle() : 90.0;
		return 2.0 * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5)) / FMath::Max(ViewWidth, 1);
	}

	/**
	 * The depth factor of one point, by the rule before Brief 13b (clamp((SampleDepth / ViewDepth)^DensityCompensation,
	 * DensityBoostMin, DensityBoostMax)) and by the rule after it (clamp((spacing / PointPixelSize)^DensityCompensation,
	 * DensityBoostMin, 1)), and the lines' factor after it (min(1, spacing / LinePixelWidth); before, the lines took the
	 * points' old factor). Spacing: the on-screen spacing between the scan's samples, SampleDepth * 2 TanHalfFov.x /
	 * CaptureWidth, times max(1, NearThinningDistance / SampleDepth) for captured points, over ViewPixelScale * ViewDepth.
	 * Angle term not included.
	 */
	struct FFactor
	{
		double Old = 1.0, New = 1.0, Line = 1.0, SpacingPx = 0.0;
	};
	static FFactor DepthFactor(const ULidarScanComponent* L, double SampleDepth, double ViewDepthHere, double TanX, int32 CaptureWidth, double ViewScale, bool bCaptured)
	{
		FFactor F;
		const double N = L->DensityCompensation;
		F.Old = FMath::Clamp(FMath::Pow(SampleDepth / ViewDepthHere, N), static_cast<double>(L->DensityBoostMin), static_cast<double>(L->DensityBoostMax));
		const double Thin = bCaptured && L->NearThinningDistance > 0.f ? FMath::Max(1.0, L->NearThinningDistance / SampleDepth) : 1.0;
		F.SpacingPx = SampleDepth * 2.0 * TanX / FMath::Max(CaptureWidth, 1) * Thin / FMath::Max(ViewScale * ViewDepthHere, 1.e-6);
		F.New = FMath::Clamp(FMath::Pow(F.SpacingPx / FMath::Max(static_cast<double>(L->PointPixelSize), 0.01), N), static_cast<double>(L->DensityBoostMin), 1.0);
		F.Line = FMath::Min(1.0, F.SpacingPx / FMath::Max(static_cast<double>(L->LinePixelWidth), 0.01));
		return F;
	}

	/** The points' density compensation in Rect, from the newest scan's samples: medians of the depths, the spacing and the factors by both rules. */
	static FString CompensationIn(const FRect& Rect, int32 Width, int32 Height)
	{
		const ULidarScanComponent* L = Lidar();
		const APlayerController* C = PC();
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 CaptureWidth = 0;
		FLidarScanContext Context;
		if (!L || !C || !C->PlayerCameraManager || !L->GetLastScanSamples(Positions, Channels, CaptureWidth, Context))
		{
			return TEXT("no samples");
		}
		const FVector ViewOrigin = C->PlayerCameraManager->GetCameraLocation();
		const FVector ViewForward = C->PlayerCameraManager->GetCameraRotation().Vector();
		const double ViewScale = ViewPixelScaleNow(Width);
		TArray<double> Old, New, Line, Spacing, SampleDepths, ViewDepths;
		for (int32 Texel = 0; Texel < Positions.Num() && Texel < Channels.Num(); ++Texel)
		{
			FVector2D Screen;
			if (Channels[Texel] < 0 || !Project(Positions[Texel], Screen, Width, Height) || !Rect.Contains(FMath::FloorToInt32(Screen.X), FMath::FloorToInt32(Screen.Y)))
			{
				continue;
			}
			const double SampleDepth = FMath::Max(FVector::DotProduct(Positions[Texel] - Context.CaptureOrigin, Context.CaptureForward), 1.0);
			const double ViewDepthHere = FMath::Max(FVector::DotProduct(Positions[Texel] - ViewOrigin, ViewForward), 1.0);
			const FFactor F = DepthFactor(L, SampleDepth, ViewDepthHere, Context.TanHalfFOV.X, CaptureWidth, ViewScale, true);
			Old.Add(F.Old);
			New.Add(F.New);
			Line.Add(F.Line);
			Spacing.Add(F.SpacingPx);
			SampleDepths.Add(SampleDepth);
			ViewDepths.Add(ViewDepthHere);
		}
		return FString::Printf(TEXT("%d samples in the middle: sample depth %.0f cm, view depth %.0f cm, spacing %.2f px; points' depth factor %.4f by the old rule, %.4f by the new; lines' %.4f by the old rule, %.4f by the new (medians; angle term not included)"),
			Old.Num(), Percentile(SampleDepths, 0.5), Percentile(ViewDepths, 0.5), Percentile(Spacing, 0.5), Percentile(Old, 0.5), Percentile(New, 0.5), Percentile(Old, 0.5), Percentile(Line, 0.5));
	}

	static void AddDistanceViews(TArray<FStep>& S, const FString& Prefix, TFunction<FVector(double)> Floor, TFunction<FVector()> LookAt, const TArray<double>& Distances,
		TFunction<void(double, const FLevels&)> OnLevels = nullptr)
	{
		for (const double D : Distances)
		{
			const FString Label = FString::Printf(TEXT("%s_View%04.1fm"), *Prefix, D / 100.0);
			S.Add(Do(Label + TEXT(": view"), [Floor, LookAt, D]() { StandAt(Floor(D), LookAt()); return 0.6f; }));
			AddViewShots(S, Label, [Label, D, OnLevels](const FViewResult& V)
			{
				const FRect Rect = CenterRect(V.VisibleLines, 0.1);
				const FLevels Lv = LevelsIn(V, Rect);
				Note(FString::Printf(TEXT("%s: points %d pixels, median %.0f, p90 %.0f, p99 %.0f levels; lines alone %d pixels, median %.0f, p90 %.0f, p99 %.0f levels; at 250 or above %.2f%% of the %d pixels (points alone), %.2f%% (lines on); with lines %d lit pixels, median %.0f, p99 %.0f levels; %s"),
					*Label, Lv.Points, Lv.PointP50, Lv.PointP90, Lv.PointP99, Lv.Lines, Lv.LineP50, Lv.LineP90, Lv.LineP99, Lv.Share250Points, Lv.RectPixels, Lv.Share250Lines,
					Lv.Lit, Lv.LitP50, Lv.LitP99, *CompensationIn(Rect, V.VisibleLines.Width, V.VisibleLines.Height)));
				if (OnLevels)
				{
					OnLevels(D, Lv);
				}
			});
		}
	}

	/** Two shots of the alive scans from the current view, geometry visible (age fade held): lines on, then off. */
	static void AddPairShots(TArray<FStep>& S, const FString& Label, TFunction<void(const FViewResult&)> Then = nullptr)
	{
		S.Add(Do(Label + TEXT(": hold the age fade, lines on"), []() { HoldAgeFade(true); ShowLines(true); return 0.3f; }));
		AddShot(S, Label + TEXT("_Lines"), [](const FShot& Shot) { GView.VisibleLines = Shot; });
		S.Add(Do(Label + TEXT(": lines off"), []() { ShowLines(false); return 0.2f; }));
		AddShot(S, Label + TEXT("_NoLines"), [Label, Then](const FShot& Shot)
		{
			GView.VisibleNoLines = Shot;
			HoldAgeFade(false);
			ShowLines(true);
			Note(FString::Printf(TEXT("%s: %d lit pixels with lines, %d without"), *Label, CountLit(GView.VisibleLines).Pixels, CountLit(GView.VisibleNoLines).Pixels));
			if (Then)
			{
				Then(GView);
			}
		});
	}

	// --- Overlap: one wall covered by many scans taken from nearby poses ---

	static FVector HallFront(double D) { return FVector(GHall.WallPoint.X, GHall.WallPoint.Y, GHall.FloorZ) - GHall.Forward * D; }

	/** Scan I of an overlap run: up to 60 cm to either side of the pose 2 m in front of the wall and up to 30 cm nearer or farther. */
	static FVector OverlapPose(int32 I)
	{
		const FVector Right(-GHall.Forward.Y, GHall.Forward.X, 0.0);
		return HallFront(200.0 + ((I / 4) % 4 - 1.5) * 20.0) + Right * ((I % 4) - 1.5) * 40.0;
	}

	/** Brief 25: the share at 250 levels or above (lines on) of each cell of an overlap or steps run, by run and view distance. */
	static TMap<FString, double> GShare;
	static FString ShareKey(const FString& Run, double D) { return FString::Printf(TEXT("%s@%.0f"), *Run, D); }
	static bool MemoryOn() { return UTheLastShiftSettings::GetScanMemory().Ratio > 0.f; }

	/** Brief 25: the share of a cell against one scan's in the same run: at most +0.5 percentage points, checked with the memory on. */
	static void CompareShare(const FString& Run, const FString& Reference, double D, const FLevels& Lv)
	{
		GShare.Add(ShareKey(Run, D), Lv.Share250Lines);
		const double* One = GShare.Find(ShareKey(Reference, D));
		if (!One || Run == Reference)
		{
			return;
		}
		const FString What = FString::Printf(TEXT("%s from %.0f m: %.2f%% at 250 levels or above (lines on) against %.2f%% for one scan (%s; at most +0.5 points)"), *Run, D / 100.0,
			Lv.Share250Lines, *One, *Reference);
		if (MemoryOn())
		{
			Check(Lv.Share250Lines <= *One + 0.5, What);
		}
		else
		{
			Note(What + TEXT(", memory off: not checked"));
		}
	}

	/** Points of the samples read back for the last searched scan that lie on the hall's end wall (within 15 cm of its plane). */
	static int32 WallPointsOfLastScan()
	{
		const ULidarScanComponent* L = Lidar();
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		if (!L || !L->GetLastScanSamples(Positions, Channels, Width, Context))
		{
			return 0;
		}
		int32 Count = 0;
		for (int32 I = 0; I < Positions.Num() && I < Channels.Num(); ++I)
		{
			Count += (Channels[I] >= 0 && FMath::Abs(FVector::DotProduct(Positions[I] - GHall.WallPoint, GHall.Forward)) < 15.0) ? 1 : 0;
		}
		return Count;
	}

	/** Captured points (and line particles) alive in each alive scan, oldest first; OutOlderSum: the points of all but Newest. */
	static FString PointsPerScan(int32& OutOlderSum, int32 Newest)
	{
		OutOlderSum = 0;
		const ULidarScanComponent* L = Lidar();
		TArray<FLidarScanSlotStats> Stats = L ? L->GetScanSlotStats() : TArray<FLidarScanSlotStats>();
		Stats.Sort([](const FLidarScanSlotStats& A, const FLidarScanSlotStats& B) { return A.ScanIndex < B.ScanIndex; });
		FString Out;
		for (const FLidarScanSlotStats& Slot : Stats)
		{
			if (Slot.ScanIndex == INDEX_NONE)
			{
				continue;
			}
			Out += FString::Printf(TEXT(" %d: %d (%d lines)"), Slot.ScanIndex, Slot.CapturedParticles, Slot.LineParticles);
			OutOlderSum += Slot.ScanIndex != Newest ? Slot.CapturedParticles : 0;
		}
		return Out;
	}

	static void AddOverlap(TArray<FStep>& S, int32 Scans)
	{
		const FString Prefix = FString::Printf(TEXT("Dist_Overlap%02d"), Scans);
		AddScanFrom(S, Prefix + TEXT("_Scan00"), []() { return OverlapPose(0); }, []() { return GHall.WallPoint; }, true);
		for (int32 I = 1; I < Scans; ++I)
		{
			const FString Label = FString::Printf(TEXT("%s_Scan%02d"), *Prefix, I);
			S.Add(Do(Label + TEXT(": pose"), [I]() { KeepEntityDormant(); StandAt(OverlapPose(I), GHall.WallPoint); return 0.3f; }));
			AddScanHere(S, Label);
		}
		S.Add(WaitFor(FString::Printf(TEXT("%s: %d scans alive"), *Prefix, Scans), [Scans]() { return AliveScans() >= Scans; }, 3.f, [Prefix]()
		{
			int32 Older = 0;
			Note(FString::Printf(TEXT("%s: memory ratio %.1f; points alive per scan:%s"), *Prefix, UTheLastShiftSettings::GetScanMemory().Ratio, *PointsPerScan(Older, NewestScan())));
			return 0.f;
		}));
		AddDistanceViews(S, Prefix, [](double D) { return HallFront(D); }, []() { return GHall.WallPoint; }, { 200.0, 500.0, 1000.0, 2000.0 },
			[Prefix](double D, const FLevels& Lv) { CompareShare(Prefix, TEXT("Dist_Overlap01"), D, Lv); });
	}

	/**
	 * Brief 25: 16 scans of the end wall from one pose (2 m in front of it); the points the 15 older scans had on the wall
	 * (read back at each scan) against what they all hold after the 16th (with the memory on: at most 2%).
	 */
	static int32 GSameWallPoints = 0;
	static void AddOverlapSamePose(TArray<FStep>& S)
	{
		const FString Prefix = TEXT("Dist_SamePose16");
		AddScanFrom(S, Prefix + TEXT("_Scan00"), []() { return OverlapPose(0); }, []() { return GHall.WallPoint; }, true);
		S.Add(Do(Prefix + TEXT(": wall points of scan 0"), []() { GSameWallPoints = WallPointsOfLastScan(); return 0.f; }));
		for (int32 I = 1; I < 16; ++I)
		{
			const FString Label = FString::Printf(TEXT("%s_Scan%02d"), *Prefix, I);
			AddScanHere(S, Label);
			if (I < 15)
			{
				S.Add(Do(Label + TEXT(": its wall points"), []() { GSameWallPoints += WallPointsOfLastScan(); return 0.f; }));
			}
		}
		S.Add(WaitFor(Prefix + TEXT(": 16 scans alive"), []() { return AliveScans() >= 16; }, 3.f, []() { return 0.5f; }));
		S.Add(Do(Prefix + TEXT(": what the older scans hold"), [Prefix]()
		{
			int32 Older = 0;
			const FString PerScan = PointsPerScan(Older, NewestScan());
			const double Share = GSameWallPoints > 0 ? 100.0 * Older / GSameWallPoints : 100.0;
			const FString What = FString::Printf(TEXT("%s: the 15 older scans had %d points on the wall; after the 16th they hold %d points in all, wall or not (%.2f%%, at most 2%%); per scan:%s"),
				*Prefix, GSameWallPoints, Older, Share, *PerScan);
			if (MemoryOn())
			{
				Check(GSameWallPoints > 10000 && Share <= 2.0, What);
			}
			else
			{
				Note(What + TEXT(", memory off: not checked"));
			}
			return 0.f;
		}));
		AddDistanceViews(S, Prefix, [](double D) { return HallFront(D); }, []() { return GHall.WallPoint; }, { 200.0, 500.0, 1000.0, 2000.0 },
			[Prefix](double D, const FLevels& Lv) { CompareShare(Prefix, TEXT("Dist_Overlap01"), D, Lv); });
	}

	/** Brief 25, steps: pose I of 16 backing away from the end wall, 2 m to 6 m in equal steps. */
	static FVector StepPose(int32 I) { return HallFront(200.0 + I * 400.0 / 15.0); }

	/** Brief 25, steps: one scan from 2 m and one from 6 m, then 16 scans backing away from 2 m to 6 m, seen from 10 and 20 m. */
	static void AddSteps(TArray<FStep>& S)
	{
		for (const double From : { 200.0, 600.0 })
		{
			const FString Prefix = FString::Printf(TEXT("Dist_StepOne%.0fm"), From / 100.0);
			AddScanFrom(S, Prefix + TEXT("_Scan"), [From]() { return HallFront(From); }, []() { return GHall.WallPoint; }, true);
			AddDistanceViews(S, Prefix, [](double D) { return HallFront(D); }, []() { return GHall.WallPoint; }, { 1000.0, 2000.0 },
				[Prefix](double D, const FLevels& Lv) { GShare.Add(ShareKey(Prefix, D), Lv.Share250Lines); });
		}
		const FString Prefix = TEXT("Dist_Steps16");
		AddScanFrom(S, Prefix + TEXT("_Scan00"), []() { return StepPose(0); }, []() { return GHall.WallPoint; }, true);
		for (int32 I = 1; I < 16; ++I)
		{
			const FString Label = FString::Printf(TEXT("%s_Scan%02d"), *Prefix, I);
			S.Add(Do(Label + TEXT(": pose"), [I]() { KeepEntityDormant(); StandAt(StepPose(I), GHall.WallPoint); return 0.3f; }));
			AddScanHere(S, Label);
		}
		S.Add(WaitFor(Prefix + TEXT(": 16 scans alive"), []() { return AliveScans() >= 16; }, 3.f, [Prefix]()
		{
			int32 Older = 0;
			Note(FString::Printf(TEXT("%s: memory ratio %.1f, scans from 2 m to 6 m; points alive per scan:%s"), *Prefix, UTheLastShiftSettings::GetScanMemory().Ratio,
				*PointsPerScan(Older, NewestScan())));
			return 0.f;
		}));
		AddDistanceViews(S, Prefix, [](double D) { return HallFront(D); }, []() { return GHall.WallPoint; }, { 1000.0, 2000.0 },
			[Prefix](double D, const FLevels& Lv)
			{
				const double* Near = GShare.Find(ShareKey(TEXT("Dist_StepOne2m"), D));
				Note(FString::Printf(TEXT("%s from %.0f m: one scan from 2 m gives %.2f%%"), *Prefix, D / 100.0, Near ? *Near : -1.0));
				CompareShare(Prefix, TEXT("Dist_StepOne6m"), D, Lv);
			});
	}

	// --- Injected points: a residue and the entity ---

	struct FInjected
	{
		TWeakObjectPtr<AResidue> Residue;
		FVector ResidueHome = FVector::ZeroVector;
		FVector ResidueAt = FVector::ZeroVector;
		TWeakObjectPtr<AEntity> Entity;
		float EntitySpeed = -1.f;
		FVector Figure = FVector::ZeroVector;
		/** Direction from the figure to the viewer of the scan taken from 3 m and of the views after it. */
		FVector Dir = FVector::ForwardVector;
		/** Direction from the figure to the viewer of the scan taken from each distance (cm), seen from there. */
		TMap<int32, FVector> DirAt;
	};
	static FInjected GInj;

	static FVector Flat(const FVector& V) { return FVector(V.X, V.Y, GHall.FloorZ); }

	/**
	 * Pixels within RadiusCm (projected, at least 6 pixels) of Center whose hue is nearest to Colour among the scan's
	 * colours (scan, residue, entity, interactable), level above 6: count, median and 90th percentile, and the depth
	 * factor of a point there by both rules.
	 */
	static FString InjectedLevels(const FShot& Shot, const FVector& Center, double RadiusCm, const FLinearColor& Colour)
	{
		FVector2D Screen;
		const double Depth = ViewDepth(Center);
		if (Depth <= 1.0 || !Project(Center, Screen, Shot.Width, Shot.Height))
		{
			return TEXT("off screen");
		}
		const double ViewScale = ViewPixelScaleNow(Shot.Width);
		const int32 Half = FMath::CeilToInt32(FMath::Max(6.0, RadiusCm / (ViewScale * Depth)));
		const FScanColors Colors = UTheLastShiftSettings::GetScanColors();
		auto Chroma = [](const FLinearColor& C)
		{
			const float M = FMath::Max3(C.R, C.G, C.B);
			return M > 0.f ? FVector3f(C.R / M, C.G / M, C.B / M) : FVector3f::ZeroVector;
		};
		const FVector3f Want = Chroma(Colour);
		const FVector3f Others[] = { Chroma(Colors.Scan), Chroma(Colors.Residue), Chroma(Colors.Entity), Chroma(Colors.Interactable) };
		TArray<double> Levels;
		for (int32 Y = FMath::Max(0, FMath::FloorToInt32(Screen.Y) - Half); Y <= FMath::Min(Shot.Height - 1, FMath::FloorToInt32(Screen.Y) + Half); ++Y)
		{
			for (int32 X = FMath::Max(0, FMath::FloorToInt32(Screen.X) - Half); X <= FMath::Min(Shot.Width - 1, FMath::FloorToInt32(Screen.X) + Half); ++X)
			{
				const FColor& P = Shot.Pixels[Y * Shot.Width + X];
				if (Level(P) <= 6)
				{
					continue;
				}
				const FVector3f C = Chroma(FLinearColor(P));
				const double Mine = FVector3f::DistSquared(C, Want);
				bool bNearest = true;
				for (const FVector3f& O : Others)
				{
					bNearest = bNearest && (O.Equals(Want, 1.e-3f) || FVector3f::DistSquared(C, O) >= Mine);
				}
				if (bNearest)
				{
					Levels.Add(Level(P));
				}
			}
		}
		FString Factor = TEXT("no scan");
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 CaptureWidth = 0;
		FLidarScanContext Context;
		if (const ULidarScanComponent* L = Lidar(); L && L->GetLastScanSamples(Positions, Channels, CaptureWidth, Context))
		{
			const double SampleDepth = FMath::Max(FVector::DotProduct(Center - Context.CaptureOrigin, Context.CaptureForward), 1.0);
			const FFactor F = DepthFactor(L, SampleDepth, FMath::Max(Depth, 1.0), Context.TanHalfFOV.X, CaptureWidth, ViewScale, false);
			Factor = FString::Printf(TEXT("scanned from %.1f m, seen from %.1f m: spacing %.2f px, depth factor %.4f by the old rule, %.4f by the new"), SampleDepth / 100.0, Depth / 100.0, F.SpacingPx, F.Old, F.New);
		}
		return FString::Printf(TEXT("%d pixels of its colour within %d px, median %.0f, p90 %.0f, p99 %.0f levels; %s"),
			Levels.Num(), Half, Percentile(Levels, 0.5), Percentile(Levels, 0.9), Percentile(Levels, 0.99), *Factor);
	}

	static void AddInjectedShot(TArray<FStep>& S, const FString& Label, TFunction<FVector()> Center, double RadiusCm, TFunction<FLinearColor()> Colour)
	{
		S.Add(Do(Label + TEXT(": hold the age fade, lines off"), []() { HoldAgeFade(true); ShowLines(false); return 0.3f; }));
		AddShot(S, Label, [Label, Center, RadiusCm, Colour](const FShot& Shot)
		{
			HoldAgeFade(false);
			ShowLines(true);
			Note(FString::Printf(TEXT("%s: %s"), *Label, *InjectedLevels(Shot, Center(), RadiusCm, Colour())));
		});
	}

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static void AddInjected(TArray<FStep>& S)
	{
		// The residue nearest to the hall view, moved 3 m in front of the hall's end wall, 1 m above the floor.
		S.Add(Do(TEXT("injected: the residue"), []()
		{
			GInj = FInjected();
			UWorld* World = W();
			double Best = TNumericLimits<double>::Max();
			for (TActorIterator<AResidue> It(World); World && It; ++It)
			{
				const double D = FVector::DistSquared(It->GetActorLocation(), GHall.Eye);
				if (It->GetState() == EResidueState::Available && D < Best)
				{
					Best = D;
					GInj.Residue = *It;
				}
			}
			GInj.ResidueAt = Flat(GHall.WallPoint) - GHall.Forward * 300.0 + FVector(0, 0, 100);
			if (AResidue* R = GInj.Residue.Get())
			{
				GInj.ResidueHome = R->GetActorLocation();
				R->SetActorLocation(GInj.ResidueAt);
				Note(FString::Printf(TEXT("residue %s moved from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)"), *R->GetName(), GInj.ResidueHome.X, GInj.ResidueHome.Y, GInj.ResidueHome.Z,
					GInj.ResidueAt.X, GInj.ResidueAt.Y, GInj.ResidueAt.Z));
			}
			else
			{
				Note(TEXT("no available residue in this map"));
			}
			return 0.f;
		}));
		auto ResidueAt = []() { return GInj.ResidueAt; };
		auto ResidueColour = []() { return UTheLastShiftSettings::GetScanColors().Residue; };
		for (const double D : { 300.0, 1000.0, 3000.0 })
		{
			const FString Label = FString::Printf(TEXT("Inj_Residue_Scan%02.0fm_View%02.0fm"), D / 100.0, D / 100.0);
			AddScanFrom(S, Label, [D]() { return Flat(GInj.ResidueAt) - GHall.Forward * D; }, ResidueAt, false);
			AddInjectedShot(S, Label, ResidueAt, 40.0, ResidueColour);
		}
		AddScanFrom(S, TEXT("Inj_Residue_Scan03m"), []() { return Flat(GInj.ResidueAt) - GHall.Forward * 300.0; }, ResidueAt, false);
		for (const double D : { 1000.0, 3000.0 })
		{
			const FString Label = FString::Printf(TEXT("Inj_Residue_Scan03m_View%02.0fm"), D / 100.0);
			S.Add(Do(Label + TEXT(": view"), [D]() { StandAt(Flat(GInj.ResidueAt) - GHall.Forward * D, GInj.ResidueAt); return 0.6f; }));
			AddInjectedShot(S, Label, ResidueAt, 40.0, ResidueColour);
		}
		S.Add(Do(TEXT("injected: the residue goes back"), []()
		{
			if (AResidue* R = GInj.Residue.Get())
			{
				R->SetActorLocation(GInj.ResidueHome);
			}
			return 0.f;
		}));

		// The entity: awakened and made to appear, held (Speed 0.1 cm/s), then scanned from a direction with sight of it at
		// 3, 10 and 30 m. It stays awake for the rest of the session: run this scenario last.
		S.Add(Do(TEXT("injected: the entity, Resume, pose"), []()
		{
			// Its points are measured by the entity's own colour (Brief 17: the scan's until it is found): found from here on.
			if (URunSubsystem* Run = URunSubsystem::Get(W()))
			{
				Run->SetEntityFound(true, TEXT("tls.Links.DistanceCheck injected"));
			}
			ResumeTime();
			KeepEntityDormant();
			StandAt(Flat(GInj.ResidueAt) - GHall.Forward * 1500.0, GInj.ResidueAt);
			return 0.7f;
		}));
		S.Add(Do(TEXT("injected: the entity, Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("injected: the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f, []()
		{
			AEntity* E = FindEntity();
			GInj.Entity = E;
			if (E)
			{
				GInj.EntitySpeed = E->Speed;
				E->Speed = 0.1f;
				E->Awaken();
				if (!E->IsManifested())
				{
					E->SpawnNow(0.f);
				}
			}
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("injected: the entity has appeared"), []() { const AEntity* E = GInj.Entity.Get(); return E && E->IsManifested(); }, 3.f, []()
		{
			const AEntity* E = GInj.Entity.Get();
			UWorld* World = W();
			if (!E || !World)
			{
				return 0.f;
			}
			GInj.Figure = E->GetFigureLocation();
			// For each distance, the first direction from back down the hall outward (0, +2.5, -2.5, +5, ... degrees) with a
			// floor there at the figure's height and sight of the figure; back down the hall when none has. The hall's floor
			// can be a narrow span, so the steps are fine.
			const double BaseYaw = (-GHall.Forward).Rotation().Yaw;
			for (const double D : { 300.0, 1000.0, 3000.0 })
			{
				const int32 Key = FMath::RoundToInt32(D);
				GInj.DirAt.Add(Key, -GHall.Forward);
				FString Found = TEXT("none with floor and sight: back down the hall");
				for (int32 K = 0; K < 144; ++K)
				{
					const double Offset = ((K + 1) / 2) * 2.5 * ((K % 2) ? 1.0 : -1.0);
					const FVector Dir = FRotator(0.0, BaseYaw + Offset, 0.0).Vector();
					const FVector At = GInj.Figure + Dir * D;
					FHitResult Hit;
					const bool bFloor = World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 150), At - FVector(0, 0, 300), ECC_Visibility);
					if (bFloor && FMath::Abs(Hit.ImpactPoint.Z - GInj.Figure.Z) < 100.0 && E->CanBeSeenFrom(Hit.ImpactPoint + FVector(0, 0, 160), GInj.Figure))
					{
						GInj.DirAt.Add(Key, Dir);
						Found = FString::Printf(TEXT("(%.2f, %.2f), %+.0f degrees from back down the hall"), Dir.X, Dir.Y, Offset);
						break;
					}
				}
				Note(FString::Printf(TEXT("entity at %.0f m: viewing direction %s"), D / 100.0, *Found));
			}
			GInj.Dir = GInj.DirAt.FindRef(300);
			Note(FString::Printf(TEXT("entity figure at (%.0f, %.0f, %.0f); scanned from 3 m along (%.2f, %.2f), then seen from 10 and 30 m along the same direction"),
				GInj.Figure.X, GInj.Figure.Y, GInj.Figure.Z, GInj.Dir.X, GInj.Dir.Y));
			return 0.f;
		}));
		auto FigureCentre = []() { return GInj.Figure + FVector(0, 0, 90); };
		auto EntityColour = []() { return UTheLastShiftSettings::GetScanColors().Entity; };
		auto AddEntityScan = [&S](const FString& Label, double D)
		{
			S.Add(Do(Label + TEXT(": pose"), [D]()
			{
				if (const AEntity* E = GInj.Entity.Get())
				{
					GInj.Figure = E->GetFigureLocation();
				}
				const FVector* Dir = GInj.DirAt.Find(FMath::RoundToInt32(D));
				StandAt(GInj.Figure + (Dir ? *Dir : GInj.Dir) * D, GInj.Figure + FVector(0, 0, 100));
				return 0.4f;
			}));
			S.Add(Do(Label + TEXT(": scan"), []() { if (ULidarScanComponent* L = Lidar()) { L->Scan(); } return 0.3f; }));
		};
		for (const double D : { 300.0, 1000.0, 3000.0 })
		{
			const FString Label = FString::Printf(TEXT("Inj_Entity_Scan%02.0fm_View%02.0fm"), D / 100.0, D / 100.0);
			AddEntityScan(Label, D);
			AddInjectedShot(S, Label, FigureCentre, 110.0, EntityColour);
		}
		AddEntityScan(TEXT("Inj_Entity_Scan03m"), 300.0);
		for (const double D : { 1000.0, 3000.0 })
		{
			const FString Label = FString::Printf(TEXT("Inj_Entity_Scan03m_View%02.0fm"), D / 100.0);
			S.Add(Do(Label + TEXT(": view"), [D]() { StandAt(GInj.Figure + GInj.Dir * D, GInj.Figure + FVector(0, 0, 100)); return 0.6f; }));
			AddInjectedShot(S, Label, FigureCentre, 110.0, EntityColour);
		}
		S.Add(Do(TEXT("injected: the entity's speed back"), []()
		{
			if (AEntity* E = GInj.Entity.Get(); E && GInj.EntitySpeed >= 0.f)
			{
				E->Speed = GInj.EntitySpeed;
			}
			return 0.f;
		}));
	}

	static const TCHAR* GLocalExposureNames[3] = { TEXT("r.LocalExposure"), TEXT("r.DefaultFeature.LocalExposure.HighlightContrastScale"), TEXT("r.DefaultFeature.LocalExposure.ShadowContrastScale") };
	static FString GSavedLocalExposure[3];

	static void StartDistanceCheck(const FString& Scenario, bool bOldLocalExposure, bool bQuit, const FString& Tag)
	{
		StartRun(TEXT("LinksDistanceCheck"), bQuit);
		if (Scenario.Equals(TEXT("injected"), ESearchCase::IgnoreCase))
		{
			// Brief 29: the entity's figure in area 1's form for the run (put back in Finish).
			EntityFormTest::ForceAreaOneForm(TEXT("LinksDistanceCheck"));
		}
		GShotFolder = TEXT("Brief13b");
		GShotTag = Tag;
		GRun.Cleanup = [bOldLocalExposure]()
		{
			CommonCleanup();
			if (AResidue* R = GInj.Residue.Get(); R && R->GetActorLocation().Equals(GInj.ResidueAt, 1.0))
			{
				R->SetActorLocation(GInj.ResidueHome);
			}
			if (AEntity* E = GInj.Entity.Get(); E && GInj.EntitySpeed >= 0.f)
			{
				E->Speed = GInj.EntitySpeed;
			}
			GInj = FInjected();
			ResumeTime();
			for (int32 I = 0; bOldLocalExposure && I < 3; ++I)
			{
				if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(GLocalExposureNames[I]))
				{
					V->Set(*GSavedLocalExposure[I], ECVF_SetByConsole);
				}
			}
		};
		auto Is = [&Scenario](const TCHAR* Name) { return Scenario.Equals(Name, ESearchCase::IgnoreCase); };
		const bool bHere = Is(TEXT("here"));
		const bool bTestLidar = Is(TEXT("testlidar"));
		const bool bHall = !bHere && !bTestLidar;
		if (bHall && !Is(TEXT("hall")) && !Is(TEXT("overlap")) && !Is(TEXT("injected")) && !Is(TEXT("ownpose")) && !Is(TEXT("steps")))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.DistanceCheck: unknown scenario '%s' (hall, testlidar, overlap, steps, injected, ownpose, here)."), *Scenario);
			return;
		}
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 30.f, [bOldLocalExposure, bHall]()
		{
			FullResolution(true);
			const TCHAR* Values[3] = { TEXT("1"), TEXT("0.8"), TEXT("0.8") };
			for (int32 I = 0; bOldLocalExposure && I < 3; ++I)
			{
				if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(GLocalExposureNames[I]))
				{
					GSavedLocalExposure[I] = V->GetString();
					V->Set(Values[I], ECVF_SetByConsole);
				}
			}
			const ULidarScanComponent* L = Lidar();
			Note(FString::Printf(TEXT("Brief 25 memory: %s (lidar ratio %.1f, tolerance %.1f cm)"), *MemoryIniLine(UTheLastShiftSettings::GetScanMemory()), L->MemoryRatio, L->MemoryTolerance));
			Note(FString::Printf(TEXT("local exposure %s; lidar: PointIntensity %.2f, DensityCompensation %.2f, AngleCompensation %.2f, DensityBoostMin %.3f, DensityBoostMax %.2f, AlphaFloor %.2f (age fade held for the shots), FadeDuration %.1f s, PointPixelSize %.2f; lines: intensity %.2f, width %.2f px"),
				bOldLocalExposure ? TEXT("as before Step 1 (r.LocalExposure 1, contrast scales 0.8)") : TEXT("off"), L->PointIntensity, L->DensityCompensation, L->AngleCompensation, L->DensityBoostMin,
				L->DensityBoostMax, L->AlphaFloor, L->FadeDuration, L->PointPixelSize, L->LineIntensity, L->LinePixelWidth));
			if (bHall)
			{
				if (APlayerController* C = PC())
				{
					C->ConsoleCommand(TEXT("tls.Beacon.View 0 middle"));
				}
			}
			return 0.8f;
		}));
		if (bHall)
		{
			S.Add(Do(TEXT("the hall"), []() { FindHall(); return GHall.bValid ? 0.f : -1.f; }));
		}
		if (Is(TEXT("hall")))
		{
			// Area 2: the hall's end wall scanned from 2 m, seen from 2, 5, 10 and 20 m.
			AddScanFrom(S, TEXT("Dist_Hall_Scan2m"), []() { return HallFront(200.0); }, []() { return GHall.WallPoint; }, true);
			AddDistanceViews(S, TEXT("Dist_Hall"), [](double D) { return HallFront(D); }, []() { return GHall.WallPoint; }, { 200.0, 500.0, 1000.0, 2000.0 });
			// The floor 3 m ahead of the hall view, scanned from there, then seen from 0, 5 and 10 m farther back: from eye
			// height, about 28, 11 and 7 degrees above the floor (edge-on, where lines crowd).
			auto FloorTarget = []() { return GHall.Floor + GHall.Forward * 300.0; };
			AddScanFrom(S, TEXT("Dist_Floor_Scan"), []() { return GHall.Floor; }, FloorTarget, true);
			AddDistanceViews(S, TEXT("Dist_Floor"), [](double D) { return GHall.Floor - GHall.Forward * D; }, FloorTarget, { 0.0, 500.0, 1000.0 });
		}
		else if (Is(TEXT("overlap")))
		{
			// Area 2: the hall's end wall covered by 1 (Brief 25: the reference), 4, 8 and 16 scans taken from nearby poses about
			// 2 m away, then (Brief 25) by 16 scans from one pose.
			GShare.Reset();
			for (const int32 Scans : { 1, 4, 8, 16 })
			{
				AddOverlap(S, Scans);
			}
			AddOverlapSamePose(S);
		}
		else if (Is(TEXT("steps")))
		{
			// Area 2 (Brief 25): one scan from 2 m and one from 6 m, then 16 scans backing away from 2 m to 6 m.
			GShare.Reset();
			AddSteps(S);
		}
		else if (Is(TEXT("injected")))
		{
			AddInjected(S);
		}
		else if (Is(TEXT("ownpose")))
		{
			// Area 2: the hall's end wall scanned and seen from 3 m and from 10 m.
			for (const double D : { 300.0, 1000.0 })
			{
				const FString Label = FString::Printf(TEXT("Own_Wall%02.0fm"), D / 100.0);
				AddScanFrom(S, Label, [D]() { return HallFront(D); }, []() { return GHall.WallPoint; }, true);
				AddPairShots(S, Label);
			}
		}
		else if (bHere)
		{
			// Any map: a scan from the current pose (the automatic scan of a Stop, or one more scan), seen from that pose.
			S.Add(Do(TEXT("scan from here"), []()
			{
				KeepEntityDormant();
				if (!IsStopped())
				{
					StopTime();
				}
				else if (ULidarScanComponent* L = Lidar())
				{
					L->Scan();
				}
				return 0.f;
			}));
			S.Add(WaitFor(TEXT("a scan is alive"), []() { return AliveScans() >= 1; }, 4.f, []() { return 0.f; }));
			S.Add(WaitFor(TEXT("its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return WaitGrowthDone(); }));
			AddPairShots(S, TEXT("Own_Here"));
		}
		else
		{
			// TestLidar: the near surface is the main room's face of the wall at X = -200; the surface behind it is the corridor's
			// east wall (X = 200, inner face). Each is scanned from 2 m, then seen from the main room 2, 5, 10 and 12.5 m from the
			// near wall, looking through it at the wall behind.
			auto View = [](double D) { return FVector(-200.0 - D, -400.0, 0.0); };
			auto Target = []() { return FVector(200.0, -400.0, 120.0); };
			AddScanFrom(S, TEXT("Dist_Near_Scan2m"), []() { return FVector(-400.0, -400.0, 0.0); }, []() { return FVector(-200.0, -400.0, 120.0); }, true);
			AddDistanceViews(S, TEXT("Dist_Near"), View, Target, { 200.0, 500.0, 1000.0, 1250.0 });
			AddScanFrom(S, TEXT("Dist_Behind_Scan2m"), []() { return FVector(0.0, -400.0, 0.0); }, []() { return FVector(200.0, -400.0, 120.0); }, true);
			AddDistanceViews(S, TEXT("Dist_Behind"), View, Target, { 200.0, 500.0, 1000.0, 1250.0 });
		}
		Launch();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksEndsCheckCommand(
	TEXT("tls.Links.EndsCheck"),
	TEXT("tls.Links.EndsCheck [LiftTexels=0.5] [Quit=0]: (PIE or a package, in Area2_ServerHalls) the hall's end wall and its floor at 10, 20 and 30 m, scanned and seen from the same pose, with 2 and 4 neighbours, lift 0 and LiftTexels: line pixels in the middle of the frame with the geometry visible against hidden. Shots to Saved/Screenshots/Brief13."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartEndsCheck(Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 2.f) : 0.5f, Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksDistanceCheckCommand(
	TEXT("tls.Links.DistanceCheck"),
	TEXT("tls.Links.DistanceCheck <hall|testlidar|overlap|steps|injected|ownpose|here> [OldLocalExposure=0] [Quit=0] [Tag]: (PIE or a package) on-screen levels in the middle of the frame (median, p90, p99, share at 250 or above) and the depth factor by the rule before Brief 13b and after it. hall (Area2_ServerHalls): the end wall scanned from 2 m, seen from 2, 5, 10 and 20 m, and the floor 3 m ahead seen from 0, 5 and 10 m farther back (edge-on); testlidar: a wall scanned from 2 m, seen from 2 to 12.5 m, and a surface behind it; overlap (Area2): the end wall covered by 1, 4, 8 and 16 scans from nearby poses and by 16 scans from one pose, seen from 2 to 20 m (Brief 25: with the scan memory on, each cell at most one scan's share at 250 or above + 0.5 points, and after 16 scans from one pose the older 15 hold at most 2% of their wall points); steps (Area2, Brief 25): 16 scans backing away from 2 to 6 m, seen from 10 and 20 m, against one scan from 2 m and from 6 m; injected (Area2): a residue and the entity scanned and seen from 3, 10 and 30 m, and scanned from 3 m, seen from 10 and 30 m (awakens the entity: run it last); ownpose (Area2): the end wall scanned and seen from 3 m and 10 m; here (any map): a scan from the current pose seen from it. Tag prefixes the shots' names. OldLocalExposure=1 sets local exposure as before Brief 13 Step 1 for the run. Shots to Saved/Screenshots/Brief13b."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartDistanceCheck(Args.Num() > 0 ? Args[0] : FString(TEXT("hall")), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0,
			Args.Num() > 3 ? Args[3] : FString());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksLiftCommand(
	TEXT("tls.Links.Lift"),
	TEXT("tls.Links.Lift <LiftTexels> [LiftMaxCm]: sets the lift of the lines' ends in the settings for this session (not saved) and has the director push it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		FScanLinks& V = GetMutableDefault<UTheLastShiftSettings>()->ScanLinks;
		if (Args.Num() > 0) { V.LiftTexels = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 2.f); }
		if (Args.Num() > 1) { V.LiftMaxCm = FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 100.f); }
		if (UScanDirectorComponent* D = LinksDebug::Director())
		{
			D->ApplyScanLinks();
		}
		const ULidarScanComponent* L = LinksDebug::Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Lift: %.2f texel, at most %.1f cm; lidar %.2f / %.1f."), V.LiftTexels, V.LiftMaxCm, L ? L->LineLiftTexels : -1.f, L ? L->LineLiftMaxCm : -1.f);
	}));

// =====================================================================================================
// Brief 23: the two modes of the links. tls.Links.Mode (the session's mode and the grid's distance), tls.Links.ModeCheck
// (TestLidar: the switch, alive scans keep their lines, lines off in both modes), tls.Links.GridSpan (the length of the
// grid's steps on the floor by distance) and tls.Links.ModeShots (the same pose in both modes, shots to
// Saved/Screenshots/Brief23).
// =====================================================================================================

namespace LinksDebug
{
	static bool ParseMode(const FString& Text, ELidarLinkMode& Out)
	{
		if (Text.Equals(TEXT("grid"), ESearchCase::IgnoreCase))
		{
			Out = ELidarLinkMode::Grid;
			return true;
		}
		if (Text.Equals(TEXT("nearest"), ESearchCase::IgnoreCase))
		{
			Out = ELidarLinkMode::Nearest;
			return true;
		}
		return false;
	}

	/** The saved line values with another mode and number of neighbours (0 = lines off), pushed through the director. */
	static void PushMode(ELidarLinkMode Mode, int32 Neighbours, float GridCm = -1.f)
	{
		FScanLinks Values = GSavedLinks;
		Values.Mode = Mode;
		Values.Neighbours = Neighbours;
		if (GridCm > 0.f)
		{
			Values.GridMaxDistanceCm = GridCm;
		}
		PushLinks(Values);
	}

	static void ExecConsole(const TCHAR* Command)
	{
		if (GEngine)
		{
			GEngine->Exec(W(), Command);
		}
	}

	// --- tls.Links.ModeCheck ---

	struct FModeState
	{
		int32 First = INDEX_NONE, Second = INDEX_NONE;
		int32 FirstLinks = 0, SecondLinks = 0;
		int32 StatsBefore = INDEX_NONE;
		int32 ScanBefore = INDEX_NONE;
	};
	static FModeState GMode;

	static int32 LinesOf(int32 Scan) { return SlotOf(Scan).LineParticles; }

	static void AddOffInMode(TArray<FStep>& S, ELidarLinkMode Mode)
	{
		const FString Name = ModeName(Mode);
		S.Add(Do(FString::Printf(TEXT("off (%s): scan"), *Name), [Mode]()
		{
			PushMode(Mode, 0);
			ULidarScanComponent* L = Lidar();
			GMode.StatsBefore = L->GetLastLinkStats().ScanIndex;
			GMode.ScanBefore = NewestScan();
			KeepEntityDormant();
			L->Scan();
			return 1.0f;
		}));
		S.Add(Do(FString::Printf(TEXT("off (%s): nothing searched or drawn"), *Name), [Name, Mode]()
		{
			const ULidarScanComponent* L = Lidar();
			const int32 Scan = NewestScan();
			const FLidarScanSlotStats Slot = SlotOf(Scan);
			Check(L->LinkNeighbours == 0 && L->LinkMode == Mode && Scan == GMode.ScanBefore + 1 && Slot.CapturedParticles > 1000,
				FString::Printf(TEXT("off (%s): lines off, the scan is taken (%d points)"), *Name, Slot.CapturedParticles));
			Check(L->GetLastLinkStats().ScanIndex == GMode.StatsBefore && L->GetPendingLinkSearches() == 0 && Slot.LineParticles == 0,
				FString::Printf(TEXT("off (%s): no search ran and no line is drawn (last searched scan %d, was %d; %d pending; %d line particles)"),
					*Name, L->GetLastLinkStats().ScanIndex, GMode.StatsBefore, L->GetPendingLinkSearches(), Slot.LineParticles));
			return 0.f;
		}));
	}

	static void StartModeCheck(bool bQuit)
	{
		StartRun(TEXT("LinksModeCheck"), bQuit);
		MemoryOffForRun();
		GMode = FModeState();
		GRun.Cleanup = []()
		{
			CommonCleanup();
			ResumeTime();
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 15.f, []()
		{
			Note(FString::Printf(TEXT("settings: %d neighbours, mode %s, %.1f cm nearest, %.1f cm grid"), GSavedLinks.Neighbours, ModeName(GSavedLinks.Mode),
				GSavedLinks.MaxDistanceCm, GSavedLinks.GridMaxDistanceCm));
			PushMode(ELidarLinkMode::Nearest, FMath::Max(GSavedLinks.Neighbours, 2));
			return 0.f;
		}));
		AddStopAndScanAt(S, TEXT("1 (nearest)"), Poses::MainRoom, Poses::WestWall);
		S.Add(Do(TEXT("1: the scan in the nearest mode"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const FLidarLinkStats St = L->GetLastLinkStats();
			GMode.First = NewestScan();
			GMode.FirstLinks = St.Links;
			Check(St.ScanIndex == GMode.First && St.Mode == ELidarLinkMode::Nearest && St.Links > 1000 && St.MaxDegree <= L->LinkNeighbours,
				FString::Printf(TEXT("1: scan %d found its links in the nearest mode (%s, %d links, at most %d a point)"), St.ScanIndex, ModeName(St.Mode), St.Links, St.MaxDegree));
			return 0.3f;
		}));
		S.Add(Do(TEXT("the command switches to the grid"), []()
		{
			Check(LinesOf(GMode.First) == GMode.FirstLinks, FString::Printf(TEXT("1: one line per link (%d lines, %d links)"), LinesOf(GMode.First), GMode.FirstLinks));
			ExecConsole(TEXT("tls.Links.Mode grid 150"));
			const ULidarScanComponent* L = Lidar();
			const FScanLinks& V = UTheLastShiftSettings::GetScanLinks();
			Check(V.Mode == ELidarLinkMode::Grid && FMath::IsNearlyEqual(V.GridMaxDistanceCm, 150.f) && FMath::IsNearlyEqual(V.MaxDistanceCm, GSavedLinks.MaxDistanceCm)
				&& L->LinkMode == ELidarLinkMode::Grid && FMath::IsNearlyEqual(L->LinkMaxDistance, 150.f),
				FString::Printf(TEXT("tls.Links.Mode grid 150: the settings hold grid and 150 cm (%s, %.1f; nearest's %.1f unchanged), the lidar got them (%s, %.1f cm)"),
					ModeName(V.Mode), V.GridMaxDistanceCm, V.MaxDistanceCm, ModeName(L->LinkMode), L->LinkMaxDistance));
			ExecConsole(*FString::Printf(TEXT("tls.Links.Mode grid %.1f"), GSavedLinks.GridMaxDistanceCm));
			Check(FMath::IsNearlyEqual(L->LinkMaxDistance, GSavedLinks.GridMaxDistanceCm),
				FString::Printf(TEXT("tls.Links.Mode grid %.1f: the grid's distance back to the settings' (%.1f cm)"), GSavedLinks.GridMaxDistanceCm, L->LinkMaxDistance));
			return 0.2f;
		}));
		S.Add(Do(TEXT("after the switch the alive scan keeps its lines"), []()
		{
			Check(LinesOf(GMode.First) == GMode.FirstLinks, FString::Printf(TEXT("switch: scan %d still draws its %d nearest lines (%d)"), GMode.First, GMode.FirstLinks, LinesOf(GMode.First)));
			return 0.f;
		}));
		AddScanHere(S, TEXT("2 (grid)"));
		S.Add(Do(TEXT("2: the scan in the grid mode"), []()
		{
			const FLidarLinkStats St = Lidar()->GetLastLinkStats();
			GMode.Second = NewestScan();
			GMode.SecondLinks = St.Links;
			Check(St.ScanIndex == GMode.Second && St.Mode == ELidarLinkMode::Grid && St.Links > 1000,
				FString::Printf(TEXT("2: scan %d, taken after the switch, found its links on the grid (%s, %d links)"), St.ScanIndex, ModeName(St.Mode), St.Links));
			Check(St.LongestLink <= GSavedLinks.GridMaxDistanceCm + 0.01f, FString::Printf(TEXT("2: the longest link is within the grid's %.1f cm (%.2f)"), GSavedLinks.GridMaxDistanceCm, St.LongestLink));
			CheckGridMesh(TEXT("2"));
			return 0.3f;
		}));
		S.Add(Do(TEXT("2: the lines of both scans"), []()
		{
			Check(LinesOf(GMode.Second) == GMode.SecondLinks && LinesOf(GMode.First) == GMode.FirstLinks,
				FString::Printf(TEXT("2: the grid scan draws its %d lines (%d), the nearest scan keeps its %d (%d)"), GMode.SecondLinks, LinesOf(GMode.Second), GMode.FirstLinks, LinesOf(GMode.First)));
			ExecConsole(TEXT("tls.Links.Mode nearest"));
			return 0.f;
		}));
		AddScanHere(S, TEXT("3 (nearest again)"));
		S.Add(Do(TEXT("3: the scan in the nearest mode again"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const FLidarLinkStats St = L->GetLastLinkStats();
			Check(St.Mode == ELidarLinkMode::Nearest && St.Links > 1000 && St.MaxDegree <= L->LinkNeighbours && FMath::IsNearlyEqual(L->LinkMaxDistance, GSavedLinks.MaxDistanceCm),
				FString::Printf(TEXT("3: back in the nearest mode (%s, %d links, at most %d a point, %.1f cm)"), ModeName(St.Mode), St.Links, St.MaxDegree, L->LinkMaxDistance));
			return 0.3f;
		}));
		S.Add(Do(TEXT("3: the older scans keep their lines"), []()
		{
			Check(LinesOf(GMode.Second) == GMode.SecondLinks && LinesOf(GMode.First) == GMode.FirstLinks,
				FString::Printf(TEXT("3: the grid scan keeps %d lines (%d), the first nearest scan %d (%d)"), GMode.SecondLinks, LinesOf(GMode.Second), GMode.FirstLinks, LinesOf(GMode.First)));
			return 0.f;
		}));
		AddOffInMode(S, ELidarLinkMode::Nearest);
		AddOffInMode(S, ELidarLinkMode::Grid);
		Launch();
	}

	// --- tls.Links.ModeShots ---

	struct FModeShot
	{
		ELidarLinkMode Mode = ELidarLinkMode::Nearest;
		float GridCm = -1.f;
		FString Name;
	};

	static FVector GShotsLocation = FVector::ZeroVector;
	static FRotator GShotsRotation = FRotator::ZeroRotator;

	/**
	 * The shots' fixed poses, from the area's plan (the same in every build) or the arrival hangar: corridor, 1.5 m into the
	 * area's first corridor looking 15 m along it at eye height (its floor at a grazing angle); corridorwall, 6 m into it,
	 * 45 cm from one of its walls, looking across at the other (about 3 m away); floorhall, 1.5 m into the area's first hall
	 * with a floor (not a web), looking 15 m along the route; ship (Prologue_Arrival), 4 m from the ship's closed long side,
	 * square on; shipoblique, the same side from its corner, at a grazing angle, its chamfer above. False when the map has
	 * none.
	 */
	static bool ModeShotPose(const FString& Pose, FVector& Floor, FVector& LookAt)
	{
		const APawn* P = Pawn();
		const double EyeZ = EyeOf(P).Z;
		if (Pose == TEXT("ship"))
		{
			Floor = FVector(200.f, 0.f, 0.f);
			LookAt = FVector(600.f, 0.f, 250.f);
			return true;
		}
		if (Pose == TEXT("shipoblique"))
		{
			Floor = FVector(150.f, -650.f, 0.f);
			LookAt = FVector(600.f, 150.f, 250.f);
			return true;
		}
		UWorld* World = W();
		const AAreaInfo* Info = AAreaInfo::Find(World);
		const AAreaGenerator* Gen = AAreaGenerator::Find(World);
		const FAreaRecipe* Recipe = Gen ? Gen->GetRecipe() : nullptr;
		for (int32 I = 0; Info && I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			const bool bFloorHall = Seg.Type == EAreaSegmentType::Hall && !(Recipe && Recipe->Segments.IsValidIndex(I) && Recipe->Segments[I].bWeb);
			const bool bWanted = (Pose == TEXT("floorhall")) ? bFloorHall : Seg.Type == EAreaSegmentType::Corridor;
			if (!bWanted)
			{
				continue;
			}
			int32 Unused = 0;
			const float S0 = Seg.RouteStart;
			const float S1 = FMath::Max(Seg.RouteEnd, S0 + 1.f);
			if (Pose == TEXT("corridorwall"))
			{
				const FVector In = Info->GetRoutePointAtDistance(S0 + 600.f, Unused);
				const FVector Ahead = Info->GetRoutePointAtDistance(S0 + 900.f, Unused);
				const FVector Dir = (Ahead - In).GetSafeNormal2D();
				const FVector Side(-Dir.Y, Dir.X, 0.f);
				FCollisionQueryParams Params(TEXT("ModeShotWall"), false, P);
				FHitResult Left, Right;
				const FVector From(In.X, In.Y, EyeZ);
				if (!World->LineTraceSingleByChannel(Left, From, From + Side * 2000.f, ECC_Pawn, Params)
					|| !World->LineTraceSingleByChannel(Right, From, From - Side * 2000.f, ECC_Pawn, Params))
				{
					return false;
				}
				Floor = Left.ImpactPoint - Side * 45.f;
				LookAt = FVector(Right.ImpactPoint.X, Right.ImpactPoint.Y, EyeZ);
				Note(FString::Printf(TEXT("corridorwall: the corridor is %.0f cm wide there; the far wall %.0f cm from the eye"), Left.Distance + Right.Distance,
					FVector::Dist2D(Floor, LookAt)));
				return true;
			}
			const FVector In = Info->GetRoutePointAtDistance(S0 + 150.f, Unused);
			const FVector Ahead = Info->GetRoutePointAtDistance(FMath::Min(S0 + 1500.f, S1), Unused);
			Floor = In;
			LookAt = FVector(Ahead.X, Ahead.Y, EyeZ);
			return true;
		}
		return false;
	}

	static void StartModeShots(const FString& Label, float SecondGridCm, bool bQuit, const FString& Pose)
	{
		StartRun(TEXT("LinksModeShots"), bQuit);
		GShotFolder = TEXT("Brief23");
		GRun.Cleanup = []()
		{
			CommonCleanup();
			ResumeTime();
		};
		const APawn* P = Pawn();
		const APlayerController* C = PC();
		if (!P || !C)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.ModeShots: no player."));
			return;
		}
		if (!Pose.IsEmpty() && Pose != TEXT("here"))
		{
			FVector Floor, LookAt;
			if (!ModeShotPose(Pose, Floor, LookAt))
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.ModeShots: no pose '%s' in this map (here, corridor, corridorwall, floorhall, ship, shipoblique)."), *Pose);
				return;
			}
			ResumeTime();
			StandAt(Floor, LookAt);
		}
		Note(FString::Printf(TEXT("pose %s: at (%.0f, %.0f, %.0f), looking (%.1f, %.1f)"), Pose.IsEmpty() ? TEXT("here") : *Pose, P->GetActorLocation().X, P->GetActorLocation().Y,
			P->GetActorLocation().Z, C->GetControlRotation().Pitch, C->GetControlRotation().Yaw));
		GShotsLocation = P->GetActorLocation();
		GShotsRotation = C->GetControlRotation();
		TArray<FModeShot> Shots;
		Shots.Add({ ELidarLinkMode::Nearest, -1.f, TEXT("Nearest") });
		Shots.Add({ ELidarLinkMode::Grid, -1.f, FString::Printf(TEXT("Grid%.0f"), GSavedLinks.GridMaxDistanceCm) });
		if (SecondGridCm > 0.f)
		{
			Shots.Add({ ELidarLinkMode::Grid, SecondGridCm, FString::Printf(TEXT("Grid%.0f"), SecondGridCm) });
		}
		TArray<FStep>& S = GRun.Steps;
		for (const FModeShot& Shot : Shots)
		{
			const FString Name = Label + TEXT("_") + Shot.Name;
			S.Add(Do(Name + TEXT(": Resume, pose, mode"), [Shot]()
			{
				ResumeTime();
				KeepEntityDormant();
				PushMode(Shot.Mode, FMath::Max(GSavedLinks.Neighbours, 2), Shot.GridCm);
				APawn* Pw = Pawn();
				Pw->SetActorLocation(GShotsLocation, false, nullptr, ETeleportType::TeleportPhysics);
				if (const ACharacter* Character = Cast<ACharacter>(Pw))
				{
					Character->GetCharacterMovement()->StopMovementImmediately();
				}
				PC()->SetControlRotation(GShotsRotation);
				return 0.7f;
			}));
			S.Add(Do(Name + TEXT(": Stop"), []() { KeepEntityDormant(); StopTime(); FullResolution(true); return 0.f; }));
			S.Add(WaitFor(Name + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f));
			S.Add(WaitFor(Name + TEXT(": its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return WaitGrowthDone(); }));
			AddShot(S, Name, [Name](const FShot&)
			{
				const FLidarLinkStats St = Lidar()->GetLastLinkStats();
				Note(FString::Printf(TEXT("%s: scan %d, %s, %.1f cm: %d points, %d links, %d dropped, at most %d a point, longest %.1f cm; shot Saved/Screenshots/Brief23/%s.png"),
					*Name, St.ScanIndex, ModeName(St.Mode), Lidar()->LinkMaxDistance, St.Points, St.Links, St.Dropped, St.MaxDegree, St.LongestLink, *Name));
				LogDegreeBands(*FString::Printf(TEXT("%s   ..  %s: links per point"), *GRun.Name, *Name));
				LogDroppedRows(*FString::Printf(TEXT("%s   ..  %s:"), *GRun.Name, *Name));
				Check(St.ScanIndex == NewestScan() && St.Links > 0, FString::Printf(TEXT("%s: the shot shows the newest scan's lines"), *Name));
			});
		}
		Launch();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksModeCommand(
	TEXT("tls.Links.Mode"),
	TEXT("tls.Links.Mode [nearest|grid] [GridMaxDistanceCm]: sets ScanLinks.Mode (and the grid mode's longest link) in the settings for this session (not saved to the ini) and has the director push the mode and the longest link of that mode; a scan taken after it uses them, alive scans keep their lines. No argument: logs them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		FScanLinks& V = GetMutableDefault<UTheLastShiftSettings>()->ScanLinks;
		if (Args.Num() > 0)
		{
			ELidarLinkMode Mode;
			if (!LinksDebug::ParseMode(Args[0], Mode))
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.Mode: unknown mode '%s' (nearest, grid)."), *Args[0]);
				return;
			}
			V.Mode = Mode;
		}
		if (Args.Num() > 1)
		{
			V.GridMaxDistanceCm = FMath::Clamp(FCString::Atof(*Args[1]), 1.f, 1000.f);
		}
		if (Args.Num() > 0)
		{
			if (UScanDirectorComponent* D = LinksDebug::Director())
			{
				D->ApplyScanLinks();
			}
		}
		const ULidarScanComponent* L = LinksDebug::Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Mode: %s, nearest %.1f cm, grid %.1f cm, %d neighbours; lidar %s, %.1f cm, %d neighbours. Ini line: ScanLinks=(Neighbours=%d,MaxDistanceCm=%.1f,PixelWidth=%.1f,Intensity=%.1f,MaxNormalAngle=%.1f,MaxLinksPerScan=%d,LiftTexels=%.1f,LiftMaxCm=%.1f,Mode=%s,GridMaxDistanceCm=%.1f)"),
			LinksDebug::ModeName(V.Mode), V.MaxDistanceCm, V.GridMaxDistanceCm, V.Neighbours, L ? LinksDebug::ModeName(L->LinkMode) : TEXT("-"), L ? L->LinkMaxDistance : -1.f, L ? L->LinkNeighbours : -1,
			V.Neighbours, V.MaxDistanceCm, V.PixelWidth, V.Intensity, V.MaxNormalAngle, V.MaxLinksPerScan, V.LiftTexels, V.LiftMaxCm, V.Mode == ELidarLinkMode::Grid ? TEXT("Grid") : TEXT("Nearest"), V.GridMaxDistanceCm);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksModeCheckCommand(
	TEXT("tls.Links.ModeCheck"),
	TEXT("tls.Links.ModeCheck [Quit=0]: (fresh PIE, in TestLidar) the switch between the two modes: a scan in the nearest mode, tls.Links.Mode grid (the values reach the lidar), a scan on the grid (the grid mesh) while the first keeps its lines, back to nearest, then lines off in each mode: nothing searched or drawn."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartModeCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksModeShotsCommand(
	TEXT("tls.Links.ModeShots"),
	TEXT("tls.Links.ModeShots <Label> [SecondGridCm=0] [Quit=0] [Pose=here]: (PIE or a package, any map) from the current pose or a fixed one (corridor, corridorwall, floorhall: from the area's plan; ship, shipoblique: Prologue_Arrival): a scan in the nearest mode, one in the grid mode at the settings' distance and, with SecondGridCm, one at that distance, each shot at full resolution once its lines have grown, with its link counts and links per point by depth. Shots to Saved/Screenshots/Brief23."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartModeShots(Args.Num() > 0 ? Args[0] : FString(TEXT("Here")), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f, Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0,
			Args.Num() > 3 ? Args[3].ToLower() : FString());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLinksGridSpanCommand(
	TEXT("tls.Links.GridSpan"),
	TEXT("tls.Links.GridSpan [MaxDistanceCm=80] [KeepToM=20] [BinM=2]: for the last searched scan's samples (either mode), on the floor (within 10 cm of the pawn's floor) by horizontal distance from the capture: the length between a sample and the next texel's sample along its row and along its column (adjacent texels: past the near thinning every texel has its sample), the distance from which the median step between two rows passes MaxDistanceCm, and the 90th percentile of those steps at KeepToM (the longest link that keeps nine in ten of them there and nearer)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksDebug;
		const ULidarScanComponent* L = Lidar();
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		TArray<FVector> Positions;
		TArray<int32> Channels;
		int32 Width = 0;
		FLidarScanContext Context;
		if (!L || !Character || !L->GetLastScanSamples(Positions, Channels, Width, Context) || Width <= 0)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Links.GridSpan: no searched scan or no pawn."));
			return;
		}
		const double MaxCm = Args.Num() > 0 ? FMath::Clamp(FCString::Atod(*Args[0]), 1.0, 1000.0) : 80.0;
		const double KeepTo = (Args.Num() > 1 ? FMath::Clamp(FCString::Atod(*Args[1]), 1.0, 500.0) : 20.0) * 100.0;
		const double Bin = (Args.Num() > 2 ? FMath::Clamp(FCString::Atod(*Args[2]), 0.5, 50.0) : 2.0) * 100.0;
		const double FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const int32 Height = Positions.Num() / Width;
		auto OnFloor = [&](int32 T) { return Channels[T] >= 0 && FMath::Abs(Positions[T].Z - FloorZ) < 10.0; };
		auto Horizontal = [&](int32 T) { return FVector::Dist2D(Positions[T], Context.CaptureOrigin); };
		TMap<int32, TArray<double>> Rows, Columns;
		TArray<double> AtKeep;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 T = Y * Width + X;
				if (!OnFloor(T))
				{
					continue;
				}
				const double D = Horizontal(T);
				const int32 Key = FMath::FloorToInt32(D / Bin);
				if (X + 1 < Width && OnFloor(T + 1))
				{
					Rows.FindOrAdd(Key).Add(FVector::Dist(Positions[T], Positions[T + 1]));
				}
				if (Y + 1 < Height && OnFloor(T + Width))
				{
					const double Step = FVector::Dist(Positions[T], Positions[T + Width]);
					Columns.FindOrAdd(Key).Add(Step);
					if (FMath::Abs(D - KeepTo) <= 100.0)
					{
						AtKeep.Add(Step);
					}
				}
			}
		}
		TArray<int32> Keys;
		Columns.GetKeys(Keys);
		Keys.Sort();
		FString Table;
		double PassFrom = -1.0;
		for (const int32 Key : Keys)
		{
			const TArray<double>& Col = Columns[Key];
			const TArray<double>* Row = Rows.Find(Key);
			const double Med = Percentile(Col, 0.5);
			Table += FString::Printf(TEXT(" [%.0f-%.0f m: %d, between rows median %.1f cm p90 %.1f, along a row median %.1f]"), Key * Bin / 100.0, (Key + 1) * Bin / 100.0, Col.Num(),
				Med, Percentile(Col, 0.9), Row ? Percentile(*Row, 0.5) : 0.0);
			if (PassFrom < 0.0 && Col.Num() >= 20 && Med > MaxCm)
			{
				PassFrom = Key * Bin / 100.0;
			}
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.GridSpan: scan %d, capture %.0f cm above the floor: the median step between two rows of the floor passes %.0f cm from %s; at %.0f m (+-1 m, %d steps) the steps between rows are median %.1f cm, p90 %.1f cm.%s"),
			Context.ScanIndex, Context.CaptureOrigin.Z - FloorZ, MaxCm, PassFrom >= 0.0 ? *FString::Printf(TEXT("%.0f m"), PassFrom) : TEXT("nowhere in range"),
			KeepTo / 100.0, AtKeep.Num(), Percentile(AtKeep, 0.5), Percentile(AtKeep, 0.9), *Table);
	}));

// =====================================================================================================
// Brief 25: scan memory by density. tls.MemoryTest (TestLidar, PIE), tls.Memory.Set, tls.Memory.Report, tls.Memory.Shots.
// =====================================================================================================

namespace LinksDebug
{
	/** One case of tls.MemoryTest: an older scan, a newer scan, the older scan's samples and what the rule and the GPU say. */
	struct FMemCase
	{
		FString Label;
		int32 Older = INDEX_NONE;
		int32 Newer = INDEX_NONE;
		FLidarMemoryCapture OlderCapture, NewerCapture;
		TArray<FVector> Positions, Normals;
		TArray<int32> Channels;
		TArray<FLidarLink> Links;
		TArray<float> Depth;
		float Ratio = 0.f;
		float Tolerance = 0.f;
		int32 PointsBefore = 0, LinesBefore = 0, InjectedBefore = 0;
		int32 PointsAfter = 0, LinesAfter = 0, InjectedAfter = 0;
		int32 NewerPoints = 0, NewerLines = 0, NewerLinkPoints = 0, NewerLinks = 0;
		// By the plugin's reference rule (ULidarScanComponent::MemoryRemovesPoint) on the read-back samples.
		int32 Live = 0, Inside = 0, InsideRemoved = 0, Outside = 0, OutsideRemoved = 0, Removed = 0, LinksKept = 0;
		TArray<uint8> RemovedMask, InsideMask;
		/** Planar depth of each sample from the newer capture (its position before the bias), -1 where none. */
		TArray<float> NewerDepthOf;
	};
	static FMemCase GCase;
	static double GScanIssuedAt = 0.0;

	/** The sample before its surface bias pulled it toward its own capture: where the rule tests it. */
	static FVector UnbiasedSample(const FVector& S, const FLidarMemoryCapture& Own)
	{
		const FLidarScanContext& O = Own.Context;
		const double D = FVector::DotProduct(S - O.CaptureOrigin, O.CaptureForward);
		return D > 0.0 ? O.CaptureOrigin + (S - O.CaptureOrigin) * ((D + Own.SurfaceBias) / D) : S;
	}

	static void EvaluateCase()
	{
		FMemCase& C = GCase;
		const int32 N = FMath::Min(C.Positions.Num(), C.Channels.Num());
		C.RemovedMask.SetNumZeroed(N);
		C.InsideMask.SetNumZeroed(N);
		C.NewerDepthOf.Init(-1.f, N);
		for (int32 I = 0; I < N; ++I)
		{
			if (C.Channels[I] < 0)
			{
				continue;
			}
			++C.Live;
			double D = 0.0;
			const bool bInside = C.NewerCapture.Context.IsInCoverage(UnbiasedSample(C.Positions[I], C.OlderCapture), D);
			C.NewerDepthOf[I] = bInside ? static_cast<float>(D) : -1.f;
			const bool bRemoved = C.Normals.IsValidIndex(I)
				&& ULidarScanComponent::MemoryRemovesPoint(C.Positions[I], C.Normals[I], C.OlderCapture, C.NewerCapture, C.Depth, C.Ratio, C.Tolerance);
			C.InsideMask[I] = bInside ? 1 : 0;
			C.RemovedMask[I] = bRemoved ? 1 : 0;
			C.Inside += bInside ? 1 : 0;
			C.Outside += bInside ? 0 : 1;
			C.InsideRemoved += (bInside && bRemoved) ? 1 : 0;
			C.OutsideRemoved += (!bInside && bRemoved) ? 1 : 0;
			C.Removed += bRemoved ? 1 : 0;
		}
		for (const FLidarLink& Link : C.Links)
		{
			const bool bA = C.RemovedMask.IsValidIndex(Link.StartTexel) && C.RemovedMask[Link.StartTexel];
			const bool bB = C.RemovedMask.IsValidIndex(Link.EndTexel) && C.RemovedMask[Link.EndTexel];
			C.LinksKept += (!bA && !bB) ? 1 : 0;
		}
	}

	static double Pct(int32 A, int32 B) { return B > 0 ? 100.0 * A / B : 0.0; }

	/** The GPU did what the rule says: the older scan's points and lines alive after the newer scan, within a few particles. */
	static void CheckGpuMatchesRule(const FMemCase& C)
	{
		const int32 WantPoints = C.Live - C.Removed;
		const int32 PointSlack = FMath::Max(5, FMath::CeilToInt32(0.003 * C.Live));
		Check(C.PointsBefore == C.Live, FString::Printf(TEXT("%s: the older scan's points alive before the newer scan are its read-back samples (%d / %d)"), *C.Label, C.PointsBefore, C.Live));
		Check(FMath::Abs(C.PointsAfter - WantPoints) <= PointSlack,
			FString::Printf(TEXT("%s: the GPU keeps the points the rule keeps (%d alive, the rule %d, slack %d)"), *C.Label, C.PointsAfter, WantPoints, PointSlack));
		const int32 LineSlack = FMath::Max(5, FMath::CeilToInt32(0.003 * C.Links.Num()));
		Check(FMath::Abs(C.LinesAfter - C.LinksKept) <= LineSlack,
			FString::Printf(TEXT("%s: no line alive with a removed point: the GPU keeps %d lines, the links with both points kept are %d (of %d, slack %d)"), *C.Label, C.LinesAfter, C.LinksKept,
				C.Links.Num(), LineSlack));
		Check(C.NewerPoints == C.NewerLinkPoints && C.NewerLines == C.NewerLinks,
			FString::Printf(TEXT("%s: the newer scan is untouched: %d points alive of its %d, %d lines of its %d links"), *C.Label, C.NewerPoints, C.NewerLinkPoints, C.NewerLines, C.NewerLinks));
	}

	static void LogCase(const FMemCase& C)
	{
		Note(FString::Printf(TEXT("%s: ratio %.1f, tolerance %.1f cm; scan %d had %d points (GPU %d) and %d links (GPU %d lines); scan %d: %d of them inside its coverage, the rule removes %d (%.2f%%); %d outside, removed %d; GPU after: %d points (the rule %d), %d lines (the rule %d); the newer scan %d points, %d links"),
			*C.Label, C.Ratio, C.Tolerance, C.Older, C.Live, C.PointsBefore, C.Links.Num(), C.LinesBefore, C.Newer, C.Inside, C.InsideRemoved, Pct(C.InsideRemoved, C.Inside), C.Outside,
			C.OutsideRemoved, C.PointsAfter, C.Live - C.Removed, C.LinesAfter, C.LinksKept, C.NewerPoints, C.NewerLinks));
	}

	/**
	 * Adds a case: Resume, the ratio, stand at First, Stop (the automatic scan, its links), record its samples, then stand
	 * where Second says, scan, wait for its links and measure.
	 */
	static void AddMemCase(TArray<FStep>& S, const FString& Label, float Ratio, TFunction<void(FVector&, FVector&)> First, TFunction<void(FVector&, FVector&)> Second,
		TFunction<void(const FMemCase&)> Then)
	{
		S.Add(Do(Label + TEXT(": ratio, Resume, pose"), [Ratio, First]()
		{
			SetMemoryRatio(Ratio);
			ResumeTime();
			KeepEntityDormant();
			FVector Floor, Look;
			First(Floor, Look);
			StandAt(Floor, Look);
			return 0.8f;
		}));
		S.Add(Do(Label + TEXT(": Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(Label + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f));
		S.Add(WaitFor(Label + TEXT(": its links reach the point system"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return 0.4f; }));
		S.Add(Do(Label + TEXT(": record the older scan, second pose"), [Label, Ratio, Second]()
		{
			ULidarScanComponent* L = Lidar();
			GCase = FMemCase();
			GCase.Label = Label;
			GCase.Older = NewestScan();
			int32 Width = 0;
			FLidarScanContext Context;
			TArray<float> Depths;
			TArray<int32> Degrees;
			L->GetLastScanSamples(GCase.Positions, GCase.Channels, Width, Context);
			L->GetLastScanSampleNormals(GCase.Normals);
			L->GetLastScanLinks(GCase.Links, Depths, Degrees, Width);
			L->GetScanMemoryCapture(GCase.Older, GCase.OlderCapture);
			const FLidarScanSlotStats Slot = SlotOf(GCase.Older);
			GCase.PointsBefore = Slot.CapturedParticles;
			GCase.LinesBefore = Slot.LineParticles;
			GCase.InjectedBefore = Slot.InjectedParticles;
			FVector Floor, Look;
			Second(Floor, Look);
			StandAt(Floor, Look);
			return 0.4f;
		}));
		S.Add(Do(Label + TEXT(": the newer scan"), []()
		{
			KeepEntityDormant();
			GScanIssuedAt = Real();
			Lidar()->Scan();
			return 0.f;
		}));
		// A scan with no point (everything nearer than MinSampleDepth) may get no search result: it is waited for 1.5 s at most.
		S.Add(WaitFor(Label + TEXT(": the newer scan is taken"), []()
		{
			const ULidarScanComponent* L = Lidar();
			return NewestScan() != GCase.Older && (L->GetLastLinkStats().ScanIndex == NewestScan() || Real() - GScanIssuedAt > 1.5);
		}, 4.f, []() { return 0.6f; }));
		S.Add(Do(Label + TEXT(": measure"), [Then]()
		{
			ULidarScanComponent* L = Lidar();
			FMemCase& C = GCase;
			C.Newer = NewestScan();
			C.Ratio = L->MemoryRatio;
			C.Tolerance = L->MemoryTolerance;
			int32 W = 0, H = 0;
			FLidarScanContext Context;
			L->ReadNewestScanDepth(C.Depth, W, H, Context);
			L->GetScanMemoryCapture(C.Newer, C.NewerCapture);
			const FLidarScanSlotStats Old = SlotOf(C.Older);
			C.PointsAfter = Old.CapturedParticles;
			C.LinesAfter = Old.LineParticles;
			C.InjectedAfter = Old.InjectedParticles;
			const FLidarScanSlotStats New = SlotOf(C.Newer);
			C.NewerPoints = New.CapturedParticles;
			C.NewerLines = New.LineParticles;
			const FLidarLinkStats Stats = L->GetLastLinkStats();
			C.NewerLinkPoints = Stats.ScanIndex == C.Newer ? Stats.Points : -1;
			C.NewerLinks = Stats.ScanIndex == C.Newer ? Stats.Links : -1;
			EvaluateCase();
			LogCase(C);
			if (Then)
			{
				Then(C);
			}
			return 0.f;
		}));
	}

	/** Count and removed count of the older scan's points that Pick selects. */
	static void CountWhere(const FMemCase& C, TFunction<bool(int32)> Pick, int32& OutCount, int32& OutRemoved)
	{
		OutCount = 0;
		OutRemoved = 0;
		for (int32 I = 0; I < C.RemovedMask.Num(); ++I)
		{
			if (C.Channels[I] >= 0 && Pick(I))
			{
				++OutCount;
				OutRemoved += C.RemovedMask[I];
			}
		}
	}

	static TStrongObjectPtr<UTlsLidarTestSource> GMemSource;

	static void StartMemoryTest(bool bQuit)
	{
		StartRun(TEXT("MemoryTest"), bQuit);
		GRun.Cleanup = []()
		{
			if (GMemSource.IsValid())
			{
				if (ULidarScanComponent* L = Lidar())
				{
					L->UnregisterPointSource(TScriptInterface<ILidarPointSource>(GMemSource.Get()));
				}
				GMemSource.Reset();
			}
			CommonCleanup();
			ResumeTime();
		};
		const float GameRatio = FMath::Max(UTheLastShiftSettings::GetScanMemory().Ratio, 0.f);
		const float Ratio = GameRatio > 0.f ? GameRatio : 2.f;
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 30.f, [Ratio]()
		{
			const ULidarScanComponent* L = Lidar();
			Note(FString::Printf(TEXT("settings %s; the test's ratio %.1f; lidar: MinSampleDepth %.0f cm, NearThinningDistance %.0f cm, SampleJitter %.2f, SurfaceBias %.1f cm, capture %d, %d slots, links %d neighbours, %s"),
				*MemoryIniLine(UTheLastShiftSettings::GetScanMemory()), Ratio, L->MinSampleDepth, L->NearThinningDistance, L->SampleJitter, L->SurfaceBias, L->CaptureResolution, L->MaxAliveScans,
				L->LinkNeighbours, ModeName(L->LinkMode)));
			return 0.f;
		}));

		auto Fixed = [](FVector Floor, FVector Look) { return [Floor, Look](FVector& F, FVector& L) { F = Floor; L = Look; }; };
		const auto MainWest = Fixed(Poses::MainRoom, Poses::WestWall);

		// --- A: two scans from one pose (nearest mode, then grid mode), and from the same place turned ---
		for (const ELidarLinkMode Mode : { ELidarLinkMode::Nearest, ELidarLinkMode::Grid })
		{
			const FString Label = FString::Printf(TEXT("A_SamePose_%s"), ModeName(Mode));
			S.Add(Do(Label + TEXT(": link mode"), [Mode]()
			{
				FScanLinks Values = GSavedLinks;
				Values.Mode = Mode;
				Values.Neighbours = FMath::Max(Values.Neighbours, 2);
				PushLinks(Values);
				return 0.f;
			}));
			AddMemCase(S, Label, Ratio, MainWest, MainWest, [](const FMemCase& C)
			{
				Check(C.Inside > 5000 && Pct(C.InsideRemoved, C.Inside) >= 98.0,
					FString::Printf(TEXT("%s: at least 98%% of the older scan's points inside the newer scan's coverage are removed (%d of %d, %.2f%%)"), *C.Label, C.InsideRemoved, C.Inside, Pct(C.InsideRemoved, C.Inside)));
				CheckGpuMatchesRule(C);
			});
		}
		S.Add(Do(TEXT("A: the settings' link mode back"), []() { PushLinks(GSavedLinks); return 0.f; }));
		AddMemCase(S, TEXT("A_Turned40"), Ratio, MainWest,
			[](FVector& F, FVector& L) { F = Poses::MainRoom; L = Poses::MainRoom + FRotator(0.f, 180.f + 40.f, 0.f).Vector() * 1000.f + FVector(0.f, 0.f, 120.f); },
			[](const FMemCase& C)
			{
				Check(C.Outside > 1000 && C.OutsideRemoved == 0,
					FString::Printf(TEXT("%s: every point of the older scan outside the newer scan's coverage is kept (%d outside, the rule removes %d; the GPU agrees below)"), *C.Label, C.Outside, C.OutsideRemoved));
				Check(C.Inside > 3000 && Pct(C.InsideRemoved, C.Inside) >= 98.0,
					FString::Printf(TEXT("%s: at least 98%% of the points inside are removed (%d of %d, %.2f%%)"), *C.Label, C.InsideRemoved, C.Inside, Pct(C.InsideRemoved, C.Inside)));
				CheckGpuMatchesRule(C);
			});

		// --- B: a wall between the newer scan and the older points: the corridor's east wall, scanned from the east room
		// (its far face from the corridor and the room behind it), then from the corridor facing it ---
		AddMemCase(S, TEXT("B_WallBetween"), Ratio, Fixed(FVector(800.f, 300.f, 0.f), FVector(200.f, 300.f, 120.f)), Fixed(Poses::CorridorFacing, Poses::CorridorWallFace),
			[](const FMemCase& C)
			{
				int32 Far = 0, FarRemoved = 0;
				CountWhere(C, [&C](int32 I) { return C.Normals[I].X > 0.7 && C.Positions[I].X < 400.0 && C.Positions[I].X > 150.0; }, Far, FarRemoved);
				Check(C.Live > 5000 && C.Removed == 0 && FMath::Abs(C.PointsAfter - C.PointsBefore) <= 5,
					FString::Printf(TEXT("%s: every older point behind the wall is kept (%d points, %d inside the newer coverage, the rule removes %d; GPU %d before, %d after)"), *C.Label, C.Live, C.Inside,
						C.Removed, C.PointsBefore, C.PointsAfter));
				Check(Far > 200 && FarRemoved == 0, FString::Printf(TEXT("%s: every point on the wall's far face is kept (%d, removed %d)"), *C.Label, Far, FarRemoved));
				CheckGpuMatchesRule(C);
			});

		// --- C: the corridor's floor at a grazing angle, scanned twice from one pose ---
		const auto Grazing = Fixed(FVector(0.f, 1300.f, 0.f), FVector(0.f, -600.f, 20.f));
		AddMemCase(S, TEXT("C_GrazingFloor"), Ratio, Grazing, Grazing, [](const FMemCase& C)
		{
			const double FloorZ = GStandZ - 90.0;
			const FVector Eye = C.OlderCapture.Context.CaptureOrigin;
			double MinZ = 1.e30;
			for (int32 I = 0; I < C.Positions.Num(); ++I)
			{
				if (C.Channels[I] >= 0 && C.Normals[I].Z > 0.9)
				{
					MinZ = FMath::Min(MinZ, C.Positions[I].Z);
				}
			}
			int32 Floor = 0, FloorRemoved = 0;
			CountWhere(C, [&C, MinZ, Eye](int32 I) { return C.Normals[I].Z > 0.7 && C.Positions[I].Z < MinZ + 15.0 && FVector::Dist2D(C.Positions[I], Eye) <= 2000.0; }, Floor, FloorRemoved);
			Note(FString::Printf(TEXT("%s: floor at Z %.0f (pawn standing Z %.0f), %d floor points to 20 m"), *C.Label, MinZ, FloorZ, Floor));
			Check(Floor > 1000 && Pct(FloorRemoved, Floor) >= 95.0,
				FString::Printf(TEXT("%s: at least 95%% of the older scan's floor points to 20 m are removed (%d of %d, %.2f%%)"), *C.Label, FloorRemoved, Floor, Pct(FloorRemoved, Floor)));
			CheckGpuMatchesRule(C);
		});

		// --- D: older points nearer than MinSampleDepth to the newer capture: the corridor's east wall scanned from 3 m, then
		// from as close to it as the pawn stands ---
		AddMemCase(S, TEXT("D_NearMinDepth"), Ratio, Fixed(Poses::CorridorFacing, Poses::CorridorWallFace),
			[](FVector& F, FVector& L)
			{
				const ACharacter* Ch = Cast<ACharacter>(Pawn());
				const double Radius = Ch && Ch->GetCapsuleComponent() ? Ch->GetCapsuleComponent()->GetScaledCapsuleRadius() : 34.0;
				F = FVector(Poses::CorridorWallFace.X - Radius - 3.0, Poses::CorridorWallFace.Y, 0.0);
				L = Poses::CorridorWallFace;
			},
			[](const FMemCase& C)
			{
				const float MinDepth = C.NewerCapture.MinSampleDepth;
				int32 Near = 0, NearRemoved = 0;
				CountWhere(C, [&C, MinDepth](int32 I) { return C.InsideMask[I] && C.NewerDepthOf[I] <= MinDepth; }, Near, NearRemoved);
				Check(Near > 100 && NearRemoved == 0,
					FString::Printf(TEXT("%s: the older points nearer than MinSampleDepth (%.0f cm) to the newer capture are kept (%d inside the coverage, removed %d)"), *C.Label, MinDepth, Near, NearRemoved));
				CheckGpuMatchesRule(C);
			});

		// --- E: the ratio: the main room's west wall scanned from 3 m, then from 4.5 m and from 9 m, at 2, the largest value and 0 ---
		for (const float R : { 2.f, 100000.f, 0.f })
		{
			for (const double K : { 1.5, 3.0 })
			{
				const FString Label = FString::Printf(TEXT("E_Ratio%s_%.1fx"), R >= 100000.f ? TEXT("Max") : *FString::Printf(TEXT("%.0f"), R), K);
				AddMemCase(S, Label, R, Fixed(FVector(-1200.f, 300.f, 0.f), Poses::WestWall), Fixed(FVector(-1500.f + 300.f * K, 300.f, 0.f), Poses::WestWall), [R, K](const FMemCase& C)
				{
					int32 Wall = 0, WallRemoved = 0;
					// The wall clear of the pillar (X -1350..-1150, Y 50..250) that hides its part at Y < 250 from the farther poses.
					CountWhere(C, [&C](int32 I) { return C.InsideMask[I] && FMath::Abs(C.Positions[I].X + 1500.0) < 30.0 && C.Normals[I].X > 0.7 && C.Positions[I].Y >= 300.0; }, Wall, WallRemoved);
					const double Share = Pct(WallRemoved, Wall);
					const bool bRemoves = R >= 100000.f || (R > 0.f && K < R);
					// Where the kept wall points are: by height (50 cm bins) and along the wall (1 m bins), and how far from the
					// wall's plane, with the share of each bin kept.
					TMap<int32, FIntPoint> ByZ, ByY;
					for (int32 I = 0; I < C.RemovedMask.Num(); ++I)
					{
						if (C.Channels[I] < 0 || !C.InsideMask[I] || FMath::Abs(C.Positions[I].X + 1500.0) >= 30.0 || C.Normals[I].X <= 0.7)
						{
							continue;
						}
						FIntPoint& Z = ByZ.FindOrAdd(FMath::FloorToInt32(C.Positions[I].Z / 50.0));
						FIntPoint& Y = ByY.FindOrAdd(FMath::FloorToInt32(C.Positions[I].Y / 100.0));
						Z.X += 1;
						Y.X += 1;
						Z.Y += C.RemovedMask[I] ? 0 : 1;
						Y.Y += C.RemovedMask[I] ? 0 : 1;
					}
					auto Table = [](TMap<int32, FIntPoint>& M, double Bin)
					{
						M.KeySort([](int32 A, int32 B) { return A < B; });
						FString Out;
						for (const TPair<int32, FIntPoint>& P : M)
						{
							Out += FString::Printf(TEXT(" [%.1f: %d kept of %d]"), P.Key * Bin / 100.0, P.Value.Y, P.Value.X);
						}
						return Out;
					};
					Note(FString::Printf(TEXT("%s: kept wall points by height (m):%s; along the wall (Y, m):%s"), *C.Label, *Table(ByZ, 50.0), *Table(ByY, 100.0)));
					if (R <= 0.f)
					{
						Check(Wall > 3000 && WallRemoved == 0 && FMath::Abs(C.PointsAfter - C.PointsBefore) <= 5,
							FString::Printf(TEXT("%s: at 0 the scan from %.1f times the distance keeps the older wall points (%d, removed %d; GPU %d before, %d after)"), *C.Label, K, Wall, WallRemoved, C.PointsBefore, C.PointsAfter));
					}
					else if (bRemoves)
					{
						Check(Wall > 3000 && Share >= 95.0, FString::Printf(TEXT("%s: the scan from %.1f times the distance removes the older wall points (%d of %d, %.2f%%)"), *C.Label, K, WallRemoved, Wall, Share));
					}
					else
					{
						Check(Wall > 3000 && Share <= 1.0, FString::Printf(TEXT("%s: the scan from %.1f times the distance keeps the older wall points (%d of %d removed, %.2f%%)"), *C.Label, K, WallRemoved, Wall, Share));
					}
					CheckGpuMatchesRule(C);
				});
			}
		}

		// --- F: injected points: a test source in view (scan fade), two scans from one pose at 0 and at the ratio ---
		static int32 GInjectedAt0 = -1;
		S.Add(Do(TEXT("F: a test point source in view"), []()
		{
			UTlsLidarTestSource* Source = NewObject<UTlsLidarTestSource>();
			Source->Center = Poses::MainRoom + (Poses::WestWall - Poses::MainRoom).GetSafeNormal() * 300.0 + FVector(0.0, 0.0, 120.0);
			Source->Size = 80.f;
			Source->PointCount = 400;
			Source->Settings.FadeMode = ELidarFadeMode::ScanFade;
			Source->Settings.bOccludeByCapture = false;
			GMemSource.Reset(Source);
			Lidar()->RegisterPointSource(TScriptInterface<ILidarPointSource>(Source), 0);
			return 0.f;
		}));
		AddMemCase(S, TEXT("F_Injected_Ratio0"), 0.f, MainWest, MainWest, [](const FMemCase& C) { GInjectedAt0 = C.InjectedAfter; });
		AddMemCase(S, TEXT("F_Injected"), Ratio, MainWest, MainWest, [](const FMemCase& C)
		{
			Check(C.InjectedBefore > 100 && C.InjectedAfter == C.InjectedBefore && C.InjectedAfter == GInjectedAt0,
				FString::Printf(TEXT("%s: the older scan's injected points alive are those with the ratio at 0 (%d before, %d after; %d at ratio 0)"), *C.Label, C.InjectedBefore, C.InjectedAfter, GInjectedAt0));
		});
		S.Add(Do(TEXT("F: the test source removed"), []()
		{
			if (GMemSource.IsValid())
			{
				Lidar()->UnregisterPointSource(TScriptInterface<ILidarPointSource>(GMemSource.Get()));
				GMemSource.Reset();
			}
			return 0.f;
		}));

		// --- G: Resume clears everything as today ---
		S.Add(Do(TEXT("G: Resume"), []() { ResumeTime(); return 1.2f; }));
		S.Add(Do(TEXT("G: nothing left"), []()
		{
			int32 Particles = 0, Active = 0;
			for (const FLidarScanSlotStats& Slot : Lidar()->GetScanSlotStats())
			{
				Particles += Slot.CapturedParticles + Slot.InjectedParticles + Slot.LineParticles;
				Active += Slot.ScanIndex != INDEX_NONE ? 1 : 0;
			}
			Check(AliveScans() == 0 && Active == 0 && Particles == 0, FString::Printf(TEXT("G: Resume clears every scan (%d alive, %d slots held, %d particles)"), AliveScans(), Active, Particles));
			return 0.f;
		}));

		// --- H: a reused slot removes what its scan had left: scan 0 facing north, then the cap's worth of scans facing south ---
		static int32 GFirstScan = INDEX_NONE, GFirstPoints = 0;
		S.Add(Do(TEXT("H: ratio, pose north"), [Ratio]()
		{
			SetMemoryRatio(Ratio);
			KeepEntityDormant();
			StandAt(Poses::MainRoom, Poses::MainRoom + FVector(0.0, 1000.0, 120.0));
			return 0.5f;
		}));
		S.Add(Do(TEXT("H: Stop"), []() { KeepEntityDormant(); StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("H: the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f));
		S.Add(WaitFor(TEXT("H: its links"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []()
		{
			GFirstScan = NewestScan();
			StandAt(Poses::MainRoom, Poses::MainRoom + FVector(0.0, -1000.0, 120.0));
			return 0.4f;
		}));
		S.Add(Do(TEXT("H: scan 0's points"), []() { GFirstPoints = SlotOf(GFirstScan).CapturedParticles; return 0.f; }));
		for (int32 I = 1; I < ULidarScanComponent::MaxAliveScansLimit; ++I)
		{
			S.Add(Do(FString::Printf(TEXT("H: south scan %d"), I), [I]()
			{
				// Only as many as the cap: the last one of the loop fills it, the next step reuses scan 0's slot.
				if (I < Lidar()->MaxAliveScans)
				{
					KeepEntityDormant();
					Lidar()->Scan();
				}
				return 0.f;
			}));
			S.Add(WaitFor(FString::Printf(TEXT("H: south scan %d's links"), I), [I]() { return I >= Lidar()->MaxAliveScans || LinksReadyFor(NewestScan()); }, 3.f, []() { return 0.1f; }));
		}
		S.Add(Do(TEXT("H: before the reuse"), []()
		{
			const FLidarScanSlotStats First = SlotOf(GFirstScan);
			Check(AliveScans() == Lidar()->MaxAliveScans && First.ScanIndex == GFirstScan && First.CapturedParticles >= GFirstPoints * 9 / 10 && GFirstPoints > 5000,
				FString::Printf(TEXT("H: with %d scans alive, scan %d (north) keeps its points, no newer scan covers them (%d of %d)"), AliveScans(), GFirstScan, First.CapturedParticles, GFirstPoints));
			KeepEntityDormant();
			Lidar()->Scan();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("H: the scan that reuses scan 0's slot"), []() { return LinksReadyFor(NewestScan()); }, 3.f, []() { return 0.5f; }));
		S.Add(Do(TEXT("H: after the reuse"), []()
		{
			const FLidarScanSlotStats Gone = SlotOf(GFirstScan);
			const FLidarScanSlotStats New = SlotOf(NewestScan());
			const FLidarLinkStats Stats = Lidar()->GetLastLinkStats();
			Check(Gone.ScanIndex == INDEX_NONE && New.CapturedParticles == Stats.Points && New.LineParticles == Stats.Links,
				FString::Printf(TEXT("H: the reused slot holds only the new scan: scan %d gone, the slot %d points of the scan's %d, %d lines of its %d links"), GFirstScan, New.CapturedParticles,
					Stats.Points, New.LineParticles, Stats.Links));
			return 0.f;
		}));
		Launch();
	}

	// --- tls.Memory.Shots: the views of criterion 7 at ratio 0, 2 and the largest value ---

	static FVector GCorridorStart = FVector::ZeroVector;
	static FVector GCorridorDir = FVector::ForwardVector;

	static FVector CorridorAt(double Along) { return GCorridorStart + GCorridorDir * Along; }

	static bool FindCorridor()
	{
		UWorld* World = W();
		const AAreaInfo* Info = AAreaInfo::Find(World);
		for (int32 I = 0; Info && I < Info->Segments.Num(); ++I)
		{
			if (Info->Segments[I].Type != EAreaSegmentType::Corridor)
			{
				continue;
			}
			int32 Unused = 0;
			const FVector A = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 150.f, Unused);
			const FVector B = Info->GetRoutePointAtDistance(Info->Segments[I].RouteStart + 1650.f, Unused);
			GCorridorStart = A;
			GCorridorDir = (B - A).GetSafeNormal2D();
			return true;
		}
		return false;
	}

	static void AddRatioShot(TArray<FStep>& S, const FString& Name)
	{
		S.Add(Do(Name + TEXT(": hold the age fade, lines on"), []() { HoldAgeFade(true); ShowLines(true); return 0.3f; }));
		AddShot(S, Name, [Name](const FShot& Shot)
		{
			int32 Older = 0;
			Note(FString::Printf(TEXT("%s: %s; %d lit pixels; points alive per scan:%s; shot Saved/Screenshots/Brief25/%s.png"), *Name, *MemoryIniLine(UTheLastShiftSettings::GetScanMemory()),
				CountLit(Shot).Pixels, *PointsPerScan(Older, NewestScan()), *Name));
			HoldAgeFade(false);
		});
	}

	static void StartMemoryShots(const FString& Scene, bool bQuit)
	{
		StartRun(TEXT("MemoryShots"), bQuit);
		GShotFolder = TEXT("Brief25");
		GRun.Cleanup = []() { CommonCleanup(); ResumeTime(); };
		const bool bHall = Scene.Equals(TEXT("hall"), ESearchCase::IgnoreCase) || Scene.Equals(TEXT("nearfar"), ESearchCase::IgnoreCase);
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar"), []() { return Lidar() && Pawn() && TS(); }, 30.f, [bHall]()
		{
			FullResolution(true);
			if (bHall)
			{
				PC()->ConsoleCommand(TEXT("tls.Beacon.View 0 middle"));
			}
			return 0.8f;
		}));
		if (bHall)
		{
			S.Add(Do(TEXT("the hall"), []() { FindHall(); return GHall.bValid ? 0.f : -1.f; }));
		}
		else
		{
			S.Add(Do(TEXT("the first corridor"), []() { return FindCorridor() ? 0.f : -1.f; }));
		}
		const float GameRatio = UTheLastShiftSettings::GetScanMemory().Ratio > 0.f ? UTheLastShiftSettings::GetScanMemory().Ratio : 2.f;
		for (const float R : { 0.f, GameRatio, 100000.f })
		{
			const FString Tag = R >= 100000.f ? FString(TEXT("RMax")) : FString::Printf(TEXT("R%.0f"), R);
			S.Add(Do(Tag + TEXT(": ratio"), [R]() { SetMemoryRatio(R); return 0.f; }));
			if (Scene.Equals(TEXT("hall"), ESearchCase::IgnoreCase))
			{
				// The hall's end wall after 16 scans from one pose (2 m in front of it), seen from 10 and 20 m.
				AddScanFrom(S, Tag + TEXT("_Hall16_Scan00"), []() { return HallFront(200.0); }, []() { return GHall.WallPoint; }, true);
				for (int32 I = 1; I < 16; ++I)
				{
					AddScanHere(S, FString::Printf(TEXT("%s_Hall16_Scan%02d"), *Tag, I));
				}
				for (const double D : { 1000.0, 2000.0 })
				{
					const FString Name = FString::Printf(TEXT("Hall16_%s_View%02.0fm"), *Tag, D / 100.0);
					S.Add(Do(Name + TEXT(": view"), [D]() { StandAt(HallFront(D), GHall.WallPoint); return 0.6f; }));
					AddRatioShot(S, Name);
				}
			}
			else if (Scene.Equals(TEXT("nearfar"), ESearchCase::IgnoreCase))
			{
				// The hall's end wall scanned from 2 m, then from 12 m, seen from 2 m.
				AddScanFrom(S, Tag + TEXT("_NearFar_Scan2m"), []() { return HallFront(200.0); }, []() { return GHall.WallPoint; }, true);
				S.Add(Do(Tag + TEXT("_NearFar: 12 m"), []() { KeepEntityDormant(); StandAt(HallFront(1200.0), GHall.WallPoint); return 0.4f; }));
				AddScanHere(S, Tag + TEXT("_NearFar_Scan12m"));
				const FString Name = FString::Printf(TEXT("NearFar_%s_View02m"), *Tag);
				S.Add(Do(Name + TEXT(": view"), []() { StandAt(HallFront(200.0), GHall.WallPoint); return 0.6f; }));
				AddRatioShot(S, Name);
			}
			else
			{
				// Area 1's first corridor: 6 scans walking forward (1.5 m in, then every 3 m), looking 15 m ahead; then from the
				// last pose looking back, and after one scan looking back.
				AddScanFrom(S, Tag + TEXT("_Corridor_Scan0"), []() { return CorridorAt(0.0); }, []() { return CorridorAt(1500.0) + FVector(0.0, 0.0, 160.0); }, true);
				for (int32 I = 1; I < 6; ++I)
				{
					S.Add(Do(FString::Printf(TEXT("%s_Corridor: step %d"), *Tag, I), [I]() { KeepEntityDormant(); StandAt(CorridorAt(300.0 * I), CorridorAt(300.0 * I + 1500.0) + FVector(0.0, 0.0, 160.0)); return 0.4f; }));
					AddScanHere(S, FString::Printf(TEXT("%s_Corridor_Scan%d"), *Tag, I));
				}
				const FString Back = FString::Printf(TEXT("Corridor6_%s_LookBack"), *Tag);
				S.Add(Do(Back + TEXT(": turn"), []() { StandAt(CorridorAt(1500.0), CorridorAt(-300.0) + FVector(0.0, 0.0, 160.0)); return 0.6f; }));
				AddRatioShot(S, Back);
				AddScanHere(S, Tag + TEXT("_Corridor_ScanBack"));
				S.Add(Do(Tag + TEXT("_Corridor: wait the growth"), []() { return WaitGrowthDone(); }));
				AddRatioShot(S, Back + TEXT("Scan"));
			}
		}
		Launch();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsMemoryTestCommand(
	TEXT("tls.MemoryTest"),
	TEXT("tls.MemoryTest [Quit=0]: (fresh PIE, in TestLidar) Brief 25, the scan memory at the game's ratio (2 when the settings have 0), against the plugin's reference rule on the read-back samples and the GPU's counts: A two scans from one pose (nearest and grid mode) and from the same place turned 40 degrees, B a wall between the newer scan and the older points, C the corridor floor at a grazing angle, D older points nearer than MinSampleDepth, E the ratio (a wall from 3 m, then from 1.5 and 3 times that, at 2, the largest value and 0), F injected points, G Resume, H a reused slot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartMemoryTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMemorySetCommand(
	TEXT("tls.Memory.Set"),
	TEXT("tls.Memory.Set <Ratio> [ToleranceCm]: sets ScanMemory in the settings for this session (not saved to the ini) and has the director push it; a scan taken after it uses it. 0 = off, 100000 = the newest scan replaces whatever it captures. No argument: logs the values."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace LinksDebug;
		FScanMemory& V = GetMutableDefault<UTheLastShiftSettings>()->ScanMemory;
		if (Args.Num() > 0)
		{
			V.Ratio = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 100000.f);
		}
		if (Args.Num() > 1)
		{
			V.ToleranceCm = FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 50.f);
		}
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanMemory();
		}
		const ULidarScanComponent* L = Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Memory.Set: ratio %.1f, tolerance %.1f cm; lidar %.1f / %.1f cm. Ini line: %s"), V.Ratio, V.ToleranceCm, L ? L->MemoryRatio : -1.f,
			L ? L->MemoryTolerance : -1.f, *MemoryIniLine(V));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMemoryReportCommand(
	TEXT("tls.Memory.Report"),
	TEXT("tls.Memory.Report: logs the scan memory's values (the settings and the lidar), the points and line particles alive for each scan, and what the last scan that tested older scans removed (their points and lines right before it and now)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		using namespace LinksDebug;
		const ULidarScanComponent* L = Lidar();
		if (!L)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Memory.Report: no player lidar."));
			return;
		}
		int32 Older = 0;
		const FString PerScan = PointsPerScan(Older, NewestScan());
		const FLidarMemoryStats M = L->GetLastMemoryStats();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Memory.Report: %s; lidar ratio %.1f, tolerance %.1f cm; %d scans alive, points (lines) per scan:%s"),
			*MemoryIniLine(UTheLastShiftSettings::GetScanMemory()), L->MemoryRatio, L->MemoryTolerance, L->GetAliveScanCount(), *PerScan);
		if (M.ScanIndex == INDEX_NONE)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Memory.Report: no scan has tested older scans yet."));
			return;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Memory.Report: scan %d (ratio %.1f, tolerance %.1f cm) tested %d older scans (%d still alive): it removed %d of their %d points and %d of their %d line particles (now %d points, %d lines)."),
			M.ScanIndex, M.Ratio, M.Tolerance, M.OlderScans, M.OlderScansAlive, M.PointsBefore - M.PointsNow, M.PointsBefore, M.LinesBefore - M.LinesNow, M.LinesBefore, M.PointsNow, M.LinesNow);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMemoryShotsCommand(
	TEXT("tls.Memory.Shots"),
	TEXT("tls.Memory.Shots <hall|nearfar|corridor> [Quit=0]: (PIE or a package) Brief 25, the same views at ratio 0, the game's ratio and the largest value, age fade held, lines on: hall (Area2_ServerHalls) the end wall after 16 scans from 2 m, seen from 10 and 20 m; nearfar (Area2) the wall scanned from 2 m then from 12 m, seen from 2 m; corridor (any area: Area1_Hull) 6 scans walking forward along the first corridor, then from the last pose looking back, and after one scan looking back. Shots to Saved/Screenshots/Brief25."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartMemoryShots(Args.Num() > 0 ? Args[0] : FString(TEXT("hall")), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0);
	}));


#endif
