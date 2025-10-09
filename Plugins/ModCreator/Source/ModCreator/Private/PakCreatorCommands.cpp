// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "PakCreatorCommands.h"
#include "PakCreatorLog.h"

#define LOCTEXT_NAMESPACE "FPakCreatorModule"

void FPakCreatorCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "Pak Creator", "Bring up the pak creator window", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
