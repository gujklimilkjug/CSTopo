#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSTopoTypes.h"
#include "CSTopoSurveyPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UCharacterMovementComponent;
class UProceduralMeshComponent;
class UTextRenderComponent;

struct FCSTopoRenderedShotMarker
{
    int32 PointNumber = INDEX_NONE;
    FVector RenderLocation = FVector::ZeroVector;
};

UCLASS()
class CSTOPO_API ACSTopoSurveyPawn : public ACharacter
{
    GENERATED_BODY()

public:
    ACSTopoSurveyPawn();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CSTopo")
    TObjectPtr<UCapsuleComponent> Collision;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CSTopo")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CSTopo")
    TObjectPtr<UCharacterMovementComponent> Movement;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoNavigationMode NavigationMode = ECSTopoNavigationMode::Walk;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkSpeed = 600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkSprintSpeed = 1200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkAcceleration = 2048.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkBrakingDeceleration = 2048.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkableFloorAngle = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float MaxStepHeight = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float FlyAcceleration = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float FlyBrakingDeceleration = 16000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<float> FlySpeedBands;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float FlyBoostMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float GroundClearance = 88.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float BaseLookSensitivityScalar = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float PrecisionIdleDelaySeconds = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float PrecisionLookSensitivityScalar = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float PrecisionMovementScalar = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float WalkSurfaceGracePeriodSeconds = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Survey")
    float ShotTraceRadius = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Survey")
    float ShotSampleRadius = 40.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Survey")
    float ShotMarkerRadius = 10.0f;

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    void SetNavigationMode(ECSTopoNavigationMode NewMode);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    void ToggleNavigationMode();

    UFUNCTION(BlueprintPure, Category = "CSTopo")
    bool IsPrecisionModeActive() const;

    UFUNCTION(BlueprintPure, Category = "CSTopo")
    int32 GetCurrentFlySpeedBandIndex() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void TriggerCollectShot();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void RedrawMeasurementDebug();

    UFUNCTION(BlueprintImplementableEvent, Category = "CSTopo")
    void OnCollectShotRequested();

    void ApplySurveyMoveForward(float Value);
    void ApplySurveyMoveRight(float Value);
    void ApplySurveyMoveUp(float Value);
    void ApplySurveyFlySpeedStep(float Value);
    void ApplySurveyTurn(float Value);
    void ApplySurveyLookUp(float Value);
    void SetSurveySprintHeld(bool bHeld);

protected:
    virtual void BeginPlay() override;

private:
    void StartSprint();
    void StopSprint();
    void IncreasePointSize();
    void DecreasePointSize();
    void CollectShot();
    void UpdateHoverPreview(float DeltaSeconds);
    void UpdatePrecisionState(float DeltaSeconds);
    void ValidateWalkSurface(float DeltaSeconds);
    void FocusOnActivePointCloudIfNeeded();
    void ConfigureMovementForMode();
    void RefreshMovementSpeed();
    void RefreshRuntimeNavigationState() const;
    float GetCurrentFlyBaseSpeed() const;
    float GetPrecisionMovementScalar() const;
    FColor GetShotColor(const FCSTopoShotRecord& Shot) const;
    void EnsureSurveyVisualization();
    void RebuildPointMarkerMesh(const TArray<TPair<FCSTopoShotRecord, FVector>>& ShotMarkers);
    void UpdatePointLabels();

    bool bSprintHeld = false;
    uint64 LastMovementAxisFrame = 0;
    uint64 LastVerticalAxisFrame = 0;
    bool bHasFocusedActivePointCloud = false;
    float HoverPreviewCooldown = 0.0f;
    float PrecisionIdleTimer = 0.0f;
    float WalkSurfaceLossTimer = 0.0f;
    float RawForwardInput = 0.0f;
    float RawRightInput = 0.0f;
    float RawUpInput = 0.0f;
    float ResolvedForwardInput = 0.0f;
    float ResolvedRightInput = 0.0f;
    float ResolvedUpInput = 0.0f;
    float CurrentLookSensitivityScalar = 1.0f;
    int32 CurrentFlySpeedBandIndex = 3;
    bool bPrecisionModeActive = false;
    bool bHasHoverSnap = false;
    int32 HoverSnapPointNumber = INDEX_NONE;
    FVector HoverSnapRenderLocation = FVector::ZeroVector;
    FString LastFocusedPointCloudId;
    TMap<FGuid, FVector> LastRenderLocationByFigure;
    TArray<FCSTopoRenderedShotMarker> RenderedShotMarkers;

    UPROPERTY()
    TObjectPtr<AActor> SurveyVisualizationActor;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> PointMarkerMeshComponent;

    UPROPERTY()
    TArray<TObjectPtr<UTextRenderComponent>> PointLabelComponents;
};
