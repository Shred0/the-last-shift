// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "MenuBackdropWidget.generated.h"

class UBorder;
class UStdScreenWidget;

/** One point of the menu's background, in picture units (the structure's radius, +Y up, the centre at 0). */
struct FMenuBackdropPoint
{
	FVector2f Position = FVector2f::ZeroVector;
	/** Its distance from the centre in space (the structure's points: 0 to 1). */
	float Distance = 0.f;
	/** The level drawn, times home's tint: a fading particle's share of being shown included. */
	float Level = 0.f;
	/** The particle's slot; INDEX_NONE for the structure's points. */
	int32 Slot = INDEX_NONE;
	/** A particle's share of being shown: 1, or the share of its fade. */
	float Weight = 1.f;
	/** The particle that comes back at the field's outer edge, fading in. */
	bool bArriving = false;
};

/** The particle of one slot at one time: the one shown with at least half its weight. */
struct FMenuBackdropParticle
{
	FVector3f Position = FVector3f::ZeroVector;
	float Distance = 0.f;
	float Level = 0.f;
	float Weight = 1.f;
	/** Its life: a new one each time the slot's particle comes back at the edge. */
	int32 Life = 0;
	/** Pulses whose wave has reached the slot. */
	int32 Hits = 0;
	/** When the latest wave reached it (-1: none yet). */
	double LastHitSeconds = -1.0;
	/** Its latest wave made it come back at the edge (a fade, not a step). */
	bool bCameBack = false;
};

/** The whole picture at one time. */
struct FMenuBackdropFrame
{
	double Seconds = 0.0;
	/** The structure's turn since time 0, unwrapped. */
	double SpinDegrees = 0.0;
	/** Pulses that have left the structure by Seconds (0 before the first). */
	int32 PulseIndex = 0;
	/** The latest pulse's start (-1 before the first). */
	double PulseStartSeconds = -1.0;
	float StructureLevel = 0.f;
	int32 StructurePoints = 0;
	/** Slots shown with at least half their weight. */
	int32 ShownParticles = 0;
	/** The particles first, then the structure's points (its six vertices first). */
	TArray<FMenuBackdropPoint> Points;
	TArray<FMenuBackdropParticle> Particles;
};

/** The picture as a function of its values and of time only: the widget paints it, the tests read it. */
namespace MenuBackdropPicture
{
	THELASTSHIFT_API void Evaluate(const FMenuBackdropTuning& Tuning, double Seconds, FMenuBackdropFrame& Out);

	/** When pulse PulseIndex (1 is the first) leaves the structure. */
	THELASTSHIFT_API double PulseStart(const FMenuBackdropTuning& Tuning, int32 PulseIndex);

	/** Pulses that have left the structure by Seconds. */
	THELASTSHIFT_API int32 PulsesBy(const FMenuBackdropTuning& Tuning, double Seconds);

	/** The structure's vertex local (1, 0, 0) at Spin degrees, in picture units (the tests read the turn back from it). */
	THELASTSHIFT_API FVector2f StructureVertex(const FMenuBackdropTuning& Tuning, double SpinDegrees);

	/** The turn, in degrees in (-180, 180], that puts the vertex local (1, 0, 0) at Vertex (picture units). */
	THELASTSHIFT_API double SpinFromVertex(const FMenuBackdropTuning& Tuning, const FVector2f& Vertex);
}

/**
 * The main menu's moving background (MenuBackdrop in UTheLastShiftSettings): the structure turning in the void and the
 * field of particles that step toward it at every pulse, points in home's tint on black. URunMainMenuWidget adds it to
 * the viewport under every screen (ViewportZOrder) when bMenuBackdrop is on, and removes it with itself. It shows behind
 * the main menu and the screens opened from it (settings, credits, the list of acts): while it draws, their full-screen
 * backdrop (Border_Background, the style's backdrop colour) is made see-through, and given back when it goes. It draws
 * nothing while the screen is covered by a text scene, the ending, the loss screen, any other screen not opened from the
 * menu, or StdCore's travel screen at full cover. Its clock is the world's real time since it was shown (a test can set
 * the time it shows); it reads nothing of the run or the save. Hit-test invisible; it holds no focus.
 */
UCLASS()
class THELASTSHIFT_API UMenuBackdropWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Under every screen of the main menu (StdCore shows the menu at 10). */
	static constexpr int32 ViewportZOrder = 0;

	/** The menu whose screens it shows behind. */
	void SetMenu(UStdScreenWidget* InMenu);

	/** The time the picture shows: seconds since it was shown, or the time a test set. */
	UFUNCTION(BlueprintPure, Category = "Run|Menu Backdrop", meta = (Units = "s"))
	double GetPictureSeconds() const;

	/** Tests: show the picture at Seconds until ClearTimeOverride. */
	void SetTimeOverride(double Seconds);
	void ClearTimeOverride();
	bool HasTimeOverride() const { return bTimeOverride; }

	/** Why it draws nothing now: "" when it draws, else what covers it ("scene", "ending", "loss screen", ...). */
	const FString& GetCoverReason() const { return CoverReason; }

	/** The last paint: its frame number (GFrameCounter), the points it drew, what covered it, the picture it drew. */
	uint64 GetLastPaintFrame() const { return LastPaintFrame; }
	int32 GetLastPaintedPoints() const { return LastPaintedPoints; }
	const FString& GetLastPaintCover() const { return LastPaintCover; }
	const FMenuBackdropFrame& GetLastFrame() const { return Frame; }
	int32 GetPaintCount() const { return PaintCount; }

	/** The screens' backdrops it holds see-through now. */
	int32 GetSeeThroughCount() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FString FindCover() const;
	void HoldScreensSeeThrough();
	void GiveScreensBack();

	TWeakObjectPtr<UStdScreenWidget> Menu;

	struct FSeeThrough
	{
		TWeakObjectPtr<UBorder> Border;
		FLinearColor Before = FLinearColor::White;
	};
	TArray<FSeeThrough> SeeThrough;

	double StartRealSeconds = -1.0;
	double OverrideSeconds = 0.0;
	bool bTimeOverride = false;
	FString CoverReason;

	mutable FMenuBackdropFrame Frame;
	mutable uint64 LastPaintFrame = 0;
	mutable int32 LastPaintedPoints = 0;
	mutable FString LastPaintCover;
	mutable int32 PaintCount = 0;
};
