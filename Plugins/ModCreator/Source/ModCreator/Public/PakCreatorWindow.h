// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"     // EActiveTimerReturnType
#include "Templates/SharedPointer.h"

// Forward decls
class SWidget;
class SListViewBase;
class FJsonObject;
class FUATProcess;

/** Simple entry used for list views (plugins, logs, layouts) */
struct FStringEntry
{
public:
	explicit FStringEntry(const FString& InPath) : PluginPath(InPath) {}
	FString PluginPath;
};

struct FModelTypeRow
{
	FString PluginName;
	FString ModelName;
	FString ModCategory;
	FString SelectedType;

	// Extra options for special attachment types
	bool bExcludeStock = false;  // used when SelectedType == "Rifle_BufferTube"
	bool bIncludeTank = false;  // used when SelectedType == "Rifle_Grip"
};

/**
 * Main window for Pak/Mod creation with a second tab for Layout authoring.
 */
class FPakCreatorWindow : public TSharedFromThis<FPakCreatorWindow>
{
public:
	FPakCreatorWindow() = default;
	~FPakCreatorWindow();                 // declaration only (definition is in .cpp)

	/** Builds the window/tab UI */
	TSharedRef<SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	
	// --- Thumbnail Override (UI + file replace in plugin) ---
	FString SelectedThumbnailPath;
	TSharedPtr<SEditableTextBox> ThumbnailPathInput;

	// Preview
	TSharedPtr<FSlateBrush> ThumbnailPreviewBrush;
	void RefreshThumbnailPreviewBrush();
	const FSlateBrush* GetThumbnailPreviewBrush() const;

	FReply HandleThumbnailBrowseClicked();
	FReply HandleClearThumbnailClicked();
	FText  GetCurrentThumbnailPath() const;

	bool   ValidateThumbnailImage(const FString& FilePath, FString& OutError) const;
	void   ApplyThumbnailToSelectedPlugins(const FString& SourceFile);

	// Optional: keep the preview updated when user selects different plugin(s)
	void OnPluginSelectionChanged(TSharedPtr<FStringEntry> Item, ESelectInfo::Type SelectInfo);

	UTexture2D* ThumbnailPreviewTexture = nullptr;
	EVisibility GetThumbnailVisibility() const;

private:
	// === Attachment/Clothing type selection dialog ===
	TArray<TSharedPtr<FModelTypeRow>> ModelTypeRows;
	TSharedPtr<SWindow> ModelTypeDialogWindow;
	TSharedPtr<SListView<TSharedPtr<FModelTypeRow>>> ModelTypeListView;

	// Options for attachment vs clothing types (string-only; adjust to your own categories)
	TArray<TSharedPtr<FString>> AttachmentTypeOptions;
	TArray<TSharedPtr<FString>> ClothingTypeOptions;

	// Cached data so we can start packaging AFTER the user hits Apply
	TArray<TSharedPtr<FStringEntry>> CachedSelectedPlugins;
	FString CachedPlatformSelection;
	FString CachedCookFlavor;
	FString CachedTargetName;

	bool bWaitingForModelTypes = false;
	bool bModelTypesApplied = false;

	// Helpers
	bool GatherAttachmentClothingModelsForSelection(
		const TArray<TSharedPtr<FStringEntry>>& SelectedPlugins
	);

	void BuildModelTypeOptions();
	void ShowModelTypeDialog();
	void CloseModelTypeDialog(bool bUserCancelled);

	FReply OnModelTypeApplyClicked();
	FReply OnModelTypeCancelClicked();

	TSharedRef<ITableRow> OnGenerateRowForModelType(
		TSharedPtr<FModelTypeRow> Item,
		const TSharedRef<STableViewBase>& OwnerTable
	);

	TSharedRef<SWidget> GenerateModelTypeComboWidget(TSharedPtr<FString> Item);
	void OnModelTypeSelected(TSharedPtr<FString> Selected, ESelectInfo::Type SelectInfo, TSharedPtr<FModelTypeRow> Row);

	// Kicks off the same packaging flow you currently do in CreateButtonPressed
	void StartPackagingAfterModelTypes();
	
	// ------------------------------------------------------------
	// Tab management
	// ------------------------------------------------------------
	TSharedPtr<SWidgetSwitcher> TabSwitcher;   // Switcher between Main and Layouts tabs
	int32 ActiveTabIndex = 0;                  // 0 = Main, 1 = Layouts

	// ------------------------------------------------------------
	// MAIN TAB: widgets & data (existing functionality)
	// ------------------------------------------------------------
	// Left side (plugins list & filter)
	TSharedPtr<SEditableTextBox> FilterInput;
	TSharedPtr<SListView<TSharedPtr<FStringEntry>>> PluginListWidget;
	TArray<TSharedPtr<FStringEntry>> Plugins;
	TArray<TSharedPtr<FStringEntry>> AllPlugins;

	// Layout logs
	TSharedPtr<SListView<TSharedPtr<FStringEntry>>> LayoutLogListWidget;

	// Right side controls
	TSharedPtr<SEditableTextBox> ProjectFileInput;
	TSharedPtr<SEditableTextBox> OutputInput;

	TSharedPtr<SButton> ProjectBrowserButton;
	TSharedPtr<SButton> BrowserButton;
	TSharedPtr<SButton> CreateButton;

	// Platform / Target selectors
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PlatformComboBox;
	TSharedPtr<STextBlock> PlatformSelectionTextBlock;
	TArray<TSharedPtr<FString>> PlatformsSource;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> TargetComboBox;
	TSharedPtr<STextBlock>   TargetSelectionTextBlock;
	TArray<TSharedPtr<FString>> TargetSource;

	// Log list
	TSharedPtr<SListView<TSharedPtr<FStringEntry>>> LogListWidget;
	TArray<TSharedPtr<FStringEntry>> LogEntries;

	// Current settings/state
	FString OutputProject = TEXT("");
	FString OutputRelease = TEXT("");
	FString OutputPath = TEXT("");

	FString CurrentTaskName = TEXT("");

	// Command queue for building paks with UAT. Pair key: DLC/plugin name, value: command line string
	TQueue<TPair<FString, FString>> PendingUATCommands;

	// Thread runner for UAT process
	TSharedPtr<FUATProcess> Runnable;

	// === Main tab handlers / utilities (signatures must match .cpp) ===
	void OnFilterTextChanged(const FText& InText);
	bool OnFilterVerifyTextChanged(const FText& NewText, FText& OutErrorText);
	void OnFilterTextCommitted(const FText& InText, ETextCommit::Type CommitType);

	TSharedRef<ITableRow> OnGenerateRowForList(TSharedPtr<FStringEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateRowForLog(TSharedPtr<FStringEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);

	void OnPlatformSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);
	void OnTargetSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);

	// ComboBox item widget
	TSharedRef<SWidget> GenerateComboBoxWidget(TSharedPtr<FString> Item);

	// Grab values for the input fields
	FText GetCurrentProjectFile()  const;
	FText GetCurrentReleaseName()  const;
	FText GetCurrentPath()         const;
	FText GetCurrentPlatform()     const;

	// Buttons
	FReply CreateButtonPressed();
	FReply HandleBrowseButtonClicked();
	FReply HandleProjectBrowseButtonClicked();

	// Build pipeline
	void   PopulatePluginList(const FString& ProjectPluginDirectory);
	void   FilterPluginList(const FString& InFilterText);

	bool   RunBuild();
	bool   RunUATBuildProcess(const FString& CommandLine);
	FString GetTemporaryStagingDirectory() const;
	void   ProcessComplete(int32 ErrorCode);

	// Log helpers / timer
	void   AddLogMessage(const FString& InMsg);
	EActiveTimerReturnType RefreshLog(double InCurrentTime, float InDeltaTime);

	// ------------------------------------------------------------
	// LAYOUTS TAB: widgets & data (new)
	// ------------------------------------------------------------
	/** Filter + list */
	TSharedPtr<SEditableTextBox> LayoutFilterInput;
	TSharedPtr<SListView<TSharedPtr<FStringEntry>>> LayoutListWidget;
	TArray<TSharedPtr<FStringEntry>> AllLayoutFiles;
	TArray<TSharedPtr<FStringEntry>> LayoutFiles;

	// --- Layout plugin picker ---
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LayoutPluginComboBox;
	TSharedPtr<STextBlock> LayoutPluginSelectionText;
	TArray<TSharedPtr<FString>> LayoutPluginSource;
	FString SelectedLayoutPluginName = TEXT("");  // e.g. "tt"
	void PopulateLayoutContentPluginList();  // builds LayoutPluginSource (excludes ModCreator)
	void OnLayoutPluginSelected(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);
	FText GetCurrentLayoutPluginText() const;

	/** Inputs */
	TSharedPtr<SEditableTextBox> LayoutNameInput;
	TSharedPtr<SEditableTextBox> UmapPathInput;

	/** Selection state */
	TSharedPtr<FStringEntry> SelectedLayoutEntry;
	FString SelectedLayoutPath;
	FString SelectedUmapPath;

	// --- Pak selection & options ---
	TSharedPtr<SEditableTextBox> PakPathInput;
	FString SelectedPakPath;                 // absolute path to the .pak (optional)

	/** Layouts tab helpers */
	void     RefreshLayoutList();
	void     FilterLayoutList(const FString& InFilter);
	TSharedRef<ITableRow> OnGenerateRowForLayout(TSharedPtr<FStringEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void     OnLayoutSelectionChanged(TSharedPtr<FStringEntry> Item, ESelectInfo::Type SelectInfo);
	FReply   HandleDeselectLayoutClicked();

	FReply   HandleSelectUmapClicked();
	FReply   HandleCreateLayoutClicked();
	bool     GatherLayoutActorsJSON(TSharedRef<FJsonObject> OutRoot);

	FString  GetLayoutsFolderOnDisk() const;
	FText    GetCreateLayoutButtonText() const;

	FReply HandleSelectPakClicked();

	// Not sure where to put
	void OnProjectFileCommitted(const FText& InText, ETextCommit::Type InTextAction);
	void OnPathTextCommitted(const FText& InText, ETextCommit::Type InTextAction);
	void OnReleaseNameCommitted(const FText& InText, ETextCommit::Type InTextAction);

};