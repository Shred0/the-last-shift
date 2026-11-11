// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLidarScan, Log, All);

class FLidarScanModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Editor only: the cook hook that adds the component's assets to every cook. */
	FDelegateHandle ModifyCookHandle;
};
