#include "UI/StdKeyBindingRowWidget.h"

#include "Components/InputKeySelector.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "UI/StdScreenWidget.h"
#include "UI/StdSettingsWidget.h"
#include "UI/StdUITheme.h"

void UStdKeyBindingRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (KeySelector)
	{
		KeySelector->SetAllowGamepadKeys(false);
		KeySelector->SetAllowModifierKeys(false);
		KeySelector->SetEscapeKeys({ EKeys::Escape, EKeys::Gamepad_FaceButton_Right });
		KeySelector->OnKeySelected.AddUniqueDynamic(this, &ThisClass::HandleKeySelected);
		KeySelector->OnIsSelectingKeyChanged.AddUniqueDynamic(this, &ThisClass::HandleIsSelectingKeyChanged);
	}
}

void UStdKeyBindingRowWidget::NativePreConstruct()
{
	if (bApplyTheme)
	{
		if (const UStdUITheme* Theme = StdUITheme::Get())
		{
			StdUITheme::ApplyToWidget(*Theme, *this);
		}
	}
	Super::NativePreConstruct();
}

void UStdKeyBindingRowWidget::NativeDestruct()
{
	// A row removed while capturing must not leave pause and back handling disabled.
	if (KeySelector && KeySelector->GetIsSelectingKey())
	{
		if (UStdInputSubsystem* InputSubsystem = GetInputSubsystem())
		{
			InputSubsystem->SetKeyCaptureActive(false);
		}
	}
	Super::NativeDestruct();
}

void UStdKeyBindingRowWidget::Setup(const FStdKeyBindingRow& InRow, UStdSettingsWidget* InOwner)
{
	Row = InRow;
	Owner = InOwner;

	if (Text_ActionName)
	{
		Text_ActionName->SetText(Row.DisplayName);
	}
	SetDisplayedKey(Row.CurrentKey);
}

void UStdKeyBindingRowWidget::SetDisplayedKey(const FKey& Key)
{
	Row.CurrentKey = Key;
	if (KeySelector)
	{
		KeySelector->SetSelectedKey(FInputChord(Key));
	}
}

UStdInputSubsystem* UStdKeyBindingRowWidget::GetInputSubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UStdInputSubsystem>() : nullptr;
}

void UStdKeyBindingRowWidget::HandleKeySelected(FInputChord SelectedKey)
{
	if (SelectedKey.Key == Row.CurrentKey || UStdScreenWidget::IsBackKey(SelectedKey.Key))
	{
		return;
	}

	if (UStdSettingsWidget* SettingsWidget = Owner.Get())
	{
		SettingsWidget->HandleRowKeySelected(this, SelectedKey.Key);
	}
}

void UStdKeyBindingRowWidget::HandleIsSelectingKeyChanged()
{
	const bool bSelecting = KeySelector && KeySelector->GetIsSelectingKey();
	if (UStdInputSubsystem* InputSubsystem = GetInputSubsystem())
	{
		InputSubsystem->SetKeyCaptureActive(bSelecting);
	}

	// While it waits for a key the selector disables its own button, which then shows its disabled look: with a framed
	// theme, a frame and a label at a third of their level, which hardly read. While it waits it shows its focused look
	// instead (the accent fill, the label on it); when the wait ends the theme's disabled look is its disabled look again.
	const UStdUITheme* Theme = bApplyTheme ? StdUITheme::Get() : nullptr;
	if (KeySelector && Theme && Theme->IsFramed())
	{
		FButtonStyle Style = KeySelector->GetButtonStyle();
		if (bSelecting)
		{
			Style.SetDisabled(Style.Hovered);
			Style.SetDisabledForeground(Style.HoveredForeground);
		}
		else
		{
			const FButtonStyle ThemeStyle = Theme->MakeButtonStyle();
			Style.SetDisabled(ThemeStyle.Disabled);
			Style.SetDisabledForeground(ThemeStyle.DisabledForeground);
		}
		StdUITheme::SetKeySelectorStyle(KeySelector, Style);
	}
}
