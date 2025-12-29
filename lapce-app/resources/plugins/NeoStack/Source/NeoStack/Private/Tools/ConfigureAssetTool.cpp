// Copyright NeoStack. All Rights Reserved.

#include "Tools/ConfigureAssetTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyIterator.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"

// For accessing Material Editor preview material
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"

// Widget Blueprint support
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Widgets/Layout/Anchors.h"
#include "WidgetBlueprintEditor.h"

// Blueprint component support
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"

FToolResult FConfigureAssetTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path, SubobjectName;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("subobject"), SubobjectName);

	// Load asset
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Track the original asset for editor refresh
	UObject* OriginalAsset = Asset;
	UObject* WorkingAsset = Asset;
	FString SubobjectContext;

	// If subobject is specified, find it within the asset
	if (!SubobjectName.IsEmpty())
	{
		UObject* Subobject = FindSubobject(Asset, SubobjectName);
		if (!Subobject)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Subobject '%s' not found in %s"), *SubobjectName, *Name));
		}
		WorkingAsset = Subobject;
		SubobjectContext = FString::Printf(TEXT(" (subobject: %s)"), *SubobjectName);
	}

	// CRITICAL: When the Material Editor is open, it works on a PREVIEW COPY of the material.
	// We must configure the preview material for live changes, not the original.

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
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
							WorkingAsset = PreviewMat;
							UE_LOG(LogTemp, Log, TEXT("NeoStack ConfigureAsset: Using preview material from Material Editor"));
						}
					}
				}
			}
		}
	}

	// Parse parameters
	bool bListProperties = false;
	Args->TryGetBoolField(TEXT("list_properties"), bListProperties);

	TArray<FString> GetProperties;
	const TArray<TSharedPtr<FJsonValue>>* GetArray;
	if (Args->TryGetArrayField(TEXT("get"), GetArray))
	{
		for (const auto& Val : *GetArray)
		{
			FString PropName;
			if (Val->TryGetString(PropName))
			{
				GetProperties.Add(PropName);
			}
		}
	}

	TArray<FPropertyChange> Changes;
	const TArray<TSharedPtr<FJsonValue>>* ChangesArray;
	if (Args->TryGetArrayField(TEXT("changes"), ChangesArray))
	{
		FString ParseError;
		if (!ParseChanges(*ChangesArray, Changes, ParseError))
		{
			return FToolResult::Fail(ParseError);
		}
	}

	// Parse slot configuration (for widgets in panels)
	const TSharedPtr<FJsonObject>* SlotConfig = nullptr;
	Args->TryGetObjectField(TEXT("slot"), SlotConfig);

	// Execute operations
	TArray<TPair<FString, FString>> GetResults;
	TArray<FString> GetErrors;
	TArray<FPropertyInfo> ListedProperties;
	TArray<FChangeResult> ChangeResults;
	FString SlotResult;

	// Get specific property values
	if (GetProperties.Num() > 0)
	{
		GetResults = GetPropertyValues(WorkingAsset, GetProperties, GetErrors);
	}

	// List all editable properties
	if (bListProperties)
	{
		ListedProperties = ListEditableProperties(WorkingAsset);
	}

	// Apply changes
	if (Changes.Num() > 0)
	{
		ChangeResults = ApplyChanges(WorkingAsset, Asset, Changes);
	}

	// Configure slot (for widgets)
	if (SlotConfig && *SlotConfig)
	{
		UWidget* Widget = Cast<UWidget>(WorkingAsset);
		if (!Widget)
		{
			return FToolResult::Fail(TEXT("'slot' parameter only valid for widgets. Use 'subobject' to target a widget first."));
		}
		SlotResult = ConfigureSlot(Widget, *SlotConfig, OriginalAsset);
	}

	// If nothing was requested, show help
	if (GetProperties.Num() == 0 && !bListProperties && Changes.Num() == 0 && !SlotConfig)
	{
		return FToolResult::Fail(TEXT("No operation specified. Use 'get', 'list_properties', 'changes', or 'slot'."));
	}

	// Format and return results
	FString Output = FormatResults(WorkingAsset->GetName(), GetAssetTypeName(WorkingAsset),
	                                GetResults, GetErrors, ListedProperties, ChangeResults);

	// Append slot configuration result
	if (!SlotResult.IsEmpty())
	{
		Output += TEXT("\n") + SlotResult;
	}

	return FToolResult::Ok(Output);
}

bool FConfigureAssetTool::ParseChanges(const TArray<TSharedPtr<FJsonValue>>& ChangesArray,
                                        TArray<FPropertyChange>& OutChanges, FString& OutError)
{
	for (const auto& ChangeVal : ChangesArray)
	{
		const TSharedPtr<FJsonObject>* ChangeObj;
		if (!ChangeVal->TryGetObject(ChangeObj))
		{
			OutError = TEXT("Each change must be an object with 'property' and 'value'");
			return false;
		}

		FPropertyChange Change;

		if (!(*ChangeObj)->TryGetStringField(TEXT("property"), Change.PropertyName) || Change.PropertyName.IsEmpty())
		{
			OutError = TEXT("Missing 'property' in change");
			return false;
		}

		// Value can be string, number, or boolean - convert all to string
		if ((*ChangeObj)->HasField(TEXT("value")))
		{
			TSharedPtr<FJsonValue> ValueField = (*ChangeObj)->TryGetField(TEXT("value"));
			if (ValueField.IsValid())
			{
				switch (ValueField->Type)
				{
				case EJson::String:
					Change.Value = ValueField->AsString();
					break;
				case EJson::Number:
					Change.Value = FString::SanitizeFloat(ValueField->AsNumber());
					break;
				case EJson::Boolean:
					Change.Value = ValueField->AsBool() ? TEXT("True") : TEXT("False");
					break;
				default:
					OutError = FString::Printf(TEXT("Invalid value type for property '%s'"), *Change.PropertyName);
					return false;
				}
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Missing 'value' for property '%s'"), *Change.PropertyName);
			return false;
		}

		OutChanges.Add(Change);
	}

	return true;
}

TArray<TPair<FString, FString>> FConfigureAssetTool::GetPropertyValues(UObject* Asset,
                                                                        const TArray<FString>& PropertyNames,
                                                                        TArray<FString>& OutErrors)
{
	TArray<TPair<FString, FString>> Results;

	for (const FString& PropName : PropertyNames)
	{
		FProperty* Property = FindProperty(Asset, PropName);
		if (!Property)
		{
			OutErrors.Add(FString::Printf(TEXT("%s - Property not found"), *PropName));
			continue;
		}

		FString Value = GetPropertyValue(Asset, Property);
		Results.Add(TPair<FString, FString>(Property->GetName(), Value));
	}

	return Results;
}

TArray<FConfigureAssetTool::FPropertyInfo> FConfigureAssetTool::ListEditableProperties(UObject* Asset)
{
	TArray<FPropertyInfo> Properties;

	if (!Asset) return Properties;

	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only show editable properties (visible in editor)
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		FPropertyInfo Info;
		Info.Name = Property->GetName();
		Info.Type = GetPropertyTypeName(Property);
		Info.CurrentValue = GetPropertyValue(Asset, Property);

		// Get category from metadata
		Info.Category = Property->GetMetaData(TEXT("Category"));
		if (Info.Category.IsEmpty())
		{
			Info.Category = TEXT("Default");
		}

		Properties.Add(Info);
	}

	// Sort by category then name
	Properties.Sort([](const FPropertyInfo& A, const FPropertyInfo& B)
	{
		if (A.Category != B.Category)
		{
			return A.Category < B.Category;
		}
		return A.Name < B.Name;
	});

	return Properties;
}

TArray<FConfigureAssetTool::FChangeResult> FConfigureAssetTool::ApplyChanges(UObject* WorkingAsset,
                                                                              UObject* OriginalAsset,
                                                                              const TArray<FPropertyChange>& Changes)
{
	TArray<FChangeResult> Results;

	// Mark object for transaction (undo/redo support)
	WorkingAsset->Modify();

	// Track which properties changed for Materials
	TArray<FProperty*> ChangedProperties;

	for (const FPropertyChange& Change : Changes)
	{
		FChangeResult Result;
		Result.PropertyName = Change.PropertyName;
		Result.bSuccess = false;

		FProperty* Property = FindProperty(WorkingAsset, Change.PropertyName);
		if (!Property)
		{
			Result.Error = TEXT("Property not found");
			Results.Add(Result);
			continue;
		}

		// Get old value
		Result.OldValue = GetPropertyValue(WorkingAsset, Property);

		// Notify pre-change with the actual property (critical for Materials!)
		WorkingAsset->PreEditChange(Property);

		// Set new value
		FString SetError;
		if (!SetPropertyValue(WorkingAsset, Property, Change.Value, SetError))
		{
			Result.Error = SetError;
			Results.Add(Result);
			continue;
		}

		// Mark package dirty
		WorkingAsset->MarkPackageDirty();

		// Create proper PropertyChangedEvent and notify post-change
		FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
		WorkingAsset->PostEditChangeProperty(PropertyEvent);

		// Track for Materials
		ChangedProperties.Add(Property);

		// Get new value for confirmation
		Result.NewValue = GetPropertyValue(WorkingAsset, Property);
		Result.bSuccess = true;

		Results.Add(Result);
	}

	// Handle asset-specific post-edit actions
	if (UMaterial* Material = Cast<UMaterial>(WorkingAsset))
	{
		// Force material recompilation for visual changes
		Material->ForceRecompileForRendering();

		// If the Material Editor is open, mark it as dirty so changes appear live
		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(OriginalAsset, false);
				if (EditorInstance)
				{
					IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
					if (MaterialEditor)
					{
						MaterialEditor->MarkMaterialDirty();
						UE_LOG(LogTemp, Log, TEXT("NeoStack ConfigureAsset: Marked Material Editor as dirty"));
					}
				}
			}
		}
	}
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(WorkingAsset))
	{
		// Recompile blueprint when directly editing it
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else if (Cast<UWidget>(WorkingAsset) || Cast<UActorComponent>(WorkingAsset))
	{
		// When editing a subobject (widget or component), refresh the parent blueprint
		RefreshBlueprintEditor(OriginalAsset);
	}

	return Results;
}

FProperty* FConfigureAssetTool::FindProperty(UObject* Asset, const FString& PropertyName)
{
	if (!Asset) return nullptr;

	// Case-insensitive search
	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		if (PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
		{
			return *PropIt;
		}
	}

	return nullptr;
}

FString FConfigureAssetTool::GetPropertyValue(UObject* Asset, FProperty* Property)
{
	if (!Asset || !Property) return TEXT("");

	const void* ContainerPtr = Asset;

	// Handle bool properties explicitly
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool bValue = BoolProp->GetPropertyValue_InContainer(ContainerPtr);
		return bValue ? TEXT("True") : TEXT("False");
	}

	// Handle enum properties explicitly
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		if (UEnum* Enum = EnumProp->GetEnum())
		{
			FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
			int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(ContainerPtr));
			return Enum->GetNameStringByValue(EnumValue);
		}
	}

	// Handle byte enums
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
		{
			uint8 ByteValue = ByteProp->GetPropertyValue_InContainer(ContainerPtr);
			return Enum->GetNameStringByValue(ByteValue);
		}
	}

	// Standard export for other types
	FString Value;
	Property->ExportText_InContainer(0, Value, ContainerPtr, nullptr, Asset, PPF_None);

	return Value;
}

bool FConfigureAssetTool::SetPropertyValue(UObject* Asset, FProperty* Property,
                                            const FString& Value, FString& OutError)
{
	if (!Asset || !Property)
	{
		OutError = TEXT("Invalid asset or property");
		return false;
	}

	void* ContainerPtr = Asset;

	// ImportText returns the pointer past the parsed text, or nullptr on failure
	const TCHAR* Result = Property->ImportText_InContainer(*Value, ContainerPtr, Asset, PPF_None);

	if (!Result)
	{
		// Try some common transformations for user-friendly input
		FString TransformedValue = Value;

		// Handle booleans
		if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			TransformedValue = TEXT("True");
		}
		else if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			TransformedValue = TEXT("False");
		}

		// Try again with transformed value
		Result = Property->ImportText_InContainer(*TransformedValue, ContainerPtr, Asset, PPF_None);

		if (!Result)
		{
			OutError = FString::Printf(TEXT("Failed to set value '%s'. Use list_properties to see valid format."), *Value);
			return false;
		}
	}

	return true;
}

FString FConfigureAssetTool::GetPropertyTypeName(FProperty* Property) const
{
	if (!Property) return TEXT("Unknown");

	// Handle enum properties specially to show available values
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		if (UEnum* Enum = EnumProp->GetEnum())
		{
			return FString::Printf(TEXT("Enum(%s)"), *Enum->GetName());
		}
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
		{
			return FString::Printf(TEXT("Enum(%s)"), *Enum->GetName());
		}
	}

	// Standard type names
	if (CastField<FBoolProperty>(Property)) return TEXT("Bool");
	if (CastField<FIntProperty>(Property)) return TEXT("Int");
	if (CastField<FFloatProperty>(Property)) return TEXT("Float");
	if (CastField<FDoubleProperty>(Property)) return TEXT("Double");
	if (CastField<FStrProperty>(Property)) return TEXT("String");
	if (CastField<FNameProperty>(Property)) return TEXT("Name");
	if (CastField<FTextProperty>(Property)) return TEXT("Text");

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return FString::Printf(TEXT("Struct(%s)"), *StructProp->Struct->GetName());
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return FString::Printf(TEXT("Object(%s)"), *ObjProp->PropertyClass->GetName());
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return TEXT("Array");
	}

	return Property->GetCPPType();
}

FString FConfigureAssetTool::GetAssetTypeName(UObject* Asset) const
{
	if (!Asset) return TEXT("Unknown");

	if (Cast<UAnimBlueprint>(Asset)) return TEXT("AnimBlueprint");
	if (Cast<UBlueprint>(Asset)) return TEXT("Blueprint");
	if (Cast<UMaterialFunction>(Asset)) return TEXT("MaterialFunction");
	if (Cast<UMaterial>(Asset)) return TEXT("Material");

	return Asset->GetClass()->GetName();
}

FString FConfigureAssetTool::FormatResults(const FString& AssetName, const FString& AssetType,
                                            const TArray<TPair<FString, FString>>& GetResults,
                                            const TArray<FString>& GetErrors,
                                            const TArray<FPropertyInfo>& ListedProperties,
                                            const TArray<FChangeResult>& ChangeResults) const
{
	FString Output = FString::Printf(TEXT("# CONFIGURE ASSET: %s\nType: %s\n"), *AssetName, *AssetType);

	// Get results
	if (GetResults.Num() > 0 || GetErrors.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Property Values (%d)\n"), GetResults.Num());

		for (const auto& Result : GetResults)
		{
			Output += FString::Printf(TEXT("  %s = %s\n"), *Result.Key, *Result.Value);
		}

		for (const FString& Error : GetErrors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *Error);
		}
	}

	// Listed properties
	if (ListedProperties.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Editable Properties (%d)\n"), ListedProperties.Num());

		FString CurrentCategory;
		for (const FPropertyInfo& Info : ListedProperties)
		{
			if (Info.Category != CurrentCategory)
			{
				CurrentCategory = Info.Category;
				Output += FString::Printf(TEXT("\n### %s\n"), *CurrentCategory);
			}

			// Truncate long values
			FString DisplayValue = Info.CurrentValue;
			if (DisplayValue.Len() > 50)
			{
				DisplayValue = DisplayValue.Left(47) + TEXT("...");
			}

			Output += FString::Printf(TEXT("  %s (%s) = %s\n"), *Info.Name, *Info.Type, *DisplayValue);
		}
	}

	// Change results
	if (ChangeResults.Num() > 0)
	{
		int32 SuccessCount = 0;
		int32 ErrorCount = 0;

		for (const FChangeResult& Result : ChangeResults)
		{
			if (Result.bSuccess) SuccessCount++;
			else ErrorCount++;
		}

		Output += FString::Printf(TEXT("\n## Changes Applied (%d)\n"), SuccessCount);

		for (const FChangeResult& Result : ChangeResults)
		{
			if (Result.bSuccess)
			{
				Output += FString::Printf(TEXT("+ %s: %s -> %s\n"),
				    *Result.PropertyName, *Result.OldValue, *Result.NewValue);
			}
		}

		if (ErrorCount > 0)
		{
			Output += FString::Printf(TEXT("\n## Errors (%d)\n"), ErrorCount);

			for (const FChangeResult& Result : ChangeResults)
			{
				if (!Result.bSuccess)
				{
					Output += FString::Printf(TEXT("! %s - %s\n"), *Result.PropertyName, *Result.Error);
				}
			}
		}
	}

	// Summary line
	int32 TotalOps = GetResults.Num() + (ListedProperties.Num() > 0 ? 1 : 0);
	int32 TotalChanges = 0;
	int32 TotalErrors = GetErrors.Num();

	for (const FChangeResult& Result : ChangeResults)
	{
		if (Result.bSuccess) TotalChanges++;
		else TotalErrors++;
	}

	if (TotalChanges > 0 || TotalErrors > 0)
	{
		Output += FString::Printf(TEXT("\n= %d properties changed, %d errors\n"), TotalChanges, TotalErrors);
	}

	return Output;
}

UObject* FConfigureAssetTool::FindSubobject(UObject* Asset, const FString& SubobjectName)
{
	if (!Asset || SubobjectName.IsEmpty())
	{
		return nullptr;
	}

	FName SubobjectFName(*SubobjectName);

	// Widget Blueprint: find widget in WidgetTree
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
	{
		if (WidgetBP->WidgetTree)
		{
			return WidgetBP->WidgetTree->FindWidget(SubobjectFName);
		}
		return nullptr;
	}

	// Regular Blueprint: find component in SimpleConstructionScript
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		if (Blueprint->SimpleConstructionScript)
		{
			USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(SubobjectFName);
			if (Node && Node->ComponentTemplate)
			{
				return Node->ComponentTemplate;
			}
		}
		return nullptr;
	}

	// For other asset types, try to find a subobject by name using UObject's subobject system
	return FindObject<UObject>(Asset, *SubobjectName);
}

void FConfigureAssetTool::RefreshBlueprintEditor(UObject* Asset)
{
	if (!Asset || !GEditor)
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	// Widget Blueprint: refresh the widget designer
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(WidgetBP, false);
		if (EditorInstance)
		{
			FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance);
			if (WidgetEditor)
			{
				WidgetEditor->InvalidatePreview();
			}
		}
		return;
	}

	// Regular Blueprint: mark as modified to trigger recompile
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return;
	}
}

FString FConfigureAssetTool::ConfigureSlot(UWidget* Widget, const TSharedPtr<FJsonObject>& SlotConfig, UObject* OriginalAsset)
{
	if (!Widget || !SlotConfig.IsValid())
	{
		return TEXT("! Invalid widget or slot config");
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		return TEXT("! Widget has no slot (not in a panel)");
	}

	FString Result = TEXT("## Slot Configuration\n");
	int32 ChangesApplied = 0;

	// Handle CanvasPanelSlot
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		// Position
		const TArray<TSharedPtr<FJsonValue>>* PositionArray;
		if (SlotConfig->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
		{
			FVector2D OldPos = CanvasSlot->GetPosition();
			FVector2D NewPos((*PositionArray)[0]->AsNumber(), (*PositionArray)[1]->AsNumber());
			CanvasSlot->SetPosition(NewPos);
			Result += FString::Printf(TEXT("+ Position: (%.1f, %.1f) -> (%.1f, %.1f)\n"), OldPos.X, OldPos.Y, NewPos.X, NewPos.Y);
			ChangesApplied++;
		}

		// Size
		const TArray<TSharedPtr<FJsonValue>>* SizeArray;
		if (SlotConfig->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
		{
			FVector2D OldSize = CanvasSlot->GetSize();
			FVector2D NewSize((*SizeArray)[0]->AsNumber(), (*SizeArray)[1]->AsNumber());
			CanvasSlot->SetSize(NewSize);
			Result += FString::Printf(TEXT("+ Size: (%.1f, %.1f) -> (%.1f, %.1f)\n"), OldSize.X, OldSize.Y, NewSize.X, NewSize.Y);
			ChangesApplied++;
		}

		// Alignment
		const TArray<TSharedPtr<FJsonValue>>* AlignmentArray;
		if (SlotConfig->TryGetArrayField(TEXT("alignment"), AlignmentArray) && AlignmentArray->Num() >= 2)
		{
			FVector2D OldAlign = CanvasSlot->GetAlignment();
			FVector2D NewAlign((*AlignmentArray)[0]->AsNumber(), (*AlignmentArray)[1]->AsNumber());
			CanvasSlot->SetAlignment(NewAlign);
			Result += FString::Printf(TEXT("+ Alignment: (%.2f, %.2f) -> (%.2f, %.2f)\n"), OldAlign.X, OldAlign.Y, NewAlign.X, NewAlign.Y);
			ChangesApplied++;
		}

		// Anchors
		const TSharedPtr<FJsonObject>* AnchorsObj;
		if (SlotConfig->TryGetObjectField(TEXT("anchors"), AnchorsObj))
		{
			FAnchors OldAnchors = CanvasSlot->GetAnchors();
			FAnchors NewAnchors = OldAnchors;

			const TArray<TSharedPtr<FJsonValue>>* MinArray;
			if ((*AnchorsObj)->TryGetArrayField(TEXT("min"), MinArray) && MinArray->Num() >= 2)
			{
				NewAnchors.Minimum = FVector2D((*MinArray)[0]->AsNumber(), (*MinArray)[1]->AsNumber());
			}

			const TArray<TSharedPtr<FJsonValue>>* MaxArray;
			if ((*AnchorsObj)->TryGetArrayField(TEXT("max"), MaxArray) && MaxArray->Num() >= 2)
			{
				NewAnchors.Maximum = FVector2D((*MaxArray)[0]->AsNumber(), (*MaxArray)[1]->AsNumber());
			}

			CanvasSlot->SetAnchors(NewAnchors);
			Result += FString::Printf(TEXT("+ Anchors: Min(%.2f, %.2f) Max(%.2f, %.2f)\n"),
				NewAnchors.Minimum.X, NewAnchors.Minimum.Y, NewAnchors.Maximum.X, NewAnchors.Maximum.Y);
			ChangesApplied++;
		}

		// ZOrder
		int32 ZOrder;
		if (SlotConfig->TryGetNumberField(TEXT("z_order"), ZOrder))
		{
			int32 OldZOrder = CanvasSlot->GetZOrder();
			CanvasSlot->SetZOrder(ZOrder);
			Result += FString::Printf(TEXT("+ ZOrder: %d -> %d\n"), OldZOrder, ZOrder);
			ChangesApplied++;
		}

		// AutoSize
		bool bAutoSize;
		if (SlotConfig->TryGetBoolField(TEXT("auto_size"), bAutoSize))
		{
			bool bOldAutoSize = CanvasSlot->GetAutoSize();
			CanvasSlot->SetAutoSize(bAutoSize);
			Result += FString::Printf(TEXT("+ AutoSize: %s -> %s\n"),
				bOldAutoSize ? TEXT("true") : TEXT("false"),
				bAutoSize ? TEXT("true") : TEXT("false"));
			ChangesApplied++;
		}
	}
	// Handle HorizontalBoxSlot
	else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			HBoxSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}

		// Size (fill rule)
		const TSharedPtr<FJsonObject>* SizeObj;
		if (SlotConfig->TryGetObjectField(TEXT("size"), SizeObj))
		{
			FSlateChildSize Size;
			FString SizeRule;
			if ((*SizeObj)->TryGetStringField(TEXT("rule"), SizeRule))
			{
				if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Automatic;
				}
				else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Fill;
				}
			}
			double Value;
			if ((*SizeObj)->TryGetNumberField(TEXT("value"), Value))
			{
				Size.Value = Value;
			}
			HBoxSlot->SetSize(Size);
			Result += FString::Printf(TEXT("+ Size: %s (%.2f)\n"),
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
			ChangesApplied++;
		}
	}
	// Handle VerticalBoxSlot
	else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			VBoxSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}

		// Size (fill rule)
		const TSharedPtr<FJsonObject>* SizeObj;
		if (SlotConfig->TryGetObjectField(TEXT("size"), SizeObj))
		{
			FSlateChildSize Size;
			FString SizeRule;
			if ((*SizeObj)->TryGetStringField(TEXT("rule"), SizeRule))
			{
				if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Automatic;
				}
				else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Fill;
				}
			}
			double Value;
			if ((*SizeObj)->TryGetNumberField(TEXT("value"), Value))
			{
				Size.Value = Value;
			}
			VBoxSlot->SetSize(Size);
			Result += FString::Printf(TEXT("+ Size: %s (%.2f)\n"),
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
			ChangesApplied++;
		}
	}
	// Handle OverlaySlot
	else if (UOverlaySlot* OvlSlot = Cast<UOverlaySlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			OvlSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}
	}
	else
	{
		Result += FString::Printf(TEXT("! Unsupported slot type: %s\n"), *Slot->GetClass()->GetName());
	}

	// Synchronize and refresh
	if (ChangesApplied > 0)
	{
		Slot->SynchronizeProperties();
		RefreshBlueprintEditor(OriginalAsset);
		Result += FString::Printf(TEXT("= %d slot properties configured\n"), ChangesApplied);
	}
	else
	{
		Result += TEXT("= No slot properties changed\n");
	}

	return Result;
}
