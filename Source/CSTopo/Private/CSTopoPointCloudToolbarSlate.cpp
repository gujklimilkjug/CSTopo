#include "CSTopoPointCloudToolbarSlate.h"

#include "CSTopoSurveySubsystem.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void SCSTopoPointCloudToolbar::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;

    ChildSlot
    [
        SNew(SConstraintCanvas)
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.0f, 0.0f))
        .Alignment(FVector2D(0.0f, 0.0f))
        .Offset(FMargin(8.0f, 42.0f, 0.0f, 0.0f))
        .AutoSize(true)
        [
            SNew(SBox)
            .WidthOverride(520.0f)
            .MaxDesiredHeight(520.0f)
            .Padding(FMargin(0.0f))
            [
                SNew(SBorder)
                .Padding(FMargin(10.0f))
                .BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.03f, 0.92f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Point Clouds")))
                        .ColorAndOpacity(FSlateColor(FLinearColor::White))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        SAssignNew(CodePaletteBox, SWrapBox)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
                        [
                            SAssignNew(SourcePathBox, SEditableTextBox)
                            .HintText(FText::FromString(TEXT("KT-192.las or absolute LAS/LAZ/COPC path")))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Add")))
                            .OnClicked(this, &SCSTopoPointCloudToolbar::AddCloud)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Refresh")))
                            .OnClicked(this, &SCSTopoPointCloudToolbar::RefreshClouds)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    .Padding(FMargin(0.0f, 8.0f, 0.0f, 8.0f))
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(CloudListBox, SVerticalBox)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Recent Shots")))
                        .ColorAndOpacity(FSlateColor(FLinearColor::White))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .MaxHeight(120.0f)
                    .Padding(FMargin(0.0f, 4.0f, 0.0f, 6.0f))
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(ShotListBox, SVerticalBox)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SAssignNew(StatusText, STextBlock)
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.9f, 1.0f, 1.0f)))
                    ]
                ]
            ]
        ]
    ];

    RefreshList();
}

void SCSTopoPointCloudToolbar::RefreshList()
{
    RefreshCodePalette();
    RefreshShotList();

    if (!CloudListBox.IsValid())
    {
        return;
    }

    CloudListBox->ClearChildren();

    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        SetStatusMessage(TEXT("Survey subsystem is not available."));
        return;
    }

    const TArray<FCSTopoPointCloudSource> Clouds = Survey->GetPointClouds();
    if (Clouds.IsEmpty())
    {
        SetStatusMessage(TEXT("No point clouds loaded. Add KT-192.las to test the toolbar."));
        return;
    }

    for (const FCSTopoPointCloudSource& Source : Clouds)
    {
        CloudListBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
        [
            BuildCloudRow(Source)
        ];
    }

    SetStatusMessage(FString::Printf(TEXT("%d point cloud(s) in project."), Clouds.Num()));
}

void SCSTopoPointCloudToolbar::RefreshCodePalette()
{
    if (!CodePaletteBox.IsValid())
    {
        return;
    }

    CodePaletteBox->ClearChildren();

    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    for (const FCSTopoCodeStyle& Style : Survey->ActiveProject.CodePalette)
    {
        CodePaletteBox->AddSlot()
        .Padding(FMargin(0.0f, 0.0f, 4.0f, 4.0f))
        [
            BuildCodeButton(Style)
        ];
    }
}

void SCSTopoPointCloudToolbar::RefreshShotList()
{
    if (!ShotListBox.IsValid())
    {
        return;
    }

    ShotListBox->ClearChildren();

    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    const TArray<FCSTopoShotRecord>& Shots = Survey->ActiveProject.Shots;
    if (Shots.IsEmpty())
    {
        ShotListBox->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No shots collected yet.")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.0f)))
        ];
        return;
    }

    const int32 FirstIndex = FMath::Max(0, Shots.Num() - 5);
    for (int32 Index = Shots.Num() - 1; Index >= FirstIndex; --Index)
    {
        const FCSTopoShotRecord& Shot = Shots[Index];
        ShotListBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(
                TEXT("%d  %s  N %.3f  E %.3f  Z %.3f"),
                Shot.PointNumber,
                *Shot.Code,
                Shot.Northing,
                Shot.Easting,
                Shot.Elevation)))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.9f, 0.96f, 1.0f)))
        ];
    }
}

void SCSTopoPointCloudToolbar::SetStatusMessage(const FString& Message)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Message));
    }
}

FReply SCSTopoPointCloudToolbar::AddCloud()
{
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        SetStatusMessage(TEXT("Survey subsystem is not available."));
        return FReply::Handled();
    }

    const FString SourcePath = SourcePathBox.IsValid() ? SourcePathBox->GetText().ToString() : FString();
    if (SourcePath.IsEmpty())
    {
        SetStatusMessage(TEXT("Enter a LAS/LAZ/COPC path first."));
        return FReply::Handled();
    }

    FCSTopoPointCloudSource AddedSource;
    FString ErrorMessage;
    if (!Survey->ImportPointCloud(SourcePath, AddedSource, ErrorMessage))
    {
        SetStatusMessage(ErrorMessage);
        RefreshList();
        return FReply::Handled();
    }

    SetStatusMessage(FString::Printf(TEXT("Imported and displayed %s"), *FPaths::GetCleanFilename(SourcePath)));
    RefreshList();
    return FReply::Handled();
}

FReply SCSTopoPointCloudToolbar::RefreshClouds()
{
    RefreshList();
    return FReply::Handled();
}

TSharedRef<SWidget> SCSTopoPointCloudToolbar::BuildCodeButton(const FCSTopoCodeStyle& Style)
{
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    const bool bIsActive = Survey != nullptr && Survey->ActiveProject.ActiveCode.Equals(Style.Code, ESearchCase::IgnoreCase);
    const FString Label = bIsActive ? FString::Printf(TEXT("* %s"), *Style.Code) : Style.Code;

    return SNew(SButton)
        .Text(FText::FromString(Label))
        .ButtonColorAndOpacity(Style.Color)
        .OnClicked_Lambda([this, Code = Style.Code]()
        {
            if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
            {
                Survey->SetActiveCode(Code);
            }
            RefreshList();
            return FReply::Handled();
        });
}

TSharedRef<SWidget> SCSTopoPointCloudToolbar::BuildCloudRow(const FCSTopoPointCloudSource& Source)
{
    const FString Name = FPaths::GetCleanFilename(Source.SourcePath);
    const FString UnitSummary = Source.LinearUnitName.IsEmpty() || Source.LinearUnitName.Equals(TEXT("unknown"), ESearchCase::IgnoreCase)
        ? TEXT("units unknown")
        : FString::Printf(TEXT("%s"), *Source.LinearUnitName);
    const FString Summary = FString::Printf(
        TEXT("%s%s | %lld pts | %s | %s | %s"),
        Source.bIsActive ? TEXT("* ") : TEXT(""),
        *Name,
        Source.PointCount,
        Source.bLoaded ? TEXT("Loaded") : TEXT("Unloaded"),
        Source.bVisible ? TEXT("Visible") : TEXT("Hidden"),
        *UnitSummary);

    return SNew(SBorder)
        .Padding(FMargin(8.0f))
        .BorderBackgroundColor(FLinearColor(0.11f, 0.12f, 0.14f, 0.95f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Summary))
                .AutoWrapText(true)
                .ColorAndOpacity(FSlateColor(FLinearColor::White))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("Cache: %s"), Source.CachePath.IsEmpty() ? TEXT("(none)") : *Source.CachePath)))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("Runtime: %s"), Source.RuntimeDisplayPath.IsEmpty() ? TEXT("(none)") : *Source.RuntimeDisplayPath)))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Source.CacheStatus))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.80f, 0.84f, 0.55f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Source.RuntimeWindowStatus))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.60f, 0.86f, 0.96f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("Surface: %s"), *Source.SurfaceStatus)))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.95f, 0.72f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                [
                    SNew(SVerticalBox)
                    .Visibility(this, &SCSTopoPointCloudToolbar::GetSurfaceBuildProgressVisibility, Source.SourceId)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SProgressBar)
                        .Percent(this, &SCSTopoPointCloudToolbar::GetSurfaceBuildProgress, Source.SourceId)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                    [
                        SNew(STextBlock)
                        .Text(this, &SCSTopoPointCloudToolbar::GetSurfaceBuildProgressText, Source.SourceId)
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.92f, 0.80f, 1.0f)))
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SWrapBox)
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Active")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetActivePointCloud(Source.SourceId);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(Source.bVisible ? TEXT("Hide") : TEXT("Show")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetPointCloudVisible(Source.SourceId, !Source.bVisible);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(Source.bLoaded ? TEXT("Unload") : TEXT("Load")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetPointCloudLoaded(Source.SourceId, !Source.bLoaded);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("COPC")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        FString Message;
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->StartCopcCacheBuild(Source.SourceId, Message);
                        }
                        else
                        {
                            Message = TEXT("Survey subsystem is not available.");
                        }
                        SetStatusMessage(Message);
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(Source.SurfaceBuildState == ECSTopoSurfaceBuildState::Ready ? TEXT("Rebuild Surface") : TEXT("Build Surface")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        FString Message;
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->StartSurfaceBuild(Source.SourceId, Source.SurfaceBuildState == ECSTopoSurfaceBuildState::Ready, Message);
                        }
                        else
                        {
                            Message = TEXT("Survey subsystem is not available.");
                        }
                        SetStatusMessage(Message);
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(Source.bSurfacePrimary ? TEXT("Hide Surface") : TEXT("Show Surface")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetSurfaceVisible(Source.SourceId, !Source.bSurfacePrimary);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(Source.bCloudOverlayVisible ? TEXT("Hide Overlay") : TEXT("Show Overlay")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetCloudOverlayVisible(Source.SourceId, !Source.bCloudOverlayVisible);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("View Mode")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        FString Message;
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->SetActivePointCloud(Source.SourceId);
                            Survey->CycleActiveViewMode(Message);
                        }
                        SetStatusMessage(Message);
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
                + SWrapBox::Slot()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Remove")))
                    .OnClicked_Lambda([this, Source]()
                    {
                        if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
                        {
                            Survey->RemovePointCloud(Source.SourceId);
                        }
                        RefreshList();
                        return FReply::Handled();
                    })
                ]
            ]
        ];
}

EVisibility SCSTopoPointCloudToolbar::GetSurfaceBuildProgressVisibility(FString SourceId) const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return EVisibility::Collapsed;
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([&SourceId](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == SourceId;
    });

    return Source != nullptr && Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Building
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

TOptional<float> SCSTopoPointCloudToolbar::GetSurfaceBuildProgress(FString SourceId) const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return TOptional<float>();
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([&SourceId](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == SourceId;
    });

    if (Source == nullptr || Source->SurfaceBuildState != ECSTopoSurfaceBuildState::Building)
    {
        return TOptional<float>();
    }

    return FMath::Clamp(Source->SurfaceBuildProgress, 0.0f, 1.0f);
}

FText SCSTopoPointCloudToolbar::GetSurfaceBuildProgressText(FString SourceId) const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::GetEmpty();
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([&SourceId](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == SourceId;
    });

    if (Source == nullptr)
    {
        return FText::GetEmpty();
    }

    const int32 Percent = FMath::Clamp(FMath::RoundToInt(Source->SurfaceBuildProgress * 100.0f), 0, 100);
    const FString Stage = Source->SurfaceBuildProgressStage.IsEmpty() ? TEXT("Building Surface") : Source->SurfaceBuildProgressStage;
    const FString Message = Source->SurfaceBuildProgressMessage.IsEmpty() ? Source->SurfaceStatus : Source->SurfaceBuildProgressMessage;
    return FText::FromString(FString::Printf(TEXT("%d%% | %s | %s"), Percent, *Stage, *Message));
}
