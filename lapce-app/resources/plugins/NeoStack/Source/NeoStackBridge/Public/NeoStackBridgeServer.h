// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/Runnable.h"

DECLARE_DELEGATE_TwoParams(FOnClientConnected, const FString& /* ClientId */, const FIPv4Endpoint& /* Endpoint */);
DECLARE_DELEGATE_OneParam(FOnClientDisconnected, const FString& /* ClientId */);
DECLARE_DELEGATE_TwoParams(FOnMessageReceived, const FString& /* ClientId */, const FString& /* Message */);

/**
 * Runnable for accepting new connections
 */
class FAcceptConnectionsRunnable : public FRunnable
{
public:
	FAcceptConnectionsRunnable(class FNeoStackBridgeServer* InServer);
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	FNeoStackBridgeServer* Server;
	FThreadSafeBool bShouldStop;
};

/**
 * Runnable for receiving data from clients
 */
class FReceiveDataRunnable : public FRunnable
{
public:
	FReceiveDataRunnable(class FNeoStackBridgeServer* InServer);
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	FNeoStackBridgeServer* Server;
	FThreadSafeBool bShouldStop;
};

/**
 * WebSocket-like TCP server for IDE communication
 * Handles multiple client connections and message routing
 */
class NEOSTACKBRIDGE_API FNeoStackBridgeServer
{
public:
	FNeoStackBridgeServer();
	~FNeoStackBridgeServer();

	/** Start the server on the specified port */
	bool Start(int32 Port);

	/** Stop the server and disconnect all clients */
	void Stop();

	/** Check if server is running */
	bool IsRunning() const;

	/** Get the port the server is listening on */
	int32 GetPort() const;

	/** Send message to a specific client */
	bool SendMessage(const FString& ClientId, const FString& Message);

	/** Send message to all connected clients */
	void BroadcastMessage(const FString& Message);

	/** Get number of connected clients */
	int32 GetClientCount() const;

	/** Callbacks */
	FOnClientConnected OnClientConnected;
	FOnClientDisconnected OnOnClientDisconnected;
	FOnMessageReceived OnMessageReceived;

	/** Friend classes for runnables */
	friend class FAcceptConnectionsRunnable;
	friend class FReceiveDataRunnable;

private:
	/** Server socket */
	FSocket* ListenSocket;

	/** Port we're listening on */
	int32 ListenPort;

	/** Connected clients */
	struct FClientConnection
	{
		FString Id;
		FSocket* Socket;
		FIPv4Endpoint Endpoint;
		FString ReceiveBuffer;
	};
	TMap<FString, TSharedPtr<FClientConnection>> Clients;

	/** Runnable for accepting connections */
	TUniquePtr<FAcceptConnectionsRunnable> AcceptRunnable;
	FRunnableThread* AcceptThread;

	/** Runnable for receiving data */
	TUniquePtr<FReceiveDataRunnable> ReceiveRunnable;
	FRunnableThread* ReceiveThread;

	/** Running flag */
	FThreadSafeBool bIsRunning;

	/** Critical section for thread safety */
	mutable FCriticalSection ClientsLock;

	/** Accept new connections (called from runnable) */
	void AcceptConnections();

	/** Receive data from clients (called from runnable) */
	void ReceiveData();

	/** Process received data for a client */
	void ProcessReceivedData(TSharedPtr<FClientConnection> Client);

	/** Generate unique client ID */
	FString GenerateClientId();
};
