// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UUserDefinedStruct;
class UUserDefinedEnum;
class UDataTable;

/**
 * Tool for editing User Defined Structs, Enums, and DataTables
 *
 * Parameters:
 *   - name: Asset name (required)
 *   - path: Asset path (optional, defaults to /Game)
 *
 * Struct operations (target="Struct"):
 *   - add_fields: Array of field definitions to add [{name, type, default_value, description}]
 *   - remove_fields: Array of field names to remove
 *   - modify_fields: Array of field modifications [{name, new_name, type, default_value, description}]
 *
 * Enum operations (target="Enum"):
 *   - add_values: Array of value definitions to add [{name, display_name}]
 *   - remove_values: Array of value names to remove
 *   - modify_values: Array of value modifications [{index, display_name}]
 *
 * DataTable operations (target="DataTable"):
 *   - add_rows: Array of row definitions [{row_name, values: {column: value, ...}}]
 *   - remove_rows: Array of row names to remove
 *   - modify_rows: Array of row modifications [{row_name, values: {column: value, ...}}]
 *
 * Supported field types for structs:
 *   Boolean, Integer, Int64, Float, Double, String, Name, Text,
 *   Vector, Rotator, Transform, LinearColor, Color, Object, Class,
 *   SoftObject, SoftClass, Byte
 */
class NEOSTACK_API FEditDataStructureTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_data_structure"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit User Defined Structs, Enums, and DataTables");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Struct field definition for adding/modifying */
	struct FStructFieldOp
	{
		FString Name;
		FString NewName;       // For renaming
		FString Type;
		FString DefaultValue;
		FString Description;
	};

	/** Enum value definition for adding/modifying */
	struct FEnumValueOp
	{
		FString Name;
		FString DisplayName;
		int32 Index;           // For modify by index
	};

	/** DataTable row operation */
	struct FRowOp
	{
		FString RowName;
		TSharedPtr<FJsonObject> Values;  // Column -> Value mapping
	};

	// Struct operations
	FToolResult EditStruct(UUserDefinedStruct* Struct, const TSharedPtr<FJsonObject>& Args);
	int32 AddStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);
	int32 RemoveStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);
	int32 ModifyStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);

	// Enum operations
	FToolResult EditEnum(UUserDefinedEnum* Enum, const TSharedPtr<FJsonObject>& Args);
	int32 AddEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);
	int32 RemoveEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);
	int32 ModifyEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);

	// DataTable operations
	FToolResult EditDataTable(UDataTable* DataTable, const TSharedPtr<FJsonObject>& Args);
	int32 AddDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);
	int32 RemoveDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);
	int32 ModifyDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);

	/** Parse struct field operation from JSON */
	FStructFieldOp ParseStructFieldOp(const TSharedPtr<FJsonObject>& FieldObj);

	/** Parse enum value operation from JSON */
	FEnumValueOp ParseEnumValueOp(const TSharedPtr<FJsonObject>& ValueObj);

	/** Parse row operation from JSON */
	FRowOp ParseRowOp(const TSharedPtr<FJsonObject>& RowObj);

	/** Convert type name to FEdGraphPinType */
	FEdGraphPinType TypeNameToPinType(const FString& TypeName);

	/** Find struct field by name */
	int32 FindStructFieldIndex(UUserDefinedStruct* Struct, const FString& FieldName);
};
