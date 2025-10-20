#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FModCreatorCommands : public TCommands<FModCreatorCommands>
{
public:
    FModCreatorCommands()
        : TCommands<FModCreatorCommands>(
            TEXT("ModCreator"),
            NSLOCTEXT("Contexts", "ModCreator", "Mod Creator"),
            NAME_None,
            FAppStyle::GetAppStyleSetName())
    {
    }

    virtual void RegisterCommands() override;

public:
    TSharedPtr<FUICommandInfo> CreateAction;
    TSharedPtr<FUICommandInfo> PackageAction;
};