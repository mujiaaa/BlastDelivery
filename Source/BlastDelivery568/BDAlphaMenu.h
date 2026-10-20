#pragma once
#include "CoreMinimal.h"
#include "BDGameMode.h"
#include "BDAlphaMenu.generated.h"
class ABDBarrier;
UCLASS()
class BLASTDELIVERY568_API ABDAlphaMenuGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDAlphaMenuGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<ABDCharacter> MenuPlayer;
    UPROPERTY() TArray<TObjectPtr<ABDBarrier>> Walls;
    bool bPossessed=false;
};
