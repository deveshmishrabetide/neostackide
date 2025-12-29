// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackSettingsPanel.h"
#include "UI/SNeoStackSidebar.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Http.h"
#include "NeoStackSettings.h"

// Static map to store provider routing preferences per model
static TMap<FString, FProviderRouting> GModelProviderRouting;

#define LOCTEXT_NAMESPACE "SNeoStackSettingsPanel"

void SNeoStackSettingsPanel::Construct(const FArguments& InArgs)
{
	OnCloseDelegate = InArgs._OnClose;

	// Initialize reasoning effort options as member variable
	EffortOptions.Add(MakeShared<FString>(TEXT("high")));
	EffortOptions.Add(MakeShared<FString>(TEXT("medium")));
	EffortOptions.Add(MakeShared<FString>(TEXT("low")));

	// Initialize sort by options
	SortByOptions.Add(MakeShared<FString>(TEXT("Default")));
	SortByOptions.Add(MakeShared<FString>(TEXT("Price")));
	SortByOptions.Add(MakeShared<FString>(TEXT("Throughput")));

	// Load saved settings
	LoadSettings();

	TSharedPtr<FString> DefaultEffort = EffortOptions[1]; // medium
	// Find the current effort level in options
	for (const TSharedPtr<FString>& Option : EffortOptions)
	{
		if (*Option == ReasoningEffort)
		{
			DefaultEffort = Option;
			break;
		}
	}

	// Find the current sort by option
	TSharedPtr<FString> DefaultSortBy = SortByOptions[0]; // Default
	for (const TSharedPtr<FString>& Option : SortByOptions)
	{
		if (Option->ToLower() == ProviderRoutingSettings.SortBy.ToLower())
		{
			DefaultSortBy = Option;
			break;
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#18181b")))))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)

			// Header
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1f1f23")))))
				.Padding(16.0f, 12.0f)
				[
					SNew(SHorizontalBox)

					// Title
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SettingsTitle", "Settings"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(FLinearColor::White)
					]

					// Close button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.OnClicked(this, &SNeoStackSettingsPanel::OnCloseClicked)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CloseButton", "X"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
							.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
						]
					]
				]
			]

			// Note
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16.0f, 12.0f, 16.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2a2a2d")))))
				.Padding(12.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SettingsNote", "Note: These are general settings that will be applied where supported by the model."))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.3f, 1.0f))
					.AutoWrapText(true)
				]
			]

			// Settings content
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(16.0f)
			[
				SNew(SScrollBox)

				// Provider Selection Section
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 20.0f)
				[
					SAssignNew(ProviderSection, SVerticalBox)

					// Row with Provider dropdown and Sort By dropdown
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SHorizontalBox)

						// Provider label and dropdown
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ProviderLabel", "Provider"))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(ProviderComboBox, SComboBox<TSharedPtr<FProviderEndpoint>>)
								.OptionsSource(&FilteredProviderOptions)
								.OnGenerateWidget(this, &SNeoStackSettingsPanel::GenerateProviderWidget)
								.OnSelectionChanged(this, &SNeoStackSettingsPanel::OnProviderSelected)
								.IsEnabled_Lambda([this]()
								{
									// Disable provider selection when sorting by price or throughput
									return ProviderRoutingSettings.SortBy == TEXT("default") || ProviderRoutingSettings.SortBy.IsEmpty();
								})
								[
									SNew(SHorizontalBox)

									// Provider icon (lightning for Auto, dot for others)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0.0f, 0.0f, 6.0f, 0.0f)
									[
										SNew(STextBlock)
										.Text_Lambda([this]()
										{
											// Show lightning when sorting (auto-select) or when Auto is selected
											if (ProviderRoutingSettings.SortBy != TEXT("default") && !ProviderRoutingSettings.SortBy.IsEmpty())
											{
												return FText::FromString(TEXT("\u26A1")); // Lightning bolt for sorting
											}
											if (CurrentProvider.IsValid() && CurrentProvider->bIsAuto)
											{
												return FText::FromString(TEXT("\u26A1")); // Lightning bolt for Auto
											}
											return FText::FromString(TEXT("\u2022")); // Bullet for specific provider
										})
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
									]

									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text_Lambda([this]()
										{
											// Show "Auto (by X)" when sorting
											if (ProviderRoutingSettings.SortBy != TEXT("default") && !ProviderRoutingSettings.SortBy.IsEmpty())
											{
												FString SortDisplay = ProviderRoutingSettings.SortBy;
												SortDisplay[0] = FChar::ToUpper(SortDisplay[0]);
												return FText::Format(LOCTEXT("AutoBySort", "Auto (by {0})"), FText::FromString(SortDisplay));
											}
											if (bLoadingProviders)
											{
												return LOCTEXT("LoadingProviders", "Loading...");
											}
											if (CurrentProvider.IsValid())
											{
												if (CurrentProvider->bIsAuto)
												{
													return LOCTEXT("AutoProvider", "Auto");
												}
												return FText::FromString(CurrentProvider->ProviderName);
											}
											return LOCTEXT("AutoProvider", "Auto");
										})
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
									]
								]
							]
						]

						// Spacer
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpacer)
						]

						// Sort By label and dropdown
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SortByLabel", "Sort By"))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(SortByComboBox, SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&SortByOptions)
								.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
								{
									return SNew(STextBlock)
										.Text(FText::FromString(*Item))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10));
								})
								.OnSelectionChanged(this, &SNeoStackSettingsPanel::OnSortByChanged)
								.InitiallySelectedItem(DefaultSortBy)
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										// Capitalize first letter for display
										FString Display = ProviderRoutingSettings.SortBy;
										if (Display.Len() > 0)
										{
											Display[0] = FChar::ToUpper(Display[0]);
										}
										return FText::FromString(Display);
									})
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								]
							]
						]
					]

					// Provider info text
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SAssignNew(ProviderInfoText, STextBlock)
						.Text_Lambda([this]()
						{
							if (CurrentProvider.IsValid() && !CurrentProvider->bIsAuto)
							{
								return FText::Format(
									LOCTEXT("ProviderInfo", "Context: {0}K | In: {1}/M | Out: {2}/M"),
									FText::AsNumber(CurrentProvider->ContextLength / 1000),
									FText::FromString(FormatCostPerMillion(CurrentProvider->InputCost)),
									FText::FromString(FormatCostPerMillion(CurrentProvider->OutputCost))
								);
							}
							if (CurrentProvider.IsValid() && CurrentProvider->bIsAuto)
							{
								return LOCTEXT("AutoProviderInfo", "OpenRouter will automatically select the best provider");
							}
							return FText::GetEmpty();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
					]
				]

				// Divider
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(SBorder)
					.BorderImage(new FSlateColorBrush(FLinearColor(0.3f, 0.3f, 0.3f, 1.0f)))
					.Padding(0.0f)
					[
						SNew(SBox)
						.HeightOverride(1.0f)
					]
				]

				// Max Cost Per Query
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					CreateSettingRow(
						LOCTEXT("MaxCostLabel", "Max Cost Per Query"),
						SNew(SSpinBox<float>)
						.MinValue(0.0f)
						.MaxValue(10.0f)
						.Delta(0.01f)
						.Value(MaxCostPerQuery)
						.OnValueChanged(this, &SNeoStackSettingsPanel::OnMaxCostChanged)
					)
				]

				// Max Tokens
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					CreateSettingRow(
						LOCTEXT("MaxTokensLabel", "Max Tokens"),
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.MaxValue(200000)
						.Delta(100)
						.Value(MaxTokens)
						.OnValueChanged(this, &SNeoStackSettingsPanel::OnMaxTokensChanged)
					)
				]

				// Enable Thinking
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					CreateSettingRow(
						LOCTEXT("EnableThinkingLabel", "Enable Thinking"),
						SNew(SCheckBox)
						.IsChecked(bEnableThinking ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.OnCheckStateChanged(this, &SNeoStackSettingsPanel::OnEnableThinkingChanged)
					)
				]

				// Max Thinking Tokens
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					CreateSettingRow(
						LOCTEXT("MaxThinkingTokensLabel", "Max Thinking Tokens"),
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.MaxValue(32000)
						.Delta(100)
						.Value(MaxThinkingTokens)
						.OnValueChanged(this, &SNeoStackSettingsPanel::OnMaxThinkingTokensChanged)
					)
				]

				// Reasoning Effort
				+ SScrollBox::Slot()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					CreateSettingRow(
						LOCTEXT("ReasoningEffortLabel", "Reasoning Effort"),
						SNew(SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&EffortOptions)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(*Item))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10));
						})
						.OnSelectionChanged(this, &SNeoStackSettingsPanel::OnReasoningEffortChanged)
						.InitiallySelectedItem(DefaultEffort)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return FText::FromString(ReasoningEffort); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					)
				]
			]
		]
	];

	// Initialize with Auto option so dropdown always has something
	TSharedPtr<FProviderEndpoint> AutoOption = MakeShared<FProviderEndpoint>();
	AutoOption->bIsAuto = true;
	AutoOption->ProviderName = TEXT("Auto");
	AutoOption->Name = TEXT("Auto");
	AutoOption->Status = TEXT("online");
	ProviderOptions.Add(AutoOption);
	FilteredProviderOptions.Add(AutoOption);
	CurrentProvider = AutoOption;

	// Refresh combo boxes with initial options
	if (ProviderComboBox.IsValid())
	{
		ProviderComboBox->RefreshOptions();
		ProviderComboBox->SetSelectedItem(CurrentProvider);
	}
	if (SortByComboBox.IsValid())
	{
		SortByComboBox->RefreshOptions();
	}

	// Load providers for current model if one is selected
	LoadProvidersForCurrentModel();
}

TSharedRef<SWidget> SNeoStackSettingsPanel::CreateSettingRow(const FText& Label, const TSharedRef<SWidget>& ValueWidget)
{
	return SNew(SVerticalBox)

		// Label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
		]

		// Value widget
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ValueWidget
		];
}

FReply SNeoStackSettingsPanel::OnCloseClicked()
{
	if (OnCloseDelegate.IsBound())
	{
		OnCloseDelegate.Execute();
	}
	return FReply::Handled();
}

void SNeoStackSettingsPanel::OnMaxCostChanged(float NewValue)
{
	MaxCostPerQuery = NewValue;
	SaveSettings();
}

void SNeoStackSettingsPanel::OnMaxTokensChanged(int32 NewValue)
{
	MaxTokens = NewValue;
	SaveSettings();
}

void SNeoStackSettingsPanel::OnEnableThinkingChanged(ECheckBoxState NewState)
{
	bEnableThinking = (NewState == ECheckBoxState::Checked);
	SaveSettings();
}

void SNeoStackSettingsPanel::OnMaxThinkingTokensChanged(int32 NewValue)
{
	MaxThinkingTokens = NewValue;
	SaveSettings();
}

void SNeoStackSettingsPanel::OnReasoningEffortChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		ReasoningEffort = *NewSelection;
		SaveSettings();
	}
}

FString SNeoStackSettingsPanel::GetSettingsFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
}

void SNeoStackSettingsPanel::SaveSettings()
{
	FString FilePath = GetSettingsFilePath();

	// Load existing settings to preserve other fields (like SelectedModelID)
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

	// Update settings fields
	JsonObject->SetNumberField(TEXT("MaxCostPerQuery"), MaxCostPerQuery);
	JsonObject->SetNumberField(TEXT("MaxTokens"), MaxTokens);
	JsonObject->SetBoolField(TEXT("EnableThinking"), bEnableThinking);
	JsonObject->SetNumberField(TEXT("MaxThinkingTokens"), MaxThinkingTokens);
	JsonObject->SetStringField(TEXT("ReasoningEffort"), ReasoningEffort);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void SNeoStackSettingsPanel::LoadSettings()
{
	FString FilePath = GetSettingsFilePath();
	FString FileContent;

	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			MaxCostPerQuery = JsonObject->GetNumberField(TEXT("MaxCostPerQuery"));
			MaxTokens = JsonObject->GetIntegerField(TEXT("MaxTokens"));
			bEnableThinking = JsonObject->GetBoolField(TEXT("EnableThinking"));
			MaxThinkingTokens = JsonObject->GetIntegerField(TEXT("MaxThinkingTokens"));
			ReasoningEffort = JsonObject->GetStringField(TEXT("ReasoningEffort"));

			// Load provider routing preferences
			const TSharedPtr<FJsonObject>* RoutingObj;
			if (JsonObject->TryGetObjectField(TEXT("ProviderRouting"), RoutingObj) && RoutingObj->IsValid())
			{
				for (const auto& ModelPair : (*RoutingObj)->Values)
				{
					const TSharedPtr<FJsonObject>* ModelRoutingObj;
					if (ModelPair.Value->TryGetObject(ModelRoutingObj) && ModelRoutingObj->IsValid())
					{
						FProviderRouting Routing;
						(*ModelRoutingObj)->TryGetStringField(TEXT("provider"), Routing.SelectedProvider);
						(*ModelRoutingObj)->TryGetStringField(TEXT("sort_by"), Routing.SortBy);
						(*ModelRoutingObj)->TryGetBoolField(TEXT("allow_fallbacks"), Routing.bAllowFallbacks);
						GModelProviderRouting.Add(ModelPair.Key, Routing);
					}
				}
			}
		}
	}
	// If file doesn't exist, keep defaults
}

void SNeoStackSettingsPanel::OnProviderSelected(TSharedPtr<FProviderEndpoint> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		CurrentProvider = NewSelection;

		// Update routing settings
		if (NewSelection->bIsAuto)
		{
			ProviderRoutingSettings.SelectedProvider = TEXT("");  // Empty means Auto
		}
		else
		{
			ProviderRoutingSettings.SelectedProvider = NewSelection->ProviderName;
		}

		// Save preference for this model
		if (!CurrentModelID.IsEmpty())
		{
			SetProviderRoutingForModel(CurrentModelID, ProviderRoutingSettings);
		}

		SaveSettings();
	}
}

void SNeoStackSettingsPanel::OnSortByChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		ProviderRoutingSettings.SortBy = NewSelection->ToLower();

		// Save preference for this model
		if (!CurrentModelID.IsEmpty())
		{
			SetProviderRoutingForModel(CurrentModelID, ProviderRoutingSettings);
		}

		SaveSettings();
	}
}

void SNeoStackSettingsPanel::FilterProviderOptions()
{
	// Just copy all options (no filtering needed)
	FilteredProviderOptions = ProviderOptions;

	// Refresh combo box
	if (ProviderComboBox.IsValid())
	{
		ProviderComboBox->RefreshOptions();
	}
}

FString SNeoStackSettingsPanel::FormatCostPerMillion(const FString& PerTokenCost)
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
}

TSharedRef<SWidget> SNeoStackSettingsPanel::GenerateProviderWidget(TSharedPtr<FProviderEndpoint> Item)
{
	if (!Item.IsValid())
	{
		return SNew(STextBlock).Text(LOCTEXT("InvalidProvider", "Invalid"));
	}

	// Special handling for Auto option
	if (Item->bIsAuto)
	{
		return SNew(SHorizontalBox)
			// Lightning icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u26A1")))  // Lightning bolt
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.3f, 0.7f, 1.0f, 1.0f))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AutoOption", "Auto"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			];
	}

	FString DisplayText = Item->ProviderName;
	if (!Item->Variant.IsEmpty())
	{
		DisplayText += FString::Printf(TEXT(" (%s)"), *Item->Variant);
	}

	FLinearColor StatusColor = FLinearColor::Green;
	if (Item->Status != TEXT("online"))
	{
		StatusColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
	}

	// Format prices as per-million tokens
	FString FormattedInputCost = FormatCostPerMillion(Item->InputCost);
	FString FormattedOutputCost = FormatCostPerMillion(Item->OutputCost);

	return SNew(SHorizontalBox)

		// Provider icon (bullet)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("\u2022")))  // Bullet point
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		]

		// Provider name
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayText))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		]

		// Price info (per million tokens)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("ProviderPrice", "{0}/{1}/M"),
				FText::FromString(FormattedInputCost),
				FText::FromString(FormattedOutputCost)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
		]

		// Status indicator
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(8.0f)
			.HeightOverride(8.0f)
			[
				SNew(SBorder)
				.BorderImage(new FSlateColorBrush(StatusColor))
			]
		];
}

void SNeoStackSettingsPanel::LoadProvidersForCurrentModel()
{
	// Get current model ID from sidebar selection
	// Since settings panel doesn't have direct access, we'll store it when the model changes
	// For now, load from saved preferences to get the current model

	// Try to get from saved settings (same file as other settings)
	FString SettingsPath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
	FString FileContent;

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Looking for settings at: %s"), *SettingsPath);

	if (FFileHelper::LoadFileToString(FileContent, *SettingsPath))
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Settings file content: %s"), *FileContent);
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			JsonObject->TryGetStringField(TEXT("SelectedModelID"), CurrentModelID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] Settings file not found at: %s"), *SettingsPath);
	}

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] LoadProvidersForCurrentModel: ModelID=%s"), *CurrentModelID);

	if (CurrentModelID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] No model selected, skipping provider load"));
		return;
	}

	bLoadingProviders = true;

	// Get settings
	const UNeoStackSettings* Settings = UNeoStackSettings::Get();
	if (!Settings || Settings->BackendURL.IsEmpty() || Settings->APIKey.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] Settings not configured, skipping provider load"));
		bLoadingProviders = false;
		return;
	}

	// Fetch endpoints from backend
	FString EndpointURL = FString::Printf(TEXT("%s/models/%s/endpoints"), *Settings->BackendURL, *CurrentModelID);
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Fetching providers from: %s"), *EndpointURL);

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(EndpointURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-API-Key"), Settings->APIKey);

	TWeakPtr<SNeoStackSettingsPanel> WeakSelf = SharedThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakSelf](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		TSharedPtr<SNeoStackSettingsPanel> StrongThis = WeakSelf.Pin();
		if (!StrongThis.IsValid())
		{
			return;
		}

		StrongThis->bLoadingProviders = false;

		if (!bWasSuccessful || !Response.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to fetch provider endpoints"));
			return;
		}

		if (Response->GetResponseCode() != 200)
		{
			UE_LOG(LogTemp, Warning, TEXT("Provider endpoints request failed with code %d: %s"),
				Response->GetResponseCode(), *Response->GetContentAsString());
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Provider endpoints response: %s"), *Response->GetContentAsString());

		// Parse response
		TSharedPtr<FJsonObject> JsonResponse;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to parse provider endpoints response"));
			return;
		}

		TArray<FProviderEndpoint> Endpoints;

		const TSharedPtr<FJsonObject>* DataObj;
		if (JsonResponse->TryGetObjectField(TEXT("data"), DataObj) && DataObj->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* EndpointsArray;
			if ((*DataObj)->TryGetArrayField(TEXT("endpoints"), EndpointsArray))
			{
				for (const TSharedPtr<FJsonValue>& EndpointValue : *EndpointsArray)
				{
					const TSharedPtr<FJsonObject>* EndpointObj;
					if (EndpointValue->TryGetObject(EndpointObj) && EndpointObj->IsValid())
					{
						FProviderEndpoint Endpoint;
						(*EndpointObj)->TryGetStringField(TEXT("name"), Endpoint.Name);
						(*EndpointObj)->TryGetStringField(TEXT("provider_name"), Endpoint.ProviderName);
						(*EndpointObj)->TryGetNumberField(TEXT("context_length"), Endpoint.ContextLength);
						(*EndpointObj)->TryGetStringField(TEXT("status"), Endpoint.Status);
						(*EndpointObj)->TryGetStringField(TEXT("quantization"), Endpoint.Quantization);
						(*EndpointObj)->TryGetStringField(TEXT("variant"), Endpoint.Variant);

						const TSharedPtr<FJsonObject>* PricingObj;
						if ((*EndpointObj)->TryGetObjectField(TEXT("pricing"), PricingObj) && PricingObj->IsValid())
						{
							(*PricingObj)->TryGetStringField(TEXT("prompt"), Endpoint.InputCost);
							(*PricingObj)->TryGetStringField(TEXT("completion"), Endpoint.OutputCost);
						}

						const TArray<TSharedPtr<FJsonValue>>* ParamsArray;
						if ((*EndpointObj)->TryGetArrayField(TEXT("supported_parameters"), ParamsArray))
						{
							for (const TSharedPtr<FJsonValue>& ParamValue : *ParamsArray)
							{
								FString Param;
								if (ParamValue->TryGetString(Param))
								{
									Endpoint.SupportedParameters.Add(Param);
								}
							}
						}

						Endpoints.Add(Endpoint);
					}
				}
			}
		}

		// Update on game thread
		AsyncTask(ENamedThreads::GameThread, [WeakSelf, Endpoints]()
		{
			TSharedPtr<SNeoStackSettingsPanel> StrongThis = WeakSelf.Pin();
			if (StrongThis.IsValid())
			{
				StrongThis->OnProvidersLoaded(Endpoints);
			}
		});
	});

	Request->ProcessRequest();
}

void SNeoStackSettingsPanel::OnProvidersLoaded(const TArray<FProviderEndpoint>& Endpoints)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] OnProvidersLoaded: Received %d endpoints"), Endpoints.Num());

	ProviderOptions.Empty();

	// Add "Auto" option first
	TSharedPtr<FProviderEndpoint> AutoOption = MakeShared<FProviderEndpoint>();
	AutoOption->bIsAuto = true;
	AutoOption->ProviderName = TEXT("Auto");
	AutoOption->Name = TEXT("Auto");
	AutoOption->Status = TEXT("online");
	ProviderOptions.Add(AutoOption);

	// Add all real providers
	for (const FProviderEndpoint& Endpoint : Endpoints)
	{
		UE_LOG(LogTemp, Log, TEXT("[NeoStack] Adding provider: %s"), *Endpoint.ProviderName);
		ProviderOptions.Add(MakeShared<FProviderEndpoint>(Endpoint));
	}

	// Load routing preferences for this model
	FProviderRouting SavedRouting = GetProviderRoutingForModel(CurrentModelID);
	ProviderRoutingSettings = SavedRouting;
	CurrentProvider = nullptr;

	// Find the saved provider (empty means Auto)
	if (SavedRouting.SelectedProvider.IsEmpty())
	{
		// Select Auto
		CurrentProvider = AutoOption;
	}
	else
	{
		for (const TSharedPtr<FProviderEndpoint>& Option : ProviderOptions)
		{
			if (!Option->bIsAuto && Option->ProviderName == SavedRouting.SelectedProvider)
			{
				CurrentProvider = Option;
				break;
			}
		}
	}

	// If no saved provider found, default to Auto
	if (!CurrentProvider.IsValid())
	{
		CurrentProvider = AutoOption;
	}

	// Update filtered options
	FilterProviderOptions();

	// Update sort by combo box
	if (SortByComboBox.IsValid())
	{
		for (const TSharedPtr<FString>& Option : SortByOptions)
		{
			if (Option->ToLower() == ProviderRoutingSettings.SortBy.ToLower())
			{
				SortByComboBox->SetSelectedItem(Option);
				break;
			}
		}
	}

	// Refresh combo box
	if (ProviderComboBox.IsValid())
	{
		if (CurrentProvider.IsValid())
		{
			ProviderComboBox->SetSelectedItem(CurrentProvider);
		}
	}
}

FProviderRouting SNeoStackSettingsPanel::GetProviderRoutingForModel(const FString& ModelID)
{
	if (FProviderRouting* Routing = GModelProviderRouting.Find(ModelID))
	{
		return *Routing;
	}
	return FProviderRouting();  // Default: Auto, sort by default
}

void SNeoStackSettingsPanel::SetProviderRoutingForModel(const FString& ModelID, const FProviderRouting& Routing)
{
	GModelProviderRouting.Add(ModelID, Routing);

	// Save to file
	FString FilePath = GetSettingsFilePath();
	FString FileContent;

	TSharedPtr<FJsonObject> JsonObject;

	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
		FJsonSerializer::Deserialize(Reader, JsonObject);
	}

	if (!JsonObject.IsValid())
	{
		JsonObject = MakeShared<FJsonObject>();
	}

	// Update provider routing preferences
	TSharedPtr<FJsonObject> RoutingObj = MakeShared<FJsonObject>();
	for (const auto& Pair : GModelProviderRouting)
	{
		TSharedPtr<FJsonObject> ModelRoutingObj = MakeShared<FJsonObject>();
		ModelRoutingObj->SetStringField(TEXT("provider"), Pair.Value.SelectedProvider);
		ModelRoutingObj->SetStringField(TEXT("sort_by"), Pair.Value.SortBy);
		ModelRoutingObj->SetBoolField(TEXT("allow_fallbacks"), Pair.Value.bAllowFallbacks);
		RoutingObj->SetObjectField(Pair.Key, ModelRoutingObj);
	}
	JsonObject->SetObjectField(TEXT("ProviderRouting"), RoutingObj);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#undef LOCTEXT_NAMESPACE
