// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class TheLastShiftTarget : TargetRules
{
	public TheLastShiftTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("TheLastShift");
	}
}
