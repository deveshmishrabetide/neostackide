// Copyright NeoStack. All Rights Reserved.

#include "NeoStackBridgeProtocol.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

FString FNeoStackPresenceMessage::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	JsonObject->SetNumberField(TEXT("version"), Version);
	JsonObject->SetStringField(TEXT("type"), Type);
	JsonObject->SetStringField(TEXT("projectId"), ProjectId);
	JsonObject->SetStringField(TEXT("projectPath"), ProjectPath);
	JsonObject->SetStringField(TEXT("projectName"), ProjectName);
	JsonObject->SetNumberField(TEXT("wsPort"), WSPort);
	JsonObject->SetStringField(TEXT("engineVersion"), EngineVersion);
	JsonObject->SetNumberField(TEXT("pid"), ProcessId);
	JsonObject->SetStringField(TEXT("streamUrl"), StreamUrl);
	JsonObject->SetBoolField(TEXT("isStreaming"), bIsStreaming);
	if (!NeoStackConn.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("neostackConn"), NeoStackConn);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}

bool FNeoStackPresenceMessage::FromJson(const FString& JsonString, FNeoStackPresenceMessage& OutMessage)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	OutMessage.Version = JsonObject->GetIntegerField(TEXT("version"));
	OutMessage.Type = JsonObject->GetStringField(TEXT("type"));
	OutMessage.ProjectId = JsonObject->GetStringField(TEXT("projectId"));
	OutMessage.ProjectPath = JsonObject->GetStringField(TEXT("projectPath"));
	OutMessage.ProjectName = JsonObject->GetStringField(TEXT("projectName"));
	OutMessage.WSPort = JsonObject->GetIntegerField(TEXT("wsPort"));
	OutMessage.EngineVersion = JsonObject->GetStringField(TEXT("engineVersion"));
	OutMessage.ProcessId = JsonObject->GetIntegerField(TEXT("pid"));
	OutMessage.StreamUrl = JsonObject->GetStringField(TEXT("streamUrl"));
	OutMessage.bIsStreaming = JsonObject->GetBoolField(TEXT("isStreaming"));
	JsonObject->TryGetStringField(TEXT("neostackConn"), OutMessage.NeoStackConn);

	return true;
}

bool FNeoStackCommand::FromJson(const FString& JsonString, FNeoStackCommand& OutCommand)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	OutCommand.Command = JsonObject->GetStringField(TEXT("cmd"));
	OutCommand.RequestId = JsonObject->GetStringField(TEXT("requestId"));

	const TSharedPtr<FJsonObject>* ArgsObject;
	if (JsonObject->TryGetObjectField(TEXT("args"), ArgsObject))
	{
		OutCommand.Args = *ArgsObject;
	}

	return true;
}

FString FNeoStackEvent::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	JsonObject->SetStringField(TEXT("event"), Event);
	JsonObject->SetBoolField(TEXT("success"), bSuccess);

	if (!RequestId.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("requestId"), RequestId);
	}

	if (!Error.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("error"), Error);
	}

	if (Data.IsValid())
	{
		JsonObject->SetObjectField(TEXT("data"), Data);
	}

	FString OutputString;
	// Use condensed writer (no newlines) for TCP protocol - read_line expects single line
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}
