#include "ModCreatorCommands.h"

#define LOCTEXT_NAMESPACE "FModCreatorCommands"

void FModCreatorCommands::RegisterCommands()
{
    UI_COMMAND(CreateAction, "Create Mod", "Open the Mod Creator.", EUserInterfaceActionType::Button, FInputGesture());
    UI_COMMAND(PackageAction, "Package Mod", "Package the selected mod.", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE