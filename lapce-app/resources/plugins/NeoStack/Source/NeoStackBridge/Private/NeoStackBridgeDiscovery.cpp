// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridgeDiscovery.h"
#include "NeoStackBridgeProtocol.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

// PixelStreaming2 includes - forward declare to avoid hard dependency
#if WITH_EDITOR
#include "IPixelStreaming2EditorModule.h"
#endif

FNeoStackBridgeDiscovery::FNeoStackBridgeDiscovery()
	: BroadcastSocket(nullptr)
	, AdvertisedWSPort(0)
	, bIsBroadcasting(false)
	, TimeSinceLastBroadcast(0.0f)
{
}

FNeoStackBridgeDiscovery::~FNeoStackBridgeDiscovery()
{
	Stop();
}

bool FNeoStackBridgeDiscovery::Start(int32 WSPort)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Discovery::Start called with port %d"), WSPort);

	if (bIsBroadcasting)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] Discovery already broadcasting"));
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to get socket subsystem for discovery"));
		return false;
	}

	// Create UDP socket for broadcasting
	BroadcastSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("NeoStackDiscovery"), true);
	if (!BroadcastSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to create broadcast socket"));
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Broadcast socket created"));

	// Enable broadcasting
	BroadcastSocket->SetBroadcast(true);
	BroadcastSocket->SetReuseAddr(true);

	AdvertisedWSPort = WSPort;
	bIsBroadcasting = true;

	// Cache project info
	CacheProjectInfo();

	// Set up ticker for periodic broadcasts
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FNeoStackBridgeDiscovery::OnTick),
		0.1f // Tick every 100ms
	);
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Ticker registered"));

	// Send first broadcast immediately
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Sending first broadcast..."));
	SendBroadcast();

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Discovery initialized, broadcasting to port %d"), NeoStackProtocol::DiscoveryPort);
	return true;
}

void FNeoStackBridgeDiscovery::Stop()
{
	bIsBroadcasting = false;

	// Remove ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Close socket
	if (BroadcastSocket)
	{
		BroadcastSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(BroadcastSocket);
		BroadcastSocket = nullptr;
	}
}

bool FNeoStackBridgeDiscovery::IsBroadcasting() const
{
	return bIsBroadcasting;
}

void FNeoStackBridgeDiscovery::BroadcastNow()
{
	SendBroadcast();
}

void FNeoStackBridgeDiscovery::CacheProjectInfo()
{
	CachedProjectPath = FPaths::GetProjectFilePath();
	CachedProjectName = FString(FApp::GetProjectName());
	CachedProjectId = GenerateProjectId(CachedProjectPath);
	CachedEngineVersion = FEngineVersion::Current().ToString();

	// Parse -NeoStackConn=<id> command line argument for auto-connect
	FString NeoStackConnArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("-NeoStackConn="), NeoStackConnArg))
	{
		CachedNeoStackConn = NeoStackConnArg;
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Found NeoStackConn ID: %s"), *CachedNeoStackConn);
	}
}

bool FNeoStackBridgeDiscovery::OnTick(float DeltaTime)
{
	if (!bIsBroadcasting)
	{
		return false; // Stop ticking
	}

	TimeSinceLastBroadcast += DeltaTime;

	if (TimeSinceLastBroadcast >= NeoStackProtocol::BroadcastInterval)
	{
		SendBroadcast();
		TimeSinceLastBroadcast = 0.0f;
	}

	return true; // Continue ticking
}

void FNeoStackBridgeDiscovery::SendBroadcast()
{
	if (!BroadcastSocket || !bIsBroadcasting)
	{
		return;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[NeoStackBridge] Sending discovery broadcast..."));

	// Build presence message
	FNeoStackPresenceMessage Message;
	Message.Version = NeoStackProtocol::ProtocolVersion;
	Message.Type = NeoStackProtocol::MessageType::Presence;
	Message.ProjectId = CachedProjectId;
	Message.ProjectPath = CachedProjectPath;
	Message.ProjectName = CachedProjectName;
	Message.WSPort = AdvertisedWSPort;
	Message.EngineVersion = CachedEngineVersion;
	Message.ProcessId = FPlatformProcess::GetCurrentProcessId();

	// Add PixelStreaming2 info
	FPixelStreamingInfo StreamInfo = GetPixelStreamingInfo();
	Message.StreamUrl = StreamInfo.StreamUrl;
	Message.bIsStreaming = StreamInfo.bIsStreaming;

	// Add NeoStackConn ID for auto-connect
	Message.NeoStackConn = CachedNeoStackConn;

	FString JsonMessage = Message.ToJson();

	// Convert to UTF8
	FTCHARToUTF8 Converter(*JsonMessage);
	const uint8* Data = (const uint8*)Converter.Get();
	int32 DataLen = Converter.Length();

	// Create broadcast address
	TSharedRef<FInternetAddr> BroadcastAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	// Broadcast to 255.255.255.255 on discovery port
	bool bIsValid;
	BroadcastAddr->SetIp(TEXT("255.255.255.255"), bIsValid);
	BroadcastAddr->SetPort(NeoStackProtocol::DiscoveryPort);

	int32 BytesSent = 0;
	bool bSentBroadcast = BroadcastSocket->SendTo(Data, DataLen, BytesSent, *BroadcastAddr);

	// Also send to localhost for local testing
	BroadcastAddr->SetIp(TEXT("127.0.0.1"), bIsValid);
	int32 BytesSentLocal = 0;
	bool bSentLocal = BroadcastSocket->SendTo(Data, DataLen, BytesSentLocal, *BroadcastAddr);

	UE_LOG(LogTemp, Verbose, TEXT("[NeoStackBridge] Broadcast sent: %d bytes (broadcast=%s, local=%s)"),
		DataLen, bSentBroadcast ? TEXT("yes") : TEXT("no"), bSentLocal ? TEXT("yes") : TEXT("no"));
}

FString FNeoStackBridgeDiscovery::GenerateProjectId(const FString& ProjectPath)
{
	// Use MD5 hash of project path as unique ID
	return FMD5::HashAnsiString(*ProjectPath);
}

bool FNeoStackBridgeDiscovery::StartPixelStreaming()
{
#if WITH_EDITOR
	if (!IPixelStreaming2EditorModule::IsAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] PixelStreaming2Editor module not available"));
		return false;
	}

	IPixelStreaming2EditorModule& PSModule = IPixelStreaming2EditorModule::Get();

	// Start signalling server if not running
	if (!PSModule.GetSignallingServer().IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Starting PixelStreaming2 signalling server..."));
		PSModule.StartSignalling();
	}

	// Start streaming the level editor viewport
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Starting PixelStreaming2 (LevelEditorViewport)..."));
	PSModule.StartStreaming(EPixelStreaming2EditorStreamTypes::LevelEditorViewport);

	return true;
#else
	return false;
#endif
}

FPixelStreamingInfo FNeoStackBridgeDiscovery::GetPixelStreamingInfo() const
{
	FPixelStreamingInfo Info;

#if WITH_EDITOR
	if (!IPixelStreaming2EditorModule::IsAvailable())
	{
		return Info;
	}

	IPixelStreaming2EditorModule& PSModule = IPixelStreaming2EditorModule::Get();

	// Check if signalling server is running
	TSharedPtr<UE::PixelStreaming2Servers::IServer> Server = PSModule.GetSignallingServer();
	if (!Server.IsValid())
	{
		return Info;
	}

	// Build the stream URL
	FString Domain = PSModule.GetSignallingDomain();
	int32 ViewerPort = PSModule.GetViewerPort();
	bool bHttps = PSModule.GetServeHttps();

	if (Domain.IsEmpty())
	{
		Domain = TEXT("localhost");
	}

	// Construct URL: ws(s)://domain:port for WebSocket signalling
	FString Protocol = bHttps ? TEXT("wss") : TEXT("ws");
	Info.StreamUrl = FString::Printf(TEXT("%s://%s:%d"), *Protocol, *Domain, ViewerPort);
	Info.bIsStreaming = true;

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] PixelStreaming URL: %s"), *Info.StreamUrl);
#endif

	return Info;
}
