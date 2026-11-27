// SPDX-License-Identifier: MIT

// tls.VideoCheck [Quit] | tls.VideoCheck restart [Quit]: the video options of StdCore's settings screen (frame-rate limit
// with its Unlimited box, anti-aliasing, V-Sync). Development builds only. Run in MainMenu, PIE in its own window (the
// shots with the UI are the game's window) or a package, the window in front.
//  - The main run checks: the three rows in the style's font with a fresh settings file (60, not unlimited, the project's
//    anti-aliasing, V-Sync off), keys and mouse reaching and changing every row; the limit at 30 and at 120 measured over
//    5 seconds, Unlimited lifting it and disabling the choice, which keeps its value; each anti-aliasing method the screen
//    offers reaching the engine and the renderer's view, V-Sync on and off reaching the engine; the three values saved
//    and read back from the file on disk; a settings file of the build before (Saved/Brief39/GameUserSettings_preBrief39.ini)
//    loading with its volumes and the three options at their defaults; a project default changed followed by a fresh
//    settings file and a hidden row absent with its option at its default; the uncapped tests' lift and its end. Then it
//    travels to Area2_ServerHalls: the first hall's entry Stopped, one scan shot under each anti-aliasing method (still,
//    again 6 frames later, and while the view turns), and ten scans with the limit at 30 and ten with it lifted, each
//    with whether it got its lines. It ends with the three values changed and saved, for the restart stage.
//  - The restart stage (a fresh PIE session, MainMenu): the values the game instance pushed when it started, before its
//    first map's first frame, and the screen showing them; then the player's settings as the main run found them.
// Shots to Saved/Screenshots/Brief39. The run's save is put back as it was (the travel into area 2 writes it).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Area/AreaGenerator.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Debug/FrameCapsTestScope.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LidarScanComponent.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Run/RunSubsystem.h"
#include "Scan/ScanDirectorComponent.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Settings/StdProjectSettings.h"
#include "Settings/StdSettingsSubsystem.h"
#include "Settings/StdUserSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdScreenWidget.h"
#include "UI/StdSettingsWidget.h"
#include "UI/StdUITheme.h"
#include "UI/StdUIStyle.h"
#include "UnrealClient.h"

namespace VideoCheck
{
	/** The anti-aliasing method of the last view the renderer set up for the test's world. */
	class FViewWatch : public FSceneViewExtensionBase
	{
	public:
		FViewWatch(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
		{
			const UWorld* Want = World.Get();
			if (!Want || !InViewFamily.Scene || InViewFamily.Scene->GetWorld() != Want || InViewFamily.Views.IsEmpty() || !InViewFamily.Views[0])
			{
				return;
			}
			Method = static_cast<int32>(InViewFamily.Views[0]->AntiAliasingMethod);
			Frame = GFrameCounter;
		}

		TWeakObjectPtr<UWorld> World;
		int32 Method = -1;
		uint64 Frame = 0;
	};

	/** Counts the lidar's "gets no lines" warnings while it is registered. */
	class FNoLinesCounter : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (FCString::Strstr(V, TEXT("gets no lines")) && !FCString::Strstr(V, TEXT("VideoCheck")))
			{
				++Count;
				Last = V;
			}
		}
		int32 Count = 0;
		FString Last;
	};

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

	struct FScanResult
	{
		int32 Index = -1;
		bool bLines = false;
		bool bRejected = false;
		int32 Links = 0;
		float ReadySeconds = 0.f;
		bool bTimedOut = false;
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
		bool bRestartStage = false;
		FString Map;
		FTSTicker::FDelegateHandle Ticker;
		FTSTicker::FDelegateHandle Sampler;
		// settings as found
		TMap<FString, FString> SavedUser;
		int32 SavedDefaultLimit = 60;
		bool bSavedDefaultUnlimited = false;
		int32 SavedDefaultMethod = -1;
		bool bSavedDefaultVSync = false;
		TArray<EStdVideoOption> SavedHidden;
		TArray<int32> SavedLimits;
		// the run's save as found
		bool bHadRunSave = false;
		TArray<uint8> RunSaveBytes;
		// keys and mouse
		FVector2D MouseAt = FVector2D::ZeroVector;
		TArray<FString> Focused;
		// frame rate
		uint64 MeasureFrame = 0;
		double MeasureStart = 0.0;
		double LastFps = 0.0;
		// renderer
		TSharedPtr<FViewWatch, ESPMode::ThreadSafe> Watch;
		// shots
		FDelegateHandle ShotHandle;
		FString ShotName;
		bool bShotReady = false;
		TMap<FString, FShot> Shots;
		// turning the view for the moving shot
		int32 TurnFrames = 0;
		float TurnDegrees = 0.f;
		// scans
		FNoLinesCounter NoLines;
		bool bNoLinesRegistered = false;
		int32 LastLinkScan = -1;
		TArray<FScanResult> Scans30;
		TArray<FScanResult> ScansLifted;
		FScanResult Pending;
		double ScanAsked = 0.0;
		TArray<FString> Report;
	};
	static FRun G;

	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static UStdSettingsSubsystem* SS() { return FrameCapsTest::Settings(W()); }
	static UStdUserSettings* User() { return UStdUserSettings::Get(); }
	static UStdProjectSettings* Project() { return GetMutableDefault<UStdProjectSettings>(); }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static int32 EngineInt(const TCHAR* Name) { return FrameCapsTest::EngineInt(Name); }

	static void Note(const FString& Text) { UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck   ..  %s"), *Text); }
	static void Check(bool bOk, const FString& Text)
	{
		(bOk ? G.Passed : G.Failed)++;
		UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *Text);
	}
	static void ReportLine(const FString& Text)
	{
		G.Report.Add(Text);
		UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck   REPORT  %s"), *Text);
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

	// --- the settings as found, and put back ---

	static TMap<FString, FString> ExportUser()
	{
		TMap<FString, FString> Out;
		const UStdUserSettings* Object = User();
		for (TFieldIterator<FProperty> It(UStdUserSettings::StaticClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Config))
			{
				FString Value;
				It->ExportTextItem_InContainer(Value, Object, nullptr, nullptr, PPF_None);
				Out.Add(It->GetName(), Value);
			}
		}
		return Out;
	}

	static void ImportUser(const TMap<FString, FString>& Values)
	{
		UStdUserSettings* Object = User();
		for (TFieldIterator<FProperty> It(UStdUserSettings::StaticClass()); It; ++It)
		{
			if (const FString* Value = It->HasAnyPropertyFlags(CPF_Config) ? Values.Find(It->GetName()) : nullptr)
			{
				It->ImportText_InContainer(**Value, Object, Object, PPF_None);
			}
		}
	}

	static FString DescribeUser(const TMap<FString, FString>& Values)
	{
		TArray<FString> Parts;
		for (const TPair<FString, FString>& Pair : Values)
		{
			Parts.Add(Pair.Key + TEXT("=") + Pair.Value);
		}
		return FString::Join(Parts, TEXT(", "));
	}

	static void SaveProject()
	{
		const UStdProjectSettings* P = Project();
		G.SavedDefaultLimit = P->DefaultFrameRateLimit;
		G.bSavedDefaultUnlimited = P->bDefaultFrameRateUnlimited;
		G.SavedDefaultMethod = P->DefaultAntiAliasingMethod;
		G.bSavedDefaultVSync = P->bDefaultVSync;
		G.SavedHidden = P->HiddenVideoOptions;
		G.SavedLimits = P->FrameRateLimits;
	}

	static void RestoreProject()
	{
		UStdProjectSettings* P = Project();
		P->DefaultFrameRateLimit = G.SavedDefaultLimit;
		P->bDefaultFrameRateUnlimited = G.bSavedDefaultUnlimited;
		P->DefaultAntiAliasingMethod = G.SavedDefaultMethod;
		P->bDefaultVSync = G.bSavedDefaultVSync;
		P->HiddenVideoOptions = G.SavedHidden;
		P->FrameRateLimits = G.SavedLimits;
	}

	static FString RunSavePath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav")); }
	static FString RestartPath() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief39"), TEXT("VideoCheck_Restart.txt"))); }
	static FString OldFilePath() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief39"), TEXT("GameUserSettings_preBrief39.ini"))); }
	static FString WrittenCopyPath() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief39"), TEXT("GameUserSettings_written.ini"))); }

	static void SaveRunFile()
	{
		G.bHadRunSave = FFileHelper::LoadFileToArray(G.RunSaveBytes, *RunSavePath());
		Note(FString::Printf(TEXT("the run's save %s: %s (%d bytes); put back at the end"), *RunSavePath(), G.bHadRunSave ? TEXT("found") : TEXT("none"), G.RunSaveBytes.Num()));
	}

	static void RestoreRunFile()
	{
		if (G.bHadRunSave)
		{
			const bool bOk = FFileHelper::SaveArrayToFile(G.RunSaveBytes, *RunSavePath());
			Check(bOk, FString::Printf(TEXT("the run's save written back as found (%d bytes)"), G.RunSaveBytes.Num()));
		}
		else
		{
			IFileManager::Get().Delete(*RunSavePath());
			Check(!IFileManager::Get().FileExists(*RunSavePath()), TEXT("the run's save removed again: there was none"));
		}
	}

	/** A fresh object loaded from a settings file on disk, as a new process would: every value at its default, then the file. */
	static UStdUserSettings* LoadFromFile(const FString& Path)
	{
		UStdUserSettings* Object = NewObject<UStdUserSettings>(GetTransientPackage());
		Object->ResetToDefaults();
		Object->LoadConfig(nullptr, *Path);
		GConfig->UnloadFile(Path);
		return Object;
	}

	// --- shots ---

	static FString ShotPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief39"), Name + TEXT(".png")));
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		FShot Shot;
		Shot.Width = Width;
		Shot.Height = Height;
		Shot.Pixels = Pixels;
		for (FColor& C : Shot.Pixels)
		{
			C.A = 255;
		}
		if (!G.ShotName.StartsWith(TEXT("_")))
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Shot.Pixels.GetData(), Shot.Pixels.Num()), Png);
			const FString Path = ShotPath(G.ShotName);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s (%d x %d): %s"), *G.ShotName, Width, Height, *Path));
		}
		G.Shots.Add(G.ShotName, MoveTemp(Shot));
		G.bShotReady = true;
	}

	static void RequestShot(const FString& Name, bool bUI)
	{
		if (!G.ShotHandle.IsValid())
		{
			G.ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		G.ShotName = Name;
		G.bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), bUI, false);
	}

	static void AddShot(TArray<FStep>& S, const FString& Name, bool bUI, float Settle = 0.3f)
	{
		S.Add(Do(Name + TEXT(": settle"), [Settle]() { return Settle; }));
		S.Add(Do(Name + TEXT(": shot"), [Name, bUI]() { RequestShot(Name, bUI); return 0.f; }));
		S.Add(WaitFor(Name + TEXT(": shot taken"), []() { return G.bShotReady; }, 5.f));
	}

	static int32 Level(const FColor& C) { return FMath::Max3(C.R, C.G, C.B); }

	/** Lit pixels (above 40 levels), bright ones (above 200), their mean level, and how many differ by more than 12 from Other. */
	static FString DescribeScanShot(const FShot& Shot, const FShot* Other, int32* OutLit = nullptr, double* OutMean = nullptr)
	{
		int32 Lit = 0, Bright = 0, Differ = 0;
		double Sum = 0.0;
		for (int32 I = 0; I < Shot.Pixels.Num(); ++I)
		{
			const int32 L = Level(Shot.Pixels[I]);
			if (L > 40)
			{
				++Lit;
				Sum += L;
			}
			Bright += L > 200 ? 1 : 0;
			if (Other && Other->Pixels.Num() == Shot.Pixels.Num())
			{
				Differ += FMath::Abs(L - Level(Other->Pixels[I])) > 12 ? 1 : 0;
			}
		}
		const double Mean = Lit ? Sum / Lit : 0.0;
		if (OutLit) { *OutLit = Lit; }
		if (OutMean) { *OutMean = Mean; }
		return FString::Printf(TEXT("lit %d px (%.2f%%), bright %d px, mean lit level %.0f%s"), Lit, Shot.Pixels.Num() ? 100.0 * Lit / Shot.Pixels.Num() : 0.0, Bright, Mean,
			Other ? *FString::Printf(TEXT(", %d px differ by more than 12 levels from the other shot"), Differ) : TEXT(""));
	}

	// --- Slate input: real keys and mouse events, as the player's ---

	static void SlateKey(const FKey& Key)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		}
	}

	static FVector2D WidgetPoint(const UWidget* Widget)
	{
		if (!Widget)
		{
			return FVector2D::ZeroVector;
		}
		const FGeometry& Geo = Widget->GetCachedGeometry();
		return FVector2D(Geo.GetAbsolutePosition()) + FVector2D(Geo.GetAbsoluteSize()) * 0.5;
	}

	/**
	 * The widgets under Position in the game's own window: the events go to the game's window whatever other window of
	 * the editor lies over it.
	 */
	static FWidgetPath PathAt(const FVector2D& Position)
	{
		const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		if (!Window.IsValid() || !FSlateApplication::IsInitialized())
		{
			return FWidgetPath();
		}
		const TArray<TSharedRef<SWindow>> Windows = { Window.ToSharedRef() };
		return FSlateApplication::Get().LocateWindowUnderMouse(Position, Windows, false, FSlateApplication::CursorUserIndex);
	}

	/** The pointer moved to Position (desktop pixels): the platform's cursor too, so Slate's own synthesized moves agree. */
	static void MouseTo(const FVector2D& Position)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		const FVector2D Last = G.MouseAt.IsZero() ? Position : G.MouseAt;
		// The game's window in front of the editor's (same place, same size): Slate's own moves at the cursor find it too.
		if (const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr)
		{
			Window->BringToFront(true);
		}
		Slate.SetCursorPos(Position);
		const FWidgetPath Path = PathAt(Position);
		Slate.RoutePointerMoveEvent(Path, FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Last, TSet<FKey>(),
			EKeys::Invalid, 0.f, FModifierKeysState()), false);
		G.MouseAt = Position;
		Note(FString::Printf(TEXT("mouse at (%.0f, %.0f): %s"), Position.X, Position.Y, Path.IsValid() ? *Path.Widgets.Last().Widget->ToString() : TEXT("<nothing>")));
	}

	static void Click(const FVector2D& Position)
	{
		MouseTo(Position);
		FSlateApplication& Slate = FSlateApplication::Get();
		const FWidgetPath Path = PathAt(Position);
		Slate.RoutePointerDownEvent(Path, FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
			TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
		Slate.RoutePointerUpEvent(PathAt(Position), FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
			TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	}

	// --- the settings screen ---

	static UStdScreenWidget* Top() { return TimeStateDebug::TopScreen(W()); }
	static UStdSettingsWidget* Screen() { return Cast<UStdSettingsWidget>(Top()); }

	template <typename T> static T* Named(const TCHAR* Name)
	{
		const UStdSettingsWidget* S = Screen();
		return S && S->WidgetTree ? Cast<T>(S->WidgetTree->FindWidget(Name)) : nullptr;
	}

	static FString TextOf(const TCHAR* Name)
	{
		const UTextBlock* Text = Named<UTextBlock>(Name);
		return Text ? Text->GetText().ToString() : FString(TEXT("<none>"));
	}

	static bool IsChecked(const TCHAR* Name)
	{
		const UCheckBox* Box = Named<UCheckBox>(Name);
		return Box && Box->IsChecked();
	}

	static FString FocusedName()
	{
		const UStdSettingsWidget* S = Screen();
		APlayerController* C = PC();
		FString Out = TEXT("<none>");
		if (S && S->WidgetTree && C)
		{
			S->WidgetTree->ForEachWidget([&Out, C](UWidget* Child)
			{
				if ((Child->IsA<UButton>() || Child->IsA<UCheckBox>() || Child->IsA<USlider>()) && (Child->HasUserFocus(C) || Child->HasUserFocusedDescendants(C)))
				{
					Out = Child->GetName();
				}
			});
		}
		return Out;
	}

	static void FocusNamed(const TCHAR* Name)
	{
		if (UWidget* Widget = Named<UWidget>(Name))
		{
			Widget->SetFocus();
		}
	}

	static bool OpenSettings()
	{
		UStdScreenWidget* T = Top();
		if (Cast<UStdSettingsWidget>(T))
		{
			return true;
		}
		UButton* Button = T && T->WidgetTree ? Cast<UButton>(T->WidgetTree->FindWidget(TEXT("Button_Settings"))) : nullptr;
		if (!Button)
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		return true;
	}

	static void CloseSettings()
	{
		if (UStdSettingsWidget* S = Screen())
		{
			S->CloseScreen();
		}
	}

	static FString AAName(int32 Method)
	{
		static const TCHAR* Names[] = { TEXT("Off"), TEXT("FXAA"), TEXT("TAA"), TEXT("MSAA"), TEXT("TSR") };
		return Method >= 0 && Method < UE_ARRAY_COUNT(Names) ? Names[Method] : FString::FromInt(Method);
	}

	static FString DescribeEngine()
	{
		return FString::Printf(TEXT("engine: t.MaxFPS %d, r.AntiAliasingMethod %d, r.VSync %d, r.PostProcessAAQuality %d; renderer's view %d (frame %llu)"),
			EngineInt(TEXT("t.MaxFPS")), EngineInt(TEXT("r.AntiAliasingMethod")), EngineInt(TEXT("r.VSync")), EngineInt(TEXT("r.PostProcessAAQuality")),
			G.Watch ? G.Watch->Method : -1, G.Watch ? G.Watch->Frame : 0);
	}

	// --- the run ---

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
		if (G.bNoLinesRegistered && GLog)
		{
			GLog->RemoveOutputDevice(&G.NoLines);
			G.bNoLinesRegistered = false;
		}
		G.Watch.Reset();
		for (const FString& Line : G.Report)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck REPORT %s"), *Line);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck summary (%s, %s, %s): %d checks passed, %d failed."), G.bRestartStage ? TEXT("restart") : TEXT("main run"), *G.Map,
			G.Failed ? TEXT("FAIL") : TEXT("PASS"), G.Passed, G.Failed);
		if (G.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.VideoCheck"));
		}
	}

	static bool Tick(float)
	{
		// The entity stays dormant through the scans.
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->ResetCumulativeStoppedSeconds();
		}
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

	/** The frame rate over Seconds of real time, measured from the frame counter. */
	static void AddMeasure(TArray<FStep>& S, const FString& Label, float Seconds, TFunction<void(double)> Then)
	{
		S.Add(Do(Label + TEXT(": settle"), []() { return 1.5f; }));
		S.Add(Do(Label + TEXT(": start"), [Seconds]()
		{
			G.MeasureFrame = GFrameCounter;
			G.MeasureStart = FPlatformTime::Seconds();
			return Seconds;
		}));
		S.Add(Do(Label + TEXT(": end"), [Label, Then]()
		{
			const double Elapsed = FPlatformTime::Seconds() - G.MeasureStart;
			G.LastFps = Elapsed > 0.0 ? static_cast<double>(GFrameCounter - G.MeasureFrame) / Elapsed : 0.0;
			Note(FString::Printf(TEXT("%s: %.2f frames a second over %.2f s (%llu frames); %s"), *Label, G.LastFps, Elapsed, GFrameCounter - G.MeasureFrame, *DescribeEngine()));
			Then(G.LastFps);
			return 0.f;
		}));
	}

	// =================================================================================================================
	// The main run, MainMenu
	// =================================================================================================================

	static void BuildMenu(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("start: the settings as found"), []()
		{
			G.SavedUser = ExportUser();
			SaveProject();
			SaveRunFile();
			Note(FString::Printf(TEXT("the player's settings as found: %s"), *DescribeUser(G.SavedUser)));
			Note(FString::Printf(TEXT("the project's video values: frame rates %d entries, default limit %d, unlimited %d, anti-aliasing %d (project's own %d), V-Sync %d, %d hidden"),
				Project()->FrameRateLimits.Num(), Project()->DefaultFrameRateLimit, Project()->bDefaultFrameRateUnlimited, Project()->DefaultAntiAliasingMethod,
				UStdUserSettings::GetProjectAntiAliasingMethod(), Project()->bDefaultVSync, Project()->HiddenVideoOptions.Num()));
			Check(SS() != nullptr, TEXT("StdCore's settings subsystem"));
			// A fresh settings file: nothing chosen.
			User()->ResetToDefaults();
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->ApplyVideoOptions();
			}
			return 0.3f;
		}));

		// --- 1: the rows with a fresh settings file, their font, keys and mouse ---
		S.Add(Do(TEXT("1: defaults"), []()
		{
			const UStdSettingsSubsystem* Sub = SS();
			const int32 ProjectMethod = UStdUserSettings::GetProjectAntiAliasingMethod();
			Check(Sub && Sub->GetFrameRateLimit() == 60 && !Sub->IsFrameRateUnlimited(), FString::Printf(TEXT("1: fresh settings: limit %d, unlimited %d (want 60, 0)"), Sub ? Sub->GetFrameRateLimit() : -1, Sub ? Sub->IsFrameRateUnlimited() : -1));
			Check(Sub && Sub->GetAntiAliasingMethod() == ProjectMethod && ProjectMethod == 0, FString::Printf(TEXT("1: fresh settings: anti-aliasing %d, the project's own %d (r.AntiAliasingMethod in DefaultEngine.ini: 0)"), Sub ? Sub->GetAntiAliasingMethod() : -1, ProjectMethod));
			Check(Sub && !Sub->IsVSyncEnabled(), TEXT("1: fresh settings: V-Sync off"));
			Check(EngineInt(TEXT("t.MaxFPS")) == 60 && EngineInt(TEXT("r.AntiAliasingMethod")) == 0 && EngineInt(TEXT("r.VSync")) == 0, TEXT("1: the engine has them: ") + DescribeEngine());
			Check(OpenSettings(), TEXT("1: the main menu's Settings opens the settings screen"));
			return 0.8f;
		}));
		S.Add(Do(TEXT("1: the rows"), []()
		{
			Check(Screen() != nullptr, TEXT("1: the settings screen is shown"));
			Check(Named<UWidget>(TEXT("Row_FrameRateLimit")) && Named<UWidget>(TEXT("Row_AntiAliasing")) && Named<UWidget>(TEXT("Row_VSync")), TEXT("1: three rows: frame rate limit, anti-aliasing, V-Sync"));
			Check(TextOf(TEXT("Text_FrameRateLimitLabel")) == TEXT("Frame rate limit") && TextOf(TEXT("Text_FrameRateUnlimited")) == TEXT("Unlimited")
				&& TextOf(TEXT("Text_AntiAliasingLabel")) == TEXT("Anti-aliasing") && TextOf(TEXT("Text_VSyncLabel")) == TEXT("V-Sync"), TEXT("1: StdCore's labels"));
			Check(TextOf(TEXT("Text_FrameRateLimit")) == TEXT("60") && !IsChecked(TEXT("CheckBox_FrameRateUnlimited")), FString::Printf(TEXT("1: the screen reads %s, Unlimited %s"),
				*TextOf(TEXT("Text_FrameRateLimit")), IsChecked(TEXT("CheckBox_FrameRateUnlimited")) ? TEXT("ticked") : TEXT("clear")));
			Check(TextOf(TEXT("Text_AntiAliasing")) == TEXT("Off") && !IsChecked(TEXT("CheckBox_VSync")), FString::Printf(TEXT("1: anti-aliasing %s (the project's), V-Sync %s"),
				*TextOf(TEXT("Text_AntiAliasing")), IsChecked(TEXT("CheckBox_VSync")) ? TEXT("ticked") : TEXT("clear")));
			const UButton* Limit = Named<UButton>(TEXT("Button_FrameRateLimit"));
			Check(Limit && Limit->GetIsEnabled(), TEXT("1: the choice is enabled while limited"));
			int32 Texts = 0, Others = 0;
			for (const TCHAR* Name : { TEXT("Text_FrameRateLimitLabel"), TEXT("Text_FrameRateLimit"), TEXT("Text_FrameRateUnlimited"), TEXT("Text_AntiAliasingLabel"), TEXT("Text_AntiAliasing"), TEXT("Text_VSyncLabel"), TEXT("Text_WindowModeLabel") })
			{
				if (const UTextBlock* Text = Named<UTextBlock>(Name))
				{
					++Texts;
					const bool bOk = Text->GetFont().FontObject.Get() == StdUIStyle::GetFontObject();
					Others += bOk ? 0 : 1;
					Note(FString::Printf(TEXT("FONT %s \"%s\": %s"), Name, *Text->GetText().ToString(), *StdUIStyle::DescribeFont(Text->GetFont())));
				}
			}
			Check(Texts == 7 && Others == 0, FString::Printf(TEXT("1: the rows' texts in the style's font (%d texts with the window mode's label, %d in another font)"), Texts, Others));
			const UStdUITheme* Theme = StdUITheme::Get();
			const UCheckBox* Box = Named<UCheckBox>(TEXT("CheckBox_VSync"));
			// Framed, the box's look is a texture made in code: the same texture as the theme's ticked and unticked boxes.
			const FCheckBoxStyle Want = Theme ? Theme->MakeCheckBoxStyle() : FCheckBoxStyle();
			Check(Theme && Box && Box->GetWidgetStyle().CheckedImage.GetResourceObject() == Want.CheckedImage.GetResourceObject()
				&& Box->GetWidgetStyle().UncheckedImage.GetResourceObject() == Want.UncheckedImage.GetResourceObject() && Want.CheckedImage.GetResourceObject() != Want.UncheckedImage.GetResourceObject(),
				TEXT("1: the boxes take the theme's look (its ticked and unticked boxes)"));
			if (UScrollBox* Scroll = Named<UScrollBox>(TEXT("ScrollBox_AudioVideo")))
			{
				Scroll->ScrollToEnd();
			}
			Check(Named<UScrollBox>(TEXT("ScrollBox_AudioVideo")) != nullptr, TEXT("1: the tab scrolls (eight rows)"));
			return 0.5f;
		}));
		AddShot(S, TEXT("Settings_VideoRows"), true);

		// Keys: navigation reaches every row, Enter changes it.
		S.Add(Do(TEXT("1: keys: navigation"), []()
		{
			FocusNamed(TEXT("Button_TabAudioVideo"));
			G.Focused.Reset();
			for (int32 I = 0; I < 14; ++I)
			{
				SlateKey(EKeys::Down);
				G.Focused.Add(FocusedName());
			}
			Note(TEXT("Down from the tab: ") + FString::Join(G.Focused, TEXT(" > ")));
			Check(G.Focused.Contains(TEXT("Button_FrameRateLimit")) && G.Focused.Contains(TEXT("Button_AntiAliasing")) && G.Focused.Contains(TEXT("CheckBox_VSync")),
				TEXT("1: keys: Down reaches the frame-rate limit, the anti-aliasing and V-Sync"));
			FocusNamed(TEXT("Button_FrameRateLimit"));
			SlateKey(EKeys::Right);
			const FString Right = FocusedName();
			Check(Right == TEXT("CheckBox_FrameRateUnlimited"), TEXT("1: keys: Right from the limit reaches Unlimited (") + Right + TEXT(")"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("1: keys: Enter"), []()
		{
			UStdSettingsSubsystem* Sub = SS();
			if (!Sub)
			{
				return 0.f;
			}
			FocusNamed(TEXT("CheckBox_FrameRateUnlimited"));
			SlateKey(EKeys::Enter);
			const bool bOn = Sub->IsFrameRateUnlimited() && IsChecked(TEXT("CheckBox_FrameRateUnlimited"));
			SlateKey(EKeys::Enter);
			Check(bOn && !Sub->IsFrameRateUnlimited(), TEXT("1: keys: Enter ticks and clears Unlimited"));
			FocusNamed(TEXT("Button_FrameRateLimit"));
			SlateKey(EKeys::Enter);
			Check(Sub->GetFrameRateLimit() == 90 && TextOf(TEXT("Text_FrameRateLimit")) == TEXT("90"), FString::Printf(TEXT("1: keys: Enter on the limit moves it to the next (%d)"), Sub->GetFrameRateLimit()));
			FocusNamed(TEXT("Button_AntiAliasing"));
			SlateKey(EKeys::Enter);
			Check(Sub->GetAntiAliasingMethod() == 1 && TextOf(TEXT("Text_AntiAliasing")) == TEXT("FXAA"), TEXT("1: keys: Enter on the anti-aliasing moves it to FXAA (") + TextOf(TEXT("Text_AntiAliasing")) + TEXT(")"));
			SlateKey(EKeys::Enter);
			SlateKey(EKeys::Enter);
			Check(Sub->GetAntiAliasingMethod() == 0, TEXT("1: keys: TAA, then Off again"));
			FocusNamed(TEXT("CheckBox_VSync"));
			SlateKey(EKeys::Enter);
			const bool bVSync = Sub->IsVSyncEnabled() && EngineInt(TEXT("r.VSync")) == 1;
			SlateKey(EKeys::Enter);
			Check(bVSync && !Sub->IsVSyncEnabled(), TEXT("1: keys: Enter ticks and clears V-Sync"));
			return 0.2f;
		}));

		// Mouse: hovering moves the focus to the row, a click changes it.
		S.Add(Do(TEXT("1: mouse: hover the limit"), []() { MouseTo(WidgetPoint(Named<UWidget>(TEXT("Button_FrameRateLimit")))); return 0.3f; }));
		S.Add(Do(TEXT("1: mouse: click the limit"), []()
		{
			Check(FocusedName() == TEXT("Button_FrameRateLimit"), TEXT("1: mouse: hovering the limit focuses it (") + FocusedName() + TEXT(")"));
			Click(WidgetPoint(Named<UWidget>(TEXT("Button_FrameRateLimit"))));
			const UStdSettingsSubsystem* Sub = SS();
			Check(Sub && Sub->GetFrameRateLimit() == 120 && EngineInt(TEXT("t.MaxFPS")) == 120, FString::Printf(TEXT("1: mouse: a click moves the limit to 120 (%d)"), Sub ? Sub->GetFrameRateLimit() : -1));
			return 0.2f;
		}));
		S.Add(Do(TEXT("1: mouse: hover Unlimited"), []() { MouseTo(WidgetPoint(Named<UWidget>(TEXT("CheckBox_FrameRateUnlimited")))); return 0.3f; }));
		S.Add(Do(TEXT("1: mouse: tick Unlimited"), []()
		{
			Check(FocusedName() == TEXT("CheckBox_FrameRateUnlimited"), TEXT("1: mouse: hovering Unlimited focuses it (") + FocusedName() + TEXT(")"));
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_FrameRateUnlimited"))));
			const UStdSettingsSubsystem* Sub = SS();
			const UButton* Limit = Named<UButton>(TEXT("Button_FrameRateLimit"));
			Check(Sub && Sub->IsFrameRateUnlimited() && EngineInt(TEXT("t.MaxFPS")) == 0, TEXT("1: mouse: a click ticks Unlimited and lifts the limit: ") + DescribeEngine());
			Check(Limit && !Limit->GetIsEnabled() && TextOf(TEXT("Text_FrameRateLimit")) == TEXT("120") && Sub->GetFrameRateLimit() == 120,
				TEXT("1: while unlimited the choice is disabled and keeps its value (") + TextOf(TEXT("Text_FrameRateLimit")) + TEXT(")"));
			return 0.2f;
		}));
		AddShot(S, TEXT("Settings_VideoRows_Unlimited"), true, 0.2f);
		S.Add(Do(TEXT("1: mouse: clear Unlimited"), []()
		{
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_FrameRateUnlimited"))));
			const UStdSettingsSubsystem* Sub = SS();
			const UButton* Limit = Named<UButton>(TEXT("Button_FrameRateLimit"));
			Check(Sub && !Sub->IsFrameRateUnlimited() && EngineInt(TEXT("t.MaxFPS")) == 120 && Limit && Limit->GetIsEnabled(), TEXT("1: mouse: cleared, the limit is back with its value and enabled: ") + DescribeEngine());
			MouseTo(WidgetPoint(Named<UWidget>(TEXT("Button_AntiAliasing"))));
			return 0.3f;
		}));
		S.Add(Do(TEXT("1: mouse: anti-aliasing and V-Sync"), []()
		{
			Check(FocusedName() == TEXT("Button_AntiAliasing"), TEXT("1: mouse: hovering the anti-aliasing focuses it (") + FocusedName() + TEXT(")"));
			Click(WidgetPoint(Named<UWidget>(TEXT("Button_AntiAliasing"))));
			const UStdSettingsSubsystem* Sub = SS();
			Check(Sub && Sub->GetAntiAliasingMethod() == 1 && EngineInt(TEXT("r.AntiAliasingMethod")) == 1, TEXT("1: mouse: a click moves the anti-aliasing to FXAA: ") + DescribeEngine());
			Click(WidgetPoint(Named<UWidget>(TEXT("Button_AntiAliasing"))));
			Click(WidgetPoint(Named<UWidget>(TEXT("Button_AntiAliasing"))));
			Check(Sub && Sub->GetAntiAliasingMethod() == 0, TEXT("1: mouse: TAA, then Off again"));
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_VSync"))));
			const bool bOn = Sub && Sub->IsVSyncEnabled() && EngineInt(TEXT("r.VSync")) == 1;
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_VSync"))));
			Check(bOn && Sub && !Sub->IsVSyncEnabled() && EngineInt(TEXT("r.VSync")) == 0, TEXT("1: mouse: a click ticks and clears V-Sync"));
			MouseTo(WidgetPoint(Named<UWidget>(TEXT("Button_Back"))) + FVector2D(0.0, 120.0));
			return 0.2f;
		}));

		// --- 2: the limit ---
		S.Add(Do(TEXT("2: limit 30"), []() { if (UStdSettingsSubsystem* Sub = SS()) { Sub->SetFrameRateLimit(30); } return 0.f; }));
		AddMeasure(S, TEXT("2: the main menu at 30"), 5.f, [](double Fps)
		{
			Check(FMath::Abs(Fps - 30.0) <= 2.0, FString::Printf(TEXT("2: limit 30: %.2f frames a second over 5 s, within 2 of 30"), Fps));
			ReportLine(FString::Printf(TEXT("frame rate, main menu, %s: limit 30 measured %.2f"), GIsEditor ? TEXT("PIE") : TEXT("package"), Fps));
		});
		S.Add(Do(TEXT("2: limit 120"), []() { if (UStdSettingsSubsystem* Sub = SS()) { Sub->SetFrameRateLimit(120); } return 0.f; }));
		AddMeasure(S, TEXT("2: the main menu at 120"), 5.f, [](double Fps)
		{
			// In PIE the editor holds its own cap of 120: the limit can be reached, never passed.
			Check(FMath::Abs(Fps - 120.0) <= 2.0, FString::Printf(TEXT("2: limit 120: %.2f frames a second over 5 s, within 2 of 120%s"), Fps, GIsEditor ? TEXT(" (PIE: the editor's own cap is 120 too)") : TEXT("")));
			ReportLine(FString::Printf(TEXT("frame rate, main menu, %s: limit 120 measured %.2f"), GIsEditor ? TEXT("PIE") : TEXT("package"), Fps));
		});
		S.Add(Do(TEXT("2: unlimited"), []()
		{
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_FrameRateUnlimited"))));
			const UStdSettingsSubsystem* Sub = SS();
			const UButton* Limit = Named<UButton>(TEXT("Button_FrameRateLimit"));
			Check(Sub && Sub->IsFrameRateUnlimited() && EngineInt(TEXT("t.MaxFPS")) == 0 && Limit && !Limit->GetIsEnabled() && TextOf(TEXT("Text_FrameRateLimit")) == TEXT("120"),
				TEXT("2: Unlimited lifts the limit, disables the choice and keeps 120: ") + DescribeEngine());
			return 0.f;
		}));
		AddMeasure(S, TEXT("2: the main menu unlimited"), 5.f, [](double Fps)
		{
			Check(Fps > 32.0, FString::Printf(TEXT("2: unlimited: %.2f frames a second (above the 30 limit; in PIE the editor's cap of 120 holds)"), Fps));
			ReportLine(FString::Printf(TEXT("frame rate, main menu, %s: unlimited measured %.2f"), GIsEditor ? TEXT("PIE") : TEXT("package"), Fps));
		});
		S.Add(Do(TEXT("2: limited again"), []()
		{
			Click(WidgetPoint(Named<UWidget>(TEXT("CheckBox_FrameRateUnlimited"))));
			const UStdSettingsSubsystem* Sub = SS();
			Check(Sub && !Sub->IsFrameRateUnlimited() && EngineInt(TEXT("t.MaxFPS")) == 120, TEXT("2: cleared: the kept 120 is the limit again"));
			if (UStdSettingsSubsystem* Mutable = SS())
			{
				Mutable->SetFrameRateLimit(60);
			}
			return 0.2f;
		}));

		// --- 3: anti-aliasing reaches the renderer, V-Sync the engine ---
		S.Add(Do(TEXT("3: start"), []()
		{
			Note(TEXT("choices offered: ") + FString::JoinBy(UStdSettingsSubsystem::GetAntiAliasingChoices(), TEXT(", "), [](int32 M) { return AAName(M); }));
			return 0.f;
		}));
		for (const int32 Method : UStdSettingsSubsystem::GetAntiAliasingChoices())
		{
			S.Add(Do(FString::Printf(TEXT("3: %s"), *AAName(Method)), [Method]()
			{
				if (UStdSettingsSubsystem* Sub = SS())
				{
					Sub->SetAntiAliasingMethod(Method);
				}
				return 0.4f;
			}));
			S.Add(Do(FString::Printf(TEXT("3: %s reached"), *AAName(Method)), [Method]()
			{
				Check(EngineInt(TEXT("r.AntiAliasingMethod")) == Method && G.Watch && G.Watch->Method == Method && GFrameCounter - G.Watch->Frame < 30,
					FString::Printf(TEXT("3: %s: the renderer uses it: %s"), *AAName(Method), *DescribeEngine()));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("3: V-Sync"), []()
		{
			UStdSettingsSubsystem* Sub = SS();
			if (!Sub)
			{
				return 0.f;
			}
			Sub->SetAntiAliasingMethod(0);
			Sub->SetVSyncEnabled(true);
			const int32 On = EngineInt(TEXT("r.VSync"));
			Sub->SetVSyncEnabled(false);
			const int32 Off = EngineInt(TEXT("r.VSync"));
			Check(On == 1 && Off == 0, FString::Printf(TEXT("3: V-Sync on and off reach the engine (r.VSync %d, %d; in PIE the editor's viewport presents by r.VSyncEditor, %d)"), On, Off, EngineInt(TEXT("r.VSyncEditor"))));
			return 0.f;
		}));

		// --- 5: a settings file saved by the build before ---
		S.Add(Do(TEXT("5: the file of the build before"), []()
		{
			const FString Path = OldFilePath();
			FString Text;
			const bool bRead = FFileHelper::LoadFileToString(Text, *Path);
			// StdCore's section only: the engine's own section holds a FrameRateLimit of its own.
			FString Section;
			if (Text.Split(TEXT("[/Script/StdCore.StdUserSettings]"), nullptr, &Section))
			{
				Section.Split(TEXT("\n["), &Section, nullptr);
			}
			Check(bRead && Section.Contains(TEXT("MasterVolume")) && !Section.Contains(TEXT("FrameRate")) && !Section.Contains(TEXT("AntiAliasing")) && !Section.Contains(TEXT("VSync")),
				TEXT("5: the old file's StdCore section holds the volumes and no video option: ") + Path);
			if (!bRead)
			{
				return 0.f;
			}
			const UStdUserSettings* Old = LoadFromFile(Path);
			Check(FMath::IsNearlyEqual(Old->GetMasterVolume(), 0.35f) && FMath::IsNearlyEqual(Old->GetMusicVolume(), 0.45f) && FMath::IsNearlyEqual(Old->GetSfxVolume(), 0.55f),
				FString::Printf(TEXT("5: its volumes load as it held them (%.2f, %.2f, %.2f)"), Old->GetMasterVolume(), Old->GetMusicVolume(), Old->GetSfxVolume()));
			Check(!Old->HasChosen(EStdVideoOption::FrameRateLimit) && !Old->HasChosen(EStdVideoOption::AntiAliasing) && !Old->HasChosen(EStdVideoOption::VSync)
				&& Old->GetFrameRateLimit() == 60 && !Old->IsFrameRateUnlimited() && Old->GetAntiAliasingMethod() == 0 && !Old->IsVSyncEnabled(),
				FString::Printf(TEXT("5: the three options at their defaults (%d, unlimited %d, anti-aliasing %d, V-Sync %d)"), Old->GetFrameRateLimit(), Old->IsFrameRateUnlimited(),
					Old->GetAntiAliasingMethod(), Old->IsVSyncEnabled()));
			return 0.f;
		}));

		// --- 6: the project's values ---
		S.Add(Do(TEXT("6: a default changed"), []()
		{
			Project()->DefaultFrameRateLimit = 144;
			User()->ResetToDefaults();
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->ApplyVideoOptions();
			}
			CloseSettings();
			return 0.4f;
		}));
		S.Add(Do(TEXT("6: reopen"), []() { Check(OpenSettings(), TEXT("6: the settings screen opens again")); return 0.6f; }));
		S.Add(Do(TEXT("6: the default followed"), []()
		{
			Check(TextOf(TEXT("Text_FrameRateLimit")) == TEXT("144") && EngineInt(TEXT("t.MaxFPS")) == 144, TEXT("6: a fresh settings file follows the project's default of 144: ") + TextOf(TEXT("Text_FrameRateLimit")) + TEXT(", ") + DescribeEngine());
			Project()->DefaultFrameRateLimit = G.SavedDefaultLimit;
			// A choice stored for the anti-aliasing, then its row hidden.
			User()->SetAntiAliasingMethod(2);
			Project()->HiddenVideoOptions = { EStdVideoOption::AntiAliasing };
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->ApplyVideoOptions();
			}
			CloseSettings();
			return 0.4f;
		}));
		S.Add(Do(TEXT("6: reopen"), []() { Check(OpenSettings(), TEXT("6: the settings screen opens again")); return 0.6f; }));
		S.Add(Do(TEXT("6: the row hidden"), []()
		{
			Check(Named<UWidget>(TEXT("Row_AntiAliasing")) == nullptr && Named<UWidget>(TEXT("Button_AntiAliasing")) == nullptr && Named<UWidget>(TEXT("Row_FrameRateLimit")) && Named<UWidget>(TEXT("Row_VSync")),
				TEXT("6: the hidden anti-aliasing row is not on the screen, the two others are"));
			const UStdSettingsSubsystem* Sub = SS();
			Check(Sub && Sub->GetAntiAliasingMethod() == 0 && EngineInt(TEXT("r.AntiAliasingMethod")) == 0, TEXT("6: hidden, the option stays at its default (Off) though TAA is stored: ") + DescribeEngine());
			Project()->HiddenVideoOptions = G.SavedHidden;
			Check(Sub && Sub->GetAntiAliasingMethod() == 2, TEXT("6: shown again, the stored choice comes back"));
			User()->ResetToDefaults();
			if (UStdSettingsSubsystem* Mutable = SS())
			{
				Mutable->ApplyVideoOptions();
			}
			CloseSettings();
			return 0.4f;
		}));

		// --- 7: the tests that measure with no cap ---
		S.Add(Do(TEXT("7: the uncapped tests' lift"), []()
		{
			UStdSettingsSubsystem* Sub = SS();
			if (!Sub)
			{
				return 0.f;
			}
			Sub->SetFrameRateLimit(60);
			Sub->SetVSyncEnabled(true);
			for (const TCHAR* Test : { TEXT("tls.Views"), TEXT("tls.Links.CostRun"), TEXT("tls.EntityFound.Cost") })
			{
				FrameCapsTest::Lift(W(), Test);
				const bool bLifted = EngineInt(TEXT("t.MaxFPS")) == 0 && EngineInt(TEXT("r.VSync")) == 0 && Sub->GetFrameRateLimit() == 60 && Sub->IsVSyncEnabled();
				FrameCapsTest::Restore(W(), Test);
				const bool bBack = EngineInt(TEXT("t.MaxFPS")) == 60 && EngineInt(TEXT("r.VSync")) == 1 && !Sub->AreFrameCapsSuspended();
				Check(bLifted && bBack, FString::Printf(TEXT("7: %s lifts the limit and V-Sync for its run (the player's 60 and V-Sync kept) and puts them back"), Test));
			}
			Sub->SetVSyncEnabled(false);
			return 0.f;
		}));

		// --- 4: kept: changed, saved, read back from the disk ---
		S.Add(Do(TEXT("4: change and save"), []()
		{
			UStdSettingsSubsystem* Sub = SS();
			if (!Sub)
			{
				return 0.f;
			}
			Sub->SetFrameRateLimit(90);
			Sub->SetFrameRateUnlimited(false);
			Sub->SetAntiAliasingMethod(2);
			Sub->SetVSyncEnabled(true);
			Sub->SaveSettings();
			return 0.3f;
		}));
		S.Add(Do(TEXT("4: on disk"), []()
		{
			// The player's settings file: Saved/Config/<platform>/GameUserSettings.ini (WindowsEditor in the editor, Windows in a package).
			FString Path;
			for (const FString& Platform : { FString(FPlatformProperties::PlatformName()), FString(FPlatformProperties::IniPlatformName()), FString(TEXT("WindowsEditor")) })
			{
				const FString Candidate = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GeneratedConfigDir(), Platform, TEXT("GameUserSettings.ini")));
				if (Path.IsEmpty() && IFileManager::Get().FileExists(*Candidate))
				{
					Path = Candidate;
				}
			}
			FString Text;
			FFileHelper::LoadFileToString(Text, *Path);
			Check(Text.Contains(TEXT("FrameRateLimit=90")) && Text.Contains(TEXT("FrameRateUnlimited=0")) && Text.Contains(TEXT("AntiAliasingMethod=2")) && Text.Contains(TEXT("VSync=1")),
				TEXT("4: the file on disk holds them, in StdCore's section: ") + Path);
			const FString Copy = WrittenCopyPath();
			IFileManager::Get().Copy(*Copy, *Path);
			const UStdUserSettings* Read = LoadFromFile(Copy);
			Check(Read->GetFrameRateLimit() == 90 && !Read->IsFrameRateUnlimited() && Read->GetAntiAliasingMethod() == 2 && Read->IsVSyncEnabled(),
				FString::Printf(TEXT("4: read back from a copy of the file: %d, unlimited %d, anti-aliasing %d, V-Sync %d"), Read->GetFrameRateLimit(), Read->IsFrameRateUnlimited(),
					Read->GetAntiAliasingMethod(), Read->IsVSyncEnabled()));
			return 0.f;
		}));

		// --- the scan view under each method, and the ten scans ---
		S.Add(Do(TEXT("area 2: travel"), []()
		{
			UStdSettingsSubsystem* Sub = SS();
			if (Sub)
			{
				Sub->SetVSyncEnabled(false);
			}
			URunSubsystem* Run = URunSubsystem::Get(W());
			Check(Run && Run->TravelToArea(2), TEXT("area 2: travel from the main menu"));
			return 2.f;
		}));
	}

	static void AddScanSeries(TArray<FStep>& S, const FString& Label, TArray<FScanResult>* Out)
	{
		S.Add(Do(Label + TEXT(": Stop"), [Out]()
		{
			Out->Reset();
			if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
			{
				T->Stop();
			}
			const ULidarScanComponent* L = Lidar();
			G.LastLinkScan = L ? L->GetLastLinkStats().ScanIndex : -1;
			G.ScanAsked = FPlatformTime::Seconds();
			return 0.f;
		}));
		for (int32 I = 0; I < 10; ++I)
		{
			if (I > 0)
			{
				S.Add(WaitFor(FString::Printf(TEXT("%s: scan %d ready"), *Label, I + 1), []() { const UScanDirectorComponent* D = Director(); return D && D->IsScanReady(); }, 10.f, []()
				{
					if (UScanDirectorComponent* D = Director())
					{
						D->RequestScan();
					}
					G.ScanAsked = FPlatformTime::Seconds();
					return 0.f;
				}));
			}
			S.Add(WaitFor(FString::Printf(TEXT("%s: scan %d lines"), *Label, I + 1), []()
			{
				const ULidarScanComponent* L = Lidar();
				return (L && L->GetLastLinkStats().ScanIndex != G.LastLinkScan) || FPlatformTime::Seconds() - G.ScanAsked > 6.0;
			}, 8.f, [Out, Label, I]()
			{
				const ULidarScanComponent* L = Lidar();
				FScanResult R;
				if (L && L->GetLastLinkStats().ScanIndex != G.LastLinkScan)
				{
					const FLidarLinkStats Stats = L->GetLastLinkStats();
					R.Index = Stats.ScanIndex;
					R.bRejected = Stats.bRejected;
					R.Links = Stats.Links;
					R.ReadySeconds = Stats.ReadySeconds;
					R.bLines = !Stats.bRejected && Stats.Links > 0;
					G.LastLinkScan = Stats.ScanIndex;
				}
				else
				{
					R.bTimedOut = true;
				}
				Out->Add(R);
				Note(FString::Printf(TEXT("%s: scan %d (index %d): %s, %d links, ready after %.3f s; frame %.1f ms"), *Label, I + 1, R.Index,
					R.bTimedOut ? TEXT("NO RESULT within 6 s") : (R.bLines ? TEXT("got its lines") : TEXT("GOT NO LINES")), R.Links, R.ReadySeconds, FApp::GetDeltaTime() * 1000.0));
				return 0.f;
			}));
		}
		S.Add(Do(Label + TEXT(": Resume"), [Out, Label]()
		{
			if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
			{
				T->Resume();
			}
			int32 With = 0;
			for (const FScanResult& R : *Out)
			{
				With += R.bLines ? 1 : 0;
			}
			ReportLine(FString::Printf(TEXT("%s: %d of %d scans got their lines; per scan: %s"), *Label, With, Out->Num(),
				*FString::JoinBy(*Out, TEXT(" "), [](const FScanResult& R) { return R.bTimedOut ? FString(TEXT("timeout")) : FString::Printf(TEXT("%s(%.2fs)"), R.bLines ? TEXT("lines") : TEXT("NONE"), R.ReadySeconds); })));
			return 1.f;
		}));
	}

	static void BuildArea(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("area 2: ready"), []()
		{
			UWorld* World = W();
			return World && FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) == TEXT("Area2_ServerHalls") && AAreaGenerator::IsAreaReady(World)
				&& PC() && PC()->GetPawn() && Lidar() && TS();
		}, 120.f, []()
		{
			if (G.Watch)
			{
				G.Watch->World = W();
			}
			Check(true, TEXT("area 2: Area2_ServerHalls is ready, the player in it"));
			if (GLog && !G.bNoLinesRegistered)
			{
				GLog->AddOutputDevice(&G.NoLines);
				G.bNoLinesRegistered = true;
			}
			return 3.f;
		}));
		S.Add(Do(TEXT("area 2: the first hall's entry"), []()
		{
			// Beacon 0's terminal stands at the entrance of area 2's first hall (its beacons are in its open segments, in route order).
			GEngine->Exec(W(), TEXT("tls.Beacon.View 0 entrance"));
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateUnlimited(false);
				Sub->SetFrameRateLimit(60);
			}
			return 1.f;
		}));
		S.Add(Do(TEXT("area 2: Stop"), []()
		{
			if (UTimeStateSubsystem* T = TS())
			{
				T->Stop();
			}
			return 3.f;
		}));

		// One scan shot under each method: a new scan from the same pose for each (the newer replaces the older's points).
		for (const int32 Method : UStdSettingsSubsystem::GetAntiAliasingChoices())
		{
			const FString Name = FString::Printf(TEXT("Area2_Scan_%s"), *AAName(Method));
			S.Add(WaitFor(Name + TEXT(": scan ready"), []() { const UScanDirectorComponent* D = Director(); return D && D->IsScanReady(); }, 10.f, [Method]()
			{
				if (UStdSettingsSubsystem* Sub = SS())
				{
					Sub->SetAntiAliasingMethod(Method);
				}
				if (UScanDirectorComponent* D = Director())
				{
					D->RequestScan();
				}
				return 2.f;
			}));
			S.Add(Do(Name + TEXT(": method"), [Method, Name]()
			{
				Check(EngineInt(TEXT("r.AntiAliasingMethod")) == Method && G.Watch && G.Watch->Method == Method, FString::Printf(TEXT("8: %s: the renderer uses %s in area 2: %s"), *Name, *AAName(Method), *DescribeEngine()));
				return 0.f;
			}));
			AddShot(S, Name, false, 0.2f);
			AddShot(S, TEXT("_") + Name + TEXT("_again"), false, 0.1f);
			S.Add(Do(Name + TEXT(": turn"), []()
			{
				// The view turns 0.5 degrees a frame for 20 frames; the shot is taken while it turns.
				G.TurnFrames = 20;
				G.TurnDegrees = 0.5f;
				G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					if (G.TurnFrames <= 0)
					{
						G.Sampler.Reset();
						return false;
					}
					if (APlayerController* C = PC())
					{
						C->SetControlRotation(C->GetControlRotation() + FRotator(0.0, G.TurnDegrees, 0.0));
					}
					--G.TurnFrames;
					return true;
				}));
				return 0.25f;
			}));
			S.Add(Do(Name + TEXT(": turning shot"), [Name]() { RequestShot(Name + TEXT("_Turning"), false); return 0.f; }));
			S.Add(WaitFor(Name + TEXT(": turning shot taken"), []() { return G.bShotReady; }, 5.f, []()
			{
				return 0.3f;
			}));
			S.Add(Do(Name + TEXT(": turn back"), [Name, Method]()
			{
				if (APlayerController* C = PC())
				{
					C->SetControlRotation(C->GetControlRotation() - FRotator(0.0, 0.5 * (20 - FMath::Max(0, G.TurnFrames)), 0.0));
				}
				G.TurnFrames = 0;
				const FShot* Still = G.Shots.Find(Name);
				const FShot* Again = G.Shots.Find(TEXT("_") + Name + TEXT("_again"));
				const FShot* Turning = G.Shots.Find(Name + TEXT("_Turning"));
				if (Still)
				{
					ReportLine(FString::Printf(TEXT("%s still: %s"), *AAName(Method), *DescribeScanShot(*Still, Again)));
				}
				if (Turning)
				{
					ReportLine(FString::Printf(TEXT("%s turning: %s"), *AAName(Method), *DescribeScanShot(*Turning, nullptr)));
				}
				return 0.5f;
			}));
		}
		S.Add(Do(TEXT("area 2: shots done"), []()
		{
			if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
			{
				T->Resume();
			}
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetAntiAliasingMethod(0);
				Sub->SetFrameRateUnlimited(false);
				Sub->SetFrameRateLimit(30);
			}
			G.NoLines.Count = 0;
			return 2.f;
		}));
		AddScanSeries(S, TEXT("ten scans at 30"), &G.Scans30);
		S.Add(Do(TEXT("ten scans at 30: no-lines warnings"), []()
		{
			ReportLine(FString::Printf(TEXT("ten scans at 30: %d \"gets no lines\" warnings in the log%s"), G.NoLines.Count, G.NoLines.Count ? *(TEXT(" (last: ") + G.NoLines.Last + TEXT(")")) : TEXT("")));
			G.NoLines.Count = 0;
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateUnlimited(true);
			}
			Note(TEXT("the limit lifted: ") + DescribeEngine());
			return 2.f;
		}));
		AddScanSeries(S, TEXT("ten scans lifted"), &G.ScansLifted);
		S.Add(Do(TEXT("ten scans lifted: no-lines warnings"), []()
		{
			ReportLine(FString::Printf(TEXT("ten scans lifted: %d \"gets no lines\" warnings in the log"), G.NoLines.Count));
			Check(G.Scans30.Num() == 10 && G.ScansLifted.Num() == 10, TEXT("area 2: ten scans at 30 and ten lifted, each with its result"));
			return 0.f;
		}));

		// The end of the main run: the run's save as found, the three values left changed and saved for the restart stage.
		S.Add(Do(TEXT("end: for the restart stage"), []()
		{
			RestoreRunFile();
			RestoreProject();
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SetFrameRateUnlimited(false);
				Sub->SetFrameRateLimit(90);
				Sub->SetAntiAliasingMethod(2);
				Sub->SetVSyncEnabled(true);
				Sub->SaveSettings();
			}
			// What to check after the restart, and the settings to put back then.
			TArray<FString> Lines = { TEXT("Expect=90,0,2,1") };
			for (const TPair<FString, FString>& Pair : G.SavedUser)
			{
				Lines.Add(TEXT("Found.") + Pair.Key + TEXT("=") + Pair.Value);
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(RestartPath()), true);
			const bool bOk = FFileHelper::SaveStringArrayToFile(Lines, *RestartPath());
			Check(bOk, TEXT("end: the restart stage's file written: ") + RestartPath());
			Note(TEXT("Now stop PIE, start a fresh PIE session in MainMenu and run: tls.VideoCheck restart. It checks what the game instance pushed at its start and puts the settings back as this run found them."));
			return 0.f;
		}));
	}

	// =================================================================================================================
	// The restart stage, MainMenu
	// =================================================================================================================

	static void BuildRestart(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("4: after the restart"), []()
		{
			TArray<FString> Lines;
			const bool bRead = FFileHelper::LoadFileToStringArray(Lines, *RestartPath());
			Check(bRead, TEXT("4: the main run's file: ") + RestartPath());
			for (const FString& Line : Lines)
			{
				FString Key, Value;
				if (Line.Split(TEXT("="), &Key, &Value) && Key.StartsWith(TEXT("Found.")))
				{
					G.SavedUser.Add(Key.RightChop(6), Value);
				}
			}
			const UStdSettingsSubsystem* Sub = SS();
			if (!Sub)
			{
				Check(false, TEXT("4: StdCore's settings subsystem"));
				return 0.f;
			}
			const UStdSettingsSubsystem::FStartupVideo& Startup = Sub->GetStartupVideo();
			Note(FString::Printf(TEXT("startup: frame %llu, first map's first frame %llu, limit %d, anti-aliasing %d, V-Sync %d"), Startup.Frame, Startup.FirstWorldFrame,
				Startup.FrameRateLimit, Startup.AntiAliasingMethod, Startup.VSync));
			Check(Startup.Frame > 0 && Startup.FirstWorldFrame >= Startup.Frame, TEXT("4: the values were pushed before the first frame of the first map (the menu's)"));
			Check(Startup.FrameRateLimit == 90 && Startup.AntiAliasingMethod == 2 && Startup.VSync == 1, TEXT("4: pushed at startup: the three changed values (90, TAA, V-Sync on)"));
			Check(EngineInt(TEXT("t.MaxFPS")) == 90 && EngineInt(TEXT("r.AntiAliasingMethod")) == 2 && EngineInt(TEXT("r.VSync")) == 1, TEXT("4: the engine has them: ") + DescribeEngine());
			Check(OpenSettings(), TEXT("4: the settings screen opens"));
			return 0.8f;
		}));
		S.Add(Do(TEXT("4: the screen"), []()
		{
			Check(TextOf(TEXT("Text_FrameRateLimit")) == TEXT("90") && !IsChecked(TEXT("CheckBox_FrameRateUnlimited")) && TextOf(TEXT("Text_AntiAliasing")) == TEXT("TAA") && IsChecked(TEXT("CheckBox_VSync")),
				FString::Printf(TEXT("4: the screen shows them: %s, Unlimited %d, %s, V-Sync %d"), *TextOf(TEXT("Text_FrameRateLimit")), IsChecked(TEXT("CheckBox_FrameRateUnlimited")),
					*TextOf(TEXT("Text_AntiAliasing")), IsChecked(TEXT("CheckBox_VSync"))));
			if (UScrollBox* Scroll = Named<UScrollBox>(TEXT("ScrollBox_AudioVideo")))
			{
				Scroll->ScrollToEnd();
			}
			return 0.4f;
		}));
		AddShot(S, TEXT("Settings_VideoRows_AfterRestart"), true);
		S.Add(Do(TEXT("end: the settings as found"), []()
		{
			CloseSettings();
			if (!G.SavedUser.IsEmpty())
			{
				ImportUser(G.SavedUser);
			}
			if (UStdSettingsSubsystem* Sub = SS())
			{
				Sub->SaveSettings();
				Sub->ApplyVideoOptions();
			}
			const TMap<FString, FString> Now = ExportUser();
			Check(!G.SavedUser.IsEmpty() && Now.OrderIndependentCompareEqual(G.SavedUser), TEXT("end: the player's settings put back as the main run found them: ") + DescribeUser(Now));
			IFileManager::Get().Delete(*RestartPath());
			Note(TEXT("end: ") + DescribeEngine());
			return 0.f;
		}));
	}

	static void Start(bool bRestart, bool bQuit)
	{
		if (G.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Ticker);
		}
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
		}
		if (G.bNoLinesRegistered && GLog)
		{
			GLog->RemoveOutputDevice(&G.NoLines);
		}
		G = FRun();
		G.bQuit = bQuit;
		G.bRestartStage = bRestart;
		UWorld* World = W();
		G.Map = World ? FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FString(TEXT("none"));
		G.Watch = FSceneViewExtensions::NewExtension<FViewWatch>();
		G.Watch->World = World;
		if (G.Map != TEXT("MainMenu"))
		{
			Check(false, FString::Printf(TEXT("tls.VideoCheck runs in MainMenu (here: %s)"), *G.Map));
		}
		else if (bRestart)
		{
			BuildRestart(G.Steps);
		}
		else
		{
			if (IFileManager::Get().FileExists(*RestartPath()))
			{
				Note(TEXT("a restart stage's file is waiting: run tls.VideoCheck restart first, or this run takes the settings it left as found"));
			}
			BuildMenu(G.Steps);
			BuildArea(G.Steps);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("VideoCheck: %s, %s, %d steps."), bRestart ? TEXT("restart stage") : TEXT("main run"), *G.Map, G.Steps.Num());
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsVideoCheckCommand(
	TEXT("tls.VideoCheck"),
	TEXT("tls.VideoCheck [restart] [Quit=0]: (MainMenu, PIE in its own window or a package, the window in front) the settings screen's video options: the rows, keys and mouse, the frame-rate limit measured at 30 and 120 and lifted, each anti-aliasing method at the renderer, V-Sync, the values saved and read back, a settings file of the build before, the project's defaults and a hidden row, the uncapped tests' lift; then in Area2_ServerHalls a scan shot under each anti-aliasing method and ten scans at 30 and ten lifted with their lines. It ends with the values changed and saved: 'tls.VideoCheck restart' in a fresh PIE session checks what the game pushed at its start, then puts the player's settings back. Shots to Saved/Screenshots/Brief39."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		bool bRestart = false;
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("restart"), ESearchCase::IgnoreCase))
			{
				bRestart = true;
			}
			else
			{
				bQuit = FCString::Atoi(*Arg) != 0;
			}
		}
		VideoCheck::Start(bRestart, bQuit);
	}));

#endif // !UE_BUILD_SHIPPING
