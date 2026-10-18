#pragma once

#include "CoreMinimal.h"
#include "BDGameMode.h"
#include "BDAlphaPlayground.generated.h"

// The Alpha room is assembled at runtime over the existing Tutorial map. Its
// saved actors and the teammate's tutorial maps are never edited or resaved.
UCLASS()
class BLASTDELIVERY568_API ABDAlphaPlaygroundGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDAlphaPlaygroundGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // Unity metres, Y-up -> Unreal centimetres, Z-up. Kept explicit so layout
    // comparisons and tests use the same convention as the physics migration.
    static FVector FromUnity(const FVector& Position);
    static FVector SizeFromUnity(const FVector& Size);
    bool IsRoomReady() const { return bRoomReady; }
private:
    void RemoveMapGameplayActors();
    void BuildRoom();
    bool TryPossessPlayer();
    bool bRoomReady = false;
    bool bIndicatorsOpen = false;
    float GateOpenProgress = 0.f;
    UPROPERTY() TObjectPtr<class ABDCharacter> PlaygroundPlayer;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> GateIndicators;
    UPROPERTY() TObjectPtr<AActor> GateVisual;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> PlateTread;
};
