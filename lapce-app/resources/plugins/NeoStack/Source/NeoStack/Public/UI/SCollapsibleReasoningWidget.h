// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Collapsible reasoning widget
 */
class SCollapsibleReasoningWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCollapsibleReasoningWidget)
		: _Reasoning(TEXT(""))
		{}
		SLATE_ARGUMENT(FString, Reasoning)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Update reasoning text (for streaming) */
	void UpdateReasoning(const FString& NewReasoning);

private:
	FReply OnToggleExpand();
	const FSlateBrush* GetExpandIcon() const;

	bool bIsExpanded = true; // Start expanded
	FString Reasoning;

	TSharedPtr<SWidget> DetailsContainer;
	TSharedPtr<class STextBlock> ReasoningTextBlock;
};
