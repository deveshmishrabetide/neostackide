// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

// Forward declarations
struct FOpenRouterModelInfo;
class SWindow;

/** Structure to hold agent information */
struct FAgentInfo
{
	FString DisplayName; // Display name shown in UI
	FString AgentID;     // API identifier (lowercase)
	FName IconStyleName;

	FAgentInfo(const FString& InDisplayName, const FString& InAgentID, const FName& InIconStyleName)
		: DisplayName(InDisplayName), AgentID(InAgentID), IconStyleName(InIconStyleName)
	{}
};

/** Structure to hold model information */
struct FModelInfo
{
	FString Name;
	FString ModelID;    // API model identifier (e.g., "x-ai/grok-4.1-fast")
	FString Provider;
	FString Description;
	FString InputCost;  // Cost per million input tokens
	FString OutputCost; // Cost per million output tokens
	bool bHasVariablePricing; // True if provider pricing can vary (e.g., OpenRouter with multiple providers)

	FModelInfo(const FString& InName, const FString& InModelID, const FString& InProvider, const FString& InDescription,
	           const FString& InInputCost, const FString& InOutputCost, bool bInHasVariablePricing = false)
		: Name(InName), ModelID(InModelID), Provider(InProvider), Description(InDescription),
		  InputCost(InInputCost), OutputCost(InOutputCost), bHasVariablePricing(bInHasVariablePricing)
	{}
};

/** Delegate for conversation selection */
DECLARE_DELEGATE_OneParam(FOnConversationSelected, int32 /* ConversationID */);

/**
 * Sidebar widget for the NeoStack plugin
 */
class SNeoStackSidebar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackSidebar)
		{}
		SLATE_EVENT(FSimpleDelegate, OnSettingsClicked)
		SLATE_EVENT(FSimpleDelegate, OnNewChat)
		SLATE_EVENT(FOnConversationSelected, OnConversationSelected)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Get currently selected agent */
	TSharedPtr<FAgentInfo> GetSelectedAgent() const { return SelectedAgent; }

	/** Get currently selected model */
	TSharedPtr<FModelInfo> GetSelectedModel() const { return SelectedModel; }

	/** Refresh the conversations list from disk */
	void RefreshConversationsList();

private:
	/** Agent selection dropdown options */
	TArray<TSharedPtr<FAgentInfo>> AgentOptions;
	TSharedPtr<FAgentInfo> SelectedAgent;

	/** Model selection dropdown options */
	TArray<TSharedPtr<FModelInfo>> ModelOptions;
	TSharedPtr<FModelInfo> SelectedModel;

	/** Generates dropdown text for agent */
	FText GetAgentSelectionText() const;

	/** Generates dropdown text for model */
	FText GetModelSelectionText() const;

	/** Called when agent selection changes */
	void OnAgentSelected(TSharedPtr<FAgentInfo> NewSelection, ESelectInfo::Type SelectInfo);

	/** Called when model selection changes */
	void OnModelSelected(TSharedPtr<FModelInfo> NewSelection, ESelectInfo::Type SelectInfo);

	/** Save/Load selection state */
	void SaveSelections();
	void LoadSelections();

	/** Load user-added models from settings */
	void LoadUserModels();

	/** Save user-added models to settings */
	void SaveUserModels();

	/** Called when new chat button is clicked */
	FReply OnNewChatClicked();

	/** Called when delete conversation button is clicked */
	FReply OnDeleteConversation(int32 ConversationID);

	/** Called when settings button is clicked */
	FReply OnSettingsButtonClicked();

	/** Called when browse models button is clicked */
	FReply OnBrowseModelsClicked();

	/** Called when a model is selected from the browser */
	void OnModelBrowserSelected(TSharedPtr<FOpenRouterModelInfo> SelectedModel);

	/** Called when the model browser is closed */
	void OnModelBrowserClosed();

	/** Add a model to the favorites list */
	void AddModelToFavorites(const FString& Name, const FString& ModelID, const FString& Provider,
	                         const FString& Description, const FString& InputCost, const FString& OutputCost);

	/** Settings clicked delegate */
	FSimpleDelegate OnSettingsClickedDelegate;

	/** New chat delegate */
	FSimpleDelegate OnNewChatDelegate;

	/** Conversation selected delegate */
	FOnConversationSelected OnConversationSelectedDelegate;

	/** Conversation list (from FConversationMetadata) */
	TArray<TSharedPtr<struct FConversationMetadata>> Conversations;

	/** ListView for conversations */
	TSharedPtr<SListView<TSharedPtr<struct FConversationMetadata>>> ConversationListView;

	/** Model browser window */
	TWeakPtr<SWindow> ModelBrowserWindow;

	/** Model ComboBox reference for refreshing */
	TSharedPtr<SComboBox<TSharedPtr<FModelInfo>>> ModelComboBox;

	/** Generates a row widget for a conversation */
	TSharedRef<ITableRow> OnGenerateConversationRow(TSharedPtr<struct FConversationMetadata> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Called when a conversation is clicked */
	void OnConversationClicked(TSharedPtr<struct FConversationMetadata> Item);
};
