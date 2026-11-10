// SPDX-License-Identifier: MIT

#include "Debug/TestLidarCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "LidarScanComponent.h"
#include "Scan/ScanCueWidget.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TestLidarCharacter)

ATestLidarCharacter::ATestLidarCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	bUseControllerRotationYaw = true;
	BaseEyeHeight = 64.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	Camera->bUsePawnControlRotation = true;

	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(Camera);
	Flashlight->SetRelativeLocation(FVector(10.f, 15.f, -10.f));
	Flashlight->SetIntensityUnits(ELightUnits::Lumens);
	Flashlight->SetIntensity(1500.f);
	Flashlight->SetAttenuationRadius(2500.f);
	Flashlight->SetInnerConeAngle(14.f);
	Flashlight->SetOuterConeAngle(26.f);

	LidarScan = CreateDefaultSubobject<ULidarScanComponent>(TEXT("LidarScan"));
	LidarScan->SetupAttachment(Camera);

	ScanDirector = CreateDefaultSubobject<UScanDirectorComponent>(TEXT("ScanDirector"));
}

void ATestLidarCharacter::BeginPlay()
{
	Super::BeginPlay();
	Flashlight->SetVisibility(bFlashlightOnAtStart);

	if (UTimeStateSubsystem* TimeState = GetWorld()->GetSubsystem<UTimeStateSubsystem>())
	{
		TimeState->OnTimeStateChanged.AddDynamic(this, &ATestLidarCharacter::HandleTimeStateChanged);
	}
}

void ATestLidarCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &ATestLidarCharacter::HandleTimeStateChanged);
	}
	if (ScanCue)
	{
		ScanCue->RemoveFromParent();
		ScanCue = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ATestLidarCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	AddInputMapping();
	CreateScanCue();
}

void ATestLidarCharacter::CreateScanCue()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (ScanCue || !ScanCueClass || !PC || !PC->IsLocalController())
	{
		return;
	}

	ScanCue = CreateWidget<UScanCueWidget>(PC, ScanCueClass);
	if (ScanCue)
	{
		ScanCue->SetScanDirector(ScanDirector);
		ScanCue->AddToViewport(0);
	}
}

void ATestLidarCharacter::AddInputMapping() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !InputMapping)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InputMapping, 0);
	}
}

void ATestLidarCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	if (MoveAction)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATestLidarCharacter::OnMove);
	}
	if (LookAction)
	{
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATestLidarCharacter::OnLook);
	}
	if (ScanAction)
	{
		Input->BindAction(ScanAction, ETriggerEvent::Started, this, &ATestLidarCharacter::OnScan);
	}
	if (FlashlightAction)
	{
		Input->BindAction(FlashlightAction, ETriggerEvent::Started, this, &ATestLidarCharacter::OnToggleFlashlight);
	}
	if (TimeStopAction)
	{
		Input->BindAction(TimeStopAction, ETriggerEvent::Started, this, &ATestLidarCharacter::OnToggleTimeStop);
	}
}

void ATestLidarCharacter::OnMove(const FInputActionValue& Value)
{
	// X = right, Y = forward.
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.Y);
	AddMovementInput(GetActorRightVector(), Axis.X);
}

void ATestLidarCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void ATestLidarCharacter::OnScan()
{
	// The director is the only path to the lidar plugin; it ignores requests during the cooldown and in Normal time.
	ScanDirector->RequestScan();
}

void ATestLidarCharacter::OnToggleFlashlight()
{
	// The flashlight is locked off while time is stopped.
	const UTimeStateSubsystem* TimeState = GetWorld()->GetSubsystem<UTimeStateSubsystem>();
	if (TimeState && TimeState->IsStopped())
	{
		return;
	}
	Flashlight->ToggleVisibility();
}

void ATestLidarCharacter::OnToggleTimeStop()
{
	if (UTimeStateSubsystem* TimeState = GetWorld()->GetSubsystem<UTimeStateSubsystem>())
	{
		TimeState->Toggle();
	}
}

void ATestLidarCharacter::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	if (NewState == ETimeState::Stopped)
	{
		bFlashlightWasOnBeforeStop = Flashlight->IsVisible();
		Flashlight->SetVisibility(false);
	}
	else
	{
		Flashlight->SetVisibility(bFlashlightWasOnBeforeStop);
	}
	UE_LOG(LogTheLastShift, Log, TEXT("TestLidarCharacter: flashlight %s."), Flashlight->IsVisible() ? TEXT("on") : TEXT("off"));
}
