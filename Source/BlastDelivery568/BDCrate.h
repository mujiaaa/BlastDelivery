#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BDPhysicsSettings.h"
#include "BDCrate.generated.h"

class UStaticMeshComponent;
class UPhysicalMaterial;

UCLASS()
class BLASTDELIVERY568_API ABDCrate : public AActor
{
    GENERATED_BODY()
public:
    ABDCrate();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    void ApplyBlast(const FVector& Origin, float Strength);
    void ApplyExternalAcceleration(const FVector& Acceleration);
    void Respawn();
    bool ApplySettings(const FBDPhysicsSettings& Settings);
    bool IsRecovering() const { return bRecovering; }
    void SetRespawnCheckpoint(FVector WorldPosition);
    FVector GetSpawnPosition() const { return SpawnTransform.GetLocation(); }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(EditAnywhere, Category="Physics") float MassKg = 30.f;
    UPROPERTY(EditAnywhere, Category="Physics") float MaxHorizontalSpeed = 0.f;
    UPROPERTY(EditAnywhere, Category="Physics") float LinearDamping = 1.15f;
private:
    UPROPERTY() TObjectPtr<UPhysicalMaterial> FrictionMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Straps;
    FTransform SpawnTransform;
    FBDPhysicsSettings Profile;
    FVector IncomingVelocity = FVector::ZeroVector;
    float StillTime = 0.f;
    float SpawnFloorZ = 0.f;
    bool bRecovering = false;
    FTimerHandle RespawnTimer;
    bool IsSupported() const;
    void FinishRespawn();
    UFUNCTION() void OnSolidHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
};
