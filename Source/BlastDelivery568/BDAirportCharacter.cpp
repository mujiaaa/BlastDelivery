#include "BDAirportCharacter.h"
#include "BDAirport.h"
#include "BDBomb.h"
#include "BDPlayerController.h"
#include "Engine/World.h"

ABDAirportCharacter::ABDAirportCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UBDAirportMovement>(ACharacter::CharacterMovementComponentName))
{
    GetCharacterMovement()->MaxStepHeight=32.f;
}

void ABDAirportCharacter::Respawn()
{
    if (IsRecovering()) return;
    if (auto* Airport=GetWorld()->GetAuthGameMode<ABDAlphaAirportGameMode>())
    {
        if (ABDBomb* Bomb=Airport->GetBombForPlayer(this))
            if (Bomb->GetHolder()==this) Bomb->Destroy();
        Airport->NotifyAirportRecovery(true);
    }
    if (auto* PC=Cast<ABDPlayerController>(GetController())) PC->ResetBombInput();
    if (auto* Move=Cast<UBDAirportMovement>(GetCharacterMovement())) Move->ResetAmbientWindHistory();
    Super::Respawn();
}

void UBDAirportMovement::ResetAmbientWindHistory()
{
    PersonalPlanarVelocity=ActualPlanarVelocity=FVector::ZeroVector;
    bHadAmbientWind=bWasBlastLocked=false;
}

void UBDAirportMovement::StopMovementImmediately()
{
    Super::StopMovementImmediately();
    ResetAmbientWindHistory();
}

void UBDAirportMovement::CalcVelocity(float Dt,float Friction,bool bFluid,float Braking)
{
    const auto* Airport=GetWorld() ? GetWorld()->GetAuthGameMode<ABDAlphaAirportGameMode>() : nullptr;
    const FVector Wind=Airport && CharacterOwner ? Airport->WindAAt(CharacterOwner->GetActorLocation()) : FVector::ZeroVector;
    const bool bWindDriven=!Wind.IsNearlyZero();
    // Separate prescribed exhaust displacement from player momentum: it must
    // neither accumulate as acceleration nor permit walking against turbine A.
    const bool bNewBlast=IsBlastLocked() && !bWasBlastLocked;
    if (bHadAmbientWind && !bNewBlast) { Velocity.X=PersonalPlanarVelocity.X; Velocity.Y=PersonalPlanarVelocity.Y; }
    const FVector Input=Acceleration;
    if (bWindDriven) Acceleration=FVector::ZeroVector;
    Super::CalcVelocity(Dt,Friction,bFluid,Braking);
    Acceleration=Input;
    PersonalPlanarVelocity=FVector(Velocity.X,Velocity.Y,0);
    Velocity+=Wind;
    bHadAmbientWind=bWindDriven;
    bWasBlastLocked=IsBlastLocked();
}

void UBDAirportMovement::PerformMovement(float Dt)
{
    const FVector Before=UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
    Super::PerformMovement(Dt);
    if (UpdatedComponent && Dt>SMALL_NUMBER)
        ActualPlanarVelocity=(UpdatedComponent->GetComponentLocation()-Before)/Dt;
    ActualPlanarVelocity.Z=0;
}
