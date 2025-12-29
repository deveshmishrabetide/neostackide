// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UBlueprintNodeSpawner;

/**
 * Tool for editing graph logic in Blueprint and Material assets:
 * - Add nodes by spawner ID (from find_node tool)
 * - Set pin default values (Blueprints) or expression properties (Materials)
 * - Create connections between nodes
 * - References work by name (session-persistent) or GUID
 *
 * Supports: Blueprints, AnimBlueprints, Materials, MaterialFunctions
 *
 * Connection format: "NodeRef:PinName->NodeRef:PinName"
 * NodeRef can be: friendly name (registered) or raw GUID
 *
 * set_pins: For Blueprints sets pin default values, for Materials sets expression
 * properties dynamically using reflection (R, Constant, Texture, etc.)
 */
class NEOSTACK_API FEditGraphTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_graph"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Add nodes, set values, and wire connections in Blueprint/AnimBP/Material graphs");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Node definition from JSON */
	struct FNodeDefinition
	{
		FString SpawnerId;          // From find_node tool
		FString Name;               // Friendly name for referencing
		TSharedPtr<FJsonObject> Pins;  // Pin name -> default value
		// Position is calculated automatically - no need for AI to specify
	};

	/** Parsed connection */
	struct FConnectionDef
	{
		FString FromNodeRef;
		FString FromPinName;
		FString ToNodeRef;
		FString ToPinName;
	};

	/** Set pins/properties operation */
	struct FSetPinsOp
	{
		FString NodeRef;                    // Node name or GUID
		TSharedPtr<FJsonObject> Values;     // Pin/property name -> value
	};

	/** Result tracking */
	struct FAddedNode
	{
		FString Name;
		FString NodeType;
		FGuid Guid;
		FVector2D Position;
		TArray<FString> PinValues;  // "PinName = Value" strings
		TArray<FString> InputPins;  // Available input pin names
		TArray<FString> OutputPins; // Available output pin names
	};

	/** Connection result type - tracks how connection was made */
	enum class EConnectionResultType
	{
		Direct,         // Direct pin-to-pin connection
		Promoted,       // Type promotion was applied (e.g., float to double)
		Converted,      // Conversion node was auto-inserted
		Failed          // Connection could not be made
	};

	/** Connection result with details */
	struct FConnectionResult
	{
		bool bSuccess = false;
		EConnectionResultType Type = EConnectionResultType::Failed;
		FString Error;
		FString Details;  // e.g., "promoted float to int" or "inserted ToText node"
	};

	/** Parse a node definition from JSON */
	bool ParseNodeDefinition(const TSharedPtr<FJsonObject>& NodeObj, FNodeDefinition& OutDef, FString& OutError);

	/** Parse connection string "NodeRef:Pin->NodeRef:Pin" */
	bool ParseConnection(const FString& ConnectionStr, FConnectionDef& OutDef, FString& OutError);

	/** Parse set_pins operation from JSON */
	bool ParseSetPinsOp(const TSharedPtr<FJsonObject>& OpObj, FSetPinsOp& OutOp, FString& OutError);

	/** Find spawner by signature ID */
	UBlueprintNodeSpawner* FindSpawnerById(const FString& SpawnerId, UEdGraph* Graph);

	/** Spawn a node using the spawner */
	UEdGraphNode* SpawnNode(UBlueprintNodeSpawner* Spawner, UEdGraph* Graph, const FVector2D& Position);

	/** Set default values on node pins (Blueprint) or expression properties (Material) */
	TArray<FString> SetPinValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& PinValues);

	/** Set values on existing node - dispatches to Blueprint pins or Material expression properties */
	TArray<FString> SetNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values, UEdGraph* Graph);

	/** Resolve a node reference (name or GUID) to actual node */
	UEdGraphNode* ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph, const FString& AssetPath,
	                             const TMap<FString, UEdGraphNode*>& NewNodes);

	/** Find a pin on a node by name */
	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);

	/** List available pins on a node for error messages */
	FString ListAvailablePins(UEdGraphNode* Node, EEdGraphPinDirection Direction);

	/** Create a connection between two pins with three-tier fallback strategy:
	 * 1. Direct connection if types match
	 * 2. Type promotion if schema supports it (e.g., float to double)
	 * 3. Auto-insert conversion node if needed (e.g., int to string)
	 */
	FConnectionResult CreateConnectionWithFallback(UEdGraphPin* FromPin, UEdGraphPin* ToPin);

	/** Legacy simple connection (for compatibility) */
	bool CreateConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Validate connection prerequisites */
	bool ValidateConnectionPrerequisites(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Break a connection between two pins */
	bool BreakConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Break all connections on a pin */
	bool BreakAllConnections(UEdGraphPin* Pin, FString& OutError);

	/** Get the target graph from a Blueprint */
	UEdGraph* GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const;

	/** Get node type display name */
	FString GetNodeTypeName(UEdGraphNode* Node) const;

	/** Calculate smart position for a new node - finds empty space near existing nodes */
	FVector2D CalculateSmartPosition(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& NewNodesThisCall) const;

	/** Format results to output string */
	FString FormatResults(const FString& AssetName, const FString& GraphName,
	                      const TArray<FAddedNode>& AddedNodes,
	                      const TArray<FString>& Connections,
	                      const TArray<FString>& Disconnections,
	                      const TArray<FString>& SetPinsResults,
	                      const TArray<FString>& Errors) const;
};
