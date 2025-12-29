// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCollapsibleToolWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "NeoStackStyle.h"

// Static set of always-allowed tools
static TSet<FString> GAlwaysAllowedTools;

TSet<FString>& SCollapsibleToolWidget::GetAlwaysAllowedTools()
{
	return GAlwaysAllowedTools;
}

void SCollapsibleToolWidget::Construct(const FArguments& InArgs)
{
	ToolName = InArgs._ToolName;
	Args = InArgs._Args;
	CallID = InArgs._CallID;
	OnApprovedDelegate = InArgs._OnApproved;
	OnRejectedDelegate = InArgs._OnRejected;
	bIsExpanded = true;

	// Check if this tool is always allowed
	bool bRequiresApproval = InArgs._RequiresApproval;
	if (GAlwaysAllowedTools.Contains(ToolName))
	{
		bRequiresApproval = false;
		ExecutionState = EToolExecutionState::Executing;
		// Auto-approve
		OnApprovedDelegate.ExecuteIfBound(CallID, false);
	}
	else
	{
		ExecutionState = bRequiresApproval ? EToolExecutionState::PendingApproval : EToolExecutionState::Executing;
	}

	TSharedPtr<SVerticalBox> MainContainer;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#18181b")))))
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
				.OnClicked(this, &SCollapsibleToolWidget::OnToggleExpand)
				.ContentPadding(FMargin(12.0f, 10.0f))
				[
					SNew(SHorizontalBox)
					// Expand/Collapse icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SImage)
						.Image(this, &SCollapsibleToolWidget::GetExpandIcon)
						.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
					]
					// Status icon (tool icon or success icon)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SAssignNew(StatusIcon, SImage)
						.Image(this, &SCollapsibleToolWidget::GetStatusIcon)
						.ColorAndOpacity(this, &SCollapsibleToolWidget::GetStatusColor)
						.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
					]
					// Tool name
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(ToolName))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f))
					]
					// Status text
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SAssignNew(StatusText, STextBlock)
						.Text(this, &SCollapsibleToolWidget::GetStatusText)
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
					]
				]
			]
		]
	];

	// Add collapsible details section
	MainContainer->AddSlot()
	.AutoHeight()
	[
		SAssignNew(DetailsContainer, SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#0f0f11")))))
		.Padding(12.0f, 8.0f, 12.0f, 12.0f)
		.Visibility(EVisibility::Visible)
		[
			SAssignNew(DetailsBox, SVerticalBox)
			// Arguments section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Arguments")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Args.IsEmpty() ? TEXT("{}") : Args))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.0f, 1.0f))
				.AutoWrapText(true)
			]
			// Approval buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SAssignNew(ApprovalButtons, SHorizontalBox)
				.Visibility(this, &SCollapsibleToolWidget::GetApprovalButtonsVisibility)
				// Accept button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.OnClicked(this, &SCollapsibleToolWidget::OnAcceptClicked)
					.ButtonColorAndOpacity(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#22c55e"))))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Accept")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
				// Always Allow button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.OnClicked(this, &SCollapsibleToolWidget::OnAlwaysAllowClicked)
					.ButtonColorAndOpacity(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#3b82f6"))))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Always Allow")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
				// Reject button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SCollapsibleToolWidget::OnRejectClicked)
					.ButtonColorAndOpacity(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ef4444"))))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Reject")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		]
	];
}

void SCollapsibleToolWidget::SetResult(const FString& InResult, bool bSuccess)
{
	// Prevent duplicate result display
	if (bResultSet)
	{
		return;
	}
	bResultSet = true;

	Result = InResult;
	ExecutionState = bSuccess ? EToolExecutionState::Completed : EToolExecutionState::Failed;

	// Add result section to the details box
	if (DetailsBox.IsValid())
	{
		FLinearColor ResultColor = bSuccess
			? FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#10b981")))  // Green
			: FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ef4444"))); // Red

		DetailsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#0a0a0c")))))
			.Padding(8.0f, 6.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(bSuccess ? TEXT("Result") : TEXT("Error")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Result))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(ResultColor)
					.AutoWrapText(true)
				]
			]
		];
	}
}

void SCollapsibleToolWidget::SetExecuting()
{
	ExecutionState = EToolExecutionState::Executing;
}

FReply SCollapsibleToolWidget::OnToggleExpand()
{
	bIsExpanded = !bIsExpanded;

	if (DetailsContainer.IsValid())
	{
		DetailsContainer->SetVisibility(bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed);
	}

	return FReply::Handled();
}

FReply SCollapsibleToolWidget::OnAcceptClicked()
{
	ExecutionState = EToolExecutionState::Executing;
	OnApprovedDelegate.ExecuteIfBound(CallID, false);
	return FReply::Handled();
}

FReply SCollapsibleToolWidget::OnAlwaysAllowClicked()
{
	ExecutionState = EToolExecutionState::Executing;
	GAlwaysAllowedTools.Add(ToolName);
	OnApprovedDelegate.ExecuteIfBound(CallID, true);
	return FReply::Handled();
}

FReply SCollapsibleToolWidget::OnRejectClicked()
{
	ExecutionState = EToolExecutionState::Rejected;
	OnRejectedDelegate.ExecuteIfBound(CallID);
	return FReply::Handled();
}

const FSlateBrush* SCollapsibleToolWidget::GetExpandIcon() const
{
	return bIsExpanded
		? FNeoStackStyle::Get().GetBrush("NeoStack.ArrowDownIcon")
		: FNeoStackStyle::Get().GetBrush("NeoStack.ArrowRightIcon");
}

const FSlateBrush* SCollapsibleToolWidget::GetStatusIcon() const
{
	switch (ExecutionState)
	{
		case EToolExecutionState::Completed:
			return FNeoStackStyle::Get().GetBrush("NeoStack.ToolSuccessIcon");
		case EToolExecutionState::Failed:
		case EToolExecutionState::Rejected:
			return FNeoStackStyle::Get().GetBrush("NeoStack.ToolIcon"); // Could add error icon
		default:
			return FNeoStackStyle::Get().GetBrush("NeoStack.ToolIcon");
	}
}

FSlateColor SCollapsibleToolWidget::GetStatusColor() const
{
	switch (ExecutionState)
	{
		case EToolExecutionState::PendingApproval:
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#f59e0b"))); // Orange/amber
		case EToolExecutionState::Executing:
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#3b82f6"))); // Blue
		case EToolExecutionState::Completed:
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#10b981"))); // Green
		case EToolExecutionState::Failed:
		case EToolExecutionState::Rejected:
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ef4444"))); // Red
		default:
			return FLinearColor::White;
	}
}

FText SCollapsibleToolWidget::GetStatusText() const
{
	switch (ExecutionState)
	{
		case EToolExecutionState::PendingApproval:
			return FText::FromString(TEXT("awaiting approval"));
		case EToolExecutionState::Executing:
			return FText::FromString(TEXT("executing..."));
		case EToolExecutionState::Completed:
			return FText::FromString(TEXT("completed"));
		case EToolExecutionState::Rejected:
			return FText::FromString(TEXT("rejected"));
		case EToolExecutionState::Failed:
			return FText::FromString(TEXT("failed"));
		default:
			return FText::GetEmpty();
	}
}

EVisibility SCollapsibleToolWidget::GetApprovalButtonsVisibility() const
{
	return ExecutionState == EToolExecutionState::PendingApproval
		? EVisibility::Visible
		: EVisibility::Collapsed;
}
