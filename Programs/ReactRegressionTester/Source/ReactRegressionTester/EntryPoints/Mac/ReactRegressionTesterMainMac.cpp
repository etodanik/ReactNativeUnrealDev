// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTester/ReactRegressionTesterApp.h"
#include "Mac/MacProgramDelegate.h"
#include "LaunchEngineLoop.h"

int main(int argc, char *argv[])
{
	return [MacProgramDelegate mainWithArgc:argc argv:argv programMain:RunReactRegressionTester programExit:FEngineLoop::AppExit];
}
