// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Context item type
 */
enum class EContextItemType : uint8
{
	CppHeader,      // .h files
	CppSource,      // .cpp files
	Blueprint,      // Blueprint assets
	Widget,         // Widget blueprints
	Material,       // Material assets
	Texture,        // Texture assets
	Level,          // Level/Map assets
	DataAsset,      // Data assets
	Category        // Category header (not selectable)
};

/**
 * A single context item (file/asset)
 */
struct FContextItem
{
	FString DisplayName;    // Short name shown in list
	FString FullPath;       // Full path for insertion
	EContextItemType Type;
	bool bIsCategory = false;

	FContextItem() : Type(EContextItemType::CppSource) {}

	FContextItem(const FString& InDisplayName, const FString& InFullPath, EContextItemType InType)
		: DisplayName(InDisplayName)
		, FullPath(InFullPath)
		, Type(InType)
		, bIsCategory(false)
	{}

	static FContextItem Category(const FString& Name)
	{
		FContextItem Item;
		Item.DisplayName = Name;
		Item.Type = EContextItemType::Category;
		Item.bIsCategory = true;
		return Item;
	}
};

/** Delegate called when a context item is selected */
DECLARE_DELEGATE_OneParam(FOnContextItemSelected, const FContextItem&);

/**
 * Context popup widget that shows available files/assets for @ mentions
 */
class SNeoStackContextPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackContextPopup)
		{}
		SLATE_EVENT(FOnContextItemSelected, OnItemSelected)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Update the filter text and refresh the list */
	void SetFilter(const FString& FilterText);

	/** Get the currently selected index */
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** Move selection up */
	void SelectPrevious();

	/** Move selection down */
	void SelectNext();

	/** Confirm current selection */
	void ConfirmSelection();

	/** Check if popup has any visible items */
	bool HasItems() const { return FilteredItems.Num() > 0; }

private:
	/** All available context items */
	TArray<FContextItem> AllItems;

	/** Filtered items based on current search */
	TArray<FContextItem> FilteredItems;

	/** Currently selected index */
	int32 SelectedIndex = 0;

	/** Current filter text */
	FString CurrentFilter;

	/** List view widget */
	TSharedPtr<class SListView<TSharedPtr<FContextItem>>> ItemListView;

	/** Items for list view (shared ptrs) */
	TArray<TSharedPtr<FContextItem>> ListViewItems;

	/** Callback for item selection */
	FOnContextItemSelected OnItemSelectedDelegate;

	/** Scan project for files and assets */
	void ScanProjectFiles();

	/** Scan directory for C++ files */
	void ScanCppDirectory(const FString& Directory);

	/** Scan for Blueprint assets */
	void ScanBlueprintAssets();

	/** Apply filter to items */
	void ApplyFilter();

	/** Update the list view items */
	void UpdateListViewItems();

	/** Generate row for list view */
	TSharedRef<ITableRow> GenerateItemRow(TSharedPtr<FContextItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Handle item click */
	void OnItemClicked(TSharedPtr<FContextItem> Item);

	/** Get icon for item type */
	const FSlateBrush* GetIconForType(EContextItemType Type) const;

	/** Get color for item type */
	FSlateColor GetColorForType(EContextItemType Type) const;
};
