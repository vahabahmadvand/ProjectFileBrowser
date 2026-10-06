// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#include "ProjectFileBrowserDataSource.h"
#include "ProjectFileBrowserSettings.h"
#include "ContentBrowserFileDataPayload.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserDataSubsystem.h"
#include "AssetThumbnail.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ObjectTools.h"
#include "Misc/ObjectThumbnail.inl"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ProjectFileBrowserDataSource)

void UProjectFileBrowserDataSource::CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UProjectFileBrowserDataSource::CompileFilter);

	const FContentBrowserDataObjectFilter* ObjectFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataObjectFilter>();
	const FContentBrowserDataPackageFilter* PackageFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataPackageFilter>();
	const FContentBrowserDataClassFilter* ClassFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataClassFilter>();
	const FContentBrowserDataCollectionFilter* CollectionFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataCollectionFilter>();

	const bool bIncludeFolders = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders);
	const bool bIncludeFiles = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles);
	const bool bIncludeMisc = EnumHasAnyFlags(InFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeMisc);

	// If we aren't including anything, then bail
	if (!bIncludeMisc || (!bIncludeFolders && !bIncludeFiles))
	{
		return;
	}

	// If filtering specifically for objects/tags, skip files
	if (ObjectFilter && (ObjectFilter->ObjectNamesToInclude.Num() > 0 || ObjectFilter->TagsAndValuesToInclude.Num() > 0))
	{
		return;
	}

	// If filtering specifically for package names (UAsset asset packages), skip files
	if (PackageFilter && PackageFilter->PackageNamesToInclude.Num() > 0)
	{
		return;
	}

	// NOTE: In Epic's UContentBrowserFileDataSource::CompileFilter, having PackagePathsToInclude > 0 bails out immediately!
	// But in the Content Browser, selecting a folder in the path tree populates PackagePathsToInclude with that folder.
	// We want project files in that folder (and subfolders if recursive) to be shown!
	if (PackageFilter && PackageFilter->PackagePathsToInclude.Num() > 0)
	{
		FName ConvertedInternalPath;
		TryConvertVirtualPath(InPath, ConvertedInternalPath);
		const FString ConvertedInternalPathStr = ConvertedInternalPath.ToString();

		// Check if ConvertedInternalPath is compatible with PackagePathsToInclude
		if (!ConvertedInternalPath.IsNone() && ConvertedInternalPath != TEXT("/") && ConvertedInternalPath != TEXT("/All"))
		{
			bool bPathMatches = false;
			for (const FName& PkgPath : PackageFilter->PackagePathsToInclude)
			{
				const FString PkgPathStr = PkgPath.ToString();
				if (ConvertedInternalPathStr.Equals(PkgPathStr, ESearchCase::IgnoreCase) ||
					ConvertedInternalPathStr.StartsWith(PkgPathStr + TEXT("/"), ESearchCase::IgnoreCase) ||
					(InFilter.bRecursivePaths && PkgPathStr.StartsWith(ConvertedInternalPathStr + TEXT("/"), ESearchCase::IgnoreCase)))
				{
					bPathMatches = true;
					break;
				}
			}

			if (!bPathMatches)
			{
				return;
			}
		}
	}

	// Class filter handling
	TArray<FString> FileExtensionsToInclude;
	if (ClassFilter && ClassFilter->ClassNamesToInclude.Num() > 0)
	{
		static const FString FileFilterPrefix = TEXT("ContentBrowserDataFilterFile");
		TArray<FString, TInlineAllocator<4>> ClassNamesToLookFor;
		TArray<FString, TInlineAllocator<4>> ClassFileExtensions;

		Config.EnumerateFileActions([&ClassNamesToLookFor, &ClassFileExtensions](TSharedRef<const ContentBrowserFileData::FFileActions> InFileActions)
		{
			TStringBuilder<64> ClassNameToLookFor;
			ClassNameToLookFor.Append(FileFilterPrefix);
			ClassNameToLookFor.Append(InFileActions->TypeName.ToString());
			ClassNamesToLookFor.Add(ClassNameToLookFor.ToString());

			ClassFileExtensions.Add(InFileActions->TypeExtension);
			return true;
		});

		for (const FString& ClassName : ClassFilter->ClassNamesToInclude)
		{
			int32 FoundIndex = INDEX_NONE;
			if (ClassNamesToLookFor.Find(ClassName, FoundIndex))
			{
				FileExtensionsToInclude.Add(ClassFileExtensions[FoundIndex]);
			}
		}

		if (FileExtensionsToInclude.Num() == 0)
		{
			return;
		}
	}

	if (CollectionFilter && CollectionFilter->Collections.Num() > 0)
	{
		return;
	}

	// Cache compiled filter
	FContentBrowserDataFilterList& FilterList = OutCompiledFilter.CompiledFilters.FindOrAdd(this);
	FContentBrowserCompiledFileDataFilter& FileDataFilter = FilterList.FindOrAddFilter<FContentBrowserCompiledFileDataFilter>();

	FileDataFilter.VirtualPathName = InPath;
	FileDataFilter.VirtualPath = InPath.ToString();
	FileDataFilter.bRecursivePaths = InFilter.bRecursivePaths;
	FileDataFilter.ItemAttributeFilter = InFilter.ItemAttributeFilter;
	FileDataFilter.FileExtensionsToInclude.Append(FileExtensionsToInclude);
	if (PackageFilter && PackageFilter->PathPermissionList && PackageFilter->PathPermissionList->HasFiltering())
	{
		FileDataFilter.PermissionList = PackageFilter->PathPermissionList;
	}
}

void UProjectFileBrowserDataSource::Shutdown()
{
	// Clean up any remaining temporary stash directories
	IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("ProjectFileBrowser/RenameStash")), false, true);
	IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("ProjectFileBrowser")), false, false);
	FolderRenameStashes.Empty();

	// Clear RegisteredAssetTypeActions to avoid crashing in UE 5.8 during editor exit
	// when AssetTools / UAssetDefinitionRegistry has already been torn down
	if (IsEngineExitRequested() || !GIsEditor)
	{
		RegisteredAssetTypeActions.Empty();
	}

	Super::Shutdown();
}

bool UProjectFileBrowserDataSource::UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail)
{
	TSharedPtr<const FContentBrowserFileItemDataPayload> FilePayload = GetFileItemPayload(InItem);
	if (!FilePayload.IsValid())
	{
		return Super::UpdateThumbnail(InItem, InThumbnail);
	}

	TSharedPtr<const ContentBrowserFileData::FFileActions> FileActions = FilePayload->GetFileActions();
	if (!FileActions.IsValid())
	{
		return Super::UpdateThumbnail(InItem, InThumbnail);
	}

	const FString DiskFilename = FilePayload->GetFilename();
	const FString FileExt = FPaths::GetExtension(DiskFilename).ToLower();
	const UProjectFileBrowserSettings* Settings = GetDefault<UProjectFileBrowserSettings>();
	const FProjectFileExtensionConfig* ExtConfig = Settings ? Settings->FindExtensionConfig(FileExt) : nullptr;

	// Build asset data identifying this file item
	const FAssetData VirtualAssetData(
		FilePayload->GetInternalPath(),
		*FPaths::GetPath(FilePayload->GetInternalPath().ToString()),
		*FPaths::GetBaseFilename(DiskFilename),
		FileActions->TypeName
	);

	InThumbnail.SetAsset(VirtualAssetData);

	// If this extension is configured for image thumbnails (PNG, JPG, etc.), load and cache the real image!
	const bool bShouldGenerateThumbnail = ExtConfig ? ExtConfig->bGenerateImageThumbnail : (FileExt == TEXT("png") || FileExt == TEXT("jpg") || FileExt == TEXT("jpeg"));

	if (bShouldGenerateThumbnail)
	{
		const FString ObjectFullName = VirtualAssetData.GetFullName();

		// Determine the package name that ThumbnailTools::FindCachedThumbnail expects
		FString PackageName;
		const int32 FirstSpaceIndex = ObjectFullName.Find(TEXT(" "));
		if (FirstSpaceIndex != INDEX_NONE && FirstSpaceIndex > 0)
		{
			const FString ObjectPathName = ObjectFullName.Mid(FirstSpaceIndex + 1);
			const int32 FirstDotIndex = ObjectPathName.Find(TEXT("."));
			if (FirstDotIndex != INDEX_NONE && FirstDotIndex > 0)
			{
				PackageName = ObjectPathName.Left(FirstDotIndex);
			}
		}

		if (PackageName.IsEmpty())
		{
			PackageName = VirtualAssetData.PackageName.ToString();
		}

		UPackage* TargetPackage = FindPackage(nullptr, *PackageName);
		if (!TargetPackage)
		{
			TargetPackage = CreatePackage(*PackageName);
		}
		if (TargetPackage)
		{
			TargetPackage->SetFlags(RF_Standalone);
		}

		// Check timestamp to see if file on disk changed
		const FDateTime CurrentTimestamp = IFileManager::Get().GetTimeStamp(*DiskFilename);
		const FDateTime* CachedTimestamp = ThumbnailFileTimestamps.Find(DiskFilename);
		const bool bDiskFileModified = !CachedTimestamp || (*CachedTimestamp != CurrentTimestamp);

		const FObjectThumbnail* ExistingThumb = TargetPackage ? ThumbnailTools::FindCachedThumbnail(ObjectFullName) : nullptr;
		if (!ExistingThumb || ExistingThumb->IsEmpty() || bDiskFileModified)
		{
			FImage LoadedImage;
			if (FImageUtils::LoadImage(*DiskFilename, LoadedImage))
			{
				ThumbnailFileTimestamps.Add(DiskFilename, CurrentTimestamp);

				// Limit thumbnail size to maximum 256x256 while preserving aspect ratio
				constexpr int32 MaxThumbnailDim = 256;
				int32 ScaledWidth = LoadedImage.SizeX;
				int32 ScaledHeight = LoadedImage.SizeY;

				if (ScaledWidth > MaxThumbnailDim || ScaledHeight > MaxThumbnailDim)
				{
					if (ScaledWidth >= ScaledHeight)
					{
						ScaledHeight = FMath::Max(1, FMath::RoundToInt((float)ScaledHeight * ((float)MaxThumbnailDim / (float)ScaledWidth)));
						ScaledWidth = MaxThumbnailDim;
					}
					else
					{
						ScaledWidth = FMath::Max(1, FMath::RoundToInt((float)ScaledWidth * ((float)MaxThumbnailDim / (float)ScaledHeight)));
						ScaledHeight = MaxThumbnailDim;
					}

					FImageCore::ResizeImageInPlace(LoadedImage, ScaledWidth, ScaledHeight, FImageCore::EResizeImageFilter::Default);
				}

				// Ensure image is converted to BGRA8 sRGB
				LoadedImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);

				// Pad into a centered 256x256 square thumbnail to match standard UE thumbnail dimensions
				FImage SquareImage;
				SquareImage.Init(MaxThumbnailDim, MaxThumbnailDim, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				FMemory::Memzero(SquareImage.RawData.GetData(), SquareImage.RawData.Num());

				const int32 OffsetX = FMath::Clamp((MaxThumbnailDim - ScaledWidth) / 2, 0, MaxThumbnailDim - ScaledWidth);
				const int32 OffsetY = FMath::Clamp((MaxThumbnailDim - ScaledHeight) / 2, 0, MaxThumbnailDim - ScaledHeight);
				constexpr int32 BytesPerPixel = 4;

				for (int32 Row = 0; Row < ScaledHeight; ++Row)
				{
					const uint8* SrcRow = LoadedImage.RawData.GetData() + (Row * ScaledWidth * BytesPerPixel);
					uint8* DstRow = SquareImage.RawData.GetData() + (((OffsetY + Row) * MaxThumbnailDim + OffsetX) * BytesPerPixel);
					FMemory::Memcpy(DstRow, SrcRow, ScaledWidth * BytesPerPixel);
				}

				FObjectThumbnail NewThumbnail;
				NewThumbnail.SetImage(MoveTemp(SquareImage));

				if (TargetPackage)
				{
					ThumbnailTools::CacheThumbnail(ObjectFullName, &NewThumbnail, TargetPackage);
				}
			}
		}

		InThumbnail.SetAsset(VirtualAssetData);
		InThumbnail.RefreshThumbnail();

		return true;
	}

	return Super::UpdateThumbnail(InItem, InThumbnail);
}

bool UProjectFileBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	FString DiskPath;
	if (GetItemPhysicalPath(InItem, DiskPath) && FPaths::FileExists(DiskPath))
	{
		return true;
	}
	return Super::CanEditItem(InItem, OutErrorMsg);
}

bool UProjectFileBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	FString DiskPath;
	if (GetItemPhysicalPath(InItem, DiskPath) && FPaths::FileExists(DiskPath))
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*DiskPath);
		return true;
	}
	return Super::EditItem(InItem);
}

bool UProjectFileBrowserDataSource::ResolveItemDiskPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	if (GetItemPhysicalPath(InItem, OutDiskPath) && !OutDiskPath.IsEmpty())
	{
		return true;
	}

	FName InternalPath;
	if (TryConvertVirtualPathToInternal(InItem.GetVirtualPath(), InternalPath))
	{
		if (IsKnownFileMount(InternalPath, &OutDiskPath) && !OutDiskPath.IsEmpty())
		{
			return true;
		}

		if (FPackageName::TryConvertLongPackageNameToFilename(InternalPath.ToString(), OutDiskPath) && !OutDiskPath.IsEmpty())
		{
			return true;
		}
	}

	return false;
}

void UProjectFileBrowserDataSource::StashFolderFiles(const FString& InSourceDiskPath)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *InSourceDiskPath, TEXT("*"), true, false);

	TArray<FString> FilesToStash;
	for (const FString& FilePath : FoundFiles)
	{
		const FString Ext = FPaths::GetExtension(FilePath).ToLower();
		if (Ext != TEXT("uasset") && Ext != TEXT("umap"))
		{
			FilesToStash.Add(FilePath);
		}
	}

	if (FilesToStash.IsEmpty())
	{
		return;
	}

	// If we already stashed this exact folder and file count, reuse existing stash
	FFolderRenameStashEntry* ExistingStash = FolderRenameStashes.Find(InSourceDiskPath);
	if (ExistingStash && ExistingStash->RelativeFilePaths.Num() == FilesToStash.Num())
	{
		return;
	}

	const FString StashDir = FPaths::ProjectSavedDir() / TEXT("ProjectFileBrowser/RenameStash") / FGuid::NewGuid().ToString();
	IFileManager::Get().MakeDirectory(*StashDir, true);

	FFolderRenameStashEntry NewStash;
	NewStash.StashDirPath = StashDir;
	NewStash.StashTime = FDateTime::UtcNow();

	for (const FString& FilePath : FilesToStash)
	{
		FString RelPath = FilePath;
		FPaths::MakePathRelativeTo(RelPath, *(InSourceDiskPath + TEXT("/")));
		const FString StashDestPath = StashDir / RelPath;

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(StashDestPath), true);
		if (IFileManager::Get().Copy(*StashDestPath, *FilePath, true, true) == COPY_OK)
		{
			NewStash.RelativeFilePaths.Add(RelPath);
		}
	}

	FolderRenameStashes.Add(InSourceDiskPath, MoveTemp(NewStash));
}

bool UProjectFileBrowserDataSource::CanRenameItem(const FContentBrowserItemData& InItem, const FString* InNewName, const IContentBrowserHideFolderIfEmptyFilter* HideFolderIfEmptyFilter, FText* OutErrorMsg)
{
	if (InItem.IsFolder())
	{
		FString SourceDiskPath;
		if (ResolveItemDiskPath(InItem, SourceDiskPath) && FPaths::DirectoryExists(SourceDiskPath))
		{
			// Preemptively stash all non-asset files in this folder to protect against engine AssetViewUtils deletion bug
			StashFolderFiles(SourceDiskPath);

			if (InNewName && !InNewName->IsEmpty())
			{
				const FString TargetDiskPath = FPaths::GetPath(SourceDiskPath) / *InNewName;
				if (FPaths::DirectoryExists(TargetDiskPath))
				{
					TArray<FString> ExistingFiles;
					IFileManager::Get().FindFilesRecursive(ExistingFiles, *TargetDiskPath, TEXT("*"), true, false);
					if (ExistingFiles.IsEmpty())
					{
						// Safely remove empty leftover directory so the rename check passes
						IFileManager::Get().DeleteDirectory(*TargetDiskPath, false, true);
					}
					else
					{
						// Check if existing files conflict with non-asset files
						bool bHasConflict = false;
						for (const FString& ExistingFile : ExistingFiles)
						{
							const FString Ext = FPaths::GetExtension(ExistingFile).ToLower();
							if (Ext != TEXT("uasset") && Ext != TEXT("umap"))
							{
								bHasConflict = true;
								break;
							}
						}

						if (bHasConflict)
						{
							if (OutErrorMsg)
							{
								*OutErrorMsg = FText::FromString(TEXT("A folder already exists at this location with this name and contains project files."));
							}
							return false;
						}
					}
				}
			}
		}
	}

	return Super::CanRenameItem(InItem, InNewName, HideFolderIfEmptyFilter, OutErrorMsg);
}

bool UProjectFileBrowserDataSource::RenameItem(const FContentBrowserItemData& InItem, const FString& InNewName, FContentBrowserItemData& OutNewItem)
{
	if (!InItem.IsFolder())
	{
		return Super::RenameItem(InItem, InNewName, OutNewItem);
	}

	// 1. Resolve source disk path and internal path
	FString SourceDiskPath;
	ResolveItemDiskPath(InItem, SourceDiskPath);

	FName OldInternalPath;
	TryConvertVirtualPathToInternal(InItem.GetVirtualPath(), OldInternalPath);

	const FString TargetDiskPath = FPaths::GetPath(SourceDiskPath) / InNewName;
	const FString NewInternalStr = FPaths::GetPath(OldInternalPath.ToString()) / InNewName;
	const FName NewInternalPath = FName(*NewInternalStr);

	// Ensure Target directory exists on disk
	IFileManager::Get().MakeDirectory(*TargetDiskPath, true);

	// 2. Restore/migrate all stashed files into TargetDiskPath
	FFolderRenameStashEntry* Stash = FolderRenameStashes.Find(SourceDiskPath);
	if (Stash)
	{
		for (const FString& RelPath : Stash->RelativeFilePaths)
		{
			const FString StashFile = Stash->StashDirPath / RelPath;
			const FString TargetFile = TargetDiskPath / RelPath;
			const FString SourceFile = SourceDiskPath / RelPath;

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetFile), true);

			if (FPaths::FileExists(SourceFile))
			{
				// Source file still exists on disk: move it directly
				IFileManager::Get().Move(*TargetFile, *SourceFile, true, true);
				// Ensure backup/stash copy is removed
				if (FPaths::FileExists(StashFile))
				{
					IFileManager::Get().Delete(*StashFile, false, true);
				}
			}
			else if (FPaths::FileExists(StashFile))
			{
				// Source file was deleted by engine's AssetViewUtils::RenameFolder: move directly from stash to remove the backup file
				IFileManager::Get().Move(*TargetFile, *StashFile, true, true);
			}
		}

		// Clean up stash directory from disk
		IFileManager::Get().DeleteDirectory(*Stash->StashDirPath, false, true);
		FolderRenameStashes.Remove(SourceDiskPath);

		// Clean up empty parent directories in Saved so no leftover backup folders remain
		const FString ParentStashDir = FPaths::ProjectSavedDir() / TEXT("ProjectFileBrowser/RenameStash");
		TArray<FString> LeftoverStashes;
		IFileManager::Get().FindFilesRecursive(LeftoverStashes, *ParentStashDir, TEXT("*"), true, false);
		if (LeftoverStashes.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*ParentStashDir, false, true);
		}

		const FString PFBDir = FPaths::ProjectSavedDir() / TEXT("ProjectFileBrowser");
		TArray<FString> LeftoverPFB;
		IFileManager::Get().FindFilesRecursive(LeftoverPFB, *PFBDir, TEXT("*"), true, false);
		if (LeftoverPFB.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*PFBDir, false, true);
		}
	}

	// 3. Move any other non-uasset files remaining in SourceDiskPath
	if (FPaths::DirectoryExists(SourceDiskPath))
	{
		TArray<FString> RemainingFiles;
		IFileManager::Get().FindFilesRecursive(RemainingFiles, *SourceDiskPath, TEXT("*"), true, false);
		for (const FString& RemFile : RemainingFiles)
		{
			const FString Ext = FPaths::GetExtension(RemFile).ToLower();
			if (Ext != TEXT("uasset") && Ext != TEXT("umap"))
			{
				FString Rel = RemFile;
				FPaths::MakePathRelativeTo(Rel, *(SourceDiskPath + TEXT("/")));
				const FString Dest = TargetDiskPath / Rel;
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest), true);
				IFileManager::Get().Move(*Dest, *RemFile, true, true);
			}
		}

		// If SourceDiskPath is now empty of all files, remove it
		TArray<FString> CheckFiles;
		IFileManager::Get().FindFilesRecursive(CheckFiles, *SourceDiskPath, TEXT("*"), true, false);
		if (CheckFiles.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*SourceDiskPath, false, true);
		}
	}

	// 4. Remove old folder and all its discovered child items from our data source
	RemoveDiscoveredItem(OldInternalPath);

	// 5. Create new folder item and register with data source
	OutNewItem = CreateFolderItem(NewInternalPath, TargetDiskPath);
	AddDiscoveredItem(FDiscoveredItem::EType::Directory, NewInternalPath.ToString(), TargetDiskPath, false);
	OnAlwaysShowPath(NewInternalPath);

	// 6. Discover and synchronously register all files in TargetDiskPath
	TArray<FString> MigratedFiles;
	IFileManager::Get().FindFilesRecursive(MigratedFiles, *TargetDiskPath, TEXT("*"), true, false);
	for (const FString& MigratedFile : MigratedFiles)
	{
		const FString Ext = FPaths::GetExtension(MigratedFile).ToLower();
		if (Config.FindFileActionsForExtension(Ext).IsValid())
		{
			FString Rel = MigratedFile;
			FPaths::MakePathRelativeTo(Rel, *(TargetDiskPath + TEXT("/")));
			const FString ItemMount = NewInternalPath.ToString() / Rel;
			AddDiscoveredItem(FDiscoveredItem::EType::File, ItemMount, MigratedFile, false);
		}
	}

	return true;
}
