// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStackAPIClient.h"
#include "NeoStackSettings.h"
#include "NeoStackConversation.h"
#include "UI/SNeoStackChatInput.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "JsonUtilities.h"

// Static buffer for tracking processed content
FString FNeoStackAPIClient::LastProcessedContent = TEXT("");
FString FNeoStackAPIClient::CurrentSessionID = TEXT("");

void FNeoStackAPIClient::SendMessage(
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
	FOnAPIError OnError)
{
	// Delegate to SendMessageWithHistory with empty history
	TArray<FConversationMessage> EmptyHistory;
	SendMessageWithHistory(Message, EmptyHistory, AgentName, ModelID,
		OnContent, OnReasoning, OnToolCall, OnUE5ToolCall, OnToolResult, OnComplete, OnCost, OnError);
}

void FNeoStackAPIClient::SendMessageWithHistory(
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
	FOnAPIError OnError)
{
	// Reset buffer for new request
	LastProcessedContent.Empty();

	// Generate a unique session ID for this request
	CurrentSessionID = FGuid::NewGuid().ToString();

	// Get settings
	const UNeoStackSettings* Settings = UNeoStackSettings::Get();
	if (!Settings)
	{
		OnError.ExecuteIfBound(TEXT("Failed to get NeoStack settings"));
		return;
	}

	// Validate API key
	if (Settings->APIKey.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("API Key not configured. Please set it in Project Settings > Game > NeoStack"));
		return;
	}

	// Validate backend URL
	if (Settings->BackendURL.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("Backend URL not configured"));
		return;
	}

	// Create HTTP request
	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = HttpModule.CreateRequest();

	// Build JSON payload
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
	JsonObject->SetStringField(TEXT("prompt"), Message);
	JsonObject->SetStringField(TEXT("agent"), AgentName);
	JsonObject->SetStringField(TEXT("model"), ModelID);
	JsonObject->SetStringField(TEXT("session_id"), CurrentSessionID);

	// Add conversation history if present
	if (History.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		for (const FConversationMessage& Msg : History)
		{
			MessagesArray.Add(MakeShareable(new FJsonValueObject(Msg.ToJson())));
		}
		JsonObject->SetArrayField(TEXT("messages"), MessagesArray);
	}

	// Load and add runtime settings
	FString SettingsFilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
	FString SettingsContent;
	if (FFileHelper::LoadFileToString(SettingsContent, *SettingsFilePath))
	{
		TSharedPtr<FJsonObject> SettingsJsonObject;
		TSharedRef<TJsonReader<>> SettingsReader = TJsonReaderFactory<>::Create(SettingsContent);

		if (FJsonSerializer::Deserialize(SettingsReader, SettingsJsonObject) && SettingsJsonObject.IsValid())
		{
			// Add settings to request
			TSharedPtr<FJsonObject> SettingsObject = MakeShareable(new FJsonObject());

			// Max cost per query
			double MaxCostPerQuery = 0.0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxCostPerQuery"), MaxCostPerQuery) && MaxCostPerQuery > 0.0)
			{
				SettingsObject->SetNumberField(TEXT("max_cost_per_query"), MaxCostPerQuery);
			}

			// Max tokens
			int32 MaxTokens = 0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxTokens"), MaxTokens) && MaxTokens > 0)
			{
				SettingsObject->SetNumberField(TEXT("max_tokens"), MaxTokens);
			}

			// Enable thinking
			bool bEnableThinking = false;
			if (SettingsJsonObject->TryGetBoolField(TEXT("EnableThinking"), bEnableThinking))
			{
				SettingsObject->SetBoolField(TEXT("enable_thinking"), bEnableThinking);
			}

			// Max thinking tokens
			int32 MaxThinkingTokens = 0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxThinkingTokens"), MaxThinkingTokens) && MaxThinkingTokens > 0)
			{
				SettingsObject->SetNumberField(TEXT("max_thinking_tokens"), MaxThinkingTokens);
			}

			// Reasoning effort
			FString ReasoningEffort;
			if (SettingsJsonObject->TryGetStringField(TEXT("ReasoningEffort"), ReasoningEffort) && !ReasoningEffort.IsEmpty())
			{
				SettingsObject->SetStringField(TEXT("reasoning_effort"), ReasoningEffort);
			}

			// Provider routing - load from per-model preferences
			const TSharedPtr<FJsonObject>* RoutingObj;
			if (SettingsJsonObject->TryGetObjectField(TEXT("ProviderRouting"), RoutingObj) && RoutingObj->IsValid())
			{
				// Look up routing for the current model
				const TSharedPtr<FJsonObject>* ModelRoutingObj;
				if ((*RoutingObj)->TryGetObjectField(ModelID, ModelRoutingObj) && ModelRoutingObj->IsValid())
				{
					TSharedPtr<FJsonObject> ProviderRoutingObject = MakeShareable(new FJsonObject());

					FString Provider;
					if ((*ModelRoutingObj)->TryGetStringField(TEXT("provider"), Provider))
					{
						ProviderRoutingObject->SetStringField(TEXT("provider"), Provider);
					}

					FString SortBy;
					if ((*ModelRoutingObj)->TryGetStringField(TEXT("sort_by"), SortBy))
					{
						ProviderRoutingObject->SetStringField(TEXT("sort_by"), SortBy);
					}

					bool bAllowFallbacks = true;
					if ((*ModelRoutingObj)->TryGetBoolField(TEXT("allow_fallbacks"), bAllowFallbacks))
					{
						ProviderRoutingObject->SetBoolField(TEXT("allow_fallbacks"), bAllowFallbacks);
					}

					SettingsObject->SetObjectField(TEXT("provider_routing"), ProviderRoutingObject);
				}
			}

			JsonObject->SetObjectField(TEXT("settings"), SettingsObject);
		}
	}

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Configure request
	FString URL = Settings->BackendURL + TEXT("/ai");
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-API-Key"), Settings->APIKey);
	Request->SetContentAsString(RequestBody);

	// Capture session ID for callbacks
	FString SessionID = CurrentSessionID;

	// Bind response callback
	Request->OnProcessRequestComplete().BindStatic(
		&FNeoStackAPIClient::OnResponseReceived,
		SessionID,
		OnContent,
		OnReasoning,
		OnToolCall,
		OnUE5ToolCall,
		OnToolResult,
		OnComplete,
		OnCost,
		OnError
	);

	// Bind progress callback for streaming
	Request->OnRequestProgress64().BindStatic(
		&FNeoStackAPIClient::OnRequestProgress,
		SessionID,
		OnContent,
		OnReasoning,
		OnToolCall,
		OnUE5ToolCall,
		OnToolResult,
		OnComplete,
		OnCost
	);

	// Send request
	if (!Request->ProcessRequest())
	{
		OnError.ExecuteIfBound(TEXT("Failed to send HTTP request"));
	}
}

void FNeoStackAPIClient::ParseSSEEvent(
	const FString& EventData,
	FString SessionID,
	FOnAIContent OnContent,
	FOnAIReasoning OnReasoning,
	FOnAIToolCall OnToolCall,
	FOnAIUE5ToolCall OnUE5ToolCall,
	FOnAIToolResult OnToolResult,
	FOnAIComplete OnComplete,
	FOnAICost OnCost)
{
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Raw event data: %s"), *EventData);

	// SSE format: "data: {json}\n\n"
	TArray<FString> Lines;
	EventData.ParseIntoArray(Lines, TEXT("\n"));

	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("data: ")))
		{
			FString JsonString = Line.RightChop(6); // Remove "data: " prefix
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] Parsed JSON: %s"), *JsonString);

			// Parse JSON
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				FString Type;
				if (JsonObject->TryGetStringField(TEXT("type"), Type))
				{
					UE_LOG(LogTemp, Log, TEXT("[NeoStack] Event type: %s"), *Type);
					if (Type == TEXT("content"))
					{
						FString Content;
						if (JsonObject->TryGetStringField(TEXT("content"), Content))
						{
							OnContent.ExecuteIfBound(Content);
						}
					}
					else if (Type == TEXT("reasoning"))
					{
						FString Reasoning;
						if (JsonObject->TryGetStringField(TEXT("reasoning"), Reasoning))
						{
							OnReasoning.ExecuteIfBound(Reasoning);
						}
					}
					else if (Type == TEXT("tool_call_backend"))
					{
						FString ToolName;
						FString CallID;
						JsonObject->TryGetStringField(TEXT("tool"), ToolName);
						JsonObject->TryGetStringField(TEXT("call_id"), CallID);

						UE_LOG(LogTemp, Log, TEXT("[NeoStack] Backend tool call - Name: %s, CallID: %s"), *ToolName, *CallID);

						// Convert args object to string
						FString ArgsString;
						const TSharedPtr<FJsonObject>* ArgsObject;
						if (JsonObject->TryGetObjectField(TEXT("args"), ArgsObject))
						{
							TSharedRef<TJsonWriter<>> ArgsWriter = TJsonWriterFactory<>::Create(&ArgsString);
							FJsonSerializer::Serialize((*ArgsObject).ToSharedRef(), ArgsWriter);
							UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool args: %s"), *ArgsString);
						}

						OnToolCall.ExecuteIfBound(ToolName, ArgsString, CallID);
					}
					else if (Type == TEXT("tool_call_ue5"))
					{
						FString ToolName;
						FString CallID;
						JsonObject->TryGetStringField(TEXT("tool"), ToolName);
						JsonObject->TryGetStringField(TEXT("call_id"), CallID);

						UE_LOG(LogTemp, Log, TEXT("[NeoStack] UE5 tool call - Name: %s, CallID: %s, SessionID: %s"), *ToolName, *CallID, *SessionID);

						// Convert args object to string
						FString ArgsString;
						const TSharedPtr<FJsonObject>* ArgsObject;
						if (JsonObject->TryGetObjectField(TEXT("args"), ArgsObject))
						{
							TSharedRef<TJsonWriter<>> ArgsWriter = TJsonWriterFactory<>::Create(&ArgsString);
							FJsonSerializer::Serialize((*ArgsObject).ToSharedRef(), ArgsWriter);
							UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool args: %s"), *ArgsString);
						}

						// UE5 tools get the session ID for result submission
						OnUE5ToolCall.ExecuteIfBound(SessionID, ToolName, ArgsString, CallID);
					}
					else if (Type == TEXT("tool_result"))
					{
						FString CallID;
						FString Result;
						JsonObject->TryGetStringField(TEXT("call_id"), CallID);
						JsonObject->TryGetStringField(TEXT("result"), Result);

						UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool result - CallID: %s, Result: %s"), *CallID, *Result);
						OnToolResult.ExecuteIfBound(CallID, Result);
					}
					else if (Type == TEXT("cost"))
					{
						double Cost = 0.0;
						if (JsonObject->TryGetNumberField(TEXT("cost"), Cost))
						{
							UE_LOG(LogTemp, Log, TEXT("[NeoStack] Cost update: $%.6f"), Cost);
							OnCost.ExecuteIfBound(static_cast<float>(Cost));
						}
					}
					else if (Type == TEXT("final"))
					{
						UE_LOG(LogTemp, Log, TEXT("[NeoStack] Stream complete"));
						OnComplete.ExecuteIfBound();
					}
					else if (Type == TEXT("error"))
					{
						// Handle error in stream
						FString ErrorMsg;
						if (JsonObject->TryGetStringField(TEXT("content"), ErrorMsg))
						{
							UE_LOG(LogTemp, Error, TEXT("[NeoStack] Stream error: %s"), *ErrorMsg);
						}
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[NeoStack] Failed to deserialize JSON: %s"), *JsonString);
			}
		}
	}
}

void FNeoStackAPIClient::OnRequestProgress(
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
	FOnAICost OnCost)
{
	// Get partial response for streaming
	if (Request.IsValid())
	{
		FHttpResponsePtr Response = Request->GetResponse();
		if (Response.IsValid())
		{
			FString FullContent = Response->GetContentAsString();

			// Only process new content
			if (FullContent.Len() > LastProcessedContent.Len())
			{
				FString NewContent = FullContent.RightChop(LastProcessedContent.Len());

				// Parse SSE events
				ParseSSEEvent(NewContent, SessionID, OnContent, OnReasoning, OnToolCall, OnUE5ToolCall, OnToolResult, OnComplete, OnCost);

				// Update processed content
				LastProcessedContent = FullContent;
			}
		}
	}
}

void FNeoStackAPIClient::OnResponseReceived(
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
	FOnAPIError OnError)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		OnError.ExecuteIfBound(TEXT("Request failed or invalid response"));
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode != 200)
	{
		FString ErrorMsg = FString::Printf(
			TEXT("Server error: %d - %s"),
			ResponseCode,
			*Response->GetContentAsString()
		);
		OnError.ExecuteIfBound(ErrorMsg);
		return;
	}

	// Final processing of any remaining content
	// Note: OnComplete is called by ParseSSEEvent when it receives the "final" event
	FString FullContent = Response->GetContentAsString();
	if (FullContent.Len() > LastProcessedContent.Len())
	{
		FString NewContent = FullContent.RightChop(LastProcessedContent.Len());
		ParseSSEEvent(NewContent, SessionID, OnContent, OnReasoning, OnToolCall, OnUE5ToolCall, OnToolResult, OnComplete, OnCost);
	}
}

void FNeoStackAPIClient::SubmitToolResult(
	const FString& SessionID,
	const FString& CallID,
	const FString& Result)
{
	// Get settings
	const UNeoStackSettings* Settings = UNeoStackSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("[NeoStack] Failed to get settings for tool result submission"));
		return;
	}

	// Create HTTP request
	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = HttpModule.CreateRequest();

	// Build JSON payload
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
	JsonObject->SetStringField(TEXT("session_id"), SessionID);
	JsonObject->SetStringField(TEXT("call_id"), CallID);
	JsonObject->SetStringField(TEXT("result"), Result);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Configure request
	FString URL = Settings->BackendURL + TEXT("/ai/tool-result");
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-API-Key"), Settings->APIKey);
	Request->SetContentAsString(RequestBody);

	UE_LOG(LogTemp, Log, TEXT("[NeoStack] Submitting tool result - SessionID: %s, CallID: %s"), *SessionID, *CallID);

	// Bind response callback
	Request->OnProcessRequestComplete().BindLambda([CallID](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
	{
		if (bSuccess && Resp.IsValid() && Resp->GetResponseCode() == 200)
		{
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool result submitted successfully for CallID: %s"), *CallID);
		}
		else
		{
			FString Error = Resp.IsValid() ? Resp->GetContentAsString() : TEXT("Request failed");
			UE_LOG(LogTemp, Error, TEXT("[NeoStack] Failed to submit tool result for CallID: %s - %s"), *CallID, *Error);
		}
	});

	// Send request
	Request->ProcessRequest();
}

void FNeoStackAPIClient::SendMessageWithImages(
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
	FOnAPIError OnError)
{
	// If no images, delegate to regular method
	if (Images.Num() == 0)
	{
		SendMessageWithHistory(Message, History, AgentName, ModelID,
			OnContent, OnReasoning, OnToolCall, OnUE5ToolCall, OnToolResult, OnComplete, OnCost, OnError);
		return;
	}

	// Reset buffer for new request
	LastProcessedContent.Empty();

	// Generate a unique session ID for this request
	CurrentSessionID = FGuid::NewGuid().ToString();

	// Get settings
	const UNeoStackSettings* Settings = UNeoStackSettings::Get();
	if (!Settings)
	{
		OnError.ExecuteIfBound(TEXT("Failed to get NeoStack settings"));
		return;
	}

	// Validate API key
	if (Settings->APIKey.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("API Key not configured. Please set it in Project Settings > Game > NeoStack"));
		return;
	}

	// Validate backend URL
	if (Settings->BackendURL.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("Backend URL not configured"));
		return;
	}

	// Create HTTP request
	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = HttpModule.CreateRequest();

	// Build JSON payload
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
	JsonObject->SetStringField(TEXT("agent"), AgentName);
	JsonObject->SetStringField(TEXT("model"), ModelID);
	JsonObject->SetStringField(TEXT("session_id"), CurrentSessionID);

	// Build content array for multimodal message (OpenRouter/OpenAI format)
	TArray<TSharedPtr<FJsonValue>> ContentArray;

	// Add text content first
	if (!Message.IsEmpty())
	{
		TSharedPtr<FJsonObject> TextContent = MakeShareable(new FJsonObject());
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), Message);
		ContentArray.Add(MakeShareable(new FJsonValueObject(TextContent)));
	}

	// Add image content
	for (const FAttachedImage& Img : Images)
	{
		TSharedPtr<FJsonObject> ImageContent = MakeShareable(new FJsonObject());
		ImageContent->SetStringField(TEXT("type"), TEXT("image_url"));

		TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject());
		// Format: data:image/png;base64,<base64data>
		FString DataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *Img.MimeType, *Img.Base64Data);
		ImageUrl->SetStringField(TEXT("url"), DataUrl);
		ImageContent->SetObjectField(TEXT("image_url"), ImageUrl);

		ContentArray.Add(MakeShareable(new FJsonValueObject(ImageContent)));
	}

	// Set multimodal content (backend expects this for images)
	JsonObject->SetArrayField(TEXT("content"), ContentArray);
	// Also set prompt for backwards compatibility
	JsonObject->SetStringField(TEXT("prompt"), Message);

	// Add conversation history if present
	if (History.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		for (const FConversationMessage& Msg : History)
		{
			MessagesArray.Add(MakeShareable(new FJsonValueObject(Msg.ToJson())));
		}
		JsonObject->SetArrayField(TEXT("messages"), MessagesArray);
	}

	// Load and add runtime settings (same as SendMessageWithHistory)
	FString SettingsFilePath = FPaths::ProjectSavedDir() / TEXT("NeoStack") / TEXT("settings.json");
	FString SettingsContent;
	if (FFileHelper::LoadFileToString(SettingsContent, *SettingsFilePath))
	{
		TSharedPtr<FJsonObject> SettingsJsonObject;
		TSharedRef<TJsonReader<>> SettingsReader = TJsonReaderFactory<>::Create(SettingsContent);

		if (FJsonSerializer::Deserialize(SettingsReader, SettingsJsonObject) && SettingsJsonObject.IsValid())
		{
			TSharedPtr<FJsonObject> SettingsObject = MakeShareable(new FJsonObject());

			double MaxCostPerQuery = 0.0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxCostPerQuery"), MaxCostPerQuery) && MaxCostPerQuery > 0.0)
			{
				SettingsObject->SetNumberField(TEXT("max_cost_per_query"), MaxCostPerQuery);
			}

			int32 MaxTokens = 0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxTokens"), MaxTokens) && MaxTokens > 0)
			{
				SettingsObject->SetNumberField(TEXT("max_tokens"), MaxTokens);
			}

			bool bEnableThinking = false;
			if (SettingsJsonObject->TryGetBoolField(TEXT("EnableThinking"), bEnableThinking))
			{
				SettingsObject->SetBoolField(TEXT("enable_thinking"), bEnableThinking);
			}

			int32 MaxThinkingTokens = 0;
			if (SettingsJsonObject->TryGetNumberField(TEXT("MaxThinkingTokens"), MaxThinkingTokens) && MaxThinkingTokens > 0)
			{
				SettingsObject->SetNumberField(TEXT("max_thinking_tokens"), MaxThinkingTokens);
			}

			FString ReasoningEffort;
			if (SettingsJsonObject->TryGetStringField(TEXT("ReasoningEffort"), ReasoningEffort) && !ReasoningEffort.IsEmpty())
			{
				SettingsObject->SetStringField(TEXT("reasoning_effort"), ReasoningEffort);
			}

			const TSharedPtr<FJsonObject>* RoutingObj;
			if (SettingsJsonObject->TryGetObjectField(TEXT("ProviderRouting"), RoutingObj) && RoutingObj->IsValid())
			{
				const TSharedPtr<FJsonObject>* ModelRoutingObj;
				if ((*RoutingObj)->TryGetObjectField(ModelID, ModelRoutingObj) && ModelRoutingObj->IsValid())
				{
					TSharedPtr<FJsonObject> ProviderRoutingObject = MakeShareable(new FJsonObject());

					FString Provider;
					if ((*ModelRoutingObj)->TryGetStringField(TEXT("provider"), Provider))
					{
						ProviderRoutingObject->SetStringField(TEXT("provider"), Provider);
					}

					FString SortBy;
					if ((*ModelRoutingObj)->TryGetStringField(TEXT("sort_by"), SortBy))
					{
						ProviderRoutingObject->SetStringField(TEXT("sort_by"), SortBy);
					}

					bool bAllowFallbacks = true;
					if ((*ModelRoutingObj)->TryGetBoolField(TEXT("allow_fallbacks"), bAllowFallbacks))
					{
						ProviderRoutingObject->SetBoolField(TEXT("allow_fallbacks"), bAllowFallbacks);
					}

					SettingsObject->SetObjectField(TEXT("provider_routing"), ProviderRoutingObject);
				}
			}

			JsonObject->SetObjectField(TEXT("settings"), SettingsObject);
		}
	}

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Configure request
	FString URL = Settings->BackendURL + TEXT("/ai");
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-API-Key"), Settings->APIKey);
	Request->SetContentAsString(RequestBody);

	// Capture session ID for callbacks
	FString SessionID = CurrentSessionID;

	// Bind response callback
	Request->OnProcessRequestComplete().BindStatic(
		&FNeoStackAPIClient::OnResponseReceived,
		SessionID,
		OnContent,
		OnReasoning,
		OnToolCall,
		OnUE5ToolCall,
		OnToolResult,
		OnComplete,
		OnCost,
		OnError
	);

	// Bind progress callback for streaming
	Request->OnRequestProgress64().BindStatic(
		&FNeoStackAPIClient::OnRequestProgress,
		SessionID,
		OnContent,
		OnReasoning,
		OnToolCall,
		OnUE5ToolCall,
		OnToolResult,
		OnComplete,
		OnCost
	);

	// Send request
	if (!Request->ProcessRequest())
	{
		OnError.ExecuteIfBound(TEXT("Failed to send HTTP request"));
	}
}
