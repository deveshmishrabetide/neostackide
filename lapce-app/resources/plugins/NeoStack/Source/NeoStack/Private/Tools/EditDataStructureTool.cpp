// Copyright NeoStack. All Rights Reserved.

#include "Tools/EditDataStructureTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"

// For struct editing
#include "Engine/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"
#include "EdGraphSchema_K2.h"

// For enum editing
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

// For datatable editing
#include "Engine/DataTable.h"

// For editor support
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"

FToolResult FEditDataStructureTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Route based on asset type
	if (UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset))
	{
		return EditStruct(Struct, Args);
	}
	else if (UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Asset))
	{
		return EditEnum(Enum, Args);
	}
	else if (UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		return EditDataTable(DataTable, Args);
	}

	return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type for editing: %s"), *Asset->GetClass()->GetName()));
}

// ============================================================================
// STRUCT OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditStruct(UUserDefinedStruct* Struct, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process add_fields
	const TArray<TSharedPtr<FJsonValue>>* AddFieldsArray;
	if (Args->TryGetArrayField(TEXT("add_fields"), AddFieldsArray))
	{
		TotalChanges += AddStructFields(Struct, AddFieldsArray, Results);
	}

	// Process remove_fields
	const TArray<TSharedPtr<FJsonValue>>* RemoveFieldsArray;
	if (Args->TryGetArrayField(TEXT("remove_fields"), RemoveFieldsArray))
	{
		TotalChanges += RemoveStructFields(Struct, RemoveFieldsArray, Results);
	}

	// Process modify_fields
	const TArray<TSharedPtr<FJsonValue>>* ModifyFieldsArray;
	if (Args->TryGetArrayField(TEXT("modify_fields"), ModifyFieldsArray))
	{
		TotalChanges += ModifyStructFields(Struct, ModifyFieldsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_fields, remove_fields, or modify_fields."));
	}

	// Mark dirty
	Struct->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified struct %s (%d changes)\n"), *Struct->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		const TSharedPtr<FJsonObject>* FieldObj;
		if (!FieldValue->TryGetObject(FieldObj))
		{
			continue;
		}

		FStructFieldOp Op = ParseStructFieldOp(*FieldObj);
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped field with no name"));
			continue;
		}

		// Check if field already exists
		if (FindStructFieldIndex(Struct, Op.Name) >= 0)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' already exists"), *Op.Name));
			continue;
		}

		// Convert type to pin type
		FEdGraphPinType PinType = TypeNameToPinType(Op.Type);

		// Add the field
		if (FStructureEditorUtils::AddVariable(Struct, PinType))
		{
			TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
			if (VarDescArray.Num() > 0)
			{
				FStructVariableDescription& NewVar = VarDescArray.Last();

				// Rename the variable
				FStructureEditorUtils::RenameVariable(Struct, NewVar.VarGuid, Op.Name);

				// Set default value if provided
				if (!Op.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewVar.VarGuid, Op.DefaultValue);
				}

				// Set description if provided
				if (!Op.Description.IsEmpty())
				{
					NewVar.ToolTip = Op.Description;
				}

				OutResults.Add(FString::Printf(TEXT("Added field '%s' (%s)"), *Op.Name, *Op.Type));
				Added++;
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add field '%s'"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		FString FieldName;
		if (!FieldValue->TryGetString(FieldName))
		{
			continue;
		}

		TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
		bool bFound = false;

		for (const FStructVariableDescription& VarDesc : VarDescArray)
		{
			if (VarDesc.VarName.ToString().Equals(FieldName, ESearchCase::IgnoreCase))
			{
				if (FStructureEditorUtils::RemoveVariable(Struct, VarDesc.VarGuid))
				{
					OutResults.Add(FString::Printf(TEXT("Removed field '%s'"), *FieldName));
					Removed++;
					bFound = true;
				}
				break;
			}
		}

		if (!bFound)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' not found"), *FieldName));
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		const TSharedPtr<FJsonObject>* FieldObj;
		if (!FieldValue->TryGetObject(FieldObj))
		{
			continue;
		}

		FStructFieldOp Op = ParseStructFieldOp(*FieldObj);
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped modification with no field name"));
			continue;
		}

		TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
		bool bFound = false;

		for (FStructVariableDescription& VarDesc : VarDescArray)
		{
			if (VarDesc.VarName.ToString().Equals(Op.Name, ESearchCase::IgnoreCase))
			{
				bFound = true;
				TArray<FString> Changes;

				// Rename if new_name is provided
				if (!Op.NewName.IsEmpty() && !Op.NewName.Equals(Op.Name))
				{
					FStructureEditorUtils::RenameVariable(Struct, VarDesc.VarGuid, Op.NewName);
					Changes.Add(FString::Printf(TEXT("renamed to '%s'"), *Op.NewName));
				}

				// Change type if provided
				if (!Op.Type.IsEmpty())
				{
					FEdGraphPinType NewPinType = TypeNameToPinType(Op.Type);
					if (FStructureEditorUtils::ChangeVariableType(Struct, VarDesc.VarGuid, NewPinType))
					{
						Changes.Add(FString::Printf(TEXT("type -> %s"), *Op.Type));
					}
				}

				// Change default value if provided
				if (!Op.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(Struct, VarDesc.VarGuid, Op.DefaultValue);
					Changes.Add(FString::Printf(TEXT("default -> %s"), *Op.DefaultValue));
				}

				// Change description if provided
				if (!Op.Description.IsEmpty())
				{
					VarDesc.ToolTip = Op.Description;
					Changes.Add(TEXT("description updated"));
				}

				if (Changes.Num() > 0)
				{
					OutResults.Add(FString::Printf(TEXT("Modified '%s': %s"), *Op.Name, *FString::Join(Changes, TEXT(", "))));
					Modified++;
				}
				break;
			}
		}

		if (!bFound)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' not found for modification"), *Op.Name));
		}
	}

	return Modified;
}

// ============================================================================
// ENUM OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditEnum(UUserDefinedEnum* Enum, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process add_values
	const TArray<TSharedPtr<FJsonValue>>* AddValuesArray;
	if (Args->TryGetArrayField(TEXT("add_values"), AddValuesArray))
	{
		TotalChanges += AddEnumValues(Enum, AddValuesArray, Results);
	}

	// Process remove_values
	const TArray<TSharedPtr<FJsonValue>>* RemoveValuesArray;
	if (Args->TryGetArrayField(TEXT("remove_values"), RemoveValuesArray))
	{
		TotalChanges += RemoveEnumValues(Enum, RemoveValuesArray, Results);
	}

	// Process modify_values
	const TArray<TSharedPtr<FJsonValue>>* ModifyValuesArray;
	if (Args->TryGetArrayField(TEXT("modify_values"), ModifyValuesArray))
	{
		TotalChanges += ModifyEnumValues(Enum, ModifyValuesArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_values, remove_values, or modify_values."));
	}

	// Mark dirty
	Enum->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified enum %s (%d changes)\n"), *Enum->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		const TSharedPtr<FJsonObject>* ValueObj;
		if (!ValueEntry->TryGetObject(ValueObj))
		{
			continue;
		}

		FEnumValueOp Op = ParseEnumValueOp(*ValueObj);
		if (Op.Name.IsEmpty() && Op.DisplayName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped value with no name"));
			continue;
		}

		// Add a new enumerator (returns void in UE 5.7+)
		int32 NumBefore = Enum->NumEnums();
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);

		if (Enum->NumEnums() > NumBefore)
		{
			int32 NewIndex = Enum->NumEnums() - 2;  // -1 for MAX, -1 for zero-based
			if (NewIndex >= 0)
			{
				// Set display name
				FString DisplayName = Op.DisplayName.IsEmpty() ? Op.Name : Op.DisplayName;
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName));

				OutResults.Add(FString::Printf(TEXT("Added value '%s' at index %d"), *DisplayName, NewIndex));
				Added++;
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add enum value '%s'"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	// Collect indices to remove (work backwards to avoid index shifting)
	TArray<int32> IndicesToRemove;

	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		FString ValueName;
		int32 ValueIndex = -1;

		if (ValueEntry->TryGetString(ValueName))
		{
			// Find by name
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (DisplayName.Equals(ValueName, ESearchCase::IgnoreCase))
				{
					ValueIndex = i;
					break;
				}
			}
		}
		else if (ValueEntry->TryGetNumber(ValueIndex))
		{
			// Use index directly
		}

		if (ValueIndex >= 0 && ValueIndex < Enum->NumEnums() - 1)
		{
			IndicesToRemove.AddUnique(ValueIndex);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Enum value '%s' not found"), *ValueName));
		}
	}

	// Sort descending to remove from end first
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });

	int32 Removed = 0;
	for (int32 Index : IndicesToRemove)
	{
		FString DisplayName = Enum->GetDisplayNameTextByIndex(Index).ToString();
		int32 NumBefore = Enum->NumEnums();
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Index);
		if (Enum->NumEnums() < NumBefore)
		{
			OutResults.Add(FString::Printf(TEXT("Removed value '%s' (was index %d)"), *DisplayName, Index));
			Removed++;
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		const TSharedPtr<FJsonObject>* ValueObj;
		if (!ValueEntry->TryGetObject(ValueObj))
		{
			continue;
		}

		FEnumValueOp Op = ParseEnumValueOp(*ValueObj);

		// Find the value by index or name
		int32 TargetIndex = Op.Index;
		if (TargetIndex < 0 && !Op.Name.IsEmpty())
		{
			// Find by name
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (DisplayName.Equals(Op.Name, ESearchCase::IgnoreCase))
				{
					TargetIndex = i;
					break;
				}
			}
		}

		if (TargetIndex < 0 || TargetIndex >= Enum->NumEnums() - 1)
		{
			OutResults.Add(FString::Printf(TEXT("Enum value not found for modification")));
			continue;
		}

		// Set new display name
		if (!Op.DisplayName.IsEmpty())
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(Enum, TargetIndex, FText::FromString(Op.DisplayName));
			OutResults.Add(FString::Printf(TEXT("Modified value at index %d -> '%s'"), TargetIndex, *Op.DisplayName));
			Modified++;
		}
	}

	return Modified;
}

// ============================================================================
// DATATABLE OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditDataTable(UDataTable* DataTable, const TSharedPtr<FJsonObject>& Args)
{
	if (!DataTable->RowStruct)
	{
		return FToolResult::Fail(TEXT("DataTable has no row struct defined"));
	}

	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process add_rows
	const TArray<TSharedPtr<FJsonValue>>* AddRowsArray;
	if (Args->TryGetArrayField(TEXT("add_rows"), AddRowsArray))
	{
		TotalChanges += AddDataTableRows(DataTable, AddRowsArray, Results);
	}

	// Process remove_rows
	const TArray<TSharedPtr<FJsonValue>>* RemoveRowsArray;
	if (Args->TryGetArrayField(TEXT("remove_rows"), RemoveRowsArray))
	{
		TotalChanges += RemoveDataTableRows(DataTable, RemoveRowsArray, Results);
	}

	// Process modify_rows
	const TArray<TSharedPtr<FJsonValue>>* ModifyRowsArray;
	if (Args->TryGetArrayField(TEXT("modify_rows"), ModifyRowsArray))
	{
		TotalChanges += ModifyDataTableRows(DataTable, ModifyRowsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_rows, remove_rows, or modify_rows."));
	}

	// Mark dirty
	DataTable->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified DataTable %s (%d changes)\n"), *DataTable->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		const TSharedPtr<FJsonObject>* RowObj;
		if (!RowEntry->TryGetObject(RowObj))
		{
			continue;
		}

		FRowOp Op = ParseRowOp(*RowObj);
		if (Op.RowName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped row with no name"));
			continue;
		}

		// Check if row already exists
		if (DataTable->FindRowUnchecked(FName(*Op.RowName)))
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' already exists"), *Op.RowName));
			continue;
		}

		// Allocate and initialize a new row with default values
		uint8* NewRowData = (uint8*)FMemory::Malloc(DataTable->RowStruct->GetStructureSize());
		DataTable->RowStruct->InitializeStruct(NewRowData);
		DataTable->AddRow(FName(*Op.RowName), *(FTableRowBase*)NewRowData);
		FMemory::Free(NewRowData);

		// Get the new row and set values
		uint8* RowData = DataTable->FindRowUnchecked(FName(*Op.RowName));
		if (RowData && Op.Values.IsValid())
		{
			TArray<FString> SetValues;
			for (const auto& ValuePair : Op.Values->Values)
			{
				FString ColumnName = ValuePair.Key;
				FString ValueStr;
				ValuePair.Value->TryGetString(ValueStr);

				// Find the property
				FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*ColumnName));
				if (Property)
				{
					void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
					Property->ImportText_Direct(*ValueStr, PropertyValue, nullptr, PPF_None);
					SetValues.Add(FString::Printf(TEXT("%s=%s"), *ColumnName, *ValueStr));
				}
			}

			OutResults.Add(FString::Printf(TEXT("Added row '%s' (%s)"), *Op.RowName, *FString::Join(SetValues, TEXT(", "))));
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Added row '%s'"), *Op.RowName));
		}
		Added++;
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		FString RowName;
		if (!RowEntry->TryGetString(RowName))
		{
			continue;
		}

		if (DataTable->FindRowUnchecked(FName(*RowName)))
		{
			DataTable->RemoveRow(FName(*RowName));
			OutResults.Add(FString::Printf(TEXT("Removed row '%s'"), *RowName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' not found"), *RowName));
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		const TSharedPtr<FJsonObject>* RowObj;
		if (!RowEntry->TryGetObject(RowObj))
		{
			continue;
		}

		FRowOp Op = ParseRowOp(*RowObj);
		if (Op.RowName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped modification with no row name"));
			continue;
		}

		uint8* RowData = DataTable->FindRowUnchecked(FName(*Op.RowName));
		if (!RowData)
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' not found"), *Op.RowName));
			continue;
		}

		if (!Op.Values.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' has no values to modify"), *Op.RowName));
			continue;
		}

		TArray<FString> ModifiedValues;
		for (const auto& ValuePair : Op.Values->Values)
		{
			FString ColumnName = ValuePair.Key;
			FString ValueStr;
			ValuePair.Value->TryGetString(ValueStr);

			// Find the property
			FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*ColumnName));
			if (Property)
			{
				void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
				Property->ImportText_Direct(*ValueStr, PropertyValue, nullptr, PPF_None);
				ModifiedValues.Add(FString::Printf(TEXT("%s=%s"), *ColumnName, *ValueStr));
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("Column '%s' not found in row struct"), *ColumnName));
			}
		}

		if (ModifiedValues.Num() > 0)
		{
			OutResults.Add(FString::Printf(TEXT("Modified row '%s': %s"), *Op.RowName, *FString::Join(ModifiedValues, TEXT(", "))));
			Modified++;
		}
	}

	return Modified;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

FEditDataStructureTool::FStructFieldOp FEditDataStructureTool::ParseStructFieldOp(const TSharedPtr<FJsonObject>& FieldObj)
{
	FStructFieldOp Op;
	FieldObj->TryGetStringField(TEXT("name"), Op.Name);
	FieldObj->TryGetStringField(TEXT("new_name"), Op.NewName);
	FieldObj->TryGetStringField(TEXT("type"), Op.Type);
	FieldObj->TryGetStringField(TEXT("default_value"), Op.DefaultValue);
	FieldObj->TryGetStringField(TEXT("description"), Op.Description);
	return Op;
}

FEditDataStructureTool::FEnumValueOp FEditDataStructureTool::ParseEnumValueOp(const TSharedPtr<FJsonObject>& ValueObj)
{
	FEnumValueOp Op;
	Op.Index = -1;
	ValueObj->TryGetStringField(TEXT("name"), Op.Name);
	ValueObj->TryGetStringField(TEXT("display_name"), Op.DisplayName);
	ValueObj->TryGetNumberField(TEXT("index"), Op.Index);
	return Op;
}

FEditDataStructureTool::FRowOp FEditDataStructureTool::ParseRowOp(const TSharedPtr<FJsonObject>& RowObj)
{
	FRowOp Op;
	RowObj->TryGetStringField(TEXT("row_name"), Op.RowName);

	const TSharedPtr<FJsonObject>* ValuesObj;
	if (RowObj->TryGetObjectField(TEXT("values"), ValuesObj))
	{
		Op.Values = *ValuesObj;
	}

	return Op;
}

FEdGraphPinType FEditDataStructureTool::TypeNameToPinType(const FString& TypeName)
{
	FEdGraphPinType PinType;

	if (TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeName.Equals(TEXT("Int64"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FVector"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeName.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FLinearColor"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (TypeName.Equals(TEXT("Color"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FColor"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
	}
	else if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("UObject"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("UClass"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("TSoftObjectPtr"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("TSoftClassPtr"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("Byte"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else
	{
		// Default to string for unknown types
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}

	return PinType;
}

int32 FEditDataStructureTool::FindStructFieldIndex(UUserDefinedStruct* Struct, const FString& FieldName)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);

	for (int32 i = 0; i < VarDescArray.Num(); i++)
	{
		if (VarDescArray[i].VarName.ToString().Equals(FieldName, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}

	return -1;
}
