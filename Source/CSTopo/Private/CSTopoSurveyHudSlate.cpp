#include "CSTopoSurveyHudSlate.h"

#include "CSTopoSurveySubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString NormalizeCollectorCode(const FString& InCode)
{
    FString Normalized = InCode.TrimStartAndEnd();
    Normalized.ToUpperInline();
    return Normalized;
}

FLinearColor GetCodeColor(const UCSTopoSurveySubsystem* Survey, const FString& Code)
{
    if (Survey == nullptr)
    {
        return FLinearColor::White;
    }

    FCSTopoCodeStyle Style;
    if (Survey->GetCodeStyle(Code, Style))
    {
        return Style.Color;
    }

    return FLinearColor::White;
}

bool ShouldRenderFigureLinework(const UCSTopoSurveySubsystem* Survey, const FCSTopoFigureRecord& Figure)
{
    FString PointType = Figure.Style.PointType;
    if (PointType.IsEmpty() && Survey != nullptr)
    {
        FCSTopoCodeStyle Style;
        if (Survey->GetCodeStyle(Figure.Code, Style))
        {
            PointType = Style.PointType;
        }
    }
    return DoesCSTopoPointTypeCreateFigureLinework(PointType);
}

bool IsControlCodeKnown(const UCSTopoSurveySubsystem* Survey, const FString& ControlCode, FCSTopoControlCodeDefinition* OutDefinition = nullptr)
{
    if (Survey == nullptr)
    {
        return false;
    }

    const FString NormalizedControl = NormalizeCollectorCode(ControlCode);
    for (const FCSTopoControlCodeDefinition& Definition : Survey->GetControlCodeDefinitions())
    {
        if (Definition.Code.Equals(NormalizedControl, ESearchCase::IgnoreCase))
        {
            if (OutDefinition != nullptr)
            {
                *OutDefinition = Definition;
            }
            return true;
        }
    }
    return false;
}

bool ControlCodeNeedsParameter(const FCSTopoControlCodeDefinition& Definition)
{
    return Definition.ParameterKind == ECSTopoControlParameterKind::Distance
        || Definition.ParameterKind == ECSTopoControlParameterKind::PointNumber
        || Definition.ParameterKind == ECSTopoControlParameterKind::DistanceOrPointNumber;
}

const FCSTopoShotRecord* FindShotByPointNumber(const TArray<FCSTopoShotRecord>& Shots, int32 PointNumber)
{
    return Shots.FindByPredicate([PointNumber](const FCSTopoShotRecord& Shot)
    {
        return Shot.PointNumber == PointNumber;
    });
}

FString GetShotTag(const FCSTopoShotRecord& Shot)
{
    switch (Shot.MeasurementSource)
    {
    case ECSTopoMeasurementSource::DerivedSurface:
        return TEXT("SURF");
    case ECSTopoMeasurementSource::RawPoint:
        return TEXT("RAW");
    case ECSTopoMeasurementSource::SurfaceFallback:
        return TEXT("FALL");
    case ECSTopoMeasurementSource::InterpolatedPoints:
    default:
        return TEXT("FIT");
    }
}

class SCSTopoSurveyMiniMap : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCSTopoSurveyMiniMap) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UCSTopoSurveySubsystem>, SurveySubsystem)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        SurveySubsystem = InArgs._SurveySubsystem;
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(392.0f, 240.0f);
    }

    virtual int32 OnPaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override
    {
        const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
        const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const FLinearColor BackgroundColor(0.035f, 0.055f, 0.07f, 1.0f);
        const FLinearColor GridColor(0.16f, 0.22f, 0.28f, 0.8f);
        const FLinearColor BorderColor(0.18f, 0.32f, 0.38f, 1.0f);

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId++,
            AllottedGeometry.ToPaintGeometry(),
            WhiteBrush,
            ESlateDrawEffect::None,
            BackgroundColor);

        const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
        const FVector2D Padding(12.0f, 12.0f);
        const FVector2D PlotOrigin = Padding;
        const FVector2D PlotSize = LocalSize - (Padding * 2.0f);
        if (PlotSize.X <= 8.0f || PlotSize.Y <= 8.0f)
        {
            return LayerId;
        }

        for (int32 GridIndex = 1; GridIndex < 4; ++GridIndex)
        {
            const float Alpha = static_cast<float>(GridIndex) / 4.0f;
            const float X = PlotOrigin.X + PlotSize.X * Alpha;
            const float Y = PlotOrigin.Y + PlotSize.Y * Alpha;
            FSlateDrawElement::MakeLines(
                OutDrawElements,
                LayerId,
                AllottedGeometry.ToPaintGeometry(),
                TArray<FVector2D>{FVector2D(X, PlotOrigin.Y), FVector2D(X, PlotOrigin.Y + PlotSize.Y)},
                ESlateDrawEffect::None,
                GridColor,
                true,
                1.0f);
            FSlateDrawElement::MakeLines(
                OutDrawElements,
                LayerId,
                AllottedGeometry.ToPaintGeometry(),
                TArray<FVector2D>{FVector2D(PlotOrigin.X, Y), FVector2D(PlotOrigin.X + PlotSize.X, Y)},
                ESlateDrawEffect::None,
                GridColor,
                true,
                1.0f);
        }
        ++LayerId;

        if (Survey == nullptr)
        {
            return LayerId;
        }

        double MinNorthing = TNumericLimits<double>::Max();
        double MaxNorthing = TNumericLimits<double>::Lowest();
        double MinEasting = TNumericLimits<double>::Max();
        double MaxEasting = TNumericLimits<double>::Lowest();
        bool bHasShotExtent = false;

        for (const FCSTopoShotRecord& Shot : Survey->ActiveProject.Shots)
        {
            bHasShotExtent = true;
            MinNorthing = FMath::Min(MinNorthing, Shot.Northing);
            MaxNorthing = FMath::Max(MaxNorthing, Shot.Northing);
            MinEasting = FMath::Min(MinEasting, Shot.Easting);
            MaxEasting = FMath::Max(MaxEasting, Shot.Easting);
        }
        for (const FCSTopoFigureSegmentRecord& Segment : Survey->ActiveProject.FigureSegments)
        {
            for (const FVector& SurveyPoint : Segment.SurveyPoints)
            {
                bHasShotExtent = true;
                MinNorthing = FMath::Min(MinNorthing, static_cast<double>(SurveyPoint.X));
                MaxNorthing = FMath::Max(MaxNorthing, static_cast<double>(SurveyPoint.X));
                MinEasting = FMath::Min(MinEasting, static_cast<double>(SurveyPoint.Y));
                MaxEasting = FMath::Max(MaxEasting, static_cast<double>(SurveyPoint.Y));
            }
        }

        if (!bHasShotExtent)
        {
            FVector2D BoundsMin;
            FVector2D BoundsMax;
            if (!Survey->GetActiveSurveyBounds(BoundsMin, BoundsMax))
            {
                return LayerId;
            }

            MinNorthing = BoundsMin.X;
            MaxNorthing = BoundsMax.X;
            MinEasting = BoundsMin.Y;
            MaxEasting = BoundsMax.Y;
        }

        const double NorthingPadding = FMath::Max((MaxNorthing - MinNorthing) * 0.08, 1.0);
        const double EastingPadding = FMath::Max((MaxEasting - MinEasting) * 0.08, 1.0);
        MinNorthing -= NorthingPadding;
        MaxNorthing += NorthingPadding;
        MinEasting -= EastingPadding;
        MaxEasting += EastingPadding;

        const double NorthingRange = FMath::Max(MaxNorthing - MinNorthing, 1.0);
        const double EastingRange = FMath::Max(MaxEasting - MinEasting, 1.0);
        const double Scale = FMath::Min(
            static_cast<double>(PlotSize.X) / EastingRange,
            static_cast<double>(PlotSize.Y) / NorthingRange);
        const double UsedWidth = EastingRange * Scale;
        const double UsedHeight = NorthingRange * Scale;
        const double OffsetX = PlotOrigin.X + (PlotSize.X - UsedWidth) * 0.5;
        const double OffsetY = PlotOrigin.Y + (PlotSize.Y - UsedHeight) * 0.5;

        auto MapSurveyPoint = [&](double Northing, double Easting)
        {
            const double X = OffsetX + (Easting - MinEasting) * Scale;
            const double Y = OffsetY + UsedHeight - (Northing - MinNorthing) * Scale;
            return FVector2D(static_cast<float>(X), static_cast<float>(Y));
        };

        if (!Survey->ActiveProject.FigureSegments.IsEmpty())
        {
            for (const FCSTopoFigureSegmentRecord& Segment : Survey->ActiveProject.FigureSegments)
            {
                if (Segment.SurveyPoints.Num() < 2)
                {
                    continue;
                }

                TArray<FVector2D> Points;
                Points.Reserve(Segment.SurveyPoints.Num());
                for (const FVector& SurveyPoint : Segment.SurveyPoints)
                {
                    Points.Add(MapSurveyPoint(SurveyPoint.X, SurveyPoint.Y));
                }

                const bool bSpecialGeometry = Segment.SegmentKind != ECSTopoFigureSegmentKind::Line;
                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    LayerId,
                    AllottedGeometry.ToPaintGeometry(),
                    Points,
                    ESlateDrawEffect::None,
                    bSpecialGeometry ? FLinearColor(0.95f, 0.76f, 0.28f, 1.0f) : GetCodeColor(Survey, Segment.Code),
                    true,
                    bSpecialGeometry ? 2.4f : 2.0f);
            }
        }
        else
        {
            for (const FCSTopoFigureRecord& Figure : Survey->ActiveProject.Figures)
            {
                if (Figure.PointNumbers.Num() < 2 || !ShouldRenderFigureLinework(Survey, Figure))
                {
                    continue;
                }

                TArray<FVector2D> Points;
                Points.Reserve(Figure.PointNumbers.Num() + (Figure.bLoopClosed ? 1 : 0));
                for (int32 PointNumber : Figure.PointNumbers)
                {
                    if (const FCSTopoShotRecord* Shot = FindShotByPointNumber(Survey->ActiveProject.Shots, PointNumber))
                    {
                        Points.Add(MapSurveyPoint(Shot->Northing, Shot->Easting));
                    }
                }

                if (Figure.bLoopClosed && Points.Num() >= 2)
                {
                    Points.Add(Points[0]);
                }

                if (Points.Num() >= 2)
                {
                    FSlateDrawElement::MakeLines(
                        OutDrawElements,
                        LayerId,
                        AllottedGeometry.ToPaintGeometry(),
                        Points,
                        ESlateDrawEffect::None,
                        GetCodeColor(Survey, Figure.Code),
                        true,
                        2.0f);
                }
            }
        }
        ++LayerId;

        for (const FCSTopoShotRecord& Shot : Survey->ActiveProject.Shots)
        {
            const FVector2D Point = MapSurveyPoint(Shot.Northing, Shot.Easting);
            FSlateDrawElement::MakeBox(
                OutDrawElements,
                LayerId,
                AllottedGeometry.ToPaintGeometry(
                    FVector2f(3.0f, 3.0f),
                    FSlateLayoutTransform(FVector2f(Point.X - 1.5f, Point.Y - 1.5f))),
                WhiteBrush,
                ESlateDrawEffect::None,
                GetCodeColor(Survey, Shot.BaseCode.IsEmpty() ? Shot.Code : Shot.BaseCode));
        }
        ++LayerId;

        FVector2D CurrentSurveyNe;
        FVector2D Heading;
        if (Survey->GetCurrentSurveyMapPose(CurrentSurveyNe, Heading))
        {
            const FVector2D Position = MapSurveyPoint(CurrentSurveyNe.X, CurrentSurveyNe.Y);
            const FVector2D Direction = Heading.GetSafeNormal();
            const FVector2D Perp(-Direction.Y, Direction.X);
            const float MarkerLength = 14.0f;
            const float MarkerWidth = 8.0f;

            const FVector2D Tip = Position + Direction * MarkerLength;
            const FVector2D Tail = Position - Direction * (MarkerLength * 0.45f);
            const FVector2D Left = Tail + Perp * MarkerWidth;
            const FVector2D Right = Tail - Perp * MarkerWidth;

            FSlateDrawElement::MakeLines(
                OutDrawElements,
                LayerId,
                AllottedGeometry.ToPaintGeometry(),
                TArray<FVector2D>{Tip, Left, Tail, Right, Tip},
                ESlateDrawEffect::None,
                FLinearColor(0.96f, 0.97f, 1.0f, 1.0f),
                true,
                2.0f);
            FSlateDrawElement::MakeBox(
                OutDrawElements,
                LayerId + 1,
                AllottedGeometry.ToPaintGeometry(
                    FVector2f(5.0f, 5.0f),
                    FSlateLayoutTransform(FVector2f(Position.X - 2.5f, Position.Y - 2.5f))),
                WhiteBrush,
                ESlateDrawEffect::None,
                BorderColor);
        }

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId + 2,
            AllottedGeometry.ToPaintGeometry(),
            WhiteBrush,
            ESlateDrawEffect::None,
            FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

        return LayerId + 3;
    }

private:
    TWeakObjectPtr<UCSTopoSurveySubsystem> SurveySubsystem;
};
}

void SCSTopoSurveyHud::Construct(const FArguments& InArgs)
{
    SurveySubsystem = InArgs._SurveySubsystem;
    OnCollectShot = InArgs._OnCollectShot;
    OnToggleFocus = InArgs._OnToggleFocus;
    OnUndo = InArgs._OnUndo;
    ActionStatusMessage = TEXT("Tab focuses the collector. Ctrl+Tab opens the point-cloud manager.");

    ChildSlot
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNullWidget::NullWidget
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Top)
        .Padding(FMargin(0.0f, 12.0f, 8.0f, 12.0f))
        [
            SNew(SBox)
            .WidthOverride(154.0f)
            .MaxDesiredHeight(720.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(10.0f))
                .BorderBackgroundColor(FLinearColor(0.035f, 0.045f, 0.06f, 0.96f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        BuildSectionHeader(TEXT("CONTROL CODES"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                    [
                        SAssignNew(ControlCodeBox, SWrapBox)
                    ]
                ]
            ]
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Top)
        .Padding(FMargin(0.0f, 12.0f, 12.0f, 12.0f))
        [
            SNew(SBox)
            .WidthOverride(420.0f)
            .MaxDesiredHeight(720.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f))
                .BorderBackgroundColor(FLinearColor(0.018f, 0.022f, 0.028f, 0.95f))
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.06f, 0.09f, 0.115f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("CSTOPO COLLECTOR"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetProjectNameText)
                                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetActiveSourceText)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.76f, 0.84f, 0.9f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot()
                                    .FillWidth(1.0f)
                                    [
                                        SNew(STextBlock)
                                        .Text(this, &SCSTopoSurveyHud::GetNavigationModeText)
                                        .ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.95f, 1.0f, 1.0f)))
                                    ]
                                    + SHorizontalBox::Slot()
                                    .AutoWidth()
                                    [
                                        SNew(STextBlock)
                                        .Text(this, &SCSTopoSurveyHud::GetCollectorPointIdText)
                                        .ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.82f, 0.25f, 1.0f)))
                                    ]
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetSurveySummaryText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.66f, 0.89f, 0.98f, 1.0f)))
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.045f, 0.055f, 0.07f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("CODE"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot()
                                    .FillWidth(1.0f)
                                    .Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
                                    [
                                        SNew(SOverlay)
                                        + SOverlay::Slot()
                                        [
                                            SNew(SBorder)
                                            .Padding(FMargin(8.0f, 5.0f))
                                            .BorderBackgroundColor(FLinearColor(0.01f, 0.012f, 0.014f, 0.88f))
                                            .Visibility_Lambda([this]()
                                            {
                                                return bCollectorInteractive ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                                            })
                                            [
                                                SNew(STextBlock)
                                                .Text(this, &SCSTopoSurveyHud::GetActiveCodeText)
                                                .ColorAndOpacity(FSlateColor(FLinearColor::White))
                                            ]
                                        ]
                                        + SOverlay::Slot()
                                        [
                                            SAssignNew(CodeTextBox, SEditableTextBox)
                                            .HintText(FText::FromString(TEXT("Code")))
                                            .Visibility_Lambda([this]()
                                            {
                                                return bCollectorInteractive ? EVisibility::Visible : EVisibility::Collapsed;
                                            })
                                            .OnTextChanged(this, &SCSTopoSurveyHud::HandleCodeTextChanged)
                                            .OnTextCommitted(this, &SCSTopoSurveyHud::HandleCodeTextCommitted)
                                        ]
                                    ]
                                    + SHorizontalBox::Slot()
                                    .AutoWidth()
                                    [
                                        SNew(SBorder)
                                        .Padding(FMargin(10.0f, 7.0f))
                                        .BorderBackgroundColor(this, &SCSTopoSurveyHud::GetActiveCodeSwatchColor)
                                        [
                                            SNew(STextBlock)
                                            .Text(FText::FromString(TEXT("ACTIVE")))
                                            .ColorAndOpacity(FSlateColor(FLinearColor(0.04f, 0.05f, 0.06f, 1.0f)))
                                        ]
                                    ]
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetActivePointTypeText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.9f, 0.96f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetActiveCodeDetailText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.76f, 0.84f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SAssignNew(CodeSearchBox, SEditableTextBox)
                                    .HintText(FText::FromString(TEXT("Search category, name, code, or point type")))
                                    .Visibility_Lambda([this]()
                                    {
                                        return bCollectorInteractive ? EVisibility::Visible : EVisibility::HitTestInvisible;
                                    })
                                    .OnTextChanged(this, &SCSTopoSurveyHud::HandleCodeFilterChanged)
                                    .OnTextCommitted(this, &SCSTopoSurveyHud::HandleCodeFilterCommitted)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SAssignNew(QuickCodeBox, SWrapBox)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetFigureStatusText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.77f, 0.82f, 0.9f, 1.0f)))
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("MEASURE"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot()
                                    .FillWidth(1.0f)
                                    .Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
                                    [
                                        SNew(SButton)
                                        .Text(FText::FromString(TEXT("Collect Shot")))
                                        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
                                        .ButtonColorAndOpacity(FLinearColor(0.14f, 0.44f, 0.28f, 1.0f))
                                        .OnClicked(this, &SCSTopoSurveyHud::HandleCollectShot)
                                    ]
                                    + SHorizontalBox::Slot()
                                    .AutoWidth()
                                    [
                                        SNew(SButton)
                                        .Text(FText::FromString(TEXT("Back")))
                                        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
                                        .OnClicked(this, &SCSTopoSurveyHud::HandleUndo)
                                    ]
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetActionStatusText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.9f, 0.96f, 1.0f)))
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.045f, 0.05f, 0.065f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("RECENT SHOTS"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SAssignNew(RecentShotBox, SVerticalBox)
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.045f, 0.055f, 0.07f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("MINIMAP"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(SBox)
                                    .HeightOverride(240.0f)
                                    [
                                        SNew(SCSTopoSurveyMiniMap)
                                        .SurveySubsystem(SurveySubsystem)
                                    ]
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(SBorder)
                            .Padding(FMargin(10.0f))
                            .BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.07f, 1.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("FEEDBACK"))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetSurveySummaryText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.74f, 0.92f, 1.0f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetHoverText)
                                    .AutoWrapText(true)
                                    .ColorAndOpacity(this, &SCSTopoSurveyHud::GetHoverColor)
                                ]
                            ]
                        ]
                    ]
                ]
            ]
        ]
    ];

    SyncCodeText();
    RefreshDynamicContent();
}

FReply SCSTopoSurveyHud::OnFocusReceived(const FGeometry&, const FFocusEvent&)
{
    if (bCollectorInteractive && CodeSearchBox.IsValid())
    {
        return FReply::Handled().SetUserFocus(CodeSearchBox.ToSharedRef(), EFocusCause::SetDirectly);
    }

    if (bCollectorInteractive && CodeTextBox.IsValid())
    {
        return FReply::Handled().SetUserFocus(CodeTextBox.ToSharedRef(), EFocusCause::SetDirectly);
    }

    return FReply::Unhandled();
}

FReply SCSTopoSurveyHud::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Tab)
    {
        if (OnToggleFocus.IsBound())
        {
            OnToggleFocus.Execute();
        }
        return FReply::Handled();
    }

    if (InKeyEvent.GetKey() == EKeys::Escape && bCollectorInteractive)
    {
        if (OnToggleFocus.IsBound())
        {
            OnToggleFocus.Execute();
        }
        return FReply::Handled();
    }

    if (InKeyEvent.GetKey() == EKeys::Z && InKeyEvent.IsControlDown())
    {
        return HandleUndo();
    }

    return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

void SCSTopoSurveyHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    SyncCodeText();
    RefreshDynamicContent();
}

void SCSTopoSurveyHud::SetCollectorInteractive(bool bInteractive)
{
    bCollectorInteractive = bInteractive;
}

void SCSTopoSurveyHud::FocusCodeEditor()
{
    if (bCollectorInteractive && CodeSearchBox.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetKeyboardFocus(CodeSearchBox, EFocusCause::SetDirectly);
    }
    else if (bCollectorInteractive && CodeTextBox.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetKeyboardFocus(CodeTextBox, EFocusCause::SetDirectly);
    }
}

void SCSTopoSurveyHud::RefreshDynamicContent()
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    const FString ActiveCode = Survey->ActiveProject.ActiveCode;
    const int32 PaletteCount = Survey->ActiveProject.CodePalette.Num();
    const int32 ControlCodeCount = Survey->GetControlCodeDefinitions().Num();
    const int32 ShotCount = Survey->ActiveProject.Shots.Num();

    if (PaletteCount != LastRenderedPaletteCount || ActiveCode != LastRenderedActiveCode || CodeFilterText != LastRenderedFilterText)
    {
        RefreshQuickCodes();
        LastRenderedPaletteCount = PaletteCount;
        LastRenderedActiveCode = ActiveCode;
        LastRenderedFilterText = CodeFilterText;
    }

    if (ControlCodeCount != LastRenderedControlCodeCount)
    {
        RefreshControlCodes();
        LastRenderedControlCodeCount = ControlCodeCount;
    }

    if (ShotCount != LastRenderedShotCount)
    {
        RefreshRecentShots();
        LastRenderedShotCount = ShotCount;
    }
}

void SCSTopoSurveyHud::RefreshQuickCodes()
{
    if (!QuickCodeBox.IsValid())
    {
        return;
    }

    QuickCodeBox->ClearChildren();

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    FString LastCategory;
    bool bRenderedAnyCode = false;
    const FString NormalizedFilter = NormalizeCollectorCode(CodeFilterText);
    for (const FCSTopoCodeStyle& Style : Survey->ActiveProject.CodePalette)
    {
        if (!NormalizedFilter.IsEmpty())
        {
            const FString SearchText = FString::Printf(
                TEXT("%s %s %s %s"),
                *Style.Code,
                *Style.DisplayName,
                *Style.Category,
                *Style.PointType).ToUpper();
            if (!SearchText.Contains(NormalizedFilter))
            {
                continue;
            }
        }

        if (!Style.Category.Equals(LastCategory, ESearchCase::CaseSensitive))
        {
            LastCategory = Style.Category;
            QuickCodeBox->AddSlot()
            .Padding(FMargin(0.0f, 2.0f, 8.0f, 6.0f))
            [
                BuildCodeCategoryLabel(LastCategory.IsEmpty() ? TEXT("UNCATEGORIZED") : LastCategory)
            ];
        }

        const bool bIsActive = Style.Code.Equals(Survey->ActiveProject.ActiveCode, ESearchCase::IgnoreCase);
        QuickCodeBox->AddSlot()
        .Padding(FMargin(0.0f, 0.0f, 6.0f, 6.0f))
        [
            BuildQuickCodeButton(Style.Code, Style.Color, bIsActive)
        ];
        bRenderedAnyCode = true;
    }

    if (!bRenderedAnyCode)
    {
        QuickCodeBox->AddSlot()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No matching codes.")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.84f, 1.0f)))
        ];
    }
}

void SCSTopoSurveyHud::RefreshControlCodes()
{
    if (!ControlCodeBox.IsValid())
    {
        return;
    }

    ControlCodeBox->ClearChildren();

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    for (const FCSTopoControlCodeDefinition& Definition : Survey->GetControlCodeDefinitions())
    {
        ControlCodeBox->AddSlot()
        .Padding(FMargin(0.0f, 0.0f, 6.0f, 6.0f))
        [
            BuildControlCodeButton(Definition)
        ];
    }
}

void SCSTopoSurveyHud::RefreshRecentShots()
{
    if (!RecentShotBox.IsValid())
    {
        return;
    }

    RecentShotBox->ClearChildren();

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    const TArray<FCSTopoShotRecord> RecentShots = Survey->GetRecentShots(8);
    if (RecentShots.IsEmpty())
    {
        RecentShotBox->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No shots collected yet.")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.84f, 1.0f)))
        ];
        return;
    }

    for (const FCSTopoShotRecord& Shot : RecentShots)
    {
        RecentShotBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(
                TEXT("%d  %s  N %.3f  E %.3f  Z %.3f  %s"),
                Shot.PointNumber,
                *Shot.Code,
                Shot.Northing,
                Shot.Easting,
                Shot.Elevation,
                *GetShotTag(Shot))))
            .ColorAndOpacity(FSlateColor(GetCodeColor(Survey, Shot.Code)))
            .AutoWrapText(true)
        ];
    }
}

void SCSTopoSurveyHud::SyncCodeText()
{
    if (!CodeTextBox.IsValid() || bUpdatingCodeText)
    {
        return;
    }

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    const FString ActiveCode = Survey->ActiveProject.ActiveCode;
    if (CodeTextBox->GetText().ToString().Equals(ActiveCode, ESearchCase::CaseSensitive))
    {
        LastSyncedCode = ActiveCode;
        return;
    }

    bUpdatingCodeText = true;
    CodeTextBox->SetText(FText::FromString(ActiveCode));
    bUpdatingCodeText = false;
    LastSyncedCode = ActiveCode;
}

TSharedRef<SWidget> SCSTopoSurveyHud::BuildSectionHeader(const FString& Label) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(Label))
        .ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.86f, 0.96f, 1.0f)));
}

TSharedRef<SWidget> SCSTopoSurveyHud::BuildQuickCodeButton(const FString& Code, const FLinearColor& Color, bool bIsActive)
{
    const FString Label = bIsActive ? FString::Printf(TEXT("[%s]"), *Code) : Code;

    return SNew(SButton)
        .Text(FText::FromString(Label))
        .ToolTipText(FText::FromString(Code))
        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
        .ButtonColorAndOpacity(Color)
        .OnClicked_Lambda([this, Code]()
        {
            if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
            {
                Survey->SetActiveCode(Code);
                ActionStatusMessage = FString::Printf(TEXT("Active code set to %s."), *Survey->ActiveProject.ActiveCode);
            }
            SyncCodeText();
            RefreshDynamicContent();
            return FReply::Handled();
        });
}

TSharedRef<SWidget> SCSTopoSurveyHud::BuildControlCodeButton(const FCSTopoControlCodeDefinition& Definition)
{
    const FString ControlCode = Definition.Code;
    return SNew(SButton)
        .Text(FText::FromString(ControlCode))
        .ToolTipText(FText::FromString(Definition.Action.IsEmpty() ? ControlCode : Definition.Action))
        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
        .ButtonColorAndOpacity(FLinearColor(0.13f, 0.18f, 0.24f, 1.0f))
        .OnClicked_Lambda([this, ControlCode, Definition]()
        {
            if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
            {
                if (ControlCodeNeedsParameter(Definition))
                {
                    PendingParameterControlCode = ControlCode;
                    ActionStatusMessage = FString::Printf(TEXT("Type the %s parameter, then press Enter."), *ControlCode);
                    if (CodeSearchBox.IsValid())
                    {
                        CodeFilterText.Empty();
                        CodeSearchBox->SetText(FText::GetEmpty());
                        if (FSlateApplication::IsInitialized())
                        {
                            FSlateApplication::Get().SetKeyboardFocus(CodeSearchBox, EFocusCause::SetDirectly);
                        }
                    }
                }
                else
                {
                    FString StatusMessage;
                    if (Survey->SetPendingControlCode(ControlCode, TEXT(""), StatusMessage))
                    {
                        PendingParameterControlCode.Empty();
                    }
                    ActionStatusMessage = StatusMessage;
                }
            }
            RefreshDynamicContent();
            return FReply::Handled();
        });
}

TSharedRef<SWidget> SCSTopoSurveyHud::BuildCodeCategoryLabel(const FString& Category) const
{
    return SNew(SBorder)
        .Padding(FMargin(7.0f, 4.0f))
        .BorderBackgroundColor(FLinearColor(0.10f, 0.13f, 0.16f, 1.0f))
        [
            SNew(STextBlock)
            .Text(FText::FromString(Category))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.86f, 0.94f, 1.0f)))
        ];
}

FReply SCSTopoSurveyHud::HandleCollectShot()
{
    if (OnCollectShot.IsBound())
    {
        OnCollectShot.Execute();
        ActionStatusMessage = TEXT("Shot collection requested.");
    }
    return FReply::Handled();
}

FReply SCSTopoSurveyHud::HandleUndo()
{
    if (OnUndo.IsBound())
    {
        OnUndo.Execute();
        ActionStatusMessage = TEXT("Undo requested.");
    }
    else if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        FString StatusMessage;
        Survey->UndoLastMeasurement(StatusMessage);
        ActionStatusMessage = StatusMessage;
    }
    RefreshDynamicContent();
    return FReply::Handled();
}

FReply SCSTopoSurveyHud::HandleSplitFigure()
{
    if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        FString StatusMessage;
        Survey->SplitActiveFigure(StatusMessage);
        ActionStatusMessage = StatusMessage;
    }
    RefreshDynamicContent();
    return FReply::Handled();
}

FReply SCSTopoSurveyHud::HandleCloseFigure()
{
    if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        FString StatusMessage;
        Survey->CloseActiveFigure(StatusMessage);
        ActionStatusMessage = StatusMessage;
    }
    RefreshDynamicContent();
    return FReply::Handled();
}

void SCSTopoSurveyHud::HandleCodeFilterChanged(const FText& NewText)
{
    CodeFilterText = NewText.ToString().TrimStartAndEnd();
    RefreshDynamicContent();
}

void SCSTopoSurveyHud::HandleCodeFilterCommitted(const FText& NewText, ETextCommit::Type)
{
    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    const FString RawText = NewText.ToString().TrimStartAndEnd();
    if (RawText.IsEmpty())
    {
        return;
    }

    TArray<FString> Tokens;
    RawText.ParseIntoArrayWS(Tokens);
    for (FString& Token : Tokens)
    {
        Token = NormalizeCollectorCode(Token);
    }

    FString StatusMessage;
    bool bConsumedCommand = false;
    if (Tokens.Num() >= 2)
    {
        FCSTopoCodeStyle Style;
        FCSTopoControlCodeDefinition Definition;
        if (Survey->GetCodeStyle(Tokens[0], Style) && IsControlCodeKnown(Survey, Tokens[1], &Definition))
        {
            Survey->SetActiveCode(Tokens[0]);
            const FString Parameter = Tokens.Num() >= 3 ? Tokens[2] : TEXT("");
            if (Survey->SetPendingControlCode(Tokens[1], Parameter, StatusMessage))
            {
                PendingParameterControlCode.Empty();
            }
            ActionStatusMessage = StatusMessage;
            bConsumedCommand = true;
        }
    }

    if (!bConsumedCommand && !PendingParameterControlCode.IsEmpty())
    {
        if (Survey->SetPendingControlCode(PendingParameterControlCode, Tokens[0], StatusMessage))
        {
            PendingParameterControlCode.Empty();
        }
        ActionStatusMessage = StatusMessage;
        bConsumedCommand = true;
    }

    if (!bConsumedCommand && Tokens.Num() == 1)
    {
        FCSTopoCodeStyle Style;
        if (Survey->GetCodeStyle(Tokens[0], Style))
        {
            Survey->SetActiveCode(Tokens[0]);
            ActionStatusMessage = FString::Printf(TEXT("Active code set to %s."), *Survey->ActiveProject.ActiveCode);
            bConsumedCommand = true;
        }
    }

    if (bConsumedCommand)
    {
        CodeFilterText.Empty();
        if (CodeSearchBox.IsValid())
        {
            CodeSearchBox->SetText(FText::GetEmpty());
        }
        SyncCodeText();
    }
    else
    {
        CodeFilterText = RawText;
    }

    RefreshDynamicContent();
}

void SCSTopoSurveyHud::HandleCodeTextChanged(const FText& NewText)
{
    if (bUpdatingCodeText)
    {
        return;
    }

    const FString NormalizedCode = NormalizeCollectorCode(NewText.ToString());
    if (NormalizedCode != NewText.ToString())
    {
        bUpdatingCodeText = true;
        CodeTextBox->SetText(FText::FromString(NormalizedCode));
        bUpdatingCodeText = false;
    }

    UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey != nullptr && !NormalizedCode.IsEmpty())
    {
        Survey->SetActiveCode(NormalizedCode);
        ActionStatusMessage = FString::Printf(TEXT("Active code set to %s."), *Survey->ActiveProject.ActiveCode);
    }

    RefreshDynamicContent();
}

void SCSTopoSurveyHud::HandleCodeTextCommitted(const FText& NewText, ETextCommit::Type)
{
    HandleCodeTextChanged(NewText);
}

FText SCSTopoSurveyHud::GetProjectNameText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    return FText::FromString(Survey != nullptr ? Survey->ActiveProject.ProjectName : TEXT("CSTopo"));
}

FText SCSTopoSurveyHud::GetActiveSourceText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("No active source"));
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([Survey](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == Survey->ActiveProject.ActivePointCloudId;
    });
    if (Source == nullptr)
    {
        return FText::FromString(TEXT("No active point cloud"));
    }

    return FText::FromString(FString::Printf(TEXT("Source: %s"), *FPaths::GetCleanFilename(Source->SourcePath)));
}

FText SCSTopoSurveyHud::GetActiveCodeText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    return FText::FromString(Survey != nullptr ? Survey->ActiveProject.ActiveCode : TEXT(""));
}

FText SCSTopoSurveyHud::GetActiveCodeDetailText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Code metadata unavailable."));
    }

    FCSTopoCodeStyle Style;
    if (!Survey->GetCodeStyle(Survey->ActiveProject.ActiveCode, Style))
    {
        return FText::FromString(TEXT("Code not in Code List"));
    }

    const FString Name = Style.DisplayName.IsEmpty() ? Style.Code : Style.DisplayName;
    const FString Category = Style.Category.IsEmpty() ? TEXT("Uncategorized") : Style.Category;
    return FText::FromString(FString::Printf(TEXT("%s | %s"), *Name, *Category));
}

FText SCSTopoSurveyHud::GetActivePointTypeText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Point Type: unavailable"));
    }

    FCSTopoCodeStyle Style;
    const FString PointType = Survey->GetCodeStyle(Survey->ActiveProject.ActiveCode, Style) && !Style.PointType.IsEmpty()
        ? Style.PointType
        : TEXT("unavailable");
    return FText::FromString(FString::Printf(TEXT("Point Type: %s"), *PointType));
}

FText SCSTopoSurveyHud::GetCollectorPointIdText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    const int32 NextPoint = Survey != nullptr ? Survey->ActiveProject.NextPointNumber : 1;
    return FText::FromString(FString::Printf(TEXT("Point ID: %d"), NextPoint));
}

FText SCSTopoSurveyHud::GetNavigationModeText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Mode: unavailable"));
    }

    return FText::FromString(Survey->GetRuntimeNavigationStatusLine());
}

FText SCSTopoSurveyHud::GetNextPointText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    const int32 NextPoint = Survey != nullptr ? Survey->ActiveProject.NextPointNumber : 1;
    return FText::FromString(FString::Printf(TEXT("Next Pt: %d"), NextPoint));
}

FText SCSTopoSurveyHud::GetSurveySummaryText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Survey subsystem unavailable."));
    }

    return FText::FromString(Survey->GetActiveSurveyStatusLine());
}

FText SCSTopoSurveyHud::GetFigureStatusText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Figure status unavailable."));
    }

    return FText::FromString(Survey->GetActiveFigureStatusLine());
}

FText SCSTopoSurveyHud::GetHoverText() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FText::FromString(TEXT("Hover: unavailable."));
    }

    return FText::FromString(Survey->GetHoverStatusLine());
}

FText SCSTopoSurveyHud::GetActionStatusText() const
{
    return FText::FromString(ActionStatusMessage);
}

FSlateColor SCSTopoSurveyHud::GetHoverColor() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr || !Survey->IsHoverMeasurable())
    {
        return FSlateColor(FLinearColor(1.0f, 0.38f, 0.32f, 1.0f));
    }

    return Survey->DoesHoverUseRawPoint()
        ? FSlateColor(FLinearColor(1.0f, 0.88f, 0.25f, 1.0f))
        : FSlateColor(FLinearColor(0.32f, 1.0f, 0.56f, 1.0f));
}

FSlateColor SCSTopoSurveyHud::GetActiveCodeSwatchColor() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return FSlateColor(FLinearColor::White);
    }

    return FSlateColor(GetCodeColor(Survey, Survey->ActiveProject.ActiveCode));
}
