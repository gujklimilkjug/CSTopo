#pragma once

#include "CoreMinimal.h"
#include "CSTopoTypes.h"
#include "GameFramework/PlayerController.h"
#include "CSTopoPlayerController.generated.h"

class SCSTopoPointCloudToolbar;
class SCSTopoMainMenu;
class SCSTopoReticle;
class SCSTopoHomeScreen;
class SCSTopoSurveyHud;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class CSTOPO_API ACSTopoPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupInputComponent() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|UI")
    bool bShowPointCloudToolbarOnStart = false;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void TogglePointCloudToolbar();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void RefreshPointCloudToolbar();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void ShowHomeScreen();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void HideHomeScreenAndEnterSurvey();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void SetSurveyInputEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|UI")
    void ToggleSurveyCollectorFocus();

    void OpenProjectFromDialog();
    void ImportPointCloudToNewProjectFromDialog();
    void ExitApplication();

private:
    enum class ESurveyInputMode : uint8
    {
        Game,
        CollectorUI,
        ModalUI
    };

    TSharedPtr<SCSTopoPointCloudToolbar> PointCloudToolbar;
    TSharedPtr<SWidget> PointCloudToolbarContainer;
    TSharedPtr<SCSTopoMainMenu> MainMenu;
    TSharedPtr<SWidget> MainMenuContainer;
    TSharedPtr<SCSTopoReticle> Reticle;
    TSharedPtr<SWidget> ReticleContainer;
    TSharedPtr<SWidget> SurveyStatusOverlay;
    TSharedPtr<SWidget> SurveyStatusOverlayContainer;
    TSharedPtr<SCSTopoSurveyHud> SurveyHud;
    TSharedPtr<SWidget> SurveyHudContainer;
    TSharedPtr<SCSTopoHomeScreen> HomeScreen;
    TSharedPtr<SWidget> HomeScreenContainer;
    FString PendingStartupProjectPath;
    bool bSurveyInputEnabled = false;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> SurveyInputMappingContext;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveForwardAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveRightAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveUpAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> LookYawAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> LookPitchAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> FlySpeedAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleNavigationAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleUserTinAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> UndoAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SprintAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> CollectShotAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> EscapeAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> TabAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> Code1Action;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> Code2Action;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> Code3Action;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> Code4Action;

    void SetToolbarVisible(bool bVisible);
    void ImportPointCloudFromShortcut();
    void HandleEscapePressed();
    void RetrySurfaceBuild();
    void CancelToHome();
    void CollectShotFromSurveyHud();
    void UndoLastMeasurementFromSurveyHud();
    void MonitorStartupSurfaceBuild();
    bool GetActiveSource(class UCSTopoSurveySubsystem* Survey, FCSTopoPointCloudSource*& Source) const;
    void SetActiveCodeByPaletteIndex(int32 PaletteIndex);
    void SetActiveCode1();
    void SetActiveCode2();
    void SetActiveCode3();
    void SetActiveCode4();
    void EnsureSurveyEnhancedInput();
    void RegisterSurveyInputMappingContext();
    void UnregisterSurveyInputMappingContext();
    void HandleMoveForwardInput(const FInputActionValue& Value);
    void HandleMoveRightInput(const FInputActionValue& Value);
    void HandleMoveUpInput(const FInputActionValue& Value);
    void HandleLookYawInput(const FInputActionValue& Value);
    void HandleLookPitchInput(const FInputActionValue& Value);
    void HandleFlySpeedInput(const FInputActionValue& Value);
    void HandleToggleNavigationInput();
    void HandleToggleUserTinInput();
    void HandleUndoInput();
    void HandleSprintStarted();
    void HandleSprintCompleted();
    void HandleCollectShotInput();
    void HandleEscapeInput();
    void HandleTabInput();
    void ApplySurveyInputMode(bool bFlushPressedKeys = true);
    void EnterSurveyGameMode();
    void EnterCollectorMode();
    void EnterModalUIMode();
    void QueueSurveyGameFocusRepair(int32 FrameCount);
    void TickSurveyGameFocusRepair();
    void ApplyHeldSurveyInput(float DeltaSeconds);
    void ResetHeldSurveyInput();
    void SetSurveyInputDebug(const FString& LastEvent);
    FText GetSurveyStatusOverlayText() const;

    ESurveyInputMode SurveyInputMode = ESurveyInputMode::ModalUI;
    bool bSprintHeld = false;
    float MoveForwardAxis = 0.0f;
    float MoveRightAxis = 0.0f;
    float MoveUpAxis = 0.0f;
    int32 PendingSurveyGameFocusRepairFrames = 0;
    float LastMouseInputDebugTimeSeconds = 0.0f;
    FString LastSurveyInputEvent = TEXT("none");
};
