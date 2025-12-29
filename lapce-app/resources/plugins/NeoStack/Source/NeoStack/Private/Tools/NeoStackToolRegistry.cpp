// Copyright NeoStack. All Rights Reserved.

#include "Tools/NeoStackToolRegistry.h"
#include "Json.h"

// Include all tool headers here
#include "Tools/CreateFileTool.h"
#include "Tools/ReadFileTool.h"
#include "Tools/ExploreTool.h"
#include "Tools/EditBlueprintTool.h"
#include "Tools/FindNodeTool.h"
#include "Tools/EditGraphTool.h"
#include "Tools/ConfigureAssetTool.h"
#include "Tools/EditBehaviorTreeTool.h"
#include "Tools/EditDataStructureTool.h"

FNeoStackToolRegistry& FNeoStackToolRegistry::Get()
{
	static FNeoStackToolRegistry Instance;
	return Instance;
}

FNeoStackToolRegistry::FNeoStackToolRegistry()
{
	RegisterBuiltInTools();
}

void FNeoStackToolRegistry::RegisterBuiltInTools()
{
	// Register all built-in tools
	Register(MakeShared<FCreateFileTool>());
	Register(MakeShared<FReadFileTool>());
	Register(MakeShared<FExploreTool>());
	Register(MakeShared<FEditBlueprintTool>());
	Register(MakeShared<FFindNodeTool>());
	Register(MakeShared<FEditGraphTool>());
	Register(MakeShared<FConfigureAssetTool>());
	Register(MakeShared<FEditBehaviorTreeTool>());
	Register(MakeShared<FEditDataStructureTool>());

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool registry initialized with %d tools"), Tools.Num());
}

void FNeoStackToolRegistry::Register(TSharedPtr<FNeoStackToolBase> Tool)
{
	if (!Tool.IsValid())
	{
		return;
	}

	FString Name = Tool->GetName();
	if (Tools.Contains(Name))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] Tool '%s' already registered, overwriting"), *Name);
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Registered tool: %s"), *Name);
	Tools.Add(Name, Tool);
}

FToolResult FNeoStackToolRegistry::Execute(const FString& ToolName, const FString& ArgsJson)
{
	// Parse JSON args
	TSharedPtr<FJsonObject> Args;

	if (!ArgsJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
		if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to parse arguments for tool '%s'"), *ToolName));
		}
	}
	else
	{
		Args = MakeShared<FJsonObject>();
	}

	return Execute(ToolName, Args);
}

FToolResult FNeoStackToolRegistry::Execute(const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Executing tool: %s"), *ToolName);

	FNeoStackToolBase* Tool = GetTool(ToolName);
	if (!Tool)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
	}

	FToolResult Result = Tool->Execute(Args);

	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool '%s' succeeded: %s"), *ToolName, *Result.Output);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] Tool '%s' failed: %s"), *ToolName, *Result.Output);
	}

	return Result;
}

bool FNeoStackToolRegistry::HasTool(const FString& ToolName) const
{
	return Tools.Contains(ToolName);
}

FNeoStackToolBase* FNeoStackToolRegistry::GetTool(const FString& ToolName) const
{
	const TSharedPtr<FNeoStackToolBase>* Found = Tools.Find(ToolName);
	return Found ? Found->Get() : nullptr;
}

TArray<FString> FNeoStackToolRegistry::GetToolNames() const
{
	TArray<FString> Names;
	Tools.GetKeys(Names);
	return Names;
}
