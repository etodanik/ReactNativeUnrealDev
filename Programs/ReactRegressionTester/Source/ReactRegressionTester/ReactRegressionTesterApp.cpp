// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTesterApp.h"
#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModule.h"

#include "Runtime/Launch/Public/RequiredProgramMainCPPInclude.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Styling/StarshipCoreStyle.h"

#include "React.h"

IMPLEMENT_APPLICATION(ReactRegressionTester, "ReactRegressionTester");

#define LOCTEXT_NAMESPACE "ReactRegressionTester"

namespace WorkspaceMenu
{
TSharedRef<FWorkspaceItem> DeveloperMenu = FWorkspaceItem::NewGroup(LOCTEXT("DeveloperMenu", "Developer"));
}

int RunReactRegressionTester(const TCHAR* CommandLine)
{
	FTaskTagScope TaskTagScope(ETaskTag::EGameThread);

	// start up the main loop
	GEngineLoop.PreInit(CommandLine);

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();

	// Tell the module manager it may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());
	FSlateApplication::InitHighDPI(true);
	FGlobalTabmanager::Get()->SetApplicationTitle(LOCTEXT("AppTitle", "React Regression Tester"));
	FAppStyle::SetAppStyleSetName(FStarshipCoreStyle::GetCoreStyle().GetStyleSetName());

	// launch the main window of the ReactRegressionTester module
	FReactRegressionTesterModule& ReactRegressionTesterModule = FModuleManager::LoadModuleChecked<FReactRegressionTesterModule>(FName("ReactRegressionTesterModule"));

	// FReactManager initialization is driven by the address bar in the Modules tab.
	// See FReactRegressionTesterModule::OnConnectClicked.
	ReactRegressionTesterModule.AppStarted();

	// loop while the server does the rest
	while (!IsEngineExitRequested())
	{
		BeginExitIfRequested();

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		UE::Stats::FStats::AdvanceFrame(false);
		FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
		React::Tick();
		FPlatformProcess::Sleep(0.01);

		GFrameCounter++;
	}

	FCoreDelegates::OnExit.Broadcast();
	FSlateApplication::Shutdown();
	FModuleManager::Get().UnloadModulesAtShutdown();

	GEngineLoop.AppPreExit();
	GEngineLoop.AppExit();

	return 0;
}

#undef LOCTEXT_NAMESPACE
