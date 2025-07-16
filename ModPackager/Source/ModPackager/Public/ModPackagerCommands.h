#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ModPackagerStyle.h"

class FModPackagerCommands : public TCommands<FModPackagerCommands>
{
public:

    FModPackagerCommands()
        : TCommands<FModPackagerCommands>(
            TEXT("ModPackager"), // Context name
            NSLOCTEXT("Contexts", "ModPackager", "Mod Packager Plugin"),
            NAME_None,
            FModPackagerStyle::GetStyleSetName() // Icon Style Set
        )
    {
    }

    virtual void RegisterCommands() override;

public:
    TSharedPtr<FUICommandInfo> OpenPluginWindow;
};