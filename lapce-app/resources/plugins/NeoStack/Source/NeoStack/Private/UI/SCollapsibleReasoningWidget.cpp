// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCollapsibleReasoningWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "NeoStackStyle.h"

void SCollapsibleReasoningWidget::Construct(const FArguments& InArgs)
{
	Reasoning = InArgs._Reasoning;
	bIsExpanded = true; // Start expanded

	TSharedPtr<SVerticalBox> MainContainer;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1a1a1d")))))
		.Padding(0.0f)
		.BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#27272a"))))
		[
			SAssignNew(MainContainer, SVerticalBox)
			// Header (always visible)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.OnClicked(this, &SCollapsibleReasoningWidget::OnToggleExpand)
				.ContentPadding(FMargin(12.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					// Expand/Collapse icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SImage)
						.Image(this, &SCollapsibleReasoningWidget::GetExpandIcon)
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
					// "Reasoning" label
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Reasoning")))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
				]
			]
		]
	];

	// Add collapsible reasoning content
	MainContainer->AddSlot()
	.AutoHeight()
	[
		SAssignNew(DetailsContainer, SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#0f0f11")))))
		.Padding(12.0f, 8.0f)
		.Visibility(EVisibility::Visible) // Start expanded
		[
			SAssignNew(ReasoningTextBlock, STextBlock)
			.Text(FText::FromString(Reasoning))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f)) // Reduced opacity
			.AutoWrapText(true)
		]
	];
}

void SCollapsibleReasoningWidget::UpdateReasoning(const FString& NewReasoning)
{
	Reasoning = NewReasoning;

	if (ReasoningTextBlock.IsValid())
	{
		ReasoningTextBlock->SetText(FText::FromString(Reasoning));
	}
}

FReply SCollapsibleReasoningWidget::OnToggleExpand()
{
	bIsExpanded = !bIsExpanded;

	if (DetailsContainer.IsValid())
	{
		DetailsContainer->SetVisibility(bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed);
	}

	return FReply::Handled();
}

const FSlateBrush* SCollapsibleReasoningWidget::GetExpandIcon() const
{
	return bIsExpanded
		? FNeoStackStyle::Get().GetBrush("NeoStack.ArrowDownIcon")
		: FNeoStackStyle::Get().GetBrush("NeoStack.ArrowRightIcon");
}
