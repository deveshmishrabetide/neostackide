// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackChatInput.h"
#include "UI/SNeoStackSidebar.h"
#include "UI/SNeoStackChatArea.h"
#include "UI/SNeoStackHeader.h"
#include "UI/SCollapsibleToolWidget.h"
#include "UI/SNeoStackContextPopup.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/SlateDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "NeoStackStyle.h"
#include "NeoStackAPIClient.h"
#include "NeoStackConversation.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformApplicationMisc.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <shellapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "SNeoStackChatInput"

void SNeoStackChatInput::Construct(const FArguments& InArgs)
{
	UE_LOG(LogTemp, Warning, TEXT("[NeoStack] SNeoStackChatInput::Construct called - KEY HANDLER TEST BUILD"));

	SidebarPtr = InArgs._Sidebar;
	ChatAreaPtr = InArgs._ChatArea;

	ChildSlot
	[
		SNew(SOverlay)
		// Main input area
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1a1a1a")))))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#252525")))))
				.Padding(0.0f)
				[
					SNew(SVerticalBox)
					// Context tags area (shown when files are attached)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.0f, 8.0f, 16.0f, 0.0f)
					[
						SAssignNew(ContextTagsContainer, SHorizontalBox)
						.Visibility(this, &SNeoStackChatInput::GetContextTagsVisibility)
					]
					// Image preview area (shown when images are attached)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.0f, 8.0f, 16.0f, 0.0f)
					[
						SAssignNew(ImagePreviewContainer, SHorizontalBox)
						.Visibility(this, &SNeoStackChatInput::GetImagePreviewVisibility)
					]
					// Input row
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SHorizontalBox)
						// Input text box with menu anchor for context popup
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(16.0f, 14.0f, 12.0f, 14.0f)
						[
							SNew(SBox)
							.MinDesiredHeight(60.0f)
							.MaxDesiredHeight(200.0f)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SAssignNew(InputTextBox, SMultiLineEditableTextBox)
									.HintText(LOCTEXT("ChatInputHint", "Message NeoStack... (use @ to add context)"))
									.AllowMultiLine(true)
									.AutoWrapText(true)
									.OnTextChanged(this, &SNeoStackChatInput::OnTextChanged)
									.OnTextCommitted(this, &SNeoStackChatInput::OnTextCommitted)
									.Marshaller(FPlainTextLayoutMarshaller::Create())
									.BackgroundColor(FLinearColor::Transparent)
									.ForegroundColor(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
								]
								// Menu anchor for context popup (positioned above)
								+ SOverlay::Slot()
								.HAlign(HAlign_Left)
								.VAlign(VAlign_Top)
								[
									SAssignNew(ContextMenuAnchor, SMenuAnchor)
									.Placement(MenuPlacement_AboveAnchor)
									.OnGetMenuContent(this, &SNeoStackChatInput::GetContextPopupContent)
								]
							]
						]
						// Send button
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						.Padding(0.0f, 0.0f, 12.0f, 12.0f)
						[
							SNew(SBorder)
							.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1e1e1e")))))
							.Padding(0.0f)
							[
								SNew(SButton)
								.ButtonStyle(FCoreStyle::Get(), "NoBorder")
								.OnClicked(this, &SNeoStackChatInput::OnSendClicked)
								.ContentPadding(FMargin(12.0f, 8.0f))
								[
									SNew(SHorizontalBox)
									// Send text
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0.0f, 0.0f, 6.0f, 0.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("SendButton", "Send"))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
										.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
									]
									// Send icon
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(14.0f)
										.HeightOverride(14.0f)
										[
											SNew(SImage)
											.Image(FNeoStackStyle::Get().GetBrush("NeoStack.SendIcon"))
											.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
										]
									]
								]
							]
						]
					]
				]
			]
		]
	];

	// Set key down handler after construction
	if (InputTextBox.IsValid())
	{
		InputTextBox->SetOnKeyDownHandler(FOnKeyDown::CreateSP(this, &SNeoStackChatInput::HandleTextBoxKeyDown));
		UE_LOG(LogTemp, Warning, TEXT("[NeoStack] SetOnKeyDownHandler bound successfully"));
	}
}

FReply SNeoStackChatInput::OnSendClicked()
{
	// Hide context popup if open
	HideContextPopup();

	if (InputTextBox.IsValid())
	{
		FText CurrentText = InputTextBox->GetText();
		bool bHasText = !CurrentText.IsEmptyOrWhitespace();
		bool bHasImages = AttachedImages.Num() > 0;
		bool bHasContext = AttachedContexts.Num() > 0;

		// Allow sending if there's text OR images OR context
		if (bHasText || bHasImages || bHasContext)
		{
			FString Message = CurrentText.ToString();

			// Load context file contents and prepend to message
			if (bHasContext)
			{
				LoadContextFileContents();

				FString ContextPrefix;
				for (const FAttachedContext& Ctx : AttachedContexts)
				{
					ContextPrefix += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *Ctx.FullPath, *Ctx.FileContent);
				}

				if (!ContextPrefix.IsEmpty())
				{
					Message = ContextPrefix + TEXT("--- User Message ---\n") + Message;
				}
			}

			// Get selected agent and model from sidebar
			FString AgentName = TEXT("orchestrator"); // Default
			FString ModelID = TEXT("anthropic/claude-haiku-4.5"); // Default

			if (SidebarPtr.IsValid())
			{
				TSharedPtr<FAgentInfo> SelectedAgent = SidebarPtr->GetSelectedAgent();
				if (SelectedAgent.IsValid())
				{
					AgentName = SelectedAgent->AgentID;
				}

				TSharedPtr<FModelInfo> SelectedModel = SidebarPtr->GetSelectedModel();
				if (SelectedModel.IsValid())
				{
					ModelID = SelectedModel->ModelID;
				}
			}

			// Extract image data before clearing (for async send) - only copy what we need
			TArray<FAttachedImage> ImagesToSend;
			for (const FAttachedImage& Img : AttachedImages)
			{
				FAttachedImage SendImg;
				SendImg.Base64Data = Img.Base64Data;
				SendImg.MimeType = Img.MimeType;
				// Don't copy ImageData, ThumbnailTexture or ThumbnailBrush - not needed for sending
				ImagesToSend.Add(MoveTemp(SendImg));
			}

			// Convert images to FConversationImage format for storage and display
			TArray<FConversationImage> ConvImages;
			for (const FAttachedImage& Img : ImagesToSend)
			{
				FConversationImage ConvImg;
				ConvImg.Base64Data = Img.Base64Data;
				ConvImg.MimeType = Img.MimeType;
				ConvImages.Add(ConvImg);
			}

			// Clear input, images, and context immediately
			InputTextBox->SetText(FText::GetEmpty());
			ClearAttachedImages();
			ClearContextReferences();

			// Save user message to conversation (crash-safe) with images
			FNeoStackConversationManager& ConversationMgr = FNeoStackConversationManager::Get();
			if (ConvImages.Num() > 0)
			{
				ConversationMgr.AppendMessage(FConversationMessage::UserWithImages(Message, ConvImages));
			}
			else
			{
				ConversationMgr.AppendMessage(FConversationMessage::User(Message));
			}

			// Refresh sidebar conversation list to show new/updated conversation
			if (SidebarPtr.IsValid())
			{
				SidebarPtr->RefreshConversationsList();
			}

			// Get conversation history for multi-turn
			TArray<FConversationMessage> History = ConversationMgr.GetCurrentMessages();
			// Remove the last message (the one we just added) since we send it as prompt
			if (History.Num() > 0)
			{
				History.RemoveAt(History.Num() - 1);
			}

			// Add user message to chat area with images
			if (ChatAreaPtr.IsValid())
			{
				ChatAreaPtr->AddUserMessageWithImages(Message, ConvImages);

				// Get display names for header
				FString AgentDisplayName = AgentName;
				FString ModelDisplayName = ModelID;

				if (SidebarPtr.IsValid())
				{
					TSharedPtr<FAgentInfo> SelectedAgent = SidebarPtr->GetSelectedAgent();
					if (SelectedAgent.IsValid())
					{
						AgentDisplayName = SelectedAgent->DisplayName;
					}

					TSharedPtr<FModelInfo> SelectedModel = SidebarPtr->GetSelectedModel();
					if (SelectedModel.IsValid())
					{
						ModelDisplayName = SelectedModel->Name;
					}
				}

				// Start assistant message
				ChatAreaPtr->StartAssistantMessage(AgentDisplayName, ModelDisplayName);
			}

			// Track assistant message content for saving
			TSharedPtr<FString> AccumulatedContent = MakeShared<FString>();
			TSharedPtr<TArray<FConversationToolCall>> PendingToolCalls = MakeShared<TArray<FConversationToolCall>>();
			TSharedPtr<TArray<TPair<FString, FString>>> PendingToolResults = MakeShared<TArray<TPair<FString, FString>>>(); // CallID, Result

			// Track pending UE5 tool calls (CallID -> (ToolName, Args))
			TSharedPtr<TMap<FString, TPair<FString, FString>>> PendingUE5Tools = MakeShared<TMap<FString, TPair<FString, FString>>>();
			TSharedPtr<FString> CurrentSessionID = MakeShared<FString>();

			// Send message to AI with images (or just text if no images)
			TWeakPtr<SNeoStackChatArea> WeakChatArea = ChatAreaPtr;
			TWeakPtr<SNeoStackSidebar> WeakSidebar = SidebarPtr;
			FNeoStackAPIClient::SendMessageWithImages(
				Message,
				ImagesToSend,
				History,
				AgentName,
				ModelID,
				// On content
				FOnAIContent::CreateLambda([WeakChatArea, AccumulatedContent](const FString& Content)
				{
					// Accumulate content for saving
					*AccumulatedContent += Content;

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->AppendContent(Content);
					}
				}),
				// On reasoning
				FOnAIReasoning::CreateLambda([WeakChatArea](const FString& Reasoning)
				{
					// Note: We don't save reasoning to conversation history
					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->AppendReasoning(Reasoning);
					}
				}),
				// On backend tool call (executed by backend)
				FOnAIToolCall::CreateLambda([WeakChatArea, PendingToolCalls](const FString& ToolName, const FString& Args, const FString& CallID)
				{
					// Track tool call for saving (will be saved with assistant message)
					FConversationToolCall TC;
					TC.ID = CallID;
					TC.Name = ToolName;
					TC.Arguments = Args;
					PendingToolCalls->Add(TC);

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->AppendToolCall(ToolName, Args, CallID);
					}
				}),
				// On UE5 tool call (needs execution in engine with approval)
				FOnAIUE5ToolCall::CreateLambda([WeakChatArea, PendingToolCalls, PendingUE5Tools, CurrentSessionID](const FString& SessionID, const FString& ToolName, const FString& Args, const FString& CallID)
				{
					UE_LOG(LogTemp, Log, TEXT("[NeoStack] UE5 Tool call received - SessionID: %s, Tool: %s, CallID: %s"), *SessionID, *ToolName, *CallID);

					// Store session ID
					*CurrentSessionID = SessionID;

					// Track tool call for saving
					FConversationToolCall TC;
					TC.ID = CallID;
					TC.Name = ToolName;
					TC.Arguments = Args;
					PendingToolCalls->Add(TC);

					// Track for execution (CallID -> (ToolName, Args))
					PendingUE5Tools->Add(CallID, TPair<FString, FString>(ToolName, Args));

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						// Use AppendUE5ToolCall to pass session ID for result submission
						ChatArea->AppendUE5ToolCall(SessionID, ToolName, Args, CallID);
					}
				}),
				// On tool result (from backend execution)
				FOnAIToolResult::CreateLambda([WeakChatArea, PendingToolResults](const FString& CallID, const FString& Result)
				{
					// Queue tool result - will be saved AFTER assistant message with tool_calls
					PendingToolResults->Add(TPair<FString, FString>(CallID, Result));

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->AppendToolResult(CallID, Result);
					}
				}),
				// On complete
				FOnAIComplete::CreateLambda([WeakChatArea, WeakSidebar, AccumulatedContent, PendingToolCalls, PendingToolResults]()
				{
					FNeoStackConversationManager& ConvMgr = FNeoStackConversationManager::Get();

					// Save assistant message with tool_calls FIRST
					if (PendingToolCalls->Num() > 0)
					{
						// Assistant message that requested tools (may have content before tool calls)
						FConversationMessage AssistantWithTools;
						AssistantWithTools.Role = TEXT("assistant");
						AssistantWithTools.ToolCalls = *PendingToolCalls;
						// Content before tools is included here
						ConvMgr.AppendMessage(AssistantWithTools);

						// Then save all tool results in order
						for (const TPair<FString, FString>& ToolResult : *PendingToolResults)
						{
							ConvMgr.AppendMessage(FConversationMessage::Tool(ToolResult.Key, ToolResult.Value));
						}

						// If there's content after tools, save as separate assistant message
						if (!AccumulatedContent->IsEmpty())
						{
							FConversationMessage FinalAssistant;
							FinalAssistant.Role = TEXT("assistant");
							FinalAssistant.Content = *AccumulatedContent;
							ConvMgr.AppendMessage(FinalAssistant);
						}
					}
					else
					{
						// No tool calls - just save assistant message with content
						FConversationMessage AssistantMsg;
						AssistantMsg.Role = TEXT("assistant");
						AssistantMsg.Content = *AccumulatedContent;
						ConvMgr.AppendMessage(AssistantMsg);
					}

					// Refresh sidebar to show updated conversation
					if (TSharedPtr<SNeoStackSidebar> Sidebar = WeakSidebar.Pin())
					{
						Sidebar->RefreshConversationsList();
					}

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->CompleteAssistantMessage();
					}
				}),
				// On cost update
				FOnAICost::CreateLambda([](float Cost)
				{
					if (TSharedPtr<SNeoStackHeader> Header = SNeoStackHeader::Get())
					{
						Header->SetCost(Cost);
					}
				}),
				// On error
				FOnAPIError::CreateLambda([WeakChatArea, WeakSidebar](const FString& Error)
				{
					UE_LOG(LogTemp, Error, TEXT("API Error: %s"), *Error);

					// Save error as assistant message so conversation state is consistent
					FConversationMessage ErrorMsg;
					ErrorMsg.Role = TEXT("assistant");
					ErrorMsg.Content = FString::Printf(TEXT("Error: %s"), *Error);
					FNeoStackConversationManager::Get().AppendMessage(ErrorMsg);

					// Refresh sidebar
					if (TSharedPtr<SNeoStackSidebar> Sidebar = WeakSidebar.Pin())
					{
						Sidebar->RefreshConversationsList();
					}

					if (TSharedPtr<SNeoStackChatArea> ChatArea = WeakChatArea.Pin())
					{
						ChatArea->AppendContent(FString::Printf(TEXT("Error: %s"), *Error));
						ChatArea->CompleteAssistantMessage();
					}
				})
			);
		}
	}
	return FReply::Handled();
}

void SNeoStackChatInput::OnTextChanged(const FText& InText)
{
	// Check for @ context trigger
	CheckForContextTrigger(InText.ToString());
}

void SNeoStackChatInput::OnTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		// TODO: Check if shift is held, if not, send the message
		OnSendClicked();
	}
}

FReply SNeoStackChatInput::HandleTextBoxKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Log ALL key presses to debug
	UE_LOG(LogTemp, Log, TEXT("[NeoStack] HandleTextBoxKeyDown - key: %s, popupVisible: %d"), *InKeyEvent.GetKey().ToString(), bContextPopupVisible ? 1 : 0);

	// Intercept Up/Down/Enter/Escape when context popup is visible
	if (bContextPopupVisible && ContextPopup.IsValid())
	{

		if (InKeyEvent.GetKey() == EKeys::Up)
		{
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectPrevious called"));
			ContextPopup->SelectPrevious();
			return FReply::Handled();
		}
		else if (InKeyEvent.GetKey() == EKeys::Down)
		{
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] SelectNext called"));
			ContextPopup->SelectNext();
			return FReply::Handled();
		}
		else if (InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Tab)
		{
			if (ContextPopup->HasItems())
			{
				ContextPopup->ConfirmSelection();
				return FReply::Handled();
			}
		}
		else if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			HideContextPopup();
			return FReply::Handled();
		}
	}

	// Let the text box handle all other keys normally
	return FReply::Unhandled();
}

FReply SNeoStackChatInput::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Handle context popup navigation when visible
	if (bContextPopupVisible && ContextPopup.IsValid())
	{
		if (InKeyEvent.GetKey() == EKeys::Up)
		{
			ContextPopup->SelectPrevious();
			return FReply::Handled();
		}
		else if (InKeyEvent.GetKey() == EKeys::Down)
		{
			ContextPopup->SelectNext();
			return FReply::Handled();
		}
		else if (InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Tab)
		{
			if (ContextPopup->HasItems())
			{
				ContextPopup->ConfirmSelection();
				return FReply::Handled();
			}
		}
		else if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			HideContextPopup();
			return FReply::Handled();
		}
	}

	// Handle Ctrl+V for image paste
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::V)
	{
		if (TryPasteImageFromClipboard())
		{
			return FReply::Handled();
		}
		// If no image, fall through to let text paste work
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SNeoStackChatInput::TryPasteImageFromClipboard()
{
#if PLATFORM_WINDOWS
	if (!OpenClipboard(nullptr))
	{
		return false;
	}

	bool bSuccess = false;

	// Try CF_DIB first (device-independent bitmap)
	HANDLE hDib = GetClipboardData(CF_DIB);
	if (hDib)
	{
		LPBITMAPINFOHEADER lpbi = (LPBITMAPINFOHEADER)GlobalLock(hDib);
		if (lpbi)
		{
			int32 Width = lpbi->biWidth;
			int32 Height = FMath::Abs(lpbi->biHeight);
			int32 BitCount = lpbi->biBitCount;

			// Calculate color table size
			int32 ColorTableSize = 0;
			if (BitCount <= 8)
			{
				ColorTableSize = (1LL << BitCount) * sizeof(RGBQUAD);
			}
			else if (lpbi->biCompression == BI_BITFIELDS)
			{
				ColorTableSize = 3 * sizeof(DWORD);
			}

			// Get pointer to pixel data
			uint8* PixelData = (uint8*)lpbi + lpbi->biSize + ColorTableSize;

			// Convert to BGRA format
			TArray<uint8> RawBGRA;
			RawBGRA.SetNumUninitialized(Width * Height * 4);

			bool bBottomUp = lpbi->biHeight > 0;

			for (int32 Y = 0; Y < Height; Y++)
			{
				int32 SrcY = bBottomUp ? (Height - 1 - Y) : Y;

				for (int32 X = 0; X < Width; X++)
				{
					int32 DstIdx = (Y * Width + X) * 4;

					if (BitCount == 32)
					{
						int32 SrcIdx = (SrcY * Width + X) * 4;
						RawBGRA[DstIdx + 0] = PixelData[SrcIdx + 0]; // B
						RawBGRA[DstIdx + 1] = PixelData[SrcIdx + 1]; // G
						RawBGRA[DstIdx + 2] = PixelData[SrcIdx + 2]; // R
						RawBGRA[DstIdx + 3] = PixelData[SrcIdx + 3]; // A
					}
					else if (BitCount == 24)
					{
						int32 RowPitch = ((Width * 3 + 3) / 4) * 4; // Rows are 4-byte aligned
						int32 SrcIdx = SrcY * RowPitch + X * 3;
						RawBGRA[DstIdx + 0] = PixelData[SrcIdx + 0]; // B
						RawBGRA[DstIdx + 1] = PixelData[SrcIdx + 1]; // G
						RawBGRA[DstIdx + 2] = PixelData[SrcIdx + 2]; // R
						RawBGRA[DstIdx + 3] = 255;                   // A
					}
				}
			}

			// Convert BGRA to PNG using ImageWrapper
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

			if (ImageWrapper.IsValid())
			{
				// SetRaw expects BGRA format
				if (ImageWrapper->SetRaw(RawBGRA.GetData(), RawBGRA.Num(), Width, Height, ERGBFormat::BGRA, 8))
				{
					TArray64<uint8> PngData = ImageWrapper->GetCompressed(90);
					if (PngData.Num() > 0)
					{
						// Convert TArray64 to TArray
						TArray<uint8> ImageData;
						ImageData.SetNumUninitialized(PngData.Num());
						FMemory::Memcpy(ImageData.GetData(), PngData.GetData(), PngData.Num());

						AddImageAttachment(ImageData);
						bSuccess = true;
					}
				}
			}

			GlobalUnlock(hDib);
		}
	}

	// Try CF_HDROP for image files
	if (!bSuccess)
	{
		HANDLE hDrop = GetClipboardData(CF_HDROP);
		if (hDrop)
		{
			HDROP hDropInfo = (HDROP)GlobalLock(hDrop);
			if (hDropInfo)
			{
				UINT FileCount = DragQueryFileW(hDropInfo, 0xFFFFFFFF, nullptr, 0);
				for (UINT i = 0; i < FileCount && !bSuccess; i++)
				{
					WCHAR FilePath[MAX_PATH];
					if (DragQueryFileW(hDropInfo, i, FilePath, MAX_PATH))
					{
						FString FilePathStr(FilePath);
						FString Extension = FPaths::GetExtension(FilePathStr).ToLower();

						if (Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
						{
							TArray<uint8> FileData;
							if (FFileHelper::LoadFileToArray(FileData, *FilePathStr))
							{
								// Convert to PNG if needed
								if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
								{
									IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
									TSharedPtr<IImageWrapper> JpgWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
									TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

									if (JpgWrapper.IsValid() && PngWrapper.IsValid())
									{
										if (JpgWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
										{
											TArray64<uint8> RawData;
											if (JpgWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
											{
												int32 Width = JpgWrapper->GetWidth();
												int32 Height = JpgWrapper->GetHeight();

												if (PngWrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
												{
													TArray64<uint8> PngData = PngWrapper->GetCompressed(90);
													if (PngData.Num() > 0)
													{
														TArray<uint8> ImageData;
														ImageData.SetNumUninitialized(PngData.Num());
														FMemory::Memcpy(ImageData.GetData(), PngData.GetData(), PngData.Num());
														AddImageAttachment(ImageData);
														bSuccess = true;
													}
												}
											}
										}
									}
								}
								else
								{
									AddImageAttachment(FileData);
									bSuccess = true;
								}
							}
						}
					}
				}
				GlobalUnlock(hDrop);
			}
		}
	}

	CloseClipboard();
	return bSuccess;
#else
	// Non-Windows platforms - use UE's generic clipboard (text only for now)
	return false;
#endif
}

void SNeoStackChatInput::AddImageAttachment(const TArray<uint8>& ImageData)
{
	FAttachedImage Attachment;
	Attachment.ImageData = ImageData;
	Attachment.Base64Data = ImageDataToBase64(ImageData);
	Attachment.MimeType = TEXT("image/png");

	// Create thumbnail texture using TStrongObjectPtr for proper UObject lifecycle
	UTexture2D* Texture = CreateThumbnailTexture(ImageData);
	if (Texture)
	{
		Attachment.ThumbnailTexture.Reset(Texture);

		// Create brush for the thumbnail
		Attachment.ThumbnailBrush = MakeShareable(new FSlateBrush());
		Attachment.ThumbnailBrush->SetResourceObject(Texture);
		Attachment.ThumbnailBrush->ImageSize = FVector2D(64, 64);
		Attachment.ThumbnailBrush->DrawAs = ESlateBrushDrawType::Image;
	}

	AttachedImages.Add(MoveTemp(Attachment));
	UpdateImagePreviewUI();
}

void SNeoStackChatInput::RemoveImageAttachment(int32 Index)
{
	if (AttachedImages.IsValidIndex(Index))
	{
		AttachedImages.RemoveAt(Index);
		UpdateImagePreviewUI();
	}
}

void SNeoStackChatInput::ClearAttachedImages()
{
	AttachedImages.Empty();
	UpdateImagePreviewUI();
}

void SNeoStackChatInput::UpdateImagePreviewUI()
{
	if (!ImagePreviewContainer.IsValid())
	{
		return;
	}

	// Clear existing previews
	ImagePreviewContainer->ClearChildren();

	// Add preview for each attached image
	for (int32 i = 0; i < AttachedImages.Num(); i++)
	{
		const FAttachedImage& Img = AttachedImages[i];
		int32 Index = i; // Capture for lambda

		ImagePreviewContainer->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(72.0f)
			.HeightOverride(72.0f)
			[
				SNew(SOverlay)
				// Thumbnail image
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#333333")))))
					.Padding(4.0f)
					[
						SNew(SImage)
						.Image(Img.ThumbnailBrush.IsValid() ? Img.ThumbnailBrush.Get() : nullptr)
					]
				]
				// X button overlay
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(2.0f))
					.OnClicked_Lambda([this, Index]()
					{
						RemoveImageAttachment(Index);
						return FReply::Handled();
					})
					[
						SNew(SBox)
						.WidthOverride(16.0f)
						.HeightOverride(16.0f)
						[
							SNew(SBorder)
							.BorderImage(new FSlateColorBrush(FLinearColor(0.2f, 0.2f, 0.2f, 0.8f)))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("X")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(FLinearColor::White)
							]
						]
					]
				]
			]
		];
	}
}

EVisibility SNeoStackChatInput::GetImagePreviewVisibility() const
{
	return AttachedImages.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

UTexture2D* SNeoStackChatInput::CreateThumbnailTexture(const TArray<uint8>& ImageData)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return nullptr;
	}

	if (!ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
	{
		return nullptr;
	}

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	// Create texture
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	// Lock and copy data
	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	// Update the texture
	Texture->UpdateResource();

	return Texture;
}

FString SNeoStackChatInput::ImageDataToBase64(const TArray<uint8>& ImageData)
{
	return FBase64::Encode(ImageData);
}

EVisibility SNeoStackChatInput::GetContextTagsVisibility() const
{
	return AttachedContexts.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

void SNeoStackChatInput::ShowContextPopup()
{
	if (ContextMenuAnchor.IsValid() && !bContextPopupVisible)
	{
		bContextPopupVisible = true;
		ContextMenuAnchor->SetIsOpen(true, false); // false = don't focus the popup

		// Keep focus on the input text box
		if (InputTextBox.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(InputTextBox);
		}
	}
}

void SNeoStackChatInput::HideContextPopup()
{
	if (ContextMenuAnchor.IsValid() && bContextPopupVisible)
	{
		bContextPopupVisible = false;
		ContextMenuAnchor->SetIsOpen(false);
		AtSymbolPosition = -1;
	}
}

TSharedRef<SWidget> SNeoStackChatInput::GetContextPopupContent()
{
	return SAssignNew(ContextPopup, SNeoStackContextPopup)
		.OnItemSelected(this, &SNeoStackChatInput::OnContextItemSelected);
}

void SNeoStackChatInput::OnContextItemSelected(const FContextItem& Item)
{
	// Add the context reference
	AddContextReference(Item.DisplayName, Item.FullPath);

	// Remove the @filter text from input
	if (InputTextBox.IsValid() && AtSymbolPosition >= 0)
	{
		FString CurrentText = InputTextBox->GetText().ToString();
		// Find end of filter (next space or end of string)
		int32 FilterEnd = CurrentText.Len();
		for (int32 i = AtSymbolPosition; i < CurrentText.Len(); i++)
		{
			if (CurrentText[i] == TEXT(' ') || CurrentText[i] == TEXT('\n'))
			{
				FilterEnd = i;
				break;
			}
		}
		// Remove @filter text
		FString NewText = CurrentText.Left(AtSymbolPosition) + CurrentText.Mid(FilterEnd);
		InputTextBox->SetText(FText::FromString(NewText));
	}

	// Hide popup
	HideContextPopup();
}

void SNeoStackChatInput::AddContextReference(const FString& DisplayName, const FString& FullPath)
{
	// Check if already added
	for (const FAttachedContext& Ctx : AttachedContexts)
	{
		if (Ctx.FullPath == FullPath)
		{
			return; // Already attached
		}
	}

	FAttachedContext NewContext;
	NewContext.DisplayName = DisplayName;
	NewContext.FullPath = FullPath;
	AttachedContexts.Add(NewContext);

	UpdateContextTagsUI();
}

void SNeoStackChatInput::RemoveContextReference(int32 Index)
{
	if (AttachedContexts.IsValidIndex(Index))
	{
		AttachedContexts.RemoveAt(Index);
		UpdateContextTagsUI();
	}
}

void SNeoStackChatInput::ClearContextReferences()
{
	AttachedContexts.Empty();
	UpdateContextTagsUI();
}

void SNeoStackChatInput::UpdateContextTagsUI()
{
	if (!ContextTagsContainer.IsValid())
	{
		return;
	}

	ContextTagsContainer->ClearChildren();

	for (int32 i = 0; i < AttachedContexts.Num(); i++)
	{
		const FAttachedContext& Ctx = AttachedContexts[i];
		int32 Index = i;

		ContextTagsContainer->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#3a3a5a")))))
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				// @ symbol
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("@")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 1.0f, 1.0f))
				]
				// File name
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Ctx.DisplayName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
				]
				// X button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0.0f))
					.OnClicked_Lambda([this, Index]()
					{
						RemoveContextReference(Index);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("x")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
					]
				]
			]
		];
	}
}

void SNeoStackChatInput::CheckForContextTrigger(const FString& Text)
{
	// Find the last @ symbol
	int32 LastAtPos = Text.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	if (LastAtPos != INDEX_NONE)
	{
		// Check if there's a space or newline after the @
		bool bHasSpaceAfter = false;
		for (int32 i = LastAtPos + 1; i < Text.Len(); i++)
		{
			if (Text[i] == TEXT(' ') || Text[i] == TEXT('\n'))
			{
				bHasSpaceAfter = true;
				break;
			}
		}

		if (!bHasSpaceAfter)
		{
			// Extract filter text after @
			FString FilterText = Text.Mid(LastAtPos + 1);
			AtSymbolPosition = LastAtPos;

			// Show popup and update filter
			ShowContextPopup();
			if (ContextPopup.IsValid())
			{
				ContextPopup->SetFilter(FilterText);
			}
			return;
		}
	}

	// No active @ trigger, hide popup
	HideContextPopup();
}

void SNeoStackChatInput::LoadContextFileContents()
{
	for (FAttachedContext& Ctx : AttachedContexts)
	{
		if (Ctx.FileContent.IsEmpty())
		{
			// Determine if it's a file path or asset path
			if (Ctx.FullPath.StartsWith(TEXT("/")))
			{
				// Asset path - try to load as text asset or get asset info
				// For now just include the path reference
				Ctx.FileContent = FString::Printf(TEXT("[Asset: %s]"), *Ctx.FullPath);
			}
			else
			{
				// File path - load content
				FString FullFilePath = FPaths::ProjectDir() / Ctx.FullPath;
				if (!FFileHelper::LoadFileToString(Ctx.FileContent, *FullFilePath))
				{
					Ctx.FileContent = FString::Printf(TEXT("[Could not load file: %s]"), *Ctx.FullPath);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
