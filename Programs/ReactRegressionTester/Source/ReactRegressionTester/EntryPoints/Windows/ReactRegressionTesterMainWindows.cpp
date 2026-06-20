// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTesterApp.h"
#include "Misc/CommandLine.h"
#include "Windows/WindowsHWrapper.h"

int WINAPI WinMain( _In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int nCmdShow )
{
	hInstance = hInInstance;
	return RunReactRegressionTester(FCommandLine::RemoveExeName(::GetCommandLineW()));
}
