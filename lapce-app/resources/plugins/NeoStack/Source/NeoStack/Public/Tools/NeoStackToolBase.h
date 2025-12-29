// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Tool execution result - plain text output, not JSON
 */
struct NEOSTACK_API FToolResult
{
	bool bSuccess = false;
	FString Output;

	static FToolResult Ok(const FString& Message)
	{
		FToolResult R;
		R.bSuccess = true;
		R.Output = Message;
		return R;
	}

	static FToolResult Fail(const FString& Message)
	{
		FToolResult R;
		R.bSuccess = false;
		R.Output = Message;
		return R;
	}
};

/**
 * Base class for all NeoStack tools
 * Each tool should inherit from this and implement the virtual methods
 */
class NEOSTACK_API FNeoStackToolBase
{
public:
	virtual ~FNeoStackToolBase() = default;

	/** Tool name used for invocation (e.g., "create_file", "open_asset") */
	virtual FString GetName() const = 0;

	/** Human-readable description for AI context */
	virtual FString GetDescription() const = 0;

	/** Execute the tool with JSON arguments, return plain text result */
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) = 0;
};
