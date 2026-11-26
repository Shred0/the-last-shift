// SPDX-License-Identifier: MIT

#include "Debug/TestLidarCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Energy/PowerTerminal.h"
#include "Epilogue/ChoiceTerminal.h"
#include "LidarScanComponent.h"
#include "Residue/Residue.h"
#include "Residue/ResidueCollectCueWidget.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanCueWidget.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "UI/FlashlightHintWidget.h"
#include "UI/InteractPromptWidget.h"
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
	// Its intensity, falloff, radius and cones have one owner, UTheLastShiftSettings::Flashlight (ApplyFlashlightTuning).

	LidarScan = CreateDefaultSubobject<ULidarScanComponent>(TEXT("LidarScan"));
	LidarScan->SetupAttachment(Camera);

	ScanDirector = CreateDefaultSubobject<UScanDirectorComponent>(TEXT("ScanDirector"));
	TextChannel = CreateDefaultSubobject<UTextChannelComponent>(TEXT("TextChannel"));
	HomeAI = CreateDefaultSubobject<UHomeAIComponent>(TEXT("HomeAI"));

	CollectCueClass = UResidueCollectCueWidget::StaticClass();
	InteractPromptClass = UInteractPromptWidget::StaticClass();
	FlashlightHintClass = UFlashlightHintWidget::StaticClass();
	TextChannelWidgetClass = UTextChannelWidget::StaticClass();
}

void ATestLidarCharacter::BeginPlay()
{
	Super::BeginPlay();
	// The map decides whether the flashlight works and how it starts (the prologue's maps, in the settings); elsewhere
	// it works and starts as bFlashlightOnAtStart says.
	ApplyFlashlightTuning();
	bool bOnAtStart = bFlashlightOnAtStart;
	// The battery's chamber is home's: what the pawn can do there is what it can do in the home map.
	const int32 FlashlightStage = URunSubsystem::IsChamberOf(GetWorld()) ? 1 : URunSubsystem::GetPrologueStageOf(GetWorld());
	bFlashlightAvailable = UTheLastShiftSettings::Get()->GetMapFlashlight(FlashlightStage, bOnAtStart);
	Flashlight->SetVisibility(bFlashlightAvailable && bOnAtStart);
	UE_LOG(LogTheLastShift, Log, TEXT("TestLidarCharacter: flashlight %s, %s."), bFlashlightAvailable ? TEXT("available") : TEXT("unavailable in this map"),
		Flashlight->IsVisible() ? TEXT("on") : TEXT("off"));

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
	if (CollectCue)
	{
		CollectCue->SetResidue(nullptr);
		CollectCue->RemoveFromParent();
		CollectCue = nullptr;
	}
	if (InteractPrompt)
	{
		InteractPrompt->RemoveFromParent();
		InteractPrompt = nullptr;
	}
	if (FlashlightHint)
	{
		FlashlightHint->RemoveFromParent();
		FlashlightHint = nullptr;
	}
	if (TextChannelWidget)
	{
		TextChannelWidget->SetChannel(nullptr);
		TextChannelWidget->RemoveFromParent();
		TextChannelWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ATestLidarCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
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

	if (!CollectCue && CollectCueClass)
	{
		CollectCue = CreateWidget<UResidueCollectCueWidget>(PC, CollectCueClass);
		if (CollectCue)
		{
			CollectCue->AddToViewport(0);
		}
	}

	if (!InteractPrompt && InteractPromptClass)
	{
		InteractPrompt = CreateWidget<UInteractPromptWidget>(PC, InteractPromptClass);
		if (InteractPrompt)
		{
			InteractPrompt->SetInteractAction(InteractAction);
			InteractPrompt->AddToViewport(0);
		}
	}

	if (!FlashlightHint && FlashlightHintClass)
	{
		FlashlightHint = CreateWidget<UFlashlightHintWidget>(PC, FlashlightHintClass);
		if (FlashlightHint)
		{
			FlashlightHint->SetFlashlightAction(FlashlightAction);
			FlashlightHint->AddToViewport(0);
		}
	}

	if (!TextChannelWidget && TextChannelWidgetClass)
	{
		TextChannelWidget = CreateWidget<UTextChannelWidget>(PC, TextChannelWidgetClass);
		if (TextChannelWidget)
		{
			TextChannelWidget->SetChannel(TextChannel);
			TextChannelWidget->AddToViewport(0);
		}
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
	if (InteractAction)
	{
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ATestLidarCharacter::OnInteractPressed);
		Input->BindAction(InteractAction, ETriggerEvent::Completed, this, &ATestLidarCharacter::OnInteractReleased);
		Input->BindAction(InteractAction, ETriggerEvent::Canceled, this, &ATestLidarCharacter::OnInteractReleased);
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

bool ATestLidarCharacter::IsRunFrozen() const
{
	const UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	return TimeState && TimeState->IsRunFrozen();
}

void ATestLidarCharacter::OnScan()
{
	// The director is the only path to the lidar plugin; it ignores requests during the cooldown and in Normal time.
	ScanDirector->RequestScan();
}

void ATestLidarCharacter::ApplyFlashlightTuning()
{
	if (!Flashlight)
	{
		return;
	}
	const FFlashlightTuning F = UTheLastShiftSettings::Get()->GetFlashlightTuning();
	Flashlight->bUseInverseSquaredFalloff = F.bInverseSquaredFalloff;
	Flashlight->SetIntensityUnits(F.bInverseSquaredFalloff ? ELightUnits::Lumens : ELightUnits::Unitless);
	Flashlight->SetIntensity(F.Intensity);
	Flashlight->SetLightFalloffExponent(F.FalloffExponent);
	Flashlight->SetAttenuationRadius(F.AttenuationRadius);
	Flashlight->SetInnerConeAngle(F.InnerConeAngle);
	Flashlight->SetOuterConeAngle(F.OuterConeAngle);
	Flashlight->MarkRenderStateDirty();
}

bool ATestLidarCharacter::IsFlashlightOn() const
{
	return Flashlight && Flashlight->IsVisible();
}

bool ATestLidarCharacter::ShowFlashlightHint()
{
	// The HUD may not exist yet on the very first frame of a map.
	CreateScanCue();
	if (!FlashlightHint)
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("TestLidarCharacter: no flashlight hint to show."));
		return false;
	}
	FlashlightHint->Activate();
	return true;
}

void ATestLidarCharacter::OnToggleFlashlight()
{
	// The flashlight is locked off while time is stopped and in a map without it, and stays as it is while the pause menu
	// freezes the run. The action stays mapped: its key just does nothing.
	const UTimeStateSubsystem* TimeState = GetWorld()->GetSubsystem<UTimeStateSubsystem>();
	if (!bFlashlightAvailable || (TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen())))
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

void ATestLidarCharacter::OnInteractPressed()
{
	// While Stopped, a residue in reach takes the hold; anything else goes to the nearest terminal in reach, which
	// refuses (and says so) while time is stopped.
	const UTimeStateSubsystem* TimeState = GetWorld()->GetSubsystem<UTimeStateSubsystem>();
	if (TimeState && TimeState->IsStopped())
	{
		if (AResidue* Residue = AResidue::FindCollectable(this); Residue && Residue->BeginCollect(this))
		{
			HeldResidue = Residue;
			if (CollectCue)
			{
				CollectCue->SetResidue(Residue);
			}
			return;
		}
	}
	APowerTerminal* Terminal = APowerTerminal::FindNearestInRange(this);
	// A place of the battery's chamber is held, not pressed; when it offers nothing, nothing happens.
	if (AChoiceTerminal* Choice = Cast<AChoiceTerminal>(Terminal))
	{
		if (Choice->BeginHold(this))
		{
			HeldChoice = Choice;
			if (CollectCue)
			{
				CollectCue->SetChoice(Choice);
			}
		}
		return;
	}
	if (Terminal)
	{
		Terminal->TryActivate(this);
	}
}

void ATestLidarCharacter::OnInteractReleased()
{
	// Opening the pause menu releases every held key: a hold in progress stands still and continues after the menu.
	if (IsRunFrozen())
	{
		return;
	}
	if (AResidue* Residue = HeldResidue.Get())
	{
		Residue->EndCollect();
	}
	HeldResidue.Reset();
	if (AChoiceTerminal* Choice = HeldChoice.Get())
	{
		Choice->EndHold();
	}
	HeldChoice.Reset();
	if (CollectCue)
	{
		CollectCue->SetResidue(nullptr);
		CollectCue->SetChoice(nullptr);
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
