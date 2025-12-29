// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Http.h"

// Forward declaration
struct FModelInfo;

/** Structure to hold OpenRouter model data */
struct FOpenRouterModelInfo
{
	FString ID;
	FString Name;
	FString Description;
	int32 ContextLength;
	FString PromptCost;      // Cost per token as string
	FString CompletionCost;  // Cost per token as string
	FString Provider;        // Extracted from ID (e.g., "anthropic" from "anthropic/claude-3")

	FOpenRouterModelInfo()
		: ContextLength(0)
	{}
};

/** Delegate for when a model is selected */
DECLARE_DELEGATE_OneParam(FOnModelBrowserSelected, TSharedPtr<FOpenRouterModelInfo>);

/**
 * Modal browser widget for browsing and selecting OpenRouter models
 */
class SNeoStackModelBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackModelBrowser)
		{}
		SLATE_EVENT(FOnModelBrowserSelected, OnModelSelected)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Fetch models from backend */
	void FetchModels();

private:
	/** All models from API */
	TArray<TSharedPtr<FOpenRouterModelInfo>> AllModels;

	/** Filtered models based on search */
	TArray<TSharedPtr<FOpenRouterModelInfo>> FilteredModels;

	/** Current search text */
	FString SearchText;

	/** Whether we're currently loading */
	bool bIsLoading;

	/** Error message if any */
	FString ErrorMessage;

	/** The list view widget */
	TSharedPtr<SListView<TSharedPtr<FOpenRouterModelInfo>>> ModelListView;

	/** Callbacks */
	FOnModelBrowserSelected OnModelSelectedDelegate;
	FSimpleDelegate OnClosedDelegate;

	/** Filter models based on search text */
	void FilterModels();

	/** Called when search text changes */
	void OnSearchTextChanged(const FText& NewText);

	/** Generate row for model list */
	TSharedRef<ITableRow> OnGenerateModelRow(TSharedPtr<FOpenRouterModelInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Called when a model is double-clicked */
	void OnModelDoubleClicked(TSharedPtr<FOpenRouterModelInfo> Item);

	/** Called when select button is clicked */
	FReply OnSelectClicked();

	/** Called when cancel button is clicked */
	FReply OnCancelClicked();

	/** Get currently selected model */
	TSharedPtr<FOpenRouterModelInfo> GetSelectedModel() const;

	/** HTTP request callbacks */
	void OnModelsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	/** Format cost for display (converts per-token to per-million) */
	static FString FormatCost(const FString& PerTokenCost);

	/** Extract provider from model ID */
	static FString ExtractProvider(const FString& ModelID);
};
