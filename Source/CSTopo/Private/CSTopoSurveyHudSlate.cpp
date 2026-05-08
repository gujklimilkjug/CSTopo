#include "CSTopoSurveyHudSlate.h"

#include "CSTopoSurveySubsystem.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
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
        || Definition.ParameterKind == ECSTopoControlParameterKind::OptionalDistance
        || Definition.ParameterKind == ECSTopoControlParameterKind::PointNumber
        || Definition.ParameterKind == ECSTopoControlParameterKind::DistanceOrPointNumber;
}

FString GetControlParameterPrompt(const FCSTopoControlCodeDefinition& Definition)
{
    if (Definition.ParameterKind == ECSTopoControlParameterKind::Distance)
    {
        return FString::Printf(TEXT("Enter %s distance in the search box, then press Enter."), *Definition.Code);
    }
    if (Definition.ParameterKind == ECSTopoControlParameterKind::OptionalDistance)
    {
        return FString::Printf(TEXT("Enter optional %s radius in the search box, or press Enter to use the next shot."), *Definition.Code);
    }
    if (Definition.ParameterKind == ECSTopoControlParameterKind::PointNumber)
    {
        return FString::Printf(TEXT("Enter %s point number in the search box, then press Enter."), *Definition.Code);
    }
    if (Definition.ParameterKind == ECSTopoControlParameterKind::DistanceOrPointNumber)
    {
        return FString::Printf(TEXT("Type the %s distance or P<point>, then press Enter."), *Definition.Code);
    }
    return FString::Printf(TEXT("%s will apply to the next measurement."), *Definition.Code);
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

const FLinearColor HudPanelColor(0.0667f, 0.0667f, 0.0667f, 0.75f);

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
        bool bHasMeasurementExtent = false;

        for (const FCSTopoShotRecord& Shot : Survey->ActiveProject.Shots)
        {
            bHasMeasurementExtent = true;
            MinNorthing = FMath::Min(MinNorthing, Shot.Northing);
            MaxNorthing = FMath::Max(MaxNorthing, Shot.Northing);
            MinEasting = FMath::Min(MinEasting, Shot.Easting);
            MaxEasting = FMath::Max(MaxEasting, Shot.Easting);
        }
        for (const FCSTopoFigureSegmentRecord& Segment : Survey->ActiveProject.FigureSegments)
        {
            for (const FVector& SurveyPoint : Segment.SurveyPoints)
            {
                bHasMeasurementExtent = true;
                MinNorthing = FMath::Min(MinNorthing, static_cast<double>(SurveyPoint.X));
                MaxNorthing = FMath::Max(MaxNorthing, static_cast<double>(SurveyPoint.X));
                MinEasting = FMath::Min(MinEasting, static_cast<double>(SurveyPoint.Y));
                MaxEasting = FMath::Max(MaxEasting, static_cast<double>(SurveyPoint.Y));
            }
        }

        FVector2D CurrentSurveyNe;
        FVector2D Heading;
        const bool bHasCurrentPose = Survey->GetCurrentSurveyMapPose(CurrentSurveyNe, Heading);

        if (bHasCurrentPose)
        {
            MinNorthing = bHasMeasurementExtent ? FMath::Min(MinNorthing, static_cast<double>(CurrentSurveyNe.X)) : CurrentSurveyNe.X;
            MaxNorthing = bHasMeasurementExtent ? FMath::Max(MaxNorthing, static_cast<double>(CurrentSurveyNe.X)) : CurrentSurveyNe.X;
            MinEasting = bHasMeasurementExtent ? FMath::Min(MinEasting, static_cast<double>(CurrentSurveyNe.Y)) : CurrentSurveyNe.Y;
            MaxEasting = bHasMeasurementExtent ? FMath::Max(MaxEasting, static_cast<double>(CurrentSurveyNe.Y)) : CurrentSurveyNe.Y;
            bHasMeasurementExtent = true;
        }

        if (!bHasMeasurementExtent)
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

        const double CenterNorthing = bHasCurrentPose ? CurrentSurveyNe.X : (MinNorthing + MaxNorthing) * 0.5;
        const double CenterEasting = bHasCurrentPose ? CurrentSurveyNe.Y : (MinEasting + MaxEasting) * 0.5;
        const double HalfNorthing = FMath::Max(
            FMath::Max(FMath::Abs(MaxNorthing - CenterNorthing), FMath::Abs(MinNorthing - CenterNorthing)),
            50.0);
        const double HalfEasting = FMath::Max(
            FMath::Max(FMath::Abs(MaxEasting - CenterEasting), FMath::Abs(MinEasting - CenterEasting)),
            50.0);
        const double NorthingRange = FMath::Max(HalfNorthing * 2.24, 1.0);
        const double EastingRange = FMath::Max(HalfEasting * 2.24, 1.0);
        const double Scale = FMath::Min(
            static_cast<double>(PlotSize.X) / EastingRange,
            static_cast<double>(PlotSize.Y) / NorthingRange);

        auto MapSurveyPoint = [&](double Northing, double Easting)
        {
            const double X = PlotOrigin.X + PlotSize.X * 0.5 + (Easting - CenterEasting) * Scale;
            const double Y = PlotOrigin.Y + PlotSize.Y * 0.5 - (Northing - CenterNorthing) * Scale;
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

        if (bHasCurrentPose)
        {
            const FVector2D Position = PlotOrigin + PlotSize * 0.5f;
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

    const FString LogoPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Logo"), TEXT("CSTopo_Logo1.png")));
    if (FPaths::FileExists(LogoPath))
    {
        LogoBrush = MakeShared<FSlateDynamicImageBrush>(FName(*LogoPath), FVector2D(144.0f, 144.0f));
    }
    else
    {
        LogoBrush.Reset();
    }

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Top)
        .Padding(FMargin(0.0f, 16.0f, 16.0f, 0.0f))
        [
            SNew(SBox)
            .WidthOverride(520.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
                [
                    SNew(SBorder)
                    .Padding(FMargin(16.0f))
                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(HudPanelColor)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBox)
                            .WidthOverride(144.0f)
                            .HeightOverride(144.0f)
                            .Clipping(EWidgetClipping::ClipToBounds)
                            [
                                SNew(SOverlay)
                                + SOverlay::Slot()
                                [
                                    SNew(SImage)
                                    .Image(LogoBrush.IsValid() ? LogoBrush.Get() : nullptr)
                                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                                    .RenderTransform(FSlateRenderTransform(FScale2D(1.35f, 1.35f)))
                                    .RenderTransformPivot(FVector2D(0.5f, 0.5f))
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
                                    .Text(FText::FromString(TEXT("CS")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
                                    .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.42f, 0.0f, 1.0f)))
                                    .Visibility_Lambda([this]()
                                    {
                                        return LogoBrush.IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                                    })
                                ]
                            ]
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(FMargin(18.0f, 0.0f, 0.0f, 0.0f))
                        .VAlign(VAlign_Center)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(this, &SCSTopoSurveyHud::GetProjectNameText)
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
                                .ColorAndOpacity(FSlateColor(FLinearColor::White))
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                            [
                                SNew(STextBlock)
                                .Text(this, &SCSTopoSurveyHud::GetActiveSourceText)
                                .ColorAndOpacity(FSlateColor(FLinearColor(0.76f, 0.84f, 0.90f, 1.0f)))
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
                            [
                                SNew(STextBlock)
                                .Text(this, &SCSTopoSurveyHud::GetNavigationModeText)
                                .ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f)))
                            ]
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBorder)
                            .Padding(FMargin(14.0f, 10.0f))
                            .BorderBackgroundColor(HudPanelColor)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .HAlign(HAlign_Center)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Point ID")))
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.82f, 0.25f, 1.0f)))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .HAlign(HAlign_Center)
                                .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                                [
                                    SNew(STextBlock)
                                    .Text_Lambda([this]()
                                    {
                                        const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
                                        const int32 NextPoint = Survey != nullptr ? Survey->ActiveProject.NextPointNumber : 1;
                                        return FText::FromString(FString::Printf(TEXT("%d"), NextPoint));
                                    })
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.82f, 0.25f, 1.0f)))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 34))
                                ]
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
                [
                    SNew(SBorder)
                    .Padding(FMargin(16.0f))
                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(HudPanelColor)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            BuildSectionHeader(TEXT("ACTIVE CODE"))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .VAlign(VAlign_Center)
                            [
                                SNew(SOverlay)
                                + SOverlay::Slot()
                                [
                                    SNew(STextBlock)
                                    .Text(this, &SCSTopoSurveyHud::GetActiveCodeText)
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 42))
                                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                                    .Visibility_Lambda([this]()
                                    {
                                        return bCollectorInteractive ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                                    })
                                ]
                                + SOverlay::Slot()
                                [
                                    SAssignNew(CodeTextBox, SEditableTextBox)
                                    .HintText(FText::FromString(TEXT("Code")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 42))
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
                            .VAlign(VAlign_Center)
                            [
                                SNew(SBorder)
                                .Padding(FMargin(24.0f, 20.0f))
                                .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                .BorderBackgroundColor(this, &SCSTopoSurveyHud::GetActiveCodeSwatchColor)
                                [
                                    SNullWidget::NullWidget
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
                        [
                            SNew(STextBlock)
                            .Text(this, &SCSTopoSurveyHud::GetActivePointTypeText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.95f, 1.0f, 1.0f)))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                        [
                            SNew(STextBlock)
                            .Text(this, &SCSTopoSurveyHud::GetActiveCodeDetailText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.95f, 1.0f, 1.0f)))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 18.0f, 0.0f, 0.0f))
                        [
                            SNew(SBox)
                            .HeightOverride(32.0f)
                            [
                                SNew(SOverlay)
                                + SOverlay::Slot()
                                [
                                    SNew(SBorder)
                                    .Padding(FMargin(2.0f))
                                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                    .BorderBackgroundColor(this, &SCSTopoSurveyHud::GetCodeSearchBorderColor)
                                    [
                                        SAssignNew(CodeSearchBox, SEditableTextBox)
                                        .HintText(this, &SCSTopoSurveyHud::GetCodeSearchHintText)
                                        .Visibility_Lambda([this]()
                                        {
                                            return bCollectorInteractive ? EVisibility::Visible : EVisibility::HitTestInvisible;
                                        })
                                        .OnTextChanged(this, &SCSTopoSurveyHud::HandleCodeFilterChanged)
                                        .OnTextCommitted(this, &SCSTopoSurveyHud::HandleCodeFilterCommitted)
                                    ]
                                ]
                                + SOverlay::Slot()
                                .VAlign(VAlign_Top)
                                .Padding(FMargin(0.0f, 36.0f, 0.0f, 0.0f))
                                [
                                    SNew(SBorder)
                                    .Padding(FMargin(6.0f))
                                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                    .BorderBackgroundColor(FLinearColor(0.03f, 0.035f, 0.04f, 0.98f))
                                    .Visibility(this, &SCSTopoSurveyHud::GetCodeSuggestionVisibility)
                                    [
                                        SAssignNew(CodeSuggestionBox, SVerticalBox)
                                    ]
                                ]
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
                    [
                        SNew(SBox)
                        .WidthOverride(170.0f)
                        .HeightOverride(420.0f)
                        [
                            SNew(SBorder)
                            .Padding(FMargin(14.0f))
                            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                            .BorderBackgroundColor(HudPanelColor)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("CONTROL CODES"))
                                ]
                                + SVerticalBox::Slot()
                                .FillHeight(1.0f)
                                .Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
                                [
                                    SNew(SScrollBox)
                                    .ScrollBarVisibility(EVisibility::Collapsed)
                                    + SScrollBox::Slot()
                                    [
                                        SAssignNew(ControlCodeContainer, SVerticalBox)
                                    ]
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                                [
                                    SNew(SButton)
                                    .Text(FText::FromString(TEXT("Cancel Code")))
                                    .HAlign(HAlign_Center)
                                    .VAlign(VAlign_Center)
                                    .ButtonColorAndOpacity(FLinearColor(0.45f, 0.16f, 0.12f, 1.0f))
                                    .Visibility(this, &SCSTopoSurveyHud::GetControlCancelVisibility)
                                    .OnClicked(this, &SCSTopoSurveyHud::HandleCancelControlCode)
                                ]
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SBox)
                        .HeightOverride(420.0f)
                        [
                            SNew(SBorder)
                            .Padding(FMargin(16.0f))
                            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                            .BorderBackgroundColor(HudPanelColor)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    BuildSectionHeader(TEXT("MINIMAP"))
                                ]
                                + SVerticalBox::Slot()
                                .FillHeight(1.0f)
                                .Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
                                [
                                    SNew(SCSTopoSurveyMiniMap)
                                    .SurveySubsystem(SurveySubsystem)
                                ]
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .Padding(FMargin(16.0f))
                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(HudPanelColor)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            BuildSectionHeader(TEXT("RECENT"))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
                        [
                            SAssignNew(RecentShotBox, SVerticalBox)
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
        LastRenderedPaletteCount = PaletteCount;
        LastRenderedActiveCode = ActiveCode;
        LastRenderedFilterText = CodeFilterText;
        RefreshCodeSuggestions();
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

void SCSTopoSurveyHud::RefreshCodeSuggestions()
{
    if (!CodeSuggestionBox.IsValid())
    {
        return;
    }

    CodeSuggestionBox->ClearChildren();

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr || CodeFilterText.IsEmpty())
    {
        return;
    }

    const FString NormalizedFilter = NormalizeCollectorCode(CodeFilterText);
    int32 SuggestionCount = 0;
    constexpr int32 MaxSuggestions = 6;

    for (const FCSTopoCodeStyle& Style : Survey->ActiveProject.CodePalette)
    {
        const bool bMatchesCode = Style.Code.Contains(NormalizedFilter, ESearchCase::IgnoreCase);
        const bool bMatchesName = Style.DisplayName.Contains(CodeFilterText, ESearchCase::IgnoreCase);
        const bool bMatchesCategory = Style.Category.Contains(CodeFilterText, ESearchCase::IgnoreCase);
        const bool bMatchesPointType = Style.PointType.Contains(CodeFilterText, ESearchCase::IgnoreCase);
        if (!bMatchesCode && !bMatchesName && !bMatchesCategory && !bMatchesPointType)
        {
            continue;
        }

        CodeSuggestionBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(0.0f, SuggestionCount == 0 ? 0.0f : 4.0f, 0.0f, 0.0f))
        [
            BuildCodeSuggestionButton(Style)
        ];

        ++SuggestionCount;
        if (SuggestionCount >= MaxSuggestions)
        {
            break;
        }
    }
}



void SCSTopoSurveyHud::RefreshControlCodes()
{
    if (!ControlCodeContainer.IsValid())
    {
        return;
    }

    ControlCodeContainer->ClearChildren();

    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    if (Survey == nullptr)
    {
        return;
    }

    TMap<FString, TArray<FCSTopoControlCodeDefinition>> Categories;
    TArray<FString> CategoryOrder = { TEXT("NAVIGATION"), TEXT("IDENTIFICATION"), TEXT("MEASUREMENT"), TEXT("SHAPE & FEATURE"), TEXT("OTHER") };
    
    for (const FString& Cat : CategoryOrder)
    {
        Categories.Add(Cat, TArray<FCSTopoControlCodeDefinition>());
    }

    for (const FCSTopoControlCodeDefinition& Definition : Survey->GetControlCodeDefinitions())
    {
        FString Code = Definition.Code.ToUpper();
        FString Category = TEXT("OTHER");
        if (Code == TEXT("CLS") || Code == TEXT("END") || Code == TEXT("ESC")) Category = TEXT("NAVIGATION");
        else if (Code == TEXT("IG") || Code == TEXT("JPT") || Code == TEXT("NPC") || Code == TEXT("NPT")) Category = TEXT("IDENTIFICATION");
        else if (Code == TEXT("OH") || Code == TEXT("OV") || Code == TEXT("PC")) Category = TEXT("MEASUREMENT");
        else if (Code == TEXT("RECT") || Code == TEXT("SCE") || Code == TEXT("SCR") || Code == TEXT("SSC") || Code == TEXT("ST")) Category = TEXT("SHAPE & FEATURE");

        Categories[Category].Add(Definition);
    }

    TArray<FCSTopoControlCodeDefinition> OrderedDefinitions;
    for (const FString& CatName : CategoryOrder)
    {
        OrderedDefinitions.Append(Categories[CatName]);
    }

    TSharedPtr<SUniformGridPanel> ControlCodeGrid;
    ControlCodeContainer->AddSlot()
    .AutoHeight()
    [
        SAssignNew(ControlCodeGrid, SUniformGridPanel)
        .SlotPadding(FMargin(0.0f, 0.0f, 6.0f, 6.0f))
    ];

    constexpr int32 ControlCodeGridColumns = 2;
    for (int32 DefIndex = 0; DefIndex < OrderedDefinitions.Num(); ++DefIndex)
    {
        const FCSTopoControlCodeDefinition& Def = OrderedDefinitions[DefIndex];
        ControlCodeGrid->AddSlot(DefIndex % ControlCodeGridColumns, DefIndex / ControlCodeGridColumns)
        [
            SNew(SBox)
            .WidthOverride(60.0f)
            .HeightOverride(34.0f)
            [
                BuildControlCodeButton(Def)
            ]
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



TSharedRef<SWidget> SCSTopoSurveyHud::BuildCodeSuggestionButton(const FCSTopoCodeStyle& Style)
{
    FString Detail = Style.DisplayName;
    if (!Style.PointType.IsEmpty())
    {
        Detail = Detail.IsEmpty() ? Style.PointType : FString::Printf(TEXT("%s | %s"), *Detail, *Style.PointType);
    }

    return SNew(SButton)
        .ButtonColorAndOpacity(FLinearColor(0.10f, 0.13f, 0.16f, 1.0f))
        .ContentPadding(FMargin(8.0f, 5.0f))
        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
        .OnClicked(this, &SCSTopoSurveyHud::HandleCodeSuggestionPicked, Style.Code)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(10.0f)
                .HeightOverride(10.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(Style.Color)
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Style.Code))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(FSlateColor(FLinearColor::White))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Detail.IsEmpty() ? Style.Category : Detail))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.78f, 0.84f, 1.0f)))
                .Clipping(EWidgetClipping::ClipToBounds)
            ]
        ];
}

TSharedRef<SWidget> SCSTopoSurveyHud::BuildControlCodeButton(const FCSTopoControlCodeDefinition& Definition)
{
    const FString ControlCode = Definition.Code;
    return SNew(SButton)
        .Text(FText::FromString(ControlCode))
        .ToolTipText(FText::FromString(Definition.Action.IsEmpty() ? ControlCode : Definition.Action))
        .IsEnabled_Lambda([this]() { return bCollectorInteractive; })
        .ButtonColorAndOpacity_Lambda([this, ControlCode]()
        {
            const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
            if (PendingParameterControlCode.Equals(ControlCode, ESearchCase::IgnoreCase)
                || (Survey != nullptr && Survey->GetPendingControlCode().Equals(ControlCode, ESearchCase::IgnoreCase)))
            {
                return FSlateColor(FLinearColor(0.0f, 0.50f, 0.64f, 1.0f));
            }
            return FSlateColor(FLinearColor(0.13f, 0.18f, 0.24f, 1.0f));
        })
        .OnClicked_Lambda([this, ControlCode, Definition]()
        {
            if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
            {
                if (ControlCodeNeedsParameter(Definition))
                {
                    PendingParameterControlCode = ControlCode;
                    ActionStatusMessage = GetControlParameterPrompt(Definition);
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

FReply SCSTopoSurveyHud::HandleCodeSuggestionPicked(FString Code)
{
    if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        Survey->SetActiveCode(Code);
        PendingParameterControlCode.Empty();
        ActionStatusMessage = FString::Printf(TEXT("Active code set to %s."), *Survey->ActiveProject.ActiveCode);
    }

    CodeFilterText.Empty();
    if (CodeSearchBox.IsValid())
    {
        CodeSearchBox->SetText(FText::GetEmpty());
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().SetKeyboardFocus(CodeSearchBox, EFocusCause::SetDirectly);
        }
    }
    SyncCodeText();
    RefreshDynamicContent();
    return FReply::Handled();
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

FReply SCSTopoSurveyHud::HandleCancelControlCode()
{
    PendingParameterControlCode.Empty();
    CodeFilterText.Empty();
    if (UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        Survey->ClearPendingControlCode();
    }
    if (CodeSearchBox.IsValid())
    {
        CodeSearchBox->SetText(FText::GetEmpty());
    }
    ActionStatusMessage = TEXT("Control code selection canceled.");
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
        if (!PendingParameterControlCode.IsEmpty())
        {
            FString StatusMessage;
            if (Survey->SetPendingControlCode(PendingParameterControlCode, TEXT(""), StatusMessage))
            {
                PendingParameterControlCode.Empty();
            }
            ActionStatusMessage = StatusMessage;
            RefreshDynamicContent();
        }
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
    if (!PendingParameterControlCode.IsEmpty())
    {
        return FText::FromString(FString::Printf(TEXT("Input needed: %s | %s"), *PendingParameterControlCode, *ActionStatusMessage));
    }
    if (const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get())
    {
        const FString PendingControlCode = Survey->GetPendingControlCode();
        if (!PendingControlCode.IsEmpty())
        {
            const FString PendingControlParameter = Survey->GetPendingControlParameter();
            const FString Prefix = Survey->IsPendingControlAutomatic() ? TEXT("Auto next shot") : TEXT("Pending");
            const FString PendingText = PendingControlParameter.IsEmpty()
                ? FString::Printf(TEXT("%s: %s"), *Prefix, *PendingControlCode)
                : FString::Printf(TEXT("%s: %s %s"), *Prefix, *PendingControlCode, *PendingControlParameter);
            return FText::FromString(FString::Printf(TEXT("%s | %s"), *PendingText, *ActionStatusMessage));
        }
    }
    return FText::FromString(ActionStatusMessage);
}

FText SCSTopoSurveyHud::GetCodeSearchHintText() const
{
    if (PendingParameterControlCode.Equals(TEXT("OH"), ESearchCase::IgnoreCase)
        || PendingParameterControlCode.Equals(TEXT("OV"), ESearchCase::IgnoreCase))
    {
        return FText::FromString(FString::Printf(TEXT("Enter %s offset distance..."), *PendingParameterControlCode));
    }
    if (PendingParameterControlCode.Equals(TEXT("JPT"), ESearchCase::IgnoreCase))
    {
        return FText::FromString(TEXT("Enter JPT point number..."));
    }
    if (PendingParameterControlCode.Equals(TEXT("SCR"), ESearchCase::IgnoreCase))
    {
        return FText::FromString(TEXT("Enter optional SCR radius..."));
    }
    if (!PendingParameterControlCode.IsEmpty())
    {
        return FText::FromString(FString::Printf(TEXT("Enter %s value..."), *PendingParameterControlCode));
    }
    return FText::FromString(TEXT("Search code or enter value..."));
}

EVisibility SCSTopoSurveyHud::GetCodeSuggestionVisibility() const
{
    return bCollectorInteractive && PendingParameterControlCode.IsEmpty() && CodeSuggestionBox.IsValid() && CodeSuggestionBox->NumSlots() > 0
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SCSTopoSurveyHud::GetControlCancelVisibility() const
{
    const UCSTopoSurveySubsystem* Survey = SurveySubsystem.Get();
    const bool bHasPendingControl = Survey != nullptr && !Survey->GetPendingControlCode().IsEmpty();
    return bCollectorInteractive && (!PendingParameterControlCode.IsEmpty() || bHasPendingControl)
        ? EVisibility::Visible
        : EVisibility::Collapsed;
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

FSlateColor SCSTopoSurveyHud::GetCodeSearchBorderColor() const
{
    return PendingParameterControlCode.IsEmpty()
        ? FSlateColor(FLinearColor(0.10f, 0.13f, 0.16f, 1.0f))
        : FSlateColor(FLinearColor(0.0f, 0.55f, 0.70f, 1.0f));
}
