// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackModelBrowser.h"
#include "NeoStackSettings.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "JsonUtilities.h"

#define LOCTEXT_NAMESPACE "SNeoStackModelBrowser"

void SNeoStackModelBrowser::Construct(const FArguments& InArgs)
{
	OnModelSelectedDelegate = InArgs._OnModelSelected;
	OnClosedDelegate = InArgs._OnClosed;
	bIsLoading = false;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1a1a1a")))))
		.Padding(0.0f)
		[
			SNew(SBox)
			.WidthOverride(700.0f)
			.HeightOverride(500.0f)
			[
				SNew(SVerticalBox)
				// Header
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(15.0f, 15.0f, 15.0f, 10.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BrowseModelsTitle", "Browse OpenRouter Models"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(FLinearColor::White)
				]
				// Search box
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(15.0f, 0.0f, 15.0f, 10.0f)
				[
					SNew(SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Search models by name, provider, or description..."))
					.OnTextChanged(this, &SNeoStackModelBrowser::OnSearchTextChanged)
				]
				// Model list
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(15.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#252525")))))
					.Padding(2.0f)
					[
						SAssignNew(ModelListView, SListView<TSharedPtr<FOpenRouterModelInfo>>)
						.ListItemsSource(&FilteredModels)
						.OnGenerateRow(this, &SNeoStackModelBrowser::OnGenerateModelRow)
						.OnMouseButtonDoubleClick(this, &SNeoStackModelBrowser::OnModelDoubleClicked)
						.SelectionMode(ESelectionMode::Single)
					]
				]
				// Status / Loading indicator
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(15.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText {
						if (bIsLoading)
						{
							return LOCTEXT("Loading", "Loading models...");
						}
						if (!ErrorMessage.IsEmpty())
						{
							return FText::FromString(ErrorMessage);
						}
						return FText::Format(LOCTEXT("ModelCount", "{0} models available"), FText::AsNumber(FilteredModels.Num()));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					.ColorAndOpacity_Lambda([this]() -> FSlateColor {
						if (!ErrorMessage.IsEmpty())
						{
							return FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);
						}
						return FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
					})
				]
				// Buttons
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(15.0f, 10.0f, 15.0f, 15.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SButton)
						.OnClicked(this, &SNeoStackModelBrowser::OnCancelClicked)
						.ContentPadding(FMargin(20.0f, 8.0f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Cancel", "Cancel"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.OnClicked(this, &SNeoStackModelBrowser::OnSelectClicked)
						.IsEnabled_Lambda([this]() { return GetSelectedModel().IsValid(); })
						.ContentPadding(FMargin(20.0f, 8.0f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddModel", "Add to Favorites"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						]
					]
				]
			]
		]
	];

	// Start fetching models
	FetchModels();
}

void SNeoStackModelBrowser::FetchModels()
{
	bIsLoading = true;
	ErrorMessage.Empty();

	const UNeoStackSettings* Settings = UNeoStackSettings::Get();
	if (!Settings)
	{
		ErrorMessage = TEXT("Failed to get NeoStack settings");
		bIsLoading = false;
		return;
	}

	if (Settings->BackendURL.IsEmpty())
	{
		ErrorMessage = TEXT("Backend URL not configured");
		bIsLoading = false;
		return;
	}

	if (Settings->APIKey.IsEmpty())
	{
		ErrorMessage = TEXT("API Key not configured");
		bIsLoading = false;
		return;
	}

	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = HttpModule.CreateRequest();

	FString URL = Settings->BackendURL + TEXT("/models");
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("X-API-Key"), Settings->APIKey);

	Request->OnProcessRequestComplete().BindRaw(this, &SNeoStackModelBrowser::OnModelsResponseReceived);

	if (!Request->ProcessRequest())
	{
		ErrorMessage = TEXT("Failed to send HTTP request");
		bIsLoading = false;
	}
}

void SNeoStackModelBrowser::OnModelsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	bIsLoading = false;

	if (!bWasSuccessful || !Response.IsValid())
	{
		ErrorMessage = TEXT("Request failed or invalid response");
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode != 200)
	{
		ErrorMessage = FString::Printf(TEXT("Server error: %d"), ResponseCode);
		return;
	}

	FString Content = Response->GetContentAsString();

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		ErrorMessage = TEXT("Failed to parse response");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* DataArray;
	if (!JsonObject->TryGetArrayField(TEXT("data"), DataArray))
	{
		ErrorMessage = TEXT("Invalid response format");
		return;
	}

	AllModels.Empty();

	for (const TSharedPtr<FJsonValue>& Value : *DataArray)
	{
		const TSharedPtr<FJsonObject>* ModelObj;
		if (!Value->TryGetObject(ModelObj))
		{
			continue;
		}

		TSharedPtr<FOpenRouterModelInfo> Model = MakeShared<FOpenRouterModelInfo>();

		(*ModelObj)->TryGetStringField(TEXT("id"), Model->ID);
		(*ModelObj)->TryGetStringField(TEXT("name"), Model->Name);
		(*ModelObj)->TryGetStringField(TEXT("description"), Model->Description);
		(*ModelObj)->TryGetNumberField(TEXT("context_length"), Model->ContextLength);

		// Strip provider prefix from name (e.g., "Anthropic: Claude Opus 4.5" -> "Claude Opus 4.5")
		int32 ColonIndex;
		if (Model->Name.FindChar(TEXT(':'), ColonIndex))
		{
			Model->Name = Model->Name.RightChop(ColonIndex + 1).TrimStart();
		}

		// Parse pricing
		const TSharedPtr<FJsonObject>* PricingObj;
		if ((*ModelObj)->TryGetObjectField(TEXT("pricing"), PricingObj))
		{
			(*PricingObj)->TryGetStringField(TEXT("prompt"), Model->PromptCost);
			(*PricingObj)->TryGetStringField(TEXT("completion"), Model->CompletionCost);
		}

		Model->Provider = ExtractProvider(Model->ID);

		AllModels.Add(Model);
	}

	FilterModels();

	if (ModelListView.IsValid())
	{
		ModelListView->RequestListRefresh();
	}
}

void SNeoStackModelBrowser::FilterModels()
{
	FilteredModels.Empty();

	FString SearchLower = SearchText.ToLower();

	for (const TSharedPtr<FOpenRouterModelInfo>& Model : AllModels)
	{
		if (SearchText.IsEmpty() ||
			Model->Name.ToLower().Contains(SearchLower) ||
			Model->ID.ToLower().Contains(SearchLower) ||
			Model->Provider.ToLower().Contains(SearchLower) ||
			Model->Description.ToLower().Contains(SearchLower))
		{
			FilteredModels.Add(Model);
		}
	}
}

void SNeoStackModelBrowser::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	FilterModels();

	if (ModelListView.IsValid())
	{
		ModelListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SNeoStackModelBrowser::OnGenerateModelRow(TSharedPtr<FOpenRouterModelInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString PricingText = FString::Printf(
		TEXT("In: %s/M | Out: %s/M"),
		*FormatCost(Item->PromptCost),
		*FormatCost(Item->CompletionCost)
	);

	FString ContextText = FString::Printf(TEXT("%dK context"), Item->ContextLength / 1000);

	// Truncate description
	FString ShortDesc = Item->Description;
	if (ShortDesc.Len() > 120)
	{
		ShortDesc = ShortDesc.Left(117) + TEXT("...");
	}

	// Build OpenRouter URL from model ID
	FString OpenRouterURL = FString::Printf(TEXT("https://openrouter.ai/%s"), *Item->ID);

	return SNew(STableRow<TSharedPtr<FOpenRouterModelInfo>>, OwnerTable)
		.Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SBox)
			.HeightOverride(58.0f)  // Fixed height to prevent expansion
			[
				SNew(SHorizontalBox)
				// Main content
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					// Model name and provider
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Item->Name))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(10.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Item->Provider))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FLinearColor(0.4f, 0.7f, 1.0f, 1.0f))
						]
					]
					// Pricing and context
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(FText::FromString(PricingText))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FLinearColor(0.6f, 0.8f, 0.6f, 1.0f))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(15.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(ContextText))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
						]
					]
					// Description - single line, clipped
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(ShortDesc))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
				]
				// Open in browser button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ToolTipText(FText::FromString(TEXT("View on OpenRouter")))
					.ContentPadding(FMargin(4.0f))
					.OnClicked_Lambda([OpenRouterURL]()
					{
						FPlatformProcess::LaunchURL(*OpenRouterURL, nullptr, nullptr);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u2197")))  // ↗ arrow symbol
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						.ColorAndOpacity(FLinearColor(0.5f, 0.7f, 1.0f, 1.0f))
					]
				]
			]
		];
}

void SNeoStackModelBrowser::OnModelDoubleClicked(TSharedPtr<FOpenRouterModelInfo> Item)
{
	if (Item.IsValid())
	{
		OnModelSelectedDelegate.ExecuteIfBound(Item);
		OnClosedDelegate.ExecuteIfBound();
	}
}

FReply SNeoStackModelBrowser::OnSelectClicked()
{
	TSharedPtr<FOpenRouterModelInfo> Selected = GetSelectedModel();
	if (Selected.IsValid())
	{
		OnModelSelectedDelegate.ExecuteIfBound(Selected);
		OnClosedDelegate.ExecuteIfBound();
	}
	return FReply::Handled();
}

FReply SNeoStackModelBrowser::OnCancelClicked()
{
	OnClosedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

TSharedPtr<FOpenRouterModelInfo> SNeoStackModelBrowser::GetSelectedModel() const
{
	if (ModelListView.IsValid())
	{
		TArray<TSharedPtr<FOpenRouterModelInfo>> SelectedItems = ModelListView->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			return SelectedItems[0];
		}
	}
	return nullptr;
}

FString SNeoStackModelBrowser::FormatCost(const FString& PerTokenCost)
{
	// Convert per-token cost to per-million tokens
	// Input is like "0.000001" (cost per token)
	// Output should be like "$1.00" (cost per million tokens)

	if (PerTokenCost.IsEmpty() || PerTokenCost == TEXT("0"))
	{
		return TEXT("Free");
	}

	double CostPerToken = FCString::Atod(*PerTokenCost);
	double CostPerMillion = CostPerToken * 1000000.0;

	if (CostPerMillion < 0.01)
	{
		return FString::Printf(TEXT("$%.4f"), CostPerMillion);
	}
	else if (CostPerMillion < 1.0)
	{
		return FString::Printf(TEXT("$%.2f"), CostPerMillion);
	}
	else
	{
		return FString::Printf(TEXT("$%.1f"), CostPerMillion);
	}
}

FString SNeoStackModelBrowser::ExtractProvider(const FString& ModelID)
{
	// Extract provider from model ID like "anthropic/claude-3" -> "Anthropic"
	int32 SlashIndex;
	if (ModelID.FindChar(TEXT('/'), SlashIndex))
	{
		FString Provider = ModelID.Left(SlashIndex);
		// Capitalize first letter
		if (Provider.Len() > 0)
		{
			Provider[0] = FChar::ToUpper(Provider[0]);
		}
		return Provider;
	}
	return TEXT("Unknown");
}

#undef LOCTEXT_NAMESPACE
