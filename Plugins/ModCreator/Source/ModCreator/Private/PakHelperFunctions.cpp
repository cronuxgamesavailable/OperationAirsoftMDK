// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "PakHelperFunctions.h"
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 27)
#include "Misc/ConfigCacheIni.h"
#endif
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "PakCreatorLog.h"

FString FPakHelperFunctions::MakeUATCommand(const FString& UProjectFile, const FString& PlatformName, const FString& Cookflavor, const FString& ConfigurationName, const FString& BuildTarget, const FString& StageDirectory)
{
	FString CommandLine = FString::Printf(TEXT("BuildCookRun -project=\"%s\" -noP4"), *UProjectFile);

	if (ConfigurationName.Len() > 0)
	{
		CommandLine += FString::Printf(TEXT(" -clientconfig=%s -serverconfig=%s"), *ConfigurationName, *ConfigurationName);
	}

	if (BuildTarget.Len() > 0)
	{
		CommandLine += FString::Printf(TEXT(" -target=%s"), *BuildTarget);

		if (BuildTarget.Contains("Server"))
		{
			CommandLine += " -server -noclient";
		}
	}

	// Only skip compiling if AutomationTool.dll already exists
	const FString AutoToolDll = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/DotNET/AutomationTool/AutomationTool.dll"))
	);
	if (FPaths::FileExists(AutoToolDll))
	{
		CommandLine += TEXT(" -nocompile -nocompileeditor");
	}

	CommandLine += FApp::IsEngineInstalled() ? TEXT(" -installed") : TEXT("");

	FString UnrealEditorCmdExe = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), 
#if PLATFORM_WINDOWS
		TEXT("Win64")
#elif PLATFORM_LINUX
		TEXT("Linux")
#elif PLATFORM_MAC
		TEXT("Mac")
#endif
		,
#if PLATFORM_WINDOWS
		TEXT("UnrealEditor-Cmd.exe")
#else
		TEXT("UnrealEditor-Cmd")
#endif
	));
	FPaths::MakePlatformFilename(UnrealEditorCmdExe);

	CommandLine += FString::Printf(TEXT(" -unrealexe=\"%s\""), *UnrealEditorCmdExe);

	CommandLine += " -utf8output";

	CommandLine += " -platform=" + PlatformName;

	if (Cookflavor.Len() > 0)
	{
		CommandLine += " -cookflavor=" + Cookflavor;
	}

	CommandLine += " -build -cook -CookCultures=en -unversionedcookedcontent -pak";

	CommandLine += " -stage";

	CommandLine += FString::Printf(TEXT(" -stagingdirectory=\"%s\""), *StageDirectory);

	return CommandLine;
}

FString FPakHelperFunctions::MakeUATParams_BaseGame(const FString& UProjectFile, const FString& ReleaseVersionName)
{
	FString OutParams = FString::Printf(TEXT(" -package -createreleaseversion=\"%s\""), *ReleaseVersionName);

	// Tell Unreal what map to use for packaging the base game. Use an empty one with no references to our content plugins.
	OutParams += " -map=/Engine/Maps/Entry";
	OutParams += " -ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=/Engine/Maps/Entry.Entry -ini:Engine:[/Script/EngineSettings.GameMapsSettings]:EditorStartupMap=/Engine/Maps/Entry.Entry -ini:DefaultEngine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=/Engine/Maps/Entry.Entry -ini:DefaultEngine:[/Script/EngineSettings.GameMapsSettings]:EditorStartupMap=/Engine/Maps/Entry.Entry";

	return OutParams;
}

FString FPakHelperFunctions::MakeUATParams_DLC(const FString& DLCName, const FString& ReleaseVersionName)
{
	FString CommandLine = FString::Printf(TEXT(" -basedonreleaseversion=\"%s\""), *ReleaseVersionName);

	CommandLine += " -stagebasereleasepaks -DLCIncludeEngineContent";

	CommandLine += FString::Printf(TEXT(" -dlcname=\"%s\""), *DLCName);

	return CommandLine;
}

void FPakHelperFunctions::GetPlatformNameAndFlavorBySelection(const FString& InSelection, FString& OutPlatform, FString& OutFlavor)
{
	int32 Idx;
	if (InSelection.FindChar('_', Idx))
	{
		OutPlatform = InSelection.Left(Idx);
		OutFlavor = InSelection.Right(InSelection.Len() - Idx - 1);
	}
	else
	{
		OutPlatform = InSelection;
		OutFlavor = "";
	}
}

TArray<FString> FPakHelperFunctions::GetPluginFolders(const FString& ProjectPluginDirectory)
{
	TArray<FString> PluginNames;
	TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPluginsWithContent();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
		{
			PluginNames.Add(Plugin->GetName());
		}
	}
	PluginNames.Sort([](const FString& A, const FString& B)
		{
			return A < B;
		});
	return PluginNames;
}

bool FPakHelperFunctions::IsIoStoreEnabled()
{
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 27)
	bool bUseIoStore = false;

	// Read directly from editor config:
	// [/Script/UnrealEd.ProjectPackagingSettings]
	// bUseIoStore=<true/false>
	GConfig->GetBool(
		TEXT("/Script/UnrealEd.ProjectPackagingSettings"),
		TEXT("bUseIoStore"),
		bUseIoStore,
		GEditorIni
	);

	return bUseIoStore;
#else
	return false;
#endif
}
