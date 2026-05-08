#pragma once

#include "CoreMinimal.h"
#include "CSTopoTypes.generated.h"

UENUM(BlueprintType)
enum class ECSTopoNavigationMode : uint8
{
    Walk UMETA(DisplayName = "Walk"),
    Fly UMETA(DisplayName = "Fly")
};

UENUM(BlueprintType)
enum class ECSTopoPointCloudCacheFormat : uint8
{
    DirectLasLaz UMETA(DisplayName = "Direct LAS/LAZ"),
    COPC UMETA(DisplayName = "COPC"),
    EPT UMETA(DisplayName = "EPT")
};

UENUM(BlueprintType)
enum class ECSTopoPointCloudCacheState : uint8
{
    None UMETA(DisplayName = "None"),
    Pending UMETA(DisplayName = "Pending"),
    Building UMETA(DisplayName = "Building"),
    Ready UMETA(DisplayName = "Ready"),
    Missing UMETA(DisplayName = "Missing"),
    Failed UMETA(DisplayName = "Failed"),
    Stale UMETA(DisplayName = "Stale")
};

UENUM(BlueprintType)
enum class ECSTopoSurfaceBuildState : uint8
{
    None UMETA(DisplayName = "None"),
    Pending UMETA(DisplayName = "Pending"),
    Building UMETA(DisplayName = "Building"),
    Ready UMETA(DisplayName = "Ready"),
    Missing UMETA(DisplayName = "Missing"),
    Failed UMETA(DisplayName = "Failed"),
    Stale UMETA(DisplayName = "Stale")
};

UENUM(BlueprintType)
enum class ECSTopoMeasurementSource : uint8
{
    DerivedSurface UMETA(DisplayName = "Derived Surface"),
    RawPoint UMETA(DisplayName = "Raw Point"),
    InterpolatedPoints UMETA(DisplayName = "Interpolated Nearby Points"),
    SurfaceFallback UMETA(DisplayName = "Surface Fallback"),
    StoredPointSnap UMETA(DisplayName = "Stored Point Snap"),
    LineVertexSnap UMETA(DisplayName = "Line Vertex Snap")
};

UENUM(BlueprintType)
enum class ECSTopoViewMode : uint8
{
    SurfaceShaded UMETA(DisplayName = "Surface Shaded"),
    RGB UMETA(DisplayName = "RGB"),
    Elevation UMETA(DisplayName = "Elevation"),
    Classification UMETA(DisplayName = "Classification"),
    Intensity UMETA(DisplayName = "Intensity")
};

UENUM(BlueprintType)
enum class ECSTopoWorkflowState : uint8
{
    Home UMETA(DisplayName = "Home"),
    OpeningProject UMETA(DisplayName = "Opening Project"),
    ImportingPointCloud UMETA(DisplayName = "Importing Point Cloud"),
    BuildingCache UMETA(DisplayName = "Building Cache"),
    BuildingSurface UMETA(DisplayName = "Building Surface"),
    SurveyReady UMETA(DisplayName = "Survey Ready"),
    Failed UMETA(DisplayName = "Failed")
};

UENUM(BlueprintType)
enum class ECSTopoControlParameterKind : uint8
{
    None UMETA(DisplayName = "None"),
    Distance UMETA(DisplayName = "Distance"),
    OptionalDistance UMETA(DisplayName = "Optional Distance"),
    PointNumber UMETA(DisplayName = "Point Number"),
    DistanceOrPointNumber UMETA(DisplayName = "Distance Or Point Number")
};

UENUM(BlueprintType)
enum class ECSTopoControlGeometryKind : uint8
{
    None UMETA(DisplayName = "None"),
    StartLine UMETA(DisplayName = "Start Line"),
    EndLine UMETA(DisplayName = "End Line"),
    CloseLine UMETA(DisplayName = "Close Line"),
    IgnoreLinework UMETA(DisplayName = "Ignore Linework"),
    JoinToPoint UMETA(DisplayName = "Join To Point"),
    HorizontalOffset UMETA(DisplayName = "Horizontal Offset"),
    VerticalOffset UMETA(DisplayName = "Vertical Offset"),
    StartTangentArc UMETA(DisplayName = "Start Tangent Arc"),
    EndTangentArc UMETA(DisplayName = "End Tangent Arc"),
    StartNonTangentArc UMETA(DisplayName = "Start Non-Tangent Arc"),
    EndNonTangentArc UMETA(DisplayName = "End Non-Tangent Arc"),
    StartSmoothCurve UMETA(DisplayName = "Start Smooth Curve"),
    EndSmoothCurve UMETA(DisplayName = "End Smooth Curve"),
    Rectangle UMETA(DisplayName = "Rectangle"),
    CircleEdge UMETA(DisplayName = "Circle Edge"),
    CircleCenter UMETA(DisplayName = "Circle Center")
};

UENUM(BlueprintType)
enum class ECSTopoFigureSegmentKind : uint8
{
    Line UMETA(DisplayName = "Line"),
    Arc UMETA(DisplayName = "Arc"),
    SmoothCurve UMETA(DisplayName = "Smooth Curve"),
    Rectangle UMETA(DisplayName = "Rectangle"),
    Circle UMETA(DisplayName = "Circle"),
    OffsetLine UMETA(DisplayName = "Offset Line"),
    JoinLine UMETA(DisplayName = "Join Line")
};

inline FString NormalizeCSTopoPointType(const FString& PointType)
{
    FString Normalized = PointType.TrimStartAndEnd();
    Normalized.ReplaceInline(TEXT("_"), TEXT(" "));
    Normalized.ToUpperInline();
    return Normalized;
}

inline bool IsCSTopoNoTriangulationPointType(const FString& PointType)
{
    const FString Normalized = NormalizeCSTopoPointType(PointType);
    return Normalized.Contains(TEXT("NO TRIANGULATION"));
}

inline bool DoesCSTopoPointTypeCreateFigureLinework(const FString& PointType)
{
    const FString Normalized = NormalizeCSTopoPointType(PointType);
    return Normalized == TEXT("BREAKLINE")
        || Normalized == TEXT("LINE")
        || Normalized == TEXT("LINE FEATURE");
}

inline bool DoesCSTopoPointTypeCreateTinBreakline(const FString& PointType)
{
    return NormalizeCSTopoPointType(PointType) == TEXT("BREAKLINE");
}

inline bool DoesCSTopoPointTypeContributeToUserTin(const FString& PointType)
{
    const FString Normalized = NormalizeCSTopoPointType(PointType);
    if (Normalized.Contains(TEXT("NO TRIANGULATION")))
    {
        return false;
    }
    return Normalized.IsEmpty()
        || Normalized == TEXT("POINT")
        || Normalized == TEXT("BREAKLINE");
}

USTRUCT(BlueprintType)
struct FCSTopoSurfaceTileRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TileId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 VertexCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString MeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Status = TEXT("Pending");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 InputPointCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 UsedPointCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString DecimationMethod = TEXT("none");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 BoundaryEdgeCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 BoundaryLoopCount = 0;
};

USTRUCT(BlueprintType)
struct FCSTopoDerivedSurfaceManifest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SourceCloudId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SchemaVersion = TEXT("5.0");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoSurfaceBuildState BuildState = ECSTopoSurfaceBuildState::Pending;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString BuildMethod = TEXT("GlobalDelaunayTIN");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString GroundSource = TEXT("existingClassification");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TileSize = 250.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TileCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SourceCrs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString LinearUnitName = TEXT("unknown");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double LinearUnitToMeters = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FDateTime GeneratedAt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ManifestPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoSurfaceTileRecord> Tiles;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TotalVertexCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TotalTriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bDecimationUsed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TinBoundaryMode = TEXT("SupportBoundary");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinResolvedMaxEdgeLengthSourceUnits = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinBoundaryEdgeMultiplier = 8.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinBoundaryAreaMultiplier = 0.75;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TinRejectedLargeTriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TinRetainedCandidateTriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TinBoundaryEdgeCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TinBoundaryLoopCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TinConstrainedTriangulationStatus = TEXT("NotRun");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 GlobalPointCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 GlobalRetainedTriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 GlobalRejectedTriangleCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TopologyAuditStatus = TEXT("NotRun");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TopologyAuditMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceCollisionMode = TEXT("RuntimeVisibleStitchedTIN");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SeamAuditStatus = TEXT("NotRun");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SeamAuditMessage;
};

USTRUCT(BlueprintType)
struct FCSTopoSurfaceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSurfaceBuildOnImport = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSurfacePrimary = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSurfaceRenderVisible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bCloudOverlayVisible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoViewMode DefaultViewMode = ECSTopoViewMode::SurfaceShaded;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceBuildMethod = TEXT("GlobalDelaunayTIN");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString GroundClassificationMethod = TEXT("SMRF");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TileSizeSourceUnits = 250.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TilePaddingSourceUnits = 25.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double VisibleTileRadiusSourceUnits = 650.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 MaxSurfaceFillCells = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double MaxSurfaceTriangleZDeltaSourceUnits = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TinMaxBufferedPointsPerTile = 150000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinMaxEdgeLengthSourceUnits = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TinBoundaryMode = TEXT("SupportBoundary");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinBoundaryEdgeMultiplier = 8.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double TinBoundaryAreaMultiplier = 0.75;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bTinConstrainToBoundary = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString TinDecimationMode = TEXT("PreserveExtremesAdaptive");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 GlobalTinMaxPoints = 5000000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceCollisionMode = TEXT("RuntimeVisibleStitchedTIN");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double SurfaceCollisionRadiusSourceUnits = 200.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double SurfaceCollisionRefreshDistanceSourceUnits = 75.0;
};

USTRUCT(BlueprintType)
struct FCSTopoAlignmentReport
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bAligned = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Message;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector SourceBoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector SourceBoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector SurfaceBoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector SurfaceBoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector CloudRenderBoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector CloudRenderBoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double RenderUnitsPerSourceUnit = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double MaxSourceBoundsDeviation = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double MaxRenderBoundsDeviation = 0.0;
};

USTRUCT(BlueprintType)
struct FCSTopoPointCloudSource
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SourceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SourcePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString CachePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoPointCloudCacheFormat CacheFormat = ECSTopoPointCloudCacheFormat::COPC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int64 PointCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector BoundsMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString CoordinateSystemWkt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString LinearUnitName = TEXT("unknown");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double LinearUnitToMeters = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int64 DirectOpenPointThreshold = 5000000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bDirectOpenEligible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSourceExists = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bCacheExists = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bCachePreferredForRuntime = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bCacheOutOfDate = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString RuntimeDisplayPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString CacheStatus = TEXT("No cache state available.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoPointCloudCacheState CacheState = ECSTopoPointCloudCacheState::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FDateTime SourceModifiedAt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FDateTime CacheModifiedAt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bRuntimeWindowActive = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bRuntimeWindowPending = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString RuntimeWindowPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString RuntimeWindowStatus = TEXT("Runtime window inactive.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector RuntimeWindowCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double RuntimeWindowRadius = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double RuntimeWindowSampleSpacing = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bLoaded = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bVisible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bIsActive = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceManifestPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceStatus = TEXT("Surface not built.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoSurfaceBuildState SurfaceBuildState = ECSTopoSurfaceBuildState::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float SurfaceBuildProgress = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceBuildProgressStage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceBuildProgressMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSurfacePrimary = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bSurfaceRenderVisible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bCloudOverlayVisible = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoViewMode DefaultViewMode = ECSTopoViewMode::SurfaceShaded;
};

USTRUCT(BlueprintType)
struct FCSTopoPointCloudCacheManifestDocument
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SchemaVersion = TEXT("1.1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FDateTime GeneratedAt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ProjectPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoPointCloudSource> Entries;
};

USTRUCT(BlueprintType)
struct FCSTopoRuntimeStreamingSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bEnableCopcWindowStreaming = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double WindowRadiusSourceUnits = 350.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double WindowRefreshDistanceSourceUnits = 175.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double VerticalPaddingSourceUnits = 25.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 TargetPointBudget = 750000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    float UpdateIntervalSeconds = 0.75f;
};

USTRUCT(BlueprintType)
struct FCSTopoCodeStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Code;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Category;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString PointType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString LayerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bVisible = true;
};

USTRUCT(BlueprintType)
struct FCSTopoControlCodeDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Code;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Action;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoControlParameterKind ParameterKind = ECSTopoControlParameterKind::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoControlGeometryKind GeometryKind = ECSTopoControlGeometryKind::None;
};

USTRUCT(BlueprintType)
struct FCSTopoShotRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 PointNumber = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double Northing = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double Easting = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double Elevation = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Code;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString BaseCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ControlCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ControlParameter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FGuid FigureId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString FitType = TEXT("PlaneLeastSquares");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    double FitResidual = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SourceCloudId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FDateTime CreatedAt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FVector ViewLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString AuditJson;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoMeasurementSource MeasurementSource = ECSTopoMeasurementSource::InterpolatedPoints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceTileId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SurfaceMethod;
};

USTRUCT(BlueprintType)
struct FCSTopoFigureRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FGuid FigureId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Code;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString LayerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<int32> PointNumbers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bClosed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    bool bLoopClosed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FCSTopoCodeStyle Style;
};

USTRUCT(BlueprintType)
struct FCSTopoFigureSegmentRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FGuid SegmentId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoFigureSegmentKind SegmentKind = ECSTopoFigureSegmentKind::Line;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString Code;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString LayerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ControlCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ControlParameter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 CreatedByPointNumber = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<int32> PointNumbers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FVector> SurveyPoints;
};

USTRUCT(BlueprintType)
struct FCSTopoProjectDocument
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString SchemaVersion = TEXT("1.4");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ProjectName = TEXT("Untitled CSTopo Project");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ActiveCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString ActivePointCloudId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    int32 NextPointNumber = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    ECSTopoNavigationMode NavigationMode = ECSTopoNavigationMode::Walk;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoPointCloudSource> PointClouds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FString CacheManifestPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FCSTopoRuntimeStreamingSettings RuntimeStreaming;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    FCSTopoSurfaceSettings SurfaceSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoDerivedSurfaceManifest> DerivedSurfaces;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoCodeStyle> CodePalette;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoShotRecord> Shots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoFigureRecord> Figures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo")
    TArray<FCSTopoFigureSegmentRecord> FigureSegments;
};
