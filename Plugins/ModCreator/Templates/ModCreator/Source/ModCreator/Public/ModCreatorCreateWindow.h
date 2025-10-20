#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SDockTab;

class SModCreatorCreatePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SModCreatorCreatePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    struct FTemplateItem
    {
        FString Name;
        FString Path;
        TSharedPtr<FSlateDynamicImageBrush> IconBrush;
    };

    // ==== Extra Content support ====
    FString ExtraContentDir;                // .../Templates/extracontent
    TArray<FString> ExtraAssetRelPaths;     // e.g. "Audio/RealstrikeHorn.uasset"
    TSet<FString> ExtraSelectedRelPaths;    // chosen by user (subset)
    bool bHasExtraContent = false;

    TArray<FTemplateItem> TemplateItems;

    void ScanTemplates(); // scans Plugins/ModCreator/Templates/*
    int32 SelectedIndex = INDEX_NONE;

    // UI state
    FText ModNameText;
    FText AuthorText;
    FText DescriptionText;

    // — Creation —
    bool CanCreate() const;
    FReply OnCreateClicked();

    // filesystem helpers
    bool CreatePluginFromTemplate(const FString& TemplateDir, const FString& ModName, const FString& Author, const FString& Description);
    bool ReadTemplateRequiresCopyContent(const FString& TemplateDir) const;
    bool CopyContentTree(const FString& SrcContent, const FString& DstContent, bool bIncludeMaps) const;
    bool EnsureUPlugin(const FString& TemplateDir, const FString& DestPluginDir, const FString& ModName, const FString& Author, const FString& Description) const;

    // convenience
    static FString SanitizeName(const FString& InName);
    void SetSelected(int32 Index) { SelectedIndex = Index; }
    TSharedRef<SWidget> BuildTemplatesGrid();
    TSharedRef<SWidget> BuildFooter();
    TSharedPtr<class SScrollBox> Scroll;
};

struct FModCreatorCreateWindow
{
    static const FName TabName;               // "ModCreator_CreateTab"
    static void Register();
    static void Unregister();
    static TSharedRef<SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
};