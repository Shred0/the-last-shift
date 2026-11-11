// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Time/TimeState.h"
#include "HomeStateSubsystem.generated.h"

class UScanDirectorComponent;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogHome, Log, All);

/** What made the outside world jump forward. */
UENUM(BlueprintType)
enum class EHomeJumpCause : uint8
{
	/** The figure reached the player with response Jump or RewindAndJump: a full jump of the act. */
	Contact,
	/** A voluntary Resume with the figure manifested and within the entity's GrazeRadius: a fraction of the act's jump. */
	Graze
};

inline const TCHAR* LexToString(EHomeJumpCause Cause)
{
	return Cause == EHomeJumpCause::Contact ? TEXT("Contact") : TEXT("Graze");
}

/** One time jump at home, as applied. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FHomeJump
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Home")
	EHomeJumpCause Cause = EHomeJumpCause::Contact;

	/** Act whose tuning the jump used (0 = a map outside the run: the test tuning). */
	UPROPERTY(BlueprintReadOnly, Category = "Home")
	int32 ActIndex = 0;

	/** Length of the Stop that ended with the jump. */
	UPROPERTY(BlueprintReadOnly, Category = "Home", meta = (Units = "s"))
	float StoppedSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Home", meta = (Units = "s"))
	double HomeSecondsAdded = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Home")
	double BatteryBefore = 1.0;

	/** Never below 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Home")
	double BatteryAfter = 1.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHomeJump, const FHomeJump&, Jump);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunLost);

/**
 * Sole owner of the home state: time elapsed at home and the home battery. A game-instance subsystem, so both survive
 * map changes. The battery starts at 1 and only goes down; at 0 the run is lost (OnRunLost, once), and from then on
 * every Stop exit is ignored until a run state is restored (RestoreState / ResetRun, from the run subsystem).
 *
 * Follows the scan director of the current world exactly like UEnergySubsystem does (the director's global lifecycle
 * delegates) and listens to its OnStopExited:
 * - Jumped / RewoundAndJumped (a contact with response Jump or RewindAndJump): a full jump with the act's ActTuning.
 * - Resumed: asks the world's entity to record this Stop's exit (AEntity::RecordStopExit, idempotent) and reads
 *   AEntity::WasLastExitGraze(); a graze jumps by UTheLastShiftSettings::GrazeJumpFraction of the act's values.
 * - Rewound: nothing.
 * The act is the director's map's position in UTheLastShiftSettings::AreaMaps, tuning read from there and copied nowhere.
 */
UCLASS()
class THELASTSHIFT_API UHomeStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The home state subsystem of WorldContext's game instance, or null. */
	static UHomeStateSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Seconds elapsed at home since the run began. */
	UFUNCTION(BlueprintPure, Category = "Home", meta = (Units = "s"))
	double GetHomeSecondsElapsed() const { return HomeSecondsElapsed; }

	/** Home battery, 1 = full, 0 = the run is lost. */
	UFUNCTION(BlueprintPure, Category = "Home")
	double GetBatteryFraction() const { return BatteryFraction; }

	UFUNCTION(BlueprintPure, Category = "Home")
	bool IsRunLost() const { return bRunLost; }

	UFUNCTION(BlueprintPure, Category = "Home")
	int32 GetJumpCount() const { return JumpCount; }

	/**
	 * Applies a jump of Cause with Act's tuning (full for Contact, GrazeJumpFraction for Graze). Broadcasts OnHomeJump,
	 * then OnRunLost if the battery reaches 0. Ignored (false) once the run is lost or when Act has no tuning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Home")
	bool ApplyJump(EHomeJumpCause Cause, int32 Act, float StoppedSeconds);

	/** Sets the state of a restored run snapshot (battery clamped to [0, 1], home time to >= 0) and clears the loss. */
	UFUNCTION(BlueprintCallable, Category = "Home")
	void RestoreState(double Battery, double HomeSeconds);

	/** A new run: battery 1, home time 0, not lost. */
	UFUNCTION(BlueprintCallable, Category = "Home")
	void ResetRun() { RestoreState(1.0, 0.0); }

	/** True while bound to a scan director's OnStopExited. */
	UFUNCTION(BlueprintPure, Category = "Home")
	bool HasBoundDirector() const { return BoundDirector.IsValid(); }

	/** Every jump, after the state changed. */
	UPROPERTY(BlueprintAssignable, Category = "Home")
	FOnHomeJump OnHomeJump;

	/** Once, when a jump takes the battery to 0. */
	UPROPERTY(BlueprintAssignable, Category = "Home")
	FOnRunLost OnRunLost;

private:
	void HandleDirectorBeganPlay(UScanDirectorComponent* Director);
	void HandleDirectorEndingPlay(UScanDirectorComponent* Director);

	UFUNCTION()
	void HandleStopExited(EStopExitReason Reason, float StoppedSeconds);

	bool IsOurWorld(const UScanDirectorComponent* Director) const;
	void Bind(UScanDirectorComponent* Director);
	void Unbind();

	double HomeSecondsElapsed = 0.0;
	double BatteryFraction = 1.0;
	bool bRunLost = false;
	int32 JumpCount = 0;

	TWeakObjectPtr<UScanDirectorComponent> BoundDirector;
	FDelegateHandle BeganPlayHandle;
	FDelegateHandle EndingPlayHandle;
};
