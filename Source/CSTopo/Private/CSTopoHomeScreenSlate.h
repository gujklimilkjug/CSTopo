#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCSTopoHomeScreen : public SCompoundWidget
{
public:
    DECLARE_DELEGATE(FOnHomeAction)

    SLATE_BEGIN_ARGS(SCSTopoHomeScreen) {}
        SLATE_ARGUMENT(TWeakObjectPtr<class UCSTopoSurveySubsystem>, SurveySubsystem)
        SLATE_EVENT(FOnHomeAction, OnOpenProject)
        SLATE_EVENT(FOnHomeAction, OnImportPointCloud)
        SLATE_EVENT(FOnHomeAction, OnRetrySurface)
        SLATE_EVENT(FOnHomeAction, OnCancelToHome)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetStatusMessage(const FString& Message);
    void Refresh();

private:
    TWeakObjectPtr<class UCSTopoSurveySubsystem> SurveySubsystem;
    FOnHomeAction OnOpenProject;
    FOnHomeAction OnImportPointCloud;
    FOnHomeAction OnRetrySurface;
    FOnHomeAction OnCancelToHome;
    TSharedPtr<class STextBlock> StatusText;
    TSharedPtr<class STextBlock> DetailText;
    TSharedPtr<class STextBlock> ProgressText;

    FReply OpenProjectClicked();
    FReply ImportPointCloudClicked();
    FReply RetrySurfaceClicked();
    FReply CancelToHomeClicked();
    EVisibility GetRetryVisibility() const;
    EVisibility GetProgressVisibility() const;
    TOptional<float> GetSurfaceBuildProgress() const;
};
