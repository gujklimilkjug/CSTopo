#pragma once

#include "CoreMinimal.h"
#include "CSTopoTypes.h"
#include "CSTopoPointCloudImport.generated.h"

USTRUCT(BlueprintType)
struct FCSTopoImportOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Import")
    FString SourcePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Import")
    FString CacheDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Import")
    int64 DirectOpenPointThreshold = 5000000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Import")
    ECSTopoPointCloudCacheFormat TargetCacheFormat = ECSTopoPointCloudCacheFormat::COPC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CSTopo|Import")
    FString CoordinateSystemOverrideWkt;
};

USTRUCT(BlueprintType)
struct FCSTopoLasHeaderInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 VersionMajor = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 VersionMinor = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 HeaderSize = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int64 PointDataOffset = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 VlrCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 PointFormat = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int32 PointRecordLength = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    int64 PointCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FVector Scale = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FVector Offset = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FVector BoundsMax = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FString CoordinateSystemWkt;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    FString LinearUnitName = TEXT("unknown");

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo|Import")
    double LinearUnitToMeters = 0.0;
};

UCLASS()
class CSTOPO_API UCSTopoPointCloudImport : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static bool CreateSourceRecord(const FCSTopoImportOptions& Options, FCSTopoPointCloudSource& Source, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static bool ReadLasHeader(const FString& SourcePath, FCSTopoLasHeaderInfo& Header, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static bool FindPdalExecutable(FString& PdalPath);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static FString BuildPdalCopcCommand(const FCSTopoImportOptions& Options);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static bool RefreshSourceRuntimeState(UPARAM(ref) FCSTopoPointCloudSource& Source, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static FString BuildDefaultCacheManifestPath(const FString& ProjectPath);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static FString BuildDefaultRuntimeWindowPath(const FString& CacheDirectory, const FString& SourceId);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static double ComputeRuntimeWindowSampleSpacing(double WindowRadiusSourceUnits, int32 TargetPointBudget);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Import")
    static FString BuildCopcWindowPipelineJson(
        const FCSTopoPointCloudSource& Source,
        const FString& OutputPath,
        const FVector& WindowCenterSource,
        double WindowRadiusSourceUnits,
        double SampleSpacingSourceUnits,
        double VerticalPaddingSourceUnits);
};
