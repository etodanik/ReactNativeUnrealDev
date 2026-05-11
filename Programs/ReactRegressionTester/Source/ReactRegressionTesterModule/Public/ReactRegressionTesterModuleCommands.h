// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ReactRegressionTesterModuleStyle.h"

class FReactRegressionTesterModuleCommands : public TCommands<FReactRegressionTesterModuleCommands>
{
public:

	FReactRegressionTesterModuleCommands()
		: TCommands<FReactRegressionTesterModuleCommands>(TEXT("ReactRegressionTesterModule"), NSLOCTEXT("Contexts", "ReactRegressionTesterModule", "ReactRegressionTesterModule application"), NAME_None, FReactRegressionTesterModuleStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenMainWindow;
};