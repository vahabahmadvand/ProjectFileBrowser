// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserFileDataSource.h"
#include "ProjectFileBrowserDataSource.generated.h"

class FAssetThumbnail;

/** Stashed folder files for crash/deletion-proof folder rename in Unreal Engine Content Browser */
struct FFolderRenameStashEntry
{
	FString StashDirPath;
	TArray<FString> RelativeFilePaths;
	FDateTime StashTime;
};

/**
 * Custom Content Browser Data Source for non-Unreal project files.
 * Inherits from UContentBrowserFileDataSource to reuse folder/file scanning,
 * directory watching, virtual hierarchy, and context menus, while providing
 * custom thumbnail rendering (including live PNG/JPG thumbnails) and settings integration.
 */
UCLASS()
class PROJECTFILEBROWSER_API UProjectFileBrowserDataSource : public UContentBrowserFileDataSource
{
	GENERATED_BODY()

public:
	/** Custom filter compiler that allows files to be shown inside selected Content Browser folders */
	virtual void CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) override;

	/** Clean shutdown without accessing torn-down AssetTools on editor exit */
	virtual void Shutdown() override;

	/** Renders thumbnail for custom file items, generating real image textures for PNG/JPG */
	virtual bool UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail) override;

	/** Checks whether a file can be opened / edited */
	virtual bool CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	/** Opens the file in the default OS application (e.g. Photoshop, VS Code, Notepad) */
	virtual bool EditItem(const FContentBrowserItemData& InItem) override;

	/** Verifies whether an item (or folder) can be renamed, safely stashing files before engine folder deletion */
	virtual bool CanRenameItem(const FContentBrowserItemData& InItem, const FString* InNewName, const IContentBrowserHideFolderIfEmptyFilter* HideFolderIfEmptyFilter, FText* OutErrorMsg) override;

	/** Renames item or folder, restoring and registering all non-asset files safely without any loss */
	virtual bool RenameItem(const FContentBrowserItemData& InItem, const FString& InNewName, FContentBrowserItemData& OutNewItem) override;

private:
	/** Resolves physical disk path for an item or folder */
	bool ResolveItemDiskPath(const FContentBrowserItemData& InItem, FString& OutDiskPath);

	/** Stashes non-asset files from folder into Saved/ProjectFileBrowser/RenameStash before engine operations */
	void StashFolderFiles(const FString& InSourceDiskPath);

	/** Timestamps of cached image files to detect on-disk modifications */
	TMap<FString, FDateTime> ThumbnailFileTimestamps;

	/** Active folder rename stashes mapped by SourceDiskPath */
	TMap<FString, FFolderRenameStashEntry> FolderRenameStashes;
};
