// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Main UI widget for the NeoStack plugin
 */
class SNeoStackWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackWidget)
		{}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

private:
	/** Reference to the sidebar widget */
	TSharedPtr<class SNeoStackSidebar> Sidebar;

	/** Reference to the chat area widget */
	TSharedPtr<class SNeoStackChatArea> ChatArea;

	/** Reference to the chat input widget */
	TSharedPtr<class SNeoStackChatInput> ChatInput;

	/** Reference to the settings panel */
	TSharedPtr<class SNeoStackSettingsPanel> SettingsPanel;

	/** Reference to the overlay for settings */
	TSharedPtr<class SOverlay> MainOverlay;

	/** Reference to the settings overlay container (the full backdrop) */
	TSharedPtr<class SWidget> SettingsOverlayContainer;

	/** Handle settings button click */
	void OnSettingsClicked();

	/** Handle settings panel close */
	void OnSettingsClosed();

	/** Handle new chat button click */
	void OnNewChat();

	/** Handle conversation selection */
	void OnConversationSelected(int32 ConversationID);

	/** Load and display messages from a conversation */
	void LoadConversationIntoChat(int32 ConversationID);

	/** Handle tool approval - execute tool and submit result */
	void OnToolApproved(const FString& CallID, const FString& ToolName, const FString& Args);

	/** Handle tool rejection - submit rejection to backend */
	void OnToolRejected(const FString& CallID);
};
