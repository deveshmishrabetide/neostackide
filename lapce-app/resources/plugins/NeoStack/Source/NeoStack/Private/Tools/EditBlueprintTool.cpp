// Copyright NeoStack. All Rights Reserved.

#include "Tools/EditBlueprintTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"

// Blueprint editing
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyAccessUtil.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"

// Widget Blueprint support
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WrapBox.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprintEditor.h"

// Animation Blueprint support
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimationStateMachineSchema.h"
#include "Kismet2/Kismet2NameValidators.h"

FToolResult FEditBlueprintTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Build asset path and load Blueprint
	if (!Path.StartsWith(TEXT("/Game")))
	{
		Path = FString::Printf(TEXT("/Game/%s"), *Path);
	}

	FString FullAssetPath = Path / Name + TEXT(".") + Name;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *FullAssetPath);

	if (!Blueprint)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Blueprint not found: %s"), *FullAssetPath));
	}

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;

	// Process add_variables
	const TArray<TSharedPtr<FJsonValue>>* AddVariables;
	if (Args->TryGetArrayField(TEXT("add_variables"), AddVariables))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddVariables)
		{
			const TSharedPtr<FJsonObject>* VarObj;
			if (Value->TryGetObject(VarObj))
			{
				FVariableDefinition VarDef;
				(*VarObj)->TryGetStringField(TEXT("name"), VarDef.Name);

				const TSharedPtr<FJsonObject>* TypeObj;
				if ((*VarObj)->TryGetObjectField(TEXT("type"), TypeObj))
				{
					VarDef.Type = ParseTypeDefinition(*TypeObj);
				}

				(*VarObj)->TryGetStringField(TEXT("default"), VarDef.Default);
				(*VarObj)->TryGetStringField(TEXT("category"), VarDef.Category);
				(*VarObj)->TryGetBoolField(TEXT("replicated"), VarDef.bReplicated);
				(*VarObj)->TryGetBoolField(TEXT("rep_notify"), VarDef.bRepNotify);
				(*VarObj)->TryGetBoolField(TEXT("expose_on_spawn"), VarDef.bExposeOnSpawn);
				(*VarObj)->TryGetBoolField(TEXT("private"), VarDef.bPrivate);
				(*VarObj)->TryGetBoolField(TEXT("transient"), VarDef.bTransient);

				FString Result = AddVariable(Blueprint, VarDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_variables
	const TArray<TSharedPtr<FJsonValue>>* RemoveVariables;
	if (Args->TryGetArrayField(TEXT("remove_variables"), RemoveVariables))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveVariables)
		{
			FString VarName;
			if (Value->TryGetString(VarName))
			{
				FString Result = RemoveVariable(Blueprint, VarName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_components
	const TArray<TSharedPtr<FJsonValue>>* AddComponents;
	if (Args->TryGetArrayField(TEXT("add_components"), AddComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddComponents)
		{
			const TSharedPtr<FJsonObject>* CompObj;
			if (Value->TryGetObject(CompObj))
			{
				FComponentDefinition CompDef;
				(*CompObj)->TryGetStringField(TEXT("name"), CompDef.Name);
				(*CompObj)->TryGetStringField(TEXT("class"), CompDef.Class);
				(*CompObj)->TryGetStringField(TEXT("parent"), CompDef.Parent);

				const TSharedPtr<FJsonObject>* PropsObj;
				if ((*CompObj)->TryGetObjectField(TEXT("properties"), PropsObj))
				{
					CompDef.Properties = *PropsObj;
				}

				FString Result = AddComponent(Blueprint, CompDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_components
	const TArray<TSharedPtr<FJsonValue>>* RemoveComponents;
	if (Args->TryGetArrayField(TEXT("remove_components"), RemoveComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveComponents)
		{
			FString CompName;
			if (Value->TryGetString(CompName))
			{
				FString Result = RemoveComponent(Blueprint, CompName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_functions
	const TArray<TSharedPtr<FJsonValue>>* AddFunctions;
	if (Args->TryGetArrayField(TEXT("add_functions"), AddFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddFunctions)
		{
			const TSharedPtr<FJsonObject>* FuncObj;
			if (Value->TryGetObject(FuncObj))
			{
				FFunctionDefinition FuncDef;
				(*FuncObj)->TryGetStringField(TEXT("name"), FuncDef.Name);
				(*FuncObj)->TryGetBoolField(TEXT("pure"), FuncDef.bPure);
				(*FuncObj)->TryGetStringField(TEXT("category"), FuncDef.Category);

				const TArray<TSharedPtr<FJsonValue>>* Inputs;
				if ((*FuncObj)->TryGetArrayField(TEXT("inputs"), Inputs))
				{
					for (const TSharedPtr<FJsonValue>& InputVal : *Inputs)
					{
						const TSharedPtr<FJsonObject>* InputObj;
						if (InputVal->TryGetObject(InputObj))
						{
							FuncDef.Inputs.Add(ParseFunctionParam(*InputObj));
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Outputs;
				if ((*FuncObj)->TryGetArrayField(TEXT("outputs"), Outputs))
				{
					for (const TSharedPtr<FJsonValue>& OutputVal : *Outputs)
					{
						const TSharedPtr<FJsonObject>* OutputObj;
						if (OutputVal->TryGetObject(OutputObj))
						{
							FuncDef.Outputs.Add(ParseFunctionParam(*OutputObj));
						}
					}
				}

				FString Result = AddFunction(Blueprint, FuncDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_functions
	const TArray<TSharedPtr<FJsonValue>>* RemoveFunctions;
	if (Args->TryGetArrayField(TEXT("remove_functions"), RemoveFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveFunctions)
		{
			FString FuncName;
			if (Value->TryGetString(FuncName))
			{
				FString Result = RemoveFunction(Blueprint, FuncName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_events
	const TArray<TSharedPtr<FJsonValue>>* AddEvents;
	if (Args->TryGetArrayField(TEXT("add_events"), AddEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FEventDefinition EventDef;
				(*EventObj)->TryGetStringField(TEXT("name"), EventDef.Name);

				const TArray<TSharedPtr<FJsonValue>>* Params;
				if ((*EventObj)->TryGetArrayField(TEXT("params"), Params))
				{
					for (const TSharedPtr<FJsonValue>& ParamVal : *Params)
					{
						const TSharedPtr<FJsonObject>* ParamObj;
						if (ParamVal->TryGetObject(ParamObj))
						{
							EventDef.Params.Add(ParseFunctionParam(*ParamObj));
						}
					}
				}

				FString Result = AddEvent(Blueprint, EventDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_events
	const TArray<TSharedPtr<FJsonValue>>* RemoveEvents;
	if (Args->TryGetArrayField(TEXT("remove_events"), RemoveEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEvents)
		{
			FString EventName;
			if (Value->TryGetString(EventName))
			{
				FString Result = RemoveEvent(Blueprint, EventName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process widget operations (only for Widget Blueprints)
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);

	// Process list_events - discover available events on a component/widget
	FString ListEventsSource;
	if (Args->TryGetStringField(TEXT("list_events"), ListEventsSource) && !ListEventsSource.IsEmpty())
	{
		FString EventsOutput = ListEvents(Blueprint, ListEventsSource);
		Results.Add(EventsOutput);
	}

	// Process bind_events
	const TArray<TSharedPtr<FJsonValue>>* BindEvents;
	if (Args->TryGetArrayField(TEXT("bind_events"), BindEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BindEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FEventBindingDef EventDef;
				(*EventObj)->TryGetStringField(TEXT("source"), EventDef.Source);
				(*EventObj)->TryGetStringField(TEXT("event"), EventDef.Event);
				(*EventObj)->TryGetStringField(TEXT("handler"), EventDef.Handler);

				FString Result = BindEvent(Blueprint, EventDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process unbind_events
	const TArray<TSharedPtr<FJsonValue>>* UnbindEvents;
	if (Args->TryGetArrayField(TEXT("unbind_events"), UnbindEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *UnbindEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FString Source, Event;
				(*EventObj)->TryGetStringField(TEXT("source"), Source);
				(*EventObj)->TryGetStringField(TEXT("event"), Event);

				FString Result = UnbindEvent(Blueprint, Source, Event);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_widgets
	const TArray<TSharedPtr<FJsonValue>>* AddWidgets;
	if (Args->TryGetArrayField(TEXT("add_widgets"), AddWidgets))
	{
		if (!WidgetBlueprint)
		{
			Results.Add(TEXT("! Widgets: Not a Widget Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddWidgets)
			{
				const TSharedPtr<FJsonObject>* WidgetObj;
				if (Value->TryGetObject(WidgetObj))
				{
					FWidgetDefinition WidgetDef;
					(*WidgetObj)->TryGetStringField(TEXT("type"), WidgetDef.Type);
					(*WidgetObj)->TryGetStringField(TEXT("name"), WidgetDef.Name);
					(*WidgetObj)->TryGetStringField(TEXT("parent"), WidgetDef.Parent);

					FString Result = AddWidget(WidgetBlueprint, WidgetDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process remove_widgets
	const TArray<TSharedPtr<FJsonValue>>* RemoveWidgets;
	if (Args->TryGetArrayField(TEXT("remove_widgets"), RemoveWidgets))
	{
		if (!WidgetBlueprint)
		{
			Results.Add(TEXT("! Widgets: Not a Widget Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *RemoveWidgets)
			{
				FString WidgetName;
				if (Value->TryGetString(WidgetName))
				{
					FString Result = RemoveWidget(WidgetBlueprint, WidgetName);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("-"))) RemovedCount++;
				}
			}
		}
	}

	// Animation Blueprint operations
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);

	// Process add_state_machine
	const TArray<TSharedPtr<FJsonValue>>* AddStateMachines;
	if (Args->TryGetArrayField(TEXT("add_state_machine"), AddStateMachines))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! StateMachine: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddStateMachines)
			{
				const TSharedPtr<FJsonObject>* SMObj;
				if (Value->TryGetObject(SMObj))
				{
					FStateMachineDefinition SMDef;
					(*SMObj)->TryGetStringField(TEXT("name"), SMDef.Name);

					FString Result = AddStateMachine(AnimBlueprint, SMDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process add_anim_state
	const TArray<TSharedPtr<FJsonValue>>* AddAnimStates;
	if (Args->TryGetArrayField(TEXT("add_anim_state"), AddAnimStates))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! AnimState: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddAnimStates)
			{
				const TSharedPtr<FJsonObject>* StateObj;
				if (Value->TryGetObject(StateObj))
				{
					FAnimStateDefinition StateDef;
					(*StateObj)->TryGetStringField(TEXT("name"), StateDef.Name);
					(*StateObj)->TryGetStringField(TEXT("state_machine"), StateDef.StateMachine);

					FString Result = AddAnimState(AnimBlueprint, StateDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process add_state_transition
	const TArray<TSharedPtr<FJsonValue>>* AddTransitions;
	if (Args->TryGetArrayField(TEXT("add_state_transition"), AddTransitions))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! Transition: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddTransitions)
			{
				const TSharedPtr<FJsonObject>* TransObj;
				if (Value->TryGetObject(TransObj))
				{
					FStateTransitionDefinition TransDef;
					(*TransObj)->TryGetStringField(TEXT("state_machine"), TransDef.StateMachine);
					(*TransObj)->TryGetStringField(TEXT("from_state"), TransDef.FromState);
					(*TransObj)->TryGetStringField(TEXT("to_state"), TransDef.ToState);

					FString Result = AddStateTransition(AnimBlueprint, TransDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Mark dirty and compile
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Build output
	FString Output = FString::Printf(TEXT("# EDIT %s at %s\n"), *Name, *Path);
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed\n"), AddedCount, RemovedCount);

	return FToolResult::Ok(Output);
}

FEditBlueprintTool::FTypeDefinition FEditBlueprintTool::ParseTypeDefinition(const TSharedPtr<FJsonObject>& TypeObj)
{
	FTypeDefinition TypeDef;

	TypeObj->TryGetStringField(TEXT("base"), TypeDef.Base);
	TypeObj->TryGetStringField(TEXT("container"), TypeDef.Container);
	TypeObj->TryGetStringField(TEXT("subtype"), TypeDef.Subtype);

	if (TypeDef.Container.IsEmpty())
	{
		TypeDef.Container = TEXT("Single");
	}

	const TSharedPtr<FJsonObject>* KeyTypeObj;
	if (TypeObj->TryGetObjectField(TEXT("key_type"), KeyTypeObj))
	{
		TypeDef.KeyType = MakeShared<FTypeDefinition>(ParseTypeDefinition(*KeyTypeObj));
	}

	return TypeDef;
}

FEditBlueprintTool::FFunctionParam FEditBlueprintTool::ParseFunctionParam(const TSharedPtr<FJsonObject>& ParamObj)
{
	FFunctionParam Param;
	ParamObj->TryGetStringField(TEXT("name"), Param.Name);

	const TSharedPtr<FJsonObject>* TypeObj;
	if (ParamObj->TryGetObjectField(TEXT("type"), TypeObj))
	{
		Param.Type = ParseTypeDefinition(*TypeObj);
	}

	return Param;
}

FEdGraphPinType FEditBlueprintTool::TypeDefinitionToPinType(const FTypeDefinition& TypeDef)
{
	FEdGraphPinType PinType;

	// Set container type
	if (TypeDef.Container.Equals(TEXT("Array"), ESearchCase::IgnoreCase))
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else if (TypeDef.Container.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
	{
		PinType.ContainerType = EPinContainerType::Set;
	}
	else if (TypeDef.Container.Equals(TEXT("Map"), ESearchCase::IgnoreCase))
	{
		PinType.ContainerType = EPinContainerType::Map;
		if (TypeDef.KeyType.IsValid())
		{
			FEdGraphPinType KeyPinType = TypeDefinitionToPinType(*TypeDef.KeyType);
			PinType.PinValueType = FEdGraphTerminalType::FromPinType(KeyPinType);
		}
	}

	// Set base type
	FString Base = TypeDef.Base;

	if (Base.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Base.Equals(TEXT("Byte"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Base.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Base.Equals(TEXT("Integer64"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Base.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (Base.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Base.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Base.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Base.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Base.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Base.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Base.Equals(TEXT("Structure"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindStructByName(TypeDef.Subtype);
		}
	}
	else if (Base.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
		}
		else
		{
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
	}
	else if (Base.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
		}
		else
		{
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
	}
	else if (Base.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
		}
	}
	else if (Base.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
		}
	}
	else if (Base.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Interface;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
		}
	}
	else if (Base.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
		if (!TypeDef.Subtype.IsEmpty())
		{
			PinType.PinSubCategoryObject = FindEnumByName(TypeDef.Subtype);
		}
	}
	else
	{
		// Default to object if unknown
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}

	return PinType;
}

UClass* FEditBlueprintTool::FindClassByName(const FString& ClassName)
{
	// Try common prefixes
	TArray<FString> SearchNames;
	SearchNames.Add(ClassName);
	SearchNames.Add(TEXT("A") + ClassName); // AActor, ACharacter
	SearchNames.Add(TEXT("U") + ClassName); // UObject, UComponent

	for (const FString& SearchName : SearchNames)
	{
		// Try to find native class
		UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *SearchName));
		if (FoundClass) return FoundClass;

		FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *SearchName));
		if (FoundClass) return FoundClass;

		// Search all classes
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	// Try loading as Blueprint
	FString BPPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *ClassName, *ClassName);
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (BP && BP->GeneratedClass)
	{
		return BP->GeneratedClass;
	}

	// Search asset registry for Blueprint
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(ClassName, ESearchCase::IgnoreCase))
		{
			UBlueprint* FoundBP = Cast<UBlueprint>(Asset.GetAsset());
			if (FoundBP && FoundBP->GeneratedClass)
			{
				return FoundBP->GeneratedClass;
			}
		}
	}

	return nullptr;
}

UScriptStruct* FEditBlueprintTool::FindStructByName(const FString& StructName)
{
	FString SearchName = StructName;
	if (!SearchName.StartsWith(TEXT("F")))
	{
		SearchName = TEXT("F") + StructName;
	}

	// Search common modules
	TArray<FString> Modules = { TEXT("Engine"), TEXT("CoreUObject"), TEXT("InputCore"), TEXT("SlateCore") };

	for (const FString& Module : Modules)
	{
		FString Path = FString::Printf(TEXT("/Script/%s.%s"), *Module, *SearchName);
		UScriptStruct* Found = FindObject<UScriptStruct>(nullptr, *Path);
		if (Found) return Found;
	}

	// Search all structs
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(StructName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	return nullptr;
}

UEnum* FEditBlueprintTool::FindEnumByName(const FString& EnumName)
{
	FString SearchName = EnumName;
	if (!SearchName.StartsWith(TEXT("E")))
	{
		SearchName = TEXT("E") + EnumName;
	}

	// Search all enums
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(EnumName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	return nullptr;
}

FString FEditBlueprintTool::AddVariable(UBlueprint* Blueprint, const FVariableDefinition& VarDef)
{
	if (VarDef.Name.IsEmpty())
	{
		return TEXT("! Variable: Missing name");
	}

	FName VarName = FName(*VarDef.Name);

	// Check if variable already exists
	for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
	{
		if (Existing.VarName == VarName)
		{
			return FString::Printf(TEXT("! Variable: %s already exists"), *VarDef.Name);
		}
	}

	// Create pin type
	FEdGraphPinType PinType = TypeDefinitionToPinType(VarDef.Type);

	// Add the variable
	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType);
	if (!bSuccess)
	{
		return FString::Printf(TEXT("! Variable: Failed to add %s"), *VarDef.Name);
	}

	// Find the variable we just added and set properties
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			// Set category
			if (!VarDef.Category.IsEmpty())
			{
				Var.Category = FText::FromString(VarDef.Category);
			}

			// Set flags
			if (VarDef.bReplicated)
			{
				Var.PropertyFlags |= CPF_Net;
			}
			if (VarDef.bRepNotify)
			{
				Var.PropertyFlags |= CPF_Net | CPF_RepNotify;
				Var.ReplicationCondition = COND_None;
				Var.RepNotifyFunc = FName(*FString::Printf(TEXT("OnRep_%s"), *VarDef.Name));
			}
			if (VarDef.bExposeOnSpawn)
			{
				Var.PropertyFlags |= CPF_ExposeOnSpawn;
			}
			if (VarDef.bPrivate)
			{
				Var.PropertyFlags |= CPF_DisableEditOnInstance;
			}
			if (VarDef.bTransient)
			{
				Var.PropertyFlags |= CPF_Transient;
			}

			break;
		}
	}

	// Set default value if provided
	if (!VarDef.Default.IsEmpty())
	{
		SetVariableDefaultValue(Blueprint, VarDef.Name, VarDef.Default);
	}

	// Build result string
	FString TypeStr = VarDef.Type.Base;
	if (!VarDef.Type.Subtype.IsEmpty())
	{
		TypeStr += TEXT("<") + VarDef.Type.Subtype + TEXT(">");
	}
	if (!VarDef.Type.Container.Equals(TEXT("Single"), ESearchCase::IgnoreCase))
	{
		TypeStr = VarDef.Type.Container + TEXT("<") + TypeStr + TEXT(">");
	}

	FString Flags;
	if (VarDef.bReplicated) Flags += TEXT(" [Replicated]");
	if (VarDef.bRepNotify) Flags += TEXT(" [RepNotify]");
	if (VarDef.bExposeOnSpawn) Flags += TEXT(" [ExposeOnSpawn]");

	FString DefaultStr;
	if (!VarDef.Default.IsEmpty())
	{
		DefaultStr = FString::Printf(TEXT(" = %s"), *VarDef.Default);
	}

	return FString::Printf(TEXT("+ Variable: %s (%s)%s%s"), *VarDef.Name, *TypeStr, *DefaultStr, *Flags);
}

FString FEditBlueprintTool::RemoveVariable(UBlueprint* Blueprint, const FString& VarName)
{
	FName Name = FName(*VarName);

	for (int32 i = Blueprint->NewVariables.Num() - 1; i >= 0; i--)
	{
		if (Blueprint->NewVariables[i].VarName == Name)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
			return FString::Printf(TEXT("- Variable: %s"), *VarName);
		}
	}

	return FString::Printf(TEXT("! Variable: %s not found"), *VarName);
}

void FEditBlueprintTool::SetVariableDefaultValue(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValue)
{
	// Find the property
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass) return;

	FProperty* Property = FindFProperty<FProperty>(GeneratedClass, FName(*VarName));
	if (!Property) return;

	// Get CDO
	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO) return;

	// Import the default value
	Property->ImportText_Direct(*DefaultValue, Property->ContainerPtrToValuePtr<void>(CDO), CDO, PPF_None);
}

FString FEditBlueprintTool::AddComponent(UBlueprint* Blueprint, const FComponentDefinition& CompDef)
{
	if (CompDef.Name.IsEmpty() || CompDef.Class.IsEmpty())
	{
		return TEXT("! Component: Missing name or class");
	}

	// Find component class
	UClass* ComponentClass = FindClassByName(CompDef.Class);
	if (!ComponentClass)
	{
		// Try with Component suffix
		ComponentClass = FindClassByName(CompDef.Class + TEXT("Component"));
	}
	if (!ComponentClass)
	{
		return FString::Printf(TEXT("! Component: Class not found: %s"), *CompDef.Class);
	}

	// Ensure SCS exists
	if (!Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint);
	}

	// Check if component already exists
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(CompDef.Name, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT("! Component: %s already exists"), *CompDef.Name);
		}
	}

	// Create the node
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*CompDef.Name));
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Component: Failed to create %s"), *CompDef.Name);
	}

	// Find parent node
	USCS_Node* ParentNode = nullptr;
	if (!CompDef.Parent.IsEmpty())
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(CompDef.Parent, ESearchCase::IgnoreCase))
			{
				ParentNode = Node;
				break;
			}
		}
	}

	// Add to hierarchy
	if (ParentNode)
	{
		ParentNode->AddChildNode(NewNode);
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	// Set properties if provided
	if (CompDef.Properties.IsValid() && NewNode->ComponentTemplate)
	{
		for (const auto& Pair : CompDef.Properties->Values)
		{
			SetComponentProperty(NewNode, Pair.Key, Pair.Value);
		}
	}

	FString ParentStr = CompDef.Parent.IsEmpty() ? TEXT("Root") : CompDef.Parent;
	return FString::Printf(TEXT("+ Component: %s (%s) -> %s"), *CompDef.Name, *CompDef.Class, *ParentStr);
}

FString FEditBlueprintTool::RemoveComponent(UBlueprint* Blueprint, const FString& CompName)
{
	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! Component: %s not found"), *CompName);
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
		{
			Blueprint->SimpleConstructionScript->RemoveNode(Node);
			return FString::Printf(TEXT("- Component: %s"), *CompName);
		}
	}

	return FString::Printf(TEXT("! Component: %s not found"), *CompName);
}

void FEditBlueprintTool::SetComponentProperty(USCS_Node* Node, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value)
{
	if (!Node || !Node->ComponentTemplate || !Value.IsValid()) return;

	UObject* Component = Node->ComponentTemplate;
	FProperty* Property = FindFProperty<FProperty>(Component->GetClass(), FName(*PropertyName));
	if (!Property) return;

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Component);

	// Handle different JSON value types
	if (Value->Type == EJson::Boolean)
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value->AsBool());
		}
	}
	else if (Value->Type == EJson::Number)
	{
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
		{
			FloatProp->SetPropertyValue(ValuePtr, (float)Value->AsNumber());
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
		{
			DoubleProp->SetPropertyValue(ValuePtr, Value->AsNumber());
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
		{
			IntProp->SetPropertyValue(ValuePtr, (int32)Value->AsNumber());
		}
	}
	else if (Value->Type == EJson::String)
	{
		Property->ImportText_Direct(*Value->AsString(), ValuePtr, Component, PPF_None);
	}
}

FString FEditBlueprintTool::AddFunction(UBlueprint* Blueprint, const FFunctionDefinition& FuncDef)
{
	if (FuncDef.Name.IsEmpty())
	{
		return TEXT("! Function: Missing name");
	}

	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FuncDef.Name))
		{
			return FString::Printf(TEXT("! Function: %s already exists"), *FuncDef.Name);
		}
	}

	// Create the function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FuncDef.Name),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		return FString::Printf(TEXT("! Function: Failed to create %s"), *FuncDef.Name);
	}

	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, false, static_cast<UFunction*>(nullptr));

	// Find the entry node and set up parameters
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}

	if (EntryNode)
	{
		// Set pure flag
		if (FuncDef.bPure)
		{
			EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() | FUNC_BlueprintPure);
		}

		// Add input parameters
		for (const FFunctionParam& Input : FuncDef.Inputs)
		{
			FEdGraphPinType PinType = TypeDefinitionToPinType(Input.Type);
			// Note: Parameters are added via FUserPinInfo, we'd need more complex logic here
			// For now, we create the function structure and parameters can be added via editor
		}
	}

	// Build result string
	FString InputsStr;
	for (const FFunctionParam& Input : FuncDef.Inputs)
	{
		if (!InputsStr.IsEmpty()) InputsStr += TEXT(", ");
		InputsStr += Input.Name;
	}

	FString OutputsStr;
	for (const FFunctionParam& Output : FuncDef.Outputs)
	{
		if (!OutputsStr.IsEmpty()) OutputsStr += TEXT(", ");
		OutputsStr += Output.Name;
	}

	FString Flags = FuncDef.bPure ? TEXT(" [Pure]") : TEXT("");

	if (!OutputsStr.IsEmpty())
	{
		return FString::Printf(TEXT("+ Function: %s(%s) -> %s%s"), *FuncDef.Name, *InputsStr, *OutputsStr, *Flags);
	}
	return FString::Printf(TEXT("+ Function: %s(%s)%s"), *FuncDef.Name, *InputsStr, *Flags);
}

FString FEditBlueprintTool::RemoveFunction(UBlueprint* Blueprint, const FString& FuncName)
{
	FName Name = FName(*FuncName);

	for (int32 i = Blueprint->FunctionGraphs.Num() - 1; i >= 0; i--)
	{
		UEdGraph* Graph = Blueprint->FunctionGraphs[i];
		if (Graph && Graph->GetFName() == Name)
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
			return FString::Printf(TEXT("- Function: %s"), *FuncName);
		}
	}

	return FString::Printf(TEXT("! Function: %s not found"), *FuncName);
}

FString FEditBlueprintTool::AddEvent(UBlueprint* Blueprint, const FEventDefinition& EventDef)
{
	if (EventDef.Name.IsEmpty())
	{
		return TEXT("! Event: Missing name");
	}

	FName EventName = FName(*EventDef.Name);

	// Check if event dispatcher already exists
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == EventName && Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			return FString::Printf(TEXT("! Event: %s already exists"), *EventDef.Name);
		}
	}

	// Create multicast delegate type
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	PinType.PinSubCategoryObject = nullptr;

	// Add as variable
	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, EventName, PinType);
	if (!bSuccess)
	{
		return FString::Printf(TEXT("! Event: Failed to add %s"), *EventDef.Name);
	}

	// Build result string
	FString ParamsStr;
	for (const FFunctionParam& Param : EventDef.Params)
	{
		if (!ParamsStr.IsEmpty()) ParamsStr += TEXT(", ");
		ParamsStr += Param.Name;
	}

	return FString::Printf(TEXT("+ Event: %s(%s)"), *EventDef.Name, *ParamsStr);
}

FString FEditBlueprintTool::RemoveEvent(UBlueprint* Blueprint, const FString& EventName)
{
	FName Name = FName(*EventName);

	// Event dispatchers are stored as variables with MCDelegate type
	for (int32 i = Blueprint->NewVariables.Num() - 1; i >= 0; i--)
	{
		const FBPVariableDescription& Var = Blueprint->NewVariables[i];
		if (Var.VarName == Name && Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
			return FString::Printf(TEXT("- Event: %s"), *EventName);
		}
	}

	return FString::Printf(TEXT("! Event: %s not found"), *EventName);
}

// Widget Blueprint operations

UClass* FEditBlueprintTool::FindWidgetClass(const FString& TypeName)
{
	// Map common widget type names to their classes
	static TMap<FString, UClass*> WidgetClassMap;
	if (WidgetClassMap.Num() == 0)
	{
		// Panels
		WidgetClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
		WidgetClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
		WidgetClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
		WidgetClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());
		WidgetClassMap.Add(TEXT("UniformGridPanel"), UUniformGridPanel::StaticClass());
		WidgetClassMap.Add(TEXT("WrapBox"), UWrapBox::StaticClass());
		WidgetClassMap.Add(TEXT("ScrollBox"), UScrollBox::StaticClass());
		WidgetClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
		WidgetClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
		WidgetClassMap.Add(TEXT("WidgetSwitcher"), UWidgetSwitcher::StaticClass());

		// Common widgets
		WidgetClassMap.Add(TEXT("Button"), UButton::StaticClass());
		WidgetClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
		WidgetClassMap.Add(TEXT("Image"), UImage::StaticClass());
		WidgetClassMap.Add(TEXT("Border"), UBorder::StaticClass());
		WidgetClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());

		// Input widgets
		WidgetClassMap.Add(TEXT("CheckBox"), UCheckBox::StaticClass());
		WidgetClassMap.Add(TEXT("EditableTextBox"), UEditableTextBox::StaticClass());
		WidgetClassMap.Add(TEXT("Slider"), USlider::StaticClass());

		// Progress
		WidgetClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
	}

	// Try direct lookup (case-insensitive)
	for (const auto& Pair : WidgetClassMap)
	{
		if (Pair.Key.Equals(TypeName, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	// Try finding by class name with U prefix
	FString SearchName = TypeName;
	if (!SearchName.StartsWith(TEXT("U")))
	{
		SearchName = TEXT("U") + TypeName;
	}

	// Search all UWidget classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UWidget::StaticClass()))
		{
			if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

UWidget* FEditBlueprintTool::FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name)
{
	if (!WidgetTree) return nullptr;

	return WidgetTree->FindWidget(FName(*Name));
}

FString FEditBlueprintTool::AddWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetDefinition& WidgetDef)
{
	if (WidgetDef.Type.IsEmpty() || WidgetDef.Name.IsEmpty())
	{
		return TEXT("! Widget: Missing type or name");
	}

	// Ensure widget tree exists
	if (!WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree = NewObject<UWidgetTree>(WidgetBlueprint, UWidgetTree::StaticClass(), NAME_None, RF_Transactional);
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;

	// Check if widget already exists
	if (FindWidgetByName(WidgetTree, WidgetDef.Name))
	{
		return FString::Printf(TEXT("! Widget: %s already exists"), *WidgetDef.Name);
	}

	// Find widget class
	UClass* WidgetClass = FindWidgetClass(WidgetDef.Type);
	if (!WidgetClass)
	{
		return FString::Printf(TEXT("! Widget: Unknown type %s"), *WidgetDef.Type);
	}

	// Create the widget
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetDef.Name));
	if (!NewWidget)
	{
		return FString::Printf(TEXT("! Widget: Failed to create %s"), *WidgetDef.Name);
	}

	// Find parent widget
	UPanelWidget* ParentPanel = nullptr;
	if (!WidgetDef.Parent.IsEmpty())
	{
		UWidget* ParentWidget = FindWidgetByName(WidgetTree, WidgetDef.Parent);
		if (!ParentWidget)
		{
			return FString::Printf(TEXT("! Widget: Parent not found: %s"), *WidgetDef.Parent);
		}

		ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			return FString::Printf(TEXT("! Widget: Parent %s is not a panel widget"), *WidgetDef.Parent);
		}
	}
	else
	{
		// Use root widget as parent, or set as root if no root exists
		if (WidgetTree->RootWidget)
		{
			ParentPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
			if (!ParentPanel)
			{
				return TEXT("! Widget: Root widget is not a panel, cannot add children");
			}
		}
	}

	// Add to parent or set as root
	if (ParentPanel)
	{
		ParentPanel->AddChild(NewWidget);
	}
	else
	{
		// This is the first widget, set as root (should be a panel)
		WidgetTree->RootWidget = NewWidget;
	}

	// Mark as modified
	WidgetBlueprint->Modify();

	// Refresh editor if open
	RefreshWidgetEditor(WidgetBlueprint);

	FString ParentStr = WidgetDef.Parent.IsEmpty() ? TEXT("Root") : WidgetDef.Parent;
	return FString::Printf(TEXT("+ Widget: %s (%s) -> %s"), *WidgetDef.Name, *WidgetDef.Type, *ParentStr);
}

FString FEditBlueprintTool::RemoveWidget(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
{
	if (!WidgetBlueprint->WidgetTree)
	{
		return FString::Printf(TEXT("! Widget: %s not found (no widget tree)"), *WidgetName);
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	UWidget* Widget = FindWidgetByName(WidgetTree, WidgetName);

	if (!Widget)
	{
		return FString::Printf(TEXT("! Widget: %s not found"), *WidgetName);
	}

	// Don't allow removing root if it has children
	if (Widget == WidgetTree->RootWidget)
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			if (Panel->GetChildrenCount() > 0)
			{
				return FString::Printf(TEXT("! Widget: Cannot remove root %s - has children"), *WidgetName);
			}
		}
		WidgetTree->RootWidget = nullptr;
	}
	else
	{
		// Remove from parent
		WidgetTree->RemoveWidget(Widget);
	}

	// Mark as modified
	WidgetBlueprint->Modify();

	// Refresh editor if open
	RefreshWidgetEditor(WidgetBlueprint);

	return FString::Printf(TEXT("- Widget: %s"), *WidgetName);
}

void FEditBlueprintTool::RefreshWidgetEditor(UWidgetBlueprint* WidgetBlueprint)
{
	if (!GEditor) return;

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem) return;

	IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(WidgetBlueprint, false);
	if (!EditorInstance) return;

	// FWidgetBlueprintEditor is the editor class for Widget Blueprints
	FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance);
	if (WidgetEditor)
	{
		// Invalidate the preview to rebuild the widget tree display
		WidgetEditor->InvalidatePreview();
		UE_LOG(LogTemp, Log, TEXT("NeoStack: Refreshed Widget Blueprint Editor"));
	}
}

// =============================================================================
// Event Binding Operations
// =============================================================================

FString FEditBlueprintTool::ListEvents(UBlueprint* Blueprint, const FString& SourceName)
{
	// Check if this is a Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);

	TArray<FEventInfo> Events;
	FString SourceType;

	if (WidgetBlueprint)
	{
		Events = ListWidgetEvents(WidgetBlueprint, SourceName);
		// Get widget type
		if (WidgetBlueprint->WidgetTree)
		{
			if (UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*SourceName)))
			{
				SourceType = Widget->GetClass()->GetName();
				SourceType.RemoveFromStart(TEXT("U"));
			}
		}
	}
	else
	{
		Events = ListComponentEvents(Blueprint, SourceName);
		// Get component type (checks both SCS and CDO)
		FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, SourceName);
		if (Discovery.ComponentTemplate)
		{
			SourceType = Discovery.ComponentTemplate->GetClass()->GetName();
			SourceType.RemoveFromStart(TEXT("U"));
		}
	}

	if (Events.Num() == 0)
	{
		return FString::Printf(TEXT("! No bindable events found on '%s'"), *SourceName);
	}

	FString Output = FString::Printf(TEXT("Events on %s (%s):\n"), *SourceName, *SourceType);
	for (const FEventInfo& Event : Events)
	{
		Output += FString::Printf(TEXT("  - %s%s\n"), *Event.Name, *Event.Signature);
	}

	return Output;
}

TArray<FEditBlueprintTool::FEventInfo> FEditBlueprintTool::ListComponentEvents(UBlueprint* Blueprint, const FString& ComponentName)
{
	TArray<FEventInfo> Events;

	if (!Blueprint)
	{
		return Events;
	}

	// Find the component (checks both SCS and CDO)
	FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, ComponentName);
	UActorComponent* ComponentTemplate = Discovery.ComponentTemplate;

	if (!ComponentTemplate)
	{
		return Events;
	}

	// Find all multicast delegate properties on the component
	UClass* ComponentClass = ComponentTemplate->GetClass();
	for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;

		// Skip if not BlueprintAssignable
		if (!DelegateProp->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			continue;
		}

		FEventInfo Info;
		Info.Name = DelegateProp->GetName();

		// Get signature from the delegate's signature function
		if (UFunction* SignatureFunc = DelegateProp->SignatureFunction)
		{
			FString Params;
			for (TFieldIterator<FProperty> ParamIt(SignatureFunc); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (Param->HasAnyPropertyFlags(CPF_Parm) && !Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (!Params.IsEmpty()) Params += TEXT(", ");
					Params += Param->GetName();
				}
			}
			Info.Signature = FString::Printf(TEXT("(%s)"), *Params);
		}
		else
		{
			Info.Signature = TEXT("()");
		}

		Events.Add(Info);
	}

	return Events;
}

TArray<FEditBlueprintTool::FEventInfo> FEditBlueprintTool::ListWidgetEvents(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
{
	TArray<FEventInfo> Events;

	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return Events;
	}

	// Find the widget
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return Events;
	}

	// Find all multicast delegate properties on the widget
	UClass* WidgetClass = Widget->GetClass();
	for (TFieldIterator<FMulticastDelegateProperty> It(WidgetClass); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;

		// Skip if not BlueprintAssignable
		if (!DelegateProp->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			continue;
		}

		FEventInfo Info;
		Info.Name = DelegateProp->GetName();

		// Get signature
		if (UFunction* SignatureFunc = DelegateProp->SignatureFunction)
		{
			FString Params;
			for (TFieldIterator<FProperty> ParamIt(SignatureFunc); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (Param->HasAnyPropertyFlags(CPF_Parm) && !Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (!Params.IsEmpty()) Params += TEXT(", ");
					Params += Param->GetName();
				}
			}
			Info.Signature = FString::Printf(TEXT("(%s)"), *Params);
		}
		else
		{
			Info.Signature = TEXT("()");
		}

		Events.Add(Info);
	}

	return Events;
}

FString FEditBlueprintTool::BindEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef)
{
	if (EventDef.Source.IsEmpty() || EventDef.Event.IsEmpty() || EventDef.Handler.IsEmpty())
	{
		return TEXT("! Event binding: Missing source, event, or handler");
	}

	// Route to widget or component binding based on Blueprint type
	if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
	{
		return BindWidgetEvent(WidgetBlueprint, EventDef);
	}
	else
	{
		return BindComponentEvent(Blueprint, EventDef);
	}
}

FString FEditBlueprintTool::BindWidgetEvent(UWidgetBlueprint* WidgetBlueprint, const FEventBindingDef& EventDef)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return TEXT("! Widget binding: Invalid Widget Blueprint");
	}

	// Find the widget to get its class
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*EventDef.Source));
	if (!Widget)
	{
		return FString::Printf(TEXT("! Widget binding: Widget '%s' not found"), *EventDef.Source);
	}

	UClass* WidgetClass = Widget->GetClass();
	FName EventName(*EventDef.Event);
	FName PropertyName(*EventDef.Source);

	// Find the corresponding variable property in the Blueprint's skeleton class
	// (Widgets become FObjectProperty variables in the generated class)
	FObjectProperty* VariableProperty = nullptr;
	if (WidgetBlueprint->SkeletonGeneratedClass)
	{
		VariableProperty = FindFProperty<FObjectProperty>(WidgetBlueprint->SkeletonGeneratedClass, PropertyName);
	}

	if (!VariableProperty)
	{
		return FString::Printf(TEXT("! Widget binding: Could not find property for widget '%s'. Try compiling the Blueprint first."),
			*EventDef.Source);
	}

	// Check if event already exists using engine utility
	if (const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, EventName, PropertyName))
	{
		// Return existing node info so AI can still wire it
		FString Output = FString::Printf(TEXT("! Widget binding: Event '%s' on '%s' already exists\n"),
			*EventDef.Event, *EventDef.Source);
		Output += FString::Printf(TEXT("  GUID: %s\n"), *ExistingNode->NodeGuid.ToString());
		Output += TEXT("  Output Pins:");
		for (UEdGraphPin* Pin : ExistingNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
			}
		}
		return Output;
	}

	// Use engine utility to create the bound event node (same as clicking "+" in editor)
	FKismetEditorUtilities::CreateNewBoundEventForClass(WidgetClass, EventName, WidgetBlueprint, VariableProperty);

	// Find the newly created node to return its info
	const UK2Node_ComponentBoundEvent* NewNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, EventName, PropertyName);
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Widget binding: Event created but node not found for %s.%s"),
			*EventDef.Source, *EventDef.Event);
	}

	// Build output with GUID and pins for wiring
	FString Output = FString::Printf(TEXT("+ Created event: %s.%s\n"), *EventDef.Source, *EventDef.Event);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *NewNode->NodeGuid.ToString());
	Output += TEXT("  Output Pins:");
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
		}
	}

	return Output;
}

FString FEditBlueprintTool::BindComponentEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef)
{
	if (!Blueprint)
	{
		return TEXT("! Component binding: Invalid Blueprint");
	}

	// Find the component (checks both SCS and CDO)
	FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, EventDef.Source);

	if (!Discovery.ComponentTemplate)
	{
		return FString::Printf(TEXT("! Component binding: Component '%s' not found"), *EventDef.Source);
	}

	UClass* ComponentClass = Discovery.ComponentTemplate->GetClass();
	FName EventName(*EventDef.Event);
	FName PropertyName = Discovery.VariableName;

	// Find the FObjectProperty for this component in the Blueprint class
	FObjectProperty* ComponentProperty = nullptr;
	if (Blueprint->SkeletonGeneratedClass)
	{
		ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, PropertyName);
	}
	if (!ComponentProperty && Blueprint->GeneratedClass)
	{
		ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, PropertyName);
	}

	if (!ComponentProperty)
	{
		return FString::Printf(TEXT("! Component binding: Could not find property for component '%s'. Try compiling the Blueprint first."),
			*EventDef.Source);
	}

	// Check if event already exists using engine utility
	if (const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, PropertyName))
	{
		// Return existing node info so AI can still wire it
		FString Output = FString::Printf(TEXT("! Component binding: Event '%s' on '%s' already exists\n"),
			*EventDef.Event, *EventDef.Source);
		Output += FString::Printf(TEXT("  GUID: %s\n"), *ExistingNode->NodeGuid.ToString());
		Output += TEXT("  Output Pins:");
		for (UEdGraphPin* Pin : ExistingNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
			}
		}
		return Output;
	}

	// Use engine utility to create the bound event node (same as clicking "+" in editor)
	FKismetEditorUtilities::CreateNewBoundEventForClass(ComponentClass, EventName, Blueprint, ComponentProperty);

	// Find the newly created node to return its info
	const UK2Node_ComponentBoundEvent* NewNode = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, PropertyName);
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Component binding: Event created but node not found for %s.%s"),
			*EventDef.Source, *EventDef.Event);
	}

	// Build output with GUID and pins for wiring
	FString Output = FString::Printf(TEXT("+ Created event: %s.%s\n"), *EventDef.Source, *EventDef.Event);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *NewNode->NodeGuid.ToString());
	Output += TEXT("  Output Pins:");
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
		}
	}

	return Output;
}

FString FEditBlueprintTool::UnbindEvent(UBlueprint* Blueprint, const FString& Source, const FString& Event)
{
	if (Source.IsEmpty() || Event.IsEmpty())
	{
		return TEXT("! Unbind: Missing source or event");
	}

	// Both Widget Blueprints and regular Blueprints use UK2Node_ComponentBoundEvent
	// when events are created via the "+" button (or our bind_events)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; i--)
		{
			if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Graph->Nodes[i]))
			{
				if (BoundEvent->ComponentPropertyName.ToString().Equals(Source, ESearchCase::IgnoreCase) &&
					BoundEvent->DelegatePropertyName.ToString().Equals(Event, ESearchCase::IgnoreCase))
				{
					Graph->RemoveNode(BoundEvent);
					Blueprint->Modify();
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					return FString::Printf(TEXT("- Removed event: %s.%s"), *Source, *Event);
				}
			}
		}
	}

	return FString::Printf(TEXT("! Unbind: No event found for %s.%s"), *Source, *Event);
}

FEditBlueprintTool::FComponentDiscoveryResult FEditBlueprintTool::FindComponentByName(UBlueprint* Blueprint, const FString& ComponentName)
{
	FComponentDiscoveryResult Result;

	if (!Blueprint)
	{
		return Result;
	}

	// First: Check SimpleConstructionScript (catches recently added, uncompiled components)
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				Result.ComponentTemplate = Node->ComponentTemplate;
				Result.SCSNode = Node;
				Result.VariableName = Node->GetVariableName();
				Result.bFoundInSCS = true;
				return Result;  // SCS is authoritative for uncompiled changes
			}
		}
	}

	// Second: Check CDO (catches compiled components that might not be in SCS)
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	if (BPClass)
	{
		AActor* CDO = Cast<AActor>(BPClass->GetDefaultObject());
		if (CDO)
		{
			TArray<UActorComponent*> Components;
			CDO->GetComponents<UActorComponent>(Components);

			for (UActorComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					Result.ComponentTemplate = Component;
					Result.VariableName = FName(*ComponentName);
					Result.bFoundInCDO = true;
					return Result;
				}
			}
		}
	}

	return Result;  // Not found
}

// =============================================================================
// Animation Blueprint State Machine Operations
// =============================================================================

UEdGraph* FEditBlueprintTool::FindAnimGraph(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}

	return nullptr;
}

UAnimGraphNode_StateMachine* FEditBlueprintTool::FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName)
{
	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
		{
			FString SMName = SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (SMName.Equals(StateMachineName, ESearchCase::IgnoreCase))
			{
				return SMNode;
			}
		}
	}

	return nullptr;
}

UAnimStateNode* FEditBlueprintTool::FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	if (!SMGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			FString NodeName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (NodeName.Equals(StateName, ESearchCase::IgnoreCase))
			{
				return StateNode;
			}
		}
	}

	return nullptr;
}

FString FEditBlueprintTool::AddStateMachine(UAnimBlueprint* AnimBlueprint, const FStateMachineDefinition& SMDef)
{
	if (SMDef.Name.IsEmpty())
	{
		return TEXT("! StateMachine: Missing name");
	}

	// Find AnimGraph
	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph)
	{
		return TEXT("! StateMachine: AnimGraph not found. Open the Animation Blueprint in the editor first.");
	}

	// Check if state machine already exists
	if (FindStateMachineNode(AnimBlueprint, SMDef.Name))
	{
		return FString::Printf(TEXT("! StateMachine: '%s' already exists"), *SMDef.Name);
	}

	// Create the state machine node
	UAnimGraphNode_StateMachine* NewSMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
	if (!NewSMNode)
	{
		return FString::Printf(TEXT("! StateMachine: Failed to create '%s'"), *SMDef.Name);
	}

	// Set up the node
	NewSMNode->CreateNewGuid();
	NewSMNode->PostPlacedNewNode();

	// Set the state machine name
	FAnimNode_StateMachine& SMNode = NewSMNode->Node;
	// Note: The actual name is in the EditorStateMachineGraph

	// Create the state machine graph
	const UAnimationStateMachineSchema* Schema = GetDefault<UAnimationStateMachineSchema>();
	UAnimationStateMachineGraph* SMGraph = CastChecked<UAnimationStateMachineGraph>(
		FBlueprintEditorUtils::CreateNewGraph(AnimBlueprint, FName(*SMDef.Name),
			UAnimationStateMachineGraph::StaticClass(),
			UAnimationStateMachineSchema::StaticClass()));

	SMGraph->OwnerAnimGraphNode = NewSMNode;
	NewSMNode->EditorStateMachineGraph = SMGraph;

	// Initialize the graph with entry node
	Schema->CreateDefaultNodesForGraph(*SMGraph);

	// Position the node in the AnimGraph
	NewSMNode->NodePosX = 200;
	NewSMNode->NodePosY = 0;

	// Add to AnimGraph
	AnimGraph->AddNode(NewSMNode, false, false);
	NewSMNode->SetFlags(RF_Transactional);
	AnimGraph->Modify();

	FString NodeGuid = NewSMNode->NodeGuid.ToString();
	return FString::Printf(TEXT("+ StateMachine: %s (GUID: %s)"), *SMDef.Name, *NodeGuid);
}

FString FEditBlueprintTool::AddAnimState(UAnimBlueprint* AnimBlueprint, const FAnimStateDefinition& StateDef)
{
	if (StateDef.Name.IsEmpty())
	{
		return TEXT("! AnimState: Missing state name");
	}
	if (StateDef.StateMachine.IsEmpty())
	{
		return TEXT("! AnimState: Missing state_machine parameter");
	}

	// Find the state machine
	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, StateDef.StateMachine);
	if (!SMNode)
	{
		return FString::Printf(TEXT("! AnimState: State machine '%s' not found"), *StateDef.StateMachine);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return FString::Printf(TEXT("! AnimState: State machine '%s' has no graph"), *StateDef.StateMachine);
	}

	// Check if state already exists
	if (FindStateNode(SMGraph, StateDef.Name))
	{
		return FString::Printf(TEXT("! AnimState: State '%s' already exists in '%s'"), *StateDef.Name, *StateDef.StateMachine);
	}

	// Create the state node
	UAnimStateNode* NewStateNode = NewObject<UAnimStateNode>(SMGraph);
	if (!NewStateNode)
	{
		return FString::Printf(TEXT("! AnimState: Failed to create state '%s'"), *StateDef.Name);
	}

	// Set up the node
	NewStateNode->CreateNewGuid();
	NewStateNode->PostPlacedNewNode();

	// Position the node
	static int32 StateOffsetX = 300;
	static int32 StateOffsetY = 0;
	NewStateNode->NodePosX = StateOffsetX;
	NewStateNode->NodePosY = StateOffsetY;
	StateOffsetY += 150;

	// Add to state machine graph
	SMGraph->AddNode(NewStateNode, false, false);
	NewStateNode->SetFlags(RF_Transactional);

	// Create the state's bound graph (for animation logic)
	const UAnimationStateMachineSchema* Schema = GetDefault<UAnimationStateMachineSchema>();
	NewStateNode->BoundGraph = FBlueprintEditorUtils::CreateNewGraph(AnimBlueprint,
		FName(*StateDef.Name),
		UAnimationStateGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());

	// BoundGraph is automatically set up by CreateNewGraph - no need to set OwnerAnimGraphNode
	// (property removed in UE 5.7+)

	SMGraph->Modify();

	FString NodeGuid = NewStateNode->NodeGuid.ToString();
	FString BoundGraphName = NewStateNode->BoundGraph ? NewStateNode->BoundGraph->GetName() : TEXT("none");

	return FString::Printf(TEXT("+ AnimState: %s in %s (GUID: %s, graph: %s)"),
		*StateDef.Name, *StateDef.StateMachine, *NodeGuid, *BoundGraphName);
}

FString FEditBlueprintTool::AddStateTransition(UAnimBlueprint* AnimBlueprint, const FStateTransitionDefinition& TransDef)
{
	if (TransDef.StateMachine.IsEmpty())
	{
		return TEXT("! Transition: Missing state_machine parameter");
	}
	if (TransDef.FromState.IsEmpty() || TransDef.ToState.IsEmpty())
	{
		return TEXT("! Transition: Missing from_state or to_state parameter");
	}

	// Find the state machine
	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, TransDef.StateMachine);
	if (!SMNode)
	{
		return FString::Printf(TEXT("! Transition: State machine '%s' not found"), *TransDef.StateMachine);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return FString::Printf(TEXT("! Transition: State machine '%s' has no graph"), *TransDef.StateMachine);
	}

	// Find source and destination states
	// Note: UAnimStateEntryNode is NOT a UAnimStateNodeBase in UE 5.7+, so handle separately
	UEdGraphNode* FromNode = nullptr;
	UAnimStateNodeBase* ToStateNode = nullptr;
	bool bFromEntry = false;

	// Check for [Entry] as special source
	if (TransDef.FromState.Equals(TEXT("[Entry]"), ESearchCase::IgnoreCase) ||
		TransDef.FromState.Equals(TEXT("Entry"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(Node))
			{
				FromNode = EntryNode;
				bFromEntry = true;
				break;
			}
		}
	}
	else
	{
		FromNode = FindStateNode(SMGraph, TransDef.FromState);
	}

	ToStateNode = FindStateNode(SMGraph, TransDef.ToState);

	if (!FromNode)
	{
		return FString::Printf(TEXT("! Transition: Source state '%s' not found"), *TransDef.FromState);
	}
	if (!ToStateNode)
	{
		return FString::Printf(TEXT("! Transition: Target state '%s' not found"), *TransDef.ToState);
	}

	// Check if transition already exists
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* ExistingTrans = Cast<UAnimStateTransitionNode>(Node))
		{
			// Compare nodes directly (GetPreviousState returns UAnimStateNodeBase, but we compare pointers)
			bool bSameFrom = (ExistingTrans->GetPreviousState() == FromNode) ||
				(bFromEntry && Cast<UAnimStateEntryNode>(FromNode));
			if (bSameFrom && ExistingTrans->GetNextState() == ToStateNode)
			{
				// Return the existing transition info for wiring
				FString TransGuid = ExistingTrans->NodeGuid.ToString();
				FString TransGraphName = TEXT("none");
				if (UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(ExistingTrans->BoundGraph))
				{
					TransGraphName = TransGraph->GetName();
				}
				return FString::Printf(TEXT("! Transition: %s -> %s already exists (GUID: %s, graph: %s)"),
					*TransDef.FromState, *TransDef.ToState, *TransGuid, *TransGraphName);
			}
		}
	}

	// Create the transition node
	UAnimStateTransitionNode* TransitionNode = NewObject<UAnimStateTransitionNode>(SMGraph);
	if (!TransitionNode)
	{
		return FString::Printf(TEXT("! Transition: Failed to create transition from '%s' to '%s'"),
			*TransDef.FromState, *TransDef.ToState);
	}

	// Set up the node
	TransitionNode->CreateNewGuid();
	TransitionNode->PostPlacedNewNode();

	// Add to graph first
	SMGraph->AddNode(TransitionNode, false, false);
	TransitionNode->SetFlags(RF_Transactional);

	// Position between states
	TransitionNode->NodePosX = (FromNode->NodePosX + ToStateNode->NodePosX) / 2;
	TransitionNode->NodePosY = (FromNode->NodePosY + ToStateNode->NodePosY) / 2;

	// Create the transition graph (this is where condition logic goes)
	FString TransitionGraphName = FString::Printf(TEXT("%s_to_%s"), *TransDef.FromState, *TransDef.ToState);
	UAnimationTransitionGraph* TransGraph = CastChecked<UAnimationTransitionGraph>(
		FBlueprintEditorUtils::CreateNewGraph(AnimBlueprint,
			FName(*TransitionGraphName),
			UAnimationTransitionGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass()));

	// BoundGraph set directly (OwnerAnimGraphNode removed in UE 5.7+)
	TransitionNode->BoundGraph = TransGraph;

	// Create the result node in the transition graph
	// The TransitionResult node has bCanEnterTransition pin that determines if transition fires
	FGraphNodeCreator<UAnimGraphNode_TransitionResult> ResultNodeCreator(*TransGraph);
	UAnimGraphNode_TransitionResult* ResultNode = ResultNodeCreator.CreateNode();
	ResultNode->NodePosX = 400;
	ResultNode->NodePosY = 0;
	ResultNodeCreator.Finalize();

	// Connect the states via pins
	// Entry nodes have OutputPin, regular states have output pins too
	// Transition nodes connect between states
	UEdGraphPin* FromOutputPin = nullptr;
	UEdGraphPin* ToInputPin = nullptr;
	UEdGraphPin* TransInputPin = nullptr;
	UEdGraphPin* TransOutputPin = nullptr;

	// Find pins on the nodes
	for (UEdGraphPin* Pin : FromNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			FromOutputPin = Pin;
			break;
		}
	}
	for (UEdGraphPin* Pin : ToStateNode->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			ToInputPin = Pin;
			break;
		}
	}
	for (UEdGraphPin* Pin : TransitionNode->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			TransInputPin = Pin;
		}
		else if (Pin->Direction == EGPD_Output)
		{
			TransOutputPin = Pin;
		}
	}

	// Make connections: FromState -> Transition -> ToState
	if (FromOutputPin && TransInputPin)
	{
		FromOutputPin->MakeLinkTo(TransInputPin);
	}
	if (TransOutputPin && ToInputPin)
	{
		TransOutputPin->MakeLinkTo(ToInputPin);
	}

	SMGraph->Modify();

	// Build result with info for adding condition nodes to the transition graph
	FString TransGuid = TransitionNode->NodeGuid.ToString();
	FString ResultGuid = ResultNode ? ResultNode->NodeGuid.ToString() : TEXT("none");

	FString Output = FString::Printf(TEXT("+ Transition: %s -> %s in %s\n"),
		*TransDef.FromState, *TransDef.ToState, *TransDef.StateMachine);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *TransGuid);
	Output += FString::Printf(TEXT("  Condition Graph: %s\n"), *TransGraph->GetName());
	Output += FString::Printf(TEXT("  Result Node GUID: %s (connect to bCanEnterTransition pin)\n"), *ResultGuid);

	return Output;
}
