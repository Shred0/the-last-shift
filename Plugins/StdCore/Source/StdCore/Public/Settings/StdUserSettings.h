#pragma once

#include "CoreMinimal.h"
#include "Settings/StdProjectSettings.h"
#include "UObject/Object.h"
#include "StdUserSettings.generated.h"

/**
 * Player preferences owned by the plugin, saved in GameUserSettings.ini under [/Script/StdCore.StdUserSettings].
 * A plain config object instead of a UGameUserSettings subclass, so the host project does not need
 * GameUserSettingsClassName in its DefaultEngine.ini. Window mode and quality stay in UGameUserSettings.
 *
 * Only the class default object is used: UStdUserSettings::Get().
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class STDCORE_API UStdUserSettings : public UObject
{
	GENERATED_BODY()

public:
	static UStdUserSettings* Get() { return GetMutableDefault<UStdUserSettings>(); }

	static constexpr float DefaultMasterVolume = 1.f;
	static constexpr float DefaultMusicVolume = 0.8f;
	static constexpr float DefaultSfxVolume = 1.f;

	/** All volumes are 0..1; values read from a hand-edited ini are clamped. */
	float GetMasterVolume() const { return FMath::Clamp(MasterVolume, 0.f, 1.f); }
	float GetMusicVolume() const { return FMath::Clamp(MusicVolume, 0.f, 1.f); }
	float GetSfxVolume() const { return FMath::Clamp(SfxVolume, 0.f, 1.f); }

	void SetMasterVolume(float Value) { MasterVolume = FMath::Clamp(Value, 0.f, 1.f); }
	void SetMusicVolume(float Value) { MusicVolume = FMath::Clamp(Value, 0.f, 1.f); }
	void SetSfxVolume(float Value) { SfxVolume = FMath::Clamp(Value, 0.f, 1.f); }

	// Video options. Each getter gives the player's choice, or the project's default (UStdProjectSettings, Video) when the
	// player has not chosen (a settings file saved before the option existed has not) or the project hides the option.

	/** Frames a second, 10 to 1000. Kept while the frame rate is unlimited. */
	int32 GetFrameRateLimit() const;
	bool IsFrameRateUnlimited() const;
	/** r.AntiAliasingMethod's number: 0 none, 1 FXAA, 2 TAA, 3 MSAA, 4 TSR. */
	int32 GetAntiAliasingMethod() const;
	bool IsVSyncEnabled() const;

	/** Clamped to 10..1000. */
	void SetFrameRateLimit(int32 FramesPerSecond);
	void SetFrameRateUnlimited(bool bUnlimited);
	/** Clamped to 0..4. */
	void SetAntiAliasingMethod(int32 Method);
	void SetVSyncEnabled(bool bEnabled);

	/** Whether the player's choice is stored for this option (else it follows the project's default). */
	bool HasChosen(EStdVideoOption Option) const;

	/** The anti-aliasing method the project's renderer settings set (r.AntiAliasingMethod in its engine config). */
	static int32 GetProjectAntiAliasingMethod();

	/** The anti-aliasing method of a player who has not chosen: the project's default, or the project's own method. */
	static int32 GetDefaultAntiAliasingMethod();

	/** Volumes to their defaults and every video option back to not chosen. */
	void ResetToDefaults();

	/** Writes the values to GameUserSettings.ini. */
	void Save();

private:
	UPROPERTY(Config)
	float MasterVolume = DefaultMasterVolume;

	UPROPERTY(Config)
	float MusicVolume = DefaultMusicVolume;

	UPROPERTY(Config)
	float SfxVolume = DefaultSfxVolume;

	/** Frames a second; 0: not chosen. */
	UPROPERTY(Config)
	int32 FrameRateLimit = 0;

	/** -1: not chosen, 0: limited, 1: unlimited. */
	UPROPERTY(Config)
	int32 FrameRateUnlimited = -1;

	/** -1: not chosen, else r.AntiAliasingMethod's number. */
	UPROPERTY(Config)
	int32 AntiAliasingMethod = -1;

	/** -1: not chosen, 0: off, 1: on. */
	UPROPERTY(Config)
	int32 VSync = -1;
};
