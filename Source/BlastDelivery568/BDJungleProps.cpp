#include "BDJungleProps.h"
#include "BDBomb.h"
#include "BDVisuals.h"
#include "Components/BoxComponent.h"

ABDBananaTree::ABDBananaTree()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Tree"));
    auto* Trunk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trunk"));
    Trunk->SetupAttachment(RootComponent);
    BDVisuals::Shape(Trunk,TEXT("Cylinder")); BDVisuals::Material(Trunk,TEXT("JungleBark"));
    Trunk->SetRelativeLocation(FVector(0,0,100)); Trunk->SetRelativeScale3D(FVector(.58,.58,2));
    Trunk->SetCollisionProfileName(TEXT("BlockAll"));
    Crown = CreateDefaultSubobject<USceneComponent>(TEXT("Crown"));
    Crown->SetupAttachment(RootComponent); Crown->SetRelativeLocation(FVector(0,0,205));
    for (int32 I=0;I<6;++I)
    {
        auto* Leaf = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Leaf%d"),I));
        Leaf->SetupAttachment(Crown); BDVisuals::Shape(Leaf,TEXT("Sphere"));
        BDVisuals::Material(Leaf,I%2?TEXT("JungleLeaf"):TEXT("JungleLeafLight"));
        const float A = I*PI/3.f;
        Leaf->SetRelativeLocation(FVector(FMath::Cos(A)*70,FMath::Sin(A)*70,8));
        Leaf->SetRelativeRotation(FRotator(-14,I*60,0)); Leaf->SetRelativeScale3D(FVector(1.65,.52,.16));
    }
    auto* Stem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FruitStem"));
    Stem->SetupAttachment(Crown); BDVisuals::Shape(Stem,TEXT("Cylinder")); BDVisuals::Material(Stem,TEXT("JungleLeaf"));
    Stem->SetRelativeLocation(FVector(45,100,-10)); Stem->SetRelativeScale3D(FVector(.08,.08,.70));
    Fruit.Add(Stem);
    for (int32 I=0;I<3;++I)
        for (int32 J=0;J<7;++J)
        {
            auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Banana%d_%d"),I,J));
            Part->SetupAttachment(Crown); BDVisuals::Shape(Part,TEXT("Sphere"));
            BDVisuals::Material(Part,J==0 || J==6 ? TEXT("JungleBark") : TEXT("JungleBanana"));
            // Large curved fruit hangs beyond the front leaves, readable from the game camera.
            const float A = -1.25f+J*(2.5f/6.f);
            Part->SetRelativeLocation(FVector(65+I*24+FMath::Sin(A)*34,75+FMath::Cos(A)*(25+I*7),-25+FMath::Sin(A)*20));
            Part->SetRelativeScale3D(J==0 || J==6 ? FVector(.14,.14,.14) : FVector(.27,.25,.22));
            Fruit.Add(Part);
        }
}
void ABDBananaTree::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    for (auto& Part:Fruit) Part->SetVisibility(bHasBananas && !bDropped);
}
void ABDBananaTree::HitByBomb()
{
    ShakeTime = .55f;
    if (!bHasBananas || (bDropped && DroppedFruit.IsValid())) return;
    // Fruit can be replenished after being eaten/lost; no puzzle soft lock.
    auto* Banana = GetWorld()->SpawnActor<ABDBomb>(GetActorLocation()+DropOffset,FRotator(0,25,0));
    if (!Banana) return;
    Banana->DropAsBananaFood();
    DroppedFruit = Banana; bDropped = true;
    for (auto& Part:Fruit) Part->SetVisibility(false);
}
void ABDBananaTree::ResetFruit()
{
    if (DroppedFruit.IsValid()) DroppedFruit->Destroy();
    DroppedFruit.Reset(); bDropped=false; ShakeTime=0;
    Crown->SetRelativeRotation(FRotator::ZeroRotator);
    for (auto& Part:Fruit) Part->SetVisibility(bHasBananas);
}
void ABDBananaTree::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ShakeTime = FMath::Max(0.f,ShakeTime-DeltaSeconds);
    Crown->SetRelativeRotation(FRotator(FMath::Sin(ShakeTime*35.f)*ShakeTime*15.f,0,0));
    if (bDropped && (!DroppedFruit.IsValid() || DroppedFruit->GetActorLocation().Z<-300.f)) ResetFruit();
}

ABDJungleDoor::ABDJungleDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    Size = FVector(300,55,135);
    LeftHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LeftHinge")); LeftHinge->SetupAttachment(RootComponent);
    RightHinge = CreateDefaultSubobject<USceneComponent>(TEXT("RightHinge")); RightHinge->SetupAttachment(RootComponent);
    for (int32 I=0;I<2;++I)
    {
        auto* Hinge = I==0 ? LeftHinge.Get() : RightHinge.Get();
        auto* Leaf = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("DoorLeaf%d"),I));
        Leaf->SetupAttachment(Hinge); BDVisuals::Shape(Leaf,TEXT("Cube")); BDVisuals::Material(Leaf,TEXT("JungleBark"));
        Leaves.Add(Leaf);
        auto* Lock = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("GoldenLock%d"),I));
        Lock->SetupAttachment(Hinge); BDVisuals::Shape(Lock,TEXT("Cube")); BDVisuals::Material(Lock,TEXT("JungleBanana"));
        Lock->SetRelativeScale3D(FVector(.32,.64,.38)); Locks.Add(Lock);
        auto* Hole = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Keyhole%d"),I));
        Hole->SetupAttachment(Lock); BDVisuals::Shape(Hole,TEXT("Sphere")); BDVisuals::Material(Hole,TEXT("Ink"));
        Hole->SetRelativeLocation(FVector(0,51,3)); Hole->SetRelativeScale3D(FVector(.32,.04,.32));
    }
}
void ABDJungleDoor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Mesh->SetVisibility(false);
    for (int32 I=0;I<2;++I)
    {
        const float Sign = I==0 ? 1.f : -1.f;
        (I==0?LeftHinge:RightHinge)->SetRelativeLocation(FVector(-Sign*Size.X*.5f,0,0));
        Leaves[I]->SetRelativeLocation(FVector(Sign*Size.X*.25f,0,Size.Z*.5f));
        Leaves[I]->SetRelativeScale3D(FVector(Size.X*.005f,Size.Y*.01f,Size.Z*.01f));
        Locks[I]->SetRelativeLocation(FVector(Sign*(Size.X*.5f-24),0,Size.Z*.7f));
    }
    Pose();
}
void ABDJungleDoor::Pose()
{
    const float T = FMath::Clamp((OpenTime-.3f)/.8f,0.f,1.f);
    const float Angle = 92.f*T*T*(3-2*T);
    LeftHinge->SetRelativeRotation(FRotator(0,Angle,0));
    RightHinge->SetRelativeRotation(FRotator(0,-Angle,0));
}
void ABDJungleDoor::BeginOpening() { if (!bOpen) bOpening=true; }
void ABDJungleDoor::ResetDoor()
{
    bOpen=bOpening=false; OpenTime=0.f;
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Pose();
}
void ABDJungleDoor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bOpening) return;
    OpenTime += DeltaSeconds; Pose();
    if (OpenTime>=1.1f)
    {
        bOpening=false; bOpen=true;
        Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

ABDBananaPackage::ABDBananaPackage()
{
    // Raised pictogram on the lid and front, built with the existing yellow material.
    for (int32 Face=0;Face<2;++Face)
        for (int32 Banana=0;Banana<3;++Banana)
            for (int32 I=0;I<5;++I)
            {
                auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("BananaLabel%d_%d_%d"),Face,Banana,I));
                Part->SetupAttachment(Mesh); BDVisuals::Shape(Part,TEXT("Sphere"));
                const float A=-1.f+I*.5f, X=FMath::Sin(A)*(18+Banana*4), Y=FMath::Cos(A)*(15+Banana*5)-18;
                Part->SetRelativeLocation(Face==0 ? FVector(X,Y,51) : FVector(X,-51,Y));
                Part->SetRelativeScale3D(Face==0 ? FVector(.12,.10,.025) : FVector(.12,.025,.10));
                Emblem.Add(Part);
            }
}
void ABDBananaPackage::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    for (auto& Part:Emblem) BDVisuals::Material(Part,TEXT("JungleBanana"));
}

ABDJungleSkidMark::ABDJungleSkidMark()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Skid"));
    for (int32 I=0;I<7;++I)
    {
        auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Smear%d"),I));
        Part->SetupAttachment(RootComponent); BDVisuals::Shape(Part,TEXT("Cube")); BDVisuals::Material(Part,TEXT("JungleBark"));
        Part->SetRelativeLocation(FVector(15+I*13,(I%3-1)*14,6.f));
        Part->SetRelativeRotation(FRotator(0,(I%2?1:-1)*(4+I*2),0));
        Part->SetRelativeScale3D(FVector(.5f+I*.11f,.025f+(I%3)*.015f,.012));
    }
}
