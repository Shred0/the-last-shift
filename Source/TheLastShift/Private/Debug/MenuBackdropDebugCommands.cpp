// SPDX-License-Identifier: MIT

// tls.MenuBackdropCheck [Quit] (MainMenu, PIE in its own window so that a shot with the UI is the game's window, or a
// package): the main menu's moving background (MenuBackdrop in UTheLastShiftSettings, UMenuBackdropWidget).
//  1  the structure: its twelve edges of points at the picture's centre, lit on a shot; its turn read back from its drawn
//     vertex over 10 s of frames at the settings' speed and at a speed set for the session, never stopping;
//  2  stillness: no particle moves between the end of a pulse's steps and the next pulse, frame after frame;
//  3  the pulse: its period (the settings' and one set for the session), each particle brightening and stepping once, in
//     the order of its distance, by StepShare toward the centre, still and at rest again before the next pulse;
//  4  the field over 3 minutes of the picture: the count shown, the comings back at the edge, nothing on the structure at
//     an arrival; every lit pixel of a shot in home's tint;
//  5  the menu read: the background's mean level in the menu's column over 10 s with two pulses, each label's contrast
//     against the frame with the background off;
//  6  where it is seen: the menu, the settings, the rebinding rows, the credits, the list of acts; nothing drawn under a
//     text scene, the ending, the loss screen and the travel screen;
//  7  the same picture with no save, a save at a battery of 0.05 and after an ending (the save is put back);
//  8  the switch: off, no background and the screens' backdrops as the style made them, the same frame twice;
//  9  the frame rate: the period and the turn over 10 s with the limit at 30 and with it lifted;
//  10 the cost: GPU time on and off over 240 frames, caps lifted;
//  11 shots to Saved/Screenshots/Brief42.
// It puts the values, the player's frame-rate settings and the save back as it found them.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "FrameCapsTestScope.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Run/MenuBackdropWidget.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunMainMenuWidget.h"
#include "Run/RunSaveGame.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Settings/StdProjectSettings.h"
#include "Settings/StdSettingsSubsystem.h"
#include "TheLastShift.h"
#include "TlsDebugCommon.h"
#include "UI/StdScreenWidget.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace MenuBackdropCheck
{
	struct FStep
	{
		FString Label;
		TFunction<float()> Run;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FShot
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};

	/** One painted frame, read on the next engine tick. */
	struct FSample
	{
		double Real = 0.0;
		double Picture = 0.0;
		double Spin = 0.0;
		int32 Pulse = 0;
		uint64 Frame = 0;
		int32 Points = 0;
		FString Cover;
		TArray<FVector3f> Positions;
	};

	struct FRun
	{
		TArray<FStep> Steps;
		int32 Next = 0;
		double WaitUntil = 0.0;
		double StepStarted = -1.0;
		int32 Passed = 0;
		int32 Failed = 0;
		bool bQuit = false;
		FString Map;
		FTSTicker::FDelegateHandle Ticker;
		FTSTicker::FDelegateHandle Sampler;
		FDelegateHandle ShotHandle;
		FString ShotName;
		bool bShotReady = false;
		FShot Shot;
		TMap<FString, FShot> Shots;
		FMenuBackdropTuning Saved;
		int32 SavedLimit = 60;
		bool bSavedUnlimited = false;
		bool bSettingsSaved = false;
		// samples of the real-time runs
		TArray<FSample> Samples;
		bool bKeepPositions = false;
		TMap<FString, TArray<FSample>> Runs;
		// the menu read
		TArray<double> ColumnMeans;
		TMap<FString, int32> OffContrast;
		int32 WorstContrastStep = 0;
		FString WorstLabel;
		// the save
		TArray<uint8> SaveBytes;
		bool bHadSave = false;
		// the cost
		TArray<double> Gpu;
		TArray<double> GpuOn;
		TArray<double> GpuOff;
		uint64 CoverFrame = 0;
		TMap<FString, double> GpuMedian;
		TWeakObjectPtr<UStdScreenWidget> Opened;
	};
	static FRun G;

	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static URunMainMenuWidget* Menu()
	{
		UWorld* World = W();
		for (TObjectIterator<URunMainMenuWidget> It; It; ++It)
		{
			if (World && It->GetWorld() == World && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}
	static UMenuBackdropWidget* Backdrop() { URunMainMenuWidget* M = Menu(); return M ? M->GetBackdrop() : nullptr; }
	static UStdScreenWidget* Top() { return TimeStateDebug::TopScreen(W()); }
	static FMenuBackdropTuning Tuning() { return UTheLastShiftSettings::Get()->GetMenuBackdropTuning(); }
	static FMenuBackdropTuning& Mutable() { return GetMutableDefault<UTheLastShiftSettings>()->MenuBackdrop; }
	static UStdSettingsSubsystem* SS() { return FrameCapsTest::Settings(W()); }

	static void Note(const FString& Text) { UE_LOG(LogTheLastShift, Display, TEXT("MenuBackdropCheck   ..  %s"), *Text); }
	static void Check(bool bOk, const FString& Text)
	{
		(bOk ? G.Passed : G.Failed)++;
		UE_LOG(LogTheLastShift, Display, TEXT("MenuBackdropCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *Text);
	}

	static FStep Do(const FString& Label, TFunction<float()> Fn) { FStep S; S.Label = Label; S.Run = MoveTemp(Fn); return S; }
	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep S;
		S.Label = Label;
		S.Until = MoveTemp(Until);
		S.Timeout = Timeout;
		S.Run = Then ? MoveTemp(Then) : TFunction<float()>([]() { return 0.f; });
		return S;
	}

	static int32 Level(const FColor& C) { return FMath::Max3(C.R, C.G, C.B); }
	static double Luma(const FColor& C) { return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B; }

	/** Hue in degrees of an 8-bit colour, and its saturation (0..1). */
	static float Hue(const FColor& C, float* OutSaturation = nullptr)
	{
		const FLinearColor HSV = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f).LinearRGBToHSV();
		if (OutSaturation)
		{
			*OutSaturation = HSV.G;
		}
		return HSV.R;
	}

	static float HueGap(float A, float B)
	{
		const float D = FMath::Abs(A - B);
		return FMath::Min(D, 360.f - D);
	}

	/** Home's tint as the screen shows it (sRGB), at a level. */
	static FColor TintOnScreen(float LevelShare)
	{
		const FLinearColor Tint = UTheLastShiftSettings::Get()->GetSurfaceVariant(true).Tint;
		return FLinearColor(Tint.R * LevelShare, Tint.G * LevelShare, Tint.B * LevelShare, 1.f).ToFColor(true);
	}

	// --- shots ---

	static FString ShotPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief42"), Name + TEXT(".png")));
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		G.Shot.Width = Width;
		G.Shot.Height = Height;
		G.Shot.Pixels = Pixels;
		for (FColor& C : G.Shot.Pixels)
		{
			C.A = 255;
		}
		G.Shots.Add(G.ShotName, G.Shot);
		if (!G.ShotName.StartsWith(TEXT("_")))
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(G.Shot.Pixels.GetData(), G.Shot.Pixels.Num()), Png);
			const FString Path = ShotPath(G.ShotName);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s (%d x %d): %s"), *G.ShotName, Width, Height, *Path));
		}
		G.bShotReady = true;
	}

	/** A shot of the game's window with its UI; a name starting with _ is measured and not saved. */
	static void RequestShot(const FString& Name)
	{
		if (!G.ShotHandle.IsValid())
		{
			G.ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		G.ShotName = Name;
		G.bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), true, false);
	}

	static void AddShot(TArray<FStep>& S, const FString& Name, float Settle = 0.2f, TFunction<void()> Then = nullptr)
	{
		S.Add(Do(Name + TEXT(": settle"), [Settle]() { return Settle; }));
		S.Add(Do(Name + TEXT(": shot"), [Name]() { RequestShot(Name); return 0.f; }));
		S.Add(WaitFor(Name + TEXT(": shot taken"), []() { return G.bShotReady; }, 5.f, [Then]() { if (Then) { Then(); } return 0.f; }));
	}

	/** A point in Slate absolute (desktop) coordinates as a pixel of a shot with the UI. */
	static FVector2D ToShotPixel(const FVector2D& Absolute)
	{
		const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		return Window.IsValid() ? Absolute - FVector2D(Window->GetPositionInScreen()) : Absolute;
	}

	struct FRect
	{
		int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
		bool IsValid() const { return X1 > X0 && Y1 > Y0; }
	};

	static FRect RectOf(const UWidget* Widget, const FShot& Shot)
	{
		FRect R;
		if (!Widget)
		{
			return R;
		}
		const FGeometry Geo = Widget->GetCachedGeometry();
		const FVector2D P0 = ToShotPixel(Geo.LocalToAbsolute(FVector2D::ZeroVector));
		const FVector2D P1 = ToShotPixel(Geo.LocalToAbsolute(Geo.GetLocalSize()));
		R.X0 = FMath::Clamp(FMath::FloorToInt(P0.X), 0, Shot.Width);
		R.Y0 = FMath::Clamp(FMath::FloorToInt(P0.Y), 0, Shot.Height);
		R.X1 = FMath::Clamp(FMath::CeilToInt(P1.X), 0, Shot.Width);
		R.Y1 = FMath::Clamp(FMath::CeilToInt(P1.Y), 0, Shot.Height);
		return R;
	}

	/** The backdrop's picture geometry in shot pixels: its centre and the structure's radius. */
	static bool PictureOnShot(const FShot& Shot, FVector2D& OutCentre, double& OutScale)
	{
		const UMenuBackdropWidget* B = Backdrop();
		if (!B)
		{
			return false;
		}
		const FGeometry Geo = B->GetCachedGeometry();
		const FVector2D Local = Geo.GetLocalSize();
		const FVector2D A0 = ToShotPixel(Geo.LocalToAbsolute(FVector2D::ZeroVector));
		const FVector2D A1 = ToShotPixel(Geo.LocalToAbsolute(Local));
		const double PixelsPerUnit = Local.Y > 0.0 ? (A1.Y - A0.Y) / Local.Y : 1.0;
		OutCentre = 0.5 * (A0 + A1);
		OutScale = Tuning().StructureScreenShare * Local.Y * PixelsPerUnit;
		return Shot.Width > 0;
	}

	/** The contrast of a label on a shot: its brightest 2% luma against the median of its rectangle. */
	static int32 LabelContrast(const FShot& Shot, const FRect& R)
	{
		TArray<double> L;
		for (int32 Y = R.Y0; Y < R.Y1; ++Y)
		{
			for (int32 X = R.X0; X < R.X1; ++X)
			{
				L.Add(Luma(Shot.Pixels[Y * Shot.Width + X]));
			}
		}
		if (L.Num() < 50)
		{
			return -1;
		}
		L.Sort();
		const int32 Top = FMath::Max(1, L.Num() / 50);
		double Core = 0.0;
		for (int32 I = L.Num() - Top; I < L.Num(); ++I)
		{
			Core += L[I];
		}
		return FMath::RoundToInt32(Core / Top - L[L.Num() / 2]);
	}

	/** The menu's labels shown now: the title and the visible entries' texts. */
	static TArray<UTextBlock*> MenuLabels()
	{
		TArray<UTextBlock*> Out;
		URunMainMenuWidget* M = Menu();
		if (!M || !M->WidgetTree)
		{
			return Out;
		}
		static const TCHAR* Names[] = { TEXT("Text_Title"), TEXT("Text_Continue"), TEXT("Text_SelectAct"), TEXT("Text_NewGame"), TEXT("Text_Settings"), TEXT("Text_Credits"), TEXT("Text_Quit") };
		for (const TCHAR* Name : Names)
		{
			UTextBlock* T = Cast<UTextBlock>(M->WidgetTree->FindWidget(Name));
			const UWidget* Button = T ? T->GetParent() : nullptr;
			if (T && T->IsVisible() && (!Button || Button->IsVisible()) && !T->GetText().IsEmpty())
			{
				Out.Add(T);
			}
		}
		return Out;
	}

	static void SetMenuShown(bool bShown)
	{
		if (URunMainMenuWidget* M = Menu())
		{
			M->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
		}
	}

	// --- sampling the painted frames ---

	static bool SampleFrame(float)
	{
		const UMenuBackdropWidget* B = Backdrop();
		if (!B || B->GetPaintCount() == 0)
		{
			return true;
		}
		if (!G.Samples.IsEmpty() && G.Samples.Last().Frame == B->GetLastPaintFrame())
		{
			return true;
		}
		const FMenuBackdropFrame& F = B->GetLastFrame();
		FSample S;
		S.Real = FPlatformTime::Seconds();
		S.Frame = B->GetLastPaintFrame();
		S.Points = B->GetLastPaintedPoints();
		S.Cover = B->GetLastPaintCover();
		S.Picture = F.Seconds;
		S.Pulse = F.PulseIndex;
		const int32 Vertex = F.Points.Num() - F.StructurePoints;
		S.Spin = F.Points.IsValidIndex(Vertex) ? MenuBackdropPicture::SpinFromVertex(Tuning(), F.Points[Vertex].Position) : 0.0;
		if (G.bKeepPositions)
		{
			S.Positions.Reserve(F.Particles.Num());
			for (const FMenuBackdropParticle& P : F.Particles)
			{
				S.Positions.Add(P.Position);
			}
		}
		G.Samples.Add(MoveTemp(S));
		return true;
	}

	static void StartSampling(bool bPositions)
	{
		G.Samples.Reset();
		G.bKeepPositions = bPositions;
		if (!G.Sampler.IsValid())
		{
			G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&SampleFrame));
		}
	}

	static void StopSampling(const FString& Key)
	{
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
		}
		G.Runs.Add(Key, G.Samples);
	}

	/** The structure's turn per second of real time over a run, read back from its drawn vertex, and the smallest step between two frames. */
	static void SpinOf(const TArray<FSample>& Run, double& OutRate, double& OutTurn, double& OutReal, double& OutMinStep, int32& OutStopped)
	{
		OutRate = OutTurn = OutReal = 0.0;
		OutMinStep = TNumericLimits<double>::Max();
		OutStopped = 0;
		if (Run.Num() < 3)
		{
			return;
		}
		double Unwrapped = 0.0;
		for (int32 I = 1; I < Run.Num(); ++I)
		{
			double D = Run[I].Spin - Run[I - 1].Spin;
			D = FMath::Fmod(D + 540.0, 360.0) - 180.0;
			Unwrapped += D;
			if (Run[I].Picture > Run[I - 1].Picture)
			{
				OutMinStep = FMath::Min(OutMinStep, FMath::Abs(D));
				OutStopped += FMath::IsNearlyZero(D, 1.e-6) ? 1 : 0;
			}
		}
		OutTurn = Unwrapped;
		OutReal = Run.Last().Real - Run[0].Real;
		OutRate = OutReal > 0.0 ? Unwrapped / OutReal : 0.0;
	}

	/** Real times at which the pulse index went up, as intervals. */
	static TArray<double> PulseIntervals(const TArray<FSample>& Run, TArray<double>* OutPictureIntervals = nullptr)
	{
		TArray<double> Starts, PictureStarts;
		for (int32 I = 1; I < Run.Num(); ++I)
		{
			if (Run[I].Pulse > Run[I - 1].Pulse)
			{
				Starts.Add(Run[I].Real);
				PictureStarts.Add(Run[I].Picture);
			}
		}
		TArray<double> Out;
		for (int32 I = 1; I < Starts.Num(); ++I)
		{
			Out.Add(Starts[I] - Starts[I - 1]);
			if (OutPictureIntervals)
			{
				OutPictureIntervals->Add(PictureStarts[I] - PictureStarts[I - 1]);
			}
		}
		return Out;
	}

	static FString Join(const TArray<double>& Values)
	{
		FString Out;
		for (const double V : Values)
		{
			Out += FString::Printf(TEXT(" %.3f"), V);
		}
		return Out;
	}

	/** A real-time run of Seconds, the frames sampled, then its checks. */
	static void AddRealRun(TArray<FStep>& S, const FString& Key, float Seconds, bool bPositions, TFunction<void()> Before, TFunction<void()> After)
	{
		S.Add(Do(Key + TEXT(": set"), [Before]()
		{
			if (Before)
			{
				Before();
			}
			// A value or a frame-rate limit changed: the picture takes its new value at once (it is a function of the values
			// and of time); the frames are sampled from half a second later.
			return 0.5f;
		}));
		S.Add(Do(Key + TEXT(": start"), [Key, bPositions]()
		{
			StartSampling(bPositions);
			Note(FString::Printf(TEXT("%s: sampling the painted frames"), *Key));
			return 0.f;
		}));
		S.Add(Do(Key + TEXT(": run"), [Seconds]() { return Seconds; }));
		S.Add(Do(Key + TEXT(": stop"), [Key, After]()
		{
			StopSampling(Key);
			if (After)
			{
				After();
			}
			return 0.f;
		}));
	}

	// --- the run ---

	static void PutBack()
	{
		Mutable() = G.Saved;
		if (UMenuBackdropWidget* B = Backdrop())
		{
			B->ClearTimeOverride();
		}
		if (URunMainMenuWidget* M = Menu())
		{
			M->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			M->RefreshBackdrop();
		}
		if (G.bSettingsSaved)
		{
			FrameCapsTest::Restore(W(), TEXT("MenuBackdropCheck"));
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateLimit(G.SavedLimit);
				Sub->SetFrameRateUnlimited(G.bSavedUnlimited);
			}
			G.bSettingsSaved = false;
		}
	}

	static void Finish()
	{
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
		}
		if (G.ShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(G.ShotHandle);
			G.ShotHandle.Reset();
		}
		PutBack();
		UE_LOG(LogTheLastShift, Display, TEXT("MenuBackdropCheck summary (%s, %s): %d checks passed, %d failed."), *G.Map, G.Failed ? TEXT("FAIL") : TEXT("PASS"), G.Passed, G.Failed);
		if (G.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.MenuBackdropCheck"));
		}
	}

	static bool Tick(float)
	{
		if (FPlatformTime::Seconds() < G.WaitUntil)
		{
			return true;
		}
		if (!G.Steps.IsValidIndex(G.Next))
		{
			Finish();
			G.Ticker.Reset();
			return false;
		}
		FStep& Step = G.Steps[G.Next];
		if (Step.Until)
		{
			if (G.StepStarted < 0.0)
			{
				G.StepStarted = FPlatformTime::Seconds();
			}
			if (!Step.Until())
			{
				if (FPlatformTime::Seconds() - G.StepStarted > Step.Timeout)
				{
					Check(false, Step.Label + TEXT(" (timed out)"));
					G.StepStarted = -1.0;
					++G.Next;
				}
				return true;
			}
			G.StepStarted = -1.0;
		}
		++G.Next;
		const float Wait = Step.Run ? Step.Run() : 0.f;
		G.WaitUntil = FPlatformTime::Seconds() + Wait;
		return true;
	}

	// =================================================================================================================

	/** Waits until Frames more frames have been drawn. */
	static void AddWaitFrames(TArray<FStep>& S, const FString& Label, uint64 Frames = 2)
	{
		TSharedRef<uint64> From = MakeShared<uint64>(0);
		S.Add(Do(Label + TEXT(": from"), [From]() { *From = GFrameCounter; return 0.f; }));
		S.Add(WaitFor(Label, [From, Frames]() { return GFrameCounter >= *From + Frames; }, 3.f));
	}

	/** The picture held at Seconds on screen (the time override), two frames to be painted. */
	static void AddHold(TArray<FStep>& S, double Seconds)
	{
		S.Add(Do(FString::Printf(TEXT("hold the picture at %.3f s"), Seconds), [Seconds]()
		{
			if (UMenuBackdropWidget* B = Backdrop())
			{
				B->SetTimeOverride(Seconds);
			}
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("painted at that time"), [Seconds]()
		{
			const UMenuBackdropWidget* B = Backdrop();
			return B && FMath::IsNearlyEqual(B->GetLastFrame().Seconds, Seconds, 1.e-9);
		}, 3.f));
		AddWaitFrames(S, TEXT("two frames at that time"));
	}

	/** Every lit pixel (level 40 or more) of a shot in home's tint; the structure's points lit where they are drawn. */
	static void CheckPictureShot(const FShot& Shot, const FString& What)
	{
		const float TintHue = Hue(TintOnScreen(1.f));
		int32 Lit = 0, Other = 0;
		float WorstGap = 0.f;
		// The game's viewport only: the shot of a floating PIE window also holds its title bar and frame.
		FRect View = RectOf(Backdrop(), Shot);
		// Inset by 2 pixels: the window's own border (grey 51) lies on the viewport's last column.
		View.X0 += 2; View.Y0 += 2; View.X1 -= 2; View.Y1 -= 2;
		for (int32 Y = View.Y0; Y < View.Y1; ++Y)
		for (int32 X = View.X0; X < View.X1; ++X)
		{
			const FColor& C = Shot.Pixels[Y * Shot.Width + X];
			if (Level(C) < 40)
			{
				continue;
			}
			++Lit;
			const float Gap = HueGap(Hue(C), TintHue);
			WorstGap = FMath::Max(WorstGap, Gap);
			Other += Gap > 12.f ? 1 : 0;
		}
		Check(Lit > 200 && Other == 0, FString::Printf(TEXT("4 colour, %s: in the game's viewport (%d, %d to %d, %d) %d lit pixels (level 40 or more), %d of another hue than home's tint (%.1f degrees; widest gap %.1f, at most 12)"),
			*What, View.X0, View.Y0, View.X1, View.Y1, Lit, Other, TintHue, WorstGap));
	}

	static void BuildSteps(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("the main menu and its background"), []() { return Menu() && Backdrop() && Backdrop()->GetPaintCount() > 2; }, 20.f, []()
		{
			G.Saved = GetDefault<UTheLastShiftSettings>()->MenuBackdrop;
			if (UStdSettingsSubsystem* Sub = SS())
			{
				G.SavedLimit = Sub->GetFrameRateLimit();
				G.bSavedUnlimited = Sub->IsFrameRateUnlimited();
				G.bSettingsSaved = true;
			}
			const FMenuBackdropTuning T = Tuning();
			Note(FString::Printf(TEXT("values: pulse %.2f s (first %.2f), wave %.2f radii/s (the field crossed in %.2f s), %d particles to %.2f, step %.2f over %.2f s, spin %.1f deg/s, tilt %.1f, roll %.1f, structure %.3f of the height, %d points an edge"),
				T.PulseSeconds, T.FirstPulseSeconds, T.WaveSpeed, T.FieldRadius / T.WaveSpeed, T.ParticleCount, T.FieldRadius, T.StepShare, T.StepSeconds, T.SpinDegreesPerSecond, T.TiltDegrees, T.RollDegrees,
				T.StructureScreenShare, T.PointsPerEdge));
			Check(T.bMenuBackdrop && Backdrop()->GetCoverReason().IsEmpty() && Backdrop()->GetLastPaintedPoints() > T.ParticleCount,
				FString::Printf(TEXT("6 the main menu: the background is on, under the menu (cover \"%s\"), %d points drawn"), *Backdrop()->GetCoverReason(), Backdrop()->GetLastPaintedPoints()));
			Check(T.FieldRadius / T.WaveSpeed < 2.f,
				FString::Printf(TEXT("3 the wave crosses the whole field in %.2f s (under 2)"), T.FieldRadius / T.WaveSpeed));
			int32 SeeThrough = Backdrop()->GetSeeThroughCount();
			const UBorder* MenuBorder = Menu()->WidgetTree ? Cast<UBorder>(Menu()->WidgetTree->FindWidget(TEXT("Border_Background"))) : nullptr;
			Check(SeeThrough >= 1 && MenuBorder && MenuBorder->GetBrushColor().A == 0.f,
				FString::Printf(TEXT("6 the main menu's backdrop (the style's backdrop colour) is held see-through: %d held"), SeeThrough));
			return 0.f;
		}));

		// ---- 1, 4: the structure and the colour, at rest ----
		const FMenuBackdropTuning T0 = Tuning();
		const double Rest = MenuBackdropPicture::PulseStart(T0, 2) - 0.1;
		const double Halfway = MenuBackdropPicture::PulseStart(T0, 2) + 0.5 * T0.FieldRadius / T0.WaveSpeed;
		S.Add(Do(TEXT("menu hidden"), []() { SetMenuShown(false); return 0.f; }));
		AddHold(S, Rest);
		AddShot(S, TEXT("BackdropAtRest"), 0.1f, []()
		{
			const FShot& Shot = G.Shot;
			const UMenuBackdropWidget* B = Backdrop();
			const FMenuBackdropFrame& F = B->GetLastFrame();
			const FMenuBackdropTuning T = Tuning();
			FVector2D Centre;
			double Scale = 0.0;
			PictureOnShot(Shot, Centre, Scale);
			// The structure: 6 vertices and 12 edges of points.
			Check(F.StructurePoints == 6 + 12 * (T.PointsPerEdge - 2),
				FString::Printf(TEXT("1 the structure: %d points (6 vertices and 12 edges of %d points, the vertices shared)"), F.StructurePoints, T.PointsPerEdge));
			FVector2f Mean = FVector2f::ZeroVector;
			const int32 First = F.Points.Num() - F.StructurePoints;
			for (int32 I = First; I < F.Points.Num(); ++I)
			{
				Mean += F.Points[I].Position / static_cast<float>(F.StructurePoints);
			}
			int32 Lit = 0;
			for (int32 I = First; I < F.Points.Num(); ++I)
			{
				const FVector2f P = F.Points[I].Position;
				const int32 X = FMath::RoundToInt32(Centre.X + P.X * Scale);
				const int32 Y = FMath::RoundToInt32(Centre.Y - P.Y * Scale);
				if (X >= 0 && Y >= 0 && X < Shot.Width && Y < Shot.Height && Level(Shot.Pixels[Y * Shot.Width + X]) >= 30)
				{
					++Lit;
				}
			}
			Check(Mean.Size() < 1.e-3f && Lit >= F.StructurePoints * 95 / 100,
				FString::Printf(TEXT("1 the structure at the picture's centre (its points' mean %.4f from it, the centre on the shot at %.0f, %.0f of %d x %d, radius %.1f px); %d of its %d points lit on the shot"),
					Mean.Size(), Centre.X, Centre.Y, Shot.Width, Shot.Height, Scale, Lit, F.StructurePoints));
			CheckPictureShot(Shot, TEXT("at rest"));
		});
		AddHold(S, Halfway);
		AddShot(S, TEXT("BackdropPulse"), 0.1f, []() { CheckPictureShot(G.Shot, TEXT("as a pulse crosses the field")); });
		S.Add(Do(TEXT("menu shown"), []() { SetMenuShown(true); return 0.f; }));
		AddHold(S, Halfway);
		AddShot(S, TEXT("MenuPulse"));
		AddHold(S, Rest);
		AddShot(S, TEXT("MenuAtRest"));

		// ---- 3, 4: the model over time ----
		S.Add(Do(TEXT("3 the pulse, sampled at 240 a second"), []()
		{
			const FMenuBackdropTuning T = Tuning();
			const int32 M = 3;
			const double Start = MenuBackdropPicture::PulseStart(T, M);
			const double End = MenuBackdropPicture::PulseStart(T, M + 1);
			const double Dt = 1.0 / 240.0;
			FMenuBackdropFrame Before, Frame, Final;
			MenuBackdropPicture::Evaluate(T, Start - 1.e-4, Before);
			MenuBackdropPicture::Evaluate(T, End - 1.e-4, Final);
			const int32 N = T.ParticleCount;
			TArray<double> HitAt, PeakAt, PeakLevel;
			HitAt.Init(-1.0, N);
			PeakAt.Init(-1.0, N);
			PeakLevel.Init(-1.0, N);
			TArray<FVector3f> Last;
			Last.SetNum(N);
			TArray<double> StillSince;
			StillSince.Init(-1.0, N);
			int32 MovedLate = 0;
			for (double Time = Start; Time < End; Time += Dt)
			{
				MenuBackdropPicture::Evaluate(T, Time, Frame);
				for (int32 I = 0; I < N; ++I)
				{
					const FMenuBackdropParticle& P = Frame.Particles[I];
					if (HitAt[I] < 0.0 && P.Hits > Before.Particles[I].Hits)
					{
						HitAt[I] = Time;
					}
					if (P.Level > PeakLevel[I])
					{
						PeakLevel[I] = P.Level;
						PeakAt[I] = Time;
					}
					// Still once its step (or fade) is done: nothing may move after that until the next pulse.
					const double Done = P.LastHitSeconds + FMath::Max(T.StepSeconds, T.FadeSeconds);
					if (HitAt[I] >= 0.0 && Time > Done + Dt && P.Position != Last[I])
					{
						++MovedLate;
					}
					Last[I] = P.Position;
				}
			}
			int32 NotHit = 0, Order = 0, Steps = 0, Back = 0, BadStep = 0, BadPeak = 0, NotRest = 0;
			TArray<int32> Sorted;
			for (int32 I = 0; I < N; ++I)
			{
				NotHit += HitAt[I] < 0.0 || Final.Particles[I].Hits != Before.Particles[I].Hits + 1 ? 1 : 0;
				Sorted.Add(I);
				const FMenuBackdropParticle& A = Before.Particles[I];
				const FMenuBackdropParticle& B = Final.Particles[I];
				if (B.bCameBack)
				{
					++Back;
					BadStep += A.Distance - T.StepShare < 1.f && B.Distance >= T.FieldRadius - T.StepShare - 1.e-3f ? 0 : 1;
				}
				else
				{
					++Steps;
					const float Cos = FVector3f::DotProduct(A.Position.GetSafeNormal(), B.Position.GetSafeNormal());
					BadStep += FMath::IsNearlyEqual(A.Distance - B.Distance, T.StepShare, 1.e-3f) && Cos > 0.99999f && B.Distance >= 1.f ? 0 : 1;
				}
				BadPeak += PeakLevel[I] >= 0.98f * T.ParticlePeakLevel && FMath::Abs(PeakAt[I] - HitAt[I]) <= Dt + 1.e-6 ? 0 : 1;
				NotRest += FMath::IsNearlyEqual(B.Level, T.ParticleRestLevel, 1.e-4f) && FMath::IsNearlyEqual(B.Weight, 1.f, 1.e-4f) ? 0 : 1;
			}
			Sorted.Sort([&Before](int32 A, int32 B) { return Before.Particles[A].Distance < Before.Particles[B].Distance; });
			double Lag = 0.0;
			for (int32 K = 1; K < Sorted.Num(); ++K)
			{
				Order += HitAt[Sorted[K]] + 1.e-9 < HitAt[Sorted[K - 1]] ? 1 : 0;
			}
			for (int32 I = 0; I < N; ++I)
			{
				Lag = FMath::Max(Lag, FMath::Abs(HitAt[I] - (Start + Before.Particles[I].Distance / T.WaveSpeed)));
			}
			Check(NotHit == 0, FString::Printf(TEXT("3 pulse %d: each of the %d particles reached once by its wave (%d not)"), M, N, NotHit));
			Check(Order == 0 && Lag <= Dt + 1.e-6, FString::Printf(TEXT("3 the order: sorted by their distance from the centre, %d particles reached before a nearer one; each at its distance over the wave's speed within %.4f s"), Order, Lag));
			Check(BadStep == 0, FString::Printf(TEXT("3 the step: %d particles stepped %.2f toward the centre along their line, %d came back at the edge (their step would have entered the structure), %d wrong"), Steps, T.StepShare, Back, BadStep));
			Check(BadPeak == 0, FString::Printf(TEXT("3 the brightness: every particle at its peak (%.2f) as its wave reaches it, %d not"), T.ParticlePeakLevel, BadPeak));
			Check(NotRest == 0 && MovedLate == 0, FString::Printf(TEXT("3 still and at rest before the next pulse: %d particles not back at rest (%.2f), %d moves after a step was done"), NotRest, T.ParticleRestLevel, MovedLate));
			return 0.f;
		}));
		S.Add(Do(TEXT("4 the field over 3 minutes, 30 samples a second"), []()
		{
			const FMenuBackdropTuning T = Tuning();
			FMenuBackdropFrame F;
			int32 MinShown = MAX_int32, MaxShown = 0, Arrivals = 0, ArrivalFlash = 0, BadArrival = 0, StructureOff = 0, Samples = 0;
			TArray<int32> LastLife;
			LastLife.Init(-1, T.ParticleCount);
			for (double Time = 0.0; Time <= 180.0; Time += 1.0 / 30.0)
			{
				MenuBackdropPicture::Evaluate(T, Time, F);
				++Samples;
				MinShown = FMath::Min(MinShown, F.ShownParticles);
				MaxShown = FMath::Max(MaxShown, F.ShownParticles);
				for (const FMenuBackdropPoint& P : F.Points)
				{
					if (P.bArriving)
					{
						BadArrival += P.Distance <= T.FieldRadius + 1.e-3f && P.Distance >= T.FieldRadius - T.StepShare - 1.e-3f ? 0 : 1;
						ArrivalFlash += P.Level > T.ParticleRestLevel + 1.e-5f ? 1 : 0;
					}
				}
				for (int32 I = 0; I < F.Particles.Num(); ++I)
				{
					if (LastLife[I] >= 0 && F.Particles[I].Life != LastLife[I])
					{
						++Arrivals;
					}
					LastLife[I] = F.Particles[I].Life;
				}
				// The structure answers the pulse only: its level is the pulse's, whatever comes back meanwhile.
				const float Expected = T.StructureRestLevel + (T.StructurePeakLevel - T.StructureRestLevel)
					* (F.PulseIndex > 0 && Time - F.PulseStartSeconds < T.StructureGlowSeconds ? FMath::Square(1.f - static_cast<float>((Time - F.PulseStartSeconds) / T.StructureGlowSeconds)) : 0.f);
				StructureOff += FMath::IsNearlyEqual(F.StructureLevel, Expected, 1.e-4f) ? 0 : 1;
			}
			const int32 Tolerance = FMath::CeilToInt32(0.05f * T.ParticleCount);
			Check(FMath::Abs(MinShown - T.ParticleCount) <= Tolerance && FMath::Abs(MaxShown - T.ParticleCount) <= Tolerance,
				FString::Printf(TEXT("4 the count shown over 180 s (%d samples): %d to %d, ParticleCount %d (within 5%%: %d)"), Samples, MinShown, MaxShown, T.ParticleCount, Tolerance));
			Check(Arrivals > 0 && BadArrival == 0 && ArrivalFlash == 0,
				FString::Printf(TEXT("4 comings back: %d particles faded out at the structure and came back at the outer edge fading in, %d not at the edge, %d brighter than rest as they arrive"), Arrivals, BadArrival, ArrivalFlash));
			Check(StructureOff == 0, FString::Printf(TEXT("4 the structure shows nothing at an arrival: its level is the pulse's at every sample (%d off)"), StructureOff));
			return 0.f;
		}));

		// ---- 1, 2, 3, 9: real-time runs ----
		S.Add(Do(TEXT("back to the real clock"), []() { if (UMenuBackdropWidget* B = Backdrop()) { B->ClearTimeOverride(); } return 0.2f; }));
		AddRealRun(S, TEXT("limit30"), 10.f, true, []()
		{
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateUnlimited(false);
				Sub->SetFrameRateLimit(30);
			}
		}, nullptr);
		AddRealRun(S, TEXT("lifted"), 10.f, true, []()
		{
			FrameCapsTest::Lift(W(), TEXT("MenuBackdropCheck"));
		}, []()
		{
			FrameCapsTest::Restore(W(), TEXT("MenuBackdropCheck"));
		});
		S.Add(Do(TEXT("1 2 3 9: the runs"), []()
		{
			const FMenuBackdropTuning T = Tuning();
			double Rates[2] = {}, Turns[2] = {}, Reals[2] = {};
			TArray<double> Periods[2];
			const TCHAR* Keys[2] = { TEXT("limit30"), TEXT("lifted") };
			for (int32 K = 0; K < 2; ++K)
			{
				const TArray<FSample>& Run = G.Runs.FindRef(Keys[K]);
				double MinStep = 0.0;
				int32 Stopped = 0;
				SpinOf(Run, Rates[K], Turns[K], Reals[K], MinStep, Stopped);
				const double Fps = Reals[K] > 0.0 ? (Run.Num() - 1) / Reals[K] : 0.0;
				Check(Run.Num() > 100 && FMath::Abs(Rates[K] - T.SpinDegreesPerSecond) <= 0.02 * FMath::Abs(T.SpinDegreesPerSecond) && Stopped == 0,
					FString::Printf(TEXT("1 %s: %d frames over %.2f s (%.1f a second); the structure turned %.2f degrees, %.3f a second read from its drawn vertex (SpinDegreesPerSecond %.2f, within 2%%); %d frames without a turn, the smallest step %.4f degrees"),
						Keys[K], Run.Num(), Reals[K], Fps, Turns[K], Rates[K], T.SpinDegreesPerSecond, Stopped, MinStep));
				TArray<double> Picture;
				Periods[K] = PulseIntervals(Run, &Picture);
				// Stillness: between the end of the steps and the next pulse, frame after frame.
				int32 Pairs = 0, Moved = 0;
				const double Settle = T.FieldRadius / T.WaveSpeed + FMath::Max(T.StepSeconds, T.FadeSeconds) + 0.02;
				for (int32 I = 1; I < Run.Num(); ++I)
				{
					const FSample& A = Run[I - 1];
					const FSample& B = Run[I];
					if (A.Pulse < 1 || B.Pulse != A.Pulse || A.Picture < MenuBackdropPicture::PulseStart(T, A.Pulse) + Settle || A.Positions.Num() != B.Positions.Num())
					{
						continue;
					}
					++Pairs;
					for (int32 P = 0; P < A.Positions.Num(); ++P)
					{
						if (A.Positions[P] != B.Positions[P])
						{
							++Moved;
							break;
						}
					}
				}
				Check(Pairs > 30 && Moved == 0, FString::Printf(TEXT("2 %s: %d pairs of frames between the end of a pulse's steps and the next pulse, %d with a particle moved (the structure turning meanwhile)"), Keys[K], Pairs, Moved));
				Check(Periods[K].Num() >= 1 && Algo::AllOf(Periods[K], [&T](double P) { return FMath::Abs(P - T.PulseSeconds) <= 0.1; }),
					FString::Printf(TEXT("3 %s: pulses every%s s of real time (picture:%s), PulseSeconds %.2f (within 0.1)"), Keys[K], *Join(Periods[K]), *Join(Picture), T.PulseSeconds));
			}
			const double Turn0 = Rates[0] * 10.0, Turn1 = Rates[1] * 10.0;
			const double P0 = Periods[0].IsEmpty() ? -1.0 : Periods[0][0];
			const double P1 = Periods[1].IsEmpty() ? -1.0 : Periods[1][0];
			Check(FMath::Abs(Turn0 - Turn1) <= 0.01 * FMath::Abs(Turn1) && FMath::Abs(P0 - P1) <= 2.0 / 30.0,
				FString::Printf(TEXT("9 the same at 30 and lifted: the turn over 10 s %.3f and %.3f degrees, the pulse's period %.3f and %.3f s (within two frames at 30)"), Turn0, Turn1, P0, P1));
			return 0.f;
		}));
		AddRealRun(S, TEXT("session"), 10.f, false, []()
		{
			FMenuBackdropTuning& M = Mutable();
			M.SpinDegreesPerSecond = 20.f;
			M.PulseSeconds = 3.f;
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateUnlimited(G.bSavedUnlimited);
				Sub->SetFrameRateLimit(G.SavedLimit);
			}
		}, []()
		{
			const TArray<FSample>& Run = G.Runs.FindRef(TEXT("session"));
			double Rate = 0.0, Turn = 0.0, Real = 0.0, MinStep = 0.0;
			int32 Stopped = 0;
			SpinOf(Run, Rate, Turn, Real, MinStep, Stopped);
			Check(FMath::Abs(Rate - 20.0) <= 0.4 && Stopped == 0,
				FString::Printf(TEXT("1 SpinDegreesPerSecond 20 set for the session: %.3f degrees a second over %.2f s (%d frames), %d frames without a turn"), Rate, Real, Run.Num(), Stopped));
			TArray<double> Picture;
			const TArray<double> Periods = PulseIntervals(Run, &Picture);
			Check(Periods.Num() >= 2 && Algo::AllOf(Periods, [](double P) { return FMath::Abs(P - 3.0) <= 0.1; }),
				FString::Printf(TEXT("3 PulseSeconds 3 set for the session: pulses every%s s (picture:%s)"), *Join(Periods), *Join(Picture)));
			Mutable() = G.Saved;
		});

		// ---- 5: the menu read ----
		S.Add(Do(TEXT("5 the frame with the background off"), []()
		{
			Mutable().bMenuBackdrop = false;
			Menu()->RefreshBackdrop();
			return 0.3f;
		}));
		AddShot(S, TEXT("_off5"), 0.1f, []()
		{
			for (UTextBlock* Label : MenuLabels())
			{
				G.OffContrast.Add(Label->GetName(), LabelContrast(G.Shot, RectOf(Label, G.Shot)));
			}
			FString List;
			for (const TPair<FString, int32>& P : G.OffContrast)
			{
				List += FString::Printf(TEXT(" %s %d"), *P.Key, P.Value);
			}
			Note(FString::Printf(TEXT("5 the labels' contrast with the background off:%s"), *List));
		});
		S.Add(Do(TEXT("5 the background on again"), []()
		{
			Mutable().bMenuBackdrop = true;
			Menu()->RefreshBackdrop();
			return 0.3f;
		}));
		{
			TArray<double> Times;
			for (int32 K = 0; K < 20; ++K)
			{
				Times.Add(0.25 + 0.5 * K);
			}
			// The waves halfway across the field, and their peaks near the centre.
			for (int32 M = 1; M <= 2; ++M)
			{
				const double Start = MenuBackdropPicture::PulseStart(T0, M);
				Times.Add(Start + 0.05);
				Times.Add(Start + 0.5 * T0.FieldRadius / T0.WaveSpeed);
				Times.Add(Start + 0.9 * T0.FieldRadius / T0.WaveSpeed);
			}
			Times.Sort();
			for (const double Time : Times)
			{
				AddHold(S, Time);
				S.Add(Do(TEXT("menu hidden"), []() { SetMenuShown(false); return 0.f; }));
				AddWaitFrames(S, TEXT("painted without the menu"));
				AddShot(S, TEXT("_bg5"), 0.f, []()
				{
					const FShot& Shot = G.Shot;
					SetMenuShown(true);
					const UWidget* Column = Menu()->WidgetTree ? Menu()->WidgetTree->FindWidget(TEXT("SizeBox_Column")) : nullptr;
					const FRect R = RectOf(Column, Shot);
					double Sum = 0.0;
					int32 N = 0;
					for (int32 Y = R.Y0; Y < R.Y1; ++Y)
					{
						for (int32 X = R.X0; X < R.X1; ++X)
						{
							Sum += Level(Shot.Pixels[Y * Shot.Width + X]);
							++N;
						}
					}
					G.ColumnMeans.Add(N ? Sum / N : -1.0);
				});
				AddWaitFrames(S, TEXT("painted with the menu"));
				AddShot(S, TEXT("_on5"), 0.f, [Time]()
				{
					for (UTextBlock* Label : MenuLabels())
					{
						const int32 On = LabelContrast(G.Shot, RectOf(Label, G.Shot));
						const int32* Off = G.OffContrast.Find(Label->GetName());
						const int32 Gap = Off ? FMath::Abs(On - *Off) : 999;
						if (Gap >= G.WorstContrastStep)
						{
							G.WorstContrastStep = Gap;
							G.WorstLabel = FString::Printf(TEXT("%s at %.2f s (%d on, %d off)"), *Label->GetName(), Time, On, Off ? *Off : -1);
						}
					}
				});
			}
		}
		S.Add(Do(TEXT("5 the menu read: the results"), []()
		{
			double Worst = 0.0;
			for (const double M : G.ColumnMeans)
			{
				Worst = FMath::Max(Worst, M);
			}
			Check(G.ColumnMeans.Num() >= 20 && Worst <= 20.0 && !Algo::AnyOf(G.ColumnMeans, [](double M) { return M < 0.0; }),
				FString::Printf(TEXT("5 the background's mean level in the rectangle of the title and the entries over 10 s with two pulses (%d frames): at most %.2f of 255 (at most 20)"), G.ColumnMeans.Num(), Worst));
			Check(!G.OffContrast.IsEmpty() && G.WorstContrastStep <= 10,
				FString::Printf(TEXT("5 each label's contrast against what is behind it within 10 levels of the frame with the background off: the widest gap %d, %s"), G.WorstContrastStep, *G.WorstLabel));
			if (UMenuBackdropWidget* B = Backdrop())
			{
				B->ClearTimeOverride();
			}
			return 0.f;
		}));

		// ---- 6: where it is seen ----
		const auto Shown = [](const FString& Where, int32 MinSeeThrough)
		{
			const UMenuBackdropWidget* B = Backdrop();
			const UStdScreenWidget* T = Top();
			const UBorder* Border = T && T->WidgetTree ? Cast<UBorder>(T->WidgetTree->FindWidget(TEXT("Border_Background"))) : nullptr;
			Check(B && B->GetLastPaintCover().IsEmpty() && B->GetLastPaintedPoints() > Tuning().ParticleCount && B->GetSeeThroughCount() >= MinSeeThrough && (!Border || Border->GetBrushColor().A == 0.f),
				FString::Printf(TEXT("6 %s (%s): the background drawn behind it (%d points, cover \"%s\"), its backdrop see-through (%s), %d held"), *Where, T ? *T->GetClass()->GetName() : TEXT("none"),
					B ? B->GetLastPaintedPoints() : -1, B ? *B->GetLastPaintCover() : TEXT("-"), Border ? *FString::Printf(TEXT("alpha %.2f"), Border->GetBrushColor().A) : TEXT("none"), B ? B->GetSeeThroughCount() : -1));
		};
		const auto LitTint = [](const FShot& Shot)
		{
			const float TintHue = Hue(TintOnScreen(1.f));
			int32 N = 0;
			for (const FColor& C : Shot.Pixels)
			{
				float Sat = 0.f;
				const float H = Hue(C, &Sat);
				N += Level(C) >= 25 && Sat > 0.25f && HueGap(H, TintHue) <= 12.f ? 1 : 0;
			}
			return N;
		};
		S.Add(Do(TEXT("settings: open"), []()
		{
			UButton* Button = Menu()->FindMenuButton(TEXT("Button_Settings"));
			if (Button)
			{
				Button->OnClicked.Broadcast();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("settings: shown"), [Shown]() { Shown(TEXT("the settings"), 1); return 0.f; }));
		AddShot(S, TEXT("Settings"), 0.1f, [LitTint]()
		{
			const int32 N = LitTint(G.Shot);
			Check(N > 100, FString::Printf(TEXT("6 the settings over it: %d pixels of home's tint on the shot"), N));
		});
		S.Add(Do(TEXT("rebinding"), []()
		{
			UStdScreenWidget* T = Top();
			UButton* Tab = T && T->WidgetTree ? Cast<UButton>(T->WidgetTree->FindWidget(TEXT("Button_TabControls"))) : nullptr;
			Check(Tab != nullptr, TEXT("6 the settings' Controls tab found"));
			if (Tab)
			{
				Tab->OnClicked.Broadcast();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("rebinding: shown"), [Shown]() { Shown(TEXT("the rebinding rows"), 1); return 0.f; }));
		AddShot(S, TEXT("Rebinding"));
		S.Add(Do(TEXT("settings: close"), []() { if (UStdScreenWidget* T = Top()) { if (T != Menu()) { T->CloseScreen(); } } return 0.4f; }));
		S.Add(Do(TEXT("credits"), []()
		{
			if (UButton* Button = Menu()->FindMenuButton(TEXT("Button_Credits")))
			{
				Button->OnClicked.Broadcast();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("credits: shown, close"), [Shown]()
		{
			Shown(TEXT("the credits"), 1);
			if (UStdScreenWidget* T = Top()) { if (T != Menu()) { T->CloseScreen(); } }
			return 0.4f;
		}));
		S.Add(Do(TEXT("the list of acts"), []()
		{
			URunActListWidget* List = Cast<URunActListWidget>(Menu()->OpenChildScreen(URunActListWidget::StaticClass()));
			if (List)
			{
				List->Configure(ERunActListMode::SelectAct);
			}
			G.Opened = List;
			return 0.5f;
		}));
		S.Add(Do(TEXT("the list of acts: shown, close"), [Shown]()
		{
			Shown(TEXT("the list of acts"), 1);
			if (UStdScreenWidget* L = G.Opened.Get()) { L->CloseScreen(); }
			return 0.4f;
		}));
		// Covered: count the frames painted with points while each cover is up.
		const auto AddCovered = [](TArray<FStep>& Steps, const FString& What, const FString& Want, TFunction<void()> Open, TFunction<void()> Close, const FString& ShotName)
		{
			Steps.Add(Do(What + TEXT(": open"), [Open]() { Open(); return 0.4f; }));
			Steps.Add(Do(What + TEXT(": sample"), []() { StartSampling(false); return 0.6f; }));
			Steps.Add(Do(What + TEXT(": covered"), [What, Want]()
			{
				StopSampling(What);
				const TArray<FSample>& Run = G.Runs.FindRef(What);
				int32 Drawn = 0;
				FString Covers;
				for (const FSample& Sm : Run)
				{
					Drawn += Sm.Points > 0 ? 1 : 0;
					if (!Covers.Contains(Sm.Cover))
					{
						Covers += TEXT(" \"") + Sm.Cover + TEXT("\"");
					}
				}
				const UMenuBackdropWidget* B = Backdrop();
				Check(B && Run.Num() >= 5 && Drawn == 0 && B->GetCoverReason().StartsWith(Want),
					FString::Printf(TEXT("6 %s: %d frames painted, %d with a point of the background (covered by%s; now \"%s\")"), *What, Run.Num(), Drawn, *Covers, B ? *B->GetCoverReason() : TEXT("-")));
				return 0.f;
			}));
			if (!ShotName.IsEmpty())
			{
				AddShot(Steps, ShotName, 0.f, []()
				{
					const float TintHue = Hue(TintOnScreen(1.f));
					int32 N = 0;
					for (const FColor& C : G.Shot.Pixels)
					{
						float Sat = 0.f;
						const float H = Hue(C, &Sat);
						N += Level(C) >= 25 && Sat > 0.25f && HueGap(H, TintHue) <= 12.f ? 1 : 0;
					}
					Check(N == 0, FString::Printf(TEXT("6 the text scene's shot (a line on the paradise's white): %d pixels of home's tint"), N));
				});
			}
			Steps.Add(Do(What + TEXT(": close"), [Close]() { Close(); return 0.4f; }));
		};
		AddCovered(S, TEXT("a text scene"), TEXT("scene"), []()
		{
			URunTextSceneWidget* Scene = CreateWidget<URunTextSceneWidget>(PC(), URunTextSceneWidget::StaticClass());
			G.Opened = Scene;
			TArray<FRunSceneLine> Lines;
			FRunSceneLine White;
			White.RowName = TEXT("MenuBackdropCheck_1");
			White.Voice = ESceneVoice::Paradise;
			White.Text = TEXT("[PLACEHOLDER] A fixture line of the menu's background check, on the paradise's white.");
			Lines.Add(White);
			if (Scene)
			{
				Scene->ShowAsRoot(60);
				Scene->Play(ERunSceneKind::EndingText, TEXT("MenuBackdropCheck"), Lines);
			}
		}, []() { if (URunTextSceneWidget* Scene = Cast<URunTextSceneWidget>(G.Opened.Get())) { Scene->Skip(); } }, TEXT("TextScene"));
		AddCovered(S, TEXT("the ending"), TEXT("ending"), []()
		{
			URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(W());
			G.Opened = Scenes ? Scenes->OpenEnding(Menu()) : nullptr;
		}, []() { if (UStdScreenWidget* E = G.Opened.Get()) { E->CloseScreen(); } }, FString());
		AddCovered(S, TEXT("the loss screen"), TEXT("screen"), []()
		{
			URunActListWidget* Loss = CreateWidget<URunActListWidget>(PC(), URunActListWidget::StaticClass());
			if (Loss)
			{
				Loss->Configure(ERunActListMode::RunLost);
				Loss->ShowAsRoot(60);
			}
			G.Opened = Loss;
		}, []() { if (UStdScreenWidget* L = G.Opened.Get()) { L->CloseScreen(); } }, FString());
		S.Add(Do(TEXT("back on the menu"), []()
		{
			if (UStdScreenWidget* M = Menu())
			{
				M->FocusDefaultWidget();
			}
			return 0.3f;
		}));
		S.Add(Do(TEXT("6 the menu again"), [Shown]() { Shown(TEXT("the main menu after them"), 1); return 0.f; }));

		// ---- 7: nothing of the run ----
		const double Seven = 7.3;
		const auto AddRunCase = [Seven](TArray<FStep>& Steps, const FString& Name, TFunction<void()> Set)
		{
			Steps.Add(Do(Name + TEXT(": set"), [Set]() { Set(); return 0.3f; }));
			AddHold(Steps, Seven);
			Steps.Add(Do(TEXT("menu hidden"), []() { SetMenuShown(false); return 0.f; }));
			AddWaitFrames(Steps, TEXT("painted without the menu"));
			AddShot(Steps, TEXT("_run_") + Name, 0.f, []() { SetMenuShown(true); });
		};
		S.Add(Do(TEXT("7 the save, kept aside"), []()
		{
			const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav"));
			G.bHadSave = FFileHelper::LoadFileToArray(G.SaveBytes, *Path);
			Note(FString::Printf(TEXT("7 the save %s: %s"), *Path, G.bHadSave ? *FString::Printf(TEXT("%d bytes kept aside"), G.SaveBytes.Num()) : TEXT("none")));
			return 0.f;
		}));
		AddRunCase(S, TEXT("nosave"), []() { if (URunSubsystem* Run = URunSubsystem::Get(W())) { Run->ClearSave(); } });
		AddRunCase(S, TEXT("battery005"), []()
		{
			URunSaveGame* Save = Cast<URunSaveGame>(UGameplayStatics::CreateSaveGameObject(URunSaveGame::StaticClass()));
			if (Save)
			{
				Save->Version = URunSaveGame::CurrentVersion;
				FAreaSnapshot Snapshot;
				Snapshot.ActIndex = 1;
				Snapshot.BatteryFraction = 0.05;
				Save->Snapshots.Add(Snapshot);
				UGameplayStatics::SaveGameToSlot(Save, URunSubsystem::SaveSlotName(), 0);
			}
			URunSubsystem* Run = URunSubsystem::Get(W());
			Check(Run && Run->LoadSave() && Run->HasSavedRun(), TEXT("7 a save at a battery of 0.05 loaded"));
		});
		AddRunCase(S, TEXT("afterending"), []()
		{
			URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(W());
			if (URunEndingWidget* Ending = Scenes ? Scenes->OpenEnding(Menu()) : nullptr)
			{
				Ending->CloseScreen();
				Check(true, TEXT("7 an ending opened over the menu and closed"));
			}
			else
			{
				Check(false, TEXT("7 an ending opened over the menu and closed"));
			}
		});
		S.Add(Do(TEXT("7 the same picture"), []()
		{
			const FShot* A = G.Shots.Find(TEXT("_run_nosave"));
			const FShot* B = G.Shots.Find(TEXT("_run_battery005"));
			const FShot* C = G.Shots.Find(TEXT("_run_afterending"));
			const auto Differ = [](const FShot* X, const FShot* Y)
			{
				if (!X || !Y || X->Pixels.Num() != Y->Pixels.Num())
				{
					return -1;
				}
				int32 N = 0;
				for (int32 I = 0; I < X->Pixels.Num(); ++I)
				{
					N += X->Pixels[I] != Y->Pixels[I] ? 1 : 0;
				}
				return N;
			};
			const int32 AB = Differ(A, B), AC = Differ(A, C);
			Check(AB == 0 && AC == 0, FString::Printf(TEXT("7 the picture at 7.3 s with no save, with a save at a battery of 0.05 and after an ending: %d and %d pixels differ from the first"), AB, AC));
			// The save back as it was.
			URunSubsystem* Run = URunSubsystem::Get(W());
			const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav"));
			if (G.bHadSave)
			{
				const bool bWritten = FFileHelper::SaveArrayToFile(G.SaveBytes, *Path);
				const bool bLoaded = Run && Run->LoadSave();
				Check(bWritten && bLoaded, FString::Printf(TEXT("7 the save put back (%d bytes) and read again"), G.SaveBytes.Num()));
			}
			else if (Run)
			{
				Run->ClearSave();
				Check(!Run->HasSavedRun(), TEXT("7 no save, as found"));
			}
			if (UMenuBackdropWidget* Bd = Backdrop())
			{
				Bd->ClearTimeOverride();
			}
			return 0.3f;
		}));

		// ---- 8: the switch ----
		S.Add(Do(TEXT("8 off"), []()
		{
			Mutable().bMenuBackdrop = false;
			Menu()->RefreshBackdrop();
			if (UStdScreenWidget* M = Menu())
			{
				M->FocusDefaultWidget();
			}
			return 0.4f;
		}));
		S.Add(Do(TEXT("8 off: nothing of it"), []()
		{
			int32 InViewport = 0;
			for (TObjectIterator<UMenuBackdropWidget> It; It; ++It)
			{
				InViewport += It->GetWorld() == W() && It->IsInViewport() ? 1 : 0;
			}
			const UBorder* Border = Menu()->WidgetTree ? Cast<UBorder>(Menu()->WidgetTree->FindWidget(TEXT("Border_Background"))) : nullptr;
			Check(!Backdrop() && InViewport == 0 && Border && Border->GetBrushColor().A == 1.f,
				FString::Printf(TEXT("8 off: no background in the viewport (%d), the menu's backdrop as the style made it (alpha %.2f)"), InViewport, Border ? Border->GetBrushColor().A : -1.f));
			return 0.f;
		}));
		AddShot(S, TEXT("MenuOff"));
		S.Add(Do(TEXT("8 on and off again"), []()
		{
			Mutable().bMenuBackdrop = true;
			Menu()->RefreshBackdrop();
			return 0.5f;
		}));
		S.Add(Do(TEXT("8 off again"), []()
		{
			Check(Backdrop() != nullptr, TEXT("8 on again: the background is back"));
			Mutable().bMenuBackdrop = false;
			Menu()->RefreshBackdrop();
			if (UStdScreenWidget* M = Menu())
			{
				M->FocusDefaultWidget();
			}
			return 0.4f;
		}));
		AddShot(S, TEXT("_off8b"), 0.1f, []()
		{
			const FShot* A = G.Shots.Find(TEXT("MenuOff"));
			int32 N = -1;
			if (A && A->Pixels.Num() == G.Shot.Pixels.Num())
			{
				N = 0;
				for (int32 I = 0; I < A->Pixels.Num(); ++I)
				{
					N += A->Pixels[I] != G.Shot.Pixels[I] ? 1 : 0;
				}
			}
			int32 Lit = 0;
			for (const FColor& C : G.Shot.Pixels)
			{
				Lit += Level(C) > 3 ? 1 : 0;
			}
			Check(N == 0, FString::Printf(TEXT("8 off, then on, then off: the menu's frame the same pixel for pixel (%d differ); %d lit pixels, the menu's alone"), N, Lit));
		});
		S.Add(Do(TEXT("8 off: the settings"), []()
		{
			if (UButton* Button = Menu()->FindMenuButton(TEXT("Button_Settings")))
			{
				Button->OnClicked.Broadcast();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("8 off: the settings' backdrop"), []()
		{
			const UStdScreenWidget* T = Top();
			const UBorder* Border = T && T->WidgetTree ? Cast<UBorder>(T->WidgetTree->FindWidget(TEXT("Border_Background"))) : nullptr;
			Check(Border && Border->GetBrushColor().A == 1.f, FString::Printf(TEXT("8 off: the settings' backdrop as the style made it (alpha %.2f)"), Border ? Border->GetBrushColor().A : -1.f));
			if (UStdScreenWidget* Top2 = Top()) { if (Top2 != Menu()) { Top2->CloseScreen(); } }
			return 0.4f;
		}));

		// ---- 10: the cost ----
		const auto AddGpu = [](TArray<FStep>& Steps, const FString& Key, bool bOn)
		{
			Steps.Add(Do(Key + TEXT(": set"), [bOn]()
			{
				Mutable().bMenuBackdrop = bOn;
				Menu()->RefreshBackdrop();
				FrameCapsTest::Lift(W(), TEXT("MenuBackdropCheck"));
				return 1.f;
			}));
			Steps.Add(Do(Key + TEXT(": start"), []()
			{
				G.Gpu.Reset();
				G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					G.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
					return G.Gpu.Num() < 120;
				}));
				return 0.f;
			}));
			Steps.Add(WaitFor(Key + TEXT(": 120 frames"), []() { return G.Gpu.Num() >= 120; }, 30.f, [Key, bOn]()
			{
				G.Sampler.Reset();
				TArray<double> Sorted = G.Gpu;
				Sorted.Sort();
				const double Median = Sorted.IsEmpty() ? -1.0 : Sorted[Sorted.Num() / 2];
				(bOn ? G.GpuOn : G.GpuOff).Append(G.Gpu);
				Note(FString::Printf(TEXT("10 %s: GPU time over %d frames: median %.3f ms, 90th %.3f ms"), *Key, Sorted.Num(), Median, Sorted.IsEmpty() ? -1.0 : Sorted[Sorted.Num() * 9 / 10]));
				FrameCapsTest::Restore(W(), TEXT("MenuBackdropCheck"));
				return 0.f;
			}));
		};
		// Rounds off and on in turn (PIE's GPU times drift and spike from one batch to the next): the medians of all.
		for (int32 Round = 1; Round <= 3; ++Round)
		{
			AddGpu(S, FString::Printf(TEXT("cost off %d"), Round), false);
			AddGpu(S, FString::Printf(TEXT("cost on %d"), Round), true);
		}
		S.Add(Do(TEXT("10 the cost"), []()
		{
			const auto Median = [](TArray<double> V) { V.Sort(); return V.IsEmpty() ? -1.0 : V[V.Num() / 2]; };
			const double Off = Median(G.GpuOff);
			const double On = Median(G.GpuOn);
			Check(Off > 0.0 && On > 0.0 && On - Off <= 0.5,
				FString::Printf(TEXT("10 the cost in PIE: GPU time of the main menu, median of %d frames with the background %.3f ms, of %d without %.3f ms (three rounds each, in turn): %+.3f ms (at most 0.5)"),
					G.GpuOn.Num(), On, G.GpuOff.Num(), Off, On - Off));
			Mutable() = G.Saved;
			Menu()->RefreshBackdrop();
			return 0.3f;
		}));

		// ---- 6: the travel screen, last ----
		S.Add(Do(TEXT("the travel screen: start"), []()
		{
			PutBack();
			UWorld* World = W();
			UStdFlowSubsystem* Flow = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			FStdTravelOptions Options;
			Options.MinCoveredSeconds = 3.f;
			Check(Flow && Flow->TravelToLevelWithOptions(UStdProjectSettings::Get()->MainMenuMap, Options), TEXT("6 a travel to the main menu starts"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("the travel screen: covered"), []()
		{
			UWorld* World = W();
			const UStdFlowSubsystem* Flow = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return Flow && Flow->GetOverlayAlpha() >= 1.f;
		}, 10.f, []()
		{
			G.CoverFrame = GFrameCounter;
			StartSampling(false);
			return 0.3f;
		}));
		S.Add(Do(TEXT("the travel screen: sampled"), []()
		{
			StopSampling(TEXT("travel"));
			const TArray<FSample>& Run = G.Runs.FindRef(TEXT("travel"));
			int32 Drawn = 0, Before = 0;
			for (const FSample& Sm : Run)
			{
				// A frame painted before the cover was full (the sampler's first read can be the frame before) is the menu's fade.
				if (Sm.Frame <= G.CoverFrame)
				{
					++Before;
					continue;
				}
				Drawn += Sm.Points > 0 ? 1 : 0;
				if (Sm.Points > 0)
				{
					Note(FString::Printf(TEXT("6 the travel screen: frame %llu (cover from %llu) drew %d points, cover \"%s\""), Sm.Frame, G.CoverFrame, Sm.Points, *Sm.Cover));
				}
			}
			Note(FString::Printf(TEXT("6 the travel screen: %d sampled frames painted before the cover was full, left out"), Before));
			// Once the old world is gone there is no menu, and no background, at all: no frame can carry a point.
			Check(Drawn == 0, FString::Printf(TEXT("6 the travel screen at full cover: %d frames painted by the background, %d with a point"), Run.Num() - Before, Drawn));
			return 0.f;
		}));
	}

	static void Start(bool bQuit)
	{
		if (G.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Ticker);
		}
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
		}
		G = FRun();
		G.bQuit = bQuit;
		UWorld* World = W();
		G.Map = World ? FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FString(TEXT("none"));
		if (G.Map == TEXT("MainMenu"))
		{
			BuildSteps(G.Steps);
		}
		else
		{
			Check(false, FString::Printf(TEXT("tls.MenuBackdropCheck runs in MainMenu (here: %s)"), *G.Map));
		}
		UE_LOG(LogTheLastShift, Display, TEXT("MenuBackdropCheck: %s, %d steps."), *G.Map, G.Steps.Num());
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsMenuBackdropCheckCommand(
	TEXT("tls.MenuBackdropCheck"),
	TEXT("tls.MenuBackdropCheck [Quit=0]: (MainMenu; PIE in its own window, or a package) the main menu's moving background: the structure and its turn, the stillness between pulses, the pulse, the field over 3 minutes, the menu read, where it is seen and covered, nothing of the run, the switch, the frame rate, the cost. Shots to Saved/Screenshots/Brief42. Puts the values, the frame-rate settings and the save back."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		MenuBackdropCheck::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMenuBackdropCommand(
	TEXT("tls.MenuBackdrop"),
	TEXT("tls.MenuBackdrop [0|1] [Seconds]: switches the main menu's background off or on for the session and, given, holds its picture at Seconds (below 0: its own clock again); no argument: logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() > 0)
		{
			MenuBackdropCheck::Mutable().bMenuBackdrop = FCString::Atoi(*Args[0]) != 0;
			if (URunMainMenuWidget* M = MenuBackdropCheck::Menu())
			{
				M->RefreshBackdrop();
			}
		}
		UMenuBackdropWidget* B = MenuBackdropCheck::Backdrop();
		if (B && Args.Num() > 1)
		{
			const double Seconds = FCString::Atod(*Args[1]);
			if (Seconds < 0.0)
			{
				B->ClearTimeOverride();
			}
			else
			{
				B->SetTimeOverride(Seconds);
			}
		}
		const FMenuBackdropFrame* F = B ? &B->GetLastFrame() : nullptr;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.MenuBackdrop: %s; picture %.3f s%s, pulse %d, spin %.2f degrees, %d particles shown, %d points drawn, cover \"%s\", %d backdrops held see-through"),
			B ? TEXT("on") : TEXT("off (or no main menu)"), B ? B->GetPictureSeconds() : 0.0, B && B->HasTimeOverride() ? TEXT(" (held)") : TEXT(""), F ? F->PulseIndex : 0,
			F ? F->SpinDegrees : 0.0, F ? F->ShownParticles : 0, B ? B->GetLastPaintedPoints() : 0, B ? *B->GetCoverReason() : TEXT("-"), B ? B->GetSeeThroughCount() : 0);
	}));

#endif // !UE_BUILD_SHIPPING
