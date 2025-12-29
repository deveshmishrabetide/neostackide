// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridgeClient.h"
#include "NeoStackBridgeProtocol.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

FNeoStackBridgeClient::FNeoStackBridgeClient()
	: bIsConnecting(false)
	, bHandshakeComplete(false)
	, ReconnectAttempts(0)
{
}

FNeoStackBridgeClient::~FNeoStackBridgeClient()
{
	Disconnect();
}

bool FNeoStackBridgeClient::Connect(const FString& Url)
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] Already connected to IDE"));
		return false;
	}

	ServerUrl = Url;
	bIsConnecting = true;
	bHandshakeComplete = false;

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Connecting to IDE at: %s"), *Url);

	// Ensure WebSockets module is loaded
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	// Create WebSocket (no subprotocol needed)
	WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url);

	if (!WebSocket.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to create WebSocket"));
		bIsConnecting = false;
		return false;
	}

	SetupHandlers();
	WebSocket->Connect();

	return true;
}

void FNeoStackBridgeClient::SetupHandlers()
{
	WebSocket->OnConnected().AddLambda([this]()
	{
		OnWsConnectedInternal();
	});

	WebSocket->OnConnectionError().AddLambda([this](const FString& Error)
	{
		OnWsConnectionError(Error);
	});

	WebSocket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		OnWsClosed(StatusCode, Reason, bWasClean);
	});

	WebSocket->OnMessage().AddLambda([this](const FString& Message)
	{
		OnWsMessageReceived(Message);
	});
}

void FNeoStackBridgeClient::Disconnect()
{
	ClearReconnectTimer();

	if (WebSocket.IsValid())
	{
		if (WebSocket->IsConnected())
		{
			WebSocket->Close();
		}
		WebSocket.Reset();
	}

	bIsConnecting = false;
	bHandshakeComplete = false;
	SessionId.Empty();
	PendingMessages.Empty();

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Disconnected from IDE"));
}

bool FNeoStackBridgeClient::IsConnected() const
{
	return WebSocket.IsValid() && WebSocket->IsConnected() && bHandshakeComplete;
}

bool FNeoStackBridgeClient::SendMessage(const FString& Message)
{
	if (!IsConnected())
	{
		// Queue message if we're reconnecting
		if (bIsConnecting && PendingMessages.Num() < MaxPendingMessages)
		{
			PendingMessages.Add(Message);
			UE_LOG(LogTemp, Verbose, TEXT("[NeoStackBridge] Queued message for later delivery"));
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] Cannot send message - not connected"));
		return false;
	}

	WebSocket->Send(Message);
	return true;
}

void FNeoStackBridgeClient::OnWsConnectedInternal()
{
	bIsConnecting = false;
	ReconnectAttempts = 0;

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] WebSocket connected to IDE"));

	// Send handshake
	SendHandshake();
}

void FNeoStackBridgeClient::SendHandshake()
{
	// Build handshake message
	FString ProjectPath = FPaths::GetProjectFilePath();
	FString ProjectId = FMD5::HashAnsiString(*ProjectPath);
	FString ProjectName = FString(FApp::GetProjectName());
	FString EngineVersion = FEngineVersion::Current().ToString();

	TSharedPtr<FJsonObject> HandshakeObj = MakeShareable(new FJsonObject);
	HandshakeObj->SetStringField(TEXT("type"), TEXT("handshake"));
	HandshakeObj->SetNumberField(TEXT("version"), NeoStackProtocol::ProtocolVersion);
	HandshakeObj->SetStringField(TEXT("projectId"), ProjectId);
	HandshakeObj->SetStringField(TEXT("projectPath"), ProjectPath);
	HandshakeObj->SetStringField(TEXT("projectName"), ProjectName);
	HandshakeObj->SetStringField(TEXT("engineVersion"), EngineVersion);
	HandshakeObj->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());

	FString HandshakeJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&HandshakeJson);
	FJsonSerializer::Serialize(HandshakeObj.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Sending handshake: %s"), *HandshakeJson);
	WebSocket->Send(HandshakeJson);
}

void FNeoStackBridgeClient::OnWsConnectionError(const FString& Error)
{
	FString ErrorMsg = Error.IsEmpty() ? TEXT("Unknown error (possibly server not running or connection refused)") : Error;
	UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Connection error: %s"), *ErrorMsg);
	UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Attempted URL: %s"), *ServerUrl);
	bIsConnecting = false;

	// Try to reconnect
	AttemptReconnect();
}

void FNeoStackBridgeClient::OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Connection closed: Code=%d, Reason=%s, Clean=%d"),
		StatusCode, *Reason, bWasClean);

	bIsConnecting = false;
	bHandshakeComplete = false;

	FString DisconnectReason = Reason.IsEmpty() ? FString::Printf(TEXT("Connection closed (code %d)"), StatusCode) : Reason;
	OnDisconnected.ExecuteIfBound(DisconnectReason);

	// Try to reconnect if not a clean close
	if (!bWasClean || StatusCode != 1000)
	{
		AttemptReconnect();
	}
}

void FNeoStackBridgeClient::OnWsMessageReceived(const FString& Message)
{
	UE_LOG(LogTemp, Verbose, TEXT("[NeoStackBridge] Received: %s"), *Message);

	// Check if this is a handshake acknowledgment
	if (!bHandshakeComplete)
	{
		ProcessHandshakeAck(Message);
		return;
	}

	// Forward to handler
	OnMessage.ExecuteIfBound(Message);
}

void FNeoStackBridgeClient::ProcessHandshakeAck(const FString& Message)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to parse handshake ack"));
		return;
	}

	FString MsgType;
	if (!JsonObject->TryGetStringField(TEXT("type"), MsgType) || MsgType != TEXT("handshake_ack"))
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Expected handshake_ack, got: %s"), *MsgType);
		return;
	}

	bool bSuccess = false;
	JsonObject->TryGetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		FString Error;
		JsonObject->TryGetStringField(TEXT("error"), Error);
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Handshake failed: %s"), *Error);
		Disconnect();
		return;
	}

	JsonObject->TryGetStringField(TEXT("sessionId"), SessionId);
	bHandshakeComplete = true;

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Handshake complete, session: %s"), *SessionId);

	// Flush pending messages
	FlushPendingMessages();

	// Notify connected
	OnConnected.ExecuteIfBound(SessionId);
}

void FNeoStackBridgeClient::AttemptReconnect()
{
	if (ServerUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] No server URL for reconnection"));
		return;
	}

	if (ReconnectAttempts >= MaxReconnectAttempts && MaxReconnectAttempts > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Max reconnection attempts (%d) reached"), MaxReconnectAttempts);
		return;
	}

	ReconnectAttempts++;
	float Delay = CalculateBackoffDelay();

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Reconnecting in %.1f seconds (attempt %d/%d)"),
		Delay, ReconnectAttempts, MaxReconnectAttempts);

	OnReconnecting.ExecuteIfBound();

	// Clear existing socket
	if (WebSocket.IsValid())
	{
		WebSocket.Reset();
	}

	// Schedule reconnection on game thread
	if (GEngine && GEngine->GetWorld())
	{
		GEngine->GetWorld()->GetTimerManager().SetTimer(
			ReconnectTimerHandle,
			[this]()
			{
				Connect(ServerUrl);
			},
			Delay,
			false
		);
	}
	else
	{
		// Fallback: try to reconnect immediately if no world available
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			Connect(ServerUrl);
			return false; // Don't repeat
		}), CalculateBackoffDelay());
	}
}

float FNeoStackBridgeClient::CalculateBackoffDelay() const
{
	// Exponential backoff: 1s, 2s, 4s, 8s, 16s, max 30s
	float BaseDelay = 1.0f;
	float Delay = BaseDelay * FMath::Pow(2.0f, FMath::Min(ReconnectAttempts - 1, 4));
	return FMath::Min(Delay, 30.0f);
}

void FNeoStackBridgeClient::FlushPendingMessages()
{
	if (PendingMessages.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Flushing %d pending messages"), PendingMessages.Num());

		for (const FString& Message : PendingMessages)
		{
			SendMessage(Message);
		}
		PendingMessages.Empty();
	}
}

void FNeoStackBridgeClient::ClearReconnectTimer()
{
	if (ReconnectTimerHandle.IsValid() && GEngine && GEngine->GetWorld())
	{
		GEngine->GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}
	ReconnectTimerHandle.Invalidate();
}
