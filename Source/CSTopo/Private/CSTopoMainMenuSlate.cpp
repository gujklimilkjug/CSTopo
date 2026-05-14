#include "CSTopoMainMenuSlate.h"

#include "CSTopoPointCloudImport.h"
#include "CSTopoPointCloudToolbarSlate.h"
#include "CSTopoSurveySubsystem.h"
#include "DesktopPlatformModule.h"
#include "Engine/Engine.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor MenuBarColor(0.925f, 0.925f, 0.925f, 0.96f);
const FLinearColor MenuDropDownColor(0.975f, 0.975f, 0.975f, 0.99f);
const FLinearColor MenuTextColor(0.02f, 0.02f, 0.02f, 1.0f);
const FLinearColor MenuMutedTextColor(0.30f, 0.30f, 0.30f, 1.0f);
const FLinearColor MenuAccentTextColor(0.03f, 0.22f, 0.40f, 1.0f);
const FLinearColor MenuBorderColor(0.55f, 0.55f, 0.55f, 1.0f);
}

void SCSTopoMainMenu::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;
    PointCloudToolbar = InArgs._PointCloudToolbar;
    OnNewProject = InArgs._OnNewProject;
    OnOpenProject = InArgs._OnOpenProject;
    OnExit = InArgs._OnExit;

    ChildSlot
    [
        SNew(SConstraintCanvas)
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f))
        .Alignment(FVector2D(0.0f, 0.0f))
        .Offset(FMargin(0.0f, 0.0f, 0.0f, 26.0f))
        [
            SNew(SBorder)
            .Padding(FMargin(4.0f, 1.0f))
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(MenuBarColor)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 2.0f))
                    .OnClicked(this, &SCSTopoMainMenu::ToggleFileMenu)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("File")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 2.0f))
                    .OnClicked(this, &SCSTopoMainMenu::ToggleOptionsMenu)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Options")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
            ]
        ]
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.0f, 0.0f))
        .Alignment(FVector2D(0.0f, 0.0f))
        .Offset(FMargin(4.0f, 26.0f, 0.0f, 0.0f))
        .AutoSize(true)
        [
            SNew(SBox)
            .WidthOverride(238.0f)
            .Visibility(this, &SCSTopoMainMenu::GetFileMenuVisibility)
            [
                BuildFileMenu()
            ]
        ]
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.0f, 0.0f))
        .Alignment(FVector2D(0.0f, 0.0f))
        .Offset(FMargin(44.0f, 26.0f, 0.0f, 0.0f))
        .AutoSize(true)
        [
            SNew(SBox)
            .WidthOverride(404.0f)
            .Visibility(this, &SCSTopoMainMenu::GetOptionsMenuVisibility)
            [
                BuildOptionsMenu()
            ]
        ]
    ];
}

TSharedRef<SWidget> SCSTopoMainMenu::BuildFileMenu()
{
    return SNew(SBorder)
        .Padding(FMargin(1.0f))
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(MenuBorderColor)
        [
            SNew(SBorder)
            .Padding(FMargin(2.0f))
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(MenuDropDownColor)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::NewProject)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("New Project...")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::CheckPdalRuntime)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Check PDAL Runtime")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::OpenProject)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Open Project...")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::SaveProject)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Save")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::SaveProjectAs)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Save As...")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::ImportPointCloud)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Import Point Cloud...")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::TogglePointCloudManager)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Show/Hide Point Cloud Manager")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::ToggleCloudOverlay)
                    [
                        SNew(STextBlock)
                        .Text(this, &SCSTopoMainMenu::GetCloudOverlayButtonText)
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .OnClicked(this, &SCSTopoMainMenu::ExitApplication)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Exit")))
                        .ColorAndOpacity(MenuTextColor)
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> SCSTopoMainMenu::BuildOptionsMenu()
{
    auto BuildNumberRow = [this](
        const TCHAR* Label,
        const TCHAR* Units,
        float MinValue,
        float MaxValue,
        float Delta,
        TFunction<float(const UCSTopoSurveySubsystem*)> Getter,
        TFunction<void(UCSTopoSurveySubsystem*, float)> Setter) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .ColorAndOpacity(MenuTextColor)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(118.0f)
                [
                    SNew(SNumericEntryBox<float>)
                    .AllowSpin(true)
                    .MinValue(MinValue)
                    .MaxValue(MaxValue)
                    .MinSliderValue(MinValue)
                    .MaxSliderValue(MaxValue)
                    .Delta(Delta)
                    .Value_Lambda([this, Getter]() -> TOptional<float>
                    {
                        const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
                        return Survey != nullptr ? TOptional<float>(Getter(Survey)) : TOptional<float>();
                    })
                    .OnValueChanged_Lambda([this, Setter](float Value)
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Setter(Survey, Value);
                        }
                    })
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Units))
                .ColorAndOpacity(MenuMutedTextColor)
            ];
    };

    return SNew(SBorder)
        .Padding(FMargin(1.0f))
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(MenuBorderColor)
        [
            SNew(SBorder)
            .Padding(FMargin(10.0f))
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(MenuDropDownColor)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Runtime Options")))
                    .ColorAndOpacity(MenuTextColor)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 8.0f, 0.0f, 4.0f))
                [
                    BuildNumberRow(
                        TEXT("Surface view range"),
                        TEXT("tiles"),
                        2.0f,
                        12.0f,
                        0.25f,
                        [](const UCSTopoSurveySubsystem* Survey) { return static_cast<float>(Survey->GetSurfaceViewTileMultiplier()); },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetSurfaceViewTileMultiplier(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(this, &SCSTopoMainMenu::GetSurfaceViewRadiusText)
                    .ColorAndOpacity(MenuAccentTextColor)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 10.0f, 0.0f, 4.0f))
                [
                    BuildNumberRow(
                        TEXT("Walk eye height"),
                        TEXT("cm"),
                        96.0f,
                        240.0f,
                        4.0f,
                        [](const UCSTopoSurveySubsystem* Survey) { return Survey->ActiveProject.NavigationSettings.WalkEyeHeight; },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetNavigationWalkEyeHeight(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 4.0f, 0.0f, 4.0f))
                [
                    BuildNumberRow(
                        TEXT("Walk speed"),
                        TEXT("cm/s"),
                        100.0f,
                        2400.0f,
                        25.0f,
                        [](const UCSTopoSurveySubsystem* Survey) { return Survey->ActiveProject.NavigationSettings.WalkSpeed; },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetNavigationWalkSpeed(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 4.0f, 0.0f, 4.0f))
                [
                    BuildNumberRow(
                        TEXT("Fly speed scale"),
                        TEXT("x"),
                        0.25f,
                        4.0f,
                        0.05f,
                        [](const UCSTopoSurveySubsystem* Survey) { return Survey->ActiveProject.NavigationSettings.FlySpeedScale; },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetNavigationFlySpeedScale(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 4.0f, 0.0f, 4.0f))
                [
                    BuildNumberRow(
                        TEXT("Look sensitivity"),
                        TEXT("x"),
                        0.05f,
                        2.0f,
                        0.05f,
                        [](const UCSTopoSurveySubsystem* Survey) { return Survey->ActiveProject.NavigationSettings.LookSensitivity; },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetNavigationLookSensitivity(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 4.0f, 0.0f, 10.0f))
                [
                    BuildNumberRow(
                        TEXT("Precision sensitivity"),
                        TEXT("x"),
                        0.05f,
                        1.0f,
                        0.05f,
                        [](const UCSTopoSurveySubsystem* Survey) { return Survey->ActiveProject.NavigationSettings.PrecisionSensitivity; },
                        [](UCSTopoSurveySubsystem* Survey, float Value) { Survey->SetNavigationPrecisionSensitivity(Value); })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .ContentPadding(FMargin(8.0f, 4.0f))
                    .Text(FText::FromString(TEXT("Reset Defaults")))
                    .OnClicked(this, &SCSTopoMainMenu::ResetRuntimeOptions)
                ]
            ]
        ];
}

EVisibility SCSTopoMainMenu::GetFileMenuVisibility() const
{
    return bFileMenuOpen ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SCSTopoMainMenu::GetOptionsMenuVisibility() const
{
    return bOptionsMenuOpen ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SCSTopoMainMenu::ToggleFileMenu()
{
    bFileMenuOpen = !bFileMenuOpen;
    if (bFileMenuOpen)
    {
        bOptionsMenuOpen = false;
    }
    return FReply::Handled();
}

FReply SCSTopoMainMenu::ToggleOptionsMenu()
{
    bOptionsMenuOpen = !bOptionsMenuOpen;
    if (bOptionsMenuOpen)
    {
        bFileMenuOpen = false;
    }
    return FReply::Handled();
}

FReply SCSTopoMainMenu::SaveProject()
{
    return SaveProjectToPath(false);
}

FReply SCSTopoMainMenu::SaveProjectAs()
{
    return SaveProjectToPath(true);
}

FReply SCSTopoMainMenu::SaveProjectToPath(bool bForceDialog)
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;

    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FReply::Handled();
    }

    FString ProjectPath = Survey->GetCurrentProjectPath();
    if (bForceDialog || ProjectPath.IsEmpty())
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (DesktopPlatform == nullptr)
        {
            return FReply::Handled();
        }

        TArray<FString> ProjectFiles;
        const FString DefaultDirectory = ProjectPath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(ProjectPath);
        const FString DefaultName = ProjectPath.IsEmpty() ? Survey->ActiveProject.ProjectName + TEXT(".cstopo") : FPaths::GetCleanFilename(ProjectPath);
        const bool bPicked = DesktopPlatform->SaveFileDialog(
            nullptr,
            TEXT("Save CSTopo Project"),
            DefaultDirectory,
            DefaultName,
            TEXT("CSTopo Projects (*.cstopo)|*.cstopo|All Files (*.*)|*.*"),
            EFileDialogFlags::None,
            ProjectFiles);
        if (!bPicked || ProjectFiles.IsEmpty())
        {
            return FReply::Handled();
        }

        ProjectPath = ProjectFiles[0];
        if (!ProjectPath.EndsWith(TEXT(".cstopo"), ESearchCase::IgnoreCase))
        {
            ProjectPath += TEXT(".cstopo");
        }
    }

    FString Message;
    const bool bSaved = Survey->SaveProject(ProjectPath, Message);
    const FString Status = bSaved
        ? FString::Printf(TEXT("Saved project: %s"), *FPaths::GetCleanFilename(ProjectPath))
        : FString::Printf(TEXT("Save failed: %s"), *Message);
    if (TSharedPtr<SCSTopoPointCloudToolbar> Toolbar = PointCloudToolbar.Pin())
    {
        Toolbar->SetStatusMessage(Status);
        Toolbar->RefreshList();
    }
    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.5f, bSaved ? FColor::Green : FColor::Red, Status);
    }

    return FReply::Handled();
}

FReply SCSTopoMainMenu::NewProject()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;
    if (OnNewProject.IsBound())
    {
        OnNewProject.Execute();
    }
    return FReply::Handled();
}

FReply SCSTopoMainMenu::OpenProject()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;
    if (OnOpenProject.IsBound())
    {
        OnOpenProject.Execute();
    }
    return FReply::Handled();
}

FReply SCSTopoMainMenu::ImportPointCloud()
{
    bFileMenuOpen = false;
    OpenImportPointCloudDialog();
    return FReply::Handled();
}

void SCSTopoMainMenu::OpenImportPointCloudDialog()
{
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform == nullptr)
    {
        return;
    }

    TArray<FString> SelectedFiles;
    const bool bPicked = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Import Point Cloud"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("Point Clouds (*.las;*.laz;*.copc.laz)|*.las;*.laz;*.copc.laz|All Files (*.*)|*.*"),
        EFileDialogFlags::None,
        SelectedFiles);

    if (!bPicked || SelectedFiles.IsEmpty())
    {
        return;
    }

    FCSTopoPointCloudSource ImportedSource;
    FString Message;
    Survey->ImportPointCloud(SelectedFiles[0], ImportedSource, Message);

    if (TSharedPtr<SCSTopoPointCloudToolbar> Toolbar = PointCloudToolbar.Pin())
    {
        Toolbar->SetStatusMessage(Message);
        Toolbar->RefreshList();
        Toolbar->SetVisibility(EVisibility::Visible);
    }
}

FReply SCSTopoMainMenu::TogglePointCloudManager()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;
    if (TSharedPtr<SCSTopoPointCloudToolbar> Toolbar = PointCloudToolbar.Pin())
    {
        Toolbar->SetVisibility(Toolbar->GetVisibility() == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible);
    }

    return FReply::Handled();
}

FReply SCSTopoMainMenu::ToggleCloudOverlay()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FReply::Handled();
    }

    FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });
    if (Source == nullptr)
    {
        return FReply::Handled();
    }

    FString StatusMessage;
    Survey->SetCloudOverlayVisibleForActiveSource(!Source->bCloudOverlayVisible, StatusMessage);
    if (TSharedPtr<SCSTopoPointCloudToolbar> Toolbar = PointCloudToolbar.Pin())
    {
        Toolbar->SetStatusMessage(StatusMessage);
        Toolbar->RefreshList();
    }

    return FReply::Handled();
}

FReply SCSTopoMainMenu::ExitApplication()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;
    if (OnExit.IsBound())
    {
        OnExit.Execute();
    }
    return FReply::Handled();
}

FReply SCSTopoMainMenu::CheckPdalRuntime()
{
    bFileMenuOpen = false;
    bOptionsMenuOpen = false;

    FString Message;
    UCSTopoPointCloudImport::EnsurePdalRuntimeAvailable(Message);
    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
    return FReply::Handled();
}

FReply SCSTopoMainMenu::ResetRuntimeOptions()
{
    if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        Survey->ResetRuntimeTuningOptions();
    }
    return FReply::Handled();
}

FText SCSTopoMainMenu::GetCloudOverlayButtonText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Show Point Cloud Overlay"));
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });

    const bool bOverlayVisible = Source != nullptr && Source->bCloudOverlayVisible;
    return FText::FromString(bOverlayVisible ? TEXT("Hide Point Cloud Overlay") : TEXT("Show Point Cloud Overlay"));
}

FText SCSTopoMainMenu::GetSurfaceViewRadiusText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Approx radius: no active project"));
    }

    return FText::FromString(FString::Printf(
        TEXT("Approx visible radius: %.0f source units"),
        Survey->GetApproxSurfaceViewRadiusSourceUnits()));
}
