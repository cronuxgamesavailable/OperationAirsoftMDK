#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Misc/Optional.h"
#include "Widgets/Notifications/SNotificationList.h"

class SModPackagerWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SModPackagerWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // Meshes
    TSet<FString> ReferencedMeshes;
    
    // UI Widgets
    TSharedPtr<SEditableTextBox> ModNameTextBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ModTypeComboBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ModSubtypeComboBox;
    TSharedPtr<SEditableTextBox> AuthorTextBox;
    TSharedPtr<SEditableTextBox> VersionTextBox;
    TSharedPtr<SMultiLineEditableTextBox> DescriptionTextBox;
    TSharedPtr<FString> SelectedAssetPath;
    TSharedPtr<SButton> AssetSelectButton;
    TArray<FString> FoundAssets;
    TSharedPtr<SNotificationItem> ActiveNotification;
    TSharedPtr<SNotificationItem> NotificationItem;

    // Options
    TArray<TSharedPtr<FString>> ModTypeOptions;
    TArray<TSharedPtr<FString>> MapSubtypes;
    TArray<TSharedPtr<FString>> MeshSubtypes;

    // Selected values
    TSharedPtr<FString> SelectedModType;
    TSharedPtr<FString> SelectedModSubtype;

    // UI Generators
    TSharedRef<SWidget> GenerateModTypeComboItem(TSharedPtr<FString> InItem);
    TSharedRef<SWidget> GenerateModSubtypeComboItem(TSharedPtr<FString> InItem);

    // Logic
    void OnModTypeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    TArray<TSharedPtr<FString>>* GetCurrentSubtypeOptions();  // <--- Just declared here
    void PackageMod();
    void ShowNotification(const FString& Message);
    void RefreshAvailableAssets();
    void OnAssetSelected(FString Asset);
    void ShowOrUpdateNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_None);
    void FinishNotification(const FString& FinalMessage, const FString& FolderToOpen);
};