#include "SCookContentWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Async/Async.h"
#include "EditorStyleSet.h"

void SCookContentWindow::Construct(const FArguments& InArgs)
{
    FAssetPickerConfig AssetPickerConfig;
    AssetPickerConfig.Filter.bRecursiveClasses = true;
    AssetPickerConfig.Filter.bRecursivePaths = true;
    AssetPickerConfig.Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());           // Maps
    AssetPickerConfig.Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());     // Meshes
    AssetPickerConfig.Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName()); // Materials
    AssetPickerConfig.SelectionMode = ESelectionMode::Multi;
    AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &SCookContentWindow::OnAssetSelected);
    AssetPickerConfig.bAllowNullSelection = true;
    AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;

    ChildSlot
        [
            SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox)
                        .HeightOverride(400)
                        [
                            FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().CreateAssetPicker(AssetPickerConfig)
                        ]
                ]
            + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton)
                        .Text(FText::FromString("Cook Selected Assets"))
                        .OnClicked(this, &SCookContentWindow::OnCookContentClicked)
                ]
        ];
}

void SCookContentWindow::OnAssetSelected(const FAssetData& AssetData)
{
    SelectedAssets.AddUnique(AssetData);
}

FReply SCookContentWindow::OnCookContentClicked()
{
    if (SelectedAssets.Num() == 0)
    {
        ShowNotification(TEXT("No assets selected to cook."), SNotificationItem::CS_Fail);
        return FReply::Handled();
    }

    FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FString UATPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));

    FString CookList;
    for (const FAssetData& Asset : SelectedAssets)
    {
        FString PackagePath = Asset.PackageName.ToString();
        CookList += FString::Printf(TEXT(" -map=\"%s\""), *PackagePath);
    }

    CookingNotification = ShowNotification(TEXT("Cooking in progress..."), SNotificationItem::CS_Pending, false);

    Async(EAsyncExecution::Thread, [this, ProjectPath, UATPath, CookList]() mutable
        {
            void* ReadPipe = nullptr;
            void* WritePipe = nullptr;
            FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

            FString UATArguments = FString::Printf(
                TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=Win64 -clientconfig=Development -cook -stage -archive -compressed%s"),
                *ProjectPath,
                *CookList
            );

            FProcHandle ProcHandle = FPlatformProcess::CreateProc(
                *UATPath,
                *UATArguments,
                true, false, false,
                nullptr, 0, nullptr,
                WritePipe
            );

            if (ProcHandle.IsValid())
            {
                FString OutputBuffer;

                while (FPlatformProcess::IsProcRunning(ProcHandle))
                {
                    OutputBuffer += FPlatformProcess::ReadPipe(ReadPipe);
                    FPlatformProcess::Sleep(0.1f);
                }

                // Read remaining data once more after process finishes
                OutputBuffer += FPlatformProcess::ReadPipe(ReadPipe);

                // Split and log each line
                TArray<FString> Lines;
                OutputBuffer.ParseIntoArrayLines(Lines);

                for (const FString& Line : Lines)
                {
                    if (!Line.IsEmpty())
                    {
                        UE_LOG(LogTemp, Display, TEXT("[CookContent] %s"), *Line);
                    }
                }

                int32 ReturnCode = 1;
                FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);

                // cleanup
                FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

                AsyncTask(ENamedThreads::GameThread, [this, ReturnCode]()
                    {
                        if (CookingNotification.IsValid())
                        {
                            CookingNotification.Pin()->SetCompletionState(
                                ReturnCode == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail
                            );
                            CookingNotification.Pin()->ExpireAndFadeout();
                        }
                    });
            }

            else
            {
                FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        ShowNotification(TEXT("Failed to start Cook process."), SNotificationItem::CS_Fail);
                    });
            }
        });

    return FReply::Handled();
}

TSharedPtr<SNotificationItem> SCookContentWindow::ShowNotification(const FString& Message, SNotificationItem::ECompletionState State, bool bAutoExpire)
{
    FNotificationInfo Info(FText::FromString(Message));
    Info.bFireAndForget = bAutoExpire;
    if (bAutoExpire)
    {
        Info.ExpireDuration = 3.0f;
    }

    TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
    if (Notification.IsValid())
    {
        Notification->SetCompletionState(State);
    }

    return Notification;
}