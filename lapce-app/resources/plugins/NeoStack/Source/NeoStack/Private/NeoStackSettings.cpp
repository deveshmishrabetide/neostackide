// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStackSettings.h"

UNeoStackSettings::UNeoStackSettings()
{
	// Set default values
	BackendURL = TEXT("http://localhost:8080");
	APIKey = TEXT("");
}

UNeoStackSettings* UNeoStackSettings::Get()
{
	return GetMutableDefault<UNeoStackSettings>();
}

FName UNeoStackSettings::GetCategoryName() const
{
	return FName(TEXT("Game"));
}
