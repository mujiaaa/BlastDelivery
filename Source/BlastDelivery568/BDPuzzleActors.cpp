#include "BDPuzzleActors.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDVisuals.h"
#include "BDGameMode.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

ABDBarrier::ABDBarrier()
{
    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("FullHeightCollision"));
    SetRootComponent(Collision);
    Collision->SetCollisionProfileName(TEXT("BlockAll"));
    Collision->SetGenerateOverlapEvents(false);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisibleWall"));
    Mesh->SetupAttachment(Collision);
    BDVisuals::Shape(Mesh, TEXT("Cube"));
}
void ABDBarrier::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Configure(Size);
}
void ABDBarrier::Configure(FVector InSize)
{
    Size = InSize;
    // The visible low wall preserves the overhead view. Collision continues well above
    // the 175 cm jump apex and below the floor, so neither gates nor walls can be hopped.
    Collision->SetBoxExtent(FVector(Size.X*.5f, Size.Y*.5f, 2000.f));
    Mesh->SetRelativeScale3D(Size / 100.f);
    Mesh->SetRelativeLocation(FVector(0,0,Size.Z*.5f));
    BDVisuals::Material(Mesh, IsA<ABDGate>() ? TEXT("Gate") : TEXT("Wall"));
}
void ABDGate::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BDVisuals::Material(Mesh, TEXT("Gate"));
}
void ABDGate::Open()
{
    if (bOpen) return;
    bOpen = true;
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetVisibility(false);
}

ABDPressurePlate::ABDPressurePlate()
{
    PrimaryActorTick.bCanEverTick = true;
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PlateTrigger"));
    SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(80,80,24));
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
    Mesh->SetupAttachment(Trigger);
    BDVisuals::Shape(Mesh, TEXT("Cube"));
    Mesh->SetRelativeScale3D(FVector(1.6,1.6,.1));
    Mesh->SetRelativeLocation(FVector(0,0,5));
}
void ABDPressurePlate::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BDVisuals::Material(Mesh, TEXT("Plate"));
}
void ABDPressurePlate::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bActivated) return;
    bool bPressed=false;
    for (TActorIterator<ABDCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->GetCharacterMovement()->IsMovingOnGround()) continue;
        const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FVector Local=GetActorTransform().InverseTransformPosition(Feet);
        bPressed |= FMath::Abs(Local.X)<80.f && FMath::Abs(Local.Y)<80.f && FMath::Abs(Local.Z)<20.f;
    }
    for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It)
    {
        const FBox Bounds=It->Mesh->Bounds.GetBox();
        const FVector P=GetActorLocation();
        bPressed |= Bounds.Min.X<P.X+80.f && Bounds.Max.X>P.X-80.f && Bounds.Min.Y<P.Y+80.f
            && Bounds.Max.Y>P.Y-80.f && FMath::Abs(Bounds.Min.Z-P.Z)<20.f;
    }
    if (bPressed)
    {
        bActivated = true;
        BDVisuals::Material(Mesh, TEXT("Goal"));
        Mesh->SetRelativeLocation(FVector(0,0,2));
        if (Gate) Gate->Open();
    }
}

ABDGoal::ABDGoal()
{
    PrimaryActorTick.bCanEverTick=true;
    Region = CreateDefaultSubobject<UBoxComponent>(TEXT("DeliveryRegion"));
    SetRootComponent(Region);
    Region->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GoalFloor"));
    Mesh->SetupAttachment(Region);
    BDVisuals::Shape(Mesh, TEXT("Cube"));
}
void ABDGoal::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Region->SetBoxExtent(Extent);
    Mesh->SetRelativeScale3D(FVector(Extent.X*.02f, Extent.Y*.02f, .06f));
    Mesh->SetRelativeLocation(FVector(0,0,-Extent.Z+3.f));
    BDVisuals::Material(Mesh, TEXT("Goal"));
}
bool ABDGoal::ContainsCrate(const ABDCrate* Crate) const
{
    if (!Crate || !Crate->Mesh) return false;
    const FBox Bounds = Crate->Mesh->Bounds.GetBox();
    // Check all eight corners in goal-local space: a partial overlap never wins.
    for (int32 Corner = 0; Corner < 8; ++Corner)
    {
        const FVector P((Corner&1) ? Bounds.Max.X : Bounds.Min.X,
                        (Corner&2) ? Bounds.Max.Y : Bounds.Min.Y,
                        (Corner&4) ? Bounds.Max.Z : Bounds.Min.Z);
        const FVector Local = GetActorTransform().InverseTransformPosition(P);
        // Chaos rests with a small contact/penetration tolerance. Keep the visible
        // horizontal boundary exact, but don't reject a settled box for sub-cm floor slop.
        if (FMath::Abs(Local.X) > Extent.X || FMath::Abs(Local.Y) > Extent.Y || FMath::Abs(Local.Z) > Extent.Z + 2.f) return false;
    }
    return true;
}
bool ABDGoal::ContainsSettledCrate(const ABDCrate* Crate) const
{
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    const float Speed=Mode ? Mode->GetPhysicsSettings().GoalSpeedLimit*100.f : MaxSettledSpeed;
    const float Spin=Mode ? Mode->GetPhysicsSettings().GoalAngularSpeedLimit : .2f;
    return ContainsCrate(Crate) && Crate->Mesh->GetPhysicsLinearVelocity().Size() <= Speed
        && Crate->Mesh->GetPhysicsAngularVelocityInRadians().Size() <= Spin;
}
void ABDGoal::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!Mode || !Mode->SupportsGoalAssist() || Mode->bCompleted || !Mode->GetPhysicsSettings().bGoalAssist) return;
    const FBox Area=Region->Bounds.GetBox();
    for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It)
    {
        auto* Body=It->Mesh.Get();
        if (!Body->IsSimulatingPhysics()) continue;
        const FVector V=Body->GetPhysicsLinearVelocity();
        if (V.Size2D()<3.f || FMath::Abs(V.Z)>30.f) continue;
        const FBox Bounds=Body->Bounds.GetBox();
        const FVector Half=Bounds.GetExtent(), Center=Bounds.GetCenter();
        const FVector Low=Area.Min+Half+FVector(6,6,0), High=Area.Max-Half-FVector(6,6,0);
        if (Low.X>High.X || Low.Y>High.Y) continue;
        const float GapX=FMath::Max(0.f,FMath::Max(Area.Min.X-Bounds.Max.X,Bounds.Min.X-Area.Max.X));
        const float GapY=FMath::Max(0.f,FMath::Max(Area.Min.Y-Bounds.Max.Y,Bounds.Min.Y-Area.Max.Y));
        const float Range=Mode->GetPhysicsSettings().GoalAssistRange*100.f;
        if (FMath::Sqrt(GapX*GapX+GapY*GapY)>Range) continue;
        const FVector Target(FMath::Clamp(Center.X,Low.X,High.X),FMath::Clamp(Center.Y,Low.Y,High.Y),Center.Z);
        FCollisionQueryParams Q(SCENE_QUERY_STAT(GoalAssist),false,*It);
        Q.AddIgnoredActor(this);
        auto Supported=[&](const FVector& P)
        {
            FHitResult Floor;
            const FVector Start(P.X,P.Y,Bounds.Min.Z+10.f);
            return GetWorld()->LineTraceSingleByChannel(Floor,Start,Start-FVector(0,0,28),ECC_Visibility,Q)
                && Floor.ImpactNormal.Z>.7f;
        };
        bool bSafe=true;
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Center,Target)/20.f));
        for (int32 I=0; I<=Steps && bSafe; ++I)
        {
            const FVector Point=FMath::Lerp(Center,Target,float(I)/Steps);
            bSafe=Supported(Point);
            for (int32 Corner=0;Corner<4 && bSafe;++Corner)
                bSafe=Supported(Point+FVector((Corner&1?1:-1)*(Half.X-3),(Corner&2?1:-1)*(Half.Y-3),0));
        }
        if (!bSafe) continue;
        FHitResult Wall;
        const FVector SweepHalf(FMath::Max(1.f,Half.X-2),FMath::Max(1.f,Half.Y-2),FMath::Max(1.f,Half.Z-7));
        if (GetWorld()->SweepSingleByChannel(Wall,Center+FVector(0,0,5),Target+FVector(0,0,5),FQuat::Identity,
            ECC_Visibility,FCollisionShape::MakeBox(SweepHalf),Q)) continue;
        const FVector Delta=Target-Center;
        const float Drag=ContainsCrate(*It) ? 2.f : .3f;
        const FVector Acceleration=Delta.GetSafeNormal2D()*FMath::Min(115.f,Delta.Size2D()*4.f)-FVector(V.X,V.Y,0)*Drag;
        Body->AddForce(Acceleration*Body->GetMass());
    }
}

ABDBlastFlash::ABDBlastFlash()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("PulseRoot"));
    SetRootComponent(Root);
    Ring=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PulseRing"));
    Ring->SetupAttachment(Root);
    BDVisuals::Shape(Ring,TEXT("Cube"));
    Ring->SetCastShadow(false);
    Flash=CreateDefaultSubobject<UPointLightComponent>(TEXT("BlastLight"));
    Flash->SetupAttachment(Root);
    Flash->SetLightColor(FLinearColor(1.f,.45f,.08f));
    Flash->SetUseInverseSquaredFalloff(false);
    Flash->SetCastShadows(false);
    Flash->SetIntensity(5.f);
}
void ABDBlastFlash::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AlphaLook/M_AlphaEffect.M_AlphaEffect")))
    {
        RingMaterial=UMaterialInstanceDynamic::Create(Base,this);
        RingMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(1.f,.60f,.12f));
        Ring->SetMaterial(0,RingMaterial);
    }
    else
    {
        BDVisuals::Material(Ring,TEXT("BombGlow"));
        RingMaterial=BDVisuals::Tint(Ring,FLinearColor(1.f,.60f,.12f),true);
    }
    for (int32 I=0;I<64;++I) Ring->AddInstance(FTransform::Identity);
    UpdatePresentation();
}
void ABDBlastFlash::Configure(float RadiusCentimetres,float FloorHeightCentimetres)
{
    Radius=FMath::Max(0.f,RadiusCentimetres);
    FloorHeight=FloorHeightCentimetres;
    UpdatePresentation();
}
void ABDBlastFlash::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    UpdatePresentation();
    if (Age>=.4f) Destroy();
}
void ABDBlastFlash::UpdatePresentation()
{
    const float T=FMath::Clamp(Age/.4f,0.f,1.f);
    const float Size=Radius*(1.f-FMath::Pow(1.f-T,3.f));
    Ring->SetWorldLocation(FVector(GetActorLocation().X,GetActorLocation().Y,FloorHeight+12.f));
    for (int32 I=0;I<Ring->GetInstanceCount();++I)
    {
        const float A=2.f*PI*I/64.f;
        const float B=2.f*PI*(I+1)/64.f;
        const FVector From(FMath::Cos(A)*Size,FMath::Sin(A)*Size,0);
        const FVector To(FMath::Cos(B)*Size,FMath::Sin(B)*Size,0);
        const FVector Delta=To-From;
        // Sixty-four short flat strips match the Unity LineRenderer loop.
        const FTransform Transform(Delta.Rotation(),(From+To)*.5f,FVector(FMath::Max(.001f,Delta.Size()/100.f),.14f,.02f));
        Ring->UpdateInstanceTransform(I,Transform,false,I==Ring->GetInstanceCount()-1,true);
    }
    if (RingMaterial) RingMaterial->SetScalarParameterValue(TEXT("Opacity"),1.f-T);
    Flash->SetAttenuationRadius(Radius*2.f);
    Flash->SetIntensity((1.f-T)*5.f);
}
