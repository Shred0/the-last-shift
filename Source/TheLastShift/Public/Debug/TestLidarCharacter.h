// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Time/TimeState.h"
#include "TestLidarCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class AResidue;
class ULidarScanComponent;
class UResidueCollectCueWidget;
class UInteractPromptWidget;
class UScanCueWidget;
class UScanDirectorComponent;
class UHomeAIComponent;
class UTextChannelComponent;
class UTextChannelWidget;
class USpotLightComponent;
struct FInputActionValue;

/**
 * Test harness pawn for the LidarScan spike (TestLidar map only).
 * First-person movement, toggleable flashlight, time stop toggle, scan requests through the scan director, the
 * scan-ready cue, and Interact: held near an Available residue while Stopped it collects (with the collect cue),
 * pressed near a power terminal it activates it; the Interact prompt shows its key where it applies. Input assets and
 * the cue widget class are assigned in BP_TestPawn. Carries the home AI's text channel (TextChannel, driven only by
 * HomeAI) and shows it with the text channel widget.
 */
UCLASS(Abstract)
class THELASTSHIFT_API ATestLidarCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ATestLidarCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PawnClientRestart() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<USpotLightComponent> Flashlight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<ULidarScanComponent> LidarScan;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UScanDirectorComponent> ScanDirector;

	/** The home AI's message queue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UTextChannelComponent> TextChannel;

	/** The one place that pushes to TextChannel and suspends it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UHomeAIComponent> HomeAI;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bFlashlightOnAtStart = true;

	/** Scan-ready cue created for the local player (WBP_ScanCue). None = no cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test|UI")
	TSubclassOf<UScanCueWidget> ScanCueClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Test|UI")
	TObjectPtr<UScanCueWidget> ScanCue;

	/** Hold-to-collect ring created for the local player. None = no cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test|UI")
	TSubclassOf<UResidueCollectCueWidget> CollectCueClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Test|UI")
	TObjectPtr<UResidueCollectCueWidget> CollectCue;

	/** Interact prompt (the key and what it does) created for the local player. None = no prompt. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test|UI")
	TSubclassOf<UInteractPromptWidget> InteractPromptClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Test|UI")
	TObjectPtr<UInteractPromptWidget> InteractPrompt;

	/** The home AI's text in a lower corner, created for the local player. None = not shown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test|UI")
	TSubclassOf<UTextChannelWidget> TextChannelWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Test|UI")
	TObjectPtr<UTextChannelWidget> TextChannelWidget;

	/**
	 * The mapping context of the actions below. StdCore adds it to the local player and registers it for rebinding
	 * (MappingContexts in Config/DefaultGame.ini), so its keys follow the Controls tab; the pawn never adds it itself.
	 * Read by tls.InputReport.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputMappingContext> InputMapping;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> ScanAction;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> FlashlightAction;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> TimeStopAction;

	UPROPERTY(EditDefaultsOnly, Category = "Test|Input")
	TObjectPtr<UInputAction> InteractAction;

private:
	void CreateScanCue();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnScan();
	void OnToggleFlashlight();
	void OnToggleTimeStop();
	void OnInteractPressed();
	void OnInteractReleased();

	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	/** Flashlight state at the moment time was stopped, restored on Resume. */
	bool bFlashlightWasOnBeforeStop = false;

	/** Residue being held for, between Interact pressed and released. */
	TWeakObjectPtr<AResidue> HeldResidue;
};
