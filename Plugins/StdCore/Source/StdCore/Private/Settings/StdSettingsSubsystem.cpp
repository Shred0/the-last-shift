#include "Settings/StdSettingsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Scalability.h"
#include "Settings/StdProjectSettings.h"
#include "Settings/StdUserSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "StdLog.h"

void UStdSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	WorldInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &ThisClass::HandleWorldInitialized);

	// In the editor the scalability belongs to the editor viewport: only a game process restores the saved video settings.
	if (!GIsEditor)
	{
		LoadStartupVideoSettings();
	}
	else
	{
		// The video options do apply to a play session; the editor gets its own values back when the session ends.
		SaveEditorVideo();
	}

	// Before the first frame of the first map: the menu's first frame already has the player's options.
	ApplyVideoOptions();
	StartupVideo.Frame = GFrameCounter;
	StartupVideo.FrameRateLimit = AppliedFrameRateLimit;
	StartupVideo.AntiAliasingMethod = GetAntiAliasingMethod();
	StartupVideo.VSync = IsVSyncEnabled() ? 1 : 0;
	UE_LOG(LogStdCore, Log, TEXT("Video options applied at startup (frame %llu): frame-rate limit %d (0: none), anti-aliasing %d, V-Sync %d."),
		StartupVideo.Frame, StartupVideo.FrameRateLimit, StartupVideo.AntiAliasingMethod, StartupVideo.VSync);

	ApplyVolumesInternal(/*bPushMix*/ true);
}

void UStdSettingsSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedHandle);
	FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
	RestoreEditorVideo();
	Super::Deinitialize();
}

void UStdSettingsSubsystem::HandleWorldInitialized(const FActorsInitializedParams& Params)
{
	if (Params.World && Params.World->IsGameWorld() && Params.World->GetGameInstance() == GetGameInstance())
	{
		if (StartupVideo.FirstWorldFrame == 0)
		{
			StartupVideo.FirstWorldFrame = GFrameCounter;
		}
		// A map load can drop the active sound mixes: push the mix again with the stored volumes.
		ApplyVolumesInternal(/*bPushMix*/ true);
	}
}

// Audio

float UStdSettingsSubsystem::GetVolume(EStdVolumeChannel Channel) const
{
	const UStdUserSettings* UserSettings = UStdUserSettings::Get();
	switch (Channel)
	{
	case EStdVolumeChannel::Master: return UserSettings->GetMasterVolume();
	case EStdVolumeChannel::Music: return UserSettings->GetMusicVolume();
	case EStdVolumeChannel::Sfx: return UserSettings->GetSfxVolume();
	}
	return 1.f;
}

void UStdSettingsSubsystem::SetVolume(EStdVolumeChannel Channel, float Volume)
{
	UStdUserSettings* UserSettings = UStdUserSettings::Get();
	switch (Channel)
	{
	case EStdVolumeChannel::Master: UserSettings->SetMasterVolume(Volume); break;
	case EStdVolumeChannel::Music: UserSettings->SetMusicVolume(Volume); break;
	case EStdVolumeChannel::Sfx: UserSettings->SetSfxVolume(Volume); break;
	}

	// The mix is already active: changing its overrides is enough, pushing it again would only add a reference.
	ApplyVolumesInternal(/*bPushMix*/ false);
}

void UStdSettingsSubsystem::ApplyVolumes()
{
	ApplyVolumesInternal(/*bPushMix*/ true);
}

FStdClassVolumes StdAudio::ComputeClassVolumes(float Master, float Music, float Sfx)
{
	FStdClassVolumes Volumes;
	Volumes.Master = 1.f;
	Volumes.Music = Music * Master;
	Volumes.Sfx = Sfx * Master;
	Volumes.Ui = Master;
	return Volumes;
}

void UStdSettingsSubsystem::ApplyVolumesInternal(bool bPushMix)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const UStdProjectSettings* Settings = UStdProjectSettings::Get();
	USoundMix* Mix = StdSettings::LoadAsset(Settings->VolumesSoundMix, TEXT("VolumesSoundMix"), StdCoreDefaults::VolumesSoundMix);
	if (!Mix)
	{
		return;
	}

	const UStdUserSettings* UserSettings = UStdUserSettings::Get();
	const FStdClassVolumes Volumes = StdAudio::ComputeClassVolumes(UserSettings->GetMasterVolume(),
		UserSettings->GetMusicVolume(), UserSettings->GetSfxVolume());

	// The branches apply to their children, so a Sound Class the game adds under SC_Music, SC_SFX or SC_UI follows
	// its branch without new adjusters.
	auto ApplyClass = [World, Mix](USoundClass* SoundClass, float Volume, bool bApplyToChildren)
	{
		if (SoundClass)
		{
			UGameplayStatics::SetSoundMixClassOverride(World, Mix, SoundClass, Volume, 1.f, 0.f, bApplyToChildren);
		}
	};

	ApplyClass(StdSettings::LoadAsset(Settings->MasterSoundClass, TEXT("MasterSoundClass"), StdCoreDefaults::MasterSoundClass),
		Volumes.Master, /*bApplyToChildren*/ false);
	ApplyClass(StdSettings::LoadAsset(Settings->MusicSoundClass, TEXT("MusicSoundClass"), StdCoreDefaults::MusicSoundClass),
		Volumes.Music, /*bApplyToChildren*/ true);
	ApplyClass(StdSettings::LoadAsset(Settings->SfxSoundClass, TEXT("SfxSoundClass"), StdCoreDefaults::SfxSoundClass),
		Volumes.Sfx, /*bApplyToChildren*/ true);
	ApplyClass(StdSettings::LoadAsset(Settings->UiSoundClass, TEXT("UiSoundClass"), StdCoreDefaults::UiSoundClass),
		Volumes.Ui, /*bApplyToChildren*/ true);

	if (bPushMix)
	{
		UGameplayStatics::PushSoundMixModifier(World, Mix);
	}
}

// Video

EWindowMode::Type UStdSettingsSubsystem::GetWindowMode() const
{
	const UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return GameSettings ? GameSettings->GetFullscreenMode() : EWindowMode::Windowed;
}

void UStdSettingsSubsystem::SetWindowMode(EWindowMode::Type WindowMode)
{
	UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GameSettings)
	{
		return;
	}

	const FIntPoint Desktop = GameSettings->GetDesktopResolution();
	if (WindowMode == EWindowMode::Windowed)
	{
		// A window as large as the desktop looks like nothing happened: shrink it to two thirds.
		const FIntPoint Current = GameSettings->GetScreenResolution();
		if (Current.X <= 0 || Current.Y <= 0 || Current.X >= Desktop.X || Current.Y >= Desktop.Y)
		{
			GameSettings->SetScreenResolution(FIntPoint(Desktop.X * 2 / 3, Desktop.Y * 2 / 3));
		}
	}
	else if (Desktop.X > 0 && Desktop.Y > 0)
	{
		GameSettings->SetScreenResolution(Desktop);
	}

	GameSettings->SetFullscreenMode(WindowMode);
	GameSettings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides*/ false);
	GameSettings->ConfirmVideoMode();
	UE_LOG(LogStdCore, Log, TEXT("Window mode set to %s."), LexToString(WindowMode));
}

int32 UStdSettingsSubsystem::GetQualityLevel() const
{
	const UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return GameSettings ? GameSettings->GetOverallScalabilityLevel() : DefaultQualityLevel;
}

void UStdSettingsSubsystem::SetQualityLevel(int32 Level)
{
	UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GameSettings)
	{
		return;
	}

	GameSettings->SetOverallScalabilityLevel(FMath::Clamp(Level, 0, 3));
	GameSettings->ApplyNonResolutionSettings();
	// The engine's apply also writes its own frame-rate limit and V-Sync: the player's options win.
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("Quality level set to %d."), GameSettings->GetOverallScalabilityLevel());
}

// Video options

namespace StdVideo
{
	/**
	 * Sets an integer console variable at least at the game-override priority: above the project's renderer settings
	 * (r.AntiAliasingMethod is set there), and at the priority of the last set when that is higher (a value typed in the
	 * console), so the player's choice always takes.
	 */
	void SetInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			const uint32 Last = static_cast<uint32>(Var->GetFlags()) & static_cast<uint32>(ECVF_SetByMask);
			const uint32 Priority = FMath::Max(Last, static_cast<uint32>(ECVF_SetByGameOverride));
			if (Var->GetInt() != Value || Last < static_cast<uint32>(ECVF_SetByGameOverride))
			{
				Var->Set(Value, static_cast<EConsoleVariableFlags>(Priority));
			}
		}
	}

	int32 GetInt(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetInt() : 0;
	}

	float GetMaxFPS()
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
		return Var ? Var->GetFloat() : 0.f;
	}
}

int32 UStdSettingsSubsystem::GetFrameRateLimit() const
{
	return UStdUserSettings::Get()->GetFrameRateLimit();
}

void UStdSettingsSubsystem::SetFrameRateLimit(int32 FramesPerSecond)
{
	UStdUserSettings::Get()->SetFrameRateLimit(FramesPerSecond);
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("Frame-rate limit set to %d."), GetFrameRateLimit());
}

bool UStdSettingsSubsystem::IsFrameRateUnlimited() const
{
	return UStdUserSettings::Get()->IsFrameRateUnlimited();
}

void UStdSettingsSubsystem::SetFrameRateUnlimited(bool bUnlimited)
{
	UStdUserSettings::Get()->SetFrameRateUnlimited(bUnlimited);
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("Frame rate %s."), bUnlimited ? TEXT("unlimited") : TEXT("limited"));
}

int32 UStdSettingsSubsystem::GetAntiAliasingMethod() const
{
	return UStdUserSettings::Get()->GetAntiAliasingMethod();
}

void UStdSettingsSubsystem::SetAntiAliasingMethod(int32 Method)
{
	UStdUserSettings::Get()->SetAntiAliasingMethod(Method);
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("Anti-aliasing set to %d."), GetAntiAliasingMethod());
}

bool UStdSettingsSubsystem::IsVSyncEnabled() const
{
	return UStdUserSettings::Get()->IsVSyncEnabled();
}

void UStdSettingsSubsystem::SetVSyncEnabled(bool bEnabled)
{
	UStdUserSettings::Get()->SetVSyncEnabled(bEnabled);
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("V-Sync %s."), bEnabled ? TEXT("on") : TEXT("off"));
}

TArray<int32> UStdSettingsSubsystem::GetAntiAliasingChoices()
{
	TArray<int32> Choices = { 0, 1, 2 };
	Choices.AddUnique(UStdUserSettings::GetProjectAntiAliasingMethod());
	Choices.AddUnique(UStdUserSettings::GetDefaultAntiAliasingMethod());
	return Choices;
}

void UStdSettingsSubsystem::ApplyVideoOptions()
{
	const UStdUserSettings* UserSettings = UStdUserSettings::Get();
	AppliedFrameRateLimit = (bFrameCapsSuspended || UserSettings->IsFrameRateUnlimited()) ? 0 : UserSettings->GetFrameRateLimit();
	if (GEngine)
	{
		// SetMaxFPS keeps the priority t.MaxFPS was last set with, so it always takes.
		GEngine->SetMaxFPS(static_cast<float>(AppliedFrameRateLimit));
	}
	StdVideo::SetInt(TEXT("r.AntiAliasingMethod"), UserSettings->GetAntiAliasingMethod());
	StdVideo::SetInt(TEXT("r.VSync"), (!bFrameCapsSuspended && UserSettings->IsVSyncEnabled()) ? 1 : 0);
}

void UStdSettingsSubsystem::SetFrameCapsSuspended(bool bSuspended)
{
	bFrameCapsSuspended = bSuspended;
	ApplyVideoOptions();
	UE_LOG(LogStdCore, Log, TEXT("Frame-rate limit and V-Sync %s (engine: t.MaxFPS %d, r.VSync %d)."), bSuspended ? TEXT("suspended") : TEXT("restored"),
		AppliedFrameRateLimit, StdVideo::GetInt(TEXT("r.VSync")));
}

void UStdSettingsSubsystem::SaveEditorVideo()
{
	bHasEditorVideo = true;
	EditorMaxFPS = StdVideo::GetMaxFPS();
	EditorAntiAliasingMethod = StdVideo::GetInt(TEXT("r.AntiAliasingMethod"));
	EditorVSync = StdVideo::GetInt(TEXT("r.VSync"));
}

void UStdSettingsSubsystem::RestoreEditorVideo()
{
	if (!bHasEditorVideo)
	{
		return;
	}
	bHasEditorVideo = false;
	if (GEngine)
	{
		GEngine->SetMaxFPS(EditorMaxFPS);
	}
	StdVideo::SetInt(TEXT("r.AntiAliasingMethod"), EditorAntiAliasingMethod);
	StdVideo::SetInt(TEXT("r.VSync"), EditorVSync);
	UE_LOG(LogStdCore, Log, TEXT("Play session ended: the editor's t.MaxFPS %.0f, r.AntiAliasingMethod %d, r.VSync %d given back."),
		EditorMaxFPS, EditorAntiAliasingMethod, EditorVSync);
}

void UStdSettingsSubsystem::SaveSettings()
{
	UStdUserSettings::Get()->Save();
	if (UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		GameSettings->SaveSettings();
	}
}

void UStdSettingsSubsystem::LoadStartupVideoSettings()
{
	UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GameSettings)
	{
		return;
	}

	// UGameUserSettings::LoadSettings does not read scalability from the ini: it copies the live CVars and assumes
	// Scalability::LoadState already ran. Load the saved state into the CVars first, then read it back.
	Scalability::LoadState(*GGameUserSettingsIni);
	GameSettings->LoadSettings();

	// The quality selector offers four presets: a mixed ("Custom", -1) state has no entry, so snap it to the default.
	// Set only: saving now would write the live CVars, which still hold the mixed state.
	if (GameSettings->GetOverallScalabilityLevel() < 0)
	{
		// Normal on a first run: nothing is saved yet and the engine defaults mix levels.
		UE_LOG(LogStdCore, Log, TEXT("Quality matches no single preset (first run or mixed settings); using level %d."), DefaultQualityLevel);
		GameSettings->SetOverallScalabilityLevel(DefaultQualityLevel);
	}

	// ApplyNonResolutionSettings skips scalability until the engine reports itself initialized, which happens after
	// the game instance starts: apply once the engine loop has finished initializing.
	EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &ThisClass::ApplyStartupVideoSettings);
}

void UStdSettingsSubsystem::ApplyStartupVideoSettings()
{
	FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);

	if (UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		GameSettings->ApplyNonResolutionSettings();
		GameSettings->SaveSettings();
		UE_LOG(LogStdCore, Log, TEXT("Startup video settings applied: quality %d, window mode %s."),
			GameSettings->GetOverallScalabilityLevel(), LexToString(GameSettings->GetFullscreenMode()));
	}
	// The engine's apply wrote its own frame-rate limit and V-Sync: push the player's options again.
	ApplyVideoOptions();
}
