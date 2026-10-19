#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BDCharacter.generated.h"

class UStaticMeshComponent;

UCLASS()
class BLASTDELIVERY568_API ABDCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ABDCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void MoveInRoom(const FVector2D& Input);
    virtual void FacePoint(const FVector& Point);
    void DrawBomb();
    void DropBomb();
    void ThrowBomb();
    virtual void Respawn();
    FVector GetBombPosition() const;
    FVector GetSpawnPosition() const { return SpawnTransform.GetLocation(); }
    UPROPERTY(EditAnywhere, Category="Movement") float WalkSpeed = 330.f;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Eyes;
    FTransform SpawnTransform;
    UStaticMeshComponent* AddPart(const FName Name, const FVector& Location, const FVector& Scale, bool bEye = false);
};
