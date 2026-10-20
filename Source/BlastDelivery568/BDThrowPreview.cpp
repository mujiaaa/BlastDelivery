#include "BDThrowPreview.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

FBDThrowPreview BDThrowPreview::Calculate(const ABDBomb* Bomb, const ABDCharacter* Player)
{
    FBDThrowPreview Result;
    if (!IsValid(Bomb) || !IsValid(Player) || Bomb->GetHolder()!=Player) return Result;
    UWorld* World=Bomb->GetWorld();
    if (!World) return Result;
    FVector Position=Bomb->GetActorLocation();
    FVector Velocity=Bomb->GetThrowVelocity(Player->GetActorForwardVector());
    Result.Points.Add(Position);
    const float FuseFlight=Bomb->IsBanana() ? 4.f : Bomb->GetRemainingFuse()/FMath::Max(.01f,Bomb->GetFuseBurnRate());
    const float Duration=FMath::Clamp(FuseFlight,0.f,4.f);
    constexpr float Step=1.f/120.f;
    const FVector Gravity(0,0,World->GetGravityZ());
    const float Damping=Bomb->Collision->GetLinearDamping();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ChargedThrowPreview),false,Bomb);
    Query.AddIgnoredActor(Player);
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Bomb->GetWorldRadius());
    for (int32 I=0; I<480 && Result.FlightSeconds<Duration; ++I)
    {
        const float Dt=FMath::Min(Step,Duration-Result.FlightSeconds);
        // Chaos applies acceleration, then linear drag, then integrates position.
        Velocity=(Velocity+Gravity*Dt)*FMath::Max(0.f,1.f-Damping*Dt);
        const FVector Next=Position+Velocity*Dt;
        FHitResult Hit;
        if (World->SweepSingleByChannel(Hit,Position,Next,FQuat::Identity,ECC_PhysicsBody,Shape,Query))
        {
            Result.bHit=true;
            Result.ContactPoint=Hit.ImpactPoint;
            Result.ContactNormal=Hit.ImpactNormal.GetSafeNormal();
            Result.FlightSeconds+=Dt*Hit.Time;
            Result.Points.Add(Hit.Location);
            return Result;
        }
        Position=Next;
        Result.FlightSeconds+=Dt;
        Result.Points.Add(Position);
    }
    Result.bFuseExpired=!Bomb->IsBanana() && FuseFlight<=4.f;
    Result.ContactPoint=Position;
    return Result;
}
