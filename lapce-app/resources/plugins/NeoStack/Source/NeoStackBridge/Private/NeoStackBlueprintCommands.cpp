// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBlueprintCommands.h"
#include "NeoStackBridgeProtocol.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

// Helper to convert UE content path to full filesystem path
static FString ContentPathToFullPath(const FString& ContentPath)
{
	// Convert /Game/Path/Asset to full filesystem path
	FString PackagePath = ContentPath;

	// Remove object name suffix if present (e.g., /Game/Test34.Test34 -> /Game/Test34)
	int32 DotIndex;
	if (PackagePath.FindLastChar('.', DotIndex))
	{
		PackagePath = PackagePath.Left(DotIndex);
	}

	// Convert to filesystem path
	FString FilePath;
	if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath, FPackageName::GetAssetPackageExtension()))
	{
		return FPaths::ConvertRelativePathToFull(FilePath);
	}

	// Fallback: return original path
	return ContentPath;
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleFindDerivedBlueprints(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::FindDerivedBlueprints, TEXT("Missing arguments"));
	}

	FString ClassName = Args->GetStringField(TEXT("className"));
	if (ClassName.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::FindDerivedBlueprints, TEXT("Missing 'className' argument"));
	}

	// Resolve the C++ class
	UClass* ParentClass = ResolveClassName(ClassName);
	if (!ParentClass)
	{
		return MakeError(NeoStackProtocol::MessageType::FindDerivedBlueprints,
			FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Finding Blueprints derived from: %s"), *ParentClass->GetName());

	// Query Asset Registry for all Blueprint assets
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> BlueprintAssets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	// Filter to only those that derive from our class
	TArray<TSharedPtr<FJsonValue>> ResultArray;

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		// Get the parent class from asset metadata
		FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
		if (!ParentClassTag.IsSet())
		{
			continue;
		}

		FString ParentClassPath = ParentClassTag.GetValue();

		// Load the parent class to check inheritance
		// The tag value is like: /Script/Engine.Actor or /Game/Blueprints/BP_Base.BP_Base_C
		UClass* BlueprintParentClass = nullptr;

		// Try to find the class without loading the Blueprint
		FSoftClassPath SoftClassPath(ParentClassPath);
		BlueprintParentClass = SoftClassPath.ResolveClass();

		if (!BlueprintParentClass)
		{
			// If it's a Blueprint parent, we might need to load it
			// Skip for now to avoid loading all blueprints
			continue;
		}

		// Check if this Blueprint's parent derives from our target class
		if (BlueprintParentClass->IsChildOf(ParentClass))
		{
			TSharedPtr<FJsonObject> BlueprintInfo = MakeShareable(new FJsonObject());
			// Use full filesystem path instead of UE content path
			BlueprintInfo->SetStringField(TEXT("path"), ContentPathToFullPath(AssetData.GetObjectPathString()));
			BlueprintInfo->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			BlueprintInfo->SetStringField(TEXT("parentClass"), BlueprintParentClass->GetName());

			ResultArray.Add(MakeShareable(new FJsonValueObject(BlueprintInfo)));

			UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Found derived Blueprint: %s"), *AssetData.AssetName.ToString());
		}
	}

	// Build response
	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());
	ResponseData->SetArrayField(TEXT("blueprints"), ResultArray);
	ResponseData->SetNumberField(TEXT("count"), ResultArray.Num());

	UE_LOG(LogTemp, Log, TEXT("[NeoStackBridge] Found %d Blueprints derived from %s"), ResultArray.Num(), *ClassName);

	return MakeSuccess(NeoStackProtocol::MessageType::FindDerivedBlueprints, ResponseData);
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleFindBlueprintReferences(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintReferences, TEXT("Missing arguments"));
	}

	FString ClassName = Args->GetStringField(TEXT("className"));
	if (ClassName.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintReferences, TEXT("Missing 'className' argument"));
	}

	UClass* TargetClass = ResolveClassName(ClassName);
	if (!TargetClass)
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintReferences,
			FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	// Use Asset Registry to find references
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Get the class's package path for reference search
	FName ClassPackageName = TargetClass->GetOutermost()->GetFName();

	TArray<FAssetIdentifier> Referencers;
	AssetRegistry.GetReferencers(FAssetIdentifier(ClassPackageName), Referencers);

	TArray<TSharedPtr<FJsonValue>> ResultArray;

	for (const FAssetIdentifier& Identifier : Referencers)
	{
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Identifier.PackageName.ToString()));
		if (AssetData.IsValid())
		{
			// Check if it's a Blueprint
			if (AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
			{
				TSharedPtr<FJsonObject> RefInfo = MakeShareable(new FJsonObject());
				// Use full filesystem path instead of UE content path
				RefInfo->SetStringField(TEXT("path"), ContentPathToFullPath(AssetData.GetObjectPathString()));
				RefInfo->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
				RefInfo->SetStringField(TEXT("usageType"), TEXT("Reference"));

				ResultArray.Add(MakeShareable(new FJsonValueObject(RefInfo)));
			}
		}
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());
	ResponseData->SetArrayField(TEXT("blueprints"), ResultArray);
	ResponseData->SetNumberField(TEXT("count"), ResultArray.Num());

	return MakeSuccess(NeoStackProtocol::MessageType::FindBlueprintReferences, ResponseData);
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleGetBlueprintPropertyOverrides(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides, TEXT("Missing arguments"));
	}

	FString BlueprintPath = Args->GetStringField(TEXT("blueprintPath"));
	if (BlueprintPath.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides, TEXT("Missing 'blueprintPath' argument"));
	}

	// Load the Blueprint
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		return MakeError(NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides,
			FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		return MakeError(NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides, TEXT("Blueprint has no generated class"));
	}

	UClass* ParentClass = GeneratedClass->GetSuperClass();
	UObject* CDO = GeneratedClass->GetDefaultObject();
	UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject() : nullptr;

	TArray<TSharedPtr<FJsonValue>> OverridesArray;

	// Iterate properties and compare values
	for (TFieldIterator<FProperty> PropIt(GeneratedClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only check properties defined in parent class
		if (Property->GetOwnerClass() != GeneratedClass)
		{
			// Compare values
			void* CDOValue = Property->ContainerPtrToValuePtr<void>(CDO);
			void* ParentCDOValue = ParentCDO ? Property->ContainerPtrToValuePtr<void>(ParentCDO) : nullptr;

			if (ParentCDOValue && !Property->Identical(CDOValue, ParentCDOValue))
			{
				TSharedPtr<FJsonObject> Override = MakeShareable(new FJsonObject());
				Override->SetStringField(TEXT("property"), Property->GetName());

				// Get string representation of values
				FString DefaultValue, BlueprintValue;
				Property->ExportTextItem_Direct(DefaultValue, ParentCDOValue, nullptr, nullptr, PPF_None);
				Property->ExportTextItem_Direct(BlueprintValue, CDOValue, nullptr, nullptr, PPF_None);

				Override->SetStringField(TEXT("defaultValue"), DefaultValue);
				Override->SetStringField(TEXT("blueprintValue"), BlueprintValue);

				OverridesArray.Add(MakeShareable(new FJsonValueObject(Override)));
			}
		}
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());
	ResponseData->SetArrayField(TEXT("overrides"), OverridesArray);
	ResponseData->SetNumberField(TEXT("count"), OverridesArray.Num());

	return MakeSuccess(NeoStackProtocol::MessageType::GetBlueprintPropertyOverrides, ResponseData);
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleFindBlueprintFunctionUsages(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintFunctionUsages, TEXT("Missing arguments"));
	}

	FString ClassName = Args->GetStringField(TEXT("className"));
	FString FunctionName = Args->GetStringField(TEXT("functionName"));

	if (ClassName.IsEmpty() || FunctionName.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintFunctionUsages,
			TEXT("Missing 'className' or 'functionName' argument"));
	}

	UClass* TargetClass = ResolveClassName(ClassName);
	if (!TargetClass)
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintFunctionUsages,
			FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	// Find the function
	UFunction* TargetFunction = TargetClass->FindFunctionByName(*FunctionName);
	if (!TargetFunction)
	{
		return MakeError(NeoStackProtocol::MessageType::FindBlueprintFunctionUsages,
			FString::Printf(TEXT("Function not found: %s::%s"), *ClassName, *FunctionName));
	}

	// Check if it's BlueprintImplementableEvent or BlueprintCallable
	bool bIsBlueprintImplementable = TargetFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent);
	bool bIsBlueprintCallable = TargetFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable);

	TArray<TSharedPtr<FJsonValue>> ImplementationsArray;
	TArray<TSharedPtr<FJsonValue>> CallSitesArray;

	// For BlueprintImplementableEvent, find implementations
	if (bIsBlueprintImplementable)
	{
		// Query blueprints that derive from this class
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> BlueprintAssets;
		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		AssetRegistry.GetAssets(Filter, BlueprintAssets);

		for (const FAssetData& AssetData : BlueprintAssets)
		{
			// Check parent class
			FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
			if (!ParentClassTag.IsSet()) continue;

			FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
			UClass* BlueprintParentClass = SoftClassPath.ResolveClass();

			if (BlueprintParentClass && BlueprintParentClass->IsChildOf(TargetClass))
			{
				// This Blueprint might implement the function
				// Note: Full check would require loading the Blueprint
				TSharedPtr<FJsonObject> ImplInfo = MakeShareable(new FJsonObject());
				// Use full filesystem path instead of UE content path
				ImplInfo->SetStringField(TEXT("path"), ContentPathToFullPath(AssetData.GetObjectPathString()));
				ImplInfo->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
				ImplInfo->SetStringField(TEXT("type"), TEXT("PotentialImplementation"));

				ImplementationsArray.Add(MakeShareable(new FJsonValueObject(ImplInfo)));
			}
		}
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());
	ResponseData->SetBoolField(TEXT("isBlueprintImplementable"), bIsBlueprintImplementable);
	ResponseData->SetBoolField(TEXT("isBlueprintCallable"), bIsBlueprintCallable);
	ResponseData->SetArrayField(TEXT("implementations"), ImplementationsArray);
	ResponseData->SetArrayField(TEXT("callSites"), CallSitesArray);

	return MakeSuccess(NeoStackProtocol::MessageType::FindBlueprintFunctionUsages, ResponseData);
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleGetPropertyOverridesAcrossBlueprints(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints, TEXT("Missing arguments"));
	}

	FString ClassName = Args->GetStringField(TEXT("className"));
	FString PropertyName = Args->GetStringField(TEXT("propertyName"));

	if (ClassName.IsEmpty() || PropertyName.IsEmpty())
	{
		return MakeError(NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints,
			TEXT("Missing 'className' or 'propertyName' argument"));
	}

	// Resolve the C++ class
	UClass* ParentClass = ResolveClassName(ClassName);
	if (!ParentClass)
	{
		return MakeError(NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints,
			FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	// Find the property on the parent class
	FProperty* TargetProperty = ParentClass->FindPropertyByName(*PropertyName);
	if (!TargetProperty)
	{
		return MakeError(NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints,
			FString::Printf(TEXT("Property not found: %s::%s"), *ClassName, *PropertyName));
	}

	// Get the default value from the parent CDO
	UObject* ParentCDO = ParentClass->GetDefaultObject();
	void* ParentValue = TargetProperty->ContainerPtrToValuePtr<void>(ParentCDO);
	FString DefaultValue;
	TargetProperty->ExportTextItem_Direct(DefaultValue, ParentValue, nullptr, nullptr, PPF_None);

	// Query Asset Registry for all Blueprint assets
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> BlueprintAssets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	// Collect overrides
	TArray<TSharedPtr<FJsonValue>> OverridesArray;
	int32 OverrideCount = 0;

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		// Get the parent class from asset metadata
		FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
		if (!ParentClassTag.IsSet())
		{
			continue;
		}

		FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
		UClass* BlueprintParentClass = SoftClassPath.ResolveClass();

		if (!BlueprintParentClass || !BlueprintParentClass->IsChildOf(ParentClass))
		{
			continue;
		}

		// Load the Blueprint to check property value
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			continue;
		}

		UObject* BlueprintCDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (!BlueprintCDO)
		{
			continue;
		}

		// Get the property value from Blueprint CDO
		void* BlueprintValue = TargetProperty->ContainerPtrToValuePtr<void>(BlueprintCDO);

		// Compare with parent default
		if (!TargetProperty->Identical(BlueprintValue, ParentValue))
		{
			FString ValueStr;
			TargetProperty->ExportTextItem_Direct(ValueStr, BlueprintValue, nullptr, nullptr, PPF_None);

			TSharedPtr<FJsonObject> OverrideInfo = MakeShareable(new FJsonObject());
			OverrideInfo->SetStringField(TEXT("blueprintName"), AssetData.AssetName.ToString());
			// Use full filesystem path instead of UE content path
			OverrideInfo->SetStringField(TEXT("blueprintPath"), ContentPathToFullPath(AssetData.GetObjectPathString()));
			OverrideInfo->SetStringField(TEXT("value"), ValueStr);

			OverridesArray.Add(MakeShareable(new FJsonValueObject(OverrideInfo)));
			OverrideCount++;
		}
	}

	// Build response
	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());
	ResponseData->SetNumberField(TEXT("overrideCount"), OverrideCount);
	ResponseData->SetBoolField(TEXT("unchanged"), OverrideCount == 0);
	ResponseData->SetStringField(TEXT("defaultValue"), DefaultValue);
	ResponseData->SetArrayField(TEXT("overrides"), OverridesArray);

	return MakeSuccess(NeoStackProtocol::MessageType::GetPropertyOverridesAcrossBlueprints, ResponseData);
}

FNeoStackEvent FNeoStackBlueprintCommands::HandleGetBlueprintHintsBatch(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return MakeError(NeoStackProtocol::MessageType::GetBlueprintHintsBatch, TEXT("Missing arguments"));
	}

	// Get Asset Registry once for all queries
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Pre-fetch all Blueprint assets once
	TArray<FAssetData> AllBlueprintAssets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	AssetRegistry.GetAssets(Filter, AllBlueprintAssets);

	// Build a map of parent class -> derived blueprints for quick lookup
	TMap<FString, TArray<FAssetData>> DerivedBlueprintsMap;
	for (const FAssetData& AssetData : AllBlueprintAssets)
	{
		FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
		if (ParentClassTag.IsSet())
		{
			FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
			UClass* ParentClass = SoftClassPath.ResolveClass();
			if (ParentClass)
			{
				DerivedBlueprintsMap.FindOrAdd(ParentClass->GetName()).Add(AssetData);
			}
		}
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShareable(new FJsonObject());

	// Process class hints
	const TArray<TSharedPtr<FJsonValue>>* ClassesArray;
	if (Args->TryGetArrayField(TEXT("classes"), ClassesArray))
	{
		TSharedPtr<FJsonObject> ClassResults = MakeShareable(new FJsonObject());

		for (const TSharedPtr<FJsonValue>& ClassValue : *ClassesArray)
		{
			FString ClassName = ClassValue->AsString();
			UClass* ParentClass = ResolveClassName(ClassName);

			TArray<TSharedPtr<FJsonValue>> BlueprintsArray;

			if (ParentClass)
			{
				// Find all Blueprints derived from this class
				for (const FAssetData& AssetData : AllBlueprintAssets)
				{
					FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
					if (!ParentClassTag.IsSet()) continue;

					FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
					UClass* BlueprintParentClass = SoftClassPath.ResolveClass();

					if (BlueprintParentClass && BlueprintParentClass->IsChildOf(ParentClass))
					{
						TSharedPtr<FJsonObject> BpInfo = MakeShareable(new FJsonObject());
						BpInfo->SetStringField(TEXT("path"), ContentPathToFullPath(AssetData.GetObjectPathString()));
						BpInfo->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
						BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BpInfo)));
					}
				}
			}

			TSharedPtr<FJsonObject> ClassResult = MakeShareable(new FJsonObject());
			ClassResult->SetArrayField(TEXT("blueprints"), BlueprintsArray);
			ClassResult->SetNumberField(TEXT("count"), BlueprintsArray.Num());
			ClassResults->SetObjectField(ClassName, ClassResult);
		}

		ResponseData->SetObjectField(TEXT("classes"), ClassResults);
	}

	// Process property hints
	const TArray<TSharedPtr<FJsonValue>>* PropertiesArray;
	if (Args->TryGetArrayField(TEXT("properties"), PropertiesArray))
	{
		TSharedPtr<FJsonObject> PropertyResults = MakeShareable(new FJsonObject());

		for (const TSharedPtr<FJsonValue>& PropValue : *PropertiesArray)
		{
			const TSharedPtr<FJsonObject>* PropObj;
			if (!PropValue->TryGetObject(PropObj)) continue;

			FString ClassName = (*PropObj)->GetStringField(TEXT("className"));
			FString PropertyName = (*PropObj)->GetStringField(TEXT("name"));
			FString Key = ClassName + TEXT("::") + PropertyName;

			UClass* ParentClass = ResolveClassName(ClassName);
			TSharedPtr<FJsonObject> PropResult = MakeShareable(new FJsonObject());

			if (ParentClass)
			{
				FProperty* TargetProperty = ParentClass->FindPropertyByName(*PropertyName);
				if (TargetProperty)
				{
					UObject* ParentCDO = ParentClass->GetDefaultObject();
					void* ParentValue = TargetProperty->ContainerPtrToValuePtr<void>(ParentCDO);
					FString DefaultValue;
					TargetProperty->ExportTextItem_Direct(DefaultValue, ParentValue, nullptr, nullptr, PPF_None);

					TArray<TSharedPtr<FJsonValue>> OverridesArray;
					int32 OverrideCount = 0;

					// Check derived blueprints for overrides
					for (const FAssetData& AssetData : AllBlueprintAssets)
					{
						FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
						if (!ParentClassTag.IsSet()) continue;

						FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
						UClass* BlueprintParentClass = SoftClassPath.ResolveClass();

						if (!BlueprintParentClass || !BlueprintParentClass->IsChildOf(ParentClass)) continue;

						// Load Blueprint to check property value
						UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
						if (!Blueprint || !Blueprint->GeneratedClass) continue;

						UObject* BlueprintCDO = Blueprint->GeneratedClass->GetDefaultObject();
						if (!BlueprintCDO) continue;

						void* BlueprintValue = TargetProperty->ContainerPtrToValuePtr<void>(BlueprintCDO);

						if (!TargetProperty->Identical(BlueprintValue, ParentValue))
						{
							FString ValueStr;
							TargetProperty->ExportTextItem_Direct(ValueStr, BlueprintValue, nullptr, nullptr, PPF_None);

							TSharedPtr<FJsonObject> OverrideInfo = MakeShareable(new FJsonObject());
							OverrideInfo->SetStringField(TEXT("blueprintName"), AssetData.AssetName.ToString());
							OverrideInfo->SetStringField(TEXT("blueprintPath"), ContentPathToFullPath(AssetData.GetObjectPathString()));
							OverrideInfo->SetStringField(TEXT("value"), ValueStr);
							OverridesArray.Add(MakeShareable(new FJsonValueObject(OverrideInfo)));
							OverrideCount++;
						}
					}

					PropResult->SetNumberField(TEXT("overrideCount"), OverrideCount);
					PropResult->SetBoolField(TEXT("unchanged"), OverrideCount == 0);
					PropResult->SetStringField(TEXT("defaultValue"), DefaultValue);
					PropResult->SetArrayField(TEXT("overrides"), OverridesArray);
				}
			}

			PropertyResults->SetObjectField(Key, PropResult);
		}

		ResponseData->SetObjectField(TEXT("properties"), PropertyResults);
	}

	// Process function hints
	const TArray<TSharedPtr<FJsonValue>>* FunctionsArray;
	if (Args->TryGetArrayField(TEXT("functions"), FunctionsArray))
	{
		TSharedPtr<FJsonObject> FunctionResults = MakeShareable(new FJsonObject());

		for (const TSharedPtr<FJsonValue>& FuncValue : *FunctionsArray)
		{
			const TSharedPtr<FJsonObject>* FuncObj;
			if (!FuncValue->TryGetObject(FuncObj)) continue;

			FString ClassName = (*FuncObj)->GetStringField(TEXT("className"));
			FString FunctionName = (*FuncObj)->GetStringField(TEXT("name"));
			FString Key = ClassName + TEXT("::") + FunctionName;

			UClass* TargetClass = ResolveClassName(ClassName);
			TSharedPtr<FJsonObject> FuncResult = MakeShareable(new FJsonObject());
			TArray<TSharedPtr<FJsonValue>> ImplementationsArray;

			if (TargetClass)
			{
				UFunction* TargetFunction = TargetClass->FindFunctionByName(*FunctionName);
				if (TargetFunction && TargetFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
				{
					// Find potential implementations
					for (const FAssetData& AssetData : AllBlueprintAssets)
					{
						FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::ParentClassPath);
						if (!ParentClassTag.IsSet()) continue;

						FSoftClassPath SoftClassPath(ParentClassTag.GetValue());
						UClass* BlueprintParentClass = SoftClassPath.ResolveClass();

						if (BlueprintParentClass && BlueprintParentClass->IsChildOf(TargetClass))
						{
							TSharedPtr<FJsonObject> ImplInfo = MakeShareable(new FJsonObject());
							ImplInfo->SetStringField(TEXT("path"), ContentPathToFullPath(AssetData.GetObjectPathString()));
							ImplInfo->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
							ImplementationsArray.Add(MakeShareable(new FJsonValueObject(ImplInfo)));
						}
					}
				}
			}

			FuncResult->SetArrayField(TEXT("implementations"), ImplementationsArray);
			FuncResult->SetNumberField(TEXT("count"), ImplementationsArray.Num());
			FunctionResults->SetObjectField(Key, FuncResult);
		}

		ResponseData->SetObjectField(TEXT("functions"), FunctionResults);
	}

	return MakeSuccess(NeoStackProtocol::MessageType::GetBlueprintHintsBatch, ResponseData);
}

UClass* FNeoStackBlueprintCommands::ResolveClassName(const FString& ClassName)
{
	// Try direct lookup first (for full path like /Script/Engine.Actor)
	UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
	if (FoundClass)
	{
		return FoundClass;
	}

	// Try common prefixes
	TArray<FString> Prefixes = {
		TEXT("/Script/Engine."),
		TEXT("/Script/CoreUObject."),
		TEXT("/Script/UMG."),
	};

	// Also try the project's script path
	// Get all loaded modules and try them
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->GetName() == ClassName || Class->GetName() == ClassName.Mid(1)) // Handle "A" prefix
		{
			return Class;
		}
	}

	// Try with common UE prefixes stripped/added
	FString CleanName = ClassName;
	if (CleanName.StartsWith(TEXT("A")) || CleanName.StartsWith(TEXT("U")) || CleanName.StartsWith(TEXT("F")))
	{
		// Already has prefix, search as-is
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == CleanName)
			{
				return *It;
			}
		}
	}

	return nullptr;
}

FNeoStackEvent FNeoStackBlueprintCommands::MakeSuccess(const FString& Event, TSharedPtr<FJsonObject> Data)
{
	FNeoStackEvent Response;
	Response.Event = Event;
	Response.bSuccess = true;
	Response.Data = Data;
	return Response;
}

FNeoStackEvent FNeoStackBlueprintCommands::MakeError(const FString& Event, const FString& ErrorMessage)
{
	FNeoStackEvent Response;
	Response.Event = Event;
	Response.bSuccess = false;
	Response.Error = ErrorMessage;
	return Response;
}
