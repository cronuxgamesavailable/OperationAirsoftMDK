#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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

private:
    TSharedPtr<class FUICommandList> PluginCommands;
};