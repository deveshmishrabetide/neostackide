// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Shared utilities for NeoStack tools
 */
namespace NeoStackToolUtils
{
	//--------------------------------------------------------------------
	// Path Utilities
	//--------------------------------------------------------------------

	/** Check if path indicates a UE asset (vs text file) */
	bool IsAssetPath(const FString& Name, const FString& Path);

	/** Build full file path from name and relative path */
	FString BuildFilePath(const FString& Name, const FString& Path);

	/** Build asset path in /Game/ format */
	FString BuildAssetPath(const FString& Name, const FString& Path);

	/** Ensure directory exists, create if needed */
	bool EnsureDirectoryExists(const FString& FilePath, FString& OutError);

	//--------------------------------------------------------------------
	// Blueprint Utilities
	//--------------------------------------------------------------------

	/** Load a Blueprint from name and path */
	UBlueprint* LoadBlueprint(const FString& Name, const FString& Path, FString& OutError);

	/** Find parent class by name (handles A/U prefixes) */
	UClass* FindParentClass(const FString& ClassName, FString& OutError);

	//--------------------------------------------------------------------
	// Graph Utilities
	//--------------------------------------------------------------------

	/** Find graph by name in a Blueprint */
	UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

	/** Get graph type string (ubergraph, function, macro, collapsed) */
	FString GetGraphType(UEdGraph* Graph, UBlueprint* Blueprint);

	//--------------------------------------------------------------------
	// Node Utilities
	//--------------------------------------------------------------------

	/** Find node by GUID string */
	UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString);

	/** Get node GUID as string */
	FString GetNodeGuid(UEdGraphNode* Node);

	/** Get comma-separated list of visible pin names */
	FString GetNodePinNames(UEdGraphNode* Node);

	//--------------------------------------------------------------------
	// Pin Utilities
	//--------------------------------------------------------------------

	/** Find pin by name on a node */
	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
}
