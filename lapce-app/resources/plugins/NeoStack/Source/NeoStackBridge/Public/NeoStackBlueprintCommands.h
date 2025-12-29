// Copyright NeoStack. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeoStackBridgeProtocol.h"

/**
 * Blueprint-related commands for IDE integration
 * Queries the Asset Registry for Blueprint information
 */
class NEOSTACKBRIDGE_API FNeoStackBlueprintCommands
{
public:
	/**
	 * Find all Blueprints that derive from a C++ class
	 * Args: { "className": "AMyCharacter" } or { "className": "/Script/MyGame.MyCharacter" }
	 * Returns: { "blueprints": [{ "path": "/Game/BP_Player", "name": "BP_Player" }, ...] }
	 */
	static FNeoStackEvent HandleFindDerivedBlueprints(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Find all Blueprints that reference/use a C++ class (as parent or variable type)
	 * Args: { "className": "UMyComponent" }
	 * Returns: { "blueprints": [{ "path": "/Game/BP_Actor", "name": "BP_Actor", "usageType": "Parent|Variable|Function" }, ...] }
	 */
	static FNeoStackEvent HandleFindBlueprintReferences(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Get property values overridden in a Blueprint
	 * Args: { "blueprintPath": "/Game/BP_Player", "className": "AMyCharacter" }
	 * Returns: { "overrides": [{ "property": "Health", "defaultValue": "100", "blueprintValue": "150" }, ...] }
	 */
	static FNeoStackEvent HandleGetBlueprintPropertyOverrides(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Check if a C++ UFUNCTION is implemented or called in any Blueprint
	 * Args: { "className": "AMyCharacter", "functionName": "TakeDamage" }
	 * Returns: { "implementations": [...], "callSites": [...] }
	 */
	static FNeoStackEvent HandleFindBlueprintFunctionUsages(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Get property override information across all derived Blueprints
	 * Args: { "className": "AMyCharacter", "propertyName": "Health" }
	 * Returns: { "overrideCount": 3, "unchanged": false, "overrides": [{ "blueprintName": "BP_Player", "value": "150" }, ...] }
	 */
	static FNeoStackEvent HandleGetPropertyOverridesAcrossBlueprints(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Batch fetch all Blueprint hints for a file in one request
	 * Args: { "classes": ["AMyActor"], "properties": [{"className": "AMyActor", "name": "Health"}, ...], "functions": [...] }
	 * Returns: { "classes": {...}, "properties": {...}, "functions": {...} }
	 */
	static FNeoStackEvent HandleGetBlueprintHintsBatch(const TSharedPtr<FJsonObject>& Args);

private:
	/** Helper to resolve class name to UClass */
	static UClass* ResolveClassName(const FString& ClassName);

	/** Create success response with data */
	static FNeoStackEvent MakeSuccess(const FString& Event, TSharedPtr<FJsonObject> Data);

	/** Create error response */
	static FNeoStackEvent MakeError(const FString& Event, const FString& ErrorMessage);
};
