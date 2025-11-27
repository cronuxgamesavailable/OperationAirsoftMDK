#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"

class FToolBarBuilder;
class FMenuBuilder;

class FModCreatorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();                // ToolMenus population
    void OnCreateClicked() const;        // handlers
    void OnPackageClicked() const;
    
    // Checks patch.json in the plugin and prompts to update project files if needed. //
    void RunPatchCheck();

private:
    TSharedPtr<class FUICommandList> PluginCommands;
};