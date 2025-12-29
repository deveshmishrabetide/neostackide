// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Tool for reading and configuring asset properties using UE5 reflection system.
 * Supports ANY editable property on Materials, Blueprints, AnimBlueprints, Widgets, Components, etc.
 *
 * Three modes of operation:
 * 1. GET - Read specific property values
 * 2. LIST - Discover all editable properties on an asset
 * 3. SET - Change property values
 *
 * Uses ExportText/ImportText for dynamic property access:
 * - Enums work directly: "BLEND_Translucent", "BLEND_Masked"
 * - Booleans: "True", "False"
 * - Numbers: "0.5", "100"
 * - Structs: "(X=1,Y=2,Z=3)" for vectors, etc.
 * - No hardcoding needed - new properties automatically work
 *
 * Subobject support (widgets in Widget Blueprints, components in Blueprints):
 * Use the "subobject" parameter to target a specific widget or component.
 *
 * Example - Configure widget property:
 * {
 *   "name": "WBP_MainMenu",
 *   "subobject": "StartButton",
 *   "changes": [{"property": "ColorAndOpacity", "value": "(R=1,G=0,B=0,A=1)"}]
 * }
 *
 * Example - Configure component property:
 * {
 *   "name": "BP_Enemy",
 *   "subobject": "MeshComponent",
 *   "changes": [{"property": "RelativeScale3D", "value": "(X=2,Y=2,Z=2)"}]
 * }
 *
 * Example - List widget properties:
 * {
 *   "name": "WBP_MainMenu",
 *   "subobject": "TitleText",
 *   "list_properties": true
 * }
 *
 * Example - Set material properties:
 * {
 *   "name": "M_BaseMaterial",
 *   "changes": [
 *     {"property": "BlendMode", "value": "BLEND_Translucent"},
 *     {"property": "TwoSided", "value": "true"}
 *   ]
 * }
 */
class NEOSTACK_API FConfigureAssetTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("configure_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Read and configure properties on Materials, Blueprints, AnimBlueprints using reflection");
	}

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Property change request from JSON */
	struct FPropertyChange
	{
		FString PropertyName;
		FString Value;
	};

	/** Result of applying a single property change */
	struct FChangeResult
	{
		FString PropertyName;
		FString OldValue;
		FString NewValue;
		bool bSuccess;
		FString Error;
	};

	/** Property info for listing */
	struct FPropertyInfo
	{
		FString Name;
		FString Type;
		FString CurrentValue;
		FString Category;
	};

	/** Parse property changes from JSON array */
	bool ParseChanges(const TArray<TSharedPtr<FJsonValue>>& ChangesArray,
	                  TArray<FPropertyChange>& OutChanges, FString& OutError);

	/** Get values of specific properties */
	TArray<TPair<FString, FString>> GetPropertyValues(UObject* Asset, const TArray<FString>& PropertyNames,
	                                                   TArray<FString>& OutErrors);

	/** List all editable properties on an asset */
	TArray<FPropertyInfo> ListEditableProperties(UObject* Asset);

	/** Apply changes to an asset using reflection (WorkingAsset may be preview copy when editor is open) */
	TArray<FChangeResult> ApplyChanges(UObject* WorkingAsset, UObject* OriginalAsset, const TArray<FPropertyChange>& Changes);

	/** Find a property on the asset by name (case-insensitive) */
	FProperty* FindProperty(UObject* Asset, const FString& PropertyName);

	/** Get the current value of a property as string */
	FString GetPropertyValue(UObject* Asset, FProperty* Property);

	/** Set a property value from string */
	bool SetPropertyValue(UObject* Asset, FProperty* Property, const FString& Value, FString& OutError);

	/** Get property type as readable string */
	FString GetPropertyTypeName(FProperty* Property) const;

	/** Get asset type display name */
	FString GetAssetTypeName(UObject* Asset) const;

	/** Format results to output string */
	FString FormatResults(const FString& AssetName, const FString& AssetType,
	                      const TArray<TPair<FString, FString>>& GetResults,
	                      const TArray<FString>& GetErrors,
	                      const TArray<FPropertyInfo>& ListedProperties,
	                      const TArray<FChangeResult>& ChangeResults) const;

	/** Find a subobject (widget in Widget Blueprint, component in Blueprint) by name */
	UObject* FindSubobject(UObject* Asset, const FString& SubobjectName);

	/** Refresh the Blueprint editor when a subobject was modified */
	void RefreshBlueprintEditor(UObject* Asset);

	/** Configure slot properties for a widget (position, size, anchors, etc.) */
	FString ConfigureSlot(class UWidget* Widget, const TSharedPtr<FJsonObject>& SlotConfig, UObject* OriginalAsset);
};
