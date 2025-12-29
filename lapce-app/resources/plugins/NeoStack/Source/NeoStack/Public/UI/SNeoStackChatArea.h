// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Message part types
 */
enum class EMessagePartType : uint8
{
	Content,        // Normal text content
	Reasoning,      // Thinking/reasoning (muted style)
	ToolCall,       // Tool execution request
	ToolResult      // Tool execution result
};

/**
 * Individual part within a message
 */
struct FMessagePart
{
	EMessagePartType Type;
	FString Text;
	FString ToolName;        // For tool calls
	FString ToolArgs;        // For tool calls
	FString CallID;          // For matching calls with results
	bool bIsWaiting;         // For pending tool results

	FMessagePart()
		: Type(EMessagePartType::Content)
		, bIsWaiting(false)
	{}
};

/** Delegate for UE5 tool execution - returns result to be sent back to backend */
DECLARE_DELEGATE_ThreeParams(FOnUE5ToolApproved, const FString& /* CallID */, const FString& /* ToolName */, const FString& /* Args */);
DECLARE_DELEGATE_OneParam(FOnUE5ToolRejected, const FString& /* CallID */);

/**
 * Chat area widget that displays conversation messages
 * Supports streaming with reasoning, content, and tool execution
 */
class SNeoStackChatArea : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackChatArea)
		{}
		SLATE_EVENT(FOnUE5ToolApproved, OnToolApproved)
		SLATE_EVENT(FOnUE5ToolRejected, OnToolRejected)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Add a user message to the chat */
	void AddUserMessage(const FString& Message);

	/** Add a user message with images to the chat */
	void AddUserMessageWithImages(const FString& Message, const TArray<struct FConversationImage>& Images);

	/** Start a new assistant message */
	void StartAssistantMessage(const FString& AgentName, const FString& ModelName);

	/** Append content to current assistant message */
	void AppendContent(const FString& Content);

	/** Append reasoning to current assistant message */
	void AppendReasoning(const FString& Reasoning);

	/** Append tool call to current assistant message (backend tool, no session needed) */
	void AppendToolCall(const FString& ToolName, const FString& Args, const FString& CallID);

	/** Append UE5 tool call with session ID for result submission */
	void AppendUE5ToolCall(const FString& SessionID, const FString& ToolName, const FString& Args, const FString& CallID);

	/** Append tool result to current assistant message */
	void AppendToolResult(const FString& CallID, const FString& Result);

	/** Mark current assistant message as complete */
	void CompleteAssistantMessage();

	/** Clear all messages */
	void ClearMessages();

	/** Get tool widget by CallID for external updates */
	TSharedPtr<class SCollapsibleToolWidget> GetToolWidget(const FString& CallID) const;

	/** Get session ID for a tool call */
	FString GetSessionIDForTool(const FString& CallID) const;

private:
	/** Container for all messages */
	TSharedPtr<class SVerticalBox> MessageContainer;

	/** Scroll box for messages */
	TSharedPtr<class SScrollBox> MessageScrollBox;

	/** Current assistant message container (for appending parts) */
	TSharedPtr<SVerticalBox> CurrentAssistantContainer;

	/** Track if we're currently in an assistant message */
	bool bInAssistantMessage;

	/** Current assistant agent name */
	FString CurrentAgentName;

	/** Current assistant model name */
	FString CurrentModelName;

	/** Current streaming content text block (for live updates) */
	TSharedPtr<class SRichTextBlock> CurrentStreamingTextBlock;

	/** Accumulated content for current streaming block */
	FString CurrentStreamingContent;

	/** Current streaming reasoning widget (for live updates) */
	TSharedPtr<class SCollapsibleReasoningWidget> CurrentStreamingReasoningWidget;

	/** Accumulated reasoning for current streaming block */
	FString CurrentStreamingReasoning;

	/** Map of Call ID to Tool Widget (for updating with results) */
	TMap<FString, TSharedPtr<class SCollapsibleToolWidget>> ToolWidgets;

	/** Map of Call ID to Tool Info (for executing after approval) */
	TMap<FString, TPair<FString, FString>> PendingToolCalls; // CallID -> (ToolName, Args)

	/** Map of Call ID to Session ID (for UE5 tools that need result submission) */
	TMap<FString, FString> ToolSessionIDs; // CallID -> SessionID

	/** Delegates for tool approval/rejection */
	FOnUE5ToolApproved OnToolApprovedDelegate;
	FOnUE5ToolRejected OnToolRejectedDelegate;

	/** Persistent storage for image brushes (prevent GC while displayed) */
	TArray<TSharedPtr<FSlateBrush>> ImageBrushes;

	/** Persistent storage for image textures (prevent GC while displayed) */
	TArray<TStrongObjectPtr<class UTexture2D>> ImageTextures;

	/** Create a user message widget */
	TSharedRef<SWidget> CreateUserMessageWidget(const FString& Message, const TArray<struct FConversationImage>& Images = TArray<struct FConversationImage>());

	/** Create assistant message header widget */
	TSharedRef<SWidget> CreateAssistantHeaderWidget(const FString& AgentName, const FString& ModelName);

	/** Create a content part widget */
	TSharedRef<SWidget> CreateContentWidget(const FString& Content);

	/** Create a reasoning part widget */
	TSharedRef<SWidget> CreateReasoningWidget(const FString& Reasoning);

	/** Create a tool call widget */
	TSharedRef<SWidget> CreateToolCallWidget(const FString& ToolName, const FString& Args, const FString& CallID);

	/** Create a tool result widget */
	TSharedRef<SWidget> CreateToolResultWidget(const FString& Result);

	/** Parse markdown and create rich text widget */
	TSharedRef<SWidget> CreateMarkdownWidget(const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color);

	/** Scroll to bottom of chat */
	void ScrollToBottom();
};
