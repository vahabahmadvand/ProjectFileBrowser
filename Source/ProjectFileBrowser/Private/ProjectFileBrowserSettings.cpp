// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

#include "ProjectFileBrowserSettings.h"

#define LOCTEXT_NAMESPACE "ProjectFileBrowser"

UProjectFileBrowserSettings::FOnSettingsChanged UProjectFileBrowserSettings::OnSettingsChanged;

UProjectFileBrowserSettings::UProjectFileBrowserSettings()
{
	CategoryName = TEXT("Editor");
	SectionName = TEXT("ProjectFileBrowser");

	// Add sensible defaults
	MonitoredExtensions.Add(FProjectFileExtensionConfig(
		TEXT("png"),
		LOCTEXT("PNGLabel", "PNG Image"),
		FLinearColor(0.2f, 0.7f, 1.0f),
		true
	));

	MonitoredExtensions.Add(FProjectFileExtensionConfig(
		TEXT("jpg"),
		LOCTEXT("JPGLabel", "JPG Image"),
		FLinearColor(0.9f, 0.6f, 0.1f),
		true
	));

	MonitoredExtensions.Add(FProjectFileExtensionConfig(
		TEXT("jpeg"),
		LOCTEXT("JPEGLabel", "JPEG Image"),
		FLinearColor(0.9f, 0.6f, 0.1f),
		true
	));

	MonitoredExtensions.Add(FProjectFileExtensionConfig(
		TEXT("json"),
		LOCTEXT("JSONLabel", "JSON Data"),
		FLinearColor(0.3f, 0.8f, 0.3f),
		false
	));

	MonitoredExtensions.Add(FProjectFileExtensionConfig(
		TEXT("txt"),
		LOCTEXT("TXTLabel", "Text Document"),
		FLinearColor(0.6f, 0.6f, 0.6f),
		false
	));
}

#if WITH_EDITOR
void UProjectFileBrowserSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnSettingsChanged.Broadcast();
}
#endif

const FProjectFileExtensionConfig* UProjectFileBrowserSettings::FindExtensionConfig(const FString& InExtension) const
{
	const FString CleanExt = InExtension.TrimStartAndEnd().ToLower().Replace(TEXT("."), TEXT(""));
	if (CleanExt.IsEmpty())
	{
		return nullptr;
	}

	for (const FProjectFileExtensionConfig& Item : MonitoredExtensions)
	{
		const FString ItemExt = Item.Extension.TrimStartAndEnd().ToLower().Replace(TEXT("."), TEXT(""));
		if (ItemExt.Equals(CleanExt))
		{
			return &Item;
		}
	}
	return nullptr;
}

bool UProjectFileBrowserSettings::IsExtensionMonitored(const FString& InExtension) const
{
	return FindExtensionConfig(InExtension) != nullptr;
}

#undef LOCTEXT_NAMESPACE
