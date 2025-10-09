#pragma once
#include "Styling/SlateStyle.h"

class FModCreatorStyle
{
public:
    static void Initialize();
    static void Shutdown();
    static FName GetStyleSetName();
    static const ISlateStyle& Get();

private:
    static TSharedPtr<FSlateStyleSet> StyleInstance;
};