// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeoStack.h"
#include "NeoStackStyle.h"
#include "NeoStackCommands.h"
#include "SNeoStackWidget.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

static const FName NeoStackTabName("NeoStack");

#define LOCTEXT_NAMESPACE "FNeoStackModule"

void FNeoStackModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FNeoStackStyle::Initialize();
	FNeoStackStyle::ReloadTextures();

	FNeoStackCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FNeoStackCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FNeoStackModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FNeoStackModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(NeoStackTabName, FOnSpawnTab::CreateRaw(this, &FNeoStackModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FNeoStackTabTitle", "NeoStack"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FNeoStackModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FNeoStackStyle::Shutdown();

	FNeoStackCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NeoStackTabName);
}

TSharedRef<SDockTab> FNeoStackModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SNeoStackWidget)
		];
}

void FNeoStackModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(NeoStackTabName);
}

void FNeoStackModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FNeoStackCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FNeoStackCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNeoStackModule, NeoStack)