// SPDX-License-Identifier: MIT

#include "Energy/LogTerminal.h"

#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Energy/EnergySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TwinWriting.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/LogDisplayWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LogTerminal)

namespace LogTerminalLayout
{
	// The body: 25 cm deep, 1 m wide, 1.8 m tall; the surface on its front face (+X), centred at 1.25 m.
	static const FVector BodyScale(0.25f, 1.0f, 1.8f);
	static constexpr float BodyHalfDepth = 12.5f;
	static constexpr float ScreenHeight = 125.f;
	/** World centimetres per widget pixel: 1000 x 720 px = 90 x 65 cm. */
	static constexpr float ScreenScale = 0.09f;
}

ALogTerminal::ALogTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	Mesh->SetRelativeScale3D(LogTerminalLayout::BodyScale);

	Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
	Screen->SetupAttachment(GetRootComponent());
	Screen->SetWidgetSpace(EWidgetSpace::World);
	Screen->SetWidgetClass(ULogDisplayWidget::StaticClass());
	Screen->SetDrawSize(SurfaceDrawSize);
	Screen->SetDrawAtDesiredSize(false);
	// Opaque: the dark backdrop is part of the surface, so the flashlight's spot cannot wash the writing out.
	Screen->SetBlendMode(EWidgetBlendMode::Opaque);
	Screen->SetTwoSided(false);
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Screen->SetCastShadow(false);
	// The widget is drawn in the component's YZ plane, its front towards the component's +X: out of the terminal's front.
	Screen->SetRelativeLocation(FVector(LogTerminalLayout::BodyHalfDepth + 0.5f, 0.f, LogTerminalLayout::ScreenHeight));
	Screen->SetRelativeRotation(FRotator(0.f, 0.f, 0.f));
	Screen->SetRelativeScale3D(FVector(LogTerminalLayout::ScreenScale));
	Screen->SetVisibility(false);
}

void ALogTerminal::BeginPlay()
{
	Super::BeginPlay();
	Screen->SetDrawSize(FVector2D(FMath::Clamp(SurfaceDrawSize.X, 100.0, 4000.0), FMath::Clamp(SurfaceDrawSize.Y, 100.0, 4000.0)));
	Screen->InitWidget();
	if (ULogDisplayWidget* Surface = GetSurfaceWidget())
	{
		Surface->SetDisplay(ELogDisplay::Surface);
	}
	Screen->SetVisibility(false);
}

void ALogTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Panel)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

ULogDisplayWidget* ALogTerminal::GetSurfaceWidget() const
{
	return Screen ? Cast<ULogDisplayWidget>(Screen->GetUserWidgetObject()) : nullptr;
}

ULogDisplayWidget* ALogTerminal::EnsurePanel()
{
	if (Panel)
	{
		return Panel;
	}
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	Panel = CreateWidget<ULogDisplayWidget>(PC, ULogDisplayWidget::StaticClass());
	if (Panel)
	{
		Panel->AddToViewport(1);
		Panel->SetDisplay(ELogDisplay::Panel);
		// Position first: setting it resets the viewport anchors to the top left. Top centre, PanelTopMargin down.
		Panel->SetPositionInViewport(FVector2D(0.f, FMath::Clamp(Panel->PanelTopMargin, 0.f, 600.f)), false);
		Panel->SetAnchorsInViewport(FAnchors(0.5f, 0.f));
		Panel->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
		Panel->SetEntry(FText::FromString(CurrentText));
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Panel;
}

bool ALogTerminal::TryActivate(APawn* InstigatorPawn)
{
	if (!bUnlocked)
	{
		return Super::TryActivate(InstigatorPawn);
	}
	const UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (!IsInRange(InstigatorPawn) || (TimeState && (TimeState->IsRunFrozen() || TimeState->IsStopped())) || Entries.IsEmpty())
	{
		// Reading needs Normal time, like any terminal; nothing is said while Stopped.
		return false;
	}
	ShowNextEntry();
	return true;
}

void ALogTerminal::HandlePaid(APawn* InstigatorPawn)
{
	UWorld* World = GetWorld();
	int32 Terminals = 0;
	int32 AlreadyUnlocked = 0;
	for (TActorIterator<ALogTerminal> It(World); World && It; ++It)
	{
		++Terminals;
		AlreadyUnlocked += (*It != this && It->IsUnlocked()) ? 1 : 0;
	}
	const TArray<int32> AllOrders = TwinWriting::GetLogOrdersNewestFirst(UTheLastShiftSettings::Get()->LoadLogsTable());
	GroupIndex = AlreadyUnlocked;
	Entries = TwinWriting::DealLogGroup(AllOrders, FMath::Max(Terminals, 1), GroupIndex);
	bUnlocked = true;
	CurrentIndex = 0;
	UE_LOG(LogEnergy, Log, TEXT("%s: unlocked, group %d of %d terminals: %d of %d entries (%s)."), *GetName(), GroupIndex, Terminals, Entries.Num(),
		AllOrders.Num(), *FString::JoinBy(Entries, TEXT(","), [](int32 Order) { return FString::FromInt(Order); }));
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->StoreLogEntries(Entries);
	}
	StartEntry();
}

void ALogTerminal::Relock()
{
	bUnlocked = false;
	bShown = false;
	GroupIndex = -1;
	Entries.Reset();
	CurrentIndex = 0;
	CurrentText.Reset();
	TypedSeconds = 0.0;
	LastPushedTranslated = -1;
	UpdateDisplays(false, false);
	UE_LOG(LogEnergy, Log, TEXT("%s: locked again."), *GetName());
}

void ALogTerminal::ShowNextEntry()
{
	if (Entries.IsEmpty())
	{
		return;
	}
	CurrentIndex = (CurrentIndex + 1) % Entries.Num();
	StartEntry();
}

void ALogTerminal::StartEntry()
{
	const int32 Order = GetCurrentOrder();
	CurrentText = TwinWriting::FindLogText(UTheLastShiftSettings::Get()->LoadLogsTable(), Order).ToString();
	TypedSeconds = 0.0;
	LastPushedTranslated = -1;
	const FText Entry = FText::FromString(CurrentText);
	if (ULogDisplayWidget* Surface = GetSurfaceWidget())
	{
		Surface->SetEntry(Entry);
	}
	if (Panel)
	{
		Panel->SetEntry(Entry);
	}
	UE_LOG(LogEnergy, Log, TEXT("%s: entry %d of %d (log %d, %d characters)."), *GetName(), CurrentIndex + 1, Entries.Num(), Order, CurrentText.Len());
}

int32 ALogTerminal::GetTranslatedCells() const
{
	const double Typed = TypedSeconds * UTheLastShiftSettings::Get()->GetLogCharsPerSecond();
	// The typewriter's tolerance: frame times summing to a whole second reach the last character.
	return FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(Typed + 1.e-4)), 0, CurrentText.Len());
}

void ALogTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	const bool bStopped = TimeState && TimeState->IsStopped();
	const bool bFrozen = TimeState && TimeState->IsRunFrozen();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const bool bInRange = PC && IsInRange(PC->GetPawn());
	const bool bPanelMode = UTheLastShiftSettings::Get()->LogDisplay == ELogDisplay::Panel;

	const bool bHasEntry = bUnlocked && !CurrentText.IsEmpty();
	const bool bShowSurface = bHasEntry && !bStopped && !bPanelMode;
	const bool bShowPanel = bHasEntry && !bStopped && bPanelMode && bInRange;
	bShown = bShowSurface || bShowPanel;
	// The world does not tick behind the pause menu; the check keeps the typing still even if it did.
	if (bShown && !bFrozen)
	{
		TypedSeconds += FMath::Max(DeltaSeconds, 0.f);
	}
	UpdateDisplays(bShowSurface, bShowPanel);
}

void ALogTerminal::UpdateDisplays(bool bShowSurface, bool bShowPanel)
{
	if (Screen->IsVisible() != bShowSurface)
	{
		Screen->SetVisibility(bShowSurface);
	}
	ULogDisplayWidget* PanelWidget = bShowPanel ? EnsurePanel() : Panel.Get();
	if (PanelWidget)
	{
		const ESlateVisibility Wanted = bShowPanel ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (PanelWidget->GetVisibility() != Wanted)
		{
			PanelWidget->SetVisibility(Wanted);
		}
	}

	const FLinearColor Color = UTheLastShiftSettings::GetTextColors().LogTranslation;
	const bool bColorChanged = !Color.Equals(LastPushedColor, 0.f);
	const int32 Translated = GetTranslatedCells();
	const bool bCountChanged = Translated != LastPushedTranslated;
	if (!bColorChanged && !bCountChanged)
	{
		return;
	}
	LastPushedColor = Color;
	LastPushedTranslated = Translated;
	for (ULogDisplayWidget* Widget : { GetSurfaceWidget(), Panel.Get() })
	{
		if (Widget)
		{
			if (bColorChanged)
			{
				Widget->ApplyColors();
			}
			Widget->SetTranslatedCells(Translated);
		}
	}
}
