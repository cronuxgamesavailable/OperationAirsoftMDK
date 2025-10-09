#include "ModCreator.h"
#include "ModCreatorStyle.h"
#include "ModCreatorCommands.h"
#include "ModCreatorCreateWindow.h"
#include "PakCreatorWindow.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "PakCreatorStyle.h"
#include "PakCreatorCommands.h"

#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Misc/MessageDialog.h"

static TSharedPtr<FPakCreatorWindow> GPakCreatorWindow;

#define LOCTEXT_NAMESPACE "FModCreatorModule"

void FModCreatorModule::StartupModule()
{
    // Icons
    FModCreatorStyle::Initialize();

    // Create Window
    FModCreatorCreateWindow::Register();
    
    // Init PakCreator UI bits now that they live in this module
    FPakCreatorStyle::Initialize();
    FPakCreatorStyle::ReloadTextures();
    FPakCreatorCommands::Register();

    // --- expose the PakCreator window so we can open it from our button ---
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        "PakCreatorWindow",
        FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
            {
                GPakCreatorWindow = MakeShared<FPakCreatorWindow>();
                return GPakCreatorWindow->OnSpawnPluginTab(Args);
            }))
        .SetDisplayName(NSLOCTEXT("PakCreator", "TabTitle", "Pak Creator"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    // Register UI commands (like the template)
    FModCreatorCommands::Register();

    // Map actions
    PluginCommands = MakeShareable(new FUICommandList);

    PluginCommands->MapAction(
        FModCreatorCommands::Get().CreateAction,
        FExecuteAction::CreateRaw(this, &FModCreatorModule::OnCreateClicked));

    PluginCommands->MapAction(
        FModCreatorCommands::Get().PackageAction,
        FExecuteAction::CreateRaw(this, &FModCreatorModule::OnPackageClicked));

    // ToolMenus callbacks
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FModCreatorModule::RegisterMenus)
    );
}

void FModCreatorModule::ShutdownModule()
{
    if (UToolMenus::Get())
    {
        UToolMenus::UnregisterOwner(this);
    }
    FModCreatorCreateWindow::Unregister();
    FModCreatorStyle::Shutdown();
    GPakCreatorWindow.Reset();
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("PakCreatorWindow");

    FPakCreatorCommands::Unregister();
    FPakCreatorStyle::Shutdown();

    FModCreatorCommands::Unregister();
    PluginCommands.Reset();
}

void FModCreatorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    // === Window menu === (same pattern as your test plugin)
    if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        // Put under WindowLayout to mirror the template; add a labeled section
        FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
        Section.AddSeparator("ModCreator_WindowSep");
        {
            FToolMenuSection& MC = Menu->AddSection("ModCreator_Window", LOCTEXT("MCWindow", "Mod Tools"));
            MC.AddMenuEntry(
                "ModCreator_Create_Menu",
                LOCTEXT("CreateMenuLabel", "Create Mod"),
                LOCTEXT("CreateMenuTooltip", "Open the Mod Creator."),
                FSlateIcon(FModCreatorStyle::GetStyleSetName(), "ModCreator.Create.Small", "ModCreator.Create.Large"),
                FUIAction(FExecuteAction::CreateRaw(this, &FModCreatorModule::OnCreateClicked))
            );
            MC.AddMenuEntry(
                "ModCreator_Package_Menu",
                LOCTEXT("PackageMenuLabel", "Package Mod"),
                LOCTEXT("PackageMenuTooltip", "Package the selected mod."),
                FSlateIcon(FModCreatorStyle::GetStyleSetName(), "ModCreator.Package.Small", "ModCreator.Package.Large"),
                FUIAction(FExecuteAction::CreateRaw(this, &FModCreatorModule::OnPackageClicked))
            );
        }
    }

    // === Toolbar === (exactly like the template: PlayToolBar / PluginTools)
    if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar"))
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
        {
            // Create Mod first
            Section.AddEntry(
                FToolMenuEntry::InitToolBarButton(
                    "ModCreator_Create_Toolbar",
                    FUIAction(FExecuteAction::CreateRaw(this, &FModCreatorModule::OnCreateClicked)),
                    LOCTEXT("CreateToolbarLabel", "Create Mod"),
                    LOCTEXT("CreateToolbarTooltip", "Open the Mod Creator to make a new mod."),
                    FSlateIcon(FModCreatorStyle::GetStyleSetName(), "ModCreator.Create.Small", "ModCreator.Create.Large")
                )
            );
            // Package Mod second
            Section.AddEntry(
                FToolMenuEntry::InitToolBarButton(
                    "ModCreator_Package_Toolbar",
                    FUIAction(FExecuteAction::CreateRaw(this, &FModCreatorModule::OnPackageClicked)),
                    LOCTEXT("PackageToolbarLabel", "Package Mod"),
                    LOCTEXT("PackageToolbarTooltip", "Package the selected mod for distribution."),
                    FSlateIcon(FModCreatorStyle::GetStyleSetName(), "ModCreator.Package.Small", "ModCreator.Package.Large")
                )
            );
        }
    }
}

void FModCreatorModule::OnCreateClicked() const
{
    FGlobalTabmanager::Get()->TryInvokeTab(FModCreatorCreateWindow::TabName);
}

void FModCreatorModule::OnPackageClicked() const
{
    FGlobalTabmanager::Get()->TryInvokeTab(FName("PakCreatorWindow"));
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FModCreatorModule, ModCreator)