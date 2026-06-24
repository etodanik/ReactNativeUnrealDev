// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionScreenshot.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ReactSurface.h"

namespace ReactRegressionScreenshot
{
bool SaveSurfaceToPng(const TSharedRef<SReactSurface>& Surface, const FString& OutputPath, FString& OutError)
{
	TArray<FColor> Pixels;
	FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(Surface, Pixels, Size))
	{
		OutError = TEXT("Slate could not capture the React surface. Make sure it is attached to a real window.");
		return false;
	}

	if (Size.X <= 0 || Size.Y <= 0)
	{
		OutError = FString::Printf(TEXT("Captured invalid image size %dx%d."), Size.X, Size.Y);
		return false;
	}

	const int64 ExpectedPixelCount = static_cast<int64>(Size.X) * static_cast<int64>(Size.Y);
	if (Pixels.Num() < ExpectedPixelCount)
	{
		OutError = FString::Printf(
			TEXT("Captured %d pixels, expected at least %lld for %dx%d."),
			Pixels.Num(),
			ExpectedPixelCount,
			Size.X,
			Size.Y);
		return false;
	}

	FImageCore::SetAlphaOpaque(FImageView(Pixels.GetData(), Size.X, Size.Y));

	TArray64<uint8> PngBytes;
	IImageWrapperModule& ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!ImageWrapper.CompressImage(PngBytes, EImageFormat::PNG, FImageView(Pixels.GetData(), Size.X, Size.Y)))
	{
		OutError = TEXT("Failed to encode React surface screenshot as PNG.");
		return false;
	}

	const FString OutputDir = FPaths::GetPath(OutputPath);
	if (!IFileManager::Get().MakeDirectory(*OutputDir, true))
	{
		OutError = FString::Printf(TEXT("Failed to create screenshot directory: %s"), *OutputDir);
		return false;
	}

	if (!FFileHelper::SaveArrayToFile(PngBytes, *OutputPath))
	{
		OutError = FString::Printf(TEXT("Failed to write screenshot: %s"), *OutputPath);
		return false;
	}

	return true;
}
}
