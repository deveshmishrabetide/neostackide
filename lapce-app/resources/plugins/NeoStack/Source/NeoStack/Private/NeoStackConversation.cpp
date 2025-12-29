// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStackConversation.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

TSharedPtr<FJsonObject> FConversationMessage::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
	JsonObject->SetStringField(TEXT("role"), Role);

	if (!Content.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("content"), Content);
	}

	if (ToolCalls.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ToolCallsArray;
		for (const FConversationToolCall& TC : ToolCalls)
		{
			TSharedPtr<FJsonObject> TCJson = MakeShareable(new FJsonObject());
			TCJson->SetStringField(TEXT("id"), TC.ID);
			TCJson->SetStringField(TEXT("type"), TC.Type);

			TSharedPtr<FJsonObject> FuncJson = MakeShareable(new FJsonObject());
			FuncJson->SetStringField(TEXT("name"), TC.Name);
			FuncJson->SetStringField(TEXT("arguments"), TC.Arguments);
			TCJson->SetObjectField(TEXT("function"), FuncJson);

			ToolCallsArray.Add(MakeShareable(new FJsonValueObject(TCJson)));
		}
		JsonObject->SetArrayField(TEXT("tool_calls"), ToolCallsArray);
	}

	if (!ToolCallID.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("tool_call_id"), ToolCallID);
	}

	if (Images.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ImagesArray;
		for (const FConversationImage& Img : Images)
		{
			TSharedPtr<FJsonObject> ImgJson = MakeShareable(new FJsonObject());
			ImgJson->SetStringField(TEXT("base64"), Img.Base64Data);
			ImgJson->SetStringField(TEXT("mime_type"), Img.MimeType);
			ImagesArray.Add(MakeShareable(new FJsonValueObject(ImgJson)));
		}
		JsonObject->SetArrayField(TEXT("images"), ImagesArray);
	}

	return JsonObject;
}

FConversationMessage FConversationMessage::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FConversationMessage Msg;

	if (!JsonObject.IsValid())
	{
		return Msg;
	}

	JsonObject->TryGetStringField(TEXT("role"), Msg.Role);
	JsonObject->TryGetStringField(TEXT("content"), Msg.Content);
	JsonObject->TryGetStringField(TEXT("tool_call_id"), Msg.ToolCallID);

	const TArray<TSharedPtr<FJsonValue>>* ToolCallsArray;
	if (JsonObject->TryGetArrayField(TEXT("tool_calls"), ToolCallsArray))
	{
		for (const TSharedPtr<FJsonValue>& TCValue : *ToolCallsArray)
		{
			const TSharedPtr<FJsonObject>* TCObj;
			if (TCValue->TryGetObject(TCObj) && TCObj->IsValid())
			{
				FConversationToolCall TC;
				(*TCObj)->TryGetStringField(TEXT("id"), TC.ID);
				(*TCObj)->TryGetStringField(TEXT("type"), TC.Type);

				const TSharedPtr<FJsonObject>* FuncObj;
				if ((*TCObj)->TryGetObjectField(TEXT("function"), FuncObj) && FuncObj->IsValid())
				{
					(*FuncObj)->TryGetStringField(TEXT("name"), TC.Name);
					(*FuncObj)->TryGetStringField(TEXT("arguments"), TC.Arguments);
				}
				Msg.ToolCalls.Add(TC);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ImagesArray;
	if (JsonObject->TryGetArrayField(TEXT("images"), ImagesArray))
	{
		for (const TSharedPtr<FJsonValue>& ImgValue : *ImagesArray)
		{
			const TSharedPtr<FJsonObject>* ImgObj;
			if (ImgValue->TryGetObject(ImgObj) && ImgObj->IsValid())
			{
				FConversationImage Img;
				(*ImgObj)->TryGetStringField(TEXT("base64"), Img.Base64Data);
				(*ImgObj)->TryGetStringField(TEXT("mime_type"), Img.MimeType);
				Msg.Images.Add(Img);
			}
		}
	}

	return Msg;
}

FNeoStackConversationManager& FNeoStackConversationManager::Get()
{
	static FNeoStackConversationManager Instance;
	return Instance;
}

FNeoStackConversationManager::FNeoStackConversationManager()
	: CurrentConversationID(-1)
	, NextID(1)
{
	// Ensure directory exists
	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*GetConversationsDir(), true);

	LoadMetadata();
}

FString FNeoStackConversationManager::GetConversationsDir() const
{
	return FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("Conversations");
}

FString FNeoStackConversationManager::GetConversationFilePath(int32 ConversationID) const
{
	return GetConversationsDir() / FString::Printf(TEXT("conversation_%d.jsonl"), ConversationID);
}

FString FNeoStackConversationManager::GetMetadataFilePath() const
{
	return GetConversationsDir() / TEXT("metadata.json");
}

void FNeoStackConversationManager::LoadMetadata()
{
	FString MetadataPath = GetMetadataFilePath();
	FString MetadataContent;

	if (FFileHelper::LoadFileToString(MetadataContent, *MetadataPath))
	{
		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetadataContent);

		if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
		{
			NextID = RootObject->GetIntegerField(TEXT("next_id"));

			const TArray<TSharedPtr<FJsonValue>>* ConversationsArray;
			if (RootObject->TryGetArrayField(TEXT("conversations"), ConversationsArray))
			{
				for (const TSharedPtr<FJsonValue>& Value : *ConversationsArray)
				{
					const TSharedPtr<FJsonObject>* ConvObj;
					if (Value->TryGetObject(ConvObj) && ConvObj->IsValid())
					{
						FConversationMetadata Meta;
						Meta.ID = (*ConvObj)->GetIntegerField(TEXT("id"));
						Meta.Title = (*ConvObj)->GetStringField(TEXT("title"));
						Meta.MessageCount = (*ConvObj)->GetIntegerField(TEXT("message_count"));

						FString CreatedAtStr, UpdatedAtStr;
						if ((*ConvObj)->TryGetStringField(TEXT("created_at"), CreatedAtStr))
						{
							FDateTime::ParseIso8601(*CreatedAtStr, Meta.CreatedAt);
						}
						if ((*ConvObj)->TryGetStringField(TEXT("updated_at"), UpdatedAtStr))
						{
							FDateTime::ParseIso8601(*UpdatedAtStr, Meta.UpdatedAt);
						}

						AllMetadata.Add(Meta);
					}
				}
			}
		}
	}
}

void FNeoStackConversationManager::SaveMetadata()
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	RootObject->SetNumberField(TEXT("next_id"), NextID);

	TArray<TSharedPtr<FJsonValue>> ConversationsArray;
	for (const FConversationMetadata& Meta : AllMetadata)
	{
		TSharedPtr<FJsonObject> ConvObj = MakeShareable(new FJsonObject());
		ConvObj->SetNumberField(TEXT("id"), Meta.ID);
		ConvObj->SetStringField(TEXT("title"), Meta.Title);
		ConvObj->SetNumberField(TEXT("message_count"), Meta.MessageCount);
		ConvObj->SetStringField(TEXT("created_at"), Meta.CreatedAt.ToIso8601());
		ConvObj->SetStringField(TEXT("updated_at"), Meta.UpdatedAt.ToIso8601());

		ConversationsArray.Add(MakeShareable(new FJsonValueObject(ConvObj)));
	}
	RootObject->SetArrayField(TEXT("conversations"), ConversationsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(OutputString, *GetMetadataFilePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

int32 FNeoStackConversationManager::GenerateNextID()
{
	return NextID++;
}

int32 FNeoStackConversationManager::CreateConversation(const FString& Title)
{
	FConversationMetadata Meta;
	Meta.ID = GenerateNextID();
	Meta.Title = Title;
	Meta.CreatedAt = FDateTime::Now();
	Meta.UpdatedAt = Meta.CreatedAt;
	Meta.MessageCount = 0;

	AllMetadata.Add(Meta);
	SaveMetadata();

	// Set as current
	SetCurrentConversation(Meta.ID);

	return Meta.ID;
}

void FNeoStackConversationManager::SetCurrentConversation(int32 ConversationID)
{
	if (CurrentConversationID != ConversationID)
	{
		CurrentConversationID = ConversationID;
		CurrentMessages.Empty();

		if (ConversationID >= 0)
		{
			// Load messages from file
			CurrentMessages = LoadMessages(ConversationID);
		}
	}
}

TArray<FConversationMetadata> FNeoStackConversationManager::GetAllConversations() const
{
	// Return sorted by updated time (most recent first)
	TArray<FConversationMetadata> Sorted = AllMetadata;
	Sorted.Sort([](const FConversationMetadata& A, const FConversationMetadata& B)
	{
		return A.UpdatedAt > B.UpdatedAt;
	});
	return Sorted;
}

TArray<FConversationMessage> FNeoStackConversationManager::LoadMessages(int32 ConversationID) const
{
	TArray<FConversationMessage> Messages;

	FString FilePath = GetConversationFilePath(ConversationID);
	FString FileContent;

	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TArray<FString> Lines;
		FileContent.ParseIntoArrayLines(Lines);

		for (const FString& Line : Lines)
		{
			if (Line.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				Messages.Add(FConversationMessage::FromJson(JsonObject));
			}
		}
	}

	return Messages;
}

void FNeoStackConversationManager::AppendMessage(const FConversationMessage& Message)
{
	if (CurrentConversationID < 0)
	{
		// Auto-create a conversation if none exists
		FString Title = TEXT("New Conversation");

		// Use first user message as title (truncated)
		if (Message.Role == TEXT("user") && !Message.Content.IsEmpty())
		{
			Title = Message.Content.Left(50);
			if (Message.Content.Len() > 50)
			{
				Title += TEXT("...");
			}
		}

		CreateConversation(Title);
	}

	// Add to in-memory list
	CurrentMessages.Add(Message);

	// Append to file (JSON Lines format - one JSON object per line)
	TSharedPtr<FJsonObject> JsonObject = Message.ToJson();
	FString JsonLine;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonLine);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	JsonLine += TEXT("\n");

	FString FilePath = GetConversationFilePath(CurrentConversationID);
	FFileHelper::SaveStringToFile(JsonLine, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), EFileWrite::FILEWRITE_Append);

	// Update metadata
	for (FConversationMetadata& Meta : AllMetadata)
	{
		if (Meta.ID == CurrentConversationID)
		{
			Meta.MessageCount++;
			Meta.UpdatedAt = FDateTime::Now();

			// Update title from first user message
			if (Meta.MessageCount == 1 && Message.Role == TEXT("user") && !Message.Content.IsEmpty())
			{
				Meta.Title = Message.Content.Left(50);
				if (Message.Content.Len() > 50)
				{
					Meta.Title += TEXT("...");
				}
			}
			break;
		}
	}
	SaveMetadata();
}

void FNeoStackConversationManager::UpdateTitle(int32 ConversationID, const FString& NewTitle)
{
	for (FConversationMetadata& Meta : AllMetadata)
	{
		if (Meta.ID == ConversationID)
		{
			Meta.Title = NewTitle;
			Meta.UpdatedAt = FDateTime::Now();
			SaveMetadata();
			break;
		}
	}
}

void FNeoStackConversationManager::DeleteConversation(int32 ConversationID)
{
	// Remove from metadata
	AllMetadata.RemoveAll([ConversationID](const FConversationMetadata& Meta)
	{
		return Meta.ID == ConversationID;
	});
	SaveMetadata();

	// Delete file
	FString FilePath = GetConversationFilePath(ConversationID);
	IFileManager::Get().Delete(*FilePath);

	// If this was the current conversation, clear it
	if (CurrentConversationID == ConversationID)
	{
		CurrentConversationID = -1;
		CurrentMessages.Empty();
	}
}

void FNeoStackConversationManager::ClearCurrentConversation()
{
	CurrentConversationID = -1;
	CurrentMessages.Empty();
}
