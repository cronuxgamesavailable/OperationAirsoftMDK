#include "ModPackager.h"
#include "ModPackagerStyle.h"
#include "ModPackagerCommands.h"
#include "SModPackagerWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

static const FName ModPackagerTabName("ModPackager");

#define LOCTEXT_NAMESPACE "FModPackagerModule"

void FModPackagerModule::StartupModule()
{
    FModPackagerStyle::Initialize();
    FModPackagerCommands::Register();

    // 🔹 Create the command list and bind the button click
    PluginCommands = MakeShareable(new FUICommandList);

    PluginCommands->MapAction(
        FModPackagerCommands::Get().OpenPluginWindow,
        FExecuteAction::CreateRaw(this, &FModPackagerModule::PluginButtonClicked),
        FCanExecuteAction());

    // 🔹 Register the menus
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FModPackagerModule::RegisterMenus));
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ModPackagerTabName,
        FOnSpawnTab::CreateRaw(this, &FModPackagerModule::OnSpawnPluginTab))
        .SetDisplayName(FText::FromString("Mod Packager"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FModPackagerModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    FModPackagerStyle::Shutdown();
    FModPackagerCommands::Unregister();
}

void FModPackagerModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    // Add to main menu
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    {
        FToolMenuSection& Section = Menu->AddSection("ModPackagerSection", LOCTEXT("ModPackagerHeader", "Mod Tools"));

        FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
            FModPackagerCommands::Get().OpenPluginWindow,
            LOCTEXT("ModPackagerLabel", "Mod Packager"),
            LOCTEXT("ModPackagerTooltip", "Open the Mod Packager Tool"),
            FSlateIcon(FModPackagerStyle::GetStyleSetName(), "ModPackager.Icon")
        );
        Entry.SetCommandList(PluginCommands);
        Section.AddEntry(Entry);
    }

    // ✅ FIX: Add to correct toolbar section
    UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");

        FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
            FModPackagerCommands::Get().OpenPluginWindow,
            LOCTEXT("ModPackagerLabel", "ModPackager"),
            LOCTEXT("ModPackagerTooltip", "Open the ModPackager tool"),
            FSlateIcon(FModPackagerStyle::GetStyleSetName(), "ModPackager.Icon")
        );

        Entry.SetCommandList(PluginCommands);
        Section.AddEntry(Entry);
    }
}

void FModPackagerModule::PluginButtonClicked()
{
    FGlobalTabmanager::Get()->TryInvokeTab(ModPackagerTabName);
}

TSharedRef<SDockTab> FModPackagerModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SModPackagerWindow)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FModPackagerModule, ModPackager)