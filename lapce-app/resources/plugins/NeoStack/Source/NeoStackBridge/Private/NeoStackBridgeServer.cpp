// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridgeServer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "IPAddress.h"
#include "Async/Async.h"

// FAcceptConnectionsRunnable implementation
FAcceptConnectionsRunnable::FAcceptConnectionsRunnable(FNeoStackBridgeServer* InServer)
	: Server(InServer)
	, bShouldStop(false)
{
}

uint32 FAcceptConnectionsRunnable::Run()
{
	Server->AcceptConnections();
	return 0;
}

void FAcceptConnectionsRunnable::Stop()
{
	bShouldStop = true;
}

// FReceiveDataRunnable implementation
FReceiveDataRunnable::FReceiveDataRunnable(FNeoStackBridgeServer* InServer)
	: Server(InServer)
	, bShouldStop(false)
{
}

uint32 FReceiveDataRunnable::Run()
{
	Server->ReceiveData();
	return 0;
}

void FReceiveDataRunnable::Stop()
{
	bShouldStop = true;
}

// FNeoStackBridgeServer implementation
FNeoStackBridgeServer::FNeoStackBridgeServer()
	: ListenSocket(nullptr)
	, ListenPort(0)
	, AcceptThread(nullptr)
	, ReceiveThread(nullptr)
	, bIsRunning(false)
{
}

FNeoStackBridgeServer::~FNeoStackBridgeServer()
{
	Stop();
}

bool FNeoStackBridgeServer::Start(int32 Port)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Server::Start called with port %d"), Port);

	if (bIsRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStackBridge] Server already running"));
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to get socket subsystem"));
		return false;
	}

	// Create listen socket
	ListenSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("NeoStackBridge"), false);
	if (!ListenSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to create listen socket"));
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Listen socket created"));

	// Set socket options
	ListenSocket->SetReuseAddr(true);
	ListenSocket->SetNonBlocking(true);

	// Bind to localhost
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetAnyAddress();
	Addr->SetPort(Port);

	if (!ListenSocket->Bind(*Addr))
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to bind to port %d"), Port);
		SocketSubsystem->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Socket bound to port %d"), Port);

	if (!ListenSocket->Listen(8))
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStackBridge] Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Socket listening"));

	ListenPort = Port;
	bIsRunning = true;

	// Create and start accept thread
	AcceptRunnable = MakeUnique<FAcceptConnectionsRunnable>(this);
	AcceptThread = FRunnableThread::Create(AcceptRunnable.Get(), TEXT("NeoStackBridge_Accept"));

	// Create and start receive thread
	ReceiveRunnable = MakeUnique<FReceiveDataRunnable>(this);
	ReceiveThread = FRunnableThread::Create(ReceiveRunnable.Get(), TEXT("NeoStackBridge_Receive"));

	return true;
}

void FNeoStackBridgeServer::Stop()
{
	bIsRunning = false;

	// Stop threads
	if (AcceptRunnable.IsValid())
	{
		AcceptRunnable->Stop();
	}
	if (ReceiveRunnable.IsValid())
	{
		ReceiveRunnable->Stop();
	}

	// Wait for threads to finish
	if (AcceptThread)
	{
		AcceptThread->WaitForCompletion();
		delete AcceptThread;
		AcceptThread = nullptr;
	}
	if (ReceiveThread)
	{
		ReceiveThread->WaitForCompletion();
		delete ReceiveThread;
		ReceiveThread = nullptr;
	}

	// Clear runnables
	AcceptRunnable.Reset();
	ReceiveRunnable.Reset();

	// Close all client connections
	{
		FScopeLock Lock(&ClientsLock);
		for (auto& Pair : Clients)
		{
			if (Pair.Value->Socket)
			{
				Pair.Value->Socket->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Pair.Value->Socket);
			}
		}
		Clients.Empty();
	}

	// Close listen socket
	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}

	ListenPort = 0;
}

bool FNeoStackBridgeServer::IsRunning() const
{
	return bIsRunning;
}

int32 FNeoStackBridgeServer::GetPort() const
{
	return ListenPort;
}

bool FNeoStackBridgeServer::SendMessage(const FString& ClientId, const FString& Message)
{
	FScopeLock Lock(&ClientsLock);

	TSharedPtr<FClientConnection>* ClientPtr = Clients.Find(ClientId);
	if (!ClientPtr || !(*ClientPtr)->Socket)
	{
		return false;
	}

	// Add newline as message delimiter
	FString MessageWithDelimiter = Message + TEXT("\n");
	FTCHARToUTF8 Converter(*MessageWithDelimiter);
	int32 BytesSent = 0;

	return (*ClientPtr)->Socket->Send((const uint8*)Converter.Get(), Converter.Length(), BytesSent);
}

void FNeoStackBridgeServer::BroadcastMessage(const FString& Message)
{
	FScopeLock Lock(&ClientsLock);

	for (auto& Pair : Clients)
	{
		SendMessage(Pair.Key, Message);
	}
}

int32 FNeoStackBridgeServer::GetClientCount() const
{
	FScopeLock Lock(&ClientsLock);
	return Clients.Num();
}

void FNeoStackBridgeServer::AcceptConnections()
{
	while (bIsRunning)
	{
		if (!ListenSocket)
		{
			break;
		}

		bool bPending = false;
		if (ListenSocket->WaitForPendingConnection(bPending, FTimespan::FromMilliseconds(100)))
		{
			if (bPending)
			{
				FSocket* ClientSocket = ListenSocket->Accept(TEXT("NeoStackClient"));
				if (ClientSocket)
				{
					ClientSocket->SetNonBlocking(true);

					TSharedPtr<FClientConnection> NewClient = MakeShareable(new FClientConnection);
					NewClient->Id = GenerateClientId();
					NewClient->Socket = ClientSocket;

					TSharedRef<FInternetAddr> RemoteAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
					ClientSocket->GetPeerAddress(*RemoteAddr);
					NewClient->Endpoint = FIPv4Endpoint(RemoteAddr);

					{
						FScopeLock Lock(&ClientsLock);
						Clients.Add(NewClient->Id, NewClient);
					}

					// Notify on game thread
					AsyncTask(ENamedThreads::GameThread, [this, Id = NewClient->Id, Endpoint = NewClient->Endpoint]()
					{
						OnClientConnected.ExecuteIfBound(Id, Endpoint);
					});
				}
			}
		}

		FPlatformProcess::Sleep(0.01f);
	}
}

void FNeoStackBridgeServer::ReceiveData()
{
	TArray<uint8> ReceiveBuffer;
	ReceiveBuffer.SetNumUninitialized(4096);

	while (bIsRunning)
	{
		TArray<FString> DisconnectedClients;

		{
			FScopeLock Lock(&ClientsLock);

			for (auto& Pair : Clients)
			{
				TSharedPtr<FClientConnection>& Client = Pair.Value;
				if (!Client->Socket)
				{
					continue;
				}

				uint32 PendingDataSize = 0;
				if (Client->Socket->HasPendingData(PendingDataSize))
				{
					int32 BytesRead = 0;
					if (Client->Socket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num() - 1, BytesRead))
					{
						if (BytesRead > 0)
						{
							ReceiveBuffer[BytesRead] = 0;
							FUTF8ToTCHAR Converter((const ANSICHAR*)ReceiveBuffer.GetData());
							Client->ReceiveBuffer += FString(Converter.Get());

							ProcessReceivedData(Client);
						}
					}
					else
					{
						// Connection error
						DisconnectedClients.Add(Client->Id);
					}
				}

				// Check connection state
				ESocketConnectionState State = Client->Socket->GetConnectionState();
				if (State != ESocketConnectionState::SCS_Connected)
				{
					DisconnectedClients.Add(Client->Id);
				}
			}
		}

		// Handle disconnections
		for (const FString& ClientId : DisconnectedClients)
		{
			FScopeLock Lock(&ClientsLock);
			TSharedPtr<FClientConnection>* ClientPtr = Clients.Find(ClientId);
			if (ClientPtr)
			{
				if ((*ClientPtr)->Socket)
				{
					(*ClientPtr)->Socket->Close();
					ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket((*ClientPtr)->Socket);
				}
				Clients.Remove(ClientId);

				AsyncTask(ENamedThreads::GameThread, [this, ClientId]()
				{
					OnOnClientDisconnected.ExecuteIfBound(ClientId);
				});
			}
		}

		FPlatformProcess::Sleep(0.01f);
	}
}

void FNeoStackBridgeServer::ProcessReceivedData(TSharedPtr<FClientConnection> Client)
{
	// Messages are newline-delimited
	int32 NewlineIndex;
	while (Client->ReceiveBuffer.FindChar(TEXT('\n'), NewlineIndex))
	{
		FString Message = Client->ReceiveBuffer.Left(NewlineIndex);
		Client->ReceiveBuffer = Client->ReceiveBuffer.Mid(NewlineIndex + 1);

		if (!Message.IsEmpty())
		{
			AsyncTask(ENamedThreads::GameThread, [this, ClientId = Client->Id, Message]()
			{
				OnMessageReceived.ExecuteIfBound(ClientId, Message);
			});
		}
	}
}

FString FNeoStackBridgeServer::GenerateClientId()
{
	return FGuid::NewGuid().ToString();
}
