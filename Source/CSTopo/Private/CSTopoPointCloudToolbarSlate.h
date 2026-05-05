#pragma once

#include "CoreMinimal.h"
#include "CSTopoTypes.h"
#include "Widgets/SCompoundWidget.h"

class SCSTopoPointCloudToolbar : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCSTopoPointCloudToolbar) {}
        SLATE_ARGUMENT(TWeakObjectPtr<class UCSTopoSurveySubsystem>, SurveySubsystem)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshList();
    void SetStatusMessage(const FString& Message);

private:
    TWeakObjectPtr<class UCSTopoSurveySubsystem> SurveySubsystem;
    TSharedPtr<class SEditableTextBox> SourcePathBox;
    TSharedPtr<class SWrapBox> CodePaletteBox;
    TSharedPtr<class SVerticalBox> CloudListBox;
    TSharedPtr<class SVerticalBox> ShotListBox;
    TSharedPtr<class STextBlock> StatusText;

    FReply AddCloud();
    FReply RefreshClouds();
    TSharedRef<SWidget> BuildCodeButton(const FCSTopoCodeStyle& Style);
    TSharedRef<SWidget> BuildCloudRow(const FCSTopoPointCloudSource& Source);
    void RefreshCodePalette();
    void RefreshShotList();
    EVisibility GetSurfaceBuildProgressVisibility(FString SourceId) const;
    TOptional<float> GetSurfaceBuildProgress(FString SourceId) const;
    FText GetSurfaceBuildProgressText(FString SourceId) const;
};
