// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FNeoStackBridgeModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the module instance */
	static FNeoStackBridgeModule& Get();

	/** Check if module is loaded */
	static bool IsAvailable();

	/** Get connection status */
	bool IsIDEConnected() const;

	/** Get the project identifier */
	FString GetProjectId() const;

private:
	/** Initialize the bridge (connects to IDE if -NeoStackIDE arg present) */
	void InitializeBridge();

	/** Shutdown the bridge connection */
	void ShutdownBridge();
};
