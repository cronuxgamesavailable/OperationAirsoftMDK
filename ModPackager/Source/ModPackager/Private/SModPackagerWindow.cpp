#include "SModPackagerWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"

void SModPackagerWindow::Construct(const FArguments& InArgs)
{
    // Populate combo box options
    ModTypeOptions.Add(MakeShared<FString>("Map"));
    ModTypeOptions.Add(MakeShared<FString>("Mesh"));

    MapSubtypes.Add(MakeShared<FString>("Base Map"));
    MapSubtypes.Add(MakeShared<FString>("Layout"));

    MeshSubtypes.Add(MakeShared<FString>("Gun Part"));
    MeshSubtypes.Add(MakeShared<FString>("Character Part"));

    SelectedModType = ModTypeOptions[0];
    SelectedModSubtype = MapSubtypes[0];

    // UI
    ChildSlot
        [
            SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight().Padding(4, 10)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString("Welcome to the Mod Packager!"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                        .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString("Fill in the mod details below and select your map or mesh to package it for use in the game."))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                        .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(4, 10)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString("Mod Details"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                        .ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
                ]

                // Mod Name
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString("Mod Name"))
                                .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(" *"))
                                .ColorAndOpacity(FLinearColor::Red)
                        ]
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(ModNameTextBox, SEditableTextBox)
                        .ToolTipText(FText::FromString("The name of your mod as it will appear in-game and on listings."))
                ]

                // Mod Type
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString("Mod Type"))
                                .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(" *"))
                                .ColorAndOpacity(FLinearColor::Red)
                        ]
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(ModTypeComboBox, SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&ModTypeOptions)
                        .OnGenerateWidget(this, &SModPackagerWindow::GenerateModTypeComboItem)
                        .OnSelectionChanged(this, &SModPackagerWindow::OnModTypeChanged)
                        .ToolTipText(FText::FromString("Choose whether your mod is a map or a mesh."))
                        [
                            SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(*SelectedModType);
                                })
                        ]
                ]

            // Mod Subtype
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString("Mod Subtype"))
                                .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(" *"))
                                .ColorAndOpacity(FLinearColor::Red)
                        ]
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(ModSubtypeComboBox, SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&MapSubtypes)
                        .OnGenerateWidget(this, &SModPackagerWindow::GenerateModSubtypeComboItem)
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type) {
                        SelectedModSubtype = NewSelection;
                            })
                        .ToolTipText_Lambda([this]() -> FText {
                        if (SelectedModType.IsValid() && *SelectedModType == "Map")
                            return FText::FromString("Further categorize your map mod: Base Map or Layout.");
                        else if (SelectedModType.IsValid() && *SelectedModType == "Mesh")
                            return FText::FromString("Further categorize your mesh mod: Gun Part or Character Part.");
                        return FText::FromString("Select a valid mod type first.");
                            })
                        [
                            SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(*SelectedModSubtype);
                                })
                        ]
                ]

            // Author
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(STextBlock).Text(FText::FromString("Author Name"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(AuthorTextBox, SEditableTextBox)
                        .ToolTipText(FText::FromString("Your name or alias to be shown as the mod author."))
                ]

                // Version
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(STextBlock).Text(FText::FromString("Version"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(VersionTextBox, SEditableTextBox)
                        .ToolTipText(FText::FromString("The version number of your mod (e.g., 1.0, 1.1-beta)."))
                ]

                // Description
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(STextBlock).Text(FText::FromString("Description"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(DescriptionTextBox, SMultiLineEditableTextBox)
                        .ToolTipText(FText::FromString("Write a short description about your mod and its contents."))
                ]

                // Asset
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString("Selected Asset"))
                                .ColorAndOpacity(FLinearColor::White)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(" *"))
                                .ColorAndOpacity(FLinearColor::Red)
                        ]
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(AssetSelectButton, SButton)
                        .Text_Lambda([this]() -> FText {
                        return SelectedAssetPath.IsValid() ? FText::FromString(*SelectedAssetPath) : FText::FromString("Select Asset");
                            })
                        .ToolTipText(FText::FromString("Click to choose the map or mesh you want to include in your mod package."))
                        .OnClicked_Lambda([this]() -> FReply {
                        RefreshAvailableAssets();
                        FMenuBuilder MenuBuilder(true, nullptr);
                        for (const FString& Asset : FoundAssets)
                        {
                            FUIAction ItemAction(FExecuteAction::CreateLambda([this, Asset]() {
                                OnAssetSelected(Asset);
                                }));
                            MenuBuilder.AddMenuEntry(FText::FromString(Asset), FText::GetEmpty(), FSlateIcon(), ItemAction);
                        }
                        FSlateApplication::Get().PushMenu(
                            AssetSelectButton.ToSharedRef(),
                            FWidgetPath(),
                            MenuBuilder.MakeWidget(),
                            FSlateApplication::Get().GetCursorPos(),
                            FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
                        );
                        return FReply::Handled();
                            })
                ]

            // Package Button
            + SVerticalBox::Slot().AutoHeight().Padding(10, 20, 10, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().HAlign(HAlign_Right).AutoWidth()
                        [
                            SNew(SButton)
                                .Text(FText::FromString("Package Mod"))
                                .ToolTipText(FText::FromString("Click to package your mod using the details and selected asset."))
                                .ButtonColorAndOpacity(FLinearColor(0.2f, 0.4f, 0.9f))
                                .OnClicked_Lambda([this]() -> FReply {

                                bool bValid = true;
                                if (ModNameTextBox->GetText().IsEmpty()) bValid = false;
                                if (!SelectedModType.IsValid()) bValid = false;
                                if (!SelectedModSubtype.IsValid()) bValid = false;
                                if (!SelectedAssetPath.IsValid()) bValid = false;

                                if (!bValid)
                                {
                                    ShowOrUpdateNotification(FText::FromString("Packaging failed. Missing required fields."), SNotificationItem::CS_Fail);
                                    return FReply::Handled();
                                }

                                PackageMod();
                                return FReply::Handled();
                                    })
                        ]
                ]
        ];
}

TSharedRef<SWidget> SModPackagerWindow::GenerateModTypeComboItem(TSharedPtr<FString> InItem)
{
    return SNew(STextBlock).Text(FText::FromString(*InItem));
}

TSharedRef<SWidget> SModPackagerWindow::GenerateModSubtypeComboItem(TSharedPtr<FString> InItem)
{
    return SNew(STextBlock).Text(FText::FromString(*InItem));
}

TArray<TSharedPtr<FString>>* SModPackagerWindow::GetCurrentSubtypeOptions()
{
    return (*SelectedModType == "Map") ? &MapSubtypes : &MeshSubtypes;
}

void SModPackagerWindow::OnModTypeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type)
{
    if (!NewSelection.IsValid()) return;

    SelectedModType = NewSelection;

    if (*SelectedModType == "Map")
    {
        SelectedModSubtype = MapSubtypes[0];
    }
    else
    {
        SelectedModSubtype = MeshSubtypes[0];
    }

    ModSubtypeComboBox->RefreshOptions();
    ModSubtypeComboBox->SetSelectedItem(SelectedModSubtype);
}

void SModPackagerWindow::PackageMod()
{
    ShowOrUpdateNotification(FText::FromString(TEXT("Packaging Mod...")), SNotificationItem::CS_Pending);

    // Collect data from UI
    FString ModName = ModNameTextBox->GetText().ToString();
    FString ModType = SelectedModType.IsValid() ? *SelectedModType : TEXT("");
    FString ModSubtype = SelectedModSubtype.IsValid() ? *SelectedModSubtype : TEXT("");
    FString Author = AuthorTextBox->GetText().ToString();
    FString Version = VersionTextBox->GetText().ToString();
    FString Description = DescriptionTextBox->GetText().ToString();
    FString AssetPath = SelectedAssetPath.IsValid() ? *SelectedAssetPath : TEXT("");

    // Create modinfo.json
    TSharedRef<FJsonObject> ModInfoJson = MakeShared<FJsonObject>();
    ModInfoJson->SetStringField(TEXT("Name"), ModName);
    ModInfoJson->SetStringField(TEXT("Type"), ModType);
    ModInfoJson->SetStringField(TEXT("Subtype"), ModSubtype);
    ModInfoJson->SetStringField(TEXT("Author"), Author);
    ModInfoJson->SetStringField(TEXT("Version"), Version);
    ModInfoJson->SetStringField(TEXT("Description"), Description);

    FString ModInfoString;
    TSharedRef<TJsonWriter<>> ModInfoWriter = TJsonWriterFactory<>::Create(&ModInfoString);
    FJsonSerializer::Serialize(ModInfoJson, ModInfoWriter);

    // Output path setup
    FString OutputDir = FPaths::ProjectDir() / TEXT("Mods") / ModName;
    FString ModInfoPath = OutputDir / TEXT("modinfo.json");
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    if (FFileHelper::SaveStringToFile(ModInfoString, *ModInfoPath))
    {
        ShowOrUpdateNotification(FText::FromString(TEXT("modinfo.json saved successfully!")), SNotificationItem::CS_Success);
    }
    else
    {
        ShowOrUpdateNotification(FText::FromString(TEXT("Failed to save modinfo.json")), SNotificationItem::CS_Fail);

    }

    // Handle mapinfo.json or meshinfo.json
    if (!AssetPath.IsEmpty())
    {
        FString InfoFileName;
        if (ModType == "Map")
        {
            InfoFileName = (ModSubtype == "Layout") ? TEXT("layoutinfo.json") : TEXT("mapinfo.json");

            // Load the map package
            FString MapPackagePath = AssetPath;
            UPackage* Package = LoadPackage(nullptr, *MapPackagePath, LOAD_None);
            if (!Package)
            {
                ShowOrUpdateNotification(FText::FromString(TEXT("Failed to load map package")), SNotificationItem::CS_Fail);
                return;
            }

            UWorld* LoadedWorld = UWorld::FindWorldInPackage(Package);
            if (!LoadedWorld)
            {
                ShowOrUpdateNotification(FText::FromString(TEXT("Failed to find world in map package")), SNotificationItem::CS_Fail);
                return;
            }

            // Iterate actors
            TArray<TSharedPtr<FJsonValue>> ActorArray;
            for (TActorIterator<AActor> ActorItr(LoadedWorld); ActorItr; ++ActorItr)
            {
                AActor* Actor = *ActorItr;
                if (!Actor) continue;

                TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
                ActorJson->SetStringField(TEXT("Name"), Actor->GetName());
                ActorJson->SetStringField(TEXT("Class"), Actor->GetClass()->GetName());
                ActorJson->SetStringField(TEXT("Location"), Actor->GetActorLocation().ToString());
                ActorJson->SetStringField(TEXT("Rotation"), Actor->GetActorRotation().ToString());
                ActorJson->SetStringField(TEXT("Scale"), Actor->GetActorScale3D().ToString());

                ActorArray.Add(MakeShared<FJsonValueObject>(ActorJson));
            }

            // Wrap it in mapinfo object
            TSharedRef<FJsonObject> MapInfoJson = MakeShared<FJsonObject>();
            MapInfoJson->SetArrayField(TEXT("Actors"), ActorArray);

            FString MapInfoOutput;
            TSharedRef<TJsonWriter<>> MapInfoWriter = TJsonWriterFactory<>::Create(&MapInfoOutput);
            FJsonSerializer::Serialize(MapInfoJson, MapInfoWriter);

            FString InfoPath = OutputDir / InfoFileName;
            if (FFileHelper::SaveStringToFile(MapInfoOutput, *InfoPath))
            {
                ShowOrUpdateNotification(FText::FromString(FString::Printf(TEXT("%s saved successfully!"), *InfoFileName)), SNotificationItem::CS_Success);
            }
            else
            {
                ShowOrUpdateNotification(FText::FromString(FString::Printf(TEXT("Failed to save %s"), *InfoFileName)), SNotificationItem::CS_Fail);
            }
        }
        else if (ModType == "Mesh")
        {
            // Just basic asset path dump for now
            InfoFileName = TEXT("meshinfo.json");

            TSharedRef<FJsonObject> InfoJson = MakeShared<FJsonObject>();
            InfoJson->SetStringField(TEXT("AssetPath"), AssetPath);

            FString InfoString;
            TSharedRef<TJsonWriter<>> InfoWriter = TJsonWriterFactory<>::Create(&InfoString);
            FJsonSerializer::Serialize(InfoJson, InfoWriter);

            FString InfoPath = OutputDir / InfoFileName;
            if (FFileHelper::SaveStringToFile(InfoString, *InfoPath))
            {
                ShowOrUpdateNotification(FText::FromString(FString::Printf(TEXT("%s saved successfully!"), *InfoFileName)), SNotificationItem::CS_Success);
            }
            else
            {
                ShowOrUpdateNotification(FText::FromString(FString::Printf(TEXT("Failed to save %s"), *InfoFileName)), SNotificationItem::CS_Fail);

            }
        }
        FinishNotification(TEXT("Mod packaged successfully!"), OutputDir);
    }
}

void SModPackagerWindow::ShowOrUpdateNotification(const FText& Message, SNotificationItem::ECompletionState State)
{
    if (!NotificationItem.IsValid())
    {
        FNotificationInfo Info(Message);
        Info.FadeInDuration = 0.1f;
        Info.FadeOutDuration = 1.0f;
        Info.ExpireDuration = 3.0f;
        Info.bUseLargeFont = false;
        Info.Image = FCoreStyle::Get().GetBrush(State == SNotificationItem::CS_Fail ? "MessageLog.Error" : "MessageLog.Success");

        NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
        if (NotificationItem.IsValid())
        {
            NotificationItem->SetCompletionState(State);
        }
    }
    else
    {
        NotificationItem->SetText(Message);
        NotificationItem->SetCompletionState(State);
        NotificationItem->ExpireAndFadeout();
    }
}

void SModPackagerWindow::FinishNotification(const FString& FinalMessage, const FString& FolderToOpen)
{
    // Clear old one if any
    if (ActiveNotification.IsValid())
    {
        ActiveNotification->ExpireAndFadeout();
        ActiveNotification.Reset();
    }

    FNotificationInfo Info(FText::FromString(FinalMessage));
    Info.bUseLargeFont = false;
    Info.FadeOutDuration = 2.0f;
    Info.ExpireDuration = 10.0f;
    Info.bFireAndForget = true;

    // ✅ Add button to open folder
    if (!FolderToOpen.IsEmpty())
    {
        Info.ButtonDetails.Add(FNotificationButtonInfo(
            FText::FromString("Open Folder"),
            FText::FromString("Opens the mod output directory"),
            FSimpleDelegate::CreateLambda([FolderToOpen]() {
                FPlatformProcess::ExploreFolder(*FolderToOpen);
                })
        ));
    }

    // ✅ Now add the notification and do NOT change it afterward
    ActiveNotification = FSlateNotificationManager::Get().AddNotification(Info);
    if (ActiveNotification.IsValid())
    {
        ActiveNotification->SetCompletionState(SNotificationItem::CS_Success);
        // ❌ Do NOT call SetText here or you will wipe out the button
    }
}

void SModPackagerWindow::RefreshAvailableAssets()
{
    FoundAssets.Empty();

    if (!SelectedModType.IsValid())
        return;

    FString AssetExtension = (*SelectedModType == "Map") ? TEXT(".umap") : TEXT(".uasset");

    const FString ContentPath = FPaths::ProjectContentDir();
    IFileManager& FileManager = IFileManager::Get();

    TArray<FString> AllFiles;
    FileManager.FindFilesRecursive(AllFiles, *ContentPath, *FString::Printf(TEXT("*%s"), *AssetExtension), true, false);

    for (FString& Path : AllFiles)
    {
        // Convert to relative path starting from /Game/
        FString RelativePath = Path;
        FPaths::MakePathRelativeTo(RelativePath, *ContentPath);
        RelativePath = FString("/Game/") + FPaths::ChangeExtension(RelativePath, TEXT(""));
        FoundAssets.Add(RelativePath);
    }
}

void SModPackagerWindow::OnAssetSelected(FString Asset)
{
    SelectedAssetPath = MakeShared<FString>(Asset);
}