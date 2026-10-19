#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDGameMode.h"
#include "BDVisuals.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

ABDBomb::ABDBomb()
{
    PrimaryActorTick.bCanEverTick = true;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BombCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(18.f);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetCollisionObjectType(ECC_WorldDynamic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bomb"));
    Mesh->SetupAttachment(Collision);
    BDVisuals::Shape(Mesh, TEXT("Sphere"));
    Mesh->SetRelativeScale3D(FVector(.36));
    FuseTip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fuse"));
    FuseTip->SetupAttachment(Collision);
    BDVisuals::Shape(FuseTip, TEXT("Sphere"));
    FuseTip->SetRelativeLocation(FVector(0,0,23));
    FuseTip->SetRelativeScale3D(FVector(.09));
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->bAutoActivate = false;
    Movement->bShouldBounce = true;
    Movement->Bounciness = .22f;
    Movement->Friction = .65f;
    Movement->BounceVelocityStopSimulatingThreshold = 25.f;
    Movement->bForceSubStepping = true;
    Movement->MaxSimulationTimeStep = 1.f / 120.f;
    Movement->MaxSimulationIterations = 16;
}

void ABDBomb::BeginPlay()
{
    Super::BeginPlay();
    BDVisuals::Material(Mesh, TEXT("Ink"));
    BDVisuals::Material(FuseTip, TEXT("BombGlow"));
}

void ABDBomb::Ignite(ABDCharacter* Character, float Fuse)
{
    Holder = Character;
    RemainingFuse = Fuse;
    SetActorLocation(Character->GetBombPosition());
    AddTickPrerequisiteActor(Character);
}
void ABDBomb::MakeBanana()
{
    if (bBanana) return;
    bBanana = true;
    BDVisuals::Material(Mesh, TEXT("JungleBanana"));
    Mesh->SetRelativeScale3D(FVector(.56,.22,.26));
    Mesh->SetRelativeRotation(FRotator(25,0,0));
    BDVisuals::Material(FuseTip, TEXT("JungleLeaf"));
}
bool ABDBomb::IsRestingBanana() const
{
    return bBanana && !IsHeld() && !IsBeingEaten() && !Movement->UpdatedComponent;
}
bool ABDBomb::PickUpBanana(ABDCharacter* Eater)
{
    // Claim once. A second wild man cannot steal food that is already being eaten.
    if (!IsValid(Eater) || !IsRestingBanana()) return false;
    Consumer = Eater;
    SetOwner(Eater);
    Movement->StopMovementImmediately();
    Movement->Deactivate();
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AddTickPrerequisiteActor(Eater);
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ClearBomb(this);
    return true;
}

void ABDBomb::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Consumer.IsValid())
    {
        // Keep the food visible in the wild man's hands until the meal finishes.
        const float Bite = 3.f * FMath::Sin(GetWorld()->GetTimeSeconds()*5.f);
        SetActorLocation(Consumer->GetActorLocation()+Consumer->GetActorForwardVector()*32.f+FVector(0,0,24.f+Bite));
        SetActorRotation(Consumer->GetActorRotation());
        return;
    }
    if (Holder.IsValid())
    {
        // Sweep from the player to prevent drawing/rotating a held bomb through a wall.
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(HeldBomb), false, this);
        Params.AddIgnoredActor(Holder.Get());
        const FVector Start = Holder->GetActorLocation();
        const FVector End = Holder->GetBombPosition();
        const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
            ECC_Visibility, FCollisionShape::MakeSphere(18.f), Params);
        SetActorLocation(bHit ? Hit.Location : End);
    }
    // Bananas are food: holding, landing and waiting never consume a fuse.
    if (bBanana) return;
    RemainingFuse -= DeltaSeconds;
    FuseTip->SetRelativeScale3D(FVector(.09f + .03f * FMath::Sin(RemainingFuse * 22.f)));
    if (RemainingFuse <= 0.f) Explode();
}

void ABDBomb::Release(bool bThrow, bool bNotifyAction)
{
    if (!Holder.IsValid()) return;
    ABDCharacter* Character = Holder.Get();
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->PrepareBomb(this, Character);
    const FVector Direction = Character->GetActorForwardVector();
    if (!bThrow) SetActorLocation(Character->GetActorLocation() + FVector(0,0,-40));
    RemoveTickPrerequisiteActor(Character);
    Holder.Reset();
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Movement->SetUpdatedComponent(Collision);
    auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    Movement->Velocity = bThrow && Mode ? Direction * Mode->ThrowSpeed + FVector(0,0,Mode->ThrowLift) : FVector::ZeroVector;
    Movement->Activate(true);
    if (Mode && bNotifyAction) Mode->OnBombReleased(this, bThrow);
}

void ABDBomb::Nudge(const FVector& Direction)
{
    if (IsHeld() || IsBeingEaten()) return;
    Movement->SetUpdatedComponent(Collision);
    Movement->Velocity = Direction * 180.f + FVector(0,0,45.f);
    Movement->Activate(true);
}

void ABDBomb::Explode()
{
    if (bExploded || bBanana) return;
    auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (Mode) Mode->PrepareBomb(this, Holder.Get());
    // Entering a banana zone on the final fuse frame must still convert safely.
    if (bBanana) return;
    bExploded = true;
    if (Mode)
    {
        Mode->ResolveBombExplosion(GetActorLocation(), Holder.Get(), this);
        Mode->OnBombExploded(this);
        Mode->ClearBomb(this);
    }
    Destroy();
}

void ABDBomb::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ClearBomb(this);
    Super::EndPlay(Reason);
}
