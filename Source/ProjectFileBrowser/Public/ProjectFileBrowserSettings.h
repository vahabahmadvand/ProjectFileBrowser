// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectFileBrowserSettings.generated.h"

USTRUCT(BlueprintType)
struct PROJECTFILEBROWSER_API FProjectFileExtensionConfig
{
	GENERATED_BODY()

	/** Extension without leading dot (e.g. "png", "jpg", "json", "txt", "md") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Extension")
	FString Extension;

	/** Display label in the Content Browser */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Extension")
	FText DisplayName;

	/** Color bar / accent color in the Content Browser tile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Extension")
	FLinearColor AccentColor = FLinearColor(0.2f, 0.7f, 1.0f);

	/** If true, and the file is an image (PNG, JPG, etc.), attempt to render its content as the thumbnail preview */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Extension")
	bool bGenerateImageThumbnail = true;

	FProjectFileExtensionConfig()
	{
	}

	FProjectFileExtensionConfig(const FString& InExtension, const FText& InDisplayName, const FLinearColor& InColor, bool bInGenerateImageThumbnail)
		: Extension(InExtension)
		, DisplayName(InDisplayName)
		, AccentColor(InColor)
		, bGenerateImageThumbnail(bInGenerateImageThumbnail)
	{
	}
};

/**
 * Settings for Project File Browser plugin.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Project File Browser"))
class PROJECTFILEBROWSER_API UProjectFileBrowserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectFileBrowserSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Editor"); }
	virtual FName GetSectionName() const override { return TEXT("ProjectFileBrowser"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Event triggered when settings change to allow data source re-synchronization */
	DECLARE_MULTICAST_DELEGATE(FOnSettingsChanged);
	static FOnSettingsChanged OnSettingsChanged;

	/** Whether the custom file data source is enabled */
	UPROPERTY(config, EditAnywhere, Category = "General")
	bool bEnableFileBrowser = true;

	/** List of file extensions to show inside the Content Browser */
	UPROPERTY(config, EditAnywhere, Category = "Extensions")
	TArray<FProjectFileExtensionConfig> MonitoredExtensions;

	/** Returns the configuration for a given extension, or nullptr if not monitored */
	const FProjectFileExtensionConfig* FindExtensionConfig(const FString& InExtension) const;

	/** Checks if an extension is supported */
	bool IsExtensionMonitored(const FString& InExtension) const;
};
