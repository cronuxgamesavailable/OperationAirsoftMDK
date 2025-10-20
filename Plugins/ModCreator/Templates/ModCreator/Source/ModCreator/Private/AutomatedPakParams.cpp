#include "AutomatedPakParams.h"

// Define the static members declared in the header
const TArray<FString> FAutomatedPakParams::ValidPlatformNames = {
    TEXT("Win64"),
    TEXT("WindowsEditor"),
    TEXT("Linux"),
    TEXT("Mac"),
    TEXT("Android"),
    TEXT("IOS")
};

const FString FAutomatedPakParams::ReleaseVersionName = TEXT("Release");