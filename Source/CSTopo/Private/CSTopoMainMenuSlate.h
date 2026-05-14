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
        SLATE_EVENT(FSimpleDelegate, OnNewProject)
        SLATE_EVENT(FSimpleDelegate, OnOpenProject)
        SLATE_EVENT(FSimpleDelegate, OnExit)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void OpenImportPointCloudDialog();

private:
    TWeakObjectPtr<class UCSTopoSurveySubsystem> SurveySubsystem;
    TWeakPtr<SCSTopoPointCloudToolbar> PointCloudToolbar;
    FSimpleDelegate OnNewProject;
    FSimpleDelegate OnOpenProject;
    FSimpleDelegate OnExit;
    bool bFileMenuOpen = false;
    bool bOptionsMenuOpen = false;

    TSharedRef<SWidget> BuildFileMenu();
    TSharedRef<SWidget> BuildOptionsMenu();
    EVisibility GetFileMenuVisibility() const;
    EVisibility GetOptionsMenuVisibility() const;
    FReply ToggleFileMenu();
    FReply ToggleOptionsMenu();
    FReply NewProject();
    FReply OpenProject();
    FReply SaveProject();
    FReply SaveProjectAs();
    FReply ImportPointCloud();
    FReply TogglePointCloudManager();
    FReply ToggleCloudOverlay();
    FReply CheckPdalRuntime();
    FReply ExitApplication();
    FReply ResetRuntimeOptions();
    FText GetCloudOverlayButtonText() const;
    FText GetSurfaceViewRadiusText() const;
    FReply SaveProjectToPath(bool bForceDialog);
};
