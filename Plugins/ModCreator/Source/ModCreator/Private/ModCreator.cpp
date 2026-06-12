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

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealEdMisc.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformProcess.h"

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

    CheckForMdkUpdate();
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

void FModCreatorModule::CheckForMdkUpdate()
{
#if WITH_EDITOR
    // 1. Load local version
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid())
    {
        RunPatchCheck();
        return;
    }

    const FString PluginDir = Plugin->GetBaseDir();
    const FString LocalVersionPath = FPaths::Combine(PluginDir, TEXT("Patch/MDKVersion.txt"));

    FString LocalVersion;
    if (!FPaths::FileExists(LocalVersionPath) ||
        !FFileHelper::LoadFileToString(LocalVersion, *LocalVersionPath))
    {
        RunPatchCheck();
        return;
    }

    LocalVersion = LocalVersion.TrimStartAndEnd();

    // 2. Remote version file on GitHub (CHANGE THIS TO YOUR REAL URL)
    const FString RemoteVersionUrl = FString::Printf(
        TEXT("https://api.github.com/repos/cronuxgamesavailable/OperationAirsoftMDK/releases/latest?ts=%lld"),
        FDateTime::UtcNow().ToUnixTimestamp()
    );
    const FString GitHubPageUrl = TEXT("https://github.com/cronuxgamesavailable/OperationAirsoftMDK");

    auto RunPatch = []()
        {
            FModCreatorModule& Mod = FModuleManager::LoadModuleChecked<FModCreatorModule>("ModCreator");
            Mod.RunPatchCheck();
        };

    // 3. Make HTTP request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(RemoteVersionUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("User-Agent"), TEXT("UnrealEngine-ModCreator"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/vnd.github+json"));

    Request->OnProcessRequestComplete().BindLambda(
        [LocalVersion, GitHubPageUrl, RunPatch](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded)
        {
            if (!bSucceeded || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                RunPatch();
                return;
            }

            FString RemoteVersion;

            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                RunPatch();
                return;
            }

            // GitHub Release title is "name"
            RemoteVersion = Root->GetStringField(TEXT("name"));
            RemoteVersion.TrimStartAndEndInline();

            // Fallback: if you ever leave Release title blank, use tag_name
            if (RemoteVersion.IsEmpty() && Root->HasField(TEXT("tag_name")))
            {
                RemoteVersion = Root->GetStringField(TEXT("tag_name"));
                RemoteVersion.TrimStartAndEndInline();
            }

            UE_LOG(LogTemp, Log, TEXT("MDK remote version returned: '%s' (local: '%s')"), *RemoteVersion, *LocalVersion);

            if (RemoteVersion.Equals(LocalVersion, ESearchCase::IgnoreCase))
            {
                RunPatch();
            }
            else
            {
                // 4. Show update popup
                FText Msg = FText::Format(
                    NSLOCTEXT(
                        "ModCreatorUpdate",
                        "UpdateMsg",
                        "A newer version of MDK is available.\n\n"
                        "Current version: {0}\n"
                        "Latest version:  {1}\n\n"
                        "Click 'Yes' to open GitHub and download it, or 'No' to skip."
                    ),
                    FText::FromString(LocalVersion),
                    FText::FromString(RemoteVersion)
                );

                EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, Msg);

                if (Result == EAppReturnType::Yes)
                {
                    FPlatformProcess::LaunchURL(*GitHubPageUrl, nullptr, nullptr);
                }

                // Continue to patch check regardless
                RunPatch();
            }
        });

    Request->ProcessRequest();
#else
    RunPatchCheck();
#endif
}

void FModCreatorModule::RunPatchCheck()
{
#if WITH_EDITOR
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid())
        return;

    const FString PatchDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Patch"));
    const FString PatchConfig = FPaths::Combine(PatchDir, TEXT("patch.json"));

    if (!FPaths::FileExists(PatchConfig))
        return;

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *PatchConfig))
        return;

    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
        return;

    const TArray<TSharedPtr<FJsonValue>>* FilesArray;
    if (!RootObj->TryGetArrayField(TEXT("Files"), FilesArray))
        return;

    struct FPatchInfo { FString Rel; FString Project; FString Patch; };
    TArray<FPatchInfo> NeedsPatch;

    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

    for (const auto& Entry : *FilesArray)
    {
        FString Rel = Entry->AsString();
        if (Rel.StartsWith(TEXT("/"))) Rel.RightChopInline(1);

        FString ProjectFile = FPaths::Combine(ProjectRoot, Rel);
        FString PatchFile = FPaths::Combine(PatchDir, Rel);

        if (!FPaths::FileExists(PatchFile))
            continue;

        bool bMismatch = false;

        if (!FPaths::FileExists(ProjectFile))
        {
            bMismatch = true;
        }
        else
        {
            // Special handling for DefaultGame.ini:
            // Only compare the [/Script/UnrealEd.ProjectPackagingSettings] section
            if (Rel.Equals(TEXT("Config/DefaultGame.ini"), ESearchCase::IgnoreCase))
            {
                FString ProjectText, PatchText;
                FFileHelper::LoadFileToString(ProjectText, *ProjectFile);
                FFileHelper::LoadFileToString(PatchText, *PatchFile);

                auto ExtractSection = [](const FString& Source, const FString& SectionHeader)
                    {
                        int32 Start = Source.Find(SectionHeader, ESearchCase::IgnoreCase, ESearchDir::FromStart);
                        if (Start == INDEX_NONE)
                        {
                            return FString(); // section not found
                        }

                        // Find the start of the next section: "\n["
                        int32 NextSection = Source.Find(
                            TEXT("\n["),
                            ESearchCase::IgnoreCase,
                            ESearchDir::FromStart,
                            Start + SectionHeader.Len()
                        );

                        if (NextSection == INDEX_NONE)
                        {
                            // Section goes to end of file
                            return Source.Mid(Start);
                        }

                        return Source.Mid(Start, NextSection - Start);
                    };

                const FString SectionHeader = TEXT("[/Script/UnrealEd.ProjectPackagingSettings]");
                FString ProjectSection = ExtractSection(ProjectText, SectionHeader);
                FString PatchSection = ExtractSection(PatchText, SectionHeader);

                // Trim whitespace so tiny formatting changes don't trigger a patch
                ProjectSection.TrimStartAndEndInline();
                PatchSection.TrimStartAndEndInline();

                if (!ProjectSection.Equals(PatchSection, ESearchCase::CaseSensitive))
                {
                    bMismatch = true;
                }
            }
            else
            {
                // Default behavior for all other files: compare full file bytes
                TArray<uint8> A, B;
                FFileHelper::LoadFileToArray(A, *ProjectFile);
                FFileHelper::LoadFileToArray(B, *PatchFile);

                if (A != B)
                {
                    bMismatch = true;
                }
            }
        }

        if (bMismatch)
            NeedsPatch.Add({ Rel, ProjectFile, PatchFile });
    }

    if (NeedsPatch.Num() == 0)
        return;

    FString FileList;
    for (auto& P : NeedsPatch)
        FileList += TEXT("\n - ") + P.Rel;

    FText Msg = FText::FromString(
        TEXT("Some project files dont match the MDK and need to be updated:\n") +
        FileList +
        TEXT("\n\nIf you choose to update, the patch will be applied automatically. \n\n")
        TEXT("Would you like to update these files now?")
    );

    EAppReturnType::Type R = FMessageDialog::Open(EAppMsgType::YesNo, Msg);
    if (R != EAppReturnType::Yes)
        return;

    IFileManager& FM = IFileManager::Get();
    for (auto& P : NeedsPatch)
    {
        // Special case: only update the [/Script/UnrealEd.ProjectPackagingSettings]
        // section inside Config/DefaultGame.ini
        if (P.Rel.Equals(TEXT("Config/DefaultGame.ini"), ESearchCase::IgnoreCase))
        {
            FString ProjectText, PatchText;
            if (!FFileHelper::LoadFileToString(ProjectText, *P.Project) ||
                !FFileHelper::LoadFileToString(PatchText, *P.Patch))
            {
                UE_LOG(LogTemp, Warning, TEXT("[ModCreator Patch] Failed to read DefaultGame.ini for section merge."));
                continue;
            }

            auto ExtractSection = [](const FString& Source, const FString& SectionHeader)
                {
                    int32 Start = Source.Find(SectionHeader, ESearchCase::IgnoreCase, ESearchDir::FromStart);
                    if (Start == INDEX_NONE)
                    {
                        return FString(); // section not found
                    }

                    int32 NextSection = Source.Find(
                        TEXT("\n["),
                        ESearchCase::IgnoreCase,
                        ESearchDir::FromStart,
                        Start + SectionHeader.Len()
                    );

                    if (NextSection == INDEX_NONE)
                    {
                        // Section goes to end of file
                        return Source.Mid(Start);
                    }

                    return Source.Mid(Start, NextSection - Start);
                };

            const FString SectionHeader = TEXT("[/Script/UnrealEd.ProjectPackagingSettings]");
            FString ProjectSection = ExtractSection(ProjectText, SectionHeader);
            FString PatchSection = ExtractSection(PatchText, SectionHeader);

            ProjectSection.TrimStartAndEndInline();
            PatchSection.TrimStartAndEndInline();

            if (PatchSection.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("[ModCreator Patch] Patch DefaultGame.ini has no ProjectPackagingSettings section."));
                continue;
            }

            // Rebuild ProjectText with the patch section merged in
            int32 Start = ProjectText.Find(SectionHeader, ESearchCase::IgnoreCase, ESearchDir::FromStart);
            FString NewContent;

            if (Start != INDEX_NONE)
            {
                int32 NextSection = ProjectText.Find(
                    TEXT("\n["),
                    ESearchCase::IgnoreCase,
                    ESearchDir::FromStart,
                    Start + SectionHeader.Len()
                );

                if (NextSection == INDEX_NONE)
                {
                    // Replace section to EOF
                    NewContent = ProjectText.Left(Start) + PatchSection + TEXT("\n");
                }
                else
                {
                    // Replace just this section
                    NewContent = ProjectText.Left(Start) + PatchSection + ProjectText.Mid(NextSection);
                }
            }
            else
            {
                // No packaging section in project yet – append it
                if (!ProjectText.EndsWith(TEXT("\n\n")))
                {
                    ProjectText += TEXT("\n\n");
                }
                NewContent = ProjectText + PatchSection + TEXT("\n");
            }

            FM.MakeDirectory(*FPaths::GetPath(P.Project), true);
            if (FFileHelper::SaveStringToFile(NewContent, *P.Project))
            {
                UE_LOG(LogTemp, Log, TEXT("[ModCreator Patch] Updated packaging section in %s"), *P.Project);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[ModCreator Patch] Failed to save merged DefaultGame.ini at %s"), *P.Project);
            }
        }
        else
        {
            // Default behavior for all other files: copy the entire patch file
            FM.MakeDirectory(*FPaths::GetPath(P.Project), true);
            FM.Copy(*P.Project, *P.Patch, true, true);
        }
    }

    // Restart editor
    FUnrealEdMisc::Get().RestartEditor(false);
#endif
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FModCreatorModule, ModCreator)