// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UProjectFileBrowserDataSource;

class FProjectFileBrowserModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void InitializeDataSource();
	void ShutdownDataSource();
	void OnPostEngineInit();

	void OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath);
	void OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath);

	void RegisterContextMenuExtensions();

	TStrongObjectPtr<UProjectFileBrowserDataSource> FileDataSource;
	bool bDataSourceInitialized = false;
};
