#include "CSTopoHomeScreenSlate.h"

#include "CSTopoSurveySubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void SCSTopoHomeScreen::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;
    OnOpenProject = InArgs._OnOpenProject;
    OnImportPointCloud = InArgs._OnImportPointCloud;
    OnRetrySurface = InArgs._OnRetrySurface;
    OnCancelToHome = InArgs._OnCancelToHome;

    ChildSlot
    [
        SNew(SBorder)
        .Padding(FMargin(0.0f))
        .BorderBackgroundColor(FLinearColor(0.015f, 0.017f, 0.02f, 0.98f))
        [
            SNew(SConstraintCanvas)
            + SConstraintCanvas::Slot()
            .Anchors(FAnchors(0.5f, 0.5f))
            .Alignment(FVector2D(0.5f, 0.5f))
            .AutoSize(true)
            [
                SNew(SBox)
                .WidthOverride(560.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("CSTopo")))
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(FSlateColor(FLinearColor::White))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 22.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Open a survey project or create one from a point cloud.")))
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.83f, 0.88f, 1.0f)))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Open Project...")))
                        .HAlign(HAlign_Center)
                        .OnClicked(this, &SCSTopoHomeScreen::OpenProjectClicked)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Import Point Cloud...")))
                        .HAlign(HAlign_Center)
                        .OnClicked(this, &SCSTopoHomeScreen::ImportPointCloudClicked)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        SNew(SSeparator)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SAssignNew(StatusText, STextBlock)
                        .AutoWrapText(true)
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.88f, 1.0f, 1.0f)))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        SAssignNew(DetailText, STextBlock)
                        .AutoWrapText(true)
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.82f, 0.86f, 1.0f)))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SNew(SVerticalBox)
                        .Visibility(this, &SCSTopoHomeScreen::GetProgressVisibility)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(SProgressBar)
                            .Percent(this, &SCSTopoHomeScreen::GetSurfaceBuildProgress)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                        [
                            SAssignNew(ProgressText, STextBlock)
                            .AutoWrapText(true)
                            .Justification(ETextJustify::Center)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.92f, 0.80f, 1.0f)))
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .HAlign(HAlign_Center)
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SNew(SHorizontalBox)
                        .Visibility(this, &SCSTopoHomeScreen::GetRetryVisibility)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Retry Surface Build")))
                            .OnClicked(this, &SCSTopoHomeScreen::RetrySurfaceClicked)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Cancel To Home")))
                            .OnClicked(this, &SCSTopoHomeScreen::CancelToHomeClicked)
                        ]
                    ]
                ]
            ]
        ]
    ];

    Refresh();
}

void SCSTopoHomeScreen::SetStatusMessage(const FString& Message)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Message));
    }
}

void SCSTopoHomeScreen::Refresh()
{
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        SetStatusMessage(TEXT("Survey subsystem is not available."));
        return;
    }

    SetStatusMessage(Survey->WorkflowStatus);

    FString Details;
    if (!Survey->ActiveProject.ActivePointCloudId.IsEmpty())
    {
        if (const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
        {
            return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
        }))
        {
            Details = FString::Printf(
                TEXT("%s\n%lld points | %s\n%s\n%s"),
                *Source->SourcePath,
                Source->PointCount,
                *Source->LinearUnitName,
                *Source->CacheStatus,
                *Source->SurfaceStatus);

            if (ProgressText.IsValid())
            {
                const int32 Percent = FMath::Clamp(FMath::RoundToInt(Source->SurfaceBuildProgress * 100.0f), 0, 100);
                const FString Stage = Source->SurfaceBuildProgressStage.IsEmpty() ? TEXT("Building Surface") : Source->SurfaceBuildProgressStage;
                const FString ProgressMessage = Source->SurfaceBuildProgressMessage.IsEmpty()
                    ? Source->SurfaceStatus
                    : Source->SurfaceBuildProgressMessage;
                ProgressText->SetText(FText::FromString(FString::Printf(TEXT("%d%% | %s\n%s"), Percent, *Stage, *ProgressMessage)));
            }
        }
    }

    if (DetailText.IsValid())
    {
        DetailText->SetText(FText::FromString(Details));
    }
}

EVisibility SCSTopoHomeScreen::GetProgressVisibility() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr || Survey->ActiveProject.ActivePointCloudId.IsEmpty())
    {
        return EVisibility::Collapsed;
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });

    return Source != nullptr && Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Building
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

TOptional<float> SCSTopoHomeScreen::GetSurfaceBuildProgress() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr || Survey->ActiveProject.ActivePointCloudId.IsEmpty())
    {
        return TOptional<float>();
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });

    if (Source == nullptr || Source->SurfaceBuildState != ECSTopoSurfaceBuildState::Building)
    {
        return TOptional<float>();
    }

    return FMath::Clamp(Source->SurfaceBuildProgress, 0.0f, 1.0f);
}

FReply SCSTopoHomeScreen::OpenProjectClicked()
{
    OnOpenProject.ExecuteIfBound();
    return FReply::Handled();
}

FReply SCSTopoHomeScreen::ImportPointCloudClicked()
{
    OnImportPointCloud.ExecuteIfBound();
    return FReply::Handled();
}

FReply SCSTopoHomeScreen::RetrySurfaceClicked()
{
    OnRetrySurface.ExecuteIfBound();
    return FReply::Handled();
}

FReply SCSTopoHomeScreen::CancelToHomeClicked()
{
    OnCancelToHome.ExecuteIfBound();
    return FReply::Handled();
}

EVisibility SCSTopoHomeScreen::GetRetryVisibility() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    return Survey != nullptr && Survey->WorkflowState == ECSTopoWorkflowState::Failed
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}
