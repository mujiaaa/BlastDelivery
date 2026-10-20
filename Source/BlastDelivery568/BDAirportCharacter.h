#pragma once
#include "CoreMinimal.h"
#include "BDCharacter.h"
#include "BDCharacterMovement.h"
#include "BDAirportCharacter.generated.h"

// Only Airport has the donor's walking-over-low-rails and exhaust-field rules.
UCLASS()
class BLASTDELIVERY568_API UBDAirportMovement : public UBDCharacterMovement
{
    GENERATED_BODY()
public:
    virtual void CalcVelocity(float DeltaTime,float Friction,bool bFluid,float BrakingDeceleration) override;
    virtual void PerformMovement(float DeltaSeconds) override;
    virtual void StopMovementImmediately() override;
    void ResetAmbientWindHistory();
    FVector ActualPlanarVelocity=FVector::ZeroVector;
private:
    FVector PersonalPlanarVelocity=FVector::ZeroVector;
    bool bHadAmbientWind=false;
    bool bWasBlastLocked=false;
};

UCLASS()
class BLASTDELIVERY568_API ABDAirportCharacter : public ABDCharacter
{
    GENERATED_BODY()
public:
    ABDAirportCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void Respawn() override;
};
