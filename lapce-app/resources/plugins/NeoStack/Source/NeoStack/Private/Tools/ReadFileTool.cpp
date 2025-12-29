// Copyright NeoStack. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Composite.h"
#include "K2Node_MacroInstance.h"
#include "Materials/Material.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

// For accessing Material Editor preview material
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"

// For Widget Blueprint support
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Widgets/Layout/Anchors.h"
#include "WidgetBlueprintEditor.h"

// For Animation Blueprint support
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"

// For Behavior Tree and Blackboard support
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"

// For User Defined Struct/Enum and DataTable support
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/DataTable.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"

FToolResult FReadFileTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path, GraphName;
	int32 Offset = 1;
	int32 Limit = 100;
	TArray<FString> Include;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("graph"), GraphName);
	Args->TryGetNumberField(TEXT("offset"), Offset);
	Args->TryGetNumberField(TEXT("limit"), Limit);

	// Parse include array
	const TArray<TSharedPtr<FJsonValue>>* IncludeArray;
	if (Args->TryGetArrayField(TEXT("include"), IncludeArray))
	{
		for (const auto& Val : *IncludeArray)
		{
			FString IncludeItem;
			if (Val->TryGetString(IncludeItem))
			{
				Include.Add(IncludeItem.ToLower());
			}
		}
	}

	// Default include if not specified
	if (Include.Num() == 0)
	{
		Include.Add(TEXT("summary"));
	}

	// Clamp values
	Offset = FMath::Max(1, Offset);
	Limit = FMath::Clamp(Limit, 1, 1000);

	// Route based on path type
	if (!NeoStackToolUtils::IsAssetPath(Name, Path))
	{
		return ReadTextFile(Name, Path, Offset, Limit);
	}

	// Load as generic UObject first (like FindNodeTool does)
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Collect graphs and metadata based on asset type
	TArray<TPair<UEdGraph*, FString>> Graphs; // Graph + Type
	FString AssetType;
	FString Summary;

	// Check for Animation Blueprint FIRST (it inherits from UBlueprint)
	if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset))
	{
		AssetType = TEXT("AnimBlueprint");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetAnimBlueprintSummary(AnimBlueprint);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(AnimBlueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("statemachines")) || Include.Contains(TEXT("states")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetAnimBlueprintStateMachines(AnimBlueprint);
		}

		// Collect standard graphs
		for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}

		// Collect AnimGraph and state machine graphs as subgraphs
		CollectAnimBlueprintGraphs(AnimBlueprint, Graphs);
	}
	// Check for Widget Blueprint (it inherits from UBlueprint)
	else if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Asset))
	{
		AssetType = TEXT("WidgetBlueprint");

		// Build summary with widget tree
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetWidgetBlueprintSummary(WidgetBlueprint);
		}
		if (Include.Contains(TEXT("widgets")) || Include.Contains(TEXT("tree")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetWidgetTree(WidgetBlueprint);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(WidgetBlueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("interfaces")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintInterfaces(WidgetBlueprint);
		}

		// Collect graphs (Widget Blueprints have event graphs too)
		for (UEdGraph* Graph : WidgetBlueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : WidgetBlueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}
	}
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		AssetType = TEXT("Blueprint");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetBlueprintSummary(Blueprint);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(Blueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("components")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintComponents(Blueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("interfaces")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintInterfaces(Blueprint);
		}

		// Collect graphs
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("macro")));
		}
	}
	else if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		AssetType = TEXT("Material");

		// CRITICAL: When the Material Editor is open, it works on a PREVIEW COPY of the material.
		// We must read from the preview material to see live changes, not the original.
		UMaterial* WorkingMaterial = Material;

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
							UE_LOG(LogTemp, Log, TEXT("NeoStack ReadFile: Using preview material from Material Editor"));
						}
					}
				}
			}
		}

		// Create MaterialGraph if it doesn't exist
		if (!WorkingMaterial->MaterialGraph)
		{
			WorkingMaterial->MaterialGraph = CastChecked<UMaterialGraph>(
				FBlueprintEditorUtils::CreateNewGraph(WorkingMaterial, NAME_None,
					UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
			WorkingMaterial->MaterialGraph->Material = WorkingMaterial;
			WorkingMaterial->MaterialGraph->RebuildGraph();
		}

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = FString::Printf(TEXT("# MATERIAL %s\n"), *WorkingMaterial->GetName());
			Summary += FString::Printf(TEXT("BlendMode: %d\n"), (int32)WorkingMaterial->BlendMode);
			Summary += FString::Printf(TEXT("ShadingModel: %d\n"), (int32)WorkingMaterial->GetShadingModels().GetFirstShadingModel());
			Summary += FString::Printf(TEXT("TwoSided: %s\n"), WorkingMaterial->IsTwoSided() ? TEXT("true") : TEXT("false"));
			Summary += FString::Printf(TEXT("Expressions: %d\n"), WorkingMaterial->GetExpressions().Num());
		}

		// Collect graph
		if (WorkingMaterial->MaterialGraph)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(WorkingMaterial->MaterialGraph, TEXT("material")));
		}
	}
	else if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Asset))
	{
		AssetType = TEXT("BehaviorTree");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetBehaviorTreeSummary(BehaviorTree);
		}
		if (Include.Contains(TEXT("nodes")) || Include.Contains(TEXT("tree")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBehaviorTreeNodes(BehaviorTree);
		}

		// BTs don't have traditional graphs, output is in Summary
		// Return early with just the summary
		if (Summary.IsEmpty())
		{
			Summary = FString::Printf(TEXT("# BEHAVIOR_TREE %s (no data)\n"), *BehaviorTree->GetName());
		}
		return FToolResult::Ok(Summary);
	}
	else if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
	{
		AssetType = TEXT("Blackboard");

		// Build summary with keys
		Summary = GetBlackboardSummary(Blackboard);

		if (Include.Contains(TEXT("keys")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlackboardKeys(Blackboard);
		}

		// Return early - Blackboards don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(Asset))
	{
		AssetType = TEXT("Struct");

		// Build summary with fields
		Summary = GetStructSummary(UserStruct);

		if (Include.Contains(TEXT("fields")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetStructFields(UserStruct);
		}

		// Return early - Structs don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(Asset))
	{
		AssetType = TEXT("Enum");

		// Build summary with values
		Summary = GetEnumSummary(UserEnum);

		if (Include.Contains(TEXT("values")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetEnumValues(UserEnum);
		}

		// Return early - Enums don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		AssetType = TEXT("DataTable");

		// Build summary
		Summary = GetDataTableSummary(DataTable);

		if (Include.Contains(TEXT("rows")) || Include.Contains(TEXT("data")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetDataTableRows(DataTable, Offset, Limit);
		}

		// Return early - DataTables don't have graphs
		return FToolResult::Ok(Summary);
	}
	else
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName()));
	}

	// If specific graph requested, find and return just that one
	if (!GraphName.IsEmpty())
	{
		for (const auto& GraphPair : Graphs)
		{
			if (GraphPair.Key->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				FString Output = GetGraphWithNodes(GraphPair.Key, GraphPair.Value, TEXT(""), Offset, Limit);
				Output += TEXT("\n") + GetGraphConnections(GraphPair.Key);
				return FToolResult::Ok(Output);
			}
		}
		return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Build output
	FString Output = Summary;

	// Add graphs if requested
	if (Include.Contains(TEXT("graphs")) || Include.Contains(TEXT("graph")))
	{
		for (const auto& GraphPair : Graphs)
		{
			if (!Output.IsEmpty()) Output += TEXT("\n");
			Output += GetGraphWithNodes(GraphPair.Key, GraphPair.Value, TEXT(""), Offset, Limit);
			Output += TEXT("\n") + GetGraphConnections(GraphPair.Key);
		}
	}

	if (Output.IsEmpty())
	{
		Output = FString::Printf(TEXT("# %s %s (no data)\n"), *AssetType, *Asset->GetName());
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadTextFile(const FString& Name, const FString& Path, int32 Offset, int32 Limit)
{
	FString FullPath = NeoStackToolUtils::BuildFilePath(Name, Path);

	// Check if file exists
	if (!FPaths::FileExists(FullPath))
	{
		return FToolResult::Fail(FString::Printf(TEXT("File not found: %s"), *FullPath));
	}

	// Read file
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FullPath))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to read file: %s"), *FullPath));
	}

	// Split into lines
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);

	int32 TotalLines = Lines.Num();
	int32 StartIndex = Offset - 1; // Convert to 0-based
	int32 EndIndex = FMath::Min(StartIndex + Limit, TotalLines);

	if (StartIndex >= TotalLines)
	{
		return FToolResult::Ok(FString::Printf(TEXT("# FILE %s lines=%d offset=%d beyond_end"), *Name, TotalLines, Offset));
	}

	// Build output
	FString Output = FString::Printf(TEXT("# FILE %s lines=%d-%d/%d\n"), *Name, Offset, EndIndex, TotalLines);

	for (int32 i = StartIndex; i < EndIndex; i++)
	{
		Output += FString::Printf(TEXT("%d\t%s\n"), i + 1, *Lines[i]);
	}

	return FToolResult::Ok(Output);
}

FString FReadFileTool::GetBlueprintSummary(UBlueprint* Blueprint)
{
	FString ParentName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");

	int32 ComponentCount = 0;
	if (Blueprint->SimpleConstructionScript)
	{
		ComponentCount = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
	}
	int32 VarCount = Blueprint->NewVariables.Num();
	int32 GraphCount = Blueprint->UbergraphPages.Num() + Blueprint->FunctionGraphs.Num() + Blueprint->MacroGraphs.Num();

	FString Output = FString::Printf(TEXT("# BLUEPRINT %s parent=%s\ncomponents=%d variables=%d graphs=%d\n"),
		*Blueprint->GetName(), *ParentName, ComponentCount, VarCount, GraphCount);

	// Add graph list
	Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		Output += FString::Printf(TEXT("%s\tmacro\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}

	return Output;
}

FString FReadFileTool::GetBlueprintVariables(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	const TArray<FBPVariableDescription>& Vars = Blueprint->NewVariables;
	int32 Total = Vars.Num();

	if (Total == 0)
	{
		return TEXT("# VARIABLES 0\n");
	}

	int32 StartIdx = Offset - 1;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# VARIABLES %d\n"), Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FBPVariableDescription& Var = Vars[i];

		// Get type name
		FString TypeName = Var.VarType.PinCategory.ToString();
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			TypeName = Var.VarType.PinSubCategoryObject->GetName();
		}

		// Get default value
		FString DefaultValue = Var.DefaultValue.IsEmpty() ? TEXT("None") : Var.DefaultValue;

		Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *Var.VarName.ToString(), *TypeName, *DefaultValue);
	}

	return Output;
}

FString FReadFileTool::GetBlueprintComponents(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	if (!Blueprint->SimpleConstructionScript)
	{
		return TEXT("# COMPONENTS 0\n");
	}

	const TArray<USCS_Node*>& Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
	int32 Total = Nodes.Num();

	if (Total == 0)
	{
		return TEXT("# COMPONENTS 0\n");
	}

	int32 StartIdx = Offset - 1;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# COMPONENTS %d\n"), Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		USCS_Node* Node = Nodes[i];
		if (Node && Node->ComponentTemplate)
		{
			FString ParentName = TEXT("ROOT");
			if (Node->ParentComponentOrVariableName != NAME_None)
			{
				ParentName = Node->ParentComponentOrVariableName.ToString();
			}

			Output += FString::Printf(TEXT("%s\t%s\t%s\n"),
				*Node->GetVariableName().ToString(),
				*Node->ComponentTemplate->GetClass()->GetName(),
				*ParentName);
		}
	}

	return Output;
}

FString FReadFileTool::GetBlueprintGraphs(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	FString Output;

	// Collect all graphs with nodes and connections
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		Output += GetGraphWithNodes(Graph, TEXT("ubergraph"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		Output += GetGraphWithNodes(Graph, TEXT("function"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		Output += GetGraphWithNodes(Graph, TEXT("macro"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	return Output;
}

FString FReadFileTool::GetBlueprintInterfaces(UBlueprint* Blueprint)
{
	int32 Total = Blueprint->ImplementedInterfaces.Num();

	if (Total == 0)
	{
		return TEXT("# INTERFACES 0\n");
	}

	FString Output = FString::Printf(TEXT("# INTERFACES %d\n"), Total);

	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		if (Interface.Interface)
		{
			Output += FString::Printf(TEXT("%s\n"), *Interface.Interface->GetName());
		}
	}

	return Output;
}

FString FReadFileTool::GetGraphType(UEdGraph* Graph, UBlueprint* Blueprint)
{
	return NeoStackToolUtils::GetGraphType(Graph, Blueprint);
}

FString FReadFileTool::GetGraphWithNodes(UEdGraph* Graph, const FString& GraphType, const FString& ParentGraph, int32 Offset, int32 Limit)
{
	int32 Total = Graph->Nodes.Num();
	int32 StartIdx = Offset - 1;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	// Build header
	FString Output;
	if (ParentGraph.IsEmpty())
	{
		Output = FString::Printf(TEXT("# GRAPH %s type=%s %d\n"), *Graph->GetName(), *GraphType, Total);
	}
	else
	{
		Output = FString::Printf(TEXT("# GRAPH %s type=%s parent=%s %d\n"), *Graph->GetName(), *GraphType, *ParentGraph, Total);
	}

	if (Total == 0)
	{
		return Output;
	}

	// Output nodes: guid, title, pins
	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		UEdGraphNode* Node = Graph->Nodes[i];
		if (!Node) continue;

		FString NodeGuid = NeoStackToolUtils::GetNodeGuid(Node);
		FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		FString PinNames = GetNodePins(Node);

		Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *NodeGuid, *NodeTitle, *PinNames);
	}

	return Output;
}

FString FReadFileTool::GetGraphConnections(UEdGraph* Graph)
{
	TArray<FString> Connections;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		FString FromGuid = NeoStackToolUtils::GetNodeGuid(Node);

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (LinkedNode)
					{
						FString ToGuid = NeoStackToolUtils::GetNodeGuid(LinkedNode);
						Connections.Add(FString::Printf(TEXT("%s\t%s\t%s\t%s"),
							*FromGuid, *Pin->PinName.ToString(),
							*ToGuid, *LinkedPin->PinName.ToString()));
					}
				}
			}
		}
	}

	FString Output = FString::Printf(TEXT("# CONNECTIONS %s %d\n"), *Graph->GetName(), Connections.Num());
	for (const FString& Conn : Connections)
	{
		Output += Conn + TEXT("\n");
	}

	return Output;
}

FString FReadFileTool::GetNodePins(UEdGraphNode* Node)
{
	return NeoStackToolUtils::GetNodePinNames(Node);
}

FString FReadFileTool::GetWidgetBlueprintSummary(UWidgetBlueprint* WidgetBlueprint)
{
	FString ParentName = WidgetBlueprint->ParentClass ? WidgetBlueprint->ParentClass->GetName() : TEXT("UserWidget");

	int32 WidgetCount = 0;
	if (WidgetBlueprint->WidgetTree)
	{
		TArray<UWidget*> AllWidgets;
		WidgetBlueprint->WidgetTree->GetAllWidgets(AllWidgets);
		WidgetCount = AllWidgets.Num();
	}

	int32 VarCount = WidgetBlueprint->NewVariables.Num();
	int32 GraphCount = WidgetBlueprint->UbergraphPages.Num() + WidgetBlueprint->FunctionGraphs.Num();
	int32 AnimCount = WidgetBlueprint->Animations.Num();

	FString Output = FString::Printf(TEXT("# WIDGET_BLUEPRINT %s parent=%s\nwidgets=%d variables=%d graphs=%d animations=%d\n"),
		*WidgetBlueprint->GetName(), *ParentName, WidgetCount, VarCount, GraphCount, AnimCount);

	// Add graph list
	if (GraphCount > 0)
	{
		Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

		for (UEdGraph* Graph : WidgetBlueprint->UbergraphPages)
		{
			Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
		}
		for (UEdGraph* Graph : WidgetBlueprint->FunctionGraphs)
		{
			Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
		}
	}

	return Output;
}

FString FReadFileTool::GetWidgetTree(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint->WidgetTree)
	{
		return TEXT("# WIDGET_TREE 0\n(no widget tree)\n");
	}

	TArray<UWidget*> AllWidgets;
	WidgetBlueprint->WidgetTree->GetAllWidgets(AllWidgets);

	FString Output = FString::Printf(TEXT("# WIDGET_TREE %d\n"), AllWidgets.Num());

	// Get root widget
	UWidget* RootWidget = WidgetBlueprint->WidgetTree->RootWidget;
	if (RootWidget)
	{
		Output += GetWidgetHierarchy(RootWidget, 0);
	}
	else
	{
		Output += TEXT("(no root widget)\n");
	}

	return Output;
}

FString FReadFileTool::GetWidgetHierarchy(UWidget* Widget, int32 Depth)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Indent;
	for (int32 i = 0; i < Depth; i++)
	{
		Indent += TEXT("  ");
	}

	// Get widget info
	FString WidgetName = Widget->GetName();
	FString WidgetClass = Widget->GetClass()->GetName();
	FString WidgetVisibility = Widget->IsVisible() ? TEXT("visible") : TEXT("hidden");

	// Get slot info if widget has a slot (layout properties)
	FString SlotInfo;
	if (UPanelSlot* Slot = Widget->Slot)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			FAnchors Anchors = CanvasSlot->GetAnchors();
			FVector2D Position = CanvasSlot->GetPosition();
			FVector2D Size = CanvasSlot->GetSize();
			SlotInfo = FString::Printf(TEXT(" pos=(%.0f,%.0f) size=(%.0f,%.0f) anchors=(%.1f,%.1f)-(%.1f,%.1f)"),
				Position.X, Position.Y, Size.X, Size.Y,
				Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y);
		}
	}

	FString Output = FString::Printf(TEXT("%s%s (%s) %s%s\n"),
		*Indent, *WidgetName, *WidgetClass, *WidgetVisibility, *SlotInfo);

	// If this is a panel widget, recursively add children
	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
	{
		int32 ChildCount = PanelWidget->GetChildrenCount();
		for (int32 i = 0; i < ChildCount; i++)
		{
			UWidget* ChildWidget = PanelWidget->GetChildAt(i);
			if (ChildWidget)
			{
				Output += GetWidgetHierarchy(ChildWidget, Depth + 1);
			}
		}
	}

	return Output;
}

// Animation Blueprint Support

FString FReadFileTool::GetAnimBlueprintSummary(UAnimBlueprint* AnimBlueprint)
{
	FString ParentName = AnimBlueprint->ParentClass ? AnimBlueprint->ParentClass->GetName() : TEXT("AnimInstance");

	// Get skeleton info
	FString SkeletonName = TEXT("None");
	if (AnimBlueprint->TargetSkeleton)
	{
		SkeletonName = AnimBlueprint->TargetSkeleton->GetName();
	}

	int32 VarCount = AnimBlueprint->NewVariables.Num();
	int32 GraphCount = AnimBlueprint->UbergraphPages.Num() + AnimBlueprint->FunctionGraphs.Num();

	// Count state machines
	int32 StateMachineCount = 0;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == TEXT("AnimGraph"))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Cast<UAnimGraphNode_StateMachine>(Node))
				{
					StateMachineCount++;
				}
			}
		}
	}

	FString Output = FString::Printf(TEXT("# ANIM_BLUEPRINT %s parent=%s skeleton=%s\nvariables=%d graphs=%d state_machines=%d\n"),
		*AnimBlueprint->GetName(), *ParentName, *SkeletonName, VarCount, GraphCount, StateMachineCount);

	// Add graph list
	Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

	for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
	{
		Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}

	return Output;
}

FString FReadFileTool::GetAnimBlueprintStateMachines(UAnimBlueprint* AnimBlueprint)
{
	FString Output;

	// Find AnimGraph
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			break;
		}
	}

	if (!AnimGraph)
	{
		return TEXT("# STATE_MACHINES 0\n(no AnimGraph found)\n");
	}

	// Collect state machines first for count
	TArray<UAnimGraphNode_StateMachine*> StateMachines;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
		{
			StateMachines.Add(SMNode);
		}
	}

	Output = FString::Printf(TEXT("# STATE_MACHINES %d\n"), StateMachines.Num());

	// Output each state machine
	for (UAnimGraphNode_StateMachine* SMNode : StateMachines)
	{
		FString SMName = SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		FString SMGuid = NeoStackToolUtils::GetNodeGuid(SMNode);

		// Get the state machine graph
		UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
		if (!SMGraph)
		{
			Output += FString::Printf(TEXT("\n## STATE_MACHINE %s guid=%s\n(no graph)\n"), *SMName, *SMGuid);
			continue;
		}

		// Count states and transitions
		int32 StateCount = 0;
		int32 TransitionCount = 0;
		for (UEdGraphNode* GraphNode : SMGraph->Nodes)
		{
			if (Cast<UAnimStateNode>(GraphNode))
			{
				StateCount++;
			}
			else if (Cast<UAnimStateTransitionNode>(GraphNode))
			{
				TransitionCount++;
			}
		}

		Output += FString::Printf(TEXT("\n## STATE_MACHINE %s guid=%s states=%d transitions=%d\n"),
			*SMName, *SMGuid, StateCount, TransitionCount);

		// List states
		Output += TEXT("# STATES\n");
		for (UEdGraphNode* GraphNode : SMGraph->Nodes)
		{
			if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(GraphNode))
			{
				FString StateName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				FString StateGuid = NeoStackToolUtils::GetNodeGuid(StateNode);

				// Check if this state has a bound graph (the state's animation logic)
				FString HasGraph = StateNode->BoundGraph ? TEXT("has_graph") : TEXT("no_graph");

				Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *StateGuid, *StateName, *HasGraph);
			}
			else if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(GraphNode))
			{
				FString EntryGuid = NeoStackToolUtils::GetNodeGuid(EntryNode);
				Output += FString::Printf(TEXT("%s\t[Entry]\tentry_point\n"), *EntryGuid);
			}
		}

		// List transitions
		Output += TEXT("# TRANSITIONS\n");
		for (UEdGraphNode* GraphNode : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(GraphNode))
			{
				FString TransGuid = NeoStackToolUtils::GetNodeGuid(TransNode);

				// Get source and destination states
				FString FromState = TEXT("Unknown");
				FString ToState = TEXT("Unknown");

				if (UAnimStateNodeBase* PrevState = TransNode->GetPreviousState())
				{
					FromState = PrevState->GetNodeTitle(ENodeTitleType::ListView).ToString();
				}
				if (UAnimStateNodeBase* NextState = TransNode->GetNextState())
				{
					ToState = NextState->GetNodeTitle(ENodeTitleType::ListView).ToString();
				}

				// Check if transition has a graph (condition logic)
				FString HasConditionGraph = TEXT("no_condition");
				if (UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph))
				{
					HasConditionGraph = FString::Printf(TEXT("condition_graph=%s"), *TransGraph->GetName());
				}

				Output += FString::Printf(TEXT("%s\t%s -> %s\t%s\n"),
					*TransGuid, *FromState, *ToState, *HasConditionGraph);
			}
		}
	}

	return Output;
}

void FReadFileTool::CollectAnimBlueprintGraphs(UAnimBlueprint* AnimBlueprint, TArray<TPair<UEdGraph*, FString>>& OutGraphs)
{
	// Find AnimGraph
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			// Add AnimGraph as its own type
			OutGraphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("animgraph")));
			break;
		}
	}

	if (!AnimGraph)
	{
		return;
	}

	// Collect state machine graphs and their subgraphs
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
		{
			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
			if (!SMGraph)
			{
				continue;
			}

			// Add the state machine graph
			FString SMName = SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			OutGraphs.Add(TPair<UEdGraph*, FString>(SMGraph, FString::Printf(TEXT("statemachine:%s"), *SMName)));

			// Collect state graphs
			for (UEdGraphNode* SMGraphNode : SMGraph->Nodes)
			{
				if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMGraphNode))
				{
					if (StateNode->BoundGraph)
					{
						FString StateName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
						OutGraphs.Add(TPair<UEdGraph*, FString>(StateNode->BoundGraph,
							FString::Printf(TEXT("state:%s/%s"), *SMName, *StateName)));
					}
				}
				else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMGraphNode))
				{
					if (UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph))
					{
						// Get transition name from source -> dest
						FString FromState = TEXT("?");
						FString ToState = TEXT("?");
						if (UAnimStateNodeBase* PrevState = TransNode->GetPreviousState())
						{
							FromState = PrevState->GetNodeTitle(ENodeTitleType::ListView).ToString();
						}
						if (UAnimStateNodeBase* NextState = TransNode->GetNextState())
						{
							ToState = NextState->GetNodeTitle(ENodeTitleType::ListView).ToString();
						}

						OutGraphs.Add(TPair<UEdGraph*, FString>(TransGraph,
							FString::Printf(TEXT("transition:%s/%s->%s"), *SMName, *FromState, *ToState)));
					}
				}
			}
		}
	}
}

// Behavior Tree Support

FString FReadFileTool::GetBehaviorTreeSummary(UBehaviorTree* BehaviorTree)
{
	FString BlackboardName = TEXT("None");
	if (BehaviorTree->BlackboardAsset)
	{
		BlackboardName = BehaviorTree->BlackboardAsset->GetName();
	}

	// Count nodes
	int32 TaskCount = 0;
	int32 CompositeCount = 0;
	int32 DecoratorCount = 0;
	int32 ServiceCount = 0;

	if (BehaviorTree->RootNode)
	{
		CountBTNodes(BehaviorTree->RootNode, TaskCount, CompositeCount, DecoratorCount, ServiceCount);
	}

	FString Output = FString::Printf(TEXT("# BEHAVIOR_TREE %s blackboard=%s\n"),
		*BehaviorTree->GetName(), *BlackboardName);
	Output += FString::Printf(TEXT("composites=%d tasks=%d decorators=%d services=%d\n"),
		CompositeCount, TaskCount, DecoratorCount, ServiceCount);

	return Output;
}

void FReadFileTool::CountBTNodes(UBTCompositeNode* Node, int32& OutTasks, int32& OutComposites, int32& OutDecorators, int32& OutServices)
{
	if (!Node)
	{
		return;
	}

	OutComposites++;

	// Count services on this composite node
	OutServices += Node->Services.Num();

	// Count children
	for (int32 i = 0; i < Node->GetChildrenNum(); i++)
	{
		const FBTCompositeChild& Child = Node->Children[i];

		// Count decorators on the child link (decorators are attached to edges, not nodes)
		OutDecorators += Child.Decorators.Num();

		if (Child.ChildComposite)
		{
			// Recurse into composite child
			CountBTNodes(Child.ChildComposite, OutTasks, OutComposites, OutDecorators, OutServices);
		}
		else if (Child.ChildTask)
		{
			OutTasks++;
			// Tasks can have services too
			OutServices += Child.ChildTask->Services.Num();
		}
	}
}

FString FReadFileTool::GetBehaviorTreeNodes(UBehaviorTree* BehaviorTree)
{
	if (!BehaviorTree->RootNode)
	{
		return TEXT("# NODES 0\n(no root node)\n");
	}

	FString Output = TEXT("# NODES\n");
	Output += GetBTNodeHierarchy(BehaviorTree->RootNode, 0);

	return Output;
}

FString FReadFileTool::GetBTNodeHierarchy(UBTCompositeNode* Node, int32 Depth)
{
	if (!Node)
	{
		return TEXT("");
	}

	FString Indent;
	for (int32 i = 0; i < Depth; i++)
	{
		Indent += TEXT("  ");
	}

	// Get node class name (remove UBT prefix for readability)
	FString NodeClass = Node->GetClass()->GetName();
	NodeClass.RemoveFromStart(TEXT("BT"));
	NodeClass.RemoveFromStart(TEXT("Composite_"));

	FString Output = FString::Printf(TEXT("%s[%s] %s\n"),
		*Indent, *NodeClass, *Node->GetNodeName());

	// List services on this composite
	for (UBTService* Service : Node->Services)
	{
		if (Service)
		{
			FString SvcClass = Service->GetClass()->GetName();
			SvcClass.RemoveFromStart(TEXT("BTService_"));
			Output += FString::Printf(TEXT("%s  $%s %s\n"),
				*Indent, *SvcClass, *Service->GetNodeName());
		}
	}

	// Process children
	for (int32 i = 0; i < Node->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Node->Children[i];

		// List decorators on the child link
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (Decorator)
			{
				FString DecClass = Decorator->GetClass()->GetName();
				DecClass.RemoveFromStart(TEXT("BTDecorator_"));
				Output += FString::Printf(TEXT("%s  @%s %s\n"),
					*Indent, *DecClass, *Decorator->GetNodeName());
			}
		}

		if (Child.ChildComposite)
		{
			// Recurse into composite child
			Output += GetBTNodeHierarchy(Child.ChildComposite, Depth + 1);
		}
		else if (Child.ChildTask)
		{
			// Output task node
			FString TaskClass = Child.ChildTask->GetClass()->GetName();
			TaskClass.RemoveFromStart(TEXT("BTTask_"));

			Output += FString::Printf(TEXT("%s  <%s> %s\n"),
				*Indent, *TaskClass, *Child.ChildTask->GetNodeName());

			// List services on the task
			for (UBTService* Service : Child.ChildTask->Services)
			{
				if (Service)
				{
					FString SvcClass = Service->GetClass()->GetName();
					SvcClass.RemoveFromStart(TEXT("BTService_"));
					Output += FString::Printf(TEXT("%s    $%s %s\n"),
						*Indent, *SvcClass, *Service->GetNodeName());
				}
			}
		}
	}

	return Output;
}

// Blackboard Support

FString FReadFileTool::GetBlackboardSummary(UBlackboardData* Blackboard)
{
	int32 KeyCount = Blackboard->Keys.Num();

	// Check for parent blackboard
	FString ParentName = TEXT("None");
	if (Blackboard->Parent)
	{
		ParentName = Blackboard->Parent->GetName();
	}

	FString Output = FString::Printf(TEXT("# BLACKBOARD %s parent=%s keys=%d\n"),
		*Blackboard->GetName(), *ParentName, KeyCount);

	return Output;
}

FString FReadFileTool::GetBlackboardKeys(UBlackboardData* Blackboard)
{
	if (Blackboard->Keys.Num() == 0)
	{
		return TEXT("# KEYS 0\n");
	}

	FString Output = FString::Printf(TEXT("# KEYS %d\n"), Blackboard->Keys.Num());

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		FString KeyName = Entry.EntryName.ToString();
		FString KeyType = TEXT("Unknown");
		FString KeyCategory = Entry.EntryCategory.ToString();

		if (Entry.KeyType)
		{
			// Get type name (remove UBlackboardKeyType_ prefix)
			KeyType = Entry.KeyType->GetClass()->GetName();
			KeyType.RemoveFromStart(TEXT("BlackboardKeyType_"));
		}

		// Format: KeyName	Type	Category	[Synced]
		FString Flags;
		if (Entry.bInstanceSynced)
		{
			Flags = TEXT("[Synced]");
		}

		if (KeyCategory.IsEmpty())
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *KeyName, *KeyType, *Flags);
		}
		else
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"), *KeyName, *KeyType, *KeyCategory, *Flags);
		}
	}

	// Also include parent keys if any
	if (Blackboard->Parent)
	{
		Output += FString::Printf(TEXT("\n# PARENT_KEYS (%s) %d\n"),
			*Blackboard->Parent->GetName(), Blackboard->Parent->Keys.Num());

		for (const FBlackboardEntry& Entry : Blackboard->Parent->Keys)
		{
			FString KeyName = Entry.EntryName.ToString();
			FString KeyType = TEXT("Unknown");

			if (Entry.KeyType)
			{
				KeyType = Entry.KeyType->GetClass()->GetName();
				KeyType.RemoveFromStart(TEXT("BlackboardKeyType_"));
			}

			Output += FString::Printf(TEXT("%s\t%s\t(inherited)\n"), *KeyName, *KeyType);
		}
	}

	return Output;
}

// User Defined Struct Support

FString FReadFileTool::GetStructSummary(UUserDefinedStruct* Struct)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
	int32 FieldCount = VarDescArray.Num();

	FString Output = FString::Printf(TEXT("# STRUCT %s fields=%d\n"),
		*Struct->GetName(), FieldCount);

	// Get struct size if available
	int32 StructSize = Struct->GetStructureSize();
	Output += FString::Printf(TEXT("size=%d bytes\n"), StructSize);

	return Output;
}

FString FReadFileTool::GetStructFields(UUserDefinedStruct* Struct)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);

	if (VarDescArray.Num() == 0)
	{
		return TEXT("# FIELDS 0\n");
	}

	FString Output = FString::Printf(TEXT("# FIELDS %d\n"), VarDescArray.Num());

	for (const FStructVariableDescription& VarDesc : VarDescArray)
	{
		// Get field name
		FString FieldName = VarDesc.VarName.ToString();

		// Get type from pin type (use ToPinType() method in UE5.7+)
		FEdGraphPinType PinType = VarDesc.ToPinType();
		FString TypeName = PinType.PinCategory.ToString();
		if (PinType.PinSubCategoryObject.IsValid())
		{
			TypeName = PinType.PinSubCategoryObject->GetName();
		}
		else if (!PinType.PinSubCategory.IsNone())
		{
			TypeName = PinType.PinSubCategory.ToString();
		}

		// Get default value
		FString DefaultValue = VarDesc.DefaultValue.IsEmpty() ? TEXT("None") : VarDesc.DefaultValue;

		// Get tooltip/description
		FString Description = VarDesc.ToolTip.IsEmpty() ? TEXT("") : VarDesc.ToolTip;

		// Format: name	type	default	[description]
		if (Description.IsEmpty())
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *FieldName, *TypeName, *DefaultValue);
		}
		else
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"), *FieldName, *TypeName, *DefaultValue, *Description);
		}
	}

	return Output;
}

// User Defined Enum Support

FString FReadFileTool::GetEnumSummary(UUserDefinedEnum* Enum)
{
	int32 ValueCount = Enum->NumEnums() - 1; // Exclude MAX value

	FString Output = FString::Printf(TEXT("# ENUM %s values=%d\n"),
		*Enum->GetName(), ValueCount);

	return Output;
}

FString FReadFileTool::GetEnumValues(UUserDefinedEnum* Enum)
{
	int32 ValueCount = Enum->NumEnums() - 1; // Exclude MAX value

	if (ValueCount == 0)
	{
		return TEXT("# VALUES 0\n");
	}

	FString Output = FString::Printf(TEXT("# VALUES %d\n"), ValueCount);

	for (int32 i = 0; i < ValueCount; i++)
	{
		// Get the enum value name
		FString ValueName = Enum->GetNameStringByIndex(i);

		// Get the display name
		FText DisplayNameText = Enum->GetDisplayNameTextByIndex(i);
		FString DisplayName = DisplayNameText.ToString();

		// Format: index	name	display_name
		Output += FString::Printf(TEXT("%d\t%s\t%s\n"), i, *ValueName, *DisplayName);
	}

	return Output;
}

// DataTable Support

FString FReadFileTool::GetDataTableSummary(UDataTable* DataTable)
{
	FString RowStructName = TEXT("None");
	if (DataTable->RowStruct)
	{
		RowStructName = DataTable->RowStruct->GetName();
	}

	// Get row names to count rows
	TArray<FName> RowNames = DataTable->GetRowNames();
	int32 RowCount = RowNames.Num();

	FString Output = FString::Printf(TEXT("# DATATABLE %s row_struct=%s rows=%d\n"),
		*DataTable->GetName(), *RowStructName, RowCount);

	// List column names (struct properties)
	if (DataTable->RowStruct)
	{
		Output += TEXT("\n# COLUMNS\n");
		for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			FString PropName = Property->GetName();
			FString PropType = Property->GetCPPType();

			Output += FString::Printf(TEXT("%s\t%s\n"), *PropName, *PropType);
		}
	}

	return Output;
}

FString FReadFileTool::GetDataTableRows(UDataTable* DataTable, int32 Offset, int32 Limit)
{
	TArray<FName> RowNames = DataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();

	if (TotalRows == 0)
	{
		return TEXT("# ROWS 0\n");
	}

	int32 StartIdx = Offset - 1; // Convert to 0-based
	int32 EndIdx = FMath::Min(StartIdx + Limit, TotalRows);

	FString Output = FString::Printf(TEXT("# ROWS %d-%d/%d\n"), Offset, EndIdx, TotalRows);

	// Get column names for header
	TArray<FString> ColumnNames;
	if (DataTable->RowStruct)
	{
		for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
		{
			ColumnNames.Add((*PropIt)->GetName());
		}
	}

	// Output rows
	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		FName RowName = RowNames[i];
		uint8* RowData = DataTable->FindRowUnchecked(RowName);

		if (!RowData)
		{
			Output += FString::Printf(TEXT("%s\t(no data)\n"), *RowName.ToString());
			continue;
		}

		Output += RowName.ToString();

		// Get property values
		if (DataTable->RowStruct)
		{
			for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				// Export property value to string
				FString ValueStr;
				const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
				Property->ExportTextItem_Direct(ValueStr, PropertyValue, nullptr, nullptr, PPF_None);

				// Truncate long values
				if (ValueStr.Len() > 50)
				{
					ValueStr = ValueStr.Left(47) + TEXT("...");
				}

				Output += FString::Printf(TEXT("\t%s"), *ValueStr);
			}
		}

		Output += TEXT("\n");
	}

	return Output;
}
