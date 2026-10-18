#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BDPhysicsSettings.h"
#include "BDBomb.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class ABDCharacter;
class UPhysicalMaterial;
class UMaterialInstanceDynamic;

UCLASS()
class BLASTDELIVERY568_API ABDBomb : public AActor
{
    GENERATED_BODY()
public:
    ABDBomb();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Ignite(ABDCharacter* Character, float Fuse);
    void Release(bool bThrow, bool bNotifyAction = true);
    void Nudge(const FVector& Direction);
    void Push(const FVector& DesiredVelocity, const FVector& Direction, float DeltaSeconds);
    bool TryPickup(ABDCharacter* Character);
    bool ApplySize(float Size);
    void ApplyExternalBlast(const FVector& VelocityChange);
    const FBDPhysicsSettings& GetProfile() const { return Profile; }
    float GetWorldRadius() const;
    ABDCharacter* GetHolder() const;
    bool WasThrown() const { return bThrown; }
    bool HasCollisionFuseAccelerated() const { return bCollisionFuseAccelerated; }
    float GetFuseBurnRate() const { return bCollisionFuseAccelerated ? Profile.CollisionFuseMultiplier : 1.f; }
    void Explode();
    bool IsHeld() const { return Holder.IsValid(); }
    float GetRemainingFuse() const { return RemainingFuse; }
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FuseTip;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Wick;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
private:
    TWeakObjectPtr<ABDCharacter> Holder;
    TWeakObjectPtr<ABDCharacter> IgnoredCharacter;
    UPROPERTY() TObjectPtr<UPhysicalMaterial> ContactMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;
    FBDPhysicsSettings Profile;
    float RemainingFuse = 4.f;
    float InitialFuse = 4.f;
    bool bExploded = false;
    bool bThrown = false;
    bool bCollisionFuseAccelerated = false;
    void FollowHolder();
    void ClearCharacterIgnore();
    void UpdatePresentation();
    UFUNCTION() void OnSolidHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
};
