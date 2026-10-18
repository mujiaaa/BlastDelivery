#include "BDAlphaPlayground.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDVisuals.h"
#include "BDAlphaLook.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FActorSpawnParameters SpawnParameters()
{
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return Parameters;
}

void Identify(AActor* Actor, const FString& Name)
{
    Actor->Tags.Add(TEXT("AlphaPlayground"));
    Actor->Tags.Add(FName(*Name));
#if WITH_EDITOR
    Actor->SetActorLabel(Name);
#endif
}

AStaticMeshActor* Box(UWorld* World, const FString& Name, FVector Position, FVector Size,
    const TCHAR* Material, bool bSolid = false)
{
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
        ABDAlphaPlaygroundGameMode::FromUnity(Position), FRotator::ZeroRotator, SpawnParameters());
    Identify(Actor, Name);
    UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    BDVisuals::Shape(Mesh, TEXT("Cube"));
    BDAlphaLook::Material(Mesh, Material);
    Mesh->SetWorldScale3D(ABDAlphaPlaygroundGameMode::SizeFromUnity(Size) / 100.f);
    if (bSolid)
    {
        // Shape deliberately disables collision for decorations; floors must
        // explicitly restore both query and physics collision after that call.
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    return Actor;
}

void FloorSection(UWorld* World, const TCHAR* Name, float MinX, float MaxX, float MinZ, float MaxZ)
{
    Box(World, Name, FVector((MinX+MaxX)*.5f,-1.f,(MinZ+MaxZ)*.5f),
        FVector(MaxX-MinX,2.f,MaxZ-MinZ), TEXT("Floor"), true);
    for (float X=MinX; X<MaxX-.01f; X+=1.f)
        for (float Z=MinZ; Z<MaxZ-.01f; Z+=1.f)
        {
            const float Width=FMath::Min(1.f,MaxX-X), Depth=FMath::Min(1.f,MaxZ-Z);
            Box(World,TEXT("Alpha floor tile"),FVector(X+Width*.5f,.006f,Z+Depth*.5f),
                FVector(Width-.035f,.012f,Depth-.035f),TEXT("FloorLight"));
        }
}

ABDBarrier* Wall(UWorld* World, const TCHAR* Name, FVector Center, FVector Size, bool bAlongX, const TCHAR* Material=TEXT("Stone"))
{
    // ABDBarrier's root is on the floor; its collision remains high enough to
    // protect the puzzle route even when playtest sliders increase blast lift.
    ABDBarrier* Barrier=World->SpawnActor<ABDBarrier>(
        ABDAlphaPlaygroundGameMode::FromUnity(FVector(Center.X,0,Center.Z)),
        FRotator::ZeroRotator,SpawnParameters());
    Identify(Barrier,Name);
    Barrier->Configure(ABDAlphaPlaygroundGameMode::SizeFromUnity(Size));
    Barrier->Mesh->SetVisibility(false);
    const float Length=bAlongX ? Size.X : Size.Z;
    const int32 Count=FMath::CeilToInt(Length/1.35f);
    const float Step=Length/Count;
    for (int32 Row=0; Row<2; ++Row)
        for (int32 Index=0; Index<Count; ++Index)
        {
            const float Along=-Length*.5f+Step*(Index+.5f);
            const FVector Offset=bAlongX ? FVector(Along,(Row-.5f)*Size.Y*.5f,0)
                : FVector(0,(Row-.5f)*Size.Y*.5f,Along);
            const FVector BlockSize=bAlongX ? FVector(Step-.035f,Size.Y*.5f-.035f,Size.Z)
                : FVector(Size.X,Size.Y*.5f-.035f,Step-.035f);
            Box(World,FString(Name)+TEXT(" stone"),Center+Offset,BlockSize,Material);
        }
    return Barrier;
}

void Marker(UWorld* World, const TCHAR* Name, FVector Center, float Radius, const TCHAR* Material)
{
    // A few slim segments communicate the jump route without text or new art.
    constexpr int32 Segments=40;
    for (int32 Index=0; Index<Segments; ++Index)
    {
        const float Angle=(Index+.5f)*2.f*PI/Segments;
        const FVector Position=Center+FVector(FMath::Cos(Angle)*Radius,0,FMath::Sin(Angle)*Radius);
        AStaticMeshActor* Segment=Box(World,Name,Position,FVector(.07f,.015f,2.f*PI*Radius/Segments),Material);
        Segment->GetStaticMeshComponent()->SetCastShadow(false);
        Segment->SetActorRotation(FRotator(0,-FMath::RadiansToDegrees(Angle),0));
    }
}

void Line(UWorld* World, const TCHAR* Name, FVector Start, FVector End, float Width, const TCHAR* Material)
{
    const FVector A=ABDAlphaPlaygroundGameMode::FromUnity(Start), B=ABDAlphaPlaygroundGameMode::FromUnity(End);
    AStaticMeshActor* Segment=Box(World,Name,(Start+End)*.5f,FVector((End-Start).Size(),.015f,Width),Material);
    Segment->SetActorRotation((B-A).Rotation());
    Segment->GetStaticMeshComponent()->SetCastShadow(false);
}
}

ABDAlphaPlaygroundGameMode::ABDAlphaPlaygroundGameMode()
{
    bAlphaPlayground=true;
    DefaultPawnClass=nullptr;
}

FVector ABDAlphaPlaygroundGameMode::FromUnity(const FVector& Position)
{
    return FVector(Position.X,-Position.Z,Position.Y)*100.f;
}

FVector ABDAlphaPlaygroundGameMode::SizeFromUnity(const FVector& Size)
{
    return FVector(FMath::Abs(Size.X),FMath::Abs(Size.Z),FMath::Abs(Size.Y))*100.f;
}

void ABDAlphaPlaygroundGameMode::RemoveMapGameplayActors()
{
    TArray<AActor*> Existing;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor=*It;
        // Do not touch the controller, HUD, camera, lights, sky, post-process,
        // world settings or the saved map. This removal affects this run only.
        if (Actor->IsA<ABDCharacter>() || Actor->IsA<ABDBomb>() || Actor->IsA<ABDCrate>()
            || Actor->IsA<ABDBarrier>() || Actor->IsA<ABDPressurePlate>() || Actor->IsA<ABDGoal>()
            || Actor->IsA<AStaticMeshActor>() || Actor->IsA<APlayerStart>()) Existing.Add(Actor);
    }
    for (AActor* Actor : Existing) Actor->Destroy();
    Crate=nullptr;
    Goal=nullptr;
    Gate=nullptr;
    ActiveBomb=nullptr;
}

void ABDAlphaPlaygroundGameMode::BeginPlay()
{
    Super::BeginPlay();
    RemoveMapGameplayActors();
    BDAlphaLook::ConfigurePlaygroundLighting(GetWorld());
    BuildRoom();
    TryPossessPlayer();
}

void ABDAlphaPlaygroundGameMode::Tick(float DeltaSeconds)
{
    // During startup the controller may become available after GameMode begins.
    // No gameplay can run without its correctly positioned pawn and view.
    if (!bRoomReady && !TryPossessPlayer()) return;
    if (Gate && Gate->bOpen!=bIndicatorsOpen)
    {
        bIndicatorsOpen=Gate->bOpen;
        for (UStaticMeshComponent* Indicator : GateIndicators)
            if (IsValid(Indicator)) BDAlphaLook::Material(Indicator,bIndicatorsOpen ? TEXT("IndicatorActive") : TEXT("Indicator"));
        if (PlateTread) BDAlphaLook::Material(PlateTread,bIndicatorsOpen ? TEXT("PlateActive") : TEXT("Plate"));
    }
    if (GateVisual && Gate && Gate->bOpen && GateOpenProgress<1.f)
    {
        GateOpenProgress=FMath::Min(1.f,GateOpenProgress+DeltaSeconds/.85f);
        const float Eased=GateOpenProgress*GateOpenProgress*(3.f-2.f*GateOpenProgress);
        GateVisual->SetActorLocation(Gate->GetActorLocation()-FVector(0,0,370.f*Eased));
    }
    if (PlateTread)
    {
        const FVector Target(275,-560,bIndicatorsOpen ? 4.5f : 12.f);
        PlateTread->SetWorldLocation(FMath::VInterpConstantTo(PlateTread->GetComponentLocation(),Target,DeltaSeconds,30.f));
    }
    Super::Tick(DeltaSeconds);
}

bool ABDAlphaPlaygroundGameMode::TryPossessPlayer()
{
    ABDPlayerController* Controller=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!Controller || !PlaygroundPlayer) return false;
    if (Controller->GetPawn()!=PlaygroundPlayer) Controller->Possess(PlaygroundPlayer);
    Controller->ResetBombInput();
    Controller->SetPlaygroundView(FVector(0,-20,0),2240.f,2280.f);
    PlaygroundPlayer->FacePoint(PlaygroundPlayer->GetActorLocation()+FVector(100,0,0));
    bRoomReady=true;
    return true;
}

void ABDAlphaPlaygroundGameMode::BuildRoom()
{
    UWorld* World=GetWorld();
    // Same three supporting floor rectangles and lower-right hole as Unity's
    // saved second level. There is no invisible floor bridging this gap.
    FloorSection(World,TEXT("Alpha west courtyard"),-10.f,1.6f,-8.f,8.f);
    FloorSection(World,TEXT("Alpha east landing"),5.f,10.f,-8.f,8.f);
    FloorSection(World,TEXT("Alpha upper right courtyard"),1.6f,5.f,-2.7f,8.f);
    Box(World,TEXT("Alpha pit west face"),FVector(1.4f,-1.5f,-5.35f),FVector(.4f,3,5.3f),TEXT("Pit"),true);
    Box(World,TEXT("Alpha pit east face"),FVector(5.2f,-1.5f,-5.35f),FVector(.4f,3,5.3f),TEXT("Pit"),true);
    Box(World,TEXT("Alpha pit north face"),FVector(3.3f,-1.5f,-2.5f),FVector(3.4f,3,.4f),TEXT("Pit"),true);
    Box(World,TEXT("Alpha pit darkness"),FVector(3.3f,-3.08f,-5.35f),FVector(3.4f,.05f,5.3f),TEXT("Ink"));
    Box(World,TEXT("Alpha backdrop"),FVector(0,-3.3f,0),FVector(80,.04f,80),TEXT("Backdrop"));

    Wall(World,TEXT("Alpha north boundary"),FVector(0,1.6f,8),FVector(20.6f,3.2f,.6f),true);
    Wall(World,TEXT("Alpha south boundary"),FVector(0,1.6f,-8),FVector(20.6f,3.2f,.6f),true);
    Wall(World,TEXT("Alpha west boundary"),FVector(-10,1.6f,0),FVector(.6f,3.2f,16),false);
    Wall(World,TEXT("Alpha east boundary"),FVector(10,1.6f,0),FVector(.6f,3.2f,16),false);
    Box(World,TEXT("Alpha south foundation"),FVector(0,-1.6f,-8),FVector(20.6f,3.2f,.6f),TEXT("Floor"),true);
    Wall(World,TEXT("Alpha L wall spine"),FVector(0,1.6f,.65f),FVector(1.4f,3.2f,6.7f),false,TEXT("Wall"));
    Wall(World,TEXT("Alpha L wall foot"),FVector(2.35f,1.6f,-2),FVector(3.3f,3.2f,1.4f),true,TEXT("Wall"));

    Gate=World->SpawnActor<ABDGate>(FromUnity(FVector(0,0,5.85f)),FRotator::ZeroRotator,SpawnParameters());
    Identify(Gate,TEXT("Alpha latching gate"));
    Gate->Configure(SizeFromUnity(FVector(.55f,3.2f,3.7f)));
    Gate->Mesh->SetVisibility(false);
    GateVisual=World->SpawnActor<AActor>(Gate->GetActorLocation(),FRotator::ZeroRotator,SpawnParameters());
    Identify(GateVisual,TEXT("Alpha sinking iron grate"));
    USceneComponent* GateRoot=NewObject<USceneComponent>(GateVisual,TEXT("GrateRoot"));
    GateRoot->SetMobility(EComponentMobility::Movable);
    GateVisual->SetRootComponent(GateRoot);
    GateRoot->RegisterComponent();
    GateVisual->SetActorLocation(Gate->GetActorLocation());
    auto GatePiece=[&](const TCHAR* Name, FVector Local, FVector Size)
    {
        AStaticMeshActor* Piece=Box(World,Name,Local+FVector(0,0,5.85f),Size,TEXT("Gate"));
        Piece->AttachToActor(GateVisual,FAttachmentTransformRules::KeepWorldTransform);
    };
    for (int32 I=0;I<8;++I) GatePiece(TEXT("Alpha iron bar"),FVector(0,1.6f,-1.69f+I*.48f),FVector(.24f,3.15f,.14f));
    GatePiece(TEXT("Alpha upper gate brace"),FVector(0,2.85f,0),FVector(.35f,.22f,3.7f));
    GatePiece(TEXT("Alpha lower gate brace"),FVector(0,.45f,0),FVector(.35f,.22f,3.7f));
    Box(World,TEXT("Alpha gate threshold"),FVector(0,.028f,5.85f),FVector(.65f,.05f,3.7f),TEXT("Gate"));
    ABDPressurePlate* Plate=World->SpawnActor<ABDPressurePlate>(FromUnity(FVector(2.75f,0,5.6f)),FRotator::ZeroRotator,SpawnParameters());
    Identify(Plate,TEXT("Alpha weight plate"));
    Plate->Gate=Gate;
    Plate->Mesh->SetVisibility(false);
    Box(World,TEXT("Alpha plate socket"),FVector(2.75f,.035f,5.6f),FVector(1.85f,.06f,1.85f),TEXT("Gate"));
    PlateTread=Box(World,TEXT("Alpha plate press surface"),FVector(2.75f,.12f,5.6f),FVector(1.5f,.16f,1.5f),TEXT("Plate"))->GetStaticMeshComponent();
    for (float X : {-.38f,.38f})
    {
        AStaticMeshActor* Stripe=Box(World,TEXT("Alpha plate stripe"),FVector(2.75f+X,.211f,5.6f),FVector(.12f,.025f,.85f),TEXT("Light"));
        Stripe->AttachToComponent(PlateTread,FAttachmentTransformRules::KeepWorldTransform);
    }
    // Keep the original visible switch-to-gate connection and its amber/green
    // state cue, so the mechanism needs no instruction paragraph on screen.
    GateIndicators.Add(Box(World,TEXT("Alpha plate wire"),FVector(2.75f,.06f,6.25f),
        FVector(.085f,.025f,1.3f),TEXT("Indicator"))->GetStaticMeshComponent());
    GateIndicators.Add(Box(World,TEXT("Alpha gate wire"),FVector(1.7f,.06f,6.9f),
        FVector(2.1f,.025f,.085f),TEXT("Indicator"))->GetStaticMeshComponent());
    GateIndicators.Add(Box(World,TEXT("Alpha gate wire end"),FVector(.65f,.06f,6.375f),
        FVector(.085f,.025f,1.05f),TEXT("Indicator"))->GetStaticMeshComponent());
    for (float Z : {3.75f,7.75f})
    {
        AStaticMeshActor* Lamp=Box(World,TEXT("Alpha gate indicator"),FVector(0,3.35f,Z),
            FVector(.3f),TEXT("Indicator"));
        BDVisuals::Shape(Lamp->GetStaticMeshComponent(),TEXT("Sphere"));
        GateIndicators.Add(Lamp->GetStaticMeshComponent());
    }

    Goal=World->SpawnActor<ABDGoal>(FVector(700,-560,120),FRotator::ZeroRotator,SpawnParameters());
    Identify(Goal,TEXT("Alpha delivery goal"));
    Goal->Extent=FVector(180,180,120); // Supports the 200% (240 cm high) crate.
    Goal->OnConstruction(Goal->GetActorTransform());
    BDAlphaLook::Material(Goal->Mesh,TEXT("Goal"));
    Goal->Mesh->SetRelativeScale3D(FVector(3.6f,3.6f,.05f));
    Goal->Mesh->SetRelativeLocation(FVector(0,0,-117.5f));
    Box(World,TEXT("Alpha goal north edge"),FVector(7,.06f,7.4f),FVector(3.6f,.04f,.10f),TEXT("GoalOutline"));
    Box(World,TEXT("Alpha goal south edge"),FVector(7,.06f,3.8f),FVector(3.6f,.04f,.10f),TEXT("GoalOutline"));
    Box(World,TEXT("Alpha goal west edge"),FVector(5.2f,.06f,5.6f),FVector(.10f,.04f,3.6f),TEXT("GoalOutline"));
    Box(World,TEXT("Alpha goal east edge"),FVector(8.8f,.06f,5.6f),FVector(.10f,.04f,3.6f),TEXT("GoalOutline"));
    Marker(World,TEXT("Alpha delivery symbol"),FVector(7,.07f,5.6f),.65f,TEXT("GoalSymbol"));
    Marker(World,TEXT("Alpha takeoff marker"),FVector(.1f,.045f,-5.1f),.65f,TEXT("Takeoff"));
    Marker(World,TEXT("Alpha landing marker"),FVector(6.2f,.045f,-5.1f),.65f,TEXT("Landing"));
    Line(World,TEXT("Alpha takeoff arrow upper"),FVector(-.9f,.06f,-4.8f),FVector(-1.25f,.06f,-5.1f),.10f,TEXT("Takeoff"));
    Line(World,TEXT("Alpha takeoff arrow lower"),FVector(-1.25f,.06f,-5.1f),FVector(-.9f,.06f,-5.4f),.10f,TEXT("Takeoff"));

    Crate=World->SpawnActor<ABDCrate>(FromUnity(FVector(-4,.62f,1.6f)),FRotator::ZeroRotator,SpawnParameters());
    Identify(Crate,TEXT("Alpha delivery crate"));
    TArray<UStaticMeshComponent*> CrateMeshes;
    Crate->GetComponents<UStaticMeshComponent>(CrateMeshes);
    for (UStaticMeshComponent* Part : CrateMeshes)
        BDAlphaLook::Material(Part,Part==Crate->Mesh ? TEXT("Crate") : TEXT("CrateBand"));
    PlaygroundPlayer=World->SpawnActor<ABDCharacter>(FromUnity(FVector(-6.7f,.95f,-.4f)),FRotator::ZeroRotator,SpawnParameters());
    Identify(PlaygroundPlayer,TEXT("Alpha courier"));
}
