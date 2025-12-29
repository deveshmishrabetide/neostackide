// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNeoStackWidget.h"
#include "UI/SNeoStackSidebar.h"
#include "UI/SNeoStackHeader.h"
#include "UI/SNeoStackChatInput.h"
#include "UI/SNeoStackChatArea.h"
#include "UI/SNeoStackSettingsPanel.h"
#include "UI/SCollapsibleToolWidget.h"
#include "NeoStackConversation.h"
#include "Tools/NeoStackToolRegistry.h"
#include "NeoStackAPIClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Brushes/SlateColorBrush.h"

#define LOCTEXT_NAMESPACE "SNeoStackWidget"

void SNeoStackWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(MainOverlay, SOverlay)

		// Main content
		+ SOverlay::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(1.0f)
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SAssignNew(Sidebar, SNeoStackSidebar)
				.OnSettingsClicked(this, &SNeoStackWidget::OnSettingsClicked)
				.OnNewChat(this, &SNeoStackWidget::OnNewChat)
				.OnConversationSelected(this, &SNeoStackWidget::OnConversationSelected)
			]
			+ SSplitter::Slot()
			.Value(0.8f)
			[
				SNew(SVerticalBox)
				// Header (fixed at top)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SNeoStackHeader)
				]
				// Chat area (scrollable, fills space)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(ChatArea, SNeoStackChatArea)
					.OnToolApproved(this, &SNeoStackWidget::OnToolApproved)
					.OnToolRejected(this, &SNeoStackWidget::OnToolRejected)
				]
				// Input area (fixed at bottom)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ChatInput, SNeoStackChatInput)
					.Sidebar(Sidebar)
					.ChatArea(ChatArea)
				]
			]
		]
	];
}

void SNeoStackWidget::OnSettingsClicked()
{
	if (!SettingsPanel.IsValid())
	{
		// Add settings panel overlay
		MainOverlay->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(SettingsOverlayContainer, SBox)
			.Padding(0.0f)
			[
				SNew(SBorder)
				.BorderImage(new FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))) // Semi-transparent backdrop
				.Padding(0.0f)
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.WidthOverride(600.0f)
					.HeightOverride(500.0f)
					[
						SAssignNew(SettingsPanel, SNeoStackSettingsPanel)
						.OnClose(this, &SNeoStackWidget::OnSettingsClosed)
					]
				]
			]
		];
	}
}

void SNeoStackWidget::OnSettingsClosed()
{
	if (SettingsOverlayContainer.IsValid())
	{
		// Remove the entire settings overlay (backdrop + panel)
		MainOverlay->RemoveSlot(SettingsOverlayContainer.ToSharedRef());
		SettingsOverlayContainer.Reset();
		SettingsPanel.Reset();
	}
}

void SNeoStackWidget::OnNewChat()
{
	// Clear the chat area
	if (ChatArea.IsValid())
	{
		ChatArea->ClearMessages();
	}

	// Refresh sidebar to update conversation list
	if (Sidebar.IsValid())
	{
		Sidebar->RefreshConversationsList();
	}
}

void SNeoStackWidget::OnConversationSelected(int32 ConversationID)
{
	LoadConversationIntoChat(ConversationID);
}

void SNeoStackWidget::LoadConversationIntoChat(int32 ConversationID)
{
	if (!ChatArea.IsValid())
	{
		return;
	}

	// Clear current messages
	ChatArea->ClearMessages();

	// Get messages from conversation manager
	FNeoStackConversationManager& ConvMgr = FNeoStackConversationManager::Get();
	const TArray<FConversationMessage>& Messages = ConvMgr.GetCurrentMessages();

	// Replay messages into chat area
	for (const FConversationMessage& Msg : Messages)
	{
		if (Msg.Role == TEXT("user"))
		{
			ChatArea->AddUserMessageWithImages(Msg.Content, Msg.Images);
		}
		else if (Msg.Role == TEXT("assistant"))
		{
			// Start assistant message (use generic names since we don't track agent/model per message)
			ChatArea->StartAssistantMessage(TEXT("Assistant"), TEXT(""));

			// Add content
			if (!Msg.Content.IsEmpty())
			{
				ChatArea->AppendContent(Msg.Content);
			}

			// Add tool calls (display as completed)
			for (const FConversationToolCall& TC : Msg.ToolCalls)
			{
				ChatArea->AppendToolCall(TC.Name, TC.Arguments, TC.ID);
			}

			ChatArea->CompleteAssistantMessage();
		}
		else if (Msg.Role == TEXT("tool"))
		{
			// Tool results should be paired with their tool calls in the UI
			// The tool call widget handles this, so we just update the result
			ChatArea->AppendToolResult(Msg.ToolCallID, Msg.Content);
		}
	}
}

void SNeoStackWidget::OnToolApproved(const FString& CallID, const FString& ToolName, const FString& Args)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack Widget] Tool approved - CallID: %s, Tool: %s"), *CallID, *ToolName);

	if (!ChatArea.IsValid())
	{
		return;
	}

	// Get the session ID for this tool call
	FString SessionID = ChatArea->GetSessionIDForTool(CallID);
	if (SessionID.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStack Widget] No session ID found for CallID: %s"), *CallID);
		return;
	}

	// Execute the tool via registry
	FToolResult Result = FNeoStackToolRegistry::Get().Execute(ToolName, Args);

	// Update the tool widget with the result
	TSharedPtr<SCollapsibleToolWidget> ToolWidget = ChatArea->GetToolWidget(CallID);
	if (ToolWidget.IsValid())
	{
		ToolWidget->SetResult(Result.Output, Result.bSuccess);
	}

	// Submit result to backend (plain text output)
	FString ResultString = Result.Output;
	FNeoStackAPIClient::SubmitToolResult(SessionID, CallID, ResultString);

	UE_LOG(LogTemp, Log, TEXT("[NeoStack Widget] Tool result submitted - Success: %d"), Result.bSuccess);
}

void SNeoStackWidget::OnToolRejected(const FString& CallID)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack Widget] Tool rejected - CallID: %s"), *CallID);

	if (!ChatArea.IsValid())
	{
		return;
	}

	// Get the session ID for this tool call
	FString SessionID = ChatArea->GetSessionIDForTool(CallID);
	if (SessionID.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStack Widget] No session ID found for CallID: %s"), *CallID);
		return;
	}

	// Submit rejection to backend
	FString RejectionResult = TEXT("{\"error\": \"Tool execution rejected by user\"}");
	FNeoStackAPIClient::SubmitToolResult(SessionID, CallID, RejectionResult);
}

#undef LOCTEXT_NAMESPACE
