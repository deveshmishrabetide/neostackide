// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "UObject/StrongObjectPtr.h"

// Forward declarations
struct FContextItem;

/**
 * Attached context file reference
 */
struct FAttachedContext
{
	FString DisplayName;
	FString FullPath;
	FString FileContent; // Loaded when message is sent
};

/**
 * Attached image data
 */
struct FAttachedImage
{
	/** Raw image data as PNG bytes */
	TArray<uint8> ImageData;

	/** Base64 encoded image data */
	FString Base64Data;

	/** MIME type (e.g., "image/png") */
	FString MimeType = TEXT("image/png");

	/** Thumbnail brush for display */
	TSharedPtr<FSlateBrush> ThumbnailBrush;

	/** Texture for the thumbnail - using TStrongObjectPtr for proper UObject lifecycle */
	TStrongObjectPtr<class UTexture2D> ThumbnailTexture;
};

/**
 * Chat input widget for the NeoStack plugin
 */
class SNeoStackChatInput : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackChatInput)
		: _Sidebar(nullptr)
		, _ChatArea(nullptr)
		{}
		SLATE_ARGUMENT(TSharedPtr<class SNeoStackSidebar>, Sidebar)
		SLATE_ARGUMENT(TSharedPtr<class SNeoStackChatArea>, ChatArea)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Override to handle keyboard input */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Override to support keyboard focus */
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Get the currently attached images */
	const TArray<FAttachedImage>& GetAttachedImages() const { return AttachedImages; }

	/** Clear all attached images */
	void ClearAttachedImages();

private:
	/** Input text box widget reference */
	TSharedPtr<SMultiLineEditableTextBox> InputTextBox;

	/** Reference to sidebar for accessing agent/model selection */
	TSharedPtr<class SNeoStackSidebar> SidebarPtr;

	/** Reference to chat area for adding messages */
	TSharedPtr<class SNeoStackChatArea> ChatAreaPtr;

	/** Container for image previews */
	TSharedPtr<class SHorizontalBox> ImagePreviewContainer;

	/** Container for context tags */
	TSharedPtr<class SHorizontalBox> ContextTagsContainer;

	/** Currently attached images */
	TArray<FAttachedImage> AttachedImages;

	/** Currently attached context files */
	TArray<FAttachedContext> AttachedContexts;

	/** Context popup widget */
	TSharedPtr<class SNeoStackContextPopup> ContextPopup;

	/** Menu anchor for context popup */
	TSharedPtr<class SMenuAnchor> ContextMenuAnchor;

	/** Is context popup currently visible */
	bool bContextPopupVisible = false;

	/** Position where @ was typed (for filter extraction) */
	int32 AtSymbolPosition = -1;

	/** Called when send button is clicked */
	FReply OnSendClicked();

	/** Called when text changes */
	void OnTextChanged(const FText& InText);

	/** Called when text is committed */
	void OnTextCommitted(const FText& InText, ETextCommit::Type CommitType);

	/** Handle key down in text box - intercepts Up/Down for popup navigation */
	FReply HandleTextBoxKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

	/** Try to paste image from clipboard */
	bool TryPasteImageFromClipboard();

	/** Add an image attachment */
	void AddImageAttachment(const TArray<uint8>& ImageData);

	/** Remove an image attachment by index */
	void RemoveImageAttachment(int32 Index);

	/** Update the image preview UI */
	void UpdateImagePreviewUI();

	/** Create thumbnail texture from image data */
	UTexture2D* CreateThumbnailTexture(const TArray<uint8>& ImageData);

	/** Convert image data to base64 */
	static FString ImageDataToBase64(const TArray<uint8>& ImageData);

	/** Get visibility of image preview container */
	EVisibility GetImagePreviewVisibility() const;

	/** Get visibility of context tags container */
	EVisibility GetContextTagsVisibility() const;

	/** Show the context popup */
	void ShowContextPopup();

	/** Hide the context popup */
	void HideContextPopup();

	/** Handle context item selection */
	void OnContextItemSelected(const FContextItem& Item);

	/** Add a context file reference */
	void AddContextReference(const FString& DisplayName, const FString& FullPath);

	/** Remove a context reference by index */
	void RemoveContextReference(int32 Index);

	/** Update the context tags UI */
	void UpdateContextTagsUI();

	/** Clear all context references */
	void ClearContextReferences();

	/** Check for @ symbol and handle context popup */
	void CheckForContextTrigger(const FString& Text);

	/** Get content widget for menu anchor */
	TSharedRef<SWidget> GetContextPopupContent();

	/** Load file content for all attached contexts */
	void LoadContextFileContents();
};
