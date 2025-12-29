// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;
class USCS_Node;
class UWidgetBlueprint;
class UWidgetTree;
class UWidget;
class UPanelWidget;
class UAnimBlueprint;
class UAnimGraphNode_StateMachine;
class UAnimationStateMachineGraph;
class UAnimStateNode;

/**
 * Tool for editing Blueprint assets:
 * - Add/remove variables with full type support
 * - Add/remove components with property setup
 * - Add/remove custom functions with inputs/outputs
 * - Add/remove event dispatchers with parameters
 * - Add/remove widgets in Widget Blueprints
 * - Add state machines, states, and transitions in Animation Blueprints
 */
class NEOSTACK_API FEditBlueprintTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_blueprint"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Blueprint assets: add/remove variables, components, functions, and events");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Type definition parsed from JSON */
	struct FTypeDefinition
	{
		FString Base;       // Boolean, Float, Object, etc.
		FString Container;  // Single, Array, Set, Map
		FString Subtype;    // For Object/Class/Structure/Interface/Enum
		TSharedPtr<FTypeDefinition> KeyType; // For Map
	};

	/** Variable definition */
	struct FVariableDefinition
	{
		FString Name;
		FTypeDefinition Type;
		FString Default;
		FString Category;
		bool bReplicated = false;
		bool bRepNotify = false;
		bool bExposeOnSpawn = false;
		bool bPrivate = false;
		bool bTransient = false;
	};

	/** Component definition */
	struct FComponentDefinition
	{
		FString Name;
		FString Class;
		FString Parent;
		TSharedPtr<FJsonObject> Properties;
	};

	/** Function parameter */
	struct FFunctionParam
	{
		FString Name;
		FTypeDefinition Type;
	};

	/** Function definition */
	struct FFunctionDefinition
	{
		FString Name;
		bool bPure = false;
		FString Category;
		TArray<FFunctionParam> Inputs;
		TArray<FFunctionParam> Outputs;
	};

	/** Event definition */
	struct FEventDefinition
	{
		FString Name;
		TArray<FFunctionParam> Params;
	};

	/** Widget definition for Widget Blueprints */
	struct FWidgetDefinition
	{
		FString Type;   // Widget class (Button, TextBlock, CanvasPanel, etc.)
		FString Name;   // Widget name (must be unique)
		FString Parent; // Parent widget name (empty = root)
	};

	/** Event binding definition - works for both Widget and regular Blueprints */
	struct FEventBindingDef
	{
		FString Source;   // Component name (BP) or Widget name (WBP)
		FString Event;    // Delegate name (OnClicked, OnComponentBeginOverlap, etc.)
		FString Handler;  // Blueprint function to call
	};

	/** Info about a bindable event/delegate */
	struct FEventInfo
	{
		FString Name;       // Delegate name (OnClicked, OnComponentBeginOverlap)
		FString Signature;  // Parameter signature
	};

	/** Parse type definition from JSON */
	FTypeDefinition ParseTypeDefinition(const TSharedPtr<FJsonObject>& TypeObj);

	/** Parse function parameter from JSON */
	FFunctionParam ParseFunctionParam(const TSharedPtr<FJsonObject>& ParamObj);

	/** Convert type definition to FEdGraphPinType */
	FEdGraphPinType TypeDefinitionToPinType(const FTypeDefinition& TypeDef);

	/** Find UClass for a type name */
	UClass* FindClassByName(const FString& ClassName);

	/** Find UScriptStruct for a struct name */
	UScriptStruct* FindStructByName(const FString& StructName);

	/** Find UEnum for an enum name */
	UEnum* FindEnumByName(const FString& EnumName);

	/** Add a variable to the Blueprint */
	FString AddVariable(UBlueprint* Blueprint, const FVariableDefinition& VarDef);

	/** Remove a variable from the Blueprint */
	FString RemoveVariable(UBlueprint* Blueprint, const FString& VarName);

	/** Add a component to the Blueprint */
	FString AddComponent(UBlueprint* Blueprint, const FComponentDefinition& CompDef);

	/** Remove a component from the Blueprint */
	FString RemoveComponent(UBlueprint* Blueprint, const FString& CompName);

	/** Add a function to the Blueprint */
	FString AddFunction(UBlueprint* Blueprint, const FFunctionDefinition& FuncDef);

	/** Remove a function from the Blueprint */
	FString RemoveFunction(UBlueprint* Blueprint, const FString& FuncName);

	/** Add an event dispatcher to the Blueprint */
	FString AddEvent(UBlueprint* Blueprint, const FEventDefinition& EventDef);

	/** Remove an event dispatcher from the Blueprint */
	FString RemoveEvent(UBlueprint* Blueprint, const FString& EventName);

	/** Set default value on a variable */
	void SetVariableDefaultValue(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValue);

	/** Set property on a component */
	void SetComponentProperty(USCS_Node* Node, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value);

	// Widget Blueprint operations
	/** Add a widget to a Widget Blueprint */
	FString AddWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetDefinition& WidgetDef);

	/** Remove a widget from a Widget Blueprint */
	FString RemoveWidget(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);

	/** Find widget class by name */
	UClass* FindWidgetClass(const FString& TypeName);

	/** Find widget in tree by name */
	UWidget* FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name);

	/** Refresh widget editor if open */
	void RefreshWidgetEditor(UWidgetBlueprint* WidgetBlueprint);

	// Event binding operations (unified for both Widget and regular Blueprints)

	/** List available events on a component or widget */
	FString ListEvents(UBlueprint* Blueprint, const FString& SourceName);

	/** List events on a component in a regular Blueprint */
	TArray<FEventInfo> ListComponentEvents(UBlueprint* Blueprint, const FString& ComponentName);

	/** List events on a widget in a Widget Blueprint */
	TArray<FEventInfo> ListWidgetEvents(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);

	/** Bind an event - routes to widget or component binding based on Blueprint type */
	FString BindEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef);

	/** Bind widget event using FDelegateEditorBinding */
	FString BindWidgetEvent(UWidgetBlueprint* WidgetBlueprint, const FEventBindingDef& EventDef);

	/** Bind component event by creating UK2Node_ComponentBoundEvent */
	FString BindComponentEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef);

	/** Unbind an event */
	FString UnbindEvent(UBlueprint* Blueprint, const FString& Source, const FString& Event);

	// Component discovery helpers

	/** Result of component discovery */
	struct FComponentDiscoveryResult
	{
		UActorComponent* ComponentTemplate = nullptr;
		USCS_Node* SCSNode = nullptr;           // Set if found in SCS
		FName VariableName = NAME_None;
		bool bFoundInSCS = false;
		bool bFoundInCDO = false;
	};

	/**
	 * Find a component by name - checks both SCS and CDO for completeness
	 * SCS catches recently added components (before compilation)
	 * CDO catches compiled components (after compilation)
	 */
	FComponentDiscoveryResult FindComponentByName(UBlueprint* Blueprint, const FString& ComponentName);

	// Animation Blueprint state machine operations

	/** State machine definition */
	struct FStateMachineDefinition
	{
		FString Name;   // State machine name
	};

	/** Animation state definition */
	struct FAnimStateDefinition
	{
		FString Name;           // State name
		FString StateMachine;   // Parent state machine name
	};

	/** State transition definition */
	struct FStateTransitionDefinition
	{
		FString StateMachine;   // Parent state machine name
		FString FromState;      // Source state name (or "[Entry]" for entry point)
		FString ToState;        // Target state name
	};

	/** Find the AnimGraph in an Animation Blueprint */
	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint);

	/** Find a state machine node by name in the AnimGraph */
	UAnimGraphNode_StateMachine* FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName);

	/** Find a state node by name in a state machine graph */
	UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName);

	/** Add a state machine to an Animation Blueprint */
	FString AddStateMachine(UAnimBlueprint* AnimBlueprint, const FStateMachineDefinition& SMDef);

	/** Add a state to a state machine */
	FString AddAnimState(UAnimBlueprint* AnimBlueprint, const FAnimStateDefinition& StateDef);

	/**
	 * Add a transition between states
	 * Creates a transition node and a transition graph where condition logic can be added
	 * Returns info about the transition graph and result node for wiring condition logic
	 */
	FString AddStateTransition(UAnimBlueprint* AnimBlueprint, const FStateTransitionDefinition& TransDef);
};
