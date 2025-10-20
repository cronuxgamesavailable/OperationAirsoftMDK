// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "PakCreatorStyle.h"
#include "PakCreatorLog.h"

class FPakCreatorCommands : public TCommands<FPakCreatorCommands>
{
public:

	FPakCreatorCommands()
		: TCommands<FPakCreatorCommands>(TEXT("PakCreator"), NSLOCTEXT("Contexts", "PakCreator", "PakCreator Plugin"), NAME_None, FPakCreatorStyle::GetStyleSetName())
	{

	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};
