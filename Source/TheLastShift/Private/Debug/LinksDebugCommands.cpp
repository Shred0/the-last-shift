// SPDX-License-Identifier: MIT

// Brief 13, neighbour lines between scan points: tls.LinksTest (TestLidar: the values pushed from the settings, the mesh of
// a scan, the cap, line of sight through a wall and past a wall's edge, lines on their own surface at 3 m, 10 m and at a
// grazing angle, growth and the pause menu, life with the scan's fade, Resume and the 17th scan, width, colour and
// brightness), tls.Links.ShipCheck (Prologue_Arrival), tls.Links.ViewCheck (any map: the current view measured with and
// without lines, geometry visible and hidden), tls.Links.Set, tls.Links.Report, tls.Links.ScanFrame and
// tls.Exposure.Report. Shots are measured by the commands themselves and saved to Saved/Screenshots/Brief13.
// Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"
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
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief13"), GShot.Label + TEXT(".png")));
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
		GShot.Label = Label;
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
		struct FBand { const TCHAR* Name; float Min; float Max; int32 Count[4] = { 0, 0, 0, 0 }; };
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
					++Band.Count[FMath::Min(Degrees[Texel], 3)];
				}
			}
		}
		for (const FBand& Band : Bands)
		{
			const int32 Total = Band.Count[0] + Band.Count[1] + Band.Count[2] + Band.Count[3];
			auto Share = [Total](int32 N) { return Total > 0 ? 100.0 * N / Total : 0.0; };
			UE_LOG(LogTheLastShift, Log, TEXT("%s %s: %d points, %.1f%% with 0 links, %.1f%% with 1, %.1f%% with 2, %.1f%% with more"),
				Prefix, Band.Name, Total, Share(Band.Count[0]), Share(Band.Count[1]), Share(Band.Count[2]), Share(Band.Count[3]));
		}
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
			Note(FString::Printf(TEXT("A: settings: %d neighbours, %.1f cm, %.2f px, intensity %.2f, angle %.1f deg, cap %d"),
				V.Neighbours, V.MaxDistanceCm, V.PixelWidth, V.Intensity, V.MaxNormalAngle, V.MaxLinksPerScan));
			Check(L && L->LinkNeighbours == V.Neighbours && FMath::IsNearlyEqual(L->LinkMaxDistance, V.MaxDistanceCm)
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
			Check(MaxDegree <= GSavedLinks.Neighbours && Stats.MaxDegree == MaxDegree, FString::Printf(TEXT("B: at most %d links on one point (%d)"), GSavedLinks.Neighbours, MaxDegree));
			Check(Longest <= GSavedLinks.MaxDistanceCm + 0.01f, FString::Printf(TEXT("B: the longest link is within %.1f cm (%.2f cm)"), GSavedLinks.MaxDistanceCm, Longest));
			Check(CrossChannel == 0, FString::Printf(TEXT("B: no link joins two channels (%d)"), CrossChannel));
			Check(NotThisScan == 0, FString::Printf(TEXT("B: every link joins two captured points of its own scan: none joins two scans or an injected point (%d ends elsewhere)"), NotThisScan));

			G.UncappedLinks = Stats.Links + Stats.Dropped;
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
			PushLinks(Values);
			Note(FString::Printf(TEXT("H: width scan with %d neighbours (settings %d)"), Values.Neighbours, GSavedLinks.Neighbours));
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

	// Prologue_Arrival: the ship is a 12 x 6 x 5 m box (X 300..1500, Y -300..300, walls 20 cm thick, open at X = 1500).
	namespace ShipPoses
	{
		// 4 m from the face: the scan covers the face and the floor strip at its foot, which the ship hides from every view.
		static const FVector OutsideRight(900.f, 700.f, 0.f);
		static const FVector RightWallOuter(900.f, 300.f, 250.f);
	}

	/** Screen rectangle of the right wall's outer face (X 300..1500, Y 320, Z 0..500) from the current view. */
	static FRect ShipRect(int32 Width, int32 Height)
	{
		FRect R;
		R.X0 = Width; R.Y0 = Height; R.X1 = 0; R.Y1 = 0;
		for (const float X : { 300.f, 1500.f }) for (const float Y : { 320.f }) for (const float Z : { 0.f, 500.f })
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
			Note(FString::Printf(TEXT("%s: eye at (%.0f, %.0f), %.1f m from the right wall's outer face"), *Label, Floor.X, Floor.Y, FVector::Dist2D(Floor, ShipPoses::RightWallOuter) / 100.f));
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
		AddStopAndScanAt(S, TEXT("Ship: a scan of the right side's outer face"), ShipPoses::OutsideRight, ShipPoses::RightWallOuter);
		S.Add(Do(TEXT("Ship: the samples read back"), []() { CheckSamplesOfLastScan(TEXT("Ship")); return 0.f; }));
		S.Add(Do(TEXT("Ship: wait for the growth"), []()
		{
			const FLidarLinkStats Stats = Lidar()->GetLastLinkStats();
			Note(FString::Printf(TEXT("Ship: scan %d, %d points, %d links"), Stats.ScanIndex, Stats.Points, Stats.Links));
			return WaitGrowthDone();
		}));
		AddShipView(S, TEXT("Ship_Inside_5m"), FVector(900.f, -200.f, 0.f), true);
		AddShipView(S, TEXT("Ship_BehindLeftWall_20m"), FVector(900.f, -1400.f, 0.f), false);
		AddShipView(S, TEXT("Ship_HangarFar_35m"), FVector(3900.f, -1450.f, 0.f), false);
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
	TEXT("tls.Links.ShipCheck [Quit=0]: (PIE or a package, in Prologue_Arrival) a scan of the ship's right side from outside, then views of that outer face from inside the ship (about 5 m), behind its left wall (about 20 m) and across the hangar (about 35 m): its points show, its lines show no pixel. Shots to Saved/Screenshots/Brief13."),
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
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Set: %d neighbours, %.1f cm, %.2f px, intensity %.2f, angle %.1f deg, cap %d; lidar %d / %.1f / %.2f / %.2f / %.1f / %d."),
			V.Neighbours, V.MaxDistanceCm, V.PixelWidth, V.Intensity, V.MaxNormalAngle, V.MaxLinksPerScan,
			L ? L->LinkNeighbours : -1, L ? L->LinkMaxDistance : -1.f, L ? L->LinePixelWidth : -1.f, L ? L->LineIntensity : -1.f, L ? L->LinkMaxNormalAngle : -1.f, L ? L->MaxLinksPerScan : -1);
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
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Links.Report: scan %d: %d points, %d links, %d dropped, at most %d per point, longest %.1f cm, search %.2f ms, ready %.3f s after the scan; %d searches pending."),
			S.ScanIndex, S.Points, S.Links, S.Dropped, S.MaxDegree, S.LongestLink, S.SearchMilliseconds, S.ReadySeconds, L->GetPendingLinkSearches());
		LinksDebug::LogDegreeBands(TEXT("tls.Links.Report:"));
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
		int32 Points = 0, Lines = 0;
		double PointP50 = 0, PointP90 = 0, LineP50 = 0, LineP90 = 0;
	};

	/** On-screen levels in Rect: points (lines off) and lines alone (lines on, where no point is lit). */
	static FLevels LevelsIn(const FViewResult& V, const FRect& Rect)
	{
		FLevels R;
		TArray<double> P, Ln;
		const int32 Width = V.VisibleLines.Width;
		for (int32 Index = 0; Index < V.VisibleNoLines.Pixels.Num() && Index < V.VisibleLines.Pixels.Num(); ++Index)
		{
			if (!Rect.Contains(Index % Width, Index / Width))
			{
				continue;
			}
			const int32 Off = Level(V.VisibleNoLines.Pixels[Index]);
			const int32 On = Level(V.VisibleLines.Pixels[Index]);
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
		R.LineP50 = Percentile(Ln, 0.5);
		R.LineP90 = Percentile(Ln, 0.9);
		return R;
	}

	/** The points' density compensation in Rect, from the newest scan's samples: medians of the depths, the raw term and the clamped factor. */
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
		TArray<double> Raw, Clamped, SampleDepths, ViewDepths;
		for (int32 Texel = 0; Texel < Positions.Num() && Texel < Channels.Num(); ++Texel)
		{
			FVector2D Screen;
			if (Channels[Texel] < 0 || !Project(Positions[Texel], Screen, Width, Height) || !Rect.Contains(FMath::FloorToInt32(Screen.X), FMath::FloorToInt32(Screen.Y)))
			{
				continue;
			}
			const double SampleDepth = FMath::Max(FVector::DotProduct(Positions[Texel] - Context.CaptureOrigin, Context.CaptureForward), 1.0);
			const double ViewDepthHere = FMath::Max(FVector::DotProduct(Positions[Texel] - ViewOrigin, ViewForward), 1.0);
			const double Term = FMath::Pow(SampleDepth / ViewDepthHere, static_cast<double>(L->DensityCompensation));
			Raw.Add(Term);
			Clamped.Add(FMath::Clamp(Term, static_cast<double>(L->DensityBoostMin), static_cast<double>(L->DensityBoostMax)));
			SampleDepths.Add(SampleDepth);
			ViewDepths.Add(ViewDepthHere);
		}
		return FString::Printf(TEXT("%d samples in the middle: sample depth %.0f cm, view depth %.0f cm, depth term %.4f, clamped to [%.3f, %.2f] %.4f (medians; angle term not included)"),
			Raw.Num(), Percentile(SampleDepths, 0.5), Percentile(ViewDepths, 0.5), Percentile(Raw, 0.5), L->DensityBoostMin, L->DensityBoostMax, Percentile(Clamped, 0.5));
	}

	static void AddDistanceViews(TArray<FStep>& S, const FString& Prefix, TFunction<FVector(double)> Floor, TFunction<FVector()> LookAt, const TArray<double>& Distances)
	{
		for (const double D : Distances)
		{
			const FString Label = FString::Printf(TEXT("%s_View%04.1fm"), *Prefix, D / 100.0);
			S.Add(Do(Label + TEXT(": view"), [Floor, LookAt, D]() { StandAt(Floor(D), LookAt()); return 0.6f; }));
			AddViewShots(S, Label, [Label](const FViewResult& V)
			{
				const FRect Rect = CenterRect(V.VisibleLines, 0.1);
				const FLevels Lv = LevelsIn(V, Rect);
				Note(FString::Printf(TEXT("%s: points %d pixels, median %.0f, p90 %.0f levels; lines alone %d pixels, median %.0f, p90 %.0f levels; %s"),
					*Label, Lv.Points, Lv.PointP50, Lv.PointP90, Lv.Lines, Lv.LineP50, Lv.LineP90, *CompensationIn(Rect, V.VisibleLines.Width, V.VisibleLines.Height)));
			});
		}
	}

	static const TCHAR* GLocalExposureNames[3] = { TEXT("r.LocalExposure"), TEXT("r.DefaultFeature.LocalExposure.HighlightContrastScale"), TEXT("r.DefaultFeature.LocalExposure.ShadowContrastScale") };
	static FString GSavedLocalExposure[3];

	static void StartDistanceCheck(const FString& Scenario, bool bOldLocalExposure, bool bQuit)
	{
		StartRun(TEXT("LinksDistanceCheck"), bQuit);
		GRun.Cleanup = [bOldLocalExposure]()
		{
			CommonCleanup();
			ResumeTime();
			for (int32 I = 0; bOldLocalExposure && I < 3; ++I)
			{
				if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(GLocalExposureNames[I]))
				{
					V->Set(*GSavedLocalExposure[I], ECVF_SetByConsole);
				}
			}
		};
		const bool bHall = Scenario.Equals(TEXT("hall"), ESearchCase::IgnoreCase);
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
			// Area 2: the hall's end wall scanned from 2 m, seen from 2, 5, 10 and 20 m.
			S.Add(Do(TEXT("the hall"), []() { FindHall(); return GHall.bValid ? 0.f : -1.f; }));
			auto Front = [](double D) { return FVector(GHall.WallPoint.X, GHall.WallPoint.Y, GHall.FloorZ) - GHall.Forward * D; };
			AddScanFrom(S, TEXT("Dist_Hall_Scan2m"), [Front]() { return Front(200.0); }, []() { return GHall.WallPoint; }, true);
			AddDistanceViews(S, TEXT("Dist_Hall"), Front, []() { return GHall.WallPoint; }, { 200.0, 500.0, 1000.0, 2000.0 });
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
	TEXT("tls.Links.DistanceCheck <hall|testlidar> [OldLocalExposure=0] [Quit=0]: (PIE or a package) one surface scanned from 2 m, seen from 2, 5, 10 and 20 m (hall, Area2_ServerHalls) or 12.5 m (testlidar, with a surface behind a wall too): on-screen levels of its points and of its lines in the middle of the frame, and the points' density compensation. OldLocalExposure=1 sets local exposure as before Step 1 for the run."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		LinksDebug::StartDistanceCheck(Args.Num() > 0 ? Args[0] : FString(TEXT("hall")), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0);
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

#endif
