// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Protocol constants for NeoStack IDE <-> UE Plugin communication
 *
 * Protocol v2: WebSocket-based communication
 * - IDE runs WebSocket server, UE connects as client
 * - UE launched with -NeoStackIDE=ws://localhost:{port} argument
 * - Handshake message sent on connect, session ID assigned
 */
namespace NeoStackProtocol
{
	/** Protocol version (v2 = WebSocket client mode) */
	constexpr int32 ProtocolVersion = 2;

	/** Legacy: UDP port for discovery broadcasts (deprecated in v2) */
	constexpr int32 DiscoveryPort = 27015;

	/** Legacy: Base WebSocket port (deprecated in v2) */
	constexpr int32 BaseWSPort = 27016;

	/** Legacy: Maximum port attempts (deprecated in v2) */
	constexpr int32 MaxPortAttempts = 10;

	/** Legacy: Discovery broadcast interval in seconds (deprecated in v2) */
	constexpr float BroadcastInterval = 2.0f;

	/** Message types */
	namespace MessageType
	{
		// Handshake (WebSocket v2)
		const FString Handshake = TEXT("handshake");
		const FString HandshakeAck = TEXT("handshake_ack");

		// Legacy: Discovery (UDP) - deprecated in v2
		const FString Presence = TEXT("neostack_presence");

		// Commands (WebSocket) - IDE -> Plugin
		const FString OpenBlueprint = TEXT("open_blueprint");
		const FString OpenAsset = TEXT("open_asset");
		const FString NavigateToFile = TEXT("navigate_to_file");
		const FString TriggerHotReload = TEXT("hot_reload");
		const FString PlayInEditor = TEXT("pie_start");
		const FString StopPIE = TEXT("pie_stop");
		const FString ExecuteCommand = TEXT("execute_command");
		const FString ExecuteTool = TEXT("execute_tool");
		const FString StartStreaming = TEXT("start_streaming");
		const FString StopStreaming = TEXT("stop_streaming");
		const FString GetStreamInfo = TEXT("get_stream_info");

		// Blueprint queries - IDE -> Plugin
		const FString FindDerivedBlueprints = TEXT("find_derived_blueprints");
		const FString FindBlueprintReferences = TEXT("find_blueprint_references");
		const FString GetBlueprintPropertyOverrides = TEXT("get_blueprint_property_overrides");
		const FString FindBlueprintFunctionUsages = TEXT("find_blueprint_function_usages");
		const FString GetPropertyOverridesAcrossBlueprints = TEXT("get_property_overrides_across_blueprints");
		const FString GetBlueprintHintsBatch = TEXT("get_blueprint_hints_batch");

		// Events (WebSocket) - Plugin -> IDE
		const FString Connected = TEXT("connected");
		const FString Disconnected = TEXT("disconnected");
		const FString LogMessage = TEXT("log_message");
		const FString CompileStarted = TEXT("compile_started");
		const FString CompileFinished = TEXT("compile_finished");
		const FString PIEStarted = TEXT("pie_started");
		const FString PIEStopped = TEXT("pie_stopped");
		const FString AssetCreated = TEXT("asset_created");
		const FString AssetModified = TEXT("asset_modified");
	}
}

/**
 * Discovery broadcast message
 * Sent via UDP to announce UE Editor presence
 */
struct FNeoStackPresenceMessage
{
	/** Protocol version */
	int32 Version;

	/** Message type */
	FString Type;

	/** Unique project identifier (hash of project path) */
	FString ProjectId;

	/** Full path to .uproject file */
	FString ProjectPath;

	/** Project name */
	FString ProjectName;

	/** WebSocket port for connection */
	int32 WSPort;

	/** Unreal Engine version */
	FString EngineVersion;

	/** Process ID */
	int32 ProcessId;

	/** PixelStreaming2 stream URL (empty if not available) */
	FString StreamUrl;

	/** Whether PixelStreaming2 is currently active */
	bool bIsStreaming = false;

	/** NeoStack connection ID from -NeoStackConn command line arg (for auto-connect) */
	FString NeoStackConn;

	/** Convert to JSON string */
	FString ToJson() const;

	/** Parse from JSON string */
	static bool FromJson(const FString& JsonString, FNeoStackPresenceMessage& OutMessage);
};

/**
 * Base command message structure
 */
struct FNeoStackCommand
{
	/** Command type */
	FString Command;

	/** Command arguments as JSON object */
	TSharedPtr<FJsonObject> Args;

	/** Unique request ID for response matching */
	FString RequestId;

	/** Parse from JSON string */
	static bool FromJson(const FString& JsonString, FNeoStackCommand& OutCommand);
};

/**
 * Base response/event message structure
 */
struct FNeoStackEvent
{
	/** Event type */
	FString Event;

	/** Event data as JSON object */
	TSharedPtr<FJsonObject> Data;

	/** Request ID if this is a response */
	FString RequestId;

	/** Success flag */
	bool bSuccess;

	/** Error message if failed */
	FString Error;

	/** Convert to JSON string */
	FString ToJson() const;
};
