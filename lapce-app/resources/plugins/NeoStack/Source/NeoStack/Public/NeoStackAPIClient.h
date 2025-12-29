// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Http.h"

// Forward declarations
struct FConversationMessage;
struct FAttachedImage;

/**
 * Delegate for content event
 */
DECLARE_DELEGATE_OneParam(FOnAIContent, const FString& /* Content */);

/**
 * Delegate for reasoning event
 */
DECLARE_DELEGATE_OneParam(FOnAIReasoning, const FString& /* Reasoning */);

/**
 * Delegate for backend tool call event (tools executed by backend)
 */
DECLARE_DELEGATE_ThreeParams(FOnAIToolCall, const FString& /* ToolName */, const FString& /* Args */, const FString& /* CallID */);

/**
 * Delegate for UE5 tool call event (tools that need execution in UE5)
 */
DECLARE_DELEGATE_FourParams(FOnAIUE5ToolCall, const FString& /* SessionID */, const FString& /* ToolName */, const FString& /* Args */, const FString& /* CallID */);

/**
 * Delegate for tool result event
 */
DECLARE_DELEGATE_TwoParams(FOnAIToolResult, const FString& /* CallID */, const FString& /* Result */);

/**
 * Delegate for complete response received
 */
DECLARE_DELEGATE(FOnAIComplete);

/**
 * Delegate for cost update event
 */
DECLARE_DELEGATE_OneParam(FOnAICost, float /* Cost */);

/**
 * Delegate for API error callback
 */
DECLARE_DELEGATE_OneParam(FOnAPIError, const FString& /* ErrorMessage */);

/**
 * API client for communicating with NeoStack backend
 */
class NEOSTACK_API FNeoStackAPIClient
{
public:
	/**
	 * Send a message to the AI endpoint with streaming support
	 * @param Message - The user's message/prompt
	 * @param AgentName - Selected agent name (e.g., "orchestrator")
	 * @param ModelID - Model identifier (e.g., "x-ai/grok-4.1-fast")
	 * @param OnContent - Callback for content events
	 * @param OnReasoning - Callback for reasoning events
	 * @param OnToolCall - Callback for backend tool call events
	 * @param OnUE5ToolCall - Callback for UE5 tool call events (tools to execute in engine)
	 * @param OnToolResult - Callback for tool result events
	 * @param OnComplete - Callback when streaming is complete
	 * @param OnCost - Callback for cost updates
	 * @param OnError - Callback when error occurs
	 */
	static void SendMessage(
		const FString& Message,
		const FString& AgentName,
		const FString& ModelID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost,
		FOnAPIError OnError
	);

	/**
	 * Send a message with conversation history to the AI endpoint
	 * @param Message - The user's message/prompt
	 * @param History - Previous messages in the conversation
	 * @param AgentName - Selected agent name (e.g., "orchestrator")
	 * @param ModelID - Model identifier (e.g., "x-ai/grok-4.1-fast")
	 * @param OnContent - Callback for content events
	 * @param OnReasoning - Callback for reasoning events
	 * @param OnToolCall - Callback for backend tool call events
	 * @param OnUE5ToolCall - Callback for UE5 tool call events (tools to execute in engine)
	 * @param OnToolResult - Callback for tool result events
	 * @param OnComplete - Callback when streaming is complete
	 * @param OnCost - Callback for cost updates
	 * @param OnError - Callback when error occurs
	 */
	static void SendMessageWithHistory(
		const FString& Message,
		const TArray<FConversationMessage>& History,
		const FString& AgentName,
		const FString& ModelID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost,
		FOnAPIError OnError
	);

	/**
	 * Send a message with images and conversation history to the AI endpoint
	 * @param Message - The user's message/prompt
	 * @param Images - Attached images (base64-encoded)
	 * @param History - Previous messages in the conversation
	 * @param AgentName - Selected agent name (e.g., "orchestrator")
	 * @param ModelID - Model identifier (e.g., "x-ai/grok-4.1-fast")
	 * @param OnContent - Callback for content events
	 * @param OnReasoning - Callback for reasoning events
	 * @param OnToolCall - Callback for backend tool call events
	 * @param OnUE5ToolCall - Callback for UE5 tool call events (tools to execute in engine)
	 * @param OnToolResult - Callback for tool result events
	 * @param OnComplete - Callback when streaming is complete
	 * @param OnCost - Callback for cost updates
	 * @param OnError - Callback when error occurs
	 */
	static void SendMessageWithImages(
		const FString& Message,
		const TArray<FAttachedImage>& Images,
		const TArray<FConversationMessage>& History,
		const FString& AgentName,
		const FString& ModelID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost,
		FOnAPIError OnError
	);

	/**
	 * Submit a tool result back to the backend
	 * @param SessionID - The session ID from the tool call
	 * @param CallID - The tool call ID
	 * @param Result - The result of the tool execution (JSON string)
	 */
	static void SubmitToolResult(
		const FString& SessionID,
		const FString& CallID,
		const FString& Result
	);

private:
	/** Buffer for accumulating partial SSE data */
	static FString LastProcessedContent;

	/** Current session ID for tool callbacks */
	static FString CurrentSessionID;

	/** Parse SSE event and call appropriate delegate */
	static void ParseSSEEvent(
		const FString& EventData,
		FString SessionID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost
	);

	/** Handle HTTP response with streaming */
	static void OnResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString SessionID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost,
		FOnAPIError OnError
	);

	/** Handle streaming progress */
	static void OnRequestProgress(
		FHttpRequestPtr Request,
		uint64 BytesSent,
		uint64 BytesReceived,
		FString SessionID,
		FOnAIContent OnContent,
		FOnAIReasoning OnReasoning,
		FOnAIToolCall OnToolCall,
		FOnAIUE5ToolCall OnUE5ToolCall,
		FOnAIToolResult OnToolResult,
		FOnAIComplete OnComplete,
		FOnAICost OnCost
	);
};
