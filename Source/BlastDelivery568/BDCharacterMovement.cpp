#include "BDCharacterMovement.h"
#include "Engine/World.h"

void UBDCharacterMovement::Configure(const FBDPhysicsSettings& InSettings)
{
    Settings = InSettings;
    MaxWalkSpeed = Settings.MoveSpeed * 100.f;
    MaxAcceleration = FMath::Max(Settings.MovementAcceleration * 100.f, 1.f);
    BrakingDecelerationWalking = Settings.MovementBraking * 100.f;
    GroundFriction = BrakingFriction = FallingLateralFriction = 0.f;
    BrakingDecelerationFalling = 0.f;
    AirControl = 1.f;
    AirControlBoostMultiplier = 1.f;
    if (GetWorld()) GravityScale = 981.f / FMath::Max(1.f, FMath::Abs(GetWorld()->GetGravityZ()));
}

void UBDCharacterMovement::StartBlastLock()
{
    BlastLockedUntil = GetWorld()->GetTimeSeconds() + Settings.BlastControlLock;
}

bool UBDCharacterMovement::IsBlastLocked() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < BlastLockedUntil;
}

void UBDCharacterMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
    if (!HasValidData() || DeltaTime < MIN_TICK_TIME || HasAnimRootMotion()) return;
    if (!IsMovingOnGround() && !IsFalling())
    {
        Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
        return;
    }
    const bool bLocked = GetWorld()->GetTimeSeconds() < BlastLockedUntil;
    const FVector Intent(Acceleration.X, Acceleration.Y, 0.f);
    const bool bHasInput = !Intent.IsNearlyZero();
    FVector Planar(Velocity.X, Velocity.Y, 0.f);
    if (IsFalling())
    {
        Planar *= FMath::Exp(-Settings.AirDrag * DeltaTime);
        if (!bLocked && bHasInput) Planar += Intent.GetSafeNormal() * Settings.AirSteering * 100.f * DeltaTime;
    }
    else if (!bLocked)
    {
        const float InputAmount = FMath::Clamp(Intent.Size() / FMath::Max(MaxAcceleration, 1.f), 0.f, 1.f);
        const FVector Target = Intent.GetSafeNormal() * MaxWalkSpeed * InputAmount;
        const float Rate = (bHasInput ? Settings.MovementAcceleration : Settings.MovementBraking) * 100.f;
        Planar = FMath::VInterpConstantTo(Planar, Target, DeltaTime, Rate);
    }
    Velocity.X = Planar.X;
    Velocity.Y = Planar.Y;
}
