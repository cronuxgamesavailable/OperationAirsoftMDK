#include "ModPackagerCommands.h"

#define LOCTEXT_NAMESPACE "FModPackagerModule"

void FModPackagerCommands::RegisterCommands()
{
    UI_COMMAND(OpenPluginWindow, "Mod Packager", "Open the Mod Packager Tool", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE