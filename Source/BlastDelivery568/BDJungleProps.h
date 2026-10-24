#pragma once

#include "CoreMinimal.h"
#include "BDPuzzleActors.h"
#include "BDCrate.h"
#include "BDJungleProps.generated.h"

class ABDBomb;

UCLASS()
class BLASTDELIVERY568_API ABDBananaTree : public AActor
{
    GENERATED_BODY()
public:
    ABDBananaTree();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    void HitByBomb();
    void ResetFruit();
    UPROPERTY(EditAnywhere, Category="Tree") bool bHasBananas = true;
    UPROPERTY(EditAnywhere, Category="Tree") FVector DropOffset = FVector(115,-45,175);
    bool HasDroppedFruit() const { return bDropped; }
private:
    UPROPERTY() TObjectPtr<USceneComponent> Crown;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Fruit;
    TWeakObjectPtr<ABDBomb> DroppedFruit;
    bool bDropped = false;
    float ShakeTime = 0.f;
};

UCLASS()
class BLASTDELIVERY568_API ABDJungleDoor : public ABDGate
{
    GENERATED_BODY()
public:
    ABDJungleDoor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    void BeginOpening();
    void ResetDoor();
    bool IsOpening() const { return bOpening; }
    float GetOpeningProgress() const { return OpenTime; }
private:
    UPROPERTY() TObjectPtr<USceneComponent> LeftHinge;
    UPROPERTY() TObjectPtr<USceneComponent> RightHinge;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Locks;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Leaves;
    bool bOpening = false;
    float OpenTime = 0.f;
    void Pose();
};

UCLASS()
class BLASTDELIVERY568_API ABDBananaPackage : public ABDCrate
{
    GENERATED_BODY()
public:
    ABDBananaPackage();
    virtual void OnConstruction(const FTransform& Transform) override;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Emblem;
};

UCLASS()
class BLASTDELIVERY568_API ABDJungleSkidMark : public AActor
{
    GENERATED_BODY()
public:
    ABDJungleSkidMark();
};
