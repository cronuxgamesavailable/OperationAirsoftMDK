#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FModPackagerStyle
{
public:
    static void Initialize();
    static void Shutdown();

    static TSharedPtr<ISlateStyle> Get();
    static FName GetStyleSetName();

private:
    static TSharedRef<FSlateStyleSet> Create();

private:
    static TSharedPtr<FSlateStyleSet> StyleInstance;
};