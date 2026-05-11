// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModuleCommands.h"

#define LOCTEXT_NAMESPACE "FReactRegressionTesterModuleModule"

void FReactRegressionTesterModuleCommands::RegisterCommands()
{
	UI_COMMAND(OpenMainWindow, "ReactRegressionTesterModule", "Bring up ReactRegressionTesterModule window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
