#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BDBomb.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class ABDCharacter;

UCLASS()
class BLASTDELIVERY568_API ABDBomb : public AActor
{
    GENERATED_BODY()
public:
    ABDBomb();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Ignite(ABDCharacter* Character, float Fuse);
    void Release(bool bThrow, bool bNotifyAction = true);
    void Nudge(const FVector& Direction);
    void Explode();
    bool IsHeld() const { return Holder.IsValid(); }
    ABDCharacter* GetHolder() const { return Holder.Get(); }
    void MakeBanana();
    bool IsBanana() const { return bBanana; }
    bool IsRestingBanana() const;
    bool PickUpBanana(ABDCharacter* Eater);
    bool IsBeingEaten() const { return Consumer.IsValid(); }
    ABDCharacter* GetConsumer() const { return Consumer.Get(); }
    float GetRemainingFuse() const { return RemainingFuse; }
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FuseTip;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
private:
    TWeakObjectPtr<ABDCharacter> Holder;
    TWeakObjectPtr<ABDCharacter> Consumer;
    float RemainingFuse = 2.5f;
    bool bExploded = false;
    bool bBanana = false;
};
