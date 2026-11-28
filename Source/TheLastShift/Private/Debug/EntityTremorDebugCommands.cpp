// SPDX-License-Identifier: MIT

// Brief 47, the figure's tremor and pulse and the walls' width. tls.Entity.Tremor [RadiusCm] [Rate] [PulseShare] [PulseRate]
// sets the figure's tremor and pulse for the session (no argument: logs them with their ini line); tls.Entity.WallLook
// [Thickness] [Strands] [Bow] sets the walls' look of the session's form (no argument: logs it with its ini line), from the
// next walls raised. tls.EntityTremorCheck (an area map, PIE): the figure stood 6 m in front of the player with one scan
// alive, a wall, the trail and a residue in the scan's coverage, the camera still, over 120 frames at each of its cases:
// the figure trembles (pixels change between frames a quarter of a second apart; no changed pixel farther from the
// rectangle of its points' own places than the radius seen from there plus a point's size) and pulses (the mean level of
// its rectangle within 5 levels of its mean; the range of single points); nothing else moves (every pixel outside within 3
// levels from frame to frame); in the scan's colour and in the entity's own; off is off (the 120 frames one frame); the
// values followed (radius 6 cm and pulse 0.5; each half off on its own); behind the pause menu it stands still and goes on
// after; what a scan decides (gathered, outside the coverage, over the cap, uploaded, behind the captured depth, alive on the
// GPU) the same with the tremor on and off. Shots to Saved/Screenshots/Brief47. It puts back the run's state, the residues,
// the lidar's alpha floor, the session's values and the walls. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Entity/EntityWall.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarPointSource.h"
#include "LidarScanComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace EntityTremorDebug
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
	}

	static UWorld* W() { return ResolveGameWorld(nullptr); }

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
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

	// --- World access ---

	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static double WorldSeconds() { const UWorld* World = W(); return World ? World->GetTimeSeconds() : 0.0; }

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	static float EyeAboveFloor()
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f + static_cast<float>(EyeOf(Character).Z - Character->GetActorLocation().Z) : 160.f;
	}

	static bool OnNav(const FVector& Point, FVector& Out, const FVector& Extent = FVector(60.f, 60.f, 300.f))
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		FNavLocation Location;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Location, Extent))
		{
			return false;
		}
		Out = Location.Location;
		return true;
	}

	static void StandOn(const FVector& Floor, const FVector& LookAt)
	{
		ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Character)
		{
			return;
		}
		const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->SetActorLocation(Floor + FVector(0.f, 0.f, Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		if (APlayerController* C = PC())
		{
			C->SetControlRotation((LookAt - EyeOf(Character)).Rotation());
		}
	}

	static bool InSight(const FVector& Eye, const FVector& Target)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsTremorSight), false, FindEntity());
		Params.AddIgnoredActor(Pawn());
		FHitResult Hit;
		return !W()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Params);
	}

	static void ResumeNow()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume();
		}
	}

	static FString MapShort()
	{
		const UWorld* World = W();
		return World ? FPaths::GetBaseFilename(World->GetOutermost()->GetName()).Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	static FString MotionText(const FVector4f& M)
	{
		return FString::Printf(TEXT("FigureTremorRadiusCm=%.2f,FigureTremorRate=%.2f,FigurePulseShare=%.2f,FigurePulseRate=%.2f"), M.X, M.Y, M.Z, M.W);
	}

	// --- The pose: a wall across a passage 9 m ahead, the figure 6 m ahead, a residue beside it ---

	struct FPose
	{
		bool bValid = false;
		int32 Candidate = INDEX_NONE;
		FAreaPassage Where;
		FVector Player = FVector::ZeroVector;
		FVector Figure = FVector::ZeroVector;
		float WallDistance = 0.f;
	};

	static FPose FindPose()
	{
		FPose Pose;
		AEntity* E = FindEntity();
		if (!E)
		{
			return Pose;
		}
		const TArray<FEntityWallCandidate>& Candidates = E->GetWallCandidates();
		const float EyeAbove = EyeAboveFloor();
		for (const EAreaPassageKind Kind : { EAreaPassageKind::CorridorMouth, EAreaPassageKind::RoomMouth, EAreaPassageKind::GateMouth, EAreaPassageKind::SideMouth, EAreaPassageKind::Walkway })
		{
			for (int32 I = 0; I < Candidates.Num(); ++I)
			{
				const FEntityWallCandidate& C = Candidates[I];
				if (C.bExcluded || C.Where.Kind != Kind || C.Where.HalfWidth < 100.f)
				{
					continue;
				}
				for (const float WallDistance : { 900.f, 800.f })
				{
					for (const float Side : { -1.f, 1.f })
					{
						const FAreaPassage& P = C.Where;
						auto Floor = [&P](float Along, FVector& Out)
						{
							const FVector Want = P.Centre + P.Along * Along;
							return OnNav(Want + FVector(0.f, 0.f, 50.f), Out, FVector(40.f, 40.f, 250.f)) && FVector::Dist2D(Out, Want) < 60.f && FMath::Abs(Out.Z - P.Centre.Z) < 80.f;
						};
						FVector Player, Figure;
						if (!Floor(Side * WallDistance, Player) || !Floor(Side * (WallDistance - 600.f), Figure))
						{
							continue;
						}
						const FVector Eye = Player + FVector(0.f, 0.f, EyeAbove);
						if (!InSight(Eye, P.Centre + FVector(0.f, 0.f, 150.f)) || !InSight(Eye, Figure + FVector(0.f, 0.f, 100.f)))
						{
							continue;
						}
						Pose.bValid = true;
						Pose.Candidate = I;
						Pose.Where = P;
						Pose.Player = Player;
						Pose.Figure = Figure;
						Pose.WallDistance = WallDistance;
						return Pose;
					}
				}
			}
		}
		return Pose;
	}

	// --- Frames ---

	/** What 120 frames of one case measured. */
	struct FFrames
	{
		FString Label;
		int32 Wanted = 120;
		int32 Frames = 0;
		bool bRequested = false;
		bool bReady = false;
		uint64 RequestAtFrame = 0;
		uint64 RequestedAtFrame = 0;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Incoming;
		TArray<FColor> First;
		TArray<FColor> Prev;
		double FirstWorld = 0.0;
		bool bQuarterSaved = false;
		bool bSaveShots = false;

		/** The rectangle of the figure's points' own places, and it grown by the margin (shot pixels, Max exclusive). */
		FIntRect Own;
		FIntRect Grown;
		float MarginPx = 0.f;
		float RadiusPx = 0.f;

		TArray<TArray<FColor>> Crops;
		TArray<double> Times;
		TArray<double> RectMeans;
		TArray<uint8> CropMin;
		TArray<uint8> CropMax;

		/** Outside Grown, frame to frame: the largest channel step and how many pixels stepped by more than 3 levels. */
		int32 OutsideMaxStep = 0;
		int64 OutsideOver3 = 0;
		/** Any pixel against the first frame by more than 3 levels: how many (all frames), the farthest from Own (px). */
		int64 ChangedFromFirst = 0;
		float FarthestFromOwn = 0.f;
		/** Pixels dark in the first frame (max channel <= 10) that light up (>= 40) in a later frame. */
		int64 NewlyLit = 0;
		double WorldSpan = 0.0;
	};
	static FFrames GF;
	static FDelegateHandle GShotHandle;

	static int32 MaxChannel(const FColor& C) { return FMath::Max3<int32>(C.R, C.G, C.B); }

	static int32 MaxStep(const FColor& A, const FColor& B)
	{
		return FMath::Max3(FMath::Abs(int32(A.R) - int32(B.R)), FMath::Abs(int32(A.G) - int32(B.G)), FMath::Abs(int32(A.B) - int32(B.B)));
	}

	static void SavePng(const FString& Name, int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief47"), Name + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("shot %s saved (%d x %d): %s"), *Name, Width, Height, *Path));
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GF.Width = Width;
		GF.Height = Height;
		GF.Incoming = Pixels;
		GF.bReady = true;
	}

	static void RequestFrame()
	{
		if (!GShotHandle.IsValid())
		{
			GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		GF.bReady = false;
		GF.bRequested = true;
		GF.RequestedAtFrame = GFrameCounter;
		FScreenshotRequest::RequestScreenshot(FString(), false, false);
	}

	/** The rectangle of the figure's points' own places on the shot, and the tremor's radius seen from the camera (max, px). */
	static void FigureRects(int32 ShotW, int32 ShotH, float RadiusCm)
	{
		GF.Own = FIntRect();
		GF.Grown = FIntRect();
		const AEntity* E = FindEntity();
		const APlayerController* C = PC();
		if (!E || !C)
		{
			return;
		}
		int32 ViewX = 0, ViewY = 0;
		C->GetViewportSize(ViewX, ViewY);
		if (ViewX <= 0 || ViewY <= 0)
		{
			return;
		}
		const double SX = static_cast<double>(ShotW) / ViewX;
		const double SY = static_cast<double>(ShotH) / ViewY;
		FVector CamLoc;
		FRotator CamRot;
		C->GetPlayerViewPoint(CamLoc, CamRot);
		const FVector Right = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
		TArray<FVector> Points;
		E->BuildFigurePoints(Points);
		FBox2D Box(ForceInit);
		float Radius = 0.f;
		for (const FVector& P : Points)
		{
			FVector2D A, B;
			if (C->ProjectWorldLocationToScreen(P, A, false))
			{
				Box += FVector2D(A.X * SX, A.Y * SY);
				if (RadiusCm > 0.f && C->ProjectWorldLocationToScreen(P + Right * RadiusCm, B, false))
				{
					Radius = FMath::Max(Radius, static_cast<float>(FVector2D::Distance(FVector2D(A.X * SX, A.Y * SY), FVector2D(B.X * SX, B.Y * SY))));
				}
			}
		}
		if (!Box.bIsValid)
		{
			return;
		}
		const ULidarScanComponent* L = Lidar();
		const float PointPx = L ? L->PointPixelSize : 3.5f;
		GF.RadiusPx = Radius;
		GF.MarginPx = Radius + PointPx + 1.f;
		GF.Own = FIntRect(FMath::FloorToInt32(Box.Min.X), FMath::FloorToInt32(Box.Min.Y), FMath::CeilToInt32(Box.Max.X) + 1, FMath::CeilToInt32(Box.Max.Y) + 1);
		const int32 M = FMath::CeilToInt32(GF.MarginPx);
		GF.Grown = FIntRect(GF.Own.Min.X - M, GF.Own.Min.Y - M, GF.Own.Max.X + M, GF.Own.Max.Y + M);
		GF.Grown.Clip(FIntRect(0, 0, ShotW, ShotH));
	}

	static float DistanceFromOwn(int32 X, int32 Y)
	{
		const float DX = X < GF.Own.Min.X ? GF.Own.Min.X - X : (X >= GF.Own.Max.X ? X - GF.Own.Max.X + 1 : 0);
		const float DY = Y < GF.Own.Min.Y ? GF.Own.Min.Y - Y : (Y >= GF.Own.Max.Y ? Y - GF.Own.Max.Y + 1 : 0);
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	static void ProcessFrame(float RadiusCm)
	{
		const int32 Wd = GF.Width, Ht = GF.Height;
		const TArray<FColor>& P = GF.Incoming;
		if (P.Num() != Wd * Ht || Wd <= 0)
		{
			return;
		}
		const double Now = WorldSeconds();
		if (GF.Frames == 0)
		{
			FigureRects(Wd, Ht, RadiusCm);
			GF.First = P;
			GF.Prev = P;
			GF.FirstWorld = Now;
			const int32 N = GF.Grown.Area();
			GF.CropMin.Init(255, FMath::Max(N, 0));
			GF.CropMax.Init(0, FMath::Max(N, 0));
			if (GF.bSaveShots)
			{
				SavePng(GF.Label + TEXT("_t0.00s"), Wd, Ht, P);
			}
		}
		else
		{
			for (int32 Y = 0; Y < Ht; ++Y)
			{
				for (int32 X = 0; X < Wd; ++X)
				{
					const int32 I = Y * Wd + X;
					const bool bInside = GF.Grown.Contains(FIntPoint(X, Y));
					if (!bInside)
					{
						const int32 Step = MaxStep(P[I], GF.Prev[I]);
						GF.OutsideMaxStep = FMath::Max(GF.OutsideMaxStep, Step);
						GF.OutsideOver3 += Step > 3 ? 1 : 0;
					}
					if (MaxStep(P[I], GF.First[I]) > 3)
					{
						++GF.ChangedFromFirst;
						GF.FarthestFromOwn = FMath::Max(GF.FarthestFromOwn, DistanceFromOwn(X, Y));
						GF.NewlyLit += (MaxChannel(GF.First[I]) <= 10 && MaxChannel(P[I]) >= 40) ? 1 : 0;
					}
				}
			}
			GF.Prev = P;
			if (GF.bSaveShots && !GF.bQuarterSaved && Now - GF.FirstWorld >= 0.25)
			{
				GF.bQuarterSaved = true;
				SavePng(GF.Label + FString::Printf(TEXT("_t%.2fs"), Now - GF.FirstWorld), Wd, Ht, P);
			}
		}
		// The figure's rectangle (grown): its crop, its mean level, each pixel's range.
		TArray<FColor> Crop;
		Crop.Reserve(GF.Grown.Area());
		double Sum = 0.0;
		int32 K = 0;
		for (int32 Y = GF.Grown.Min.Y; Y < GF.Grown.Max.Y; ++Y)
		{
			for (int32 X = GF.Grown.Min.X; X < GF.Grown.Max.X; ++X)
			{
				const FColor& C = P[Y * Wd + X];
				Crop.Add(C);
				Sum += (C.R + C.G + C.B) / 3.0;
				const uint8 Level = static_cast<uint8>(MaxChannel(C));
				if (GF.CropMin.IsValidIndex(K))
				{
					GF.CropMin[K] = FMath::Min(GF.CropMin[K], Level);
					GF.CropMax[K] = FMath::Max(GF.CropMax[K], Level);
				}
				++K;
			}
		}
		GF.RectMeans.Add(Crop.Num() > 0 ? Sum / Crop.Num() : 0.0);
		GF.Crops.Add(MoveTemp(Crop));
		GF.Times.Add(Now);
		GF.WorldSpan = Now - GF.FirstWorld;
		++GF.Frames;
	}

	/** 120 frames of the current view, measured (see FFrames). */
	static void AddFrames(TArray<FStep>& S, const FString& Label, bool bSaveShots, TFunction<float()> RadiusCm, int32 Count = 120)
	{
		S.Add(Do(Label + TEXT(": first frame"), [Label, bSaveShots, Count]()
		{
			GF = FFrames();
			GF.Label = FString::Printf(TEXT("%s_%s"), *MapShort(), *Label);
			GF.bSaveShots = bSaveShots;
			GF.Wanted = Count;
			RequestFrame();
			return 0.f;
		}));
		S.Add(WaitFor(Label + FString::Printf(TEXT(": %d frames"), Count), [RadiusCm]()
		{
			if (GF.bRequested && GF.bReady)
			{
				ProcessFrame(RadiusCm());
				GF.bRequested = false;
				// The engine clears the screenshot request a frame after delivering one: the next request waits two frames.
				GF.RequestAtFrame = GFrameCounter + 2;
			}
			else if (GF.bRequested && GFrameCounter > GF.RequestedAtFrame + 30)
			{
				// A request lost to that clearing: ask again.
				RequestFrame();
			}
			if (!GF.bRequested && GF.Frames < GF.Wanted && GFrameCounter >= GF.RequestAtFrame)
			{
				RequestFrame();
			}
			return GF.Frames >= GF.Wanted;
		}, 300.f));
	}

	/** Changed pixels (> 3 levels) inside the figure's rectangle between frames about a quarter of a second apart: the median and the least over the pairs. */
	static void QuarterChanges(int32& OutMedian, int32& OutMin, int32& OutPairs)
	{
		TArray<int32> Counts;
		for (int32 F = 1; F < GF.Frames; ++F)
		{
			int32 Best = INDEX_NONE;
			for (int32 G = F - 1; G >= 0; --G)
			{
				if (GF.Times[F] - GF.Times[G] >= 0.25)
				{
					Best = G;
					break;
				}
			}
			if (Best == INDEX_NONE)
			{
				continue;
			}
			const TArray<FColor>& A = GF.Crops[F];
			const TArray<FColor>& B = GF.Crops[Best];
			int32 Changed = 0;
			for (int32 I = 0; I < FMath::Min(A.Num(), B.Num()); ++I)
			{
				Changed += MaxStep(A[I], B[I]) > 3 ? 1 : 0;
			}
			Counts.Add(Changed);
		}
		Counts.Sort();
		OutPairs = Counts.Num();
		OutMedian = Counts.Num() > 0 ? Counts[Counts.Num() / 2] : 0;
		OutMin = Counts.Num() > 0 ? Counts[0] : 0;
	}

	/** The largest departure of the rectangle's mean level from its mean over the frames. */
	static double MeanDeviation(double& OutMean)
	{
		OutMean = 0.0;
		for (const double M : GF.RectMeans)
		{
			OutMean += M;
		}
		OutMean /= FMath::Max(GF.RectMeans.Num(), 1);
		double Dev = 0.0;
		for (const double M : GF.RectMeans)
		{
			Dev = FMath::Max(Dev, FMath::Abs(M - OutMean));
		}
		return Dev;
	}

	/**
	 * Of the pixels in the figure's rectangle lit in every frame (max channel >= 30) whose level varies (by more than 3 levels:
	 * the figure's points, not the still scan points behind them), the share (max - min) / (max + min) of their level: median
	 * and 90th percentile; and how many.
	 */
	static void PointRanges(float& OutMedian, float& OutP90, int32& OutPixels)
	{
		TArray<float> Shares;
		for (int32 I = 0; I < GF.CropMin.Num(); ++I)
		{
			if (GF.CropMin[I] >= 30 && GF.CropMax[I] - GF.CropMin[I] > 3)
			{
				Shares.Add((GF.CropMax[I] - GF.CropMin[I]) / FMath::Max(1.f, float(GF.CropMax[I] + GF.CropMin[I])));
			}
		}
		Shares.Sort();
		OutPixels = Shares.Num();
		OutMedian = Shares.Num() > 0 ? Shares[Shares.Num() / 2] : 0.f;
		OutP90 = Shares.Num() > 0 ? Shares[FMath::Min(Shares.Num() - 1, Shares.Num() * 9 / 10)] : 0.f;
	}

	static FString Describe()
	{
		int32 QMedian = 0, QMin = 0, QPairs = 0;
		QuarterChanges(QMedian, QMin, QPairs);
		double Mean = 0.0;
		const double Dev = MeanDeviation(Mean);
		float RMedian = 0.f, RP90 = 0.f;
		int32 RPixels = 0;
		PointRanges(RMedian, RP90, RPixels);
		return FString::Printf(TEXT("%d frames over %.2f s; figure rect %dx%d px (own places), margin %.1f px (radius %.1f px); quarter-second pairs %d: changed pixels median %d, least %d; rect mean %.2f, largest departure %.2f levels; "
			"lit pixels %d: level range share median %.3f, p90 %.3f; outside: largest frame step %d, steps over 3 levels %lld; changed against frame 0: %lld pixels, farthest %.1f px from the own-places rect, newly lit %lld"),
			GF.Frames, GF.WorldSpan, GF.Own.Width(), GF.Own.Height(), GF.MarginPx, GF.RadiusPx, QPairs, QMedian, QMin, Mean, Dev, RPixels, RMedian, RP90, GF.OutsideMaxStep, GF.OutsideOver3,
			GF.ChangedFromFirst, GF.FarthestFromOwn, GF.NewlyLit);
	}

	// =====================================================================================================
	// tls.EntityTremorCheck
	// =====================================================================================================

	struct FCheck
	{
		FPose Pose;
		bool bSavedFound = false;
		TOptional<FVector4f> SavedMotion;
		float SavedAlphaFloor = 0.12f;
		bool bSavedVerify = false;
		float SavedTrailReach = 600.f;
		TArray<TPair<TWeakObjectPtr<AResidue>, FVector>> MovedResidues;
		FVector4f Config = FVector4f(0.f, 0.f, 0.f, 0.f);
		// Numbers kept for the report's comparisons.
		float FarthestConfig = 0.f;
		int32 QuarterConfig = 0;
		float RangeConfig = 0.f;
		double PausedWorld = 0.0;
		FLidarInjectionStats StatsOn;
		FLidarInjectionStats StatsOff;
		int32 AliveOn = -1;
		int32 AliveOff = -1;
		int32 ScanOn = INDEX_NONE;
		int32 ScanOff = INDEX_NONE;
	};
	static FCheck G;

	static void PutResiduesBack()
	{
		for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : G.MovedResidues)
		{
			if (AResidue* Residue = Moved.Key.Get())
			{
				Residue->SetActorLocation(Moved.Value);
			}
		}
		G.MovedResidues.Reset();
	}

	static void Cleanup()
	{
		if (AStdPlayerController* C = StdPC(); C && C->IsPausedByMenu())
		{
			C->ResumeGame();
		}
		PutResiduesBack();
		AEntity::SetSessionFigureMotion(G.SavedMotion);
		if (ULidarScanComponent* L = Lidar())
		{
			L->AlphaFloor = G.SavedAlphaFloor;
			L->bVerifyInjectedOcclusion = G.bSavedVerify;
		}
		if (AEntity* E = FindEntity())
		{
			E->TrailReach = G.SavedTrailReach;
			E->ClearWalls(TEXT("tls.EntityTremorCheck: end"));
		}
		ResumeNow();
		if (URunSubsystem* R = Run())
		{
			R->SetEntityFound(G.bSavedFound, TEXT("tls.EntityTremorCheck: end"));
		}
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
	}

	static int32 InjectedAliveOf(int32 ScanIndex)
	{
		if (const ULidarScanComponent* L = Lidar())
		{
			for (const FLidarScanSlotStats& S : L->GetScanSlotStats())
			{
				if (S.ScanIndex == ScanIndex)
				{
					return S.InjectedParticles;
				}
			}
		}
		return -1;
	}

	static int32 NewestScan()
	{
		int32 Newest = INDEX_NONE;
		if (const ULidarScanComponent* L = Lidar())
		{
			for (const FLidarScanContext& Context : L->GetAliveScans())
			{
				Newest = FMath::Max(Newest, Context.ScanIndex);
			}
		}
		return Newest;
	}

	/** The scene of every case: the player at the pose, the figure 6 m ahead held, the wall raised, a residue beside the figure, one scan (then wait out the lines' growth, the pulse and the cooldown). */
	static void AddScene(TArray<FStep>& S, const FString& Label, TFunction<FVector4f()> Motion, bool bFound)
	{
		S.Add(Do(Label + TEXT(": Stop"), [Motion, bFound]()
		{
			ResumeNow();
			Run()->SetEntityFound(bFound, TEXT("tls.EntityTremorCheck"));
			AEntity::SetSessionFigureMotion(Motion());
			StandOn(G.Pose.Player, G.Pose.Figure + FVector(0.f, 0.f, 120.f));
			if (UTimeStateSubsystem* T = TS())
			{
				T->Stop();
			}
			AEntity* E = FindEntity();
			if (E && !E->IsManifested())
			{
				E->SpawnNow(800.f);
			}
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": Stopped, the entity manifested, the automatic scan fired"), []()
		{
			const AEntity* E = FindEntity();
			const UScanDirectorComponent* D = Director();
			return IsStopped() && E && E->IsManifested() && D && D->GetScansThisStop() >= 1;
		}, 6.f, [Label]()
		{
			AEntity* E = FindEntity();
			E->PlaceFigure(G.Pose.Figure, true);
			E->RaiseWallsAtForTest({ G.Pose.Candidate });
			const APawn* P = Pawn();
			const FVector Ahead = (G.Pose.Figure - G.Pose.Player).GetSafeNormal2D();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
			PutResiduesBack();
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				G.MovedResidues.Emplace(*It, It->GetActorLocation());
				// One beside the figure, in sight; the others out of every scan's way.
				It->SetActorLocation(G.MovedResidues.Num() == 1 ? G.Pose.Player + Ahead * 500.f + Right * 150.f + FVector(0.f, 0.f, 60.f) : It->GetActorLocation() - FVector(0.f, 0.f, 100000.f));
			}
			StandOn(G.Pose.Player, E->GetFigureLocation() + FVector(0.f, 0.f, 120.f));
			(void)P;
			return 0.3f;
		}));
		S.Add(Do(Label + TEXT(": scan"), []()
		{
			Lidar()->Scan();
			return 0.3f;
		}));
		S.Add(Do(Label + TEXT(": scan's content"), [Label]()
		{
			const AEntity* E = FindEntity();
			const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
			int32 ResiduePoints = 0;
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				ResiduePoints = FMath::Max(ResiduePoints, It->GetLastGatheredPoints());
			}
			const ULidarScanComponent* L = Lidar();
			Check(E->GetLastGatheredFigurePoints() > 0 && E->GetLastGatheredWallPoints() > 0 && E->GetLastGatheredTrailPoints() > 0 && ResiduePoints > 0 && Stats.OverCap == 0,
				FString::Printf(TEXT("%s: the scan holds the figure %d, the trail %d, a wall %d and a residue %d points (gathered %d, outside the coverage %d, over the cap %d, uploaded %d); its source asks for %s"), *Label,
					E->GetLastGatheredFigurePoints(), E->GetLastGatheredTrailPoints(), E->GetLastGatheredWallPoints(), ResiduePoints, Stats.Gathered, Stats.OutsideCoverage, Stats.OverCap, Stats.Uploaded,
					*MotionText(AEntity::GetFigureMotionNow())));
			(void)L;
			// The lines' growth (LinkSeconds), the occlusion pulse and the scan-ready ring's cooldown are over after 5 s.
			return 5.f;
		}));
	}

	static void StartCheck()
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = TEXT("tls.EntityTremorCheck");
		G = FCheck();
		TArray<FStep>& S = GRun.Steps;

		S.Add(Do(TEXT("setup"), []()
		{
			AEntity* E = FindEntity();
			ULidarScanComponent* L = Lidar();
			if (!E || !L || !Run() || !TS())
			{
				Check(false, TEXT("an area map with the entity, the player's lidar and the run"));
				return -1.f;
			}
			G.bSavedFound = Run()->IsEntityFound();
			G.SavedMotion = AEntity::GetSessionFigureMotion();
			G.SavedAlphaFloor = L->AlphaFloor;
			G.bSavedVerify = L->bVerifyInjectedOcclusion;
			G.SavedTrailReach = E->TrailReach;
			AEntity::SetSessionFigureMotion(TOptional<FVector4f>());
			G.Config = AEntity::GetFigureMotionNow();
			// The scans' own age fade held at full (the alpha floor at 1), so that only what this brief moves can change a pixel.
			L->AlphaFloor = 1.f;
			GRun.Cleanup = &Cleanup;
			const FEntityForms& Forms = UTheLastShiftSettings::Get()->EntityForms;
			Check(FMath::IsNearlyEqual(G.Config.X, 1.5f) && FMath::IsNearlyEqual(G.Config.Y, 6.f) && FMath::IsNearlyEqual(G.Config.Z, 0.2f) && FMath::IsNearlyEqual(G.Config.W, 1.2f),
				FString::Printf(TEXT("the settings' values (EntityForms: %s): radius 1.5 cm at 6 a second, pulse 0.2 at 1.2 a second; form %s"),
					*MotionText(FVector4f(Forms.FigureTremorRadiusCm, Forms.FigureTremorRate, Forms.FigurePulseShare, Forms.FigurePulseRate)), LexToString(E->GetRequestedForm())));
			G.Pose = FindPose();
			Check(G.Pose.bValid, FString::Printf(TEXT("a pose: a wall candidate %d (%s) %.0f m ahead, the figure 6 m ahead, both in sight"), G.Pose.Candidate,
				G.Pose.bValid ? LexToString(G.Pose.Where.Kind) : TEXT("none"), G.Pose.WallDistance / 100.f));
			return G.Pose.bValid ? 0.f : -1.f;
		}));

		// --- 1: the settings' values, the scan's colour ---
		AddScene(S, TEXT("1 scan's colour"), []() { return G.Config; }, false);
		AddFrames(S, TEXT("Figure6m_ScanColour"), true, []() { return G.Config.X; });
		S.Add(Do(TEXT("1 measure"), []()
		{
			Note(FString::Printf(TEXT("1 NUMBERS scan's colour, settings' values: %s"), *Describe()));
			int32 QMedian = 0, QMin = 0, QPairs = 0;
			QuarterChanges(QMedian, QMin, QPairs);
			double Mean = 0.0;
			const double Dev = MeanDeviation(Mean);
			float RMedian = 0.f, RP90 = 0.f;
			int32 RPixels = 0;
			PointRanges(RMedian, RP90, RPixels);
			G.FarthestConfig = GF.FarthestFromOwn;
			G.QuarterConfig = QMedian;
			G.RangeConfig = RMedian;
			Check(QPairs > 0 && QMin > 0, FString::Printf(TEXT("1: it trembles: between frames a quarter of a second apart pixels change inside the figure's rectangle (median %d, least %d, %d pairs)"), QMedian, QMin, QPairs));
			Check(GF.FarthestFromOwn <= GF.MarginPx, FString::Printf(TEXT("1: slightly: no pixel changes farther from the rectangle of its own places than the radius seen from there plus a point's size (%.1f px, at most %.1f)"),
				GF.FarthestFromOwn, GF.MarginPx));
			Check(Dev <= 5.0, FString::Printf(TEXT("1: it pulses slightly: the rectangle's mean level stays within 5 levels of its mean (%.2f, largest departure %.2f)"), Mean, Dev));
			Check(GF.OutsideOver3 == 0, FString::Printf(TEXT("1: nothing else moves: outside the figure's rectangle, with the wall, the trail and the residue in the scan, every pixel within 3 levels frame to frame (largest step %d, %lld over 3)"),
				GF.OutsideMaxStep, GF.OutsideOver3));
			return 0.f;
		}));

		// --- 7: behind the pause menu ---
		S.Add(Do(TEXT("7 the pause menu opens"), []()
		{
			Check(StdPC() && StdPC()->PauseGame() && StdPC()->IsPausedByMenu(), TEXT("7: the pause menu opens"));
			G.PausedWorld = WorldSeconds();
			return 0.4f;
		}));
		AddFrames(S, TEXT("Paused"), false, []() { return G.Config.X; }, 30);
		S.Add(Do(TEXT("7 still behind the menu"), []()
		{
			Note(FString::Printf(TEXT("7 NUMBERS behind the pause menu: %s"), *Describe()));
			Check(GF.ChangedFromFirst == 0 && GF.WorldSpan < 1e-6, FString::Printf(TEXT("7: behind the pause menu it stands still (%lld pixels changed over 30 frames; the world clock moved %.4f s over them)"),
				GF.ChangedFromFirst, GF.WorldSpan));
			StdPC()->ResumeGame();
			return 0.4f;
		}));
		AddFrames(S, TEXT("AfterPause"), false, []() { return G.Config.X; }, 30);
		S.Add(Do(TEXT("7 goes on after"), []()
		{
			Note(FString::Printf(TEXT("7 NUMBERS after the pause menu: %s"), *Describe()));
			int32 QMedian = 0, QMin = 0, QPairs = 0;
			QuarterChanges(QMedian, QMin, QPairs);
			Check(!StdPC()->IsPausedByMenu() && GF.ChangedFromFirst > 0 && QMedian > 0, FString::Printf(TEXT("7: the menu closed, it goes on (%lld pixels changed against the first frame, %d a quarter of a second apart)"),
				GF.ChangedFromFirst, QMedian));
			return 0.f;
		}));

		// --- 2: found: the entity's own colour ---
		AddScene(S, TEXT("2 entity's colour"), []() { return G.Config; }, true);
		AddFrames(S, TEXT("Figure6m_EntityColour"), true, []() { return G.Config.X; });
		S.Add(Do(TEXT("2 measure"), []()
		{
			Note(FString::Printf(TEXT("2 NUMBERS entity's colour, settings' values: %s"), *Describe()));
			const AEntity* E = FindEntity();
			int32 QMedian = 0, QMin = 0, QPairs = 0;
			QuarterChanges(QMedian, QMin, QPairs);
			double Mean = 0.0;
			const double Dev = MeanDeviation(Mean);
			Check(E->IsFound() && E->GetLidarSourceSettings_Implementation().Color.Equals(UTheLastShiftSettings::GetScanColors().Entity), TEXT("2: found, the figure takes the entity's own colour"));
			Check(QPairs > 0 && QMin > 0 && GF.FarthestFromOwn <= GF.MarginPx, FString::Printf(TEXT("2: found, it trembles as before (a quarter of a second apart: median %d, least %d; farthest %.1f px of %.1f)"), QMedian, QMin, GF.FarthestFromOwn, GF.MarginPx));
			Check(Dev <= 5.0, FString::Printf(TEXT("2: found, its rectangle's mean level within 5 levels of its mean (%.2f, departure %.2f)"), Mean, Dev));
			Check(GF.OutsideOver3 == 0, FString::Printf(TEXT("2: found, nothing else moves (largest step %d, %lld over 3)"), GF.OutsideMaxStep, GF.OutsideOver3));
			return 0.f;
		}));

		// --- 3: off is off ---
		AddScene(S, TEXT("3 off"), []() { return FVector4f(0.f, 0.f, 0.f, 0.f); }, false);
		AddFrames(S, TEXT("Off"), false, []() { return 0.f; });
		S.Add(Do(TEXT("3 measure"), []()
		{
			Note(FString::Printf(TEXT("3 NUMBERS off: %s"), *Describe()));
			Check(GF.ChangedFromFirst == 0, FString::Printf(TEXT("3: off is off: with the four values at 0 the 120 frames are one frame within 3 levels (%lld pixels departed)"), GF.ChangedFromFirst));
			// The source asks for nothing: every point's motion uploaded is 0, so each is drawn at its own place (frames identical above).
			Check(AEntity::GetFigureMotionNow() == FVector4f(0.f, 0.f, 0.f, 0.f) && GF.RadiusPx == 0.f,
				FString::Printf(TEXT("3: off: the source asks for no motion (%s), so the figure's pixels are those of its points' own places"), *MotionText(AEntity::GetFigureMotionNow())));
			return 0.f;
		}));

		// --- 4: the values followed: radius 6 cm, pulse 0.5 ---
		AddScene(S, TEXT("4 radius 6, pulse 0.5"), []() { return FVector4f(6.f, G.Config.Y, 0.5f, G.Config.W); }, false);
		AddFrames(S, TEXT("Radius6Pulse05"), false, []() { return 6.f; });
		S.Add(Do(TEXT("4 measure"), []()
		{
			Note(FString::Printf(TEXT("4 NUMBERS radius 6 cm, pulse 0.5: %s"), *Describe()));
			int32 QMedian = 0, QMin = 0, QPairs = 0;
			QuarterChanges(QMedian, QMin, QPairs);
			Check(GF.FarthestFromOwn <= GF.MarginPx && QMedian > G.QuarterConfig, FString::Printf(TEXT("4: radius 6 cm and pulse 0.5: it moves visibly more (pixels changed a quarter of a second apart: median %d against %d; farthest %.1f px of %.1f, against %.1f)"),
				QMedian, G.QuarterConfig, GF.FarthestFromOwn, GF.MarginPx, G.FarthestConfig));
			Check(GF.OutsideOver3 == 0, FString::Printf(TEXT("4: nothing else moves (largest step %d, %lld over 3)"), GF.OutsideMaxStep, GF.OutsideOver3));
			return 0.f;
		}));

		// --- 5: the pulse alone (no tremor): no point moves, single points' levels rise and fall ---
		AddScene(S, TEXT("5 pulse only"), []() { return FVector4f(0.f, 0.f, G.Config.Z, G.Config.W); }, false);
		AddFrames(S, TEXT("PulseOnly"), false, []() { return 0.f; });
		S.Add(Do(TEXT("5 measure"), []()
		{
			Note(FString::Printf(TEXT("5 NUMBERS pulse 0.2 only: %s"), *Describe()));
			float RMedian = 0.f, RP90 = 0.f;
			int32 RPixels = 0;
			PointRanges(RMedian, RP90, RPixels);
			double Mean = 0.0;
			const double Dev = MeanDeviation(Mean);
			Check(GF.NewlyLit == 0 && GF.FarthestFromOwn <= Lidar()->PointPixelSize + 1.f, FString::Printf(TEXT("5: the tremor off on its own: no point moves (newly lit pixels %lld, farthest change %.1f px)"), GF.NewlyLit, GF.FarthestFromOwn));
			Check(RPixels > 0 && RMedian > 0.03f && Dev <= 5.0, FString::Printf(TEXT("5: the pulse 0.2 on its own: single points rise and fall (level range share median %.3f, p90 %.3f over %d pixels), the rectangle's mean steady (departure %.2f)"),
				RMedian, RP90, RPixels, Dev));
			G.RangeConfig = RMedian;
			return 0.f;
		}));

		// --- 6: the tremor alone (no pulse) ---
		AddScene(S, TEXT("6 tremor only"), []() { return FVector4f(G.Config.X, G.Config.Y, 0.f, 0.f); }, false);
		AddFrames(S, TEXT("TremorOnly"), false, []() { return G.Config.X; });
		S.Add(Do(TEXT("6 measure"), []()
		{
			Note(FString::Printf(TEXT("6 NUMBERS tremor 1.5 cm only: %s"), *Describe()));
			int32 QMedian = 0, QMin = 0, QPairs = 0;
			QuarterChanges(QMedian, QMin, QPairs);
			Check(QMedian > 0 && GF.FarthestFromOwn <= GF.MarginPx, FString::Printf(TEXT("6: the pulse off on its own: it still trembles (changed a quarter of a second apart: median %d; farthest %.1f px of %.1f)"),
				QMedian, GF.FarthestFromOwn, GF.MarginPx));
			return 0.f;
		}));

		// --- 8: what a scan decides: the same scan's counts with the tremor on and off (the trail and the walls left out: they are drawn anew for every scan) ---
		S.Add(Do(TEXT("8 setup"), []()
		{
			AEntity* E = FindEntity();
			E->ClearWalls(TEXT("tls.EntityTremorCheck: counts"));
			E->TrailReach = 0.f;
			// The residues out of every scan's coverage too: their points are drawn anew for every scan.
			PutResiduesBack();
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				G.MovedResidues.Emplace(*It, It->GetActorLocation());
				It->SetActorLocation(It->GetActorLocation() - FVector(0.f, 0.f, 100000.f));
			}
			Lidar()->bVerifyInjectedOcclusion = true;
			AEntity::SetSessionFigureMotion(FVector4f(6.f, G.Config.Y, 0.5f, G.Config.W));
			StandOn(G.Pose.Player, E->GetFigureLocation() + FVector(0.f, 0.f, 120.f));
			return 0.2f;
		}));
		S.Add(Do(TEXT("8 scan, motion on"), []() { Lidar()->Scan(); G.ScanOn = NewestScan(); return 0.3f; }));
		S.Add(Do(TEXT("8 counts, motion on"), []()
		{
			G.StatsOn = Lidar()->GetLastInjectionStats();
			G.ScanOn = NewestScan();
			return 0.5f;
		}));
		S.Add(Do(TEXT("8 alive, motion on; scan, motion off"), []()
		{
			G.AliveOn = InjectedAliveOf(G.ScanOn);
			AEntity::SetSessionFigureMotion(FVector4f(0.f, 0.f, 0.f, 0.f));
			Lidar()->Scan();
			return 0.3f;
		}));
		S.Add(Do(TEXT("8 counts, motion off"), []()
		{
			G.StatsOff = Lidar()->GetLastInjectionStats();
			G.ScanOff = NewestScan();
			return 0.5f;
		}));
		S.Add(Do(TEXT("8 compare"), []()
		{
			G.AliveOff = InjectedAliveOf(G.ScanOff);
			const FLidarInjectionStats& A = G.StatsOn;
			const FLidarInjectionStats& B = G.StatsOff;
			Check(A.Gathered == B.Gathered && A.OutsideCoverage == B.OutsideCoverage && A.OverCap == B.OverCap && A.Uploaded == B.Uploaded && A.BehindCapturedDepth == B.BehindCapturedDepth && G.AliveOn == G.AliveOff,
				FString::Printf(TEXT("8: what a scan decides is unchanged: tremor 6 cm and pulse 0.5 on / off: gathered %d / %d, outside the coverage %d / %d, over the cap %d / %d, uploaded %d / %d, behind the captured depth %d / %d, alive on the GPU %d / %d"),
					A.Gathered, B.Gathered, A.OutsideCoverage, B.OutsideCoverage, A.OverCap, B.OverCap, A.Uploaded, B.Uploaded, A.BehindCapturedDepth, B.BehindCapturedDepth, G.AliveOn, G.AliveOff));
			return 0.f;
		}));

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun), 0.f);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: started (%d steps)."), *GRun.Name, GRun.Steps.Num());
	}

	// =====================================================================================================
	// tls.Entity.Tremor, tls.Entity.WallLook
	// =====================================================================================================

	static void LogTremor()
	{
		const TOptional<FVector4f> Session = AEntity::GetSessionFigureMotion();
		const FVector4f Now = AEntity::GetFigureMotionNow();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Tremor: the figure's tremor %.2f cm at %.2f a second, pulse %.2f at %.2f a second (%s). Ini (EntityForms): %s"), Now.X, Now.Y, Now.Z, Now.W,
			Session.IsSet() ? TEXT("the session's") : TEXT("the settings'"), *MotionText(Now));
	}

	static EEntityForm FormNow()
	{
		if (const AEntity* E = FindEntity())
		{
			return E->GetRequestedForm();
		}
		const TOptional<EEntityForm> Session = AEntity::GetSessionForm();
		return Session.IsSet() ? Session.GetValue() : UTheLastShiftSettings::Get()->GetEntityFormOfAct(1);
	}

	static void LogWallLook()
	{
		const EEntityForm Form = FormNow();
		float Thickness = 0.f;
		int32 Strands = 0;
		float Bow = 0.f;
		AEntity::GetWallLookOf(Form, Thickness, Strands, Bow);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.WallLook: %s's walls: %d strands, bowed %.2f, %.1f cm either side of their line (%.1f cm wide) (%s). Ini (EntityForms.Forms, Form=%s): WallStrands=%d,WallBow=%.2f,WallThickness=%.1f"),
			LexToString(Form), Strands, Bow, Thickness, Thickness * 2.f, AEntity::GetSessionWallLook(Form).IsSet() ? TEXT("the session's") : TEXT("the settings'"), LexToString(Form), Strands, Bow, Thickness);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityTremorCheckCommand(
	TEXT("tls.EntityTremorCheck"),
	TEXT("tls.EntityTremorCheck: Brief 47, the figure's tremor and pulse (an area map, PIE): 120 frames of one scan per case; trembles, pulses, nothing else moves, found and not, off, the values, the pause menu, the scan's counts; shots to Saved/Screenshots/Brief47."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		EntityTremorDebug::StartCheck();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityTremorCommand(
	TEXT("tls.Entity.Tremor"),
	TEXT("tls.Entity.Tremor [RadiusCm] [Rate] [PulseShare] [PulseRate]: Brief 47, the figure's tremor and pulse for the session, from the next scan (no argument: logs them with their ini line; a value left out keeps the current one)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() > 0)
		{
			FVector4f M = AEntity::GetFigureMotionNow();
			M.X = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 50.f);
			M.Y = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 30.f) : M.Y;
			M.Z = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), 0.f, 1.f) : M.Z;
			M.W = Args.Num() > 3 ? FMath::Clamp(FCString::Atof(*Args[3]), 0.f, 30.f) : M.W;
			AEntity::SetSessionFigureMotion(M);
		}
		EntityTremorDebug::LogTremor();
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityWallLookCommand(
	TEXT("tls.Entity.WallLook"),
	TEXT("tls.Entity.WallLook [Thickness] [Strands] [Bow]: Brief 47, the walls' look of the session's form (its strands' half-width in cm, their count, their bow) for the session, from the next walls raised (no argument: logs it with its ini line; a value left out keeps the current one)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() > 0)
		{
			const EEntityForm Form = EntityTremorDebug::FormNow();
			float Thickness = 0.f;
			int32 Strands = 0;
			float Bow = 0.f;
			AEntity::GetWallLookOf(Form, Thickness, Strands, Bow);
			Thickness = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 50.f);
			Strands = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 32) : Strands;
			Bow = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), 0.f, 0.5f) : Bow;
			AEntity::SetSessionWallLook(Form, FVector3f(Thickness, static_cast<float>(Strands), Bow));
		}
		EntityTremorDebug::LogWallLook();
	}));

#endif // !UE_BUILD_SHIPPING
