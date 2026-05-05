#include "CSTopoMainMenuSlate.h"

#include "CSTopoPointCloudToolbarSlate.h"
#include "CSTopoSurveySubsystem.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SCSTopoMainMenu::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;
    PointCloudToolbar = InArgs._PointCloudToolbar;

    ChildSlot
    [
        SNew(SConstraintCanvas)
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.0f, 0.0f))
        .Alignment(FVector2D(0.0f, 0.0f))
        .Offset(FMargin(6.0f, 6.0f, 0.0f, 0.0f))
        .AutoSize(true)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBox)
                .WidthOverride(54.0f)
                .HeightOverride(28.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("File")))
                    .OnClicked(this, &SCSTopoMainMenu::ToggleFileMenu)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
            [
                SNew(SBox)
                .WidthOverride(230.0f)
                .Visibility(this, &SCSTopoMainMenu::GetFileMenuVisibility)
                [
                    BuildFileMenu()
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> SCSTopoMainMenu::BuildFileMenu()
{
    return SNew(SBorder)
        .Padding(FMargin(6.0f))
        .BorderBackgroundColor(FLinearColor(0.035f, 0.04f, 0.048f, 0.98f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Import Point Cloud...")))
                .OnClicked(this, &SCSTopoMainMenu::ImportPointCloud)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Show/Hide Point Cloud Manager")))
                .OnClicked(this, &SCSTopoMainMenu::TogglePointCloudManager)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SButton)
                .Text(this, &SCSTopoMainMenu::GetCloudOverlayButtonText)
                .OnClicked(this, &SCSTopoMainMenu::ToggleCloudOverlay)
            ]
        ];
}

EVisibility SCSTopoMainMenu::GetFileMenuVisibility() const
{
    return bFileMenuOpen ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SCSTopoMainMenu::ToggleFileMenu()
{
    bFileMenuOpen = !bFileMenuOpen;
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
    if (TSharedPtr<SCSTopoPointCloudToolbar> Toolbar = PointCloudToolbar.Pin())
    {
        Toolbar->SetVisibility(Toolbar->GetVisibility() == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible);
    }

    return FReply::Handled();
}

FReply SCSTopoMainMenu::ToggleCloudOverlay()
{
    bFileMenuOpen = false;
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
