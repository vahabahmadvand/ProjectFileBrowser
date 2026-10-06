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
