#include "ModPackagerStyle.h"
#include "ModPackager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"

TSharedPtr<FSlateStyleSet> FModPackagerStyle::StyleInstance = nullptr;

// Define the brush loading macro
#define IMAGE_BRUSH(RelativePath, Size) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), Size)

void FModPackagerStyle::Initialize()
{
    if (!StyleInstance.IsValid())
    {
        StyleInstance = Create();
        FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
    }
}

void FModPackagerStyle::Shutdown()
{
    if (StyleInstance.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
        ensure(StyleInstance.IsUnique());
        StyleInstance.Reset();
    }
}

FName FModPackagerStyle::GetStyleSetName()
{
    static FName StyleSetName(TEXT("ModPackagerStyle"));
    return StyleSetName;
}

TSharedRef<FSlateStyleSet> FModPackagerStyle::Create()
{
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("ModPackagerStyle"));

    // Path: <PluginDir>/Resources/Button.png
    FString PluginDir = IPluginManager::Get().FindPlugin("ModPackager")->GetBaseDir();
    Style->SetContentRoot(PluginDir / TEXT("Resources"));

    // ✅ Use a consistent and simple icon name
    Style->Set("ModPackager.Icon", new IMAGE_BRUSH(TEXT("Button"), FVector2D(128.0f, 128.0f)));

    return Style;
}

TSharedPtr<ISlateStyle> FModPackagerStyle::Get()
{
    return StyleInstance;
}

#undef IMAGE_BRUSH