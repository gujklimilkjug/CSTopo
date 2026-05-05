#include "CSTopoReticleSlate.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SOverlay.h"

void SCSTopoReticle::Construct(const FArguments& InArgs)
{
    SetVisibility(EVisibility::HitTestInvisible);

    const FLinearColor ReticleColor(0.05f, 0.95f, 1.0f, 0.9f);

    ChildSlot
    [
        SNew(SConstraintCanvas)
        + SConstraintCanvas::Slot()
        .Anchors(FAnchors(0.5f, 0.5f))
        .Alignment(FVector2D(0.5f, 0.5f))
        .AutoSize(true)
        [
            SNew(SBox)
            .WidthOverride(30.0f)
            .HeightOverride(30.0f)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(26.0f)
                    .HeightOverride(2.0f)
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(ReticleColor)
                    ]
                ]
                + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(2.0f)
                    .HeightOverride(26.0f)
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(ReticleColor)
                    ]
                ]
                + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(6.0f)
                    .HeightOverride(6.0f)
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
                    ]
                ]
            ]
        ]
    ];
}
