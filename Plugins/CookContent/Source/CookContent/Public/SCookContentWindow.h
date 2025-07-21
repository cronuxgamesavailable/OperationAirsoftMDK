#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Notifications/SNotificationList.h"

class SCookContentWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCookContentWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<FAssetData> SelectedAssets;
    TWeakPtr<SNotificationItem> CookingNotification;

    FReply OnCookContentClicked();
    void OnAssetSelected(const FAssetData& AssetData);

    // Unified ShowNotification method
    TSharedPtr<SNotificationItem> ShowNotification(const FString& Message, SNotificationItem::ECompletionState State, bool bFireAndForget = true);
};