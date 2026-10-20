#include "BDCrate.h"
#include "BDGameMode.h"
#include "BDVisuals.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"

ABDCrate::ABDCrate()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeavyCrate"));
    SetRootComponent(Mesh);
    BDVisuals::Shape(Mesh,TEXT("Cube"));
    Mesh->SetRelativeScale3D(FVector(1.2f));
    Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    Mesh->SetCollisionObjectType(ECC_PhysicsBody);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    Mesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Mesh->SetSimulatePhysics(true);
    Mesh->SetUseCCD(true);
    Mesh->SetNotifyRigidBodyCollision(true);
    Mesh->CanCharacterStepUpOn = ECB_No;
    Mesh->BodyInstance.bLockXRotation = true;
    Mesh->BodyInstance.bLockYRotation = true;
    Mesh->BodyInstance.bLockZRotation = false;
    Mesh->OnComponentHit.AddDynamic(this,&ABDCrate::OnSolidHit);
    for (int32 Index=0;Index<2;++Index)
    {
        auto* Strap = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Strap%d"),Index));
        Strap->SetupAttachment(Mesh);
        BDVisuals::Shape(Strap,TEXT("Cube"));
        Strap->SetRelativeScale3D(FVector(.10f,1.015f,1.015f));
        Strap->SetRelativeLocation(FVector(Index==0 ? -28 : 28,0,0));
        Straps.Add(Strap);
    }
}
void ABDCrate::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BDVisuals::Material(Mesh,TEXT("Crate"));
    for (auto& Strap:Straps) BDVisuals::Material(Strap,TEXT("CrateBand"));
}
void ABDCrate::BeginPlay()
{
    Super::BeginPlay();
    if (const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Profile = Mode->GetPhysicsSettings();
    // Saved actor instances may still contain the old all-axis rotation lock.
    Mesh->BodyInstance.bLockXRotation = true;
    Mesh->BodyInstance.bLockYRotation = true;
    Mesh->BodyInstance.bLockZRotation = false;
    Mesh->SetConstraintMode(EDOFMode::SixDOF);
    FrictionMaterial = NewObject<UPhysicalMaterial>(this);
    FrictionMaterial->Friction = FrictionMaterial->StaticFriction = .07f;
    FrictionMaterial->Restitution = .02f;
    FrictionMaterial->bOverrideFrictionCombineMode = true;
    FrictionMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
    FrictionMaterial->bOverrideRestitutionCombineMode = true;
    FrictionMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
    Mesh->SetPhysMaterialOverride(FrictionMaterial);
    ApplySettings(Profile);
    // Bring legacy map spawns clear of the floor after changing the base edge to 120 cm.
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CrateInitialFloor),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Floor,GetActorLocation()+FVector(0,0,100),
        GetActorLocation()-FVector(0,0,200),ECC_Visibility,Query) && Floor.ImpactNormal.Z>.7f)
    {
        const float MinimumZ = Floor.ImpactPoint.Z+Mesh->Bounds.BoxExtent.Z+1.f;
        if (GetActorLocation().Z<MinimumZ)
            SetActorLocation(FVector(GetActorLocation().X,GetActorLocation().Y,MinimumZ),false,nullptr,ETeleportType::TeleportPhysics);
    }
    SpawnTransform = GetActorTransform();
    SpawnFloorZ = GetActorLocation().Z-Mesh->Bounds.BoxExtent.Z;
}
bool ABDCrate::ApplySettings(const FBDPhysicsSettings& Settings)
{
    const float Size = FMath::Clamp(Settings.CrateSize,.5f,2.f);
    const float Previous = GetActorScale3D().X/1.2f;
    const FVector Position = GetActorLocation()+FVector(0,0,60.f*(Size-Previous));
    if (Size>Previous+KINDA_SMALL_NUMBER && GetWorld())
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CrateResize),false,this);
        if (GetWorld()->OverlapBlockingTestByChannel(Position,GetActorQuat(),ECC_PhysicsBody,
            FCollisionShape::MakeBox(FVector(60.f*Size-.5f)),Query)) return false;
    }
    const bool bSimulating = Mesh->IsSimulatingPhysics();
    const FVector Velocity = bSimulating ? Mesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
    const FVector Spin = bSimulating ? Mesh->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
    SetActorScale3D(FVector(1.2f*Size));
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Profile = Settings;
    Profile.CrateSize = Size;
    MassKg = Profile.CrateMass;
    LinearDamping = Profile.CrateDamping;
    Mesh->SetMassOverrideInKg(NAME_None,MassKg);
    Mesh->SetLinearDamping(LinearDamping);
    Mesh->SetAngularDamping(Profile.CrateAngularDamping);
    Mesh->SetPhysicsMaxAngularVelocityInRadians(Profile.CrateMaxAngularSpeed);
    if (bSimulating)
    {
        Mesh->SetPhysicsLinearVelocity(Velocity);
        Mesh->SetPhysicsAngularVelocityInRadians(Spin);
    }
    return true;
}
bool ABDCrate::IsSupported() const
{
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CrateSupport),false,this);
    return GetWorld()->LineTraceSingleByChannel(Floor,GetActorLocation(),
        GetActorLocation()-FVector(0,0,Mesh->Bounds.BoxExtent.Z+12.f),ECC_Visibility,Query)
        && Floor.ImpactNormal.Z>.5f;
}
void ABDCrate::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bRecovering) return;
    if (GetActorLocation().Z<Profile.FallKillHeight*100.f) { Respawn(); return; }
    if (!Mesh->IsSimulatingPhysics())
    {
        IncomingVelocity = FVector::ZeroVector;
        if (!IsSupported()) { Mesh->SetSimulatePhysics(true); Mesh->WakeAllRigidBodies(); StillTime=0.f; }
        return;
    }
    IncomingVelocity = Mesh->GetPhysicsLinearVelocity();
    if (MaxHorizontalSpeed>0.f && IncomingVelocity.Size2D()>MaxHorizontalSpeed)
    {
        const FVector XY = IncomingVelocity.GetSafeNormal2D()*MaxHorizontalSpeed;
        IncomingVelocity = FVector(XY.X,XY.Y,IncomingVelocity.Z);
        Mesh->SetPhysicsLinearVelocity(IncomingVelocity);
    }
    const bool bStill = IncomingVelocity.SizeSquared()<25.f
        && Mesh->GetPhysicsAngularVelocityInRadians().SizeSquared()<.0025f && IsSupported();
    StillTime = bStill ? StillTime+DeltaSeconds : 0.f;
    if (StillTime>.25f)
    {
        Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        Mesh->SetSimulatePhysics(false);
        IncomingVelocity = FVector::ZeroVector;
    }
}
void ABDCrate::ApplyBlast(const FVector& Origin,float Strength)
{
    if (bRecovering || Strength<=0.f) return;
    StillTime = 0.f;
    Mesh->SetSimulatePhysics(true);
    Mesh->WakeAllRigidBodies();
    FVector Direction = (GetActorLocation()-Origin).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;
    const FVector Impulse = Direction*Strength;
    FVector Contact = Mesh->GetCenterOfMass();
    Mesh->GetClosestPointOnCollision(Origin,Contact);
    Contact.Z = Mesh->GetCenterOfMass().Z;
    const FVector Lever = Contact-Mesh->GetCenterOfMass();
    const float YawImpulse = FVector::CrossProduct(Lever,Impulse).Z*Profile.CrateImpactTorqueScale;
    const float Inertia = FMath::Max(1.f,Mesh->GetInertiaTensor().Z);
    const float CurrentYaw = Mesh->GetPhysicsAngularVelocityInRadians().Z;
    const float DesiredYaw = FMath::Clamp(CurrentYaw+YawImpulse/Inertia,-Profile.CrateMaxAngularSpeed,Profile.CrateMaxAngularSpeed);
    IncomingVelocity = Mesh->GetPhysicsLinearVelocity()+Impulse/FMath::Max(Mesh->GetMass(),.01f);
    Mesh->AddImpulse(Impulse);
    Mesh->AddAngularImpulseInRadians(FVector(0,0,(DesiredYaw-CurrentYaw)*Inertia));
}
void ABDCrate::OnSolidHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if (!Mesh->IsSimulatingPhysics() || !OtherComponent || OtherComponent->IsSimulatingPhysics()
        || FMath::Abs(Hit.ImpactNormal.Z)>.35f) return;
    const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
    const FVector ObstacleVelocity = OtherComponent->GetComponentVelocity();
    const FVector RelativeIncoming = IncomingVelocity-ObstacleVelocity;
    const float Approach = -FVector::DotProduct(RelativeIncoming,Normal);
    if (Approach<=15.f) return;
    const FVector Velocity = Mesh->GetPhysicsLinearVelocity();
    const float Separating = FVector::DotProduct(Velocity-ObstacleVelocity,Normal);
    const float Missing = Approach*FMath::Clamp(Profile.CrateWallRestitution,0.f,1.f)-Separating;
    if (Missing<=0.f) return;
    FVector Rebound = Velocity+Normal*Missing;
    const FVector Planar = FVector(Rebound.X-ObstacleVelocity.X,Rebound.Y-ObstacleVelocity.Y,0.f);
    Rebound = Rebound-Planar+Planar.GetClampedToMaxSize(RelativeIncoming.Size2D());
    Mesh->SetPhysicsLinearVelocity(Rebound);
}
void ABDCrate::Respawn()
{
    if (bRecovering) return;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (Mode->RequestSceneReset()) return;
    bRecovering = true;
    Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Mesh->SetSimulatePhysics(false);
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    GetWorldTimerManager().SetTimer(RespawnTimer,this,&ABDCrate::FinishRespawn,FMath::Max(.01f,Profile.RespawnDelay),false);
}
void ABDCrate::FinishRespawn()
{
    FVector Position = SpawnTransform.GetLocation();
    Position.Z = SpawnFloorZ+60.f*Profile.CrateSize;
    SetActorLocationAndRotation(Position,SpawnTransform.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    Mesh->SetSimulatePhysics(true);
    Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Mesh->WakeAllRigidBodies();
    IncomingVelocity = FVector::ZeroVector;
    StillTime = 0.f;
    bRecovering = false;
}

void ABDCrate::SetRespawnCheckpoint(FVector WorldPosition)
{
    SpawnTransform.SetLocation(WorldPosition);
    SpawnFloorZ=WorldPosition.Z-(60.f*GetActorScale3D().Z/1.2f);
}

void ABDCrate::ApplyExternalAcceleration(const FVector& Acceleration)
{
    if (bRecovering || Acceleration.IsNearlyZero() || Acceleration.ContainsNaN()) return;
    // Conveyors / fans are intentional forces, so release the parked-cargo lock
    // before Chaos integrates this frame. Ordinary character contact still cannot push cargo.
    StillTime=0.f;
    Mesh->SetSimulatePhysics(true);
    Mesh->WakeAllRigidBodies();
    Mesh->AddForce(Acceleration*Mesh->GetMass());
}
