#include "CSTopoPlayerController.h"

#include "CSTopoMainMenuSlate.h"
#include "CSTopoHomeScreenSlate.h"
#include "CSTopoPointCloudToolbarSlate.h"
#include "CSTopoReticleSlate.h"
#include "CSTopoSurveyHudSlate.h"
#include "CSTopoSurveyPawn.h"
#include "CSTopoSurveySubsystem.h"
#include "DesktopPlatformModule.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Input/Events.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/Paths.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogCSTopoUI, Log, All);

namespace
{
constexpr int32 CSTopoSurveyGameFocusRepairFrames = 30;

const void* GetCSTopoDialogParentWindowHandle()
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
    }

    return FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
}
}

void ACSTopoPlayerController::BeginPlay()
{
    Super::BeginPlay();
    EnsureSurveyEnhancedInput();
    RegisterSurveyInputMappingContext();

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (GEngine != nullptr && GEngine->GameViewport != nullptr)
    {
        SAssignNew(PointCloudToolbar, SCSTopoPointCloudToolbar)
            .SurveySubsystem(Survey);

        PointCloudToolbarContainer = SNew(SWeakWidget)
            .PossiblyNullContent(PointCloudToolbar.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(PointCloudToolbarContainer.ToSharedRef(), 100);
        PointCloudToolbar->SetVisibility(EVisibility::Collapsed);

        SAssignNew(Reticle, SCSTopoReticle);

        ReticleContainer = SNew(SWeakWidget)
            .PossiblyNullContent(Reticle.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(ReticleContainer.ToSharedRef(), 90);
        Reticle->SetVisibility(EVisibility::Collapsed);

        SAssignNew(SurveyStatusOverlay, SBorder)
            .Visibility(EVisibility::Collapsed)
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f))
            .Padding(FMargin(10.0f, 8.0f))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return GetSurveyStatusOverlayText(); })
                .ColorAndOpacity(FLinearColor(0.82f, 0.96f, 1.0f, 1.0f))
                .AutoWrapText(true)
            ];

        SurveyStatusOverlayContainer = SNew(SWeakWidget)
            .PossiblyNullContent(SurveyStatusOverlay.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(SurveyStatusOverlayContainer.ToSharedRef(), 91);

        SAssignNew(SurveyHud, SCSTopoSurveyHud)
            .SurveySubsystem(Survey)
            .OnCollectShot(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::CollectShotFromSurveyHud))
            .OnToggleFocus(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::ToggleSurveyCollectorFocus))
            .OnUndo(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::UndoLastMeasurementFromSurveyHud));

        SurveyHudContainer = SNew(SWeakWidget)
            .PossiblyNullContent(SurveyHud.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(SurveyHudContainer.ToSharedRef(), 92);
        SurveyHud->SetVisibility(EVisibility::Collapsed);

        SAssignNew(MainMenu, SCSTopoMainMenu)
            .SurveySubsystem(Survey)
            .PointCloudToolbar(PointCloudToolbar)
            .OnNewProject(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::ImportPointCloudToNewProjectFromDialog))
            .OnOpenProject(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::OpenProjectFromDialog))
            .OnExit(FSimpleDelegate::CreateUObject(this, &ACSTopoPlayerController::ExitApplication));

        MainMenuContainer = SNew(SWeakWidget)
            .PossiblyNullContent(MainMenu.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(MainMenuContainer.ToSharedRef(), 110);
        MainMenu->SetVisibility(EVisibility::Collapsed);

        SAssignNew(HomeScreen, SCSTopoHomeScreen)
            .SurveySubsystem(Survey)
            .OnOpenProject(SCSTopoHomeScreen::FOnHomeAction::CreateUObject(this, &ACSTopoPlayerController::OpenProjectFromDialog))
            .OnImportPointCloud(SCSTopoHomeScreen::FOnHomeAction::CreateUObject(this, &ACSTopoPlayerController::ImportPointCloudToNewProjectFromDialog))
            .OnRetrySurface(SCSTopoHomeScreen::FOnHomeAction::CreateUObject(this, &ACSTopoPlayerController::RetrySurfaceBuild))
            .OnCancelToHome(SCSTopoHomeScreen::FOnHomeAction::CreateUObject(this, &ACSTopoPlayerController::CancelToHome));

        HomeScreenContainer = SNew(SWeakWidget)
            .PossiblyNullContent(HomeScreen.ToSharedRef());

        GEngine->GameViewport->AddViewportWidgetContent(HomeScreenContainer.ToSharedRef(), 200);
        UE_LOG(LogCSTopoUI, Log, TEXT("CSTopo point-cloud toolbar added to viewport."));
        SetSurveyInputEnabled(false);
        ShowHomeScreen();
    }
    else
    {
        UE_LOG(LogCSTopoUI, Warning, TEXT("CSTopo point-cloud toolbar could not be added because GameViewport is unavailable."));
    }
}

void ACSTopoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterSurveyInputMappingContext();

    Super::EndPlay(EndPlayReason);
}

void ACSTopoPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->UpdateWorkflow();
    }

    MonitorStartupSurfaceBuild();
    TickSurveyGameFocusRepair();
    if (HomeScreen.IsValid() && HomeScreen->GetVisibility() == EVisibility::Visible)
    {
        HomeScreen->Refresh();
    }

    ApplyHeldSurveyInput(DeltaSeconds);
}

void ACSTopoPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    EnsureSurveyEnhancedInput();

    UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
    if (EnhancedInput == nullptr)
    {
        UE_LOG(LogCSTopoUI, Error, TEXT("CSTopo requires Enhanced Input. DefaultInputComponentClass must be EnhancedInputComponent."));
        return;
    }

    EnhancedInput->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleMoveForwardInput);
    EnhancedInput->BindAction(MoveForwardAction, ETriggerEvent::Completed, this, &ACSTopoPlayerController::HandleMoveForwardInput);
    EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleMoveRightInput);
    EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Completed, this, &ACSTopoPlayerController::HandleMoveRightInput);
    EnhancedInput->BindAction(MoveUpAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleMoveUpInput);
    EnhancedInput->BindAction(MoveUpAction, ETriggerEvent::Completed, this, &ACSTopoPlayerController::HandleMoveUpInput);
    EnhancedInput->BindAction(LookYawAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleLookYawInput);
    EnhancedInput->BindAction(LookPitchAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleLookPitchInput);
    EnhancedInput->BindAction(FlySpeedAction, ETriggerEvent::Triggered, this, &ACSTopoPlayerController::HandleFlySpeedInput);
    EnhancedInput->BindAction(ToggleNavigationAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleToggleNavigationInput);
    EnhancedInput->BindAction(ToggleUserTinAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleToggleUserTinInput);
    EnhancedInput->BindAction(UndoAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleUndoInput);
    EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleSprintStarted);
    EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &ACSTopoPlayerController::HandleSprintCompleted);
    EnhancedInput->BindAction(CollectShotAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleCollectShotInput);
    EnhancedInput->BindAction(EscapeAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleEscapeInput);
    EnhancedInput->BindAction(TabAction, ETriggerEvent::Started, this, &ACSTopoPlayerController::HandleTabInput);
    EnhancedInput->BindAction(Code1Action, ETriggerEvent::Started, this, &ACSTopoPlayerController::SetActiveCode1);
    EnhancedInput->BindAction(Code2Action, ETriggerEvent::Started, this, &ACSTopoPlayerController::SetActiveCode2);
    EnhancedInput->BindAction(Code3Action, ETriggerEvent::Started, this, &ACSTopoPlayerController::SetActiveCode3);
    EnhancedInput->BindAction(Code4Action, ETriggerEvent::Started, this, &ACSTopoPlayerController::SetActiveCode4);
}

void ACSTopoPlayerController::TogglePointCloudToolbar()
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    if (!PointCloudToolbar.IsValid())
    {
        return;
    }

    SetToolbarVisible(PointCloudToolbar->GetVisibility() != EVisibility::Visible);
}

void ACSTopoPlayerController::RefreshPointCloudToolbar()
{
    if (PointCloudToolbar.IsValid())
    {
        PointCloudToolbar->RefreshList();
    }
}

void ACSTopoPlayerController::SetToolbarVisible(bool bVisible)
{
    if (!PointCloudToolbar.IsValid())
    {
        return;
    }

    PointCloudToolbar->SetVisibility(bVisible ? EVisibility::Visible : EVisibility::Collapsed);
    if (bVisible)
    {
        PointCloudToolbar->RefreshList();
    }

    if (bVisible)
    {
        EnterModalUIMode();
        FInputModeGameAndUI InputMode;
        InputMode.SetWidgetToFocus(PointCloudToolbar);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        InputMode.SetHideCursorDuringCapture(false);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
        ResetIgnoreMoveInput();
        ResetIgnoreLookInput();
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
    }
    else
    {
        EnterSurveyGameMode();
    }
}

void ACSTopoPlayerController::ImportPointCloudFromShortcut()
{
    if (!bSurveyInputEnabled)
    {
        ImportPointCloudToNewProjectFromDialog();
        return;
    }

    if (MainMenu.IsValid())
    {
        bShowMouseCursor = true;
        MainMenu->OpenImportPointCloudDialog();
    }
}

void ACSTopoPlayerController::HandleEscapePressed()
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    if (SurveyInputMode == ESurveyInputMode::CollectorUI)
    {
        EnterSurveyGameMode();
        SetSurveyInputDebug(TEXT("Escape collector close"));
        return;
    }

    if (PointCloudToolbar.IsValid() && PointCloudToolbar->GetVisibility() == EVisibility::Visible)
    {
        SetToolbarVisible(false);
        return;
    }

    EnterSurveyGameMode();
    SetSurveyInputDebug(TEXT("Escape focus reset"));
}

void ACSTopoPlayerController::ApplyHeldSurveyInput(float DeltaSeconds)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn());
    if (SurveyPawn == nullptr)
    {
        return;
    }

    SurveyPawn->SetSurveySprintHeld(bSprintHeld);
    SurveyPawn->ApplySurveyMoveForward(MoveForwardAxis);
    SurveyPawn->ApplySurveyMoveRight(MoveRightAxis);
    SurveyPawn->ApplySurveyMoveUp(MoveUpAxis);
}

void ACSTopoPlayerController::ResetHeldSurveyInput()
{
    bSprintHeld = false;
    MoveForwardAxis = 0.0f;
    MoveRightAxis = 0.0f;
    MoveUpAxis = 0.0f;
    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->SetSurveySprintHeld(false);
        SurveyPawn->ApplySurveyMoveForward(0.0f);
        SurveyPawn->ApplySurveyMoveRight(0.0f);
        SurveyPawn->ApplySurveyMoveUp(0.0f);
    }
}

void ACSTopoPlayerController::SetSurveyInputDebug(const FString& LastEvent)
{
    LastSurveyInputEvent = LastEvent;
    const TCHAR* ModeText = TEXT("UI");
    if (SurveyInputMode == ESurveyInputMode::Game)
    {
        ModeText = TEXT("Game");
    }
    else if (SurveyInputMode == ESurveyInputMode::CollectorUI)
    {
        ModeText = TEXT("Collector");
    }

    if (UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr)
    {
        Survey->SetRuntimeInputDebugState(FString::Printf(TEXT("Input: %s | Last: %s"), ModeText, *LastSurveyInputEvent));
    }
}

FText ACSTopoPlayerController::GetSurveyStatusOverlayText() const
{
    const UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("CSTopo survey"));
    }

    return FText::FromString(FString::Printf(
        TEXT("%s\nCode: %s | Next Pt: %d\n%s\n%s"),
        *Survey->GetActiveSurveyStatusLine(),
        *Survey->ActiveProject.ActiveCode,
        Survey->ActiveProject.NextPointNumber,
        *Survey->GetHoverStatusLine(),
        *Survey->GetUserTinStatusLine()));
}

void ACSTopoPlayerController::ShowHomeScreen()
{
    if (UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr)
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Home, TEXT("Choose a project or point cloud to begin."));
    }

    SetSurveyInputEnabled(false);
    if (HomeScreen.IsValid())
    {
        HomeScreen->SetVisibility(EVisibility::Visible);
        HomeScreen->Refresh();
    }
}

void ACSTopoPlayerController::HideHomeScreenAndEnterSurvey()
{
    if (HomeScreen.IsValid())
    {
        HomeScreen->SetVisibility(EVisibility::Collapsed);
    }

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        Survey->PrepareSurveyScene();
    }

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        if (Survey != nullptr)
        {
            FString FocusStatus;
            if (Survey->FocusPawnOnActiveSurfaceCenter(SurveyPawn, SurveyPawn->GroundClearance, FocusStatus))
            {
                if (AController* CurrentController = SurveyPawn->GetController())
                {
                    const FRotator CurrentRotation = CurrentController->GetControlRotation();
                    CurrentController->SetControlRotation(FRotator(-12.0f, CurrentRotation.Yaw, 0.0f));
                }
            }
            if (GEngine != nullptr && !FocusStatus.IsEmpty())
            {
                GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan, FocusStatus);
            }
        }
        if (SurveyPawn->Movement != nullptr)
        {
            SurveyPawn->Movement->StopMovementImmediately();
        }
    }

    if (Survey != nullptr)
    {
        FString OverlayStatus;
        Survey->SetCloudOverlayVisibleForActiveSource(false, OverlayStatus);
        FCSTopoAlignmentReport AlignmentReport;
        Survey->ValidateActiveCloudSurfaceAlignment(AlignmentReport);
        Survey->SetWorkflowState(ECSTopoWorkflowState::SurveyReady, FString::Printf(TEXT("Ready to survey. %s"), *AlignmentReport.Message));
        if (GEngine != nullptr && !AlignmentReport.Message.IsEmpty())
        {
            GEngine->AddOnScreenDebugMessage(-1, 6.0f, AlignmentReport.bAligned ? FColor::Green : FColor::Yellow, AlignmentReport.Message);
        }
    }

    SetSurveyInputEnabled(true);
    RefreshPointCloudToolbar();

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->SetNavigationMode(ECSTopoNavigationMode::Walk);
        SurveyPawn->RedrawMeasurementDebug();
    }
}

void ACSTopoPlayerController::SetSurveyInputEnabled(bool bEnabled)
{
    bSurveyInputEnabled = bEnabled;
    SurveyInputMode = bEnabled ? ESurveyInputMode::Game : ESurveyInputMode::ModalUI;
    ResetHeldSurveyInput();

    if (MainMenu.IsValid())
    {
        MainMenu->SetVisibility(bEnabled ? EVisibility::Visible : EVisibility::Collapsed);
    }
    if (Reticle.IsValid())
    {
        Reticle->SetVisibility(bEnabled ? EVisibility::Visible : EVisibility::Collapsed);
    }
    if (SurveyStatusOverlay.IsValid())
    {
        SurveyStatusOverlay->SetVisibility(bEnabled ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    }
    if (SurveyHud.IsValid())
    {
        SurveyHud->SetVisibility(bEnabled ? EVisibility::Visible : EVisibility::Collapsed);
        SurveyHud->SetCollectorInteractive(false);
    }
    if (PointCloudToolbar.IsValid())
    {
        PointCloudToolbar->SetVisibility(EVisibility::Collapsed);
    }

    if (bEnabled)
    {
        EnterSurveyGameMode();
    }
    else
    {
        EnterModalUIMode();
        bShowMouseCursor = true;
        FInputModeUIOnly InputMode;
        if (HomeScreen.IsValid())
        {
            InputMode.SetWidgetToFocus(HomeScreen);
        }
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        ResetIgnoreMoveInput();
        ResetIgnoreLookInput();
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
    }
}

void ACSTopoPlayerController::ToggleSurveyCollectorFocus()
{
    if (!bSurveyInputEnabled || !SurveyHud.IsValid())
    {
        return;
    }

    if (SurveyInputMode == ESurveyInputMode::CollectorUI)
    {
        EnterSurveyGameMode();
    }
    else
    {
        EnterCollectorMode();
    }
}

void ACSTopoPlayerController::OpenProjectFromDialog()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (Survey == nullptr || DesktopPlatform == nullptr)
    {
        return;
    }

    TArray<FString> SelectedFiles;
    const void* ParentWindowHandle = GetCSTopoDialogParentWindowHandle();
    const bool bPicked = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("Open CSTopo Project"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("CSTopo Projects (*.cstopo)|*.cstopo|All Files (*.*)|*.*"),
        EFileDialogFlags::None,
        SelectedFiles);
    if (!bPicked || SelectedFiles.IsEmpty())
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Home, TEXT("Open canceled. Choose a project or point cloud to begin."));
        return;
    }

    Survey->SetWorkflowState(ECSTopoWorkflowState::OpeningProject, FString::Printf(TEXT("Opening project: %s"), *SelectedFiles[0]));
    PendingStartupProjectPath = SelectedFiles[0];
    FString ErrorMessage;
    if (!Survey->LoadProject(SelectedFiles[0], ErrorMessage))
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, FString::Printf(TEXT("Failed to open project. %s"), *ErrorMessage));
        return;
    }

    FCSTopoPointCloudSource* Source = nullptr;
    if (!GetActiveSource(Survey, Source) || Source == nullptr)
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, TEXT("Project opened, but it has no active point cloud."));
        return;
    }

    if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready)
    {
        HideHomeScreenAndEnterSurvey();
        return;
    }

    FString SurfaceMessage;
    if (!Survey->StartSurfaceBuild(Source->SourceId, Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Stale, SurfaceMessage))
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, SurfaceMessage);
        return;
    }

    Survey->SetWorkflowState(ECSTopoWorkflowState::BuildingSurface, SurfaceMessage);
}

void ACSTopoPlayerController::ImportPointCloudToNewProjectFromDialog()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (Survey == nullptr || DesktopPlatform == nullptr)
    {
        return;
    }

    TArray<FString> SelectedClouds;
    const void* ParentWindowHandle = GetCSTopoDialogParentWindowHandle();
    const bool bCloudPicked = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("Import Point Cloud"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("Point Clouds (*.las;*.laz;*.copc.laz)|*.las;*.laz;*.copc.laz|All Files (*.*)|*.*"),
        EFileDialogFlags::None,
        SelectedClouds);
    if (!bCloudPicked || SelectedClouds.IsEmpty())
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Home, TEXT("Import canceled. Choose a project or point cloud to begin."));
        return;
    }

    TArray<FString> ProjectFiles;
    const FString DefaultFolder = FPaths::GetPath(SelectedClouds[0]);
    const FString DefaultName = FPaths::GetBaseFilename(SelectedClouds[0]) + TEXT(".cstopo");
    ParentWindowHandle = GetCSTopoDialogParentWindowHandle();
    const bool bProjectPicked = DesktopPlatform->SaveFileDialog(
        ParentWindowHandle,
        TEXT("Create CSTopo Project"),
        DefaultFolder,
        DefaultName,
        TEXT("CSTopo Projects (*.cstopo)|*.cstopo|All Files (*.*)|*.*"),
        EFileDialogFlags::None,
        ProjectFiles);
    if (!bProjectPicked || ProjectFiles.IsEmpty())
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Home, TEXT("Project creation canceled. Choose a project or point cloud to begin."));
        return;
    }

    FString ProjectPath = ProjectFiles[0];
    if (!ProjectPath.EndsWith(TEXT(".cstopo"), ESearchCase::IgnoreCase))
    {
        ProjectPath += TEXT(".cstopo");
    }

    PendingStartupProjectPath = ProjectPath;
    Survey->SetWorkflowState(ECSTopoWorkflowState::ImportingPointCloud, FString::Printf(TEXT("Creating project: %s"), *ProjectPath));
    Survey->NewProject(FPaths::GetBaseFilename(ProjectPath));

    FString ErrorMessage;
    if (!Survey->SaveProject(ProjectPath, ErrorMessage))
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, FString::Printf(TEXT("Failed to create project. %s"), *ErrorMessage));
        return;
    }

    FCSTopoPointCloudSource ImportedSource;
    if (!Survey->ImportPointCloud(SelectedClouds[0], ImportedSource, ErrorMessage))
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, FString::Printf(TEXT("Failed to import point cloud. %s"), *ErrorMessage));
        return;
    }

    Survey->SetWorkflowState(ECSTopoWorkflowState::BuildingSurface, ErrorMessage);
    MonitorStartupSurfaceBuild();
}

void ACSTopoPlayerController::ExitApplication()
{
    FPlatformMisc::RequestExit(false);
}

void ACSTopoPlayerController::RetrySurfaceBuild()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    FCSTopoPointCloudSource* Source = nullptr;
    if (Survey == nullptr || !GetActiveSource(Survey, Source) || Source == nullptr)
    {
        return;
    }

    FString Message;
    if (Survey->StartSurfaceBuild(Source->SourceId, true, Message))
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::BuildingSurface, Message);
    }
    else
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, Message);
    }
}

void ACSTopoPlayerController::CancelToHome()
{
    PendingStartupProjectPath.Empty();
    if (UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr)
    {
        Survey->NewProject(TEXT("Untitled CSTopo Project"));
    }
    ShowHomeScreen();
}

void ACSTopoPlayerController::CollectShotFromSurveyHud()
{
    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->TriggerCollectShot();
    }
}

void ACSTopoPlayerController::UndoLastMeasurementFromSurveyHud()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        return;
    }

    FString StatusMessage;
    Survey->UndoLastMeasurement(StatusMessage);
    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->RedrawMeasurementDebug();
    }
    RefreshPointCloudToolbar();
    SetSurveyInputDebug(TEXT("Ctrl+Z undo"));
    if (GEngine != nullptr && !StatusMessage.IsEmpty())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Yellow, StatusMessage);
    }
}

void ACSTopoPlayerController::MonitorStartupSurfaceBuild()
{
    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || Survey->WorkflowState != ECSTopoWorkflowState::BuildingSurface)
    {
        return;
    }

    FCSTopoPointCloudSource* Source = nullptr;
    if (!GetActiveSource(Survey, Source) || Source == nullptr)
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, TEXT("No active point cloud is available for surface build."));
        return;
    }

    if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready)
    {
        FString SaveMessage;
        const FString ProjectPath = !PendingStartupProjectPath.IsEmpty() ? PendingStartupProjectPath : Survey->GetCurrentProjectPath();
        if (!ProjectPath.IsEmpty())
        {
            Survey->SaveProject(ProjectPath, SaveMessage);
        }
        Survey->SetWorkflowState(ECSTopoWorkflowState::SurveyReady, FString::Printf(TEXT("Ready to survey. Units: %s | %lld points"), *Source->LinearUnitName, Source->PointCount));
        HideHomeScreenAndEnterSurvey();
    }
    else if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Failed)
    {
        Survey->SetWorkflowState(ECSTopoWorkflowState::Failed, Source->SurfaceStatus);
    }
}

bool ACSTopoPlayerController::GetActiveSource(UCSTopoSurveySubsystem* Survey, FCSTopoPointCloudSource*& Source) const
{
    Source = nullptr;
    if (Survey == nullptr || Survey->ActiveProject.ActivePointCloudId.IsEmpty())
    {
        return false;
    }

    Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });
    return Source != nullptr;
}

void ACSTopoPlayerController::SetActiveCodeByPaletteIndex(int32 PaletteIndex)
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || !Survey->ActiveProject.CodePalette.IsValidIndex(PaletteIndex))
    {
        return;
    }

    const FString Code = Survey->ActiveProject.CodePalette[PaletteIndex].Code;
    Survey->SetActiveCode(Code);
    RefreshPointCloudToolbar();

    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, Survey->ActiveProject.CodePalette[PaletteIndex].Color.ToFColor(true), FString::Printf(TEXT("Active code: %s"), *Code));
    }
}

void ACSTopoPlayerController::SetActiveCode1()
{
    SetActiveCodeByPaletteIndex(0);
}

void ACSTopoPlayerController::SetActiveCode2()
{
    SetActiveCodeByPaletteIndex(1);
}

void ACSTopoPlayerController::SetActiveCode3()
{
    SetActiveCodeByPaletteIndex(2);
}

void ACSTopoPlayerController::SetActiveCode4()
{
    SetActiveCodeByPaletteIndex(3);
}

void ACSTopoPlayerController::EnsureSurveyEnhancedInput()
{
    if (SurveyInputMappingContext != nullptr)
    {
        return;
    }

    auto MakeAction = [this](const TCHAR* Name, EInputActionValueType ValueType)
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = ValueType;
        Action->bConsumeInput = true;
        return Action;
    };

    SurveyInputMappingContext = NewObject<UInputMappingContext>(this, TEXT("CSTopoSurveyInputMappingContext"));
    MoveForwardAction = MakeAction(TEXT("IA_CSTopo_MoveForward"), EInputActionValueType::Axis1D);
    MoveRightAction = MakeAction(TEXT("IA_CSTopo_MoveRight"), EInputActionValueType::Axis1D);
    MoveUpAction = MakeAction(TEXT("IA_CSTopo_MoveUp"), EInputActionValueType::Axis1D);
    LookYawAction = MakeAction(TEXT("IA_CSTopo_LookYaw"), EInputActionValueType::Axis1D);
    LookPitchAction = MakeAction(TEXT("IA_CSTopo_LookPitch"), EInputActionValueType::Axis1D);
    FlySpeedAction = MakeAction(TEXT("IA_CSTopo_FlySpeed"), EInputActionValueType::Axis1D);
    ToggleNavigationAction = MakeAction(TEXT("IA_CSTopo_ToggleNavigation"), EInputActionValueType::Boolean);
    ToggleUserTinAction = MakeAction(TEXT("IA_CSTopo_ToggleUserTIN"), EInputActionValueType::Boolean);
    UndoAction = MakeAction(TEXT("IA_CSTopo_UndoMeasurement"), EInputActionValueType::Boolean);
    SprintAction = MakeAction(TEXT("IA_CSTopo_Sprint"), EInputActionValueType::Boolean);
    CollectShotAction = MakeAction(TEXT("IA_CSTopo_CollectShot"), EInputActionValueType::Boolean);
    EscapeAction = MakeAction(TEXT("IA_CSTopo_Escape"), EInputActionValueType::Boolean);
    TabAction = MakeAction(TEXT("IA_CSTopo_Tab"), EInputActionValueType::Boolean);
    Code1Action = MakeAction(TEXT("IA_CSTopo_Code1"), EInputActionValueType::Boolean);
    Code2Action = MakeAction(TEXT("IA_CSTopo_Code2"), EInputActionValueType::Boolean);
    Code3Action = MakeAction(TEXT("IA_CSTopo_Code3"), EInputActionValueType::Boolean);
    Code4Action = MakeAction(TEXT("IA_CSTopo_Code4"), EInputActionValueType::Boolean);

    MoveForwardAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    MoveRightAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    MoveUpAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;

    auto MapKey = [this](UInputAction* Action, const FKey& Key, bool bNegate = false)
    {
        FEnhancedActionKeyMapping& Mapping = SurveyInputMappingContext->MapKey(Action, Key);
        if (bNegate)
        {
            UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(SurveyInputMappingContext);
            Mapping.Modifiers.Add(Negate);
        }
    };

    MapKey(MoveForwardAction, EKeys::W);
    MapKey(MoveForwardAction, EKeys::S, true);
    MapKey(MoveRightAction, EKeys::D);
    MapKey(MoveRightAction, EKeys::A, true);
    MapKey(MoveUpAction, EKeys::E);
    MapKey(MoveUpAction, EKeys::Q, true);
    MapKey(LookYawAction, EKeys::MouseX);
    MapKey(LookPitchAction, EKeys::MouseY);
    MapKey(FlySpeedAction, EKeys::MouseWheelAxis);
    MapKey(ToggleNavigationAction, EKeys::F);
    MapKey(ToggleUserTinAction, EKeys::T);
    MapKey(UndoAction, EKeys::Z);
    MapKey(SprintAction, EKeys::LeftShift);
    MapKey(SprintAction, EKeys::RightShift);
    MapKey(CollectShotAction, EKeys::LeftMouseButton);
    MapKey(EscapeAction, EKeys::Escape);
    MapKey(TabAction, EKeys::Tab);
    MapKey(Code1Action, EKeys::One);
    MapKey(Code2Action, EKeys::Two);
    MapKey(Code3Action, EKeys::Three);
    MapKey(Code4Action, EKeys::Four);
}

void ACSTopoPlayerController::RegisterSurveyInputMappingContext()
{
    EnsureSurveyEnhancedInput();

    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            Subsystem->AddMappingContext(SurveyInputMappingContext, 100);
        }
    }
}

void ACSTopoPlayerController::UnregisterSurveyInputMappingContext()
{
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            if (SurveyInputMappingContext != nullptr)
            {
                Subsystem->RemoveMappingContext(SurveyInputMappingContext);
            }
        }
    }
}

void ACSTopoPlayerController::HandleMoveForwardInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        MoveForwardAxis = 0.0f;
        return;
    }

    MoveForwardAxis = Value.Get<float>();
    if (!FMath::IsNearlyZero(MoveForwardAxis))
    {
        SetSurveyInputDebug(FString::Printf(TEXT("MoveForward %.1f enhanced"), MoveForwardAxis));
    }
}

void ACSTopoPlayerController::HandleMoveRightInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        MoveRightAxis = 0.0f;
        return;
    }

    MoveRightAxis = Value.Get<float>();
    if (!FMath::IsNearlyZero(MoveRightAxis))
    {
        SetSurveyInputDebug(FString::Printf(TEXT("MoveRight %.1f enhanced"), MoveRightAxis));
    }
}

void ACSTopoPlayerController::HandleMoveUpInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        MoveUpAxis = 0.0f;
        return;
    }

    MoveUpAxis = Value.Get<float>();
    if (!FMath::IsNearlyZero(MoveUpAxis))
    {
        SetSurveyInputDebug(FString::Printf(TEXT("MoveUp %.1f enhanced"), MoveUpAxis));
    }
}

void ACSTopoPlayerController::HandleLookYawInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        const float AxisValue = Value.Get<float>();
        SurveyPawn->ApplySurveyTurn(AxisValue);
        const float CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;
        if (!FMath::IsNearlyZero(AxisValue) && CurrentTime - LastMouseInputDebugTimeSeconds >= 0.1f)
        {
            LastMouseInputDebugTimeSeconds = CurrentTime;
            SetSurveyInputDebug(FString::Printf(TEXT("MouseX %.2f enhanced"), AxisValue));
        }
    }
}

void ACSTopoPlayerController::HandleLookPitchInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        const float AxisValue = Value.Get<float>();
        SurveyPawn->ApplySurveyLookUp(AxisValue);
        const float CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;
        if (!FMath::IsNearlyZero(AxisValue) && CurrentTime - LastMouseInputDebugTimeSeconds >= 0.1f)
        {
            LastMouseInputDebugTimeSeconds = CurrentTime;
            SetSurveyInputDebug(FString::Printf(TEXT("MouseY %.2f enhanced"), AxisValue));
        }
    }
}

void ACSTopoPlayerController::HandleFlySpeedInput(const FInputActionValue& Value)
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    const float AxisValue = Value.Get<float>();
    if (FMath::IsNearlyZero(AxisValue))
    {
        return;
    }

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->ApplySurveyFlySpeedStep(AxisValue);
        SetSurveyInputDebug(FString::Printf(TEXT("Wheel %.1f enhanced"), AxisValue));
    }
}

void ACSTopoPlayerController::HandleToggleNavigationInput()
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    if (ACSTopoSurveyPawn* SurveyPawn = Cast<ACSTopoSurveyPawn>(GetPawn()))
    {
        SurveyPawn->ToggleNavigationMode();
        SetSurveyInputDebug(TEXT("F toggle enhanced"));
    }
}

void ACSTopoPlayerController::HandleToggleUserTinInput()
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    UCSTopoSurveySubsystem* Survey = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        return;
    }

    FString StatusMessage;
    if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
    {
        Survey->ToggleUserTinVisible(StatusMessage);
        SetSurveyInputDebug(TEXT("Shift+T user TIN enhanced"));
    }
    else
    {
        Survey->ToggleActiveSourceTinRenderVisible(StatusMessage);
        SetSurveyInputDebug(TEXT("T source TIN enhanced"));
    }

    if (GEngine != nullptr && !StatusMessage.IsEmpty())
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, StatusMessage);
    }
}

void ACSTopoPlayerController::HandleUndoInput()
{
    if (!bSurveyInputEnabled || (SurveyInputMode != ESurveyInputMode::Game && SurveyInputMode != ESurveyInputMode::CollectorUI))
    {
        return;
    }

    if (!IsInputKeyDown(EKeys::LeftControl) && !IsInputKeyDown(EKeys::RightControl))
    {
        return;
    }

    UndoLastMeasurementFromSurveyHud();
}

void ACSTopoPlayerController::HandleSprintStarted()
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    bSprintHeld = true;
    SetSurveyInputDebug(TEXT("Shift down enhanced"));
}

void ACSTopoPlayerController::HandleSprintCompleted()
{
    bSprintHeld = false;
    SetSurveyInputDebug(TEXT("Shift up enhanced"));
}

void ACSTopoPlayerController::HandleCollectShotInput()
{
    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        return;
    }

    CollectShotFromSurveyHud();
    SetSurveyInputDebug(TEXT("LeftMouse collect enhanced"));
}

void ACSTopoPlayerController::HandleEscapeInput()
{
    HandleEscapePressed();
    SetSurveyInputDebug(TEXT("Escape enhanced"));
}

void ACSTopoPlayerController::HandleTabInput()
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    ToggleSurveyCollectorFocus();
}

void ACSTopoPlayerController::ApplySurveyInputMode(bool bFlushPressedKeys)
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    if (bFlushPressedKeys && PlayerInput != nullptr)
    {
        PlayerInput->FlushPressedKeys();
    }

    if (SurveyStatusOverlay.IsValid())
    {
        SurveyStatusOverlay->SetVisibility(EVisibility::HitTestInvisible);
    }
    if (SurveyHud.IsValid())
    {
        SurveyHud->SetCollectorInteractive(false);
    }

    SurveyInputMode = ESurveyInputMode::Game;
    FInputModeGameOnly InputMode;
    SetInputMode(InputMode);
    bShowMouseCursor = false;
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
        FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
    }
    SetSurveyInputDebug(TEXT("game focus"));
}

void ACSTopoPlayerController::EnterSurveyGameMode()
{
    if (!bSurveyInputEnabled)
    {
        return;
    }

    SurveyInputMode = ESurveyInputMode::Game;
    ApplySurveyInputMode();
    QueueSurveyGameFocusRepair(CSTopoSurveyGameFocusRepairFrames);
}

void ACSTopoPlayerController::EnterCollectorMode()
{
    if (!bSurveyInputEnabled || !SurveyHud.IsValid())
    {
        return;
    }

    SurveyInputMode = ESurveyInputMode::CollectorUI;
    PendingSurveyGameFocusRepairFrames = 0;
    ResetHeldSurveyInput();
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    SetIgnoreMoveInput(true);
    SetIgnoreLookInput(true);

    SurveyHud->SetCollectorInteractive(true);
    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(SurveyHud);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
    bShowMouseCursor = true;
    SurveyHud->FocusCodeEditor();
    SetSurveyInputDebug(TEXT("collector focus"));
}

void ACSTopoPlayerController::EnterModalUIMode()
{
    SurveyInputMode = ESurveyInputMode::ModalUI;
    PendingSurveyGameFocusRepairFrames = 0;
    ResetHeldSurveyInput();
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    SetSurveyInputDebug(TEXT("modal ui"));
}

void ACSTopoPlayerController::QueueSurveyGameFocusRepair(int32 FrameCount)
{
    PendingSurveyGameFocusRepairFrames = FMath::Max(PendingSurveyGameFocusRepairFrames, FrameCount);
}

void ACSTopoPlayerController::TickSurveyGameFocusRepair()
{
    if (PendingSurveyGameFocusRepairFrames <= 0)
    {
        return;
    }

    if (!bSurveyInputEnabled || SurveyInputMode != ESurveyInputMode::Game)
    {
        PendingSurveyGameFocusRepairFrames = 0;
        return;
    }

    ApplySurveyInputMode(false);
    --PendingSurveyGameFocusRepairFrames;
}
