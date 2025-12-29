// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBehaviorTree;
class UBlackboardData;
class UBTCompositeNode;
class UBTTaskNode;
class UBTNode;
class UBTDecorator;
class UBTService;

/**
 * Tool for editing Behavior Trees and Blackboards:
 * - Add/remove composite nodes (Selector, Sequence, Parallel)
 * - Add/remove task nodes
 * - Add/remove decorators
 * - Add/remove services
 * - Add/remove blackboard keys
 * - Set blackboard on behavior tree
 */
class NEOSTACK_API FEditBehaviorTreeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_behavior_tree"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Behavior Trees and Blackboards: add/remove nodes, decorators, services, and keys");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// ========== Definitions ==========

	/** Composite node definition (Selector, Sequence, Parallel) */
	struct FCompositeDefinition
	{
		FString Type;       // Selector, Sequence, Parallel, SimpleParallel
		FString Name;       // Node name
		FString Parent;     // Parent composite (empty = root)
		int32 Index = -1;   // Child index in parent (-1 = append)
	};

	/** Task node definition */
	struct FTaskDefinition
	{
		FString Type;       // Task class name (e.g., "MoveTo", "Wait", "RunBehavior")
		FString Name;       // Node name
		FString Parent;     // Parent composite
		int32 Index = -1;   // Child index in parent (-1 = append)
	};

	/** Decorator definition */
	struct FDecoratorDefinition
	{
		FString Type;       // Decorator class (e.g., "Blackboard", "CoolDown", "Loop")
		FString Name;       // Node name
		FString Target;     // Target node name to attach to
	};

	/** Service definition */
	struct FServiceDefinition
	{
		FString Type;       // Service class (e.g., "DefaultFocus", "RunEQS")
		FString Name;       // Node name
		FString Target;     // Target composite to attach to
	};

	/** Blackboard key definition */
	struct FBlackboardKeyDefinition
	{
		FString Name;       // Key name
		FString Type;       // Key type (Bool, Int, Float, String, Name, Vector, Rotator, Object, Class, Enum)
		FString BaseClass;  // For Object/Class types, the base class name
		FString Category;   // Optional category
		bool bInstanceSynced = false;
	};

	// ========== Behavior Tree Operations ==========

	/** Find a BT node by name (searches recursively) */
	UBTCompositeNode* FindCompositeByName(UBTCompositeNode* Root, const FString& Name);

	/** Find a task node by name */
	UBTTaskNode* FindTaskByName(UBTCompositeNode* Root, const FString& Name);

	/** Find the composite class by type name */
	UClass* FindCompositeClass(const FString& TypeName);

	/** Find the task class by type name */
	UClass* FindTaskClass(const FString& TypeName);

	/** Find the decorator class by type name */
	UClass* FindDecoratorClass(const FString& TypeName);

	/** Find the service class by type name */
	UClass* FindServiceClass(const FString& TypeName);

	/** Attach a decorator to the child edge that contains the target node */
	bool AttachDecoratorToChildEdge(UBTCompositeNode* Parent, UBTNode* TargetNode, UBTDecorator* Decorator);

	/** Add a composite node to the behavior tree */
	FString AddComposite(UBehaviorTree* BehaviorTree, const FCompositeDefinition& CompDef);

	/** Add a task node to the behavior tree */
	FString AddTask(UBehaviorTree* BehaviorTree, const FTaskDefinition& TaskDef);

	/** Add a decorator to a node */
	FString AddDecorator(UBehaviorTree* BehaviorTree, const FDecoratorDefinition& DecDef);

	/** Add a service to a composite */
	FString AddService(UBehaviorTree* BehaviorTree, const FServiceDefinition& SvcDef);

	/** Remove a node from the behavior tree */
	FString RemoveNode(UBehaviorTree* BehaviorTree, const FString& NodeName);

	/** Set the blackboard asset for the behavior tree */
	FString SetBlackboard(UBehaviorTree* BehaviorTree, const FString& BlackboardName);

	// ========== Blackboard Operations ==========

	/** Find a blackboard key type class by name */
	UClass* FindBlackboardKeyTypeClass(const FString& TypeName);

	/** Add a key to the blackboard */
	FString AddBlackboardKey(UBlackboardData* Blackboard, const FBlackboardKeyDefinition& KeyDef);

	/** Remove a key from the blackboard */
	FString RemoveBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName);
};
