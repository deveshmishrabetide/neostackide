// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

/** Structure to hold provider/endpoint information */
struct FProviderEndpoint
{
	FString Name;           // Endpoint name/display name
	FString ProviderName;   // Provider identifier (e.g., "Anthropic", "Google")
	int32 ContextLength;
	FString InputCost;      // Cost per million input tokens
	FString OutputCost;     // Cost per million output tokens
	FString Status;         // "online", "offline", etc.
	FString Quantization;   // Quantization level if any
	FString Variant;        // Variant (e.g., "nitro", "self-moderated")
	TArray<FString> SupportedParameters;
	bool bIsAuto = false;   // True for the "Auto" option

	FProviderEndpoint()
		: ContextLength(0)
		, bIsAuto(false)
	{}
};

/** Provider routing preferences (matches OpenRouter's provider routing) */
struct FProviderRouting
{
	FString SelectedProvider;  // Empty or "Auto" means let OpenRouter choose, otherwise specific provider
	FString SortBy;            // "default", "price", or "throughput"
	bool bAllowFallbacks = true;

	FProviderRouting()
		: SortBy(TEXT("default"))
		, bAllowFallbacks(true)
	{}
};

/**
 * Settings panel for NeoStack
 * Note: Settings are general and will be applied where supported by the model
 */
class SNeoStackSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackSettingsPanel)
		{}
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Get current settings values
	float GetMaxCostPerQuery() const { return MaxCostPerQuery; }
	int32 GetMaxTokens() const { return MaxTokens; }
	bool GetEnableThinking() const { return bEnableThinking; }
	int32 GetMaxThinkingTokens() const { return MaxThinkingTokens; }
	FString GetReasoningEffort() const { return ReasoningEffort; }
	FProviderRouting GetProviderRouting() const { return ProviderRoutingSettings; }

	// Save/Load settings
	void SaveSettings();
	void LoadSettings();
	static FString GetSettingsFilePath();

	// Provider routing (static for access from other classes)
	static FProviderRouting GetProviderRoutingForModel(const FString& ModelID);
	static void SetProviderRoutingForModel(const FString& ModelID, const FProviderRouting& Routing);

private:
	// Settings values
	float MaxCostPerQuery = 0.0f;
	int32 MaxTokens = 0;
	bool bEnableThinking = false;
	int32 MaxThinkingTokens = 2000;
	FString ReasoningEffort = TEXT("medium");
	FProviderRouting ProviderRoutingSettings;

	// Effort options (must be member variable for ComboBox)
	TArray<TSharedPtr<FString>> EffortOptions;

	// Sort by options
	TArray<TSharedPtr<FString>> SortByOptions;

	// Provider options
	TArray<TSharedPtr<FProviderEndpoint>> ProviderOptions;
	TArray<TSharedPtr<FProviderEndpoint>> FilteredProviderOptions;  // For search filtering
	TSharedPtr<FProviderEndpoint> CurrentProvider;
	TSharedPtr<SComboBox<TSharedPtr<FProviderEndpoint>>> ProviderComboBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SortByComboBox;
	TSharedPtr<class SEditableTextBox> ProviderSearchBox;
	TSharedPtr<class STextBlock> ProviderInfoText;
	TSharedPtr<class SVerticalBox> ProviderSection;
	bool bLoadingProviders = false;
	FString CurrentModelID;  // Track which model's providers we're showing
	FString ProviderSearchText;  // Current search filter

	// Close callback
	FSimpleDelegate OnCloseDelegate;

	// UI callbacks
	FReply OnCloseClicked();
	void OnMaxCostChanged(float NewValue);
	void OnMaxTokensChanged(int32 NewValue);
	void OnEnableThinkingChanged(ECheckBoxState NewState);
	void OnMaxThinkingTokensChanged(int32 NewValue);
	void OnReasoningEffortChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnProviderSelected(TSharedPtr<FProviderEndpoint> NewSelection, ESelectInfo::Type SelectInfo);
	void OnSortByChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnProviderSearchTextChanged(const FText& NewText);

	// Provider loading
	void LoadProvidersForCurrentModel();
	void OnProvidersLoaded(const TArray<FProviderEndpoint>& Endpoints);
	void FilterProviderOptions();

	// Helper to create a setting row
	TSharedRef<SWidget> CreateSettingRow(const FText& Label, const TSharedRef<SWidget>& ValueWidget);

	// Generate provider display widget
	TSharedRef<SWidget> GenerateProviderWidget(TSharedPtr<FProviderEndpoint> Item);

	// Format cost as per-million tokens (e.g., "$1.50")
	static FString FormatCostPerMillion(const FString& PerTokenCost);
};
