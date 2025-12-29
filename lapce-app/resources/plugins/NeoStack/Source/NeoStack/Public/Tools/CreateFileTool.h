// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "Tools/NeoStackToolBase.h"

/**
 * Tool for creating files and assets
 *
 * Parameters:
 *   - name: File/asset name (e.g., "MyActor.cpp" or "BP_Enemy")
 *   - parent: "Text" for text files, asset type for non-Blueprints, or UE class name for Blueprints
 *   - path: Optional folder path (relative to project for text, /Game/... for assets)
 *   - content: File content (required for text files, ignored for other types)
 *   - fields: Array of field definitions for Struct (name, type, default_value)
 *   - values: Array of enum value definitions for Enum (name, display_name)
 *   - row_struct: Row struct name for DataTable creation
 *
 * Supported asset types:
 *   - Text files: parent="Text"
 *   - Blueprints: parent=<ClassName> (e.g., "Actor", "Character", "UserWidget", "AnimInstance")
 *   - AI: "BehaviorTree", "Blackboard"
 *   - Data: "DataTable", "CurveTable", "CurveFloat", "CurveVector", "CurveLinearColor"
 *   - Data Structures: "Struct", "Enum"
 *   - Materials: "Material", "MaterialInstance", "MaterialFunction", "MaterialParameterCollection"
 *   - Audio: "SoundCue"
 *   - Animation: "AnimMontage", "AnimComposite", "BlendSpace", "BlendSpace1D"
 *   - Physics: "PhysicalMaterial"
 *   - FX: "ParticleSystem"
 *   - Textures: "RenderTarget", "RenderTarget2D"
 *   - Widgets: "Widget", "WidgetBlueprint", "UserWidget"
 */
class NEOSTACK_API FCreateFileTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("create_file"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create a file or asset. Use parent='Text' for text files, asset type name for non-Blueprints (e.g., 'BehaviorTree', 'Material', 'Struct', 'Enum'), 'Widget' for Widget Blueprints, or a UE class name for Blueprints (e.g., 'Actor', 'Character').");
	}
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;

private:
	/** Struct field definition */
	struct FStructFieldDef
	{
		FString Name;
		FString Type;           // Boolean, Integer, Float, String, Vector, Object, etc.
		FString DefaultValue;
		FString Description;
	};

	/** Enum value definition */
	struct FEnumValueDef
	{
		FString Name;
		FString DisplayName;
		FString Description;
	};

	FToolResult CreateTextFile(const FString& Name, const FString& Path, const FString& Content);
	FToolResult CreateAsset(const FString& Name, UClass* AssetClass, const FString& Path);
	FToolResult CreateBlueprint(const FString& Name, const FString& ParentClass, const FString& Path);
	FToolResult CreateWidgetBlueprint(const FString& Name, const FString& Path);

	/** Create a User Defined Struct with optional fields */
	FToolResult CreateUserDefinedStruct(const FString& Name, const FString& Path, const TArray<FStructFieldDef>& Fields);

	/** Create a User Defined Enum with values */
	FToolResult CreateUserDefinedEnum(const FString& Name, const FString& Path, const TArray<FEnumValueDef>& Values);

	/** Create a DataTable with a specified row struct */
	FToolResult CreateDataTable(const FString& Name, const FString& Path, const FString& RowStructName);

	/** Parse struct fields from JSON array */
	TArray<FStructFieldDef> ParseStructFields(const TArray<TSharedPtr<FJsonValue>>* FieldsArray);

	/** Parse enum values from JSON array */
	TArray<FEnumValueDef> ParseEnumValues(const TArray<TSharedPtr<FJsonValue>>* ValuesArray);
};
