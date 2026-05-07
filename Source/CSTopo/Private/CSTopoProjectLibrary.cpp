#include "CSTopoProjectLibrary.h"

#include "CSTopoPointCloudImport.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

bool ParseHexColor(const FString& ColorText, FLinearColor& OutColor)
{
    FString Hex = ColorText.TrimStartAndEnd();
    Hex.RemoveFromStart(TEXT("#"));
    if (Hex.Len() != 6)
    {
        return false;
    }

    const auto ParseChannel = [](const FString& Text, int32 Offset, uint8& OutChannel)
    {
        const FString ChannelText = Text.Mid(Offset, 2);
        for (TCHAR Character : ChannelText)
        {
            if (!FChar::IsHexDigit(Character))
            {
                return false;
            }
        }
        if (ChannelText.Len() != 2)
        {
            return false;
        }

        OutChannel = static_cast<uint8>(FParse::HexNumber(*ChannelText));
        return true;
    };

    uint8 R = 255;
    uint8 G = 255;
    uint8 B = 255;
    if (!ParseChannel(Hex, 0, R) || !ParseChannel(Hex, 2, G) || !ParseChannel(Hex, 4, B))
    {
        return false;
    }

    OutColor = FLinearColor(FColor(R, G, B, 255));
    return true;
}

const TSet<FString>& LoadKnownControlCodes()
{
    static TSet<FString> ControlCodes;
    static bool bLoaded = false;
    if (bLoaded)
    {
        return ControlCodes;
    }

    bLoaded = true;
    const FString ControlListPath = FPaths::ProjectDir() / TEXT("Config/CSTopoControlCodeList.json");
    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *ControlListPath))
    {
        return ControlCodes;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return ControlCodes;
    }

    const TArray<TSharedPtr<FJsonValue>>* Controls = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("controls"), Controls))
    {
        return ControlCodes;
    }

    for (const TSharedPtr<FJsonValue>& ControlValue : *Controls)
    {
        const TSharedPtr<FJsonObject>* ControlObject = nullptr;
        if (!ControlValue.IsValid() || !ControlValue->TryGetObject(ControlObject) || ControlObject == nullptr || !ControlObject->IsValid())
        {
            continue;
        }

        FString ControlCode;
        if ((*ControlObject)->TryGetStringField(TEXT("code"), ControlCode))
        {
            ControlCode = ControlCode.TrimStartAndEnd().ToUpper();
            if (!ControlCode.IsEmpty())
            {
                ControlCodes.Add(ControlCode);
            }
        }
    }
    return ControlCodes;
}

bool IsKnownControlCode(const FString& Code)
{
    return LoadKnownControlCodes().Contains(Code.TrimStartAndEnd().ToUpper());
}

void ParseMeasurementCode(const FString& InCode, FString& OutBaseCode, FString& OutControlCode, FString& OutParameter)
{
    TArray<FString> Tokens;
    InCode.TrimStartAndEnd().ParseIntoArrayWS(Tokens);
    if (Tokens.IsEmpty())
    {
        OutBaseCode.Empty();
        OutControlCode.Empty();
        return;
    }

    OutBaseCode = Tokens[0].TrimStartAndEnd().ToUpper();
    OutControlCode.Empty();
    if (Tokens.Num() >= 2 && IsKnownControlCode(Tokens[1]))
    {
        OutControlCode = Tokens[1].TrimStartAndEnd().ToUpper();
        if (OutParameter.IsEmpty() && Tokens.Num() >= 3)
        {
            OutParameter = Tokens[2].TrimStartAndEnd().ToUpper();
        }
    }
}

FString BuildDisplayShotCode(const FString& BaseCode, const FString& ControlCode)
{
    return ControlCode.IsEmpty() ? BaseCode : FString::Printf(TEXT("%s %s"), *BaseCode, *ControlCode);
}

}

FCSTopoProjectDocument UCSTopoProjectLibrary::CreateDefaultProject(const FString& ProjectName)
{
    FCSTopoProjectDocument Project;
    Project.ProjectName = ProjectName.IsEmpty() ? TEXT("Untitled CSTopo Project") : ProjectName;

    Project.SchemaVersion = TEXT("1.4");
    Project.CodePalette = LoadBuiltInCodePalette();
    Project.ActiveCode = Project.CodePalette.IsEmpty() ? TEXT("") : Project.CodePalette[0].Code;
    if (Project.CodePalette.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Code List did not load any codes. New projects will start without an active code."));
    }
    return Project;
}

bool UCSTopoProjectLibrary::SaveProjectToFile(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage)
{
    const FString NormalizedFilePath = NormalizeCSTopoFilePath(FilePath);
    FCSTopoProjectDocument ProjectToSave = Project;
    ProjectToSave.SchemaVersion = TEXT("1.4");
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

    const bool bLegacyLoopClosureSemantics =
        !Project.SchemaVersion.Equals(TEXT("1.3"), ESearchCase::CaseSensitive)
        && !Project.SchemaVersion.Equals(TEXT("1.4"), ESearchCase::CaseSensitive);
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
    Project.SchemaVersion = TEXT("1.4");
    ApplyBuiltInCodeMetadata(Project);

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

TArray<FCSTopoCodeStyle> UCSTopoProjectLibrary::LoadBuiltInCodePalette()
{
    const FString CodeListPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("CSTopoCodeList.json"));
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *CodeListPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Code List not found: %s"), *CodeListPath);
        return {};
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Code List could not be parsed: %s"), *CodeListPath);
        return {};
    }

    const TArray<TSharedPtr<FJsonValue>>* CodeValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("codes"), CodeValues))
    {
        UE_LOG(LogTemp, Warning, TEXT("CSTopo Code List is missing the codes array: %s"), *CodeListPath);
        return {};
    }

    TArray<FCSTopoCodeStyle> Palette;
    Palette.Reserve(CodeValues->Num());
    for (const TSharedPtr<FJsonValue>& CodeValue : *CodeValues)
    {
        const TSharedPtr<FJsonObject>* CodeObject = nullptr;
        if (!CodeValue.IsValid() || !CodeValue->TryGetObject(CodeObject) || CodeObject == nullptr || !CodeObject->IsValid())
        {
            continue;
        }

        FString Code;
        if (!(*CodeObject)->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
        {
            continue;
        }

        FCSTopoCodeStyle Style;
        Style.Code = Code.TrimStartAndEnd().ToUpper();
        Style.LayerName = Style.Code;
        (*CodeObject)->TryGetStringField(TEXT("name"), Style.DisplayName);
        (*CodeObject)->TryGetStringField(TEXT("category"), Style.Category);
        (*CodeObject)->TryGetStringField(TEXT("point_type"), Style.PointType);

        FString ColorText;
        if ((*CodeObject)->TryGetStringField(TEXT("color"), ColorText))
        {
            ParseHexColor(ColorText, Style.Color);
        }

        Style.bVisible = true;
        Palette.Add(Style);
    }

    return Palette;
}

bool UCSTopoProjectLibrary::FindBuiltInCodeStyle(const FString& Code, FCSTopoCodeStyle& OutStyle)
{
    const FString NormalizedCode = Code.TrimStartAndEnd().ToUpper();
    for (const FCSTopoCodeStyle& Style : LoadBuiltInCodePalette())
    {
        if (Style.Code.Equals(NormalizedCode, ESearchCase::IgnoreCase))
        {
            OutStyle = Style;
            return true;
        }
    }

    return false;
}

void UCSTopoProjectLibrary::ApplyBuiltInCodeMetadata(FCSTopoProjectDocument& Project)
{
    const TArray<FCSTopoCodeStyle> BuiltInPalette = LoadBuiltInCodePalette();
    if (BuiltInPalette.IsEmpty())
    {
        return;
    }

    Project.CodePalette = BuiltInPalette;

    const bool bActiveCodeInPalette = Project.CodePalette.ContainsByPredicate([&Project](const FCSTopoCodeStyle& Candidate)
    {
        return Candidate.Code.Equals(Project.ActiveCode, ESearchCase::IgnoreCase);
    });
    if (!bActiveCodeInPalette)
    {
        Project.ActiveCode = Project.CodePalette.IsEmpty() ? TEXT("") : Project.CodePalette[0].Code;
    }

    for (FCSTopoShotRecord& Shot : Project.Shots)
    {
        if (Shot.BaseCode.IsEmpty() || Shot.ControlCode.IsEmpty())
        {
            FString ParsedBaseCode;
            FString ParsedControlCode;
            FString ParsedParameter = Shot.ControlParameter;
            ParseMeasurementCode(Shot.Code, ParsedBaseCode, ParsedControlCode, ParsedParameter);
            if (Shot.BaseCode.IsEmpty())
            {
                Shot.BaseCode = ParsedBaseCode.IsEmpty() ? Shot.Code : ParsedBaseCode;
            }
            if (Shot.ControlCode.IsEmpty())
            {
                Shot.ControlCode = ParsedControlCode;
            }
            if (Shot.ControlParameter.IsEmpty())
            {
                Shot.ControlParameter = ParsedParameter;
            }
        }
    }

    for (FCSTopoFigureRecord& Figure : Project.Figures)
    {
        if (FCSTopoCodeStyle* Style = Project.CodePalette.FindByPredicate([&Figure](const FCSTopoCodeStyle& Candidate)
        {
            return Candidate.Code.Equals(Figure.Code, ESearchCase::IgnoreCase);
        }))
        {
            Figure.Style = *Style;
            Figure.LayerName = Style->LayerName;
        }
        else
        {
            FCSTopoCodeStyle EmptyStyle;
            EmptyStyle.Code = Figure.Code;
            EmptyStyle.DisplayName = Figure.Code;
            EmptyStyle.PointType = TEXT("Point (no triangulation)");
            EmptyStyle.LayerName = Figure.Code;
            EmptyStyle.bVisible = false;
            Figure.Style = EmptyStyle;
            Figure.LayerName = Figure.Code;
        }
    }
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

FCSTopoShotRecord UCSTopoProjectLibrary::AddFittedShot(FCSTopoProjectDocument& Project, const FString& Code, double Northing, double Easting, double Elevation, double Residual, const FString& SourceCloudId, const FString& ControlParameter, bool bJoinLinework)
{
    FString BaseCode;
    FString ControlCode;
    FString ResolvedControlParameter = ControlParameter;
    ParseMeasurementCode(Code, BaseCode, ControlCode, ResolvedControlParameter);

    const FCSTopoCodeStyle Style = FindOrCreateStyle(Project, BaseCode);
    FCSTopoFigureRecord* Figure = nullptr;
    if (bJoinLinework && DoesCSTopoPointTypeCreateFigureLinework(Style.PointType))
    {
        Figure = &FindOrCreateOpenFigure(Project, BaseCode);
    }

    FCSTopoShotRecord Shot;
    Shot.PointNumber = Project.NextPointNumber++;
    Shot.Northing = Northing;
    Shot.Easting = Easting;
    Shot.Elevation = Elevation;
    Shot.Code = BuildDisplayShotCode(BaseCode, ControlCode);
    Shot.BaseCode = BaseCode;
    Shot.ControlCode = ControlCode;
    Shot.ControlParameter = ResolvedControlParameter;
    if (Figure != nullptr)
    {
        Shot.FigureId = Figure->FigureId;
    }
    Shot.FitResidual = Residual;
    Shot.SourceCloudId = SourceCloudId;
    Shot.CreatedAt = FDateTime::UtcNow();

    Project.Shots.Add(Shot);
    if (Figure != nullptr)
    {
        Figure->PointNumbers.Add(Shot.PointNumber);
    }
    Project.ActiveCode = BaseCode;
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
        Source.bSurfaceRenderVisible = ManifestEntry->bSurfaceRenderVisible;
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
    if (FindBuiltInCodeStyle(Code, Style))
    {
        Project.CodePalette.Add(Style);
        return Style;
    }

    Style.Code = Code;
    Style.DisplayName = Code;
    Style.PointType = TEXT("Point (no triangulation)");
    Style.LayerName = Code;
    Style.bVisible = false;
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
