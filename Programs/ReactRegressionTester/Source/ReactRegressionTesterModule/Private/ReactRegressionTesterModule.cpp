// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModule.h"
#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModuleStyle.h"
#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModuleCommands.h"

#include "ReactRegressionScreenshot.h"
#include "React.h"
#include "ReactSurface.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"

static const FName ReactRegressionTesterModuleTabName("ReactRegressionTesterModule");

#define LOCTEXT_NAMESPACE "FReactRegressionTesterModule"

namespace
{
constexpr uint32 DefaultAutomationTimeoutMs = 30000;
constexpr int32 AutomationPrepareFrames = 2;

FString GetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
{
	FString Value;
	Object->TryGetStringField(FieldName, Value);
	return Value;
}

bool ReadIntField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	int32& OutValue,
	FString& OutError,
	bool bRequired = true,
	int32 DefaultValue = 0)
{
	const TSharedPtr<FJsonValue> Field = Object->TryGetField(FieldName);
	if (!Field.IsValid() || Field->Type == EJson::Null)
	{
		if (!bRequired)
		{
			OutValue = DefaultValue;
			return true;
		}

		OutError = FString::Printf(TEXT("Scenario manifest entry is missing numeric field '%s'."), FieldName);
		return false;
	}

	double Value = 0.0;
	if (!Field->TryGetNumber(Value))
	{
		OutError = FString::Printf(TEXT("Scenario manifest field '%s' must be an integer."), FieldName);
		return false;
	}

	const int32 IntValue = static_cast<int32>(Value);
	if (!FMath::IsFinite(Value)
		|| Value < static_cast<double>(MIN_int32)
		|| Value > static_cast<double>(MAX_int32)
		|| static_cast<double>(IntValue) != Value)
	{
		OutError = FString::Printf(TEXT("Scenario manifest field '%s' must be an integer."), FieldName);
		return false;
	}

	OutValue = IntValue;
	return true;
}

FString DeriveBaseId(const FString& VariantId)
{
	FString BaseId;
	FString Unused;
	if (VariantId.Split(TEXT("__"), &BaseId, &Unused, ESearchCase::CaseSensitive, ESearchDir::FromStart))
	{
		return BaseId;
	}
	return VariantId;
}

bool ReadScenarioVariant(const TSharedPtr<FJsonObject>& Object, FReactRegressionScenarioVariant& OutScenario, FString& OutError)
{
	OutScenario.Id = GetStringField(Object, TEXT("id"));
	OutScenario.BaseId = GetStringField(Object, TEXT("baseId"));
	OutScenario.Title = GetStringField(Object, TEXT("title"));
	OutScenario.AppName = GetStringField(Object, TEXT("appName"));
	OutScenario.SizeName = GetStringField(Object, TEXT("sizeName"));
	OutScenario.Background = GetStringField(Object, TEXT("background"));
	if (!ReadIntField(Object, TEXT("width"), OutScenario.Width, OutError)
		|| !ReadIntField(Object, TEXT("height"), OutScenario.Height, OutError)
		|| !ReadIntField(Object, TEXT("settleFrames"), OutScenario.SettleFrames, OutError, false))
	{
		return false;
	}

	if (OutScenario.BaseId.IsEmpty())
	{
		OutScenario.BaseId = DeriveBaseId(OutScenario.Id);
	}
	if (OutScenario.SizeName.IsEmpty() && OutScenario.Width > 0 && OutScenario.Height > 0)
	{
		OutScenario.SizeName = FString::Printf(TEXT("%dx%d"), OutScenario.Width, OutScenario.Height);
	}

	const TSharedPtr<FJsonValue> TagsValue = Object->TryGetField(TEXT("tags"));
	if (TagsValue.IsValid() && TagsValue->Type != EJson::Null)
	{
		if (TagsValue->Type != EJson::Array)
		{
			OutError = FString::Printf(TEXT("Scenario '%s' has non-array tags."), *OutScenario.Id);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& Tags = TagsValue->AsArray();
		for (int32 TagIndex = 0; TagIndex < Tags.Num(); ++TagIndex)
		{
			const TSharedPtr<FJsonValue>& TagValue = Tags[TagIndex];
			FString Tag;
			if (!TagValue.IsValid() || !TagValue->TryGetString(Tag))
			{
				OutError = FString::Printf(TEXT("Scenario '%s' has non-string tag at index %d."), *OutScenario.Id, TagIndex);
				return false;
			}
			OutScenario.Tags.Add(MoveTemp(Tag));
		}
	}

	if (OutScenario.Id.IsEmpty())
	{
		OutError = TEXT("Scenario manifest entry is missing id.");
		return false;
	}
	if (OutScenario.AppName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Scenario '%s' is missing appName."), *OutScenario.Id);
		return false;
	}
	if (OutScenario.Width <= 0 || OutScenario.Height <= 0)
	{
		OutError = FString::Printf(
			TEXT("Scenario '%s' has invalid dimensions %dx%d."),
			*OutScenario.Id,
			OutScenario.Width,
			OutScenario.Height);
		return false;
	}
	if (OutScenario.SettleFrames < 0)
	{
		OutError = FString::Printf(TEXT("Scenario '%s' has invalid settleFrames %d."), *OutScenario.Id, OutScenario.SettleFrames);
		return false;
	}

	return true;
}

TSharedRef<FJsonObject> ScenarioToJson(const FReactRegressionScenarioVariant& Scenario)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("id"), Scenario.Id);
	Object->SetStringField(TEXT("baseId"), Scenario.BaseId);
	Object->SetStringField(TEXT("title"), Scenario.Title);
	Object->SetStringField(TEXT("appName"), Scenario.AppName);
	Object->SetNumberField(TEXT("width"), Scenario.Width);
	Object->SetNumberField(TEXT("height"), Scenario.Height);
	Object->SetStringField(TEXT("sizeName"), Scenario.SizeName);
	if (Scenario.SettleFrames > 0)
	{
		Object->SetNumberField(TEXT("settleFrames"), Scenario.SettleFrames);
	}
	if (!Scenario.Background.IsEmpty())
	{
		Object->SetStringField(TEXT("background"), Scenario.Background);
	}
	if (!Scenario.Tags.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Tags;
		Tags.Reserve(Scenario.Tags.Num());
		for (const FString& Tag : Scenario.Tags)
		{
			Tags.Add(MakeShared<FJsonValueString>(Tag));
		}
		Object->SetArrayField(TEXT("tags"), MoveTemp(Tags));
	}
	return Object;
}

FString AutomationStageToString(EReactRegressionAutomationStage Stage)
{
	switch (Stage)
	{
	case EReactRegressionAutomationStage::WaitingForManifest:
		return TEXT("waiting-for-manifest");
	case EReactRegressionAutomationStage::PreparingVariant:
		return TEXT("preparing-variant");
	case EReactRegressionAutomationStage::WaitingForReady:
		return TEXT("waiting-for-ready");
	case EReactRegressionAutomationStage::Settling:
		return TEXT("settling");
	case EReactRegressionAutomationStage::Complete:
		return TEXT("complete");
	case EReactRegressionAutomationStage::Failed:
		return TEXT("failed");
	case EReactRegressionAutomationStage::Disabled:
	default:
		return TEXT("disabled");
	}
}
} // namespace

void FReactRegressionTesterModule::StartupModule()
{
	FReactRegressionTesterModuleStyle::Initialize();
	FReactRegressionTesterModuleStyle::ReloadTextures();

	FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("RNUETestHarness"));

	FReactRegressionTesterModuleCommands::Register();
	AppCommands = MakeShareable(new FUICommandList);
	AppCommands->MapAction(
		FReactRegressionTesterModuleCommands::Get().OpenMainWindow,
		FExecuteAction::CreateRaw(this, &FReactRegressionTesterModule::AppStarted),
		FCanExecuteAction());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ReactRegressionTesterModuleTabName, FOnSpawnTab::CreateRaw(this, &FReactRegressionTesterModule::OnSpawnMainTab))
		.SetDisplayName(LOCTEXT("FReactRegressionTesterModuleTabTitle", "React Regression Tester"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	BundleAliveHandle = React::AddOnBundleAlive(FSimpleDelegate::CreateRaw(
		this, &FReactRegressionTesterModule::OnBundleAlive));

	HarnessManifestHandle = RNUETestHarness::FEvents::OnManifestPublished.AddRaw(
		this, &FReactRegressionTesterModule::OnHarnessManifestPublished);
	HarnessReadyHandle = RNUETestHarness::FEvents::OnScenarioReady.AddRaw(
		this, &FReactRegressionTesterModule::OnHarnessScenarioReady);
	HarnessFailedHandle = RNUETestHarness::FEvents::OnScenarioFailed.AddRaw(
		this, &FReactRegressionTesterModule::OnHarnessScenarioFailed);

	InitializeAutomationFromCommandLine();
}

void FReactRegressionTesterModule::ShutdownModule()
{
	if (RetryTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RetryTickerHandle);
		RetryTickerHandle.Reset();
	}

	if (BundleAliveHandle.IsValid())
	{
		React::RemoveOnBundleAlive(BundleAliveHandle);
		BundleAliveHandle.Reset();
	}

	if (AutomationTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutomationTickerHandle);
		AutomationTickerHandle.Reset();
	}
	if (HarnessManifestHandle.IsValid())
	{
		RNUETestHarness::FEvents::OnManifestPublished.Remove(HarnessManifestHandle);
		HarnessManifestHandle.Reset();
	}
	if (HarnessReadyHandle.IsValid())
	{
		RNUETestHarness::FEvents::OnScenarioReady.Remove(HarnessReadyHandle);
		HarnessReadyHandle.Reset();
	}
	if (HarnessFailedHandle.IsValid())
	{
		RNUETestHarness::FEvents::OnScenarioFailed.Remove(HarnessFailedHandle);
		HarnessFailedHandle.Reset();
	}

	AutomationReactSurface.Reset();
	AutomationSurfaceBox.Reset();
	AutomationWindow.Reset();

	LastRunModuleName.Reset();
	ReactSurface.Reset();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ReactRegressionTesterModuleTabName);

	AppCommands.Reset();

	FReactRegressionTesterModuleCommands::Unregister();

	FReactRegressionTesterModuleStyle::Shutdown();
}

TSharedRef<SDockTab> FReactRegressionTesterModule::OnSpawnMainTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SWidgetSwitcher> TabSwitcherRef =
		SNew(SWidgetSwitcher)
		.WidgetIndex(0)

		+ SWidgetSwitcher::Slot()
			[BuildModulesTab()]

		+ SWidgetSwitcher::Slot()
			[BuildTestsTab()];
	TabSwitcher = TabSwitcherRef;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("MainTabLabel", "React Regression Tester"))
			[SNew(SVerticalBox)

				+ SVerticalBox::Slot()
					.AutoHeight()
						[BuildAddressBar()]

				+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 4.0f, 4.0f, 0.0f)
						[SNew(SSegmentedControl<int32>)
								.OnValueChanged_Raw(this, &FReactRegressionTesterModule::OnTabValueChanged)
								.Value(0)

							+ SSegmentedControl<int32>::Slot(0)
								.Text(LOCTEXT("ModulesTab", "Modules"))

							+ SSegmentedControl<int32>::Slot(1)
								.Text(LOCTEXT("TestsTab", "Tests"))]

				+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(4.0f)
						[TabSwitcherRef]];
}

TSharedRef<SWidget> FReactRegressionTesterModule::BuildAddressBar()
{
	TSharedRef<SEditableTextBox> AddressTextBoxRef =
		SNew(SEditableTextBox)
		.Text(FText::FromString(TEXT("localhost:8081")))
		.HintText(LOCTEXT("AddressHint", "host:port"));
	AddressTextBox = AddressTextBoxRef;

	TSharedRef<SButton> ConnectButtonRef =
		SNew(SButton)
		.Text(LOCTEXT("ConnectButton", "Connect"))
		.OnClicked_Raw(this, &FReactRegressionTesterModule::OnConnectClicked);
	ConnectButton = ConnectButtonRef;

	TSharedRef<STextBlock> StatusTextRef =
		SNew(STextBlock)
		.Text(LOCTEXT("StatusDisconnected", "Disconnected"));
	StatusText = StatusTextRef;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.0f)
			[SNew(SVerticalBox)

				+ SVerticalBox::Slot()
					.AutoHeight()
						[SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(4.0f, 0.0f)
									[SNew(STextBlock)
											.Text(LOCTEXT("AddressLabel", "Dev server:"))]

							+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								.Padding(4.0f, 0.0f)
									[AddressTextBoxRef]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(4.0f, 0.0f)
									[ConnectButtonRef]]

				+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 4.0f, 4.0f, 0.0f)
						[StatusTextRef]];
}

TSharedRef<SWidget> FReactRegressionTesterModule::BuildModulesTab()
{
	TSharedRef<SComboBox<TSharedPtr<FString>>> ModuleComboBoxRef =
		SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&ModuleItems)
		.OnGenerateWidget_Raw(this, &FReactRegressionTesterModule::OnGenerateModuleItem)
		.OnSelectionChanged_Raw(this, &FReactRegressionTesterModule::OnModuleSelectionChanged)
			[SNew(STextBlock)
					.Text_Raw(this, &FReactRegressionTesterModule::GetSelectedModuleText)];
	ModuleComboBox = ModuleComboBoxRef;

	TSharedRef<SReactSurface> ReactSurfaceRef = SNew(SReactSurface);
	ReactSurface = ReactSurfaceRef;

	TSharedRef<SButton> ScreenshotButtonRef =
		SNew(SButton)
		.Text(LOCTEXT("ScreenshotButton", "Screenshot"))
		.IsEnabled_Raw(this, &FReactRegressionTesterModule::CanTakeScreenshot)
		.OnClicked_Raw(this, &FReactRegressionTesterModule::OnScreenshotClicked);
	ScreenshotButton = ScreenshotButtonRef;

	TSharedRef<STextBlock> ScreenshotStatusTextRef =
		SNew(STextBlock)
		.Text(FText::GetEmpty());
	ScreenshotStatusText = ScreenshotStatusTextRef;

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
							[SNew(STextBlock)
									.Text(LOCTEXT("ModuleLabel", "Module:"))]

					+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
							[ModuleComboBoxRef]

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
							[SNew(SButton)
									.Text(LOCTEXT("RunButton", "Run"))
									.OnClicked_Raw(this, &FReactRegressionTesterModule::OnRunClicked)]

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
							[ScreenshotButtonRef]]

		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 0.0f, 4.0f, 4.0f)
				[ScreenshotStatusTextRef]

		+ SVerticalBox::Slot()
			.FillHeight(1.0f)
				[SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(4.0f)
							[ReactSurfaceRef]];
}

TSharedRef<SWidget> FReactRegressionTesterModule::BuildTestsTab()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.0f)
			[SNew(SVerticalBox)

				+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f)
						[SNew(STextBlock)
								.Text(LOCTEXT("TestListHeader", "Test Cases"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]

				+ SVerticalBox::Slot()
					.FillHeight(1.0f)
						[SNew(SListView<TSharedPtr<FString>>)
								.ListItemsSource(&TestItems)
								.OnGenerateRow_Raw(this, &FReactRegressionTesterModule::OnGenerateTestRow)
								.OnSelectionChanged_Raw(this, &FReactRegressionTesterModule::OnTestSelectionChanged)]];
}

TSharedRef<ITableRow> FReactRegressionTesterModule::OnGenerateTestRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Padding(FMargin(4.0f, 2.0f))
			[SNew(STextBlock)
					.Text(FText::FromString(*Item))];
}

void FReactRegressionTesterModule::OnTestSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
{
	SelectedTest = SelectedItem;
}

TSharedRef<SWidget> FReactRegressionTesterModule::OnGenerateModuleItem(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty());
}

void FReactRegressionTesterModule::OnModuleSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
{
	SelectedModule = SelectedItem;
}

FText FReactRegressionTesterModule::GetSelectedModuleText() const
{
	if (SelectedModule.IsValid())
	{
		return FText::FromString(*SelectedModule);
	}
	return LOCTEXT("SelectModulePlaceholder", "Select a module...");
}

FReply FReactRegressionTesterModule::OnConnectClicked()
{
	if (ConnectionState == EReactTesterConnectionState::Disconnected)
	{
		ConnectFlow();
	}
	else
	{
		React::Shutdown();
		ModuleItems.Reset();
		SelectedModule.Reset();
		LastRunModuleName.Reset();
		if (TSharedPtr<SComboBox<TSharedPtr<FString>>> ModuleComboBoxPtr = ModuleComboBox.Pin())
		{
			ModuleComboBoxPtr->RefreshOptions();
			ModuleComboBoxPtr->ClearSelection();
		}
		if (TSharedPtr<STextBlock> ScreenshotStatusTextPtr = ScreenshotStatusText.Pin())
		{
			ScreenshotStatusTextPtr->SetText(FText::GetEmpty());
		}
		SetConnectionState(EReactTesterConnectionState::Disconnected);
	}
	return FReply::Handled();
}

void FReactRegressionTesterModule::ConnectFlow()
{
	LastRunModuleName.Reset();
	if (TSharedPtr<STextBlock> ScreenshotStatusTextPtr = ScreenshotStatusText.Pin())
	{
		ScreenshotStatusTextPtr->SetText(FText::GetEmpty());
	}

	const TSharedPtr<SEditableTextBox> AddressTextBoxPtr = AddressTextBox.Pin();
	const FString Input = AddressTextBoxPtr.IsValid() ? AddressTextBoxPtr->GetText().ToString() : FString();

	FString Host;
	uint32 Port = 0;
	if (!ParseAddress(Input, Host, Port))
	{
		if (TSharedPtr<STextBlock> StatusTextPtr = StatusText.Pin())
		{
			StatusTextPtr->SetText(FText::Format(
				LOCTEXT("StatusBadAddress", "Invalid address: '{0}' (expected host:port)"),
				FText::FromString(Input)));
		}
		return;
	}

	SetConnectionState(EReactTesterConnectionState::Connecting);

	const FTCHARToUTF8 HostUtf8(*Host);
	React::Reconnect("ReactRegressionTester", "Unreal", HostUtf8.Get(), Port);
	React::LoadScript();
}

bool FReactRegressionTesterModule::OnRetryTick(float DeltaTime)
{
	if (ConnectionState == EReactTesterConnectionState::Connecting)
	{
		if (!React::IsAvailable())
		{
			// Initialize never succeeded (e.g., Metro unreachable). Retry.
			ConnectFlow();
		}
		else
		{
			// Manager is up but OnBundleAlive hasn't fired - possibly the bundle
			// evaluated without calling runApplication (e.g., on a reconnect where
			// Metro served a different/lazy bundle). Try populating the dropdown
			// directly via AppRegistry.getAppKeys(); RefreshModuleList will flip
			// state to Connected if any keys are registered.
			RefreshModuleList();
		}
	}
	return true; // keep ticker alive
}

FReply FReactRegressionTesterModule::OnRunClicked()
{
	if (ConnectionState != EReactTesterConnectionState::Connected)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Run clicked while not connected"));
		return FReply::Handled();
	}
	if (!SelectedModule.IsValid() || SelectedModule->IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Run clicked with no module selected"));
		return FReply::Handled();
	}
	TSharedPtr<SReactSurface> ReactSurfacePtr = ReactSurface.Pin();
	if (!ReactSurfacePtr.IsValid())
	{
		return FReply::Handled();
	}

	React::RunApplication(*ReactSurfacePtr, *SelectedModule);
	LastRunModuleName = *SelectedModule;
	if (TSharedPtr<STextBlock> ScreenshotStatusTextPtr = ScreenshotStatusText.Pin())
	{
		ScreenshotStatusTextPtr->SetText(FText::Format(
			LOCTEXT("ScreenshotReady", "Screenshot ready for {0}"),
			FText::FromString(LastRunModuleName)));
	}
	return FReply::Handled();
}

FReply FReactRegressionTesterModule::OnScreenshotClicked()
{
	if (!CanTakeScreenshot())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Screenshot clicked before a module was run"));
		return FReply::Handled();
	}

	TSharedPtr<SReactSurface> ReactSurfacePtr = ReactSurface.Pin();
	if (!ReactSurfacePtr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Screenshot clicked with no valid React surface"));
		return FReply::Handled();
	}

	const FString SanitizedModuleName = FPaths::MakeValidFileName(LastRunModuleName, TCHAR('_'));
	const FString ScreenshotModuleName = SanitizedModuleName.IsEmpty() ? TEXT("ReactSurface") : SanitizedModuleName;
	const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"));
	const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("ReactRegressionTester");
	const FString OutputPath = OutputDir / FString::Printf(TEXT("%s-%s.png"), *ScreenshotModuleName, *Timestamp);

	FString Error;
	if (ReactRegressionScreenshot::SaveSurfaceToPng(ReactSurfacePtr.ToSharedRef(), OutputPath, Error))
	{
		UE_LOG(LogTemp, Display, TEXT("[ReactRegressionTester] Saved screenshot: %s"), *OutputPath);
		if (TSharedPtr<STextBlock> ScreenshotStatusTextPtr = ScreenshotStatusText.Pin())
		{
			ScreenshotStatusTextPtr->SetText(FText::Format(
				LOCTEXT("ScreenshotSaved", "Saved screenshot: {0}"),
				FText::FromString(OutputPath)));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Failed to save screenshot: %s"), *Error);
		if (TSharedPtr<STextBlock> ScreenshotStatusTextPtr = ScreenshotStatusText.Pin())
		{
			ScreenshotStatusTextPtr->SetText(FText::Format(
				LOCTEXT("ScreenshotFailed", "Screenshot failed: {0}"),
				FText::FromString(Error)));
		}
	}

	return FReply::Handled();
}

void FReactRegressionTesterModule::OnTabValueChanged(int32 NewIndex)
{
	if (TSharedPtr<SWidgetSwitcher> TabSwitcherPtr = TabSwitcher.Pin())
	{
		TabSwitcherPtr->SetActiveWidgetIndex(NewIndex);
	}
}

void FReactRegressionTesterModule::SetConnectionState(EReactTesterConnectionState NewState)
{
	ConnectionState = NewState;

	if (TSharedPtr<STextBlock> StatusTextPtr = StatusText.Pin())
	{
		switch (NewState)
		{
		case EReactTesterConnectionState::Disconnected:
			StatusTextPtr->SetText(LOCTEXT("StatusDisconnected", "Disconnected"));
			break;
		case EReactTesterConnectionState::Connecting:
			StatusTextPtr->SetText(LOCTEXT("StatusConnecting", "Connecting..."));
			break;
		case EReactTesterConnectionState::Connected:
			StatusTextPtr->SetText(LOCTEXT("StatusConnected", "Connected"));
			break;
		}
	}

	if (TSharedPtr<SButton> ConnectButtonPtr = ConnectButton.Pin())
	{
		const FText ButtonText = NewState == EReactTesterConnectionState::Disconnected
			? LOCTEXT("ConnectButton", "Connect")
			: LOCTEXT("DisconnectButton", "Disconnect");
		ConnectButtonPtr->SetContent(SNew(STextBlock).Text(ButtonText));
	}

	if (TSharedPtr<SEditableTextBox> AddressTextBoxPtrForState = AddressTextBox.Pin())
	{
		AddressTextBoxPtrForState->SetIsReadOnly(NewState != EReactTesterConnectionState::Disconnected);
	}
}

bool FReactRegressionTesterModule::CanTakeScreenshot() const
{
	return ConnectionState == EReactTesterConnectionState::Connected
		&& ReactSurface.Pin().IsValid()
		&& !LastRunModuleName.IsEmpty();
}

bool FReactRegressionTesterModule::ParseAddress(const FString& Input, FString& OutHost, uint32& OutPort) const
{
	const FString Trimmed = Input.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return false;
	}

	FString HostPart;
	FString PortPart;
	if (Trimmed.Split(TEXT(":"), &HostPart, &PortPart, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		HostPart = HostPart.TrimStartAndEnd();
		PortPart = PortPart.TrimStartAndEnd();
		if (HostPart.IsEmpty() || PortPart.IsEmpty() || !PortPart.IsNumeric())
		{
			return false;
		}
		const int32 PortInt = FCString::Atoi(*PortPart);
		if (PortInt <= 0 || PortInt > 65535)
		{
			return false;
		}
		OutHost = HostPart;
		OutPort = static_cast<uint32>(PortInt);
		return true;
	}

	OutHost = Trimmed;
	OutPort = 8081;
	return true;
}

void FReactRegressionTesterModule::OnBundleAlive()
{
	if (ConnectionState == EReactTesterConnectionState::Connecting)
	{
		SetConnectionState(EReactTesterConnectionState::Connected);
	}
	RefreshModuleList();
}

void FReactRegressionTesterModule::RefreshModuleList()
{
	if (!React::IsAvailable() || bModuleQueryInFlight)
	{
		return;
	}
	bModuleQueryInFlight = true;

	React::QueryAppKeys(
		[this](TArray<FString> Keys)
		{
			bModuleQueryInFlight = false;

			// Promote to Connected if we got any keys - the bundle has evaluated
			// AppRegistry registrations, even if no mount happened (and so
			// OnBundleAlive wouldn't have fired on its own).
			if (ConnectionState == EReactTesterConnectionState::Connecting && Keys.Num() > 0)
			{
				SetConnectionState(EReactTesterConnectionState::Connected);
			}

			// Skip the rebuild if the key set is unchanged. Without this, the retry
			// ticker would reset the dropdown every 3s and drop the user's selection
			// mid-interaction.
			bool bKeysChanged = Keys.Num() != ModuleItems.Num();
			for (int32 i = 0; !bKeysChanged && i < Keys.Num(); ++i)
			{
				if (!ModuleItems[i].IsValid() || *ModuleItems[i] != Keys[i])
				{
					bKeysChanged = true;
				}
			}
			if (!bKeysChanged)
			{
				return;
			}

			ModuleItems.Reset(Keys.Num());
			for (FString& Key : Keys)
			{
				ModuleItems.Add(MakeShared<FString>(MoveTemp(Key)));
			}

			const FString PreviousSelection = SelectedModule.IsValid() ? *SelectedModule : FString();
			SelectedModule.Reset();
			for (const TSharedPtr<FString>& Item : ModuleItems)
			{
				if (Item.IsValid() && *Item == PreviousSelection)
				{
					SelectedModule = Item;
					break;
				}
			}
			if (!SelectedModule.IsValid() && ModuleItems.Num() > 0)
			{
				SelectedModule = ModuleItems[0];
			}

			if (TSharedPtr<SComboBox<TSharedPtr<FString>>> ModuleComboBoxPtr = ModuleComboBox.Pin())
			{
				ModuleComboBoxPtr->RefreshOptions();
				if (SelectedModule.IsValid())
				{
					ModuleComboBoxPtr->SetSelectedItem(SelectedModule);
				}
				else
				{
					ModuleComboBoxPtr->ClearSelection();
				}
			}
		});
}

void FReactRegressionTesterModule::InitializeAutomationFromCommandLine()
{
	bAutomationEnabled = FParse::Param(FCommandLine::Get(), TEXT("RNUERegressionAuto"));
	if (!bAutomationEnabled)
	{
		return;
	}

	AutomationStage = EReactRegressionAutomationStage::WaitingForManifest;
	AutomationTimeoutMs = DefaultAutomationTimeoutMs;
	FParse::Value(FCommandLine::Get(), TEXT("RNUERegressionManifest="), AutomationManifestPath);
	FParse::Value(FCommandLine::Get(), TEXT("RNUERegressionOut="), AutomationOutRoot);
	FParse::Value(FCommandLine::Get(), TEXT("RNUERegressionMetroUrl="), AutomationMetroUrl);
	FParse::Value(FCommandLine::Get(), TEXT("RNUERegressionScenario="), AutomationScenarioFilter);

	int32 ParsedTimeoutMs = static_cast<int32>(AutomationTimeoutMs);
	if (FParse::Value(FCommandLine::Get(), TEXT("RNUERegressionTimeoutMs="), ParsedTimeoutMs) && ParsedTimeoutMs > 0)
	{
		AutomationTimeoutMs = static_cast<uint32>(ParsedTimeoutMs);
	}

	if (AutomationOutRoot.IsEmpty())
	{
		AutomationOutRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("artifacts/runs/latest"));
	}
	if (AutomationMetroUrl.IsEmpty())
	{
		AutomationMetroUrl = TEXT("http://127.0.0.1:8081");
	}
}

bool FReactRegressionTesterModule::LoadAutomationManifest(FString& OutError)
{
	if (AutomationManifestPath.IsEmpty())
	{
		OutError = TEXT("-RNUERegressionManifest is required in automation mode.");
		return false;
	}

	AutomationManifestPath = FPaths::ConvertRelativePathToFull(AutomationManifestPath);
	FString ManifestJson;
	if (!FFileHelper::LoadFileToString(ManifestJson, *AutomationManifestPath))
	{
		OutError = FString::Printf(TEXT("Failed to read manifest: %s"), *AutomationManifestPath);
		return false;
	}

	TSharedPtr<FJsonValue> RootValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestJson);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to parse manifest JSON: %s"), *AutomationManifestPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ScenarioValues = nullptr;
	if (RootValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> RootObject = RootValue->AsObject();
		if (!RootObject.IsValid() || !RootObject->TryGetArrayField(TEXT("scenarios"), ScenarioValues))
		{
			OutError = TEXT("Manifest JSON object must contain a scenarios array.");
			return false;
		}
	}
	else if (RootValue->Type == EJson::Array)
	{
		ScenarioValues = &RootValue->AsArray();
	}

	if (!ScenarioValues)
	{
		OutError = TEXT("Manifest JSON root must be an object with scenarios or an array.");
		return false;
	}

	AutomationManifest.Reset(ScenarioValues->Num());
	TSet<FString> SeenIds;
	for (const TSharedPtr<FJsonValue>& ScenarioValue : *ScenarioValues)
	{
		if (!ScenarioValue.IsValid() || ScenarioValue->Type != EJson::Object)
		{
			OutError = TEXT("Manifest scenarios must be objects.");
			return false;
		}

		FReactRegressionScenarioVariant Scenario;
		if (!ReadScenarioVariant(ScenarioValue->AsObject(), Scenario, OutError))
		{
			return false;
		}
		if (SeenIds.Contains(Scenario.Id))
		{
			OutError = FString::Printf(TEXT("Duplicate scenario id in manifest: %s"), *Scenario.Id);
			return false;
		}
		SeenIds.Add(Scenario.Id);
		AutomationManifest.Add(MoveTemp(Scenario));
	}

	AutomationQueue.Reset();
	for (const FReactRegressionScenarioVariant& Scenario : AutomationManifest)
	{
		if (AutomationScenarioFilter.IsEmpty()
			|| Scenario.Id == AutomationScenarioFilter
			|| Scenario.BaseId == AutomationScenarioFilter)
		{
			AutomationQueue.Add(Scenario);
		}
	}

	if (AutomationQueue.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("No scenarios matched '%s'. Use a base scenario id or exact variant id."),
			*AutomationScenarioFilter);
		return false;
	}

	return true;
}

void FReactRegressionTesterModule::StartAutomation()
{
	FString Error;
	if (!LoadAutomationManifest(Error))
	{
		CompleteAutomation(false, Error);
		return;
	}

	WriteAutomationManifestCopy();

	FString Host;
	uint32 Port = 0;
	if (!ParseMetroUrl(AutomationMetroUrl, Host, Port))
	{
		CompleteAutomation(false, FString::Printf(TEXT("Invalid Metro URL: %s"), *AutomationMetroUrl));
		return;
	}

	bHarnessManifestPublished = false;
	HarnessReadyIds.Reset();
	HarnessFailedMessages.Reset();
	HarnessPublishedScenarios.Reset();
	AutomationResults.Reset();
	AutomationCurrentIndex = INDEX_NONE;
	AutomationStage = EReactRegressionAutomationStage::WaitingForManifest;
	AutomationStageStartedAtSeconds = FPlatformTime::Seconds();

	const FTCHARToUTF8 HostUtf8(*Host);
	UE_LOG(LogTemp, Display, TEXT("[ReactRegressionTester] Automation connecting to Metro at %s:%u"), *Host, Port);
	React::Reconnect("ReactRegressionTester", "Unreal", HostUtf8.Get(), Port);
	if (!React::LoadScript())
	{
		CompleteAutomation(false, TEXT("Failed to load React Native bundle."));
		return;
	}

	if (!AutomationTickerHandle.IsValid())
	{
		AutomationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FReactRegressionTesterModule::OnAutomationTick),
			0.0f);
	}
}

bool FReactRegressionTesterModule::OnAutomationTick(float DeltaTime)
{
	if (!bAutomationEnabled)
	{
		return false;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const double TimeoutSeconds = static_cast<double>(AutomationTimeoutMs) / 1000.0;

	if (AutomationStage != EReactRegressionAutomationStage::Complete
		&& AutomationStage != EReactRegressionAutomationStage::Failed
		&& TimeoutSeconds > 0.0
		&& NowSeconds - AutomationStageStartedAtSeconds > TimeoutSeconds)
	{
		CompleteAutomation(false, FString::Printf(
			TEXT("Timed out in automation stage '%s'."),
			*AutomationStageToString(AutomationStage)));
		return false;
	}

	switch (AutomationStage)
	{
	case EReactRegressionAutomationStage::WaitingForManifest:
		if (bHarnessManifestPublished)
		{
			FString ManifestError;
			if (!ValidateHarnessManifest(ManifestError))
			{
				CompleteAutomation(false, ManifestError);
				return false;
			}
			StartNextAutomationVariant();
		}
		break;

	case EReactRegressionAutomationStage::PreparingVariant:
		if (AutomationFrameWait > 0)
		{
			--AutomationFrameWait;
			break;
		}
		if (!AutomationReactSurface.IsValid() || !AutomationQueue.IsValidIndex(AutomationCurrentIndex))
		{
			CompleteAutomation(false, TEXT("Automation surface is not valid."));
			return false;
		}
		React::RunApplication(*AutomationReactSurface, AutomationQueue[AutomationCurrentIndex].AppName);
		AutomationStage = EReactRegressionAutomationStage::WaitingForReady;
		AutomationStageStartedAtSeconds = NowSeconds;
		break;

	case EReactRegressionAutomationStage::WaitingForReady:
	{
		const FReactRegressionScenarioVariant& Scenario = AutomationQueue[AutomationCurrentIndex];
		if (const FString* Failure = HarnessFailedMessages.Find(Scenario.Id))
		{
			FReactRegressionCaptureResult Result;
			Result.Scenario = Scenario;
			Result.Status = TEXT("failed");
			Result.Message = *Failure;
			Result.StartedAtSeconds = AutomationVariantStartedAtSeconds;
			Result.EndedAtSeconds = NowSeconds;
			AutomationResults.Add(MoveTemp(Result));
			StartNextAutomationVariant();
			break;
		}
		if (HarnessReadyIds.Contains(Scenario.Id))
		{
			AutomationFrameWait = FMath::Max(Scenario.SettleFrames, 1);
			AutomationStage = EReactRegressionAutomationStage::Settling;
			AutomationStageStartedAtSeconds = NowSeconds;
		}
		break;
	}

	case EReactRegressionAutomationStage::Settling:
		if (AutomationFrameWait > 0)
		{
			--AutomationFrameWait;
			break;
		}
		CaptureCurrentAutomationVariant();
		StartNextAutomationVariant();
		break;

	case EReactRegressionAutomationStage::Complete:
	case EReactRegressionAutomationStage::Failed:
		return false;

	case EReactRegressionAutomationStage::Disabled:
	default:
		break;
	}

	return true;
}

void FReactRegressionTesterModule::StartNextAutomationVariant()
{
	++AutomationCurrentIndex;
	if (!AutomationQueue.IsValidIndex(AutomationCurrentIndex))
	{
		bool bAllPassed = AutomationResults.Num() == AutomationQueue.Num();
		for (const FReactRegressionCaptureResult& Result : AutomationResults)
		{
			if (Result.Status != TEXT("passed"))
			{
				bAllPassed = false;
				break;
			}
		}
		CompleteAutomation(bAllPassed, FString::Printf(TEXT("Captured %d RNUE scenario(s)."), AutomationResults.Num()));
		return;
	}

	const FReactRegressionScenarioVariant& Scenario = AutomationQueue[AutomationCurrentIndex];
	UE_LOG(LogTemp, Display, TEXT("[ReactRegressionTester] Capturing %s (%s) at %dx%d"),
		*Scenario.Id,
		*Scenario.AppName,
		Scenario.Width,
		Scenario.Height);

	HarnessReadyIds.Remove(Scenario.Id);
	HarnessFailedMessages.Remove(Scenario.Id);
	AutomationVariantStartedAtSeconds = FPlatformTime::Seconds();
	EnsureAutomationWindow(Scenario);
	AutomationFrameWait = AutomationPrepareFrames;
	AutomationStage = EReactRegressionAutomationStage::PreparingVariant;
	AutomationStageStartedAtSeconds = AutomationVariantStartedAtSeconds;
}

void FReactRegressionTesterModule::EnsureAutomationWindow(const FReactRegressionScenarioVariant& Scenario)
{
	if (!AutomationWindow.IsValid())
	{
		TSharedRef<SReactSurface> Surface = SNew(SReactSurface);
		AutomationReactSurface = Surface;

		TSharedRef<SBox> SurfaceBox =
			SNew(SBox)
			.WidthOverride(Scenario.Width)
			.HeightOverride(Scenario.Height)
				[Surface];
		AutomationSurfaceBox = SurfaceBox;

		AutomationWindow =
			SNew(SWindow)
			.Title(LOCTEXT("AutomationWindowTitle", "RNUE Regression Capture"))
			.ClientSize(FVector2D(Scenario.Width, Scenario.Height))
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.SizingRule(ESizingRule::FixedSize)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.CreateTitleBar(true)
			.UseOSWindowBorder(true)
				[SurfaceBox];

		FSlateApplication::Get().AddWindow(AutomationWindow.ToSharedRef());
		return;
	}

	if (AutomationSurfaceBox.IsValid())
	{
		AutomationSurfaceBox->SetWidthOverride(Scenario.Width);
		AutomationSurfaceBox->SetHeightOverride(Scenario.Height);
	}
	AutomationWindow->Resize(FVector2D(Scenario.Width, Scenario.Height));
}

void FReactRegressionTesterModule::CaptureCurrentAutomationVariant()
{
	if (!AutomationQueue.IsValidIndex(AutomationCurrentIndex) || !AutomationReactSurface.IsValid())
	{
		CompleteAutomation(false, TEXT("No current automation scenario to capture."));
		return;
	}

	const FReactRegressionScenarioVariant& Scenario = AutomationQueue[AutomationCurrentIndex];
	const FString RnueDir = FPaths::ConvertRelativePathToFull(AutomationOutRoot / TEXT("rnue"));
	const FString OutputPath = RnueDir / FString::Printf(TEXT("%s.png"), *FPaths::MakeValidFileName(Scenario.Id, TCHAR('_')));

	FReactRegressionCaptureResult Result;
	Result.Scenario = Scenario;
	Result.OutputPath = OutputPath;
	Result.StartedAtSeconds = AutomationVariantStartedAtSeconds;

	FString Error;
	FIntPoint CapturedSize(0, 0);
	const FIntPoint ExpectedSize(Scenario.Width, Scenario.Height);
	if (!ReactRegressionScreenshot::SaveSurfaceToPng(
		AutomationReactSurface.ToSharedRef(),
		OutputPath,
		Error,
		&CapturedSize,
		&ExpectedSize))
	{
		Result.Status = TEXT("failed");
		Result.Message = Error;
	}
	else if (CapturedSize.X != Scenario.Width || CapturedSize.Y != Scenario.Height)
	{
		Result.Status = TEXT("failed");
		Result.Message = FString::Printf(
			TEXT("Captured %dx%d, expected %dx%d."),
			CapturedSize.X,
			CapturedSize.Y,
			Scenario.Width,
			Scenario.Height);
	}
	else
	{
		Result.Status = TEXT("passed");
	}
	Result.EndedAtSeconds = FPlatformTime::Seconds();
	AutomationResults.Add(MoveTemp(Result));
}

void FReactRegressionTesterModule::CompleteAutomation(bool bSuccess, const FString& Message)
{
	if (AutomationStage == EReactRegressionAutomationStage::Complete || AutomationStage == EReactRegressionAutomationStage::Failed)
	{
		return;
	}

	AutomationStage = bSuccess ? EReactRegressionAutomationStage::Complete : EReactRegressionAutomationStage::Failed;
	if (!Message.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("[ReactRegressionTester] %s"), *Message);
	}
	WriteAutomationOutputs();

	if (AutomationTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutomationTickerHandle);
		AutomationTickerHandle.Reset();
	}

	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1, TEXT("ReactRegressionTester automation complete"));
}

void FReactRegressionTesterModule::WriteAutomationOutputs()
{
	const FString RnueDir = FPaths::ConvertRelativePathToFull(AutomationOutRoot / TEXT("rnue"));
	IFileManager::Get().MakeDirectory(*RnueDir, true);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), AutomationStage == EReactRegressionAutomationStage::Complete ? TEXT("passed") : TEXT("failed"));
	Root->SetStringField(TEXT("manifestPath"), AutomationManifestPath);
	Root->SetStringField(TEXT("metroUrl"), AutomationMetroUrl);
	Root->SetStringField(TEXT("scenarioFilter"), AutomationScenarioFilter);

	TArray<TSharedPtr<FJsonValue>> Results;
	Results.Reserve(AutomationResults.Num());
	for (const FReactRegressionCaptureResult& Result : AutomationResults)
	{
		TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("id"), Result.Scenario.Id);
		ResultObject->SetStringField(TEXT("baseId"), Result.Scenario.BaseId);
		ResultObject->SetStringField(TEXT("appName"), Result.Scenario.AppName);
		ResultObject->SetNumberField(TEXT("width"), Result.Scenario.Width);
		ResultObject->SetNumberField(TEXT("height"), Result.Scenario.Height);
		ResultObject->SetStringField(TEXT("status"), Result.Status);
		ResultObject->SetStringField(TEXT("outputPath"), Result.OutputPath);
		ResultObject->SetNumberField(TEXT("startedAtSeconds"), Result.StartedAtSeconds);
		ResultObject->SetNumberField(TEXT("endedAtSeconds"), Result.EndedAtSeconds);
		if (!Result.Message.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("message"), Result.Message);
		}
		Results.Add(MakeShared<FJsonValueObject>(ResultObject));
	}
	Root->SetArrayField(TEXT("results"), MoveTemp(Results));

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ResultsPath = RnueDir / TEXT("results.json");
	if (!FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *ResultsPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Failed to write results: %s"), *ResultsPath);
	}
}

void FReactRegressionTesterModule::WriteAutomationManifestCopy()
{
	const FString RnueDir = FPaths::ConvertRelativePathToFull(AutomationOutRoot / TEXT("rnue"));
	IFileManager::Get().MakeDirectory(*RnueDir, true);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Scenarios;
	Scenarios.Reserve(AutomationManifest.Num());
	for (const FReactRegressionScenarioVariant& Scenario : AutomationManifest)
	{
		Scenarios.Add(MakeShared<FJsonValueObject>(ScenarioToJson(Scenario)));
	}
	Root->SetArrayField(TEXT("scenarios"), MoveTemp(Scenarios));

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ManifestCopyPath = RnueDir / TEXT("manifest.json");
	if (!FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *ManifestCopyPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ReactRegressionTester] Failed to write manifest copy: %s"), *ManifestCopyPath);
	}
}

bool FReactRegressionTesterModule::ParseMetroUrl(const FString& Input, FString& OutHost, uint32& OutPort) const
{
	FString Address = Input.TrimStartAndEnd();
	FString Scheme;
	FString Rest;
	if (Address.Split(TEXT("://"), &Scheme, &Rest, ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{
		Address = Rest;
	}

	FString HostAndPort;
	FString PathPart;
	if (Address.Split(TEXT("/"), &HostAndPort, &PathPart, ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{
		Address = HostAndPort;
	}

	return ParseAddress(Address, OutHost, OutPort);
}

bool FReactRegressionTesterModule::ValidateHarnessManifest(FString& OutError) const
{
	TMap<FString, const RNUETestHarness::FScenarioMeta*> PublishedById;
	PublishedById.Reserve(HarnessPublishedScenarios.Num());
	for (const RNUETestHarness::FScenarioMeta& PublishedScenario : HarnessPublishedScenarios)
	{
		if (PublishedById.Contains(PublishedScenario.Id))
		{
			OutError = FString::Printf(TEXT("Harness published duplicate scenario id: %s"), *PublishedScenario.Id);
			return false;
		}
		PublishedById.Add(PublishedScenario.Id, &PublishedScenario);
	}

	if (PublishedById.Num() != AutomationManifest.Num())
	{
		OutError = FString::Printf(
			TEXT("Harness published %d scenario(s), expected %d from %s."),
			PublishedById.Num(),
			AutomationManifest.Num(),
			*AutomationManifestPath);
		return false;
	}

	for (const FReactRegressionScenarioVariant& ExpectedScenario : AutomationManifest)
	{
		const RNUETestHarness::FScenarioMeta* const* PublishedScenarioPtr = PublishedById.Find(ExpectedScenario.Id);
		if (!PublishedScenarioPtr)
		{
			OutError = FString::Printf(TEXT("Harness did not publish scenario id: %s"), *ExpectedScenario.Id);
			return false;
		}

		const RNUETestHarness::FScenarioMeta& PublishedScenario = **PublishedScenarioPtr;
		const FString PublishedBackground = PublishedScenario.Background.IsSet()
			? PublishedScenario.Background.GetValue()
			: FString();
		if (PublishedScenario.AppName != ExpectedScenario.AppName
			|| PublishedScenario.Width != ExpectedScenario.Width
			|| PublishedScenario.Height != ExpectedScenario.Height
			|| PublishedScenario.SettleFrames != ExpectedScenario.SettleFrames
			|| PublishedBackground != ExpectedScenario.Background)
		{
			OutError = FString::Printf(TEXT("Harness scenario '%s' does not match %s."), *ExpectedScenario.Id, *AutomationManifestPath);
			return false;
		}
	}

	return true;
}

void FReactRegressionTesterModule::OnHarnessManifestPublished(const TArray<RNUETestHarness::FScenarioMeta>& Scenarios)
{
	HarnessPublishedScenarios = Scenarios;
	bHarnessManifestPublished = true;
	UE_LOG(LogTemp, Display, TEXT("[ReactRegressionTester] Harness published %d scenario(s)"), Scenarios.Num());
}

void FReactRegressionTesterModule::OnHarnessScenarioReady(const FString& Id)
{
	HarnessReadyIds.Add(Id);
}

void FReactRegressionTesterModule::OnHarnessScenarioFailed(const FString& Id, const FString& Message)
{
	HarnessFailedMessages.Add(Id, Message);
}

void FReactRegressionTesterModule::AppStarted()
{
	if (bAutomationEnabled)
	{
		StartAutomation();
		return;
	}

	FGlobalTabmanager::Get()->TryInvokeTab(ReactRegressionTesterModuleTabName);

	ConnectFlow();

	if (!RetryTickerHandle.IsValid())
	{
		RetryTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FReactRegressionTesterModule::OnRetryTick),
			3.0f);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReactRegressionTesterModule, ReactRegressionTesterModule)
