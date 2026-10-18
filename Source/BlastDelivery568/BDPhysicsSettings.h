#pragma once

#include "CoreMinimal.h"
#include "BDPhysicsSettings.generated.h"

// Designer values use the Unity Alpha's metres, seconds, kilograms and radians.
// Conversion to Unreal centimetres happens at the simulation boundary, not in UI.
USTRUCT(BlueprintType)
struct BLASTDELIVERY568_API FBDPhysicsSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PlayerSize = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BombSize = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateSize = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MoveSpeed = 5.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MovementAcceleration = 14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MovementBraking = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastControlLock = .48f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AirDrag = 1.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AirSteering = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FuseSeconds = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ThrowSpeed = 5.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ThrowLift = 2.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BombRadius = .38f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BombMass = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BombPushAcceleration = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastRadius = 3.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastImpulse = 340.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PlayerBlastSpeed = 17.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float HeldRecoilSpeed = 8.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float HeldBlastLift = 6.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GroundBlastLift = 7.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PickupRadius = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RespawnDelay = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FallKillHeight = -4.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateMass = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateDamping = 1.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateWallRestitution = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateAngularDamping = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateImpactTorqueScale = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrateMaxAngularSpeed = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GoalSpeedLimit = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GoalAngularSpeedLimit = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GoalSettleSeconds = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bFastCollisionFuse = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CollisionFuseMultiplier = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bGoalAssist = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float GoalAssistRange = .75f;
};

struct FBDTuningParameter
{
    FName Id;
    FString Label;
    int32 Group;
    float Min;
    float Max;
    float FBDPhysicsSettings::* Field;
};
