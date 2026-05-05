#include "CSTopoPointCloudImport.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
const TCHAR* QgisPdalPath = TEXT("C:/Program Files/QGIS 3.40.12/bin/pdal.exe");

uint16 ReadUInt16(const TArray<uint8>& Bytes, int32 Offset)
{
    return static_cast<uint16>(Bytes[Offset]) | (static_cast<uint16>(Bytes[Offset + 1]) << 8);
}

uint32 ReadUInt32(const TArray<uint8>& Bytes, int32 Offset)
{
    return static_cast<uint32>(Bytes[Offset])
        | (static_cast<uint32>(Bytes[Offset + 1]) << 8)
        | (static_cast<uint32>(Bytes[Offset + 2]) << 16)
        | (static_cast<uint32>(Bytes[Offset + 3]) << 24);
}

uint64 ReadUInt64(const TArray<uint8>& Bytes, int32 Offset)
{
    uint64 Value = 0;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        Value |= static_cast<uint64>(Bytes[Offset + Index]) << (Index * 8);
    }
    return Value;
}

double ReadDouble(const TArray<uint8>& Bytes, int32 Offset)
{
    static_assert(sizeof(double) == sizeof(uint64), "Unexpected double size.");
    const uint64 Raw = ReadUInt64(Bytes, Offset);
    double Value = 0.0;
    FMemory::Memcpy(&Value, &Raw, sizeof(double));
    return Value;
}

FString ReadLasString(const TArray<uint8>& Bytes, int32 Offset, int32 Length)
{
    TArray<ANSICHAR> Chars;
    for (int32 Index = 0; Index < Length && Offset + Index < Bytes.Num(); ++Index)
    {
        const uint8 Byte = Bytes[Offset + Index];
        if (Byte == 0)
        {
            break;
        }
        Chars.Add(static_cast<ANSICHAR>(Byte));
    }

    Chars.Add(0);
    return FString(ANSI_TO_TCHAR(Chars.GetData())).TrimStartAndEnd();
}

bool ExtractLinearUnit(const FString& Wkt, FString& UnitName, double& UnitToMeters)
{
    const FString UnitToken = TEXT("UNIT[\"");
    int32 UnitStart = Wkt.Find(UnitToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    if (UnitStart == INDEX_NONE)
    {
        return false;
    }

    const int32 NameStart = UnitStart + UnitToken.Len();
    const int32 NameEnd = Wkt.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
    if (NameEnd == INDEX_NONE)
    {
        return false;
    }

    const int32 ValueStart = Wkt.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameEnd);
    if (ValueStart == INDEX_NONE)
    {
        return false;
    }

    int32 ValueEnd = Wkt.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart + 1);
    if (ValueEnd == INDEX_NONE)
    {
        ValueEnd = Wkt.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart + 1);
    }
    if (ValueEnd == INDEX_NONE)
    {
        return false;
    }

    UnitName = Wkt.Mid(NameStart, NameEnd - NameStart);
    UnitToMeters = FCString::Atod(*Wkt.Mid(ValueStart + 1, ValueEnd - ValueStart - 1));
    return !UnitName.IsEmpty() && UnitToMeters > 0.0;
}

bool IsExecutableFile(const FString& Path)
{
    return !Path.IsEmpty() && FPaths::FileExists(Path);
}

FDateTime GetFileTimestampOrDefault(const FString& Path)
{
    if (Path.IsEmpty() || !FPaths::FileExists(Path))
    {
        return FDateTime();
    }

    return IFileManager::Get().GetTimeStamp(*Path);
}

FString CacheStateToStatusMessage(const FCSTopoPointCloudSource& Source)
{
    switch (Source.CacheState)
    {
    case ECSTopoPointCloudCacheState::Ready:
        return Source.bCachePreferredForRuntime
            ? FString::Printf(TEXT("Using cache: %s"), *Source.CachePath)
            : FString::Printf(TEXT("Cache ready: %s"), *Source.CachePath);
    case ECSTopoPointCloudCacheState::Stale:
        return FString::Printf(TEXT("Cache is stale. Source is newer than %s"), *Source.CachePath);
    case ECSTopoPointCloudCacheState::Missing:
        return FString::Printf(TEXT("Cache missing: %s"), *Source.CachePath);
    case ECSTopoPointCloudCacheState::Building:
        return FString::Printf(TEXT("Building cache: %s"), *Source.CachePath);
    case ECSTopoPointCloudCacheState::Pending:
        return FString::Printf(TEXT("Cache planned: %s"), *Source.CachePath);
    case ECSTopoPointCloudCacheState::Failed:
        return FString::Printf(TEXT("Cache failed: %s"), *Source.CachePath);
    default:
        return TEXT("No cache state available.");
    }
}
}

bool UCSTopoPointCloudImport::CreateSourceRecord(const FCSTopoImportOptions& Options, FCSTopoPointCloudSource& Source, FString& ErrorMessage)
{
    if (Options.SourcePath.IsEmpty() || !FPaths::FileExists(Options.SourcePath))
    {
        ErrorMessage = FString::Printf(TEXT("Point cloud source does not exist: %s"), *Options.SourcePath);
        return false;
    }

    const FString Extension = FPaths::GetExtension(Options.SourcePath).ToLower();
    const bool bLooksLikeCopc = Options.SourcePath.ToLower().EndsWith(TEXT(".copc.laz"));
    if (Extension != TEXT("las") && Extension != TEXT("laz"))
    {
        ErrorMessage = TEXT("CSTopo imports .las, .laz, and .copc.laz files.");
        return false;
    }

    Source.SourceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Source.SourcePath = Options.SourcePath;
    Source.CoordinateSystemWkt = Options.CoordinateSystemOverrideWkt;
    Source.CacheFormat = bLooksLikeCopc ? ECSTopoPointCloudCacheFormat::COPC : Options.TargetCacheFormat;
    Source.DirectOpenPointThreshold = Options.DirectOpenPointThreshold;

    FCSTopoLasHeaderInfo Header;
    FString HeaderError;
    if (ReadLasHeader(Options.SourcePath, Header, HeaderError))
    {
        Source.PointCount = Header.PointCount;
        Source.BoundsMin = Header.BoundsMin;
        Source.BoundsMax = Header.BoundsMax;
        Source.CoordinateSystemWkt = Header.CoordinateSystemWkt.IsEmpty() ? Source.CoordinateSystemWkt : Header.CoordinateSystemWkt;
        Source.LinearUnitName = Header.LinearUnitName;
        Source.LinearUnitToMeters = Header.LinearUnitToMeters;
    }

    Source.bDirectOpenEligible = Source.PointCount <= Options.DirectOpenPointThreshold;

    if (bLooksLikeCopc)
    {
        Source.CachePath = Options.SourcePath;
    }
    else if (Source.CacheFormat == ECSTopoPointCloudCacheFormat::COPC)
    {
        const FString CacheDir = Options.CacheDirectory.IsEmpty() ? FPaths::GetPath(Options.SourcePath) : Options.CacheDirectory;
        const FString BaseName = FPaths::GetBaseFilename(Options.SourcePath);
        Source.CachePath = FPaths::Combine(CacheDir, BaseName + TEXT(".copc.laz"));
    }
    else
    {
        Source.CachePath = Options.SourcePath;
    }

    RefreshSourceRuntimeState(Source, ErrorMessage);

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoPointCloudImport::ReadLasHeader(const FString& SourcePath, FCSTopoLasHeaderInfo& Header, FString& ErrorMessage)
{
    Header = FCSTopoLasHeaderInfo();
    if (!FPaths::FileExists(SourcePath))
    {
        ErrorMessage = FString::Printf(TEXT("Point cloud source does not exist: %s"), *SourcePath);
        return false;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*SourcePath));
    if (!File)
    {
        ErrorMessage = FString::Printf(TEXT("Failed to open LAS file: %s"), *SourcePath);
        return false;
    }

    TArray<uint8> Bytes;
    Bytes.SetNumZeroed(375);
    const bool bRead = File->Read(Bytes.GetData(), Bytes.Num());
    if (!bRead || Bytes.Num() < 227 || Bytes[0] != 'L' || Bytes[1] != 'A' || Bytes[2] != 'S' || Bytes[3] != 'F')
    {
        ErrorMessage = TEXT("File does not contain a readable LAS header.");
        return false;
    }

    Header.bValid = true;
    Header.VersionMajor = Bytes[24];
    Header.VersionMinor = Bytes[25];
    Header.HeaderSize = ReadUInt16(Bytes, 94);
    Header.PointDataOffset = ReadUInt32(Bytes, 96);
    Header.VlrCount = ReadUInt32(Bytes, 100);
    Header.PointFormat = Bytes[104] & 0x3F;
    Header.PointRecordLength = ReadUInt16(Bytes, 105);
    Header.PointCount = ReadUInt32(Bytes, 107);

    if (Header.VersionMajor == 1 && Header.VersionMinor >= 4)
    {
        const uint64 ExtendedPointCount = ReadUInt64(Bytes, 247);
        if (ExtendedPointCount > 0)
        {
            Header.PointCount = static_cast<int64>(ExtendedPointCount);
        }
    }

    Header.Scale = FVector(ReadDouble(Bytes, 131), ReadDouble(Bytes, 139), ReadDouble(Bytes, 147));
    Header.Offset = FVector(ReadDouble(Bytes, 155), ReadDouble(Bytes, 163), ReadDouble(Bytes, 171));
    const double MaxX = ReadDouble(Bytes, 179);
    const double MinX = ReadDouble(Bytes, 187);
    const double MaxY = ReadDouble(Bytes, 195);
    const double MinY = ReadDouble(Bytes, 203);
    const double MaxZ = ReadDouble(Bytes, 211);
    const double MinZ = ReadDouble(Bytes, 219);
    Header.BoundsMin = FVector(MinX, MinY, MinZ);
    Header.BoundsMax = FVector(MaxX, MaxY, MaxZ);

    if (Header.PointDataOffset > Header.HeaderSize && Header.PointDataOffset <= 1048576)
    {
        TArray<uint8> HeaderAndVlrs;
        HeaderAndVlrs.SetNumZeroed(Header.PointDataOffset);
        if (File->Seek(0) && File->Read(HeaderAndVlrs.GetData(), HeaderAndVlrs.Num()))
        {
            int32 VlrOffset = Header.HeaderSize;
            for (int32 VlrIndex = 0; VlrIndex < Header.VlrCount && VlrOffset + 54 <= HeaderAndVlrs.Num(); ++VlrIndex)
            {
                const FString UserId = ReadLasString(HeaderAndVlrs, VlrOffset + 2, 16);
                const uint16 RecordId = ReadUInt16(HeaderAndVlrs, VlrOffset + 18);
                const uint16 RecordLength = ReadUInt16(HeaderAndVlrs, VlrOffset + 20);
                const int32 RecordDataOffset = VlrOffset + 54;
                if (RecordDataOffset + RecordLength > HeaderAndVlrs.Num())
                {
                    break;
                }

                if (UserId.Equals(TEXT("LASF_Projection"), ESearchCase::IgnoreCase) && (RecordId == 2111 || RecordId == 2112))
                {
                    const FString Wkt = ReadLasString(HeaderAndVlrs, RecordDataOffset, RecordLength);
                    if (!Wkt.IsEmpty())
                    {
                        Header.CoordinateSystemWkt = Wkt;
                        FString UnitName;
                        double UnitToMeters = 0.0;
                        if (ExtractLinearUnit(Wkt, UnitName, UnitToMeters))
                        {
                            Header.LinearUnitName = UnitName;
                            Header.LinearUnitToMeters = UnitToMeters;
                        }
                    }
                }

                VlrOffset = RecordDataOffset + RecordLength;
            }
        }
    }

    ErrorMessage.Empty();
    return true;
}

bool UCSTopoPointCloudImport::FindPdalExecutable(FString& PdalPath)
{
    const FString Configured = FPlatformMisc::GetEnvironmentVariable(TEXT("CSTOPO_PDAL_PATH"));
    if (IsExecutableFile(Configured))
    {
        PdalPath = Configured;
        return true;
    }

    const FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
    TArray<FString> SearchPaths;
    PathEnv.ParseIntoArray(SearchPaths, TEXT(";"), true);
    for (const FString& SearchPath : SearchPaths)
    {
        const FString Candidate = FPaths::Combine(SearchPath, TEXT("pdal.exe"));
        if (IsExecutableFile(Candidate))
        {
            PdalPath = Candidate;
            return true;
        }
    }

    if (IsExecutableFile(QgisPdalPath))
    {
        PdalPath = QgisPdalPath;
        return true;
    }

    PdalPath.Empty();
    return false;
}

FString UCSTopoPointCloudImport::BuildPdalCopcCommand(const FCSTopoImportOptions& Options)
{
    const FString CacheDir = Options.CacheDirectory.IsEmpty() ? FPaths::GetPath(Options.SourcePath) : Options.CacheDirectory;
    const FString BaseName = FPaths::GetBaseFilename(Options.SourcePath);
    const FString OutputPath = FPaths::Combine(CacheDir, BaseName + TEXT(".copc.laz"));
    FString PdalPath = TEXT("pdal");
    FindPdalExecutable(PdalPath);

    return FString::Printf(
        TEXT("\"%s\" translate \"%s\" \"%s\" --writer writers.copc"),
        *PdalPath,
        *Options.SourcePath,
        *OutputPath);
}

bool UCSTopoPointCloudImport::RefreshSourceRuntimeState(FCSTopoPointCloudSource& Source, FString& ErrorMessage)
{
    Source.bSourceExists = FPaths::FileExists(Source.SourcePath);
    Source.bCacheExists = !Source.CachePath.IsEmpty() && FPaths::FileExists(Source.CachePath);
    Source.SourceModifiedAt = GetFileTimestampOrDefault(Source.SourcePath);
    Source.CacheModifiedAt = GetFileTimestampOrDefault(Source.CachePath);
    Source.bDirectOpenEligible = Source.PointCount <= Source.DirectOpenPointThreshold || Source.PointCount <= 0;

    if (Source.CacheFormat == ECSTopoPointCloudCacheFormat::DirectLasLaz)
    {
        Source.bCachePreferredForRuntime = false;
        Source.bCacheOutOfDate = false;
        Source.CacheState = Source.bSourceExists ? ECSTopoPointCloudCacheState::Ready : ECSTopoPointCloudCacheState::Missing;
        Source.RuntimeDisplayPath = Source.SourcePath;
        Source.CacheStatus = Source.bSourceExists
            ? FString::Printf(TEXT("Using direct source path: %s"), *Source.SourcePath)
            : FString::Printf(TEXT("Source missing: %s"), *Source.SourcePath);
        ErrorMessage = Source.CacheStatus;
        return Source.bSourceExists;
    }

    if (Source.SourcePath.Equals(Source.CachePath, ESearchCase::IgnoreCase) && Source.bCacheExists)
    {
        Source.bCachePreferredForRuntime = true;
        Source.bCacheOutOfDate = false;
        Source.CacheState = ECSTopoPointCloudCacheState::Ready;
        Source.RuntimeDisplayPath = Source.CachePath;
        Source.CacheStatus = FString::Printf(TEXT("Using source COPC cache: %s"), *Source.CachePath);
        ErrorMessage = Source.CacheStatus;
        return true;
    }

    Source.bCacheOutOfDate = Source.bCacheExists && Source.SourceModifiedAt > FDateTime() && Source.CacheModifiedAt > FDateTime() && Source.SourceModifiedAt > Source.CacheModifiedAt;
    if (Source.bCacheExists)
    {
        Source.CacheState = Source.bCacheOutOfDate ? ECSTopoPointCloudCacheState::Stale : ECSTopoPointCloudCacheState::Ready;
    }
    else
    {
        Source.CacheState = ECSTopoPointCloudCacheState::Missing;
    }

    const bool bPreferCache = Source.bCacheExists && !Source.bCacheOutOfDate;
    const bool bCanUseSourceDirectly = Source.bSourceExists && (Source.bDirectOpenEligible || Source.CacheFormat == ECSTopoPointCloudCacheFormat::DirectLasLaz);
    Source.bCachePreferredForRuntime = bPreferCache || (!bCanUseSourceDirectly && Source.bCacheExists);
    Source.RuntimeDisplayPath = Source.bCachePreferredForRuntime ? Source.CachePath : Source.SourcePath;

    if (!Source.bSourceExists && !Source.bCacheExists)
    {
        Source.CacheState = ECSTopoPointCloudCacheState::Missing;
        Source.RuntimeDisplayPath.Empty();
        Source.CacheStatus = FString::Printf(TEXT("Source and cache are missing for %s"), *Source.SourceId);
        ErrorMessage = Source.CacheStatus;
        return false;
    }

    Source.CacheStatus = CacheStateToStatusMessage(Source);
    if (!Source.bCachePreferredForRuntime && Source.bSourceExists)
    {
        const FString RuntimeReason = Source.bDirectOpenEligible
            ? TEXT("Source is direct-open eligible.")
            : TEXT("Source is being used until cache is built.");
        Source.CacheStatus = FString::Printf(TEXT("%s Runtime path: %s"), *RuntimeReason, *Source.SourcePath);
    }

    ErrorMessage = Source.CacheStatus;
    return !Source.RuntimeDisplayPath.IsEmpty();
}

FString UCSTopoPointCloudImport::BuildDefaultCacheManifestPath(const FString& ProjectPath)
{
    if (ProjectPath.IsEmpty())
    {
        return FString();
    }

    FString NormalizedProjectPath = ProjectPath;
    FPaths::NormalizeFilename(NormalizedProjectPath);
    int32 LastDriveRootIndex = INDEX_NONE;
    for (int32 Index = 0; Index + 2 < NormalizedProjectPath.Len(); ++Index)
    {
        if (FChar::IsAlpha(NormalizedProjectPath[Index])
            && NormalizedProjectPath[Index + 1] == TEXT(':')
            && (NormalizedProjectPath[Index + 2] == TEXT('/') || NormalizedProjectPath[Index + 2] == TEXT('\\')))
        {
            LastDriveRootIndex = Index;
        }
    }
    if (LastDriveRootIndex > 0)
    {
        NormalizedProjectPath = NormalizedProjectPath.Mid(LastDriveRootIndex);
        FPaths::NormalizeFilename(NormalizedProjectPath);
    }
    const bool bDriveRooted = NormalizedProjectPath.Len() >= 3
        && FChar::IsAlpha(NormalizedProjectPath[0])
        && NormalizedProjectPath[1] == TEXT(':')
        && (NormalizedProjectPath[2] == TEXT('/') || NormalizedProjectPath[2] == TEXT('\\'));
    if (!bDriveRooted && FPaths::IsRelative(NormalizedProjectPath))
    {
        NormalizedProjectPath = FPaths::ConvertRelativePathToFull(NormalizedProjectPath);
        FPaths::NormalizeFilename(NormalizedProjectPath);
    }

    const FString Directory = FPaths::GetPath(NormalizedProjectPath);
    const FString BaseName = FPaths::GetBaseFilename(NormalizedProjectPath, false);
    FString ManifestPath = FPaths::Combine(Directory, BaseName + TEXT(".cachemanifest.json"));
    FPaths::NormalizeFilename(ManifestPath);
    return ManifestPath;
}

FString UCSTopoPointCloudImport::BuildDefaultRuntimeWindowPath(const FString& CacheDirectory, const FString& SourceId)
{
    if (CacheDirectory.IsEmpty() || SourceId.IsEmpty())
    {
        return FString();
    }

    return FPaths::Combine(CacheDirectory, TEXT("runtime"), FString::Printf(TEXT("%s_window.las"), *SourceId.Left(8)));
}

double UCSTopoPointCloudImport::ComputeRuntimeWindowSampleSpacing(double WindowRadiusSourceUnits, int32 TargetPointBudget)
{
    const double SafeRadius = FMath::Max(WindowRadiusSourceUnits, 1.0);
    const int32 SafeBudget = FMath::Max(TargetPointBudget, 1);
    const double WindowArea = FMath::Square(SafeRadius * 2.0);
    return FMath::Clamp(FMath::Sqrt(WindowArea / static_cast<double>(SafeBudget)), 0.15, SafeRadius);
}

FString UCSTopoPointCloudImport::BuildCopcWindowPipelineJson(
    const FCSTopoPointCloudSource& Source,
    const FString& OutputPath,
    const FVector& WindowCenterSource,
    double WindowRadiusSourceUnits,
    double SampleSpacingSourceUnits,
    double VerticalPaddingSourceUnits)
{
    const double XMin = WindowCenterSource.X - WindowRadiusSourceUnits;
    const double XMax = WindowCenterSource.X + WindowRadiusSourceUnits;
    const double YMin = WindowCenterSource.Y - WindowRadiusSourceUnits;
    const double YMax = WindowCenterSource.Y + WindowRadiusSourceUnits;
    const double ZMin = Source.BoundsMin.Z - VerticalPaddingSourceUnits;
    const double ZMax = Source.BoundsMax.Z + VerticalPaddingSourceUnits;

    return FString::Printf(
        TEXT("[\n")
        TEXT("  {\n")
        TEXT("    \"type\":\"readers.copc\",\n")
        TEXT("    \"filename\":\"%s\",\n")
        TEXT("    \"bounds\":\"([%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f])\"\n")
        TEXT("  },\n")
        TEXT("  {\n")
        TEXT("    \"type\":\"filters.sample\",\n")
        TEXT("    \"radius\":%.6f\n")
        TEXT("  },\n")
        TEXT("  {\n")
        TEXT("    \"type\":\"writers.las\",\n")
        TEXT("    \"filename\":\"%s\"\n")
        TEXT("  }\n")
        TEXT("]\n"),
        *Source.CachePath.Replace(TEXT("\\"), TEXT("/")),
        XMin,
        XMax,
        YMin,
        YMax,
        ZMin,
        ZMax,
        SampleSpacingSourceUnits,
        *OutputPath.Replace(TEXT("\\"), TEXT("/")));
}
