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
	case EGameSound::Energy: return &Energy;
	case EGameSound::Interruption: return &Interruption;
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
	// A motion that reverses before it ends: its sound stops, the next one starts.
	if (HeldMovers.Contains(Mover))
	{
		ReleaseMotionSound(Mover, 0.f, TEXT("motion reverses"));
	}
	const bool bDoor = Mover->IsA<ADoorDevice>();
	const float Pitch = bDoor ? 1.f : UTheLastShiftSettings::Get()->Audio.GetHatchPitch();
	const FString Source = FString::Printf(TEXT("%s %s"), *Mover->GetName(), bOpening ? TEXT("opens") : TEXT("closes"));
	if (!bTimed)
	{
		// At once: played once, as any world sound.
		PlaySoundAt(EGameSound::Door, Mover->GetActorLocation(), Pitch, Source);
		return;
	}
	// Over time: held from now to the end of the motion, played again each time it finishes before that.
	FGameSoundRequest& Request = AddRequest(EGameSound::Door, Source);
	const FVector Location = Mover->GetActorLocation();
	Request.bInWorld = true;
	Request.Location = Location;
	Request.Pitch = Pitch;
	UAudioComponent* Component = CreateSoundComponent(EGameSound::Door, &Location, Pitch, Request);
	HeldMovers.Add(Mover);
	MotionPlays.Add(Mover, Component ? 1 : 0);
	MotionSounds.Add(Mover, Component);
	if (Component)
	{
		HeldComponents.Add(Component);
		Component->OnAudioFinishedNative.AddUObject(this, &UGameAudioSubsystem::HandleMotionSoundFinished);
		Component->Play();
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
			// Shorter than the motion: again until it ends.
			MotionPlays.FindOrAdd(Pair.Key) += 1;
			Component->Play();
			return;
		}
	}
	HeldComponents.Remove(Component);
}

void UGameAudioSubsystem::ReleaseMotionSound(AActor* Mover, float FadeSeconds, const TCHAR* Why)
{
	HeldMovers.Remove(Mover);
	UAudioComponent* Component = MotionSounds.FindRef(Mover).Get();
	FGameSoundRequest& Request = AddRequest(EGameSound::Door, FString::Printf(TEXT("%s %s (played %d)"), *GetNameSafe(Mover), Why, MotionPlays.FindRef(Mover)));
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
		PlaySound(EGameSound::Interruption, FString::Printf(TEXT("scene %s line %d"), *Scene.ToString(), Index + 1));
	}
	bSceneLastWhite = bWhite;
	PlaySound(EGameSound::Line, FString::Printf(TEXT("scene %s line %d %s"), *Scene.ToString(), Index + 1, LexToString(Voice)));
}

void UGameAudioSubsystem::HandleSceneScreenChanged(FName Scene, bool bOpen)
{
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
