// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Flow/StdFlowSubsystem.h"
#include "Narrative/NarrativeRows.h"
#include "Run/TheLastShiftSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TextChannel/TextChannelComponent.h"
#include "Containers/Ticker.h"
#include "Time/TimeState.h"
#include "GameAudioSubsystem.generated.h"

class AActor;
class AChamberBattery;
class UAudioComponent;
class ALogTerminal;
class APowerTerminal;
class UScanDirectorComponent;
class UStdTextSceneWidget;
class UWorld;
struct FActorsInitializedParams;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogGameAudio, Log, All);

/** One request for a sound, as the audio recorded it (for the tests: what was asked, where and at which pitch). */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FGameSoundRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	EGameSound Sound = EGameSound::Scan;

	/** From a place in the world (doors, hatches, terminals); false: the suit's (or the menu's music). */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bInWorld = false;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	float Pitch = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	float Volume = 1.f;

	/** The slot had a sound and it was asked to play (false: an empty slot, nothing played). */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bHadSound = false;

	/** Music only: a request to stop (fade out) instead of to play. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bStop = false;

	/** What asked: the actor, the voice, the scene and line, the exit reason. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FString Source;

	/** The map, without the PIE prefix. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FString Map;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	double RealSeconds = 0.0;
};

/**
 * The one place of the game that plays sounds (Brief 24). It listens to the events of the game's systems, which know
 * nothing of sound, and plays the slot of UTheLastShiftSettings::Audio (the one owner of every sound and its volume):
 *
 * - The scan director of each world: every scan's start (Scan), the end of a cooldown (ScanReady), every exit from
 *   stopped time, exactly one sound: Slip when it jumps (Jumped, RewoundAndJumped, or Resumed with the entity's graze:
 *   AEntity::RecordStopExit then WasLastExitGraze, as UHomeStateSubsystem does), TimeResume otherwise (a Resume, a
 *   rewind without a jump; in a map with no entity a Resume is TimeResume). The time state: every Stop (TimeStop).
 * - Doors, hatches and terminals, from where they stand, fading over WorldFalloffCm: every motion of a door or
 *   hatch is a timed motion (Brief 45, and since Brief 46 every ADoorDevice too: Motion, pitch 1 for an ADoorDevice, HatchPitch for APrologueSlidingPanel and
 *   APrologueHatch: one looping sound asked for once at the motion's start, faded in over MotionFadeInSeconds, never asked
 *   for again while the motion lasts, faded out over MotionFadeOutSeconds at OnMotionEnded or when the motion reverses),
 *   APowerTerminal::OnAnyActivated and ALogTerminal::OnRead (Terminal).
 * - The suit's: AResidue::OnCollected (Energy), UTextChannelComponent::OnTypingStarted (Line), the text scenes' lines
 *   (Line) and their first change from the paradise's white to black (Interruption, once in a scene). Brief 35: the
 *   interruption's noise lasts as long as the static at that cut: it starts with it at its volume, is held (Brief 45: one
 *   sound made to loop, played once and never again) for the static's StaticSeconds and fades out with the static's own fade over StaticFadeSeconds (its volume
 *   follows the static's strength, read from the text scene screen: UStdTextSceneWidget::GetEffectOpacity), and stops
 *   with it whenever the static ends early (the next line, the scene skipped or closed).
 * - The main menu's music (MenuMusic, through StdCore's music subsystem): on while the main menu map shows its menu, off
 *   (faded over MusicFadeOutSeconds) when a text scene's screen opens over it (the opening scene, the ending), when a
 *   travel leaves it, and in every other map.
 *
 * - Brief 36B: the battery's hall's hum (ChamberHum), the game's one ambience: one looping sound asked for once when a map
 *   with the hall's battery (AChamberBattery) begins, from the battery's place, at full level within ChamberHumFullRadiusCm
 *   and falling to nothing over ChamberHumFalloffCm beyond it. Every frame it follows what the battery announces: its
 *   level (volume and pitch between the Hum values), the time state (out over ChamberHumFadeSeconds at a Stop, back the
 *   same way at the return to Normal time) and the aftermath of the choice (Open: volume and pitch sinking to nothing with
 *   the glow; Inject: rising past their full values by AftermathSurge). Behind the pause menu it pauses with the game.
 * The effects play in StdCore's SFX sound class and are no UI sounds: they pause with the game behind the pause menu.
 * An empty slot plays nothing and logs nothing. Every request is recorded (GetRecord) for the tests.
 */
UCLASS()
class THELASTSHIFT_API UGameAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UGameAudioSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Plays Sound as the suit's: the same wherever the player stands. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlaySound(EGameSound Sound, const FString& Source);

	/** Plays Sound from Location in the current world, fading over WorldFalloffCm, at Pitch. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlaySoundAt(EGameSound Sound, FVector Location, float Pitch, const FString& Source);

	/** The menu's music is wanted now (the main menu map shows its menu, no scene over it, not leaving). */
	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsMusicOn() const { return bMusicOn; }

	/** Every request since the last ClearRecord, oldest first (at most MaxRecord: the oldest are dropped). */
	const TArray<FGameSoundRequest>& GetRecord() const { return Record; }

	/** Requests dropped from the front of the record since the last ClearRecord (so that indices stay comparable). */
	int32 GetRecordOffset() const { return RecordOffset; }

	void ClearRecord();

	/** Requests of Sound (music stops excluded) recorded from absolute index From (GetRecordOffset() + index) on. */
	int32 CountRequests(EGameSound Sound, int32 From = 0) const;

	/** Absolute index of the next request to be recorded. */
	int32 GetRecordEnd() const { return RecordOffset + Record.Num(); }

	/** The request at absolute index Index, or null when dropped or not recorded yet. */
	const FGameSoundRequest* GetRequest(int32 Index) const;

	static constexpr int32 MaxRecord = 4096;

	/** The sound of Mover's timed motion, while it is held (from the motion's start to the end of its fade); null otherwise. */
	UAudioComponent* GetMotionSound(const AActor* Mover) const;

	/** Mover's timed motion sound is playing now. */
	bool IsMotionSoundPlaying(const AActor* Mover) const;

	/** Times the sound of Mover's current or last timed motion was asked to play: 1 (Brief 45: it loops, never played again). */
	int32 GetMotionPlays(const AActor* Mover) const { return MotionPlays.FindRef(Mover); }

	/** Brief 35: the interruption's noise is held now (from the static's start to the end of its fade, or until it ends early). */
	bool IsInterruptionHeld() const { return bInterruptionHeld; }

	/** Brief 35: the interruption's noise is playing now. */
	bool IsInterruptionPlaying() const;

	/** Brief 35: times the current or last interruption's noise was asked to play: 1 (Brief 45: it loops, never played again). */
	int32 GetInterruptionPlays() const { return InterruptionPlays; }

	/** Brief 45: the interruption's noise's one component while it is held (null otherwise or with an empty slot). */
	UAudioComponent* GetInterruptionComponent() const { return InterruptionSound; }

	/** Brief 35: the share of its slot's volume the interruption's noise plays at now (the static's strength). */
	float GetInterruptionShare() const { return InterruptionShare; }

	/** Brief 36B: the hall's hum: its component while the hall's map shows (null otherwise or with an empty slot). */
	UAudioComponent* GetHumComponent() const { return HumSound; }

	/** Brief 36B: the hum is playing now (it plays on, silent, while Stopped). */
	bool IsHumPlaying() const;

	/** Brief 36B: the volume multiplier and pitch the hum is given now, and its fade share (0 while Stopped, 1 in Normal time). */
	float GetHumVolume() const { return HumVolume; }
	float GetHumPitch() const { return HumPitch; }
	float GetHumFade() const { return HumFade; }

	/** Brief 36B: times the hum was asked for since the game instance began (once a hall). */
	int32 GetHumStarts() const { return HumStarts; }

private:
	void HandleWorldInitializedActors(const FActorsInitializedParams& Params);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleWorldBeganPlay(UWorld* World);
	void HandleActorSpawned(AActor* Actor);
	void BindActor(AActor* Actor);
	bool IsOurWorld(const UWorld* World) const;
	UWorld* CurrentWorld() const;

	void HandleDirectorBeganPlay(UScanDirectorComponent* Director);
	void HandleDirectorEndingPlay(UScanDirectorComponent* Director);
	void HandleAnyTerminalActivated(APowerTerminal* Terminal, int32 Cost);

	UFUNCTION()
	void HandleScanStarted(int32 ScanIndex, float Timestamp);

	UFUNCTION()
	void HandleScanReady();

	UFUNCTION()
	void HandleStopExited(EStopExitReason Reason, float StoppedSeconds);

	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	UFUNCTION()
	void HandleMotionStarted(AActor* Mover, bool bOpening, bool bTimed);

	UFUNCTION()
	void HandleMotionEnded(AActor* Mover);

	/** A motion sound finished (faded out; or, while held, a sound wave that does not loop: warned, never played again). */
	void HandleMotionSoundFinished(UAudioComponent* Component);

	/** Stops Mover's held sound: faded over FadeSeconds (0: at once), recorded as a stop with Why. */
	void ReleaseMotionSound(AActor* Mover, float FadeSeconds, const TCHAR* Why);

	/** Creates the component of a sound (not yet playing; null for an empty slot or no audio device). */
	UAudioComponent* CreateSoundComponent(EGameSound Sound, const FVector* Location, float Pitch, FGameSoundRequest& Request);

	UFUNCTION()
	void HandleLogRead(ALogTerminal* Terminal);

	UFUNCTION()
	void HandleResidueCollected();

	UFUNCTION()
	void HandleTypingStarted(FName RowName, ETextVoice Voice, bool bRestart);

	UFUNCTION()
	void HandleSceneLineStarted(FName Scene, int32 Index, ESceneVoice Voice, FName Cue);

	UFUNCTION()
	void HandleSceneScreenChanged(FName Scene, bool bOpen);

	UFUNCTION()
	void HandleFlowStateChanged(EStdFlowState NewState);

	/** Starts or stops the menu's music to match the state now. */
	void UpdateMusic(const TCHAR* Why);

	/** Brief 35: holds the interruption's noise with the static of Screen's line Index (its sound already asked for). */
	void HoldInterruption(UStdTextSceneWidget* Screen, int32 Index, const FString& Source);

	/** Brief 35: follows the static every frame: its strength sets the noise's volume; its end, or the line's, stops it. */
	bool TickInterruption(float DeltaSeconds);

	/** Brief 35: a held interruption's noise finished before the static (a sound wave that does not loop): warned, never played again. */
	void HandleInterruptionFinished(UAudioComponent* Component);

	/** Brief 35: stops the interruption's noise (recorded as a stop with Why). */
	void ReleaseInterruption(const TCHAR* Why);

	/** Brief 36B: starts the hall's hum from Battery's place (recorded once), and follows the battery every frame. */
	void StartHum(AChamberBattery* Battery);
	bool TickHum(float DeltaSeconds);
	void StopHum(const TCHAR* Why);

	/** The world is the main menu map (StdCore's MainMenuMap). */
	static bool IsMenuWorld(const UWorld* World);

	FGameSoundRequest& AddRequest(EGameSound Sound, const FString& Source);

	TArray<FGameSoundRequest> Record;
	int32 RecordOffset = 0;

	FDelegateHandle InitializedActorsHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle DirectorBeganHandle;
	FDelegateHandle DirectorEndingHandle;
	FDelegateHandle TerminalHandle;
	TMap<TWeakObjectPtr<UWorld>, FDelegateHandle> SpawnHandles;
	TMap<TWeakObjectPtr<UWorld>, FDelegateHandle> BeginPlayHandles;

	/** The current text scene: whether its last line was on the paradise's white, and whether it was interrupted. */
	FName SceneName;
	bool bSceneLastWhite = false;
	bool bSceneInterrupted = false;

	/** Brief 35: the interruption's noise held with the static: its component, the screen and line it follows, its plays. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> InterruptionSound;
	TWeakObjectPtr<UStdTextSceneWidget> InterruptionScreen;
	int32 InterruptionIndex = INDEX_NONE;
	int32 InterruptionPlays = 0;
	float InterruptionShare = 0.f;
	float InterruptionVolume = 1.f;
	double InterruptionStartReal = 0.0;
	bool bInterruptionHeld = false;
	FTSTicker::FDelegateHandle InterruptionTicker;

	bool bMusicOn = false;

	/** Brief 36B: the hall's hum. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HumSound;
	TWeakObjectPtr<AChamberBattery> HumBattery;
	FTSTicker::FDelegateHandle HumTicker;
	float HumVolume = 0.f;
	float HumPitch = 1.f;
	float HumFade = 0.f;
	int32 HumStarts = 0;

	/** The held sounds of the timed motions under way, by mover; the components kept alive while held. */
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UAudioComponent>> MotionSounds;
	TMap<TWeakObjectPtr<AActor>, int32> MotionPlays;

	/** The movers whose motion is under way (their sound loops); a mover leaves it when its motion ends or reverses. */
	TSet<TWeakObjectPtr<AActor>> HeldMovers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> HeldComponents;
	/** A travel left the main menu (until the next world begins). */
	bool bLeavingMenu = false;
};
