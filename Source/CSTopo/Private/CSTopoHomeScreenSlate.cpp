#include "CSTopoHomeScreenSlate.h"

#include "CSTopoSurveySubsystem.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor HomeScreenBackgroundColor(0.0667f, 0.0667f, 0.0667f, 1.0f);
const FVector2D HomeScreenLogoSize(2520.0f, 2520.0f);
const int32 HomeScreenMainButtonFontSize = 24;
const int32 HomeScreenStatusFontSize = 20;
}

void SCSTopoHomeScreen::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;
    OnOpenProject = InArgs._OnOpenProject;
    OnImportPointCloud = InArgs._OnImportPointCloud;
    OnRetrySurface = InArgs._OnRetrySurface;
    OnCancelToHome = InArgs._OnCancelToHome;

    const FString LogoPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Logo"), TEXT("CSTopo_Logo1.png")));
    if (FPaths::FileExists(LogoPath))
    {
        LogoBrush = MakeShared<FSlateDynamicImageBrush>(FName(*LogoPath), HomeScreenLogoSize);
    }
    else
    {
        LogoBrush.Reset();
    }

    ChildSlot
    [
        SNew(SBorder)
        .Padding(FMargin(0.0f))
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(HomeScreenBackgroundColor)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(FMargin(32.0f, 16.0f, 32.0f, 8.0f))
            [
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(HomeScreenLogoSize.X)
                    .HeightOverride(HomeScreenLogoSize.Y)
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()
                        [
                            SNew(SImage)
                            .Image(LogoBrush.IsValid() ? LogoBrush.Get() : nullptr)
                            .ColorAndOpacity(FSlateColor(FLinearColor::White))
                            .Visibility_Lambda([this]()
                            {
                                return LogoBrush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                            })
                        ]
                        + SOverlay::Slot()
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("CSTopo")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 72))
                            .Justification(ETextJustify::Center)
                            .ColorAndOpacity(FSlateColor(FLinearColor::White))
                            .Visibility_Lambda([this]()
                            {
                                return LogoBrush.IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                            })
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Center)
            .Padding(FMargin(0.0f, 0.0f, 0.0f, 42.0f))
            [
                SNew(SBox)
                .WidthOverride(720.0f)
                [
                    SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                        [
                            SNew(SButton)
                            .HAlign(HAlign_Center)
                            .ContentPadding(FMargin(16.0f, 10.0f))
                            .OnClicked(this, &SCSTopoHomeScreen::OpenProjectClicked)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Open Project...")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenMainButtonFontSize))
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
                        [
                            SNew(SButton)
                            .HAlign(HAlign_Center)
                            .ContentPadding(FMargin(16.0f, 10.0f))
                            .OnClicked(this, &SCSTopoHomeScreen::ImportPointCloudClicked)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Import Point Cloud...")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenMainButtonFontSize))
                                .Justification(ETextJustify::Center)
                            ]
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
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenStatusFontSize))
                            .Justification(ETextJustify::Center)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.88f, 1.0f, 1.0f)))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
                        [
                            SAssignNew(DetailText, STextBlock)
                            .AutoWrapText(true)
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenStatusFontSize))
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
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenStatusFontSize))
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
                                .ContentPadding(FMargin(12.0f, 8.0f))
                                .OnClicked(this, &SCSTopoHomeScreen::RetrySurfaceClicked)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Retry Surface Build")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenStatusFontSize))
                                    .Justification(ETextJustify::Center)
                                ]
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(SButton)
                                .ContentPadding(FMargin(12.0f, 8.0f))
                                .OnClicked(this, &SCSTopoHomeScreen::CancelToHomeClicked)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Cancel To Home")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", HomeScreenStatusFontSize))
                                    .Justification(ETextJustify::Center)
                                ]
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
