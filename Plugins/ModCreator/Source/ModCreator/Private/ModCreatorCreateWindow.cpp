#include "ModCreatorCreateWindow.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Styling/SlateBrush.h"
#include "Styling/AppStyle.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "ModCreatorStyle.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "Engine/UserDefinedEnum.h"

// --- NEW: helpers for map/attachment selection & enum read ---
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

static TArray<TSharedPtr<FString>> GAttachmentTypes;
static TSharedPtr<FString> GSelectedAttachmentType;

static TArray<TSharedPtr<FString>> GClothingTypes;
static TSharedPtr<FString> GSelectedClothingType;


static bool FillFromEnum(UEnum* Enum)
{
    if (!Enum) return false;

    const int32 Count = Enum->NumEnums();
    for (int32 i = 0; i < Count; ++i)
    {
        if (Enum->HasMetaData(TEXT("Hidden"), i)) continue;
        const FString Name = Enum->GetNameStringByIndex(i);
        if (!Name.IsEmpty())
        {
            GAttachmentTypes.Add(MakeShared<FString>(Name));
        }
    }
    return GAttachmentTypes.Num() > 0;
}

// --- Mount /Templates/extracontent/Content as /ModTemplates/ --- //
static bool GTemplatesMounted = false;

static void EnsureTemplatesMount()
{
    if (GTemplatesMounted)
        return;

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid())
        return;

    const FString RealPath = Plugin->GetBaseDir() / TEXT("Templates/extracontent/Content");
    if (!FPaths::DirectoryExists(RealPath))
        return;

    const FString MountPoint = TEXT("/ModTemplates/");
    if (!FPackageName::MountPointExists(MountPoint))
    {
        FPackageName::RegisterMountPoint(MountPoint, RealPath);
    }

    GTemplatesMounted = true;
}

// Load attachment types from a JSON file at:
//   <ModCreator Plugin>/Templates/extracontent/Content/attachment_types.json
// Accepts either:
//   ["Sight","Grip","Stock","Muzzle","Magazine"]
// or
//   { "AttachmentTypes": ["Sight","Grip",...] }
static bool LoadAttachmentTypesFromJson()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid())
        return false;

    const FString JsonPath = Plugin->GetBaseDir() / TEXT("Templates/extracontent/Content/attachment_types.json");
    if (!FPaths::FileExists(JsonPath))
        return false;

    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
        return false;

    TSharedPtr<FJsonValue> RootVal;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, RootVal) || !RootVal.IsValid())
        return false;

    auto Push = [](const FString& Name)
        {
            if (!Name.IsEmpty())
                GAttachmentTypes.Add(MakeShared<FString>(Name));
        };

    if (RootVal->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& Arr = RootVal->AsArray();
        for (const TSharedPtr<FJsonValue>& V : Arr)
        {
            Push(V->AsString());
        }
    }
    else if (RootVal->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> Obj = RootVal->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;

        // Case-insensitive keys allowed
        static const TCHAR* Keys[] = { TEXT("AttachmentTypes"), TEXT("attachmenttypes"), TEXT("types") };
        for (const TCHAR* K : Keys)
        {
            if (Obj->TryGetArrayField(FStringView(K), ArrPtr) && ArrPtr)
            {
                for (const TSharedPtr<FJsonValue>& V : *ArrPtr)
                {
                    Push(V->AsString());
                }
                break;
            }
        }
    }

    return GAttachmentTypes.Num() > 0;
}

static void BuildAttachmentTypesSource()
{
    GAttachmentTypes.Reset();
    GSelectedAttachmentType.Reset();

    // 1) JSON first (Templates/extracontent/Content/attachment_types.json)
    if (!LoadAttachmentTypesFromJson())
    {
        // 2) Fall back to a project/global enum named EAttachmentType
        if (UEnum* Enum = FindObject<UEnum>(nullptr, TEXT("EAttachmentType")))
        {
            FillFromEnum(Enum);
        }
    }

    // 3) Final fallback: sane defaults
    if (GAttachmentTypes.Num() == 0)
    {
        static const TCHAR* Defaults[] = { TEXT("Sight"), TEXT("Grip"), TEXT("Stock"), TEXT("Muzzle"), TEXT("Magazine") };
        for (const TCHAR* S : Defaults)
        {
            GAttachmentTypes.Add(MakeShared<FString>(FString(S)));
        }
    }

    if (GAttachmentTypes.Num() > 0)
    {
        GSelectedAttachmentType = GAttachmentTypes[0];
    }
}

// Load clothing types from a JSON file at:
//   <ModCreator Plugin>/Templates/extracontent/Content/clothes_types.json
// Accepts either:
//   ["Mask","Shoes","Gloves",...]
// or
//   { "ClothingTypes": ["Mask","Shoes",...] }
static bool LoadClothingTypesFromJson()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid())
        return false;

    // NOTE: file name as requested: clothes_types.json
    const FString JsonPath = Plugin->GetBaseDir() / TEXT("Templates/extracontent/Content/clothes_types.json");
    if (!FPaths::FileExists(JsonPath))
        return false;

    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
        return false;

    TSharedPtr<FJsonValue> RootVal;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, RootVal) || !RootVal.IsValid())
        return false;

    auto Push = [](const FString& Name)
        {
            if (!Name.IsEmpty())
                GClothingTypes.Add(MakeShared<FString>(Name));
        };

    if (RootVal->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& Arr = RootVal->AsArray();
        for (const TSharedPtr<FJsonValue>& V : Arr)
        {
            Push(V->AsString());
        }
    }
    else if (RootVal->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> Obj = RootVal->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;

        // Case-insensitive keys allowed
        static const TCHAR* Keys[] = { TEXT("ClothingTypes"), TEXT("clothingtypes"), TEXT("types") };
        for (const TCHAR* K : Keys)
        {
            if (Obj->TryGetArrayField(FStringView(K), ArrPtr) && ArrPtr)
            {
                for (const TSharedPtr<FJsonValue>& V : *ArrPtr)
                {
                    Push(V->AsString());
                }
                break;
            }
        }
    }

    return GClothingTypes.Num() > 0;
}

static void BuildClothingTypesSource()
{
    GClothingTypes.Reset();
    GSelectedClothingType.Reset();

    // 1) JSON first (Templates/extracontent/Content/clothes_types.json)
    if (!LoadClothingTypesFromJson())
    {
        // 2) Optional: fall back to a project/global enum named EClothingType
        if (UEnum* Enum = FindObject<UEnum>(nullptr, TEXT("EClothingType")))
        {
            // Reuse the same helper if you have it, or inline your own fill logic.
            // Assuming FillFromEnum can be reused for clothing if it just pushes strings:
            TArray<TSharedPtr<FString>> Temp;
            FillFromEnum(Enum); // if FillFromEnum currently fills GAttachmentTypes, you can
            // instead make a new FillFromEnumIntoArray(Enum, GClothingTypes).
        }
    }

    // 3) Final fallback: hard-coded defaults (your clothing list)
    if (GClothingTypes.Num() == 0)
    {
        static const TCHAR* Defaults[] =
        {
            TEXT("Mask"),
            TEXT("Shoes"),
            TEXT("Gloves"),
            TEXT("Shirt"),
            TEXT("Pants"),
            TEXT("KneePads"),
            TEXT("ElbowPads"),
            TEXT("Vest")
        };

        for (const TCHAR* S : Defaults)
        {
            GClothingTypes.Add(MakeShared<FString>(FString(S)));
        }
    }

    if (GClothingTypes.Num() > 0)
    {
        GSelectedClothingType = GClothingTypes[0];
    }
}

const FName FModCreatorCreateWindow::TabName(TEXT("ModCreator_CreateTab"));

namespace
{
    bool CopyIfExists(const FString& Src, const FString& Dst)
    {
        if (!FPaths::FileExists(Src)) return true; // nothing to copy = not an error
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dst), /*Tree*/true);
        return IFileManager::Get().Copy(*Dst, *Src) == ECopyResult::COPY_OK;
    }

    // Return "Content/..."-relative path for a file under Content/
    static bool MakeContentRelative(const FString& ContentRoot, const FString& Abs, FString& OutRel)
    {
        FString Root = ContentRoot;               FPaths::NormalizeDirectoryName(Root);
        FString P = Abs;                       FPaths::NormalizeFilename(P);
        if (P.StartsWith(Root + TEXT("/"))) { OutRel = P.Mid(Root.Len() + 1); return true; }

        // Backslash fallback
        Root.ReplaceInline(TEXT("/"), TEXT("\\"));
        P.ReplaceInline(TEXT("/"), TEXT("\\"));
        if (P.StartsWith(Root + TEXT("\\"))) {
            OutRel = P.Mid(Root.Len() + 1);
            OutRel.ReplaceInline(TEXT("\\"), TEXT("/"));
            return true;
        }
        return false;
    }

    // Scan extracontent/Content for .uasset files (skip BPI_GameRules.uasset)
    static void GatherExtraUassets(const FString& ExtraTemplateDir, TArray<FString>& OutRelPaths)
    {
        OutRelPaths.Reset();
        const FString ContentDir = ExtraTemplateDir / TEXT("Content");
        if (!FPaths::DirectoryExists(ContentDir)) return;

        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *ContentDir, TEXT("*.uasset"), /*Files*/true, /*Dirs*/false);

        for (const FString& Abs : Files)
        {
            FString Rel;
            if (!MakeContentRelative(ContentDir, Abs, Rel)) continue;

            const FString Base = FPaths::GetCleanFilename(Rel);
            if (Base.Equals(TEXT("BPI_GameRules.uasset"), ESearchCase::IgnoreCase)) continue; // skip interface
            OutRelPaths.Add(Rel); // keep folder + filename.uasset
        }
    }

    // Copy only the selected relative files from extracontent -> destination plugin Content/
    static bool CopySelectedExtraContent(const FString& ExtraTemplateDir, const FString& DestPluginDir,
        const TSet<FString>& SelectedRelPaths)
    {
        if (SelectedRelPaths.Num() == 0) return true;

        const FString SrcContent = ExtraTemplateDir / TEXT("Content");
        const FString DstContent = DestPluginDir / TEXT("Content");

        IFileManager& FM = IFileManager::Get();
        bool bOk = true;

        for (const FString& Rel : SelectedRelPaths)
        {
            const FString Src = FPaths::Combine(SrcContent, Rel);
            const FString Dst = FPaths::Combine(DstContent, Rel);

            FM.MakeDirectory(*FPaths::GetPath(Dst), /*Tree*/true);
            if (FM.Copy(*Dst, *Src) != ECopyResult::COPY_OK)
            {
                UE_LOG(LogTemp, Error, TEXT("[ModCreator] Extra copy failed: %s -> %s"), *Src, *Dst);
                bOk = false;
            }
        }
        return bOk;
    }

    // Reuse the same helper name/signature, but now it gathers Maps + Blueprints + Models + Materials.
    static void PopulateAssetsWithMaps(TSharedPtr<FJsonObject>& Root, const FString& PluginDir, const FString& ModName)
    {
        // We'll prefer the Asset Registry (fast & reliable). Fallback to the old file-scan for maps if needed.
        TArray<TSharedPtr<FJsonValue>> AssetsArray;

        auto Push = [&AssetsArray](const FString& Type, const FString& Name)
            {
                TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                Obj->SetStringField(TEXT("Type"), Type);
                Obj->SetStringField(TEXT("Name"), Name);
                AssetsArray.Add(MakeShared<FJsonValueObject>(Obj));
            };

        bool bUsedAssetRegistry = false;

        // --- Asset Registry path (recommended) ---
        // Content-only plugins mount at "/<ModName>"
        const FName PackageRoot(*FString::Printf(TEXT("/%s"), *ModName));

        if (FModuleManager::Get().IsModuleLoaded("AssetRegistry") || FModuleManager::Get().LoadModule("AssetRegistry") != nullptr)
        {
            FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            IAssetRegistry& AR = ARM.Get();

            FARFilter Filter;
            Filter.PackagePaths.Add(PackageRoot);
            Filter.bRecursivePaths = true;
            Filter.bIncludeOnlyOnDiskAssets = true;

            TArray<FAssetData> Assets;
            AR.GetAssets(Filter, Assets);

            for (const FAssetData& AD : Assets)
            {
#if ENGINE_MAJOR_VERSION >= 5
                const FString ClassName = AD.AssetClassPath.GetAssetName().ToString();
#else
                const FString ClassName = AD.AssetClass.ToString();
#endif
                const FString AssetName = AD.AssetName.ToString();

                // Maps are UWorld assets
                if (ClassName == TEXT("World"))
                {
                    Push(TEXT("Map"), AssetName);
                    continue;
                }

                // Blueprints (primary BP asset)
                if (ClassName == TEXT("Blueprint"))
                {
                    Push(TEXT("Blueprint"), AssetName);
                    continue;
                }

                // Models
                if (ClassName == TEXT("StaticMesh") || ClassName == TEXT("SkeletalMesh"))
                {
                    Push(TEXT("Model"), AssetName);
                    continue;
                }

                // Materials (base + instances)
                if (ClassName == TEXT("Material") ||
                    ClassName == TEXT("MaterialInstance") ||
                    ClassName == TEXT("MaterialInstanceConstant"))     // UE4 compat
                {
                    Push(TEXT("Material"), AssetName);
                    continue;
                }

                // (Optional: add Texture2D, NiagaraSystem, SoundWave, etc.)
            }

            bUsedAssetRegistry = true;
        }

        // --- Fallback: if AssetRegistry wasn't available (unlikely), keep your old map scan so at least Maps appear ---
        if (!bUsedAssetRegistry)
        {
            const FString ContentDir = PluginDir / TEXT("Content");
            if (FPaths::DirectoryExists(ContentDir))
            {
                TArray<FString> MapFiles;
                IFileManager::Get().FindFilesRecursive(MapFiles, *ContentDir, TEXT("*.umap"), /*Files*/true, /*Dirs*/false);

                for (const FString& AbsPath : MapFiles)
                {
                    FString Rel = AbsPath;
                    FPaths::NormalizeFilename(Rel);

                    FString ContentNorm = ContentDir;
                    FPaths::NormalizeDirectoryName(ContentNorm);
                    if (Rel.StartsWith(ContentNorm + TEXT("/")))
                    {
                        Rel = Rel.Mid(ContentNorm.Len() + 1); // e.g. "Maps/Arena01.umap"
                    }

                    const FString AssetName = FPaths::GetBaseFilename(Rel);
                    Push(TEXT("Map"), AssetName);
                }
            }
        }

        Root->SetArrayField(TEXT("Assets"), AssetsArray);
    }

    // --- UPDATED: Remove MainMap and auto-populate Assets with maps found in this plugin.
    // Pass PluginDir so we can scan its Content folder.
    bool UpdateModInfoJson(const FString& ModInfoPath, const FString& ModName, const FString& Description, const FString& PluginDir)
    {
        FString In;
        TSharedPtr<FJsonObject> Root;

        if (FPaths::FileExists(ModInfoPath) && FFileHelper::LoadFileToString(In, *ModInfoPath))
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                Root = MakeShared<FJsonObject>();
            }
        }
        else
        {
            Root = MakeShared<FJsonObject>();
        }

        // Update basic fields
        Root->SetStringField(TEXT("ModName"), ModName);
        if (!Description.IsEmpty())
        {
            Root->SetStringField(TEXT("Description"), Description);
        }

        // Include current project name
        const FString ProjectName = FApp::GetProjectName();
        if (!ProjectName.IsEmpty())
        {
            Root->SetStringField(TEXT("ProjectName"), ProjectName);
        }

        // ✅ Remove "MainMap" (schema change)
        Root->RemoveField(TEXT("MainMap"));

        // ✅ Auto-populate Assets with all maps in this plugin
        PopulateAssetsWithMaps(Root, PluginDir, ModName);

        // (Optional) ensure arrays exist even if empty
        if (!Root->HasField(TEXT("LayoutsEnabled")))
        {
            Root->SetBoolField(TEXT("LayoutsEnabled"), false);
        }
        if (!Root->HasField(TEXT("Layouts")))
        {
            Root->SetArrayField(TEXT("Layouts"), {});
        }
        if (!Root->HasField(TEXT("Thumbnail")))
        {
            Root->SetStringField(TEXT("Thumbnail"), TEXT("Thumbnail.png"));
        }
        if (!Root->HasField(TEXT("BuildRequirements")))
        {
            Root->SetStringField(TEXT("BuildRequirements"), TEXT(""));
        }

        FString Out;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
        {
            return false;
        }
        return FFileHelper::SaveStringToFile(Out, *ModInfoPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
}

/* ---------- Tab Registration ---------- */
void FModCreatorCreateWindow::Register()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabName,
        FOnSpawnTab::CreateStatic(&FModCreatorCreateWindow::SpawnTab)
    )
        .SetDisplayName(NSLOCTEXT("ModCreator", "CreateTabTitle", "Create Mod"))
        .SetTooltipText(NSLOCTEXT("ModCreator", "CreateTabTooltip", "Create a new mod"))
        // Use our custom plus icon (small + large variants from the style set)
        .SetIcon(FSlateIcon(
            FModCreatorStyle::GetStyleSetName(),
            "ModCreator.Create.Small",
            "ModCreator.Create.Large"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FModCreatorCreateWindow::Unregister()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> FModCreatorCreateWindow::SpawnTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SModCreatorCreatePanel)
        ];
}

/* ---------- Panel ---------- */
void SModCreatorCreatePanel::Construct(const FArguments& InArgs)
{
    ScanTemplates();
    EnsureTemplatesMount();
    BuildAttachmentTypesSource();
    BuildClothingTypesSource();

    ChildSlot
        [
            SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight().Padding(10, 8)
                [
                    SNew(STextBlock)
                        .Text(NSLOCTEXT("ModCreator", "IntroText",
                            "Select the content you want to first create in your mod, then give your mod a name to create it.\n"
                            "Restart is required to see the mod in editor!"))
                        .WrapTextAt(900.f)
                ]

            + SVerticalBox::Slot().FillHeight(1.f).Padding(10, 6)
                [
                    SAssignNew(Scroll, SScrollBox)
                        + SScrollBox::Slot()
                        [
                            BuildTemplatesGrid()
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(10, 6)
                [
                    BuildFooter()
                ]
        ];
}

void SModCreatorCreatePanel::ScanTemplates()
{
    TemplateItems.Reset();
    bHasExtraContent = false;
    ExtraContentDir.Reset();
    ExtraAssetRelPaths.Reset();
    ExtraSelectedRelPaths.Reset();

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModCreator"));
    if (!Plugin.IsValid()) return;

    const FString Root = Plugin->GetBaseDir() / TEXT("Templates");
    IFileManager& FM = IFileManager::Get();

    FM.IterateDirectory(*Root, [this](const TCHAR* Path, bool bIsDir) -> bool
        {
            if (!bIsDir) return true;

            const FString FolderPath(Path);
            const FString Name = FPaths::GetCleanFilename(FolderPath);

            // Special cases: do NOT show a tile for "extracontent" or "ModCreator"
            if (Name.Equals(TEXT("extracontent"), ESearchCase::IgnoreCase) ||
                Name.Equals(TEXT("ModCreator"), ESearchCase::IgnoreCase))
            {
                // Handle extracontent separately (used for dropdown)
                if (Name.Equals(TEXT("extracontent"), ESearchCase::IgnoreCase))
                {
                    bHasExtraContent = true;
                    ExtraContentDir = FolderPath;
                    GatherExtraUassets(ExtraContentDir, ExtraAssetRelPaths);
                }

                // Skip adding this folder as a visible template
                return true;
            }

            // Normal template tile
            FString IconPath = FolderPath / TEXT("Thumbnail.png");
            TSharedPtr<FSlateDynamicImageBrush> Brush;
            if (FPaths::FileExists(IconPath))
            {
                Brush = MakeShared<FSlateDynamicImageBrush>(*IconPath, FVector2D(128, 128));
            }

            TemplateItems.Add({ Name, FolderPath, Brush });
            return true;
        });
}

TSharedRef<SWidget> SModCreatorCreatePanel::BuildTemplatesGrid()
{
    // Uniform tile sizing (use the dimensions you liked)
    const float TileW = 180.f;
    const float TileH = 180.f;
    const FVector2D ThumbSize(140.f, 140.f);

    TSharedRef<SWrapBox> Wrap =
        SNew(SWrapBox)
        .UseAllottedWidth(true)
        .InnerSlotPadding(FVector2D(12.f, 12.f));

    for (int32 i = 0; i < TemplateItems.Num(); ++i)
    {
        const FTemplateItem& Item = TemplateItems[i];

        Wrap->AddSlot()
            [
                // A fixed-size tile that behaves like a button
                SNew(SButton)
                    .OnClicked_Lambda([this, i]()
                        {
                            SetSelected(i);
                            return FReply::Handled();
                        })
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton")) // flat-looking
                    .ContentPadding(0)
                    [
                        SNew(SBox)
                            .WidthOverride(TileW)
                            .HeightOverride(TileH)
                            [
                                // Outer border + selection tint
                                SNew(SBorder)
                                    .Padding(8)
                                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                    .BorderBackgroundColor_Lambda([this, i]()
                                        {
                                            // Darker blue-gray tint when selected
                                            return (SelectedIndex == i)
                                                ? FLinearColor(0.05f, 0.15f, 0.3f, 0.6f)   // darker & more opaque
                                                : FLinearColor::Transparent;
                                        })
                                    [
                                        SNew(SVerticalBox)

                                            // Thumbnail
                                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                                            [
                                                SNew(SBox).WidthOverride(ThumbSize.X).HeightOverride(ThumbSize.Y)
                                                    [
                                                        Item.IconBrush.IsValid()
                                                            ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Item.IconBrush.Get()))
                                                            : StaticCastSharedRef<SWidget>(SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Box")))
                                                    ]
                                            ]

                                        // Name
                                        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Center)
                                            [
                                                SNew(STextBlock)
                                                    .Text(FText::FromString(Item.Name))
                                                    .Justification(ETextJustify::Center)
                                                    .WrapTextAt(TileW - 16.f)
                                            ]
                                    ]
                            ]
                    ]
            ];
    }

    return Wrap;
}

TSharedRef<SWidget> SModCreatorCreatePanel::BuildFooter()
{
    // Build the list widget of checkboxes once (used inside the dropdown) – unchanged
    TSharedRef<SVerticalBox> ExtraList = SNew(SVerticalBox);
    if (ExtraAssetRelPaths.Num() > 0)
    {
        for (const FString& Rel : ExtraAssetRelPaths)
        {
            const FString NiceName = FPaths::GetBaseFilename(Rel); // no .uasset
            ExtraList->AddSlot()
                .AutoHeight()
                .Padding(0, 2)
                [
                    SNew(SCheckBox)
                        .OnCheckStateChanged_Lambda([this, Rel](ECheckBoxState State)
                            {
                                if (State == ECheckBoxState::Checked)
                                {
                                    ExtraSelectedRelPaths.Add(Rel);
                                }
                                else
                                {
                                    ExtraSelectedRelPaths.Remove(Rel);
                                }
                            })
                        .Content()
                        [
                            SNew(STextBlock).Text(FText::FromString(NiceName))
                        ]
                ];
        }
    }

    // Common footer container
    return SNew(SVerticalBox)

        // Mod Name
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 4)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SNew(SEditableTextBox)
                        .HintText(NSLOCTEXT("ModCreator", "NameHint", "Mod Name"))
                        .OnTextChanged_Lambda([this](const FText& T) { ModNameText = T; })
                ]
        ]

    // Author / Description
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 6)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(0, 0, 6, 0)
                [
                    SNew(SEditableTextBox)
                        .HintText(NSLOCTEXT("ModCreator", "Author", "Author"))
                        .OnTextChanged_Lambda([this](const FText& T) { AuthorText = T; })
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                [
                    SNew(SEditableTextBox)
                        .HintText(NSLOCTEXT("ModCreator", "Description", "Description"))
                        .OnTextChanged_Lambda([this](const FText& T) { DescriptionText = T; })
                ]
        ]

    // --- Attachment Type (only when "Base Attachment" template is selected) ---
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 6)
        [
            SNew(SVerticalBox)
                .Visibility_Lambda([this]()
                    {
                        const bool bAttachment =
                            TemplateItems.IsValidIndex(SelectedIndex) &&
                            TemplateItems[SelectedIndex].Name.Contains(TEXT("Attachment"), ESearchCase::IgnoreCase);
                        return bAttachment ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Attachment Type")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 4)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&GAttachmentTypes)
                        .InitiallySelectedItem(GAttachmentTypes.Num() > 0 ? GAttachmentTypes[0] : TSharedPtr<FString>())
                        .OnSelectionChanged_Lambda([](TSharedPtr<FString> Sel, ESelectInfo::Type)
                            {
                                GSelectedAttachmentType = Sel;
                            })
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                            {
                                return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
                            })
                        [
                            SNew(STextBlock)
                                .Text_Lambda([]()
                                    {
                                        return FText::FromString(GSelectedAttachmentType.IsValid() ? *GSelectedAttachmentType : TEXT("(select)"));
                                    })
                        ]
                ]
        ]

    // --- Clothing Type (only when "Base Clothes" template is selected) ---
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 6)
        [
            SNew(SVerticalBox)
                .Visibility_Lambda([this]()
                    {
                        const bool bClothes =
                            TemplateItems.IsValidIndex(SelectedIndex) &&
                            TemplateItems[SelectedIndex].Name.Contains(TEXT("Clothes"), ESearchCase::IgnoreCase);
                        return bClothes ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Clothing Type")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 4)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&GClothingTypes)
                        .InitiallySelectedItem(GClothingTypes.Num() > 0 ? GClothingTypes[0] : TSharedPtr<FString>())
                        .OnSelectionChanged_Lambda([](TSharedPtr<FString> Sel, ESelectInfo::Type)
                            {
                                GSelectedClothingType = Sel;
                            })
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                            {
                                return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
                            })
                        [
                            SNew(STextBlock)
                                .Text_Lambda([]()
                                    {
                                        return FText::FromString(GSelectedClothingType.IsValid() ? *GSelectedClothingType : TEXT("(select)"));
                                    })
                        ]
                ]
        ]

    // + Extra Content (show ONLY for Base Map)
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 4)
        [
            SNew(SVerticalBox)
                .Visibility_Lambda([this]()
                    {
                        const bool bMap =
                            TemplateItems.IsValidIndex(SelectedIndex) &&
                            TemplateItems[SelectedIndex].Name.Contains(TEXT("Map"), ESearchCase::IgnoreCase);
                        const bool bShow = bHasExtraContent && bMap;
                        return bShow ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SExpandableArea)
                        .AreaTitle(NSLOCTEXT("ModCreator", "ExtraContentHeader", "+ Extra Content"))
                        .InitiallyCollapsed(true)
                        .BodyContent()
                        [
                            SNew(SScrollBox)
                                + SScrollBox::Slot()[ExtraList]
                        ]
                ]
        ]

    // Options + Create button
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 6)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SCheckBox)
                        .IsChecked(ECheckBoxState::Checked)
                        .Content()[SNew(STextBlock).Text(NSLOCTEXT("ModCreator", "ShowOnStartup", "Show on Startup"))]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(12, 0, 0, 0)
                [
                    SNew(SCheckBox)
                        .IsChecked(ECheckBoxState::Checked)
                        .Content()[SNew(STextBlock).Text(NSLOCTEXT("ModCreator", "ShowContentDir", "Show Content Directory"))]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                        .IsEnabled_Lambda([this]() { return CanCreate(); })
                        .OnClicked(this, &SModCreatorCreatePanel::OnCreateClicked)
                        .Text(NSLOCTEXT("ModCreator", "CreateButton", "Create Mod"))
                ]
        ];
}

bool SModCreatorCreatePanel::CanCreate() const
{
    return SelectedIndex != INDEX_NONE && !ModNameText.IsEmptyOrWhitespace();
}

FReply SModCreatorCreatePanel::OnCreateClicked()
{
    if (!CanCreate())
        return FReply::Handled();

    const FString RawModName = SanitizeName(ModNameText.ToString());
    const FString Author = AuthorText.ToString();
    const FString Desc = DescriptionText.ToString();

    // Detect if the selected tile is an Attachment or Clothes template
    const bool bIsAttachmentTemplate =
        TemplateItems.IsValidIndex(SelectedIndex) &&
        TemplateItems[SelectedIndex].Name.Contains(TEXT("Attachment"), ESearchCase::IgnoreCase);

    const bool bIsClothingTemplate =
        TemplateItems.IsValidIndex(SelectedIndex) &&
        TemplateItems[SelectedIndex].Name.Contains(TEXT("Clothes"), ESearchCase::IgnoreCase);

    // Final name (append _{Type} when making an Attachment or Clothes mod)
    FString FinalModName = RawModName;

    if (bIsAttachmentTemplate && GSelectedAttachmentType.IsValid())
    {
        FString Suffix = *GSelectedAttachmentType;   // from Attachment dropdown
        Suffix.ReplaceInline(TEXT(" "), TEXT(""));   // keep folder/object names clean
        FinalModName = RawModName + TEXT("_") + Suffix;
    }
    else if (bIsClothingTemplate && GSelectedClothingType.IsValid())
    {
        FString Suffix = *GSelectedClothingType;     // from Clothing dropdown
        Suffix.ReplaceInline(TEXT(" "), TEXT(""));
        FinalModName = RawModName + TEXT("_") + Suffix;
    }

    const FString TemplateDir = TemplateItems[SelectedIndex].Path;

    if (CreatePluginFromTemplate(TemplateDir, FinalModName, Author, Desc))
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                NSLOCTEXT("ModCreator", "CreatedOK",
                    "Mod '{0}' was created under Plugins. You may need to restart the editor to load the new plugin."),
                FText::FromString(FinalModName)));
    }
    else
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            NSLOCTEXT("ModCreator", "CreateFailed",
                "Failed to create the mod. Check the Output Log for details."));
    }

    return FReply::Handled();
}

FString SModCreatorCreatePanel::SanitizeName(const FString& InName)
{
    FString Out = InName;
    Out = Out.ToLower();
    FPaths::MakeValidFileName(Out);
    if (Out.IsEmpty()) Out = TEXT("NewMod");
    return Out;
}

bool SModCreatorCreatePanel::CreatePluginFromTemplate(const FString& TemplateDir, const FString& ModName, const FString& Author, const FString& Description)
{
    const FString DestPluginDir = FPaths::ProjectPluginsDir() / ModName;
    IFileManager& FM = IFileManager::Get();

    if (FM.DirectoryExists(*DestPluginDir))
    {
        UE_LOG(LogTemp, Warning, TEXT("Destination plugin already exists: %s"), *DestPluginDir);
        return false;
    }
    if (!FM.MakeDirectory(*DestPluginDir, /*Tree*/true))
    {
        UE_LOG(LogTemp, Error, TEXT("Could not create destination: %s"), *DestPluginDir);
        return false;
    }

    // Content copy — optionally include maps
    const FString SrcContent = TemplateDir / TEXT("Content");
    const FString DstContent = DestPluginDir / TEXT("Content");
    const bool bIncludeMaps = ReadTemplateRequiresCopyContent(TemplateDir);

    if (FM.DirectoryExists(*SrcContent))
    {
        if (!CopyContentTree(SrcContent, DstContent, bIncludeMaps))
        {
            UE_LOG(LogTemp, Error, TEXT("Content copy failed."));
            return false;
        }
    }

    // ---- NEW: copy any selected "Extra Content" assets (from Templates/extracontent) ----
    if (bHasExtraContent && ExtraSelectedRelPaths.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[ModCreator] Copying %d extra asset(s) into plugin..."), ExtraSelectedRelPaths.Num());

        if (!CopySelectedExtraContent(ExtraContentDir, DestPluginDir, ExtraSelectedRelPaths))
        {
            UE_LOG(LogTemp, Warning, TEXT("[ModCreator] Some extra-content files failed to copy."));
            // Not fatal — continue
        }
        else
        {
            // Nice to log what was copied
            for (const FString& Rel : ExtraSelectedRelPaths)
            {
                UE_LOG(LogTemp, Log, TEXT("[ModCreator]   + Extra: %s"), *Rel);
            }
        }
    }
    // ---- END NEW ----

    // Ensure .uplugin exists and has the right name/metadata
    if (!EnsureUPlugin(TemplateDir, DestPluginDir, ModName, Author, Description))
    {
        UE_LOG(LogTemp, Error, TEXT(".uplugin creation failed."));
        return false;
    }

    // --- Copy modinfo.json into plugin root (supports either Templates/modinfo.json or Templates/Config/modinfo.json)
    {
        const FString SrcA = TemplateDir / TEXT("modinfo.json");
        const FString SrcB = TemplateDir / TEXT("Config/modinfo.json");
        const FString Dst = DestPluginDir / TEXT("modinfo.json");

        const FString* Src = nullptr;
        if (FPaths::FileExists(SrcA)) Src = &SrcA;
        else if (FPaths::FileExists(SrcB)) Src = &SrcB;

        if (Src)
        {
            if (!CopyIfExists(*Src, Dst))
            {
                UE_LOG(LogTemp, Warning, TEXT("[ModCreator] Failed to copy modinfo.json: %s -> %s"), **Src, *Dst);
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("[ModCreator] Copied modinfo.json to %s"), *Dst);
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[ModCreator] No modinfo.json present in template."));
        }
    }

    // --- Copy Thumbnail.png into plugin root if present
    {
        const FString SrcThumb = TemplateDir / TEXT("Thumbnail.png");
        const FString DstThumb = DestPluginDir / TEXT("Thumbnail.png");
        if (!CopyIfExists(SrcThumb, DstThumb))
        {
            UE_LOG(LogTemp, Warning, TEXT("[ModCreator] Failed to copy Thumbnail.png: %s -> %s"), *SrcThumb, *DstThumb);
        }
    }

    // After copy: update ModName / Description inside modinfo.json
    {
        const FString DstRoot = DestPluginDir / TEXT("modinfo.json");
        const FString DstCfg = DestPluginDir / TEXT("Config/modinfo.json");

        FString ModInfoDst;
        if (FPaths::FileExists(DstRoot))      ModInfoDst = DstRoot;
        else if (FPaths::FileExists(DstCfg))  ModInfoDst = DstCfg;

        if (!ModInfoDst.IsEmpty())
        {
            if (!UpdateModInfoJson(ModInfoDst, ModName, Description, DestPluginDir))
            {
                UE_LOG(LogTemp, Warning, TEXT("[ModCreator] Failed to update %s with ModName/Description/Assets"), *ModInfoDst);
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("[ModCreator] Updated %s (ModName=%s, Assets=Maps)"), *ModInfoDst, *ModName);
            }
        }
    }

    // Done — project will detect this plugin next startup
    return true;
}

bool SModCreatorCreatePanel::ReadTemplateRequiresCopyContent(const FString& TemplateDir) const
{
    // Look in either location
    const FString JsonPathA = TemplateDir / TEXT("modinfo.json");
    const FString JsonPathB = TemplateDir / TEXT("Config/modinfo.json");

    FString JsonPath;
    if (FPaths::FileExists(JsonPathA)) JsonPath = JsonPathA;
    else if (FPaths::FileExists(JsonPathB)) JsonPath = JsonPathB;

    UE_LOG(LogTemp, Log, TEXT("[ModCreator] modinfo path: %s"),
        JsonPath.IsEmpty() ? TEXT("<not found>") : *JsonPath);

    bool bCopyContent  = false;

    if (!JsonPath.IsEmpty())
    {
        FString JsonStr;
        if (FFileHelper::LoadFileToString(JsonStr, *JsonPath))
        {
            TSharedPtr<FJsonObject> Root;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
            if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
            {
                // 1) Accept string form: "BuildRequirements": "CopyContent"
                {
                    FString S;
                    if (Root->TryGetStringField(TEXT("BuildRequirements"), S))
                    {
                        S.ToLowerInline();
                        if (S == TEXT("copycontent") || S == TEXT("copy content") || S == TEXT("map"))
                        {
                            bCopyContent  = true;
                        }
                    }
                }

                // 2) Accept array/object forms on several field names
                auto HasBuildMapStringInArray = [](const TArray<TSharedPtr<FJsonValue>>* Arr)->bool
                    {
                        if (!Arr) return false;
                        for (const auto& V : *Arr)
                        {
                            FString S = V->AsString();
                            S.ToLowerInline();
                            if (S == TEXT("copycontent") || S == TEXT("copy content") || S == TEXT("map"))
                            {
                                return true;
                            }
                        }
                        return false;
                    };

                auto HasBuildMapTrueInObject = [](const TSharedPtr<FJsonObject>& Obj)->bool
                    {
                        if (!Obj.IsValid()) return false;
                        static const TCHAR* Keys[] = { TEXT("CopyContent"), TEXT("copycontent"), TEXT("Map"), TEXT("map") };
                        for (auto* K : Keys)
                        {
                            bool bVal = false;
                            if (Obj->TryGetBoolField(K, bVal) && bVal)
                            {
                                return true;
                            }
                        }
                        return false;
                    };

                const TCHAR* CandidateFields[] =
                {
                    TEXT("BuildRequirements"), TEXT("buildrequirements"),
                    TEXT("Requires"), TEXT("requires")
                };

                for (const TCHAR* Field : CandidateFields)
                {
                    if (bCopyContent ) break;

                    // Array form
                    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
                    if (Root->TryGetArrayField(FStringView(Field), Arr))
                    {
                        if (HasBuildMapStringInArray(Arr))
                        {
                            bCopyContent  = true;
                            break;
                        }
                    }

                    // Object form (note: out pointer)
                    const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
                    if (Root->TryGetObjectField(FStringView(Field), ObjPtr) && ObjPtr)
                    {
                        if (HasBuildMapTrueInObject(*ObjPtr))
                        {
                            bCopyContent  = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Fallback heuristic: folder name contains "map" and we actually have a .umap present
    if (!bCopyContent )
    {
        const FString FolderName = FPaths::GetCleanFilename(TemplateDir).ToLower();
        if (FolderName.Contains(TEXT("map")))
        {
            TArray<FString> FoundMaps;
            IFileManager::Get().FindFilesRecursive(FoundMaps, *(TemplateDir / TEXT("Content")), TEXT("*.umap"), true, false);
            if (FoundMaps.Num() > 0)
            {
                UE_LOG(LogTemp, Log, TEXT("[ModCreator] Heuristic: '%s' contains %d map(s). Treating as BuildMap."),
                    *FolderName, FoundMaps.Num());
                bCopyContent  = true;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[ModCreator] BuildRequirements => CopyContent = %s"),
        bCopyContent  ? TEXT("true") : TEXT("false"));
    return bCopyContent ;
}

bool SModCreatorCreatePanel::CopyContentTree(const FString& SrcContent, const FString& DstContent, bool bIncludeMaps) const
{
    IFileManager& FM = IFileManager::Get();
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

    // Ensure destination exists
    if (!FM.MakeDirectory(*DstContent, /*Tree*/true))
    {
        UE_LOG(LogTemp, Error, TEXT("Could not create destination content dir: %s"), *DstContent);
        return false;
    }

    // Gather all files under SrcContent
    TArray<FString> Files;
    FM.FindFilesRecursive(Files, *SrcContent, TEXT("*.*"), /*Files*/true, /*Directories*/false);

    bool bOk = true;

    for (const FString& SrcFile : Files)
    {
        const FString Ext = FPaths::GetExtension(SrcFile, /*bIncludeDot*/false).ToLower();

        // Skip maps when not requested
        if (!bIncludeMaps && Ext == TEXT("umap"))
        {
            continue;
        }

        // Compute relative path robustly (works even if MakePathRelativeTo fails)
        FString SrcNorm = SrcContent;               FPaths::NormalizeDirectoryName(SrcNorm);
        FString Rel = SrcFile;                  FPaths::NormalizeFilename(Rel);
        if (Rel.StartsWith(SrcNorm + TEXT("/")))
        {
            Rel = Rel.Mid(SrcNorm.Len() + 1);      // drop "…/Content/" prefix
        }
        else
        {
            // Fallback: if only slashes differ (or case), try again with relaxed check
            FString SrcNormBack = SrcNorm; SrcNormBack.ReplaceInline(TEXT("/"), TEXT("\\"));
            FString RelBack = Rel;     RelBack.ReplaceInline(TEXT("/"), TEXT("\\"));
            if (RelBack.StartsWith(SrcNormBack + TEXT("\\")))
            {
                Rel = Rel.Mid(SrcNorm.Len() + 1);
            }
        }

        const FString DstFile = FPaths::Combine(DstContent, Rel);

        // Ensure parent directory exists
        const FString DstDir = FPaths::GetPath(DstFile);
        if (!FM.MakeDirectory(*DstDir, /*Tree*/true))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create dir: %s"), *DstDir);
            bOk = false;
            continue;
        }

        // Copy file
        if (!PF.CopyFile(*DstFile, *SrcFile))
        {
            UE_LOG(LogTemp, Error, TEXT("Copy failed: %s -> %s"), *SrcFile, *DstFile);
            bOk = false;
        }
    }

    // Optional: log when no maps were present but requested
    if (bIncludeMaps)
    {
        TArray<FString> Maps;
        FM.FindFilesRecursive(Maps, *SrcContent, TEXT("*.umap"), /*Files*/true, /*Directories*/false);
        if (Maps.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("BuildRequirements requested 'BuildMap' but no .umap was found under: %s"), *SrcContent);
        }
    }

    return bOk;
}

bool SModCreatorCreatePanel::EnsureUPlugin(const FString& TemplateDir, const FString& DestPluginDir,
    const FString& ModName, const FString& Author, const FString& Description) const
{
    const FString TemplateUplugin = TemplateDir / TEXT("Template.uplugin");        // (optional) your chosen template name
    const FString TemplateUpluginAlt = TemplateDir / TEXT("ModTemplate.uplugin");  // alt name, try both
    const FString DestUplugin = DestPluginDir / (ModName + TEXT(".uplugin"));

    FString JsonStr;
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    bool bHaveTemplate = false;

    auto TryLoad = [&](const FString& P)->bool {
        if (FPaths::FileExists(P) && FFileHelper::LoadFileToString(JsonStr, *P))
        {
            TSharedPtr<FJsonObject> Obj;
            auto Reader = TJsonReaderFactory<>::Create(JsonStr);
            if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
            {
                Root = Obj;
                return true;
            }
        }
        return false;
        };

    if (TryLoad(TemplateUplugin) || TryLoad(TemplateUpluginAlt))
    {
        bHaveTemplate = true;
    }
    else
    {
        // Build a minimal content-only uplugin
        Root->SetNumberField(TEXT("FileVersion"), 3);
        Root->SetNumberField(TEXT("Version"), 1);
        Root->SetStringField(TEXT("VersionName"), TEXT("1.0"));
        Root->SetStringField(TEXT("Category"), TEXT("Mods"));
        Root->SetBoolField(TEXT("CanContainContent"), true);
        Root->SetBoolField(TEXT("IsBetaVersion"), false);
    }

    // Overwrite/ensure naming + description
    Root->SetStringField(TEXT("FriendlyName"), ModName);
    Root->SetStringField(TEXT("Description"), Description.IsEmpty() ? FString::Printf(TEXT("Mod %s"), *ModName) : Description);
    Root->SetStringField(TEXT("CreatedBy"), Author.IsEmpty() ? TEXT("Unknown") : Author);

    // Serialize JSON out
    FString OutStr;
    auto Writer = TJsonWriterFactory<>::Create(&OutStr);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    return FFileHelper::SaveStringToFile(OutStr, *DestUplugin, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}