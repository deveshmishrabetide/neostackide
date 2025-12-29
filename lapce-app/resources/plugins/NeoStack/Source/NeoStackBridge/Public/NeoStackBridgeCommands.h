// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeoStackBridgeProtocol.h"

/**
 * Command handler for IDE commands
 * Executes actions in the Unreal Editor based on IDE requests
 */
class NEOSTACKBRIDGE_API FNeoStackBridgeCommands
{
public:
	/** Process incoming command and return response */
	static FNeoStackEvent ProcessCommand(const FNeoStackCommand& Command);

private:
	/** Open a Blueprint asset in the editor */
	static FNeoStackEvent HandleOpenBlueprint(const TSharedPtr<FJsonObject>& Args);

	/** Open any asset in appropriate editor */
	static FNeoStackEvent HandleOpenAsset(const TSharedPtr<FJsonObject>& Args);

	/** Navigate to a file and line in the code editor */
	static FNeoStackEvent HandleNavigateToFile(const TSharedPtr<FJsonObject>& Args);

	/** Trigger hot reload */
	static FNeoStackEvent HandleHotReload(const TSharedPtr<FJsonObject>& Args);

	/** Start Play in Editor */
	static FNeoStackEvent HandlePlayInEditor(const TSharedPtr<FJsonObject>& Args);

	/** Stop Play in Editor */
	static FNeoStackEvent HandleStopPIE(const TSharedPtr<FJsonObject>& Args);

	/** Execute arbitrary console command */
	static FNeoStackEvent HandleExecuteCommand(const TSharedPtr<FJsonObject>& Args);

	/** Execute a tool via the tool registry */
	static FNeoStackEvent HandleExecuteTool(const TSharedPtr<FJsonObject>& Args);

	/** Start PixelStreaming2 and return stream URL */
	static FNeoStackEvent HandleStartStreaming(const TSharedPtr<FJsonObject>& Args);

	/** Stop PixelStreaming2 */
	static FNeoStackEvent HandleStopStreaming(const TSharedPtr<FJsonObject>& Args);

	/** Get current stream info */
	static FNeoStackEvent HandleGetStreamInfo(const TSharedPtr<FJsonObject>& Args);

	/** Create success response */
	static FNeoStackEvent MakeSuccess(const FString& Event, TSharedPtr<FJsonObject> Data = nullptr);

	/** Create error response */
	static FNeoStackEvent MakeError(const FString& Event, const FString& ErrorMessage);
};
