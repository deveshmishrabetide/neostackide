// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SNeoStackChatArea.h"
#include "UI/SCollapsibleToolWidget.h"
#include "UI/SCollapsibleReasoningWidget.h"
#include "NeoStackConversation.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "NeoStackStyle.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/IRun.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "SNeoStackChatArea"

void SNeoStackChatArea::Construct(const FArguments& InArgs)
{
	bInAssistantMessage = false;
	OnToolApprovedDelegate = InArgs._OnToolApproved;
	OnToolRejectedDelegate = InArgs._OnToolRejected;

	ChildSlot
	[
		SAssignNew(MessageScrollBox, SScrollBox)
		+ SScrollBox::Slot()
		.Padding(16.0f)
		[
			SAssignNew(MessageContainer, SVerticalBox)
		]
	];
}

void SNeoStackChatArea::AddUserMessage(const FString& Message)
{
	AddUserMessageWithImages(Message, TArray<FConversationImage>());
}

void SNeoStackChatArea::AddUserMessageWithImages(const FString& Message, const TArray<FConversationImage>& Images)
{
	if (!MessageContainer.IsValid())
		return;

	MessageContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			CreateUserMessageWidget(Message, Images)
		];

	ScrollToBottom();
}

void SNeoStackChatArea::StartAssistantMessage(const FString& AgentName, const FString& ModelName)
{
	if (!MessageContainer.IsValid())
		return;

	CurrentAgentName = AgentName;
	CurrentModelName = ModelName;
	bInAssistantMessage = true;
	CurrentStreamingContent = TEXT("");
	CurrentStreamingTextBlock.Reset();
	CurrentStreamingReasoning = TEXT("");
	CurrentStreamingReasoningWidget.Reset();

	// Create a new vertical box for this assistant message
	TSharedPtr<SVerticalBox> AssistantMessageBox;

	MessageContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			SNew(SVerticalBox)
			// Header
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateAssistantHeaderWidget(AgentName, ModelName)
			]
			// Content container
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(AssistantMessageBox, SVerticalBox)
			]
		];

	CurrentAssistantContainer = AssistantMessageBox;
	ScrollToBottom();
}

void SNeoStackChatArea::AppendContent(const FString& Content)
{
	if (!CurrentAssistantContainer.IsValid() || !bInAssistantMessage)
		return;

	// Finalize any streaming reasoning before adding content
	CurrentStreamingReasoningWidget.Reset();
	CurrentStreamingReasoning.Empty();

	// Accumulate content for streaming
	CurrentStreamingContent += Content;

	// If we don't have a streaming text block yet, create one
	if (!CurrentStreamingTextBlock.IsValid())
	{
		CurrentAssistantContainer->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SAssignNew(CurrentStreamingTextBlock, SRichTextBlock)
				.TextStyle(FCoreStyle::Get(), "NormalText")
				.DecoratorStyleSet(&FCoreStyle::Get())
				.AutoWrapText(true)
			];
	}

	// Convert accumulated markdown to rich text and update
	if (CurrentStreamingTextBlock.IsValid())
	{
		FString RichText = CurrentStreamingContent;

		// 1) Handle headings (line-based)
		TArray<FString> Lines;
		RichText.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

		for (FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("## ")))
			{
				const FString LineContent = Line.RightChop(3);
				Line = FString::Printf(TEXT("<RichTextBlock.Bold>%s</>"), *LineContent);
			}
			else if (Line.StartsWith(TEXT("# ")))
			{
				const FString LineContent = Line.RightChop(2);
				Line = FString::Printf(TEXT("<Credits.H2>%s</>"), *LineContent);
			}
		}

		RichText = FString::Join(Lines, TEXT("\n"));

		// 2) Handle **bold**
		while (true)
		{
			int32 StartPos = RichText.Find(TEXT("**"));
			if (StartPos == INDEX_NONE) break;

			int32 EndPos = RichText.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartPos + 2);
			if (EndPos == INDEX_NONE) break;

			FString BoldText = RichText.Mid(StartPos + 2, EndPos - StartPos - 2);
			FString Replacement = FString::Printf(TEXT("<RichTextBlock.Bold>%s</>"), *BoldText);

			RichText = RichText.Left(StartPos) + Replacement + RichText.Mid(EndPos + 2);
		}

		// 3) Handle *italic*
		int32 SearchPos = 0;
		while (SearchPos < RichText.Len())
		{
			int32 StartPos = RichText.Find(TEXT("*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
			if (StartPos == INDEX_NONE) break;

			if (StartPos > 0 && RichText[StartPos - 1] == '*')
			{
				SearchPos = StartPos + 1;
				continue;
			}
			if (StartPos + 1 < RichText.Len() && RichText[StartPos + 1] == '*')
			{
				SearchPos = StartPos + 2;
				continue;
			}

			int32 EndPos = StartPos + 1;
			while (EndPos < RichText.Len())
			{
				if (RichText[EndPos] == '*' && (EndPos + 1 >= RichText.Len() || RichText[EndPos + 1] != '*'))
				{
					break;
				}
				EndPos++;
			}

			if (EndPos < RichText.Len())
			{
				FString ItalicText = RichText.Mid(StartPos + 1, EndPos - StartPos - 1);
				FString Replacement = FString::Printf(TEXT("<RichTextBlock.Italic>%s</>"), *ItalicText);

				RichText = RichText.Left(StartPos) + Replacement + RichText.Mid(EndPos + 1);
				SearchPos = StartPos + Replacement.Len();
			}
			else
			{
				break;
			}
		}

		// Remove backticks
		RichText.ReplaceInline(TEXT("`"), TEXT(""));

		CurrentStreamingTextBlock->SetText(FText::FromString(RichText));
	}

	ScrollToBottom();
}

void SNeoStackChatArea::AppendReasoning(const FString& Reasoning)
{
	if (!CurrentAssistantContainer.IsValid() || !bInAssistantMessage)
		return;

	// Finalize any streaming content before adding reasoning
	CurrentStreamingTextBlock.Reset();
	CurrentStreamingContent.Empty();

	// Accumulate reasoning for streaming
	CurrentStreamingReasoning += Reasoning;

	// If we don't have a streaming reasoning widget yet, create one
	if (!CurrentStreamingReasoningWidget.IsValid())
	{
		TSharedPtr<SCollapsibleReasoningWidget> ReasoningWidget;

		CurrentAssistantContainer->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SAssignNew(ReasoningWidget, SCollapsibleReasoningWidget)
				.Reasoning(TEXT(""))
			];

		CurrentStreamingReasoningWidget = ReasoningWidget;
	}

	// Update the reasoning widget with accumulated text
	if (CurrentStreamingReasoningWidget.IsValid())
	{
		CurrentStreamingReasoningWidget->UpdateReasoning(CurrentStreamingReasoning);
	}

	ScrollToBottom();
}

void SNeoStackChatArea::AppendToolCall(const FString& ToolName, const FString& Args, const FString& CallID)
{
	// Backend tools don't require approval - just show them
	AppendUE5ToolCall(TEXT(""), ToolName, Args, CallID);
}

void SNeoStackChatArea::AppendUE5ToolCall(const FString& SessionID, const FString& ToolName, const FString& Args, const FString& CallID)
{
	if (!CurrentAssistantContainer.IsValid() || !bInAssistantMessage)
		return;

	// Finalize any streaming content before adding tool call
	CurrentStreamingTextBlock.Reset();
	CurrentStreamingContent.Empty();

	// Finalize any streaming reasoning before adding tool call
	CurrentStreamingReasoningWidget.Reset();
	CurrentStreamingReasoning.Empty();

	// Store session ID for this tool call (needed for result submission)
	if (!SessionID.IsEmpty())
	{
		ToolSessionIDs.Add(CallID, SessionID);
	}

	CurrentAssistantContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			CreateToolCallWidget(ToolName, Args, CallID)
		];

	ScrollToBottom();
}

void SNeoStackChatArea::AppendToolResult(const FString& CallID, const FString& Result)
{
	// Find the tool widget by CallID and update it with the result
	// Note: Don't check bInAssistantMessage here - we need to update tool widgets
	// even when loading historical conversations where CompleteAssistantMessage was already called
	TSharedPtr<SCollapsibleToolWidget>* ToolWidgetPtr = ToolWidgets.Find(CallID);
	if (ToolWidgetPtr && ToolWidgetPtr->IsValid())
	{
		(*ToolWidgetPtr)->SetResult(Result);
	}

	ScrollToBottom();
}

void SNeoStackChatArea::CompleteAssistantMessage()
{
	bInAssistantMessage = false;
	CurrentAssistantContainer.Reset();
	CurrentStreamingTextBlock.Reset();
	CurrentStreamingContent.Empty();
	CurrentStreamingReasoningWidget.Reset();
	CurrentStreamingReasoning.Empty();
}

void SNeoStackChatArea::ClearMessages()
{
	if (MessageContainer.IsValid())
	{
		MessageContainer->ClearChildren();
	}
	bInAssistantMessage = false;
	CurrentAssistantContainer.Reset();
	CurrentStreamingTextBlock.Reset();
	CurrentStreamingContent.Empty();
	CurrentStreamingReasoningWidget.Reset();
	CurrentStreamingReasoning.Empty();
	ToolWidgets.Empty();
	PendingToolCalls.Empty();
	ToolSessionIDs.Empty();
	// Clear persistent image storage
	ImageBrushes.Empty();
	ImageTextures.Empty();
}

TSharedPtr<SCollapsibleToolWidget> SNeoStackChatArea::GetToolWidget(const FString& CallID) const
{
	const TSharedPtr<SCollapsibleToolWidget>* WidgetPtr = ToolWidgets.Find(CallID);
	return WidgetPtr ? *WidgetPtr : nullptr;
}

FString SNeoStackChatArea::GetSessionIDForTool(const FString& CallID) const
{
	const FString* SessionIDPtr = ToolSessionIDs.Find(CallID);
	return SessionIDPtr ? *SessionIDPtr : FString();
}

TSharedRef<SWidget> SNeoStackChatArea::CreateUserMessageWidget(const FString& Message, const TArray<FConversationImage>& Images)
{
	TSharedRef<SVerticalBox> UserMessageBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("User")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
		];

	// Add images if present
	if (Images.Num() > 0)
	{
		TSharedRef<SHorizontalBox> ImageRow = SNew(SHorizontalBox);

		for (const FConversationImage& Img : Images)
		{
			// Decode base64 to create texture
			TArray<uint8> ImageData;
			FBase64::Decode(Img.Base64Data, ImageData);

			if (ImageData.Num() > 0)
			{
				// Create texture from PNG data
				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
				{
					TArray64<uint8> RawData;
					if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
					{
						int32 Width = ImageWrapper->GetWidth();
						int32 Height = ImageWrapper->GetHeight();

						// Create transient texture
						UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
						if (Texture)
						{
							void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
							FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
							Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
							Texture->UpdateResource();

							// Store texture in persistent array to prevent GC
							ImageTextures.Add(TStrongObjectPtr<UTexture2D>(Texture));

							// Create brush
							TSharedPtr<FSlateBrush> ImageBrush = MakeShareable(new FSlateBrush());
							ImageBrush->SetResourceObject(Texture);

							// Calculate display size (max 80px thumbnail, maintain aspect ratio)
							float MaxSize = 80.0f;
							float Scale = FMath::Min(MaxSize / Width, MaxSize / Height);
							float DisplayWidth = Width * Scale;
							float DisplayHeight = Height * Scale;

							ImageBrush->ImageSize = FVector2D(DisplayWidth, DisplayHeight);
							ImageBrush->DrawAs = ESlateBrushDrawType::Image;

							// Store brush in persistent array to prevent destruction
							ImageBrushes.Add(ImageBrush);

							ImageRow->AddSlot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(DisplayWidth)
								.HeightOverride(DisplayHeight)
								[
									SNew(SBorder)
									.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#333333")))))
									.Padding(2.0f)
									[
										SNew(SImage)
										.Image(ImageBrush.Get())
									]
								]
							];
						}
					}
				}
			}
		}

		UserMessageBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			ImageRow
		];
	}

	// Add text message if present
	if (!Message.IsEmpty())
	{
		UserMessageBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			CreateMarkdownWidget(
				Message,
				FCoreStyle::GetDefaultFontStyle("Regular", 10),
				FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)
			)
		];
	}

	return UserMessageBox;
}

TSharedRef<SWidget> SNeoStackChatArea::CreateAssistantHeaderWidget(const FString& AgentName, const FString& ModelName)
{
	FString HeaderText = FString::Printf(TEXT("Assistant • %s • %s"), *AgentName, *ModelName);

	return SNew(STextBlock)
		.Text(FText::FromString(HeaderText))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f));
}

TSharedRef<SWidget> SNeoStackChatArea::CreateContentWidget(const FString& Content)
{
	return CreateMarkdownWidget(
		Content,
		FCoreStyle::GetDefaultFontStyle("Regular", 10),
		FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)
	);
}

TSharedRef<SWidget> SNeoStackChatArea::CreateReasoningWidget(const FString& Reasoning)
{
	return SNew(SCollapsibleReasoningWidget)
		.Reasoning(Reasoning);
}

TSharedRef<SWidget> SNeoStackChatArea::CreateToolCallWidget(const FString& ToolName, const FString& Args, const FString& CallID)
{
	TSharedPtr<SCollapsibleToolWidget> ToolWidget;

	// Store tool info for later execution
	PendingToolCalls.Add(CallID, TPair<FString, FString>(ToolName, Args));

	// Check if this is a UE5 tool (has a session ID stored)
	FString* SessionIDPtr = ToolSessionIDs.Find(CallID);
	bool bIsUE5Tool = SessionIDPtr != nullptr;

	// Create widget with approval callbacks
	TSharedRef<SCollapsibleToolWidget> Widget = SAssignNew(ToolWidget, SCollapsibleToolWidget)
		.ToolName(ToolName)
		.Args(Args)
		.CallID(CallID)
		.RequiresApproval(bIsUE5Tool) // Only UE5 tools require approval
		.OnApproved_Lambda([this, ToolName, Args](const FString& InCallID, bool bAlwaysAllow)
		{
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool approved: %s (CallID: %s, AlwaysAllow: %d)"), *ToolName, *InCallID, bAlwaysAllow);
			// Execute the approved tool
			OnToolApprovedDelegate.ExecuteIfBound(InCallID, ToolName, Args);
		})
		.OnRejected_Lambda([this, ToolName](const FString& InCallID)
		{
			UE_LOG(LogTemp, Log, TEXT("[NeoStack] Tool rejected: %s (CallID: %s)"), *ToolName, *InCallID);
			OnToolRejectedDelegate.ExecuteIfBound(InCallID);
		});

	// Store the widget so we can update it with the result later
	if (!CallID.IsEmpty())
	{
		ToolWidgets.Add(CallID, ToolWidget);
	}

	return Widget;
}

TSharedRef<SWidget> SNeoStackChatArea::CreateToolResultWidget(const FString& Result)
{
	return SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#0f0f11")))))
		.Padding(10.0f, 8.0f)
		.BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#1a1a1d"))))
		[
			SNew(SHorizontalBox)
			// Success icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FNeoStackStyle::Get().GetBrush("NeoStack.ToolSuccessIcon"))
				.ColorAndOpacity(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#10b981"))))
				.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
			]
			// "Result" label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Result:")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
			]
			// Result text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Result))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SNeoStackChatArea::CreateMarkdownWidget(const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
{
	// Convert markdown to Slate's rich text format
	// # Heading -> <Credits.H2>Heading</>
	// ## Heading -> <RichTextBlock.Bold>Heading</>
	// **bold** -> <RichTextBlock.Bold>bold</>
	// *italic* -> <RichTextBlock.Italic>italic</>
	// `code` -> plain text for now (rich text doesn't support inline code well)

	FString RichText = Text;

	// 1) FIRST handle headings (line-based) - must be done before inline formatting
	TArray<FString> Lines;
	RichText.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	for (FString& Line : Lines)
	{
		// Check for ## first (more specific)
		if (Line.StartsWith(TEXT("## ")))
		{
			const FString Content = Line.RightChop(3); // strip "## "
			Line = FString::Printf(TEXT("<RichTextBlock.Bold>%s</>"), *Content);
		}
		else if (Line.StartsWith(TEXT("# ")))
		{
			const FString Content = Line.RightChop(2); // strip "# "
			Line = FString::Printf(TEXT("<Credits.H2>%s</>"), *Content);
		}
	}

	RichText = FString::Join(Lines, TEXT("\n"));

	// 2) THEN handle inline formatting (bold, italic, etc.)
	// Replace **bold** with rich text tags
	while (true)
	{
		int32 StartPos = RichText.Find(TEXT("**"));
		if (StartPos == INDEX_NONE) break;

		int32 EndPos = RichText.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartPos + 2);
		if (EndPos == INDEX_NONE) break;

		FString BoldText = RichText.Mid(StartPos + 2, EndPos - StartPos - 2);
		FString Replacement = FString::Printf(TEXT("<RichTextBlock.Bold>%s</>"), *BoldText);

		RichText = RichText.Left(StartPos) + Replacement + RichText.Mid(EndPos + 2);
	}

	// Replace *italic* with rich text tags (but not if part of **)
	int32 SearchPos = 0;
	while (SearchPos < RichText.Len())
	{
		int32 StartPos = RichText.Find(TEXT("*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
		if (StartPos == INDEX_NONE) break;

		// Skip if this is part of a rich text tag
		if (StartPos > 0 && RichText[StartPos - 1] == '*')
		{
			SearchPos = StartPos + 1;
			continue;
		}
		if (StartPos + 1 < RichText.Len() && RichText[StartPos + 1] == '*')
		{
			SearchPos = StartPos + 2;
			continue;
		}

		int32 EndPos = StartPos + 1;
		while (EndPos < RichText.Len())
		{
			if (RichText[EndPos] == '*' && (EndPos + 1 >= RichText.Len() || RichText[EndPos + 1] != '*'))
			{
				break;
			}
			EndPos++;
		}

		if (EndPos < RichText.Len())
		{
			FString ItalicText = RichText.Mid(StartPos + 1, EndPos - StartPos - 1);
			FString Replacement = FString::Printf(TEXT("<RichTextBlock.Italic>%s</>"), *ItalicText);

			RichText = RichText.Left(StartPos) + Replacement + RichText.Mid(EndPos + 1);
			SearchPos = StartPos + Replacement.Len();
		}
		else
		{
			break;
		}
	}

	// For code, just remove the backticks for now
	RichText.ReplaceInline(TEXT("`"), TEXT(""));

	return SNew(SRichTextBlock)
		.Text(FText::FromString(RichText))
		.TextStyle(FCoreStyle::Get(), "NormalText")
		.DecoratorStyleSet(&FCoreStyle::Get())
		.AutoWrapText(true);
}

void SNeoStackChatArea::ScrollToBottom()
{
	if (MessageScrollBox.IsValid())
	{
		MessageScrollBox->ScrollToEnd();
	}
}

#undef LOCTEXT_NAMESPACE
