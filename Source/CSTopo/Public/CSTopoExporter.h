#pragma once

#include "CoreMinimal.h"
#include "CSTopoTypes.h"
#include "CSTopoExporter.generated.h"

UCLASS()
class CSTOPO_API UCSTopoExporter : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "CSTopo|Export")
    static bool ExportCsv(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage);

    UFUNCTION(BlueprintCallable, Category = "CSTopo|Export")
    static bool ExportDxf(const FCSTopoProjectDocument& Project, const FString& FilePath, FString& ErrorMessage);
};
