#include "Settings/StdUserSettings.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace StdUserSettingsDetail
{
	constexpr int32 MinFrameRate = 10;
	constexpr int32 MaxFrameRate = 1000;
	constexpr int32 MaxAntiAliasingMethod = 4;

	bool Hidden(EStdVideoOption Option)
	{
		return UStdProjectSettings::Get()->IsVideoOptionHidden(Option);
	}
}

int32 UStdUserSettings::GetFrameRateLimit() const
{
	using namespace StdUserSettingsDetail;
	const int32 Value = HasChosen(EStdVideoOption::FrameRateLimit) ? FrameRateLimit : UStdProjectSettings::Get()->DefaultFrameRateLimit;
	return FMath::Clamp(Value, MinFrameRate, MaxFrameRate);
}

bool UStdUserSettings::IsFrameRateUnlimited() const
{
	// The box belongs to the frame-rate row: the limit and the box are chosen, or hidden, together.
	if (StdUserSettingsDetail::Hidden(EStdVideoOption::FrameRateLimit) || FrameRateUnlimited < 0)
	{
		return UStdProjectSettings::Get()->bDefaultFrameRateUnlimited;
	}
	return FrameRateUnlimited != 0;
}

int32 UStdUserSettings::GetAntiAliasingMethod() const
{
	const int32 Value = HasChosen(EStdVideoOption::AntiAliasing) ? AntiAliasingMethod : GetDefaultAntiAliasingMethod();
	return FMath::Clamp(Value, 0, StdUserSettingsDetail::MaxAntiAliasingMethod);
}

bool UStdUserSettings::IsVSyncEnabled() const
{
	return HasChosen(EStdVideoOption::VSync) ? VSync != 0 : UStdProjectSettings::Get()->bDefaultVSync;
}

void UStdUserSettings::SetFrameRateLimit(int32 FramesPerSecond)
{
	using namespace StdUserSettingsDetail;
	FrameRateLimit = FMath::Clamp(FramesPerSecond, MinFrameRate, MaxFrameRate);
}

void UStdUserSettings::SetFrameRateUnlimited(bool bUnlimited)
{
	FrameRateUnlimited = bUnlimited ? 1 : 0;
}

void UStdUserSettings::SetAntiAliasingMethod(int32 Method)
{
	AntiAliasingMethod = FMath::Clamp(Method, 0, StdUserSettingsDetail::MaxAntiAliasingMethod);
}

void UStdUserSettings::SetVSyncEnabled(bool bEnabled)
{
	VSync = bEnabled ? 1 : 0;
}

bool UStdUserSettings::HasChosen(EStdVideoOption Option) const
{
	if (StdUserSettingsDetail::Hidden(Option))
	{
		return false;
	}
	switch (Option)
	{
	case EStdVideoOption::FrameRateLimit: return FrameRateLimit > 0;
	case EStdVideoOption::AntiAliasing: return AntiAliasingMethod >= 0;
	case EStdVideoOption::VSync: return VSync >= 0;
	}
	return false;
}

int32 UStdUserSettings::GetProjectAntiAliasingMethod()
{
	// The project's renderer settings, as written in its engine config; the console variable itself may already hold the
	// player's choice.
	int32 Method = INDEX_NONE;
	if (GConfig && GConfig->GetInt(TEXT("/Script/Engine.RendererSettings"), TEXT("r.AntiAliasingMethod"), Method, GEngineIni))
	{
		return FMath::Clamp(Method, 0, StdUserSettingsDetail::MaxAntiAliasingMethod);
	}
	// No line in the config: the engine's own default, which is the variable's value until something sets it.
	static const int32 EngineDefault = []()
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
		return Var ? Var->GetInt() : 0;
	}();
	return FMath::Clamp(EngineDefault, 0, StdUserSettingsDetail::MaxAntiAliasingMethod);
}

int32 UStdUserSettings::GetDefaultAntiAliasingMethod()
{
	const int32 Configured = UStdProjectSettings::Get()->DefaultAntiAliasingMethod;
	return Configured >= 0 ? FMath::Min(Configured, StdUserSettingsDetail::MaxAntiAliasingMethod) : GetProjectAntiAliasingMethod();
}

void UStdUserSettings::ResetToDefaults()
{
	MasterVolume = DefaultMasterVolume;
	MusicVolume = DefaultMusicVolume;
	SfxVolume = DefaultSfxVolume;
	FrameRateLimit = 0;
	FrameRateUnlimited = -1;
	AntiAliasingMethod = -1;
	VSync = -1;
}

void UStdUserSettings::Save()
{
	SaveConfig();
}
