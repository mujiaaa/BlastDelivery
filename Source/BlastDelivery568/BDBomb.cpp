#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDGameMode.h"
#include "BDVisuals.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
bool FindClearGrip(const ABDBomb* Bomb, const ABDCharacter* Character, float Radius, FVector& Position)
{
    const FVector CharacterPosition=Character->GetActorLocation();
    const float CapsuleRadius=Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float Feet=CharacterPosition.Z-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Desired=CharacterPosition+Character->GetActorForwardVector()*(CapsuleRadius+Radius+12.f)
        +FVector(0,0,15.f*Character->GetActorScale3D().Z);
    Desired.Z=FMath::Max(Desired.Z,Feet+Radius+4.f);
    FVector Start(CharacterPosition.X,CharacterPosition.Y,Desired.Z);
    const FVector OriginalStart=Start;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(HeldBombGrip),false,Bomb);
    Query.AddIgnoredActor(Character);
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Radius);
    UWorld* World=Bomb->GetWorld();
    // A large held bomb can start a sweep inside a wall even though its smaller
    // carrier fits. Resolve only a bounded local penetration before sweeping;
    // never accept Hit.Location while that hit still starts in solid geometry.
    for (int32 Attempt=0;Attempt<4;++Attempt)
    {
        FHitResult Hit;
        const bool bHit=World->SweepSingleByChannel(Hit,Start,Desired,FQuat::Identity,ECC_Visibility,Shape,Query);
        if (!bHit) { Position=Desired; return true; }
        if (!Hit.bStartPenetrating)
        {
            const FVector Candidate=Hit.Location+Hit.Normal*.5f;
            if (World->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Visibility,
                FCollisionShape::MakeSphere(FMath::Max(1.f,Radius-.25f)),Query)) return false;
            Position=Candidate;
            return true;
        }
        if (Hit.PenetrationDepth<=KINDA_SMALL_NUMBER || Hit.Normal.IsNearlyZero()) return false;
        Start+=Hit.Normal*(Hit.PenetrationDepth+.5f);
        if (FVector::DistSquared(Start,OriginalStart)>FMath::Square(Radius+CapsuleRadius+16.f)) return false;
    }
    return false;
}
}

ABDBomb::ABDBomb()
{
    PrimaryActorTick.bCanEverTick = true;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BombCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(38.f);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetCollisionObjectType(ECC_PhysicsBody);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Collision->SetNotifyRigidBodyCollision(true);
    Collision->SetUseCCD(true);
    // A moving throwable is not level geometry. In particular, banana bait
    // must not carve a hole in the navigation surface at its own destination.
    Collision->SetCanEverAffectNavigation(false);
    Collision->SetLinearDamping(.8f);
    Collision->SetAngularDamping(.5f);
    Collision->CanCharacterStepUpOn = ECB_No;
    Collision->OnComponentHit.AddDynamic(this,&ABDBomb::OnSolidHit);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bomb"));
    Mesh->SetupAttachment(Collision);
    BDVisuals::Shape(Mesh, TEXT("Sphere"));
    Mesh->SetRelativeScale3D(FVector(.76));
    Wick=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wick"));
    Wick->SetupAttachment(Collision);
    BDVisuals::Shape(Wick,TEXT("Cylinder"));
    Wick->SetRelativeLocation(FVector(0,0,46));
    Wick->SetRelativeScale3D(FVector(.075,.075,.18));
    FuseTip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fuse"));
    FuseTip->SetupAttachment(Collision);
    BDVisuals::Shape(FuseTip, TEXT("Sphere"));
    FuseTip->SetRelativeLocation(FVector(0,0,56));
    FuseTip->SetRelativeScale3D(FVector(.13));
    // Retained for existing API/test consumers; Chaos owns movement and collisions.
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->bAutoActivate = false;
    Movement->bSimulationEnabled = false;
    Movement->PrimaryComponentTick.bStartWithTickEnabled = false;
}
void ABDBomb::BeginPlay()
{
    Super::BeginPlay();
    BDVisuals::Material(Mesh, TEXT("Ink"));
    BDVisuals::Material(Wick,TEXT("CrateBand"));
    BDVisuals::Material(FuseTip, TEXT("BombGlow"));
    ShellMaterial=BDVisuals::Tint(Mesh,FLinearColor(.10f,.14f,.20f));
    BDVisuals::Tint(Wick,FLinearColor(1.f,.64f,.25f));
    BDVisuals::Tint(FuseTip,FLinearColor(1.f,.64f,.25f),true);
    ContactMaterial = NewObject<UPhysicalMaterial>(this);
    ContactMaterial->Friction = ContactMaterial->StaticFriction = .45f;
    ContactMaterial->Restitution = .28f;
    ContactMaterial->bOverrideFrictionCombineMode = true;
    ContactMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
    ContactMaterial->bOverrideRestitutionCombineMode = true;
    ContactMaterial->RestitutionCombineMode = EFrictionCombineMode::Max;
    Collision->SetPhysMaterialOverride(ContactMaterial);
}
bool ABDBomb::Ignite(ABDCharacter* Character, float Fuse)
{
    if (!IsValid(Character)) return false;
    if (const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
    {
        Profile = Mode->GetPhysicsSettings();

    }
    Profile.CollisionFuseMultiplier = FMath::Clamp(Profile.CollisionFuseMultiplier,2.f,20.f);
    Holder = Character;
    RemainingFuse = Fuse;
    InitialFuse=FMath::Max(.01f,Fuse);
    HeldChargeSeconds = 0.f;
    AppliedChargeScale = 1.f;
    bThrown = bExploded = bCollisionFuseAccelerated = false;
    Collision->SetSphereRadius(Profile.BombRadius*100.f);
    Collision->SetMassOverrideInKg(NAME_None,Profile.BombMass);
    Mesh->SetRelativeScale3D(FVector(Profile.BombRadius*2.f));
    Wick->SetRelativeLocation(FVector(0,0,Profile.BombRadius*100.f+8.f));
    FuseTip->SetRelativeLocation(FVector(0,0,Profile.BombRadius*100.f+18.f));
    FVector Grip;
    if (!FindClearGrip(this,Character,Profile.BombRadius*100.f*FMath::Clamp(Profile.BombSize,.5f,2.f),Grip)
        || !ApplySize(Profile.BombSize))
    {
        Holder.Reset();
        return false;
    }
    AddTickPrerequisiteActor(Character);
    FollowHolder();
    UpdatePresentation();
    return true;
}
float ABDBomb::GetWorldRadius() const
{
    return Collision->GetScaledSphereRadius();
}
ABDCharacter* ABDBomb::GetHolder() const
{
    return Holder.Get();
}
float ABDBomb::GetChargeFraction() const
{
    return bBanana ? 0.f : FMath::Clamp(HeldChargeSeconds/FMath::Max(.01f,Profile.ChargeSeconds),0.f,1.f);
}
FVector ABDBomb::GetThrowVelocity(const FVector& Forward) const
{
    const FVector Direction=Forward.GetSafeNormal2D();
    // Jungle bait keeps its original throw, independent of the new bomb charge settings.
    if (bBanana) return Direction*520.f+FVector(0,0,270.f);
    const float Alpha=FMath::Pow(GetChargeFraction(),FMath::Max(.01f,Profile.ChargeThrowExponent));
    const float Speed=FMath::Lerp(Profile.MinThrowSpeed,Profile.ThrowSpeed,Alpha);
    const float Lift=FMath::Lerp(Profile.MinThrowLift,Profile.ThrowLift,Alpha);
    return Direction*Speed*100.f+FVector(0,0,Lift*100.f);
}
FBDPhysicsSettings ABDBomb::GetExplosionProfile() const
{
    FBDPhysicsSettings Result=Profile;
    if (bBanana) return Result;
    const float Charge=GetChargeFraction();
    const float RadiusMultiplier=FMath::Lerp(1.f,FMath::Max(1.f,Profile.MaxChargeRadiusMultiplier),
        FMath::Pow(Charge,FMath::Max(.01f,Profile.ChargeRadiusExponent)));
    const float ImpulseMultiplier=FMath::Lerp(1.f,FMath::Max(1.f,Profile.MaxChargeImpulseMultiplier),
        FMath::Pow(Charge,FMath::Max(.01f,Profile.ChargeImpulseExponent)));
    // Derive every effect from the ignition snapshot; repeated reads and pickups never compound it.
    Result.BlastRadius*=RadiusMultiplier;
    Result.BlastImpulse*=ImpulseMultiplier;
    Result.PlayerBlastSpeed*=ImpulseMultiplier;
    Result.HeldRecoilSpeed*=ImpulseMultiplier;
    Result.HeldBlastLift*=ImpulseMultiplier;
    Result.GroundBlastLift*=ImpulseMultiplier;
    return Result;
}
float ABDBomb::GetChargeVisualScale() const
{
    return AppliedChargeScale;
}
bool ABDBomb::ApplySize(float Size)
{
    Size = FMath::Clamp(Size,.5f,2.f);
    const float OldRadius = GetWorldRadius();
    const float NewRadius = Collision->GetUnscaledSphereRadius()*Size;
    FVector Position = GetActorLocation();
    if (IsHeld() && NewRadius>OldRadius && !FindClearGrip(this,Holder.Get(),NewRadius,Position)) return false;
    if (!IsHeld() && NewRadius > OldRadius)
    {
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BombResize),false,this);
        if (GetWorld()->LineTraceSingleByChannel(Floor,Position,Position-FVector(0,0,OldRadius+5.f),ECC_Visibility,Query)
            && Floor.ImpactNormal.Z > .7f)
            Position.Z += NewRadius-OldRadius;
        if (GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_PhysicsBody,
            FCollisionShape::MakeSphere(FMath::Max(1.f,NewRadius-.5f)),Query)) return false;
    }
    const FVector Velocity = Collision->IsSimulatingPhysics() ? Collision->GetPhysicsLinearVelocity() : FVector::ZeroVector;
    const FVector Spin = Collision->IsSimulatingPhysics() ? Collision->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
    SetActorScale3D(FVector(Size));
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    if (Collision->IsSimulatingPhysics())
    {
        Collision->SetMassOverrideInKg(NAME_None,Profile.BombMass);
        Collision->SetPhysicsLinearVelocity(Velocity);
        Collision->SetPhysicsAngularVelocityInRadians(Spin);
    }
    if (IsHeld()) FollowHolder();
    return true;
}
void ABDBomb::FollowHolder()
{
    if (!Holder.IsValid()) return;
    FVector Position;
    if (FindClearGrip(this,Holder.Get(),GetWorldRadius(),Position))
        SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
}
void ABDBomb::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Consumer.IsValid())
    {
        SetActorLocation(Consumer->GetBombPosition()+FVector(0,0,3.f*FMath::Sin(GetWorld()->GetTimeSeconds()*5.f)));
        SetActorRotation(Consumer->GetActorRotation());
        return;
    }
    if (bExploded) return;
    if (auto* Game=GetWorld()->GetAuthGameMode<ABDGameMode>()) Game->PrepareBomb(this,Holder.Get());
    if (IsHeld()) FollowHolder();
    else
    {
        Movement->Velocity = Collision->GetPhysicsLinearVelocity();
        if (IgnoredCharacter.IsValid())
        {
            FVector Closest;
            const float Distance = IgnoredCharacter->GetCapsuleComponent()->GetClosestPointOnCollision(GetActorLocation(),Closest);
            if (Distance > GetWorldRadius()+4.f || IgnoredCharacter->IsRecovering()) ClearCharacterIgnore();
        }
    }
    if (bBanana) return;
    const float Elapsed=FMath::Max(0.f,DeltaSeconds);
    const float BurnRate=GetFuseBurnRate();
    if (IsHeld())
    {
        // Charging and burning run together. Even a long frame cannot add charge after detonation.
        const float LiveElapsed=FMath::Min(Elapsed,RemainingFuse/FMath::Max(.01f,BurnRate));
        HeldChargeSeconds=FMath::Min(FMath::Max(.01f,Profile.ChargeSeconds),HeldChargeSeconds+LiveElapsed);
        UpdateChargeSize();
    }
    RemainingFuse = FMath::Max(0.f,RemainingFuse-Elapsed*BurnRate);
    UpdatePresentation();
    if (RemainingFuse <= 0.f) Explode();
}
void ABDBomb::UpdateChargeSize()
{
    if (bBanana || !IsHeld()) return;
    const float DesiredScale=FMath::Lerp(1.f,FMath::Max(1.f,Profile.MaxChargeVisualScale),GetChargeFraction());
    if (DesiredScale<=AppliedChargeScale+KINDA_SMALL_NUMBER) return;
    const float LocalRadius=Profile.BombRadius*100.f*DesiredScale;
    const float WorldScale=GetWorldRadius()/FMath::Max(KINDA_SMALL_NUMBER,Collision->GetUnscaledSphereRadius());
    FVector Grip;
    // Retain the last safe size in a tight space. Charge and fuse still advance;
    // retry growth when the holder has more clearance, never grow during flight.
    if (!FindClearGrip(this,Holder.Get(),LocalRadius*WorldScale,Grip)) return;
    AppliedChargeScale=DesiredScale;
    Collision->SetSphereRadius(LocalRadius);
    Collision->SetMassOverrideInKg(NAME_None,Profile.BombMass);
    SetActorLocation(Grip,false,nullptr,ETeleportType::TeleportPhysics);
}
void ABDBomb::UpdatePresentation()
{
    if (bBanana) return;
    // The shell and spherical collision grow together so the thrown shell rests on the floor.
    // Actor scale remains the independent BombSize tuning and mass is preserved.
    const float VisualScale=GetChargeVisualScale();
    Mesh->SetRelativeScale3D(FVector(Profile.BombRadius*2.f*VisualScale));
    Wick->SetRelativeLocation(FVector(0,0,(Profile.BombRadius*100.f+8.f)*VisualScale));
    Wick->SetRelativeScale3D(FVector(.075f,.075f,.18f)*VisualScale);
    FuseTip->SetRelativeLocation(FVector(0,0,(Profile.BombRadius*100.f+18.f)*VisualScale));
    // Unity's fuse warning is driven by the original fuse fraction, so catching
    // or resizing a live bomb never makes its visual warning start over.
    const float Urgency=FMath::Clamp(1.f-RemainingFuse/InitialFuse,0.f,1.f);
    const float Pulse=.5f+.5f*FMath::Sin(GetWorld()->GetTimeSeconds()*FMath::Lerp(9.f,35.f,Urgency));
    if (ShellMaterial)
    {
        ShellMaterial->SetVectorParameterValue(TEXT("Color"),FMath::Lerp(FLinearColor(.10f,.14f,.20f),FLinearColor(1.f,.20f,.04f),Urgency*Pulse));
        ShellMaterial->SetVectorParameterValue(TEXT("EmissionColor"),FLinearColor(1.f,.19f,.01f)*(Pulse*Urgency*.6f));
    }
    FuseTip->SetRelativeScale3D(FVector((.11f+Pulse*.05f)*VisualScale));
}
void ABDBomb::ClearCharacterIgnore()
{
    if (IgnoredCharacter.IsValid())
    {
        Collision->IgnoreActorWhenMoving(IgnoredCharacter.Get(),false);
        IgnoredCharacter->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false);
    }
    IgnoredCharacter.Reset();
}
void ABDBomb::Release(bool bThrow, bool bNotifyAction)
{
    if (!Holder.IsValid()) return;
    ABDCharacter* Character = Holder.Get();
    if (auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->PrepareBomb(this,Character);
    FollowHolder();
    FVector Position = GetActorLocation();
    if (!bThrow)
    {
        const float FloorZ = Character->GetActorLocation().Z-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Desired(Position.X,Position.Y,FloorZ+GetWorldRadius()+3.f);
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BombDrop),false,this);
        Query.AddIgnoredActor(Character);
        const bool bHit = GetWorld()->SweepSingleByChannel(Hit,Position,Desired,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(GetWorldRadius()),Query);
        Position = bHit ? Hit.Location : Desired;
    }
    const FVector Velocity = bThrow ? GetThrowVelocity(Character->GetActorForwardVector()) : FVector::ZeroVector;
    RemoveTickPrerequisiteActor(Character);
    ClearCharacterIgnore();
    IgnoredCharacter = Character;
    Collision->IgnoreActorWhenMoving(Character,true);
    Character->GetCapsuleComponent()->IgnoreActorWhenMoving(this,true);
    Holder.Reset();
    bThrown = bThrow;
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetSimulatePhysics(true);
    Collision->SetMassOverrideInKg(NAME_None,Profile.BombMass);
    Collision->SetPhysicsLinearVelocity(Velocity);
    Collision->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Collision->WakeAllRigidBodies();
    Movement->Velocity = Velocity;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (bNotifyAction) Mode->OnBombReleased(this,bThrow);
}
bool ABDBomb::TryPickup(ABDCharacter* Character)
{
    auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!IsValid(Character) || Character->IsRecovering() || IsHeld() || IsBeingEaten() || bExploded || (!bBanana && RemainingFuse<=0.f)
        || !Mode || !Mode->IsGameplayInputAllowed()) return false;
    if (FVector::Dist(GetActorLocation(),Character->GetActorLocation()) > Mode->GetPhysicsSettings().PickupRadius*100.f
        || !Mode->HasBlastLineOfSight(Character->GetActorLocation(),this,Character)) return false;
    FVector Grip;
    if (!FindClearGrip(this,Character,GetWorldRadius(),Grip)) return false;
    ClearCharacterIgnore();
    Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Collision->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Collision->SetSimulatePhysics(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->Velocity = FVector::ZeroVector;
    Holder = Character;
    bThrown = false;
    AddTickPrerequisiteActor(Character);
    FollowHolder();
    // Charge, remaining fuse, profile and first-contact acceleration deliberately survive pickup.
    Mode->AssignBomb(this,Character);
    return true;
}
void ABDBomb::Push(const FVector& DesiredVelocity,const FVector& Direction,float DeltaSeconds)
{
    if (IsHeld() || bExploded || !Collision->IsSimulatingPhysics()) return;
    const FVector Planar = Direction.GetSafeNormal2D();
    const float DesiredSpeed = FMath::Max(0.f,FVector::DotProduct(DesiredVelocity,Planar));
    const float Delta = FMath::Clamp(DesiredSpeed-FVector::DotProduct(Collision->GetPhysicsLinearVelocity(),Planar),
        0.f,Profile.BombPushAcceleration*100.f*DeltaSeconds);
    if (Delta <= 0.f) return;
    Collision->AddImpulse(Planar*Delta,NAME_None,true);
    Collision->AddAngularImpulseInRadians(FVector::CrossProduct(FVector::UpVector,Planar)*(Delta/100.f),NAME_None,true);
}
void ABDBomb::Nudge(const FVector& Direction)
{
    Push(Direction.GetSafeNormal2D()*520.f,Direction,GetWorld()->GetDeltaSeconds());
}
void ABDBomb::ApplyExternalBlast(const FVector& VelocityChange)
{
    if (IsHeld() || bExploded || !Collision->IsSimulatingPhysics()) return;
    Collision->WakeAllRigidBodies();
    Collision->AddImpulse(VelocityChange,NAME_None,true);
}
void ABDBomb::OnSolidHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if (!bExploded && !IsHeld() && bThrown && Profile.bFastCollisionFuse && !bCollisionFuseAccelerated
        && OtherComponent && OtherActor != this
        && OtherComponent->GetCollisionResponseToChannel(Collision->GetCollisionObjectType()) == ECR_Block)
        bCollisionFuseAccelerated = true;
}
void ABDBomb::Explode()
{
    if (bExploded || bBanana) return;
    if (auto* Game=GetWorld()->GetAuthGameMode<ABDGameMode>()) Game->PrepareBomb(this,Holder.Get());
    if (bBanana) return;
    if (IsHeld()) FollowHolder();
    bExploded = true;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
    {
        Mode->ResolveBombExplosion(GetActorLocation(),Holder.Get(),this);
        Mode->OnBombExploded(this);
        Mode->ClearBomb(this);
    }
    Destroy();
}
void ABDBomb::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearCharacterIgnore();
    if (Holder.IsValid()) RemoveTickPrerequisiteActor(Holder.Get());
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ClearBomb(this);
    Super::EndPlay(Reason);
}

void ABDBomb::MakeBanana()
{
    if (bBanana) return;
    bBanana=true;
    AppliedChargeScale=1.f;
    Collision->SetSphereRadius(Profile.BombRadius*100.f);
    Collision->SetMassOverrideInKg(NAME_None,Profile.BombMass);
    BDVisuals::Material(Mesh,TEXT("JungleBanana"));
    Mesh->SetRelativeScale3D(FVector(.56,.22,.26));
    Mesh->SetRelativeRotation(FRotator(25,0,0));
    Wick->SetVisibility(false); FuseTip->SetVisibility(false);
}
bool ABDBomb::IsRestingBanana() const
{
    if (!bBanana || IsHeld() || IsBeingEaten() || IsActorBeingDestroyed()) return false;
    if (Collision->GetPhysicsLinearVelocity().SizeSquared()>FMath::Square(30.f)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(BananaSupport),false,this);
    return GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),GetActorLocation()-FVector(0,0,GetWorldRadius()+10.f),ECC_Visibility,Q) && Hit.ImpactNormal.Z>.4f;
}
void ABDBomb::DropAsBananaFood()
{
    // Tree fruit shares the existing pushable Chaos body and eating lifecycle,
    // but never claims a player's active bomb slot or starts an explosive fuse.
    MakeBanana();
    Tags.AddUnique(TEXT("TreeBanana"));
    Collision->SetSphereRadius(24.f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetSimulatePhysics(true);
    Collision->SetMassOverrideInKg(NAME_None,2.f);
    Collision->SetLinearDamping(2.f);
    Collision->SetPhysicsLinearVelocity(FVector(0,0,-40));
}
bool ABDBomb::PickUpBanana(ABDCharacter* Eater)
{
    if (!IsValid(Eater) || !IsRestingBanana()) return false;
    Consumer=Eater; SetOwner(Eater);
    Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Collision->SetSimulatePhysics(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->Velocity=FVector::ZeroVector;
    AddTickPrerequisiteActor(Eater);
    if (auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ClearBomb(this);
    return true;
}

ABDCharacter* ABDBomb::GetConsumer() const { return Consumer.Get(); }
