// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NeoStackSettings.generated.h"

/**
 * Settings for the NeoStack plugin
 * Appears in Project Settings under Game category
 * Stored in DefaultGame.ini (not packaged with game)
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="NeoStack"))
class NEOSTACK_API UNeoStackSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNeoStackSettings();

	/** API Key for authenticating with the NeoStack backend (format: nsk_...) */
	UPROPERTY(config, EditAnywhere, Category="Authentication", meta=(DisplayName="API Key"))
	FString APIKey;

	/** Backend server URL */
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="Backend URL"))
	FString BackendURL;

	/** Get the singleton instance */
	static UNeoStackSettings* Get();

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings Interface
};
