// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "CookContentStyle.h"

class FCookContentCommands : public TCommands<FCookContentCommands>
{
public:

	FCookContentCommands()
		: TCommands<FCookContentCommands>(TEXT("CookContent"), NSLOCTEXT("Contexts", "CookContent", "CookContent Plugin"), NAME_None, FCookContentStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
