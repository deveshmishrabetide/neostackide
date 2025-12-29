// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackHeader.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"

#define LOCTEXT_NAMESPACE "SNeoStackHeader"

// Static instance
TWeakPtr<SNeoStackHeader> SNeoStackHeader::Instance;

void SNeoStackHeader::Construct(const FArguments& InArgs)
{
	// Store weak reference to self
	Instance = SharedThis(this);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#151515")))))
			.Padding(FMargin(15.0f, 10.0f))
			[
				SNew(SHorizontalBox)
			// Left side - NeoStack Title
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BrandingTitle", "NeoStack"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.ColorAndOpacity(FLinearColor::White)
			]

			// Middle - spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)

			// Right side - Live Cost Watcher
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 5.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CostLabel", "Cost:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("$%.6f"), CurrentCost));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f, 1.0f))
				]
			]
			]
		]
		// Dividing line
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2a2a2a")))))
			.Padding(0.0f)
			[
				SNew(SBox)
				.HeightOverride(1.0f)
			]
		]
	];
}

void SNeoStackHeader::SetCost(float Cost)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Header SetCost called: $%.6f"), Cost);
	CurrentCost = Cost;
}

TSharedPtr<SNeoStackHeader> SNeoStackHeader::Get()
{
	return Instance.Pin();
}

#undef LOCTEXT_NAMESPACE
