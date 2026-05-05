#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCSTopoPointCloudToolbar;

class SCSTopoMainMenu : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCSTopoMainMenu) {}
        SLATE_ARGUMENT(TWeakObjectPtr<class UCSTopoSurveySubsystem>, SurveySubsystem)
        SLATE_ARGUMENT(TWeakPtr<SCSTopoPointCloudToolbar>, PointCloudToolbar)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void OpenImportPointCloudDialog();

private:
    TWeakObjectPtr<class UCSTopoSurveySubsystem> SurveySubsystem;
    TWeakPtr<SCSTopoPointCloudToolbar> PointCloudToolbar;
    bool bFileMenuOpen = false;

    TSharedRef<SWidget> BuildFileMenu();
    EVisibility GetFileMenuVisibility() const;
    FReply ToggleFileMenu();
    FReply ImportPointCloud();
    FReply TogglePointCloudManager();
    FReply ToggleCloudOverlay();
    FText GetCloudOverlayButtonText() const;
};
