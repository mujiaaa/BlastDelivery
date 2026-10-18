#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BDPuzzleActors.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class ABDCrate;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

UCLASS()
class BLASTDELIVERY568_API ABDBarrier : public AActor
{
    GENERATED_BODY()
public:
    ABDBarrier();
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Layout") void Configure(FVector InSize);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout") FVector Size = FVector(100, 600, 130);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class BLASTDELIVERY568_API ABDGate : public ABDBarrier
{
    GENERATED_BODY()
public:
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable, Category="Gate") void Open();
    UPROPERTY(BlueprintReadOnly, Category="Gate") bool bOpen = false;
};

UCLASS()
class BLASTDELIVERY568_API ABDPressurePlate : public AActor
{
    GENERATED_BODY()
public:
    ABDPressurePlate();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Mechanism") TObjectPtr<ABDGate> Gate;
    UPROPERTY(BlueprintReadOnly, Category="Mechanism") bool bActivated = false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class BLASTDELIVERY568_API ABDGoal : public AActor
{
    GENERATED_BODY()
public:
    ABDGoal();
    virtual void OnConstruction(const FTransform& Transform) override;
    bool ContainsSettledCrate(const ABDCrate* Crate) const;
    bool ContainsCrate(const ABDCrate* Crate) const;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, Category="Goal") FVector Extent = FVector(180,180,100);
    UPROPERTY(EditAnywhere, Category="Goal") float MaxSettledSpeed = 25.f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Region;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
};

// Unity's short floor-level blast pulse, independent of physical blast forces.
UCLASS()
class BLASTDELIVERY568_API ABDBlastFlash : public AActor
{
    GENERATED_BODY()
public:
    ABDBlastFlash();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void Configure(float RadiusCentimetres,float FloorHeightCentimetres=0.f);
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UPointLightComponent> Flash;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
    float Age = 0.f;
    float Radius = 360.f;
    float FloorHeight = 0.f;
    void UpdatePresentation();
};
