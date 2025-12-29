// Copyright NeoStack. All Rights Reserved.

#include "Tools/FindNodeTool.h"
#include "Tools/FuzzyMatchingUtils.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"

// Blueprint includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintVariableNodeSpawner.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/KismetEditorUtilities.h"

// Animation Blueprint
#include "Animation/AnimBlueprint.h"

// Behavior Tree
#include "BehaviorTree/BehaviorTree.h"
#include "AIGraphTypes.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"

FToolResult FFindNodeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	// Parse required parameters
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset"));
	}

	// Parse optional parameters
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	FString CategoryFilter;
	Args->TryGetStringField(TEXT("category"), CategoryFilter);

	// Parse pin type filters - find nodes by what they accept/output
	FString InputTypeFilter;
	Args->TryGetStringField(TEXT("input_type"), InputTypeFilter);
	InputTypeFilter = InputTypeFilter.ToLower();

	FString OutputTypeFilter;
	Args->TryGetStringField(TEXT("output_type"), OutputTypeFilter);
	OutputTypeFilter = OutputTypeFilter.ToLower();

	// Parse limit parameter (default 15 per query)
	int32 Limit = 15;
	if (Args->HasField(TEXT("limit")))
	{
		Limit = FMath::Max(1, static_cast<int32>(Args->GetNumberField(TEXT("limit"))));
	}

	// Parse query array
	TArray<FString> Queries;
	const TArray<TSharedPtr<FJsonValue>>* QueryArray;
	if (Args->TryGetArrayField(TEXT("query"), QueryArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *QueryArray)
		{
			FString Query;
			if (Value->TryGetString(Query) && !Query.IsEmpty())
			{
				Queries.Add(Query.ToLower());
			}
		}
	}

	if (Queries.Num() == 0)
	{
		return FToolResult::Fail(TEXT("Missing required parameter: query (array of search terms)"));
	}

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

	// Detect graph type and find nodes
	EGraphType GraphType = DetectGraphType(Asset);
	TArray<FNodeInfo> Results;

	switch (GraphType)
	{
	case EGraphType::Blueprint:
	case EGraphType::AnimBlueprint:
		Results = FindNodesInBlueprint(Cast<UBlueprint>(Asset), GraphName, Queries, CategoryFilter, InputTypeFilter, OutputTypeFilter);
		break;

	case EGraphType::BehaviorTree:
		Results = FindNodesInBehaviorTree(Asset, Queries, CategoryFilter);
		break;

	case EGraphType::Material:
		Results = FindNodesInMaterial(Asset, Queries, CategoryFilter);
		break;

	default:
		return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName()));
	}

	// Format and return results
	FString Output = FormatResults(AssetName, GraphName, GraphType, Queries, Results, Limit);
	return FToolResult::Ok(Output);
}

FFindNodeTool::EGraphType FFindNodeTool::DetectGraphType(UObject* Asset) const
{
	if (!Asset)
	{
		return EGraphType::Unknown;
	}

	if (Cast<UAnimBlueprint>(Asset))
	{
		return EGraphType::AnimBlueprint;
	}
	if (Cast<UBlueprint>(Asset))
	{
		return EGraphType::Blueprint;
	}
	if (Cast<UBehaviorTree>(Asset))
	{
		return EGraphType::BehaviorTree;
	}
	if (Cast<UMaterial>(Asset) || Cast<UMaterialFunction>(Asset))
	{
		return EGraphType::Material;
	}

	return EGraphType::Unknown;
}

FString FFindNodeTool::GraphTypeToString(EGraphType Type) const
{
	switch (Type)
	{
	case EGraphType::Blueprint: return TEXT("Blueprint");
	case EGraphType::AnimBlueprint: return TEXT("AnimBlueprint");
	case EGraphType::BehaviorTree: return TEXT("BehaviorTree");
	case EGraphType::Material: return TEXT("Material");
	default: return TEXT("Unknown");
	}
}

UEdGraph* FFindNodeTool::GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// If no name specified, return the main event graph (UbergraphPages[0])
	if (GraphName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search UbergraphPages (EventGraph, etc.)
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

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInBlueprint(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const TArray<FString>& Queries,
	const FString& CategoryFilter,
	const FString& InputTypeFilter,
	const FString& OutputTypeFilter)
{
	TArray<FNodeInfo> Results;

	if (!Blueprint)
	{
		return Results;
	}

	UEdGraph* TargetGraph = GetGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		// Try to get any available graph
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			TargetGraph = Blueprint->UbergraphPages[0];
		}
		else if (Blueprint->FunctionGraphs.Num() > 0)
		{
			TargetGraph = Blueprint->FunctionGraphs[0];
		}
	}

	// Must have a valid graph to query nodes
	if (!TargetGraph)
	{
		return Results;
	}

	// Get actions from BlueprintActionDatabase
	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();

	// CRITICAL: Compile the Blueprint if it's dirty to ensure newly added variables/functions
	// have their FProperty objects created. The action database creates spawners from FProperties,
	// so variables added via edit_blueprint won't be findable until compiled.
	UE_LOG(LogTemp, Log, TEXT("FindNode: Blueprint status before compile check: %d (UpToDate=%d)"),
		(int32)Blueprint->Status, (int32)BS_UpToDate);
	UE_LOG(LogTemp, Log, TEXT("FindNode: NewVariables count: %d"), Blueprint->NewVariables.Num());
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		UE_LOG(LogTemp, Log, TEXT("FindNode: Variable in NewVariables: %s"), *Var.VarName.ToString());
	}

	if (Blueprint->Status != BS_UpToDate)
	{
		UE_LOG(LogTemp, Log, TEXT("FindNode: Compiling Blueprint..."));
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		UE_LOG(LogTemp, Log, TEXT("FindNode: Blueprint status after compile: %d"), (int32)Blueprint->Status);
	}

	// Check if GeneratedClass has the property
	if (Blueprint->GeneratedClass)
	{
		UE_LOG(LogTemp, Log, TEXT("FindNode: GeneratedClass exists: %s"), *Blueprint->GeneratedClass->GetName());
		for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
		{
			UE_LOG(LogTemp, Log, TEXT("FindNode: Property in GeneratedClass: %s"), *It->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FindNode: GeneratedClass is NULL!"));
	}

	// Refresh actions for this Blueprint to pick up newly added variables/functions
	// The action database caches actions and won't see new variables until refreshed
	UE_LOG(LogTemp, Log, TEXT("FindNode: Refreshing action database..."));
	ActionDatabase.RefreshAssetActions(Blueprint);
	const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDatabase.GetAllActions();
	UE_LOG(LogTemp, Log, TEXT("FindNode: Total actions in database: %d"), AllActions.Num());

	// Get the graph schema for compatibility checking
	const UEdGraphSchema* GraphSchema = TargetGraph->GetSchema();

	int32 VarSpawnerCount = 0;
	for (const auto& ActionPair : AllActions)
	{
		for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
		{
			if (!Spawner || !Spawner->NodeClass)
			{
				continue;
			}

			// Log variable spawners specifically
			bool bIsVarSpawner = false;
			if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
			{
				bIsVarSpawner = true;
				VarSpawnerCount++;
				if (FProperty const* VarProp = VarSpawner->GetVarProperty())
				{
					UClass* OwnerClass = VarProp->GetOwnerClass();
					UE_LOG(LogTemp, Log, TEXT("FindNode: Found VarSpawner for property: %s (Owner: %s, NodeClass: %s)"),
						*VarProp->GetName(),
						OwnerClass ? *OwnerClass->GetName() : TEXT("null"),
						Spawner->NodeClass ? *Spawner->NodeClass->GetName() : TEXT("null"));
				}
			}

			// Check if this node type is compatible with the graph's schema
			UEdGraphNode* NodeCDO = Spawner->NodeClass->GetDefaultObject<UEdGraphNode>();
			if (!NodeCDO || !NodeCDO->CanCreateUnderSpecifiedSchema(GraphSchema))
			{
				if (bIsVarSpawner)
				{
					UE_LOG(LogTemp, Warning, TEXT("FindNode: VarSpawner FILTERED by schema check! NodeCDO=%s, Schema=%s"),
						NodeCDO ? TEXT("valid") : TEXT("null"),
						GraphSchema ? *GraphSchema->GetClass()->GetName() : TEXT("null"));
				}
				continue;
			}

			// Get UI spec for menu name, category, etc.
			const FBlueprintActionUiSpec& UiSpec = Spawner->PrimeDefaultUiSpec(TargetGraph);

			FString NodeName = UiSpec.MenuName.ToString();
			FString NodeCategory = UiSpec.Category.ToString();
			FString NodeKeywords = UiSpec.Keywords.ToString();
			FString NodeTooltip = UiSpec.Tooltip.ToString();

			// For variable spawners, generate fallback name from property if UiSpec is empty
			if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
			{
				UE_LOG(LogTemp, Log, TEXT("FindNode: VarSpawner UiSpec - Name: '%s', Category: '%s', Keywords: '%s'"),
					*NodeName, *NodeCategory, *NodeKeywords);

				// If MenuName is empty, construct it from the property name
				if (NodeName.IsEmpty())
				{
					if (FProperty const* VarProp = VarSpawner->GetVarProperty())
					{
						FString PropName = VarProp->GetName();
						// Convert to display name (adds spaces for CamelCase)
						FString DisplayName = FName::NameToDisplayString(PropName, false);
						bool bIsGetter = Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
						NodeName = FString::Printf(TEXT("%s %s"), bIsGetter ? TEXT("Get") : TEXT("Set"), *DisplayName);
						// Also add the raw property name as a keyword for better matching
						if (NodeKeywords.IsEmpty())
						{
							NodeKeywords = PropName.ToLower();
						}
						else
						{
							NodeKeywords += TEXT(" ") + PropName.ToLower();
						}
						UE_LOG(LogTemp, Log, TEXT("FindNode: VarSpawner fallback name: '%s', added keyword: '%s'"),
							*NodeName, *PropName);
					}
				}
			}

			// Skip empty names
			if (NodeName.IsEmpty())
			{
				continue;
			}

			// Check category filter
			if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
			{
				continue;
			}

			// Check query match
			FString MatchedQuery;
			int32 Score = 0;
			if (!MatchesQuery(NodeName, NodeKeywords, Queries, MatchedQuery, Score))
			{
				continue;
			}

			// Generate a unique spawner ID
			// IMPORTANT: UBlueprintVariableNodeSpawner has a bug where GetSpawnerSignature()
			// doesn't include the property for member variables - all getters have the same GUID!
			// We fix this by using property path for variable spawners.
			FString SpawnerId;
			if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
			{
				if (FProperty const* VarProp = VarSpawner->GetVarProperty())
				{
					// Use property path as unique identifier for member variables
					// Format: VARGET:PropertyPath or VARSET:PropertyPath
					bool bIsGetter = Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
					SpawnerId = FString::Printf(TEXT("%s:%s"),
						bIsGetter ? TEXT("VARGET") : TEXT("VARSET"),
						*VarProp->GetPathName());
				}
				else
				{
					// Local variable - the GUID should work (signature includes local var info)
					FGuid SpawnerGuid = Spawner->GetSpawnerSignature().AsGuid();
					SpawnerId = SpawnerGuid.ToString();
				}
			}
			else
			{
				// All other spawner types - use GUID (signatures work correctly)
				FGuid SpawnerGuid = Spawner->GetSpawnerSignature().AsGuid();
				SpawnerId = SpawnerGuid.ToString();
			}

			// Create node info
			FNodeInfo Info;
			Info.Name = NodeName;
			Info.SpawnerId = SpawnerId;
			Info.Category = NodeCategory;
			Info.Tooltip = NodeTooltip;
			Info.Keywords = NodeKeywords;
			Info.MatchedQuery = MatchedQuery;
			Info.Score = Score;

			// Try to get pin info and flags from template node
			if (TargetGraph)
			{
				UEdGraphNode* TemplateNode = Spawner->GetTemplateNode(TargetGraph);
				if (TemplateNode)
				{
					if (TemplateNode->Pins.Num() == 0)
					{
						TemplateNode->AllocateDefaultPins();
					}
					ExtractPinInfo(TemplateNode, Info.InputPins, Info.OutputPins);
					ExtractNodeFlags(TemplateNode, Info.Flags);
				}
			}

			// Check pin type filters - skip nodes that don't match
			if (!MatchesPinType(Info.InputPins, InputTypeFilter))
			{
				continue;
			}
			if (!MatchesPinType(Info.OutputPins, OutputTypeFilter))
			{
				continue;
			}

			Results.Add(Info);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FindNode: Summary - VarSpawners found: %d, Total results matching query: %d"),
		VarSpawnerCount, Results.Num());

	return Results;
}

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInBehaviorTree(
	UObject* BehaviorTree,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	// Get all BT node classes using TObjectIterator
	TArray<UClass*> BTNodeClasses;

	// Tasks
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		FString ClassCategory;
		if (Class->IsChildOf(UBTTaskNode::StaticClass()))
		{
			ClassCategory = TEXT("Tasks");
		}
		else if (Class->IsChildOf(UBTCompositeNode::StaticClass()))
		{
			ClassCategory = TEXT("Composites");
		}
		else if (Class->IsChildOf(UBTDecorator::StaticClass()))
		{
			ClassCategory = TEXT("Decorators");
		}
		else if (Class->IsChildOf(UBTService::StaticClass()))
		{
			ClassCategory = TEXT("Services");
		}
		else
		{
			continue;
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(ClassCategory, CategoryFilter))
		{
			continue;
		}

		FString NodeName = Class->GetName();
		// Remove common prefixes
		NodeName.RemoveFromStart(TEXT("BTTask_"));
		NodeName.RemoveFromStart(TEXT("BTComposite_"));
		NodeName.RemoveFromStart(TEXT("BTDecorator_"));
		NodeName.RemoveFromStart(TEXT("BTService_"));

		FString DisplayName = FName::NameToDisplayString(NodeName, false);

		// Check query match
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(DisplayName, TEXT(""), Queries, MatchedQuery, Score))
		{
			continue;
		}

		FNodeInfo Info;
		Info.Name = DisplayName;
		Info.SpawnerId = Class->GetPathName();
		Info.Category = ClassCategory;
		Info.Tooltip = Class->GetMetaData(TEXT("Tooltip"));
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		Results.Add(Info);
	}

	return Results;
}

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInMaterial(
	UObject* Material,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	// Iterate all MaterialExpression classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		if (!Class->IsChildOf(UMaterialExpression::StaticClass()))
		{
			continue;
		}

		// Skip private expressions
		if (Class->HasMetaData(TEXT("Private")))
		{
			continue;
		}

		// Get display name
		FString NodeName = Class->GetName();
		static const FString ExpressionPrefix = TEXT("MaterialExpression");
		if (NodeName.StartsWith(ExpressionPrefix))
		{
			NodeName.MidInline(ExpressionPrefix.Len(), MAX_int32, EAllowShrinking::No);
		}

		if (Class->HasMetaData(TEXT("DisplayName")))
		{
			NodeName = Class->GetDisplayNameText().ToString();
		}

		// Get category from CDO
		FString NodeCategory;
		if (UMaterialExpression* CDO = Cast<UMaterialExpression>(Class->GetDefaultObject()))
		{
			if (CDO->MenuCategories.Num() > 0)
			{
				NodeCategory = CDO->MenuCategories[0].ToString();
			}
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		// Check query match
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(NodeName, TEXT(""), Queries, MatchedQuery, Score))
		{
			continue;
		}

		FNodeInfo Info;
		Info.Name = NodeName;
		Info.SpawnerId = Class->GetPathName();
		Info.Category = NodeCategory;
		Info.Tooltip = Class->GetToolTipText().ToString();
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		Results.Add(Info);
	}

	return Results;
}

void FFindNodeTool::ExtractPinInfo(UEdGraphNode* TemplateNode, TArray<FString>& OutInputs, TArray<FString>& OutOutputs)
{
	if (!TemplateNode)
	{
		return;
	}

	for (UEdGraphPin* Pin : TemplateNode->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}

		FString PinStr = FString::Printf(TEXT("%s (%s)"),
			*Pin->PinName.ToString(),
			*PinTypeToString(Pin->PinType));

		// For input pins, show default value or indicate if required
		if (Pin->Direction == EGPD_Input)
		{
			// Skip exec pins for default value display
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				// Check if pin has a default value
				bool bHasDefault = false;
				FString DefaultStr;

				if (!Pin->DefaultValue.IsEmpty())
				{
					bHasDefault = true;
					DefaultStr = Pin->DefaultValue;
				}
				else if (Pin->DefaultObject)
				{
					bHasDefault = true;
					DefaultStr = Pin->DefaultObject->GetName();
				}
				else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
				{
					bHasDefault = true;
					DefaultStr = Pin->AutogeneratedDefaultValue;
				}

				if (bHasDefault)
				{
					// Truncate long default values
					if (DefaultStr.Len() > 50)
					{
						DefaultStr = DefaultStr.Left(47) + TEXT("...");
					}
					PinStr += FString::Printf(TEXT(" = %s"), *DefaultStr);
				}
				else if (!Pin->bNotConnectable)
				{
					// Pin has no default and can be connected - might need a value
					PinStr += TEXT(" [REQUIRED]");
				}
			}

			OutInputs.Add(PinStr);
		}
		else
		{
			OutOutputs.Add(PinStr);
		}
	}
}

void FFindNodeTool::ExtractNodeFlags(UEdGraphNode* TemplateNode, TArray<FString>& OutFlags)
{
	if (!TemplateNode)
	{
		return;
	}

	// Check if it's a K2Node (Blueprint node)
	if (UK2Node* K2Node = Cast<UK2Node>(TemplateNode))
	{
		// Pure nodes have no exec pins and no side effects
		if (K2Node->IsNodePure())
		{
			OutFlags.Add(TEXT("Pure"));
		}

		// Check for function call nodes to get more info
		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(K2Node))
		{
			if (UFunction* Function = CallNode->GetTargetFunction())
			{
				// Const function - can be called from const contexts
				if (Function->HasAnyFunctionFlags(FUNC_Const))
				{
					OutFlags.Add(TEXT("Const"));
				}

				// Thread safe
				if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) &&
					Function->HasMetaData(TEXT("BlueprintThreadSafe")))
				{
					OutFlags.Add(TEXT("ThreadSafe"));
				}

				// Static function
				if (Function->HasAnyFunctionFlags(FUNC_Static))
				{
					OutFlags.Add(TEXT("Static"));
				}

				// Check for latent via metadata (Latent keyword in UFUNCTION)
				if (Function->HasMetaData(TEXT("Latent")))
				{
					OutFlags.Add(TEXT("Latent"));
				}

				// Deprecated - check via metadata
				if (Function->HasMetaData(TEXT("DeprecatedFunction")))
				{
					OutFlags.Add(TEXT("Deprecated"));
				}

				// Development only
				if (Function->HasMetaData(TEXT("DevelopmentOnly")))
				{
					OutFlags.Add(TEXT("DevOnly"));
				}
			}
		}

		// Check for event nodes
		if (Cast<UK2Node_Event>(K2Node))
		{
			OutFlags.Add(TEXT("Event"));
		}

		// Check for macro instance
		if (Cast<UK2Node_MacroInstance>(K2Node))
		{
			OutFlags.Add(TEXT("Macro"));
		}

		// Compact node (displayed as small operator like +, -, etc.)
		if (K2Node->ShouldDrawCompact())
		{
			OutFlags.Add(TEXT("Compact"));
		}
	}

	// Check if node is deprecated via its own flag
	if (TemplateNode->IsDeprecated())
	{
		OutFlags.Add(TEXT("Deprecated"));
	}
}

FString FFindNodeTool::PinTypeToString(const FEdGraphPinType& PinType) const
{
	// Handle exec pins
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return TEXT("exec");
	}

	// Get base type name
	FString TypeName = PinType.PinCategory.ToString();

	// For object/struct types, include the subtype
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeName = PinType.PinSubCategoryObject->GetName();
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeName = PinType.PinSubCategory.ToString();
	}

	// Handle containers
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		TypeName = FString::Printf(TEXT("Array<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		TypeName = FString::Printf(TEXT("Set<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Map)
	{
		TypeName = FString::Printf(TEXT("Map<%s>"), *TypeName);
	}

	// Handle reference
	if (PinType.bIsReference)
	{
		TypeName += TEXT("&");
	}

	return TypeName;
}

bool FFindNodeTool::MatchesQuery(const FString& NodeName, const FString& Keywords, const TArray<FString>& Queries, FString& OutMatchedQuery, int32& OutScore) const
{
	FString LowerName = NodeName.ToLower();
	FString LowerKeywords = Keywords.ToLower();
	OutScore = 0;

	// Scoring weights:
	// 100 = Exact name match (case-insensitive)
	// 80  = Name starts with query
	// 60  = Query is a word in name (word boundary match)
	// 50  = Normalized match (spaces removed) - handles "getmyint" matching "Get My Int"
	// 40  = Name contains query as substring
	// 35  = Acronym match (e.g., "mvm" -> "Move Mouse Vertically")  [NEW]
	// 30  = Levenshtein similarity >= 70% (typo tolerance)  [NEW]
	// 20  = Keyword match
	// 15  = Normalized keyword match (spaces removed)

	// Create normalized versions (no spaces) for fuzzy matching
	// This handles cases like query "getmyint" matching "Get My Int"
	FString NormalizedName = LowerName.Replace(TEXT(" "), TEXT(""));

	// Helper to check word boundary match on original (non-lowercased) text
	auto MatchesWithWordBoundary = [](const FString& OriginalText, const FString& LowerQuery) -> bool
	{
		FString LowerText = OriginalText.ToLower();
		int32 Index = LowerText.Find(LowerQuery, ESearchCase::CaseSensitive);
		if (Index == INDEX_NONE)
		{
			return false;
		}

		// Check if it's at start or preceded by non-alpha or is CamelCase boundary
		bool bStartOk = (Index == 0) ||
			!FChar::IsAlpha(OriginalText[Index - 1]) ||
			(FChar::IsLower(OriginalText[Index - 1]) && FChar::IsUpper(OriginalText[Index]));

		// Check if it's at end or followed by non-alpha or CamelCase boundary
		int32 EndIndex = Index + LowerQuery.Len();
		bool bEndOk = (EndIndex >= OriginalText.Len()) ||
			!FChar::IsAlpha(OriginalText[EndIndex]) ||
			FChar::IsUpper(OriginalText[EndIndex]);

		return bStartOk && bEndOk;
	};

	for (const FString& Query : Queries)
	{
		int32 CurrentScore = 0;

		// Check exact name match (case-insensitive)
		if (LowerName.Equals(Query))
		{
			CurrentScore = 100;
		}
		// Check if name starts with query
		else if (LowerName.StartsWith(Query))
		{
			CurrentScore = 80;
		}
		// Check word boundary match in name
		else if (MatchesWithWordBoundary(NodeName, Query))
		{
			CurrentScore = 60;
		}
		// Check normalized match (spaces removed from both)
		// Handles "getmyint" or "get myint" matching "Get My Int"
		else if (NormalizedName.Contains(Query.Replace(TEXT(" "), TEXT(""))))
		{
			CurrentScore = 50;
		}
		// Check simple contains in name (fallback, less relevant)
		else if (LowerName.Contains(Query))
		{
			CurrentScore = 40;
		}
		// NEW: Check acronym match (e.g., "mvm" -> "Move Mouse Vertically", "sa" -> "Spawn Actor")
		else
		{
			float AcronymScore = 0.0f;
			if (FFuzzyMatchingUtils::MatchesAsAcronym(Query, NodeName, AcronymScore))
			{
				// Scale acronym score (0.5-1.0) to integer score (35-45)
				CurrentScore = 35 + (int32)((AcronymScore - 0.5f) * 20.0f);
			}
		}

		// NEW: Check Levenshtein similarity for typo tolerance (only if no match yet and query is substantial)
		if (CurrentScore == 0 && Query.Len() >= 4)
		{
			float LevenshteinScore = FFuzzyMatchingUtils::CalculateLevenshteinScore(Query, LowerName);
			if (LevenshteinScore >= 0.7f)  // 70% similarity threshold
			{
				// Scale Levenshtein score (0.7-1.0) to integer score (30-40)
				CurrentScore = 30 + (int32)((LevenshteinScore - 0.7f) * 33.0f);
			}
		}

		// Check keywords (if still no match from name-based checks)
		if (CurrentScore == 0 && !LowerKeywords.IsEmpty())
		{
			if (LowerKeywords.Contains(Query))
			{
				CurrentScore = 20;
			}
			// Check normalized keywords (spaces removed)
			else if (LowerKeywords.Replace(TEXT(" "), TEXT("")).Contains(Query.Replace(TEXT(" "), TEXT(""))))
			{
				CurrentScore = 15;
			}
		}

		if (CurrentScore > OutScore)
		{
			OutScore = CurrentScore;
			OutMatchedQuery = Query;
		}
	}

	return OutScore > 0;
}

bool FFindNodeTool::MatchesCategory(const FString& NodeCategory, const FString& CategoryFilter) const
{
	if (CategoryFilter.IsEmpty())
	{
		return true;
	}

	return NodeCategory.Contains(CategoryFilter, ESearchCase::IgnoreCase);
}

bool FFindNodeTool::MatchesPinType(const TArray<FString>& Pins, const FString& TypeFilter) const
{
	if (TypeFilter.IsEmpty())
	{
		return true;
	}

	// Check if any pin contains the type filter
	// Pin format: "PinName (TypeName) = default" or "PinName (TypeName) [REQUIRED]"
	// We want to match the type part, e.g., "Array" matches "Array<wildcard>&"
	for (const FString& Pin : Pins)
	{
		// Extract the type from parentheses
		int32 OpenParen = Pin.Find(TEXT("("));
		int32 CloseParen = Pin.Find(TEXT(")"));
		if (OpenParen != INDEX_NONE && CloseParen != INDEX_NONE && CloseParen > OpenParen)
		{
			FString PinType = Pin.Mid(OpenParen + 1, CloseParen - OpenParen - 1).ToLower();
			if (PinType.Contains(TypeFilter))
			{
				return true;
			}
		}
	}

	return false;
}

FString FFindNodeTool::FormatResults(
	const FString& AssetName,
	const FString& GraphName,
	EGraphType GraphType,
	const TArray<FString>& Queries,
	const TArray<FNodeInfo>& Results,
	int32 Limit) const
{
	FString Output;

	// Header
	Output += FString::Printf(TEXT("# FIND NODES in %s (%s)\n"),
		*AssetName, *GraphTypeToString(GraphType));

	if (!GraphName.IsEmpty())
	{
		Output += FString::Printf(TEXT("Graph: %s\n"), *GraphName);
	}

	// Query info
	FString QueryStr = FString::Join(Queries, TEXT(", "));
	Output += FString::Printf(TEXT("Query: %s\n\n"), *QueryStr);

	// Results count
	Output += FString::Printf(TEXT("## Results (%d found, showing top %d per query)\n\n"), Results.Num(), Limit);

	if (Results.Num() == 0)
	{
		Output += TEXT("No matching nodes found.\n");
		return Output;
	}

	// Group by matched query
	TMap<FString, TArray<const FNodeInfo*>> GroupedResults;
	for (const FNodeInfo& Info : Results)
	{
		GroupedResults.FindOrAdd(Info.MatchedQuery).Add(&Info);
	}

	// Output each group
	for (const FString& Query : Queries)
	{
		TArray<const FNodeInfo*>* Group = GroupedResults.Find(Query);
		if (!Group || Group->Num() == 0)
		{
			continue;
		}

		// Sort by score descending (best matches first)
		Group->Sort([](const FNodeInfo& A, const FNodeInfo& B)
		{
			if (A.Score != B.Score)
			{
				return A.Score > B.Score;
			}
			// Secondary sort by name length (shorter names are often more relevant)
			return A.Name.Len() < B.Name.Len();
		});

		int32 TotalCount = Group->Num();
		int32 ShownCount = FMath::Min(TotalCount, Limit);

		if (TotalCount > Limit)
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d of %d, +%d more)\n"),
				*Query, ShownCount, TotalCount, TotalCount - Limit);
			Output += TEXT("    TIP: Too many results? Add input_type/output_type filter (e.g., input_type=\"array\") or category filter.\n\n");
		}
		else
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d)\n\n"), *Query, TotalCount);
		}

		for (int32 i = 0; i < ShownCount; ++i)
		{
			const FNodeInfo* Info = (*Group)[i];

			Output += FString::Printf(TEXT("+ %s\n"), *Info->Name);
			Output += FString::Printf(TEXT("  ID: %s\n"), *Info->SpawnerId);

			if (!Info->Category.IsEmpty())
			{
				Output += FString::Printf(TEXT("  Category: %s\n"), *Info->Category);
			}

			// Add node flags if present
			if (Info->Flags.Num() > 0)
			{
				Output += FString::Printf(TEXT("  Flags: %s\n"), *FString::Join(Info->Flags, TEXT(", ")));
			}

			// Add tooltip/description (truncate if too long)
			if (!Info->Tooltip.IsEmpty())
			{
				FString Desc = Info->Tooltip;
				// Remove newlines and truncate for readability
				Desc.ReplaceInline(TEXT("\r\n"), TEXT(" "), ESearchCase::CaseSensitive);
				Desc.ReplaceInline(TEXT("\n"), TEXT(" "), ESearchCase::CaseSensitive);
				if (Desc.Len() > 120)
				{
					Desc = Desc.Left(117) + TEXT("...");
				}
				Output += FString::Printf(TEXT("  Desc: %s\n"), *Desc);
			}

			// Input pins
			if (Info->InputPins.Num() > 0)
			{
				Output += TEXT("  Inputs:\n");
				for (const FString& Pin : Info->InputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			// Output pins
			if (Info->OutputPins.Num() > 0)
			{
				Output += TEXT("  Outputs:\n");
				for (const FString& Pin : Info->OutputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			Output += TEXT("\n");
		}
	}

	return Output;
}
