#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CSTopoGameMode.generated.h"

UCLASS()
class CSTOPO_API ACSTopoGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ACSTopoGameMode();

protected:
    virtual void BeginPlay() override;
};
