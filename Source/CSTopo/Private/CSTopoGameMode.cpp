#include "CSTopoGameMode.h"

#include "CSTopoPlayerController.h"
#include "CSTopoSurveyPawn.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"

ACSTopoGameMode::ACSTopoGameMode()
{
    DefaultPawnClass = ACSTopoSurveyPawn::StaticClass();
    PlayerControllerClass = ACSTopoPlayerController::StaticClass();
}

void ACSTopoGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
    {
        if (AExponentialHeightFog* FogActor = *It)
        {
            FogActor->SetActorHiddenInGame(true);
            FogActor->SetActorEnableCollision(false);
            FogActor->SetActorTickEnabled(false);

            if (UExponentialHeightFogComponent* FogComponent = FogActor->GetComponent())
            {
                FogComponent->SetVisibility(false, true);
                FogComponent->SetHiddenInGame(true, true);
                FogComponent->FogDensity = 0.0f;
                FogComponent->SetVolumetricFog(false);
                FogComponent->MarkRenderStateDirty();
            }
        }
    }
}
