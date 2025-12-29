// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStackCommands.h"

#define LOCTEXT_NAMESPACE "FNeoStackModule"

void FNeoStackCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "NeoStack", "Bring up NeoStack window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
