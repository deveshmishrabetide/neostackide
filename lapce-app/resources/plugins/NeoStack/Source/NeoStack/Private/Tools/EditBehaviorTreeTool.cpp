// Copyright NeoStack. All Rights Reserved.

#include "Tools/EditBehaviorTreeTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"

// Behavior Tree
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"

// Blackboard
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"

// Asset utilities
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"

FToolResult FEditBehaviorTreeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);

	// Build asset path
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);

	// Try to load as BehaviorTree first, then as Blackboard
	UBehaviorTree* BehaviorTree = LoadObject<UBehaviorTree>(nullptr, *FullAssetPath);
	UBlackboardData* Blackboard = nullptr;

	if (!BehaviorTree)
	{
		Blackboard = LoadObject<UBlackboardData>(nullptr, *FullAssetPath);
		if (!Blackboard)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Asset not found (expected BehaviorTree or Blackboard): %s"), *FullAssetPath));
		}
	}

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;

	// ========== Behavior Tree Operations ==========
	if (BehaviorTree)
	{
		// Process set_blackboard
		FString BlackboardName;
		if (Args->TryGetStringField(TEXT("set_blackboard"), BlackboardName) && !BlackboardName.IsEmpty())
		{
			FString Result = SetBlackboard(BehaviorTree, BlackboardName);
			Results.Add(Result);
			if (Result.StartsWith(TEXT("+"))) AddedCount++;
		}

		// Process add_composite
		const TArray<TSharedPtr<FJsonValue>>* AddComposites;
		if (Args->TryGetArrayField(TEXT("add_composite"), AddComposites))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddComposites)
			{
				const TSharedPtr<FJsonObject>* CompObj;
				if (Value->TryGetObject(CompObj))
				{
					FCompositeDefinition CompDef;
					(*CompObj)->TryGetStringField(TEXT("type"), CompDef.Type);
					(*CompObj)->TryGetStringField(TEXT("name"), CompDef.Name);
					(*CompObj)->TryGetStringField(TEXT("parent"), CompDef.Parent);
					(*CompObj)->TryGetNumberField(TEXT("index"), CompDef.Index);

					FString Result = AddComposite(BehaviorTree, CompDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process add_task
		const TArray<TSharedPtr<FJsonValue>>* AddTasks;
		if (Args->TryGetArrayField(TEXT("add_task"), AddTasks))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddTasks)
			{
				const TSharedPtr<FJsonObject>* TaskObj;
				if (Value->TryGetObject(TaskObj))
				{
					FTaskDefinition TaskDef;
					(*TaskObj)->TryGetStringField(TEXT("type"), TaskDef.Type);
					(*TaskObj)->TryGetStringField(TEXT("name"), TaskDef.Name);
					(*TaskObj)->TryGetStringField(TEXT("parent"), TaskDef.Parent);
					(*TaskObj)->TryGetNumberField(TEXT("index"), TaskDef.Index);

					FString Result = AddTask(BehaviorTree, TaskDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process add_decorator
		const TArray<TSharedPtr<FJsonValue>>* AddDecorators;
		if (Args->TryGetArrayField(TEXT("add_decorator"), AddDecorators))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddDecorators)
			{
				const TSharedPtr<FJsonObject>* DecObj;
				if (Value->TryGetObject(DecObj))
				{
					FDecoratorDefinition DecDef;
					(*DecObj)->TryGetStringField(TEXT("type"), DecDef.Type);
					(*DecObj)->TryGetStringField(TEXT("name"), DecDef.Name);
					(*DecObj)->TryGetStringField(TEXT("target"), DecDef.Target);

					FString Result = AddDecorator(BehaviorTree, DecDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process add_service
		const TArray<TSharedPtr<FJsonValue>>* AddServices;
		if (Args->TryGetArrayField(TEXT("add_service"), AddServices))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddServices)
			{
				const TSharedPtr<FJsonObject>* SvcObj;
				if (Value->TryGetObject(SvcObj))
				{
					FServiceDefinition SvcDef;
					(*SvcObj)->TryGetStringField(TEXT("type"), SvcDef.Type);
					(*SvcObj)->TryGetStringField(TEXT("name"), SvcDef.Name);
					(*SvcObj)->TryGetStringField(TEXT("target"), SvcDef.Target);

					FString Result = AddService(BehaviorTree, SvcDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process remove_node
		const TArray<TSharedPtr<FJsonValue>>* RemoveNodes;
		if (Args->TryGetArrayField(TEXT("remove_node"), RemoveNodes))
		{
			for (const TSharedPtr<FJsonValue>& Value : *RemoveNodes)
			{
				FString NodeName;
				if (Value->TryGetString(NodeName))
				{
					FString Result = RemoveNode(BehaviorTree, NodeName);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("-"))) RemovedCount++;
				}
			}
		}

		// Mark dirty
		BehaviorTree->Modify();
	}

	// ========== Blackboard Operations (works for both standalone and BT's blackboard) ==========
	// Explicitly get raw pointer from TObjectPtr to avoid ternary ambiguity
	UBlackboardData* TargetBlackboard = Blackboard ? Blackboard : (BehaviorTree ? BehaviorTree->BlackboardAsset.Get() : nullptr);

	if (TargetBlackboard)
	{
		// Process add_key
		const TArray<TSharedPtr<FJsonValue>>* AddKeys;
		if (Args->TryGetArrayField(TEXT("add_key"), AddKeys))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddKeys)
			{
				const TSharedPtr<FJsonObject>* KeyObj;
				if (Value->TryGetObject(KeyObj))
				{
					FBlackboardKeyDefinition KeyDef;
					(*KeyObj)->TryGetStringField(TEXT("name"), KeyDef.Name);
					(*KeyObj)->TryGetStringField(TEXT("type"), KeyDef.Type);
					(*KeyObj)->TryGetStringField(TEXT("base_class"), KeyDef.BaseClass);
					(*KeyObj)->TryGetStringField(TEXT("category"), KeyDef.Category);
					(*KeyObj)->TryGetBoolField(TEXT("instance_synced"), KeyDef.bInstanceSynced);

					FString Result = AddBlackboardKey(TargetBlackboard, KeyDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process remove_key
		const TArray<TSharedPtr<FJsonValue>>* RemoveKeys;
		if (Args->TryGetArrayField(TEXT("remove_key"), RemoveKeys))
		{
			for (const TSharedPtr<FJsonValue>& Value : *RemoveKeys)
			{
				FString KeyName;
				if (Value->TryGetString(KeyName))
				{
					FString Result = RemoveBlackboardKey(TargetBlackboard, KeyName);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("-"))) RemovedCount++;
				}
			}
		}

		// Mark dirty
		TargetBlackboard->Modify();
	}

	// Build output
	FString AssetType = BehaviorTree ? TEXT("BehaviorTree") : TEXT("Blackboard");
	FString Output = FString::Printf(TEXT("# EDIT %s %s\n"), *AssetType, *Name);
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed\n"), AddedCount, RemovedCount);

	return FToolResult::Ok(Output);
}

// ========== Find Helpers ==========

UBTCompositeNode* FEditBehaviorTreeTool::FindCompositeByName(UBTCompositeNode* Root, const FString& Name)
{
	if (!Root)
	{
		return nullptr;
	}

	// Check this node
	if (Root->GetNodeName().Equals(Name, ESearchCase::IgnoreCase))
	{
		return Root;
	}

	// Check children
	for (int32 i = 0; i < Root->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Root->Children[i];
		if (Child.ChildComposite)
		{
			UBTCompositeNode* Found = FindCompositeByName(Child.ChildComposite, Name);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UBTTaskNode* FEditBehaviorTreeTool::FindTaskByName(UBTCompositeNode* Root, const FString& Name)
{
	if (!Root)
	{
		return nullptr;
	}

	for (int32 i = 0; i < Root->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Root->Children[i];
		if (Child.ChildTask && Child.ChildTask->GetNodeName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Child.ChildTask;
		}
		if (Child.ChildComposite)
		{
			UBTTaskNode* Found = FindTaskByName(Child.ChildComposite, Name);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UClass* FEditBehaviorTreeTool::FindCompositeClass(const FString& TypeName)
{
	// Handle common composite types
	if (TypeName.Equals(TEXT("Selector"), ESearchCase::IgnoreCase))
	{
		return UBTComposite_Selector::StaticClass();
	}
	if (TypeName.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
	{
		return UBTComposite_Sequence::StaticClass();
	}
	if (TypeName.Equals(TEXT("SimpleParallel"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Parallel"), ESearchCase::IgnoreCase))
	{
		return UBTComposite_SimpleParallel::StaticClass();
	}

	// Try to find by class name
	FString ClassName = TypeName;
	if (!ClassName.StartsWith(TEXT("BTComposite_")))
	{
		ClassName = TEXT("BTComposite_") + TypeName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UBTCompositeNode::StaticClass()) &&
			!It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

UClass* FEditBehaviorTreeTool::FindTaskClass(const FString& TypeName)
{
	FString ClassName = TypeName;
	if (!ClassName.StartsWith(TEXT("BTTask_")))
	{
		ClassName = TEXT("BTTask_") + TypeName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UBTTaskNode::StaticClass()) &&
			!It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

UClass* FEditBehaviorTreeTool::FindDecoratorClass(const FString& TypeName)
{
	FString ClassName = TypeName;
	if (!ClassName.StartsWith(TEXT("BTDecorator_")))
	{
		ClassName = TEXT("BTDecorator_") + TypeName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UBTDecorator::StaticClass()) &&
			!It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

UClass* FEditBehaviorTreeTool::FindServiceClass(const FString& TypeName)
{
	FString ClassName = TypeName;
	if (!ClassName.StartsWith(TEXT("BTService_")))
	{
		ClassName = TEXT("BTService_") + TypeName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UBTService::StaticClass()) &&
			!It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

// ========== Helper for Decorator Attachment ==========

bool FEditBehaviorTreeTool::AttachDecoratorToChildEdge(UBTCompositeNode* Parent, UBTNode* TargetNode, UBTDecorator* Decorator)
{
	if (!Parent || !TargetNode || !Decorator)
	{
		return false;
	}

	// Check children of this parent
	for (FBTCompositeChild& Child : Parent->Children)
	{
		if (Child.ChildComposite == TargetNode || Child.ChildTask == TargetNode)
		{
			// Found the child edge - attach decorator here
			Child.Decorators.Add(Decorator);
			return true;
		}

		// Recurse into composite children
		if (Child.ChildComposite)
		{
			if (AttachDecoratorToChildEdge(Child.ChildComposite, TargetNode, Decorator))
			{
				return true;
			}
		}
	}

	return false;
}

// ========== Behavior Tree Add/Remove Operations ==========

FString FEditBehaviorTreeTool::AddComposite(UBehaviorTree* BehaviorTree, const FCompositeDefinition& CompDef)
{
	if (CompDef.Type.IsEmpty())
	{
		return TEXT("! Composite: Missing type");
	}

	UClass* CompositeClass = FindCompositeClass(CompDef.Type);
	if (!CompositeClass)
	{
		return FString::Printf(TEXT("! Composite: Unknown type '%s'"), *CompDef.Type);
	}

	// Create the composite node
	UBTCompositeNode* NewNode = NewObject<UBTCompositeNode>(BehaviorTree, CompositeClass);
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Composite: Failed to create '%s'"), *CompDef.Type);
	}

	// Set node name
	if (!CompDef.Name.IsEmpty())
	{
		NewNode->NodeName = CompDef.Name;
	}

	// Find parent or set as root
	if (CompDef.Parent.IsEmpty())
	{
		// Set as root
		if (BehaviorTree->RootNode)
		{
			return TEXT("! Composite: Root already exists. Specify 'parent' to add as child.");
		}
		BehaviorTree->RootNode = NewNode;
	}
	else
	{
		UBTCompositeNode* ParentNode = FindCompositeByName(BehaviorTree->RootNode, CompDef.Parent);
		if (!ParentNode)
		{
			return FString::Printf(TEXT("! Composite: Parent '%s' not found"), *CompDef.Parent);
		}

		// Add as child
		FBTCompositeChild NewChild;
		NewChild.ChildComposite = NewNode;

		if (CompDef.Index >= 0 && CompDef.Index < ParentNode->Children.Num())
		{
			ParentNode->Children.Insert(NewChild, CompDef.Index);
		}
		else
		{
			ParentNode->Children.Add(NewChild);
		}
	}

	FString NodeName = CompDef.Name.IsEmpty() ? CompDef.Type : CompDef.Name;
	FString ParentStr = CompDef.Parent.IsEmpty() ? TEXT("(root)") : CompDef.Parent;
	return FString::Printf(TEXT("+ Composite: %s (%s) -> %s"), *NodeName, *CompDef.Type, *ParentStr);
}

FString FEditBehaviorTreeTool::AddTask(UBehaviorTree* BehaviorTree, const FTaskDefinition& TaskDef)
{
	if (TaskDef.Type.IsEmpty())
	{
		return TEXT("! Task: Missing type");
	}
	if (TaskDef.Parent.IsEmpty())
	{
		return TEXT("! Task: Missing parent (tasks must be added to a composite)");
	}

	UClass* TaskClass = FindTaskClass(TaskDef.Type);
	if (!TaskClass)
	{
		return FString::Printf(TEXT("! Task: Unknown type '%s'"), *TaskDef.Type);
	}

	// Find parent
	UBTCompositeNode* ParentNode = FindCompositeByName(BehaviorTree->RootNode, TaskDef.Parent);
	if (!ParentNode)
	{
		return FString::Printf(TEXT("! Task: Parent '%s' not found"), *TaskDef.Parent);
	}

	// Create the task node
	UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BehaviorTree, TaskClass);
	if (!NewTask)
	{
		return FString::Printf(TEXT("! Task: Failed to create '%s'"), *TaskDef.Type);
	}

	// Set node name
	if (!TaskDef.Name.IsEmpty())
	{
		NewTask->NodeName = TaskDef.Name;
	}

	// Add as child
	FBTCompositeChild NewChild;
	NewChild.ChildTask = NewTask;

	if (TaskDef.Index >= 0 && TaskDef.Index < ParentNode->Children.Num())
	{
		ParentNode->Children.Insert(NewChild, TaskDef.Index);
	}
	else
	{
		ParentNode->Children.Add(NewChild);
	}

	FString NodeName = TaskDef.Name.IsEmpty() ? TaskDef.Type : TaskDef.Name;
	return FString::Printf(TEXT("+ Task: %s (%s) -> %s"), *NodeName, *TaskDef.Type, *TaskDef.Parent);
}

FString FEditBehaviorTreeTool::AddDecorator(UBehaviorTree* BehaviorTree, const FDecoratorDefinition& DecDef)
{
	if (DecDef.Type.IsEmpty())
	{
		return TEXT("! Decorator: Missing type");
	}
	if (DecDef.Target.IsEmpty())
	{
		return TEXT("! Decorator: Missing target node");
	}

	UClass* DecoratorClass = FindDecoratorClass(DecDef.Type);
	if (!DecoratorClass)
	{
		return FString::Printf(TEXT("! Decorator: Unknown type '%s'"), *DecDef.Type);
	}

	// Find target - could be composite or task
	UBTCompositeNode* TargetComposite = FindCompositeByName(BehaviorTree->RootNode, DecDef.Target);
	UBTTaskNode* TargetTask = TargetComposite ? nullptr : FindTaskByName(BehaviorTree->RootNode, DecDef.Target);

	if (!TargetComposite && !TargetTask)
	{
		return FString::Printf(TEXT("! Decorator: Target '%s' not found"), *DecDef.Target);
	}

	// Create decorator
	UBTDecorator* NewDecorator = NewObject<UBTDecorator>(BehaviorTree, DecoratorClass);
	if (!NewDecorator)
	{
		return FString::Printf(TEXT("! Decorator: Failed to create '%s'"), *DecDef.Type);
	}

	if (!DecDef.Name.IsEmpty())
	{
		NewDecorator->NodeName = DecDef.Name;
	}

	// Decorators are attached to edges (FBTCompositeChild), not directly to nodes
	// For root node, use BehaviorTree->RootDecorators
	bool bAttached = false;

	if (TargetComposite == BehaviorTree->RootNode)
	{
		// Target is root - add to root decorators
		BehaviorTree->RootDecorators.Add(NewDecorator);
		bAttached = true;
	}
	else
	{
		// Find the parent and attach to the child edge
		UBTNode* TargetNode = TargetComposite ? (UBTNode*)TargetComposite : (UBTNode*)TargetTask;
		bAttached = AttachDecoratorToChildEdge(BehaviorTree->RootNode, TargetNode, NewDecorator);
	}

	if (!bAttached)
	{
		NewDecorator->ConditionalBeginDestroy();
		return FString::Printf(TEXT("! Decorator: Failed to attach to '%s' - no valid parent edge"), *DecDef.Target);
	}

	FString NodeName = DecDef.Name.IsEmpty() ? DecDef.Type : DecDef.Name;
	return FString::Printf(TEXT("+ Decorator: %s (%s) -> %s"), *NodeName, *DecDef.Type, *DecDef.Target);
}

FString FEditBehaviorTreeTool::AddService(UBehaviorTree* BehaviorTree, const FServiceDefinition& SvcDef)
{
	if (SvcDef.Type.IsEmpty())
	{
		return TEXT("! Service: Missing type");
	}
	if (SvcDef.Target.IsEmpty())
	{
		return TEXT("! Service: Missing target composite");
	}

	UClass* ServiceClass = FindServiceClass(SvcDef.Type);
	if (!ServiceClass)
	{
		return FString::Printf(TEXT("! Service: Unknown type '%s'"), *SvcDef.Type);
	}

	// Find target composite (services only attach to composites)
	UBTCompositeNode* TargetComposite = FindCompositeByName(BehaviorTree->RootNode, SvcDef.Target);
	if (!TargetComposite)
	{
		return FString::Printf(TEXT("! Service: Target composite '%s' not found"), *SvcDef.Target);
	}

	// Create service
	UBTService* NewService = NewObject<UBTService>(BehaviorTree, ServiceClass);
	if (!NewService)
	{
		return FString::Printf(TEXT("! Service: Failed to create '%s'"), *SvcDef.Type);
	}

	if (!SvcDef.Name.IsEmpty())
	{
		NewService->NodeName = SvcDef.Name;
	}

	TargetComposite->Services.Add(NewService);

	FString NodeName = SvcDef.Name.IsEmpty() ? SvcDef.Type : SvcDef.Name;
	return FString::Printf(TEXT("+ Service: %s (%s) -> %s"), *NodeName, *SvcDef.Type, *SvcDef.Target);
}

FString FEditBehaviorTreeTool::RemoveNode(UBehaviorTree* BehaviorTree, const FString& NodeName)
{
	// This is a simplified implementation - removing nodes from BTs is complex
	// because of the parent-child relationships

	if (!BehaviorTree->RootNode)
	{
		return FString::Printf(TEXT("! Remove: Tree is empty"));
	}

	// Check if removing root
	if (BehaviorTree->RootNode->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
	{
		BehaviorTree->RootNode = nullptr;
		return FString::Printf(TEXT("- Node: %s (was root)"), *NodeName);
	}

	// TODO: Implement recursive search and removal from parent's children array
	// This requires finding the parent of the node to remove it from Children

	return FString::Printf(TEXT("! Remove: Not implemented for non-root nodes yet"));
}

FString FEditBehaviorTreeTool::SetBlackboard(UBehaviorTree* BehaviorTree, const FString& BlackboardName)
{
	// Try to find the blackboard asset
	FString BlackboardPath = BlackboardName;
	if (!BlackboardPath.StartsWith(TEXT("/")))
	{
		BlackboardPath = FString::Printf(TEXT("/Game/AI/%s.%s"), *BlackboardName, *BlackboardName);
	}

	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!Blackboard)
	{
		// Try without path
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(UBlackboardData::StaticClass()->GetClassPathName(), Assets);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(BlackboardName, ESearchCase::IgnoreCase))
			{
				Blackboard = Cast<UBlackboardData>(Asset.GetAsset());
				break;
			}
		}
	}

	if (!Blackboard)
	{
		return FString::Printf(TEXT("! Blackboard: '%s' not found"), *BlackboardName);
	}

	BehaviorTree->BlackboardAsset = Blackboard;
	return FString::Printf(TEXT("+ Blackboard: Set to %s"), *Blackboard->GetName());
}

// ========== Blackboard Key Operations ==========

UClass* FEditBehaviorTreeTool::FindBlackboardKeyTypeClass(const FString& TypeName)
{
	if (TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Bool::StaticClass();
	}
	if (TypeName.Equals(TEXT("Int"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Int::StaticClass();
	}
	if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Float::StaticClass();
	}
	if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_String::StaticClass();
	}
	if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Name::StaticClass();
	}
	if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Vector::StaticClass();
	}
	if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Rotator::StaticClass();
	}
	if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Object::StaticClass();
	}
	if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Class::StaticClass();
	}
	if (TypeName.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Enum::StaticClass();
	}

	return nullptr;
}

FString FEditBehaviorTreeTool::AddBlackboardKey(UBlackboardData* Blackboard, const FBlackboardKeyDefinition& KeyDef)
{
	if (KeyDef.Name.IsEmpty())
	{
		return TEXT("! Key: Missing name");
	}
	if (KeyDef.Type.IsEmpty())
	{
		return TEXT("! Key: Missing type");
	}

	// Check if key already exists
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (Entry.EntryName.ToString().Equals(KeyDef.Name, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT("! Key: '%s' already exists"), *KeyDef.Name);
		}
	}

	// Find key type class
	UClass* KeyTypeClass = FindBlackboardKeyTypeClass(KeyDef.Type);
	if (!KeyTypeClass)
	{
		return FString::Printf(TEXT("! Key: Unknown type '%s'"), *KeyDef.Type);
	}

	// Create the key type instance
	UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(Blackboard, KeyTypeClass);
	if (!KeyType)
	{
		return FString::Printf(TEXT("! Key: Failed to create type '%s'"), *KeyDef.Type);
	}

	// Create entry
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyDef.Name);
	NewEntry.KeyType = KeyType;
	NewEntry.bInstanceSynced = KeyDef.bInstanceSynced;

	if (!KeyDef.Category.IsEmpty())
	{
		NewEntry.EntryCategory = FName(*KeyDef.Category);
	}

	Blackboard->Keys.Add(NewEntry);

	FString Flags = KeyDef.bInstanceSynced ? TEXT(" [Synced]") : TEXT("");
	return FString::Printf(TEXT("+ Key: %s (%s)%s"), *KeyDef.Name, *KeyDef.Type, *Flags);
}

FString FEditBehaviorTreeTool::RemoveBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName)
{
	for (int32 i = Blackboard->Keys.Num() - 1; i >= 0; i--)
	{
		if (Blackboard->Keys[i].EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase))
		{
			Blackboard->Keys.RemoveAt(i);
			return FString::Printf(TEXT("- Key: %s"), *KeyName);
		}
	}

	return FString::Printf(TEXT("! Key: '%s' not found"), *KeyName);
}
