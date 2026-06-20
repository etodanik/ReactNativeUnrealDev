// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTester/ReactRegressionTesterApp.h"
#include "Mac/MacProgramDelegate.h"

static void ReactRegressionTesterExit()
{
}

int main(int argc, char *argv[])
{
	return [MacProgramDelegate mainWithArgc:argc argv:argv programMain:RunReactRegressionTester programExit:ReactRegressionTesterExit];
}
