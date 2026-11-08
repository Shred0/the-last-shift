// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TestLidarCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class ULidarScanComponent;
class USpotLightComponent;
struct FInputActionValue;

/**
 * Test harness pawn for the LidarScan spike (TestLidar map only).
 * First-person movement, toggleable flashlight, debug scan key. Input assets are assigned in BP_TestPawn.
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
	virtual void PawnClientRestart() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<USpotLightComponent> Flashlight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<ULidarScanComponent> LidarScan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bFlashlightOnAtStart = true;

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

private:
	void AddInputMapping() const;
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnScan();
	void OnToggleFlashlight();
};
