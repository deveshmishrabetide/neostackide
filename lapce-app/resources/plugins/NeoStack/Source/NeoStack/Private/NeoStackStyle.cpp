// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStackStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FNeoStackStyle::StyleInstance = nullptr;

void FNeoStackStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FNeoStackStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FNeoStackStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("NeoStackStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FNeoStackStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("NeoStackStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("NeoStack")->GetBaseDir() / TEXT("Resources"));

	Style->Set("NeoStack.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));

	// Register agent icons
	Style->Set("NeoStack.Agent.Orchestrator", new IMAGE_BRUSH_SVG(TEXT("MainIcons/orchestrator-icon"), Icon16x16));
	Style->Set("NeoStack.Agent.BlueprintAgent", new IMAGE_BRUSH_SVG(TEXT("MainIcons/blueprint-agent-icon"), Icon16x16));
	Style->Set("NeoStack.Agent.MaterialAgent", new IMAGE_BRUSH_SVG(TEXT("MainIcons/material-agent-icon"), Icon16x16));
	Style->Set("NeoStack.Agent.WidgetAgent", new IMAGE_BRUSH_SVG(TEXT("MainIcons/widget-agent-icon"), Icon16x16));

	// Register chat input icons
	Style->Set("NeoStack.SendIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/send-icon"), Icon16x16));
	Style->Set("NeoStack.AttachmentIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/attachment-icon"), Icon16x16));
	Style->Set("NeoStack.SettingsIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/settings-icon"), Icon16x16));

	// Register tool icons
	Style->Set("NeoStack.ToolIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/tool-icon"), Icon16x16));
	Style->Set("NeoStack.ToolSuccessIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/tool-success-icon"), Icon16x16));
	Style->Set("NeoStack.ArrowDownIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/arrow-down-icon"), Icon16x16));
	Style->Set("NeoStack.ArrowRightIcon", new IMAGE_BRUSH_SVG(TEXT("MainIcons/arrow-right-icon"), Icon16x16));

	return Style;
}

void FNeoStackStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FNeoStackStyle::Get()
{
	return *StyleInstance;
}
