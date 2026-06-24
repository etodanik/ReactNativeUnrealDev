// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Input/Reply.h"
#include "Types/SlateEnums.h"
#include "Containers/Ticker.h"
#include "RNUETestHarness/Events.h"

class SReactSurface;
class FUICommandList;
class SDockTab;
class FSpawnTabArgs;
class SEditableTextBox;
class STextBlock;
class SWidgetSwitcher;
class SButton;
class SWidget;
class SBox;
class STableViewBase;
class SWindow;
class ITableRow;
template <typename OptionType> class SComboBox;

enum class EReactTesterConnectionState : uint8
{
	Disconnected,
	Connecting,
	Connected,
};

enum class EReactRegressionAutomationStage : uint8
{
	Disabled,
	WaitingForManifest,
	PreparingVariant,
	WaitingForReady,
	Settling,
	Complete,
	Failed,
};

struct FReactRegressionScenarioVariant
{
	FString Id;
	FString BaseId;
	FString Title;
	FString AppName;
	FString SizeName;
	FString Background;
	TArray<FString> Tags;
	int32 Width = 0;
	int32 Height = 0;
	int32 SettleFrames = 0;
};

struct FReactRegressionCaptureResult
{
	FReactRegressionScenarioVariant Scenario;
	FString Status;
	FString OutputPath;
	FString Message;
	double StartedAtSeconds = 0.0;
	double EndedAtSeconds = 0.0;
};

class FReactRegressionTesterModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command (by default it will bring up application window) */
	void AppStarted();

private:
	TSharedRef<SDockTab> OnSpawnMainTab(const FSpawnTabArgs& SpawnTabArgs);

	TSharedRef<SWidget> BuildAddressBar();
	TSharedRef<SWidget> BuildModulesTab();
	TSharedRef<SWidget> BuildTestsTab();

	TSharedRef<ITableRow> OnGenerateTestRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnTestSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);

	TSharedRef<SWidget> OnGenerateModuleItem(TSharedPtr<FString> Item);
	void OnModuleSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedModuleText() const;

	FReply OnConnectClicked();
	FReply OnRunClicked();
	FReply OnScreenshotClicked();
	void OnTabValueChanged(int32 NewIndex);

	void SetConnectionState(EReactTesterConnectionState NewState);
	bool CanTakeScreenshot() const;
	bool ParseAddress(const FString& Input, FString& OutHost, uint32& OutPort) const;
	void OnBundleAlive();
	void RefreshModuleList();
	void ConnectFlow();
	bool OnRetryTick(float DeltaTime);
	void InitializeAutomationFromCommandLine();
	bool LoadAutomationManifest(FString& OutError);
	void StartAutomation();
	bool OnAutomationTick(float DeltaTime);
	void StartNextAutomationVariant();
	void EnsureAutomationWindow(const FReactRegressionScenarioVariant& Scenario);
	void CaptureCurrentAutomationVariant();
	void CompleteAutomation(bool bSuccess, const FString& Message);
	void WriteAutomationOutputs();
	void WriteAutomationManifestCopy();
	bool ParseMetroUrl(const FString& Input, FString& OutHost, uint32& OutPort) const;
	bool ValidateHarnessManifest(FString& OutError) const;
	void OnHarnessManifestPublished(const TArray<struct RNUETestHarness::FScenarioMeta>& Scenarios);
	void OnHarnessScenarioReady(const FString& Id);
	void OnHarnessScenarioFailed(const FString& Id, const FString& Message);

private:
	TSharedPtr<FUICommandList> AppCommands;
	EReactTesterConnectionState ConnectionState = EReactTesterConnectionState::Disconnected;
	FDelegateHandle BundleAliveHandle;
	FTSTicker::FDelegateHandle RetryTickerHandle;
	FTSTicker::FDelegateHandle AutomationTickerHandle;
	FDelegateHandle HarnessManifestHandle;
	FDelegateHandle HarnessReadyHandle;
	FDelegateHandle HarnessFailedHandle;

	TWeakPtr<SEditableTextBox> AddressTextBox;
	TWeakPtr<STextBlock> StatusText;
	TWeakPtr<SButton> ConnectButton;
	TWeakPtr<SButton> ScreenshotButton;
	TWeakPtr<STextBlock> ScreenshotStatusText;
	TWeakPtr<SWidgetSwitcher> TabSwitcher;

	TWeakPtr<SReactSurface> ReactSurface;
	TWeakPtr<SComboBox<TSharedPtr<FString>>> ModuleComboBox;
	TArray<TSharedPtr<FString>> ModuleItems;
	TSharedPtr<FString> SelectedModule;
	FString LastRunModuleName;
	bool bModuleQueryInFlight = false;

	TArray<TSharedPtr<FString>> TestItems;
	TSharedPtr<FString> SelectedTest;

	bool bAutomationEnabled = false;
	bool bHarnessManifestPublished = false;
	EReactRegressionAutomationStage AutomationStage = EReactRegressionAutomationStage::Disabled;
	FString AutomationManifestPath;
	FString AutomationOutRoot;
	FString AutomationMetroUrl;
	FString AutomationScenarioFilter;
	uint32 AutomationTimeoutMs = 30000;
	int32 AutomationCurrentIndex = INDEX_NONE;
	int32 AutomationFrameWait = 0;
	double AutomationStageStartedAtSeconds = 0.0;
	double AutomationVariantStartedAtSeconds = 0.0;
	TArray<FReactRegressionScenarioVariant> AutomationManifest;
	TArray<FReactRegressionScenarioVariant> AutomationQueue;
	TArray<FReactRegressionCaptureResult> AutomationResults;
	TSet<FString> HarnessReadyIds;
	TMap<FString, FString> HarnessFailedMessages;
	TArray<RNUETestHarness::FScenarioMeta> HarnessPublishedScenarios;
	TSharedPtr<SWindow> AutomationWindow;
	TSharedPtr<SBox> AutomationSurfaceBox;
	TSharedPtr<SReactSurface> AutomationReactSurface;
};
