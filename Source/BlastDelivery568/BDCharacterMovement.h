#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BDPhysicsSettings.h"
#include "BDCharacterMovement.generated.h"

// Keep Unreal's floor finding, sweeps and sliding, but use the Alpha's motion model.
UCLASS()
class BLASTDELIVERY568_API UBDCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    void Configure(const FBDPhysicsSettings& InSettings);
    void StartBlastLock();
    void ClearBlastLock() { BlastLockedUntil = 0.f; }
    bool IsBlastLocked() const;
    virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
private:
    FBDPhysicsSettings Settings;
    float BlastLockedUntil = 0.f;
};
