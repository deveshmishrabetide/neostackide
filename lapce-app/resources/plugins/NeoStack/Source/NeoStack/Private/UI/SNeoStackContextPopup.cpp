// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackContextPopup.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "SNeoStackContextPopup"

void SNeoStackContextPopup::Construct(const FArguments& InArgs)
{
	OnItemSelectedDelegate = InArgs._OnItemSelected;

	// Scan for files
	ScanProjectFiles();
	ApplyFilter();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1e1e1e")))))
		.Padding(2.0f)
		[
			SNew(SBox)
			.MinDesiredWidth(400.0f)
			.MaxDesiredHeight(300.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(ItemListView, SListView<TSharedPtr<FContextItem>>)
					.ListItemsSource(&ListViewItems)
					.OnGenerateRow(this, &SNeoStackContextPopup::GenerateItemRow)
					.OnMouseButtonClick(this, &SNeoStackContextPopup::OnItemClicked)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]
	];

	UpdateListViewItems();
}

void SNeoStackContextPopup::ScanProjectFiles()
{
	AllItems.Empty();

	// Add C++ files category
	AllItems.Add(FContextItem::Category(TEXT("C++ Files")));

	// Scan Source directory for C++ files
	FString SourceDir = FPaths::ProjectDir() / TEXT("Source");
	ScanCppDirectory(SourceDir);

	// Also scan plugin source directories
	FString PluginsDir = FPaths::ProjectDir() / TEXT("Plugins");
	TArray<FString> PluginDirs;
	IFileManager::Get().FindFiles(PluginDirs, *(PluginsDir / TEXT("*")), false, true);
	for (const FString& PluginDir : PluginDirs)
	{
		FString PluginSourceDir = PluginsDir / PluginDir / TEXT("Source");
		ScanCppDirectory(PluginSourceDir);
	}

	// Add Blueprints category
	AllItems.Add(FContextItem::Category(TEXT("Blueprints")));
	ScanBlueprintAssets();
}

void SNeoStackContextPopup::ScanCppDirectory(const FString& Directory)
{
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		return;
	}

	TArray<FString> FoundFiles;

	// Find all .h and .cpp files recursively
	IFileManager::Get().FindFilesRecursive(FoundFiles, *Directory, TEXT("*.h"), true, false);
	IFileManager::Get().FindFilesRecursive(FoundFiles, *Directory, TEXT("*.cpp"), true, false);

	for (const FString& FilePath : FoundFiles)
	{
		FString FileName = FPaths::GetCleanFilename(FilePath);
		FString Extension = FPaths::GetExtension(FilePath).ToLower();

		EContextItemType Type = Extension == TEXT("h") ? EContextItemType::CppHeader : EContextItemType::CppSource;

		// Make path relative to project
		FString RelativePath = FilePath;
		FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectDir());

		AllItems.Add(FContextItem(FileName, RelativePath, Type));
	}
}

void SNeoStackContextPopup::ScanBlueprintAssets()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Query for Blueprint assets
	TArray<FAssetData> BlueprintAssets;

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));

	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	for (const FAssetData& Asset : BlueprintAssets)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString AssetPath = Asset.GetObjectPathString();

		// Determine type based on parent class or naming
		EContextItemType Type = EContextItemType::Blueprint;

		// Check if it's a widget blueprint
		if (AssetName.Contains(TEXT("Widget")) || AssetName.StartsWith(TEXT("WBP_")) || AssetName.StartsWith(TEXT("W_")))
		{
			Type = EContextItemType::Widget;
		}

		AllItems.Add(FContextItem(AssetName, AssetPath, Type));
	}

	// Also scan for materials
	TArray<FAssetData> MaterialAssets;
	FARFilter MaterialFilter;
	MaterialFilter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	MaterialFilter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
	MaterialFilter.bRecursiveClasses = true;
	MaterialFilter.bRecursivePaths = true;
	MaterialFilter.PackagePaths.Add(TEXT("/Game"));

	AssetRegistry.GetAssets(MaterialFilter, MaterialAssets);

	if (MaterialAssets.Num() > 0)
	{
		AllItems.Add(FContextItem::Category(TEXT("Materials")));
		for (const FAssetData& Asset : MaterialAssets)
		{
			AllItems.Add(FContextItem(Asset.AssetName.ToString(), Asset.GetObjectPathString(), EContextItemType::Material));
		}
	}
}

void SNeoStackContextPopup::SetFilter(const FString& FilterText)
{
	CurrentFilter = FilterText;
	ApplyFilter();
	UpdateListViewItems();

	// Reset selection
	SelectedIndex = 0;
	if (FilteredItems.Num() > 0 && FilteredItems[0].bIsCategory && FilteredItems.Num() > 1)
	{
		SelectedIndex = 1; // Skip category header
	}
}

void SNeoStackContextPopup::ApplyFilter()
{
	FilteredItems.Empty();

	if (CurrentFilter.IsEmpty())
	{
		// Show all items (up to a limit)
		int32 Count = 0;
		for (const FContextItem& Item : AllItems)
		{
			FilteredItems.Add(Item);
			if (!Item.bIsCategory)
			{
				Count++;
				if (Count >= 50) break; // Limit initial display
			}
		}
	}
	else
	{
		// Filter by name (case insensitive fuzzy match)
		FString LowerFilter = CurrentFilter.ToLower();

		EContextItemType LastCategory = EContextItemType::Category;
		bool bHasItemsInCategory = false;
		int32 CategoryIndex = -1;

		for (int32 i = 0; i < AllItems.Num(); i++)
		{
			const FContextItem& Item = AllItems[i];

			if (Item.bIsCategory)
			{
				// Remember category, add later if we find matching items
				LastCategory = Item.Type;
				CategoryIndex = FilteredItems.Num();
				bHasItemsInCategory = false;
				continue;
			}

			// Check if item matches filter
			FString LowerName = Item.DisplayName.ToLower();
			FString LowerPath = Item.FullPath.ToLower();

			if (LowerName.Contains(LowerFilter) || LowerPath.Contains(LowerFilter))
			{
				// Add category header if this is first match in category
				if (!bHasItemsInCategory && CategoryIndex >= 0)
				{
					// Find and insert the category
					for (const FContextItem& CatItem : AllItems)
					{
						if (CatItem.bIsCategory && CatItem.Type == LastCategory)
						{
							FilteredItems.Insert(CatItem, CategoryIndex);
							break;
						}
					}
					bHasItemsInCategory = true;
				}

				FilteredItems.Add(Item);

				if (FilteredItems.Num() >= 30) break; // Limit results
			}
		}
	}
}

void SNeoStackContextPopup::UpdateListViewItems()
{
	ListViewItems.Empty();

	for (FContextItem& Item : FilteredItems)
	{
		ListViewItems.Add(MakeShared<FContextItem>(Item));
	}

	if (ItemListView.IsValid())
	{
		ItemListView->RequestListRefresh();
	}
}

void SNeoStackContextPopup::SelectPrevious()
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectPrevious - FilteredItems: %d, CurrentIndex: %d"), FilteredItems.Num(), SelectedIndex);
	if (FilteredItems.Num() == 0) return;

	do
	{
		SelectedIndex--;
		if (SelectedIndex < 0)
		{
			SelectedIndex = FilteredItems.Num() - 1;
		}
	} while (FilteredItems[SelectedIndex].bIsCategory && FilteredItems.Num() > 1);

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectPrevious - NewIndex: %d, ListViewValid: %d"), SelectedIndex, ItemListView.IsValid() ? 1 : 0);

	// Update selection in list view
	if (ItemListView.IsValid() && ListViewItems.IsValidIndex(SelectedIndex))
	{
		ItemListView->SetSelection(ListViewItems[SelectedIndex]);
		ItemListView->RequestScrollIntoView(ListViewItems[SelectedIndex]);
	}
}

void SNeoStackContextPopup::SelectNext()
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectNext - FilteredItems: %d, CurrentIndex: %d"), FilteredItems.Num(), SelectedIndex);
	if (FilteredItems.Num() == 0) return;

	do
	{
		SelectedIndex++;
		if (SelectedIndex >= FilteredItems.Num())
		{
			SelectedIndex = 0;
		}
	} while (FilteredItems[SelectedIndex].bIsCategory && FilteredItems.Num() > 1);

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectNext - NewIndex: %d, ListViewValid: %d"), SelectedIndex, ItemListView.IsValid() ? 1 : 0);

	// Update selection in list view
	if (ItemListView.IsValid() && ListViewItems.IsValidIndex(SelectedIndex))
	{
		ItemListView->SetSelection(ListViewItems[SelectedIndex]);
		ItemListView->RequestScrollIntoView(ListViewItems[SelectedIndex]);
	}
}

void SNeoStackContextPopup::ConfirmSelection()
{
	if (FilteredItems.IsValidIndex(SelectedIndex) && !FilteredItems[SelectedIndex].bIsCategory)
	{
		OnItemSelectedDelegate.ExecuteIfBound(FilteredItems[SelectedIndex]);
	}
}

void SNeoStackContextPopup::OnItemClicked(TSharedPtr<FContextItem> Item)
{
	if (Item.IsValid() && !Item->bIsCategory)
	{
		OnItemSelectedDelegate.ExecuteIfBound(*Item);
	}
}

TSharedRef<ITableRow> SNeoStackContextPopup::GenerateItemRow(TSharedPtr<FContextItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Item->bIsCategory)
	{
		// Category header row
		return SNew(STableRow<TSharedPtr<FContextItem>>, OwnerTable)
			.Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
			.Padding(FMargin(8.0f, 6.0f, 8.0f, 2.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
			];
	}

	// Regular item row with visible selection
	return SNew(STableRow<TSharedPtr<FContextItem>>, OwnerTable)
		.Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		.Padding(FMargin(8.0f, 4.0f))
		.ShowSelection(true)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(0)
			.ColorAndOpacity_Lambda([this, Item]()
			{
				// Check if this item is selected
				if (ListViewItems.IsValidIndex(SelectedIndex) && ListViewItems[SelectedIndex].Get() == Item.Get())
				{
					return FLinearColor(0.2f, 0.4f, 0.8f, 1.0f); // Blue tint when selected
				}
				return FLinearColor::White;
			})
			[
				SNew(SHorizontalBox)
			// Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->Type == EContextItemType::CppHeader ? TEXT("H") :
						Item->Type == EContextItemType::CppSource ? TEXT("C") :
						Item->Type == EContextItemType::Blueprint ? TEXT("BP") :
						Item->Type == EContextItemType::Widget ? TEXT("W") :
						Item->Type == EContextItemType::Material ? TEXT("M") : TEXT("?")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(GetColorForType(Item->Type))
					.Justification(ETextJustify::Center)
				]
			]
			// Name and path
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->DisplayName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->FullPath))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
				]
			]
		]
	]; // Close SBorder and STableRow
}

const FSlateBrush* SNeoStackContextPopup::GetIconForType(EContextItemType Type) const
{
	// For now return nullptr, could add custom icons later
	return nullptr;
}

FSlateColor SNeoStackContextPopup::GetColorForType(EContextItemType Type) const
{
	switch (Type)
	{
	case EContextItemType::CppHeader:
		return FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)); // Blue
	case EContextItemType::CppSource:
		return FSlateColor(FLinearColor(0.4f, 0.9f, 0.4f)); // Green
	case EContextItemType::Blueprint:
		return FSlateColor(FLinearColor(0.3f, 0.5f, 1.0f)); // Dark blue
	case EContextItemType::Widget:
		return FSlateColor(FLinearColor(0.9f, 0.6f, 0.2f)); // Orange
	case EContextItemType::Material:
		return FSlateColor(FLinearColor(0.8f, 0.3f, 0.8f)); // Purple
	default:
		return FSlateColor(FLinearColor::White);
	}
}

#undef LOCTEXT_NAMESPACE
