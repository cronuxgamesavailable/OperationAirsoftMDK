// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookContent.h"
#include "CookContentStyle.h"
#include "CookContentCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "SCookContentWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "LevelEditor.h"

static const FName CookContentTabName("CookContent");

#define LOCTEXT_NAMESPACE "FCookContentModule"

void FCookContentModule::StartupModule()
{
	FCookContentStyle::Initialize();
	FCookContentStyle::ReloadTextures();

	FCookContentCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FCookContentCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FCookContentModule::PluginButtonClicked),
		FCanExecuteAction());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CookContentTabName, FOnSpawnTab::CreateRaw(this, &FCookContentModule::OnSpawnCookContentTab))
		.SetDisplayName(FText::FromString("Cook Content"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCookContentModule::RegisterMenus));
}

void FCookContentModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CookContentTabName);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FCookContentStyle::Shutdown();
	FCookContentCommands::Unregister();
}

void FCookContentModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(CookContentTabName);
}

TSharedRef<SDockTab> FCookContentModule::OnSpawnCookContentTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SCookContentWindow)
		];
}

void FCookContentModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntryWithCommandList(FCookContentCommands::Get().PluginAction, PluginCommands);
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
		FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FCookContentCommands::Get().PluginAction));
		Entry.SetCommandList(PluginCommands);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCookContentModule, CookContent)