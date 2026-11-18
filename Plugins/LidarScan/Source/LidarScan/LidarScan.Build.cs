// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

using UnrealBuildTool;

public class LidarScan : ModuleRules
{
	public LidarScan(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Niagara"
		});

		// Asynchronous GPU readback for the neighbour search (FRHIGPUTextureReadback, render commands).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI"
		});
	}
}
