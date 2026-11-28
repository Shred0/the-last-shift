// SPDX-License-Identifier: MIT

#include "Audio/GameAudioSubsystem.h"

#include "AudioDevice.h"
#include "Audio/StdMusicSubsystem.h"
#include "Components/AudioComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Epilogue/ChamberBattery.h"
#include "Prologue/PrologueHatch.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Residue/Residue.h"
#include "Run/RunSceneSubsystem.h"
#include "Scan/ScanDirectorComponent.h"
#include "Settings/StdProjectSettings.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdTextSceneWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameAudioSubsystem)

DEFINE_LOG_CATEGORY(LogGameAudio);

const FGameSoundSlot* FGameAudio::FindEffect(EGameSound Sound) const
{
	switch (Sound)
	{
	case EGameSound::Scan: return &Scan;
	case EGameSound::ScanReady: return &ScanReady;
	case EGameSound::TimeStop: return &TimeStop;
	case EGameSound::TimeResume: return &TimeResume;
	case EGameSound::Slip: return &Slip;
	case EGameSound::Line: return &Line;
	case EGameSound::Terminal: return &Terminal;
	case EGameSound::Door: return &Door;
	case EGameSound::Motion: return &Motion;
	case EGameSound::Energy: return &Energy;
	case EGameSound::Interruption: return &Interruption;
	case EGameSound::ChamberHum: return &ChamberHum;
	default: return nullptr;
	}
}

FGameSoundSlot* FGameAudio::FindEffect(EGameSound Sound)
{
	return const_cast<FGameSoundSlot*>(static_cast<const FGameAudio*>(this)->FindEffect(Sound));
}

const TSoftObjectPtr<USoundBase>& FGameAudio::GetSound(EGameSound Sound) const
{
	const FGameSoundSlot* Slot = FindEffect(Sound);
	return Slot ? Slot->Sound : MenuMusic;
}

TSoftObjectPtr<USoundBase>& FGameAudio::GetSound(EGameSound Sound)
{
	return const_cast<TSoftObjectPtr<USoundBase>&>(static_cast<const FGameAudio*>(this)->GetSound(Sound));
}

float FGameAudio::GetVolume(EGameSound Sound) const
{
	const FGameSoundSlot* Slot = FindEffect(Sound);
	return Slot ? Slot->GetVolume() : 1.f;
}

namespace GameAudio
{

	static const TCHAR* VoiceName(ETextVoice Voice)
	{
		switch (Voice)
		{
		case ETextVoice::Twin: return TEXT("Twin");
		case ETextVoice::Engineer: return TEXT("Engineer");
		case ETextVoice::Home:
		default: return TEXT("Home");
		}
	}

	static FString MapOf(const UWorld* World)
	{
		return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
	}

	/** StdCore's SFX sound class, so that the game's effects follow its SFX and Master volumes. */
	static USoundClass* SfxClass()
	{
		const UStdProjectSettings* Std = UStdProjectSettings::Get();
		return Std ? StdSettings::LoadAsset(Std->SfxSoundClass, TEXT("SfxSoundClass"), StdCoreDefaults::SfxSoundClass) : nullptr;
	}
}

UGameAudioSubsystem* UGameAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UGameAudioSubsystem>() : nullptr;
}

bool UGameAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UGameAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<URunSceneSubsystem>();
	Collection.InitializeDependency<UStdFlowSubsystem>();
	Collection.InitializeDependency<UStdMusicSubsystem>();

	InitializedActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UGameAudioSubsystem::HandleWorldInitializedActors);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UGameAudioSubsystem::HandleWorldCleanup);
	DirectorBeganHandle = UScanDirectorComponent::OnDirectorBeganPlay.AddUObject(this, &UGameAudioSubsystem::HandleDirectorBeganPlay);
	DirectorEndingHandle = UScanDirectorComponent::OnDirectorEndingPlay.AddUObject(this, &UGameAudioSubsystem::HandleDirectorEndingPlay);
	TerminalHandle = APowerTerminal::OnAnyActivated.AddUObject(this, &UGameAudioSubsystem::HandleAnyTerminalActivated);

	if (URunSceneSubsystem* Scenes = GetGameInstance()->GetSubsystem<URunSceneSubsystem>())
	{
		Scenes->OnSceneLineStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleSceneLineStarted);
		Scenes->OnSceneScreenChanged.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleSceneScreenChanged);
	}
	if (UStdFlowSubsystem* Flow = GetGameInstance()->GetSubsystem<UStdFlowSubsystem>())
	{
		Flow->OnStateChanged.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleFlowStateChanged);
	}
}

void UGameAudioSubsystem::Deinitialize()
{
	ReleaseInterruption(TEXT("the game instance ends"));
	StopHum(TEXT("the game instance ends"));
	FWorldDelegates::OnWorldInitializedActors.Remove(InitializedActorsHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	UScanDirectorComponent::OnDirectorBeganPlay.Remove(DirectorBeganHandle);
	UScanDirectorComponent::OnDirectorEndingPlay.Remove(DirectorEndingHandle);
	APowerTerminal::OnAnyActivated.Remove(TerminalHandle);
	for (const TPair<TWeakObjectPtr<UWorld>, FDelegateHandle>& Pair : SpawnHandles)
	{
		if (UWorld* World = Pair.Key.Get())
		{
			World->RemoveOnActorSpawnedHandler(Pair.Value);
		}
	}
	for (const TPair<TWeakObjectPtr<UWorld>, FDelegateHandle>& Pair : BeginPlayHandles)
	{
		if (UWorld* World = Pair.Key.Get())
		{
			World->OnWorldBeginPlay.Remove(Pair.Value);
		}
	}
	SpawnHandles.Reset();
	BeginPlayHandles.Reset();
	Super::Deinitialize();
}

bool UGameAudioSubsystem::IsOurWorld(const UWorld* World) const
{
	return World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance();
}

UWorld* UGameAudioSubsystem::CurrentWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

bool UGameAudioSubsystem::IsMenuWorld(const UWorld* World)
{
	const UStdProjectSettings* Std = UStdProjectSettings::Get();
	return World && Std && !Std->MainMenuMap.IsNull() && GameAudio::MapOf(World) == Std->MainMenuMap.ToSoftObjectPath().GetLongPackageName();
}

// --- Worlds and actors ---

void UGameAudioSubsystem::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!IsOurWorld(World))
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		BindActor(*It);
	}
	if (!SpawnHandles.Contains(World))
	{
		SpawnHandles.Add(World, World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UGameAudioSubsystem::HandleActorSpawned)));
	}
	if (!BeginPlayHandles.Contains(World))
	{
		TWeakObjectPtr<UWorld> Weak(World);
		BeginPlayHandles.Add(World, World->OnWorldBeginPlay.AddWeakLambda(this, [this, Weak]() { HandleWorldBeganPlay(Weak.Get()); }));
	}
	if (UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>())
	{
		Time->OnTimeStateChanged.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleTimeStateChanged);
	}
}

void UGameAudioSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	for (auto It = MotionSounds.CreateIterator(); It; ++It)
	{
		const AActor* Mover = It->Key.Get();
		if (!Mover || Mover->GetWorld() == World)
		{
			HeldMovers.Remove(It->Key);
			MotionPlays.Remove(It->Key);
			It.RemoveCurrent();
		}
	}
	HeldComponents.RemoveAll([World](const TObjectPtr<UAudioComponent>& C) { return !C || C->GetWorld() == World; });
	if ((HumSound && HumSound->GetWorld() == World) || (HumBattery.IsValid() && HumBattery->GetWorld() == World))
	{
		StopHum(TEXT("the hall's map ends"));
	}
	if (const FDelegateHandle* Handle = SpawnHandles.Find(World))
	{
		World->RemoveOnActorSpawnedHandler(*Handle);
		SpawnHandles.Remove(World);
	}
	if (const FDelegateHandle* Handle = BeginPlayHandles.Find(World))
	{
		World->OnWorldBeginPlay.Remove(*Handle);
		BeginPlayHandles.Remove(World);
	}
}

void UGameAudioSubsystem::HandleWorldBeganPlay(UWorld* World)
{
	if (!IsOurWorld(World))
	{
		return;
	}
	bLeavingMenu = false;
	UpdateMusic(IsMenuWorld(World) ? TEXT("the main menu map began") : TEXT("a map began"));
	if (AChamberBattery* Battery = AChamberBattery::Find(World))
	{
		StartHum(Battery);
	}
}

void UGameAudioSubsystem::StartHum(AChamberBattery* Battery)
{
	StopHum(TEXT("a hall begins"));
	if (!Battery)
	{
		return;
	}
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	const FVector Location = Battery->GetActorLocation();
	FGameSoundRequest& Request = AddRequest(EGameSound::ChamberHum, FString::Printf(TEXT("the hall's hum: %s"), *Battery->GetName()));
	Request.bInWorld = true;
	Request.Location = Location;
	Request.Pitch = 1.f;
	++HumStarts;
	HumBattery = Battery;
	UAudioComponent* Component = CreateSoundComponent(EGameSound::ChamberHum, &Location, 1.f, Request);
	if (!Component)
	{
		// An empty slot: nothing plays, and that is not an error.
		return;
	}
	// From the battery: at its full level within ChamberHumFullRadiusCm, falling to nothing over ChamberHumFalloffCm.
	Component->AttenuationOverrides.AttenuationShapeExtents = FVector(Audio.GetChamberHumFullRadiusCm(), 0.f, 0.f);
	Component->AttenuationOverrides.FalloffDistance = Audio.GetChamberHumFalloffCm();
	Component->bAutoDestroy = false;
	HumSound = Component;
	const UTimeStateSubsystem* Time = Battery->GetWorld() ? Battery->GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	HumFade = Time && Time->IsStopped() ? 0.f : 1.f;
	TickHum(0.f);
	Component->Play();
	HumTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UGameAudioSubsystem::TickHum));
	UE_LOG(LogGameAudio, Log, TEXT("Audio: the hall's hum from %s (volume %.2f, pitch %.2f)."), *Location.ToCompactString(), HumVolume, HumPitch);
}

bool UGameAudioSubsystem::TickHum(float DeltaSeconds)
{
	AChamberBattery* Battery = HumBattery.Get();
	UAudioComponent* Component = HumSound;
	if (!Battery || !Component)
	{
		return true;
	}
	const UWorld* World = Battery->GetWorld();
	const UTimeStateSubsystem* Time = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	// Behind the pause menu it pauses with the game and keeps its values.
	if (Time && Time->IsRunFrozen())
	{
		return true;
	}
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	// Normal time only: out at a Stop and back at the return to Normal time, over ChamberHumFadeSeconds (real time).
	const float Target = Time && Time->IsStopped() ? 0.f : 1.f;
	const float Step = DeltaSeconds / Audio.GetChamberHumFadeSeconds();
	HumFade = Target > HumFade ? FMath::Min(Target, HumFade + Step) : FMath::Max(Target, HumFade - Step);

	// The battery the hall shows, and the aftermath of the choice.
	const float B = Battery->GetBatteryShown();
	float Volume = Audio.GetHumVolumeAt(B);
	float Pitch = Audio.GetHumPitchAt(B);
	const FName Ending = Battery->GetAftermathEnding();
	const float A = FMath::Clamp(Battery->GetAftermathAlpha(), 0.f, 1.f);
	const float Ease = A * A * (3.f - 2.f * A);
	if (Ending == TEXT("Open"))
	{
		// The structure lets go: volume and pitch sink to nothing with the glow; silent at the aftermath's end.
		Volume *= 1.f - Ease;
		Pitch = FMath::Lerp(Pitch, 0.4f, Ease);
	}
	else if (Ending == TEXT("Inject"))
	{
		const float Surge = 1.f + Settings->GetAftermathSurge();
		Volume = FMath::Lerp(Volume, Audio.GetHumVolumeFull() * Surge, Ease);
		Pitch = FMath::Lerp(Pitch, FMath::Min(Audio.GetHumPitchFull() * Surge, 2.f), Ease);
	}
	HumVolume = Audio.GetVolume(EGameSound::ChamberHum) * Volume * HumFade;
	HumPitch = Pitch;
	Component->SetVolumeMultiplier(HumVolume);
	Component->SetPitchMultiplier(HumPitch);
	if (!Component->IsPlaying() && HumVolume > 0.f)
	{
		Component->Play();
	}
	return true;
}

void UGameAudioSubsystem::StopHum(const TCHAR* Why)
{
	if (HumTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HumTicker);
		HumTicker.Reset();
	}
	if (UAudioComponent* Component = HumSound)
	{
		Component->Stop();
		Component->DestroyComponent();
		UE_LOG(LogGameAudio, Log, TEXT("Audio: the hall's hum stops (%s)."), Why);
	}
	HumSound = nullptr;
	HumBattery = nullptr;
	HumVolume = 0.f;
	HumFade = 0.f;
}

bool UGameAudioSubsystem::IsHumPlaying() const
{
	return HumSound && HumSound->IsPlaying();
}

void UGameAudioSubsystem::HandleActorSpawned(AActor* Actor)
{
	BindActor(Actor);
}

void UGameAudioSubsystem::BindActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	if (ADoorDevice* Door = Cast<ADoorDevice>(Actor))
	{
		Door->OnMotionStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionStarted);
		Door->OnMotionEnded.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionEnded);
	}
	else if (APrologueSlidingPanel* Panel = Cast<APrologueSlidingPanel>(Actor))
	{
		Panel->OnMotionStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionStarted);
		Panel->OnMotionEnded.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionEnded);
	}
	else if (APrologueHatch* Hatch = Cast<APrologueHatch>(Actor))
	{
		Hatch->OnMotionStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionStarted);
		Hatch->OnMotionEnded.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleMotionEnded);
	}
	if (ALogTerminal* Log = Cast<ALogTerminal>(Actor))
	{
		Log->OnRead.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleLogRead);
	}
	if (AResidue* Residue = Cast<AResidue>(Actor))
	{
		Residue->OnCollected.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleResidueCollected);
	}
	TInlineComponentArray<UTextChannelComponent*> Channels(Actor);
	for (UTextChannelComponent* Channel : Channels)
	{
		Channel->OnTypingStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleTypingStarted);
	}
}

void UGameAudioSubsystem::HandleDirectorBeganPlay(UScanDirectorComponent* Director)
{
	if (Director && IsOurWorld(Director->GetWorld()))
	{
		Director->OnScanStarted.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleScanStarted);
		Director->OnScanReady.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleScanReady);
		Director->OnStopExited.AddUniqueDynamic(this, &UGameAudioSubsystem::HandleStopExited);
	}
}

void UGameAudioSubsystem::HandleDirectorEndingPlay(UScanDirectorComponent* Director)
{
	if (Director)
	{
		Director->OnScanStarted.RemoveDynamic(this, &UGameAudioSubsystem::HandleScanStarted);
		Director->OnScanReady.RemoveDynamic(this, &UGameAudioSubsystem::HandleScanReady);
		Director->OnStopExited.RemoveDynamic(this, &UGameAudioSubsystem::HandleStopExited);
	}
}

// --- Events ---

void UGameAudioSubsystem::HandleScanStarted(int32 ScanIndex, float Timestamp)
{
	PlaySound(EGameSound::Scan, FString::Printf(TEXT("scan %d"), ScanIndex));
}

void UGameAudioSubsystem::HandleScanReady()
{
	PlaySound(EGameSound::ScanReady, TEXT("cooldown ended"));
}

void UGameAudioSubsystem::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	if (NewState == ETimeState::Stopped && OldState != ETimeState::Stopped)
	{
		PlaySound(EGameSound::TimeStop, TEXT("Stop"));
	}
}

void UGameAudioSubsystem::HandleStopExited(EStopExitReason Reason, float StoppedSeconds)
{
	bool bJump = IsJumpExit(Reason);
	bool bGraze = false;
	if (Reason == EStopExitReason::Resumed)
	{
		UWorld* World = CurrentWorld();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			// The same single measurement the home state reads: records this Stop's exit if the entity has not yet.
			It->RecordStopExit(Reason);
			bGraze = It->WasLastExitGraze();
			break;
		}
		bJump = bGraze;
	}
	PlaySound(bJump ? EGameSound::Slip : EGameSound::TimeResume, FString::Printf(TEXT("%s%s"), LexToString(Reason), bGraze ? TEXT(", graze") : TEXT("")));
}

void UGameAudioSubsystem::HandleMotionStarted(AActor* Mover, bool bOpening, bool bTimed)
{
	if (!Mover)
	{
		return;
	}
	// A motion that reverses before it ends: its sound fades out over MotionFadeOutSeconds, the next one starts.
	if (HeldMovers.Contains(Mover))
	{
		ReleaseMotionSound(Mover, UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds(), TEXT("motion reverses"));
	}
	const bool bDoor = Mover->IsA<ADoorDevice>();
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	const float Pitch = bDoor ? 1.f : Audio.GetHatchPitch();
	const FString Source = FString::Printf(TEXT("%s %s"), *Mover->GetName(), bOpening ? TEXT("opens") : TEXT("closes"));
	if (!bTimed)
	{
		// At once: the door's short sound, played once, as any world sound.
		PlaySoundAt(EGameSound::Door, Mover->GetActorLocation(), Pitch, Source);
		return;
	}
	// Over time: the motion's sound, a file made to loop, asked for once and played once, looping, from now to the end of
	// the motion; faded in over MotionFadeInSeconds, never asked for again while the motion lasts.
	FGameSoundRequest& Request = AddRequest(EGameSound::Motion, Source);
	const FVector Location = Mover->GetActorLocation();
	Request.bInWorld = true;
	Request.Location = Location;
	Request.Pitch = Pitch;
	UAudioComponent* Component = CreateSoundComponent(EGameSound::Motion, &Location, Pitch, Request);
	HeldMovers.Add(Mover);
	MotionPlays.Add(Mover, Component ? 1 : 0);
	MotionSounds.Add(Mover, Component);
	if (Component)
	{
		HeldComponents.Add(Component);
		Component->OnAudioFinishedNative.AddUObject(this, &UGameAudioSubsystem::HandleMotionSoundFinished);
		const float FadeIn = Audio.GetMotionFadeInSeconds();
		if (FadeIn > 0.f)
		{
			Component->FadeIn(FadeIn, 1.f);
		}
		else
		{
			Component->Play();
		}
	}
}

void UGameAudioSubsystem::HandleMotionEnded(AActor* Mover)
{
	if (Mover && HeldMovers.Contains(Mover))
	{
		ReleaseMotionSound(Mover, UTheLastShiftSettings::Get()->Audio.GetMotionFadeOutSeconds(), TEXT("motion ends"));
	}
}

void UGameAudioSubsystem::HandleMotionSoundFinished(UAudioComponent* Component)
{
	for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UAudioComponent>>& Pair : MotionSounds)
	{
		if (Pair.Value.Get() == Component && HeldMovers.Contains(Pair.Key))
		{
			// The motion's sound loops: it is never played again. It ending before its motion is a sound wave that does not
			// loop (the slot's file must be imported with Looping ticked).
			UE_LOG(LogGameAudio, Warning, TEXT("Audio: the motion's sound of %s ended before its motion: its sound wave does not loop."), *GetNameSafe(Pair.Key.Get()));
			break;
		}
	}
	HeldComponents.Remove(Component);
}

void UGameAudioSubsystem::ReleaseMotionSound(AActor* Mover, float FadeSeconds, const TCHAR* Why)
{
	HeldMovers.Remove(Mover);
	UAudioComponent* Component = MotionSounds.FindRef(Mover).Get();
	FGameSoundRequest& Request = AddRequest(EGameSound::Motion, FString::Printf(TEXT("%s %s (played %d)"), *GetNameSafe(Mover), Why, MotionPlays.FindRef(Mover)));
	Request.bStop = true;
	Request.bInWorld = true;
	Request.Location = Mover ? Mover->GetActorLocation() : FVector::ZeroVector;
	Request.Pitch = Mover && Mover->IsA<ADoorDevice>() ? 1.f : UTheLastShiftSettings::Get()->Audio.GetHatchPitch();
	Request.bHadSound = Component != nullptr;
	if (!Component)
	{
		return;
	}
	// Destroyed once it has stopped; the finished callback no longer plays it again.
	Component->bAutoDestroy = true;
	if (FadeSeconds > 0.f && Component->IsPlaying())
	{
		Component->FadeOut(FadeSeconds, 0.f);
	}
	else
	{
		Component->Stop();
	}
}

UAudioComponent* UGameAudioSubsystem::GetMotionSound(const AActor* Mover) const
{
	return MotionSounds.FindRef(Mover).Get();
}

bool UGameAudioSubsystem::IsMotionSoundPlaying(const AActor* Mover) const
{
	const UAudioComponent* Component = GetMotionSound(Mover);
	return Component && Component->IsPlaying();
}

void UGameAudioSubsystem::HandleAnyTerminalActivated(APowerTerminal* Terminal, int32 Cost)
{
	if (Terminal && IsOurWorld(Terminal->GetWorld()))
	{
		PlaySoundAt(EGameSound::Terminal, Terminal->GetActorLocation(), 1.f, FString::Printf(TEXT("%s paid %d"), *Terminal->GetName(), Cost));
	}
}

void UGameAudioSubsystem::HandleLogRead(ALogTerminal* Terminal)
{
	if (Terminal)
	{
		PlaySoundAt(EGameSound::Terminal, Terminal->GetActorLocation(), 1.f, FString::Printf(TEXT("%s read"), *Terminal->GetName()));
	}
}

void UGameAudioSubsystem::HandleResidueCollected()
{
	PlaySound(EGameSound::Energy, TEXT("residue collected"));
}

void UGameAudioSubsystem::HandleTypingStarted(FName RowName, ETextVoice Voice, bool bRestart)
{
	PlaySound(EGameSound::Line, FString::Printf(TEXT("channel %s %s%s"), GameAudio::VoiceName(Voice), *RowName.ToString(), bRestart ? TEXT(" restart") : TEXT("")));
}

void UGameAudioSubsystem::HandleSceneLineStarted(FName Scene, int32 Index, ESceneVoice Voice, FName Cue)
{
	// The rule of the interruption is RunScenes' (the static at the cut follows the same one).
	const bool bWhite = RunScenes::IsOnWhite(Voice);
	if (Index == 0 || Scene != SceneName)
	{
		// A new scene: nothing before its first line.
		SceneName = Scene;
		bSceneInterrupted = false;
	}
	else if (bSceneLastWhite && !bWhite && !bSceneInterrupted)
	{
		bSceneInterrupted = true;
		// Brief 35: held with the static the line's screen shows at this cut (RunScenes::IsInterruptionLine decides both).
		const URunSceneSubsystem* Scenes = GetGameInstance()->GetSubsystem<URunSceneSubsystem>();
		HoldInterruption(Scenes ? Scenes->GetLineScreen() : nullptr, Index, FString::Printf(TEXT("scene %s line %d"), *Scene.ToString(), Index + 1));
	}
	bSceneLastWhite = bWhite;
	PlaySound(EGameSound::Line, FString::Printf(TEXT("scene %s line %d %s"), *Scene.ToString(), Index + 1, LexToString(Voice)));
}

void UGameAudioSubsystem::HandleSceneScreenChanged(FName Scene, bool bOpen)
{
	if (!bOpen && bInterruptionHeld)
	{
		ReleaseInterruption(TEXT("the scene's screen closed"));
	}
	UpdateMusic(bOpen ? TEXT("a scene's screen opened") : TEXT("a scene's screen closed"));
}

void UGameAudioSubsystem::HandleFlowStateChanged(EStdFlowState NewState)
{
	if (NewState == EStdFlowState::FadingOut && IsMenuWorld(CurrentWorld()))
	{
		bLeavingMenu = true;
		UpdateMusic(TEXT("a travel leaves the main menu"));
	}
}

// --- Brief 35: the interruption's noise, held with the static ---

void UGameAudioSubsystem::HoldInterruption(UStdTextSceneWidget* Screen, int32 Index, const FString& Source)
{
	if (bInterruptionHeld)
	{
		ReleaseInterruption(TEXT("a new interruption"));
	}
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	FGameSoundRequest& Request = AddRequest(EGameSound::Interruption, Source);
	Request.Volume = Audio.GetVolume(EGameSound::Interruption);
	USoundBase* Asset = Audio.GetSound(EGameSound::Interruption).LoadSynchronous();
	Request.bHadSound = Asset != nullptr;
	UWorld* World = CurrentWorld();
	InterruptionPlays = 0;
	InterruptionShare = 0.f;
	UE_LOG(LogGameAudio, Verbose, TEXT("Audio: Interruption (%s)%s."), *Source, Asset ? TEXT("") : TEXT(", empty slot"));
	if (!Asset || !World)
	{
		return;
	}
	FAudioDevice::FCreateComponentParams Params(World);
	Params.bPlay = false;
	Params.bAutoDestroy = false;
	UAudioComponent* Component = FAudioDevice::CreateComponent(Asset, Params);
	if (!Component)
	{
		return;
	}
	Component->SoundClassOverride = GameAudio::SfxClass();
	Component->bAllowSpatialization = false;
	Component->bIsUISound = false;
	InterruptionVolume = Request.Volume;
	InterruptionShare = 1.f;
	Component->SetVolumeMultiplier(InterruptionVolume);
	Component->SetPitchMultiplier(1.f);
	Component->OnAudioFinishedNative.AddUObject(this, &UGameAudioSubsystem::HandleInterruptionFinished);
	InterruptionSound = Component;
	InterruptionScreen = Screen;
	InterruptionIndex = Index;
	InterruptionStartReal = FPlatformTime::Seconds();
	bInterruptionHeld = true;
	InterruptionPlays = 1;
	Component->Play();
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	UE_LOG(LogGameAudio, Log, TEXT("Audio: Interruption (%s) held with the static of line %d (%s; StaticSeconds %.2f, StaticFadeSeconds %.2f)."), *Source, Index + 1,
		Screen ? *Screen->GetName() : TEXT("no screen: the settings' times on real time"), Settings->GetStaticSeconds(), Settings->GetStaticFadeSeconds());
	InterruptionTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UGameAudioSubsystem::TickInterruption));
}

bool UGameAudioSubsystem::TickInterruption(float DeltaSeconds)
{
	if (!bInterruptionHeld)
	{
		InterruptionTicker.Reset();
		return false;
	}
	float Share = 0.f;
	const TCHAR* Why = nullptr;
	if (InterruptionScreen.IsValid())
	{
		// The static's own state on its screen: its strength (1 for StaticSeconds, then down to 0 over StaticFadeSeconds, on
		// the screen's clock), and its end with its line (the next line, a skip).
		UStdTextSceneWidget* Screen = InterruptionScreen.Get();
		if (!Screen->IsInViewport() && !Screen->GetParent())
		{
			Why = TEXT("the scene's screen is gone");
		}
		else if (Screen->GetCurrentIndex() != InterruptionIndex)
		{
			Why = TEXT("the static ended with its line");
		}
		else if (!Screen->IsEffectShown())
		{
			Why = TEXT("the static has faded out");
		}
		else
		{
			Share = FMath::Clamp(Screen->GetEffectOpacity(), 0.f, 1.f);
		}
	}
	else if (InterruptionScreen.IsStale())
	{
		Why = TEXT("the scene's screen is gone");
	}
	else
	{
		// No screen to follow (a scene shown some other way): the settings' two values, on real time.
		const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
		const double Elapsed = FPlatformTime::Seconds() - InterruptionStartReal;
		const double Hold = Settings->GetStaticSeconds();
		const double Fade = Settings->GetStaticFadeSeconds();
		Share = Elapsed <= Hold ? 1.f : (Fade > 0.0 ? static_cast<float>(FMath::Clamp(1.0 - (Elapsed - Hold) / Fade, 0.0, 1.0)) : 0.f);
	}
	if (!Why && Share <= 0.f)
	{
		Why = TEXT("the static has faded out");
	}
	if (Why)
	{
		ReleaseInterruption(Why);
		return false;
	}
	InterruptionShare = Share;
	if (UAudioComponent* Component = InterruptionSound.Get())
	{
		Component->SetVolumeMultiplier(InterruptionVolume * Share);
	}
	return true;
}

void UGameAudioSubsystem::HandleInterruptionFinished(UAudioComponent* Component)
{
	if (bInterruptionHeld && Component && Component == InterruptionSound)
	{
		// The interruption's noise loops: it is never played again. It ending before the static is a sound wave that does
		// not loop (the slot's file must be imported with Looping ticked).
		UE_LOG(LogGameAudio, Warning, TEXT("Audio: the interruption's noise ended before the static: its sound wave does not loop."));
	}
}

bool UGameAudioSubsystem::IsInterruptionPlaying() const
{
	return InterruptionSound && InterruptionSound->IsPlaying();
}

void UGameAudioSubsystem::ReleaseInterruption(const TCHAR* Why)
{
	if (!bInterruptionHeld)
	{
		return;
	}
	bInterruptionHeld = false;
	InterruptionShare = 0.f;
	if (InterruptionTicker.IsValid())
	{
		FTSTicker::RemoveTicker(InterruptionTicker);
		InterruptionTicker.Reset();
	}
	FGameSoundRequest& Request = AddRequest(EGameSound::Interruption, FString::Printf(TEXT("interruption: %s (played %d)"), Why, InterruptionPlays));
	Request.bStop = true;
	Request.bHadSound = InterruptionSound != nullptr;
	if (UAudioComponent* Component = InterruptionSound.Get())
	{
		Component->OnAudioFinishedNative.RemoveAll(this);
		Component->Stop();
		Component->DestroyComponent();
	}
	InterruptionSound = nullptr;
	InterruptionScreen.Reset();
	InterruptionIndex = INDEX_NONE;
	UE_LOG(LogGameAudio, Log, TEXT("Audio: Interruption stopped: %s (played %d)."), Why, InterruptionPlays);
}

// --- Playing ---

FGameSoundRequest& UGameAudioSubsystem::AddRequest(EGameSound Sound, const FString& Source)
{
	if (Record.Num() >= MaxRecord)
	{
		const int32 Drop = MaxRecord / 4;
		Record.RemoveAt(0, Drop);
		RecordOffset += Drop;
	}
	FGameSoundRequest& Request = Record.AddDefaulted_GetRef();
	Request.Sound = Sound;
	Request.Source = Source;
	Request.Map = GameAudio::MapOf(CurrentWorld());
	Request.RealSeconds = FPlatformTime::Seconds();
	return Request;
}

void UGameAudioSubsystem::PlaySound(EGameSound Sound, const FString& Source)
{
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	FGameSoundRequest& Request = AddRequest(Sound, Source);
	Request.Volume = Audio.GetVolume(Sound);
	USoundBase* Asset = Audio.GetSound(Sound).LoadSynchronous();
	Request.bHadSound = Asset != nullptr;
	UWorld* World = CurrentWorld();
	UE_LOG(LogGameAudio, Verbose, TEXT("Audio: %s (%s)%s."), LexToString(Sound), *Source, Asset ? TEXT("") : TEXT(", empty slot"));
	if (!Asset || !World)
	{
		return;
	}
	FAudioDevice::FCreateComponentParams Params(World);
	Params.bPlay = false;
	Params.bAutoDestroy = true;
	if (UAudioComponent* Component = FAudioDevice::CreateComponent(Asset, Params))
	{
		Component->SoundClassOverride = GameAudio::SfxClass();
		Component->bAllowSpatialization = false;
		Component->bIsUISound = false;
		Component->SetVolumeMultiplier(Request.Volume);
		Component->SetPitchMultiplier(1.f);
		Component->Play();
	}
}

void UGameAudioSubsystem::PlaySoundAt(EGameSound Sound, FVector Location, float Pitch, const FString& Source)
{
	FGameSoundRequest& Request = AddRequest(Sound, Source);
	Request.bInWorld = true;
	Request.Location = Location;
	Request.Pitch = Pitch;
	if (UAudioComponent* Component = CreateSoundComponent(Sound, &Location, Pitch, Request))
	{
		Component->bAutoDestroy = true;
		Component->Play();
	}
}

UAudioComponent* UGameAudioSubsystem::CreateSoundComponent(EGameSound Sound, const FVector* InLocation, float Pitch, FGameSoundRequest& Request)
{
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	Request.Volume = Audio.GetVolume(Sound);
	USoundBase* Asset = Audio.GetSound(Sound).LoadSynchronous();
	Request.bHadSound = Asset != nullptr;
	UWorld* World = CurrentWorld();
	UE_LOG(LogGameAudio, Verbose, TEXT("Audio: %s at %s, pitch %.2f (%s)%s."), LexToString(Sound), InLocation ? *InLocation->ToCompactString() : TEXT("-"), Pitch,
		*Request.Source, Asset ? TEXT("") : TEXT(", empty slot"));
	if (!Asset || !World || !InLocation)
	{
		return nullptr;
	}
	const FVector Location = *InLocation;
	FAudioDevice::FCreateComponentParams Params(World);
	Params.SetLocation(Location);
	Params.bPlay = false;
	Params.bAutoDestroy = false;
	UAudioComponent* Component = FAudioDevice::CreateComponent(Asset, Params);
	if (Component)
	{
		FSoundAttenuationSettings Attenuation;
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = true;
		Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation.AttenuationShapeExtents = FVector::ZeroVector;
		Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		Attenuation.FalloffDistance = Audio.GetWorldFalloffCm();
		Component->bOverrideAttenuation = true;
		Component->AttenuationOverrides = Attenuation;
		Component->bAllowSpatialization = true;
		Component->SoundClassOverride = GameAudio::SfxClass();
		Component->bIsUISound = false;
		Component->SetWorldLocation(Location);
		Component->SetVolumeMultiplier(Request.Volume);
		Component->SetPitchMultiplier(Pitch);
	}
	return Component;
}

void UGameAudioSubsystem::UpdateMusic(const TCHAR* Why)
{
	UWorld* World = CurrentWorld();
	const URunSceneSubsystem* Scenes = GetGameInstance()->GetSubsystem<URunSceneSubsystem>();
	const bool bWant = IsMenuWorld(World) && !bLeavingMenu && !(Scenes && (Scenes->IsSceneScreenOpen() || Scenes->IsEndingPending()));
	if (bWant == bMusicOn)
	{
		return;
	}
	bMusicOn = bWant;
	const FGameAudio& Audio = UTheLastShiftSettings::Get()->Audio;
	FGameSoundRequest& Request = AddRequest(EGameSound::MenuMusic, Why);
	Request.bStop = !bWant;
	// The music has no volume of its own: the file's level, under StdCore's Music volume.
	Request.Volume = 1.f;
	UStdMusicSubsystem* Music = GetGameInstance()->GetSubsystem<UStdMusicSubsystem>();
	UE_LOG(LogGameAudio, Log, TEXT("Audio: menu music %s (%s)."), bWant ? TEXT("on") : TEXT("off"), Why);
	if (!Music)
	{
		return;
	}
	if (!bWant)
	{
		Request.bHadSound = Music->GetCurrentMusic() != nullptr;
		Music->StopMusic(Audio.GetMusicFadeOutSeconds());
		return;
	}
	USoundBase* Asset = Audio.MenuMusic.LoadSynchronous();
	Request.bHadSound = Asset != nullptr;
	if (Asset)
	{
		Music->PlayMusic(Asset, Audio.GetMusicFadeInSeconds());
	}
}

// --- Record ---

void UGameAudioSubsystem::ClearRecord()
{
	RecordOffset += Record.Num();
	Record.Reset();
}

const FGameSoundRequest* UGameAudioSubsystem::GetRequest(int32 Index) const
{
	const int32 Local = Index - RecordOffset;
	return Record.IsValidIndex(Local) ? &Record[Local] : nullptr;
}

int32 UGameAudioSubsystem::CountRequests(EGameSound Sound, int32 From) const
{
	int32 Count = 0;
	for (int32 I = FMath::Max(From - RecordOffset, 0); I < Record.Num(); ++I)
	{
		Count += (Record[I].Sound == Sound && !Record[I].bStop) ? 1 : 0;
	}
	return Count;
}
