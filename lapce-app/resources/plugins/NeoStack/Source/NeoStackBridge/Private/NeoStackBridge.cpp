// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridge.h"
#include "NeoStackBridgeClient.h"
#include "NeoStackBridgeProtocol.h"
#include "NeoStackBridgeCommands.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

#define LOCTEXT_NAMESPACE "FNeoStackBridgeModule"

/** Singleton instances */
static TUniquePtr<FNeoStackBridgeClient> GBridgeClient;
static FString GProjectId;

void FNeoStackBridgeModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Module starting up..."));

	// Initialize immediately since we're loaded PostEngineInit anyway
	InitializeBridge();
}

void FNeoStackBridgeModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Module shutting down..."));
	ShutdownBridge();
}

FNeoStackBridgeModule& FNeoStackBridgeModule::Get()
{
	return FModuleManager::LoadModuleChecked<FNeoStackBridgeModule>("NeoStackBridge");
}

bool FNeoStackBridgeModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("NeoStackBridge");
}

bool FNeoStackBridgeModule::IsIDEConnected() const
{
	return GBridgeClient.IsValid() && GBridgeClient->IsConnected();
}

FString FNeoStackBridgeModule::GetProjectId() const
{
	return GProjectId;
}

void FNeoStackBridgeModule::InitializeBridge()
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] InitializeBridge starting..."));

	// Check for -NeoStackIDE command line argument
	FString NeoStackIDEUrl;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-NeoStackIDE="), NeoStackIDEUrl))
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] No -NeoStackIDE argument found, bridge disabled"));
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Launch from NeoStack IDE to enable bridge connection"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] IDE URL: %s"), *NeoStackIDEUrl);

	// Generate project ID
	FString ProjectPath = FPaths::GetProjectFilePath();
	GProjectId = FMD5::HashAnsiString(*ProjectPath);

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Project: %s"), FApp::GetProjectName());
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Project ID: %s"), *GProjectId);

	// Create WebSocket client
	GBridgeClient = MakeUnique<FNeoStackBridgeClient>();

	// Set up callbacks
	GBridgeClient->OnConnected.BindLambda([](const FString& SessionId)
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Connected to IDE, session: %s"), *SessionId);
	});

	GBridgeClient->OnDisconnected.BindLambda([](const FString& Reason)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] Disconnected from IDE: %s"), *Reason);
	});

	GBridgeClient->OnReconnecting.BindLambda([]()
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Attempting to reconnect to IDE..."));
	});

	GBridgeClient->OnMessage.BindLambda([](const FString& Message)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[NeoStackBridge] Received: %s"), *Message);

		// Parse and handle command
		FNeoStackCommand Command;
		if (FNeoStackCommand::FromJson(Message, Command))
		{
			// Process on game thread
			AsyncTask(ENamedThreads::GameThread, [Command]()
			{
				FNeoStackEvent Response = FNeoStackBridgeCommands::ProcessCommand(Command);
				Response.RequestId = Command.RequestId;

				if (GBridgeClient.IsValid() && GBridgeClient->IsConnected())
				{
					GBridgeClient->SendMessage(Response.ToJson());
				}
			});
		}
	});

	// Connect to IDE
	if (GBridgeClient->Connect(NeoStackIDEUrl))
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Connecting to IDE..."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to initiate connection to IDE"));
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Bridge initialized"));
}

void FNeoStackBridgeModule::ShutdownBridge()
{
	if (GBridgeClient.IsValid())
	{
		GBridgeClient->Disconnect();
		GBridgeClient.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Bridge shut down"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNeoStackBridgeModule, NeoStackBridge)
