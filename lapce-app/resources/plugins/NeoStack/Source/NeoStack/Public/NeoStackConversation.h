// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Tool call information for conversation messages
 */
struct FConversationToolCall
{
	FString ID;
	FString Type;      // "function"
	FString Name;      // Function name
	FString Arguments; // JSON string

	FConversationToolCall()
		: Type(TEXT("function"))
	{}
};

/**
 * Image data for conversation messages
 */
struct FConversationImage
{
	FString Base64Data;   // Base64 encoded PNG data
	FString MimeType;     // e.g., "image/png"

	FConversationImage()
		: MimeType(TEXT("image/png"))
	{}
};

/**
 * A single message in a conversation
 */
struct FConversationMessage
{
	FString Role;      // "user", "assistant", "tool"
	FString Content;
	TArray<FConversationToolCall> ToolCalls;  // For assistant messages with tool calls
	FString ToolCallID;                        // For tool response messages
	TArray<FConversationImage> Images;         // For messages with images

	FConversationMessage()
	{}

	FConversationMessage(const FString& InRole, const FString& InContent)
		: Role(InRole)
		, Content(InContent)
	{}

	/** Create a user message */
	static FConversationMessage User(const FString& Content)
	{
		return FConversationMessage(TEXT("user"), Content);
	}

	/** Create a user message with images */
	static FConversationMessage UserWithImages(const FString& Content, const TArray<FConversationImage>& InImages)
	{
		FConversationMessage Msg(TEXT("user"), Content);
		Msg.Images = InImages;
		return Msg;
	}

	/** Create an assistant message */
	static FConversationMessage Assistant(const FString& Content)
	{
		return FConversationMessage(TEXT("assistant"), Content);
	}

	/** Create a tool result message */
	static FConversationMessage Tool(const FString& CallID, const FString& Result)
	{
		FConversationMessage Msg;
		Msg.Role = TEXT("tool");
		Msg.Content = Result;
		Msg.ToolCallID = CallID;
		return Msg;
	}

	/** Convert to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Parse from JSON object */
	static FConversationMessage FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * Metadata for a conversation
 */
struct FConversationMetadata
{
	int32 ID;
	FString Title;
	FDateTime CreatedAt;
	FDateTime UpdatedAt;
	int32 MessageCount;

	FConversationMetadata()
		: ID(-1)
		, MessageCount(0)
	{
		CreatedAt = FDateTime::Now();
		UpdatedAt = CreatedAt;
	}
};

/**
 * Manager for conversation persistence
 * Uses JSON Lines format for crash-safe storage
 */
class NEOSTACK_API FNeoStackConversationManager
{
public:
	/** Get the singleton instance */
	static FNeoStackConversationManager& Get();

	/** Create a new conversation, returns conversation ID */
	int32 CreateConversation(const FString& Title = TEXT("New Conversation"));

	/** Get the current conversation ID (-1 if none) */
	int32 GetCurrentConversationID() const { return CurrentConversationID; }

	/** Set the current conversation */
	void SetCurrentConversation(int32 ConversationID);

	/** Get all conversation metadata */
	TArray<FConversationMetadata> GetAllConversations() const;

	/** Load messages for a conversation */
	TArray<FConversationMessage> LoadMessages(int32 ConversationID) const;

	/** Append a message to the current conversation (crash-safe) */
	void AppendMessage(const FConversationMessage& Message);

	/** Update the title of a conversation */
	void UpdateTitle(int32 ConversationID, const FString& NewTitle);

	/** Delete a conversation */
	void DeleteConversation(int32 ConversationID);

	/** Get the messages for the current conversation */
	const TArray<FConversationMessage>& GetCurrentMessages() const { return CurrentMessages; }

	/** Clear current conversation messages (for new chat) */
	void ClearCurrentConversation();

private:
	FNeoStackConversationManager();

	/** Get the base directory for conversations */
	FString GetConversationsDir() const;

	/** Get the file path for a conversation */
	FString GetConversationFilePath(int32 ConversationID) const;

	/** Get the metadata file path */
	FString GetMetadataFilePath() const;

	/** Load metadata from disk */
	void LoadMetadata();

	/** Save metadata to disk */
	void SaveMetadata();

	/** Generate next conversation ID */
	int32 GenerateNextID();

	/** Current conversation ID */
	int32 CurrentConversationID;

	/** Current conversation messages (in memory) */
	TArray<FConversationMessage> CurrentMessages;

	/** All conversation metadata */
	TArray<FConversationMetadata> AllMetadata;

	/** Next available ID */
	int32 NextID;
};
