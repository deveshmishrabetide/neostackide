// Copyright NeoStack. All Rights Reserved.

#include "Tools/CreateFileTool.h"
#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Asset creation
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Factories/Factory.h"
#include "UObject/UObjectIterator.h"

// Specialized Blueprint types
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprintFactory.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimBlueprint.h"

// Schema for struct field types
#include "EdGraphSchema_K2.h"

// Non-Blueprint asset types
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Factories/StructureFactory.h"
#include "Factories/EnumFactory.h"
#include "Factories/DataTableFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "Sound/SoundCue.h"
#include "WidgetBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimComposite.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Particles/ParticleSystem.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"

namespace
{
	/**
	 * Resolves the correct Blueprint and GeneratedClass types based on the parent class.
	 * Most types use standard UBlueprint + UBlueprintGeneratedClass.
	 * Only Widget and Animation Blueprints need special handling.
	 */
	void GetBlueprintClasses(UClass* ParentClass, UClass*& OutBlueprintClass, UClass*& OutGeneratedClass)
	{
		// Widget Blueprint - requires special classes for UMG editor
		if (ParentClass && ParentClass->IsChildOf(UUserWidget::StaticClass()))
		{
			OutBlueprintClass = UWidgetBlueprint::StaticClass();
			OutGeneratedClass = UWidgetBlueprintGeneratedClass::StaticClass();
			return;
		}

		// Animation Blueprint - requires UAnimBlueprint for anim editor
		if (ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			OutBlueprintClass = UAnimBlueprint::StaticClass();
			OutGeneratedClass = UBlueprintGeneratedClass::StaticClass();
			return;
		}

		// Default: standard Blueprint classes (Actor, Component, Pawn, Character, etc.)
		OutBlueprintClass = UBlueprint::StaticClass();
		OutGeneratedClass = UBlueprintGeneratedClass::StaticClass();
	}

	/**
	 * Registry of non-Blueprint asset types.
	 * Maps user-friendly type names to their UClass.
	 */
	struct FAssetTypeInfo
	{
		UClass* AssetClass;
		FString DefaultPath;

		FAssetTypeInfo() : AssetClass(nullptr) {}
		FAssetTypeInfo(UClass* InClass, const FString& InPath)
			: AssetClass(InClass), DefaultPath(InPath) {}
	};

	/**
	 * Gets the asset type info for a given type name.
	 * Returns nullptr for AssetClass if not a recognized non-Blueprint type.
	 */
	FAssetTypeInfo GetAssetTypeInfo(const FString& TypeName)
	{
		// AI
		if (TypeName.Equals(TEXT("BehaviorTree"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UBehaviorTree::StaticClass(), TEXT("/Game/AI"));
		}
		if (TypeName.Equals(TEXT("Blackboard"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("BlackboardData"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UBlackboardData::StaticClass(), TEXT("/Game/AI"));
		}

		// Data
		if (TypeName.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UDataTable::StaticClass(), TEXT("/Game/Data"));
		}
		if (TypeName.Equals(TEXT("CurveTable"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UCurveTable::StaticClass(), TEXT("/Game/Data"));
		}
		if (TypeName.Equals(TEXT("CurveFloat"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UCurveFloat::StaticClass(), TEXT("/Game/Curves"));
		}
		if (TypeName.Equals(TEXT("CurveVector"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UCurveVector::StaticClass(), TEXT("/Game/Curves"));
		}
		if (TypeName.Equals(TEXT("CurveLinearColor"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("CurveColor"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UCurveLinearColor::StaticClass(), TEXT("/Game/Curves"));
		}

		// Materials
		if (TypeName.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UMaterial::StaticClass(), TEXT("/Game/Materials"));
		}
		if (TypeName.Equals(TEXT("MaterialInstance"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("MaterialInstanceConstant"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UMaterialInstanceConstant::StaticClass(), TEXT("/Game/Materials"));
		}
		if (TypeName.Equals(TEXT("MaterialFunction"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UMaterialFunction::StaticClass(), TEXT("/Game/Materials/Functions"));
		}
		if (TypeName.Equals(TEXT("MaterialParameterCollection"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UMaterialParameterCollection::StaticClass(), TEXT("/Game/Materials"));
		}

		// Audio
		if (TypeName.Equals(TEXT("SoundCue"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(USoundCue::StaticClass(), TEXT("/Game/Audio"));
		}

		// Animation assets (non-Blueprint)
		if (TypeName.Equals(TEXT("AnimMontage"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UAnimMontage::StaticClass(), TEXT("/Game/Animations"));
		}
		if (TypeName.Equals(TEXT("AnimComposite"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UAnimComposite::StaticClass(), TEXT("/Game/Animations"));
		}
		if (TypeName.Equals(TEXT("BlendSpace"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UBlendSpace::StaticClass(), TEXT("/Game/Animations"));
		}
		if (TypeName.Equals(TEXT("BlendSpace1D"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UBlendSpace1D::StaticClass(), TEXT("/Game/Animations"));
		}

		// Physics
		if (TypeName.Equals(TEXT("PhysicalMaterial"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("PhysicsMaterial"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UPhysicalMaterial::StaticClass(), TEXT("/Game/Physics"));
		}

		// Particles (legacy)
		if (TypeName.Equals(TEXT("ParticleSystem"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UParticleSystem::StaticClass(), TEXT("/Game/FX"));
		}

		// Textures
		if (TypeName.Equals(TEXT("RenderTarget"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("RenderTarget2D"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("TextureRenderTarget2D"), ESearchCase::IgnoreCase))
		{
			return FAssetTypeInfo(UTextureRenderTarget2D::StaticClass(), TEXT("/Game/Textures"));
		}

		// Not a recognized non-Blueprint type
		// Note: Widget Blueprints are handled specially in Execute() using IAssetTools
		return FAssetTypeInfo();
	}

	/**
	 * Checks if the type name refers to a Widget Blueprint.
	 */
	bool IsWidgetBlueprintType(const FString& TypeName)
	{
		return TypeName.Equals(TEXT("Widget"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("WidgetBlueprint"), ESearchCase::IgnoreCase) ||
			TypeName.Equals(TEXT("UserWidget"), ESearchCase::IgnoreCase);
	}

	/**
	 * Finds a factory that can create the given asset class.
	 * Iterates all UFactory subclasses to find one with matching SupportedClass.
	 */
	UFactory* FindFactoryForClass(UClass* AssetClass)
	{
		if (!AssetClass)
		{
			return nullptr;
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* FactoryClass = *It;

			if (!FactoryClass->IsChildOf(UFactory::StaticClass()) ||
				FactoryClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}

			UFactory* Factory = FactoryClass->GetDefaultObject<UFactory>();
			if (Factory && Factory->SupportedClass == AssetClass && Factory->CanCreateNew())
			{
				// Create a new instance of this factory
				return NewObject<UFactory>(GetTransientPackage(), FactoryClass);
			}
		}

		return nullptr;
	}
}

FToolResult FCreateFileTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Parent, Path, Content;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	if (!Args->TryGetStringField(TEXT("parent"), Parent) || Parent.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: parent"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("content"), Content);

	// Handle case where Name contains a full path (e.g., "/Game/Blueprints/BP_Enemy")
	// Extract the actual asset name and use the path from Name if no Path was provided
	if (Name.Contains(TEXT("/")))
	{
		FString ExtractedPath;
		FString ExtractedName;
		Name.Split(TEXT("/"), &ExtractedPath, &ExtractedName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

		// If no path was provided, use the path from the name
		if (Path.IsEmpty() && !ExtractedPath.IsEmpty())
		{
			Path = ExtractedPath;
		}

		// Use just the asset name
		Name = ExtractedName;

		if (Name.IsEmpty())
		{
			return FToolResult::Fail(TEXT("Invalid asset name: could not extract name from path"));
		}
	}

	// Route based on parent type
	if (Parent.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
	{
		return CreateTextFile(Name, Path, Content);
	}

	// Widget Blueprints need special handling via IAssetTools
	if (IsWidgetBlueprintType(Parent))
	{
		return CreateWidgetBlueprint(Name, Path.IsEmpty() ? TEXT("/Game/UI") : Path);
	}

	// User Defined Struct - special handling with fields
	if (Parent.Equals(TEXT("Struct"), ESearchCase::IgnoreCase) ||
		Parent.Equals(TEXT("UserDefinedStruct"), ESearchCase::IgnoreCase))
	{
		TArray<FStructFieldDef> Fields;
		const TArray<TSharedPtr<FJsonValue>>* FieldsArray;
		if (Args->TryGetArrayField(TEXT("fields"), FieldsArray))
		{
			Fields = ParseStructFields(FieldsArray);
		}
		return CreateUserDefinedStruct(Name, Path.IsEmpty() ? TEXT("/Game/Data") : Path, Fields);
	}

	// User Defined Enum - special handling with values
	if (Parent.Equals(TEXT("Enum"), ESearchCase::IgnoreCase) ||
		Parent.Equals(TEXT("UserDefinedEnum"), ESearchCase::IgnoreCase))
	{
		TArray<FEnumValueDef> Values;
		const TArray<TSharedPtr<FJsonValue>>* ValuesArray;
		if (Args->TryGetArrayField(TEXT("values"), ValuesArray))
		{
			Values = ParseEnumValues(ValuesArray);
		}
		return CreateUserDefinedEnum(Name, Path.IsEmpty() ? TEXT("/Game/Data") : Path, Values);
	}

	// DataTable - special handling with row_struct
	if (Parent.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase))
	{
		FString RowStructName;
		Args->TryGetStringField(TEXT("row_struct"), RowStructName);
		return CreateDataTable(Name, Path.IsEmpty() ? TEXT("/Game/Data") : Path, RowStructName);
	}

	// Check if it's a non-Blueprint asset type
	FAssetTypeInfo AssetInfo = GetAssetTypeInfo(Parent);
	if (AssetInfo.AssetClass)
	{
		return CreateAsset(Name, AssetInfo.AssetClass, Path.IsEmpty() ? AssetInfo.DefaultPath : Path);
	}

	// Otherwise, treat as Blueprint parent class
	return CreateBlueprint(Name, Parent, Path);
}

FToolResult FCreateFileTool::CreateTextFile(const FString& Name, const FString& Path, const FString& Content)
{
	// Validate name has extension
	if (!Name.Contains(TEXT(".")))
	{
		return FToolResult::Fail(TEXT("Text file name must include extension (e.g., MyActor.cpp)"));
	}

	// Build full path using shared utility
	FString FullPath = NeoStackToolUtils::BuildFilePath(Name, Path);

	// Ensure directory exists
	FString Error;
	if (!NeoStackToolUtils::EnsureDirectoryExists(FullPath, Error))
	{
		return FToolResult::Fail(Error);
	}

	// Write file
	if (!FFileHelper::SaveStringToFile(Content, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to write file: %s"), *FullPath));
	}

	int32 ByteCount = Content.Len();
	return FToolResult::Ok(FString::Printf(TEXT("Created %s (%d bytes)"), *FullPath, ByteCount));
}

FToolResult FCreateFileTool::CreateAsset(const FString& Name, UClass* AssetClass, const FString& Path)
{
	// Build asset path
	FString AssetPath = Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;
	FString FullAssetPath = PackageName + TEXT(".") + Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (ExistingAsset)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingAsset);
			}
			return FToolResult::Ok(FString::Printf(TEXT("Asset already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Find factory for this asset type
	UFactory* Factory = FindFactoryForClass(AssetClass);
	if (!Factory)
	{
		return FToolResult::Fail(FString::Printf(TEXT("No factory found for asset type: %s"), *AssetClass->GetName()));
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}

	// Create the asset using the factory
	UObject* NewAsset = Factory->FactoryCreateNew(
		AssetClass,
		Package,
		FName(*Name),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	);

	if (!NewAsset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create %s"), *AssetClass->GetName()));
	}

	// Mark dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAsset);

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}

	return FToolResult::Ok(FString::Printf(TEXT("Created %s at %s (type: %s)"),
		*Name, *PackageName, *AssetClass->GetName()));
}

FToolResult FCreateFileTool::CreateBlueprint(const FString& Name, const FString& ParentClassName, const FString& Path)
{
	// Find parent class using shared utility
	FString Error;
	UClass* ParentClass = NeoStackToolUtils::FindParentClass(ParentClassName, Error);
	if (!ParentClass)
	{
		return FToolResult::Fail(Error);
	}

	// Build asset path
	FString AssetPath = Path.IsEmpty() ? TEXT("/Game/Blueprints") : Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;
	FString FullAssetPath = PackageName + TEXT(".") + Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		// Try to load existing asset
		UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *FullAssetPath);
		if (ExistingBlueprint)
		{
			// Open existing Blueprint in editor
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingBlueprint);
			}
			return FToolResult::Ok(FString::Printf(TEXT("Blueprint already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}

	// Double-check no Blueprint with this name exists in the package
	if (FindObject<UBlueprint>(Package, *Name))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Blueprint '%s' already exists in %s"), *Name, *AssetPath));
	}

	// Resolve the correct Blueprint and GeneratedClass types for this parent
	UClass* BlueprintClass = nullptr;
	UClass* GeneratedClass = nullptr;
	GetBlueprintClasses(ParentClass, BlueprintClass, GeneratedClass);

	// Create Blueprint with the resolved types
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*Name),
		BPTYPE_Normal,
		BlueprintClass,
		GeneratedClass
	);

	if (!NewBlueprint)
	{
		return FToolResult::Fail(TEXT("Failed to create Blueprint"));
	}

	// Mark dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBlueprint);

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewBlueprint);
	}

	// Read the created asset's state using ReadFileTool
	FReadFileTool ReadTool;
	TSharedPtr<FJsonObject> ReadArgs = MakeShareable(new FJsonObject());
	ReadArgs->SetStringField(TEXT("name"), Name);
	ReadArgs->SetStringField(TEXT("path"), AssetPath);
	// Include summary, components, variables, and graphs to show full state
	TArray<TSharedPtr<FJsonValue>> IncludeArray;
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("summary"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("components"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("variables"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("graphs"))));
	ReadArgs->SetArrayField(TEXT("include"), IncludeArray);

	FToolResult ReadResult = ReadTool.Execute(ReadArgs);

	// Return creation message plus asset state
	FString Output = FString::Printf(TEXT("Created %s at %s (parent: %s)\n\n"),
		*Name, *PackageName, *ParentClass->GetName());
	Output += ReadResult.Output;

	return FToolResult::Ok(Output);
}

FToolResult FCreateFileTool::CreateWidgetBlueprint(const FString& Name, const FString& Path)
{
	// Build asset path
	FString AssetPath = Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		FString FullAssetPath = PackageName + TEXT(".") + Name;
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (ExistingAsset)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingAsset);
			}
			return FToolResult::Ok(FString::Printf(TEXT("Widget Blueprint already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Use IAssetTools with UWidgetBlueprintFactory for proper Widget Blueprint creation
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = UUserWidget::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(Name, AssetPath, UWidgetBlueprint::StaticClass(), Factory);

	if (!NewAsset)
	{
		return FToolResult::Fail(TEXT("Failed to create Widget Blueprint"));
	}

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}

	// Read the created asset's state using ReadFileTool
	FReadFileTool ReadTool;
	TSharedPtr<FJsonObject> ReadArgs = MakeShareable(new FJsonObject());
	ReadArgs->SetStringField(TEXT("name"), Name);
	ReadArgs->SetStringField(TEXT("path"), AssetPath);
	// Include summary, widgets, variables, and graphs for widget blueprints
	TArray<TSharedPtr<FJsonValue>> IncludeArray;
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("summary"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("widgets"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("variables"))));
	IncludeArray.Add(MakeShareable(new FJsonValueString(TEXT("graphs"))));
	ReadArgs->SetArrayField(TEXT("include"), IncludeArray);

	FToolResult ReadResult = ReadTool.Execute(ReadArgs);

	// Return creation message plus asset state
	FString Output = FString::Printf(TEXT("Created Widget Blueprint %s at %s\n\n"), *Name, *PackageName);
	Output += ReadResult.Output;

	return FToolResult::Ok(Output);
}

TArray<FCreateFileTool::FStructFieldDef> FCreateFileTool::ParseStructFields(const TArray<TSharedPtr<FJsonValue>>* FieldsArray)
{
	TArray<FStructFieldDef> Fields;
	if (!FieldsArray)
	{
		return Fields;
	}

	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		const TSharedPtr<FJsonObject>* FieldObj;
		if (!FieldValue->TryGetObject(FieldObj))
		{
			continue;
		}

		FStructFieldDef Field;
		(*FieldObj)->TryGetStringField(TEXT("name"), Field.Name);
		(*FieldObj)->TryGetStringField(TEXT("type"), Field.Type);
		(*FieldObj)->TryGetStringField(TEXT("default_value"), Field.DefaultValue);
		(*FieldObj)->TryGetStringField(TEXT("description"), Field.Description);

		if (!Field.Name.IsEmpty())
		{
			Fields.Add(Field);
		}
	}

	return Fields;
}

TArray<FCreateFileTool::FEnumValueDef> FCreateFileTool::ParseEnumValues(const TArray<TSharedPtr<FJsonValue>>* ValuesArray)
{
	TArray<FEnumValueDef> Values;
	if (!ValuesArray)
	{
		return Values;
	}

	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		const TSharedPtr<FJsonObject>* ValueObj;
		if (!ValueEntry->TryGetObject(ValueObj))
		{
			continue;
		}

		FEnumValueDef EnumVal;
		(*ValueObj)->TryGetStringField(TEXT("name"), EnumVal.Name);
		(*ValueObj)->TryGetStringField(TEXT("display_name"), EnumVal.DisplayName);
		(*ValueObj)->TryGetStringField(TEXT("description"), EnumVal.Description);

		if (!EnumVal.Name.IsEmpty())
		{
			Values.Add(EnumVal);
		}
	}

	return Values;
}

FToolResult FCreateFileTool::CreateUserDefinedStruct(const FString& Name, const FString& Path, const TArray<FStructFieldDef>& Fields)
{
	// Build asset path
	FString AssetPath = Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;
	FString FullAssetPath = PackageName + TEXT(".") + Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (ExistingAsset)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingAsset);
			}
			return FToolResult::Ok(FString::Printf(TEXT("Struct already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}

	// Create the User Defined Struct using FStructureEditorUtils
	UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*Name), RF_Public | RF_Standalone);

	if (!NewStruct)
	{
		return FToolResult::Fail(TEXT("Failed to create User Defined Struct"));
	}

	// Add fields to the struct
	TArray<FString> AddedFields;
	for (const FStructFieldDef& Field : Fields)
	{
		// Map type name to FEdGraphPinType
		FEdGraphPinType PinType;

		// Common type mappings
		if (Field.Type.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (Field.Type.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (Field.Type.Equals(TEXT("Int64"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		}
		else if (Field.Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (Field.Type.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (Field.Type.Equals(TEXT("String"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (Field.Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (Field.Type.Equals(TEXT("Text"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		}
		else if (Field.Type.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FVector"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (Field.Type.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (Field.Type.Equals(TEXT("Transform"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		}
		else if (Field.Type.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FLinearColor"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		}
		else if (Field.Type.Equals(TEXT("Color"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("FColor"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
		}
		else if (Field.Type.Equals(TEXT("Object"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("UObject"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
		else if (Field.Type.Equals(TEXT("Class"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("UClass"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
		else if (Field.Type.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("TSoftObjectPtr"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
		else if (Field.Type.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("TSoftClassPtr"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
		else if (Field.Type.Equals(TEXT("Byte"), ESearchCase::IgnoreCase) || Field.Type.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		}
		else
		{
			// Default to string for unknown types
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}

		// Add the field using FStructureEditorUtils
		if (FStructureEditorUtils::AddVariable(NewStruct, PinType))
		{
			// Get the newly added variable and rename it
			TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(NewStruct);
			if (VarDescArray.Num() > 0)
			{
				FStructVariableDescription& NewVar = VarDescArray.Last();

				// Rename the variable
				FStructureEditorUtils::RenameVariable(NewStruct, NewVar.VarGuid, Field.Name);

				// Set default value if provided
				if (!Field.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(NewStruct, NewVar.VarGuid, Field.DefaultValue);
				}

				// Set tooltip/description if provided
				if (!Field.Description.IsEmpty())
				{
					NewVar.ToolTip = Field.Description;
				}

				AddedFields.Add(FString::Printf(TEXT("%s: %s"), *Field.Name, *Field.Type));
			}
		}
	}

	// Mark dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewStruct);

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewStruct);
	}

	// Build output message
	FString Output = FString::Printf(TEXT("Created User Defined Struct %s at %s"), *Name, *PackageName);
	if (AddedFields.Num() > 0)
	{
		Output += TEXT("\n\nFields:");
		for (const FString& FieldDesc : AddedFields)
		{
			Output += FString::Printf(TEXT("\n  - %s"), *FieldDesc);
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FCreateFileTool::CreateUserDefinedEnum(const FString& Name, const FString& Path, const TArray<FEnumValueDef>& Values)
{
	// Build asset path
	FString AssetPath = Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;
	FString FullAssetPath = PackageName + TEXT(".") + Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (ExistingAsset)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingAsset);
			}
			return FToolResult::Ok(FString::Printf(TEXT("Enum already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}

	// Create the User Defined Enum using FEnumEditorUtils (returns UEnum*, cast to UUserDefinedEnum*)
	UEnum* CreatedEnum = FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*Name), RF_Public | RF_Standalone);
	UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(CreatedEnum);

	if (!NewEnum)
	{
		return FToolResult::Fail(TEXT("Failed to create User Defined Enum"));
	}

	// Add enum values
	TArray<FString> AddedValues;
	for (const FEnumValueDef& EnumVal : Values)
	{
		// AddNewEnumeratorForUserDefinedEnum returns void in UE 5.7+
		int32 NumBefore = NewEnum->NumEnums();
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);

		if (NewEnum->NumEnums() > NumBefore)
		{
			// Get the index of the newly added enumerator (before MAX)
			int32 EnumeratorIndex = NewEnum->NumEnums() - 2;  // -1 for MAX, -1 for zero-based
			if (EnumeratorIndex >= 0)
			{
				// Set the display name
				FString DisplayName = EnumVal.DisplayName.IsEmpty() ? EnumVal.Name : EnumVal.DisplayName;
				FEnumEditorUtils::SetEnumeratorDisplayName(NewEnum, EnumeratorIndex, FText::FromString(DisplayName));

				AddedValues.Add(DisplayName);
			}
		}
	}

	// Mark dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewEnum);

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewEnum);
	}

	// Build output message
	FString Output = FString::Printf(TEXT("Created User Defined Enum %s at %s"), *Name, *PackageName);
	if (AddedValues.Num() > 0)
	{
		Output += TEXT("\n\nValues:");
		for (int32 i = 0; i < AddedValues.Num(); ++i)
		{
			Output += FString::Printf(TEXT("\n  %d: %s"), i, *AddedValues[i]);
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FCreateFileTool::CreateDataTable(const FString& Name, const FString& Path, const FString& RowStructName)
{
	// Build asset path
	FString AssetPath = Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	FString PackageName = AssetPath / Name;
	FString FullAssetPath = PackageName + TEXT(".") + Name;

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (ExistingAsset)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingAsset);
			}
			return FToolResult::Ok(FString::Printf(TEXT("DataTable already exists: %s (opened in editor)"), *PackageName));
		}
	}

	// Find the row struct if specified
	UScriptStruct* RowStruct = nullptr;
	if (!RowStructName.IsEmpty())
	{
		// Try to find as User Defined Struct first
		FString StructPath = RowStructName;
		if (!StructPath.Contains(TEXT(".")))
		{
			// Could be a simple name - search in common locations
			// First try to find by path
			StructPath = FString::Printf(TEXT("/Game/Data/%s.%s"), *RowStructName, *RowStructName);
		}

		UObject* FoundObject = LoadObject<UObject>(nullptr, *StructPath);
		if (FoundObject)
		{
			RowStruct = Cast<UScriptStruct>(FoundObject);
			if (!RowStruct)
			{
				// Maybe it's a User Defined Struct asset
				UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(FoundObject);
				if (UserStruct)
				{
					RowStruct = UserStruct;
				}
			}
		}

		// If not found, try to find native struct by name (ANY_PACKAGE deprecated in UE5)
		if (!RowStruct)
		{
			RowStruct = FindFirstObject<UScriptStruct>(*RowStructName, EFindFirstObjectOptions::None);
		}

		if (!RowStruct)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Row struct not found: %s"), *RowStructName));
		}
	}
	else
	{
		// Default to FTableRowBase if no struct specified
		RowStruct = FTableRowBase::StaticStruct();
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}

	// Create DataTable using UDataTableFactory
	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = RowStruct;

	UDataTable* NewDataTable = Cast<UDataTable>(Factory->FactoryCreateNew(
		UDataTable::StaticClass(),
		Package,
		FName(*Name),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewDataTable)
	{
		return FToolResult::Fail(TEXT("Failed to create DataTable"));
	}

	// Mark dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewDataTable);

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewDataTable);
	}

	return FToolResult::Ok(FString::Printf(TEXT("Created DataTable %s at %s (row struct: %s)"),
		*Name, *PackageName, *RowStruct->GetName()));
}
