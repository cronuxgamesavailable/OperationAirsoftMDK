// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "PakCreatorWindow.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Views/ITypedTableView.h"
#include "HAL/FileManager.h"
#include "FileHelpers.h"
#include "Widgets/SWidget.h"
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0) || (ENGINE_MAJOR_VERSION == 4)
#include "EditorStyleSet.h"
#endif
#include "Async/Async.h"
#include "PakCreatorLog.h"
#include "PakCreatorStyle.h"
#include "UATProcess.h"
#include "AutomatedPakParams.h"
#include "PakHelperFunctions.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "IDesktopPlatform.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"


#define LOCTEXT_NAMESPACE "FPakCreatorWindow"

FPakCreatorWindow::~FPakCreatorWindow()
{
	if (Runnable.IsValid())
	{
		// Cancel thread
		Runnable->Cancel();

		// Wait for join
		Runnable.Reset();
	}
}

FText FPakCreatorWindow::GetCurrentProjectFile() const
{
	return FText::FromString(OutputProject);
}

FText FPakCreatorWindow::GetCurrentReleaseName() const
{
	return FText::FromString(OutputRelease);
}

FText FPakCreatorWindow::GetCurrentPath() const
{
	return FText::FromString(OutputPath);
}

FText FPakCreatorWindow::GetCurrentPlatform() const
{
	return FText::FromString(*FAutomatedPakParams::ValidPlatformNames[0]);
}

TSharedRef<SDockTab> FPakCreatorWindow::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	for (const FString& PlatformName : FAutomatedPakParams::ValidPlatformNames)
	{
		PlatformsSource.Add(MakeShared<FString>(PlatformName));
	}
	check(PlatformsSource.Num() == FAutomatedPakParams::ValidPlatformNames.Num());

	FProjectStatus ProjectStatus;
	const bool bHasCode = IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && ProjectStatus.bCodeBasedProject;

	const IDesktopPlatform* const DesktopPlatform = FDesktopPlatformModule::Get();
	TArray<FTargetInfo> Targets = bHasCode ? DesktopPlatform->GetTargetsForCurrentProject() : DesktopPlatform->GetTargetsForProject(FString());

	if (Targets.Num() > 0)
	{
		Targets.Sort([](const FTargetInfo& A, const FTargetInfo& B) { return A.Name < B.Name; });

		const TArray<FTargetInfo> ValidTargets = Targets.FilterByPredicate([](const FTargetInfo& Target)
			{
				return Target.Type == EBuildTargetType::Game || Target.Type == EBuildTargetType::Client || Target.Type == EBuildTargetType::Server;
			});

		for (const FTargetInfo& Target : ValidTargets)
		{
			TargetSource.Add(MakeShared<FString>(Target.Name));
			UE_LOG(LogPakCreator, Verbose, TEXT("Added target %s"), *Target.Name);
		}
	}

	FText ProjectPathText = LOCTEXT("ProjectPathWidgetText", ".uproject File");
	FText ReleaseNameText = LOCTEXT("ReleasenameWidgetText", "Release Name of Project");
	FText SelectPluginText = LOCTEXT("SelectPluginWidgetText", "Select Content Plugin");
	FText PlatformText = LOCTEXT("PlatformText", "Platform");
	FText PlatformToolTip = LOCTEXT("PlatformToolTip", "Choose a target platform for cooking the assets for the pak file.");
	FText TargetText = LOCTEXT("TargetText", "Build Target");
	FText TargetToolTip = LOCTEXT("TargetToolTip", "Choose a build target.");
	FText CreatePakText = LOCTEXT("CreatePakWidgetText", "Create .pak File");
	FText OutputPathText = LOCTEXT("OutputPathWidgetText", "Select .pak Output Path");
	FText HintFilterContentPluginsText = LOCTEXT("HintFilterContentPluginsWidgetText", "Filter by name");
	FText SetFolderText = LOCTEXT("SetFolderhWidgetText", "Browse");
	FText SetProjectText = LOCTEXT("SetProjectWidgetText", "Browse");
	FText LogText = LOCTEXT("LogWidgetText", "Log goes here");

	// Populate plugin list (excludes ModCreator) before building the UI
	PopulateLayoutContentPluginList();

	TSharedRef<SDockTab> PluginTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SOverlay)
				+ SOverlay::Slot()
				.Padding(10.0f)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					// === VERTICAL LAYOUT WITH TABS ===
					SNew(SVerticalBox)

						// --- TAB BUTTONS ROW (toggle-style) ---
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 8)
						[
							SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(0, 0, 12, 0)
								[
									SNew(SCheckBox)
										.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
										.IsChecked_Lambda([this]() { return ActiveTabIndex == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
										.OnCheckStateChanged_Lambda([this](ECheckBoxState)
											{
												RefreshLayoutList();
												ActiveTabIndex = 0;
												if (TabSwitcher.IsValid()) { TabSwitcher->SetActiveWidgetIndex(0); }
											})
										[
											SNew(STextBlock).Text(FText::FromString(TEXT("Package Mod")))
										]
								]
							+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SCheckBox)
										.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
										.IsChecked_Lambda([this]() { return ActiveTabIndex == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
										.OnCheckStateChanged_Lambda([this](ECheckBoxState)
											{
												ActiveTabIndex = 1;
												if (TabSwitcher.IsValid()) { TabSwitcher->SetActiveWidgetIndex(1); }
												RefreshLayoutList();
											})
										[
											SNew(STextBlock).Text(FText::FromString(TEXT("Layouts")))
										]
								]
						]

					// --- TAB CONTENT SWITCHER ---
					+ SVerticalBox::Slot()
						.FillHeight(1.f)
						.HAlign(HAlign_Fill)
						[
							SAssignNew(TabSwitcher, SWidgetSwitcher)

								// === TAB 0: MAIN (fills window) ===
								+ SWidgetSwitcher::Slot()
								[
									SNew(SHorizontalBox)

										// LEFT COLUMN
										+ SHorizontalBox::Slot()
										.FillWidth(0.5f)
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										[
											SNew(SVerticalBox)
												+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock)
														.Text(SelectPluginText)
												]
												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(10.0f)
												.AutoHeight()
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.VAlign(VAlign_Fill)
														.HAlign(HAlign_Fill)
														[
															SAssignNew(FilterInput, SEditableTextBox)
																.HintText(HintFilterContentPluginsText)
																.OnTextChanged(this, &FPakCreatorWindow::OnFilterTextChanged)
																.OnVerifyTextChanged(this, &FPakCreatorWindow::OnFilterVerifyTextChanged)
																.OnTextCommitted(this, &FPakCreatorWindow::OnFilterTextCommitted)
														]
												]
											+ SVerticalBox::Slot()
												.Padding(10.0f)
												.FillHeight(1.f)
												[
													SAssignNew(PluginListWidget, SListView<TSharedPtr<FStringEntry>>)
														.ListItemsSource(&Plugins)
														.SelectionMode(ESelectionMode::Type::Multi)
														.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForList)
														.ScrollbarVisibility(EVisibility::Hidden)
												]
										]

									// RIGHT COLUMN
									+ SHorizontalBox::Slot()
										.FillWidth(0.5f)
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										[
											SNew(SVerticalBox)
												+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock)
														.Text(ProjectPathText)
												]
												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(10.0f)
												.AutoHeight()
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.VAlign(VAlign_Fill)
														.HAlign(HAlign_Fill)
														[
															SAssignNew(ProjectFileInput, SEditableTextBox)
																.Text(this, &FPakCreatorWindow::GetCurrentProjectFile)
																.OnTextCommitted(this, &FPakCreatorWindow::OnProjectFileCommitted)
																.IsReadOnly(false)
														]
														+ SHorizontalBox::Slot()
														.HAlign(HAlign_Right)
														.AutoWidth()
														[
															SAssignNew(ProjectBrowserButton, SButton)
																.ButtonStyle(FCoreStyle::Get(), "NoBorder")
																.OnClicked(this, &FPakCreatorWindow::HandleProjectBrowseButtonClicked)
																.HAlign(HAlign_Right)
																.VAlign(VAlign_Center)
																.ForegroundColor(FSlateColor::UseForeground())
																[
																	SNew(STextBlock)
																		.Text(SetProjectText)
																]
														]
												]
											+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock)
														.Text(PlatformText)
												]
												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(10.0f)
												.AutoHeight()
												[
													SAssignNew(PlatformComboBox, SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&PlatformsSource)
														.InitiallySelectedItem(PlatformsSource[0])
														.ToolTipText(PlatformToolTip)
														.OnSelectionChanged(this, &FPakCreatorWindow::OnPlatformSelected)
														.OnGenerateWidget(this, &FPakCreatorWindow::GenerateComboBoxWidget)
														[
															SAssignNew(PlatformSelectionTextBlock, STextBlock)
																.Text(this, &FPakCreatorWindow::GetCurrentPlatform)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
																.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
#else
																.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
#endif
														]
												]
											+ SVerticalBox::Slot()
												.AutoHeight()
												[
													TargetSource.Num() > 1 ? SNew(STextBlock)
														.Text(TargetText)
														: SNullWidget::NullWidget
												]
												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(TargetSource.Num() > 1 ? 10.0f : 0.0f)
												.AutoHeight()
												[
													TargetSource.Num() > 1 ? SAssignNew(TargetComboBox, SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&TargetSource)
														.InitiallySelectedItem(TargetSource[0])
														.ToolTipText(TargetToolTip)
														.OnSelectionChanged(this, &FPakCreatorWindow::OnTargetSelected)
														.OnGenerateWidget(this, &FPakCreatorWindow::GenerateComboBoxWidget)
														[
															SAssignNew(TargetSelectionTextBlock, STextBlock)
																.Text(FText::FromString(*TargetSource[0]))
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
																.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
#else
																.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
#endif
														]
														: SNullWidget::NullWidget
												]
												+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock)
														.Text(OutputPathText)
												]
												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(10.0f)
												.AutoHeight()
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.VAlign(VAlign_Fill)
														.HAlign(HAlign_Fill)
														[
															SAssignNew(OutputInput, SEditableTextBox)
																.Text(this, &FPakCreatorWindow::GetCurrentPath)
																.OnTextCommitted(this, &FPakCreatorWindow::OnPathTextCommitted)
														]
														+ SHorizontalBox::Slot()
														.HAlign(HAlign_Right)
														.AutoWidth()
														[
															SAssignNew(BrowserButton, SButton)
																.ButtonStyle(FCoreStyle::Get(), "NoBorder")
																.OnClicked(this, &FPakCreatorWindow::HandleBrowseButtonClicked)
																.HAlign(HAlign_Right)
																.VAlign(VAlign_Center)
																.ForegroundColor(FSlateColor::UseForeground())
																[
																	SNew(STextBlock)
																		.Text(SetFolderText)
																]
														]
												]
											+ SVerticalBox::Slot()
												.Padding(10.0f)
												.AutoHeight()
												[
													SAssignNew(CreateButton, SButton)
#if ENGINE_MAJOR_VERSION == 4
														.ButtonStyle(FCoreStyle::Get(), "NoBorder")
#endif
														.OnClicked(this, &FPakCreatorWindow::CreateButtonPressed)
														.HAlign(HAlign_Center)
														.VAlign(VAlign_Center)
														.ForegroundColor(FSlateColor::UseForeground())
														[
															SNew(STextBlock)
																.Text(CreatePakText)
														]
												]
											+ SVerticalBox::Slot()
												.FillHeight(1.f)
												.HAlign(HAlign_Fill)
												[
													SAssignNew(LogListWidget, SListView<TSharedPtr<FStringEntry>>)
														.ListItemsSource(&LogEntries)
														.SelectionMode(ESelectionMode::Type::None)
														.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForLog)
														.ScrollbarVisibility(EVisibility::Hidden)
												]
										]
								]

							// === TAB 1: LAYOUTS (list + create/overwrite) ===
							+ SWidgetSwitcher::Slot()
								[
									SNew(SHorizontalBox)

										// LEFT: list of *.layout
										+ SHorizontalBox::Slot()
										.FillWidth(0.5f)
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										[
											SNew(SVerticalBox)
												+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock).Text(FText::FromString(TEXT("Layouts (Project/Mods/Layouts)")))
												]
												+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(10.0f, 6.0f, 10.0f, 6.0f)
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.FillWidth(1.f)
														[
															SAssignNew(LayoutFilterInput, SEditableTextBox)
																.HintText(FText::FromString(TEXT("Filter layouts by name")))
																.OnTextChanged_Lambda([this](const FText& T) { FilterLayoutList(T.ToString()); })
														]
														+ SHorizontalBox::Slot()
														.AutoWidth()
														.Padding(6.f, 0, 0, 0)
														[
															SNew(SButton)
																.OnClicked(this, &FPakCreatorWindow::HandleDeselectLayoutClicked)
																[
																	SNew(STextBlock).Text(FText::FromString(TEXT("Deselect")))
																]
														]
												]
											+ SVerticalBox::Slot()
												.Padding(10.0f)
												.FillHeight(1.f)
												[
													SAssignNew(LayoutListWidget, SListView<TSharedPtr<FStringEntry>>)
														.ListItemsSource(&LayoutFiles)
														.SelectionMode(ESelectionMode::Type::Single)
														.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForLayout)
														.OnSelectionChanged(this, &FPakCreatorWindow::OnLayoutSelectionChanged)
														.ScrollbarVisibility(EVisibility::Hidden)
												]
										]

									// RIGHT: create/export controls
									+ SHorizontalBox::Slot()
										.FillWidth(0.5f)
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										[
											SNew(SVerticalBox)

												+ SVerticalBox::Slot()
												.AutoHeight()
												[
													SNew(STextBlock).Text(FText::FromString(TEXT("Layout Name")))
												]
												+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(10.0f, 6.0f, 10.0f, 6.0f)
												[
													SAssignNew(LayoutNameInput, SEditableTextBox)
														.HintText(FText::FromString(TEXT("e.g. Speedball")))
												]

												// Content Plugin for Map
												+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(0, 10, 0, 0)
												[
													SNew(STextBlock).Text(FText::FromString(TEXT("Plugin with Map Assets")))
												]
												+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(10.0f, 6.0f)
												[
													SAssignNew(LayoutPluginComboBox, SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&LayoutPluginSource)
														.InitiallySelectedItem(LayoutPluginSource.Num() > 0 ? LayoutPluginSource[0] : TSharedPtr<FString>())
														.OnSelectionChanged(this, &FPakCreatorWindow::OnLayoutPluginSelected)
														.OnGenerateWidget(this, &FPakCreatorWindow::GenerateComboBoxWidget)
														[
															SAssignNew(LayoutPluginSelectionText, STextBlock)
																.Text(this, &FPakCreatorWindow::GetCurrentLayoutPluginText)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
																.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
#else
																.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
#endif
														]
												]

											// Map (.umap)
											+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(0, 10, 0, 0)
												[
													SNew(STextBlock).Text(FText::FromString(TEXT("Map for the Layout")))
												]
												+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(10.0f)
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.FillWidth(1.f)
														[
															SAssignNew(UmapPathInput, SEditableTextBox)
																.IsReadOnly(true)
																.HintText(FText::FromString(TEXT("Select a .umap")))
														]
														+ SHorizontalBox::Slot()
														.AutoWidth()
														[
															SNew(SButton)
																.OnClicked(this, &FPakCreatorWindow::HandleSelectUmapClicked)
																[
																	SNew(STextBlock).Text(FText::FromString(TEXT("Select .umap")))
																]
														]
												]
											+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(10.0f)
												[
													SNew(SButton)
														.OnClicked(this, &FPakCreatorWindow::HandleCreateLayoutClicked)
														.HAlign(HAlign_Center)
														[
															SNew(STextBlock)
																.Text(this, &FPakCreatorWindow::GetCreateLayoutButtonText)
														]
												]

											// --- Layout Log (same spot as Package Mod) --- // NEW
											+ SVerticalBox::Slot() // NEW
												.FillHeight(1.f)    // NEW
												.HAlign(HAlign_Fill)// NEW
												[                   // NEW
													SAssignNew(LayoutLogListWidget, SListView<TSharedPtr<FStringEntry>>) // NEW
														.ListItemsSource(&LogEntries)    // NEW
														.SelectionMode(ESelectionMode::Type::None) // NEW
														.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForLog) // NEW
														.ScrollbarVisibility(EVisibility::Hidden) // NEW
												]                   // NEW
										]
								]
						]
				]
		];

	PluginTab->SetTabIcon(FPakCreatorStyle::Get().GetBrush("PakCreator.OpenPluginWindow"));
	PluginTab->SetLabel(FText::FromString(TEXT("Package Mod")));

	// Grab saved vars
	if (GConfig != nullptr)
	{
		GConfig->GetString(
			TEXT("PakCreator.Core"),
			TEXT("ReleaseGameName"),
			OutputRelease,
			GEditorPerProjectIni
		);

		GConfig->GetString(
			TEXT("PakCreator.Core"),
			TEXT("OutputPath"),
			OutputPath,
			GEditorPerProjectIni
		);
	}

	if (FPaths::IsProjectFilePathSet())
	{
		OutputProject = FPaths::GetProjectFilePath();
	}
	else
	{
		OutputProject = FPaths::ProjectDir() / FApp::GetProjectName() + ".uproject";
	}

	OutputProject = FPaths::ConvertRelativePathToFull(OutputProject);

	PopulatePluginList(FPaths::ProjectPluginsDir());

	LogListWidget->RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &FPakCreatorWindow::RefreshLog));

	return PluginTab;
}

void FPakCreatorWindow::PopulatePluginList(const FString& ProjectPluginDirectory)
{
	AllPlugins.Empty();
	Plugins.Empty();

	// Populate plugins
	const TArray<FString> PluginDirectories = FPakHelperFunctions::GetPluginFolders(FPaths::GetPath(OutputProject) / TEXT("Plugins"));
	for (const FString& Directory : PluginDirectories)
	{
		// Skip this plugin
		if (Directory == "PakCreator")
			continue;

		TSharedPtr<FStringEntry> Entry = MakeShareable(new FStringEntry(Directory));

		AllPlugins.Add(Entry);
	}

	Plugins = AllPlugins;

	if (PluginListWidget && PluginListWidget->IsParentValid())
	{
		PluginListWidget->RebuildList();
	}
}

void FPakCreatorWindow::FilterPluginList(const FString& InFilterText)
{
	Plugins.Empty();

	if (InFilterText.Len() > 0)
	{
		for (const TSharedPtr<FStringEntry>& PluginName : AllPlugins)
		{
			if (PluginName->PluginPath.Contains(InFilterText))
			{
				Plugins.Add(PluginName);
			}
		}
	}
	else
	{
		Plugins = AllPlugins;
	}

	if (PluginListWidget && PluginListWidget->IsParentValid())
	{
		PluginListWidget->RebuildList();
	}
}

FReply FPakCreatorWindow::CreateButtonPressed()
{
	ensure(IsInGameThread());

	if (PendingUATCommands.Peek())
	{
		// Already running
		return FReply::Handled();
	}

	LogEntries.Empty();

	if (FPakHelperFunctions::IsIoStoreEnabled())
	{
		AddLogMessage(FString::Printf(TEXT("Warning: \"IoStore\" is enabled in project settings. In order to only use .pak files you must disable this setting.")));
	}

	FText Reason;
	if (OutputPath.Len() == 0 || !FPaths::ValidatePath(OutputPath, &Reason))
	{
		AddLogMessage(FString::Printf(TEXT("Error: Output path or length is invalid %s (%s)"), *Reason.ToString(), *OutputPath));
		return FReply::Handled();
	}

	const TArray<TSharedPtr<FStringEntry>> SelectedItems = PluginListWidget->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		AddLogMessage(TEXT("Error: No plugin selected!"));
		return FReply::Handled();
	}

	const TSharedPtr<FString> SelectedPlatform = PlatformComboBox->GetSelectedItem();
	if (!SelectedPlatform.IsValid())
	{
		AddLogMessage(TEXT("Error: No platform selected!"));
		return FReply::Handled();
	}

	const TSharedPtr<FString> TargetName = TargetComboBox ? TargetComboBox->GetSelectedItem() : nullptr;

	PendingUATCommands.Empty();

	const FString ConfigurationName = "Shipping";
	FString PlatformName = FAutomatedPakParams::ValidPlatformNames[0];
	FString Cookflavor = "";

	FPakHelperFunctions::GetPlatformNameAndFlavorBySelection(*SelectedPlatform, PlatformName, Cookflavor);

	AddLogMessage(FString::Printf(TEXT("Platform: %s%s"), *PlatformName, *Cookflavor));

	const FString BaseGameCommand =
		FPakHelperFunctions::MakeUATCommand(
			OutputProject,
			PlatformName,
			Cookflavor,
			ConfigurationName,
			TargetName ? *TargetName : "",
			FPaths::Combine(GetTemporaryStagingDirectory(), FAutomatedPakParams::ReleaseVersionName))
		+ FPakHelperFunctions::MakeUATParams_BaseGame(OutputProject, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
	PendingUATCommands.Enqueue({ FAutomatedPakParams::ReleaseVersionName, BaseGameCommand });
#else
	PendingUATCommands.Enqueue(TPair<FString, FString>(FAutomatedPakParams::ReleaseVersionName, BaseGameCommand));
#endif

	int32 NumPluginBuilds = 0;

	for (const TSharedPtr<FStringEntry>& Item : SelectedItems)
	{
		const FString DLCCommand =
			FPakHelperFunctions::MakeUATCommand(
				OutputProject,
				PlatformName,
				Cookflavor,
				ConfigurationName,
				TargetName ? *TargetName : "",
				FPaths::Combine(GetTemporaryStagingDirectory(), Item->PluginPath))
			+ FPakHelperFunctions::MakeUATParams_DLC(Item->PluginPath, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
		PendingUATCommands.Enqueue({ Item->PluginPath, DLCCommand });
#else
		PendingUATCommands.Enqueue(TPair<FString, FString>(Item->PluginPath, DLCCommand));
#endif

		NumPluginBuilds++;
	}

	AddLogMessage(FString::Printf(TEXT("Building %i plugin%s"), NumPluginBuilds, NumPluginBuilds > 1 ? TEXT("s") : TEXT("")));
	AddLogMessage(TEXT("Notice: Building can take longer on the first run due to shaders compiling. Please be patient."));

	// Start off first build
	RunBuild();

	return FReply::Handled();
}

bool FPakCreatorWindow::RunBuild()
{
	check(IsInGameThread());

#if ENGINE_MAJOR_VERSION == 5
	decltype(PendingUATCommands)::FElementType BuildItem;
#else
	TPair<FString, FString> BuildItem;
#endif
	
	if (PendingUATCommands.Dequeue(BuildItem))
	{
		AddLogMessage(FString::Printf(TEXT("Building \"%s\""), *BuildItem.Key));

		CurrentTaskName = BuildItem.Key;

		return RunUATBuildProcess(BuildItem.Value);
	}

	return false;
}

bool FPakCreatorWindow::RunUATBuildProcess(const FString& CommandLine)
{
	check(Runnable == nullptr);

	FString ExecutablePath, Executable;
	FUATProcess::GetUATExecutable(ExecutablePath, Executable);

	Runnable = MakeShared<FUATProcess>();
	Runnable->OnTerminated().BindSP(this, &FPakCreatorWindow::ProcessComplete);

	return Runnable->Launch(ExecutablePath / Executable, ExecutablePath, CommandLine);
}

FString FPakCreatorWindow::GetTemporaryStagingDirectory() const
{
	return FPaths::Combine(OutputPath, TEXT("__TMP_STAGING__"));
}

void FPakCreatorWindow::ProcessComplete(int32 ErrorCode)
{
	if (!IsInGameThread())
	{
		// Retry on the GameThread.
		TWeakPtr<FPakCreatorWindow> WeakShared = this->AsShared();
		AsyncTask(ENamedThreads::GameThread, [WeakShared, ErrorCode]()
		{
			const TSharedPtr<FPakCreatorWindow>& PakCreatorWindow = WeakShared.Pin();
			if (PakCreatorWindow.IsValid())
			{
				PakCreatorWindow->ProcessComplete(ErrorCode);
			}
		});
		return;
	}

	Runnable.Reset();

	if (ErrorCode != 0)
	{
		PendingUATCommands.Empty();

		AddLogMessage(FString::Printf(TEXT("UAT BuildCookRun exited with error code = %i. See the Output Log for more information. (Window -> Developer Tools -> Output Log)"), ErrorCode));
		return;
	}

	AddLogMessage(TEXT("Build completed"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Check if the current task is a pak or just the base game.
	if (CurrentTaskName != FAutomatedPakParams::ReleaseVersionName)
	{
		// Its a pak, let's move the new pak to our desired location.

		const FString CurrentStagingDirectory = GetTemporaryStagingDirectory() / CurrentTaskName;

		TArray<FString> FoundFiles;
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".pak"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".ucas"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".utoc"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".sig"));

		if (FoundFiles.Num() > 0)
		{
			for (const FString& File : FoundFiles)
			{
				const FString CopyTo = OutputPath / FPaths::GetCleanFilename(File);

				// Delete existing pak at target location
				if (PlatformFile.FileExists(*CopyTo))
				{
					PlatformFile.DeleteFile(*CopyTo);
				}

				if (PlatformFile.MoveFile(*CopyTo, *File))
				{
					AddLogMessage(FString::Printf(TEXT("Moving %s to %s"), *FPaths::GetCleanFilename(File), *OutputPath));
				}
				else
				{
					AddLogMessage(FString::Printf(TEXT("Error: Failed to move file from %s to %s"), *File, *CopyTo));
				}
			}
		}
		else
		{
			AddLogMessage(FString::Printf(TEXT("Error: Pak file not found in directory %s"), *CurrentStagingDirectory));
		}
	}

	if (!RunBuild())
	{
		// If no more builds have been launched do some cleanup

		// Remove temporary staging directory
		if (!PlatformFile.DeleteDirectoryRecursively(*GetTemporaryStagingDirectory()))
		{
			AddLogMessage(FString::Printf(TEXT("Warning: Failed to delete temporary build directory %s"), *GetTemporaryStagingDirectory()));
		}

		// === Copy modinfo.json into subfolder named after mod ===
		const FString PluginFolder = FPaths::Combine(FPaths::ProjectPluginsDir(), CurrentTaskName);
		const FString ModInfoSource = FPaths::Combine(PluginFolder, TEXT("modinfo.json"));

		if (PlatformFile.FileExists(*ModInfoSource))
		{
			const FString SubFolderPath = FPaths::Combine(OutputPath, CurrentTaskName);
			PlatformFile.CreateDirectoryTree(*SubFolderPath);

			const FString Destination = FPaths::Combine(SubFolderPath, TEXT("modinfo.json"));

			if (PlatformFile.CopyFile(*Destination, *ModInfoSource))
			{
				AddLogMessage(TEXT("Copied modinfo.json into subfolder"));
			}
			else
			{
				AddLogMessage(TEXT("Error: Failed to copy modinfo.json into subfolder"));
			}
		}
		else
		{
			AddLogMessage(TEXT("Warning: modinfo.json not found in plugin folder"));
		}

		AddLogMessage(TEXT("All builds finished"));
	}

}

void FPakCreatorWindow::AddLogMessage(const FString& Message)
{
	checkf(IsInGameThread(), TEXT("AddLogMessage called from non game thread. %s"), *Message);

	UE_LOG(LogPakCreator, Log, TEXT("%s"), *Message);

	TSharedPtr<FStringEntry> Entry = MakeShareable(new FStringEntry(Message));
	LogEntries.Add(Entry);

	// Instant UI refresh on both tabs (even if one is hidden in the switcher)
	if (LogListWidget.IsValid())
	{
		LogListWidget->RequestListRefresh();
		LogListWidget->RequestScrollIntoView(Entry);
	}
	if (LayoutLogListWidget.IsValid()) // NEW
	{
		LayoutLogListWidget->RequestListRefresh();
		LayoutLogListWidget->RequestScrollIntoView(Entry);
	}
}

EActiveTimerReturnType FPakCreatorWindow::RefreshLog(double InCurrentTime, float InDeltaTime)
{
	if (LogListWidget.IsValid())
	{
		LogListWidget->RequestListRefresh();

		// Optional auto-scroll
		if (LogEntries.Num() > 0)
		{
			LogListWidget->RequestScrollIntoView(LogEntries.Last());
		}
	}
	if (LayoutLogListWidget.IsValid()) // NEW
	{
		LayoutLogListWidget->RequestListRefresh();

		// Optional auto-scroll
		if (LogEntries.Num() > 0)
		{
			LayoutLogListWidget->RequestScrollIntoView(LogEntries.Last());
		}
	}

	return EActiveTimerReturnType::Continue;
}

FReply FPakCreatorWindow::HandleBrowseButtonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(CreateButton.ToSharedRef());
		void* ParentWindowHandle = (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid()) ? ParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

		FString FolderName;
		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			ParentWindowHandle,
			LOCTEXT("FolderDialogTitle", "Choose a directory").ToString(),
			OutputPath,
			FolderName
		);

		UE_LOG(LogPakCreator, Verbose, TEXT("Folder Name = %s"), *FolderName);
		OutputPath = FolderName;

		if (GConfig != nullptr)
		{
			GConfig->SetString(
				TEXT("PakCreator.Core"),
				TEXT("OutputPath"),
				*OutputPath,
				GEditorPerProjectIni
			);

			GConfig->Flush(false, GEditorPerProjectIni);
		}
	}

	return FReply::Handled();
}

FReply FPakCreatorWindow::HandleProjectBrowseButtonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(CreateButton.ToSharedRef());
		void* ParentWindowHandle = (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid()) ? ParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

		TArray<FString> Files;

		const bool bFileSelected = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			LOCTEXT("FileDialogTitle", "Choose a file").ToString(),
			FPaths::GetPath(OutputProject),
			OutputProject,
			".uproject",
			0,
			Files
		);

		if (bFileSelected && Files.Num() > 0)
		{
			UE_LOG(LogPakCreator, Verbose, TEXT("Folder Name = %s"), *Files[0]);
			
			OutputProject = FPaths::ConvertRelativePathToFull(Files[0]);

			PopulatePluginList(FPaths::GetPath(OutputProject) / TEXT("Plugins"));
		}
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> FPakCreatorWindow::OnGenerateRowForList(TSharedPtr<FStringEntry> Plugin, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Plugins.IsValidIndex(0) || !Plugin.IsValid() || !Plugin.Get()) // Error catcher
	{
		return
			SNew(STableRow<TSharedPtr<FStringEntry>>, OwnerTable)
			[
				SNew(SBox)
			];
	}

	return SNew(STableRow<TSharedPtr<FStringEntry>>, OwnerTable)
		[
			SNew(SBox)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Plugin.Get()->PluginPath))
				.ColorAndOpacity(FLinearColor(0.25f, 0.25f, 0.25f, 1.f))
				//.Font(fontinfo)
				.ShadowColorAndOpacity(FLinearColor::Black)
				.ShadowOffset(FIntPoint(1, 1))
				.AutoWrapText(true)
			]
		];
}

TSharedRef<ITableRow> FPakCreatorWindow::OnGenerateRowForLog(TSharedPtr<FStringEntry> LogEntry, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!LogEntries.IsValidIndex(0) || !LogEntry.IsValid() || !LogEntry.Get()) // Error catcher
	{
		return
			SNew(STableRow<TSharedPtr<FStringEntry>>, OwnerTable)
			[
				SNew(SBox)
			];
	}

	return SNew(STableRow<TSharedPtr<FStringEntry>>, OwnerTable)
		[
			SNew(SBox)
			[
				SNew(STextBlock)
				.Text(FText::FromString(LogEntry.Get()->PluginPath))
				.ColorAndOpacity(FLinearColor(0.25f, 0.25f, 0.25f, 1.f))
				//.Font(fontinfo)
				.ShadowColorAndOpacity(FLinearColor::Black)
				.ShadowOffset(FIntPoint(1, 1))
			]
		];
}

TSharedRef<SWidget> FPakCreatorWindow::GenerateComboBoxWidget(TSharedPtr<FString> Item)
{
	return
		SNew(STextBlock)
		.Text_Lambda([this, Item]()
		{
			return FText::FromString(*Item);
	})
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
#else
		.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
#endif
		;
}

void FPakCreatorWindow::OnPathTextCommitted(const FText& InText, const ETextCommit::Type InTextAction)
{
	OutputPath = InText.ToString();

	if (GConfig != nullptr)
	{
		GConfig->SetString(
			TEXT("PakCreator.Core"),
			TEXT("OutputPath"),
			*OutputPath,
			GEditorPerProjectIni
		);

		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

void FPakCreatorWindow::OnFilterTextChanged(const FText& InText)
{
	FilterPluginList(InText.ToString());
}

bool FPakCreatorWindow::OnFilterVerifyTextChanged(const FText& InText, FText& OutText)
{
	OutText = InText;
	return true;
}

void FPakCreatorWindow::OnFilterTextCommitted(const FText& InText, const ETextCommit::Type InTextAction)
{
	FilterPluginList(InText.ToString());
}

void FPakCreatorWindow::OnPlatformSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
{
	PlatformSelectionTextBlock->SetText(FText::FromString(**SelectedItem));
}

void FPakCreatorWindow::OnTargetSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
{
	TargetSelectionTextBlock->SetText(FText::FromString(**SelectedItem));
}

void FPakCreatorWindow::OnProjectFileCommitted(const FText& InText, const ETextCommit::Type InTextAction)
{
	OutputProject = InText.ToString();
}

void FPakCreatorWindow::OnReleaseNameCommitted(const FText& InText, const ETextCommit::Type InTextAction)
{
	OutputRelease = InText.ToString();

	if (GConfig != nullptr)
	{
		GConfig->SetString(
			TEXT("PakCreator.Core"),
			TEXT("ReleaseGameName"),
			*OutputRelease,
			GEditorPerProjectIni
		);

		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

// Layout Helpers
FString FPakCreatorWindow::GetLayoutsFolderOnDisk() const
{
	// <Project>/Mods/Layouts
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Mods"), TEXT("Layouts")));
}

void FPakCreatorWindow::RefreshLayoutList()
{
	const FString Root = GetLayoutsFolderOnDisk();
	IFileManager::Get().MakeDirectory(*Root, /*Tree=*/true);

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.layout"), /*Files=*/true, /*Directories=*/false);
	Files.Sort();

	AllLayoutFiles.Empty();
	for (const FString& F : Files)
	{
		AllLayoutFiles.Add(MakeShared<FStringEntry>(F));
	}
	// keep filter
	const FString CurrentFilter = LayoutFilterInput.IsValid() ? LayoutFilterInput->GetText().ToString() : TEXT("");
	FilterLayoutList(CurrentFilter);

	if (LayoutListWidget.IsValid())
	{
		LayoutListWidget->RequestListRefresh();
	}
}

void FPakCreatorWindow::FilterLayoutList(const FString& InFilter)
{
	const FString Filter = InFilter.TrimStartAndEnd();
	LayoutFiles.Empty();
	if (Filter.IsEmpty())
	{
		LayoutFiles = AllLayoutFiles;
	}
	else
	{
		for (const auto& E : AllLayoutFiles)
		{
			const FString Name = FPaths::GetCleanFilename(E->PluginPath);
			if (Name.Contains(Filter))
			{
				LayoutFiles.Add(E);
			}
		}
	}
	if (LayoutListWidget.IsValid())
	{
		LayoutListWidget->RequestListRefresh();
	}
}

TSharedRef<ITableRow> FPakCreatorWindow::OnGenerateRowForLayout(TSharedPtr<FStringEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FStringEntry>>, OwnerTable)
		[
			SNew(STextBlock).Text(FText::FromString(FPaths::GetCleanFilename(Item->PluginPath)))
		];
}

void FPakCreatorWindow::OnLayoutSelectionChanged(TSharedPtr<FStringEntry> Item, ESelectInfo::Type)
{
	SelectedLayoutEntry = Item;
	SelectedLayoutPath = Item.IsValid() ? Item->PluginPath : TEXT("");

	// If picked something, reflect its name in the name box
	if (LayoutNameInput.IsValid())
	{
		if (Item.IsValid())
		{
			LayoutNameInput->SetText(FText::FromString(FPaths::GetBaseFilename(Item->PluginPath)));
		}
	}
}

FReply FPakCreatorWindow::HandleDeselectLayoutClicked()
{
	SelectedLayoutEntry.Reset();
	SelectedLayoutPath.Reset();
	if (LayoutListWidget.IsValid())
	{
		LayoutListWidget->ClearSelection();
	}
	return FReply::Handled();
}

FReply FPakCreatorWindow::HandleSelectPakClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return FReply::Handled();

	TArray<FString> OutFiles;
	const bool bOk = Desktop->OpenFileDialog(
		nullptr,
		TEXT("Select .pak"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Unreal Pak (*.pak)|*.pak"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOk && OutFiles.Num() > 0)
	{
		SelectedPakPath = FPaths::ConvertRelativePathToFull(OutFiles[0]);
		if (PakPathInput.IsValid())
		{
			PakPathInput->SetText(FText::FromString(SelectedPakPath));
		}
	}
	return FReply::Handled();
}

FReply FPakCreatorWindow::HandleSelectUmapClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return FReply::Handled();

	TArray<FString> OutFiles;
	const bool bOk = Desktop->OpenFileDialog(
		nullptr,
		TEXT("Select .umap"),
		FPaths::ProjectContentDir(),
		TEXT(""),
		TEXT("Unreal Map (*.umap)|*.umap"),
		EFileDialogFlags::None,
		OutFiles
	);
	if (bOk && OutFiles.Num() > 0)
	{
		SelectedUmapPath = OutFiles[0];
		if (UmapPathInput.IsValid())
		{
			UmapPathInput->SetText(FText::FromString(SelectedUmapPath));
		}
	}
	return FReply::Handled();
}

static void AddMaterialOverridesIfAny(TSharedPtr<FJsonObject> OutActorJson, AActor* A, bool bCapture)
{
	if (!bCapture || !A) return;

	UStaticMeshComponent* SMC = nullptr;
	if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(A))
		SMC = SMA->GetStaticMeshComponent();
	if (!SMC)
		SMC = A->FindComponentByClass<UStaticMeshComponent>();
	if (!SMC) return;

	UStaticMesh* SM = SMC->GetStaticMesh();
	const int32 MatCount = SM ? SM->GetStaticMaterials().Num() : SMC->GetNumMaterials();

	TArray<TSharedPtr<FJsonValue>> Overrides;
	for (int32 i = 0; i < MatCount; ++i)
	{
		UMaterialInterface* MI = SMC->GetMaterial(i);
		if (!MI) continue;

		const FString MatPath = MI->GetPathName();
		FName SlotName = NAME_None;
		if (SM && SM->GetStaticMaterials().IsValidIndex(i))
			SlotName = SM->GetStaticMaterials()[i].MaterialSlotName;

		TSharedPtr<FJsonObject> JMO = MakeShared<FJsonObject>();
		JMO->SetNumberField(TEXT("slot_index"), i);
		if (!SlotName.IsNone())
			JMO->SetStringField(TEXT("slot_name"), SlotName.ToString());
		JMO->SetStringField(TEXT("material"), MatPath);

		Overrides.Add(MakeShared<FJsonValueObject>(JMO));
	}

	if (Overrides.Num() > 0)
		OutActorJson->SetArrayField(TEXT("material_overrides"), Overrides);
}

bool FPakCreatorWindow::GatherLayoutActorsJSON(TSharedRef<FJsonObject> OutRoot)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;

	OutRoot->SetNumberField(TEXT("version"), 1);

	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	static const FName LayoutTag(TEXT("LayoutItem"));

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || !A->ActorHasTag(LayoutTag)) continue;

		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("id"), A->GetName());
		J->SetStringField(TEXT("class"), A->GetClass()->GetPathName());

		FString AssetPath;
		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(A))
		{
			if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
			{
				if (UStaticMesh* SM = SMC->GetStaticMesh())
				{
					AssetPath = SM->GetPathName();
				}
			}
		}
		else if (UStaticMeshComponent* AnySMC = A->FindComponentByClass<UStaticMeshComponent>())
		{
			if (UStaticMesh* SM = AnySMC->GetStaticMesh())
			{
				AssetPath = SM->GetPathName();
			}
		}
		if (!AssetPath.IsEmpty())
		{
			J->SetStringField(TEXT("asset"), AssetPath);
		}

		const FTransform T = A->GetActorTransform();
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		const FVector S = T.GetScale3D();

		auto VecToArray = [](const FVector& V)
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				Arr.Add(MakeShared<FJsonValueNumber>(V.X));
				Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
				Arr.Add(MakeShared<FJsonValueNumber>(V.Z));
				return Arr;
			};

		TSharedPtr<FJsonObject> JT = MakeShared<FJsonObject>();
		JT->SetArrayField(TEXT("location"), VecToArray(L));
		JT->SetArrayField(TEXT("rotation"), VecToArray(FVector(R.Pitch, R.Yaw, R.Roll)));
		JT->SetArrayField(TEXT("scale"), VecToArray(S));
		J->SetObjectField(TEXT("transform"), JT);

		TArray<TSharedPtr<FJsonValue>> TagsJson;
		for (const FName& Tag : A->Tags)
		{
			TagsJson.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		J->SetArrayField(TEXT("tags"), TagsJson);

		// NEW: capture editor-assigned materials for runtime re-apply
		AddMaterialOverridesIfAny(J, A, true);

		ActorsJson.Add(MakeShared<FJsonValueObject>(J));
	}

	OutRoot->SetArrayField(TEXT("actors"), ActorsJson);
	return true;
}

FText FPakCreatorWindow::GetCreateLayoutButtonText() const
{
	if (SelectedLayoutEntry.IsValid())
	{
		return FText::FromString(TEXT("Overwrite .layout"));
	}
	return FText::FromString(TEXT("Create .layout File"));
}

FReply FPakCreatorWindow::HandleCreateLayoutClicked()
{
	// Clear logs like the pak flow
	LogEntries.Reset();
	if (LogListWidget.IsValid())          LogListWidget->RequestListRefresh();
	if (LayoutLogListWidget.IsValid())    LayoutLogListWidget->RequestListRefresh();

	const FString DestDir = GetLayoutsFolderOnDisk();
	IFileManager::Get().MakeDirectory(*DestDir, /*Tree=*/true);
	AddLogMessage(TEXT("Layout: Starting export…"));
	AddLogMessage(FString::Printf(TEXT("Layout: Output folder = %s"), *DestDir));

	FString Name = LayoutNameInput.IsValid() ? LayoutNameInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
	AddLogMessage(FString::Printf(TEXT("Layout: Input name = \"%s\""), *Name));

	FString DestFile;
	if (SelectedLayoutEntry.IsValid())
	{
		// Overwrite the selected file
		DestFile = SelectedLayoutPath;
		if (Name.IsEmpty())
		{
			Name = FPaths::GetBaseFilename(DestFile);
			AddLogMessage(FString::Printf(TEXT("Layout: No name typed; using selected file base name = \"%s\""), *Name));
		}
		AddLogMessage(FString::Printf(TEXT("Layout: Overwriting existing file = %s"), *DestFile));
	}
	else
	{
		if (Name.IsEmpty())
		{
			AddLogMessage(TEXT("Layout: Name is required (or select a layout to overwrite)."));
			return FReply::Handled();
		}
		DestFile = FPaths::Combine(DestDir, Name + TEXT(".layout"));
		AddLogMessage(FString::Printf(TEXT("Layout: Target file = %s"), *DestFile));
	}

	// Build JSON from current level actors with tag "LayoutItem"
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	AddLogMessage(TEXT("Layout: Gathering actors (tag: LayoutItem)…"));
	if (!GatherLayoutActorsJSON(Root.ToSharedRef()))
	{
		AddLogMessage(TEXT("Layout: Failed to gather actors from current level."));
		return FReply::Handled();
	}
	{
		const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
		const int32 ActorCount = (Root->TryGetArrayField(TEXT("actors"), ActorsArr) && ActorsArr) ? ActorsArr->Num() : 0;
		AddLogMessage(FString::Printf(TEXT("Layout: Collected %d actor(s)."), ActorCount));
	}

	// === Build mapAssetPath + mapForLayout ===
	if (SelectedUmapPath.IsEmpty())
	{
		AddLogMessage(TEXT("Layout: Please select a .umap."));
		return FReply::Handled();
	}
	AddLogMessage(FString::Printf(TEXT("Layout: Selected .umap = %s"), *SelectedUmapPath));

	FString AbsUmap = FPaths::ConvertRelativePathToFull(SelectedUmapPath);
	FPaths::NormalizeFilename(AbsUmap); // forward slashes

	const FString ProjectPluginsRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
	FString ProjectPluginsRootNorm = ProjectPluginsRoot;
	FPaths::NormalizeDirectoryName(ProjectPluginsRootNorm);

	FString UmapPluginName; // e.g. "tt"
	FString RelNoExt;       // e.g. "Maps/BaseMap"
	const TArray<TSharedRef<IPlugin>>& Discovered = IPluginManager::Get().GetDiscoveredPlugins();
	for (const TSharedRef<IPlugin>& P : Discovered)
	{
		if (!P->CanContainContent() || !P->IsEnabled())
			continue;

		FString BaseDir = FPaths::ConvertRelativePathToFull(P->GetBaseDir());
		FPaths::NormalizeDirectoryName(BaseDir);

		// exclude Engine/editor plugins
		if (!BaseDir.StartsWith(ProjectPluginsRootNorm, ESearchCase::IgnoreCase))
			continue;

		const FString ContentDir = FPaths::Combine(BaseDir, TEXT("Content"));
		FString ContentDirNorm = ContentDir;
		FPaths::NormalizeDirectoryName(ContentDirNorm);

		if (AbsUmap.StartsWith(ContentDirNorm + TEXT("/"), ESearchCase::IgnoreCase))
		{
			FString Rel = AbsUmap;
			if (FPaths::MakePathRelativeTo(Rel, *ContentDirNorm))
			{
				Rel = FPaths::ChangeExtension(Rel, TEXT("")); // drop .umap
				Rel.ReplaceInline(TEXT("\\"), TEXT("/"));

				static const FString Prefix = TEXT("Content/");
				if (Rel.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					Rel.RightChopInline(Prefix.Len());
				}

				UmapPluginName = P->GetName();
				RelNoExt = Rel;
				break;
			}
		}
	}

	if (UmapPluginName.IsEmpty())
	{
		AddLogMessage(TEXT("Layout: Selected .umap is not inside a project content plugin (<Project>/Plugins/*/Content)."));
		return FReply::Handled();
	}
	AddLogMessage(FString::Printf(TEXT("Layout: .umap plugin = %s, Relative = %s"), *UmapPluginName, *RelNoExt));

	if (SelectedLayoutPluginName.IsEmpty())
	{
		AddLogMessage(TEXT("Layout: Please select a Content Plugin for the map (dropdown)."));
		return FReply::Handled();
	}
	AddLogMessage(FString::Printf(TEXT("Layout: Content Plugin for map = %s"), *SelectedLayoutPluginName));

	const FString MapAssetPathRoot = FString::Printf(TEXT("/%s/Content/"), *SelectedLayoutPluginName);
	const FString MapForLayout = FString::Printf(TEXT("/%s/Content/%s"), *UmapPluginName, *RelNoExt);
	AddLogMessage(FString::Printf(TEXT("Layout: mapAssetPath = %s"), *MapAssetPathRoot));
	AddLogMessage(FString::Printf(TEXT("Layout: mapForLayout = %s"), *MapForLayout));

	// Update JSON
	Root->SetStringField(TEXT("displayName"), Name);
	Root->SetStringField(TEXT("mapAssetPath"), MapAssetPathRoot);
	Root->SetStringField(TEXT("mapForLayout"), MapForLayout);

	// Optional pak/mount
	if (!SelectedPakPath.IsEmpty())
	{
		Root->SetStringField(TEXT("pak"), SelectedPakPath);
		const FString PakBase = FPaths::GetBaseFilename(SelectedPakPath);
		const FString MountPath = FString::Printf(TEXT("/Mods/%s"), *PakBase);
		Root->SetStringField(TEXT("mount"), MountPath);
		AddLogMessage(FString::Printf(TEXT("Layout: pak = %s"), *SelectedPakPath));
		AddLogMessage(FString::Printf(TEXT("Layout: mount = %s"), *MountPath));
	}

	FString OutText;
	auto Writer = TJsonWriterFactory<>::Create(&OutText);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutText, *DestFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
		const int32 ActorCount = (Root->TryGetArrayField(TEXT("actors"), ActorsArr) && ActorsArr) ? ActorsArr->Num() : 0;

		AddLogMessage(FString::Printf(TEXT("Layout saved: %s (%d actors)"), *DestFile, ActorCount));
		RefreshLayoutList();

		// Reselect the file we just wrote
		for (const auto& E : LayoutFiles)
		{
			if (FPaths::IsSamePath(E->PluginPath, DestFile))
			{
				SelectedLayoutEntry = E;
				SelectedLayoutPath = DestFile;
				if (LayoutListWidget.IsValid())
				{
					LayoutListWidget->SetSelection(E);
				}
				AddLogMessage(FString::Printf(TEXT("Layout: Reselected %s"), *DestFile));
				break;
			}
		}

		// FINAL line (matches your requested wording)
		AddLogMessage(FString::Printf(TEXT("Layout Finished!"),
			*FPaths::GetCleanFilename(DestFile)));
	}
	else
	{
		AddLogMessage(TEXT("Layout: failed to write file."));
	}

	return FReply::Handled();
}

void FPakCreatorWindow::PopulateLayoutContentPluginList()
{
	LayoutPluginSource.Empty();
	SelectedLayoutPluginName.Empty();

	const FString ProjectPluginsRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
	FString ProjectPluginsRootNorm = ProjectPluginsRoot; FPaths::NormalizeDirectoryName(ProjectPluginsRootNorm);

	const TArray<TSharedRef<IPlugin>>& Discovered = IPluginManager::Get().GetDiscoveredPlugins();

	for (const TSharedRef<IPlugin>& P : Discovered)
	{
		// Only content plugins that live under <Project>/Plugins (exclude Engine/editor plugins)
		if (!P->CanContainContent() || !P->IsEnabled())
			continue;

		FString BaseDir = FPaths::ConvertRelativePathToFull(P->GetBaseDir());
		FPaths::NormalizeDirectoryName(BaseDir);

		// Must be inside the project's Plugins directory
		if (!BaseDir.StartsWith(ProjectPluginsRootNorm, ESearchCase::IgnoreCase))
			continue;

		// Explicitly exclude this tool plugin if you want
		if (P->GetName().Equals(TEXT("ModCreator"), ESearchCase::IgnoreCase))
			continue;

		LayoutPluginSource.Add(MakeShared<FString>(P->GetName()));
	}

	if (LayoutPluginSource.Num() > 0)
	{
		SelectedLayoutPluginName = *LayoutPluginSource[0];
	}
}

void FPakCreatorWindow::OnLayoutPluginSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
{
	if (SelectedItem.IsValid())
	{
		SelectedLayoutPluginName = *SelectedItem;
		if (LayoutPluginSelectionText.IsValid())
		{
			LayoutPluginSelectionText->SetText(FText::FromString(SelectedLayoutPluginName));
		}
	}
}

FText FPakCreatorWindow::GetCurrentLayoutPluginText() const
{
	return FText::FromString(SelectedLayoutPluginName.IsEmpty() ? TEXT("(select content plugin)") : SelectedLayoutPluginName);
}

#undef LOCTEXT_NAMESPACE
