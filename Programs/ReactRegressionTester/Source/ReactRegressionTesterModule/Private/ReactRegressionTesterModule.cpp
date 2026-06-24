// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModule.h"
#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModuleStyle.h"
#include "ReactRegressionTesterModule/Public/ReactRegressionTesterModuleCommands.h"

#include "ReactRegressionScreenshot.h"
#include "React.h"
#include "ReactSurface.h"

#include "Misc/DateTime.h"
#include "Misc/Paths.h"
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
#include "Styling/AppStyle.h"

static const FName ReactRegressionTesterModuleTabName("ReactRegressionTesterModule");

#define LOCTEXT_NAMESPACE "FReactRegressionTesterModule"

void FReactRegressionTesterModule::StartupModule()
{
	FReactRegressionTesterModuleStyle::Initialize();
	FReactRegressionTesterModuleStyle::ReloadTextures();

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

	// Placeholder scenarios for the Tests tab (will be replaced by publishManifest data later)
	TestItems.Add(MakeShared<FString>(TEXT("Text Rendering")));
	TestItems.Add(MakeShared<FString>(TEXT("Button Press")));
	TestItems.Add(MakeShared<FString>(TEXT("Scroll View")));
	TestItems.Add(MakeShared<FString>(TEXT("Flex Layout")));
	TestItems.Add(MakeShared<FString>(TEXT("Image Loading")));
	TestItems.Add(MakeShared<FString>(TEXT("Text Input")));
	TestItems.Add(MakeShared<FString>(TEXT("Animation")));
	TestItems.Add(MakeShared<FString>(TEXT("Touch Handling")));
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

void FReactRegressionTesterModule::AppStarted()
{
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
