// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookContentStyle.h"
#include "CookContent.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FCookContentStyle::StyleInstance = nullptr;

void FCookContentStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FCookContentStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FCookContentStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("CookContentStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FCookContentStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("CookContentStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("CookContent")->GetBaseDir() / TEXT("Resources"));

	// Use IMAGE_BRUSH for .png instead of IMAGE_BRUSH_SVG
	Style->Set("CookContent.PluginAction", new IMAGE_BRUSH(TEXT("Button"), Icon20x20));
	return Style;
}

void FCookContentStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FCookContentStyle::Get()
{
	return *StyleInstance;
}
