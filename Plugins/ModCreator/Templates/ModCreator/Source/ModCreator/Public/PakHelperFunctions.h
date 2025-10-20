// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PakCreatorLog.h"

class FPakHelperFunctions
{
public:

	static FString MakeUATCommand(const FString& UProjectFile, const FString& PlatformName, const FString& Cookflavor, const FString& ConfigurationName, const FString& BuildTarget, const FString& StageDirectory);
	static FString MakeUATParams_BaseGame(const FString& UProjectFile, const FString& ReleaseVersionName);
	static FString MakeUATParams_DLC(const FString& DLCName, const FString& ReleaseVersionName);

	static void GetPlatformNameAndFlavorBySelection(const FString& InSelection, FString& OutPlatform, FString& OutFlavor);

	static TArray<FString> GetPluginFolders(const FString& ProjectPluginDirectory);

	static bool IsIoStoreEnabled();
};
