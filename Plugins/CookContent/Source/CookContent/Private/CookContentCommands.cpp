// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookContentCommands.h"

#define LOCTEXT_NAMESPACE "FCookContentModule"

void FCookContentCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "CookContent", "Execute CookContent action", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
