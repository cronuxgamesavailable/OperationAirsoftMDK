// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "PakCreatorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "PakCreatorLog.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FPakCreatorStyle::StyleInstance = NULL;

void FPakCreatorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FPakCreatorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FPakCreatorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("PakCreatorStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef<FSlateStyleSet> FPakCreatorStyle::Create()
{
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("PakCreatorStyle"));

    // Always resolve a REAL plugin — use ModCreator (where the Resources live)
    FString ResourcesDir;

    if (TSharedPtr<IPlugin> ModCreator = IPluginManager::Get().FindPlugin(TEXT("ModCreator")))
    {
        ResourcesDir = ModCreator->GetBaseDir() / TEXT("Resources");
    }
    else if (TSharedPtr<IPlugin> PakCreator = IPluginManager::Get().FindPlugin(TEXT("PakCreator")))
    {
        // Optional: only if you truly ship a second plugin named PakCreator
        ResourcesDir = PakCreator->GetBaseDir() / TEXT("Resources");
    }
    else
    {
        // Final fallback so we never crash even on misinstalls
        ResourcesDir = FPaths::EngineContentDir() / TEXT("Slate");
    }

    Style->SetContentRoot(ResourcesDir);

    const FVector2D Icon40(40.f, 40.f);
    // Use the PNGs you actually have in /Resources
    Style->Set("PakCreator.OpenPluginWindow",
        new FSlateImageBrush(Style->RootToContentDir(TEXT("ButtonIcon_40x"), TEXT(".png")), Icon40));
    Style->Set("PakCreator.Create.Large",
        new FSlateImageBrush(Style->RootToContentDir(TEXT("CreateButton_40x"), TEXT(".png")), Icon40));
    Style->Set("PakCreator.Package.Large",
        new FSlateImageBrush(Style->RootToContentDir(TEXT("PackageButton"), TEXT(".png")), Icon40));

    return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FPakCreatorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FPakCreatorStyle::Get()
{
	return *StyleInstance;
}
