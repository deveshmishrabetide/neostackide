// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"

DECLARE_DELEGATE_OneParam(FOnWsConnected, const FString& /* SessionId */);
DECLARE_DELEGATE_OneParam(FOnWsDisconnected, const FString& /* Reason */);
DECLARE_DELEGATE_OneParam(FOnWsMessage, const FString& /* Message */);
DECLARE_DELEGATE(FOnWsReconnecting);

/**
 * WebSocket client for NeoStack IDE communication
 * Connects to the IDE's WebSocket server when UE is launched with -NeoStackIDE argument
 */
class NEOSTACKBRIDGE_API FNeoStackBridgeClient
{
public:
	FNeoStackBridgeClient();
	~FNeoStackBridgeClient();

	/** Connect to WebSocket server at specified URL */
	bool Connect(const FString& Url);

	/** Disconnect from server */
	void Disconnect();

	/** Check if connected */
	bool IsConnected() const;

	/** Check if currently attempting to connect */
	bool IsConnecting() const { return bIsConnecting; }

	/** Send message to server */
	bool SendMessage(const FString& Message);

	/** Get the connection URL */
	FString GetUrl() const { return ServerUrl; }

	/** Get the session ID assigned by the server */
	FString GetSessionId() const { return SessionId; }

	/** Callbacks */
	FOnWsConnected OnConnected;
	FOnWsDisconnected OnDisconnected;
	FOnWsMessage OnMessage;
	FOnWsReconnecting OnReconnecting;

private:
	/** The WebSocket instance */
	TSharedPtr<IWebSocket> WebSocket;

	/** Server URL */
	FString ServerUrl;

	/** Session ID assigned by server */
	FString SessionId;

	/** Is currently attempting to connect */
	bool bIsConnecting;

	/** Has completed handshake */
	bool bHandshakeComplete;

	/** Reconnection attempt count */
	int32 ReconnectAttempts;

	/** Max reconnection attempts (0 = infinite) */
	static constexpr int32 MaxReconnectAttempts = 10;

	/** Timer handle for reconnection */
	FTimerHandle ReconnectTimerHandle;

	/** Message queue for messages during reconnection */
	TArray<FString> PendingMessages;

	/** Max pending messages to queue */
	static constexpr int32 MaxPendingMessages = 100;

	/** Setup WebSocket event handlers */
	void SetupHandlers();

	/** Handle connection success */
	void OnWsConnectedInternal();

	/** Handle connection error */
	void OnWsConnectionError(const FString& Error);

	/** Handle connection closed */
	void OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

	/** Handle received message */
	void OnWsMessageReceived(const FString& Message);

	/** Send handshake message to IDE */
	void SendHandshake();

	/** Process handshake acknowledgment from IDE */
	void ProcessHandshakeAck(const FString& Message);

	/** Attempt reconnection with exponential backoff */
	void AttemptReconnect();

	/** Calculate backoff delay in seconds */
	float CalculateBackoffDelay() const;

	/** Flush pending messages after reconnection */
	void FlushPendingMessages();

	/** Clear reconnection timer */
	void ClearReconnectTimer();
};
