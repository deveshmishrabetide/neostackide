// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "NeoStackStyle.h"

class FNeoStackCommands : public TCommands<FNeoStackCommands>
{
public:

	FNeoStackCommands()
		: TCommands<FNeoStackCommands>(TEXT("NeoStack"), NSLOCTEXT("Contexts", "NeoStack", "NeoStack Plugin"), NAME_None, FNeoStackStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};