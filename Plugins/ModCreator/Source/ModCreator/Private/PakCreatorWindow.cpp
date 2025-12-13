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
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"



#define LOCTEXT_NAMESPACE "FPakCreatorWindow"

// ===== ModInfo refresh helpers (local copy) =====
namespace
{
	// Scans a content-only plugin’s assets and fills Root["Assets"] with Maps, Blueprints, Models, Materials.
	// Scans a content-only plugin’s assets and fills Root["Assets"] with Maps, Blueprints, Models, Materials.
	static void PopulateAssetsWithMaps(TSharedPtr<FJsonObject>& Root, const FString& PluginDir, const FString& ModName)
	{
		TArray<TSharedPtr<FJsonValue>> AssetsArray;

		// Single helper that can also store AssetClass (for models / materials)
		auto Push = [&AssetsArray](const FString& Type, const FString& Name, const FString& AssetClass)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("Type"), Type);
				Obj->SetStringField(TEXT("Name"), Name);

				// Only set AssetClass when it is meaningful (models, materials, etc.)
				if (!AssetClass.IsEmpty())
				{
					Obj->SetStringField(TEXT("AssetClass"), AssetClass);
				}

				AssetsArray.Add(MakeShared<FJsonValueObject>(Obj));
			};

		bool bUsedAssetRegistry = false;

		// Content-only plugins mount at "/<ModName>"
		const FName PackageRoot(*FString::Printf(TEXT("/%s"), *ModName));

		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry") || FModuleManager::Get().LoadModule("AssetRegistry") != nullptr)
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			FARFilter Filter;
			Filter.PackagePaths.Add(PackageRoot);
			Filter.bRecursivePaths = true;
			Filter.bIncludeOnlyOnDiskAssets = true;

			TArray<FAssetData> Assets;
			AR.GetAssets(Filter, Assets);

			for (const FAssetData& AD : Assets)
			{
#if ENGINE_MAJOR_VERSION >= 5
				const FString ClassName = AD.AssetClassPath.GetAssetName().ToString();
#else
				const FString ClassName = AD.AssetClass.ToString();
#endif
				const FString AssetName = AD.AssetName.ToString();

				// Maps
				if (ClassName == TEXT("World"))
				{
					Push(TEXT("Map"), AssetName, TEXT("World"));
					continue;
				}

				// Blueprints
				if (ClassName == TEXT("Blueprint"))
				{
					Push(TEXT("Blueprint"), AssetName, TEXT("Blueprint"));
					continue;
				}

				// Models (Static / Skeletal)  ✅ important for Attachment vs Clothing filter
				if (ClassName == TEXT("StaticMesh") || ClassName == TEXT("SkeletalMesh"))
				{
					Push(TEXT("Model"), AssetName, ClassName);
					continue;
				}

				// Materials / instances
				if (ClassName == TEXT("Material") ||
					ClassName == TEXT("MaterialInstance") ||
					ClassName == TEXT("MaterialInstanceConstant"))
				{
					Push(TEXT("Material"), AssetName, ClassName);
					continue;
				}
			}

			bUsedAssetRegistry = true;
		}

		// Fallback: at least list maps via file scan if AR wasn’t available
		if (!bUsedAssetRegistry)
		{
			const FString ContentDir = PluginDir / TEXT("Content");
			if (FPaths::DirectoryExists(ContentDir))
			{
				TArray<FString> MapFiles;
				IFileManager::Get().FindFilesRecursive(MapFiles, *ContentDir, TEXT("*.umap"), true, false);
				for (const FString& AbsPath : MapFiles)
				{
					FString Rel = AbsPath; FPaths::NormalizeFilename(Rel);
					FString ContentNorm = ContentDir; FPaths::NormalizeDirectoryName(ContentNorm);
					if (Rel.StartsWith(ContentNorm + TEXT("/")))
					{
						const FString AssetName = FPaths::GetBaseFilename(Rel.Mid(ContentNorm.Len() + 1));
						// No asset class info in fallback; leave AssetClass empty
						Push(TEXT("Map"), AssetName, TEXT(""));
					}
				}
			}
		}

		Root->SetArrayField(TEXT("Assets"), AssetsArray);
	}

	// Loads or creates modinfo.json, updates fields, repopulates Assets, and writes back to disk.
	static bool UpdateModInfoJson(const FString& ModInfoPath, const FString& ModName, const FString& Description, const FString& PluginDir)
	{
		FString In;
		TSharedPtr<FJsonObject> Root;

		if (FPaths::FileExists(ModInfoPath) && FFileHelper::LoadFileToString(In, *ModInfoPath))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				Root = MakeShared<FJsonObject>();
			}
		}
		else
		{
			Root = MakeShared<FJsonObject>();
		}

		// Basic fields
		Root->SetStringField(TEXT("ModName"), ModName);
		if (!Description.IsEmpty())
		{
			Root->SetStringField(TEXT("Description"), Description);
		}

		const FString ProjectName = FApp::GetProjectName();
		if (!ProjectName.IsEmpty())
		{
			Root->SetStringField(TEXT("ProjectName"), ProjectName);
		}

		// Remove legacy fields and repopulate assets
		Root->RemoveField(TEXT("MainMap"));
		Root->RemoveField(TEXT("Assets"));      
		PopulateAssetsWithMaps(Root, PluginDir, ModName);

		// ✅ NEVER copy BuildRequirements to the final modinfo
		Root->RemoveField(TEXT("BuildRequirements"));

		// Decide how to handle layout fields based on ModType
		FString ModTypeValue;
		Root->TryGetStringField(TEXT("ModType"), ModTypeValue);
		ModTypeValue = ModTypeValue.TrimStartAndEnd();

		const bool bIsAttachmentOrClothing =
			ModTypeValue.Equals(TEXT("Attachment"), ESearchCase::IgnoreCase) ||
			ModTypeValue.Equals(TEXT("Clothing"), ESearchCase::IgnoreCase);

		if (bIsAttachmentOrClothing)
		{
			// For Attachment / Clothing mods: strip layout stuff
			Root->RemoveField(TEXT("LayoutsEnabled"));
			Root->RemoveField(TEXT("Layouts"));
		}

		// Ensure optional fields exist
		// Thumbnail is always okay
		if (!Root->HasField(TEXT("Thumbnail")))
		{
			Root->SetStringField(TEXT("Thumbnail"), TEXT("Thumbnail.png"));
		}

		// Only non-attachment/clothing mods get LayoutsEnabled
		if (!bIsAttachmentOrClothing)
		{
			if (!Root->HasField(TEXT("LayoutsEnabled")))
			{
				Root->SetBoolField(TEXT("LayoutsEnabled"), false);
			}

			// Remove Layouts always — we no longer use it
			Root->RemoveField(TEXT("Layouts"));
		}
		else
		{
			// For attachment/clothing, remove BOTH to keep JSON clean
			Root->RemoveField(TEXT("LayoutsEnabled"));
			Root->RemoveField(TEXT("Layouts"));
		}

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Out, *ModInfoPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

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

								// =========================================================
								// === TAB 0: PACKAGE MOD
								// =========================================================
								+SWidgetSwitcher::Slot()
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
														.OnSelectionChanged(this, &FPakCreatorWindow::OnPluginSelectionChanged)
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
													TargetSource.Num() > 1
														? SNew(STextBlock).Text(TargetText)
														: SNullWidget::NullWidget
												]

												+ SVerticalBox::Slot()
												.VAlign(VAlign_Fill)
												.HAlign(HAlign_Fill)
												.Padding(TargetSource.Num() > 1 ? 10.0f : 0.0f)
												.AutoHeight()
												[
													TargetSource.Num() > 1
														? SAssignNew(TargetComboBox, SComboBox<TSharedPtr<FString>>)
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

											// === Thumbnail (click to change) ===
											+ SVerticalBox::Slot()
												.AutoHeight()
												.Padding(0, 8, 0, 0)
												[
													SNew(SBox)
														.Visibility(this, &FPakCreatorWindow::GetThumbnailVisibility)
														[
															SNew(SVerticalBox)

																+ SVerticalBox::Slot()
																.AutoHeight()
																.Padding(10.f, 0.f, 10.f, 6.f)
																[
																	SNew(STextBlock)
																		.Text(FText::FromString(TEXT("Thumbnail (click to change)")))
																]

																+ SVerticalBox::Slot()
																.AutoHeight()
																.Padding(10.0f)
																.HAlign(HAlign_Left) // prevents the row from stretching wide
																[
																	SNew(SButton)
																		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
																		.ContentPadding(0)
																		.OnClicked(this, &FPakCreatorWindow::HandleThumbnailBrowseClicked)
																		[
																			SNew(SBox)
																				.WidthOverride(180.f)
																				.HeightOverride(180.f)
																				[
																					SNew(SBorder)
																						.Padding(2.f)
																						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
																						[
																							SNew(SScaleBox)
																								.Stretch(EStretch::ScaleToFit)
																								[
																									SNew(SImage)
																										.Image(this, &FPakCreatorWindow::GetThumbnailPreviewBrush)
																								]
																						]
																				]
																		]
																]
														]
												]

											// Create .pak button
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

											// Log list (IMPORTANT: this must exist)
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
										] // end RIGHT COLUMN
								] // end TAB0 horizontal
								// =========================================================

								// =========================================================
								// === TAB 1: LAYOUTS (list + create/overwrite)
								// =========================================================
							+SWidgetSwitcher::Slot()
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

											// --- Layout Log (same spot as Package Mod) ---
											+ SVerticalBox::Slot()
												.FillHeight(1.f)
												.HAlign(HAlign_Fill)
												[
													SAssignNew(LayoutLogListWidget, SListView<TSharedPtr<FStringEntry>>)
														.ListItemsSource(&LogEntries)
														.SelectionMode(ESelectionMode::Type::None)
														.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForLog)
														.ScrollbarVisibility(EVisibility::Hidden)
												]
										]
								]
							// =========================================================
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
	RefreshThumbnailPreviewBrush();

	if (LogListWidget.IsValid())
	{
		LogListWidget->RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &FPakCreatorWindow::RefreshLog));
	}

	return PluginTab;
}

void FPakCreatorWindow::BuildModelTypeOptions()
{
	if (AttachmentTypeOptions.Num() == 0)
	{
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Mag")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Scope")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_BarrelEnd")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Barrel")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Upper")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Lower")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_ChargingHandle")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Grip")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_OuterBarrel")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_BufferTube")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Stock")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Trigger")));
		AttachmentTypeOptions.Add(MakeShared<FString>(TEXT("Rifle_Tank")));
	}

	if (ClothingTypeOptions.Num() == 0)
	{
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Hat")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Mask")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Shirt")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Pants")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Gloves")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("KneePads")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("ElbowPads")));
		ClothingTypeOptions.Add(MakeShared<FString>(TEXT("Vest")));
	}
}

bool FPakCreatorWindow::GatherAttachmentClothingModelsForSelection(
	const TArray<TSharedPtr<FStringEntry>>& SelectedPlugins
)
{
	ModelTypeRows.Empty();

	const FString ProjectPluginsDir = FPaths::ProjectPluginsDir();

	for (const TSharedPtr<FStringEntry>& Item : SelectedPlugins)
	{
		const FString& PluginName = Item->PluginPath;
		const FString PluginDir = FPaths::Combine(ProjectPluginsDir, PluginName);

		AddLogMessage(FString::Printf(TEXT("Checking plugin \"%s\" for Attachment/Clothing models..."), *PluginName));

		// Look for <Plugin>/modinfo.json
		FString ModInfoPath = FPaths::Combine(PluginDir, TEXT("modinfo.json"));
		if (!FPaths::FileExists(ModInfoPath))
		{
			const FString AltPath = FPaths::Combine(PluginDir, TEXT("Config/modinfo.json"));
			if (FPaths::FileExists(AltPath))
			{
				ModInfoPath = AltPath;
			}
		}

		if (!FPaths::FileExists(ModInfoPath))
		{
			AddLogMessage(FString::Printf(TEXT("  -> No modinfo.json found for plugin \"%s\""), *PluginName));
			continue;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ModInfoPath))
		{
			AddLogMessage(FString::Printf(TEXT("  -> Failed to read modinfo.json for \"%s\""), *PluginName));
			continue;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			AddLogMessage(FString::Printf(TEXT("  -> Failed to parse modinfo.json for \"%s\""), *PluginName));
			continue;
		}

		// Use ModType (Attachment / Clothing)
		FString ModCategory;
		if (!Root->TryGetStringField(TEXT("ModType"), ModCategory))
		{
			AddLogMessage(FString::Printf(TEXT("  -> ModType not set in modinfo.json for \"%s\""), *PluginName));
			continue;
		}

		ModCategory = ModCategory.TrimStartAndEnd();
		if (!ModCategory.Equals(TEXT("Attachment"), ESearchCase::IgnoreCase) &&
			!ModCategory.Equals(TEXT("Clothing"), ESearchCase::IgnoreCase))
		{
			AddLogMessage(FString::Printf(TEXT("  -> ModType=\"%s\" (not Attachment/Clothing), skipping \"%s\""),
				*ModCategory, *PluginName));
			continue;
		}

		AddLogMessage(FString::Printf(TEXT("  -> ModType=\"%s\""), *ModCategory));

		// 🔄 ALWAYS refresh Assets[] from the plugin's content so renamed assets are picked up
		AddLogMessage(TEXT("  -> Refreshing Assets[] from plugin content for type selection..."));
		Root->RemoveField(TEXT("Assets"));
		PopulateAssetsWithMaps(Root, PluginDir, PluginName);

		const TArray<TSharedPtr<FJsonValue>>* AssetsArray = nullptr;
		if (!Root->TryGetArrayField(TEXT("Assets"), AssetsArray) || !AssetsArray || AssetsArray->Num() == 0)
		{
			AddLogMessage(FString::Printf(TEXT("  -> No Assets/Models found for \"%s\" after refresh."), *PluginName));
			continue;
		}

		int32 ModelsAddedForPlugin = 0;

		for (const TSharedPtr<FJsonValue>& V : *AssetsArray)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			const FString Type = Obj->GetStringField(TEXT("Type"));
			if (!Type.Equals(TEXT("Model"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			const FString ModelName = Obj->GetStringField(TEXT("Name"));
			const FString AssetClass = Obj->GetStringField(TEXT("AssetClass")); // NEW

			// Clothing mods -> require SkeletalMesh
			if (ModCategory.Equals(TEXT("Clothing"), ESearchCase::IgnoreCase))
			{
				if (!AssetClass.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase))
					continue;
			}

			if (ModCategory.Equals(TEXT("Attachment"), ESearchCase::IgnoreCase))
			{
				if (!AssetClass.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase))
					continue;
			}

			TSharedPtr<FModelTypeRow> Row = MakeShared<FModelTypeRow>();
			Row->PluginName = PluginName;
			Row->ModelName = ModelName;
			Row->ModCategory = ModCategory;
			Row->SelectedType = TEXT("");   // set by the UI

			ModelTypeRows.Add(Row);
			ModelsAddedForPlugin++;
		}

		AddLogMessage(FString::Printf(TEXT("  -> Plugin \"%s\": %d model(s) found for type selection."),
			*PluginName, ModelsAddedForPlugin));
	}

	if (ModelTypeRows.Num() == 0)
	{
		AddLogMessage(TEXT("No Attachment/Clothing models found; skipping type selection dialog."));
		return false;
	}

	return true;
}

TSharedRef<SWidget> FPakCreatorWindow::GenerateModelTypeComboWidget(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text_Lambda([Item]()
			{
				return Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty();
			});
}

void FPakCreatorWindow::OnModelTypeSelected(
	TSharedPtr<FString> Selected,
	ESelectInfo::Type /*SelectInfo*/,
	TSharedPtr<FModelTypeRow> Row
)
{
	if (Row.IsValid() && Selected.IsValid())
	{
		Row->SelectedType = *Selected;
	}
}

TSharedRef<ITableRow> FPakCreatorWindow::OnGenerateRowForModelType(
	TSharedPtr<FModelTypeRow> Item,
	const TSharedRef<STableViewBase>& OwnerTable
)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FModelTypeRow>>, OwnerTable)
			[
				SNew(SBox)
			];
	}

	// Select options based on category
	const bool bIsAttachment = Item->ModCategory.Equals(TEXT("Attachment"), ESearchCase::IgnoreCase);
	TArray<TSharedPtr<FString>>& Options = bIsAttachment ? AttachmentTypeOptions : ClothingTypeOptions;

	return SNew(STableRow<TSharedPtr<FModelTypeRow>>, OwnerTable)
		[
			SNew(SVerticalBox)

				// --- First row: model name + type combo ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(0.5f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(Item->ModelName))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(0.5f)
						.VAlign(VAlign_Center)
						[
							SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&Options)
								.OnGenerateWidget(this, &FPakCreatorWindow::GenerateModelTypeComboWidget)
								.OnSelectionChanged_Lambda([this, Item](TSharedPtr<FString> Selected, ESelectInfo::Type Info)
									{
										OnModelTypeSelected(Selected, Info, Item);
									})
								[
									SNew(STextBlock)
										.Text_Lambda([Item]()
											{
												return Item->SelectedType.IsEmpty()
													? FText::FromString(TEXT("(Select Type)"))
													: FText::FromString(Item->SelectedType);
											})
								]
						]
				]

			// --- Second row: L-connector + special checkboxes ---
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(24.0f, 4.0f, 0.0f, 0.0f) // small indent under combo
				[
					SNew(SBox)
						.Visibility_Lambda([Item]()
							{
								const bool bIsBufferTube = Item->SelectedType.Equals(TEXT("Rifle_BufferTube"), ESearchCase::IgnoreCase);
								const bool bIsGrip = Item->SelectedType.Equals(TEXT("Rifle_Grip"), ESearchCase::IgnoreCase);
								return (bIsBufferTube || bIsGrip) ? EVisibility::Visible : EVisibility::Collapsed;
							})
						[
							SNew(SHorizontalBox)

								// L-shaped connector on the left
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SBox)
										.WidthOverride(12.f)
										.HeightOverride(12.f)
										[
											SNew(SOverlay)

												// vertical part of L
												+ SOverlay::Slot()
												.HAlign(HAlign_Left)
												.VAlign(VAlign_Fill)
												[
													SNew(SBox)
														.WidthOverride(2.f)
														[
															SNew(SBorder)
																.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
																.BorderBackgroundColor(FLinearColor::Gray)
														]
												]

											// horizontal part of L
											+ SOverlay::Slot()
												.HAlign(HAlign_Fill)
												.VAlign(VAlign_Bottom)
												[
													SNew(SBox)
														.HeightOverride(2.f)
														[
															SNew(SBorder)
																.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
																.BorderBackgroundColor(FLinearColor::Gray)
														]
												]
										]
								]

							// Checkboxes area to the right of the L
							+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(4.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)

										// Exclude Stock (only when type == Rifle_BufferTube)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SCheckBox)
												.Visibility_Lambda([Item]()
													{
														return Item->SelectedType.Equals(TEXT("Rifle_BufferTube"), ESearchCase::IgnoreCase)
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
												.IsChecked_Lambda([Item]()
													{
														return Item->bExcludeStock ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
												.OnCheckStateChanged_Lambda([Item](ECheckBoxState NewState)
													{
														Item->bExcludeStock = (NewState == ECheckBoxState::Checked);
													})
												[
													SNew(STextBlock)
														.Text(FText::FromString(TEXT("Exclude Stock")))
												]
										]

									// Include Tank (only when type == Rifle_Grip)
									+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(16.0f, 0.0f, 0.0f, 0.0f)
										[
											SNew(SCheckBox)
												.Visibility_Lambda([Item]()
													{
														return Item->SelectedType.Equals(TEXT("Rifle_Grip"), ESearchCase::IgnoreCase)
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
												.IsChecked_Lambda([Item]()
													{
														return Item->bIncludeTank ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
												.OnCheckStateChanged_Lambda([Item](ECheckBoxState NewState)
													{
														Item->bIncludeTank = (NewState == ECheckBoxState::Checked);
													})
												[
													SNew(STextBlock)
														.Text(FText::FromString(TEXT("Include Tank")))
												]
										]
								]
						]
				]
		];
}

void FPakCreatorWindow::ShowModelTypeDialog()
{
	if (ModelTypeDialogWindow.IsValid())
	{
		return;
	}

	BuildModelTypeOptions();

	const TSharedRef<SVerticalBox> DialogContent =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT(
					"Select the Attachment/Clothing type for each model.\n\n"
					"If you cancel or close this window without applying, the build will be aborted."
				)))
				.AutoWrapText(true)
		]
	+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(8.0f)
		[
			SAssignNew(ModelTypeListView, SListView<TSharedPtr<FModelTypeRow>>)
				.ListItemsSource(&ModelTypeRows)
				.SelectionMode(ESelectionMode::Type::None)
				.OnGenerateRow(this, &FPakCreatorWindow::OnGenerateRowForModelType)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.f)
				[
					SNew(SButton)
						.OnClicked(this, &FPakCreatorWindow::OnModelTypeApplyClicked)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Apply")))
						]
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.f)
				[
					SNew(SButton)
						.OnClicked(this, &FPakCreatorWindow::OnModelTypeCancelClicked)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Cancel")))
						]
				]
		];

	ModelTypeDialogWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Select Attachment / Clothing Types")))
		.ClientSize(FVector2D(600.f, 400.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			DialogContent
		];

	// If user closes via X, treat as cancel
	ModelTypeDialogWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([this](const TSharedRef<SWindow>&)
		{
			// Called when user clicks the X or the OS closes the window.
			if (bWaitingForModelTypes && !bModelTypesApplied)
			{
				AddLogMessage(TEXT("Model type selection canceled. Build aborted."));
			}

			bWaitingForModelTypes = false;
			ModelTypeDialogWindow.Reset();
		}));

	FSlateApplication::Get().AddWindow(ModelTypeDialogWindow.ToSharedRef());

	bWaitingForModelTypes = true;
	bModelTypesApplied = false;

	AddLogMessage(TEXT("Attachment/Clothing mod detected. Waiting for user to finish selecting types..."));
}

FReply FPakCreatorWindow::OnModelTypeApplyClicked()
{
	// 1) Ensure all rows have a type selected
	for (const TSharedPtr<FModelTypeRow>& Row : ModelTypeRows)
	{
		if (Row.IsValid() && Row->SelectedType.IsEmpty())
		{
			AddLogMessage(FString::Printf(TEXT("Error: Model \"%s\" has no type selected."), *Row->ModelName));
			return FReply::Handled();
		}
	}

	// 2) Group rows by plugin name
	TMap<FString, TArray<TSharedPtr<FModelTypeRow>>> PluginToRows;
	for (const TSharedPtr<FModelTypeRow>& Row : ModelTypeRows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		TArray<TSharedPtr<FModelTypeRow>>& Arr = PluginToRows.FindOrAdd(Row->PluginName);
		Arr.Add(Row);

		AddLogMessage(FString::Printf(TEXT("Model \"%s\" in plugin \"%s\" set to type \"%s\" (%s)"),
			*Row->ModelName,
			*Row->PluginName,
			*Row->SelectedType,
			*Row->ModCategory));
	}

	const FString ProjectPluginsDir = FPaths::ProjectPluginsDir();

	// 3) For each plugin, load modinfo.json and update either Attachments[] OR Clothes[]
	for (const TPair<FString, TArray<TSharedPtr<FModelTypeRow>>>& Pair : PluginToRows)
	{
		const FString& PluginName = Pair.Key;
		const TArray<TSharedPtr<FModelTypeRow>>& Rows = Pair.Value;

		const FString PluginDir = FPaths::Combine(ProjectPluginsDir, PluginName);
		FString       ModInfoPath = FPaths::Combine(PluginDir, TEXT("modinfo.json"));

		if (!FPaths::FileExists(ModInfoPath))
		{
			const FString AltPath = FPaths::Combine(PluginDir, TEXT("Config/modinfo.json"));
			if (FPaths::FileExists(AltPath))
			{
				ModInfoPath = AltPath;
			}
		}

		if (!FPaths::FileExists(ModInfoPath))
		{
			AddLogMessage(FString::Printf(TEXT("Warning: modinfo.json not found for \"%s\" when saving types."), *PluginName));
			continue;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ModInfoPath))
		{
			AddLogMessage(FString::Printf(TEXT("Warning: Failed to read modinfo.json for \"%s\" when saving types."), *PluginName));
			continue;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			AddLogMessage(FString::Printf(TEXT("Warning: Failed to parse modinfo.json for \"%s\" when saving types."), *PluginName));
			continue;
		}

		// Look at root ModType
		FString RootModType;
		Root->TryGetStringField(TEXT("ModType"), RootModType);
		const bool bIsClothingMod = RootModType.Equals(TEXT("Clothing"), ESearchCase::IgnoreCase);

		if (bIsClothingMod)
		{
			// ---------------- CLOTHING MOD ----------------
			// Build Clothes[] from rows whose ModCategory is Clothing
			TArray<TSharedPtr<FJsonValue>> ClothesArray;

			for (const TSharedPtr<FModelTypeRow>& Row : Rows)
			{
				if (!Row.IsValid())
				{
					continue;
				}

				if (!Row->ModCategory.Equals(TEXT("Clothing"), ESearchCase::IgnoreCase))
				{
					continue; // ignore non-clothing rows for a clothing mod
				}

				TSharedPtr<FJsonObject> ClothesObj = MakeShared<FJsonObject>();
				ClothesObj->SetStringField(TEXT("Name"), Row->ModelName);
				ClothesObj->SetStringField(TEXT("Type"), Row->SelectedType);
				// NOTE: no "ModType" here – it stays only at the root

				ClothesArray.Add(MakeShared<FJsonValueObject>(ClothesObj));
			}

			Root->SetArrayField(TEXT("Clothes"), ClothesArray);
			Root->RemoveField(TEXT("Attachments")); // remove attachments for clothing mods
		}
		else
		{
			// ---------------- ATTACHMENT / OTHER MOD ----------------
			// Build Attachments[] from rows (and handle Include/Exclude rules)
			TArray<TSharedPtr<FJsonValue>> AttachmentsArray;

			// Start with any existing Include/Exclude arrays already in the template
			TSet<FString> IncludeSet;
			TSet<FString> ExcludeSet;

			const TArray<TSharedPtr<FJsonValue>>* ExistingInclude = nullptr;
			if (Root->TryGetArrayField(TEXT("IncludeAttachments"), ExistingInclude) && ExistingInclude)
			{
				for (const TSharedPtr<FJsonValue>& V : *ExistingInclude)
				{
					const FString Val = V->AsString();
					if (!Val.IsEmpty())
					{
						IncludeSet.Add(Val);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ExistingExclude = nullptr;
			if (Root->TryGetArrayField(TEXT("ExcludeAttachments"), ExistingExclude) && ExistingExclude)
			{
				for (const TSharedPtr<FJsonValue>& V : *ExistingExclude)
				{
					const FString Val = V->AsString();
					if (!Val.IsEmpty())
					{
						ExcludeSet.Add(Val);
					}
				}
			}

			for (const TSharedPtr<FModelTypeRow>& Row : Rows)
			{
				if (!Row.IsValid())
				{
					continue;
				}

				TSharedPtr<FJsonObject> AttachObj = MakeShared<FJsonObject>();
				AttachObj->SetStringField(TEXT("Name"), Row->ModelName);
				AttachObj->SetStringField(TEXT("Type"), Row->SelectedType);
				// NOTE: no "ModType" here – it stays only at the root

				AttachmentsArray.Add(MakeShared<FJsonValueObject>(AttachObj));

				// Special rules for certain attachment types:
				//  - Rifle_BufferTube + "Exclude Stock" => ExcludeAttachments += "Rifle_Stock"
				//  - Rifle_Grip       + "Include Tank"  => IncludeAttachments += "Rifle_Tank"
				if (Row->ModCategory.Equals(TEXT("Attachment"), ESearchCase::IgnoreCase))
				{
					if (Row->SelectedType.Equals(TEXT("Rifle_BufferTube"), ESearchCase::IgnoreCase)
						&& Row->bExcludeStock)
					{
						ExcludeSet.Add(TEXT("Rifle_Stock"));
					}

					if (Row->SelectedType.Equals(TEXT("Rifle_Grip"), ESearchCase::IgnoreCase)
						&& Row->bIncludeTank)
					{
						IncludeSet.Add(TEXT("Rifle_Tank"));
					}
				}
			}

			Root->SetArrayField(TEXT("Attachments"), AttachmentsArray);
			Root->RemoveField(TEXT("Clothes")); // remove clothes for attachment/other mods

			// Write IncludeAttachments / ExcludeAttachments back (only if they have entries)
			if (IncludeSet.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> NewIncludeArray;
				for (const FString& S : IncludeSet)
				{
					NewIncludeArray.Add(MakeShared<FJsonValueString>(S));
				}
				Root->SetArrayField(TEXT("IncludeAttachments"), NewIncludeArray);
			}
			else
			{
				Root->RemoveField(TEXT("IncludeAttachments"));
			}

			if (ExcludeSet.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> NewExcludeArray;
				for (const FString& S : ExcludeSet)
				{
					NewExcludeArray.Add(MakeShared<FJsonValueString>(S));
				}
				Root->SetArrayField(TEXT("ExcludeAttachments"), NewExcludeArray);
			}
			else
			{
				Root->RemoveField(TEXT("ExcludeAttachments"));
			}
		}

		// Save JSON
		FString OutJson;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) ||
			!FFileHelper::SaveStringToFile(OutJson, *ModInfoPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			AddLogMessage(FString::Printf(TEXT("Warning: Failed to write updated Attachments/Clothes to modinfo.json for \"%s\""), *PluginName));
		}
		else
		{
			AddLogMessage(FString::Printf(TEXT("Updated Attachments/Clothes in modinfo.json for \"%s\""), *PluginName));
		}
	}

	// 4) Close dialog & proceed to packaging
	bModelTypesApplied = true;
	bWaitingForModelTypes = false;

	if (ModelTypeDialogWindow.IsValid())
	{
		FSlateApplication::Get().RequestDestroyWindow(ModelTypeDialogWindow.ToSharedRef());
		ModelTypeDialogWindow.Reset();
	}

	AddLogMessage(TEXT("Model types applied. Starting packaging..."));
	StartPackagingAfterModelTypes();
	return FReply::Handled();
}

FReply FPakCreatorWindow::OnModelTypeCancelClicked()
{
	bModelTypesApplied = false;
	bWaitingForModelTypes = false;

	if (ModelTypeDialogWindow.IsValid())
	{
		FSlateApplication::Get().RequestDestroyWindow(ModelTypeDialogWindow.ToSharedRef());
		ModelTypeDialogWindow.Reset();
	}

	AddLogMessage(TEXT("Model type selection canceled. Build aborted."));
	return FReply::Handled();
}

void FPakCreatorWindow::StartPackagingAfterModelTypes()
{
	// Rebuild the UAT command queue using cached selections, same as your
	// original CreateButtonPressed logic AFTER the validation.

	PendingUATCommands.Empty();

	const FString ConfigurationName = TEXT("Shipping");
	FString PlatformName;
	FString Cookflavor;

	FPakHelperFunctions::GetPlatformNameAndFlavorBySelection(
		CachedPlatformSelection,
		PlatformName,
		Cookflavor
	);

	AddLogMessage(FString::Printf(TEXT("Platform: %s%s"), *PlatformName, *Cookflavor));

	const FString BaseGameCommand =
		FPakHelperFunctions::MakeUATCommand(
			OutputProject,
			PlatformName,
			Cookflavor,
			ConfigurationName,
			CachedTargetName,
			FPaths::Combine(GetTemporaryStagingDirectory(), FAutomatedPakParams::ReleaseVersionName))
		+ FPakHelperFunctions::MakeUATParams_BaseGame(OutputProject, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
	PendingUATCommands.Enqueue({ FAutomatedPakParams::ReleaseVersionName, BaseGameCommand });
#else
	PendingUATCommands.Enqueue(TPair<FString, FString>(FAutomatedPakParams::ReleaseVersionName, BaseGameCommand));
#endif

	int32 NumPluginBuilds = 0;

	for (const TSharedPtr<FStringEntry>& Item : CachedSelectedPlugins)
	{
		const FString DLCCommand =
			FPakHelperFunctions::MakeUATCommand(
				OutputProject,
				PlatformName,
				Cookflavor,
				ConfigurationName,
				CachedTargetName,
				FPaths::Combine(GetTemporaryStagingDirectory(), Item->PluginPath))
			+ FPakHelperFunctions::MakeUATParams_DLC(Item->PluginPath, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
		PendingUATCommands.Enqueue({ Item->PluginPath, DLCCommand });
#else
		PendingUATCommands.Enqueue(TPair<FString, FString>(Item->PluginPath, DLCCommand));
#endif

		NumPluginBuilds++;
	}

	AddLogMessage(FString::Printf(TEXT("Building %i Mod%s"), NumPluginBuilds, NumPluginBuilds > 1 ? TEXT("s") : TEXT("")));
	AddLogMessage(TEXT("Notice: Building can take longer on the first run due to shaders compiling. Please be patient."));

	RunBuild();
}

void FPakCreatorWindow::PopulatePluginList(const FString& ProjectPluginDirectory)
{
	AllPlugins.Empty();
	Plugins.Empty();

	// Populate plugins
	const TArray<FString> PluginDirectories = FPakHelperFunctions::GetPluginFolders(FPaths::GetPath(OutputProject) / TEXT("Plugins"));
	for (const FString& Directory : PluginDirectories)
	{
		// Block these from being p akable
		if (Directory.Equals(TEXT("PakCreator"), ESearchCase::IgnoreCase) ||
			Directory.Equals(TEXT("ModCreator"), ESearchCase::IgnoreCase) ||   // already effectively blocked, keep explicit
			Directory.Equals(TEXT("extracontent"), ESearchCase::IgnoreCase) ||   // <-- add your extra content plugin name here
			Directory.Equals(TEXT("ExtraContent"), ESearchCase::IgnoreCase))     // (optional alt-case)
		{
			continue;
		}

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

	// === NEW: attachment / clothing pre-step ===
	if (GatherAttachmentClothingModelsForSelection(SelectedItems))
	{
		// We have at least one attachment/clothing mod that needs per-model type selection.
		// Cache values so we can start packaging AFTER the user hits Apply in the dialog.
		CachedSelectedPlugins = SelectedItems;
		CachedPlatformSelection = *SelectedPlatform;
		CachedCookFlavor = TEXT("");               // recalculated inside StartPackagingAfterModelTypes
		CachedTargetName = TargetName.IsValid() ? *TargetName : TEXT("");

		ShowModelTypeDialog();

		// Packaging will start later from StartPackagingAfterModelTypes()
		return FReply::Handled();
	}
	// === END NEW BLOCK ===

	// === ORIGINAL PACKAGING FLOW (unchanged) ===
	PendingUATCommands.Empty();

	const FString ConfigurationName = TEXT("Shipping");
	FString PlatformName = FAutomatedPakParams::ValidPlatformNames[0];
	FString Cookflavor = TEXT("");

	FPakHelperFunctions::GetPlatformNameAndFlavorBySelection(*SelectedPlatform, PlatformName, Cookflavor);

	AddLogMessage(FString::Printf(TEXT("Platform: %s%s"), *PlatformName, *Cookflavor));

	const FString BaseGameCommand =
		FPakHelperFunctions::MakeUATCommand(
			OutputProject,
			PlatformName,
			Cookflavor,
			ConfigurationName,
			TargetName ? *TargetName : TEXT(""),
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
				TargetName ? *TargetName : TEXT(""),
				FPaths::Combine(GetTemporaryStagingDirectory(), Item->PluginPath))
			+ FPakHelperFunctions::MakeUATParams_DLC(Item->PluginPath, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
		PendingUATCommands.Enqueue({ Item->PluginPath, DLCCommand });
#else
		PendingUATCommands.Enqueue(TPair<FString, FString>(Item->PluginPath, DLCCommand));
#endif

		NumPluginBuilds++;
	}

	AddLogMessage(FString::Printf(TEXT("Building %i Mod%s"), NumPluginBuilds, NumPluginBuilds > 1 ? TEXT("s") : TEXT("")));
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

		// === Copy modinfo.json (and thumbnail) into subfolder named after mod ===
		{
			const FString PluginFolder = FPaths::Combine(FPaths::ProjectPluginsDir(), CurrentTaskName);

			// Prefer <Plugin>/modinfo.json, else <Plugin>/Config/modinfo.json, else create one in <Plugin>/modinfo.json
			FString ModInfoPath = FPaths::Combine(PluginFolder, TEXT("modinfo.json"));
			if (!FPaths::FileExists(ModInfoPath))
			{
				const FString AltPath = FPaths::Combine(PluginFolder, TEXT("Config/modinfo.json"));
				if (FPaths::FileExists(AltPath))
				{
					ModInfoPath = AltPath;
				}
				else
				{
					// Create a minimal modinfo.json so we can refresh/populate assets
					ModInfoPath = FPaths::Combine(PluginFolder, TEXT("modinfo.json"));

					TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
					Root->SetStringField(TEXT("ModName"), CurrentTaskName);
					Root->SetStringField(TEXT("Description"), TEXT(""));
					Root->SetStringField(TEXT("ProjectName"), FApp::GetProjectName());
					Root->SetBoolField(TEXT("LayoutsEnabled"), false);
					Root->SetArrayField(TEXT("Layouts"), {});
					Root->SetStringField(TEXT("Thumbnail"), TEXT("Thumbnail.png"));   // default thumbnail name
					Root->SetStringField(TEXT("BuildRequirements"), TEXT(""));

					// Seed Assets from the plugin's Content folder
					// (PopulateAssetsWithMaps adds Map, Blueprint, Model[Static/Skeletal], Material/Instances)
					PopulateAssetsWithMaps(Root, PluginFolder, CurrentTaskName);

					FString OutJson;
					const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
					FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
					FFileHelper::SaveStringToFile(OutJson, *ModInfoPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				}
			}

			// Refresh/update fields AND repopulate the Assets list before exporting
			if (UpdateModInfoJson(ModInfoPath, CurrentTaskName, TEXT(""), PluginFolder))
			{
				AddLogMessage(FString::Printf(TEXT("Updated modinfo.json (assets) for \"%s\""), *CurrentTaskName));
			}
			else
			{
				AddLogMessage(FString::Printf(TEXT("Warning: Failed to update modinfo.json for \"%s\""), *CurrentTaskName));
			}

			// Ensure output subfolder exists: <OutputPath>/<ModName>/
			const FString SubFolderPath = FPaths::Combine(OutputPath, CurrentTaskName);
			IFileManager::Get().MakeDirectory(*SubFolderPath, /*Tree=*/true);

			// --- Copy the refreshed modinfo.json into the final mod folder ---
			const FString Destination = FPaths::Combine(SubFolderPath, TEXT("modinfo.json"));
			if (PlatformFile.CopyFile(*Destination, *ModInfoPath))
			{
				AddLogMessage(TEXT("Wrote refreshed modinfo.json to output folder"));
			}
			else
			{
				AddLogMessage(TEXT("Error: Failed to copy refreshed modinfo.json to output folder"));
			}

			// --- NEW: Copy the thumbnail into the final mod folder next to modinfo.json ---

			// 1) Get thumbnail name from modinfo.json (fallback to "Thumbnail.png")
			FString ThumbnailName = TEXT("Thumbnail.png");
			{
				FString JsonText;
				if (FFileHelper::LoadFileToString(JsonText, *ModInfoPath))
				{
					TSharedPtr<FJsonObject> Root;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
					if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
					{
						FString FromJson;
						if (Root->TryGetStringField(TEXT("Thumbnail"), FromJson))
						{
							FromJson = FromJson.TrimStartAndEnd();
							if (!FromJson.IsEmpty())
							{
								ThumbnailName = FromJson;
							}
						}
					}
				}
			}

			// 2) Look for the thumbnail file inside the plugin (root first, then Config/)
			FString SourceThumbnail = FPaths::Combine(PluginFolder, ThumbnailName);
			if (!FPaths::FileExists(SourceThumbnail))
			{
				const FString AltThumb = FPaths::Combine(PluginFolder, TEXT("Config"), ThumbnailName);
				if (FPaths::FileExists(AltThumb))
				{
					SourceThumbnail = AltThumb;
				}
			}

			// 3) Copy it into the mod output folder if found
			if (FPaths::FileExists(SourceThumbnail))
			{
				const FString DestThumbnail = FPaths::Combine(SubFolderPath, ThumbnailName);
				if (PlatformFile.CopyFile(*DestThumbnail, *SourceThumbnail))
				{
					AddLogMessage(FString::Printf(TEXT("Copied thumbnail \"%s\" to mod output folder"), *ThumbnailName));
				}
				else
				{
					AddLogMessage(FString::Printf(TEXT("Error: Failed to copy thumbnail \"%s\" to mod output folder"), *ThumbnailName));
				}
			}
			else
			{
				AddLogMessage(FString::Printf(TEXT("Warning: Thumbnail file \"%s\" not found for plugin \"%s\""),
					*ThumbnailName, *CurrentTaskName));
			}
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

EVisibility FPakCreatorWindow::GetThumbnailVisibility() const
{
	if (!PluginListWidget.IsValid())
	{
		return EVisibility::Collapsed;
	}

	const TArray<TSharedPtr<FStringEntry>> SelectedItems = PluginListWidget->GetSelectedItems();
	if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
	{
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

const FSlateBrush* FPakCreatorWindow::GetThumbnailPreviewBrush() const
{
	return ThumbnailPreviewBrush.IsValid()
		? ThumbnailPreviewBrush.Get()
		: FAppStyle::Get().GetBrush("Icons.Warning"); // fallback
}

static UTexture2D* LoadPngToTexture2D_Editor(const FString& FilePath)
{
	TArray<uint8> PngData;
	if (!FFileHelper::LoadFileToArray(PngData, *FilePath))
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(PngData.GetData(), PngData.Num()))
	{
		return nullptr;
	}

	TArray<uint8> RawBGRA;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA))
	{
		return nullptr;
	}

	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	if (W <= 0 || H <= 0)
	{
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	Tex->MipGenSettings = TMGS_NoMipmaps;
#endif
	Tex->SRGB = true;

	void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, RawBGRA.GetData(), RawBGRA.Num());
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();

	Tex->UpdateResource();
	return Tex;
}

void FPakCreatorWindow::RefreshThumbnailPreviewBrush()
{
	FString PreviewPath;

	// 1) If user picked an override, show that
	if (!SelectedThumbnailPath.IsEmpty() && FPaths::FileExists(SelectedThumbnailPath))
	{
		PreviewPath = SelectedThumbnailPath;
	}
	else
	{
		// 2) Otherwise show the selected plugin's thumbnail
		if (PluginListWidget.IsValid())
		{
			const TArray<TSharedPtr<FStringEntry>> SelectedItems = PluginListWidget->GetSelectedItems();
			if (SelectedItems.Num() > 0 && SelectedItems[0].IsValid())
			{
				const FString PluginName = SelectedItems[0]->PluginPath;
				const FString PluginDir = FPaths::Combine(FPaths::ProjectPluginsDir(), PluginName);

				FString ThumbName = TEXT("Thumbnail.png");

				const FString ModInfoPath = FPaths::Combine(PluginDir, TEXT("modinfo.json"));
				FString JsonText;
				if (FPaths::FileExists(ModInfoPath) && FFileHelper::LoadFileToString(JsonText, *ModInfoPath))
				{
					TSharedPtr<FJsonObject> Root;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
					if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
					{
						FString FromJson;
						if (Root->TryGetStringField(TEXT("Thumbnail"), FromJson))
						{
							FromJson = FromJson.TrimStartAndEnd();
							if (!FromJson.IsEmpty())
							{
								ThumbName = FromJson;
							}
						}
					}
				}

				FString Candidate = FPaths::Combine(PluginDir, ThumbName);
				if (!FPaths::FileExists(Candidate))
				{
					const FString Alt = FPaths::Combine(PluginDir, TEXT("Config"), ThumbName);
					if (FPaths::FileExists(Alt))
					{
						Candidate = Alt;
					}
				}

				if (FPaths::FileExists(Candidate))
				{
					PreviewPath = Candidate;
				}
			}
		}
	}

	// Clear old preview texture (avoid leaking roots)
	if (ThumbnailPreviewTexture)
	{
		ThumbnailPreviewTexture->RemoveFromRoot();
		ThumbnailPreviewTexture = nullptr;
	}

	if (PreviewPath.IsEmpty())
	{
		ThumbnailPreviewBrush.Reset();
		return;
	}

	ThumbnailPreviewTexture = LoadPngToTexture2D_Editor(PreviewPath);
	if (!ThumbnailPreviewTexture)
	{
		ThumbnailPreviewBrush.Reset();
		return;
	}

	ThumbnailPreviewTexture->AddToRoot();

	// Build a brush that uses the texture
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(ThumbnailPreviewTexture);
	Brush->ImageSize = FVector2D(180.f, 180.f);

	ThumbnailPreviewBrush = Brush;
}

void FPakCreatorWindow::OnPluginSelectionChanged(
	TSharedPtr<FStringEntry> Item,
	ESelectInfo::Type SelectInfo)
{
	// Clear manual override when switching plugins
	SelectedThumbnailPath.Empty();

	RefreshThumbnailPreviewBrush();
}

FReply FPakCreatorWindow::HandleThumbnailBrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
		return FReply::Handled();

	// Parent window handle (same pattern you use elsewhere)
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(CreateButton.ToSharedRef());
	void* ParentWindowHandle =
		(ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	TArray<FString> Files;
	const bool bOk = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		TEXT("Select Thumbnail"),
		FPaths::ProjectDir(),
		TEXT("Thumbnail.png"),
		TEXT("PNG Image (*.png)|*.png"),
		EFileDialogFlags::None,
		Files
	);

	if (!bOk || Files.Num() == 0)
		return FReply::Handled();

	FString Error;
	if (!ValidateThumbnailImage(Files[0], Error))
	{
		AddLogMessage(FString::Printf(TEXT("Thumbnail Error: %s"), *Error));
		return FReply::Handled();
	}

	SelectedThumbnailPath = Files[0];
	ApplyThumbnailToSelectedPlugins(SelectedThumbnailPath);
	RefreshThumbnailPreviewBrush();

	// Refresh textbox display
	if (ThumbnailPathInput.IsValid())
	{
		ThumbnailPathInput->SetText(GetCurrentThumbnailPath());
	}

	AddLogMessage(TEXT("Thumbnail updated for selected plugin(s)."));
	return FReply::Handled();
}

FReply FPakCreatorWindow::HandleClearThumbnailClicked()
{
	SelectedThumbnailPath.Empty();
	RefreshThumbnailPreviewBrush();

	if (ThumbnailPathInput.IsValid())
	{
		ThumbnailPathInput->SetText(GetCurrentThumbnailPath());
	}

	AddLogMessage(TEXT("Thumbnail override cleared (does not restore file automatically)."));
	return FReply::Handled();
}

FText FPakCreatorWindow::GetCurrentThumbnailPath() const
{
	return SelectedThumbnailPath.IsEmpty()
		? FText::FromString(TEXT("Using plugin thumbnail"))
		: FText::FromString(SelectedThumbnailPath);
}

bool FPakCreatorWindow::ValidateThumbnailImage(const FString& FilePath, FString& OutError) const
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *FilePath))
	{
		OutError = TEXT("Failed to read image file.");
		return false;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");

	TSharedPtr<IImageWrapper> Wrapper =
		ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Data.GetData(), Data.Num()))
	{
		OutError = TEXT("Invalid PNG file.");
		return false;
	}

	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();

	if (W != H)
	{
		OutError = TEXT("Thumbnail must be square.");
		return false;
	}

	// Allow any square size, but keep some sane limits
	const int32 MinSize = 256;
	const int32 MaxSize = 8192;

	if (W < MinSize || W > MaxSize)
	{
		OutError = FString::Printf(TEXT("Thumbnail must be between %d and %d pixels (square)."), MinSize, MaxSize);
		return false;
	}


	return true;
}

void FPakCreatorWindow::ApplyThumbnailToSelectedPlugins(const FString& SourceFile)
{
	if (!PluginListWidget.IsValid())
	{
		AddLogMessage(TEXT("Thumbnail Error: Plugin list not valid."));
		return;
	}

	const TArray<TSharedPtr<FStringEntry>> SelectedItems = PluginListWidget->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		AddLogMessage(TEXT("Thumbnail Error: No plugin selected."));
		return;
	}

	for (const TSharedPtr<FStringEntry>& PluginEntry : SelectedItems)
	{
		if (!PluginEntry.IsValid())
			continue;

		const FString PluginName = PluginEntry->PluginPath;
		const FString PluginDir = FPaths::Combine(FPaths::ProjectPluginsDir(), PluginName);

		// Default destination filename
		FString DestFileName = TEXT("Thumbnail.png");

		// Respect Thumbnail field in modinfo.json if present
		const FString ModInfoPath = FPaths::Combine(PluginDir, TEXT("modinfo.json"));
		FString JsonText;
		if (FPaths::FileExists(ModInfoPath) && FFileHelper::LoadFileToString(JsonText, *ModInfoPath))
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				FString FromJson;
				if (Root->TryGetStringField(TEXT("Thumbnail"), FromJson))
				{
					FromJson = FromJson.TrimStartAndEnd();
					if (!FromJson.IsEmpty())
					{
						DestFileName = FromJson;
					}
				}
			}
		}

		const FString DestPath = FPaths::Combine(PluginDir, DestFileName);

		if (IFileManager::Get().Copy(*DestPath, *SourceFile, /*bReplace=*/true, /*bEvenIfReadOnly=*/true) == COPY_OK)
		{
			AddLogMessage(FString::Printf(TEXT("Thumbnail copied to %s"), *DestPath));
		}
		else
		{
			AddLogMessage(FString::Printf(TEXT("Thumbnail Error: Failed to copy to %s"), *DestPath));
		}
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
