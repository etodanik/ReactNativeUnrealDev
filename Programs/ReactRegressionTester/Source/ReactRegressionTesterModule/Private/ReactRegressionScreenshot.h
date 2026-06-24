// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SReactSurface;

namespace ReactRegressionScreenshot
{
	bool SaveSurfaceToPng(
		const TSharedRef<SReactSurface>& Surface,
		const FString& OutputPath,
		FString& OutError,
		FIntPoint* OutSize = nullptr,
		const FIntPoint* TargetSize = nullptr);
}
