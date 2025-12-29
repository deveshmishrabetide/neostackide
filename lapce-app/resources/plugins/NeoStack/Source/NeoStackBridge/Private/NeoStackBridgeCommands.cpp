// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridgeCommands.h"
#include "NeoStackBridgeProtocol.h"
#include "NeoStackBlueprintCommands.h"
#include "NeoStackBridgeDiscovery.h"
#include "Tools/NeoStackToolRegistry.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "SourceCodeNavigation.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IMainFrameModule.h"

#if WITH_EDITOR
#include "IPixelStreaming2EditorModule.h"
#endif

FNeoStackEvent FNeoStackBridgeCommands::ProcessCommand(const FNeoStackCommand& Command)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Processing command: %s"), *Command.Command);

	if (Command.Command == NeoStackProtocol::MessageType::OpenBlueprint)
	{
		return HandleOpenBlueprint(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::OpenAsset)
	{
		return HandleOpenAsset(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::NavigateToFile)
	{
		return HandleNavigateToFile(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::TriggerHotReload)
	{
		return HandleHotReload(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::PlayInEditor)
	{
		return HandlePlayInEditor(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::StopPIE)
	{
		return HandleStopPIE(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::ExecuteCommand)
	{
		return HandleExecuteCommand(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::ExecuteTool)
	{
		return HandleExecuteTool(Command.Args);
	}
	// Blueprint query commands
	else if (Command.Command == NeoStackProtocol::MessageType::FindDerivedBlueprints)
	{
		return FNeoStackBlueprintCommands::HandleFindDerivedBlueprints(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::FindBlueprintReferences)
	{
		return FNeoStackBlueprintCommands::HandleFindBlueprintReferences(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides)
	{
		return FNeoStackBlueprintCommands::HandleGetBlueprintPropertyOverrides(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::FindBlueprintFunctionUsages)
	{
		return FNeoStackBlueprintCommands::HandleFindBlueprintFunctionUsages(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints)
	{
		return FNeoStackBlueprintCommands::HandleGetPropertyOverridesAcrossBlueprints(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::GetBlueprintHintsBatch)
	{
		return FNeoStackBlueprintCommands::HandleGetBlueprintHintsBatch(Command.Args);
	}
	// Streaming commands
	else if (Command.Command == NeoStackProtocol::MessageType::StartStreaming)
	{
		return HandleStartStreaming(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::StopStreaming)
	{
		return HandleStopStreaming(Command.Args);
	}
	else if (Command.Command == NeoStackProtocol::MessageType::GetStreamInfo)
	{
		return HandleGetStreamInfo(Command.Args);
	}

	return MakeError(Command.Command, FString::Printf(TEXT("Unknown command: %s"), *Command.Command));
}

FNeoStackEvent FNeoStackBridgeCommands::HandleOpenBlueprint(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::OpenBlueprint, TEXT("Missing arguments"));
	}

	FString AssetPath = Args->GetStringField(TEXT("path"));
	if (AssetPath.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::OpenBlueprint, TEXT("Missing 'path' argument"));
	}

	// Load the asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MakeError(NeoStackProtocol::MessageType::OpenBlueprint, FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Open in editor
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
		return MakeSuccess(NeoStackProtocol::MessageType::OpenBlueprint);
	}

	return MakeError(NeoStackProtocol::MessageType::OpenBlueprint, TEXT("Asset is not a Blueprint"));
}

FNeoStackEvent FNeoStackBridgeCommands::HandleOpenAsset(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::OpenAsset, TEXT("Missing arguments"));
	}

	FString InputPath = Args->GetStringField(TEXT("path"));
	if (InputPath.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::OpenAsset, TEXT("Missing 'path' argument"));
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] OpenAsset requested: %s"), *InputPath);

	FString AssetPath;

	// Check if this is already a UE content path (starts with /Game/, /Engine/, /Script/, etc.)
	if (InputPath.StartsWith(TEXT("/Game/")) || InputPath.StartsWith(TEXT("/Engine/")) ||
		InputPath.StartsWith(TEXT("/Script/")) || InputPath.StartsWith(TEXT("/Temp/")))
	{
		// Already a UE content path
		AssetPath = InputPath;
	}
	// Check if this is an absolute file path (contains drive letter on Windows)
	else if (InputPath.Contains(TEXT(":")))
	{
		// Convert absolute path to UE content path
		// e.g., C:/Users/.../ueproj/Content/Blueprints/BP_Player.uasset -> /Game/Blueprints/BP_Player

		// Normalize path separators
		FString NormalizedPath = InputPath.Replace(TEXT("\\"), TEXT("/"));

		// Find the Content folder
		int32 ContentIndex = NormalizedPath.Find(TEXT("/Content/"), ESearchCase::IgnoreCase);
		if (ContentIndex == INDEX_NONE)
		{
			return MakeError(NeoStackProtocol::MessageType::OpenAsset,
				FString::Printf(TEXT("Path is not inside Content folder: %s"), *InputPath));
		}

		// Extract the part after /Content/
		FString RelativePath = NormalizedPath.Mid(ContentIndex + 9); // +9 for "/Content/"

		// Remove .uasset extension if present
		if (RelativePath.EndsWith(TEXT(".uasset")))
		{
			RelativePath = RelativePath.LeftChop(7);
		}

		// Build UE content path
		AssetPath = TEXT("/Game/") + RelativePath;

		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Converted path: %s -> %s"), *InputPath, *AssetPath);
	}
	else
	{
		// Assume it's a relative UE path, prepend /Game/
		AssetPath = TEXT("/Game/") + InputPath;
	}

	// Load and open asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MakeError(NeoStackProtocol::MessageType::OpenAsset,
			FString::Printf(TEXT("Asset not found: %s (from %s)"), *AssetPath, *InputPath));
	}

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Opened asset: %s"), *AssetPath);
	return MakeSuccess(NeoStackProtocol::MessageType::OpenAsset);
}

FNeoStackEvent FNeoStackBridgeCommands::HandleNavigateToFile(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::NavigateToFile, TEXT("Missing arguments"));
	}

	FString FilePath = Args->GetStringField(TEXT("path"));
	int32 Line = Args->GetIntegerField(TEXT("line"));
	int32 Column = Args->GetIntegerField(TEXT("column"));

	if (FilePath.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::NavigateToFile, TEXT("Missing 'path' argument"));
	}

	// Navigate to the file/line in the source code editor
	if (FSourceCodeNavigation::OpenSourceFile(FilePath, Line, Column))
	{
		return MakeSuccess(NeoStackProtocol::MessageType::NavigateToFile);
	}

	return MakeError(NeoStackProtocol::MessageType::NavigateToFile, TEXT("Failed to open source file"));
}

FNeoStackEvent FNeoStackBridgeCommands::HandleHotReload(const TSharedPtr<FJsonObject>& Args)
{
	// Trigger Live Coding compile via console command
	if (GEditor)
	{
		// This triggers Live Coding if enabled, or shows an error message if not
		GEditor->Exec(GEditor->GetWorld(), TEXT("LiveCoding.Compile"));
		return MakeSuccess(NeoStackProtocol::MessageType::TriggerHotReload);
	}

	return MakeError(NeoStackProtocol::MessageType::TriggerHotReload, TEXT("Editor not available"));
}

FNeoStackEvent FNeoStackBridgeCommands::HandlePlayInEditor(const TSharedPtr<FJsonObject>& Args)
{
	if (!GEditor)
	{
		return MakeError(NeoStackProtocol::MessageType::PlayInEditor, TEXT("Editor not available"));
	}

	// Check if already playing
	if (GEditor->PlayWorld)
	{
		return MakeError(NeoStackProtocol::MessageType::PlayInEditor, TEXT("Already playing in editor"));
	}

	// Start PIE
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;

	GEditor->RequestPlaySession(Params);

	return MakeSuccess(NeoStackProtocol::MessageType::PlayInEditor);
}

FNeoStackEvent FNeoStackBridgeCommands::HandleStopPIE(const TSharedPtr<FJsonObject>& Args)
{
	if (!GEditor)
	{
		return MakeError(NeoStackProtocol::MessageType::StopPIE, TEXT("Editor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeError(NeoStackProtocol::MessageType::StopPIE, TEXT("Not playing in editor"));
	}

	GEditor->RequestEndPlayMap();

	return MakeSuccess(NeoStackProtocol::MessageType::StopPIE);
}

FNeoStackEvent FNeoStackBridgeCommands::HandleExecuteCommand(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::ExecuteCommand, TEXT("Missing arguments"));
	}

	FString Command = Args->GetStringField(TEXT("command"));
	if (Command.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::ExecuteCommand, TEXT("Missing 'command' argument"));
	}

	// Execute console command
	if (GEditor)
	{
		GEditor->Exec(GEditor->GetWorld(), *Command);
		return MakeSuccess(NeoStackProtocol::MessageType::ExecuteCommand);
	}

	return MakeError(NeoStackProtocol::MessageType::ExecuteCommand, TEXT("Failed to execute command"));
}

FNeoStackEvent FNeoStackBridgeCommands::HandleExecuteTool(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::ExecuteTool, TEXT("Missing arguments"));
	}

	FString ToolName = Args->GetStringField(TEXT("tool"));
	if (ToolName.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::ExecuteTool, TEXT("Missing 'tool' argument"));
	}

	// Get tool args (optional)
	const TSharedPtr<FJsonObject>* ToolArgs = nullptr;
	TSharedPtr<FJsonObject> ToolArgsObj = MakeShared<FJsonObject>();
	if (Args->TryGetObjectField(TEXT("args"), ToolArgs))
	{
		ToolArgsObj = *ToolArgs;
	}

	// Execute via tool registry
	FToolResult Result = FNeoStackToolRegistry::Get().Execute(ToolName, ToolArgsObj);

	if (Result.bSuccess)
	{
		// Return plain text output in data.output
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("output"), Result.Output);
		return MakeSuccess(NeoStackProtocol::MessageType::ExecuteTool, Data);
	}
	else
	{
		return MakeError(NeoStackProtocol::MessageType::ExecuteTool, Result.Output);
	}
}

FNeoStackEvent FNeoStackBridgeCommands::MakeSuccess(const FString& Event, TSharedPtr<FJsonObject> Data)
{
	FNeoStackEvent Response;
	Response.Event = Event;
	Response.bSuccess = true;
	Response.Data = Data;
	return Response;
}

FNeoStackEvent FNeoStackBridgeCommands::MakeError(const FString& Event, const FString& ErrorMessage)
{
	FNeoStackEvent Response;
	Response.Event = Event;
	Response.bSuccess = false;
	Response.Error = ErrorMessage;
	return Response;
}

FNeoStackEvent FNeoStackBridgeCommands::HandleStartStreaming(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!IPixelStreaming2EditorModule::IsAvailable())
	{
		return MakeError(NeoStackProtocol::MessageType::StartStreaming, TEXT("PixelStreaming2 plugin not available"));
	}

	IPixelStreaming2EditorModule& PSModule = IPixelStreaming2EditorModule::Get();

	// Start signalling server if not running
	if (!PSModule.GetSignallingServer().IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Starting PixelStreaming2 signalling server..."));
		PSModule.StartSignalling();
	}

	// Determine stream type from args (default to LevelEditorViewport)
	EPixelStreaming2EditorStreamTypes StreamType = EPixelStreaming2EditorStreamTypes::LevelEditorViewport;
	if (Args.IsValid())
	{
		FString TypeStr = Args->GetStringField(TEXT("type"));
		if (TypeStr == TEXT("Editor"))
		{
			StreamType = EPixelStreaming2EditorStreamTypes::Editor;
		}
	}

	// Start streaming
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Starting PixelStreaming2..."));
	PSModule.StartStreaming(StreamType);

	// Build response with stream URL
	FString Domain = PSModule.GetSignallingDomain();
	int32 ViewerPort = PSModule.GetViewerPort();
	bool bHttps = PSModule.GetServeHttps();

	if (Domain.IsEmpty())
	{
		Domain = TEXT("localhost");
	}

	// Strip any existing protocol prefix from domain (GetSignallingDomain may return ws://host)
	if (Domain.StartsWith(TEXT("ws://")))
	{
		Domain = Domain.RightChop(5); // Remove "ws://"
	}
	else if (Domain.StartsWith(TEXT("wss://")))
	{
		Domain = Domain.RightChop(6); // Remove "wss://"
	}

	// Use ws:// protocol for WebSocket signalling
	FString Protocol = bHttps ? TEXT("wss") : TEXT("ws");
	FString StreamUrl = FString::Printf(TEXT("%s://%s:%d"), *Protocol, *Domain, ViewerPort);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("streamUrl"), StreamUrl);
	Data->SetBoolField(TEXT("isStreaming"), true);
	Data->SetNumberField(TEXT("viewerPort"), ViewerPort);

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] PixelStreaming2 started at: %s"), *StreamUrl);

	return MakeSuccess(NeoStackProtocol::MessageType::StartStreaming, Data);
#else
	return MakeError(NeoStackProtocol::MessageType::StartStreaming, TEXT("PixelStreaming2 only available in Editor builds"));
#endif
}

FNeoStackEvent FNeoStackBridgeCommands::HandleStopStreaming(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!IPixelStreaming2EditorModule::IsAvailable())
	{
		return MakeError(NeoStackProtocol::MessageType::StopStreaming, TEXT("PixelStreaming2 plugin not available"));
	}

	IPixelStreaming2EditorModule& PSModule = IPixelStreaming2EditorModule::Get();

	PSModule.StopStreaming();
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] PixelStreaming2 stopped"));

	return MakeSuccess(NeoStackProtocol::MessageType::StopStreaming);
#else
	return MakeError(NeoStackProtocol::MessageType::StopStreaming, TEXT("PixelStreaming2 only available in Editor builds"));
#endif
}

FNeoStackEvent FNeoStackBridgeCommands::HandleGetStreamInfo(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	if (!IPixelStreaming2EditorModule::IsAvailable())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetBoolField(TEXT("isStreaming"), false);
		Data->SetStringField(TEXT("streamUrl"), TEXT(""));
		return MakeSuccess(NeoStackProtocol::MessageType::GetStreamInfo, Data);
	}

	IPixelStreaming2EditorModule& PSModule = IPixelStreaming2EditorModule::Get();

	// Check if signalling server is running
	TSharedPtr<UE::PixelStreaming2Servers::IServer> Server = PSModule.GetSignallingServer();
	bool bIsStreaming = Server.IsValid();

	Data->SetBoolField(TEXT("available"), true);
	Data->SetBoolField(TEXT("isStreaming"), bIsStreaming);

	if (bIsStreaming)
	{
		FString Domain = PSModule.GetSignallingDomain();
		int32 ViewerPort = PSModule.GetViewerPort();
		bool bHttps = PSModule.GetServeHttps();

		if (Domain.IsEmpty())
		{
			Domain = TEXT("localhost");
		}

		// Strip any existing protocol prefix from domain
		if (Domain.StartsWith(TEXT("ws://")))
		{
			Domain = Domain.RightChop(5);
		}
		else if (Domain.StartsWith(TEXT("wss://")))
		{
			Domain = Domain.RightChop(6);
		}

		// Use ws:// protocol for WebSocket signalling
		FString Protocol = bHttps ? TEXT("wss") : TEXT("ws");
		FString StreamUrl = FString::Printf(TEXT("%s://%s:%d"), *Protocol, *Domain, ViewerPort);

		Data->SetStringField(TEXT("streamUrl"), StreamUrl);
		Data->SetNumberField(TEXT("viewerPort"), ViewerPort);
	}
	else
	{
		Data->SetStringField(TEXT("streamUrl"), TEXT(""));
	}

	return MakeSuccess(NeoStackProtocol::MessageType::GetStreamInfo, Data);
#else
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("available"), false);
	Data->SetBoolField(TEXT("isStreaming"), false);
	Data->SetStringField(TEXT("streamUrl"), TEXT(""));
	return MakeSuccess(NeoStackProtocol::MessageType::GetStreamInfo, Data);
#endif
}
