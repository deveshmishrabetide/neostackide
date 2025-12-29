// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/** Tool execution state */
enum class EToolExecutionState : uint8
{
	PendingApproval,  // Waiting for user to accept/reject
	Executing,        // Tool is running
	Completed,        // Tool finished successfully
	Rejected,         // User rejected the tool
	Failed            // Tool execution failed
};

/** Delegate for tool approval - bool bAlwaysAllow indicates if tool should be auto-approved in future */
DECLARE_DELEGATE_TwoParams(FOnToolApproved, const FString& /* CallID */, bool /* bAlwaysAllow */);
DECLARE_DELEGATE_OneParam(FOnToolRejected, const FString& /* CallID */);

/**
 * Collapsible tool execution widget with approval UI
 */
class SCollapsibleToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCollapsibleToolWidget)
		: _ToolName(TEXT(""))
		, _Args(TEXT(""))
		, _CallID(TEXT(""))
		, _RequiresApproval(true)
		{}
		SLATE_ARGUMENT(FString, ToolName)
		SLATE_ARGUMENT(FString, Args)
		SLATE_ARGUMENT(FString, CallID)
		SLATE_ARGUMENT(bool, RequiresApproval)
		SLATE_EVENT(FOnToolApproved, OnApproved)
		SLATE_EVENT(FOnToolRejected, OnRejected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the result after tool execution completes */
	void SetResult(const FString& Result, bool bSuccess = true);

	/** Mark tool as executing (after approval) */
	void SetExecuting();

	/** Get the tool name */
	FString GetToolName() const { return ToolName; }

	/** Get the call ID */
	FString GetCallID() const { return CallID; }

	/** Global set of always-allowed tools */
	static TSet<FString>& GetAlwaysAllowedTools();

private:
	FReply OnToggleExpand();
	FReply OnAcceptClicked();
	FReply OnAlwaysAllowClicked();
	FReply OnRejectClicked();

	const FSlateBrush* GetExpandIcon() const;
	const FSlateBrush* GetStatusIcon() const;
	FSlateColor GetStatusColor() const;
	FText GetStatusText() const;
	EVisibility GetApprovalButtonsVisibility() const;

	bool bIsExpanded = true;
	bool bResultSet = false;  // Guard against duplicate result display
	EToolExecutionState ExecutionState = EToolExecutionState::PendingApproval;
	FString ToolName;
	FString Args;
	FString CallID;
	FString Result;

	FOnToolApproved OnApprovedDelegate;
	FOnToolRejected OnRejectedDelegate;

	TSharedPtr<SWidget> DetailsContainer;
	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<SHorizontalBox> ApprovalButtons;
	TSharedPtr<SImage> StatusIcon;
	TSharedPtr<STextBlock> StatusText;
};
