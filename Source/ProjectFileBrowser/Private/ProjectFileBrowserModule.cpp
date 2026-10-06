// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#include "ProjectFileBrowserModule.h"
#include "ProjectFileBrowserDataSource.h"
#include "ProjectFileBrowserSettings.h"
#include "ContentBrowserFileDataCore.h"
#include "ContentBrowserFileDataPayload.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataMenuContexts.h"
#include "IContentBrowserDataModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"

#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "ProjectFileBrowser"

void FProjectFileBrowserModule::StartupModule()
{
	if (!GIsEditor || IsRunningCommandlet())
	{
		return;
	}

	// Listen for settings change
	UProjectFileBrowserSettings::OnSettingsChanged.AddRaw(this, &FProjectFileBrowserModule::InitializeDataSource);

	// Context menu extensions
	RegisterContextMenuExtensions();

	// If editor engine is already initialized, initialize immediately; otherwise wait for OnPostEngineInit
	if (GEditor)
	{
		InitializeDataSource();
	}
	else
	{
		FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FProjectFileBrowserModule::OnPostEngineInit);
	}
}

void FProjectFileBrowserModule::OnPostEngineInit()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	InitializeDataSource();
}

void FProjectFileBrowserModule::ShutdownModule()
{
	if (!GIsEditor || IsRunningCommandlet())
	{
		return;
	}

	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	UProjectFileBrowserSettings::OnSettingsChanged.RemoveAll(this);
	ShutdownDataSource();
	UToolMenus::UnregisterOwner(this);
}

void FProjectFileBrowserModule::InitializeDataSource()
{
	ShutdownDataSource();

	const UProjectFileBrowserSettings* Settings = GetDefault<UProjectFileBrowserSettings>();
	if (!Settings || !Settings->bEnableFileBrowser)
	{
		return;
	}

	ContentBrowserFileData::FFileConfigData ConfigData;

	// Common actions for directory items
	ContentBrowserFileData::FDirectoryActions DirectoryActions;
	DirectoryActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, false);
	DirectoryActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);
	ConfigData.SetDirectoryActions(DirectoryActions);

	// Register file actions for each configured extension
	TSet<FString> RegisteredExtensions;
	for (const FProjectFileExtensionConfig& ExtConfig : Settings->MonitoredExtensions)
	{
		const FString CleanExt = ExtConfig.Extension.TrimStartAndEnd().ToLower().Replace(TEXT("."), TEXT(""));
		if (CleanExt.IsEmpty() || RegisteredExtensions.Contains(CleanExt))
		{
			continue;
		}

		RegisteredExtensions.Add(CleanExt);

		ContentBrowserFileData::FFileActions FileActions;
		FileActions.TypeExtension = CleanExt;

		// Virtual asset class name to make it recognisable to Content Browser filters
		FileActions.TypeName = FTopLevelAssetPath(
			*FString::Printf(TEXT("/Script/ProjectFileBrowser.%sFile"), *CleanExt.ToUpper())
		);

		FileActions.TypeDisplayName = ExtConfig.DisplayName.IsEmpty()
			? FText::FromString(CleanExt.ToUpper() + TEXT(" File"))
			: ExtConfig.DisplayName;

		FileActions.TypeShortDescription = FileActions.TypeDisplayName;
		FileActions.TypeFullDescription = FText::Format(
			LOCTEXT("FileDescFmt", "{0} (*.{1})"),
			FileActions.TypeDisplayName,
			FText::FromString(CleanExt)
		);

		FileActions.TypeColor = ExtConfig.AccentColor;

		// Double-click edit action: launch in default OS application
		FileActions.CanEdit.BindLambda([](const FName InFilePath, const FString& InFilename, FText* OutErrorMsg)
		{
			return FPaths::FileExists(InFilename);
		});

		FileActions.Edit.BindLambda([](const FName InFilePath, const FString& InFilename)
		{
			FPlatformProcess::LaunchFileInDefaultExternalApplication(*InFilename);
			return true;
		});

		FileActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, true);
		FileActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);

		ConfigData.RegisterFileActions(FileActions);
	}

	FileDataSource.Reset(NewObject<UProjectFileBrowserDataSource>(GetTransientPackage()));
	FileDataSource->Initialize(ConfigData);

	// Activate data source in ContentBrowserDataSubsystem
	if (IContentBrowserDataModule* DataModule = IContentBrowserDataModule::GetPtr())
	{
		if (UContentBrowserDataSubsystem* Subsystem = DataModule->GetSubsystem())
		{
			Subsystem->ActivateDataSource(FileDataSource->GetFName());
			UE_LOG(LogTemp, Log, TEXT("ProjectFileBrowser: Activated data source '%s' in ContentBrowserDataSubsystem."), *FileDataSource->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ProjectFileBrowser: ContentBrowserDataSubsystem not yet available."));
		}
	}

	// Mount all existing content roots (/Game, plugins, etc.)
	TArray<FString> RootPaths;
	FPackageName::QueryRootContentPaths(RootPaths);
	for (const FString& RootPath : RootPaths)
	{
		OnContentPathMounted(RootPath, FPackageName::LongPackageNameToFilename(RootPath));
	}

	// Listen for dynamically mounted / dismounted content paths
	FPackageName::OnContentPathMounted().AddRaw(this, &FProjectFileBrowserModule::OnContentPathMounted);
	FPackageName::OnContentPathDismounted().AddRaw(this, &FProjectFileBrowserModule::OnContentPathDismounted);

	bDataSourceInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("ProjectFileBrowser: Initialized data source with %d root content paths mounted."), RootPaths.Num());
}

void FProjectFileBrowserModule::ShutdownDataSource()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	FPackageName::OnContentPathMounted().RemoveAll(this);
	FPackageName::OnContentPathDismounted().RemoveAll(this);

	if (FileDataSource)
	{
		// Only deactivate/shutdown if the editor is NOT exiting.
		// During editor exit, AssetTools and ContentBrowserDataSubsystem are torn down
		// and touching them causes access violations.
		if (!IsEngineExitRequested())
		{
			if (IContentBrowserDataModule* DataModule = IContentBrowserDataModule::GetPtr())
			{
				if (UContentBrowserDataSubsystem* Subsystem = DataModule->GetSubsystem())
				{
					Subsystem->DeactivateDataSource(FileDataSource->GetFName());
				}
			}

			FileDataSource->Shutdown();
		}

		FileDataSource.Reset();
	}

	bDataSourceInitialized = false;
}

void FProjectFileBrowserModule::OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (!FileDataSource)
	{
		return;
	}

	// Normalize mount path (no trailing slashes)
	FString MountPath = InAssetPath;
	if (MountPath.EndsWith(TEXT("/")))
	{
		MountPath.LeftChopInline(1);
	}

	FString DiskPath = FPaths::ConvertRelativePathToFull(InFilesystemPath);
	if (DiskPath.EndsWith(TEXT("/")))
	{
		DiskPath.LeftChopInline(1);
	}

	if (FPaths::DirectoryExists(DiskPath) && !FileDataSource->HasFileMount(*MountPath))
	{
		UE_LOG(LogTemp, Log, TEXT("ProjectFileBrowser: Adding file mount '%s' -> '%s'"), *MountPath, *DiskPath);
		FileDataSource->AddFileMount(*MountPath, DiskPath);
	}
}

void FProjectFileBrowserModule::OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (!FileDataSource)
	{
		return;
	}

	FString MountPath = InAssetPath;
	if (MountPath.EndsWith(TEXT("/")))
	{
		MountPath.LeftChopInline(1);
	}

	if (FileDataSource->HasFileMount(*MountPath))
	{
		FileDataSource->RemoveFileMount(*MountPath);
	}
}

void FProjectFileBrowserModule::RegisterContextMenuExtensions()
{
	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu"))
	{
		Menu->AddDynamicSection(TEXT("DynamicSection_ProjectFileBrowser"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			const UContentBrowserDataMenuContext_FileMenu* Context = InMenu->FindContext<UContentBrowserDataMenuContext_FileMenu>();
			if (!Context)
			{
				return;
			}

			// Collect any project file browser items in selection
			TArray<FString> SelectedFilePaths;
			for (const FContentBrowserItem& SelectedItem : Context->SelectedItems)
			{
				if (!SelectedItem.IsFile())
				{
					continue;
				}

				for (const FContentBrowserItemData& ItemData : SelectedItem.GetInternalItems())
				{
					if (Cast<UProjectFileBrowserDataSource>(ItemData.GetOwnerDataSource()))
					{
						FString DiskPath;
						if (ContentBrowserFileData::GetItemPhysicalPath(ItemData.GetOwnerDataSource(), ItemData, DiskPath))
						{
							SelectedFilePaths.Add(DiskPath);
						}
					}
				}
			}

			if (SelectedFilePaths.Num() == 0)
			{
				return;
			}

			FToolMenuSection& Section = InMenu->FindOrAddSection("CommonAssetActions");
			Section.AddMenuEntry(
				TEXT("OpenFileInExternalEditor"),
				LOCTEXT("OpenFileExternal", "Open in External Editor"),
				LOCTEXT("OpenFileExternalTooltip", "Opens selected file(s) in default operating system application."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([SelectedFilePaths]()
				{
					for (const FString& Path : SelectedFilePaths)
					{
						FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
					}
				}))
			);

			Section.AddMenuEntry(
				TEXT("ShowFileInExplorer"),
				LOCTEXT("ShowFileInExplorer", "Show in Explorer"),
				LOCTEXT("ShowFileInExplorerTooltip", "Opens the containing folder and highlights this file in Windows Explorer."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([SelectedFilePaths]()
				{
					for (const FString& Path : SelectedFilePaths)
					{
						FPlatformProcess::ExploreFolder(*Path);
					}
				}))
			);
		}));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FProjectFileBrowserModule, ProjectFileBrowser);
