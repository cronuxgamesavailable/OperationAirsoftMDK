#include "ModCreatorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FModCreatorStyle::StyleInstance = nullptr;

#define IMAGE_BRUSH(RelativePath, Size) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), Size)

void FModCreatorStyle::Initialize()
{
    if (StyleInstance.IsValid())
        return;

    const FString ContentDir = IPluginManager::Get()
        .FindPlugin(TEXT("ModCreator"))->GetBaseDir() / TEXT("Resources");

    TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
    Style->SetContentRoot(ContentDir);

    // Tab/toolbar icons – use your actual filenames
    // Create (you have CreateButton.png and CreateButton_40x.png)
    Style->Set("ModCreator.Create.Small", new IMAGE_BRUSH(TEXT("CreateButton_40x"), FVector2D(20.f, 20.f)));
    Style->Set("ModCreator.Create.Large", new IMAGE_BRUSH(TEXT("CreateButton_40x"), FVector2D(40.f, 40.f)));

    // Package (you have PackageButton.png only – reuse and scale)
    Style->Set("ModCreator.Package.Small", new IMAGE_BRUSH(TEXT("PackageButton"), FVector2D(20.f, 20.f)));
    Style->Set("ModCreator.Package.Large", new IMAGE_BRUSH(TEXT("PackageButton"), FVector2D(40.f, 40.f)));

    // Optional: plugin/settings icon (you have ButtonIcon_40x.png)
    Style->Set("ModCreator.PluginIcon", new IMAGE_BRUSH(TEXT("ButtonIcon_40x"), FVector2D(40.f, 40.f)));

    FSlateStyleRegistry::RegisterSlateStyle(*Style);
    StyleInstance = Style;
}

void FModCreatorStyle::Shutdown()
{
    if (StyleInstance.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
        ensure(StyleInstance.IsUnique());
        StyleInstance.Reset();
    }
}

FName FModCreatorStyle::GetStyleSetName()
{
    static FName StyleSetName(TEXT("ModCreatorStyle"));
    return StyleSetName;
}

const ISlateStyle& FModCreatorStyle::Get()
{
    return *StyleInstance.Get();
}

#undef IMAGE_BRUSH