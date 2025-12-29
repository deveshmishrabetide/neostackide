// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackSidebar.h"
#include "UI/SNeoStackModelBrowser.h"
#include "NeoStackConversation.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateColor.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "NeoStackStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "SNeoStackSidebar"

void SNeoStackSidebar::Construct(const FArguments& InArgs)
{
	OnSettingsClickedDelegate = InArgs._OnSettingsClicked;
	OnNewChatDelegate = InArgs._OnNewChat;
	OnConversationSelectedDelegate = InArgs._OnConversationSelected;

	// Initialize Agent options with icons from style system
	AgentOptions.Add(MakeShareable(new FAgentInfo(
		TEXT("Orchestrator"),
		TEXT("orchestrator"),
		FName("NeoStack.Agent.Orchestrator")
	)));
	AgentOptions.Add(MakeShareable(new FAgentInfo(
		TEXT("Blueprint Agent"),
		TEXT("blueprint"),
		FName("NeoStack.Agent.BlueprintAgent")
	)));
	AgentOptions.Add(MakeShareable(new FAgentInfo(
		TEXT("Material Agent"),
		TEXT("material"),
		FName("NeoStack.Agent.MaterialAgent")
	)));
	AgentOptions.Add(MakeShareable(new FAgentInfo(
		TEXT("Widget Agent"),
		TEXT("widget"),
		FName("NeoStack.Agent.WidgetAgent")
	)));
	SelectedAgent = AgentOptions[0]; // Default to Orchestrator

	// Initialize Model options with detailed information
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("Claude Haiku 4.5"),
		TEXT("anthropic/claude-haiku-4.5"),
		TEXT("Anthropic"),
		TEXT("Fast, affordable AI model"),
		TEXT("$0.25"),
		TEXT("$1.25"),
		false
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("Claude Sonnet 4.5"),
		TEXT("anthropic/claude-sonnet-4.5"),
		TEXT("Anthropic"),
		TEXT("Balanced performance and speed"),
		TEXT("$3"),
		TEXT("$15"),
		false
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("GPT-5.1"),
		TEXT("openai/gpt-5.1"),
		TEXT("OpenAI"),
		TEXT("Advanced reasoning capabilities"),
		TEXT("$5"),
		TEXT("$15"),
		false
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("GPT-5.1 Mini"),
		TEXT("openai/gpt-5.1-mini"),
		TEXT("OpenAI"),
		TEXT("Lightweight and efficient"),
		TEXT("$0.15"),
		TEXT("$0.60"),
		false
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("Grok 4.1 Fast"),
		TEXT("x-ai/grok-4.1-fast"),
		TEXT("xAI"),
		TEXT("Fast reasoning model"),
		TEXT("$2"),
		TEXT("$10"),
		false
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("GLM 4.6 Exacto"),
		TEXT("z-ai/glm-4.6:exacto"),
		TEXT("OpenRouter"),
		TEXT("Multilingual support"),
		TEXT("$0.50"),
		TEXT("$1.50"),
		true // Variable pricing through OpenRouter
	)));
	ModelOptions.Add(MakeShareable(new FModelInfo(
		TEXT("Gemini 3 Pro"),
		TEXT("google/gemini-3-pro-preview"),
		TEXT("Google"),
		TEXT("Advanced multimodal AI"),
		TEXT("$1.25"),
		TEXT("$5"),
		false
	)));

	// Load user-added models from settings
	LoadUserModels();

	SelectedModel = ModelOptions[0]; // Default to Claude Haiku 4.5

	// Load saved selections (will override defaults if file exists)
	LoadSelections();

	// Load conversations from disk
	RefreshConversationsList();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#242424")))))
		.Padding(0.0f)
		[
			SNew(SBox)
			.WidthOverride(250.0f)
			.Padding(2.0f)
			[
				SNew(SScrollBox)
			// Agent Dropdown
			+ SScrollBox::Slot()
			.Padding(0.0f, 2.0f)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor::Transparent)
				.Padding(5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AgentLabel", "Agent:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SComboBox<TSharedPtr<FAgentInfo>>)
						.OptionsSource(&AgentOptions)
						.OnSelectionChanged(this, &SNeoStackSidebar::OnAgentSelected)
						.OnGenerateWidget_Lambda([](TSharedPtr<FAgentInfo> Item)
						{
							return SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.0f, 0.0f, 8.0f, 0.0f)
								[
									SNew(SImage)
									.Image(FNeoStackStyle::Get().GetBrush(Item->IconStyleName))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
								+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(FText::FromString(Item->DisplayName))
								];
						})
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								SNew(SImage)
								.Image_Lambda([this]() -> const FSlateBrush*
								{
									if (SelectedAgent.IsValid())
									{
										return FNeoStackStyle::Get().GetBrush(SelectedAgent->IconStyleName);
									}
									return FCoreStyle::Get().GetDefaultBrush();
								})
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
							]
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &SNeoStackSidebar::GetAgentSelectionText)
							]
						]
					]
				]
			]
			// Model Dropdown
			+ SScrollBox::Slot()
			.Padding(0.0f, 2.0f)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor::Transparent)
				.Padding(5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ModelLabel", "Model:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(ModelComboBox, SComboBox<TSharedPtr<FModelInfo>>)
						.OptionsSource(&ModelOptions)
						.OnSelectionChanged(this, &SNeoStackSidebar::OnModelSelected)
						.OnGenerateWidget_Lambda([](TSharedPtr<FModelInfo> Item)
						{
							// Build pricing text with variable pricing indicator
							FString PricingText = FString::Printf(
								TEXT("In: %s/M | Out: %s/M%s"),
								*Item->InputCost,
								*Item->OutputCost,
								Item->bHasVariablePricing ? TEXT(" *") : TEXT("")
							);

							// Truncate description to prevent layout issues
							FString ShortDesc = Item->Description;
							if (ShortDesc.Len() > 45)
							{
								ShortDesc = ShortDesc.Left(42) + TEXT("...");
							}

							return SNew(SBox)
								.HeightOverride(55.0f)  // Fixed height for consistent dropdown
								.WidthOverride(220.0f)  // Fixed width
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(5.0f, 5.0f, 5.0f, 2.0f)
									[
										SNew(STextBlock)
										.Text(FText::FromString(Item->Name))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(5.0f, 0.0f, 5.0f, 2.0f)
									[
										SNew(STextBlock)
										.Text(FText::FromString(FString::Printf(TEXT("%s | %s"), *Item->Provider, *PricingText)))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
										.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(5.0f, 0.0f, 5.0f, 5.0f)
									[
										SNew(STextBlock)
										.Text(FText::FromString(ShortDesc))
										.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
										.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
									]
								];
						})
						[
							SNew(STextBlock)
							.Text(this, &SNeoStackSidebar::GetModelSelectionText)
						]
					]
					// Browse models button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.OnClicked(this, &SNeoStackSidebar::OnBrowseModelsClicked)
						.ToolTipText(LOCTEXT("BrowseModelsTooltip", "Browse All Models"))
						.ContentPadding(FMargin(4.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("+")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
							.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
						]
					]
					// Settings button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.OnClicked(this, &SNeoStackSidebar::OnSettingsButtonClicked)
						.ToolTipText(LOCTEXT("SettingsTooltip", "Open Settings"))
						.ContentPadding(FMargin(4.0f))
						[
							SNew(SImage)
							.Image(FNeoStackStyle::Get().GetBrush("NeoStack.SettingsIcon"))
							.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
						]
					]
				]
			]
			// New Chat Button
			+ SScrollBox::Slot()
			.Padding(0.0f, 10.0f, 0.0f, 5.0f)
			[
				SNew(SButton)
				.OnClicked(this, &SNeoStackSidebar::OnNewChatClicked)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(10.0f, 8.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NewChatButton", "+ New Chat"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
			// Conversations List
			+ SScrollBox::Slot()
			.Padding(0.0f, 5.0f)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor::Transparent)
				.Padding(5.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 5.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ConversationsLabel", "Conversations"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(ConversationListView, SListView<TSharedPtr<FConversationMetadata>>)
						.ListItemsSource(&Conversations)
						.OnGenerateRow(this, &SNeoStackSidebar::OnGenerateConversationRow)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
			]
		]
	];
}

TSharedRef<ITableRow> SNeoStackSidebar::OnGenerateConversationRow(TSharedPtr<FConversationMetadata> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Format timestamp for display
	FString TimeDisplay;
	FTimespan TimeSince = FDateTime::Now() - Item->UpdatedAt;
	if (TimeSince.GetTotalMinutes() < 1)
	{
		TimeDisplay = TEXT("Just now");
	}
	else if (TimeSince.GetTotalHours() < 1)
	{
		TimeDisplay = FString::Printf(TEXT("%d min ago"), FMath::FloorToInt(TimeSince.GetTotalMinutes()));
	}
	else if (TimeSince.GetTotalDays() < 1)
	{
		TimeDisplay = FString::Printf(TEXT("%d hr ago"), FMath::FloorToInt(TimeSince.GetTotalHours()));
	}
	else if (TimeSince.GetTotalDays() < 7)
	{
		TimeDisplay = FString::Printf(TEXT("%d days ago"), FMath::FloorToInt(TimeSince.GetTotalDays()));
	}
	else
	{
		TimeDisplay = Item->UpdatedAt.ToString(TEXT("%m/%d/%Y"));
	}

	return SNew(STableRow<TSharedPtr<FConversationMetadata>>, OwnerTable)
		.Padding(FMargin(0.0f, 3.0f))
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, Item]()
			{
				OnConversationClicked(Item);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				// Conversation title and time
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(8.0f, 6.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item->Title))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TimeDisplay))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
				]
				// Delete button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(6.0f))
					.OnClicked_Lambda([this, Item]() { return OnDeleteConversation(Item->ID); })
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("×")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
				]
			]
		];
}

FText SNeoStackSidebar::GetAgentSelectionText() const
{
	if (SelectedAgent.IsValid())
	{
		return FText::FromString(SelectedAgent->DisplayName);
	}
	return LOCTEXT("SelectAgent", "Select Agent");
}

FText SNeoStackSidebar::GetModelSelectionText() const
{
	if (SelectedModel.IsValid())
	{
		return FText::FromString(SelectedModel->Name);
	}
	return LOCTEXT("SelectModel", "Select Model");
}

void SNeoStackSidebar::OnAgentSelected(TSharedPtr<FAgentInfo> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedAgent = NewSelection;
	SaveSelections();
}

void SNeoStackSidebar::OnModelSelected(TSharedPtr<FModelInfo> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedModel = NewSelection;
	SaveSelections();
}

FReply SNeoStackSidebar::OnNewChatClicked()
{
	// Clear current conversation in manager
	FNeoStackConversationManager::Get().ClearCurrentConversation();

	// Notify delegate
	OnNewChatDelegate.ExecuteIfBound();

	return FReply::Handled();
}

FReply SNeoStackSidebar::OnDeleteConversation(int32 ConversationID)
{
	// Delete from conversation manager
	FNeoStackConversationManager::Get().DeleteConversation(ConversationID);

	// Refresh the list
	RefreshConversationsList();

	return FReply::Handled();
}

void SNeoStackSidebar::RefreshConversationsList()
{
	// Get all conversations from manager
	TArray<FConversationMetadata> AllConversations = FNeoStackConversationManager::Get().GetAllConversations();

	// Clear and rebuild our shared pointer list
	Conversations.Empty();
	for (const FConversationMetadata& Meta : AllConversations)
	{
		Conversations.Add(MakeShared<FConversationMetadata>(Meta));
	}

	// Refresh the list view if it exists
	if (ConversationListView.IsValid())
	{
		ConversationListView->RequestListRefresh();
	}
}

void SNeoStackSidebar::OnConversationClicked(TSharedPtr<FConversationMetadata> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	// Set as current conversation
	FNeoStackConversationManager::Get().SetCurrentConversation(Item->ID);

	// Notify delegate
	OnConversationSelectedDelegate.ExecuteIfBound(Item->ID);
}

void SNeoStackSidebar::SaveSelections()
{
	FString FilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");

	// Ensure directory exists
	FString DirPath = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*DirPath, true);

	// Load existing settings or create new object
	TSharedPtr<FJsonObject> JsonObject;
	FString ExistingContent;
	if (FFileHelper::LoadFileToString(ExistingContent, *FilePath))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingContent);
		FJsonSerializer::Deserialize(Reader, JsonObject);
	}

	if (!JsonObject.IsValid())
	{
		JsonObject = MakeShared<FJsonObject>();
	}

	// Update selections
	if (SelectedAgent.IsValid())
	{
		JsonObject->SetStringField(TEXT("SelectedAgentID"), SelectedAgent->AgentID);
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Saving SelectedAgentID: %s"), *SelectedAgent->AgentID);
	}

	if (SelectedModel.IsValid())
	{
		JsonObject->SetStringField(TEXT("SelectedModelID"), SelectedModel->ModelID);
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Saving SelectedModelID: %s"), *SelectedModel->ModelID);
	}

	// Serialize and save
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Saved selections to: %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStack] Failed to save selections to: %s"), *FilePath);
	}
}

void SNeoStackSidebar::LoadSelections()
{
	FString FilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
	FString FileContent;

	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			// Load agent selection
			FString SavedAgentID;
			if (JsonObject->TryGetStringField(TEXT("SelectedAgentID"), SavedAgentID))
			{
				for (const TSharedPtr<FAgentInfo>& Agent : AgentOptions)
				{
					if (Agent->AgentID == SavedAgentID)
					{
						SelectedAgent = Agent;
						break;
					}
				}
			}

			// Load model selection
			FString SavedModelID;
			if (JsonObject->TryGetStringField(TEXT("SelectedModelID"), SavedModelID))
			{
				for (const TSharedPtr<FModelInfo>& Model : ModelOptions)
				{
					if (Model->ModelID == SavedModelID)
					{
						SelectedModel = Model;
						break;
					}
				}
			}
		}
	}
	// If file doesn't exist or fields missing, keep defaults
}

FReply SNeoStackSidebar::OnSettingsButtonClicked()
{
	if (OnSettingsClickedDelegate.IsBound())
	{
		OnSettingsClickedDelegate.Execute();
	}
	return FReply::Handled();
}

FReply SNeoStackSidebar::OnBrowseModelsClicked()
{
	// Don't open multiple windows
	if (ModelBrowserWindow.IsValid())
	{
		TSharedPtr<SWindow> Window = ModelBrowserWindow.Pin();
		if (Window.IsValid())
		{
			Window->BringToFront();
			return FReply::Handled();
		}
	}

	// Create the model browser window
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ModelBrowserTitle", "Browse Models"))
		.ClientSize(FVector2D(720, 520))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize)
		[
			SNew(SNeoStackModelBrowser)
			.OnModelSelected(this, &SNeoStackSidebar::OnModelBrowserSelected)
			.OnClosed(this, &SNeoStackSidebar::OnModelBrowserClosed)
		];

	ModelBrowserWindow = Window;

	FSlateApplication::Get().AddWindow(Window);

	return FReply::Handled();
}

void SNeoStackSidebar::OnModelBrowserSelected(TSharedPtr<FOpenRouterModelInfo> SelectedOpenRouterModel)
{
	if (!SelectedOpenRouterModel.IsValid())
	{
		return;
	}

	// Check if this model already exists in our list
	for (const TSharedPtr<FModelInfo>& Existing : ModelOptions)
	{
		if (Existing->ModelID == SelectedOpenRouterModel->ID)
		{
			// Already exists, just select it
			SelectedModel = Existing;
			if (ModelComboBox.IsValid())
			{
				ModelComboBox->SetSelectedItem(Existing);
			}
			SaveSelections();
			return;
		}
	}

	// Add new model to favorites
	AddModelToFavorites(
		SelectedOpenRouterModel->Name,
		SelectedOpenRouterModel->ID,
		SelectedOpenRouterModel->Provider,
		SelectedOpenRouterModel->Description,
		SelectedOpenRouterModel->PromptCost,
		SelectedOpenRouterModel->CompletionCost
	);
}

void SNeoStackSidebar::OnModelBrowserClosed()
{
	if (ModelBrowserWindow.IsValid())
	{
		TSharedPtr<SWindow> Window = ModelBrowserWindow.Pin();
		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}
		ModelBrowserWindow.Reset();
	}
}

void SNeoStackSidebar::AddModelToFavorites(const FString& Name, const FString& ModelID, const FString& Provider,
                                           const FString& Description, const FString& InputCost, const FString& OutputCost)
{
	// Format costs for display (convert from per-token to per-million)
	auto FormatCost = [](const FString& PerTokenCost) -> FString
	{
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
	};

	FString FormattedInputCost = FormatCost(InputCost);
	FString FormattedOutputCost = FormatCost(OutputCost);

	// Truncate description if too long
	FString ShortDesc = Description;
	if (ShortDesc.Len() > 60)
	{
		ShortDesc = ShortDesc.Left(57) + TEXT("...");
	}

	// Create new model info
	TSharedPtr<FModelInfo> NewModel = MakeShareable(new FModelInfo(
		Name,
		ModelID,
		Provider,
		ShortDesc,
		FormattedInputCost,
		FormattedOutputCost,
		true  // User-added models are marked as variable pricing
	));

	ModelOptions.Add(NewModel);

	// Save to settings
	SaveUserModels();

	// Select the new model
	SelectedModel = NewModel;
	if (ModelComboBox.IsValid())
	{
		ModelComboBox->RefreshOptions();
		ModelComboBox->SetSelectedItem(NewModel);
	}
	SaveSelections();
}

void SNeoStackSidebar::LoadUserModels()
{
	FString FilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
	FString FileContent;

	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* UserModelsArray;
			if (JsonObject->TryGetArrayField(TEXT("UserModels"), UserModelsArray))
			{
				for (const TSharedPtr<FJsonValue>& Value : *UserModelsArray)
				{
					const TSharedPtr<FJsonObject>* ModelObj;
					if (Value->TryGetObject(ModelObj))
					{
						FString Name, ModelID, Provider, Description, InputCost, OutputCost;

						(*ModelObj)->TryGetStringField(TEXT("Name"), Name);
						(*ModelObj)->TryGetStringField(TEXT("ModelID"), ModelID);
						(*ModelObj)->TryGetStringField(TEXT("Provider"), Provider);
						(*ModelObj)->TryGetStringField(TEXT("Description"), Description);
						(*ModelObj)->TryGetStringField(TEXT("InputCost"), InputCost);
						(*ModelObj)->TryGetStringField(TEXT("OutputCost"), OutputCost);

						// Check if model already exists (from default list)
						bool bExists = false;
						for (const TSharedPtr<FModelInfo>& Existing : ModelOptions)
						{
							if (Existing->ModelID == ModelID)
							{
								bExists = true;
								break;
							}
						}

						if (!bExists && !ModelID.IsEmpty())
						{
							ModelOptions.Add(MakeShareable(new FModelInfo(
								Name, ModelID, Provider, Description, InputCost, OutputCost, true
							)));
						}
					}
				}
			}
		}
	}
}

void SNeoStackSidebar::SaveUserModels()
{
	FString FilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");

	// Load existing settings
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	FString ExistingContent;
	if (FFileHelper::LoadFileToString(ExistingContent, *FilePath))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingContent);
		FJsonSerializer::Deserialize(Reader, JsonObject);
	}

	if (!JsonObject.IsValid())
	{
		JsonObject = MakeShared<FJsonObject>();
	}

	// Build user models array (only models not in the default list)
	TArray<TSharedPtr<FJsonValue>> UserModelsArray;

	// Default model IDs to skip
	TSet<FString> DefaultModelIDs;
	DefaultModelIDs.Add(TEXT("anthropic/claude-haiku-4.5"));
	DefaultModelIDs.Add(TEXT("anthropic/claude-sonnet-4.5"));
	DefaultModelIDs.Add(TEXT("openai/gpt-5.1"));
	DefaultModelIDs.Add(TEXT("openai/gpt-5.1-mini"));
	DefaultModelIDs.Add(TEXT("x-ai/grok-4.1-fast"));
	DefaultModelIDs.Add(TEXT("z-ai/glm-4.6:exacto"));
	DefaultModelIDs.Add(TEXT("google/gemini-3-pro-preview"));

	for (const TSharedPtr<FModelInfo>& Model : ModelOptions)
	{
		if (!DefaultModelIDs.Contains(Model->ModelID))
		{
			TSharedPtr<FJsonObject> ModelObj = MakeShared<FJsonObject>();
			ModelObj->SetStringField(TEXT("Name"), Model->Name);
			ModelObj->SetStringField(TEXT("ModelID"), Model->ModelID);
			ModelObj->SetStringField(TEXT("Provider"), Model->Provider);
			ModelObj->SetStringField(TEXT("Description"), Model->Description);
			ModelObj->SetStringField(TEXT("InputCost"), Model->InputCost);
			ModelObj->SetStringField(TEXT("OutputCost"), Model->OutputCost);

			UserModelsArray.Add(MakeShareable(new FJsonValueObject(ModelObj)));
		}
	}

	JsonObject->SetArrayField(TEXT("UserModels"), UserModelsArray);

	// Save
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Ensure directory exists
	FString DirPath = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*DirPath, true);

	FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#undef LOCTEXT_NAMESPACE
