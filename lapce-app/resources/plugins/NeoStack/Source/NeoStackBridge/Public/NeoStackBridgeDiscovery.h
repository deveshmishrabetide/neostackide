// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Containers/Ticker.h"

/**
 * PixelStreaming2 stream info
 */
struct FPixelStreamingInfo
{
	FString StreamUrl;
	bool bIsStreaming = false;
};

/**
 * UDP Discovery broadcaster
 * Periodically broadcasts presence to allow IDE to discover this UE instance
 */
class NEOSTACKBRIDGE_API FNeoStackBridgeDiscovery
{
public:
	FNeoStackBridgeDiscovery();
	~FNeoStackBridgeDiscovery();

	/** Start broadcasting discovery messages */
	bool Start(int32 WSPort);

	/** Stop broadcasting */
	void Stop();

	/** Check if broadcasting */
	bool IsBroadcasting() const;

	/** Force an immediate broadcast */
	void BroadcastNow();

	/** Start PixelStreaming2 if available and not already running */
	bool StartPixelStreaming();

	/** Get current PixelStreaming2 stream info */
	FPixelStreamingInfo GetPixelStreamingInfo() const;

private:
	/** UDP socket for broadcasting */
	FSocket* BroadcastSocket;

	/** WebSocket port to advertise */
	int32 AdvertisedWSPort;

	/** Ticker delegate handle for periodic broadcasts */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Is actively broadcasting */
	bool bIsBroadcasting;

	/** Time accumulator for broadcast interval */
	float TimeSinceLastBroadcast;

	/** Cached project info */
	FString CachedProjectId;
	FString CachedProjectPath;
	FString CachedProjectName;
	FString CachedEngineVersion;
	FString CachedNeoStackConn;

	/** Initialize project info cache */
	void CacheProjectInfo();

	/** Ticker callback */
	bool OnTick(float DeltaTime);

	/** Build and send broadcast message */
	void SendBroadcast();

	/** Get unique project identifier */
	static FString GenerateProjectId(const FString& ProjectPath);
};
