#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSTopoTypes.h"
#include "CSTopoProjectLibrary.generated.h"

USTRUCT(BlueprintType)
struct FCSTopoPlaneFitResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo")
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo")
    double Elevation = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "CSTopo")
    double Residual = 0.0;
};

UCLASS()
class CSTOPO_API UCSTopoProjectLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static FCSTopoProjectDocument CreateDefaultProject(const FString& ProjectName);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static bool SaveProjectToFile(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static bool LoadProjectFromFile(const FString& FilePath, FCSTopoProjectDocument& Project, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static bool SaveCacheManifestToFile(const FCSTopoPointCloudCacheManifestDocument& Manifest, const FString& FilePath, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static bool LoadCacheManifestFromFile(const FString& FilePath, FCSTopoPointCloudCacheManifestDocument& Manifest, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static FCSTopoPointCloudCacheManifestDocument BuildCacheManifest(const FCSTopoProjectDocument& Project, const FString& ProjectPath);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static bool FindBuiltInCodeStyle(const FString& Code, FCSTopoCodeStyle& OutStyle);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static FCSTopoPlaneFitResult FitPlaneAtSurveyCoordinate(const TArray<FVector>& Samples, double Northing, double Easting);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static FCSTopoShotRecord AddFittedShot(UPARAM(ref) FCSTopoProjectDocument& Project, const FString& Code, double Northing, double Easting, double Elevation, double Residual, const FString& SourceCloudId, const FString& ControlParameter = TEXT(""), bool bJoinLinework = true);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static void SplitFigure(UPARAM(ref) FCSTopoProjectDocument& Project, const FString& Code);

    UFUNCTION(BlueprintCallable, Category = "CSTopo")
    static void CloseActiveFigure(UPARAM(ref) FCSTopoProjectDocument& Project, const FString& Code);

private:
    static TArray<FCSTopoCodeStyle> LoadBuiltInCodePalette();
    static void ApplyBuiltInCodeMetadata(UPARAM(ref) FCSTopoProjectDocument& Project);
    static void ApplyCacheManifest(UPARAM(ref) FCSTopoProjectDocument& Project, const FCSTopoPointCloudCacheManifestDocument& Manifest);
    static FCSTopoCodeStyle FindOrCreateStyle(FCSTopoProjectDocument& Project, const FString& Code);
    static FCSTopoFigureRecord* FindOpenFigure(FCSTopoProjectDocument& Project, const FString& Code);
    static FCSTopoFigureRecord& FindOrCreateOpenFigure(FCSTopoProjectDocument& Project, const FString& Code);
};
