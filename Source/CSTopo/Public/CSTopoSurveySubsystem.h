#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSTopoTypes.h"
#include "CSTopoSurveySubsystem.generated.h"

struct FCSTopoLoadedSurfaceTile
{
    FString TileId;
    FString MeshPath;
    FString MeshFormat;
    FVector BoundsMin = FVector::ZeroVector;
    FVector BoundsMax = FVector::ZeroVector;
    TArray<FVector> SourceVertices;
    TArray<int32> Triangles;
    TObjectPtr<class UProceduralMeshComponent> MeshComponent = nullptr;
    bool bRuntimeVisible = true;
    bool bGeometryLoaded = false;
    bool bTileLoadQueued = false;
    int32 BoundaryEdgeCount = 0;
    int32 BoundaryLoopCount = 0;
};

struct FCSTopoLoadedSurface
{
    FString SourceId;
    FString SurfaceId;
    FString ManifestPath;
    FVector SourceCenter = FVector::ZeroVector;
    TArray<FCSTopoLoadedSurfaceTile> Tiles;
    TSet<FString> RuntimeVisibleTileIds;
    TSet<FString> CollisionProxyTileIds;
    TArray<FString> PendingTileLoadIds;
    TObjectPtr<class UProceduralMeshComponent> CollisionProxyComponent = nullptr;
    FVector LastCollisionProxySourceLocation = FVector::ZeroVector;
    FVector LastVisibilitySourceLocation = FVector::ZeroVector;
    bool bHasCollisionProxySourceLocation = false;
    bool bHasVisibilitySourceLocation = false;
    float TileVisibilityRefreshCooldown = 0.0f;

    bool IsReady() const
    {
        return !SurfaceId.IsEmpty() && !Tiles.IsEmpty();
    }
};

enum class ECSTopoMeasurementSnapKind : uint8
{
    None,
    StoredPoint,
    LineVertex
};

struct FCSTopoMeasurementPreview
{
    bool bMeasurable = false;
    bool bUsesDerivedSurface = false;
    bool bUsesRawPoint = false;
    bool bUsesSnap = false;
    ECSTopoMeasurementSnapKind SnapKind = ECSTopoMeasurementSnapKind::None;
    int32 SnapPointNumber = INDEX_NONE;
    FString FitType = TEXT("Unmeasurable");
    FString Message;
    FString SurfaceTileId;
    FVector RenderLocation = FVector::ZeroVector;
    FVector SurveyNez = FVector::ZeroVector;
    int32 SampleCount = 0;
    float ResolvedTraceRadius = 0.0f;
    float ResolvedSampleRadius = 0.0f;
};

UCLASS()
class CSTOPO_API UCSTopoSurveySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo")
    FCSTopoProjectDocument ActiveProject;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Workflow")
    ECSTopoWorkflowState WorkflowState = ECSTopoWorkflowState::Home;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Workflow")
    FString WorkflowStatus = TEXT("Choose a project or point cloud to begin.");

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    void NewProject(const FString& ProjectName);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    bool LoadProject(const FString& FilePath, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    bool SaveProject(const FString& FilePath, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Workflow")
    void SetWorkflowState(ECSTopoWorkflowState NewState, const FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Workflow")
    bool IsSurveyReady() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Workflow")
    void UpdateWorkflow();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Workflow")
    FString GetCurrentProjectPath() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    FCSTopoShotRecord AddFittedShotFromSamples(const FString& Code, double Northing, double Easting, const TArray<FVector>& Samples, const FString& SourceCloudId, bool& bSuccess, const FString& ControlParameter = TEXT(""), bool bJoinLinework = true);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    bool ExportCadDeliverables(const FString& CsvPath, const FString& DxfPath, FString& ErrorMessage) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    void SetActiveCode(const FString& Code);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool SplitActiveFigure(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool CloseActiveFigure(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool AddPointCloud(const FString& SourcePath, const FString& CacheDirectory, FCSTopoPointCloudSource& AddedSource, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool ImportPointCloud(const FString& SourcePath, FCSTopoPointCloudSource& ImportedSource, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool RemovePointCloud(const FString& SourceId);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool SetActivePointCloud(const FString& SourceId);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool SetPointCloudVisible(const FString& SourceId, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool SetPointCloudLoaded(const FString& SourceId, bool bLoaded);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    TArray<FCSTopoPointCloudSource> GetPointClouds() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Point Clouds")
    bool StartCopcCacheBuild(const FString& SourceId, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool StartSurfaceBuild(const FString& SourceId, bool bForceRebuild, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool SetSurfaceVisible(const FString& SourceId, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool ToggleActiveSourceTinRenderVisible(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool SetActiveSourceTinRenderVisible(bool bVisible, FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool ToggleUserTinVisible(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool SetUserTinVisible(bool bVisible, FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    FString GetUserTinStatusLine() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool SetCloudOverlayVisible(const FString& SourceId, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool CycleActiveViewMode(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool CanWalkActiveSurface(FString& StatusMessage) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool CollectTopoShotFromView(const FVector& ViewOrigin, const FVector& ViewDirection, float TraceRadius, float SampleRadius, FCSTopoShotRecord& Shot, FVector& RenderLocation, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    TArray<FCSTopoControlCodeDefinition> GetControlCodeDefinitions() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool SetPendingControlCode(const FString& ControlCode, const FString& Parameter, FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void ClearPendingControlCode();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void CancelPendingControlCode(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    FString GetPendingControlCode() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    FString GetPendingControlParameter() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool IsPendingControlAutomatic() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool UndoLastMeasurement(FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool QueryActiveSurfaceHeightAtRenderLocation(const FVector& RenderLocation, FVector& SurfaceRenderLocation) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool GetActivePointCloudFocusLocation(FVector& FocusRenderLocation) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void PrepareSurveyScene();

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool FocusPawnOnActiveSurfaceCenter(class APawn* Pawn, float Clearance, FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool ValidateActiveCloudSurfaceAlignment(FCSTopoAlignmentReport& Report);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Surface")
    bool SetCloudOverlayVisibleForActiveSource(bool bVisible, FString& StatusMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    FString GetActiveSurveyStatusLine() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    FString GetActiveFigureStatusLine() const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    TArray<FCSTopoShotRecord> GetRecentShots(int32 MaxShots) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool GetActiveSurveyBounds(FVector2D& OutMinSurveyNe, FVector2D& OutMaxSurveyNe) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool SurveyPointToRenderLocation(const FVector& SurveyPoint, FVector& RenderLocation) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    void UpdateSurveyMapPose(const FVector& CameraRenderLocation, const FVector& ViewDirection);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool GetCurrentSurveyMapPose(FVector2D& OutSurveyNe, FVector2D& OutHeading) const;

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Survey")
    bool GetCodeStyle(const FString& Code, FCSTopoCodeStyle& OutStyle) const;

    void SetRuntimeNavigationState(ECSTopoNavigationMode Mode, int32 FlySpeedBandIndex, int32 FlySpeedBandCount, bool bPrecisionActive);
    FString GetRuntimeNavigationStatusLine() const;
    void SetRuntimeInputDebugState(const FString& DebugLine);
    void SetHoverStatusLine(const FString& Message, bool bMeasurable, bool bUsesRawPoint);
    FString GetHoverStatusLine() const;
    bool IsHoverMeasurable() const;
    bool DoesHoverUseRawPoint() const;

    void UpdateRuntimeStreaming(const FVector& CameraRenderLocation, const FVector& ViewDirection, float DeltaSeconds);
    bool PreviewMeasurementAtView(const FVector& ViewOrigin, const FVector& ViewDirection, float TraceRadius, float SampleRadius, FCSTopoMeasurementPreview& Preview, bool bAllowExpensiveSearch = true) const;
    bool AdjustActivePointCloudPointSize(float Delta, float& NewPointSize, FString& ErrorMessage);
    bool GetActivePointCloudPointSize(float& PointSize, FString& ErrorMessage) const;

private:
    UPROPERTY()
    TMap<FString, TObjectPtr<class ALidarPointCloudActor>> PointCloudActors;
    UPROPERTY()
    TMap<FString, TObjectPtr<class AActor>> SurfaceActors;
    UPROPERTY()
    TObjectPtr<class AActor> UserTinActor;
    UPROPERTY()
    TObjectPtr<class UProceduralMeshComponent> UserTinMeshComponent;

    TMap<FString, FString> PointCloudActorDisplayPaths;
    TMap<FString, FCSTopoLoadedSurface> LoadedSurfaces;
    FString CurrentProjectPath;
    FProcHandle RuntimeWindowProcessHandle;
    FString RuntimeWindowProcessSourceId;
    FString RuntimeWindowProcessPipelinePath;
    FString RuntimeWindowProcessOutputPath;
    FProcHandle SurfaceBuildProcessHandle;
    FString SurfaceBuildProcessSourceId;
    FString SurfaceBuildManifestPath;
    FString SurfaceBuildProgressPath;
    float RuntimeWindowUpdateCooldown = 0.0f;
    FCSTopoAlignmentReport LastAlignmentReport;
    FString LastHoverStatusLine = TEXT("Hover: no measurement yet.");
    bool bLastHoverMeasurable = false;
    bool bLastHoverUsesRawPoint = false;
    ECSTopoNavigationMode RuntimeNavigationMode = ECSTopoNavigationMode::Walk;
    int32 RuntimeFlySpeedBandIndex = 3;
    int32 RuntimeFlySpeedBandCount = 5;
    bool bRuntimePrecisionActive = false;
    FString RuntimeInputDebugLine = TEXT("Input: UI | Last: none");
    FVector LastSurveyMapRenderLocation = FVector::ZeroVector;
    FVector LastSurveyMapViewDirection = FVector::ForwardVector;
    bool bHasSurveyMapPose = false;
    bool bUserTinVisible = false;
    bool bUserTinDirty = true;
    int32 LastUserTinShotCount = INDEX_NONE;
    int32 LastUserTinFigureCount = INDEX_NONE;
    FString UserTinStatusLine = TEXT("User TIN hidden.");
    FString PendingControlCode;
    FString PendingControlParameter;
    bool bPendingControlStartsNewFigure = false;
    FString PendingControlBaseCode;
    bool bPendingControlAutomatic = false;
    TArray<FCSTopoControlCodeDefinition> ControlCodeDefinitions;

    bool SpawnPointCloudActor(FCSTopoPointCloudSource& Source, FString& ErrorMessage);
    bool LoadDerivedSurfaceForSource(FCSTopoPointCloudSource& Source, FString& ErrorMessage);
    bool TraceActiveDerivedSurface(const FCSTopoPointCloudSource& Source, const FVector& ViewOrigin, const FVector& ViewDirection, FVector& SurfaceRenderHit, FString& TileId, FString& ErrorMessage) const;
    bool QueryDerivedSurfaceHeightAtSourceXY(const FCSTopoPointCloudSource& Source, double SourceX, double SourceY, double& SurfaceZ, FString* OutTileId = nullptr) const;
    bool QueryNearestDerivedSurfaceHeightAtSourceXY(const FCSTopoPointCloudSource& Source, double SourceX, double SourceY, double SearchRadiusSourceUnits, double& SurfaceZ, FString* OutTileId = nullptr) const;
    bool TraceTriangleFromView(const FVector& ViewOrigin, const FVector& ViewDirection, const FVector& A, const FVector& B, const FVector& C, double& OutDistance, FVector& OutHit) const;
    bool LoadTileGeometry(const FString& MeshPath, const FString& MeshFormat, FCSTopoLoadedSurfaceTile& Tile, FString& ErrorMessage) const;
    bool EnsureSurfaceTileGeometryLoaded(const FCSTopoPointCloudSource& Source, FCSTopoLoadedSurfaceTile& Tile, FString& ErrorMessage);
    bool EnsureSurfaceTileRenderMesh(FCSTopoPointCloudSource& Source, FCSTopoLoadedSurface& Surface, FCSTopoLoadedSurfaceTile& Tile, FString& ErrorMessage);
    void QueueSurfaceTileLoad(FCSTopoLoadedSurface& Surface, FCSTopoLoadedSurfaceTile& Tile);
    void ProcessPendingSurfaceTileLoads(FCSTopoPointCloudSource& Source, int32 MaxTiles, int64 MaxBytes);
    void RefreshSurfacePresentation(FCSTopoPointCloudSource& Source);
    void UpdateVisibleSurfaceTiles(FCSTopoPointCloudSource& Source, const FVector& SourceLocation, bool bForce);
    void RebuildSurfaceCollisionProxy(const FCSTopoPointCloudSource& Source, FCSTopoLoadedSurface& Surface, const FVector& SourceLocation, bool bForce);
    bool ShouldUseSurfaceTileForRuntimeQuery(const FCSTopoLoadedSurface& Surface, const FCSTopoLoadedSurfaceTile& Tile) const;
    void RefreshSurfaceBuildProcess();
    void RefreshSurfaceBuildProgress(FCSTopoPointCloudSource& Source);
    bool FindActiveRenderablePointCloud(FCSTopoPointCloudSource*& Source, class ALidarPointCloudActor*& Actor, FString& ErrorMessage);
    bool FindActiveRenderablePointCloud(const FCSTopoPointCloudSource*& Source, class ALidarPointCloudActor*& Actor, FString& ErrorMessage) const;
    FCSTopoPointCloudSource* FindPointCloudMutable(const FString& SourceId);
    const FCSTopoPointCloudSource* FindPointCloud(const FString& SourceId) const;
    FCSTopoDerivedSurfaceManifest* FindDerivedSurfaceMutable(const FString& SourceId);
    const FCSTopoDerivedSurfaceManifest* FindDerivedSurface(const FString& SourceId) const;
    bool FindSnapTargetFromView(const FVector& ViewOrigin, const FVector& ViewDirection, FCSTopoMeasurementPreview& Preview) const;
    FString GetProjectCacheDirectory() const;
    bool ShouldUseCopcWindowStreaming(const FCSTopoPointCloudSource& Source) const;
    void RefreshRuntimeWindowProcess();
    void RefreshUserTinPreviewIfNeeded();
    void DestroyUserTinPreview();
    bool RebuildUserTinPreview(FString& StatusMessage);
    void LoadControlCodeDefinitions();
    const FCSTopoControlCodeDefinition* FindControlCodeDefinition(const FString& ControlCode) const;
    bool ValidateControlParameter(const FString& ControlCode, const FString& Parameter, FString& StatusMessage) const;
    FString BuildNextMeasurementCode() const;
    void ApplyControlBeforeShot(const FString& BaseCode, const FString& ControlCode);
    void ApplyControlAfterShot(const FCSTopoShotRecord& Shot);
    void RebuildFigureSegments();
    bool StartRuntimeWindowUpdate(FCSTopoPointCloudSource& Source, const FVector& CameraSourcePoint, FString& ErrorMessage);
    void SetLidarPointBudget(bool bUseStreamingBudget) const;
    void RefreshPointCloudSourceStates();
    void RebuildLoadedPointCloudActors();
    void SyncActivePointCloudFlags();
    void RefreshPointCloudViewMode(FCSTopoPointCloudSource& Source);
    FString NormalizeCode(const FString& Code) const;
};
