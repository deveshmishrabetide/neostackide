// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Header widget for the NeoStack plugin
 */
class SNeoStackHeader : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNeoStackHeader)
		{}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Update the displayed cost */
	void SetCost(float Cost);

	/** Get the singleton instance */
	static TSharedPtr<SNeoStackHeader> Get();

private:
	float CurrentCost = 0.0f;

	/** Singleton instance for global access */
	static TWeakPtr<SNeoStackHeader> Instance;
};
