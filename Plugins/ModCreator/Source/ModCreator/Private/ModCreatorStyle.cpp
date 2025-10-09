#include "ModCreatorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FModCreatorStyle::StyleInstance = nullptr;

#define IMAGE_BRUSH(RelativePath, Size) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), Size)

void FModCreatorStyle::Initialize()
{
    if (StyleInstance.IsValid())
        return;

    const FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("ModCreator"))->GetBaseDir() / TEXT("Resources");

    TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
    Style->SetContentRoot(ContentDir);

    // 20x20 (toolbar small) + 40x40 (menu large) – both point to your PNGs
    Style->Set("ModCreator.Create.Small", new IMAGE_BRUSH("CreateButton", FVector2D(20.f, 20.f)));
    Style->Set("ModCreator.Create.Large", new IMAGE_BRUSH("CreateButton", FVector2D(40.f, 40.f)));
    Style->Set("ModCreator.Package.Small", new IMAGE_BRUSH("PackageButton", FVector2D(20.f, 20.f)));
    Style->Set("ModCreator.Package.Large", new IMAGE_BRUSH("PackageButton", FVector2D(40.f, 40.f)));

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