// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/Timespan.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "PakCreatorLog.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPakCreatorProcess, Log, All);

// Delegates
DECLARE_DELEGATE_OneParam(FOnExitProcessOutput, int32)

/**
* Implements an external process that can be interacted.
*/
class MODCREATOR_API FUATProcess : public FRunnable
{
public:

	FUATProcess();
	~FUATProcess();

	// Is this process still running?
	bool IsRunning() const
	{
		return bThreadRunning;
	}

	void Cancel()
	{
		bCanceling = true;
	}

	// Launch the process and create the thread.
	bool Launch(const FString& InURL, const FString& InWorkingDirectory, const FString& InParams);

	// Called when the program ends.
	FOnExitProcessOutput& OnTerminated()
	{
		return TerminatedDelegate;
	}

	// FRunnable interface
	virtual bool Init() override
	{
		return true;
	}

	virtual uint32 Run() override;

	/*virtual void Stop() override
	{
		Cancel();
	}*/

	virtual void Exit() override { }

	static void GetUATExecutable(FString& OutExecutablePath, FString& OutExecutable);

protected:

	// Pass the log file back to us.
	void ProcessOutput(const FString& Output);

private:

	// Whether this thread is running
	FThreadSafeBool bThreadRunning;

	// Whether the process is being canceled.
	FThreadSafeBool bCanceling;

	// Whether the window of the process should be hidden.
	bool bHidden : 1;

	// Holds the handle to the process.
	FProcHandle ProcessHandle;

	// Holds the read pipe.
	void* ReadPipe;

	// Holds the write pipe.
	void* WritePipe;

	// Holds the monitoring thread object.
	FRunnableThread* Thread;

	// Holds a delegate that is executed when the process has been canceled.
	FOnExitProcessOutput TerminatedDelegate;
};
