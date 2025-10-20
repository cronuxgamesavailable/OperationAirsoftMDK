// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/*

	Example usage of the C++ API.

 
	#if WITH_EDITOR

	#include "PakCreator.h"

	if (FPakCreatorModule::Get().IsAvailable())
	{
		FAutomatedPakParams Params;
		Params.OutputPath = "D:\\OUTPUT_AUTOMATED";
		Params.PlatformName = "Win64";
		Params.PluginNames.Add("TestModD");
		Params.UProjectPath = "D:/pathto/MyProject.uproject";

		FPakCreatorModule::Get().CreatePakProcess(Params);

		bool bRunning = FPakCreatorModule::Get().IsPakProcessRunning();

		UE_LOG(LogTemp, Warning, TEXT("bRunning: %i"), bRunning);
	}

	#endif


	Add this piece to your project's build.cs file.

	if (Target.Type == TargetRules.TargetType.Editor)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "PakCreator" });
	}

*/

class MODCREATOR_API FAutomatedPakParams
{
public:
	FAutomatedPakParams() { }

	// Name of the plugins to create pak files for.
	TArray<FString> PluginNames;

	// Specify path to the .uproject file where we can find the plugins.
	FString UProjectPath;

	// Desired platform names for which to create the pak file for.
	FString PlatformName;

	// Build target, refers to the *.Build.cs files. Leave empty if unsure.
	FString BuildTarget;

	// Path where to copy the created pak files to.
	FString OutputPath;

	bool IsValid() const
	{
		return FPaths::GetExtension(UProjectPath) == "uproject" &&
			FPaths::FileExists(UProjectPath) &&
			FPaths::ValidatePath(OutputPath) &&
			ValidPlatformNames.Contains(PlatformName);
	}

	FString GetTemporaryStagingDirectory() const
	{
		return FPaths::Combine(OutputPath, TEXT("__TMP_STAGING__"));
	}

	static const TArray<FString> ValidPlatformNames;

	static const FString ReleaseVersionName;
};
