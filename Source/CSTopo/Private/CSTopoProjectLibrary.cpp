#include "CSTopoProjectLibrary.h"

#include "CSTopoPointCloudImport.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
FString NormalizeCSTopoFilePath(const FString& FilePath)
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

FLinearColor MakeDefaultCodeColor(const FString& Code)
{
    if (Code.IsEmpty())
    {
        return FLinearColor::White;
    }

    const uint32 Hash = GetTypeHash(Code);
    const uint8 Hue = static_cast<uint8>(Hash % 255);
    return FLinearColor::MakeFromHSV8(Hue, 170, 240);
}
}

FCSTopoProjectDocument UCSTopoProjectLibrary::CreateDefaultProject(const FString& ProjectName)
{
    FCSTopoProjectDocument Project;
    Project.ProjectName = ProjectName.IsEmpty() ? TEXT("Untitled CSTopo Project") : ProjectName;

    const TArray<TPair<FString, FLinearColor>> Defaults = {
        { TEXT("EOP"), FLinearColor(1.0f, 0.85f, 0.05f) },
        { TEXT("CL"), FLinearColor(0.15f, 0.65f, 1.0f) },
        { TEXT("FL"), FLinearColor(0.1f, 0.9f, 0.35f) },
        { TEXT("GB"), FLinearColor(1.0f, 0.35f, 0.2f) }
    };

    for (const TPair<FString, FLinearColor>& Item : Defaults)
    {
        FCSTopoCodeStyle Style;
        Style.Code = Item.Key;
        Style.LayerName = Item.Key;
        Style.Color = Item.Value;
        Project.CodePalette.Add(Style);
    }

    Project.ActiveCode = TEXT("EOP");
    return Project;
}

bool UCSTopoProjectLibrary::SaveProjectToFile(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoFilePath(FilePath);
    FCSTopoProjectDocument ProjectToSave = Project;
    ProjectToSave.SchemaVersion = TEXT("1.3");
    if (ProjectToSave.CacheManifestPath.IsEmpty())
    {
        ProjectToSave.CacheManifestPath = UCSTopoPointCloudImport::BuildDefaultCacheManifestPath(NormalizedFilePath);
    }
    else
    {
        ProjectToSave.CacheManifestPath = NormalizeCSTopoFilePath(ProjectToSave.CacheManifestPath);
    }

    const FCSTopoPointCloudCacheManifestDocument Manifest = BuildCacheManifest(ProjectToSave, NormalizedFilePath);
    ProjectToSave.PointClouds = Manifest.Entries;

    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(ProjectToSave, Json, 0, 0, 0, nullptr, true))
    {
        ErrorMessage = TEXT("Failed to serialize CSTopo project.");
        return false;
    }

    if (!FFileHelper::SaveStringToFile(Json, *NormalizedFilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to write project file: %s"), *NormalizedFilePath);
        return false;
    }

    FString ManifestError;
    if (!SaveCacheManifestToFile(Manifest, ProjectToSave.CacheManifestPath, ManifestError))
    {
        ErrorMessage = ManifestError;
        return false;
    }

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoProjectLibrary::LoadProjectFromFile(const FString& FilePath, FCSTopoProjectDocument& Project, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoFilePath(FilePath);
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *NormalizedFilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to read project file: %s"), *NormalizedFilePath);
        return false;
    }

    if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Project, 0, 0))
    {
        ErrorMessage = TEXT("Failed to parse CSTopo project JSON.");
        return false;
    }

    const bool bLegacyLoopClosureSemantics = !Project.SchemaVersion.Equals(TEXT("1.3"), ESearchCase::CaseSensitive);
    if (bLegacyLoopClosureSemantics)
    {
        for (FCSTopoFigureRecord& Figure : Project.Figures)
        {
            if (Figure.bClosed)
            {
                Figure.bLoopClosed = true;
            }
        }
    }
    Project.SchemaVersion = TEXT("1.3");

    if (Project.CacheManifestPath.IsEmpty())
    {
        Project.CacheManifestPath = UCSTopoPointCloudImport::BuildDefaultCacheManifestPath(NormalizedFilePath);
    }
    else
    {
        Project.CacheManifestPath = NormalizeCSTopoFilePath(Project.CacheManifestPath);
    }

    if (FPaths::FileExists(Project.CacheManifestPath))
    {
        FCSTopoPointCloudCacheManifestDocument Manifest;
        FString ManifestError;
        if (LoadCacheManifestFromFile(Project.CacheManifestPath, Manifest, ManifestError))
        {
            ApplyCacheManifest(Project, Manifest);
        }
    }

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoProjectLibrary::SaveCacheManifestToFile(const FCSTopoPointCloudCacheManifestDocument& Manifest, const FString& FilePath, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoFilePath(FilePath);
    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(Manifest, Json, 0, 0, 0, nullptr, true))
    {
        ErrorMessage = TEXT("Failed to serialize CSTopo cache manifest.");
        return false;
    }

    if (!FFileHelper::SaveStringToFile(Json, *NormalizedFilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to write cache manifest: %s"), *NormalizedFilePath);
        return false;
    }

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoProjectLibrary::LoadCacheManifestFromFile(const FString& FilePath, FCSTopoPointCloudCacheManifestDocument& Manifest, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoFilePath(FilePath);
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *NormalizedFilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to read cache manifest: %s"), *NormalizedFilePath);
        return false;
    }

    if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Manifest, 0, 0))
    {
        ErrorMessage = TEXT("Failed to parse CSTopo cache manifest JSON.");
        return false;
    }

    ErrorMessage.Empty();
    return true;
}

FCSTopoPointCloudCacheManifestDocument UCSTopoProjectLibrary::BuildCacheManifest(const FCSTopoProjectDocument& Project, const FString& ProjectPath)
{
    FCSTopoPointCloudCacheManifestDocument Manifest;
    Manifest.GeneratedAt = FDateTime::UtcNow();
    Manifest.ProjectPath = ProjectPath;
    Manifest.Entries = Project.PointClouds;
    return Manifest;
}

FCSTopoPlaneFitResult UCSTopoProjectLibrary::FitPlaneAtSurveyCoordinate(const TArray<FVector>& Samples, double Northing, double Easting)
{
    FCSTopoPlaneFitResult Result;
    if (Samples.Num() < 3)
    {
        return Result;
    }

    double Sx = 0.0, Sy = 0.0, Sz = 0.0;
    double Sxx = 0.0, Sxy = 0.0, Syy = 0.0;
    double Sxz = 0.0, Syz = 0.0;

    for (const FVector& P : Samples)
    {
        Sx += P.X;
        Sy += P.Y;
        Sz += P.Z;
        Sxx += P.X * P.X;
        Sxy += P.X * P.Y;
        Syy += P.Y * P.Y;
        Sxz += P.X * P.Z;
        Syz += P.Y * P.Z;
    }

    const double N = static_cast<double>(Samples.Num());
    double M[3][4] = {
        { Sxx, Sxy, Sx, Sxz },
        { Sxy, Syy, Sy, Syz },
        { Sx, Sy, N, Sz }
    };

    for (int32 Col = 0; Col < 3; ++Col)
    {
        int32 Pivot = Col;
        for (int32 Row = Col + 1; Row < 3; ++Row)
        {
            if (FMath::Abs(M[Row][Col]) > FMath::Abs(M[Pivot][Col]))
            {
                Pivot = Row;
            }
        }

        if (FMath::IsNearlyZero(M[Pivot][Col], KINDA_SMALL_NUMBER))
        {
            return Result;
        }

        for (int32 K = Col; K < 4; ++K)
        {
            Swap(M[Col][K], M[Pivot][K]);
        }

        const double Divisor = M[Col][Col];
        for (int32 K = Col; K < 4; ++K)
        {
            M[Col][K] /= Divisor;
        }

        for (int32 Row = 0; Row < 3; ++Row)
        {
            if (Row == Col)
            {
                continue;
            }

            const double Factor = M[Row][Col];
            for (int32 K = Col; K < 4; ++K)
            {
                M[Row][K] -= Factor * M[Col][K];
            }
        }
    }

    const double PlaneA = M[0][3];
    const double PlaneB = M[1][3];
    const double PlaneC = M[2][3];
    double ResidualSum = 0.0;

    for (const FVector& P : Samples)
    {
        const double Dz = (PlaneA * P.X + PlaneB * P.Y + PlaneC) - P.Z;
        ResidualSum += Dz * Dz;
    }

    Result.bSuccess = true;
    Result.Elevation = PlaneA * Northing + PlaneB * Easting + PlaneC;
    Result.Residual = FMath::Sqrt(ResidualSum / N);
    return Result;
}

FCSTopoShotRecord UCSTopoProjectLibrary::AddFittedShot(FCSTopoProjectDocument& Project, const FString& Code, double Northing, double Easting, double Elevation, double Residual, const FString& SourceCloudId)
{
    FCSTopoFigureRecord& Figure = FindOrCreateOpenFigure(Project, Code);

    FCSTopoShotRecord Shot;
    Shot.PointNumber = Project.NextPointNumber++;
    Shot.Northing = Northing;
    Shot.Easting = Easting;
    Shot.Elevation = Elevation;
    Shot.Code = Code;
    Shot.FigureId = Figure.FigureId;
    Shot.FitResidual = Residual;
    Shot.SourceCloudId = SourceCloudId;
    Shot.CreatedAt = FDateTime::UtcNow();

    Project.Shots.Add(Shot);
    Figure.PointNumbers.Add(Shot.PointNumber);
    Project.ActiveCode = Code;
    return Shot;
}

void UCSTopoProjectLibrary::SplitFigure(FCSTopoProjectDocument& Project, const FString& Code)
{
    if (FCSTopoFigureRecord* Figure = FindOpenFigure(Project, Code))
    {
        Figure->bClosed = true;
        Figure->bLoopClosed = false;
    }
}

void UCSTopoProjectLibrary::CloseActiveFigure(FCSTopoProjectDocument& Project, const FString& Code)
{
    if (FCSTopoFigureRecord* Figure = FindOpenFigure(Project, Code))
    {
        Figure->bClosed = true;
        Figure->bLoopClosed = true;
    }
}

void UCSTopoProjectLibrary::ApplyCacheManifest(FCSTopoProjectDocument& Project, const FCSTopoPointCloudCacheManifestDocument& Manifest)
{
    for (FCSTopoPointCloudSource& Source : Project.PointClouds)
    {
        const FCSTopoPointCloudSource* ManifestEntry = Manifest.Entries.FindByPredicate([&Source](const FCSTopoPointCloudSource& Candidate)
        {
            return Candidate.SourceId == Source.SourceId;
        });

        if (ManifestEntry == nullptr)
        {
            continue;
        }

        Source.CachePath = ManifestEntry->CachePath;
        Source.CacheFormat = ManifestEntry->CacheFormat;
        Source.PointCount = ManifestEntry->PointCount;
        Source.BoundsMin = ManifestEntry->BoundsMin;
        Source.BoundsMax = ManifestEntry->BoundsMax;
        Source.CoordinateSystemWkt = ManifestEntry->CoordinateSystemWkt;
        Source.LinearUnitName = ManifestEntry->LinearUnitName;
        Source.LinearUnitToMeters = ManifestEntry->LinearUnitToMeters;
        Source.DirectOpenPointThreshold = ManifestEntry->DirectOpenPointThreshold;
        Source.bDirectOpenEligible = ManifestEntry->bDirectOpenEligible;
        Source.bSourceExists = ManifestEntry->bSourceExists;
        Source.bCacheExists = ManifestEntry->bCacheExists;
        Source.bCachePreferredForRuntime = ManifestEntry->bCachePreferredForRuntime;
        Source.bCacheOutOfDate = ManifestEntry->bCacheOutOfDate;
        Source.RuntimeDisplayPath = ManifestEntry->RuntimeDisplayPath;
        Source.CacheStatus = ManifestEntry->CacheStatus;
        Source.CacheState = ManifestEntry->CacheState;
        Source.SourceModifiedAt = ManifestEntry->SourceModifiedAt;
        Source.CacheModifiedAt = ManifestEntry->CacheModifiedAt;
        Source.bRuntimeWindowActive = ManifestEntry->bRuntimeWindowActive;
        Source.bRuntimeWindowPending = ManifestEntry->bRuntimeWindowPending;
        Source.RuntimeWindowPath = ManifestEntry->RuntimeWindowPath;
        Source.RuntimeWindowStatus = ManifestEntry->RuntimeWindowStatus;
        Source.RuntimeWindowCenter = ManifestEntry->RuntimeWindowCenter;
        Source.RuntimeWindowRadius = ManifestEntry->RuntimeWindowRadius;
        Source.RuntimeWindowSampleSpacing = ManifestEntry->RuntimeWindowSampleSpacing;
        Source.SurfaceId = ManifestEntry->SurfaceId;
        Source.SurfaceManifestPath = ManifestEntry->SurfaceManifestPath;
        Source.SurfaceStatus = ManifestEntry->SurfaceStatus;
        Source.SurfaceBuildState = ManifestEntry->SurfaceBuildState;
        Source.bSurfacePrimary = ManifestEntry->bSurfacePrimary;
        Source.bCloudOverlayVisible = ManifestEntry->bCloudOverlayVisible;
        Source.DefaultViewMode = ManifestEntry->DefaultViewMode;
    }
}

FCSTopoCodeStyle UCSTopoProjectLibrary::FindOrCreateStyle(FCSTopoProjectDocument& Project, const FString& Code)
{
    for (const FCSTopoCodeStyle& Style : Project.CodePalette)
    {
        if (Style.Code.Equals(Code, ESearchCase::IgnoreCase))
        {
            return Style;
        }
    }

    FCSTopoCodeStyle Style;
    Style.Code = Code;
    Style.LayerName = Code;
    Style.Color = MakeDefaultCodeColor(Code);
    Project.CodePalette.Add(Style);
    return Style;
}

FCSTopoFigureRecord* UCSTopoProjectLibrary::FindOpenFigure(FCSTopoProjectDocument& Project, const FString& Code)
{
    for (FCSTopoFigureRecord& Figure : Project.Figures)
    {
        if (Figure.Code.Equals(Code, ESearchCase::IgnoreCase) && !Figure.bClosed)
        {
            return &Figure;
        }
    }

    return nullptr;
}

FCSTopoFigureRecord& UCSTopoProjectLibrary::FindOrCreateOpenFigure(FCSTopoProjectDocument& Project, const FString& Code)
{
    if (FCSTopoFigureRecord* ExistingFigure = FindOpenFigure(Project, Code))
    {
        return *ExistingFigure;
    }

    FCSTopoFigureRecord Figure;
    Figure.FigureId = FGuid::NewGuid();
    Figure.Code = Code;
    Figure.Style = FindOrCreateStyle(Project, Code);
    Figure.LayerName = Figure.Style.LayerName;
    return Project.Figures[Project.Figures.Add(Figure)];
}
