// Copyright NeoStack. All Rights Reserved.

#include "Tools/EditGraphTool.h"
#include "Tools/NodeNameRegistry.h"
#include "Json.h"

// Blueprint includes
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"

// Animation Blueprint
#include "Animation/AnimBlueprint.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialEditorUtilities.h"
#include "IMaterialEditor.h"
#include "UObject/UnrealType.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

// Universal schema action system
#include "EdGraph/EdGraphSchema.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintVariableNodeSpawner.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

FToolResult FEditGraphTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	// Parse required parameters
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset"));
	}

	// Parse optional path
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Parse graph name
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	// Build asset path and load
	if (!Path.StartsWith(TEXT("/Game")) && !Path.StartsWith(TEXT("/Engine")))
	{
		Path = FString::Printf(TEXT("/Game/%s"), *Path);
	}

	FString FullAssetPath = Path / AssetName + TEXT(".") + AssetName;
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Ensure asset editor is open for proper schema initialization
	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			bool bIsAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
			if (!bIsAlreadyOpen)
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
				FPlatformProcess::Sleep(0.1f); // Give editor time to initialize
			}
		}
	}

	// Get target graph based on asset type
	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	UMaterial* WorkingMaterial = nullptr; // Track which material we're actually working with

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		// CRITICAL: When the Material Editor is open, it works on a PREVIEW COPY of the material.
		// We must modify the preview material's graph, not the original, otherwise our changes
		// will be lost when the user clicks "Apply" (the editor overwrites original with preview).
		WorkingMaterial = Material;

		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
				if (EditorInstance)
				{
					// The Material Editor implements IMaterialEditor
					IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
					if (MaterialEditor)
					{
						// GetMaterialInterface returns the PREVIEW material that the editor is working on
						UMaterialInterface* PreviewMaterial = MaterialEditor->GetMaterialInterface();
						if (UMaterial* PreviewMat = Cast<UMaterial>(PreviewMaterial))
						{
							WorkingMaterial = PreviewMat;
							UE_LOG(LogTemp, Log, TEXT("NeoStack: Using preview material from Material Editor"));
						}
					}
				}
			}
		}

		// Material graph - create if needed
		if (!WorkingMaterial->MaterialGraph)
		{
			WorkingMaterial->MaterialGraph = CastChecked<UMaterialGraph>(
				FBlueprintEditorUtils::CreateNewGraph(WorkingMaterial, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
			WorkingMaterial->MaterialGraph->Material = WorkingMaterial;
			WorkingMaterial->MaterialGraph->RebuildGraph();
		}
		Graph = WorkingMaterial->MaterialGraph;
	}
	else if (UMaterialFunction* MaterialFunc = Cast<UMaterialFunction>(Asset))
	{
		// MaterialFunction graph - create if needed
		if (!MaterialFunc->MaterialGraph)
		{
			MaterialFunc->MaterialGraph = CastChecked<UMaterialGraph>(
				FBlueprintEditorUtils::CreateNewGraph(MaterialFunc, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
			MaterialFunc->MaterialGraph->MaterialFunction = MaterialFunc;
			MaterialFunc->MaterialGraph->RebuildGraph();
		}
		Graph = MaterialFunc->MaterialGraph;
	}
	else if ((Blueprint = Cast<UBlueprint>(Asset)) != nullptr)
	{
		// Blueprint graph
		Graph = GetGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		}
	}
	else
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName()));
	}

	// Use actual graph name for registry
	FString ActualGraphName = Graph->GetName();

	// Track results
	TArray<FAddedNode> AddedNodes;
	TArray<FString> ConnectionResults;
	TArray<FString> DisconnectResults;
	TArray<FString> SetPinsResults;
	TArray<FString> Errors;

	// Map of new node names to their instances (for connection resolution within this call)
	TMap<FString, UEdGraphNode*> NewNodeMap;

	// Process add_nodes
	const TArray<TSharedPtr<FJsonValue>>* AddNodesArray;
	if (Args->TryGetArrayField(TEXT("add_nodes"), AddNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *AddNodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObj;
			if (!NodeValue->TryGetObject(NodeObj))
			{
				Errors.Add(TEXT("Invalid node definition (not an object)"));
				continue;
			}

			FNodeDefinition NodeDef;
			FString ParseError;
			if (!ParseNodeDefinition(*NodeObj, NodeDef, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			// Find action using UNIVERSAL schema approach
			TSharedPtr<FEdGraphSchemaAction> FoundAction;
			const UEdGraphSchema* Schema = Graph->GetSchema();

			if (Blueprint)
			{
				// Blueprint-specific action database
				FBlueprintActionContext FilterContext;
				FilterContext.Blueprints.Add(Blueprint);
				FilterContext.Graphs.Add(Graph);

				FBlueprintActionMenuBuilder MenuBuilder(FBlueprintActionMenuBuilder::DefaultConfig);
				uint32 ClassTargetMask = EContextTargetFlags::TARGET_Blueprint |
				                         EContextTargetFlags::TARGET_BlueprintLibraries |
				                         EContextTargetFlags::TARGET_SubComponents |
				                         EContextTargetFlags::TARGET_NonImportedTypes;

				FBlueprintActionMenuUtils::MakeContextMenu(FilterContext, false, ClassTargetMask, MenuBuilder);

				// Check if this is a variable getter/setter ID (VARGET: or VARSET:)
				// These have special handling because UE's spawner signature doesn't distinguish member variables
				bool bIsVarGetterOrSetter = NodeDef.SpawnerId.StartsWith(TEXT("VARGET:")) || NodeDef.SpawnerId.StartsWith(TEXT("VARSET:"));
				FString TargetPropertyPath;
				bool bIsGetter = false;

				if (bIsVarGetterOrSetter)
				{
					bIsGetter = NodeDef.SpawnerId.StartsWith(TEXT("VARGET:"));
					TargetPropertyPath = NodeDef.SpawnerId.Mid(bIsGetter ? 7 : 7); // Skip "VARGET:" or "VARSET:"
					UE_LOG(LogTemp, Log, TEXT("NeoStack: Searching for variable %s with property path '%s'"),
						bIsGetter ? TEXT("getter") : TEXT("setter"), *TargetPropertyPath);
				}

				// Try to parse SpawnerId as a GUID (the canonical unique identifier)
				FGuid TargetGuid;
				bool bHasTargetGuid = !bIsVarGetterOrSetter && FGuid::Parse(NodeDef.SpawnerId, TargetGuid);

				UE_LOG(LogTemp, Log, TEXT("NeoStack: Searching for action with SpawnerId='%s' (IsGuid=%d, IsVar=%d)"),
					*NodeDef.SpawnerId, bHasTargetGuid ? 1 : 0, bIsVarGetterOrSetter ? 1 : 0);

				for (int32 i = 0; i < MenuBuilder.GetNumActions(); i++)
				{
					TSharedPtr<FEdGraphSchemaAction> Action = MenuBuilder.GetSchemaAction(i);
					if (!Action.IsValid())
					{
						continue;
					}

					// Check if this is a FBlueprintActionMenuItem (which wraps UBlueprintNodeSpawner)
					if (Action->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
					{
						FBlueprintActionMenuItem* BPMenuItem = static_cast<FBlueprintActionMenuItem*>(Action.Get());
						if (const UBlueprintNodeSpawner* Spawner = BPMenuItem->GetRawAction())
						{
							// Special handling for variable getters/setters
							if (bIsVarGetterOrSetter)
							{
								if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
								{
									// Check if it's the right type (getter vs setter)
									bool bSpawnerIsGetter = Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
									if (bSpawnerIsGetter != bIsGetter)
									{
										continue;
									}

									// Match by property path
									if (FProperty const* VarProp = VarSpawner->GetVarProperty())
									{
										FString PropPath = VarProp->GetPathName();
										if (PropPath.Equals(TargetPropertyPath, ESearchCase::IgnoreCase))
										{
											UE_LOG(LogTemp, Log, TEXT("NeoStack: MATCHED variable by property path: %s"), *PropPath);
											FoundAction = Action;
											break;
										}
									}
								}
								continue; // Only check variable spawners for VARGET/VARSET
							}

							// Get the spawner's unique signature GUID
							FGuid SpawnerGuid = Spawner->GetSpawnerSignature().AsGuid();

							if (bHasTargetGuid)
							{
								// Direct GUID match - most reliable
								if (SpawnerGuid == TargetGuid)
								{
									UE_LOG(LogTemp, Log, TEXT("NeoStack: MATCHED by GUID: %s"), *SpawnerGuid.ToString());
									FoundAction = Action;
									break;
								}
							}
							else
							{
								// Fallback: try to match by signature string if SpawnerId isn't a GUID
								FString SignatureStr = Spawner->GetSpawnerSignature().ToString();
								if (SignatureStr.Equals(NodeDef.SpawnerId, ESearchCase::IgnoreCase) ||
									SignatureStr.Contains(NodeDef.SpawnerId) ||
									NodeDef.SpawnerId.Contains(SignatureStr))
								{
									UE_LOG(LogTemp, Log, TEXT("NeoStack: MATCHED by Signature: %s"), *SignatureStr);
									FoundAction = Action;
									break;
								}
							}
						}
					}
				}
			}
			else
			{
				// UNIVERSAL schema-based discovery for Materials, etc.
				FGraphContextMenuBuilder ContextMenuBuilder(Graph);
				Schema->GetGraphContextActions(ContextMenuBuilder);

				// For material expressions, SpawnerId is like "/Script/Engine.MaterialExpressionConstant3Vector"
				// We need to find the action that creates this expression class
				FString TargetClassName = NodeDef.SpawnerId;
				// Extract just the class name
				int32 LastDot = INDEX_NONE;
				if (TargetClassName.FindLastChar('.', LastDot))
				{
					TargetClassName = TargetClassName.Mid(LastDot + 1);
				}

				// Find action by matching MaterialExpressionClass
				for (int32 i = 0; i < ContextMenuBuilder.GetNumActions(); i++)
				{
					TSharedPtr<FEdGraphSchemaAction> Action = ContextMenuBuilder.GetSchemaAction(i);
					if (Action.IsValid())
					{
						FString TypeId = Action->GetTypeId().ToString();

						// For material expressions, the action stores the class directly
						if (TypeId == TEXT("FMaterialGraphSchemaAction_NewNode"))
						{
							// Cast to get the MaterialExpressionClass
							FMaterialGraphSchemaAction_NewNode* MaterialAction =
								static_cast<FMaterialGraphSchemaAction_NewNode*>(Action.Get());

							if (MaterialAction && MaterialAction->MaterialExpressionClass)
							{
								FString ActionClassName = MaterialAction->MaterialExpressionClass->GetName();

								// Direct class name match
								if (ActionClassName.Equals(TargetClassName, ESearchCase::IgnoreCase))
								{
									FoundAction = Action;
									break;
								}

								// Also try full path match
								FString ActionClassPath = MaterialAction->MaterialExpressionClass->GetPathName();
								if (ActionClassPath.Equals(NodeDef.SpawnerId, ESearchCase::IgnoreCase))
								{
									FoundAction = Action;
									break;
								}
							}
						}
						else
						{
							// For other non-Blueprint graph types, try menu description match
							FString MenuDesc = Action->GetMenuDescription().ToString();
							if (NodeDef.SpawnerId.Contains(MenuDesc) || MenuDesc.Contains(NodeDef.SpawnerId))
							{
								FoundAction = Action;
								break;
							}
						}
					}
				}
			}

			if (!FoundAction.IsValid())
			{
				Errors.Add(FString::Printf(TEXT("Action not found: %s"), *NodeDef.SpawnerId));
				continue;
			}

			// Calculate smart position - finds empty space near existing nodes
			FVector2D SmartPosition = CalculateSmartPosition(Graph, NewNodeMap);

			// UNIVERSAL node creation using PerformAction
			TArray<UEdGraphPin*> EmptyPins;
			UEdGraphNode* NewNode = FoundAction->PerformAction(Graph, EmptyPins, FVector2f(SmartPosition.X, SmartPosition.Y), true);
			if (!NewNode)
			{
				Errors.Add(FString::Printf(TEXT("Failed to create node: %s"), *NodeDef.SpawnerId));
				continue;
			}

			// Set pin values
			TArray<FString> PinValueResults;
			if (NodeDef.Pins.IsValid())
			{
				PinValueResults = SetPinValues(NewNode, NodeDef.Pins);
			}

			// Generate name if not provided
			FString NodeName = NodeDef.Name;
			if (NodeName.IsEmpty())
			{
				NodeName = FString::Printf(TEXT("%s_%s"),
					*GetNodeTypeName(NewNode),
					*NewNode->NodeGuid.ToString().Left(8));
			}

			// Register in session registry (replaces if exists)
			FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);

			// Track in local map for this call's connections
			NewNodeMap.Add(NodeName, NewNode);

			// Track result
			FAddedNode Added;
			Added.Name = NodeName;
			Added.NodeType = GetNodeTypeName(NewNode);
			Added.Guid = NewNode->NodeGuid;
			Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
			Added.PinValues = PinValueResults;

			// Capture available pins for AI to know what connections are possible
			// Skip hidden, not-connectable, and orphaned pins
			for (UEdGraphPin* Pin : NewNode->Pins)
			{
				if (Pin->bHidden || Pin->bNotConnectable || Pin->bOrphanedPin) continue;
				if (Pin->Direction == EGPD_Input)
				{
					Added.InputPins.Add(Pin->PinName.ToString());
				}
				else if (Pin->Direction == EGPD_Output)
				{
					Added.OutputPins.Add(Pin->PinName.ToString());
				}
			}

			AddedNodes.Add(Added);
		}
	}

	// Process connections
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (Args->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
		{
			FString ConnectionStr;
			if (!ConnValue->TryGetString(ConnectionStr))
			{
				Errors.Add(TEXT("Invalid connection (not a string)"));
				continue;
			}

			FConnectionDef ConnDef;
			FString ParseError;
			if (!ParseConnection(ConnectionStr, ConnDef, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			// Resolve 'from' node
			UEdGraphNode* FromNode = ResolveNodeRef(ConnDef.FromNodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!FromNode)
			{
				Errors.Add(FString::Printf(TEXT("Cannot resolve 'from' node: %s"), *ConnDef.FromNodeRef));
				continue;
			}

			// Resolve 'to' node
			UEdGraphNode* ToNode = ResolveNodeRef(ConnDef.ToNodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!ToNode)
			{
				Errors.Add(FString::Printf(TEXT("Cannot resolve 'to' node: %s"), *ConnDef.ToNodeRef));
				continue;
			}

			// Find pins
			UEdGraphPin* FromPin = FindPinByName(FromNode, ConnDef.FromPinName, EGPD_Output);
			if (!FromPin)
			{
				FString AvailablePins = ListAvailablePins(FromNode, EGPD_Output);
				Errors.Add(FString::Printf(TEXT("Output pin '%s' not found on %s. Available outputs: %s"),
					*ConnDef.FromPinName, *ConnDef.FromNodeRef, *AvailablePins));
				continue;
			}

			UEdGraphPin* ToPin = FindPinByName(ToNode, ConnDef.ToPinName, EGPD_Input);
			if (!ToPin)
			{
				FString AvailablePins = ListAvailablePins(ToNode, EGPD_Input);
				Errors.Add(FString::Printf(TEXT("Input pin '%s' not found on %s. Available inputs: %s"),
					*ConnDef.ToPinName, *ConnDef.ToNodeRef, *AvailablePins));
				continue;
			}

			// Create connection using three-tier fallback strategy
			FConnectionResult ConnResult = CreateConnectionWithFallback(FromPin, ToPin);
			if (ConnResult.bSuccess)
			{
				FString ConnStr = FString::Printf(TEXT("%s:%s -> %s:%s"),
					*ConnDef.FromNodeRef, *ConnDef.FromPinName,
					*ConnDef.ToNodeRef, *ConnDef.ToPinName);

				// Add connection type suffix for non-direct connections
				if (ConnResult.Type == EConnectionResultType::Promoted)
				{
					ConnStr += FString::Printf(TEXT(" [promoted: %s]"), *ConnResult.Details);
				}
				else if (ConnResult.Type == EConnectionResultType::Converted)
				{
					ConnStr += FString::Printf(TEXT(" [converted: %s]"), *ConnResult.Details);
				}
				else if (ConnResult.Details == TEXT("already connected"))
				{
					ConnStr += TEXT(" [already connected]");
				}

				ConnectionResults.Add(ConnStr);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Connection failed %s:%s -> %s:%s: %s"),
					*ConnDef.FromNodeRef, *ConnDef.FromPinName,
					*ConnDef.ToNodeRef, *ConnDef.ToPinName,
					*ConnResult.Error));
			}
		}
	}

	// Process disconnect - break connections
	const TArray<TSharedPtr<FJsonValue>>* DisconnectArray;
	if (Args->TryGetArrayField(TEXT("disconnect"), DisconnectArray))
	{
		for (const TSharedPtr<FJsonValue>& DisconnValue : *DisconnectArray)
		{
			FString DisconnStr;
			if (!DisconnValue->TryGetString(DisconnStr))
			{
				Errors.Add(TEXT("Invalid disconnect entry (not a string)"));
				continue;
			}

			// Check if it's a specific connection "NodeA:PinA -> NodeB:PinB" or just a pin "NodeA:PinA"
			if (DisconnStr.Contains(TEXT("->")))
			{
				// Specific connection to break
				FConnectionDef ConnDef;
				FString ParseError;
				if (!ParseConnection(DisconnStr, ConnDef, ParseError))
				{
					Errors.Add(ParseError);
					continue;
				}

				// Resolve nodes
				UEdGraphNode* FromNode = ResolveNodeRef(ConnDef.FromNodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!FromNode)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve 'from' node for disconnect: %s"), *ConnDef.FromNodeRef));
					continue;
				}

				UEdGraphNode* ToNode = ResolveNodeRef(ConnDef.ToNodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!ToNode)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve 'to' node for disconnect: %s"), *ConnDef.ToNodeRef));
					continue;
				}

				// Find pins
				UEdGraphPin* FromPin = FindPinByName(FromNode, ConnDef.FromPinName, EGPD_Output);
				if (!FromPin)
				{
					FString AvailablePins = ListAvailablePins(FromNode, EGPD_Output);
					Errors.Add(FString::Printf(TEXT("Output pin '%s' not found on %s for disconnect. Available: %s"),
						*ConnDef.FromPinName, *ConnDef.FromNodeRef, *AvailablePins));
					continue;
				}

				UEdGraphPin* ToPin = FindPinByName(ToNode, ConnDef.ToPinName, EGPD_Input);
				if (!ToPin)
				{
					FString AvailablePins = ListAvailablePins(ToNode, EGPD_Input);
					Errors.Add(FString::Printf(TEXT("Input pin '%s' not found on %s for disconnect. Available: %s"),
						*ConnDef.ToPinName, *ConnDef.ToNodeRef, *AvailablePins));
					continue;
				}

				// Break specific connection
				FString BreakError;
				if (BreakConnection(FromPin, ToPin, BreakError))
				{
					DisconnectResults.Add(FString::Printf(TEXT("%s:%s -x- %s:%s"),
						*ConnDef.FromNodeRef, *ConnDef.FromPinName,
						*ConnDef.ToNodeRef, *ConnDef.ToPinName));
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Disconnect failed %s:%s -> %s:%s: %s"),
						*ConnDef.FromNodeRef, *ConnDef.FromPinName,
						*ConnDef.ToNodeRef, *ConnDef.ToPinName,
						*BreakError));
				}
			}
			else
			{
				// Break all connections on a pin "NodeRef:PinName"
				FString NodeRef, PinName;
				if (!DisconnStr.Split(TEXT(":"), &NodeRef, &PinName))
				{
					Errors.Add(FString::Printf(TEXT("Invalid disconnect format (missing :): %s"), *DisconnStr));
					continue;
				}

				UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!Node)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve node for disconnect: %s"), *NodeRef));
					continue;
				}

				// Try to find as output first, then input
				UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Output);
				if (!Pin)
				{
					Pin = FindPinByName(Node, PinName, EGPD_Input);
				}

				if (!Pin)
				{
					FString AvailableOutputs = ListAvailablePins(Node, EGPD_Output);
					FString AvailableInputs = ListAvailablePins(Node, EGPD_Input);
					Errors.Add(FString::Printf(TEXT("Pin '%s' not found on %s. Outputs: %s | Inputs: %s"),
						*PinName, *NodeRef, *AvailableOutputs, *AvailableInputs));
					continue;
				}

				int32 BrokenCount = Pin->LinkedTo.Num();
				FString BreakError;
				if (BreakAllConnections(Pin, BreakError))
				{
					DisconnectResults.Add(FString::Printf(TEXT("%s:%s -x- (all %d connections)"),
						*NodeRef, *PinName, BrokenCount));
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Disconnect all failed %s:%s: %s"),
						*NodeRef, *PinName, *BreakError));
				}
			}
		}
	}

	// Process set_pins - set values on existing nodes
	const TArray<TSharedPtr<FJsonValue>>* SetPinsArray;
	if (Args->TryGetArrayField(TEXT("set_pins"), SetPinsArray))
	{
		for (const TSharedPtr<FJsonValue>& SetPinValue : *SetPinsArray)
		{
			const TSharedPtr<FJsonObject>* SetPinObj;
			if (!SetPinValue->TryGetObject(SetPinObj))
			{
				Errors.Add(TEXT("Invalid set_pins entry (not an object)"));
				continue;
			}

			FSetPinsOp SetOp;
			FString ParseError;
			if (!ParseSetPinsOp(*SetPinObj, SetOp, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			// Resolve the node
			UEdGraphNode* TargetNode = ResolveNodeRef(SetOp.NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!TargetNode)
			{
				Errors.Add(FString::Printf(TEXT("Node not found for set_pins: %s"), *SetOp.NodeRef));
				continue;
			}

			// Set values on the node
			TArray<FString> Results = SetNodeValues(TargetNode, SetOp.Values, Graph);
			for (const FString& Result : Results)
			{
				SetPinsResults.Add(FString::Printf(TEXT("%s: %s"), *SetOp.NodeRef, *Result));
			}
		}
	}

	// Mark asset dirty and trigger updates
	Asset->Modify();
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else if (UMaterialGraph* MatGraph = Cast<UMaterialGraph>(Graph))
	{
		// Critical: TryCreateConnection only creates pin links (visual/transient),
		// but FExpressionInput is the persistent storage that saves to .uasset.
		// LinkMaterialExpressionsFromGraph syncs graph pins TO FExpressionInput.
		// Order matters: Modify -> Link -> MarkDirty -> UpdatePinTypes -> Recompile

		UMaterial* Mat = MatGraph->Material;
		if (Mat)
		{
			// Step 1: Mark material as being modified BEFORE sync
			Mat->Modify();

			// Step 2: Sync graph connections to FExpressionInput
			MatGraph->LinkMaterialExpressionsFromGraph();

			// Step 3: Notify Material Editor of changes
			FMaterialEditorUtilities::UpdateMaterialAfterGraphChange(MatGraph);

			// Step 4: Mark the Material Editor as dirty so user knows to save
			// This is CRITICAL when working with preview materials
			if (GEditor)
			{
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem)
				{
					IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
					if (EditorInstance)
					{
						IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
						if (MaterialEditor)
						{
							MaterialEditor->MarkMaterialDirty();
							UE_LOG(LogTemp, Log, TEXT("NeoStack: Marked Material Editor as dirty"));
						}
					}
				}
			}

			// Step 5: Mark package dirty and force recompile
			Mat->MarkPackageDirty();
			Mat->ForceRecompileForRendering();

			// Debug: Log RootNode connection state AND FExpressionInput state
			if (MatGraph->RootNode)
			{
				UE_LOG(LogTemp, Log, TEXT("NeoStack: RootNode has %d pins"), MatGraph->RootNode->Pins.Num());
				for (UEdGraphPin* Pin : MatGraph->RootNode->Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						UE_LOG(LogTemp, Log, TEXT("  Pin '%s' SourceIndex=%d LinkedTo=%d"),
							*Pin->PinName.ToString(), Pin->SourceIndex, Pin->LinkedTo.Num());

						// Check if the FExpressionInput was actually set (the PERSISTENT storage)
						if (Pin->SourceIndex >= 0 && Pin->SourceIndex < MatGraph->MaterialInputs.Num())
						{
							FExpressionInput& MatInput = MatGraph->MaterialInputs[Pin->SourceIndex].GetExpressionInput(Mat);
							if (MatInput.Expression)
							{
								UE_LOG(LogTemp, Log, TEXT("NeoStack: FExpressionInput CONNECTED to %s (OutputIndex=%d)"),
									*MatInput.Expression->GetName(), MatInput.OutputIndex);
							}
							else if (Pin->LinkedTo.Num() > 0)
							{
								UE_LOG(LogTemp, Warning, TEXT("NeoStack: FExpressionInput is NULL but Pin has %d links! NOT PERSISTED!"),
									Pin->LinkedTo.Num());
							}
						}
					}
				}
			}
		}
	}

	// Format and return results
	FString Output = FormatResults(AssetName, ActualGraphName, AddedNodes, ConnectionResults, DisconnectResults, SetPinsResults, Errors);

	if (Errors.Num() > 0 && AddedNodes.Num() == 0 && ConnectionResults.Num() == 0 && DisconnectResults.Num() == 0 && SetPinsResults.Num() == 0)
	{
		return FToolResult::Fail(Output);
	}

	return FToolResult::Ok(Output);
}

bool FEditGraphTool::ParseNodeDefinition(const TSharedPtr<FJsonObject>& NodeObj, FNodeDefinition& OutDef, FString& OutError)
{
	// Required: id (spawner ID)
	if (!NodeObj->TryGetStringField(TEXT("id"), OutDef.SpawnerId) || OutDef.SpawnerId.IsEmpty())
	{
		OutError = TEXT("Node missing required 'id' field");
		return false;
	}

	// Optional: name
	NodeObj->TryGetStringField(TEXT("name"), OutDef.Name);

	// Position is calculated automatically - not parsed from JSON

	// Optional: pins (object with pin values)
	const TSharedPtr<FJsonObject>* PinsObj;
	if (NodeObj->TryGetObjectField(TEXT("pins"), PinsObj))
	{
		OutDef.Pins = *PinsObj;
	}

	return true;
}

bool FEditGraphTool::ParseConnection(const FString& ConnectionStr, FConnectionDef& OutDef, FString& OutError)
{
	// Format: "NodeRef:PinName->NodeRef:PinName"
	FString FromPart, ToPart;
	if (!ConnectionStr.Split(TEXT("->"), &FromPart, &ToPart))
	{
		OutError = FString::Printf(TEXT("Invalid connection format (missing ->): %s"), *ConnectionStr);
		return false;
	}

	FromPart.TrimStartAndEndInline();
	ToPart.TrimStartAndEndInline();

	// Parse from part
	if (!FromPart.Split(TEXT(":"), &OutDef.FromNodeRef, &OutDef.FromPinName))
	{
		OutError = FString::Printf(TEXT("Invalid 'from' format (missing :): %s"), *FromPart);
		return false;
	}

	// Parse to part
	if (!ToPart.Split(TEXT(":"), &OutDef.ToNodeRef, &OutDef.ToPinName))
	{
		OutError = FString::Printf(TEXT("Invalid 'to' format (missing :): %s"), *ToPart);
		return false;
	}

	return true;
}

bool FEditGraphTool::ParseSetPinsOp(const TSharedPtr<FJsonObject>& OpObj, FSetPinsOp& OutOp, FString& OutError)
{
	// Required: node reference
	if (!OpObj->TryGetStringField(TEXT("node"), OutOp.NodeRef) || OutOp.NodeRef.IsEmpty())
	{
		OutError = TEXT("set_pins entry missing required 'node' field");
		return false;
	}

	// Required: values object
	const TSharedPtr<FJsonObject>* ValuesObj;
	if (!OpObj->TryGetObjectField(TEXT("values"), ValuesObj))
	{
		OutError = TEXT("set_pins entry missing required 'values' field");
		return false;
	}
	OutOp.Values = *ValuesObj;

	return true;
}

TArray<FString> FEditGraphTool::SetNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values, UEdGraph* Graph)
{
	TArray<FString> Results;

	if (!Node || !Values.IsValid())
	{
		return Results;
	}

	// Check if this is a Material expression node
	UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
	if (MatNode && MatNode->MaterialExpression)
	{
		// Material expression - use reflection to set properties
		UMaterialExpression* Expression = MatNode->MaterialExpression;

		for (const auto& Pair : Values->Values)
		{
			const FString& PropertyName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			// Convert JSON value to string
			FString ValueStr;
			if (Value->Type == EJson::String)
			{
				Value->TryGetString(ValueStr);
			}
			else if (Value->Type == EJson::Number)
			{
				double NumVal;
				Value->TryGetNumber(NumVal);
				ValueStr = FString::SanitizeFloat(NumVal);
			}
			else if (Value->Type == EJson::Boolean)
			{
				bool BoolVal;
				Value->TryGetBool(BoolVal);
				ValueStr = BoolVal ? TEXT("True") : TEXT("False");
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PropertyName));
				continue;
			}

			// Find property using reflection
			FProperty* Property = Expression->GetClass()->FindPropertyByName(*PropertyName);
			if (!Property)
			{
				Results.Add(FString::Printf(TEXT("! %s: property not found"), *PropertyName));
				continue;
			}

			// Set property using reflection (same pattern as configure_asset)
			Expression->Modify();
			Expression->PreEditChange(Property);

			// Import the value
			const TCHAR* ImportResult = Property->ImportText_InContainer(*ValueStr, Expression, Expression, PPF_None);

			if (!ImportResult)
			{
				Results.Add(FString::Printf(TEXT("! %s: failed to set value '%s'"), *PropertyName, *ValueStr));
				continue;
			}

			// Post-edit change
			Expression->MarkPackageDirty();
			FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
			Expression->PostEditChangeProperty(PropertyEvent);

			// Mark for preview update
			Expression->bNeedToUpdatePreview = true;

			Results.Add(FString::Printf(TEXT("%s = %s"), *PropertyName, *ValueStr));
		}
	}
	else
	{
		// Blueprint node - use pin default values
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;

		for (const auto& Pair : Values->Values)
		{
			const FString& PinName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			// Find the pin
			UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
			if (!Pin)
			{
				Results.Add(FString::Printf(TEXT("! %s: pin not found"), *PinName));
				continue;
			}

			// Convert JSON value to string
			FString ValueStr;
			if (Value->Type == EJson::String)
			{
				Value->TryGetString(ValueStr);
			}
			else if (Value->Type == EJson::Number)
			{
				double NumVal;
				Value->TryGetNumber(NumVal);
				ValueStr = FString::SanitizeFloat(NumVal);
			}
			else if (Value->Type == EJson::Boolean)
			{
				bool BoolVal;
				Value->TryGetBool(BoolVal);
				ValueStr = BoolVal ? TEXT("true") : TEXT("false");
			}
			else if (Value->Type == EJson::Array)
			{
				// Handle arrays - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)" for vectors
				const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
				if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() > 0)
				{
					if (ArrayVal->Num() == 2)
					{
						double X = 0, Y = 0;
						(*ArrayVal)[0]->TryGetNumber(X);
						(*ArrayVal)[1]->TryGetNumber(Y);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s)"),
							*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y));
					}
					else if (ArrayVal->Num() == 3)
					{
						double X = 0, Y = 0, Z = 0;
						(*ArrayVal)[0]->TryGetNumber(X);
						(*ArrayVal)[1]->TryGetNumber(Y);
						(*ArrayVal)[2]->TryGetNumber(Z);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
							*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y), *FString::SanitizeFloat(Z));
					}
					else if (ArrayVal->Num() == 4)
					{
						double A = 0, B = 0, C = 0, D = 0;
						(*ArrayVal)[0]->TryGetNumber(A);
						(*ArrayVal)[1]->TryGetNumber(B);
						(*ArrayVal)[2]->TryGetNumber(C);
						(*ArrayVal)[3]->TryGetNumber(D);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
							*FString::SanitizeFloat(A), *FString::SanitizeFloat(B),
							*FString::SanitizeFloat(C), *FString::SanitizeFloat(D));
					}
					else
					{
						TArray<FString> Elements;
						for (const TSharedPtr<FJsonValue>& Elem : *ArrayVal)
						{
							double NumVal;
							FString StrVal;
							if (Elem->TryGetNumber(NumVal))
							{
								Elements.Add(FString::SanitizeFloat(NumVal));
							}
							else if (Elem->TryGetString(StrVal))
							{
								Elements.Add(StrVal);
							}
						}
						ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
					}
				}
			}
			else if (Value->Type == EJson::Object)
			{
				// Handle objects - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)"
				const TSharedPtr<FJsonObject>* ObjVal;
				if (Value->TryGetObject(ObjVal))
				{
					TArray<FString> Parts;
					for (const auto& Field : (*ObjVal)->Values)
					{
						FString FieldValue;
						double NumVal;
						bool BoolVal;
						if (Field.Value->TryGetNumber(NumVal))
						{
							FieldValue = FString::SanitizeFloat(NumVal);
						}
						else if (Field.Value->TryGetBool(BoolVal))
						{
							FieldValue = BoolVal ? TEXT("True") : TEXT("False");
						}
						else if (Field.Value->TryGetString(FieldValue))
						{
							// Already assigned
						}
						Parts.Add(FString::Printf(TEXT("%s=%s"), *Field.Key, *FieldValue));
					}
					ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
				}
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PinName));
				continue;
			}

			// Check if we got a value
			if (ValueStr.IsEmpty())
			{
				Results.Add(FString::Printf(TEXT("! %s: could not parse value"), *PinName));
				continue;
			}

			// Set the default value
			if (Schema)
			{
				Schema->TrySetDefaultValue(*Pin, ValueStr);
			}
			else
			{
				Pin->DefaultValue = ValueStr;
			}

			Results.Add(FString::Printf(TEXT("%s = %s"), *PinName, *ValueStr));
		}
	}

	return Results;
}

UBlueprintNodeSpawner* FEditGraphTool::FindSpawnerById(const FString& SpawnerId, UEdGraph* Graph)
{
	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
	const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDatabase.GetAllActions();

	for (const auto& ActionPair : AllActions)
	{
		for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
		{
			if (!Spawner)
			{
				continue;
			}

			FString Signature = Spawner->GetSpawnerSignature().ToString();
			if (Signature.Equals(SpawnerId, ESearchCase::IgnoreCase))
			{
				return Spawner;
			}
		}
	}

	return nullptr;
}

UEdGraphNode* FEditGraphTool::SpawnNode(UBlueprintNodeSpawner* Spawner, UEdGraph* Graph, const FVector2D& Position)
{
	if (!Spawner || !Graph)
	{
		return nullptr;
	}

	IBlueprintNodeBinder::FBindingSet Bindings;
	UEdGraphNode* NewNode = Spawner->Invoke(Graph, Bindings, Position);

	if (NewNode)
	{
		// Ensure pins are allocated
		if (NewNode->Pins.Num() == 0)
		{
			NewNode->AllocateDefaultPins();
		}
	}

	return NewNode;
}

TArray<FString> FEditGraphTool::SetPinValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& PinValues)
{
	TArray<FString> Results;

	if (!Node || !PinValues.IsValid())
	{
		return Results;
	}

	const UEdGraphSchema* Schema = Node->GetGraph()->GetSchema();

	for (const auto& Pair : PinValues->Values)
	{
		const FString& PinName = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Find the pin
		UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
		if (!Pin)
		{
			Results.Add(FString::Printf(TEXT("! Pin not found: %s"), *PinName));
			continue;
		}

		// Convert JSON value to string
		FString ValueStr;
		if (Value->Type == EJson::String)
		{
			Value->TryGetString(ValueStr);
		}
		else if (Value->Type == EJson::Number)
		{
			double NumVal;
			Value->TryGetNumber(NumVal);
			ValueStr = FString::SanitizeFloat(NumVal);
		}
		else if (Value->Type == EJson::Boolean)
		{
			bool BoolVal;
			Value->TryGetBool(BoolVal);
			ValueStr = BoolVal ? TEXT("true") : TEXT("false");
		}
		else if (Value->Type == EJson::Array)
		{
			// Handle arrays - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)" for vectors
			const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
			if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() > 0)
			{
				// Check array size to determine struct type
				if (ArrayVal->Num() == 2)
				{
					// Vector2D format: (X=val,Y=val)
					double X = 0, Y = 0;
					(*ArrayVal)[0]->TryGetNumber(X);
					(*ArrayVal)[1]->TryGetNumber(Y);
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s)"),
						*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y));
				}
				else if (ArrayVal->Num() == 3)
				{
					// Vector format: (X=val,Y=val,Z=val)
					double X = 0, Y = 0, Z = 0;
					(*ArrayVal)[0]->TryGetNumber(X);
					(*ArrayVal)[1]->TryGetNumber(Y);
					(*ArrayVal)[2]->TryGetNumber(Z);
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
						*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y), *FString::SanitizeFloat(Z));
				}
				else if (ArrayVal->Num() == 4)
				{
					// Vector4/Rotator/Color format: (X=val,Y=val,Z=val,W=val) or (R=val,G=val,B=val,A=val)
					double A = 0, B = 0, C = 0, D = 0;
					(*ArrayVal)[0]->TryGetNumber(A);
					(*ArrayVal)[1]->TryGetNumber(B);
					(*ArrayVal)[2]->TryGetNumber(C);
					(*ArrayVal)[3]->TryGetNumber(D);
					// Use XYZW for Vector4, could also be RGBA for colors
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
						*FString::SanitizeFloat(A), *FString::SanitizeFloat(B),
						*FString::SanitizeFloat(C), *FString::SanitizeFloat(D));
				}
				else
				{
					// Generic array - just join with commas
					TArray<FString> Elements;
					for (const TSharedPtr<FJsonValue>& Elem : *ArrayVal)
					{
						double NumVal;
						FString StrVal;
						if (Elem->TryGetNumber(NumVal))
						{
							Elements.Add(FString::SanitizeFloat(NumVal));
						}
						else if (Elem->TryGetString(StrVal))
						{
							Elements.Add(StrVal);
						}
					}
					ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
				}
			}
		}
		else if (Value->Type == EJson::Object)
		{
			// Handle objects - convert to UE struct format
			const TSharedPtr<FJsonObject>* ObjVal;
			if (Value->TryGetObject(ObjVal))
			{
				TArray<FString> Parts;
				for (const auto& Field : (*ObjVal)->Values)
				{
					FString FieldValue;
					double NumVal;
					bool BoolVal;
					if (Field.Value->TryGetNumber(NumVal))
					{
						FieldValue = FString::SanitizeFloat(NumVal);
					}
					else if (Field.Value->TryGetBool(BoolVal))
					{
						FieldValue = BoolVal ? TEXT("True") : TEXT("False");
					}
					else if (Field.Value->TryGetString(FieldValue))
					{
						// Already assigned
					}
					Parts.Add(FString::Printf(TEXT("%s=%s"), *Field.Key, *FieldValue));
				}
				ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
			}
		}
		else
		{
			Results.Add(FString::Printf(TEXT("! Unsupported value type for pin: %s"), *PinName));
			continue;
		}

		// Check if we actually got a value
		if (ValueStr.IsEmpty())
		{
			Results.Add(FString::Printf(TEXT("! Could not parse value for pin: %s"), *PinName));
			continue;
		}

		// Special handling for Class and Object pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			// For Class pins, we need to load the UClass and set DefaultObject
			// ValueStr can be:
			// - Short name like "BP_Enemy" (we'll search for it)
			// - Full path like "/Game/Blueprints/BP_Enemy.BP_Enemy_C"
			// - Blueprint path like "/Game/Blueprints/BP_Enemy" (we append _C)

			UClass* FoundClass = nullptr;

			// Try loading as full path first
			if (ValueStr.StartsWith(TEXT("/")))
			{
				// Check if it's a Blueprint path without _C suffix
				FString ClassPath = ValueStr;
				if (!ClassPath.EndsWith(TEXT("_C")))
				{
					// Append the class suffix for Blueprints
					int32 LastDot = ClassPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (LastDot != INDEX_NONE)
					{
						ClassPath = ClassPath + TEXT("_C");
					}
					else
					{
						// No dot - add one with the asset name + _C
						int32 LastSlash = ClassPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						FString AssetName = ClassPath.Mid(LastSlash + 1);
						ClassPath = ClassPath + TEXT(".") + AssetName + TEXT("_C");
					}
				}
				FoundClass = LoadClass<UObject>(nullptr, *ClassPath);

				// If that failed, try the original path
				if (!FoundClass)
				{
					FoundClass = LoadClass<UObject>(nullptr, *ValueStr);
				}
			}
			else
			{
				// Short name - search in /Game/ for a matching Blueprint
				FString SearchPath = FString::Printf(TEXT("/Game/%s.%s_C"), *ValueStr, *ValueStr);
				FoundClass = LoadClass<UObject>(nullptr, *SearchPath);

				// Try common Blueprint paths
				if (!FoundClass)
				{
					TArray<FString> SearchPaths = {
						FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *ValueStr, *ValueStr),
						FString::Printf(TEXT("/Game/AI/%s.%s_C"), *ValueStr, *ValueStr),
						FString::Printf(TEXT("/Game/Characters/%s.%s_C"), *ValueStr, *ValueStr),
					};

					for (const FString& Path : SearchPaths)
					{
						FoundClass = LoadClass<UObject>(nullptr, *Path);
						if (FoundClass)
						{
							break;
						}
					}
				}

				// Also try loading as an engine class
				if (!FoundClass)
				{
					FString EngineClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ValueStr);
					FoundClass = LoadClass<UObject>(nullptr, *EngineClassPath);
				}
			}

			if (FoundClass)
			{
				// Check if the class is compatible with the pin's expected base class
				UClass* PinBaseClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
				if (PinBaseClass && !FoundClass->IsChildOf(PinBaseClass))
				{
					Results.Add(FString::Printf(TEXT("! Class %s is not a subclass of %s for pin: %s"),
						*FoundClass->GetName(), *PinBaseClass->GetName(), *PinName));
					continue;
				}

				Pin->DefaultObject = FoundClass;
				Results.Add(FString::Printf(TEXT("%s = %s (class)"), *PinName, *FoundClass->GetPathName()));
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! Could not find class for pin %s: %s"), *PinName, *ValueStr));
			}
			continue;
		}
		else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		         Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			// For Object pins, we need to load the UObject and set DefaultObject
			// This is typically used for asset references
			UObject* FoundObject = LoadObject<UObject>(nullptr, *ValueStr);
			if (FoundObject)
			{
				Pin->DefaultObject = FoundObject;
				Results.Add(FString::Printf(TEXT("%s = %s (object)"), *PinName, *FoundObject->GetPathName()));
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! Could not find object for pin %s: %s"), *PinName, *ValueStr));
			}
			continue;
		}

		// Try to set the default value for other pin types
		if (Schema)
		{
			Schema->TrySetDefaultValue(*Pin, ValueStr);
		}
		else
		{
			Pin->DefaultValue = ValueStr;
		}

		Results.Add(FString::Printf(TEXT("%s = %s"), *PinName, *ValueStr));
	}

	return Results;
}

UEdGraphNode* FEditGraphTool::ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph, const FString& AssetPath,
                                              const TMap<FString, UEdGraphNode*>& NewNodes)
{
	if (NodeRef.IsEmpty() || !Graph)
	{
		return nullptr;
	}

	// 1. Check new nodes from this call
	if (const UEdGraphNode* const* Found = NewNodes.Find(NodeRef))
	{
		return const_cast<UEdGraphNode*>(*Found);
	}

	// 2. Check session registry
	FGuid RegisteredGuid = FNodeNameRegistry::Get().Resolve(AssetPath, Graph->GetName(), NodeRef);
	if (RegisteredGuid.IsValid())
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == RegisteredGuid)
			{
				return Node;
			}
		}
	}

	// 3. Try parsing as raw GUID
	FGuid DirectGuid;
	if (FGuid::Parse(NodeRef, DirectGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == DirectGuid)
			{
				return Node;
			}
		}
	}

	return nullptr;
}

UEdGraphPin* FEditGraphTool::FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	// Helper lambda to check if pin is connectable
	auto IsPinConnectable = [](UEdGraphPin* Pin) -> bool
	{
		return Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin;
	};

	// First try exact match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
			Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// Try friendly name match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
			Pin->PinFriendlyName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// For exec pins, try common aliases
	if (PinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("in"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	if (PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("out"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

FString FEditGraphTool::ListAvailablePins(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return TEXT("(node is null)");
	}

	TArray<FString> PinNames;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden || Pin->bNotConnectable || Pin->bOrphanedPin)
		{
			continue;
		}
		if (Pin->Direction != Direction)
		{
			continue;
		}

		// Build pin description: "PinName (Type)"
		FString TypeStr = Pin->PinType.PinCategory.ToString();
		if (Pin->PinType.PinSubCategoryObject.IsValid())
		{
			TypeStr = Pin->PinType.PinSubCategoryObject->GetName();
		}
		if (Pin->PinType.ContainerType == EPinContainerType::Array)
		{
			TypeStr = FString::Printf(TEXT("Array<%s>"), *TypeStr);
		}

		PinNames.Add(FString::Printf(TEXT("%s (%s)"), *Pin->PinName.ToString(), *TypeStr));
	}

	if (PinNames.Num() == 0)
	{
		return TEXT("(no connectable pins)");
	}

	return FString::Join(PinNames, TEXT(", "));
}

bool FEditGraphTool::ValidateConnectionPrerequisites(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	if (!FromPin || !ToPin)
	{
		OutError = TEXT("Invalid pins");
		return false;
	}

	// Check pin directions
	if (FromPin->Direction != EGPD_Output)
	{
		OutError = TEXT("Source pin must be an output pin");
		return false;
	}
	if (ToPin->Direction != EGPD_Input)
	{
		OutError = TEXT("Target pin must be an input pin");
		return false;
	}

	// Check if nodes are in the same graph
	UEdGraphNode* FromNode = FromPin->GetOwningNode();
	UEdGraphNode* ToNode = ToPin->GetOwningNode();
	if (!FromNode || !ToNode)
	{
		OutError = TEXT("Could not get owning nodes");
		return false;
	}
	if (FromNode->GetGraph() != ToNode->GetGraph())
	{
		OutError = TEXT("Cannot connect nodes from different graphs");
		return false;
	}

	// Check if already connected
	if (FromPin->LinkedTo.Contains(ToPin))
	{
		// Already connected - not an error, but caller should know
		OutError = TEXT("Already connected");
		return true;
	}

	// Check execution pin uniqueness - exec output pins can only have ONE connection
	if (FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
		FromPin->Direction == EGPD_Output &&
		FromPin->LinkedTo.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Exec output pin '%s' already has a connection (exec pins can only have one outgoing connection)"),
			*FromPin->PinName.ToString());
		return false;
	}

	return true;
}

FEditGraphTool::FConnectionResult FEditGraphTool::CreateConnectionWithFallback(UEdGraphPin* FromPin, UEdGraphPin* ToPin)
{
	FConnectionResult Result;

	// Validate prerequisites
	FString ValidationError;
	if (!ValidateConnectionPrerequisites(FromPin, ToPin, ValidationError))
	{
		// Check if it's the "already connected" case
		if (ValidationError == TEXT("Already connected"))
		{
			Result.bSuccess = true;
			Result.Type = EConnectionResultType::Direct;
			Result.Details = TEXT("already connected");
			return Result;
		}
		Result.Error = ValidationError;
		return Result;
	}

	// Get schema from graph
	UEdGraph* Graph = FromPin->GetOwningNode()->GetGraph();
	if (!Graph)
	{
		Result.Error = TEXT("Could not get graph from node");
		return Result;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		Result.Error = TEXT("Could not get schema from graph");
		return Result;
	}

	// Check what type of connection is possible
	FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);

	switch (Response.Response.GetValue())
	{
		case CONNECT_RESPONSE_MAKE:
		{
			// Direct connection - types are compatible
			if (Schema->TryCreateConnection(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Direct;
				Result.Details = TEXT("direct");
			}
			else
			{
				Result.Error = TEXT("TryCreateConnection failed unexpectedly");
			}
			break;
		}

		case CONNECT_RESPONSE_MAKE_WITH_PROMOTION:
		{
			// Type promotion needed (e.g., float to double, int to int64)
			// The schema will handle the promotion automatically
			if (Schema->CreatePromotedConnection(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Promoted;
				Result.Details = FString::Printf(TEXT("promoted %s to %s"),
					*FromPin->PinType.PinCategory.ToString(),
					*ToPin->PinType.PinCategory.ToString());
				UE_LOG(LogTemp, Log, TEXT("NeoStack: Connection with promotion: %s.%s -> %s.%s (%s)"),
					*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(),
					*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(),
					*Result.Details);
			}
			else
			{
				Result.Error = FString::Printf(TEXT("Type promotion failed: %s"), *Response.Message.ToString());
			}
			break;
		}

		case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
		{
			// Need to insert a conversion node (e.g., int to string, vector to text)
			if (Schema->CreateAutomaticConversionNodeAndConnections(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Converted;
				Result.Details = FString::Printf(TEXT("auto-inserted conversion node for %s to %s"),
					*FromPin->PinType.PinCategory.ToString(),
					*ToPin->PinType.PinCategory.ToString());
				UE_LOG(LogTemp, Log, TEXT("NeoStack: Connection with conversion node: %s.%s -> %s.%s (%s)"),
					*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(),
					*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(),
					*Result.Details);
			}
			else
			{
				Result.Error = FString::Printf(TEXT("Failed to create conversion node: %s"), *Response.Message.ToString());
			}
			break;
		}

		case CONNECT_RESPONSE_DISALLOW:
		default:
		{
			// Build detailed type information for debugging
			FString FromTypeStr = FromPin->PinType.PinCategory.ToString();
			FString ToTypeStr = ToPin->PinType.PinCategory.ToString();

			// Add subtype info for object/struct types
			if (FromPin->PinType.PinSubCategoryObject.IsValid())
			{
				FromTypeStr = FromPin->PinType.PinSubCategoryObject->GetName();
			}
			if (ToPin->PinType.PinSubCategoryObject.IsValid())
			{
				ToTypeStr = ToPin->PinType.PinSubCategoryObject->GetName();
			}

			// Add container type if present
			if (FromPin->PinType.ContainerType == EPinContainerType::Array)
			{
				FromTypeStr = FString::Printf(TEXT("Array<%s>"), *FromTypeStr);
			}
			if (ToPin->PinType.ContainerType == EPinContainerType::Array)
			{
				ToTypeStr = FString::Printf(TEXT("Array<%s>"), *ToTypeStr);
			}

			Result.Error = FString::Printf(
				TEXT("Cannot connect %s:%s (%s) -> %s:%s (%s). %s"),
				*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(), *FromTypeStr,
				*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(), *ToTypeStr,
				*Response.Message.ToString());
			break;
		}
	}

	return Result;
}

bool FEditGraphTool::CreateConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	// Use the new fallback system
	FConnectionResult Result = CreateConnectionWithFallback(FromPin, ToPin);
	if (!Result.bSuccess)
	{
		OutError = Result.Error;
		return false;
	}
	return true;
}

bool FEditGraphTool::BreakConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	if (!FromPin || !ToPin)
	{
		OutError = TEXT("Invalid pins");
		return false;
	}

	// Check if they're actually connected
	if (!FromPin->LinkedTo.Contains(ToPin))
	{
		OutError = TEXT("Pins are not connected");
		return false;
	}

	// Break the link
	FromPin->BreakLinkTo(ToPin);

	return true;
}

bool FEditGraphTool::BreakAllConnections(UEdGraphPin* Pin, FString& OutError)
{
	if (!Pin)
	{
		OutError = TEXT("Invalid pin");
		return false;
	}

	if (Pin->LinkedTo.Num() == 0)
	{
		// Not an error, just nothing to break
		return true;
	}

	// Break all links
	Pin->BreakAllPinLinks(true);

	return true;
}

UEdGraph* FEditGraphTool::GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// If no name specified, return the main event graph
	if (GraphName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search UbergraphPages
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Search FunctionGraphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Search MacroGraphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
}

FString FEditGraphTool::GetNodeTypeName(UEdGraphNode* Node) const
{
	if (!Node)
	{
		return TEXT("Unknown");
	}

	// Try to get a nice title
	FText Title = Node->GetNodeTitle(ENodeTitleType::MenuTitle);
	if (!Title.IsEmpty())
	{
		return Title.ToString();
	}

	// Fall back to class name
	return Node->GetClass()->GetName();
}

FVector2D FEditGraphTool::CalculateSmartPosition(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& NewNodesThisCall) const
{
	if (!Graph)
	{
		return FVector2D(0, 0);
	}

	// Constants for layout (width is typically consistent, height varies by pins)
	const float DefaultNodeWidth = 250.0f;   // Typical Blueprint node width
	const float DefaultNodeHeight = 100.0f;  // Fallback if estimation fails
	const float SpacingX = 50.0f;            // Horizontal spacing between nodes
	const float SpacingY = 30.0f;            // Vertical spacing between nodes

	// Helper to get node dimensions
	auto GetNodeBounds = [DefaultNodeWidth, DefaultNodeHeight](UEdGraphNode* Node) -> FVector2D
	{
		if (!Node)
		{
			return FVector2D(DefaultNodeWidth, DefaultNodeHeight);
		}

		// Use UE's built-in height estimation for K2 (Blueprint) nodes
		float Height = UEdGraphSchema_K2::EstimateNodeHeight(Node);
		if (Height <= 0.0f)
		{
			Height = DefaultNodeHeight;
		}

		// Width: use node's stored width if available, otherwise default
		float Width = Node->NodeWidth > 0 ? static_cast<float>(Node->NodeWidth) : DefaultNodeWidth;

		return FVector2D(Width, Height);
	};

	// Collect all existing node bounds
	TArray<FBox2D> ExistingBounds;
	float MaxX = 0.0f;
	float MinY = 0.0f;
	float MaxY = 0.0f;
	bool bHasNodes = false;

	// Include existing nodes in graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			float X = Node->NodePosX;
			float Y = Node->NodePosY;
			FVector2D Size = GetNodeBounds(Node);
			FBox2D Bounds(FVector2D(X, Y), FVector2D(X + Size.X, Y + Size.Y));
			ExistingBounds.Add(Bounds);

			if (!bHasNodes)
			{
				MaxX = X + Size.X;
				MinY = Y;
				MaxY = Y + Size.Y;
				bHasNodes = true;
			}
			else
			{
				MaxX = FMath::Max(MaxX, X + Size.X);
				MinY = FMath::Min(MinY, Y);
				MaxY = FMath::Max(MaxY, Y + Size.Y);
			}
		}
	}

	// Include nodes added in this call
	for (const auto& Pair : NewNodesThisCall)
	{
		if (UEdGraphNode* Node = Pair.Value)
		{
			float X = Node->NodePosX;
			float Y = Node->NodePosY;
			FVector2D Size = GetNodeBounds(Node);
			FBox2D Bounds(FVector2D(X, Y), FVector2D(X + Size.X, Y + Size.Y));
			ExistingBounds.Add(Bounds);

			MaxX = FMath::Max(MaxX, X + Size.X);
			MinY = FMath::Min(MinY, Y);
			MaxY = FMath::Max(MaxY, Y + Size.Y);
		}
	}

	// If no existing nodes, start at origin
	if (!bHasNodes && NewNodesThisCall.Num() == 0)
	{
		return FVector2D(0, 0);
	}

	// Try to place to the right of existing nodes
	FVector2D CandidatePos(MaxX + SpacingX, MinY);

	// Use a reasonable default size for overlap checking (new node size unknown yet)
	const float NewNodeWidth = DefaultNodeWidth;
	const float NewNodeHeight = DefaultNodeHeight;

	// Check for overlap and adjust Y if needed
	auto DoesOverlap = [&ExistingBounds, NewNodeWidth, NewNodeHeight](const FVector2D& Pos) -> bool
	{
		FBox2D NewBounds(Pos, FVector2D(Pos.X + NewNodeWidth, Pos.Y + NewNodeHeight));
		for (const FBox2D& Existing : ExistingBounds)
		{
			// Check if bounds intersect
			if (!(NewBounds.Max.X < Existing.Min.X || NewBounds.Min.X > Existing.Max.X ||
				  NewBounds.Max.Y < Existing.Min.Y || NewBounds.Min.Y > Existing.Max.Y))
			{
				return true;
			}
		}
		return false;
	};

	// If overlapping, try different Y positions
	int32 MaxAttempts = 20;
	float YOffset = 0.0f;
	while (DoesOverlap(CandidatePos) && MaxAttempts > 0)
	{
		YOffset += NewNodeHeight + SpacingY;
		CandidatePos.Y = MinY + YOffset;
		MaxAttempts--;
	}

	// If still overlapping after attempts, just place further right
	if (MaxAttempts == 0)
	{
		CandidatePos = FVector2D(MaxX + SpacingX + NewNodeWidth, MinY);
	}

	return CandidatePos;
}

FString FEditGraphTool::FormatResults(const FString& AssetName, const FString& GraphName,
                                       const TArray<FAddedNode>& AddedNodes,
                                       const TArray<FString>& Connections,
                                       const TArray<FString>& Disconnections,
                                       const TArray<FString>& SetPinsResults,
                                       const TArray<FString>& Errors) const
{
	FString Output;

	// Header
	Output += FString::Printf(TEXT("# EDIT GRAPH: %s\n"), *AssetName);
	Output += FString::Printf(TEXT("Graph: %s\n\n"), *GraphName);

	// Added nodes
	if (AddedNodes.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Added Nodes (%d)\n\n"), AddedNodes.Num());

		for (const FAddedNode& Node : AddedNodes)
		{
			Output += FString::Printf(TEXT("+ %s (%s) at (%.0f, %.0f)\n"),
				*Node.Name, *Node.NodeType, Node.Position.X, Node.Position.Y);
			Output += FString::Printf(TEXT("  GUID: %s\n"), *Node.Guid.ToString());

			// Show available pins for connections
			if (Node.OutputPins.Num() > 0)
			{
				Output += FString::Printf(TEXT("  Out: %s\n"), *FString::Join(Node.OutputPins, TEXT(", ")));
			}
			if (Node.InputPins.Num() > 0)
			{
				Output += FString::Printf(TEXT("  In: %s\n"), *FString::Join(Node.InputPins, TEXT(", ")));
			}

			for (const FString& PinVal : Node.PinValues)
			{
				Output += FString::Printf(TEXT("  - %s\n"), *PinVal);
			}
		}
	}

	// Connections
	if (Connections.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Connections (%d)\n\n"), Connections.Num());

		for (const FString& Conn : Connections)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Conn);
		}
		Output += TEXT("\n");
	}

	// Disconnections
	if (Disconnections.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Disconnections (%d)\n\n"), Disconnections.Num());

		for (const FString& Disconn : Disconnections)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Disconn);
		}
		Output += TEXT("\n");
	}

	// Set pins results
	if (SetPinsResults.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Values Set (%d)\n\n"), SetPinsResults.Num());

		for (const FString& Result : SetPinsResults)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Result);
		}
		Output += TEXT("\n");
	}

	// Errors
	if (Errors.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Errors (%d)\n\n"), Errors.Num());

		for (const FString& Err : Errors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *Err);
		}
		Output += TEXT("\n");
	}

	// Summary
	Output += FString::Printf(TEXT("= %d nodes added, %d connections, %d disconnections, %d values set"),
		AddedNodes.Num(), Connections.Num(), Disconnections.Num(), SetPinsResults.Num());

	if (Errors.Num() > 0)
	{
		Output += FString::Printf(TEXT(", %d errors"), Errors.Num());
	}

	Output += TEXT("\n");

	return Output;
}
