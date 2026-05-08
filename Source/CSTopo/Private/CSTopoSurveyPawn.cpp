#include "CSTopoSurveyPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "CSTopoPlayerController.h"
#include "CSTopoSurveySubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr ECollisionChannel CSTopoSurfaceCollisionChannel = ECC_GameTraceChannel1;

bool IsCSTopoDerivedSurfaceComponent(const UPrimitiveComponent* Component)
{
    if (Component == nullptr)
    {
        return false;
    }

    // CharacterMovement will happily treat any blocking walkable primitive as
    // floor. CSTopo walk mode must be stricter: only derived-surface procedural
    // mesh tiles are valid survey ground. This prevents editor/template planes
    // or stale invisible collision from becoming a fake surface.
    if (Component->GetName().Contains(TEXT("SurfaceTile")))
    {
        return true;
    }

    const AActor* Owner = Component->GetOwner();
    if (Owner == nullptr)
    {
        return false;
    }

    return Owner->GetName().Contains(TEXT("CSTopoSurface"));
}

}

ACSTopoSurveyPawn::ACSTopoSurveyPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessPlayer = EAutoReceiveInput::Player0;

    Collision = GetCapsuleComponent();
    Collision->InitCapsuleSize(34.0f, 88.0f);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Collision);
    Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
    Camera->bUsePawnControlRotation = true;

    Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = false;
    Movement->bUseControllerDesiredRotation = false;
    Movement->GravityScale = 1.0f;
    Movement->AirControl = 0.0f;
    Movement->BrakingFrictionFactor = 1.0f;

    bUseControllerRotationYaw = true;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;

    FlySpeedBands = {600.0f, 1800.0f, 6000.0f, 18000.0f, 54000.0f};
    CurrentFlySpeedBandIndex = 3;
    ConfigureMovementForMode();
    RefreshMovementSpeed();
    RefreshRuntimeNavigationState();
}

void ACSTopoSurveyPawn::BeginPlay()
{
    Super::BeginPlay();
    const UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && !Survey->IsSurveyReady())
    {
        NavigationMode = ECSTopoNavigationMode::Fly;
        ConfigureMovementForMode();
        RefreshMovementSpeed();
        RefreshRuntimeNavigationState();
        return;
    }
    SetNavigationMode(NavigationMode);
}

void ACSTopoSurveyPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && Camera != nullptr)
    {
        Survey->UpdateRuntimeStreaming(Camera->GetComponentLocation(), Camera->GetForwardVector(), DeltaSeconds);
        Survey->UpdateSurveyMapPose(Camera->GetComponentLocation(), Camera->GetForwardVector());
        FocusOnActivePointCloudIfNeeded();
    }

    if (NavigationMode == ECSTopoNavigationMode::Walk)
    {
        ValidateWalkSurface(DeltaSeconds);
    }
    else
    {
        WalkSurfaceLossTimer = 0.0f;
    }

    UpdateHoverPreview(DeltaSeconds);
    UpdatePointLabels();
    UpdatePrecisionState(DeltaSeconds);
}

void ACSTopoSurveyPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ACSTopoSurveyPawn::SetNavigationMode(ECSTopoNavigationMode NewMode)
{
    if (NewMode == ECSTopoNavigationMode::Walk)
    {
        UGameInstance* GameInstance = GetGameInstance();
        UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
        FString StatusMessage;
        if (Survey != nullptr && !Survey->CanWalkActiveSurface(StatusMessage))
        {
            NavigationMode = ECSTopoNavigationMode::Fly;
            Survey->ActiveProject.NavigationMode = NavigationMode;
            Survey->SetHoverStatusLine(StatusMessage, false, false);
            ConfigureMovementForMode();
            RefreshMovementSpeed();
            RefreshRuntimeNavigationState();
            return;
        }

        if (Survey != nullptr)
        {
            FVector SurfaceRenderLocation = FVector::ZeroVector;
            if (!Survey->QueryActiveSurfaceHeightAtRenderLocation(GetActorLocation(), SurfaceRenderLocation))
            {
                NavigationMode = ECSTopoNavigationMode::Fly;
                Survey->ActiveProject.NavigationMode = NavigationMode;
                Survey->SetHoverStatusLine(
                    TEXT("Walk mode needs the pawn to be over the derived surface. Fly over the surface and press F again."),
                    false,
                    false);
                ConfigureMovementForMode();
                RefreshMovementSpeed();
                RefreshRuntimeNavigationState();
                return;
            }

            FVector Location = GetActorLocation();
            Location.Z = SurfaceRenderLocation.Z + GroundClearance;
            SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        }
    }

    NavigationMode = NewMode;
    bPrecisionModeActive = false;
    PrecisionIdleTimer = 0.0f;
    WalkSurfaceLossTimer = 0.0f;
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UCSTopoSurveySubsystem* Survey = GameInstance->GetSubsystem<UCSTopoSurveySubsystem>())
        {
            Survey->ActiveProject.NavigationMode = NavigationMode;
        }
    }
    ConfigureMovementForMode();
    RefreshMovementSpeed();
    RefreshRuntimeNavigationState();
}

void ACSTopoSurveyPawn::ToggleNavigationMode()
{
    const UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && !Survey->IsSurveyReady())
    {
        return;
    }

    SetNavigationMode(NavigationMode == ECSTopoNavigationMode::Walk ? ECSTopoNavigationMode::Fly : ECSTopoNavigationMode::Walk);
}

void ACSTopoSurveyPawn::ApplySurveyMoveForward(float Value)
{
    RawForwardInput = Value;

    const UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && !Survey->IsSurveyReady())
    {
        return;
    }

    ResolvedForwardInput = Value;
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    FVector Direction = Camera != nullptr ? Camera->GetForwardVector() : GetActorForwardVector();
    if (NavigationMode == ECSTopoNavigationMode::Walk)
    {
        Direction.Z = 0.0f;
        Direction.Normalize();
    }
    AddMovementInput(Direction, Value, true);
}

void ACSTopoSurveyPawn::ApplySurveyMoveRight(float Value)
{
    RawRightInput = Value;

    const UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && !Survey->IsSurveyReady())
    {
        return;
    }

    ResolvedRightInput = Value;
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    FVector Direction = Camera != nullptr ? Camera->GetRightVector() : GetActorRightVector();
    Direction.Z = 0.0f;
    Direction.Normalize();
    AddMovementInput(Direction, Value, true);
}

void ACSTopoSurveyPawn::ApplySurveyMoveUp(float Value)
{
    RawUpInput = Value;

    const UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr && !Survey->IsSurveyReady())
    {
        return;
    }

    ResolvedUpInput = NavigationMode == ECSTopoNavigationMode::Fly ? Value : 0.0f;
    if (NavigationMode == ECSTopoNavigationMode::Fly && !FMath::IsNearlyZero(Value))
    {
        AddMovementInput(FVector::UpVector, Value, true);
    }
}

void ACSTopoSurveyPawn::ApplySurveyFlySpeedStep(float Value)
{
    if (NavigationMode != ECSTopoNavigationMode::Fly || FMath::IsNearlyZero(Value))
    {
        return;
    }

    if (FlySpeedBands.IsEmpty())
    {
        return;
    }

    const int32 PreviousBand = CurrentFlySpeedBandIndex;
    CurrentFlySpeedBandIndex = FMath::Clamp(CurrentFlySpeedBandIndex + (Value > 0.0f ? 1 : -1), 0, FlySpeedBands.Num() - 1);
    if (CurrentFlySpeedBandIndex == PreviousBand)
    {
        return;
    }

    bPrecisionModeActive = false;
    PrecisionIdleTimer = 0.0f;
    RefreshMovementSpeed();
    RefreshRuntimeNavigationState();

    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            1.75f,
            FColor::Cyan,
            FString::Printf(TEXT("Fly speed x%d: %.0f cm/s"), CurrentFlySpeedBandIndex + 1, GetCurrentFlyBaseSpeed()));
    }
}

void ACSTopoSurveyPawn::ApplySurveyTurn(float Value)
{
    AController* CurrentController = GetController();
    if (CurrentController == nullptr || FMath::IsNearlyZero(Value))
    {
        return;
    }

    const FRotator CurrentRotation = CurrentController->GetControlRotation();
    CurrentController->SetControlRotation(FRotator(
        CurrentRotation.Pitch,
        CurrentRotation.Yaw + (Value * CurrentLookSensitivityScalar),
        0.0f));
}

void ACSTopoSurveyPawn::ApplySurveyLookUp(float Value)
{
    AController* CurrentController = GetController();
    if (CurrentController == nullptr || FMath::IsNearlyZero(Value))
    {
        return;
    }

    const FRotator CurrentRotation = CurrentController->GetControlRotation();
    const float NewPitch = FMath::ClampAngle(
        CurrentRotation.Pitch + (Value * CurrentLookSensitivityScalar),
        -89.0f,
        89.0f);
    CurrentController->SetControlRotation(FRotator(NewPitch, CurrentRotation.Yaw, 0.0f));
}

void ACSTopoSurveyPawn::SetSurveySprintHeld(bool bHeld)
{
    if (bSprintHeld == bHeld)
    {
        return;
    }

    bSprintHeld = bHeld;
    RefreshMovementSpeed();
}

void ACSTopoSurveyPawn::StartSprint()
{
    bSprintHeld = true;
    RefreshMovementSpeed();
}

void ACSTopoSurveyPawn::StopSprint()
{
    bSprintHeld = false;
    RefreshMovementSpeed();
}

void ACSTopoSurveyPawn::IncreasePointSize()
{
    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || GEngine == nullptr)
    {
        return;
    }

    float PointSize = 0.0f;
    FString Message;
    if (Survey->AdjustActivePointCloudPointSize(0.025f, PointSize, Message))
    {
        const FString SizeMessage = PointSize <= 0.0f
            ? TEXT("Point size: default")
            : FString::Printf(TEXT("Point size: %.3f"), PointSize);
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, SizeMessage);
    }
    else if (!Message.IsEmpty())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, Message);
    }
}

void ACSTopoSurveyPawn::DecreasePointSize()
{
    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || GEngine == nullptr)
    {
        return;
    }

    float PointSize = 0.0f;
    FString Message;
    if (Survey->AdjustActivePointCloudPointSize(-0.025f, PointSize, Message))
    {
        const FString SizeMessage = PointSize <= 0.0f
            ? TEXT("Point size: default")
            : FString::Printf(TEXT("Point size: %.3f"), PointSize);
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, SizeMessage);
    }
    else if (!Message.IsEmpty())
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, Message);
    }
}

void ACSTopoSurveyPawn::CollectShot()
{
    const UGameInstance* ExistingGameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* ExistingSurvey = ExistingGameInstance != nullptr ? ExistingGameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (ExistingSurvey != nullptr && !ExistingSurvey->IsSurveyReady())
    {
        return;
    }

    OnCollectShotRequested();

    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || Camera == nullptr)
    {
        return;
    }

    FCSTopoShotRecord Shot;
    FVector RenderLocation = FVector::ZeroVector;
    FString Message;
    const bool bCollected = Survey->CollectTopoShotFromView(
        Camera->GetComponentLocation(),
        Camera->GetForwardVector(),
        ShotTraceRadius,
        ShotSampleRadius,
        Shot,
        RenderLocation,
        Message);

    if (!bCollected)
    {
        if (GEngine != nullptr && !Message.IsEmpty())
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, Message);
        }
        return;
    }

    RedrawMeasurementDebug();
    const FColor ShotColor = GetShotColor(Shot);

    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(-1, 4.0f, ShotColor, Message);
    }

    if (ACSTopoPlayerController* PlayerController = Cast<ACSTopoPlayerController>(GetController()))
    {
        PlayerController->RefreshPointCloudToolbar();
    }
}

void ACSTopoSurveyPawn::TriggerCollectShot()
{
    CollectShot();
}

void ACSTopoSurveyPawn::RedrawMeasurementDebug()
{
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        return;
    }

    FlushPersistentDebugLines(World);
    LastRenderLocationByFigure.Reset();
    RenderedShotMarkers.Reset();

    TMap<int32, FCSTopoShotRecord> ShotByPointNumber;
    for (const FCSTopoShotRecord& Shot : Survey->ActiveProject.Shots)
    {
        ShotByPointNumber.Add(Shot.PointNumber, Shot);
        FVector RenderLocation = FVector::ZeroVector;
        if (Survey->SurveyPointToRenderLocation(FVector(Shot.Northing, Shot.Easting, Shot.Elevation), RenderLocation))
        {
            FCSTopoRenderedShotMarker Marker;
            Marker.PointNumber = Shot.PointNumber;
            Marker.RenderLocation = RenderLocation;
            Marker.Color = GetShotColor(Shot);
            RenderedShotMarkers.Add(Marker);
        }
    }
    RebuildPointMarkerMesh(RenderedShotMarkers);
    UpdatePointLabels();

    if (!Survey->ActiveProject.FigureSegments.IsEmpty())
    {
        for (const FCSTopoFigureSegmentRecord& Segment : Survey->ActiveProject.FigureSegments)
        {
            if (Segment.SurveyPoints.Num() < 2)
            {
                continue;
            }

            FCSTopoShotRecord ColorShot;
            ColorShot.Code = Segment.Code;
            ColorShot.BaseCode = Segment.Code;
            const FColor SegmentColor = GetShotColor(ColorShot);
            for (int32 Index = 0; Index + 1 < Segment.SurveyPoints.Num(); ++Index)
            {
                FVector A = FVector::ZeroVector;
                FVector B = FVector::ZeroVector;
                if (Survey->SurveyPointToRenderLocation(Segment.SurveyPoints[Index], A)
                    && Survey->SurveyPointToRenderLocation(Segment.SurveyPoints[Index + 1], B))
                {
                    DrawDebugLine(World, A, B, SegmentColor, true, -1.0f, 0, 5.0f);
                }
            }
        }
        return;
    }

    for (const FCSTopoFigureRecord& Figure : Survey->ActiveProject.Figures)
    {
        FCSTopoCodeStyle FigureStyle;
        if (!Survey->GetCodeStyle(Figure.Code, FigureStyle) || !DoesCSTopoPointTypeCreateFigureLinework(FigureStyle.PointType))
        {
            continue;
        }

        FCSTopoShotRecord ColorShot;
        ColorShot.Code = Figure.Code;
        ColorShot.BaseCode = Figure.Code;
        const FColor FigureColor = GetShotColor(ColorShot);
        for (int32 Index = 0; Index + 1 < Figure.PointNumbers.Num(); ++Index)
        {
            const FCSTopoShotRecord* A = ShotByPointNumber.Find(Figure.PointNumbers[Index]);
            const FCSTopoShotRecord* B = ShotByPointNumber.Find(Figure.PointNumbers[Index + 1]);
            if (A == nullptr || B == nullptr)
            {
                continue;
            }

            FVector RenderA = FVector::ZeroVector;
            FVector RenderB = FVector::ZeroVector;
            if (Survey->SurveyPointToRenderLocation(FVector(A->Northing, A->Easting, A->Elevation), RenderA)
                && Survey->SurveyPointToRenderLocation(FVector(B->Northing, B->Easting, B->Elevation), RenderB))
            {
                DrawDebugLine(World, RenderA, RenderB, FigureColor, true, -1.0f, 0, 5.0f);
            }
        }

        if (Figure.bLoopClosed && Figure.PointNumbers.Num() >= 2)
        {
            const FCSTopoShotRecord* A = ShotByPointNumber.Find(Figure.PointNumbers.Last());
            const FCSTopoShotRecord* B = ShotByPointNumber.Find(Figure.PointNumbers[0]);
            if (A != nullptr && B != nullptr)
            {
                FVector RenderA = FVector::ZeroVector;
                FVector RenderB = FVector::ZeroVector;
                if (Survey->SurveyPointToRenderLocation(FVector(A->Northing, A->Easting, A->Elevation), RenderA)
                    && Survey->SurveyPointToRenderLocation(FVector(B->Northing, B->Easting, B->Elevation), RenderB))
                {
                    DrawDebugLine(World, RenderA, RenderB, FigureColor, true, -1.0f, 0, 5.0f);
                }
            }
        }
    }
}

void ACSTopoSurveyPawn::UpdateHoverPreview(float DeltaSeconds)
{
    HoverPreviewCooldown -= DeltaSeconds;
    if (HoverPreviewCooldown > 0.0f)
    {
        return;
    }
    HoverPreviewCooldown = 0.2f;

    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || Camera == nullptr)
    {
        return;
    }
    if (!Survey->IsSurveyReady())
    {
        return;
    }

    FCSTopoMeasurementPreview Preview;
    Survey->PreviewMeasurementAtView(
        Camera->GetComponentLocation(),
        Camera->GetForwardVector(),
        ShotTraceRadius,
        ShotSampleRadius,
        Preview,
        false);

    bHasHoverSnap = Preview.bUsesSnap;
    HoverSnapPointNumber = Preview.SnapPointNumber;
    HoverSnapRenderLocation = Preview.RenderLocation;
    Survey->SetHoverStatusLine(Preview.Message, Preview.bMeasurable, Preview.bUsesRawPoint);
}

void ACSTopoSurveyPawn::UpdatePrecisionState(float DeltaSeconds)
{
    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr || !Survey->IsSurveyReady())
    {
        if (bPrecisionModeActive)
        {
            bPrecisionModeActive = false;
            RefreshMovementSpeed();
            RefreshRuntimeNavigationState();
        }
        PrecisionIdleTimer = 0.0f;
        CurrentLookSensitivityScalar = BaseLookSensitivityScalar;
        return;
    }

    const bool bHasMovementIntent = !FMath::IsNearlyZero(ResolvedForwardInput)
        || !FMath::IsNearlyZero(ResolvedRightInput)
        || (NavigationMode == ECSTopoNavigationMode::Fly && !FMath::IsNearlyZero(ResolvedUpInput));
    const bool bCanUsePrecision = Survey->IsHoverMeasurable() && !bHasMovementIntent;

    if (bCanUsePrecision)
    {
        PrecisionIdleTimer += DeltaSeconds;
    }
    else
    {
        PrecisionIdleTimer = 0.0f;
    }

    const bool bShouldBePrecise = bCanUsePrecision && PrecisionIdleTimer >= PrecisionIdleDelaySeconds;
    if (bPrecisionModeActive != bShouldBePrecise)
    {
        bPrecisionModeActive = bShouldBePrecise;
        RefreshMovementSpeed();
        RefreshRuntimeNavigationState();
    }

    CurrentLookSensitivityScalar = BaseLookSensitivityScalar * (bPrecisionModeActive ? PrecisionLookSensitivityScalar : 1.0f);
}

void ACSTopoSurveyPawn::ValidateWalkSurface(float DeltaSeconds)
{
    if (Movement == nullptr || Movement->MovementMode != MOVE_Walking)
    {
        WalkSurfaceLossTimer = 0.0f;
        return;
    }

    if (Movement->CurrentFloor.bBlockingHit
        && Movement->CurrentFloor.IsWalkableFloor()
        && IsCSTopoDerivedSurfaceComponent(Movement->CurrentFloor.HitResult.GetComponent()))
    {
        WalkSurfaceLossTimer = 0.0f;
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        FVector SurfaceRenderLocation = FVector::ZeroVector;
        if (Survey->QueryActiveSurfaceHeightAtRenderLocation(GetActorLocation(), SurfaceRenderLocation))
        {
            const float FloorZ = GetActorLocation().Z - GroundClearance;
            if (FMath::Abs(FloorZ - SurfaceRenderLocation.Z) <= MaxStepHeight + 18.0f)
            {
                WalkSurfaceLossTimer = 0.0f;
                return;
            }
        }
    }

    WalkSurfaceLossTimer += DeltaSeconds;
    if (WalkSurfaceLossTimer < WalkSurfaceGracePeriodSeconds)
    {
        return;
    }

    if (GEngine != nullptr)
    {
        GEngine->AddOnScreenDebugMessage(
            7772,
            2.5f,
            FColor::Yellow,
            TEXT("Walk mode paused: the pawn left the derived surface. Fly back over the surface and press F again."));
    }
    SetNavigationMode(ECSTopoNavigationMode::Fly);
}

void ACSTopoSurveyPawn::FocusOnActivePointCloudIfNeeded()
{
    UGameInstance* GameInstance = GetGameInstance();
    UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey == nullptr)
    {
        return;
    }

    const FString ActiveSourceId = Survey->ActiveProject.ActivePointCloudId;
    if (ActiveSourceId.IsEmpty())
    {
        bHasFocusedActivePointCloud = false;
        LastFocusedPointCloudId.Empty();
        return;
    }

    const FCSTopoPointCloudSource* Source = Survey->ActiveProject.PointClouds.FindByPredicate([&ActiveSourceId](const FCSTopoPointCloudSource& Candidate)
    {
        return Candidate.SourceId == ActiveSourceId;
    });

    if (Source == nullptr || !Source->bLoaded || !Source->bVisible)
    {
        bHasFocusedActivePointCloud = false;
        return;
    }

    if (LastFocusedPointCloudId != ActiveSourceId)
    {
        bHasFocusedActivePointCloud = false;
    }

    if (bHasFocusedActivePointCloud)
    {
        return;
    }

    FVector FocusLocation = FVector::ZeroVector;
    if (!Survey->GetActivePointCloudFocusLocation(FocusLocation))
    {
        return;
    }

    SetActorLocation(FocusLocation, false, nullptr, ETeleportType::TeleportPhysics);
    if (AController* CurrentController = GetController())
    {
        const FRotator CurrentRotation = CurrentController->GetControlRotation();
        CurrentController->SetControlRotation(FRotator(-12.0f, CurrentRotation.Yaw, 0.0f));
    }

    LastFocusedPointCloudId = ActiveSourceId;
    bHasFocusedActivePointCloud = true;
}

void ACSTopoSurveyPawn::ConfigureMovementForMode()
{
    if (Movement == nullptr)
    {
        return;
    }

    if (Collision != nullptr)
    {
        // Walk needs the capsule to collide with the active derived-surface
        // tiles only. Fly is survey navigation, not physics traversal, so it
        // avoids all blocking collision and cannot be trapped above/below
        // hidden surfaces or tile seams.
        if (NavigationMode == ECSTopoNavigationMode::Walk)
        {
            Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
            Collision->SetCollisionResponseToChannel(CSTopoSurfaceCollisionChannel, ECR_Block);
        }
        else
        {
            Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    Movement->SetMovementMode(NavigationMode == ECSTopoNavigationMode::Walk ? MOVE_Walking : MOVE_Flying);
    Movement->SetWalkableFloorAngle(WalkableFloorAngle);
    Movement->MaxStepHeight = MaxStepHeight;

    if (NavigationMode == ECSTopoNavigationMode::Walk)
    {
        Movement->MaxAcceleration = WalkAcceleration;
        Movement->BrakingDecelerationWalking = WalkBrakingDeceleration;
        Movement->BrakingDecelerationFlying = FlyBrakingDeceleration;
    }
    else
    {
        Movement->MaxAcceleration = FlyAcceleration;
        Movement->BrakingDecelerationFlying = FlyBrakingDeceleration;
        Movement->BrakingDecelerationWalking = WalkBrakingDeceleration;
    }
}

void ACSTopoSurveyPawn::RefreshMovementSpeed()
{
    if (Movement == nullptr)
    {
        return;
    }

    const float BaseSpeed = NavigationMode == ECSTopoNavigationMode::Walk
        ? (bSprintHeld ? WalkSprintSpeed : WalkSpeed)
        : (GetCurrentFlyBaseSpeed() * (bSprintHeld ? FlyBoostMultiplier : 1.0f));
    const float FinalSpeed = BaseSpeed * GetPrecisionMovementScalar();

    Movement->MaxWalkSpeed = FinalSpeed;
    Movement->MaxFlySpeed = FinalSpeed;
    Movement->MaxCustomMovementSpeed = FinalSpeed;
}

void ACSTopoSurveyPawn::RefreshRuntimeNavigationState() const
{
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UCSTopoSurveySubsystem* Survey = GameInstance->GetSubsystem<UCSTopoSurveySubsystem>())
        {
            Survey->SetRuntimeNavigationState(
                NavigationMode,
                CurrentFlySpeedBandIndex,
                FlySpeedBands.Num(),
                bPrecisionModeActive);
        }
    }
}

float ACSTopoSurveyPawn::GetCurrentFlyBaseSpeed() const
{
    if (!FlySpeedBands.IsValidIndex(CurrentFlySpeedBandIndex))
    {
        return 18000.0f;
    }

    return FlySpeedBands[CurrentFlySpeedBandIndex];
}

float ACSTopoSurveyPawn::GetPrecisionMovementScalar() const
{
    return bPrecisionModeActive ? PrecisionMovementScalar : 1.0f;
}

void ACSTopoSurveyPawn::EnsureSurveyVisualization()
{
    if (SurveyVisualizationActor != nullptr && PointMarkerMeshComponent != nullptr)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    if (SurveyVisualizationActor == nullptr)
    {
        SurveyVisualizationActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
        if (SurveyVisualizationActor == nullptr)
        {
            return;
        }

        SurveyVisualizationActor->SetOwner(this);
        USceneComponent* VisualizationRoot = NewObject<USceneComponent>(SurveyVisualizationActor, TEXT("Root"));
        SurveyVisualizationActor->SetRootComponent(VisualizationRoot);
        VisualizationRoot->RegisterComponent();
#if WITH_EDITOR
        SurveyVisualizationActor->SetActorLabel(TEXT("CSTopoSurveyPointDisplay"));
#endif
    }

    if (PointMarkerMeshComponent == nullptr)
    {
        PointMarkerMeshComponent = NewObject<UProceduralMeshComponent>(SurveyVisualizationActor, TEXT("PointPyramids"));
        PointMarkerMeshComponent->SetupAttachment(SurveyVisualizationActor->GetRootComponent());
        PointMarkerMeshComponent->RegisterComponent();
        PointMarkerMeshComponent->SetMobility(EComponentMobility::Movable);
        PointMarkerMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PointMarkerMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
        PointMarkerMeshComponent->SetCastShadow(false);
        PointMarkerMeshComponent->bCastDynamicShadow = false;
        static UMaterialInterface* VertexColorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
        PointMarkerMeshComponent->SetMaterial(0, VertexColorMaterial != nullptr ? VertexColorMaterial : UMaterial::GetDefaultMaterial(MD_Surface));
    }
}

void ACSTopoSurveyPawn::RebuildPointMarkerMesh(const TArray<FCSTopoRenderedShotMarker>& ShotMarkers)
{
    EnsureSurveyVisualization();
    if (PointMarkerMeshComponent == nullptr)
    {
        return;
    }

    PointMarkerMeshComponent->ClearAllMeshSections();
    if (ShotMarkers.IsEmpty())
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(ShotMarkers.Num() * 5);
    Triangles.Reserve(ShotMarkers.Num() * 18);
    Colors.Reserve(ShotMarkers.Num() * 5);

    const float PyramidHeight = FMath::Max(ShotMarkerRadius * 3.0f, 30.0f);
    const float BaseHalfWidth = FMath::Max(ShotMarkerRadius * 0.45f, 4.0f);
    for (const FCSTopoRenderedShotMarker& Marker : ShotMarkers)
    {
        const int32 BaseIndex = Vertices.Num();
        const FVector Tip = Marker.RenderLocation;
        const FVector BaseCenter = Tip + FVector(0.0f, 0.0f, PyramidHeight);
        Vertices.Add(Tip);
        Vertices.Add(BaseCenter + FVector(BaseHalfWidth, BaseHalfWidth, 0.0f));
        Vertices.Add(BaseCenter + FVector(-BaseHalfWidth, BaseHalfWidth, 0.0f));
        Vertices.Add(BaseCenter + FVector(-BaseHalfWidth, -BaseHalfWidth, 0.0f));
        Vertices.Add(BaseCenter + FVector(BaseHalfWidth, -BaseHalfWidth, 0.0f));

        Triangles.Append({
            BaseIndex, BaseIndex + 1, BaseIndex + 2,
            BaseIndex, BaseIndex + 2, BaseIndex + 3,
            BaseIndex, BaseIndex + 3, BaseIndex + 4,
            BaseIndex, BaseIndex + 4, BaseIndex + 1,
            BaseIndex + 1, BaseIndex + 3, BaseIndex + 2,
            BaseIndex + 1, BaseIndex + 4, BaseIndex + 3
        });

        const FLinearColor MarkerColor = FLinearColor(Marker.Color);
        for (int32 ColorIndex = 0; ColorIndex < 5; ++ColorIndex)
        {
            Colors.Add(MarkerColor);
        }
    }

    PointMarkerMeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
    PointMarkerMeshComponent->SetVisibility(true, true);
    PointMarkerMeshComponent->SetHiddenInGame(false, true);
}

void ACSTopoSurveyPawn::UpdatePointLabels()
{
    if (Camera == nullptr || RenderedShotMarkers.IsEmpty())
    {
        for (UTextRenderComponent* Label : PointLabelComponents)
        {
            if (Label != nullptr)
            {
                Label->SetVisibility(false, true);
            }
        }
        for (UTextRenderComponent* Label : PointLabelBoldComponents)
        {
            if (Label != nullptr)
            {
                Label->SetVisibility(false, true);
            }
        }
        LastVisiblePointLabelNumbers.Reset();
        return;
    }

    EnsureSurveyVisualization();
    if (SurveyVisualizationActor == nullptr)
    {
        return;
    }

    struct FLabelCandidate
    {
        int32 PointNumber = INDEX_NONE;
        FVector RenderLocation = FVector::ZeroVector;
        FColor Color = FColor::White;
        double DistanceSq = 0.0;
        bool bIsSnapTarget = false;
        bool bWasVisible = false;
    };

    constexpr int32 MaxNearbyLabels = 24;
    TArray<FLabelCandidate> Candidates;
    TSet<int32> AddedPointNumbers;
    const FVector CameraLocation = Camera->GetComponentLocation();
    const FVector CameraForward = Camera->GetForwardVector();

    auto AddLabelCandidate = [&](const FCSTopoRenderedShotMarker& Marker, bool bForce, bool bIsSnapTarget)
    {
        if (Marker.PointNumber == INDEX_NONE || AddedPointNumbers.Contains(Marker.PointNumber))
        {
            return;
        }

        const FVector ToMarker = Marker.RenderLocation - CameraLocation;
        const bool bWasVisible = LastVisiblePointLabelNumbers.Contains(Marker.PointNumber);
        const float FacingDot = FVector::DotProduct(ToMarker.GetSafeNormal(), CameraForward);
        if (!bForce && FacingDot <= (bWasVisible ? -0.08f : 0.04f))
        {
            return;
        }

        FLabelCandidate Candidate;
        Candidate.PointNumber = Marker.PointNumber;
        Candidate.RenderLocation = Marker.RenderLocation;
        Candidate.Color = Marker.Color;
        Candidate.DistanceSq = ToMarker.SizeSquared();
        Candidate.bIsSnapTarget = bIsSnapTarget;
        Candidate.bWasVisible = bWasVisible;
        Candidates.Add(Candidate);
        AddedPointNumbers.Add(Marker.PointNumber);
    };

    if (bHasHoverSnap && HoverSnapPointNumber != INDEX_NONE)
    {
        if (const FCSTopoRenderedShotMarker* HoverMarker = RenderedShotMarkers.FindByPredicate([this](const FCSTopoRenderedShotMarker& Marker)
        {
            return Marker.PointNumber == HoverSnapPointNumber;
        }))
        {
            AddLabelCandidate(*HoverMarker, true, true);
        }
    }

    TArray<FCSTopoRenderedShotMarker> NearbyMarkers = RenderedShotMarkers;
    NearbyMarkers.Sort([&CameraLocation](const FCSTopoRenderedShotMarker& A, const FCSTopoRenderedShotMarker& B)
    {
        const double ADistanceSq = FVector::DistSquared(A.RenderLocation, CameraLocation);
        const double BDistanceSq = FVector::DistSquared(B.RenderLocation, CameraLocation);
        if (!FMath::IsNearlyEqual(ADistanceSq, BDistanceSq))
        {
            return ADistanceSq < BDistanceSq;
        }
        return A.PointNumber < B.PointNumber;
    });

    for (const FCSTopoRenderedShotMarker& Marker : NearbyMarkers)
    {
        AddLabelCandidate(Marker, false, false);
    }

    Candidates.Sort([](const FLabelCandidate& A, const FLabelCandidate& B)
    {
        if (A.bIsSnapTarget != B.bIsSnapTarget)
        {
            return A.bIsSnapTarget;
        }

        const double AScore = A.DistanceSq * (A.bWasVisible ? 0.72 : 1.0);
        const double BScore = B.DistanceSq * (B.bWasVisible ? 0.72 : 1.0);
        if (!FMath::IsNearlyEqual(AScore, BScore))
        {
            return AScore < BScore;
        }
        return A.PointNumber < B.PointNumber;
    });

    if (Candidates.Num() > MaxNearbyLabels)
    {
        Candidates.SetNum(MaxNearbyLabels);
    }

    while (PointLabelComponents.Num() < Candidates.Num())
    {
        UTextRenderComponent* Label = NewObject<UTextRenderComponent>(SurveyVisualizationActor, *FString::Printf(TEXT("PointLabel_%d"), PointLabelComponents.Num()));
        Label->SetupAttachment(SurveyVisualizationActor->GetRootComponent());
        Label->RegisterComponent();
        Label->SetMobility(EComponentMobility::Movable);
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->SetCollisionResponseToAllChannels(ECR_Ignore);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(24.0f);
        Label->SetTextRenderColor(FColor::White);
        Label->SetCastShadow(false);
        Label->bCastDynamicShadow = false;
        PointLabelComponents.Add(Label);
    }
    while (PointLabelBoldComponents.Num() < Candidates.Num())
    {
        UTextRenderComponent* Label = NewObject<UTextRenderComponent>(SurveyVisualizationActor, *FString::Printf(TEXT("PointLabelBold_%d"), PointLabelBoldComponents.Num()));
        Label->SetupAttachment(SurveyVisualizationActor->GetRootComponent());
        Label->RegisterComponent();
        Label->SetMobility(EComponentMobility::Movable);
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->SetCollisionResponseToAllChannels(ECR_Ignore);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(30.0f);
        Label->SetTextRenderColor(FColor::White);
        Label->SetCastShadow(false);
        Label->bCastDynamicShadow = false;
        PointLabelBoldComponents.Add(Label);
    }

    const float LabelHeight = FMath::Max(ShotMarkerRadius * 4.2f, 42.0f);
    TSet<int32> NewVisiblePointLabelNumbers;
    for (int32 Index = 0; Index < PointLabelComponents.Num(); ++Index)
    {
        UTextRenderComponent* Label = PointLabelComponents[Index];
        UTextRenderComponent* BoldLabel = PointLabelBoldComponents.IsValidIndex(Index) ? PointLabelBoldComponents[Index] : nullptr;
        if (Label == nullptr)
        {
            continue;
        }

        if (!Candidates.IsValidIndex(Index))
        {
            Label->SetVisibility(false, true);
            if (BoldLabel != nullptr)
            {
                BoldLabel->SetVisibility(false, true);
            }
            continue;
        }

        const FLabelCandidate& Candidate = Candidates[Index];
        const FVector LabelLocation = Candidate.RenderLocation + FVector(0.0f, 0.0f, LabelHeight);
        const FRotator LabelRotation = (CameraLocation - LabelLocation).Rotation();
        const FString LabelText = FString::Printf(TEXT("%d"), Candidate.PointNumber);
        const float LabelWorldSize = Candidate.bIsSnapTarget ? 29.0f : 24.0f;

        Label->SetText(FText::FromString(LabelText));
        Label->SetTextRenderColor(Candidate.Color);
        Label->SetWorldSize(LabelWorldSize);
        Label->SetWorldLocation(LabelLocation);
        Label->SetWorldRotation(LabelRotation);
        Label->SetVisibility(true, true);
        Label->SetHiddenInGame(false, true);

        if (BoldLabel != nullptr)
        {
            if (Candidate.bIsSnapTarget)
            {
                const FVector CameraRight = Camera->GetRightVector();
                const FVector CameraUp = Camera->GetUpVector();
                BoldLabel->SetText(FText::FromString(LabelText));
                BoldLabel->SetTextRenderColor(Candidate.Color);
                BoldLabel->SetWorldSize(LabelWorldSize);
                BoldLabel->SetWorldLocation(LabelLocation + CameraRight * 1.5f + CameraUp * 1.5f);
                BoldLabel->SetWorldRotation(LabelRotation);
                BoldLabel->SetVisibility(true, true);
                BoldLabel->SetHiddenInGame(false, true);
            }
            else
            {
                BoldLabel->SetVisibility(false, true);
            }
        }

        NewVisiblePointLabelNumbers.Add(Candidate.PointNumber);
    }

    LastVisiblePointLabelNumbers = MoveTemp(NewVisiblePointLabelNumbers);
}

bool ACSTopoSurveyPawn::IsPrecisionModeActive() const
{
    return bPrecisionModeActive;
}

int32 ACSTopoSurveyPawn::GetCurrentFlySpeedBandIndex() const
{
    return CurrentFlySpeedBandIndex;
}

FColor ACSTopoSurveyPawn::GetShotColor(const FCSTopoShotRecord& Shot) const
{
    UGameInstance* GameInstance = GetGameInstance();
    const UCSTopoSurveySubsystem* Survey = GameInstance != nullptr ? GameInstance->GetSubsystem<UCSTopoSurveySubsystem>() : nullptr;
    if (Survey != nullptr)
    {
        const FString StyleCode = Shot.BaseCode.IsEmpty() ? Shot.Code : Shot.BaseCode;
        for (const FCSTopoCodeStyle& Style : Survey->ActiveProject.CodePalette)
        {
            if (Style.Code.Equals(StyleCode, ESearchCase::IgnoreCase))
            {
                return Style.Color.ToFColor(true);
            }
        }
    }

    return FColor::Yellow;
}
