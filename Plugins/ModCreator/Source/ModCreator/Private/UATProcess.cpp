// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "UATProcess.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "PakCreatorLog.h"

DEFINE_LOG_CATEGORY(LogPakCreatorProcess);

FUATProcess::FUATProcess()
	: bThreadRunning(false)
	, bCanceling(false)
	, bHidden(true)
	, ReadPipe(nullptr)
	, WritePipe(nullptr)
	, Thread(nullptr)
{
}

FUATProcess::~FUATProcess()
{
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

static bool EnsureAutomationToolPresent()
{
	const FString AutoDll = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/DotNET/AutomationTool/AutomationTool.dll"));
	if (!FPaths::FileExists(AutoDll))
	{
		UE_LOG(LogPakCreator, Error, TEXT("Missing AutomationTool.dll at: %s\nFix: Epic Launcher -> UE 5.4 -> Verify (or use a source engine)."), *AutoDll);
		return false;
	}
	return true;
}

bool FUATProcess::Launch(const FString& URL, const FString& WorkingDirectory, const FString& Params)
{
	if (IsRunning())
	{
		return false;
	}

	// ✅ Verify AutomationTool.dll exists before trying to run UAT
	if (!EnsureAutomationToolPresent())
	{
		UE_LOG(LogPakCreatorProcess, Error, TEXT("AutomationTool missing. Aborting UAT run."));
		TerminatedDelegate.ExecuteIfBound(1);  // report failure back to the window
		return false;
	}

	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		return false;
	}

	UE_LOG(LogPakCreatorProcess, Log, TEXT("Launch process [%s] [%s] [%s]"), *URL, *WorkingDirectory, *Params);

	// Launch the builder and monitor its process
	ProcessHandle = FPlatformProcess::CreateProc(*URL, *Params, false, bHidden, bHidden, nullptr, 0, *WorkingDirectory, WritePipe, ReadPipe);

	if (!ProcessHandle.IsValid())
	{
		return false;
	}

	// Creating name for the process
	static uint32 TempInteractiveProcessIndex = 0;
	const FString ThreadName = FString::Printf(TEXT("FUATProcess %d"), TempInteractiveProcessIndex);
	TempInteractiveProcessIndex++;

	Thread = FRunnableThread::Create(this, *ThreadName);

	return Thread != nullptr;
}

void FUATProcess::ProcessOutput(const FString& Output)
{
	TArray<FString> LogLines;
	if (Output.Len() > 0 && Output.ParseIntoArray(LogLines, TEXT("\n"), true) > 0)
	{
		for (FString& Line : LogLines)
		{
			if (Line.Len() > 0)
			{
				//Line.RemoveFromEnd(TEXT("\r\n"));
				//Line.RemoveFromEnd(TEXT("\n"));
				Line.RemoveFromEnd(TEXT("\r"));

				UE_LOG(LogPakCreatorProcess, Log, TEXT("%s"), *Line);
			}
		}
	}
}

// FRunnable interface
uint32 FUATProcess::Run()
{
	bThreadRunning = true;

	bool bIsProcRunning = false;
	do
	{
		// 1 millisecond sleep is a problem on Windows platform, dont change this
		FPlatformProcess::Sleep(0.0f);

		// Read pipe
		ProcessOutput(FPlatformProcess::ReadPipe(ReadPipe));

		bIsProcRunning = FPlatformProcess::IsProcRunning(ProcessHandle);
	}
	while (!bCanceling && bThreadRunning && bIsProcRunning);

	int32 Code = 0;
	FPlatformProcess::GetProcReturnCode(ProcessHandle, &Code);

	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	ReadPipe = WritePipe = nullptr;

	// Ensure process is killed when this thread exists
	FPlatformProcess::CloseProc(ProcessHandle);

	FPlatformProcess::Sleep(0.25f);

	if (bCanceling == false)
	{
		TerminatedDelegate.ExecuteIfBound(Code);
	}

	bThreadRunning = false;

	return 0;
}

void FUATProcess::GetUATExecutable(FString& OutExecutablePath, FString& OutExecutable)
{
	// UAT executable
	OutExecutablePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Build"), TEXT("BatchFiles")));
#if PLATFORM_MAC
	OutExecutable = TEXT("RunUAT.command");
#elif PLATFORM_LINUX
	OutExecutable = TEXT("RunUAT.sh");
#else
	OutExecutable = TEXT("RunUAT.bat");
#endif
}
