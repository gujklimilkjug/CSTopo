#include "CSTopoSurveySubsystem.h"

#include "CSTopoExporter.h"
#include "CSTopoPointCloudImport.h"
#include "CSTopoProjectLibrary.h"
#include "CompGeom/Delaunay2.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudActor.h"
#include "LidarPointCloudComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr ECollisionChannel CSTopoSurfaceCollisionChannel = ECC_GameTraceChannel1;

const TCHAR* CurrentSurfaceManifestSchemaVersion = TEXT("5.0");

int32 ReadInt32LE(const uint8* Bytes)
{
    return static_cast<int32>(
        static_cast<uint32>(Bytes[0])
        | (static_cast<uint32>(Bytes[1]) << 8)
        | (static_cast<uint32>(Bytes[2]) << 16)
        | (static_cast<uint32>(Bytes[3]) << 24));
}

double ClampPositive(double Value, double Fallback)
{
    return Value > 0.0 ? Value : Fallback;
}

double GetRenderUnitsPerSourceUnit(const FCSTopoPointCloudSource& Source)
{
    const double SourceUnitToMeters = ClampPositive(Source.LinearUnitToMeters, 0.0);
    return SourceUnitToMeters > 0.0 ? SourceUnitToMeters * 100.0 : 1.0;
}

FVector GetSourceCenter(const FCSTopoPointCloudSource& Source)
{
    return (Source.BoundsMin + Source.BoundsMax) * 0.5;
}

FVector SourceToRenderPoint(const FCSTopoPointCloudSource& Source, const FVector& SourcePoint)
{
    return (SourcePoint - GetSourceCenter(Source)) * static_cast<float>(GetRenderUnitsPerSourceUnit(Source));
}

FVector RenderToSourcePoint(const FCSTopoPointCloudSource& Source, const FVector& RenderPoint)
{
    return (RenderPoint / static_cast<float>(GetRenderUnitsPerSourceUnit(Source))) + GetSourceCenter(Source);
}

double RenderDistanceToSourceDistance(const FCSTopoPointCloudSource& Source, double RenderDistance)
{
    return RenderDistance / GetRenderUnitsPerSourceUnit(Source);
}

double SourceWindowDistance2D(const FVector& A, const FVector& B)
{
    return FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y));
}

double MaxAbsVectorDelta(const FVector& A, const FVector& B)
{
    return FMath::Max3(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z));
}

bool IsCSTopoOwnedActor(const AActor* Actor)
{
    if (Actor == nullptr)
    {
        return false;
    }

    const FString ActorName = Actor->GetName();
#if WITH_EDITOR
    const FString ActorLabel = Actor->GetActorLabel();
#else
    const FString ActorLabel;
#endif
    return ActorName.Contains(TEXT("CSTopo"))
        || ActorLabel.Contains(TEXT("CSTopo"))
        || Actor->IsA<ALidarPointCloudActor>()
        || Actor->IsA<APawn>();
}

bool IsSurveyEnvironmentActor(const AActor* Actor)
{
    if (Actor == nullptr || IsCSTopoOwnedActor(Actor))
    {
        return false;
    }

#if WITH_EDITOR
    const FString ActorLabel = Actor->GetActorLabel();
#else
    const FString ActorLabel;
#endif
    const FString NameAndClass = FString::Printf(TEXT("%s %s %s"), *Actor->GetName(), *ActorLabel, *Actor->GetClass()->GetName());
    if (NameAndClass.Contains(TEXT("Sky")) || NameAndClass.Contains(TEXT("Light")) || NameAndClass.Contains(TEXT("Camera")))
    {
        return false;
    }

    if (NameAndClass.Contains(TEXT("Floor"))
        || NameAndClass.Contains(TEXT("Plane"))
        || NameAndClass.Contains(TEXT("Landscape"))
        || NameAndClass.Contains(TEXT("Terrain"))
        || NameAndClass.Contains(TEXT("Template")))
    {
        return true;
    }

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Actor->GetComponents(PrimitiveComponents);
    for (const UPrimitiveComponent* Component : PrimitiveComponents)
    {
        if (Component != nullptr && Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        {
            return true;
        }
    }

    return false;
}

bool ShouldRenderAllSurfaceTiles(const FCSTopoLoadedSurface& Surface)
{
    return Surface.Tiles.Num() <= 128;
}

bool IsSurfaceManifestSchemaCurrent(const FString& ManifestPath)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    FString SchemaVersion;
    return Root->TryGetStringField(TEXT("schema_version"), SchemaVersion)
        && SchemaVersion == CurrentSurfaceManifestSchemaVersion;
}

FString NormalizeCSTopoDialogPath(const FString& FilePath)
{
    FString Normalized = FilePath;
    FPaths::NormalizeFilename(Normalized);
    int32 LastDriveRootIndex = INDEX_NONE;
    for (int32 Index = 0; Index + 2 < Normalized.Len(); ++Index)
    {
        if (FChar::IsAlpha(Normalized[Index])
            && Normalized[Index + 1] == TEXT(':')
            && (Normalized[Index + 2] == TEXT('/') || Normalized[Index + 2] == TEXT('\\')))
        {
            LastDriveRootIndex = Index;
        }
    }
    if (LastDriveRootIndex > 0)
    {
        Normalized = Normalized.Mid(LastDriveRootIndex);
        FPaths::NormalizeFilename(Normalized);
    }

    const bool bDriveRooted = Normalized.Len() >= 3
        && FChar::IsAlpha(Normalized[0])
        && Normalized[1] == TEXT(':')
        && (Normalized[2] == TEXT('/') || Normalized[2] == TEXT('\\'));
    if (!bDriveRooted && FPaths::IsRelative(Normalized))
    {
        Normalized = FPaths::ConvertRelativePathToFull(Normalized);
        FPaths::NormalizeFilename(Normalized);
    }
    return Normalized;
}

bool SweepPointCloudForVisibleHit(
    ULidarPointCloudComponent* Component,
    const FVector& ViewOrigin,
    const FVector& ViewDirection,
    float InitialSearchRadius,
    float MaxDistance,
    FVector& BestHit,
    float& ResolvedRadius)
{
    if (Component == nullptr)
    {
        return false;
    }

    const FVector Direction = ViewDirection.GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        return false;
    }

    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
    {
        const float SearchRadius = FMath::Max(InitialSearchRadius * FMath::Pow(2.0f, static_cast<float>(Attempt)), 25.0f);
        const float StepDistance = FMath::Max(SearchRadius * 1.5f, 100.0f);
        const double MaxPerpDistanceSq = FMath::Square(static_cast<double>(SearchRadius));

        bool bFound = false;
        double BestPerpDistanceSq = TNumericLimits<double>::Max();
        double BestAlongDistance = TNumericLimits<double>::Max();

        for (float Distance = 0.0f; Distance <= MaxDistance; Distance += StepDistance)
        {
            const FVector SampleCenter = ViewOrigin + Direction * Distance;
            TArray<FLidarPointCloudPoint> Candidates;
            Component->GetPointsInBoxAsCopies(
                Candidates,
                FBox(SampleCenter - FVector(SearchRadius), SampleCenter + FVector(SearchRadius)),
                true,
                true);

            for (const FLidarPointCloudPoint& CandidatePoint : Candidates)
            {
                const FVector Candidate(CandidatePoint.Location);
                const double AlongDistance = FVector::DotProduct(Candidate - ViewOrigin, Direction);
                if (AlongDistance < 0.0 || AlongDistance > MaxDistance)
                {
                    continue;
                }

                const FVector ClosestPointOnRay = ViewOrigin + Direction * static_cast<float>(AlongDistance);
                const double PerpDistanceSq = FVector::DistSquared(Candidate, ClosestPointOnRay);
                if (PerpDistanceSq > MaxPerpDistanceSq)
                {
                    continue;
                }

                if (!bFound || PerpDistanceSq < BestPerpDistanceSq || (FMath::IsNearlyEqual(PerpDistanceSq, BestPerpDistanceSq) && AlongDistance < BestAlongDistance))
                {
                    bFound = true;
                    BestPerpDistanceSq = PerpDistanceSq;
                    BestAlongDistance = AlongDistance;
                    BestHit = Candidate;
                    ResolvedRadius = SearchRadius;
                }
            }
        }

        if (bFound)
        {
            return true;
        }
    }

    return false;
}

FString ViewModeLabel(ECSTopoViewMode Mode)
{
    switch (Mode)
    {
    case ECSTopoViewMode::RGB:
        return TEXT("RGB");
    case ECSTopoViewMode::Elevation:
        return TEXT("Elevation");
    case ECSTopoViewMode::Classification:
        return TEXT("Classification");
    case ECSTopoViewMode::Intensity:
        return TEXT("Intensity");
    case ECSTopoViewMode::SurfaceShaded:
    default:
        return TEXT("Surface shaded");
    }
}

FString SurfaceStateLabel(ECSTopoSurfaceBuildState State)
{
    switch (State)
    {
    case ECSTopoSurfaceBuildState::Pending:
        return TEXT("Pending");
    case ECSTopoSurfaceBuildState::Building:
        return TEXT("Building");
    case ECSTopoSurfaceBuildState::Ready:
        return TEXT("Ready");
    case ECSTopoSurfaceBuildState::Missing:
        return TEXT("Missing");
    case ECSTopoSurfaceBuildState::Failed:
        return TEXT("Failed");
    case ECSTopoSurfaceBuildState::Stale:
        return TEXT("Stale");
    case ECSTopoSurfaceBuildState::None:
    default:
        return TEXT("None");
    }
}

FString MeasurementSourceLabel(ECSTopoMeasurementSource Source)
{
    switch (Source)
    {
    case ECSTopoMeasurementSource::DerivedSurface:
        return TEXT("DerivedSurface");
    case ECSTopoMeasurementSource::RawPoint:
        return TEXT("RawPoint");
    case ECSTopoMeasurementSource::SurfaceFallback:
        return TEXT("SurfaceFallback");
    case ECSTopoMeasurementSource::InterpolatedPoints:
    default:
        return TEXT("InterpolatedPoints");
    }
}

FString DelaunayResultLabel(UE::Geometry::FDelaunay2::EResult Result)
{
    switch (Result)
    {
    case UE::Geometry::FDelaunay2::EResult::Success:
        return TEXT("Success");
    case UE::Geometry::FDelaunay2::EResult::NotComputed:
        return TEXT("NotComputed");
    case UE::Geometry::FDelaunay2::EResult::EmptyInput:
        return TEXT("EmptyInput");
    case UE::Geometry::FDelaunay2::EResult::Collinear:
        return TEXT("Collinear");
    case UE::Geometry::FDelaunay2::EResult::MissingEdges:
        return TEXT("MissingEdges");
    case UE::Geometry::FDelaunay2::EResult::Unknown:
    default:
        return TEXT("Unknown");
    }
}

ECSTopoSurfaceBuildState SurfaceStateFromString(const FString& Value)
{
    if (Value.Equals(TEXT("Pending"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Pending;
    }
    if (Value.Equals(TEXT("Building"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Building;
    }
    if (Value.Equals(TEXT("Ready"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Ready;
    }
    if (Value.Equals(TEXT("Missing"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Missing;
    }
    if (Value.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Failed;
    }
    if (Value.Equals(TEXT("Stale"), ESearchCase::IgnoreCase))
    {
        return ECSTopoSurfaceBuildState::Stale;
    }
    return ECSTopoSurfaceBuildState::None;
}

ECSTopoControlParameterKind ControlParameterKindFromString(const FString& Value)
{
    if (Value.Equals(TEXT("Distance"), ESearchCase::IgnoreCase))
    {
        return ECSTopoControlParameterKind::Distance;
    }
    if (Value.Equals(TEXT("OptionalDistance"), ESearchCase::IgnoreCase))
    {
        return ECSTopoControlParameterKind::OptionalDistance;
    }
    if (Value.Equals(TEXT("PointNumber"), ESearchCase::IgnoreCase))
    {
        return ECSTopoControlParameterKind::PointNumber;
    }
    if (Value.Equals(TEXT("DistanceOrPointNumber"), ESearchCase::IgnoreCase))
    {
        return ECSTopoControlParameterKind::DistanceOrPointNumber;
    }
    return ECSTopoControlParameterKind::None;
}

ECSTopoControlGeometryKind ControlGeometryKindFromString(const FString& Value)
{
    if (Value.Equals(TEXT("StartLine"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::StartLine;
    if (Value.Equals(TEXT("EndLine"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::EndLine;
    if (Value.Equals(TEXT("CloseLine"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::CloseLine;
    if (Value.Equals(TEXT("IgnoreLinework"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::IgnoreLinework;
    if (Value.Equals(TEXT("JoinToPoint"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::JoinToPoint;
    if (Value.Equals(TEXT("HorizontalOffset"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::HorizontalOffset;
    if (Value.Equals(TEXT("VerticalOffset"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::VerticalOffset;
    if (Value.Equals(TEXT("StartTangentArc"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::StartTangentArc;
    if (Value.Equals(TEXT("EndTangentArc"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::EndTangentArc;
    if (Value.Equals(TEXT("StartNonTangentArc"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::StartNonTangentArc;
    if (Value.Equals(TEXT("EndNonTangentArc"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::EndNonTangentArc;
    if (Value.Equals(TEXT("StartSmoothCurve"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::StartSmoothCurve;
    if (Value.Equals(TEXT("EndSmoothCurve"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::EndSmoothCurve;
    if (Value.Equals(TEXT("Rectangle"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::Rectangle;
    if (Value.Equals(TEXT("CircleEdge"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::CircleEdge;
    if (Value.Equals(TEXT("CircleCenter"), ESearchCase::IgnoreCase)) return ECSTopoControlGeometryKind::CircleCenter;
    return ECSTopoControlGeometryKind::None;
}

FString NormalizeControlCode(const FString& Code)
{
    FString Normalized = Code.TrimStartAndEnd();
    Normalized.ToUpperInline();
    return Normalized;
}

int32 ParsePointNumberParameter(const FString& Parameter)
{
    FString Text = Parameter.TrimStartAndEnd().ToUpper();
    Text.RemoveFromStart(TEXT("P"));
    return Text.IsNumeric() ? FCString::Atoi(*Text) : INDEX_NONE;
}

bool ParseNumericParameter(const FString& Parameter, double& OutValue)
{
    const FString Text = Parameter.TrimStartAndEnd();
    return !Text.IsEmpty() && LexTryParseString(OutValue, *Text);
}

FVector SurveyPointFromShot(const FCSTopoShotRecord& Shot)
{
    return FVector(Shot.Northing, Shot.Easting, Shot.Elevation);
}

FString BuildLinePairKey(const FString& Code, int32 FirstPointNumber, int32 SecondPointNumber)
{
    return FString::Printf(TEXT("%s:%d:%d"), *NormalizeControlCode(Code), FirstPointNumber, SecondPointNumber);
}

void SuppressLinePair(TSet<FString>& SuppressedPairs, const FString& Code, const FCSTopoShotRecord& A, const FCSTopoShotRecord& B)
{
    SuppressedPairs.Add(BuildLinePairKey(Code, A.PointNumber, B.PointNumber));
}

void SuppressLineRange(TSet<FString>& SuppressedPairs, const FString& Code, const TArray<FCSTopoShotRecord>& BaseShots, int32 FirstIndex, int32 LastIndex)
{
    const int32 ClampedFirst = FMath::Max(0, FirstIndex);
    const int32 ClampedLast = FMath::Min(LastIndex, BaseShots.Num() - 1);
    for (int32 Index = ClampedFirst; Index < ClampedLast; ++Index)
    {
        SuppressLinePair(SuppressedPairs, Code, BaseShots[Index], BaseShots[Index + 1]);
    }
}

double NormalizeAngleDelta(double Delta, int32 Direction)
{
    if (Direction >= 0)
    {
        while (Delta < 0.0)
        {
            Delta += 2.0 * UE_DOUBLE_PI;
        }
    }
    else
    {
        while (Delta > 0.0)
        {
            Delta -= 2.0 * UE_DOUBLE_PI;
        }
    }
    return Delta;
}

bool CircleCenterFromThreeShots(const FCSTopoShotRecord& A, const FCSTopoShotRecord& B, const FCSTopoShotRecord& C, double& OutCenterE, double& OutCenterN, double& OutRadius)
{
    const double AX = A.Easting;
    const double AY = A.Northing;
    const double BX = B.Easting;
    const double BY = B.Northing;
    const double CX = C.Easting;
    const double CY = C.Northing;
    const double D = 2.0 * (AX * (BY - CY) + BX * (CY - AY) + CX * (AY - BY));
    if (FMath::IsNearlyZero(D))
    {
        return false;
    }

    OutCenterE = ((AX * AX + AY * AY) * (BY - CY) + (BX * BX + BY * BY) * (CY - AY) + (CX * CX + CY * CY) * (AY - BY)) / D;
    OutCenterN = ((AX * AX + AY * AY) * (CX - BX) + (BX * BX + BY * BY) * (AX - CX) + (CX * CX + CY * CY) * (BX - AX)) / D;
    OutRadius = FVector2D::Distance(FVector2D(AX, AY), FVector2D(OutCenterE, OutCenterN));
    return OutRadius > UE_DOUBLE_SMALL_NUMBER;
}

TArray<FVector> SampleArcPoints(double CenterE, double CenterN, double Radius, const FCSTopoShotRecord& Start, const FCSTopoShotRecord& End, int32 Direction, int32 Count = 49)
{
    TArray<FVector> Points;
    if (Radius <= UE_DOUBLE_SMALL_NUMBER || Count < 2)
    {
        return Points;
    }

    const double StartAngle = FMath::Atan2(Start.Northing - CenterN, Start.Easting - CenterE);
    const double EndAngle = FMath::Atan2(End.Northing - CenterN, End.Easting - CenterE);
    const double Delta = NormalizeAngleDelta(EndAngle - StartAngle, Direction);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const double Alpha = static_cast<double>(Index) / static_cast<double>(Count - 1);
        const double Angle = StartAngle + Delta * Alpha;
        const double Elevation = FMath::Lerp(Start.Elevation, End.Elevation, Alpha);
        Points.Add(FVector(CenterN + FMath::Sin(Angle) * Radius, CenterE + FMath::Cos(Angle) * Radius, Elevation));
    }
    Points[0] = SurveyPointFromShot(Start);
    Points.Last() = SurveyPointFromShot(End);
    return Points;
}

bool BuildArcFromThreeShots(const FCSTopoShotRecord& Start, const FCSTopoShotRecord& Middle, const FCSTopoShotRecord& End, TArray<FVector>& OutPoints)
{
    double CenterE = 0.0;
    double CenterN = 0.0;
    double Radius = 0.0;
    if (!CircleCenterFromThreeShots(Start, Middle, End, CenterE, CenterN, Radius))
    {
        return false;
    }

    const double StartAngle = FMath::Atan2(Start.Northing - CenterN, Start.Easting - CenterE);
    const double MiddleAngle = FMath::Atan2(Middle.Northing - CenterN, Middle.Easting - CenterE);
    const double EndAngle = FMath::Atan2(End.Northing - CenterN, End.Easting - CenterE);
    const double CcwEnd = NormalizeAngleDelta(EndAngle - StartAngle, 1);
    const double CcwMiddle = NormalizeAngleDelta(MiddleAngle - StartAngle, 1);
    const int32 Direction = (CcwMiddle >= 0.0 && CcwMiddle <= CcwEnd) ? 1 : -1;
    OutPoints = SampleArcPoints(CenterE, CenterN, Radius, Start, End, Direction);
    return OutPoints.Num() >= 2;
}

bool BuildTangentArcPoints(const FCSTopoShotRecord& Previous, const FCSTopoShotRecord& Pc, const FCSTopoShotRecord& Pt, TArray<FVector>& OutPoints)
{
    FVector2D Tangent(Pc.Easting - Previous.Easting, Pc.Northing - Previous.Northing);
    const double TangentLength = Tangent.Size();
    if (TangentLength <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }
    Tangent /= TangentLength;

    const FVector2D Normal(-Tangent.Y, Tangent.X);
    const FVector2D Chord(Pt.Easting - Pc.Easting, Pt.Northing - Pc.Northing);
    const double Denominator = 2.0 * FVector2D::DotProduct(Normal, Chord);
    if (FMath::IsNearlyZero(Denominator))
    {
        return false;
    }

    const double Scale = Chord.SizeSquared() / Denominator;
    const double CenterE = Pc.Easting + Normal.X * Scale;
    const double CenterN = Pc.Northing + Normal.Y * Scale;
    const double Radius = FMath::Abs(Scale);
    const double StartAngle = FMath::Atan2(Pc.Northing - CenterN, Pc.Easting - CenterE);
    const FVector2D CcwTangent(-FMath::Sin(StartAngle), FMath::Cos(StartAngle));
    const int32 Direction = FVector2D::DotProduct(CcwTangent, Tangent) >= 0.0 ? 1 : -1;
    OutPoints = SampleArcPoints(CenterE, CenterN, Radius, Pc, Pt, Direction);
    return OutPoints.Num() >= 2;
}

TArray<FVector> BuildCatmullRomPoints(const TArray<FCSTopoShotRecord>& Shots, int32 FirstIndex, int32 LastIndex, int32 SamplesPerSpan = 8)
{
    TArray<FVector> SourcePoints;
    for (int32 Index = FirstIndex; Index <= LastIndex && Shots.IsValidIndex(Index); ++Index)
    {
        SourcePoints.Add(SurveyPointFromShot(Shots[Index]));
    }
    if (SourcePoints.Num() < 2)
    {
        return {};
    }
    if (SourcePoints.Num() == 2)
    {
        return SourcePoints;
    }

    TArray<FVector> Points;
    for (int32 Index = 0; Index + 1 < SourcePoints.Num(); ++Index)
    {
        const FVector P0 = SourcePoints[FMath::Max(0, Index - 1)];
        const FVector P1 = SourcePoints[Index];
        const FVector P2 = SourcePoints[Index + 1];
        const FVector P3 = SourcePoints[FMath::Min(SourcePoints.Num() - 1, Index + 2)];
        for (int32 Sample = 0; Sample < SamplesPerSpan; ++Sample)
        {
            if (Index > 0 && Sample == 0)
            {
                continue;
            }
            const double T = static_cast<double>(Sample) / static_cast<double>(SamplesPerSpan);
            const double T2 = T * T;
            const double T3 = T2 * T;
            Points.Add(0.5 * ((2.0 * P1) + (-P0 + P2) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * T3));
        }
    }
    Points.Add(SourcePoints.Last());
    return Points;
}

TArray<FVector> BuildRectanglePoints(const FCSTopoShotRecord& Corner, const FCSTopoShotRecord& LengthShot, const FCSTopoShotRecord& WidthShot)
{
    const FVector2D LengthVector(LengthShot.Northing - Corner.Northing, LengthShot.Easting - Corner.Easting);
    const double Length = LengthVector.Size();
    if (Length <= UE_DOUBLE_SMALL_NUMBER)
    {
        return {};
    }

    const FVector2D WidthVector(WidthShot.Northing - LengthShot.Northing, WidthShot.Easting - LengthShot.Easting);
    const double SignedWidth = FVector2D::DotProduct(FVector2D(-LengthVector.Y / Length, LengthVector.X / Length), WidthVector);
    const FVector2D Offset(-LengthVector.Y / Length * SignedWidth, LengthVector.X / Length * SignedWidth);
    return {
        SurveyPointFromShot(Corner),
        SurveyPointFromShot(LengthShot),
        FVector(LengthShot.Northing + Offset.X, LengthShot.Easting + Offset.Y, WidthShot.Elevation),
        FVector(Corner.Northing + Offset.X, Corner.Easting + Offset.Y, WidthShot.Elevation),
        SurveyPointFromShot(Corner)
    };
}
}

void UCSTopoSurveySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadControlCodeDefinitions();
    if (ActiveProject.CodePalette.IsEmpty())
    {
        NewProject(TEXT("Untitled CSTopo Project"));
    }
}

void UCSTopoSurveySubsystem::NewProject(const FString& ProjectName)
{
    for (TPair<FString, TObjectPtr<ALidarPointCloudActor>>& Pair : PointCloudActors)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->Destroy();
        }
    }
    PointCloudActors.Empty();
    PointCloudActorDisplayPaths.Empty();
    for (TPair<FString, TObjectPtr<AActor>>& Pair : SurfaceActors)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->Destroy();
        }
    }
    SurfaceActors.Empty();
    DestroyUserTinPreview();

    ActiveProject = UCSTopoProjectLibrary::CreateDefaultProject(ProjectName);
    CurrentProjectPath.Empty();
    RuntimeWindowUpdateCooldown = 0.0f;
    RuntimeWindowProcessSourceId.Empty();
    RuntimeWindowProcessPipelinePath.Empty();
    RuntimeWindowProcessOutputPath.Empty();
    if (RuntimeWindowProcessHandle.IsValid())
    {
        FPlatformProcess::CloseProc(RuntimeWindowProcessHandle);
        RuntimeWindowProcessHandle = FProcHandle();
    }
    SurfaceBuildProcessSourceId.Empty();
    SurfaceBuildManifestPath.Empty();
    SurfaceBuildProgressPath.Empty();
    if (SurfaceBuildProcessHandle.IsValid())
    {
        FPlatformProcess::CloseProc(SurfaceBuildProcessHandle);
        SurfaceBuildProcessHandle = FProcHandle();
    }
    LoadedSurfaces.Empty();
    LastAlignmentReport = FCSTopoAlignmentReport();
    ClearPendingControlCode();
    RebuildFigureSegments();
}

void UCSTopoSurveySubsystem::SetWorkflowState(ECSTopoWorkflowState NewState, const FString& StatusMessage)
{
    WorkflowState = NewState;
    WorkflowStatus = StatusMessage;
}

bool UCSTopoSurveySubsystem::IsSurveyReady() const
{
    return WorkflowState == ECSTopoWorkflowState::SurveyReady;
}

void UCSTopoSurveySubsystem::UpdateWorkflow()
{
    RefreshSurfaceBuildProcess();
    RefreshUserTinPreviewIfNeeded();
}

FString UCSTopoSurveySubsystem::GetCurrentProjectPath() const
{
    return CurrentProjectPath;
}

bool UCSTopoSurveySubsystem::LoadProject(const FString& FilePath, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoDialogPath(FilePath);

    const bool bLoaded = UCSTopoProjectLibrary::LoadProjectFromFile(NormalizedFilePath, ActiveProject, ErrorMessage);
    if (bLoaded)
    {
        DestroyUserTinPreview();
        CurrentProjectPath = NormalizedFilePath;
        RefreshPointCloudSourceStates();
        SyncActivePointCloudFlags();
        ClearPendingControlCode();
        RebuildFigureSegments();
        RebuildLoadedPointCloudActors();
    }
    return bLoaded;
}

bool UCSTopoSurveySubsystem::SaveProject(const FString& FilePath, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoDialogPath(FilePath);

    RefreshPointCloudSourceStates();
    if (ActiveProject.CacheManifestPath.IsEmpty())
    {
        ActiveProject.CacheManifestPath = UCSTopoPointCloudImport::BuildDefaultCacheManifestPath(NormalizedFilePath);
    }
    const bool bSaved = UCSTopoProjectLibrary::SaveProjectToFile(ActiveProject, NormalizedFilePath, ErrorMessage);
    if (bSaved)
    {
        CurrentProjectPath = NormalizedFilePath;
    }
    return bSaved;
}

FCSTopoShotRecord UCSTopoSurveySubsystem::AddFittedShotFromSamples(const FString& Code, double Northing, double Easting, const TArray<FVector>& Samples, const FString& SourceCloudId, bool& bSuccess, const FString& ControlParameter, bool bJoinLinework)
{
    bSuccess = false;
    FCSTopoShotRecord EmptyShot;

    const FCSTopoPlaneFitResult Fit = UCSTopoProjectLibrary::FitPlaneAtSurveyCoordinate(Samples, Northing, Easting);
    if (!Fit.bSuccess)
    {
        return EmptyShot;
    }

    const FString ShotCode = NormalizeCode(Code.IsEmpty() ? ActiveProject.ActiveCode : Code);
    TArray<FString> ShotTokens;
    ShotCode.ParseIntoArrayWS(ShotTokens);
    const FString BaseCode = ShotTokens.IsEmpty() ? ShotCode : ShotTokens[0];
    FCSTopoCodeStyle ShotStyle;
    if (!GetCodeStyle(BaseCode, ShotStyle))
    {
        return EmptyShot;
    }

    bSuccess = true;
    return UCSTopoProjectLibrary::AddFittedShot(ActiveProject, ShotCode, Northing, Easting, Fit.Elevation, Fit.Residual, SourceCloudId, ControlParameter, bJoinLinework);
}

bool UCSTopoSurveySubsystem::ExportCadDeliverables(const FString& CsvPath, const FString& DxfPath, FString& ErrorMessage) const
{
    FString CsvError;
    FString DxfError;
    const bool bCsvOk = UCSTopoExporter::ExportCsv(ActiveProject, CsvPath, CsvError);
    const bool bDxfOk = UCSTopoExporter::ExportDxf(ActiveProject, DxfPath, DxfError);

    if (!bCsvOk || !bDxfOk)
    {
        ErrorMessage = FString::Printf(TEXT("%s %s"), *CsvError, *DxfError).TrimStartAndEnd();
        return false;
    }

    ErrorMessage.Empty();
    return true;
}

void UCSTopoSurveySubsystem::SetActiveCode(const FString& Code)
{
    const FString NormalizedCode = NormalizeCode(Code);
    if (NormalizedCode.IsEmpty())
    {
        return;
    }

    if (!ActiveProject.CodePalette.ContainsByPredicate([&NormalizedCode](const FCSTopoCodeStyle& Style)
    {
        return Style.Code.Equals(NormalizedCode, ESearchCase::IgnoreCase);
    }))
    {
        return;
    }

    ActiveProject.ActiveCode = NormalizedCode;
}

bool UCSTopoSurveySubsystem::SplitActiveFigure(FString& StatusMessage)
{
    const FString ActiveCode = NormalizeCode(ActiveProject.ActiveCode);
    if (ActiveCode.IsEmpty())
    {
        StatusMessage = TEXT("Enter a code before starting a new line.");
        return false;
    }

    FCSTopoFigureRecord* OpenFigure = ActiveProject.Figures.FindByPredicate([&ActiveCode](const FCSTopoFigureRecord& Candidate)
    {
        return Candidate.Code.Equals(ActiveCode, ESearchCase::IgnoreCase) && !Candidate.bClosed;
    });

    if (OpenFigure == nullptr)
    {
        StatusMessage = FString::Printf(TEXT("No open figure for %s. The next shot will start a new line."), *ActiveCode);
        return false;
    }

    UCSTopoProjectLibrary::SplitFigure(ActiveProject, ActiveCode);
    StatusMessage = FString::Printf(TEXT("Started a new %s line after %d shot(s)."), *ActiveCode, OpenFigure->PointNumbers.Num());
    return true;
}

bool UCSTopoSurveySubsystem::CloseActiveFigure(FString& StatusMessage)
{
    const FString ActiveCode = NormalizeCode(ActiveProject.ActiveCode);
    if (ActiveCode.IsEmpty())
    {
        StatusMessage = TEXT("Enter a code before closing a line.");
        return false;
    }

    FCSTopoFigureRecord* OpenFigure = ActiveProject.Figures.FindByPredicate([&ActiveCode](const FCSTopoFigureRecord& Candidate)
    {
        return Candidate.Code.Equals(ActiveCode, ESearchCase::IgnoreCase) && !Candidate.bClosed;
    });

    if (OpenFigure == nullptr)
    {
        StatusMessage = FString::Printf(TEXT("No open figure for %s to close."), *ActiveCode);
        return false;
    }

    UCSTopoProjectLibrary::CloseActiveFigure(ActiveProject, ActiveCode);
    StatusMessage = FString::Printf(TEXT("Closed %s as a loop with %d shot(s)."), *ActiveCode, OpenFigure->PointNumbers.Num());
    return true;
}

bool UCSTopoSurveySubsystem::AddPointCloud(const FString& SourcePath, const FString& CacheDirectory, FCSTopoPointCloudSource& AddedSource, FString& ErrorMessage)
{
    FString ResolvedSourcePath = SourcePath;
    if (!FPaths::FileExists(ResolvedSourcePath) && FPaths::IsRelative(ResolvedSourcePath))
    {
        ResolvedSourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ResolvedSourcePath));
    }

    FCSTopoImportOptions Options;
    Options.SourcePath = ResolvedSourcePath;
    Options.CacheDirectory = CacheDirectory;
    Options.TargetCacheFormat = ECSTopoPointCloudCacheFormat::COPC;

    if (!UCSTopoPointCloudImport::CreateSourceRecord(Options, AddedSource, ErrorMessage))
    {
        return false;
    }

    AddedSource.bLoaded = true;
    AddedSource.bVisible = true;
    AddedSource.bIsActive = ActiveProject.ActivePointCloudId.IsEmpty();
    ActiveProject.PointClouds.Add(AddedSource);

    if (ActiveProject.ActivePointCloudId.IsEmpty())
    {
        ActiveProject.ActivePointCloudId = AddedSource.SourceId;
    }

    SyncActivePointCloudFlags();
    ErrorMessage = AddedSource.CacheStatus;
    return true;
}

bool UCSTopoSurveySubsystem::ImportPointCloud(const FString& SourcePath, FCSTopoPointCloudSource& ImportedSource, FString& ErrorMessage)
{
    const FString CacheDir = GetProjectCacheDirectory();
    if (!AddPointCloud(SourcePath, CacheDir, ImportedSource, ErrorMessage))
    {
        return false;
    }

    if (!ImportedSource.bDirectOpenEligible
        && ImportedSource.CacheFormat == ECSTopoPointCloudCacheFormat::COPC
        && !ImportedSource.bCacheExists)
    {
        FString CacheBuildMessage;
        if (!StartCopcCacheBuild(ImportedSource.SourceId, CacheBuildMessage))
        {
            ErrorMessage = FString::Printf(TEXT("Imported metadata for %s, but COPC cache build failed: %s"), *FPaths::GetCleanFilename(ImportedSource.SourcePath), *CacheBuildMessage);
            return false;
        }

        if (FCSTopoPointCloudSource* UpdatedSource = FindPointCloudMutable(ImportedSource.SourceId))
        {
            ImportedSource = *UpdatedSource;
        }
    }

    if (!SpawnPointCloudActor(ImportedSource, ErrorMessage))
    {
        return false;
    }

    if (ImportedSource.bSurfacePrimary)
    {
        if (FCSTopoPointCloudSource* UpdatedSource = FindPointCloudMutable(ImportedSource.SourceId))
        {
            UpdatedSource->bSurfacePrimary = ActiveProject.SurfaceSettings.bSurfacePrimary;
            UpdatedSource->bCloudOverlayVisible = ActiveProject.SurfaceSettings.bCloudOverlayVisible;
            UpdatedSource->DefaultViewMode = ActiveProject.SurfaceSettings.DefaultViewMode;
        }
        ImportedSource.bSurfacePrimary = ActiveProject.SurfaceSettings.bSurfacePrimary;
        ImportedSource.bCloudOverlayVisible = ActiveProject.SurfaceSettings.bCloudOverlayVisible;
        ImportedSource.DefaultViewMode = ActiveProject.SurfaceSettings.DefaultViewMode;
    }

    FString SurfaceMessage = TEXT("Surface build is disabled.");
    if (ActiveProject.SurfaceSettings.bSurfaceBuildOnImport)
    {
        StartSurfaceBuild(ImportedSource.SourceId, false, SurfaceMessage);
    }

    ErrorMessage = FString::Printf(TEXT("Imported and displayed %s. %s"), *FPaths::GetCleanFilename(ImportedSource.SourcePath), *SurfaceMessage);
    return true;
}

bool UCSTopoSurveySubsystem::RemovePointCloud(const FString& SourceId)
{
    if (TObjectPtr<ALidarPointCloudActor>* Actor = PointCloudActors.Find(SourceId))
    {
        if (Actor->Get() != nullptr)
        {
            Actor->Get()->Destroy();
        }
        PointCloudActors.Remove(SourceId);
    }
    PointCloudActorDisplayPaths.Remove(SourceId);
    if (TObjectPtr<AActor>* SurfaceActor = SurfaceActors.Find(SourceId))
    {
        if (SurfaceActor->Get() != nullptr)
        {
            SurfaceActor->Get()->Destroy();
        }
        SurfaceActors.Remove(SourceId);
    }
    LoadedSurfaces.Remove(SourceId);
    ActiveProject.DerivedSurfaces.RemoveAll([&SourceId](const FCSTopoDerivedSurfaceManifest& Candidate)
    {
        return Candidate.SourceCloudId == SourceId;
    });

    const int32 Removed = ActiveProject.PointClouds.RemoveAll([&SourceId](const FCSTopoPointCloudSource& Source)
    {
        return Source.SourceId == SourceId;
    });

    if (Removed == 0)
    {
        return false;
    }

    if (ActiveProject.ActivePointCloudId == SourceId)
    {
        ActiveProject.ActivePointCloudId = ActiveProject.PointClouds.IsEmpty() ? FString() : ActiveProject.PointClouds[0].SourceId;
    }

    SyncActivePointCloudFlags();
    return true;
}

bool UCSTopoSurveySubsystem::SetActivePointCloud(const FString& SourceId)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        return false;
    }

    ActiveProject.ActivePointCloudId = SourceId;
    SyncActivePointCloudFlags();
    FString SurfaceError;
    LoadDerivedSurfaceForSource(*Source, SurfaceError);
    RefreshSurfacePresentation(*Source);
    RefreshPointCloudViewMode(*Source);
    return true;
}

bool UCSTopoSurveySubsystem::SetPointCloudVisible(const FString& SourceId, bool bVisible)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        return false;
    }

    Source->bVisible = bVisible;
    RefreshSurfacePresentation(*Source);
    return true;
}

bool UCSTopoSurveySubsystem::SetPointCloudLoaded(const FString& SourceId, bool bLoaded)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        return false;
    }

    Source->bLoaded = bLoaded;
    if (!bLoaded)
    {
        Source->bVisible = false;
        if (TObjectPtr<ALidarPointCloudActor>* Actor = PointCloudActors.Find(SourceId))
        {
            if (Actor->Get() != nullptr)
            {
                Actor->Get()->Destroy();
            }
            PointCloudActors.Remove(SourceId);
        }
        PointCloudActorDisplayPaths.Remove(SourceId);
    }
    else
    {
        FString ErrorMessage;
        if (SpawnPointCloudActor(*Source, ErrorMessage))
        {
            LoadDerivedSurfaceForSource(*Source, ErrorMessage);
            RefreshSurfacePresentation(*Source);
            RefreshPointCloudViewMode(*Source);
        }
    }
    return true;
}

TArray<FCSTopoPointCloudSource> UCSTopoSurveySubsystem::GetPointClouds() const
{
    return ActiveProject.PointClouds;
}

bool UCSTopoSurveySubsystem::StartCopcCacheBuild(const FString& SourceId, FString& ErrorMessage)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        ErrorMessage = TEXT("Point cloud was not found.");
        return false;
    }

    FString PdalPath;
    if (!UCSTopoPointCloudImport::FindPdalExecutable(PdalPath))
    {
        ErrorMessage = TEXT("PDAL was not found. Set CSTOPO_PDAL_PATH or install QGIS with PDAL.");
        return false;
    }

    if (Source->CachePath.IsEmpty())
    {
        ErrorMessage = TEXT("Point cloud has no COPC cache path.");
        return false;
    }

    Source->CacheState = ECSTopoPointCloudCacheState::Building;
    Source->CacheStatus = FString::Printf(TEXT("Building cache: %s"), *Source->CachePath);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Source->CachePath), true);
    const FString Args = FString::Printf(TEXT("translate \"%s\" \"%s\" --writer writers.copc"), *Source->SourcePath, *Source->CachePath);
    int32 ReturnCode = 0;
    FString StdOut;
    FString StdErr;
    const bool bExecOk = FPlatformProcess::ExecProcess(*PdalPath, *Args, &ReturnCode, &StdOut, &StdErr);
    if (!bExecOk || ReturnCode != 0)
    {
        Source->CacheState = ECSTopoPointCloudCacheState::Failed;
        Source->CacheStatus = FString::Printf(TEXT("Cache build failed for %s"), *Source->CachePath);
        ErrorMessage = FString::Printf(TEXT("PDAL COPC build failed (%d). %s %s"), ReturnCode, *StdOut, *StdErr).TrimStartAndEnd();
        return false;
    }

    UCSTopoPointCloudImport::RefreshSourceRuntimeState(*Source, ErrorMessage);
    Source->CacheState = Source->bCacheExists ? ECSTopoPointCloudCacheState::Ready : ECSTopoPointCloudCacheState::Missing;
    Source->CacheStatus = Source->bCacheExists
        ? FString::Printf(TEXT("Built COPC cache: %s"), *Source->CachePath)
        : FString::Printf(TEXT("PDAL reported success but cache is missing: %s"), *Source->CachePath);

    if (Source->bLoaded)
    {
        if (TObjectPtr<ALidarPointCloudActor>* Actor = PointCloudActors.Find(Source->SourceId))
        {
            if (Actor->Get() != nullptr)
            {
                Actor->Get()->Destroy();
            }
            PointCloudActors.Remove(Source->SourceId);
            PointCloudActorDisplayPaths.Remove(Source->SourceId);
        }

        LoadedSurfaces.Remove(Source->SourceId);
        FString SpawnError;
        SpawnPointCloudActor(*Source, SpawnError);
        LoadDerivedSurfaceForSource(*Source, SpawnError);
    }

    ErrorMessage = Source->CacheStatus;
    return Source->bCacheExists;
}

bool UCSTopoSurveySubsystem::StartSurfaceBuild(const FString& SourceId, bool bForceRebuild, FString& ErrorMessage)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        ErrorMessage = TEXT("Point cloud was not found.");
        return false;
    }

    const FString ManifestPath = FPaths::Combine(GetProjectCacheDirectory(), TEXT("surfaces"), Source->SourceId.Left(8), TEXT("surface_manifest.json"));
    const FString ProgressPath = FPaths::Combine(FPaths::GetPath(ManifestPath), TEXT("surface_progress.json"));
    Source->SurfaceManifestPath = ManifestPath;

    if (!bForceRebuild && FPaths::FileExists(ManifestPath))
    {
        if (LoadDerivedSurfaceForSource(*Source, ErrorMessage))
        {
            Source->SurfaceBuildState = ECSTopoSurfaceBuildState::Ready;
            Source->SurfaceBuildProgress = 1.0f;
            Source->SurfaceBuildProgressStage = TEXT("Ready");
            Source->SurfaceBuildProgressMessage = TEXT("Existing surface manifest is current.");
            Source->SurfaceStatus = FString::Printf(TEXT("Surface ready: %s"), *ManifestPath);
            return true;
        }
    }

    if (SurfaceBuildProcessHandle.IsValid())
    {
        ErrorMessage = TEXT("A surface build is already running.");
        return false;
    }

    const FString ScriptPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("scripts"), TEXT("cstopo_cli.py"));
    if (!FPaths::FileExists(ScriptPath))
    {
        ErrorMessage = FString::Printf(TEXT("Surface build script is missing: %s"), *ScriptPath);
        return false;
    }

    const FString TileSizeArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.TileSizeSourceUnits);
    const FString TilePaddingArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.TilePaddingSourceUnits);
    const FString TriangleZDeltaArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.MaxSurfaceTriangleZDeltaSourceUnits);
    const FString TinMaxEdgeLengthArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.TinMaxEdgeLengthSourceUnits);
    const FString TinBoundaryEdgeMultiplierArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.TinBoundaryEdgeMultiplier);
    const FString TinBoundaryAreaMultiplierArgument = FString::SanitizeFloat(ActiveProject.SurfaceSettings.TinBoundaryAreaMultiplier);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProgressPath), true);
    IFileManager::Get().Delete(*ProgressPath, false, true);
    const FString Arguments = FString::Printf(
        TEXT("\"%s\" build-surface --source \"%s\" --source-id \"%s\" --cache-dir \"%s\" --surface-build-method \"%s\" --ground-method \"%s\" --tile-size-source-units %s --tile-padding-source-units %s --max-surface-fill-cells %d --max-surface-triangle-z-delta-source-units %s --tin-max-buffered-points-per-tile %d --tin-max-edge-length-source-units %s --tin-boundary-mode \"%s\" --tin-boundary-edge-multiplier %s --tin-boundary-area-multiplier %s %s --tin-decimation-mode \"%s\" --global-tin-max-points %d --surface-collision-mode \"%s\" --progress-path \"%s\"%s"),
        *ScriptPath,
        *Source->SourcePath,
        *Source->SourceId,
        *GetProjectCacheDirectory(),
        *ActiveProject.SurfaceSettings.SurfaceBuildMethod,
        *ActiveProject.SurfaceSettings.GroundClassificationMethod,
        *TileSizeArgument,
        *TilePaddingArgument,
        ActiveProject.SurfaceSettings.MaxSurfaceFillCells,
        *TriangleZDeltaArgument,
        ActiveProject.SurfaceSettings.TinMaxBufferedPointsPerTile,
        *TinMaxEdgeLengthArgument,
        *ActiveProject.SurfaceSettings.TinBoundaryMode,
        *TinBoundaryEdgeMultiplierArgument,
        *TinBoundaryAreaMultiplierArgument,
        ActiveProject.SurfaceSettings.bTinConstrainToBoundary ? TEXT("--tin-constrain-to-boundary") : TEXT("--no-tin-constrain-to-boundary"),
        *ActiveProject.SurfaceSettings.TinDecimationMode,
        ActiveProject.SurfaceSettings.GlobalTinMaxPoints,
        *ActiveProject.SurfaceSettings.SurfaceCollisionMode,
        *ProgressPath,
        bForceRebuild ? TEXT(" --force") : TEXT(""));
    SurfaceBuildProcessHandle = FPlatformProcess::CreateProc(TEXT("python"), *Arguments, false, true, true, nullptr, 0, *FPaths::ProjectDir(), nullptr);
    if (!SurfaceBuildProcessHandle.IsValid())
    {
        ErrorMessage = TEXT("Failed to start background surface build.");
        return false;
    }

    Source->SurfaceBuildState = ECSTopoSurfaceBuildState::Building;
    Source->SurfaceBuildProgress = 0.02f;
    Source->SurfaceBuildProgressStage = TEXT("Starting");
    Source->SurfaceBuildProgressMessage = TEXT("Starting derived surface build.");
    Source->SurfaceStatus = FString::Printf(TEXT("Building derived surface for %s..."), *FPaths::GetCleanFilename(Source->SourcePath));
    SurfaceBuildProcessSourceId = Source->SourceId;
    SurfaceBuildManifestPath = ManifestPath;
    SurfaceBuildProgressPath = ProgressPath;
    ErrorMessage = Source->SurfaceStatus;
    return true;
}

bool UCSTopoSurveySubsystem::SetSurfaceVisible(const FString& SourceId, bool bVisible)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        return false;
    }

    Source->bSurfacePrimary = bVisible;
    RefreshSurfacePresentation(*Source);
    return true;
}

bool UCSTopoSurveySubsystem::ToggleActiveSourceTinRenderVisible(FString& StatusMessage)
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }
    return SetActiveSourceTinRenderVisible(!Source->bSurfaceRenderVisible, StatusMessage);
}

bool UCSTopoSurveySubsystem::SetActiveSourceTinRenderVisible(bool bVisible, FString& StatusMessage)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    Source->bSurfaceRenderVisible = bVisible;
    RefreshSurfacePresentation(*Source);
    StatusMessage = bVisible ? TEXT("Source TIN visible.") : TEXT("Source TIN hidden; walk collision remains active.");
    return true;
}

bool UCSTopoSurveySubsystem::ToggleUserTinVisible(FString& StatusMessage)
{
    return SetUserTinVisible(!bUserTinVisible, StatusMessage);
}

bool UCSTopoSurveySubsystem::SetUserTinVisible(bool bVisible, FString& StatusMessage)
{
    bUserTinVisible = bVisible;
    if (!bUserTinVisible)
    {
        if (UserTinMeshComponent != nullptr)
        {
            UserTinMeshComponent->ClearAllMeshSections();
            UserTinMeshComponent->SetVisibility(false, true);
            UserTinMeshComponent->SetHiddenInGame(true, true);
        }
        if (UserTinActor != nullptr)
        {
            UserTinActor->SetActorHiddenInGame(true);
        }
        UserTinStatusLine = TEXT("User TIN hidden.");
        StatusMessage = UserTinStatusLine;
        return true;
    }

    bUserTinDirty = true;
    return RebuildUserTinPreview(StatusMessage);
}

FString UCSTopoSurveySubsystem::GetUserTinStatusLine() const
{
    return UserTinStatusLine;
}

TArray<FCSTopoControlCodeDefinition> UCSTopoSurveySubsystem::GetControlCodeDefinitions() const
{
    return ControlCodeDefinitions;
}

bool UCSTopoSurveySubsystem::SetPendingControlCode(const FString& ControlCode, const FString& Parameter, FString& StatusMessage)
{
    const FString NormalizedControl = NormalizeControlCode(ControlCode);
    if (NormalizedControl.IsEmpty())
    {
        ClearPendingControlCode();
        StatusMessage = TEXT("Control code cleared.");
        return true;
    }

    if (FindControlCodeDefinition(NormalizedControl) == nullptr)
    {
        StatusMessage = FString::Printf(TEXT("%s is not in the CSTopo control-code list."), *NormalizedControl);
        return false;
    }

    const FString NormalizedParameter = Parameter.TrimStartAndEnd().ToUpper();
    if (!ValidateControlParameter(NormalizedControl, NormalizedParameter, StatusMessage))
    {
        return false;
    }

    PendingControlCode = NormalizedControl;
    PendingControlParameter = NormalizedParameter;
    bPendingControlAutomatic = false;
    StatusMessage = PendingControlParameter.IsEmpty()
        ? FString::Printf(TEXT("%s will apply to the next measurement."), *PendingControlCode)
        : FString::Printf(TEXT("%s %s will apply to the next measurement."), *PendingControlCode, *PendingControlParameter);
    return true;
}

void UCSTopoSurveySubsystem::ClearPendingControlCode()
{
    PendingControlCode.Empty();
    PendingControlParameter.Empty();
    bPendingControlAutomatic = false;
}

FString UCSTopoSurveySubsystem::GetPendingControlCode() const
{
    return PendingControlCode;
}

FString UCSTopoSurveySubsystem::GetPendingControlParameter() const
{
    return PendingControlParameter;
}

bool UCSTopoSurveySubsystem::IsPendingControlAutomatic() const
{
    return bPendingControlAutomatic;
}

bool UCSTopoSurveySubsystem::UndoLastMeasurement(FString& StatusMessage)
{
    if (ActiveProject.Shots.IsEmpty())
    {
        StatusMessage = TEXT("No measurements to remove.");
        return false;
    }

    const FCSTopoShotRecord RemovedShot = ActiveProject.Shots.Last();
    ActiveProject.Shots.Pop();
    for (FCSTopoFigureRecord& Figure : ActiveProject.Figures)
    {
        Figure.PointNumbers.Remove(RemovedShot.PointNumber);
    }
    ActiveProject.Figures.RemoveAll([](const FCSTopoFigureRecord& Figure)
    {
        return Figure.PointNumbers.IsEmpty();
    });
    int32 MaxPointNumber = 0;
    for (const FCSTopoShotRecord& Shot : ActiveProject.Shots)
    {
        MaxPointNumber = FMath::Max(MaxPointNumber, Shot.PointNumber);
    }
    ActiveProject.NextPointNumber = MaxPointNumber + 1;
    RebuildFigureSegments();
    bUserTinDirty = true;
    StatusMessage = FString::Printf(TEXT("Removed shot %d %s."), RemovedShot.PointNumber, *RemovedShot.Code);
    return true;
}

bool UCSTopoSurveySubsystem::SetCloudOverlayVisible(const FString& SourceId, bool bVisible)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SourceId);
    if (Source == nullptr)
    {
        return false;
    }

    Source->bCloudOverlayVisible = bVisible;
    RefreshSurfacePresentation(*Source);
    return true;
}

bool UCSTopoSurveySubsystem::CycleActiveViewMode(FString& StatusMessage)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    const int32 NextValue = (static_cast<int32>(Source->DefaultViewMode) + 1) % 5;
    Source->DefaultViewMode = static_cast<ECSTopoViewMode>(NextValue);
    RefreshPointCloudViewMode(*Source);
    StatusMessage = FString::Printf(TEXT("View mode: %s"), *ViewModeLabel(Source->DefaultViewMode));
    return true;
}

bool UCSTopoSurveySubsystem::CanWalkActiveSurface(FString& StatusMessage) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    if (Source->SurfaceBuildState != ECSTopoSurfaceBuildState::Ready)
    {
        StatusMessage = FString::Printf(TEXT("Walk mode needs a ready derived surface. Current state: %s."), *SurfaceStateLabel(Source->SurfaceBuildState));
        return false;
    }

    const FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source->SourceId);
    if (Surface == nullptr || !Surface->IsReady())
    {
        StatusMessage = TEXT("Derived surface is not loaded yet.");
        return false;
    }

    StatusMessage = TEXT("Derived surface is ready.");
    return true;
}

bool UCSTopoSurveySubsystem::LoadTileGeometry(const FString& MeshPath, FCSTopoLoadedSurfaceTile& Tile, FString& ErrorMessage) const
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *MeshPath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to read surface tile: %s"), *MeshPath);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        ErrorMessage = FString::Printf(TEXT("Failed to parse surface tile JSON: %s"), *MeshPath);
        return false;
    }

    Tile.TileId = Root->GetStringField(TEXT("tile_id"));
    const TArray<TSharedPtr<FJsonValue>>* BoundsMinArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* BoundsMaxArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* VerticesArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* TrianglesArray = nullptr;
    if (!Root->TryGetArrayField(TEXT("bounds_min"), BoundsMinArray)
        || !Root->TryGetArrayField(TEXT("bounds_max"), BoundsMaxArray)
        || !Root->TryGetArrayField(TEXT("vertices"), VerticesArray)
        || !Root->TryGetArrayField(TEXT("triangles"), TrianglesArray))
    {
        ErrorMessage = TEXT("Surface tile JSON is missing bounds, vertices, or triangles.");
        return false;
    }

    if (BoundsMinArray->Num() >= 3)
    {
        Tile.BoundsMin = FVector((*BoundsMinArray)[0]->AsNumber(), (*BoundsMinArray)[1]->AsNumber(), (*BoundsMinArray)[2]->AsNumber());
    }
    if (BoundsMaxArray->Num() >= 3)
    {
        Tile.BoundsMax = FVector((*BoundsMaxArray)[0]->AsNumber(), (*BoundsMaxArray)[1]->AsNumber(), (*BoundsMaxArray)[2]->AsNumber());
    }

    Tile.SourceVertices.Reset();
    Tile.Triangles.Reset();
    for (const TSharedPtr<FJsonValue>& VertexValue : *VerticesArray)
    {
        const TArray<TSharedPtr<FJsonValue>>* VertexArray = nullptr;
        if (VertexValue.IsValid() && VertexValue->TryGetArray(VertexArray) && VertexArray->Num() >= 3)
        {
            Tile.SourceVertices.Add(FVector((*VertexArray)[0]->AsNumber(), (*VertexArray)[1]->AsNumber(), (*VertexArray)[2]->AsNumber()));
        }
    }
    for (const TSharedPtr<FJsonValue>& TriangleValue : *TrianglesArray)
    {
        Tile.Triangles.Add(static_cast<int32>(TriangleValue->AsNumber()));
    }

    ErrorMessage.Empty();
    return Tile.SourceVertices.Num() >= 3 && Tile.Triangles.Num() >= 3;
}

bool UCSTopoSurveySubsystem::LoadDerivedSurfaceForSource(FCSTopoPointCloudSource& Source, FString& ErrorMessage)
{
    if (Source.SurfaceManifestPath.IsEmpty())
    {
        Source.SurfaceManifestPath = FPaths::Combine(GetProjectCacheDirectory(), TEXT("surfaces"), Source.SourceId.Left(8), TEXT("surface_manifest.json"));
    }

    if (!FPaths::FileExists(Source.SurfaceManifestPath))
    {
        ErrorMessage = FString::Printf(TEXT("Surface manifest is missing: %s"), *Source.SurfaceManifestPath);
        Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Missing;
        return false;
    }

    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Source.SurfaceManifestPath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to read surface manifest: %s"), *Source.SurfaceManifestPath);
        Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Failed;
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        ErrorMessage = FString::Printf(TEXT("Failed to parse surface manifest JSON: %s"), *Source.SurfaceManifestPath);
        Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Failed;
        return false;
    }

    FCSTopoDerivedSurfaceManifest Manifest;
    if (!Root->TryGetStringField(TEXT("schema_version"), Manifest.SchemaVersion)
        || Manifest.SchemaVersion != CurrentSurfaceManifestSchemaVersion)
    {
        ErrorMessage = FString::Printf(TEXT("Surface manifest is stale; rebuild required: %s"), *Source.SurfaceManifestPath);
        Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Stale;
        Source.SurfaceStatus = ErrorMessage;
        return false;
    }
    Manifest.SurfaceId = Root->GetStringField(TEXT("surface_id"));
    Manifest.SourceCloudId = Root->GetStringField(TEXT("source_cloud_id"));
    Manifest.BuildState = SurfaceStateFromString(Root->GetStringField(TEXT("build_state")));
    Manifest.BuildMethod = Root->GetStringField(TEXT("build_method"));
    Manifest.GroundSource = Root->GetStringField(TEXT("ground_source"));
    Manifest.TileSize = Root->GetNumberField(TEXT("tile_size"));
    Manifest.TileCount = Root->GetIntegerField(TEXT("tile_count"));
    Manifest.SourceCrs = Root->GetStringField(TEXT("source_crs"));
    Manifest.LinearUnitName = Root->GetStringField(TEXT("linear_unit_name"));
    Manifest.LinearUnitToMeters = Root->GetNumberField(TEXT("linear_unit_to_meters"));
    Manifest.ManifestPath = Source.SurfaceManifestPath;
    Manifest.TotalVertexCount = Root->GetIntegerField(TEXT("total_vertex_count"));
    Manifest.TotalTriangleCount = Root->GetIntegerField(TEXT("total_triangle_count"));
    Root->TryGetBoolField(TEXT("decimation_used"), Manifest.bDecimationUsed);
    Root->TryGetStringField(TEXT("tin_boundary_mode"), Manifest.TinBoundaryMode);
    Root->TryGetNumberField(TEXT("tin_resolved_max_edge_length_source_units"), Manifest.TinResolvedMaxEdgeLengthSourceUnits);
    Root->TryGetNumberField(TEXT("tin_boundary_edge_multiplier"), Manifest.TinBoundaryEdgeMultiplier);
    Root->TryGetNumberField(TEXT("tin_boundary_area_multiplier"), Manifest.TinBoundaryAreaMultiplier);
    if (Root->HasTypedField<EJson::Number>(TEXT("tin_rejected_large_triangle_count")))
    {
        Manifest.TinRejectedLargeTriangleCount = Root->GetIntegerField(TEXT("tin_rejected_large_triangle_count"));
    }
    if (Root->HasTypedField<EJson::Number>(TEXT("tin_retained_candidate_triangle_count")))
    {
        Manifest.TinRetainedCandidateTriangleCount = Root->GetIntegerField(TEXT("tin_retained_candidate_triangle_count"));
    }
    if (Root->HasTypedField<EJson::Number>(TEXT("tin_boundary_edge_count")))
    {
        Manifest.TinBoundaryEdgeCount = Root->GetIntegerField(TEXT("tin_boundary_edge_count"));
    }
    if (Root->HasTypedField<EJson::Number>(TEXT("tin_boundary_loop_count")))
    {
        Manifest.TinBoundaryLoopCount = Root->GetIntegerField(TEXT("tin_boundary_loop_count"));
    }
    Root->TryGetStringField(TEXT("tin_constrained_triangulation_status"), Manifest.TinConstrainedTriangulationStatus);
    if (Root->HasTypedField<EJson::Number>(TEXT("global_point_count")))
    {
        Manifest.GlobalPointCount = Root->GetIntegerField(TEXT("global_point_count"));
    }
    if (Root->HasTypedField<EJson::Number>(TEXT("global_retained_triangle_count")))
    {
        Manifest.GlobalRetainedTriangleCount = Root->GetIntegerField(TEXT("global_retained_triangle_count"));
    }
    if (Root->HasTypedField<EJson::Number>(TEXT("global_rejected_triangle_count")))
    {
        Manifest.GlobalRejectedTriangleCount = Root->GetIntegerField(TEXT("global_rejected_triangle_count"));
    }
    Root->TryGetStringField(TEXT("topology_audit_status"), Manifest.TopologyAuditStatus);
    Root->TryGetStringField(TEXT("topology_audit_message"), Manifest.TopologyAuditMessage);
    Root->TryGetStringField(TEXT("surface_collision_mode"), Manifest.SurfaceCollisionMode);
    Root->TryGetStringField(TEXT("seam_audit_status"), Manifest.SeamAuditStatus);
    Root->TryGetStringField(TEXT("seam_audit_message"), Manifest.SeamAuditMessage);
    const FString GeneratedAtString = Root->GetStringField(TEXT("generated_at"));
    FDateTime::ParseIso8601(*GeneratedAtString, Manifest.GeneratedAt);

    const TArray<TSharedPtr<FJsonValue>>* BoundsMinArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* BoundsMaxArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* TilesArray = nullptr;
    Root->TryGetArrayField(TEXT("bounds_min"), BoundsMinArray);
    Root->TryGetArrayField(TEXT("bounds_max"), BoundsMaxArray);
    Root->TryGetArrayField(TEXT("tiles"), TilesArray);
    if (BoundsMinArray != nullptr && BoundsMinArray->Num() >= 3)
    {
        Manifest.BoundsMin = FVector((*BoundsMinArray)[0]->AsNumber(), (*BoundsMinArray)[1]->AsNumber(), (*BoundsMinArray)[2]->AsNumber());
    }
    if (BoundsMaxArray != nullptr && BoundsMaxArray->Num() >= 3)
    {
        Manifest.BoundsMax = FVector((*BoundsMaxArray)[0]->AsNumber(), (*BoundsMaxArray)[1]->AsNumber(), (*BoundsMaxArray)[2]->AsNumber());
    }

    FCSTopoLoadedSurface Loaded;
    Loaded.SourceId = Source.SourceId;
    Loaded.SurfaceId = Manifest.SurfaceId;
    Loaded.ManifestPath = Source.SurfaceManifestPath;
    Loaded.SourceCenter = GetSourceCenter(Source);

    if (TilesArray != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& TileValue : *TilesArray)
        {
            const TSharedPtr<FJsonObject>* TileObject = nullptr;
            if (!TileValue.IsValid() || !TileValue->TryGetObject(TileObject) || TileObject == nullptr || !TileObject->IsValid())
            {
                continue;
            }

            FCSTopoSurfaceTileRecord TileRecord;
            TileRecord.TileId = (*TileObject)->GetStringField(TEXT("tile_id"));
            TileRecord.VertexCount = (*TileObject)->GetIntegerField(TEXT("vertex_count"));
            TileRecord.TriangleCount = (*TileObject)->GetIntegerField(TEXT("triangle_count"));
            TileRecord.MeshPath = (*TileObject)->GetStringField(TEXT("mesh_path"));
            TileRecord.Status = (*TileObject)->GetStringField(TEXT("status"));
            if ((*TileObject)->HasField(TEXT("input_point_count")))
            {
                TileRecord.InputPointCount = (*TileObject)->GetIntegerField(TEXT("input_point_count"));
            }
            if ((*TileObject)->HasField(TEXT("used_point_count")))
            {
                TileRecord.UsedPointCount = (*TileObject)->GetIntegerField(TEXT("used_point_count"));
            }
            (*TileObject)->TryGetStringField(TEXT("decimation_method"), TileRecord.DecimationMethod);
            if ((*TileObject)->HasField(TEXT("boundary_edge_count")))
            {
                TileRecord.BoundaryEdgeCount = (*TileObject)->GetIntegerField(TEXT("boundary_edge_count"));
            }
            if ((*TileObject)->HasField(TEXT("boundary_loop_count")))
            {
                TileRecord.BoundaryLoopCount = (*TileObject)->GetIntegerField(TEXT("boundary_loop_count"));
            }

            const TArray<TSharedPtr<FJsonValue>>* TileBoundsMin = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* TileBoundsMax = nullptr;
            if ((*TileObject)->TryGetArrayField(TEXT("bounds_min"), TileBoundsMin) && TileBoundsMin->Num() >= 3)
            {
                TileRecord.BoundsMin = FVector((*TileBoundsMin)[0]->AsNumber(), (*TileBoundsMin)[1]->AsNumber(), (*TileBoundsMin)[2]->AsNumber());
            }
            if ((*TileObject)->TryGetArrayField(TEXT("bounds_max"), TileBoundsMax) && TileBoundsMax->Num() >= 3)
            {
                TileRecord.BoundsMax = FVector((*TileBoundsMax)[0]->AsNumber(), (*TileBoundsMax)[1]->AsNumber(), (*TileBoundsMax)[2]->AsNumber());
            }
            Manifest.Tiles.Add(TileRecord);

            FCSTopoLoadedSurfaceTile LoadedTile;
            if (LoadTileGeometry(TileRecord.MeshPath, LoadedTile, ErrorMessage))
            {
                LoadedTile.BoundaryEdgeCount = TileRecord.BoundaryEdgeCount;
                LoadedTile.BoundaryLoopCount = TileRecord.BoundaryLoopCount;
                Loaded.Tiles.Add(MoveTemp(LoadedTile));
            }
        }
    }

    if (Loaded.Tiles.IsEmpty())
    {
        ErrorMessage = TEXT("Surface manifest loaded, but no surface tiles were available.");
        Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Failed;
        return false;
    }

    ActiveProject.DerivedSurfaces.RemoveAll([&Source](const FCSTopoDerivedSurfaceManifest& Candidate)
    {
        return Candidate.SourceCloudId == Source.SourceId;
    });
    ActiveProject.DerivedSurfaces.Add(Manifest);
    LoadedSurfaces.Add(Source.SourceId, MoveTemp(Loaded));

    if (TObjectPtr<AActor>* ExistingActor = SurfaceActors.Find(Source.SourceId))
    {
        if (ExistingActor->Get() != nullptr)
        {
            ExistingActor->Get()->Destroy();
        }
        SurfaceActors.Remove(Source.SourceId);
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        ErrorMessage = TEXT("World is not available for surface rendering.");
        return false;
    }

    AActor* SurfaceActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
    if (SurfaceActor == nullptr)
    {
        ErrorMessage = TEXT("Failed to spawn derived surface actor.");
        return false;
    }

    USceneComponent* RootComponent = NewObject<USceneComponent>(SurfaceActor, TEXT("Root"));
    SurfaceActor->SetRootComponent(RootComponent);
    RootComponent->RegisterComponent();
#if WITH_EDITOR
    SurfaceActor->SetActorLabel(FString::Printf(TEXT("CSTopoSurface_%s"), *FPaths::GetBaseFilename(Source.SourcePath)));
#endif

    FCSTopoLoadedSurface& Surface = LoadedSurfaces.FindChecked(Source.SourceId);
    UProceduralMeshComponent* CollisionProxy = NewObject<UProceduralMeshComponent>(SurfaceActor, TEXT("SurfaceCollisionProxy"));
    CollisionProxy->SetupAttachment(RootComponent);
    CollisionProxy->RegisterComponent();
    CollisionProxy->SetMobility(EComponentMobility::Movable);
    CollisionProxy->SetVisibility(false, true);
    CollisionProxy->SetHiddenInGame(true, true);
    CollisionProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CollisionProxy->SetCollisionObjectType(CSTopoSurfaceCollisionChannel);
    CollisionProxy->SetCollisionResponseToAllChannels(ECR_Block);
    CollisionProxy->bUseAsyncCooking = true;
    Surface.CollisionProxyComponent = CollisionProxy;

    for (int32 TileIndex = 0; TileIndex < Surface.Tiles.Num(); ++TileIndex)
    {
        FCSTopoLoadedSurfaceTile& Tile = Surface.Tiles[TileIndex];
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(SurfaceActor, *FString::Printf(TEXT("SurfaceTile_%d"), TileIndex));
        Mesh->SetupAttachment(RootComponent);
        Mesh->RegisterComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        // CSTopo walk mode blocks only this dedicated survey-surface channel.
        // Keeping derived tiles off WorldStatic prevents hidden/editor/level
        // collision from becoming indistinguishable from the survey floor.
        Mesh->SetCollisionObjectType(CSTopoSurfaceCollisionChannel);
        Mesh->SetCollisionResponseToAllChannels(ECR_Block);
        Mesh->bUseAsyncCooking = false;

        TArray<FVector> RenderVertices;
        TArray<int32> RenderTriangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        RenderVertices.Reserve(Tile.SourceVertices.Num());
        RenderTriangles.Reserve(Tile.Triangles.Num());
        Normals.Init(FVector::ZeroVector, Tile.SourceVertices.Num());
        UVs.Init(FVector2D::ZeroVector, Tile.SourceVertices.Num());
        Colors.Init(FLinearColor(0.72f, 0.76f, 0.78f, 1.0f), Tile.SourceVertices.Num());
        Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), Tile.SourceVertices.Num());
        for (const FVector& SourceVertex : Tile.SourceVertices)
        {
            RenderVertices.Add(SourceToRenderPoint(Source, SourceVertex));
        }
        for (int32 Index = 0; Index + 2 < Tile.Triangles.Num(); Index += 3)
        {
            const int32 AIndex = Tile.Triangles[Index];
            const int32 BIndex = Tile.Triangles[Index + 1];
            const int32 CIndex = Tile.Triangles[Index + 2];
            if (!RenderVertices.IsValidIndex(AIndex) || !RenderVertices.IsValidIndex(BIndex) || !RenderVertices.IsValidIndex(CIndex))
            {
                continue;
            }

            FVector FaceNormal = FVector::CrossProduct(RenderVertices[BIndex] - RenderVertices[AIndex], RenderVertices[CIndex] - RenderVertices[AIndex]);
            if (FaceNormal.Z < 0.0f)
            {
                FaceNormal *= -1.0f;
            }
            FaceNormal.Normalize();
            Normals[AIndex] += FaceNormal;
            Normals[BIndex] += FaceNormal;
            Normals[CIndex] += FaceNormal;

            RenderTriangles.Add(AIndex);
            RenderTriangles.Add(CIndex);
            RenderTriangles.Add(BIndex);
        }
        for (FVector& Normal : Normals)
        {
            if (!Normal.Normalize())
            {
                Normal = FVector::UpVector;
            }
        }
        const double TileMinZ = Tile.BoundsMin.Z;
        const double TileZRange = FMath::Max(Tile.BoundsMax.Z - Tile.BoundsMin.Z, 1.0);
        for (int32 VertexIndex = 0; VertexIndex < RenderVertices.Num(); ++VertexIndex)
        {
            const double ElevationT = FMath::Clamp((Tile.SourceVertices[VertexIndex].Z - TileMinZ) / TileZRange, 0.0, 1.0);
            const float SlopeShade = FMath::Clamp(0.52f + Normals[VertexIndex].Z * 0.42f, 0.45f, 0.98f);
            const FLinearColor LowColor(0.36f, 0.45f, 0.43f, 1.0f);
            const FLinearColor MidColor(0.60f, 0.58f, 0.50f, 1.0f);
            const FLinearColor HighColor(0.82f, 0.76f, 0.62f, 1.0f);
            const FLinearColor BaseColor = ElevationT < 0.5
                ? FMath::Lerp(LowColor, MidColor, static_cast<float>(ElevationT * 2.0))
                : FMath::Lerp(MidColor, HighColor, static_cast<float>((ElevationT - 0.5) * 2.0));
            Colors[VertexIndex] = FLinearColor(BaseColor.R * SlopeShade, BaseColor.G * SlopeShade, BaseColor.B * SlopeShade, 1.0f);
        }

        Mesh->CreateMeshSection_LinearColor(0, RenderVertices, RenderTriangles, Normals, UVs, Colors, Tangents, true);
        static UMaterialInterface* VertexColorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
        Mesh->SetMaterial(0, VertexColorMaterial != nullptr ? VertexColorMaterial : UMaterial::GetDefaultMaterial(MD_Surface));
        Mesh->SetCastShadow(false);
        Mesh->bCastDynamicShadow = false;
        Mesh->SetVisibility(false, true);
        Mesh->SetHiddenInGame(true, true);
        Tile.bRuntimeVisible = false;
        Tile.MeshComponent = Mesh;
    }

    SurfaceActors.Add(Source.SourceId, SurfaceActor);
    Source.SurfaceId = Manifest.SurfaceId;
    Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Ready;
    Source.SurfaceBuildProgress = 1.0f;
    Source.SurfaceBuildProgressStage = TEXT("Ready");
    Source.SurfaceBuildProgressMessage = TEXT("Derived surface is ready.");
    Source.SurfaceStatus = FString::Printf(
        TEXT("Surface ready: %d tile(s) | %s | %s | %d vertices | %d triangles | %s | TIN trim edge %.2f | rejected %d large triangles | %s"),
        Manifest.TileCount,
        *Manifest.BuildMethod,
        *Manifest.GroundSource,
        Manifest.TotalVertexCount,
        Manifest.TotalTriangleCount,
        Manifest.bDecimationUsed ? TEXT("decimated") : TEXT("all points"),
        Manifest.TinResolvedMaxEdgeLengthSourceUnits,
        Manifest.TinRejectedLargeTriangleCount,
        *Manifest.SurfaceCollisionMode);
    UpdateVisibleSurfaceTiles(Source, GetSourceCenter(Source), true);
    RefreshSurfacePresentation(Source);
    ErrorMessage = Source.SurfaceStatus;
    return true;
}

bool UCSTopoSurveySubsystem::TraceTriangleFromView(const FVector& ViewOrigin, const FVector& ViewDirection, const FVector& A, const FVector& B, const FVector& C, double& OutDistance, FVector& OutHit) const
{
    const FVector Direction = ViewDirection.GetSafeNormal();
    const FVector Edge1 = B - A;
    const FVector Edge2 = C - A;
    const FVector P = FVector::CrossProduct(Direction, Edge2);
    const double Determinant = FVector::DotProduct(Edge1, P);
    if (FMath::Abs(Determinant) < KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const double InvDeterminant = 1.0 / Determinant;
    const FVector T = ViewOrigin - A;
    const double U = FVector::DotProduct(T, P) * InvDeterminant;
    if (U < 0.0 || U > 1.0)
    {
        return false;
    }

    const FVector Q = FVector::CrossProduct(T, Edge1);
    const double V = FVector::DotProduct(Direction, Q) * InvDeterminant;
    if (V < 0.0 || U + V > 1.0)
    {
        return false;
    }

    const double Distance = FVector::DotProduct(Edge2, Q) * InvDeterminant;
    if (Distance < 0.0)
    {
        return false;
    }

    OutDistance = Distance;
    OutHit = ViewOrigin + Direction * static_cast<float>(Distance);
    return true;
}

bool UCSTopoSurveySubsystem::TraceActiveDerivedSurface(const FCSTopoPointCloudSource& Source, const FVector& ViewOrigin, const FVector& ViewDirection, FVector& SurfaceRenderHit, FString& TileId, FString& ErrorMessage) const
{
    const FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source.SourceId);
    if (Surface == nullptr || !Surface->IsReady())
    {
        ErrorMessage = TEXT("No derived surface is ready for the active point cloud.");
        return false;
    }

    double BestDistance = TNumericLimits<double>::Max();
    bool bHit = false;
    for (const FCSTopoLoadedSurfaceTile& Tile : Surface->Tiles)
    {
        if (!ShouldUseSurfaceTileForRuntimeQuery(*Surface, Tile))
        {
            continue;
        }

        const FVector TileCenterSource = (Tile.BoundsMin + Tile.BoundsMax) * 0.5;
        const FVector TileCenterRender = SourceToRenderPoint(Source, TileCenterSource);
        const double TileRadiusRender = FVector::Distance(SourceToRenderPoint(Source, Tile.BoundsMax), TileCenterRender);
        const FVector Direction = ViewDirection.GetSafeNormal();
        const double AlongDistance = FVector::DotProduct(TileCenterRender - ViewOrigin, Direction);
        if (AlongDistance < -TileRadiusRender)
        {
            continue;
        }
        const FVector ClosestPointOnRay = ViewOrigin + Direction * static_cast<float>(FMath::Max(0.0, AlongDistance));
        if (FVector::DistSquared(ClosestPointOnRay, TileCenterRender) > FMath::Square(TileRadiusRender * 1.35))
        {
            continue;
        }

        for (int32 Index = 0; Index + 2 < Tile.Triangles.Num(); Index += 3)
        {
            const FVector A = SourceToRenderPoint(Source, Tile.SourceVertices[Tile.Triangles[Index]]);
            const FVector B = SourceToRenderPoint(Source, Tile.SourceVertices[Tile.Triangles[Index + 1]]);
            const FVector C = SourceToRenderPoint(Source, Tile.SourceVertices[Tile.Triangles[Index + 2]]);
            double Distance = 0.0;
            FVector Hit = FVector::ZeroVector;
            if (TraceTriangleFromView(ViewOrigin, ViewDirection, A, B, C, Distance, Hit) && Distance < BestDistance)
            {
                BestDistance = Distance;
                SurfaceRenderHit = Hit;
                TileId = Tile.TileId;
                bHit = true;
            }
        }
    }

    ErrorMessage = bHit ? FString() : TEXT("Reticle ray did not intersect the derived surface.");
    return bHit;
}

bool UCSTopoSurveySubsystem::QueryDerivedSurfaceHeightAtSourceXY(const FCSTopoPointCloudSource& Source, double SourceX, double SourceY, double& SurfaceZ, FString* OutTileId) const
{
    const FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source.SourceId);
    if (Surface == nullptr || !Surface->IsReady())
    {
        return false;
    }

    for (const FCSTopoLoadedSurfaceTile& Tile : Surface->Tiles)
    {
        if (SourceX < Tile.BoundsMin.X || SourceX > Tile.BoundsMax.X || SourceY < Tile.BoundsMin.Y || SourceY > Tile.BoundsMax.Y)
        {
            continue;
        }

        const FVector2D P(SourceX, SourceY);
        for (int32 Index = 0; Index + 2 < Tile.Triangles.Num(); Index += 3)
        {
            const FVector A = Tile.SourceVertices[Tile.Triangles[Index]];
            const FVector B = Tile.SourceVertices[Tile.Triangles[Index + 1]];
            const FVector C = Tile.SourceVertices[Tile.Triangles[Index + 2]];
            const FVector2D A2(A.X, A.Y);
            const FVector2D B2(B.X, B.Y);
            const FVector2D C2(C.X, C.Y);
            const double Denominator = ((B2.Y - C2.Y) * (A2.X - C2.X)) + ((C2.X - B2.X) * (A2.Y - C2.Y));
            if (FMath::IsNearlyZero(Denominator))
            {
                continue;
            }

            const double W1 = (((B2.Y - C2.Y) * (P.X - C2.X)) + ((C2.X - B2.X) * (P.Y - C2.Y))) / Denominator;
            const double W2 = (((C2.Y - A2.Y) * (P.X - C2.X)) + ((A2.X - C2.X) * (P.Y - C2.Y))) / Denominator;
            const double W3 = 1.0 - W1 - W2;
            if (W1 < -KINDA_SMALL_NUMBER || W2 < -KINDA_SMALL_NUMBER || W3 < -KINDA_SMALL_NUMBER)
            {
                continue;
            }

            SurfaceZ = W1 * A.Z + W2 * B.Z + W3 * C.Z;
            if (OutTileId != nullptr)
            {
                *OutTileId = Tile.TileId;
            }
            return true;
        }
    }

    return false;
}

bool UCSTopoSurveySubsystem::QueryNearestDerivedSurfaceHeightAtSourceXY(const FCSTopoPointCloudSource& Source, double SourceX, double SourceY, double SearchRadiusSourceUnits, double& SurfaceZ, FString* OutTileId) const
{
    const FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source.SourceId);
    if (Surface == nullptr || !Surface->IsReady())
    {
        return false;
    }

    struct FNearbySurfaceVertex
    {
        double DistanceSq = 0.0;
        double Elevation = 0.0;
        FString TileId;
    };

    TArray<FNearbySurfaceVertex> Nearby;
    const double SearchRadiusSq = FMath::Square(SearchRadiusSourceUnits);
    const FVector2D Query(SourceX, SourceY);

    for (const FCSTopoLoadedSurfaceTile& Tile : Surface->Tiles)
    {
        if (SourceX < Tile.BoundsMin.X - SearchRadiusSourceUnits
            || SourceX > Tile.BoundsMax.X + SearchRadiusSourceUnits
            || SourceY < Tile.BoundsMin.Y - SearchRadiusSourceUnits
            || SourceY > Tile.BoundsMax.Y + SearchRadiusSourceUnits)
        {
            continue;
        }

        for (const FVector& Vertex : Tile.SourceVertices)
        {
            const double DistanceSq = FVector2D::DistSquared(Query, FVector2D(Vertex.X, Vertex.Y));
            if (DistanceSq > SearchRadiusSq)
            {
                continue;
            }

            Nearby.Add({ DistanceSq, Vertex.Z, Tile.TileId });
        }
    }

    if (Nearby.IsEmpty())
    {
        return false;
    }

    Nearby.Sort([](const FNearbySurfaceVertex& A, const FNearbySurfaceVertex& B)
    {
        return A.DistanceSq < B.DistanceSq;
    });

    const int32 Count = FMath::Min(8, Nearby.Num());
    double WeightedZ = 0.0;
    double WeightSum = 0.0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const double Weight = 1.0 / FMath::Max(Nearby[Index].DistanceSq, 0.01);
        WeightedZ += Nearby[Index].Elevation * Weight;
        WeightSum += Weight;
    }

    if (WeightSum <= 0.0)
    {
        return false;
    }

    SurfaceZ = WeightedZ / WeightSum;
    if (OutTileId != nullptr)
    {
        *OutTileId = Nearby[0].TileId;
    }
    return true;
}

bool UCSTopoSurveySubsystem::QueryActiveSurfaceHeightAtRenderLocation(const FVector& RenderLocation, FVector& SurfaceRenderLocation) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr || Source->SurfaceBuildState != ECSTopoSurfaceBuildState::Ready)
    {
        return false;
    }

    const FVector SourcePoint = RenderToSourcePoint(*Source, RenderLocation);
    double SurfaceZ = 0.0;
    if (!QueryDerivedSurfaceHeightAtSourceXY(*Source, SourcePoint.X, SourcePoint.Y, SurfaceZ))
    {
        const double SearchRadius = FMath::Max(25.0, ActiveProject.SurfaceSettings.TileSizeSourceUnits * 0.25);
        if (!QueryNearestDerivedSurfaceHeightAtSourceXY(*Source, SourcePoint.X, SourcePoint.Y, SearchRadius, SurfaceZ))
        {
            return false;
        }
    }

    SurfaceRenderLocation = SourceToRenderPoint(*Source, FVector(SourcePoint.X, SourcePoint.Y, SurfaceZ));
    return true;
}

bool UCSTopoSurveySubsystem::GetActivePointCloudFocusLocation(FVector& FocusRenderLocation) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        return false;
    }

    const FCSTopoDerivedSurfaceManifest* Surface = FindDerivedSurface(Source->SourceId);
    FVector FocusSourcePoint(
        (Source->BoundsMin.X + Source->BoundsMax.X) * 0.5,
        (Source->BoundsMin.Y + Source->BoundsMax.Y) * 0.5,
        Surface != nullptr ? Surface->BoundsMax.Z : Source->BoundsMax.Z);
    double SurfaceZ = 0.0;
    if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready
        && QueryNearestDerivedSurfaceHeightAtSourceXY(*Source, FocusSourcePoint.X, FocusSourcePoint.Y, ActiveProject.SurfaceSettings.TileSizeSourceUnits, SurfaceZ))
    {
        FocusSourcePoint.Z = SurfaceZ;
    }
    FocusRenderLocation = SourceToRenderPoint(*Source, FocusSourcePoint) + FVector(0.0f, 0.0f, 700.0f);
    return true;
}

void UCSTopoSurveySubsystem::PrepareSurveyScene()
{
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsSurveyEnvironmentActor(Actor))
        {
            continue;
        }

        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);
        Actor->SetActorTickEnabled(false);

        TArray<UPrimitiveComponent*> PrimitiveComponents;
        Actor->GetComponents(PrimitiveComponents);
        for (UPrimitiveComponent* Component : PrimitiveComponents)
        {
            if (Component == nullptr)
            {
                continue;
            }

            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetVisibility(false, true);
            Component->SetHiddenInGame(true, true);
        }
    }
}

bool UCSTopoSurveySubsystem::FocusPawnOnActiveSurfaceCenter(APawn* Pawn, float Clearance, FString& StatusMessage)
{
    if (Pawn == nullptr)
    {
        StatusMessage = TEXT("No pawn is available for surface focus.");
        return false;
    }

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    const FCSTopoDerivedSurfaceManifest* Surface = FindDerivedSurface(Source->SourceId);
    const FVector BoundsMin = Surface != nullptr ? Surface->BoundsMin : Source->BoundsMin;
    const FVector BoundsMax = Surface != nullptr ? Surface->BoundsMax : Source->BoundsMax;
    FVector FocusSourcePoint(
        (BoundsMin.X + BoundsMax.X) * 0.5,
        (BoundsMin.Y + BoundsMax.Y) * 0.5,
        BoundsMax.Z);

    FString TileId;
    double SurfaceZ = 0.0;
    const double CenterSearchRadius = FMath::Max(ActiveProject.SurfaceSettings.TileSizeSourceUnits * 2.0, 50.0);
    bool bFocusedExactCenter = QueryNearestDerivedSurfaceHeightAtSourceXY(
        *Source,
        FocusSourcePoint.X,
        FocusSourcePoint.Y,
        CenterSearchRadius,
        SurfaceZ,
        &TileId);

    if (!bFocusedExactCenter)
    {
        const FCSTopoLoadedSurface* LoadedSurface = LoadedSurfaces.Find(Source->SourceId);
        double BestDistanceSq = TNumericLimits<double>::Max();
        FVector BestTileCenter = FVector::ZeroVector;
        if (LoadedSurface != nullptr)
        {
            for (const FCSTopoLoadedSurfaceTile& Tile : LoadedSurface->Tiles)
            {
                const FVector TileCenter = (Tile.BoundsMin + Tile.BoundsMax) * 0.5;
                const double DistanceSq = FVector2D::DistSquared(FVector2D(FocusSourcePoint.X, FocusSourcePoint.Y), FVector2D(TileCenter.X, TileCenter.Y));
                if (DistanceSq < BestDistanceSq)
                {
                    BestDistanceSq = DistanceSq;
                    BestTileCenter = TileCenter;
                    TileId = Tile.TileId;
                }
            }
        }

        if (BestDistanceSq == TNumericLimits<double>::Max())
        {
            StatusMessage = TEXT("No loaded derived surface tile is available for focus.");
            return false;
        }

        FocusSourcePoint = BestTileCenter;
        SurfaceZ = BestTileCenter.Z;
    }
    else
    {
        FocusSourcePoint.Z = SurfaceZ;
    }

    const FVector FocusRenderLocation = SourceToRenderPoint(*Source, FVector(FocusSourcePoint.X, FocusSourcePoint.Y, SurfaceZ))
        + FVector(0.0f, 0.0f, Clearance);
    Pawn->SetActorLocation(FocusRenderLocation, false, nullptr, ETeleportType::TeleportPhysics);
    UpdateVisibleSurfaceTiles(*Source, FocusSourcePoint, true);
    StatusMessage = bFocusedExactCenter
        ? FString::Printf(TEXT("Focused on surface center tile %s."), *TileId)
        : FString::Printf(TEXT("Surface center had no direct tile; focused nearest tile %s."), *TileId);
    return true;
}

bool UCSTopoSurveySubsystem::ValidateActiveCloudSurfaceAlignment(FCSTopoAlignmentReport& Report)
{
    Report = FCSTopoAlignmentReport();

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        Report.Message = TEXT("No active point cloud is selected.");
        LastAlignmentReport = Report;
        return false;
    }

    const FCSTopoDerivedSurfaceManifest* Surface = FindDerivedSurface(Source->SourceId);
    if (Surface == nullptr)
    {
        Report.Message = TEXT("No derived surface manifest is loaded for the active point cloud.");
        LastAlignmentReport = Report;
        return false;
    }

    Report.SourceBoundsMin = Source->BoundsMin;
    Report.SourceBoundsMax = Source->BoundsMax;
    Report.SurfaceBoundsMin = Surface->BoundsMin;
    Report.SurfaceBoundsMax = Surface->BoundsMax;
    Report.RenderUnitsPerSourceUnit = GetRenderUnitsPerSourceUnit(*Source);

    Report.MaxSourceBoundsDeviation = FMath::Max(
        MaxAbsVectorDelta(Report.SourceBoundsMin, Report.SurfaceBoundsMin),
        MaxAbsVectorDelta(Report.SourceBoundsMax, Report.SurfaceBoundsMax));

    const FVector ExpectedRenderMin = SourceToRenderPoint(*Source, Source->BoundsMin);
    const FVector ExpectedRenderMax = SourceToRenderPoint(*Source, Source->BoundsMax);
    if (const TObjectPtr<ALidarPointCloudActor>* ActorPtr = PointCloudActors.Find(Source->SourceId))
    {
        if (const ALidarPointCloudActor* Actor = ActorPtr->Get())
        {
            const FBox CloudBounds = Actor->GetComponentsBoundingBox(true);
            Report.CloudRenderBoundsMin = CloudBounds.Min;
            Report.CloudRenderBoundsMax = CloudBounds.Max;
            Report.MaxRenderBoundsDeviation = FMath::Max(
                MaxAbsVectorDelta(CloudBounds.Min, ExpectedRenderMin),
                MaxAbsVectorDelta(CloudBounds.Max, ExpectedRenderMax));
        }
    }

    const double SourceTolerance = FMath::Max(1.0, ActiveProject.SurfaceSettings.TileSizeSourceUnits * 0.01);
    const double JobRenderDiagonal = FVector::Distance(ExpectedRenderMin, ExpectedRenderMax);
    const double RenderTolerance = FMath::Max(Report.RenderUnitsPerSourceUnit * SourceTolerance, JobRenderDiagonal * 0.35);
    const bool bSourceBoundsAligned = Report.MaxSourceBoundsDeviation <= SourceTolerance;
    const bool bCloudBoundsUsable = Report.CloudRenderBoundsMin != FVector::ZeroVector || Report.CloudRenderBoundsMax != FVector::ZeroVector;
    const bool bCloudBoundsAligned = !bCloudBoundsUsable || Report.MaxRenderBoundsDeviation <= RenderTolerance;

    Report.bAligned = bSourceBoundsAligned;
    if (Report.bAligned)
    {
        const FString CloudNote = bCloudBoundsAligned
            ? FString::Printf(TEXT("cloud diagnostic %.0f cm"), Report.MaxRenderBoundsDeviation)
            : FString::Printf(TEXT("cloud render diagnostic %.0f cm; overlay kept optional"), Report.MaxRenderBoundsDeviation);
        Report.Message = FString::Printf(
            TEXT("Alignment OK | source/surface dev %.2f %s | %s"),
            Report.MaxSourceBoundsDeviation,
            *Source->LinearUnitName,
            *CloudNote);
    }
    else
    {
        Source->bCloudOverlayVisible = false;
        RefreshSurfacePresentation(*Source);
        Report.Message = FString::Printf(
            TEXT("Alignment warning | source/surface dev %.2f %s | cloud overlay hidden."),
            Report.MaxSourceBoundsDeviation,
            *Source->LinearUnitName);
    }

    LastAlignmentReport = Report;
    return Report.bAligned;
}

bool UCSTopoSurveySubsystem::SetCloudOverlayVisibleForActiveSource(bool bVisible, FString& StatusMessage)
{
    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        StatusMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    Source->bCloudOverlayVisible = bVisible;
    RefreshSurfacePresentation(*Source);
    SetLidarPointBudget(Source->bRuntimeWindowActive);
    StatusMessage = bVisible ? TEXT("Point cloud overlay shown.") : TEXT("Point cloud overlay hidden.");
    return true;
}

FString UCSTopoSurveySubsystem::GetActiveSurveyStatusLine() const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        return TEXT("No active point cloud.");
    }

    const FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source->SourceId);
    const int32 VisibleTileCount = Surface != nullptr ? Surface->RuntimeVisibleTileIds.Num() : 0;
    const FString AlignmentText = LastAlignmentReport.Message.IsEmpty()
        ? TEXT("alignment pending")
        : (LastAlignmentReport.bAligned ? TEXT("alignment OK") : TEXT("alignment warning"));
    return FString::Printf(
        TEXT("%s | %s | Surface %s | visible tiles %d | cloud overlay %s | %s"),
        *GetRuntimeNavigationStatusLine(),
        *RuntimeInputDebugLine,
        *SurfaceStateLabel(Source->SurfaceBuildState),
        VisibleTileCount,
        Source->bCloudOverlayVisible ? TEXT("on") : TEXT("off"),
        *AlignmentText);
}

FString UCSTopoSurveySubsystem::GetActiveFigureStatusLine() const
{
    const FString ActiveCode = NormalizeCode(ActiveProject.ActiveCode);
    if (ActiveCode.IsEmpty())
    {
        return TEXT("Enter a code to begin collecting linework.");
    }

    const FCSTopoFigureRecord* OpenFigure = ActiveProject.Figures.FindByPredicate([&ActiveCode](const FCSTopoFigureRecord& Candidate)
    {
        return Candidate.Code.Equals(ActiveCode, ESearchCase::IgnoreCase) && !Candidate.bClosed;
    });

    if (OpenFigure == nullptr)
    {
        return FString::Printf(TEXT("%s has no open figure. The next shot will start a new line."), *ActiveCode);
    }

    return FString::Printf(
        TEXT("%s line open | %d shot(s) | %s"),
        *ActiveCode,
        OpenFigure->PointNumbers.Num(),
        OpenFigure->bLoopClosed ? TEXT("loop") : TEXT("polyline"));
}

TArray<FCSTopoShotRecord> UCSTopoSurveySubsystem::GetRecentShots(int32 MaxShots) const
{
    TArray<FCSTopoShotRecord> RecentShots;
    if (MaxShots <= 0 || ActiveProject.Shots.IsEmpty())
    {
        return RecentShots;
    }

    const int32 StartIndex = FMath::Max(0, ActiveProject.Shots.Num() - MaxShots);
    for (int32 Index = ActiveProject.Shots.Num() - 1; Index >= StartIndex; --Index)
    {
        RecentShots.Add(ActiveProject.Shots[Index]);
    }

    return RecentShots;
}

bool UCSTopoSurveySubsystem::GetActiveSurveyBounds(FVector2D& OutMinSurveyNe, FVector2D& OutMaxSurveyNe) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        return false;
    }

    FVector BoundsMin = Source->BoundsMin;
    FVector BoundsMax = Source->BoundsMax;
    if (const FCSTopoDerivedSurfaceManifest* Surface = FindDerivedSurface(Source->SourceId))
    {
        BoundsMin = Surface->BoundsMin;
        BoundsMax = Surface->BoundsMax;
    }

    OutMinSurveyNe = FVector2D(BoundsMin.Y, BoundsMin.X);
    OutMaxSurveyNe = FVector2D(BoundsMax.Y, BoundsMax.X);
    return true;
}

bool UCSTopoSurveySubsystem::SurveyPointToRenderLocation(const FVector& SurveyPoint, FVector& RenderLocation) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        return false;
    }

    RenderLocation = SourceToRenderPoint(*Source, FVector(SurveyPoint.Y, SurveyPoint.X, SurveyPoint.Z));
    return true;
}

void UCSTopoSurveySubsystem::UpdateSurveyMapPose(const FVector& CameraRenderLocation, const FVector& ViewDirection)
{
    LastSurveyMapRenderLocation = CameraRenderLocation;
    LastSurveyMapViewDirection = ViewDirection;
    bHasSurveyMapPose = true;
}

bool UCSTopoSurveySubsystem::GetCurrentSurveyMapPose(FVector2D& OutSurveyNe, FVector2D& OutHeading) const
{
    if (!bHasSurveyMapPose)
    {
        return false;
    }

    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        return false;
    }

    const FVector SourcePoint = RenderToSourcePoint(*Source, LastSurveyMapRenderLocation);
    OutSurveyNe = FVector2D(SourcePoint.Y, SourcePoint.X);

    const FVector2D Heading2D(LastSurveyMapViewDirection.Y, LastSurveyMapViewDirection.X);
    OutHeading = Heading2D.GetSafeNormal();
    if (OutHeading.IsNearlyZero())
    {
        OutHeading = FVector2D(1.0, 0.0);
    }

    return true;
}

bool UCSTopoSurveySubsystem::GetCodeStyle(const FString& Code, FCSTopoCodeStyle& OutStyle) const
{
    const FString NormalizedCode = NormalizeCode(Code);
    const FCSTopoCodeStyle* Style = ActiveProject.CodePalette.FindByPredicate([&NormalizedCode](const FCSTopoCodeStyle& Candidate)
    {
        return Candidate.Code.Equals(NormalizedCode, ESearchCase::IgnoreCase);
    });

    if (Style == nullptr)
    {
        return false;
    }

    OutStyle = *Style;
    return true;
}

void UCSTopoSurveySubsystem::SetRuntimeNavigationState(
    ECSTopoNavigationMode Mode,
    int32 FlySpeedBandIndex,
    int32 FlySpeedBandCount,
    bool bPrecisionActive)
{
    RuntimeNavigationMode = Mode;
    RuntimeFlySpeedBandIndex = FlySpeedBandIndex;
    RuntimeFlySpeedBandCount = FlySpeedBandCount;
    bRuntimePrecisionActive = bPrecisionActive;
}

FString UCSTopoSurveySubsystem::GetRuntimeNavigationStatusLine() const
{
    FString Status = RuntimeNavigationMode == ECSTopoNavigationMode::Walk
        ? TEXT("Mode: Walk")
        : FString::Printf(TEXT("Mode: Fly x%d"), FMath::Max(RuntimeFlySpeedBandIndex + 1, 1));

    if (bRuntimePrecisionActive)
    {
        Status += TEXT(" | Precise");
    }

    return Status;
}

void UCSTopoSurveySubsystem::SetRuntimeInputDebugState(const FString& DebugLine)
{
    RuntimeInputDebugLine = DebugLine.IsEmpty() ? TEXT("Input: unknown | Last: none") : DebugLine;
}

void UCSTopoSurveySubsystem::SetHoverStatusLine(const FString& Message, bool bMeasurable, bool bUsesRawPoint)
{
    LastHoverStatusLine = Message.IsEmpty() ? TEXT("Hover: no measurement.") : Message;
    bLastHoverMeasurable = bMeasurable;
    bLastHoverUsesRawPoint = bUsesRawPoint;
}

FString UCSTopoSurveySubsystem::GetHoverStatusLine() const
{
    return LastHoverStatusLine;
}

bool UCSTopoSurveySubsystem::IsHoverMeasurable() const
{
    return bLastHoverMeasurable;
}

bool UCSTopoSurveySubsystem::DoesHoverUseRawPoint() const
{
    return bLastHoverUsesRawPoint;
}

bool UCSTopoSurveySubsystem::PreviewMeasurementAtView(const FVector& ViewOrigin, const FVector& ViewDirection, float TraceRadius, float SampleRadius, FCSTopoMeasurementPreview& Preview, bool bAllowExpensiveSearch) const
{
    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        Preview.Message = TEXT("No active point cloud is selected.");
        return false;
    }

    FString SurfaceError;
    FString SurfaceTileId;
    FVector SurfaceRenderHit = FVector::ZeroVector;
    if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready
        && TraceActiveDerivedSurface(*Source, ViewOrigin, ViewDirection, SurfaceRenderHit, SurfaceTileId, SurfaceError))
    {
        const FVector SourceXyz = RenderToSourcePoint(*Source, SurfaceRenderHit);
        Preview.bMeasurable = true;
        Preview.bUsesDerivedSurface = true;
        Preview.FitType = TEXT("DerivedSurface");
        Preview.SurfaceTileId = SurfaceTileId;
        Preview.RenderLocation = SurfaceRenderHit;
        Preview.SurveyNez = FVector(SourceXyz.Y, SourceXyz.X, SourceXyz.Z);
        Preview.Message = FString::Printf(
            TEXT("Hover: derived surface tile %s | N %.3f E %.3f Z %.3f %s"),
            *SurfaceTileId,
            Preview.SurveyNez.X,
            Preview.SurveyNez.Y,
            Preview.SurveyNez.Z,
            *Source->LinearUnitName);
        return true;
    }

    if (Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready && Source->bSurfacePrimary && !Source->bCloudOverlayVisible)
    {
        Preview.Message = SurfaceError.IsEmpty()
            ? TEXT("Hover: unmeasurable | reticle did not intersect a visible derived surface tile.")
            : FString::Printf(TEXT("Hover: unmeasurable | %s"), *SurfaceError);
        return false;
    }

    const FCSTopoPointCloudSource* RenderSource = nullptr;
    ALidarPointCloudActor* Actor = nullptr;
    FString ErrorMessage;
    if (!FindActiveRenderablePointCloud(RenderSource, Actor, ErrorMessage))
    {
        Preview.Message = Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Building
            ? TEXT("Hover: surface is still building. Fly mode is available until it is ready.")
            : ErrorMessage;
        return false;
    }

    ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();
    if (Component == nullptr)
    {
        Preview.Message = TEXT("Active point cloud has no render component.");
        return false;
    }

    const FVector Direction = ViewDirection.GetSafeNormal();
    TArray<FLidarPointCloudPoint> Hits;
    float ResolvedTraceRadius = FMath::Max(1.0f, TraceRadius);
    for (int32 Attempt = 0; Attempt < 5; ++Attempt)
    {
        Hits.Reset();
        Component->LineTraceMulti(ViewOrigin, Direction, ResolvedTraceRadius, true, true, Hits);
        if (!Hits.IsEmpty())
        {
            break;
        }
        ResolvedTraceRadius *= 2.0f;
    }

    Preview.ResolvedTraceRadius = RenderDistanceToSourceDistance(*RenderSource, ResolvedTraceRadius);
    FVector RenderHit = FVector::ZeroVector;
    bool bHasRenderHit = false;
    if (!Hits.IsEmpty())
    {
        RenderHit = FVector(Hits[0].Location);
        bHasRenderHit = true;
    }
    else if (bAllowExpensiveSearch)
    {
        FVector BoundsOrigin = FVector::ZeroVector;
        FVector BoundsExtent = FVector::ZeroVector;
        Actor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
        const float MaxDistance = FMath::Max(2000.0f, FVector::Distance(ViewOrigin, BoundsOrigin) + BoundsExtent.Size() * 2.0f);
        float SweepRadius = ResolvedTraceRadius;
        if (SweepPointCloudForVisibleHit(Component, ViewOrigin, Direction, ResolvedTraceRadius * 0.5f, MaxDistance, RenderHit, SweepRadius))
        {
            Preview.ResolvedTraceRadius = RenderDistanceToSourceDistance(*RenderSource, SweepRadius);
            bHasRenderHit = true;
        }
    }

    if (!bHasRenderHit)
    {
        Preview.Message = Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Building
            ? TEXT("Hover: surface is building and no cloud point is under the reticle yet.")
            : FString::Printf(TEXT("Hover: unmeasurable | no point-cloud hit within %.1f %s"), Preview.ResolvedTraceRadius, *RenderSource->LinearUnitName);
        return false;
    }

    const FVector SourceXyz = RenderToSourcePoint(*RenderSource, RenderHit);
    Preview.bMeasurable = true;
    Preview.bUsesRawPoint = true;
    Preview.FitType = TEXT("RawPoint");
    Preview.RenderLocation = RenderHit;
    Preview.SurveyNez = FVector(SourceXyz.Y, SourceXyz.X, SourceXyz.Z);
    Preview.Message = FString::Printf(TEXT("Hover: raw point-cloud point | N %.3f E %.3f Z %.3f %s"), Preview.SurveyNez.X, Preview.SurveyNez.Y, Preview.SurveyNez.Z, *RenderSource->LinearUnitName);
    return true;
}

void UCSTopoSurveySubsystem::RefreshSurfacePresentation(FCSTopoPointCloudSource& Source)
{
    const bool bSurfaceReadyPrimary = Source.bSurfacePrimary && Source.SurfaceBuildState == ECSTopoSurfaceBuildState::Ready;
    const bool bShowSurfaceActor = bSurfaceReadyPrimary && Source.bSurfaceRenderVisible;
    if (TObjectPtr<AActor>* SurfaceActor = SurfaceActors.Find(Source.SourceId))
    {
        if (SurfaceActor->Get() != nullptr)
        {
            SurfaceActor->Get()->SetActorHiddenInGame(!bShowSurfaceActor);
        }
    }

    if (FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source.SourceId))
    {
        const bool bRenderAllTiles = ShouldRenderAllSurfaceTiles(*Surface);
        for (FCSTopoLoadedSurfaceTile& Tile : Surface->Tiles)
        {
            if (Tile.MeshComponent == nullptr)
            {
                continue;
            }

            const bool bShowTile = bShowSurfaceActor && (bRenderAllTiles || Tile.bRuntimeVisible);
            Tile.MeshComponent->SetVisibility(bShowTile, true);
            Tile.MeshComponent->SetHiddenInGame(!bShowTile, true);
            Tile.MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        if (Surface->CollisionProxyComponent != nullptr)
        {
            const bool bEnableProxyCollision = bSurfaceReadyPrimary && !Surface->RuntimeVisibleTileIds.IsEmpty();
            Surface->CollisionProxyComponent->SetVisibility(false, true);
            Surface->CollisionProxyComponent->SetHiddenInGame(true, true);
            Surface->CollisionProxyComponent->SetCollisionEnabled(bEnableProxyCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        }
    }

    if (TObjectPtr<ALidarPointCloudActor>* CloudActor = PointCloudActors.Find(Source.SourceId))
    {
        if (CloudActor->Get() != nullptr)
        {
            const bool bShowCloud = Source.bVisible && (!Source.bSurfacePrimary || !Source.bSurfaceRenderVisible || Source.bCloudOverlayVisible || Source.SurfaceBuildState != ECSTopoSurfaceBuildState::Ready);
            CloudActor->Get()->SetActorHiddenInGame(!bShowCloud);
        }
    }
}

void UCSTopoSurveySubsystem::UpdateVisibleSurfaceTiles(FCSTopoPointCloudSource& Source, const FVector& SourceLocation, bool bForce)
{
    FCSTopoLoadedSurface* Surface = LoadedSurfaces.Find(Source.SourceId);
    if (Surface == nullptr || !Surface->IsReady())
    {
        return;
    }

    Surface->TileVisibilityRefreshCooldown = FMath::Max(0.0f, Surface->TileVisibilityRefreshCooldown);
    const double RefreshDistance = FMath::Max(ActiveProject.SurfaceSettings.TileSizeSourceUnits * 0.25, 25.0);
    if (!bForce
        && Surface->bHasVisibilitySourceLocation
        && SourceWindowDistance2D(SourceLocation, Surface->LastVisibilitySourceLocation) < RefreshDistance)
    {
        return;
    }

    const double VisibleRadius = FMath::Max(
        ActiveProject.SurfaceSettings.VisibleTileRadiusSourceUnits,
        ActiveProject.SurfaceSettings.TileSizeSourceUnits * 2.0) + (ActiveProject.SurfaceSettings.TileSizeSourceUnits * 0.15);
    Surface->RuntimeVisibleTileIds.Reset();

    for (FCSTopoLoadedSurfaceTile& Tile : Surface->Tiles)
    {
        const FVector TileCenter = (Tile.BoundsMin + Tile.BoundsMax) * 0.5;
        const double HalfDiagonal = FVector2D::Distance(FVector2D(Tile.BoundsMin.X, Tile.BoundsMin.Y), FVector2D(Tile.BoundsMax.X, Tile.BoundsMax.Y)) * 0.5;
        const double DistanceSq = FVector2D::DistSquared(FVector2D(SourceLocation.X, SourceLocation.Y), FVector2D(TileCenter.X, TileCenter.Y));
        const bool bTileVisible = DistanceSq <= FMath::Square(VisibleRadius + HalfDiagonal);
        Tile.bRuntimeVisible = bTileVisible;
        if (bTileVisible)
        {
            Surface->RuntimeVisibleTileIds.Add(Tile.TileId);
        }
    }

    Surface->LastVisibilitySourceLocation = SourceLocation;
    Surface->bHasVisibilitySourceLocation = true;
    RebuildSurfaceCollisionProxy(Source, *Surface, SourceLocation, bForce);
    RefreshSurfacePresentation(Source);
}

void UCSTopoSurveySubsystem::RebuildSurfaceCollisionProxy(const FCSTopoPointCloudSource& Source, FCSTopoLoadedSurface& Surface, const FVector& SourceLocation, bool bForce)
{
    if (Surface.CollisionProxyComponent == nullptr)
    {
        return;
    }

    const double CollisionRadius = FMath::Max(
        ActiveProject.SurfaceSettings.SurfaceCollisionRadiusSourceUnits,
        ActiveProject.SurfaceSettings.TileSizeSourceUnits * 0.5);
    const double CollisionRefreshDistance = FMath::Max(
        ActiveProject.SurfaceSettings.SurfaceCollisionRefreshDistanceSourceUnits,
        CollisionRadius * 0.25);
    if (!bForce
        && Surface.bHasCollisionProxySourceLocation
        && SourceWindowDistance2D(SourceLocation, Surface.LastCollisionProxySourceLocation) < CollisionRefreshDistance)
    {
        return;
    }

    TSet<FString> CandidateTileIds;
    const FVector2D Query(SourceLocation.X, SourceLocation.Y);
    for (const FCSTopoLoadedSurfaceTile& Tile : Surface.Tiles)
    {
        if (!Surface.RuntimeVisibleTileIds.Contains(Tile.TileId))
        {
            continue;
        }
        const FVector TileCenter = (Tile.BoundsMin + Tile.BoundsMax) * 0.5;
        const double HalfDiagonal = FVector2D::Distance(FVector2D(Tile.BoundsMin.X, Tile.BoundsMin.Y), FVector2D(Tile.BoundsMax.X, Tile.BoundsMax.Y)) * 0.5;
        const double DistanceSq = FVector2D::DistSquared(Query, FVector2D(TileCenter.X, TileCenter.Y));
        if (DistanceSq <= FMath::Square(CollisionRadius + HalfDiagonal))
        {
            CandidateTileIds.Add(Tile.TileId);
        }
    }

    if (!bForce
        && Surface.bHasCollisionProxySourceLocation
        && Surface.CollisionProxyTileIds.Num() == CandidateTileIds.Num())
    {
        bool bSameTileSet = true;
        for (const FString& TileId : CandidateTileIds)
        {
            if (!Surface.CollisionProxyTileIds.Contains(TileId))
            {
                bSameTileSet = false;
                break;
            }
        }
        if (bSameTileSet && SourceWindowDistance2D(SourceLocation, Surface.LastCollisionProxySourceLocation) < CollisionRadius * 0.5)
        {
            return;
        }
    }

    TArray<FVector> RenderVertices;
    TArray<int32> RenderTriangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TMap<FIntVector, int32> VertexIndexBySourceKey;

    auto AddVertex = [&](const FVector& SourceVertex) -> int32
    {
        const FIntVector Key(
            FMath::RoundToInt(SourceVertex.X / 0.01),
            FMath::RoundToInt(SourceVertex.Y / 0.01),
            FMath::RoundToInt(SourceVertex.Z / 0.01));
        if (const int32* Existing = VertexIndexBySourceKey.Find(Key))
        {
            return *Existing;
        }
        const int32 NewIndex = RenderVertices.Num();
        VertexIndexBySourceKey.Add(Key, NewIndex);
        RenderVertices.Add(SourceToRenderPoint(Source, SourceVertex));
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D::ZeroVector);
        Colors.Add(FLinearColor::Transparent);
        Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
        return NewIndex;
    };

    const double CollisionRadiusSq = FMath::Square(CollisionRadius);
    for (const FCSTopoLoadedSurfaceTile& Tile : Surface.Tiles)
    {
        if (!CandidateTileIds.Contains(Tile.TileId))
        {
            continue;
        }
        for (int32 Index = 0; Index + 2 < Tile.Triangles.Num(); Index += 3)
        {
            const int32 ASourceIndex = Tile.Triangles[Index];
            const int32 BSourceIndex = Tile.Triangles[Index + 1];
            const int32 CSourceIndex = Tile.Triangles[Index + 2];
            if (!Tile.SourceVertices.IsValidIndex(ASourceIndex) || !Tile.SourceVertices.IsValidIndex(BSourceIndex) || !Tile.SourceVertices.IsValidIndex(CSourceIndex))
            {
                continue;
            }
            const FVector& ASource = Tile.SourceVertices[ASourceIndex];
            const FVector& BSource = Tile.SourceVertices[BSourceIndex];
            const FVector& CSource = Tile.SourceVertices[CSourceIndex];
            const FVector2D A2(ASource.X, ASource.Y);
            const FVector2D B2(BSource.X, BSource.Y);
            const FVector2D C2(CSource.X, CSource.Y);
            const FVector2D Centroid = (A2 + B2 + C2) / 3.0;
            if (FVector2D::DistSquared(Query, Centroid) > CollisionRadiusSq
                && FVector2D::DistSquared(Query, A2) > CollisionRadiusSq
                && FVector2D::DistSquared(Query, B2) > CollisionRadiusSq
                && FVector2D::DistSquared(Query, C2) > CollisionRadiusSq)
            {
                continue;
            }
            const int32 AIndex = AddVertex(ASource);
            const int32 BIndex = AddVertex(BSource);
            const int32 CIndex = AddVertex(CSource);
            RenderTriangles.Add(AIndex);
            RenderTriangles.Add(CIndex);
            RenderTriangles.Add(BIndex);
        }
    }

    Surface.CollisionProxyComponent->ClearAllMeshSections();
    if (RenderVertices.Num() >= 3 && RenderTriangles.Num() >= 3)
    {
        Surface.CollisionProxyComponent->CreateMeshSection_LinearColor(0, RenderVertices, RenderTriangles, Normals, UVs, Colors, Tangents, true);
    }
    Surface.CollisionProxyTileIds = CandidateTileIds;
    Surface.LastCollisionProxySourceLocation = SourceLocation;
    Surface.bHasCollisionProxySourceLocation = true;
}

bool UCSTopoSurveySubsystem::ShouldUseSurfaceTileForRuntimeQuery(const FCSTopoLoadedSurface& Surface, const FCSTopoLoadedSurfaceTile& Tile) const
{
    return Surface.RuntimeVisibleTileIds.IsEmpty() || Surface.RuntimeVisibleTileIds.Contains(Tile.TileId);
}

void UCSTopoSurveySubsystem::RefreshSurfaceBuildProgress(FCSTopoPointCloudSource& Source)
{
    if (SurfaceBuildProgressPath.IsEmpty() || !FPaths::FileExists(SurfaceBuildProgressPath))
    {
        return;
    }

    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *SurfaceBuildProgressPath))
    {
        return;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return;
    }

    double Progress = Source.SurfaceBuildProgress;
    Root->TryGetNumberField(TEXT("progress"), Progress);
    FString Stage = Source.SurfaceBuildProgressStage;
    Root->TryGetStringField(TEXT("stage"), Stage);
    FString Message = Source.SurfaceBuildProgressMessage;
    Root->TryGetStringField(TEXT("message"), Message);

    Source.SurfaceBuildProgress = FMath::Clamp(static_cast<float>(Progress), 0.0f, 1.0f);
    Source.SurfaceBuildProgressStage = Stage;
    Source.SurfaceBuildProgressMessage = Message;

    const int32 Percent = FMath::Clamp(FMath::RoundToInt(Source.SurfaceBuildProgress * 100.0f), 0, 100);
    const FString StagePrefix = Stage.IsEmpty() ? TEXT("Building surface") : Stage;
    Source.SurfaceStatus = Message.IsEmpty()
        ? FString::Printf(TEXT("Surface build %d%% | %s"), Percent, *StagePrefix)
        : FString::Printf(TEXT("Surface build %d%% | %s | %s"), Percent, *StagePrefix, *Message);
}

void UCSTopoSurveySubsystem::RefreshSurfaceBuildProcess()
{
    if (!SurfaceBuildProcessHandle.IsValid())
    {
        return;
    }

    if (FCSTopoPointCloudSource* Source = FindPointCloudMutable(SurfaceBuildProcessSourceId))
    {
        RefreshSurfaceBuildProgress(*Source);
    }

    if (FPlatformProcess::IsProcRunning(SurfaceBuildProcessHandle))
    {
        return;
    }

    int32 ReturnCode = INDEX_NONE;
    FPlatformProcess::GetProcReturnCode(SurfaceBuildProcessHandle, &ReturnCode);
    FPlatformProcess::CloseProc(SurfaceBuildProcessHandle);
    SurfaceBuildProcessHandle = FProcHandle();

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(SurfaceBuildProcessSourceId);
    if (Source == nullptr)
    {
        SurfaceBuildProcessSourceId.Empty();
        SurfaceBuildManifestPath.Empty();
        SurfaceBuildProgressPath.Empty();
        return;
    }

    RefreshSurfaceBuildProgress(*Source);
    Source->SurfaceManifestPath = SurfaceBuildManifestPath;
    if (ReturnCode == 0)
    {
        FString LoadMessage;
        if (!LoadDerivedSurfaceForSource(*Source, LoadMessage))
        {
            Source->SurfaceBuildState = ECSTopoSurfaceBuildState::Failed;
            Source->SurfaceStatus = LoadMessage;
        }
    }
    else
    {
        Source->SurfaceBuildState = ECSTopoSurfaceBuildState::Failed;
        Source->SurfaceStatus = Source->SurfaceBuildProgressMessage.IsEmpty()
            ? FString::Printf(TEXT("Surface build failed (process %d)."), ReturnCode)
            : FString::Printf(TEXT("Surface build failed (process %d). %s"), ReturnCode, *Source->SurfaceBuildProgressMessage);
        Source->SurfaceBuildProgress = 1.0f;
        Source->SurfaceBuildProgressStage = TEXT("Failed");
    }

    SurfaceBuildProcessSourceId.Empty();
    SurfaceBuildManifestPath.Empty();
    SurfaceBuildProgressPath.Empty();
}

bool UCSTopoSurveySubsystem::AdjustActivePointCloudPointSize(float Delta, float& NewPointSize, FString& ErrorMessage)
{
    FCSTopoPointCloudSource* Source = nullptr;
    ALidarPointCloudActor* Actor = nullptr;
    if (!FindActiveRenderablePointCloud(Source, Actor, ErrorMessage))
    {
        return false;
    }

    ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();
    if (Component == nullptr)
    {
        ErrorMessage = TEXT("Active point cloud has no render component.");
        return false;
    }

    const float CurrentPointSize = Component->PointSize;
    NewPointSize = FMath::Clamp(CurrentPointSize + Delta, 0.0f, 0.35f);
    if (NewPointSize < 0.025f)
    {
        NewPointSize = 0.0f;
    }
    Component->ScalingMethod = ELidarPointCloudScalingMethod::FixedScreenSize;
    Component->PointSize = NewPointSize;
    Component->PointSizeBias = 0.0f;
    Component->GapFillingStrength = 0.0f;
    ErrorMessage.Empty();
    return true;
}

bool UCSTopoSurveySubsystem::GetActivePointCloudPointSize(float& PointSize, FString& ErrorMessage) const
{
    const FCSTopoPointCloudSource* Source = nullptr;
    ALidarPointCloudActor* Actor = nullptr;
    if (!FindActiveRenderablePointCloud(Source, Actor, ErrorMessage))
    {
        return false;
    }

    const ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();
    if (Component == nullptr)
    {
        ErrorMessage = TEXT("Active point cloud has no render component.");
        return false;
    }

    PointSize = Component->PointSize;
    ErrorMessage.Empty();
    return true;
}

bool UCSTopoSurveySubsystem::CollectTopoShotFromView(const FVector& ViewOrigin, const FVector& ViewDirection, float TraceRadius, float SampleRadius, FCSTopoShotRecord& Shot, FVector& RenderLocation, FString& ErrorMessage)
{
    auto IsDuplicateAgainstOpenFigure = [this](const FVector& CandidateNez, const FString& Code, double ToleranceSourceUnits, FCSTopoShotRecord& ExistingShot) -> bool
    {
        const FCSTopoFigureRecord* OpenFigure = nullptr;
        for (const FCSTopoFigureRecord& Figure : ActiveProject.Figures)
        {
            if (Figure.Code.Equals(Code, ESearchCase::IgnoreCase) && !Figure.bClosed)
            {
                OpenFigure = &Figure;
                break;
            }
        }

        if (OpenFigure == nullptr || OpenFigure->PointNumbers.IsEmpty())
        {
            return false;
        }

        const int32 LastPointNumber = OpenFigure->PointNumbers.Last();
        const FCSTopoShotRecord* LastShot = ActiveProject.Shots.FindByPredicate([LastPointNumber](const FCSTopoShotRecord& Candidate)
        {
            return Candidate.PointNumber == LastPointNumber;
        });

        if (LastShot == nullptr)
        {
            return false;
        }

        const FVector LastNez(LastShot->Northing, LastShot->Easting, LastShot->Elevation);
        if (FVector::DistSquared(LastNez, CandidateNez) <= FMath::Square(ToleranceSourceUnits))
        {
            ExistingShot = *LastShot;
            return true;
        }

        return false;
    };

    FCSTopoMeasurementPreview Preview;
    if (!PreviewMeasurementAtView(ViewOrigin, ViewDirection, TraceRadius, SampleRadius, Preview))
    {
        ErrorMessage = Preview.Message;
        return false;
    }

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        ErrorMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    const FString BaseCode = NormalizeCode(ActiveProject.ActiveCode);
    const FString MeasurementCode = BuildNextMeasurementCode();
    const FString MeasurementControlCode = PendingControlCode;
    const FString MeasurementControlParameter = PendingControlParameter;
    const bool bStartsNewLine = MeasurementControlCode.Equals(TEXT("ST"), ESearchCase::IgnoreCase) || MeasurementControlCode.Equals(TEXT("RECT"), ESearchCase::IgnoreCase);
    const bool bJoinLinework = !MeasurementControlCode.Equals(TEXT("IG"), ESearchCase::IgnoreCase);

    if (Preview.bUsesDerivedSurface)
    {
        const FVector ShotNez = Preview.SurveyNez;
        FCSTopoShotRecord ExistingShot;
        if (bJoinLinework && !bStartsNewLine && IsDuplicateAgainstOpenFigure(ShotNez, BaseCode, 0.02, ExistingShot))
        {
            ErrorMessage = FString::Printf(
                TEXT("Reticle is still on the previous shot location. Move the reticle to continue linework. Last shot: %d %s"),
                ExistingShot.PointNumber,
                *ExistingShot.Code);
            return false;
        }

        ApplyControlBeforeShot(BaseCode, MeasurementControlCode);
        Shot = UCSTopoProjectLibrary::AddFittedShot(ActiveProject, MeasurementCode, ShotNez.X, ShotNez.Y, ShotNez.Z, 0.0, Source->SourceId, MeasurementControlParameter, bJoinLinework);
        RenderLocation = Preview.RenderLocation;

        if (FCSTopoShotRecord* StoredShot = ActiveProject.Shots.FindByPredicate([PointNumber = Shot.PointNumber](const FCSTopoShotRecord& Candidate)
        {
            return Candidate.PointNumber == PointNumber;
        }))
        {
            StoredShot->ViewLocation = ViewOrigin;
            StoredShot->FitType = TEXT("DerivedSurface");
            StoredShot->MeasurementSource = ECSTopoMeasurementSource::DerivedSurface;
            StoredShot->SurfaceId = Source->SurfaceId;
            StoredShot->SurfaceTileId = Preview.SurfaceTileId;
            StoredShot->SurfaceMethod = TEXT("TriangleBarycentric");
            StoredShot->AuditJson = FString::Printf(
                TEXT("{\"measurementSource\":\"DerivedSurface\",\"surfaceId\":\"%s\",\"surfaceTileId\":\"%s\",\"surfaceMethod\":\"TriangleBarycentric\",\"renderHit\":[%.3f,%.3f,%.3f],\"sourceXyz\":[%.3f,%.3f,%.3f],\"linearUnit\":\"%s\",\"linearUnitToMeters\":%.12f}"),
                *Source->SurfaceId,
                *Preview.SurfaceTileId,
                Preview.RenderLocation.X,
                Preview.RenderLocation.Y,
                Preview.RenderLocation.Z,
                ShotNez.Y,
                ShotNez.X,
                ShotNez.Z,
                *Source->LinearUnitName,
                Source->LinearUnitToMeters);
            Shot = *StoredShot;
        }

        ApplyControlAfterShot(Shot);
        ErrorMessage = FString::Printf(
            TEXT("Shot %d %s N %.3f E %.3f Z %.3f %s (DerivedSurface %s)"),
            Shot.PointNumber,
            *Shot.Code,
            Shot.Northing,
            Shot.Easting,
            Shot.Elevation,
            *Source->LinearUnitName,
            *Preview.SurfaceTileId);
        return true;
    }

    ALidarPointCloudActor* Actor = nullptr;
    FString ActorError;
    if (!FindActiveRenderablePointCloud(Source, Actor, ActorError))
    {
        ErrorMessage = ActorError;
        return false;
    }

    ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();
    if (Component == nullptr)
    {
        ErrorMessage = TEXT("Active point cloud has no render component.");
        return false;
    }

    const FVector RenderHit = Preview.RenderLocation;
    TArray<FLidarPointCloudPoint> Neighborhood;
    float ResolvedSampleRadius = FMath::Max(1.0f, SampleRadius);
    for (int32 Attempt = 0; Attempt < 6; ++Attempt)
    {
        Neighborhood.Reset();
        Component->GetPointsInSphereAsCopies(Neighborhood, FSphere(RenderHit, ResolvedSampleRadius), true, true);
        if (Neighborhood.Num() >= 3 || Preview.bUsesRawPoint)
        {
            break;
        }
        ResolvedSampleRadius *= 2.0f;
    }

    auto RenderToSurveyNez = [Source](const FVector& RenderPoint)
    {
        const FVector SourceXyz = RenderToSourcePoint(*Source, RenderPoint);
        return FVector(SourceXyz.Y, SourceXyz.X, SourceXyz.Z);
    };

    TArray<FVector> SurveySamples;
    SurveySamples.Reserve(Neighborhood.Num());
    for (const FLidarPointCloudPoint& Point : Neighborhood)
    {
        SurveySamples.Add(RenderToSurveyNez(FVector(Point.Location)));
    }

    const FVector ShotNez = RenderToSurveyNez(RenderHit);
    FCSTopoShotRecord ExistingShot;
    if (bJoinLinework && !bStartsNewLine && IsDuplicateAgainstOpenFigure(ShotNez, BaseCode, 0.02, ExistingShot))
    {
        ErrorMessage = FString::Printf(
            TEXT("Reticle is still on the previous shot location. Move the reticle to continue linework. Last shot: %d %s"),
            ExistingShot.PointNumber,
            *ExistingShot.Code);
        return false;
    }

    bool bShotAdded = false;
    FString ResolvedFitType = TEXT("NearestPoint");
    ApplyControlBeforeShot(BaseCode, MeasurementControlCode);
    if (Neighborhood.Num() >= 3)
    {
        bool bFitSucceeded = false;
        Shot = AddFittedShotFromSamples(MeasurementCode, ShotNez.X, ShotNez.Y, SurveySamples, Source->SourceId, bFitSucceeded, MeasurementControlParameter, bJoinLinework);
        if (bFitSucceeded)
        {
            bShotAdded = true;
            ResolvedFitType = Shot.FitType;
        }
    }

    if (!bShotAdded)
    {
        Shot = UCSTopoProjectLibrary::AddFittedShot(ActiveProject, MeasurementCode, ShotNez.X, ShotNez.Y, ShotNez.Z, 0.0, Source->SourceId, MeasurementControlParameter, bJoinLinework);
        ResolvedFitType = TEXT("NearestPoint");
    }

    RenderLocation = SourceToRenderPoint(*Source, FVector(Shot.Easting, Shot.Northing, Shot.Elevation));

    if (FCSTopoShotRecord* StoredShot = ActiveProject.Shots.FindByPredicate([PointNumber = Shot.PointNumber](const FCSTopoShotRecord& Candidate)
    {
        return Candidate.PointNumber == PointNumber;
    }))
    {
        StoredShot->ViewLocation = ViewOrigin;
        StoredShot->FitType = ResolvedFitType;
        StoredShot->MeasurementSource = Neighborhood.Num() >= 3 ? ECSTopoMeasurementSource::InterpolatedPoints : ECSTopoMeasurementSource::RawPoint;
        StoredShot->AuditJson = FString::Printf(
            TEXT("{\"measurementSource\":\"%s\",\"renderHit\":[%.3f,%.3f,%.3f],\"sampleCount\":%d,\"traceRadius\":%.3f,\"sampleRadius\":%.3f,\"linearUnit\":\"%s\",\"linearUnitToMeters\":%.12f}"),
            *MeasurementSourceLabel(StoredShot->MeasurementSource),
            RenderHit.X,
            RenderHit.Y,
            RenderHit.Z,
            Neighborhood.Num(),
            Preview.ResolvedTraceRadius,
            RenderDistanceToSourceDistance(*Source, ResolvedSampleRadius),
            *Source->LinearUnitName,
            Source->LinearUnitToMeters);
        Shot = *StoredShot;
    }

    ApplyControlAfterShot(Shot);
    ErrorMessage = FString::Printf(
        TEXT("Shot %d %s N %.3f E %.3f Z %.3f %s (%s, %d samples)"),
        Shot.PointNumber,
        *Shot.Code,
        Shot.Northing,
        Shot.Easting,
        Shot.Elevation,
        *Source->LinearUnitName,
        *Shot.FitType,
        Neighborhood.Num());
    return true;
}

bool UCSTopoSurveySubsystem::SpawnPointCloudActor(FCSTopoPointCloudSource& Source, FString& ErrorMessage)
{
    UCSTopoPointCloudImport::RefreshSourceRuntimeState(Source, ErrorMessage);
    const FString DisplayPath = (Source.bRuntimeWindowActive && FPaths::FileExists(Source.RuntimeWindowPath))
        ? Source.RuntimeWindowPath
        : (!Source.RuntimeDisplayPath.IsEmpty() ? Source.RuntimeDisplayPath : Source.SourcePath);

    if (TObjectPtr<ALidarPointCloudActor>* ExistingActor = PointCloudActors.Find(Source.SourceId))
    {
        const FString* ExistingDisplayPath = PointCloudActorDisplayPaths.Find(Source.SourceId);
        if (ExistingActor->Get() != nullptr && ExistingDisplayPath != nullptr && ExistingDisplayPath->Equals(DisplayPath, ESearchCase::IgnoreCase))
        {
            ExistingActor->Get()->SetActorHiddenInGame(!Source.bVisible);
            return true;
        }

        if (ExistingActor->Get() != nullptr)
        {
            ExistingActor->Get()->Destroy();
        }
        PointCloudActors.Remove(Source.SourceId);
        PointCloudActorDisplayPaths.Remove(Source.SourceId);
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        ErrorMessage = TEXT("World is not available.");
        return false;
    }

    if (!FPaths::FileExists(DisplayPath))
    {
        ErrorMessage = FString::Printf(TEXT("Point cloud file does not exist: %s"), *DisplayPath);
        return false;
    }

    const FString Extension = FPaths::GetExtension(DisplayPath).ToLower();
    if (Extension != TEXT("las") && Extension != TEXT("laz"))
    {
        ErrorMessage = FString::Printf(TEXT("LiDAR display currently supports LAS/LAZ files. Cannot display: %s"), *DisplayPath);
        return false;
    }

    const FName AssetName(*FString::Printf(TEXT("CSTopo_%s"), *Source.SourceId.Left(8)));
    ULidarPointCloud* PointCloud = ULidarPointCloud::CreateFromFile(
        DisplayPath,
        FLidarPointCloudAsyncParameters(false),
        nullptr,
        GetTransientPackage(),
        AssetName,
        RF_Transient);

    if (PointCloud == nullptr)
    {
        ErrorMessage = FString::Printf(TEXT("Failed to import point cloud for display: %s"), *DisplayPath);
        return false;
    }

    // Keep the prototype render near the Unreal origin; source NEZ stays in the CSTopo records.
    PointCloud->CenterPoints();
    const FVector DisplayCenterSource = Source.bRuntimeWindowActive ? Source.RuntimeWindowCenter : GetSourceCenter(Source);

    ALidarPointCloudActor* Actor = World->SpawnActor<ALidarPointCloudActor>(
        ALidarPointCloudActor::StaticClass(),
        SourceToRenderPoint(Source, DisplayCenterSource),
        FRotator::ZeroRotator);
    if (Actor == nullptr)
    {
        ErrorMessage = TEXT("Failed to spawn point-cloud actor.");
        return false;
    }

#if WITH_EDITOR
    Actor->SetActorLabel(FString::Printf(TEXT("CSTopo_%s"), *FPaths::GetBaseFilename(DisplayPath)));
#endif
    Actor->SetActorEnableCollision(false);
    Actor->SetPointCloud(PointCloud);
    Actor->SetActorScale3D(FVector(static_cast<float>(GetRenderUnitsPerSourceUnit(Source))));

    if (ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent())
    {
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->ScalingMethod = ELidarPointCloudScalingMethod::FixedScreenSize;
        Component->PointSize = 0.0f;
        Component->PointSizeBias = 0.0f;
        Component->GapFillingStrength = 0.0f;
    }

    PointCloudActors.Add(Source.SourceId, Actor);
    PointCloudActorDisplayPaths.Add(Source.SourceId, DisplayPath);
    SetLidarPointBudget(Source.bRuntimeWindowActive);

    Source.bLoaded = true;
    Source.bVisible = true;
    RefreshPointCloudViewMode(Source);
    RefreshSurfacePresentation(Source);
    ErrorMessage = Source.CacheStatus;
    return true;
}

bool UCSTopoSurveySubsystem::FindActiveRenderablePointCloud(FCSTopoPointCloudSource*& Source, ALidarPointCloudActor*& Actor, FString& ErrorMessage)
{
    Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        ErrorMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    if (!Source->bLoaded || !Source->bVisible)
    {
        ErrorMessage = TEXT("Active point cloud is not loaded and visible.");
        return false;
    }

    TObjectPtr<ALidarPointCloudActor>* ActorPtr = PointCloudActors.Find(Source->SourceId);
    Actor = ActorPtr != nullptr ? ActorPtr->Get() : nullptr;
    if (Actor == nullptr)
    {
        ErrorMessage = TEXT("Active point cloud has no display actor. Load or import it first.");
        return false;
    }

    return true;
}

bool UCSTopoSurveySubsystem::FindActiveRenderablePointCloud(const FCSTopoPointCloudSource*& Source, ALidarPointCloudActor*& Actor, FString& ErrorMessage) const
{
    Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        ErrorMessage = TEXT("No active point cloud is selected.");
        return false;
    }

    if (!Source->bLoaded || !Source->bVisible)
    {
        ErrorMessage = TEXT("Active point cloud is not loaded and visible.");
        return false;
    }

    const TObjectPtr<ALidarPointCloudActor>* ActorPtr = PointCloudActors.Find(Source->SourceId);
    Actor = ActorPtr != nullptr ? ActorPtr->Get() : nullptr;
    if (Actor == nullptr)
    {
        ErrorMessage = TEXT("Active point cloud has no display actor. Load or import it first.");
        return false;
    }

    return true;
}

FCSTopoPointCloudSource* UCSTopoSurveySubsystem::FindPointCloudMutable(const FString& SourceId)
{
    return ActiveProject.PointClouds.FindByPredicate([&SourceId](const FCSTopoPointCloudSource& Source)
    {
        return Source.SourceId == SourceId;
    });
}

const FCSTopoPointCloudSource* UCSTopoSurveySubsystem::FindPointCloud(const FString& SourceId) const
{
    return ActiveProject.PointClouds.FindByPredicate([&SourceId](const FCSTopoPointCloudSource& Source)
    {
        return Source.SourceId == SourceId;
    });
}

FCSTopoDerivedSurfaceManifest* UCSTopoSurveySubsystem::FindDerivedSurfaceMutable(const FString& SourceId)
{
    return ActiveProject.DerivedSurfaces.FindByPredicate([&SourceId](const FCSTopoDerivedSurfaceManifest& Surface)
    {
        return Surface.SourceCloudId == SourceId;
    });
}

const FCSTopoDerivedSurfaceManifest* UCSTopoSurveySubsystem::FindDerivedSurface(const FString& SourceId) const
{
    return ActiveProject.DerivedSurfaces.FindByPredicate([&SourceId](const FCSTopoDerivedSurfaceManifest& Surface)
    {
        return Surface.SourceCloudId == SourceId;
    });
}

FString UCSTopoSurveySubsystem::GetProjectCacheDirectory() const
{
    if (!CurrentProjectPath.IsEmpty())
    {
        return FPaths::Combine(FPaths::GetPath(CurrentProjectPath), TEXT("cache"));
    }

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("cache"));
}

void UCSTopoSurveySubsystem::UpdateRuntimeStreaming(const FVector& CameraRenderLocation, const FVector& ViewDirection, float DeltaSeconds)
{
    RuntimeWindowUpdateCooldown = FMath::Max(0.0f, RuntimeWindowUpdateCooldown - DeltaSeconds);
    RefreshSurfaceBuildProcess();
    RefreshRuntimeWindowProcess();

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(ActiveProject.ActivePointCloudId);
    if (Source == nullptr || !Source->bLoaded || !Source->bVisible)
    {
        return;
    }

    const FVector CameraSourcePoint = RenderToSourcePoint(*Source, CameraRenderLocation);
    UpdateVisibleSurfaceTiles(*Source, CameraSourcePoint, false);

    if (!ShouldUseCopcWindowStreaming(*Source))
    {
        if (Source->bRuntimeWindowActive || Source->bRuntimeWindowPending)
        {
            Source->bRuntimeWindowActive = false;
            Source->bRuntimeWindowPending = false;
            Source->RuntimeWindowStatus = TEXT("Runtime window inactive. Using project runtime path.");
            Source->RuntimeWindowPath.Empty();
            FString SpawnMessage;
            SpawnPointCloudActor(*Source, SpawnMessage);
        }
        return;
    }

    if (RuntimeWindowProcessHandle.IsValid() || RuntimeWindowUpdateCooldown > 0.0f)
    {
        return;
    }

    if (Source->bRuntimeWindowActive
        && Source->RuntimeWindowRadius > 0.0
        && SourceWindowDistance2D(CameraSourcePoint, Source->RuntimeWindowCenter) <= ActiveProject.RuntimeStreaming.WindowRefreshDistanceSourceUnits)
    {
        return;
    }

    FString ErrorMessage;
    if (StartRuntimeWindowUpdate(*Source, CameraSourcePoint, ErrorMessage))
    {
        RuntimeWindowUpdateCooldown = ActiveProject.RuntimeStreaming.UpdateIntervalSeconds;
    }
}

bool UCSTopoSurveySubsystem::ShouldUseCopcWindowStreaming(const FCSTopoPointCloudSource& Source) const
{
    return ActiveProject.RuntimeStreaming.bEnableCopcWindowStreaming
        && Source.CacheFormat == ECSTopoPointCloudCacheFormat::COPC
        && Source.bCachePreferredForRuntime
        && !Source.bDirectOpenEligible
        && Source.bCacheExists
        && Source.CachePath.EndsWith(TEXT(".copc.laz"), ESearchCase::IgnoreCase);
}

void UCSTopoSurveySubsystem::RefreshRuntimeWindowProcess()
{
    if (!RuntimeWindowProcessHandle.IsValid())
    {
        return;
    }

    if (FPlatformProcess::IsProcRunning(RuntimeWindowProcessHandle))
    {
        return;
    }

    int32 ReturnCode = INDEX_NONE;
    FPlatformProcess::GetProcReturnCode(RuntimeWindowProcessHandle, &ReturnCode);
    FPlatformProcess::CloseProc(RuntimeWindowProcessHandle);
    RuntimeWindowProcessHandle = FProcHandle();

    FCSTopoPointCloudSource* Source = FindPointCloudMutable(RuntimeWindowProcessSourceId);
    if (Source == nullptr)
    {
        RuntimeWindowProcessSourceId.Empty();
        RuntimeWindowProcessPipelinePath.Empty();
        RuntimeWindowProcessOutputPath.Empty();
        return;
    }

    Source->bRuntimeWindowPending = false;

    const int64 OutputSize = FPaths::FileExists(RuntimeWindowProcessOutputPath)
        ? IFileManager::Get().FileSize(*RuntimeWindowProcessOutputPath)
        : -1;

    if (ReturnCode == 0 && OutputSize > 512)
    {
        FCSTopoLasHeaderInfo WindowHeader;
        FString HeaderError;
        if (UCSTopoPointCloudImport::ReadLasHeader(RuntimeWindowProcessOutputPath, WindowHeader, HeaderError))
        {
            Source->RuntimeWindowCenter = (WindowHeader.BoundsMin + WindowHeader.BoundsMax) * 0.5;
        }

        Source->bRuntimeWindowActive = true;
        Source->RuntimeWindowPath = RuntimeWindowProcessOutputPath;
        Source->RuntimeWindowStatus = FString::Printf(
            TEXT("Runtime window ready: %s | center E %.2f N %.2f | radius %.1f | sample %.3f"),
            *FPaths::GetCleanFilename(RuntimeWindowProcessOutputPath),
            Source->RuntimeWindowCenter.X,
            Source->RuntimeWindowCenter.Y,
            Source->RuntimeWindowRadius,
            Source->RuntimeWindowSampleSpacing);

        if (Source->bLoaded)
        {
            if (TObjectPtr<ALidarPointCloudActor>* ExistingActor = PointCloudActors.Find(Source->SourceId))
            {
                if (ExistingActor->Get() != nullptr)
                {
                    ExistingActor->Get()->Destroy();
                }
                PointCloudActors.Remove(Source->SourceId);
                PointCloudActorDisplayPaths.Remove(Source->SourceId);
            }

            FString SpawnMessage;
            SpawnPointCloudActor(*Source, SpawnMessage);
        }
    }
    else
    {
        Source->RuntimeWindowStatus = ReturnCode == 0
            ? TEXT("Runtime window query returned no nearby COPC points. Keeping previous display.")
            : FString::Printf(TEXT("Runtime window update failed (PDAL %d). Keeping previous display."), ReturnCode);
    }

    if (!RuntimeWindowProcessPipelinePath.IsEmpty())
    {
        IFileManager::Get().Delete(*RuntimeWindowProcessPipelinePath, false, true);
    }

    RuntimeWindowProcessSourceId.Empty();
    RuntimeWindowProcessPipelinePath.Empty();
    RuntimeWindowProcessOutputPath.Empty();
}

void UCSTopoSurveySubsystem::RefreshUserTinPreviewIfNeeded()
{
    if (!bUserTinVisible)
    {
        return;
    }

    if (bUserTinDirty
        || LastUserTinShotCount != ActiveProject.Shots.Num()
        || LastUserTinFigureCount != ActiveProject.Figures.Num())
    {
        FString StatusMessage;
        RebuildUserTinPreview(StatusMessage);
    }
}

void UCSTopoSurveySubsystem::DestroyUserTinPreview()
{
    if (UserTinActor != nullptr)
    {
        UserTinActor->Destroy();
    }
    UserTinActor = nullptr;
    UserTinMeshComponent = nullptr;
    bUserTinVisible = false;
    bUserTinDirty = true;
    LastUserTinShotCount = INDEX_NONE;
    LastUserTinFigureCount = INDEX_NONE;
    UserTinStatusLine = TEXT("User TIN hidden.");
}

bool UCSTopoSurveySubsystem::RebuildUserTinPreview(FString& StatusMessage)
{
    LastUserTinShotCount = ActiveProject.Shots.Num();
    LastUserTinFigureCount = ActiveProject.Figures.Num();
    bUserTinDirty = false;

    const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
    if (Source == nullptr)
    {
        UserTinStatusLine = TEXT("User TIN needs an active point cloud.");
        StatusMessage = UserTinStatusLine;
        return false;
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        UserTinStatusLine = TEXT("World is not available for User TIN rendering.");
        StatusMessage = UserTinStatusLine;
        return false;
    }

    TArray<FVector> RenderVertices;
    TArray<FVector2d> TinVertices;
    TArray<FLinearColor> Colors;
    TMap<int32, int32> VertexIndexByPointNumber;
    int32 BreaklineVertexCount = 0;

    for (const FCSTopoShotRecord& Shot : ActiveProject.Shots)
    {
        if (!Shot.SourceCloudId.IsEmpty() && Shot.SourceCloudId != Source->SourceId)
        {
            continue;
        }

        const FString BaseCode = Shot.BaseCode.IsEmpty() ? Shot.Code : Shot.BaseCode;
        FCSTopoCodeStyle Style;
        if (!GetCodeStyle(BaseCode, Style))
        {
            continue;
        }

        const FString PointType = Style.PointType;
        if (!DoesCSTopoPointTypeContributeToUserTin(PointType))
        {
            continue;
        }

        const int32 VertexIndex = RenderVertices.Num();
        VertexIndexByPointNumber.Add(Shot.PointNumber, VertexIndex);
        RenderVertices.Add(SourceToRenderPoint(*Source, FVector(Shot.Easting, Shot.Northing, Shot.Elevation)));
        TinVertices.Add(FVector2d(Shot.Easting, Shot.Northing));
        Colors.Add(DoesCSTopoPointTypeCreateTinBreakline(PointType)
            ? FLinearColor(1.0f, 0.52f, 0.08f, 1.0f)
            : FLinearColor(0.94f, 0.86f, 0.28f, 1.0f));
        if (DoesCSTopoPointTypeCreateTinBreakline(PointType))
        {
            ++BreaklineVertexCount;
        }
    }

    if (RenderVertices.Num() < 3)
    {
        if (UserTinMeshComponent != nullptr)
        {
            UserTinMeshComponent->ClearAllMeshSections();
        }
        UserTinStatusLine = FString::Printf(TEXT("User TIN needs at least 3 triangulatable shots. Current: %d."), RenderVertices.Num());
        StatusMessage = UserTinStatusLine;
        return false;
    }

    TArray<UE::Geometry::FIndex2i> BreaklineEdges;
    TSet<uint64> SeenBreaklineEdges;
    int32 SkippedBreaklineSegments = 0;
    auto AddBreaklineEdge = [&BreaklineEdges, &SeenBreaklineEdges, &SkippedBreaklineSegments](int32 A, int32 B)
    {
        if (A == B)
        {
            ++SkippedBreaklineSegments;
            return;
        }
        const uint32 Low = static_cast<uint32>(FMath::Min(A, B));
        const uint32 High = static_cast<uint32>(FMath::Max(A, B));
        const uint64 Key = (static_cast<uint64>(Low) << 32) | static_cast<uint64>(High);
        if (SeenBreaklineEdges.Contains(Key))
        {
            return;
        }
        SeenBreaklineEdges.Add(Key);
        BreaklineEdges.Add(UE::Geometry::FIndex2i(A, B));
    };

    for (const FCSTopoFigureRecord& Figure : ActiveProject.Figures)
    {
        FString PointType = Figure.Style.PointType;
        if (PointType.IsEmpty())
        {
            FCSTopoCodeStyle Style;
            if (GetCodeStyle(Figure.Code, Style))
            {
                PointType = Style.PointType;
            }
        }
        if (!DoesCSTopoPointTypeCreateTinBreakline(PointType) || Figure.PointNumbers.Num() < 2)
        {
            continue;
        }

        for (int32 Index = 0; Index + 1 < Figure.PointNumbers.Num(); ++Index)
        {
            const int32* A = VertexIndexByPointNumber.Find(Figure.PointNumbers[Index]);
            const int32* B = VertexIndexByPointNumber.Find(Figure.PointNumbers[Index + 1]);
            if (A == nullptr || B == nullptr)
            {
                ++SkippedBreaklineSegments;
                continue;
            }
            AddBreaklineEdge(*A, *B);
        }

        if (Figure.bLoopClosed)
        {
            const int32* A = VertexIndexByPointNumber.Find(Figure.PointNumbers.Last());
            const int32* B = VertexIndexByPointNumber.Find(Figure.PointNumbers[0]);
            if (A == nullptr || B == nullptr)
            {
                ++SkippedBreaklineSegments;
            }
            else
            {
                AddBreaklineEdge(*A, *B);
            }
        }
    }

    TArray<UE::Geometry::FIndex3i> TinTriangles;
    FString WarningMessage;
    bool bUsedBreaklineConstraints = false;
    if (!BreaklineEdges.IsEmpty())
    {
        UE::Geometry::FDelaunay2 Delaunay;
        Delaunay.bAutomaticallyFixEdgesToDuplicateVertices = true;
        Delaunay.bValidateEdges = true;
        if (Delaunay.Triangulate(TinVertices, BreaklineEdges))
        {
            TinTriangles = Delaunay.GetTriangles();
            bUsedBreaklineConstraints = true;
            if (!Delaunay.HasEdges(BreaklineEdges))
            {
                WarningMessage = TEXT("Warning: some breakline constraints were not preserved.");
            }
        }
        else
        {
            WarningMessage = FString::Printf(TEXT("Warning: breakline constraints were not fully applied (%s). Rendering best-effort TIN."), *DelaunayResultLabel(Delaunay.GetResult()));
        }
    }

    if (TinTriangles.IsEmpty())
    {
        UE::Geometry::FDelaunay2 Delaunay;
        if (!Delaunay.Triangulate(TinVertices))
        {
            UserTinStatusLine = FString::Printf(TEXT("User TIN failed: %s."), *DelaunayResultLabel(Delaunay.GetResult()));
            StatusMessage = UserTinStatusLine;
            return false;
        }
        TinTriangles = Delaunay.GetTriangles();
    }

    TArray<int32> RenderTriangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
    RenderTriangles.Reserve(TinTriangles.Num() * 3);
    Normals.Init(FVector::ZeroVector, RenderVertices.Num());
    UVs.Init(FVector2D::ZeroVector, RenderVertices.Num());
    Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), RenderVertices.Num());

    for (const UE::Geometry::FIndex3i& Triangle : TinTriangles)
    {
        if (!RenderVertices.IsValidIndex(Triangle.A)
            || !RenderVertices.IsValidIndex(Triangle.B)
            || !RenderVertices.IsValidIndex(Triangle.C))
        {
            continue;
        }

        const FVector& A = RenderVertices[Triangle.A];
        const FVector& B = RenderVertices[Triangle.B];
        const FVector& C = RenderVertices[Triangle.C];
        FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
        if (!FaceNormal.Normalize())
        {
            continue;
        }
        if (FaceNormal.Z < 0.0f)
        {
            FaceNormal *= -1.0f;
        }
        Normals[Triangle.A] += FaceNormal;
        Normals[Triangle.B] += FaceNormal;
        Normals[Triangle.C] += FaceNormal;
        RenderTriangles.Add(Triangle.A);
        RenderTriangles.Add(Triangle.C);
        RenderTriangles.Add(Triangle.B);
    }

    if (RenderTriangles.IsEmpty())
    {
        UserTinStatusLine = TEXT("User TIN produced no renderable triangles.");
        StatusMessage = UserTinStatusLine;
        return false;
    }

    for (FVector& Normal : Normals)
    {
        if (!Normal.Normalize())
        {
            Normal = FVector::UpVector;
        }
    }

    if (UserTinActor == nullptr)
    {
        UserTinActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
        if (UserTinActor == nullptr)
        {
            UserTinStatusLine = TEXT("Failed to spawn User TIN actor.");
            StatusMessage = UserTinStatusLine;
            return false;
        }

        USceneComponent* RootComponent = NewObject<USceneComponent>(UserTinActor, TEXT("Root"));
        UserTinActor->SetRootComponent(RootComponent);
        RootComponent->RegisterComponent();
#if WITH_EDITOR
        UserTinActor->SetActorLabel(TEXT("CSTopoUserTIN"));
#endif
    }

    if (UserTinMeshComponent == nullptr)
    {
        UserTinMeshComponent = NewObject<UProceduralMeshComponent>(UserTinActor, TEXT("UserTinPreview"));
        UserTinMeshComponent->SetupAttachment(UserTinActor->GetRootComponent());
        UserTinMeshComponent->RegisterComponent();
        UserTinMeshComponent->SetMobility(EComponentMobility::Movable);
        UserTinMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        UserTinMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
        UserTinMeshComponent->bUseAsyncCooking = false;
    }

    UserTinActor->SetActorHiddenInGame(false);
    UserTinMeshComponent->ClearAllMeshSections();
    UserTinMeshComponent->CreateMeshSection_LinearColor(0, RenderVertices, RenderTriangles, Normals, UVs, Colors, Tangents, false);
    static UMaterialInterface* VertexColorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
    UserTinMeshComponent->SetMaterial(0, VertexColorMaterial != nullptr ? VertexColorMaterial : UMaterial::GetDefaultMaterial(MD_Surface));
    UserTinMeshComponent->SetCastShadow(false);
    UserTinMeshComponent->bCastDynamicShadow = false;
    UserTinMeshComponent->SetVisibility(true, true);
    UserTinMeshComponent->SetHiddenInGame(false, true);

    if (SkippedBreaklineSegments > 0)
    {
        const FString SegmentWarning = FString::Printf(TEXT("%d breakline segment(s) could not be used."), SkippedBreaklineSegments);
        WarningMessage = WarningMessage.IsEmpty() ? FString::Printf(TEXT("Warning: %s"), *SegmentWarning) : FString::Printf(TEXT("%s %s"), *WarningMessage, *SegmentWarning);
    }

    const FString WarningSuffix = WarningMessage.IsEmpty() ? TEXT(".") : FString::Printf(TEXT(". %s"), *WarningMessage);
    UserTinStatusLine = FString::Printf(
        TEXT("User TIN visible: %d point(s), %d breakline vertex/vertices, %d breakline segment(s), %d triangle(s)%s%s"),
        RenderVertices.Num(),
        BreaklineVertexCount,
        BreaklineEdges.Num(),
        RenderTriangles.Num() / 3,
        bUsedBreaklineConstraints ? TEXT(" | constrained") : TEXT(""),
        *WarningSuffix);
    StatusMessage = UserTinStatusLine;
    return true;
}

bool UCSTopoSurveySubsystem::StartRuntimeWindowUpdate(FCSTopoPointCloudSource& Source, const FVector& CameraSourcePoint, FString& ErrorMessage)
{
    FString PdalPath;
    if (!UCSTopoPointCloudImport::FindPdalExecutable(PdalPath))
    {
        ErrorMessage = TEXT("PDAL was not found. Runtime COPC windowing is unavailable.");
        return false;
    }

    const FString CacheDirectory = GetProjectCacheDirectory();
    const FString OutputPath = UCSTopoPointCloudImport::BuildDefaultRuntimeWindowPath(CacheDirectory, Source.SourceId);
    const FString PipelinePath = FPaths::ChangeExtension(OutputPath, TEXT("json"));
    const double SampleSpacing = UCSTopoPointCloudImport::ComputeRuntimeWindowSampleSpacing(
        ActiveProject.RuntimeStreaming.WindowRadiusSourceUnits,
        ActiveProject.RuntimeStreaming.TargetPointBudget);
    const FString PipelineJson = UCSTopoPointCloudImport::BuildCopcWindowPipelineJson(
        Source,
        OutputPath,
        CameraSourcePoint,
        ActiveProject.RuntimeStreaming.WindowRadiusSourceUnits,
        SampleSpacing,
        ActiveProject.RuntimeStreaming.VerticalPaddingSourceUnits);

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
    IFileManager::Get().Delete(*OutputPath, false, true);
    if (!FFileHelper::SaveStringToFile(PipelineJson, *PipelinePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to write runtime window pipeline: %s"), *PipelinePath);
        return false;
    }

    const FString Arguments = FString::Printf(TEXT("pipeline \"%s\""), *PipelinePath);
    RuntimeWindowProcessHandle = FPlatformProcess::CreateProc(*PdalPath, *Arguments, false, true, true, nullptr, 0, *FPaths::GetPath(OutputPath), nullptr);
    if (!RuntimeWindowProcessHandle.IsValid())
    {
        ErrorMessage = TEXT("Failed to start PDAL runtime window update.");
        return false;
    }

    Source.bRuntimeWindowPending = true;
    Source.RuntimeWindowCenter = CameraSourcePoint;
    Source.RuntimeWindowRadius = ActiveProject.RuntimeStreaming.WindowRadiusSourceUnits;
    Source.RuntimeWindowSampleSpacing = SampleSpacing;
    Source.RuntimeWindowStatus = FString::Printf(
        TEXT("Updating runtime COPC window around E %.2f N %.2f (radius %.1f, sample %.3f)..."),
        CameraSourcePoint.X,
        CameraSourcePoint.Y,
        Source.RuntimeWindowRadius,
        Source.RuntimeWindowSampleSpacing);

    RuntimeWindowProcessSourceId = Source.SourceId;
    RuntimeWindowProcessPipelinePath = PipelinePath;
    RuntimeWindowProcessOutputPath = OutputPath;
    ErrorMessage = Source.RuntimeWindowStatus;
    return true;
}

void UCSTopoSurveySubsystem::SetLidarPointBudget(bool bUseStreamingBudget) const
{
    if (IConsoleVariable* BudgetVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LidarPointBudget")))
    {
        const FCSTopoPointCloudSource* Source = FindPointCloud(ActiveProject.ActivePointCloudId);
        const bool bSurfacePrimaryOverlay = Source != nullptr
            && Source->bSurfacePrimary
            && Source->SurfaceBuildState == ECSTopoSurfaceBuildState::Ready;
        const int32 DirectBudget = bSurfacePrimaryOverlay ? 500000 : 3000000;
        BudgetVar->Set(bUseStreamingBudget ? ActiveProject.RuntimeStreaming.TargetPointBudget : DirectBudget, ECVF_SetByCode);
    }

    if (IConsoleVariable* IncrementalVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LidarIncrementalBudget")))
    {
        IncrementalVar->Set(1, ECVF_SetByCode);
    }
}

void UCSTopoSurveySubsystem::RefreshPointCloudSourceStates()
{
    for (FCSTopoPointCloudSource& Source : ActiveProject.PointClouds)
    {
        FString StatusMessage;
        UCSTopoPointCloudImport::RefreshSourceRuntimeState(Source, StatusMessage);
        if (!ShouldUseCopcWindowStreaming(Source))
        {
            Source.bRuntimeWindowActive = false;
            Source.bRuntimeWindowPending = false;
            Source.RuntimeWindowPath.Empty();
            Source.RuntimeWindowStatus = TEXT("Runtime window inactive. Using project runtime path.");
        }
        if (Source.bRuntimeWindowActive && (Source.RuntimeWindowPath.IsEmpty() || !FPaths::FileExists(Source.RuntimeWindowPath)))
        {
            Source.bRuntimeWindowActive = false;
            Source.bRuntimeWindowPending = false;
            Source.RuntimeWindowStatus = TEXT("Runtime window missing. Using project runtime path.");
            Source.RuntimeWindowPath.Empty();
        }
        if (Source.SurfaceManifestPath.IsEmpty())
        {
            Source.SurfaceManifestPath = FPaths::Combine(GetProjectCacheDirectory(), TEXT("surfaces"), Source.SourceId.Left(8), TEXT("surface_manifest.json"));
        }
        if (Source.SurfaceBuildState != ECSTopoSurfaceBuildState::Building)
        {
            if (FPaths::FileExists(Source.SurfaceManifestPath))
            {
                const FDateTime SourceTime = Source.SourceModifiedAt;
                const FDateTime SurfaceTime = IFileManager::Get().GetTimeStamp(*Source.SurfaceManifestPath);
                const bool bCurrentSurfaceSchema = IsSurfaceManifestSchemaCurrent(Source.SurfaceManifestPath);
                Source.SurfaceBuildState = SourceTime > SurfaceTime || !bCurrentSurfaceSchema ? ECSTopoSurfaceBuildState::Stale : ECSTopoSurfaceBuildState::Ready;
                Source.SurfaceStatus = Source.SurfaceBuildState == ECSTopoSurfaceBuildState::Ready
                    ? FString::Printf(TEXT("Surface manifest ready: %s"), *Source.SurfaceManifestPath)
                    : (bCurrentSurfaceSchema
                        ? FString::Printf(TEXT("Surface stale; rebuild recommended: %s"), *Source.SurfaceManifestPath)
                        : FString::Printf(TEXT("Surface manifest schema is stale; rebuild required: %s"), *Source.SurfaceManifestPath));
            }
            else
            {
                Source.SurfaceBuildState = ECSTopoSurfaceBuildState::Missing;
                Source.SurfaceStatus = TEXT("Surface not built.");
            }
        }
    }
}

void UCSTopoSurveySubsystem::RebuildLoadedPointCloudActors()
{
    for (TPair<FString, TObjectPtr<ALidarPointCloudActor>>& Pair : PointCloudActors)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->Destroy();
        }
    }
    PointCloudActors.Empty();
    PointCloudActorDisplayPaths.Empty();
    for (TPair<FString, TObjectPtr<AActor>>& Pair : SurfaceActors)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->Destroy();
        }
    }
    SurfaceActors.Empty();
    LoadedSurfaces.Empty();

    for (FCSTopoPointCloudSource& Source : ActiveProject.PointClouds)
    {
        if (!Source.bLoaded)
        {
            continue;
        }

        FString SpawnMessage;
        if (SpawnPointCloudActor(Source, SpawnMessage))
        {
            FString SurfaceMessage;
            LoadDerivedSurfaceForSource(Source, SurfaceMessage);
            RefreshSurfacePresentation(Source);
        }
    }
}

void UCSTopoSurveySubsystem::SyncActivePointCloudFlags()
{
    if (ActiveProject.ActivePointCloudId.IsEmpty() && !ActiveProject.PointClouds.IsEmpty())
    {
        ActiveProject.ActivePointCloudId = ActiveProject.PointClouds[0].SourceId;
    }

    for (FCSTopoPointCloudSource& Source : ActiveProject.PointClouds)
    {
        Source.bIsActive = Source.SourceId == ActiveProject.ActivePointCloudId;
    }
}

void UCSTopoSurveySubsystem::RefreshPointCloudViewMode(FCSTopoPointCloudSource& Source)
{
    TObjectPtr<ALidarPointCloudActor>* ActorPtr = PointCloudActors.Find(Source.SourceId);
    ALidarPointCloudActor* Actor = ActorPtr != nullptr ? ActorPtr->Get() : nullptr;
    if (Actor == nullptr)
    {
        return;
    }

    ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();
    if (Component == nullptr)
    {
        return;
    }

    Component->ScalingMethod = ELidarPointCloudScalingMethod::FixedScreenSize;
    Component->GapFillingStrength = 0.0f;
    Component->ClassificationColors.Add(0, FLinearColor(0.25f, 0.25f, 0.25f, 1.0f));
    Component->ClassificationColors.Add(1, FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
    Component->ClassificationColors.Add(2, FLinearColor(0.22f, 0.86f, 0.34f, 1.0f));
    Component->ClassificationColors.Add(3, FLinearColor(0.18f, 0.55f, 0.18f, 1.0f));
    Component->ClassificationColors.Add(4, FLinearColor(0.17f, 0.48f, 0.22f, 1.0f));
    Component->ClassificationColors.Add(5, FLinearColor(0.74f, 0.64f, 0.42f, 1.0f));
    Component->ClassificationColors.Add(6, FLinearColor(0.84f, 0.34f, 0.29f, 1.0f));
    Component->ClassificationColors.Add(9, FLinearColor(0.24f, 0.52f, 0.94f, 1.0f));
    Component->ElevationColorBottom = FLinearColor(0.08f, 0.18f, 0.82f, 1.0f);
    Component->ElevationColorTop = FLinearColor(1.0f, 0.92f, 0.25f, 1.0f);

    switch (Source.DefaultViewMode)
    {
    case ECSTopoViewMode::Elevation:
        Component->ColorSource = ELidarPointCloudColorationMode::Elevation;
        Component->IntensityInfluence = 0.0f;
        Component->Contrast = FVector4(1.1f, 1.1f, 1.1f, 1.0f);
        break;
    case ECSTopoViewMode::Classification:
        Component->ColorSource = ELidarPointCloudColorationMode::Classification;
        Component->IntensityInfluence = 0.0f;
        Component->Contrast = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        break;
    case ECSTopoViewMode::Intensity:
        Component->ColorSource = ELidarPointCloudColorationMode::Data;
        Component->IntensityInfluence = 1.0f;
        Component->Saturation = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
        Component->Contrast = FVector4(1.45f, 1.45f, 1.45f, 1.0f);
        break;
    case ECSTopoViewMode::RGB:
        Component->ColorSource = ELidarPointCloudColorationMode::Data;
        Component->IntensityInfluence = 0.0f;
        Component->Saturation = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        Component->Contrast = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        break;
    case ECSTopoViewMode::SurfaceShaded:
    default:
        Component->ColorSource = ELidarPointCloudColorationMode::Data;
        Component->IntensityInfluence = 0.25f;
        Component->Saturation = FVector4(0.85f, 0.85f, 0.85f, 1.0f);
        Component->Contrast = FVector4(1.2f, 1.2f, 1.2f, 1.0f);
        break;
    }

    Component->ApplyRenderingParameters();
    RefreshSurfacePresentation(Source);
}

void UCSTopoSurveySubsystem::LoadControlCodeDefinitions()
{
    ControlCodeDefinitions.Reset();
    const FString ControlListPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("CSTopoControlCodeList.json"));
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *ControlListPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Control Code List not found: %s"), *ControlListPath);
        return;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Control Code List could not be parsed: %s"), *ControlListPath);
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* ControlValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("controls"), ControlValues))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Control Code List is missing the controls array: %s"), *ControlListPath);
        return;
    }

    for (const TSharedPtr<FJsonValue>& ControlValue : *ControlValues)
    {
        const TSharedPtr<FJsonObject>* ControlObject = nullptr;
        if (!ControlValue.IsValid() || !ControlValue->TryGetObject(ControlObject) || ControlObject == nullptr || !ControlObject->IsValid())
        {
            continue;
        }

        FString Code;
        if (!(*ControlObject)->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
        {
            continue;
        }

        FCSTopoControlCodeDefinition Definition;
        Definition.Code = NormalizeControlCode(Code);
        (*ControlObject)->TryGetStringField(TEXT("name"), Definition.Name);
        (*ControlObject)->TryGetStringField(TEXT("action"), Definition.Action);
        FString ParameterKind;
        FString GeometryKind;
        (*ControlObject)->TryGetStringField(TEXT("parameter_kind"), ParameterKind);
        (*ControlObject)->TryGetStringField(TEXT("geometry_kind"), GeometryKind);
        Definition.ParameterKind = ControlParameterKindFromString(ParameterKind);
        Definition.GeometryKind = ControlGeometryKindFromString(GeometryKind);
        ControlCodeDefinitions.Add(Definition);
    }
}

const FCSTopoControlCodeDefinition* UCSTopoSurveySubsystem::FindControlCodeDefinition(const FString& ControlCode) const
{
    const FString NormalizedControl = NormalizeControlCode(ControlCode);
    return ControlCodeDefinitions.FindByPredicate([&NormalizedControl](const FCSTopoControlCodeDefinition& Definition)
    {
        return Definition.Code.Equals(NormalizedControl, ESearchCase::IgnoreCase);
    });
}

bool UCSTopoSurveySubsystem::ValidateControlParameter(const FString& ControlCode, const FString& Parameter, FString& StatusMessage) const
{
    const FCSTopoControlCodeDefinition* Definition = FindControlCodeDefinition(ControlCode);
    if (Definition == nullptr)
    {
        StatusMessage = FString::Printf(TEXT("%s is not in the CSTopo control-code list."), *ControlCode);
        return false;
    }

    const FString NormalizedParameter = Parameter.TrimStartAndEnd();
    double NumericValue = 0.0;
    switch (Definition->ParameterKind)
    {
    case ECSTopoControlParameterKind::Distance:
        if (!ParseNumericParameter(NormalizedParameter, NumericValue))
        {
            StatusMessage = FString::Printf(TEXT("%s needs a numeric distance."), *ControlCode);
            return false;
        }
        break;
    case ECSTopoControlParameterKind::PointNumber:
        if (ParsePointNumberParameter(Parameter) == INDEX_NONE)
        {
            StatusMessage = FString::Printf(TEXT("%s needs a point number."), *ControlCode);
            return false;
        }
        break;
    case ECSTopoControlParameterKind::DistanceOrPointNumber:
        if (!ParseNumericParameter(NormalizedParameter, NumericValue) && ParsePointNumberParameter(NormalizedParameter) == INDEX_NONE)
        {
            StatusMessage = FString::Printf(TEXT("%s needs a distance or P<pointNumber>."), *ControlCode);
            return false;
        }
        break;
    case ECSTopoControlParameterKind::OptionalDistance:
        if (!NormalizedParameter.IsEmpty() && !ParseNumericParameter(NormalizedParameter, NumericValue))
        {
            StatusMessage = FString::Printf(TEXT("%s needs a numeric radius when a parameter is supplied."), *ControlCode);
            return false;
        }
        break;
    case ECSTopoControlParameterKind::None:
    default:
        break;
    }

    return true;
}

FString UCSTopoSurveySubsystem::BuildNextMeasurementCode() const
{
    const FString BaseCode = NormalizeControlCode(ActiveProject.ActiveCode);
    return PendingControlCode.IsEmpty() ? BaseCode : FString::Printf(TEXT("%s %s"), *BaseCode, *PendingControlCode);
}

void UCSTopoSurveySubsystem::ApplyControlBeforeShot(const FString& BaseCode, const FString& ControlCode)
{
    if (ControlCode.Equals(TEXT("ST"), ESearchCase::IgnoreCase) || ControlCode.Equals(TEXT("RECT"), ESearchCase::IgnoreCase))
    {
        UCSTopoProjectLibrary::SplitFigure(ActiveProject, BaseCode);
    }
}

void UCSTopoSurveySubsystem::ApplyControlAfterShot(const FCSTopoShotRecord& Shot)
{
    if (Shot.ControlCode.Equals(TEXT("END"), ESearchCase::IgnoreCase))
    {
        UCSTopoProjectLibrary::SplitFigure(ActiveProject, Shot.BaseCode);
    }
    else if (Shot.ControlCode.Equals(TEXT("CLS"), ESearchCase::IgnoreCase))
    {
        UCSTopoProjectLibrary::CloseActiveFigure(ActiveProject, Shot.BaseCode);
    }
    else
    {
        TArray<const FCSTopoShotRecord*> BaseShots;
        for (const FCSTopoShotRecord& Candidate : ActiveProject.Shots)
        {
            const FString CandidateBaseCode = Candidate.BaseCode.IsEmpty() ? Candidate.Code : Candidate.BaseCode;
            if (CandidateBaseCode.Equals(Shot.BaseCode, ESearchCase::IgnoreCase))
            {
                BaseShots.Add(&Candidate);
            }
        }

        for (int32 Index = 0; Index < BaseShots.Num(); ++Index)
        {
            if (BaseShots[Index]->PointNumber == Shot.PointNumber)
            {
                if (Index >= 2 && BaseShots[Index - 2]->ControlCode.Equals(TEXT("RECT"), ESearchCase::IgnoreCase))
                {
                    UCSTopoProjectLibrary::SplitFigure(ActiveProject, Shot.BaseCode);
                }
                break;
            }
        }
    }

    const bool bArmAutomaticPt = Shot.ControlCode.Equals(TEXT("PC"), ESearchCase::IgnoreCase);
    ClearPendingControlCode();
    if (bArmAutomaticPt)
    {
        PendingControlCode = TEXT("PT");
        PendingControlParameter.Empty();
        bPendingControlAutomatic = true;
    }
    RebuildFigureSegments();
    bUserTinDirty = true;
}

void UCSTopoSurveySubsystem::RebuildFigureSegments()
{
    ActiveProject.FigureSegments.Reset();
    TMap<int32, FCSTopoShotRecord> ShotByNumber;
    TMap<FString, TArray<FCSTopoShotRecord>> ShotsByBaseCode;
    for (const FCSTopoShotRecord& Shot : ActiveProject.Shots)
    {
        ShotByNumber.Add(Shot.PointNumber, Shot);
        const FString BaseCode = Shot.BaseCode.IsEmpty() ? Shot.Code : Shot.BaseCode;
        ShotsByBaseCode.FindOrAdd(BaseCode).Add(Shot);
    }

    auto AddSegment = [this](ECSTopoFigureSegmentKind Kind, const FString& Code, const TArray<int32>& PointNumbers, const TArray<FVector>& SurveyPoints, const FString& ControlCode, const FString& ControlParameter, int32 CreatedByPointNumber)
    {
        if (SurveyPoints.Num() < 2)
        {
            return;
        }
        FCSTopoCodeStyle Style;
        GetCodeStyle(Code, Style);
        FCSTopoFigureSegmentRecord Segment;
        Segment.SegmentId = FGuid::NewGuid();
        Segment.SegmentKind = Kind;
        Segment.Code = Code;
        Segment.LayerName = Style.LayerName.IsEmpty() ? Code : Style.LayerName;
        Segment.ControlCode = ControlCode;
        Segment.ControlParameter = ControlParameter;
        Segment.CreatedByPointNumber = CreatedByPointNumber;
        Segment.PointNumbers = PointNumbers;
        Segment.SurveyPoints = SurveyPoints;
        ActiveProject.FigureSegments.Add(Segment);
    };

    TSet<FString> SuppressedLinePairs;
    for (const TPair<FString, TArray<FCSTopoShotRecord>>& Pair : ShotsByBaseCode)
    {
        const FString BaseCode = Pair.Key;
        const TArray<FCSTopoShotRecord>& BaseShots = Pair.Value;
        int32 ActiveSmoothStartIndex = INDEX_NONE;

        for (int32 ShotIndex = 0; ShotIndex < BaseShots.Num(); ++ShotIndex)
        {
            const FCSTopoShotRecord& Shot = BaseShots[ShotIndex];
            if (Shot.ControlCode.Equals(TEXT("RECT"), ESearchCase::IgnoreCase))
            {
                if (ShotIndex > 0)
                {
                    SuppressLinePair(SuppressedLinePairs, BaseCode, BaseShots[ShotIndex - 1], Shot);
                }
                if (ShotIndex + 2 < BaseShots.Num())
                {
                    const FCSTopoShotRecord& LengthShot = BaseShots[ShotIndex + 1];
                    const FCSTopoShotRecord& WidthShot = BaseShots[ShotIndex + 2];
                    AddSegment(
                        ECSTopoFigureSegmentKind::Rectangle,
                        BaseCode,
                        { Shot.PointNumber, LengthShot.PointNumber, WidthShot.PointNumber },
                        BuildRectanglePoints(Shot, LengthShot, WidthShot),
                        Shot.ControlCode,
                        Shot.ControlParameter,
                        WidthShot.PointNumber);
                    SuppressLineRange(SuppressedLinePairs, BaseCode, BaseShots, ShotIndex, ShotIndex + 2);
                    if (ShotIndex + 3 < BaseShots.Num())
                    {
                        SuppressLinePair(SuppressedLinePairs, BaseCode, WidthShot, BaseShots[ShotIndex + 3]);
                    }
                }
                continue;
            }

            if (Shot.ControlCode.Equals(TEXT("PT"), ESearchCase::IgnoreCase))
            {
                int32 PcIndex = INDEX_NONE;
                for (int32 CandidateIndex = ShotIndex - 1; CandidateIndex >= 0; --CandidateIndex)
                {
                    if (BaseShots[CandidateIndex].ControlCode.Equals(TEXT("PC"), ESearchCase::IgnoreCase))
                    {
                        PcIndex = CandidateIndex;
                        break;
                    }
                }
                if (PcIndex > 0)
                {
                    TArray<FVector> Points;
                    if (BuildTangentArcPoints(BaseShots[PcIndex - 1], BaseShots[PcIndex], Shot, Points))
                    {
                        AddSegment(ECSTopoFigureSegmentKind::Arc, BaseCode, { BaseShots[PcIndex].PointNumber, Shot.PointNumber }, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
                        SuppressLineRange(SuppressedLinePairs, BaseCode, BaseShots, PcIndex, ShotIndex);
                    }
                }
                continue;
            }

            if (Shot.ControlCode.Equals(TEXT("NPT"), ESearchCase::IgnoreCase))
            {
                int32 NpcIndex = INDEX_NONE;
                for (int32 CandidateIndex = ShotIndex - 1; CandidateIndex >= 0; --CandidateIndex)
                {
                    if (BaseShots[CandidateIndex].ControlCode.Equals(TEXT("NPC"), ESearchCase::IgnoreCase))
                    {
                        NpcIndex = CandidateIndex;
                        break;
                    }
                }
                if (NpcIndex != INDEX_NONE && ShotIndex - NpcIndex >= 2)
                {
                    const int32 MiddleIndex = NpcIndex + ((ShotIndex - NpcIndex) / 2);
                    TArray<FVector> Points;
                    if (BuildArcFromThreeShots(BaseShots[NpcIndex], BaseShots[MiddleIndex], Shot, Points))
                    {
                        AddSegment(ECSTopoFigureSegmentKind::Arc, BaseCode, { BaseShots[NpcIndex].PointNumber, BaseShots[MiddleIndex].PointNumber, Shot.PointNumber }, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
                        SuppressLineRange(SuppressedLinePairs, BaseCode, BaseShots, NpcIndex, ShotIndex);
                    }
                }
                continue;
            }

            if (Shot.ControlCode.Equals(TEXT("SSC"), ESearchCase::IgnoreCase))
            {
                ActiveSmoothStartIndex = ShotIndex;
                continue;
            }

            if (Shot.ControlCode.Equals(TEXT("ESC"), ESearchCase::IgnoreCase) && ActiveSmoothStartIndex != INDEX_NONE)
            {
                TArray<FVector> Points = BuildCatmullRomPoints(BaseShots, ActiveSmoothStartIndex, ShotIndex);
                TArray<int32> PointNumbers;
                for (int32 Index = ActiveSmoothStartIndex; Index <= ShotIndex; ++Index)
                {
                    PointNumbers.Add(BaseShots[Index].PointNumber);
                }
                AddSegment(ECSTopoFigureSegmentKind::SmoothCurve, BaseCode, PointNumbers, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
                SuppressLineRange(SuppressedLinePairs, BaseCode, BaseShots, ActiveSmoothStartIndex, ShotIndex);
                ActiveSmoothStartIndex = INDEX_NONE;
            }
        }

        if (ActiveSmoothStartIndex != INDEX_NONE && BaseShots.Num() - ActiveSmoothStartIndex >= 2)
        {
            TArray<FVector> Points = BuildCatmullRomPoints(BaseShots, ActiveSmoothStartIndex, BaseShots.Num() - 1);
            TArray<int32> PointNumbers;
            for (int32 Index = ActiveSmoothStartIndex; Index < BaseShots.Num(); ++Index)
            {
                PointNumbers.Add(BaseShots[Index].PointNumber);
            }
            AddSegment(ECSTopoFigureSegmentKind::SmoothCurve, BaseCode, PointNumbers, Points, TEXT("SSC"), TEXT(""), BaseShots.Last().PointNumber);
            SuppressLineRange(SuppressedLinePairs, BaseCode, BaseShots, ActiveSmoothStartIndex, BaseShots.Num() - 1);
        }
    }

    for (const FCSTopoFigureRecord& Figure : ActiveProject.Figures)
    {
        for (int32 Index = 0; Index + 1 < Figure.PointNumbers.Num(); ++Index)
        {
            const FCSTopoShotRecord* A = ShotByNumber.Find(Figure.PointNumbers[Index]);
            const FCSTopoShotRecord* B = ShotByNumber.Find(Figure.PointNumbers[Index + 1]);
            if (A != nullptr && B != nullptr)
            {
                if (SuppressedLinePairs.Contains(BuildLinePairKey(Figure.Code, A->PointNumber, B->PointNumber)))
                {
                    continue;
                }
                AddSegment(ECSTopoFigureSegmentKind::Line, Figure.Code, { A->PointNumber, B->PointNumber }, { SurveyPointFromShot(*A), SurveyPointFromShot(*B) }, TEXT(""), TEXT(""), B->PointNumber);
            }
        }
        if (Figure.bLoopClosed && Figure.PointNumbers.Num() >= 2)
        {
            const FCSTopoShotRecord* A = ShotByNumber.Find(Figure.PointNumbers.Last());
            const FCSTopoShotRecord* B = ShotByNumber.Find(Figure.PointNumbers[0]);
            if (A != nullptr && B != nullptr)
            {
                AddSegment(ECSTopoFigureSegmentKind::Line, Figure.Code, { A->PointNumber, B->PointNumber }, { SurveyPointFromShot(*A), SurveyPointFromShot(*B) }, TEXT("CLS"), TEXT(""), A->PointNumber);
            }
        }
    }

    for (const FCSTopoShotRecord& Shot : ActiveProject.Shots)
    {
        const FString BaseCode = Shot.BaseCode.IsEmpty() ? Shot.Code : Shot.BaseCode;
        const TArray<FCSTopoShotRecord>* BaseShots = ShotsByBaseCode.Find(BaseCode);
        if (BaseShots == nullptr)
        {
            continue;
        }

        int32 ShotIndex = INDEX_NONE;
        for (int32 Index = 0; Index < BaseShots->Num(); ++Index)
        {
            if ((*BaseShots)[Index].PointNumber == Shot.PointNumber)
            {
                ShotIndex = Index;
                break;
            }
        }
        if (ShotIndex == INDEX_NONE)
        {
            continue;
        }

        const FCSTopoShotRecord* Previous = ShotIndex > 0 ? &(*BaseShots)[ShotIndex - 1] : nullptr;
        if (Shot.ControlCode.Equals(TEXT("JPT"), ESearchCase::IgnoreCase))
        {
            const FCSTopoShotRecord* Target = ShotByNumber.Find(ParsePointNumberParameter(Shot.ControlParameter));
            if (Target != nullptr)
            {
                AddSegment(ECSTopoFigureSegmentKind::JoinLine, BaseCode, { Target->PointNumber, Shot.PointNumber }, { SurveyPointFromShot(*Target), SurveyPointFromShot(Shot) }, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
            }
        }
        else if ((Shot.ControlCode.Equals(TEXT("OH"), ESearchCase::IgnoreCase) || Shot.ControlCode.Equals(TEXT("OV"), ESearchCase::IgnoreCase)) && Previous != nullptr)
        {
            const double Distance = FCString::Atod(*Shot.ControlParameter);
            TArray<FVector> Points;
            if (Shot.ControlCode.Equals(TEXT("OV"), ESearchCase::IgnoreCase))
            {
                Points = { FVector(Previous->Northing, Previous->Easting, Previous->Elevation + Distance), FVector(Shot.Northing, Shot.Easting, Shot.Elevation + Distance) };
            }
            else
            {
                const FVector2D Delta(Shot.Northing - Previous->Northing, Shot.Easting - Previous->Easting);
                const double Length = FMath::Max(Delta.Size(), 0.000001);
                const FVector2D Offset(-Delta.Y / Length * Distance, Delta.X / Length * Distance);
                Points = { FVector(Previous->Northing + Offset.X, Previous->Easting + Offset.Y, Previous->Elevation), FVector(Shot.Northing + Offset.X, Shot.Easting + Offset.Y, Shot.Elevation) };
            }
            AddSegment(ECSTopoFigureSegmentKind::OffsetLine, BaseCode, { Previous->PointNumber, Shot.PointNumber }, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
        }
        else if (Shot.ControlCode.Equals(TEXT("SCR"), ESearchCase::IgnoreCase))
        {
            double Radius = FCString::Atod(*Shot.ControlParameter);
            if (Radius <= 0.0 && ShotIndex + 1 < BaseShots->Num())
            {
                const FCSTopoShotRecord& NextShot = (*BaseShots)[ShotIndex + 1];
                Radius = FVector2D::Distance(FVector2D(Shot.Northing, Shot.Easting), FVector2D(NextShot.Northing, NextShot.Easting));
            }
            if (Radius > 0.0)
            {
                TArray<FVector> Points;
                for (int32 Index = 0; Index <= 48; ++Index)
                {
                    const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / 48.0;
                    Points.Add(FVector(Shot.Northing + FMath::Sin(Angle) * Radius, Shot.Easting + FMath::Cos(Angle) * Radius, Shot.Elevation));
                }
                AddSegment(ECSTopoFigureSegmentKind::Circle, BaseCode, { Shot.PointNumber }, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
            }
        }
        else if (Shot.ControlCode.Equals(TEXT("SCE"), ESearchCase::IgnoreCase) && ShotIndex >= 2)
        {
            const FCSTopoShotRecord& A = (*BaseShots)[ShotIndex - 2];
            const FCSTopoShotRecord& B = (*BaseShots)[ShotIndex - 1];
            const double AX = A.Easting;
            const double AY = A.Northing;
            const double BX = B.Easting;
            const double BY = B.Northing;
            const double CX = Shot.Easting;
            const double CY = Shot.Northing;
            const double D = 2.0 * (AX * (BY - CY) + BX * (CY - AY) + CX * (AY - BY));
            if (!FMath::IsNearlyZero(D))
            {
                const double UX = ((AX * AX + AY * AY) * (BY - CY) + (BX * BX + BY * BY) * (CY - AY) + (CX * CX + CY * CY) * (AY - BY)) / D;
                const double UY = ((AX * AX + AY * AY) * (CX - BX) + (BX * BX + BY * BY) * (AX - CX) + (CX * CX + CY * CY) * (BX - AX)) / D;
                const double Radius = FVector2D::Distance(FVector2D(AX, AY), FVector2D(UX, UY));
                TArray<FVector> Points;
                for (int32 Index = 0; Index <= 48; ++Index)
                {
                    const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / 48.0;
                    Points.Add(FVector(UY + FMath::Sin(Angle) * Radius, UX + FMath::Cos(Angle) * Radius, Shot.Elevation));
                }
                AddSegment(ECSTopoFigureSegmentKind::Circle, BaseCode, { A.PointNumber, B.PointNumber, Shot.PointNumber }, Points, Shot.ControlCode, Shot.ControlParameter, Shot.PointNumber);
            }
        }
    }
}

FString UCSTopoSurveySubsystem::NormalizeCode(const FString& Code) const
{
    FString Normalized = Code.TrimStartAndEnd();
    Normalized.ToUpperInline();
    return Normalized;
}
