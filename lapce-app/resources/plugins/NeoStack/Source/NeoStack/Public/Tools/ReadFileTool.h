// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

// Forward declarations
class UWidgetBlueprint;
class UWidget;
class UAnimBlueprint;
class UEdGraph;
class UBehaviorTree;
class UBTCompositeNode;
class UBlackboardData;
class UUserDefinedStruct;
class UUserDefinedEnum;
class UDataTable;

/**
 * Tool for reading files and UE assets (Blueprint, Material, WidgetBlueprint, AnimBlueprint, BehaviorTree, etc.)
 * - Text files: returns content with pagination
 * - Graph assets: returns nodes and connections using shared UEdGraph reading
 * - Widget Blueprints: returns widget tree hierarchy
 * - Animation Blueprints: returns state machines, states, transitions, and their subgraphs
 * - Behavior Trees: returns node hierarchy with composites, tasks, decorators, and services
 * - Blackboards: returns keys with types and inheritance
 * - User Defined Structs: returns fields with names, types, and default values
 * - User Defined Enums: returns values with names and display names
 * - DataTables: returns row struct info and row data
 */
class NEOSTACK_API FReadFileTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("read_file"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Read a file or asset from the project");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Read a text file with pagination */
	FToolResult ReadTextFile(const FString& Name, const FString& Path, int32 Offset, int32 Limit);

	/** Get Blueprint summary with graph list */
	FString GetBlueprintSummary(class UBlueprint* Blueprint);

	/** Get Blueprint components in tab-delimited format */
	FString GetBlueprintComponents(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get Blueprint variables in tab-delimited format */
	FString GetBlueprintVariables(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get all graphs with full nodes and connections */
	FString GetBlueprintGraphs(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get Blueprint interfaces */
	FString GetBlueprintInterfaces(class UBlueprint* Blueprint);

	/** Get graph type string */
	FString GetGraphType(class UEdGraph* Graph, class UBlueprint* Blueprint);

	/** Get single graph with nodes in UNIX format */
	FString GetGraphWithNodes(class UEdGraph* Graph, const FString& GraphType, const FString& ParentGraph, int32 Offset, int32 Limit);

	/** Get connections for a graph */
	FString GetGraphConnections(class UEdGraph* Graph);

	/** Get pin names for a node */
	FString GetNodePins(class UEdGraphNode* Node);

	/** Get Widget Blueprint summary */
	FString GetWidgetBlueprintSummary(UWidgetBlueprint* WidgetBlueprint);

	/** Get widget tree structure */
	FString GetWidgetTree(UWidgetBlueprint* WidgetBlueprint);

	/** Recursively get widget hierarchy */
	FString GetWidgetHierarchy(UWidget* Widget, int32 Depth);

	// Animation Blueprint support

	/** Get Animation Blueprint summary with skeleton and state machine info */
	FString GetAnimBlueprintSummary(UAnimBlueprint* AnimBlueprint);

	/** Get detailed state machine information including states and transitions */
	FString GetAnimBlueprintStateMachines(UAnimBlueprint* AnimBlueprint);

	/** Collect all graphs from AnimBP including AnimGraph, state machines, states, and transitions */
	void CollectAnimBlueprintGraphs(UAnimBlueprint* AnimBlueprint, TArray<TPair<UEdGraph*, FString>>& OutGraphs);

	// Behavior Tree support

	/** Get Behavior Tree summary with blackboard and node counts */
	FString GetBehaviorTreeSummary(UBehaviorTree* BehaviorTree);

	/** Count nodes recursively in the behavior tree */
	void CountBTNodes(UBTCompositeNode* Node, int32& OutTasks, int32& OutComposites, int32& OutDecorators, int32& OutServices);

	/** Get behavior tree node hierarchy */
	FString GetBehaviorTreeNodes(UBehaviorTree* BehaviorTree);

	/** Recursively get BT node hierarchy with decorators and services */
	FString GetBTNodeHierarchy(UBTCompositeNode* Node, int32 Depth);

	// Blackboard support

	/** Get Blackboard summary with parent and key count */
	FString GetBlackboardSummary(UBlackboardData* Blackboard);

	/** Get all Blackboard keys with types */
	FString GetBlackboardKeys(UBlackboardData* Blackboard);

	// User Defined Struct support

	/** Get User Defined Struct summary with field count */
	FString GetStructSummary(UUserDefinedStruct* Struct);

	/** Get all struct fields with types and default values */
	FString GetStructFields(UUserDefinedStruct* Struct);

	// User Defined Enum support

	/** Get User Defined Enum summary with value count */
	FString GetEnumSummary(UUserDefinedEnum* Enum);

	/** Get all enum values with display names */
	FString GetEnumValues(UUserDefinedEnum* Enum);

	// DataTable support

	/** Get DataTable summary with row struct and row count */
	FString GetDataTableSummary(UDataTable* DataTable);

	/** Get DataTable rows with values */
	FString GetDataTableRows(UDataTable* DataTable, int32 Offset, int32 Limit);
};
