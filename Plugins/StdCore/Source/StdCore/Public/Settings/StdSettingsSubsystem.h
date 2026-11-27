#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StdSettingsSubsystem.generated.h"

struct FActorsInitializedParams;

UENUM(BlueprintType)
enum class EStdVolumeChannel : uint8
{
	Master,
	Music,
	Sfx
};

/** The volume the Sound Mix gives each plugin Sound Class. */
struct FStdClassVolumes
{
	float Master = 1.f;
	float Music = 1.f;
	float Sfx = 1.f;
	float Ui = 1.f;
};

namespace StdAudio
{
	/**
	 * One explicit formula per branch, so the master is applied exactly once whatever the hierarchy does: the root
	 * stays at 1 and each branch carries Master x its own volume (the UI branch follows the master only).
	 */
	STDCORE_API FStdClassVolumes ComputeClassVolumes(float Master, float Music, float Sfx);
}

/**
 * Volumes, window mode, graphics quality and the video options (frame-rate limit, anti-aliasing, V-Sync).
 *
 * Volumes are stored in UStdUserSettings and applied through the Sound Mix in UStdProjectSettings: every change
 * is audible at once, SaveSettings writes it to disk. The mix is applied again on every new world, because a map
 * load can drop it. Window mode and quality use the engine's UGameUserSettings.
 *
 * The video options are stored in UStdUserSettings too (nowhere else) and pushed to the engine's console variables
 * (t.MaxFPS, r.AntiAliasingMethod, r.VSync): when the game instance starts, before the first frame of its first map; at
 * once when one changes; and again after every apply of the engine's UGameUserSettings, which writes its own frame-rate
 * limit and V-Sync. In the editor a play session pushes them too, and the values the editor had are given back when it
 * ends.
 */
UCLASS()
class STDCORE_API UStdSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Audio

	UFUNCTION(BlueprintPure, Category = "Std Core|Settings")
	float GetVolume(EStdVolumeChannel Channel) const;

	/** Clamped to 0..1 and audible immediately. Not saved until SaveSettings. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings")
	void SetVolume(EStdVolumeChannel Channel, float Volume);

	/** Applies the stored volumes to the Sound Mix. Safe to call at any time; does nothing without a world or a mix. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings")
	void ApplyVolumes();

	// Video

	UFUNCTION(BlueprintPure, Category = "Std Core|Settings")
	EWindowMode::Type GetWindowMode() const;

	/** Applied immediately. Fullscreen modes use the desktop resolution; windowed mode shrinks a desktop-sized window. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings")
	void SetWindowMode(EWindowMode::Type WindowMode);

	/** 0 Low, 1 Medium, 2 High, 3 Epic; -1 when the scalability groups do not match a single level. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Settings")
	int32 GetQualityLevel() const;

	/** Applied immediately. Clamped to 0..3. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings")
	void SetQualityLevel(int32 Level);

	// Video options: each applied at once, saved by SaveSettings.

	/** Frames a second. Kept while the frame rate is unlimited. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	int32 GetFrameRateLimit() const;

	/** One of UStdProjectSettings::FrameRateLimits, or any value from 10 to 1000. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void SetFrameRateLimit(int32 FramesPerSecond);

	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	bool IsFrameRateUnlimited() const;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void SetFrameRateUnlimited(bool bUnlimited);

	/** r.AntiAliasingMethod's number: 0 none, 1 FXAA, 2 TAA, 3 MSAA, 4 TSR. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	int32 GetAntiAliasingMethod() const;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void SetAntiAliasingMethod(int32 Method);

	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	bool IsVSyncEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void SetVSyncEnabled(bool bEnabled);

	/**
	 * The anti-aliasing methods the settings screen offers, in order: none, FXAA and TAA, then the project's own method
	 * and the project's default when they are not among them.
	 */
	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	static TArray<int32> GetAntiAliasingChoices();

	/** Pushes the three video options to the engine. Safe to call at any time. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void ApplyVideoOptions();

	/**
	 * While suspended the engine gets no frame-rate limit and no V-Sync, whatever the options say; nothing is saved and
	 * the options are pushed again when the suspension ends. For measures that must run without a cap.
	 */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings|Video")
	void SetFrameCapsSuspended(bool bSuspended);

	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	bool AreFrameCapsSuspended() const { return bFrameCapsSuspended; }

	/** The frame-rate limit last pushed to the engine (0: none). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Settings|Video")
	int32 GetAppliedFrameRateLimit() const { return AppliedFrameRateLimit; }

	/** What this game instance pushed when it started, and on which frame (GFrameCounter); its first map's frame. */
	struct FStartupVideo
	{
		uint64 Frame = 0;
		uint64 FirstWorldFrame = 0;
		int32 FrameRateLimit = -1;
		int32 AntiAliasingMethod = -1;
		int32 VSync = -1;
	};
	const FStartupVideo& GetStartupVideo() const { return StartupVideo; }

	/** Writes volumes (GameUserSettings.ini, StdUserSettings section) and video settings to disk. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Settings")
	void SaveSettings();

	/** Quality used when nothing valid is saved: Medium. Fixed, so every log can state what the game started on. */
	static constexpr int32 DefaultQualityLevel = 1;

private:
	void HandleWorldInitialized(const FActorsInitializedParams& Params);
	void ApplyVolumesInternal(bool bPushMix);
	void LoadStartupVideoSettings();
	void ApplyStartupVideoSettings();
	void SaveEditorVideo();
	void RestoreEditorVideo();

	FDelegateHandle WorldInitializedHandle;
	FDelegateHandle EngineInitCompleteHandle;

	bool bFrameCapsSuspended = false;
	int32 AppliedFrameRateLimit = 0;
	FStartupVideo StartupVideo;

	/** In the editor: the engine's values before this play session pushed its own. */
	bool bHasEditorVideo = false;
	float EditorMaxFPS = 0.f;
	int32 EditorAntiAliasingMethod = 0;
	int32 EditorVSync = 0;
};
