// SPDX-License-Identifier: MIT

// Brief 12, scan points behind geometry: tls.OcclusionTest (TestLidar: off identical, through a wall at 0 / 0.2 / 0.4 / 1,
// from the scan's own pose at 3 m and 10 m, the pulse and its timing, the pause menu, the entity never through a wall,
// a residue through a wall, an interactable's channel colour through a door), tls.Occlusion.PoseShots (the scan's own
// pose at 0 and at the configured intensity, in any map), tls.Occlusion.Set (live values) and tls.FrameTime. Every shot
// is measured on its pixels by the test itself and saved to Saved/Screenshots/Brief12. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarPointSource.h"
#include "LidarScanComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/LidarTestSource.h"

#if !UE_BUILD_SHIPPING

#include "DynamicRHI.h"
#include "RenderTimer.h"

namespace OcclusionDebug
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
		bool bSaveShots = true;
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
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 12 test"));
		}
	}

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static double WorldNow() { const UWorld* World = W(); return World ? World->GetTimeSeconds() : 0.0; }

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static TArray<AResidue*> Residues()
	{
		TArray<AResidue*> Out;
		UWorld* World = W();
		for (TActorIterator<AResidue> It(World); World && It; ++It)
		{
			Out.Add(*It);
		}
		Out.Sort([](const AResidue& A, const AResidue& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	/** The pawn at a floor point (its current height kept), looking at LookAt. */
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

	// --- The occlusion values: the settings are their one owner; the test changes them there and the director pushes ---

	static FScanOcclusion GSavedOcclusion;

	static void SetOcclusion(float Intensity)
	{
		FScanOcclusion& Values = GetMutableDefault<UTheLastShiftSettings>()->ScanOcclusion;
		Values.OccludedIntensity = Intensity;
		Values.LinkSeconds = GSavedOcclusion.LinkSeconds;
		Values.FadeSeconds = GSavedOcclusion.FadeSeconds;
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanOcclusion();
		}
	}

	static float PulseEndSeconds()
	{
		return FMath::Clamp(GSavedOcclusion.LinkSeconds, 0.f, 10.f) + FMath::Clamp(GSavedOcclusion.FadeSeconds, 0.f, 10.f);
	}

	// --- Shots, measured on their pixels ---

	/** A colour class of scan points, as seen on screen after tonemapping (8-bit sRGB). */
	enum class EHue : uint8 { Any, Scan, Residue, Entity, Interactable, TestSource, InteractableDim };

	struct FShot
	{
		FString Label;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};
	static FShot GShot;
	static bool bShotReady = false;
	static FDelegateHandle GShotHandle;
	static FString GShotFolder = TEXT("Brief12");

	static bool IsHue(const FColor& C, EHue Hue)
	{
		const int32 R = C.R, G = C.G, B = C.B;
		const int32 Max = FMath::Max3(R, G, B);
		switch (Hue)
		{
		case EHue::Any: return Max > 6;
		case EHue::Scan: return B > 6 && G * 10 >= B * 6 && R * 10 < G * 8;
		// Bright residue points saturate towards white-pink, so only "red and blue above green" is required.
		case EHue::Residue: return B > 12 && R >= G + 8 && B >= G + 8;
		case EHue::Entity: return R > 12 && R * 10 > G * 25 && R * 10 > B * 25;
		case EHue::Interactable: return G > 12 && G * 10 > R * 18 && G * 10 > B * 14;
		case EHue::TestSource: return R > 20 && G > 20 && B * 10 < FMath::Min(R, G) * 5;
		// The interactables' hue down to dim pixels (Brief 13, check F with local exposure off): green at least 4 levels,
		// the same ratios to red and blue as Interactable.
		case EHue::InteractableDim: return G >= 4 && G * 10 > R * 18 && G * 10 > B * 14;
		}
		return false;
	}

	struct FShotStats
	{
		int32 Lit = 0;
		double Sum = 0.0;
		int32 Peak = 0;
	};

	/** Pixels of Hue inside the rectangle (fractions of the frame), their summed max channel and the brightest one. */
	static FShotStats Measure(const FShot& Shot, EHue Hue, const FBox2D& Rect = FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0)))
	{
		FShotStats S;
		if (Shot.Pixels.Num() < Shot.Width * Shot.Height)
		{
			return S;
		}
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(Rect.Min.X * Shot.Width), 0, Shot.Width);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt32(Rect.Max.X * Shot.Width), 0, Shot.Width);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(Rect.Min.Y * Shot.Height), 0, Shot.Height);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(Rect.Max.Y * Shot.Height), 0, Shot.Height);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const FColor& C = Shot.Pixels[Y * Shot.Width + X];
				if (IsHue(C, Hue))
				{
					const int32 Max = FMath::Max3(C.R, C.G, C.B);
					++S.Lit;
					S.Sum += Max;
					S.Peak = FMath::Max(S.Peak, Max);
				}
			}
		}
		return S;
	}

	static FString Describe(const FShotStats& S)
	{
		return FString::Printf(TEXT("%d lit pixels, sum %.0f, peak %d"), S.Lit, S.Sum, S.Peak);
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		if (GRun.bSaveShots)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), GShotFolder, GShot.Label + TEXT(".png")));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s saved (%d x %d): %s"), *GShot.Label, Width, Height, *Path));
		}
		bShotReady = true;
	}

	/** Requests a shot of the game viewport (no UI); WaitShot() waits for its pixels. */
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

	/** Adds the steps of one shot: request it, wait for its pixels, then hand them to Then. */
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

	/**
	 * Two shots of the same frame content compared pixel by pixel: Darker counts the pixels lit in A that are dimmer in B
	 * by more than Tol levels (points wrongly attenuated if B is the occluded display), Brighter the opposite.
	 */
	static void Compare(const FShot& A, const FShot& B, int32 Tol, int32& OutDarker, int32& OutBrighter, int32& OutLitA)
	{
		OutDarker = OutBrighter = OutLitA = 0;
		if (A.Width != B.Width || A.Height != B.Height || A.Pixels.Num() != B.Pixels.Num())
		{
			OutDarker = OutBrighter = -1;
			return;
		}
		for (int32 Index = 0; Index < A.Pixels.Num(); ++Index)
		{
			const int32 MA = FMath::Max3(A.Pixels[Index].R, A.Pixels[Index].G, A.Pixels[Index].B);
			const int32 MB = FMath::Max3(B.Pixels[Index].R, B.Pixels[Index].G, B.Pixels[Index].B);
			OutLitA += MA > 6 ? 1 : 0;
			OutDarker += (MA > 6 && MB < MA - Tol) ? 1 : 0;
			OutBrighter += (MB > MA + Tol) ? 1 : 0;
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

	static void StartRun(const FString& Name, bool bQuit, bool bShots)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuit = bQuit;
		GRun.bSaveShots = bShots;
		GSavedOcclusion = UTheLastShiftSettings::GetScanOcclusion();
	}

	// --- Lines off for the run (Brief 13): these tests measure the points, and lines uncovered by hiding the geometry would
	// count as points seen through it. The settings are the lines' one owner; the director pushes them before every scan. ---

	static FScanLinks GSavedLinks;
	static bool bLinksOffForRun = false;

	static void LinesOffForRun()
	{
		FScanLinks& Links = GetMutableDefault<UTheLastShiftSettings>()->ScanLinks;
		GSavedLinks = Links;
		bLinksOffForRun = true;
		Links.Neighbours = 0;
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanLinks();
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  lines off for this run (%d neighbours in the settings, put back at the end)"), *GRun.Name, GSavedLinks.Neighbours);
	}

	static void RestoreLines()
	{
		if (!bLinksOffForRun)
		{
			return;
		}
		GetMutableDefault<UTheLastShiftSettings>()->ScanLinks = GSavedLinks;
		bLinksOffForRun = false;
		if (UScanDirectorComponent* D = Director())
		{
			D->ApplyScanLinks();
		}
	}

	static void Launch()
	{
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started."), *GRun.Name);
	}

	// =====================================================================================================
	// tls.OcclusionTest
	// =====================================================================================================

	// TestLidar: a 30 x 30 m hall split by two walls at X = -200 and X = 200 into the main room (X < -200), a 4 m corridor
	// and the east room. The corridor opens on the main room at Y 1100..1500 (the door) and on the east room at Y -1500..-1100.
	namespace Poses
	{
		static const FVector CorridorEntrance(0.f, 1250.f, 0.f);
		static const FVector CorridorFar(0.f, -1400.f, 120.f);
		static const FVector MainRoom(-850.f, 300.f, 0.f);
		static const FVector CorridorMiddle(0.f, 300.f, 120.f);
		static const FVector WestWall(-1500.f, 300.f, 120.f);
		static const FVector DoorEast(100.f, 1300.f, 0.f);
		static const FVector DoorWest(-800.f, 1300.f, 0.f);
		static const FVector Door(-200.f, 1300.f, 150.f);
	}

	struct FState
	{
		float Intensity = 0.2f;
		float EntitySpeed = 66.6667f;
		bool bEntitySpeedSaved = false;
		TWeakObjectPtr<AResidue> MovedResidue;
		FVector ResidueHome = FVector::ZeroVector;
		bool bVerifySaved = false;
		// Shots kept for comparisons.
		FShot OffShot;
		TMap<FString, FShotStats> Stats;
		// Pulse timing.
		double PulseScanTime = 0.0;
		double LinkEnd = -1.0;
		double FadeEnd = -1.0;
		bool bMidOk = true;
		double PauseFactor = -1.0;
		double PauseWorld = 0.0;
		// Entity.
		FVector EntityFloor = FVector::ZeroVector;
		FVector EntityFigure = FVector::ZeroVector;
		FVector HiddenFloor = FVector::ZeroVector;
		FVector SeeingFloor = FVector::ZeroVector;
		double EntityScanTime = 0.0;
		int32 EntityScanIndex = INDEX_NONE;
		// Plugin check with a field-off source.
		TWeakObjectPtr<UTlsLidarTestSource> TestSource;
		TStrongObjectPtr<UTlsLidarTestSource> TestSourceRoot;
	};
	static FState G;

	static int32 AliveScans() { const ULidarScanComponent* L = Lidar(); return L ? L->GetAliveScanCount() : 0; }
	static int32 ScanCount() { const ULidarScanComponent* L = Lidar(); return L ? L->GetScanCount() : 0; }

	static FString SlotSummary()
	{
		const ULidarScanComponent* L = Lidar();
		if (!L)
		{
			return TEXT("no lidar");
		}
		int32 Points = 0;
		int32 Injected = 0;
		for (const FLidarScanSlotStats& S : L->GetScanSlotStats())
		{
			Points += S.CapturedParticles;
			Injected += S.InjectedParticles;
		}
		return FString::Printf(TEXT("%d alive scans, %d captured and %d injected particles"), L->GetAliveScanCount(), Points, Injected);
	}

	/** Adds: Resume (scans cleared), occlusion at Intensity, stand at Floor looking at LookAt, Stop, wait for the automatic scan. */
	static void AddStopAndScanAt(TArray<FStep>& S, const FString& Label, const FVector& Floor, const FVector& LookAt, float Intensity)
	{
		S.Add(Do(Label + TEXT(": Resume, pose"), [Floor, LookAt, Intensity]()
		{
			ResumeTime();
			KeepEntityDormant();
			SetOcclusion(Intensity);
			StandAt(Floor, LookAt);
			return 0.6f;
		}));
		S.Add(Do(Label + TEXT(": Stop"), []()
		{
			G.PulseScanTime = 0.0;
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f, []() { return 0.3f; }));
	}

	static FStep WaitPulseOver(const FString& Label)
	{
		return WaitFor(Label + TEXT(": the pulse is over"), []()
		{
			const ULidarScanComponent* L = Lidar();
			return L && FMath::IsNearlyEqual(L->GetOccludedFactor(), FMath::Clamp(L->OccludedIntensity, 0.f, 1.f), 1.e-4f);
		}, 6.f, []() { return 0.2f; });
	}

	/**
	 * Compared shots must not differ by the scans' age fade: while held, the lidar's AlphaFloor is 1 (no age fade, every
	 * point at full alpha), then put back.
	 */
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

	/**
	 * At the scan's own pose: a shot at 0, then at Intensity once the pulse is over. No lit pixel may get dimmer. Below 1
	 * the picture must also be the one at 0; at 1 (no depth test for the points) the pixels that get brighter are the
	 * sprites that sink into their own surface and the samples the capture placed behind a silhouette, which the depth
	 * test hides at 0: they are counted, not failed.
	 */
	static void AddOwnPoseAt(TArray<FStep>& S, const FString& Label, float Intensity)
	{
		S.Add(Do(FString::Printf(TEXT("%s: occluded intensity %.2f"), *Label, Intensity), [Intensity]() { SetOcclusion(Intensity); return 0.4f; }));
		AddShot(S, FString::Printf(TEXT("%s_%03d"), *Label, FMath::RoundToInt32(Intensity * 100.f)), [Label, Intensity](const FShot& Shot)
		{
			int32 Darker = 0, Brighter = 0, Lit = 0;
			Compare(G.OffShot, Shot, 3, Darker, Brighter, Lit);
			Note(FString::Printf(TEXT("%s: %d lit pixels at 0; at %.2f %d dimmer and %d brighter by more than 3 levels"), *Label, Lit, Intensity, Darker, Brighter));
			if (Intensity < 0.9999f)
			{
				Check(Darker == 0, FString::Printf(TEXT("%s at %.2f: from the scan's own pose no point in view is attenuated (%d pixels dimmer)"), *Label, Intensity, Darker));
				Check(Brighter <= Lit / 200, FString::Printf(TEXT("%s at %.2f: from the scan's own pose the picture is the one at 0 (%d of %d pixels brighter)"), *Label, Intensity, Brighter, Lit));
			}
		});
	}

	static void AddOwnPose(TArray<FStep>& S, const FString& Label, const FVector& Floor, const FVector& LookAt)
	{
		AddStopAndScanAt(S, Label, Floor, LookAt, 0.f);
		S.Add(Do(Label + TEXT(": wait as long as the pulse, no age fade"), []() { HoldAgeFade(true); return PulseEndSeconds() + 0.2f; }));
		AddShot(S, Label + TEXT("_000"), [Label](const FShot& Shot)
		{
			G.OffShot = Shot;
			int32 Darker = 0, Brighter = 0, Lit = 0;
			Compare(Shot, Shot, 3, Darker, Brighter, Lit);
			Check(Lit > 500, FString::Printf(TEXT("%s: the scan shows from its own pose (%d lit pixels)"), *Label, Lit));
		});
		AddOwnPoseAt(S, Label, G.Intensity);
		AddOwnPoseAt(S, Label, 1.f);
		S.Add(Do(Label + TEXT(": age fade back"), []() { HoldAgeFade(false); return 0.f; }));
	}

	// --- Geometry hidden from rendering: the criterion at 1 is that the points look as if it were not there ---

	struct FHiddenGeometry
	{
		TArray<TWeakObjectPtr<UPrimitiveComponent>> Hidden;
	};
	static FHiddenGeometry GHidden;

	/** Hides from rendering every visible primitive of the world except the point systems (collision unchanged). */
	static int32 HideGeometry()
	{
		GHidden.Hidden.Reset();
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
			GHidden.Hidden.Add(Prim);
		}
		return GHidden.Hidden.Num();
	}

	static void ShowGeometry()
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Prim : GHidden.Hidden)
		{
			if (UPrimitiveComponent* P = Prim.Get())
			{
				P->SetHiddenInGame(false);
			}
		}
		GHidden.Hidden.Reset();
	}

	static FShot GVisibleShot;

	/**
	 * The same alive scans from the same view, once with the geometry visible and once hidden from rendering (age fade
	 * held so the two frames match): the images must be the same. Hue, if not Any, is also measured in both (it must show).
	 */
	/**
	 * A pixel of B differs from A as Compare counts it: dimmer (A lit above 6 levels) or brighter, by more than Tol levels of
	 * its brightest channel.
	 */
	static bool Differs(const FColor& A, const FColor& B, int32 Tol)
	{
		const int32 MA = FMath::Max3(A.R, A.G, A.B);
		const int32 MB = FMath::Max3(B.R, B.G, B.B);
		return (MA > 6 && MB < MA - Tol) || MB > MA + Tol;
	}

	/**
	 * IsolatedAllowed > 0 (tls.Occlusion.ShipCheck only, Brief 18): a differing pixel whose 8 neighbours all match within
	 * the same 3 levels is isolated: a single pixel where the two point renderers' complementary depth tests disagree at a
	 * tie (a sprite cut by the surface its point lies on). Up to IsolatedAllowed of them pass, each logged; a differing
	 * pixel with a differing neighbour (a point is at least PointPixelSize wide, so a wrongly hidden or shown point is never
	 * a single pixel) or one isolated pixel too many fails, as before.
	 */
	static void AddHiddenCompare(TArray<FStep>& S, const FString& Label, EHue Hue = EHue::Any, bool bCheck = true, int32 IsolatedAllowed = 0)
	{
		S.Add(Do(Label + TEXT(": hold the age fade"), []() { HoldAgeFade(true); return 0.3f; }));
		AddShot(S, Label + TEXT("_Visible"), [](const FShot& Shot) { GVisibleShot = Shot; });
		S.Add(Do(Label + TEXT(": geometry hidden from rendering"), [Label]() { Note(FString::Printf(TEXT("%s: %d primitives hidden"), *Label, HideGeometry())); return 0.3f; }));
		AddShot(S, Label + TEXT("_Hidden"), [Label, Hue, bCheck, IsolatedAllowed](const FShot& Shot)
		{
			ShowGeometry();
			HoldAgeFade(false);
			int32 Darker = 0, Brighter = 0, Lit = 0;
			Compare(Shot, GVisibleShot, 3, Darker, Brighter, Lit);
			int32 Differ = 0, MaxDiff = 0;
			for (int32 Index = 0; Index < Shot.Pixels.Num() && Index < GVisibleShot.Pixels.Num(); ++Index)
			{
				const FColor& A = Shot.Pixels[Index];
				const FColor& B = GVisibleShot.Pixels[Index];
				const int32 D = FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B));
				Differ += D > 0 ? 1 : 0;
				MaxDiff = FMath::Max(MaxDiff, D);
			}
			Note(FString::Printf(TEXT("%s: %d lit pixels with the geometry hidden; with it visible %d dimmer and %d brighter by more than 3 levels, %d differ at all, largest difference %d levels"),
				*Label, Lit, Darker, Brighter, Differ, MaxDiff));
			if (Hue != EHue::Any)
			{
				const FShotStats V = Measure(GVisibleShot, Hue);
				const FShotStats H = Measure(Shot, Hue);
				Note(FString::Printf(TEXT("%s: in its colour, visible %s / hidden %s"), *Label, *Describe(V), *Describe(H)));
				if (Hue == EHue::Interactable)
				{
					Note(FString::Printf(TEXT("%s: dim interactable hue, visible %s"), *Label, *Describe(Measure(GVisibleShot, EHue::InteractableDim))));
				}
				if (bCheck)
				{
					Check(V.Lit > 5 && FMath::Abs(V.Lit - H.Lit) <= FMath::Max(5, H.Lit / 100) && FMath::Abs(V.Sum - H.Sum) <= 0.01 * H.Sum,
						FString::Printf(TEXT("%s: through the geometry it shows at full intensity, as with the geometry hidden (%d / %d pixels)"), *Label, V.Lit, H.Lit));
				}
			}
			if (bCheck)
			{
				Check(Lit > 100, FString::Printf(TEXT("%s: the scan shows (%d lit pixels)"), *Label, Lit));
				if (IsolatedAllowed <= 0)
				{
					Check(Darker == 0 && Brighter == 0, FString::Printf(TEXT("%s: the image is the one with the geometry hidden (%d dimmer, %d brighter; none dark along the edges of what covers them)"), *Label, Darker, Brighter));
				}
				else
				{
					const int32 Wd = Shot.Width;
					const int32 Ht = Shot.Height;
					int32 Isolated = 0, Grouped = 0;
					const bool bSame = Wd == GVisibleShot.Width && Ht == GVisibleShot.Height && Shot.Pixels.Num() == GVisibleShot.Pixels.Num();
					for (int32 Y = 0; bSame && Y < Ht; ++Y)
					{
						for (int32 X = 0; X < Wd; ++X)
						{
							const int32 Index = Y * Wd + X;
							if (!Differs(GVisibleShot.Pixels[Index], Shot.Pixels[Index], 3))
							{
								continue;
							}
							bool bAlone = true;
							for (int32 DY = -1; DY <= 1 && bAlone; ++DY)
							{
								for (int32 DX = -1; DX <= 1; ++DX)
								{
									const int32 NX = X + DX, NY = Y + DY;
									if ((DX || DY) && NX >= 0 && NY >= 0 && NX < Wd && NY < Ht && Differs(GVisibleShot.Pixels[NY * Wd + NX], Shot.Pixels[NY * Wd + NX], 3))
									{
										bAlone = false;
										break;
									}
								}
							}
							if (bAlone)
							{
								++Isolated;
								const FColor& V = GVisibleShot.Pixels[Index];
								const FColor& H = Shot.Pixels[Index];
								Note(FString::Printf(TEXT("%s: isolated differing pixel %d at (%d, %d): geometry visible (%d, %d, %d), hidden (%d, %d, %d), %d levels; its 8 neighbours within 3"),
									*Label, Isolated, X, Y, V.R, V.G, V.B, H.R, H.G, H.B, FMath::Max3(H.R, H.G, H.B) - FMath::Max3(V.R, V.G, V.B)));
							}
							else
							{
								++Grouped;
							}
						}
					}
					Check(bSame && Grouped == 0 && Isolated <= IsolatedAllowed,
						FString::Printf(TEXT("%s: the image is the one with the geometry hidden but for isolated pixels (%d differing pixels with a differing neighbour, %d isolated, at most %d allowed; none dark along the edges of what covers them)"),
							*Label, Grouped, Isolated, IsolatedAllowed));
				}
			}
		});
	}

	static void AddIntensityShot(TArray<FStep>& S, const FString& Label, float Intensity, EHue Hue)
	{
		S.Add(Do(FString::Printf(TEXT("%s at %.2f"), *Label, Intensity), [Intensity]() { SetOcclusion(Intensity); return 0.4f; }));
		AddShot(S, FString::Printf(TEXT("%s_%03d"), *Label, FMath::RoundToInt32(Intensity * 100.f)), [Label, Intensity, Hue](const FShot& Shot)
		{
			const FShotStats Stats = Measure(Shot, Hue);
			G.Stats.Add(FString::Printf(TEXT("%s_%03d"), *Label, FMath::RoundToInt32(Intensity * 100.f)), Stats);
			Note(FString::Printf(TEXT("%s at %.2f: %s"), *Label, Intensity, *Describe(Stats)));
			if (Hue == EHue::Interactable)
			{
				const FShotStats Dim = Measure(Shot, EHue::InteractableDim);
				G.Stats.Add(FString::Printf(TEXT("%s_%03d_Dim"), *Label, FMath::RoundToInt32(Intensity * 100.f)), Dim);
				Note(FString::Printf(TEXT("%s at %.2f, dim interactable hue: %s"), *Label, Intensity, *Describe(Dim)));
			}
		});
	}

	static FShotStats StatsOf(const FString& Key)
	{
		const FShotStats* Found = G.Stats.Find(Key);
		return Found ? *Found : FShotStats();
	}

	/** Looks around Figure for a floor point from which the eye sees it, 4 to 9 m away. */
	static bool FindSeeingFloor(const AEntity* Entity, const FVector& Figure, FVector& OutFloor)
	{
		const APawn* P = Pawn();
		if (!Entity || !P)
		{
			return false;
		}
		const float EyeHeight = EyeOf(P).Z - P->GetActorLocation().Z;
		for (const float Radius : { 450.f, 650.f, 850.f })
		{
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Angle = 2.f * PI * Step / 24.f;
				const FVector Floor = Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
				if (FMath::Abs(Floor.X) > 1400.f || FMath::Abs(Floor.Y) > 1400.f)
				{
					continue;
				}
				const FVector Eye(Floor.X, Floor.Y, P->GetActorLocation().Z + EyeHeight);
				if (Entity->CanBeSeenFrom(Eye, Figure))
				{
					OutFloor = Floor;
					return true;
				}
			}
		}
		return false;
	}

	static void AddResidueAndChannelParts(TArray<FStep>& S);

	static void StartOcclusionTest(bool bQuit, bool bShots)
	{
		StartRun(TEXT("OcclusionTest"), bQuit, bShots);
		LinesOffForRun();
		G = FState();
		// Values between 0 and 1 keep working as built: they are checked at 0.2; the game's value (1) has checks of its own.
		G.Intensity = 0.2f;
		// Check D measures the entity in its own colour (Brief 17: before it is found it shows in the scan's): found for the run.
		static bool bFoundSaved = false;
		if (URunSubsystem* Run = URunSubsystem::Get(W()))
		{
			bFoundSaved = Run->IsEntityFound();
			Run->SetEntityFound(true, TEXT("tls.OcclusionTest: the entity in its own colour"));
		}
		GRun.Cleanup = []()
		{
			if (URunSubsystem* Run = URunSubsystem::Get(W()))
			{
				Run->SetEntityFound(bFoundSaved, TEXT("tls.OcclusionTest: end"));
			}
			GetMutableDefault<UTheLastShiftSettings>()->ScanOcclusion = GSavedOcclusion;
			if (UScanDirectorComponent* D = Director())
			{
				D->ApplyScanOcclusion();
			}
			RestoreLines();
			ShowGeometry();
			if (G.TestSource.IsValid())
			{
				if (UScanDirectorComponent* D = Director())
				{
					D->UnregisterPointSource(G.TestSource.Get());
				}
			}
			if (AEntity* Entity = FindEntity(); Entity && G.bEntitySpeedSaved)
			{
				Entity->Speed = G.EntitySpeed;
			}
			if (AResidue* R = G.MovedResidue.Get())
			{
				R->SetActorLocation(G.ResidueHome);
			}
			if (ULidarScanComponent* L = Lidar())
			{
				L->bVerifyInjectedOcclusion = G.bVerifySaved;
			}
			if (AStdPlayerController* C = Cast<AStdPlayerController>(PC()); C && C->IsPausedByMenu())
			{
				C->ResumeGame();
			}
			HoldAgeFade(false);
			ResumeTime();
			KeepEntityDormant();
			ReleaseShots();
		};

		TArray<FStep>& S = GRun.Steps;

		// --- Setup: the values come from their one owner, the settings, pushed by the director ---
		S.Add(WaitFor(TEXT("a game world with the player's lidar, an entity and residues"), []() { return Lidar() && Pawn() && FindEntity() && Residues().Num() > 0; }, 10.f, []()
		{
			const ULidarScanComponent* L = Lidar();
			if (!L || !FindEntity() || Residues().Num() == 0)
			{
				return -1.f;
			}
			KeepEntityDormant();
			G.bVerifySaved = L->bVerifyInjectedOcclusion;
			AEntity* Entity = FindEntity();
			G.EntitySpeed = Entity->Speed;
			G.bEntitySpeedSaved = true;
			Entity->Speed = 0.f;
			Check(FMath::IsNearlyEqual(GSavedOcclusion.OccludedIntensity, 1.f) && GSavedOcclusion.LinkSeconds >= 0.f && GSavedOcclusion.FadeSeconds >= 0.f,
				FString::Printf(TEXT("ScanOcclusion in the settings: intensity %.2f, link %.2f s, fade %.2f s"), GSavedOcclusion.OccludedIntensity, GSavedOcclusion.LinkSeconds, GSavedOcclusion.FadeSeconds));
			Check(FMath::IsNearlyEqual(L->OccludedIntensity, GSavedOcclusion.OccludedIntensity) && FMath::IsNearlyEqual(L->LinkSeconds, GSavedOcclusion.LinkSeconds)
				&& FMath::IsNearlyEqual(L->OcclusionFadeSeconds, GSavedOcclusion.FadeSeconds),
				FString::Printf(TEXT("the director pushed them to the lidar at BeginPlay (lidar %.2f / %.2f / %.2f)"), L->OccludedIntensity, L->LinkSeconds, L->OcclusionFadeSeconds));
			FLidarSourceSettings ResidueSettings = ILidarPointSource::Execute_GetLidarSourceSettings(Residues()[0]);
			FLidarSourceSettings EntitySettings = ILidarPointSource::Execute_GetLidarSourceSettings(Entity);
			Check(ResidueSettings.bShowBehindGeometry, TEXT("the residues' source shows its points behind geometry"));
			Check(EntitySettings.bShowBehindGeometry, TEXT("the entity's source shows its points behind geometry"));
			Check(ResidueSettings.bOccludeByCapture && EntitySettings.bOccludeByCapture, TEXT("both still drop at scan time what the capture could not see"));
			Check(FLidarSourceSettings().bShowBehindGeometry == false, TEXT("the new source field is off by default"));
			return 0.f;
		}));

		// --- A. Off: identical to a plugin without this change ---
		S.Add(Do(TEXT("A: off, the pose of the 'before' shot"), []()
		{
			ResumeTime();
			KeepEntityDormant();
			SetOcclusion(0.f);
			if (APlayerController* C = PC())
			{
				// The pose and command of Off_Before, taken before this brief.
				C->ConsoleCommand(TEXT("BugItGo -850 -944 100 0 0 0"));
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("A: Stop"), []() { StopTime(); return 0.f; }));
		// Off_Before was shot 12.7 s after its scan, once the age fade (12 s) had settled at the alpha floor: same here.
		S.Add(WaitFor(TEXT("A: the automatic scan is taken"), []() { return AliveScans() >= 1; }, 4.f, []() { KeepEntityDormant(); return 6.5f; }));
		S.Add(Do(TEXT("A: the age fade settles"), []() { KeepEntityDormant(); return 6.5f; }));
		S.Add(Do(TEXT("A: off state"), []()
		{
			const ULidarScanComponent* L = Lidar();
			Check(L && !L->IsOcclusionDisplayActive() && L->GetOccludedMaterialSlotCount() == 0,
				FString::Printf(TEXT("A: at 0 no slot uses the occluded material (%d)"), L ? L->GetOccludedMaterialSlotCount() : -1));
			Check(L && L->GetOccludedFactor() == 0.f, TEXT("A: at 0 the occluded factor is 0"));
			Note(FString::Printf(TEXT("A: scan index %d, %s"), L ? L->GetScanCount() - 1 : -1, *SlotSummary()));
			return 0.f;
		}));
		AddShot(S, TEXT("Off_After"), [](const FShot& Shot)
		{
			Note(FString::Printf(TEXT("A: Off_After: %s (compare with Off_Before.png, taken before this brief from the same pose and scan index)"), *Describe(Measure(Shot, EHue::Any))));
		});
		S.Add(Do(TEXT("A: on, then off again"), []()
		{
			SetOcclusion(G.Intensity);
			const ULidarScanComponent* L = Lidar();
			const int32 On = L ? L->GetOccludedMaterialSlotCount() : -1;
			SetOcclusion(0.f);
			const int32 Off = L ? L->GetOccludedMaterialSlotCount() : -1;
			Check(L && On == L->MaxAliveScans && Off == 0, FString::Printf(TEXT("A: switching on gives every slot the occluded material (%d of %d), switching off takes it back (%d)"), On, L ? L->MaxAliveScans : -1, Off));
			return 0.f;
		}));

		// --- From the scan's own pose: at 3 m and 10 m from the wall in front ---
		AddOwnPose(S, TEXT("OwnPose_3m"), FVector(-500.f, -944.f, 0.f), FVector(-200.f, -944.f, 150.f));
		AddOwnPose(S, TEXT("OwnPose_10m"), FVector(-1200.f, -944.f, 0.f), FVector(-200.f, -944.f, 150.f));

		// --- B. Behind a wall: a scan from the corridor entrance seen from the main room ---
		AddStopAndScanAt(S, TEXT("B"), Poses::CorridorEntrance, Poses::CorridorFar, G.Intensity);
		S.Add(Do(TEXT("B: into the main room, facing the corridor wall"), []()
		{
			StandAt(Poses::MainRoom, Poses::CorridorMiddle);
			Note(FString::Printf(TEXT("B: %s"), *SlotSummary()));
			return 0.f;
		}));
		S.Add(WaitPulseOver(TEXT("B")));
		AddIntensityShot(S, TEXT("Wall"), 0.f, EHue::Scan);
		AddIntensityShot(S, TEXT("Wall"), 0.2f, EHue::Scan);
		AddIntensityShot(S, TEXT("Wall"), 0.4f, EHue::Scan);
		AddIntensityShot(S, TEXT("Wall"), 1.0f, EHue::Scan);
		S.Add(Do(TEXT("B: measured"), []()
		{
			const FShotStats S0 = StatsOf(TEXT("Wall_000")), S2 = StatsOf(TEXT("Wall_020")), S4 = StatsOf(TEXT("Wall_040")), S10 = StatsOf(TEXT("Wall_100"));
			Check(S0.Lit <= 20, FString::Printf(TEXT("B: at 0 the corridor's points are hidden behind the wall (%d lit pixels)"), S0.Lit));
			Check(S2.Lit > 200, FString::Printf(TEXT("B: at 0.2 they show through the wall (%d lit pixels)"), S2.Lit));
			Check(S4.Sum > S2.Sum * 1.2 && S10.Sum > S4.Sum * 1.2, FString::Printf(TEXT("B: brighter at 0.4 and at 1 (sums %.0f, %.0f, %.0f)"), S2.Sum, S4.Sum, S10.Sum));
			Note(FString::Printf(TEXT("B: hidden point brightness against the same points at intensity 1: sum ratio %.3f at 0.2, %.3f at 0.4; peak %d / %d / %d (8-bit, after tonemapping)"),
				S10.Sum > 0.0 ? S2.Sum / S10.Sum : 0.0, S10.Sum > 0.0 ? S4.Sum / S10.Sum : 0.0, S2.Peak, S4.Peak, S10.Peak));
			SetOcclusion(1.f);
			return 0.3f;
		}));
		AddHiddenCompare(S, TEXT("Wall_At1"), EHue::Residue);
		S.Add(Do(TEXT("B: back to 0.2"), []() { SetOcclusion(G.Intensity); return 0.f; }));

		// --- C. The pulse: a new scan taken facing away; the corridor's older scan pulses with it ---
		S.Add(Do(TEXT("C: face away, a new scan"), []()
		{
			StandAt(Poses::MainRoom, Poses::WestWall);
			return 0.2f;
		}));
		S.Add(Do(TEXT("C: scan"), []()
		{
			ULidarScanComponent* L = Lidar();
			if (!L)
			{
				return -1.f;
			}
			L->Scan();
			G.PulseScanTime = WorldNow();
			G.LinkEnd = G.FadeEnd = -1.0;
			G.bMidOk = true;
			Check(L->GetOccludedFactor() == 1.f, TEXT("C: at the scan the factor is 1"));
			Check(L->GetOccludedMaterialSlotCount() == L->MaxAliveScans, FString::Printf(TEXT("C: every slot, the older scan's included, shares the pulsing material (%d)"), L->GetOccludedMaterialSlotCount()));
			return 0.f;
		}));
		S.Add(Do(TEXT("C: back to the corridor wall"), []() { StandAt(Poses::MainRoom, Poses::CorridorMiddle); return 0.4f; }));
		AddShot(S, TEXT("Pulse_Link"), [](const FShot& Shot)
		{
			G.Stats.Add(TEXT("Pulse_Link"), Measure(Shot, EHue::Scan));
			Note(FString::Printf(TEXT("C: Pulse_Link (%.2f s after the scan): %s"), WorldNow() - G.PulseScanTime, *Describe(StatsOf(TEXT("Pulse_Link")))));
		});
		S.Add(WaitFor(TEXT("C: the factor stays 1 for the link time, then reaches the occluded intensity over the fade time"), []()
		{
			const ULidarScanComponent* L = Lidar();
			if (!L)
			{
				return false;
			}
			const double Age = WorldNow() - G.PulseScanTime;
			const float Factor = L->GetOccludedFactor();
			if (G.LinkEnd < 0.0 && Factor < 1.f)
			{
				G.LinkEnd = Age;
			}
			if (G.LinkEnd >= 0.0 && G.FadeEnd < 0.0 && Factor > G.Intensity + 1.e-4f)
			{
				// Mid-fade: the factor is the linear interpolation.
				const double Expected = FMath::Lerp(1.0, (double)G.Intensity, FMath::Clamp((Age - GSavedOcclusion.LinkSeconds) / FMath::Max(GSavedOcclusion.FadeSeconds, 1.e-3f), 0.0, 1.0));
				G.bMidOk &= FMath::Abs(Factor - Expected) < 0.02;
			}
			if (G.FadeEnd < 0.0 && FMath::IsNearlyEqual(Factor, G.Intensity, 1.e-4f))
			{
				G.FadeEnd = Age;
			}
			return G.FadeEnd >= 0.0;
		}, 8.f, []()
		{
			const float Frame = 0.05f;
			Note(FString::Printf(TEXT("C: measured link time %.3f s (set %.2f), fade over at %.3f s (set %.2f + %.2f)"), G.LinkEnd, GSavedOcclusion.LinkSeconds, G.FadeEnd, GSavedOcclusion.LinkSeconds, GSavedOcclusion.FadeSeconds));
			Check(G.LinkEnd >= GSavedOcclusion.LinkSeconds && G.LinkEnd < GSavedOcclusion.LinkSeconds + Frame, FString::Printf(TEXT("C: link time %.3f s"), G.LinkEnd));
			Check(G.FadeEnd >= PulseEndSeconds() && G.FadeEnd < PulseEndSeconds() + Frame, FString::Printf(TEXT("C: fade over %.3f s after the scan"), G.FadeEnd));
			Check(G.bMidOk, TEXT("C: during the fade the factor follows the linear ramp"));
			return 0.2f;
		}));
		AddShot(S, TEXT("Pulse_After"), [](const FShot& Shot)
		{
			const FShotStats After = Measure(Shot, EHue::Scan);
			const FShotStats Link = StatsOf(TEXT("Pulse_Link"));
			Note(FString::Printf(TEXT("C: Pulse_After: %s"), *Describe(After)));
			Check(Link.Sum > After.Sum * 2.0, FString::Printf(TEXT("C: the older scan's hidden points are full during the link time and attenuated after (sums %.0f -> %.0f)"), Link.Sum, After.Sum));
		});

		// The pulse stands still behind the pause menu.
		S.Add(Do(TEXT("C: face away again"), []() { StandAt(Poses::MainRoom, Poses::WestWall); return 0.2f; }));
		S.Add(Do(TEXT("C: a new scan, then the pause menu in the fade"), []()
		{
			if (ULidarScanComponent* L = Lidar())
			{
				L->Scan();
			}
			G.PulseScanTime = WorldNow();
			return GSavedOcclusion.LinkSeconds + GSavedOcclusion.FadeSeconds * 0.4f;
		}));
		S.Add(Do(TEXT("C: open the pause menu"), []()
		{
			AStdPlayerController* C = Cast<AStdPlayerController>(PC());
			const ULidarScanComponent* L = Lidar();
			G.PauseFactor = L ? L->GetOccludedFactor() : -1.0;
			G.PauseWorld = WorldNow();
			Check(C && C->PauseGame(), TEXT("C: the pause menu opens"));
			return 1.5f;
		}));
		S.Add(Do(TEXT("C: still behind the menu"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const double Factor = L ? L->GetOccludedFactor() : -2.0;
			Check(G.PauseFactor > G.Intensity && G.PauseFactor < 1.0 && FMath::IsNearlyEqual(Factor, G.PauseFactor, 1.e-4),
				FString::Printf(TEXT("C: 1.5 s behind the pause menu the factor stood still mid-fade (%.3f -> %.3f)"), G.PauseFactor, Factor));
			Check(FMath::IsNearlyEqual(WorldNow(), G.PauseWorld, 1.e-3), TEXT("C: the pulse's clock (world time) stood still"));
			if (AStdPlayerController* C = Cast<AStdPlayerController>(PC()))
			{
				C->ResumeGame();
			}
			return 0.f;
		}));
		S.Add(WaitPulseOver(TEXT("C: after the menu")));

		AddResidueAndChannelParts(S);

		// --- D (last: an awake entity appears at every later Stop). The entity is never seen through geometry ---
		S.Add(Do(TEXT("D: Resume, awaken the entity"), []()
		{
			ResumeTime();
			SetOcclusion(1.f);
			StandAt(Poses::MainRoom, Poses::CorridorMiddle);
			if (AEntity* Entity = FindEntity())
			{
				Entity->Awaken();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("D: Stop"), []() { StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("D: the entity appears out of sight"), []() { const AEntity* E = FindEntity(); return E && E->IsManifested() && AliveScans() >= 1; }, 5.f, []()
		{
			const AEntity* Entity = FindEntity();
			G.EntityFigure = Entity->GetFigureLocation();
			G.HiddenFloor = Pawn()->GetActorLocation();
			Check(!Entity->CanBeSeenFrom(EyeOf(Pawn()), G.EntityFigure), FString::Printf(TEXT("D: the figure at (%.0f, %.0f, %.0f) is not in sight of the player"), G.EntityFigure.X, G.EntityFigure.Y, G.EntityFigure.Z));
			if (!FindSeeingFloor(Entity, G.EntityFigure, G.SeeingFloor))
			{
				Check(false, TEXT("D: a pose that sees the figure"));
				return -1.f;
			}
			Note(FString::Printf(TEXT("D: seen from (%.0f, %.0f)"), G.SeeingFloor.X, G.SeeingFloor.Y));
			return 0.f;
		}));
		// The camera follows the control rotation on the pawn's next tick: turn, then scan a few frames later.
		S.Add(Do(TEXT("D: where the figure is seen"), []() { StandAt(G.SeeingFloor, G.EntityFigure); return 0.2f; }));
		S.Add(Do(TEXT("D: scan the entity from where it is seen"), []()
		{
			Check(FindEntity()->CanBeSeenFrom(EyeOf(Pawn()), G.EntityFigure), TEXT("D: the figure is in sight of the scan"));
			ULidarScanComponent* L = Lidar();
			L->Scan();
			G.EntityScanTime = WorldNow();
			G.EntityScanIndex = L->GetScanCount() - 1;
			const FLidarInjectionStats Stats = L->GetLastInjectionStats();
			Check(Stats.Uploaded > 0, FString::Printf(TEXT("D: the scan takes the entity's points (%d uploaded)"), Stats.Uploaded));
			return 0.25f;
		}));
		AddShot(S, TEXT("Entity_Seen"), [](const FShot& Shot)
		{
			const FShotStats Red = Measure(Shot, EHue::Entity);
			Check(Red.Lit > 20, FString::Printf(TEXT("D: seen, the entity shows (%s)"), *Describe(Red)));
		});
		S.Add(Do(TEXT("D: back out of sight, facing it"), []()
		{
			StandAt(G.HiddenFloor, G.EntityFigure);
			return 0.3f;
		}));
		S.Add(Do(TEXT("D: geometry between"), []()
		{
			Check(!FindEntity()->CanBeSeenFrom(EyeOf(Pawn()), G.EntityFigure), TEXT("D: back where geometry is between the figure and the player"));
			return 0.f;
		}));
		AddHiddenCompare(S, TEXT("Entity_At1"), EHue::Entity);
		S.Add(Do(TEXT("D: the entity's scan is still the newest"), []()
		{
			Check(ScanCount() - 1 == G.EntityScanIndex, TEXT("D: no later scan removed the entity's points during the shots"));
			return 0.f;
		}));

		Launch();
	}

	/**
	 * The moved residue's points against the newest scan's depth capture, by the rule of the injected emitter's spawn:
	 * how many are in the capture's coverage and how many of those lie behind the captured depth (they die at spawn).
	 */
	static bool ResidueAgainstNewestScan(AResidue* R, int32& OutInCoverage, int32& OutBehind)
	{
		OutInCoverage = OutBehind = 0;
		const ULidarScanComponent* L = Lidar();
		TArray<float> Depth;
		int32 Width = 0, Height = 0;
		FLidarScanContext Context;
		if (!R || !L || !L->ReadNewestScanDepth(Depth, Width, Height, Context))
		{
			return false;
		}
		TArray<FLidarInjectedPoint> Points;
		ILidarPointSource::Execute_GatherLidarPoints(R, Context, Points);
		const float Tolerance = FMath::Clamp(R->OcclusionTolerance, 0.f, 200.f);
		for (const FLidarInjectedPoint& Point : Points)
		{
			double PointDepth = 0.0;
			if (!Context.IsInCoverage(Point.WorldPosition, PointDepth))
			{
				continue;
			}
			++OutInCoverage;
			const FVector Offset = Point.WorldPosition - Context.CaptureOrigin;
			const double U = FVector::DotProduct(Offset, Context.CaptureRight) / (PointDepth * Context.TanHalfFOV.X);
			const double V = FVector::DotProduct(Offset, Context.CaptureUp) / (PointDepth * Context.TanHalfFOV.Y);
			const int32 X = FMath::Clamp(FMath::FloorToInt32((U + 1.0) * 0.5 * Width), 0, Width - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt32((1.0 - V) * 0.5 * Height), 0, Height - 1);
			const double Captured = Depth[Y * Width + X];
			OutBehind += (Captured > 1.0 && Captured < Context.MaxRange && Captured < PointDepth - Tolerance) ? 1 : 0;
		}
		return true;
	}

	static void AddResidueAndChannelParts(TArray<FStep>& S)
	{
		// --- E. A residue scanned in the corridor, then seen from behind the wall ---
		S.Add(Do(TEXT("E: a residue in the corridor"), []()
		{
			ResumeTime();
			AResidue* R = Residues()[0];
			G.MovedResidue = R;
			G.ResidueHome = R->GetActorLocation();
			R->SetActorLocation(FVector(0.f, -300.f, G.ResidueHome.Z));
			return 0.3f;
		}));
		AddStopAndScanAt(S, TEXT("E"), Poses::CorridorEntrance, FVector(0.f, -300.f, 100.f), G.Intensity);
		S.Add(Do(TEXT("E: the scan took it"), []()
		{
			int32 InCoverage = 0, Behind = 0;
			const bool bRead = ResidueAgainstNewestScan(G.MovedResidue.Get(), InCoverage, Behind);
			Check(bRead && InCoverage > 0 && Behind < InCoverage, FString::Printf(TEXT("E: the residue's points are in the scan (%d in coverage, %d of them behind the captured depth)"), InCoverage, Behind));
			StandAt(FVector(-850.f, -300.f, 0.f), FVector(0.f, -300.f, 100.f));
			return 0.f;
		}));
		S.Add(WaitPulseOver(TEXT("E")));
		AddIntensityShot(S, TEXT("Residue"), 0.f, EHue::Residue);
		AddIntensityShot(S, TEXT("Residue"), G.Intensity, EHue::Residue);
		S.Add(Do(TEXT("E: measured"), []()
		{
			const FShotStats Off = StatsOf(TEXT("Residue_000"));
			const FShotStats On = StatsOf(FString::Printf(TEXT("Residue_%03d"), FMath::RoundToInt32(G.Intensity * 100.f)));
			Check(Off.Lit <= 5, FString::Printf(TEXT("E: at 0 the residue is hidden behind the wall (%s)"), *Describe(Off)));
			Check(On.Lit > 5, FString::Printf(TEXT("E: at %.2f it shows through the wall, attenuated (%s)"), G.Intensity, *Describe(On)));
			// The next scan, facing away: the residue's points pulse with it.
			StandAt(FVector(-850.f, -300.f, 0.f), Poses::WestWall);
			return 0.2f;
		}));
		S.Add(Do(TEXT("E: the next scan, facing away"), []()
		{
			Lidar()->Scan();
			G.PulseScanTime = WorldNow();
			return 0.1f;
		}));
		S.Add(Do(TEXT("E: back to the residue"), []()
		{
			StandAt(FVector(-850.f, -300.f, 0.f), FVector(0.f, -300.f, 100.f));
			return 0.3f;
		}));
		AddShot(S, TEXT("Residue_Pulse"), [](const FShot& Shot)
		{
			const FShotStats Pulse = Measure(Shot, EHue::Residue);
			const FShotStats On = StatsOf(FString::Printf(TEXT("Residue_%03d"), FMath::RoundToInt32(G.Intensity * 100.f)));
			// The residue's dense core clips at 255 once at full intensity, so the measured sum grows far less than 1 / 0.2.
			Check(Pulse.Sum > On.Sum * 1.2,FString::Printf(TEXT("E: the residue pulses with the next scan (%s; attenuated sum %.0f)"), *Describe(Pulse), On.Sum));
		});
		// A residue behind a wall when the scan is taken: dropped by the capture, as before this brief.
		S.Add(Do(TEXT("E: a scan facing the residue behind the wall"), []()
		{
			ULidarScanComponent* L = Lidar();
			L->Scan();
			int32 InCoverage = 0, Behind = 0;
			const bool bRead = ResidueAgainstNewestScan(G.MovedResidue.Get(), InCoverage, Behind);
			Check(bRead && InCoverage > 0 && Behind == InCoverage, FString::Printf(TEXT("E: a residue behind the wall at scan time is not picked up, as before: all %d of its points in coverage lie behind the captured depth (%d) and die at spawn"), InCoverage, Behind));
			return 0.f;
		}));
		S.Add(Do(TEXT("E: residue back home"), []()
		{
			if (AResidue* R = G.MovedResidue.Get())
			{
				Check(R->GetState() == EResidueState::Available, TEXT("E: the residue was not collected by being seen"));
				R->SetActorLocation(G.ResidueHome);
			}
			G.MovedResidue.Reset();
			return 0.f;
		}));

		// --- F. Channels: the door's far face seen through the door ---
		AddStopAndScanAt(S, TEXT("F"), Poses::DoorEast, Poses::Door, G.Intensity);
		S.Add(Do(TEXT("F: the other side of the door"), []() { StandAt(Poses::DoorWest, Poses::Door); return 0.f; }));
		S.Add(WaitPulseOver(TEXT("F")));
		AddIntensityShot(S, TEXT("Channel"), 0.f, EHue::Interactable);
		AddIntensityShot(S, TEXT("Channel"), G.Intensity, EHue::Interactable);
		S.Add(Do(TEXT("F: measured"), []()
		{
			// Brief 13: with local exposure off the interactables at 0.20 through the door are dim (about 13 levels at most),
			// below the Interactable hue's 12-level floor; the check counts the interactables' hue down to 4 levels instead.
			const FShotStats Off = StatsOf(TEXT("Channel_000_Dim"));
			const FShotStats On = StatsOf(FString::Printf(TEXT("Channel_%03d_Dim"), FMath::RoundToInt32(G.Intensity * 100.f)));
			Check(Off.Lit <= 5, FString::Printf(TEXT("F: at 0 the door's far face is hidden (%s)"), *Describe(Off)));
			Check(On.Lit > 200, FString::Printf(TEXT("F: at %.2f it shows through the door in the interactables' colour, dim pixels included (%s)"), G.Intensity, *Describe(On)));
			SetOcclusion(1.f);
			return 0.3f;
		}));
		AddHiddenCompare(S, TEXT("Channel_At1"), EHue::Interactable);

		// --- H. Plugin rule: a source with bShowBehindGeometry off stays hidden behind geometry, at every value ---
		S.Add(Do(TEXT("H: a test source with the field off in the corridor"), []()
		{
			UTlsLidarTestSource* Source = NewObject<UTlsLidarTestSource>(GetTransientPackage());
			Source->Center = FVector(0.f, 300.f, 120.f);
			Source->Size = 60.f;
			Source->PointCount = 400;
			Source->Settings.Color = FLinearColor(1.f, 1.f, 0.f, 1.f);
			Source->Settings.IntensityMultiplier = 2.f;
			Source->Settings.FadeMode = ELidarFadeMode::None;
			Source->Settings.bOccludeByCapture = true;
			Source->Settings.bShowBehindGeometry = false;
			G.TestSource = Source;
			G.TestSourceRoot.Reset(Source);
			if (UScanDirectorComponent* D = Director())
			{
				D->RegisterPointSourceAtPriorityForTest(Source, 0);
			}
			return 0.f;
		}));
		AddStopAndScanAt(S, TEXT("H"), Poses::CorridorEntrance, Poses::CorridorMiddle, 1.f);
		S.Add(Do(TEXT("H: into the main room"), []() { StandAt(Poses::MainRoom, Poses::CorridorMiddle); return 0.3f; }));
		AddIntensityShot(S, TEXT("TestSource"), 1.f, EHue::TestSource);
		S.Add(WaitPulseOver(TEXT("H")));
		AddIntensityShot(S, TEXT("TestSource"), G.Intensity, EHue::TestSource);
		S.Add(Do(TEXT("H: back to 1"), []() { SetOcclusion(1.f); return 0.3f; }));
		AddHiddenCompare(S, TEXT("TestSource_At1"), EHue::TestSource, false);
		S.Add(Do(TEXT("H: measured"), []()
		{
			const FShotStats Full = StatsOf(TEXT("TestSource_100"));
			const FShotStats Mid = StatsOf(FString::Printf(TEXT("TestSource_%03d"), FMath::RoundToInt32(G.Intensity * 100.f)));
			const FShotStats Bare = Measure(GShot, EHue::TestSource);
			Check(Bare.Lit > 20, FString::Printf(TEXT("H: the scan took the field-off source (with the geometry hidden it shows: %s)"), *Describe(Bare)));
			Check(Full.Lit == 0 && Mid.Lit == 0, FString::Printf(TEXT("H: behind the wall it stays hidden at 1 (%d pixels) and at %.2f (%d pixels)"), Full.Lit, G.Intensity, Mid.Lit));
			if (UScanDirectorComponent* D = Director())
			{
				D->UnregisterPointSource(G.TestSource.Get());
			}
			G.TestSource.Reset();
			G.TestSourceRoot.Reset();
			return 0.f;
		}));
	}

	// =====================================================================================================
	// tls.Occlusion.PoseShots: the scan's own pose, any map
	// =====================================================================================================

	static void StartPoseShots(const FString& Label)
	{
		StartRun(TEXT("OcclusionPoseShots"), false, true);
		G = FState();
		G.Intensity = GSavedOcclusion.OccludedIntensity > 0.f ? GSavedOcclusion.OccludedIntensity : 0.2f;
		GRun.Cleanup = []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->ScanOcclusion = GSavedOcclusion;
			if (UScanDirectorComponent* D = Director())
			{
				D->ApplyScanOcclusion();
			}
			HoldAgeFade(false);
			ReleaseShots();
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(Do(TEXT("Stop and scan from here"), []()
		{
			KeepEntityDormant();
			SetOcclusion(0.f);
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
		S.Add(WaitFor(TEXT("a scan is alive"), []() { return AliveScans() >= 1; }, 4.f, []() { HoldAgeFade(true); return PulseEndSeconds() + 0.3f; }));
		AddShot(S, Label + TEXT("_000"), [](const FShot& Shot) { G.OffShot = Shot; });
		S.Add(Do(TEXT("occluded intensity on"), []() { SetOcclusion(G.Intensity); return 0.4f; }));
		AddShot(S, FString::Printf(TEXT("%s_%03d"), *Label, FMath::RoundToInt32(G.Intensity * 100.f)), [Label](const FShot& Shot)
		{
			int32 Darker = 0, Brighter = 0, Lit = 0;
			Compare(G.OffShot, Shot, 3, Darker, Brighter, Lit);
			const APawn* P = Pawn();
			const FVector Eye = EyeOf(P);
			Note(FString::Printf(TEXT("%s from (%.0f, %.0f, %.0f): %d lit pixels at 0; at %.2f %d dimmer and %d brighter by more than 3 levels"), *Label, Eye.X, Eye.Y, Eye.Z, Lit, G.Intensity, Darker, Brighter));
			if (G.Intensity < 0.9999f)
			{
				Check(Darker == 0, FString::Printf(TEXT("%s: no point in view is attenuated from the scan's own pose (%d pixels dimmer)"), *Label, Darker));
				Check(Brighter <= Lit / 200, FString::Printf(TEXT("%s: the picture is the one at 0 (%d of %d pixels brighter)"), *Label, Brighter, Lit));
			}
		});
		Launch();
	}

	// =====================================================================================================
	// tls.Occlusion.HiddenCompare and tls.Occlusion.ShipCheck: geometry visible against geometry hidden
	// =====================================================================================================

	static void StartHiddenCompare(const FString& Label)
	{
		StartRun(TEXT("OcclusionHiddenCompare"), false, true);
		G = FState();
		GRun.Cleanup = []()
		{
			ShowGeometry();
			HoldAgeFade(false);
			ReleaseShots();
		};
		AddHiddenCompare(GRun.Steps, Label);
		Launch();
	}

	// Prologue_Arrival (Brief 15): the ship is the modelled 6 x 12 x 5 m ship (X 600..1200, Y -600..600), its 4 x 3 m
	// opening in the middle of its long side at X = 1200, facing the exit door, in a 40 x 30 m hangar (X 0..4000,
	// Y -1500..1500). Two scans, one from the ship's entrance into the ship and one from 4 m in front of its closed long
	// side (X = 600, facing the hatch's wall), then views of that side's outer face from inside the ship, from 20 m
	// beyond its open side and from across the hangar, each of them through the ship. (Brief 12 to 14: a 12 x 6 m box
	// X 300..1500, Y -300..300, open at its +X end, and its +Y long side, scanned from 7 m.)
	namespace ShipPoses
	{
		static const FVector Entrance(1450.f, 0.f, 0.f);
		static const FVector ShipBack(600.f, 0.f, 200.f);
		static const FVector OutsideRight(200.f, 0.f, 0.f);
		static const FVector RightWallOuter(600.f, 0.f, 250.f);
	}

	static void AddShipView(TArray<FStep>& S, const FString& Label, const FVector& Floor)
	{
		S.Add(Do(Label + TEXT(": view"), [Label, Floor]()
		{
			StandAt(Floor, ShipPoses::RightWallOuter);
			const float Distance = FVector::Dist2D(Floor, ShipPoses::RightWallOuter);
			Note(FString::Printf(TEXT("%s: eye at (%.0f, %.0f), %.1f m from the closed long side's outer face"), *Label, Floor.X, Floor.Y, Distance / 100.f));
			SetOcclusion(1.f);
			return 0.4f;
		}));
		AddHiddenCompare(S, Label + TEXT("_At1"), EHue::Any, true, 3);
		// The margin of the attenuated path (values below 1), for the cause of the gap the developer saw at 1 before.
		S.Add(Do(Label + TEXT(": 0.99, the attenuated path"), []() { SetOcclusion(0.99f); return 0.4f; }));
		AddHiddenCompare(S, Label + TEXT("_At099"), EHue::Any, false);
	}

	static void StartShipCheck(bool bQuit)
	{
		StartRun(TEXT("OcclusionShipCheck"), bQuit, true);
		LinesOffForRun();
		G = FState();
		GRun.Cleanup = []()
		{
			RestoreLines();
			ShowGeometry();
			HoldAgeFade(false);
			GetMutableDefault<UTheLastShiftSettings>()->ScanOcclusion = GSavedOcclusion;
			if (UScanDirectorComponent* D = Director())
			{
				D->ApplyScanOcclusion();
			}
			ResumeTime();
			ReleaseShots();
		};
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("a game world with the player's lidar and time states"), []() { return Lidar() && Pawn() && TS(); }, 15.f));
		S.Add(Do(TEXT("Ship: the points' size"), []()
		{
			const ULidarScanComponent* L = Lidar();
			Note(FString::Printf(TEXT("Ship: a point is drawn %.1f pixels wide (PointPixelSize), so a point wrongly hidden or shown changes more than one pixel"), L ? L->PointPixelSize : -1.f));
			return 0.f;
		}));
		AddStopAndScanAt(S, TEXT("Ship"), ShipPoses::Entrance, ShipPoses::ShipBack, 1.f);
		S.Add(Do(TEXT("Ship: in front of its closed long side"), []() { StandAt(ShipPoses::OutsideRight, ShipPoses::RightWallOuter); return 0.3f; }));
		S.Add(Do(TEXT("Ship: scan its closed long side"), []()
		{
			if (ULidarScanComponent* L = Lidar())
			{
				L->Scan();
			}
			return 0.4f;
		}));
		S.Add(Do(TEXT("Ship: two scans alive"), []()
		{
			Check(AliveScans() >= 2, FString::Printf(TEXT("Ship: two scans alive (%d)"), AliveScans()));
			return 0.f;
		}));
		AddShipView(S, TEXT("Ship_Inside_5m"), FVector(1100.f, 250.f, 0.f));
		AddShipView(S, TEXT("Ship_BeyondOpenSide_20m"), FVector(2600.f, -400.f, 0.f));
		AddShipView(S, TEXT("Ship_InsideFront_5m"), FVector(1150.f, -250.f, 0.f));
		AddShipView(S, TEXT("Ship_Hangar_33m"), FVector(3900.f, -600.f, 0.f));
		AddShipView(S, TEXT("Ship_HangarFar_34m"), FVector(3900.f, 700.f, 0.f));
		Launch();
	}

	// =====================================================================================================
	// tls.FrameTime
	// =====================================================================================================

	struct FFrameTimeRun
	{
		int32 Frames = 120;
		TArray<double> Gpu, Render, Game;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FFrameTimeRun GFrame;

	static double Median(TArray<double> V)
	{
		if (V.IsEmpty())
		{
			return 0.0;
		}
		V.Sort();
		return V[V.Num() / 2];
	}

	static bool TickFrameTime(float)
	{
		GFrame.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		GFrame.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		GFrame.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		if (GFrame.Gpu.Num() < GFrame.Frames)
		{
			return true;
		}
		const ULidarScanComponent* L = Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("FrameTime: ===== %d frames, %s, occluded intensity %.2f (%d slots on the occluded material), %s. Median GPU %.3f ms, render thread %.3f ms, game thread %.3f ms. ====="),
			GFrame.Frames, IsStopped() ? TEXT("Stopped") : TEXT("Normal time"), L ? L->OccludedIntensity : -1.f, L ? L->GetOccludedMaterialSlotCount() : -1, *SlotSummary(),
			Median(GFrame.Gpu), Median(GFrame.Render), Median(GFrame.Game));
		return false;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsOcclusionTestCommand(
	TEXT("tls.OcclusionTest"),
	TEXT("tls.OcclusionTest [Quit=0] [Shots=1]: (fresh PIE or a package, in TestLidar) scan points behind geometry: the values pushed from the settings, off identical "
		"to before (Off_After, same pose and scan index as Off_Before), the scan's own pose at 3 m and 10 m, a corridor scan seen through the wall at 0 / 0.2 / 0.4 / 1, "
		"the pulse and its timing, the pulse still behind the pause menu, the entity never through geometry during its pulse, a residue through the wall and its pulse, "
		"a residue behind the wall at scan time, the interactables' colour through the door. Shots measured and saved to Saved/Screenshots/Brief12."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		OcclusionDebug::StartOcclusionTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsOcclusionPoseShotsCommand(
	TEXT("tls.Occlusion.PoseShots"),
	TEXT("tls.Occlusion.PoseShots <Label>: (PIE or a package, any map) stops time (or scans if already Stopped) from the current pose, waits for the pulse, "
		"shoots at occluded intensity 0 and at the settings' value and compares them pixel by pixel. Shots to Saved/Screenshots/Brief12."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		OcclusionDebug::StartPoseShots(Args.Num() > 0 ? Args[0] : FString(TEXT("Pose")));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsOcclusionHiddenCompareCommand(
	TEXT("tls.Occlusion.HiddenCompare"),
	TEXT("tls.Occlusion.HiddenCompare <Label>: (PIE or a package, any map, scans alive) shoots the current view with the geometry visible and with every primitive but the point systems hidden from rendering, and compares the two. Shots to Saved/Screenshots/Brief12."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		OcclusionDebug::StartHiddenCompare(Args.Num() > 0 ? Args[0] : FString(TEXT("Hidden")));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsOcclusionShipCheckCommand(
	TEXT("tls.Occlusion.ShipCheck"),
	TEXT("tls.Occlusion.ShipCheck [Quit=0]: (PIE or a package, in Prologue_Arrival) at occluded intensity 1, a scan from the ship's entrance and one from 4 m in front of its closed long side; that side's outer face seen through the ship from inside it, from 20 m beyond its open side and from across the hangar must look the same with the geometry visible and hidden. Also measures the attenuated path at 0.99. Shots to Saved/Screenshots/Brief12."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		OcclusionDebug::StartShipCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsOcclusionSetCommand(
	TEXT("tls.Occlusion.Set"),
	TEXT("tls.Occlusion.Set <Intensity> [LinkSeconds] [FadeSeconds]: sets ScanOcclusion in the settings for this session (not saved to the ini) and has the director push it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		FScanOcclusion& Values = GetMutableDefault<UTheLastShiftSettings>()->ScanOcclusion;
		if (Args.Num() > 0) { Values.OccludedIntensity = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 1.f); }
		if (Args.Num() > 1) { Values.LinkSeconds = FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 10.f); }
		if (Args.Num() > 2) { Values.FadeSeconds = FMath::Clamp(FCString::Atof(*Args[2]), 0.f, 10.f); }
		if (UScanDirectorComponent* D = OcclusionDebug::Director())
		{
			D->ApplyScanOcclusion();
		}
		const ULidarScanComponent* L = OcclusionDebug::Lidar();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Occlusion.Set: intensity %.2f, link %.2f s, fade %.2f s; lidar %.2f / %.2f / %.2f, %d slots on the occluded material."),
			Values.OccludedIntensity, Values.LinkSeconds, Values.FadeSeconds, L ? L->OccludedIntensity : -1.f, L ? L->LinkSeconds : -1.f, L ? L->OcclusionFadeSeconds : -1.f,
			L ? L->GetOccludedMaterialSlotCount() : -1);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsFrameTimeCommand(
	TEXT("tls.FrameTime"),
	TEXT("tls.FrameTime [Frames=120]: logs the median GPU, render-thread and game-thread time over the next Frames frames, with the time state, the occluded intensity and the alive scans."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace OcclusionDebug;
		if (GFrame.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GFrame.Ticker);
		}
		GFrame = FFrameTimeRun();
		GFrame.Frames = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 10, 2000) : 120;
		GFrame.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickFrameTime));
	}));

#endif
