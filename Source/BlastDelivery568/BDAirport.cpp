#include "BDAirport.h"
#include "BDAirportCharacter.h"
#include "BDAirportMesh.h"
#include "BDAlphaLook.h"
#include "BDAlphaSession.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDVisuals.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodyInstance.h"

namespace BDAirport
{
constexpr float WindX=34.925f, RampX=39.3f, RampStart=3.4f, RampEnd=9.4f;
constexpr float RampHeight=2.34f, HoldHeight=2.22f, AStart=33.15f, AEnd=36.7f;
constexpr float ClearDuration=3.5f, RebuildSpeed=24.f, SmokeRadius=4.1f;
const FVector CargoStart(-10,.64f,-8.6f), WeightStart(-7,.64f,7), PlayerStart(4,.95f,7);
const FVector Plate(-2.7f,.08f,10.7f), CarStart(16.5f,0,0), CarEnd(28.5f,0,0);
const FVector ControlA(37.1f,.65f,5.6f), ControlB(47,.8f,5.1f);
const FVector EngineA(WindX,1.38f,7.65f), EngineB(44.45f,1.38f,-.2f);

FActorSpawnParameters Spawn()
{
    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return P;
}
void Identify(AActor* Actor,const TCHAR* Name)
{
    if (!Actor) return;
    Actor->Tags.Add(TEXT("AlphaAirport"));
#if WITH_EDITOR
    Actor->SetActorLabel(Name);
#endif
}
FVector Dimensions(const FVector& S) { return FVector(FMath::Abs(S.X),FMath::Abs(S.Z),FMath::Abs(S.Y)); }
void Tint(UStaticMeshComponent* Mesh,const TCHAR* Name)
{
    if (FCString::Strcmp(Name,TEXT("AirportCream"))==0) { BDVisuals::Tint(Mesh,FLinearColor(.87f,.73f,.44f)); return; }
    if (FCString::Strcmp(Name,TEXT("AirportBlue"))==0) { BDVisuals::Tint(Mesh,FLinearColor(.025f,.20f,.34f)); return; }
    if (FCString::Strcmp(Name,TEXT("AirportTeal"))==0) { BDVisuals::Tint(Mesh,FLinearColor(.004f,.043f,.059f)); return; }
    if (FCString::Strcmp(Name,TEXT("AirportGrey"))==0) { BDVisuals::Tint(Mesh,FLinearColor(.18f,.23f,.30f)); return; }
    if (FCString::Strcmp(Name,TEXT("Smoke"))==0) { BDVisuals::Tint(Mesh,FLinearColor(.33f,.22f,.52f)); return; }
    BDAlphaLook::Material(Mesh,Name);
}
}
using namespace BDAirport;

ABDAlphaAirportGameMode::ABDAlphaAirportGameMode()
{
    DefaultPawnClass=nullptr;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    PlayerCheckpoint=FromUnity(PlayerStart);
    CargoCheckpoint=FromUnity(CargoStart);
}

float ABDAlphaAirportGameMode::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; }

AStaticMeshActor* ABDAlphaAirportGameMode::Box(const TCHAR* Name,const FVector& P,const FVector& Size,const TCHAR* Material,bool bSolid,float UnityYaw)
{
    // Unity +Y yaw maps to Unreal +Z yaw under (x,-z,y). Inverting this
    // sign mirrors both the rebound wall and the diagonal conveyor artwork.
    auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(FromUnity(P),FRotator(0,UnityYaw,0),Spawn());
    Identify(Actor,Name);
    auto* Mesh=Actor->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    BDVisuals::Shape(Mesh,TEXT("Cube"));
    Actor->SetActorScale3D(Dimensions(Size));
    Tint(Mesh,Material);
    if (bSolid)
    {
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetCollisionObjectType(ECC_WorldStatic);
    }
    else Mesh->SetCastShadow(false);
    RoomActors.Add(Actor);
    return Actor;
}
AStaticMeshActor* ABDAlphaAirportGameMode::Shape(const TCHAR* Name,const TCHAR* ShapeName,const FVector& P,const FVector& Size,const TCHAR* Material)
{
    auto* Actor=Box(Name,P,Size,Material);
    BDVisuals::Shape(Actor->GetStaticMeshComponent(),ShapeName);
    return Actor;
}
AActor* ABDAlphaAirportGameMode::Root(const TCHAR* Name,const FVector& P)
{
    AActor* Actor=GetWorld()->SpawnActor<AActor>(FromUnity(P),FRotator::ZeroRotator,Spawn());
    auto* Component=NewObject<USceneComponent>(Actor);
    Component->SetMobility(EComponentMobility::Movable);
    Actor->SetRootComponent(Component);
    Component->RegisterComponent();
    Actor->SetActorLocation(FromUnity(P));
    Identify(Actor,Name);
    RoomActors.Add(Actor);
    return Actor;
}
void ABDAlphaAirportGameMode::Floor(const TCHAR* Name,const FVector& P,const FVector2D& Size,const TCHAR* Material,bool bGrid)
{
    Box(Name,P-FVector(0,.2f,0),FVector(Size.X,.4f,Size.Y),Material,true);
    if (!bGrid) return;
    for (float X=-Size.X*.5f+1;X<Size.X*.5f;X+=2)
        Box(TEXT("Airport floor seam"),P+FVector(X,.008f,0),FVector(.018f,.014f,Size.Y),TEXT("AirportCream"));
    for (float Z=-Size.Y*.5f+1;Z<Size.Y*.5f;Z+=2)
        Box(TEXT("Airport floor seam"),P+FVector(0,.009f,Z),FVector(Size.X,.014f,.018f),TEXT("AirportCream"));
}
void ABDAlphaAirportGameMode::Wall(const TCHAR* Name,const FVector& P,const FVector& Size)
{
    MarkHardWall(Box(Name,P,Size,TEXT("Wall"),true));
    Box(TEXT("Airport wall cap"),P+FVector(0,Size.Y*.5f+.02f,0),FVector(Size.X+.07f,.12f,Size.Z+.07f),TEXT("Ink"));
}
AStaticMeshActor* ABDAlphaAirportGameMode::MarkHardWall(AStaticMeshActor* Actor)
{
    if (Actor) { Actor->Tags.Add(TEXT("AirportHardWall")); HardWalls.Add(Actor->GetStaticMeshComponent()); }
    return Actor;
}
bool ABDAlphaAirportGameMode::TraceHardWall(const FVector& Start,const FVector& End,FHitResult* Nearest) const
{
    bool bBlocked=false;
    float Distance=TNumericLimits<float>::Max();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AirportHardWall),false);
    for (UStaticMeshComponent* WallMesh:HardWalls)
    {
        if (!IsValid(WallMesh) || !WallMesh->IsCollisionEnabled()) continue;
        FHitResult Hit;
        if (WallMesh->LineTraceComponent(Hit,Start,End,Q) && Hit.Distance<Distance)
        { Distance=Hit.Distance; bBlocked=true; if (Nearest) *Nearest=Hit; }
    }
    return bBlocked;
}
bool ABDAlphaAirportGameMode::HasBlastLineOfSight(const FVector& From,AActor* Target,AActor* Bomb) const
{
    return Target && !TraceHardWall(From,Target->GetActorLocation());
}
AStaticMeshActor* ABDAlphaAirportGameMode::Line(const TCHAR* Name,const FVector& A,const FVector& B,float Width,const TCHAR* Material)
{
    auto* Actor=Box(Name,(A+B)*.5f,FVector(.01f),Material);
    PlaceLine(Actor,A,B,Width);
    return Actor;
}
void ABDAlphaAirportGameMode::PlaceLine(AStaticMeshActor* Actor,const FVector& A,const FVector& B,float Width)
{
    const FVector Start=FromUnity(A), End=FromUnity(B);
    Actor->SetActorLocationAndRotation((Start+End)*.5f,(End-Start).Rotation());
    Actor->SetActorScale3D(FVector(FMath::Max(.005f,(B-A).Size()),Width,Width));
}
TArray<TObjectPtr<AStaticMeshActor>> ABDAlphaAirportGameMode::Ring(const TCHAR* Name,const FVector& P,float Radius,const TCHAR* Material,int32 Segments)
{
    TArray<TObjectPtr<AStaticMeshActor>> Parts;
    for (int32 I=0;I<Segments;++I)
    {
        const float A=2*PI*I/Segments,B=2*PI*(I+1)/Segments;
        Parts.Add(Line(Name,P+FVector(FMath::Cos(A)*Radius,0,FMath::Sin(A)*Radius),
            P+FVector(FMath::Cos(B)*Radius,0,FMath::Sin(B)*Radius),.045f,Material));
    }
    return Parts;
}
UTextRenderComponent* ABDAlphaAirportGameMode::Label(const FString& Text,const FVector& P,float Size)
{
    auto* Actor=Root(TEXT("Airport marker"),P);
    auto* Label=NewObject<UTextRenderComponent>(Actor);
    Label->SetupAttachment(Actor->GetRootComponent());
    Label->SetText(FText::FromString(Text));
    Label->SetWorldSize(Size);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetTextRenderColor(FColor(237,244,215));
    Label->SetRelativeRotation(FRotator(65,90,0));
    Label->RegisterComponent();
    return Label;
}
AActor* ABDAlphaAirportGameMode::Glyph(const TCHAR* Kind,const FVector& P,float Size,const TCHAR* Mat,float Yaw)
{
    AActor* Icon=Root(Kind,P);
    auto Point=[&](float X,float Z) { return P+FVector(X,0,Z).RotateAngleAxis(Yaw,FVector(0,1,0))*Size; };
    auto Stroke=[&](float X,float Z,float X2,float Z2)
    { Line(Kind,Point(X,Z),Point(X2,Z2),.045f*Size,Mat)->AttachToActor(Icon,FAttachmentTransformRules::KeepWorldTransform); };
    auto Circle=[&](float X,float Z,float R,float Begin=0.f,float End=360.f)
    { for(int32 I=0;I<24;++I) { const float A=FMath::DegreesToRadians(FMath::Lerp(Begin,End,I/24.f)),B=FMath::DegreesToRadians(FMath::Lerp(Begin,End,(I+1)/24.f)); Stroke(X+FMath::Cos(A)*R,Z+FMath::Sin(A)*R,X+FMath::Cos(B)*R,Z+FMath::Sin(B)*R); } };
    const FString Type(Kind);
    if (Type==TEXT("Bomb")) { Circle(0,-.06f,.28f); Stroke(.12f,.19f,.18f,.34f); Stroke(.18f,.34f,.32f,.39f); Stroke(.32f,.39f,.4f,.35f); }
    else if (Type==TEXT("Agent")) { Circle(0,.25f,.12f); Stroke(0,.12f,0,-.15f); Stroke(-.25f,0,.25f,0); Stroke(0,-.15f,-.2f,-.4f); Stroke(0,-.15f,.2f,-.4f); }
    else if (Type==TEXT("Crate")) { Stroke(-.3f,-.3f,.3f,-.3f); Stroke(.3f,-.3f,.3f,.3f); Stroke(.3f,.3f,-.3f,.3f); Stroke(-.3f,.3f,-.3f,-.3f); Stroke(-.3f,-.3f,.3f,.3f); }
    else if (Type==TEXT("Power")) { Circle(0,0,.3f,130,410); Stroke(0,.1f,0,.42f); }
    else if (Type==TEXT("Lock")) { Circle(0,.13f,.19f,0,180); Stroke(-.19f,.13f,-.19f,-.08f); Stroke(.19f,.13f,.19f,-.08f); Stroke(-.3f,-.08f,.3f,-.08f); Stroke(.3f,-.08f,.3f,-.38f); Stroke(.3f,-.38f,-.3f,-.38f); Stroke(-.3f,-.38f,-.3f,-.08f); }
    else if (Type==TEXT("Check")) { Stroke(-.3f,0,-.07f,-.23f); Stroke(-.07f,-.23f,.36f,.28f); }
    else if (Type==TEXT("Smoke")) { Circle(-.18f,0,.22f); Circle(.12f,.16f,.26f); Circle(.32f,-.1f,.19f); }
    else if (Type==TEXT("Aim")) { Circle(0,0,.25f); Stroke(-.42f,0,-.15f,0); Stroke(.15f,0,.42f,0); Stroke(0,-.42f,0,-.15f); Stroke(0,.15f,0,.42f); }
    else if (Type==TEXT("MouseRight")) { Circle(0,0,.33f); Stroke(0,0,0,.3f); Stroke(-.3f,0,.3f,0); Stroke(.1f,.15f,.22f,.15f); }
    else if (Type==TEXT("Return")) { Circle(0,0,.3f,25,315); Stroke(.21f,-.21f,.04f,-.23f); Stroke(.21f,-.21f,.21f,-.04f); }
    else if (Type==TEXT("Shuttle")) { Stroke(-.4f,-.22f,.4f,-.22f); Stroke(-.4f,-.22f,-.4f,.28f); Stroke(-.4f,.28f,.4f,.28f); Stroke(.4f,.28f,.4f,-.22f); Circle(-.23f,-.28f,.08f); Circle(.23f,-.28f,.08f); }
    else { Stroke(-.35f,0,.35f,0); Stroke(.35f,0,.1f,.22f); Stroke(.35f,0,.1f,-.22f); }
    return Icon;
}
void ABDAlphaAirportGameMode::Belt(const FVector& A,const FVector& B,const TCHAR* Material,float Width)
{
    const FVector D=B-A;
    const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(D.X,D.Z));
    if (B.X>9)
        Box(TEXT("Cargo dock conveyor deck"),(A+B)*.5f+FVector(0,.035f,0),FVector(Width,.07f,D.Size()+.3f),TEXT("AirportTeal"),true,Yaw);
    Box(TEXT("Conveyor surface"),(A+B)*.5f+FVector(0,B.X>9 ? .075f : .035f,0),FVector(Width,.01f,D.Size()+.3f),TEXT("AirportTeal"),false,Yaw);
    const FVector Forward=D.GetSafeNormal(),Side=FVector::CrossProduct(FVector(0,1,0),Forward);
    for (float T=.4f;T<D.Size();T+=1.2f)
    {
        const FVector Tip=A+Forward*T+FVector(0,B.X>9 ? .1f : .06f,0);
        for (int32 Sign : {-1,1}) Line(TEXT("Conveyor direction"),Tip,Tip-Forward*.38f+Side*(Sign*.36f),.07f,Material);
    }
}

void ABDAlphaAirportGameMode::RemoveMapGameplay()
{
    TArray<AActor*> Remove;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->IsA<ABDCharacter>() || It->IsA<ABDBomb>() || It->IsA<ABDCrate>() || It->IsA<ABDBarrier>()
            || It->IsA<ABDPressurePlate>() || It->IsA<ABDGoal>() || It->IsA<AStaticMeshActor>() || It->IsA<APlayerStart>()) Remove.Add(*It);
    for (AActor* Actor:Remove) Actor->Destroy();
    Crate=nullptr; Goal=nullptr; Gate=nullptr; ActiveBomb=nullptr;
}
void ABDAlphaAirportGameMode::BeginPlay()
{
    Super::BeginPlay();
    RemoveMapGameplay();
    BDAlphaLook::ConfigurePlaygroundLighting(GetWorld());
    // Visual backdrop only: the shuttle crossing remains a real unsupported
    // gap. The large unlit plane also covers every staged orthographic view.
    auto* Backdrop=Box(TEXT("Airport distant teal backdrop"),FVector(18,-7,3),FVector(150,.04f,120),TEXT("Backdrop"));
    BDVisuals::Tint(Backdrop->GetStaticMeshComponent(),BDAlphaLook::Color(TEXT("Backdrop")),true);
    BuildSecurity(); BuildDock(); BuildLoading(); BuildActors(); BuildWindVisuals();
    TryPossessPlayer();
}
bool ABDAlphaAirportGameMode::TryPossessPlayer()
{
    auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!PC || !AirportPlayer) return false;
    if (PC->GetPawn()!=AirportPlayer) PC->Possess(AirportPlayer);
    PC->ResetBombInput();
    PC->SetPlaygroundView(FromUnity(CameraCenter),3000,2840);
    AirportPlayer->FacePoint(FromUnity(FVector(-7,.95f,7)));
    bReady=true;
    return true;
}
bool ABDAlphaAirportGameMode::CanRun() const
{
    if (!bReady || bCompleted || UGameplayStatics::IsGamePaused(this)) return false;
    if (const auto* Session=GetGameInstance<UBDAlphaSession>()) return Session->GetScreen()==EBDAlphaScreen::Playing;
    return true;
}

void ABDAlphaAirportGameMode::BuildSecurity()
{
    Floor(TEXT("Security floor"),FVector(-3,0,-3),FVector2D(24,16),TEXT("Floor"));
    Floor(TEXT("Control floor"),FVector(-2,0,9),FVector2D(20,8),TEXT("Floor"));
    Wall(TEXT("Security west wall"),FVector(-15,1,1),FVector(.5f,2,24));
    Wall(TEXT("Security south wall"),FVector(-3,1,-11),FVector(24,2,.5f));
    Wall(TEXT("Control north wall"),FVector(-2,1,13),FVector(20,2,.5f));
    Wall(TEXT("Control east wall"),FVector(8,1,9.2f),FVector(.45f,2,7.6f));
    Wall(TEXT("Control partition west"),FVector(-5,1,5),FVector(14,2,.4f));
    Wall(TEXT("Control partition east"),FVector(7,1,5),FVector(2,2,.4f));
    for (float X:{2.2f,5.8f}) MarkHardWall(Box(TEXT("Control doorway"),FVector(X,.95f,5),FVector(.25f,1.9f,.4f),TEXT("Plate"),true));

    Belt(FVector(-10,0,-7),FVector(-10,0,0),TEXT("AirportCream"),2.8f);
    Belt(FVector(-10,0,0),FVector(-12.8f,0,2.5f),TEXT("Plate"));
    Belt(FVector(-12.8f,0,2.5f),FVector(-12.8f,0,-8.6f),TEXT("Plate"));
    Belt(FVector(-12.8f,0,-8.6f),FVector(-10,0,-8.6f),TEXT("Plate"));
    Belt(FVector(-10,0,0),FVector(-5,0,1.5f),TEXT("GoalOutline"));
    Belt(FVector(-5,0,1.5f),FVector(5,0,0),TEXT("GoalOutline"));
    Belt(FVector(5,0,0),FVector(12.8f,0,0),TEXT("GoalOutline"));
    Diverter=Box(TEXT("Y conveyor diverter"),FVector(-10,.27f,.5f),FVector(.15f,.35f,2),TEXT("Plate"),false,-45);
    for (int32 Sign:{-1,1}) MarkHardWall(Box(TEXT("X-ray scanner pillar"),FVector(-10+Sign*1.9f,.9f,-4.5f),FVector(.28f,1.8f,.45f),TEXT("AirportTeal"),true));
    Box(TEXT("X-ray scanner arch"),FVector(-10,1.9f,-4.5f),FVector(4.08f,.3f,.45f),TEXT("AirportTeal"));
    ScannerLamp=Box(TEXT("X-ray scanner light"),FVector(-10,2.09f,-4.5f),FVector(3.4f,.08f,.32f),TEXT("Plate"))->GetStaticMeshComponent();
    Label(TEXT("X-RAY"),FVector(-10,2.5f,-4.5f),30);
    Box(TEXT("Cargo loading pad"),FVector(-10,.018f,-8.45f),FVector(2.8f,.02f,2.6f),TEXT("Plate"));
    Line(TEXT("Load arrow"),FVector(-10,.08f,-8.2f),FVector(-10,.08f,-7.5f),.08f,TEXT("Light"));

    Box(TEXT("Weight channel lower rail"),FVector(-6.4f,.14f,5.95f),FVector(4.8f,.28f,.2f),TEXT("Plate"),true);
    Box(TEXT("Weight channel upper rail"),FVector(-6.2f,.14f,8.1f),FVector(4.4f,.28f,.2f),TEXT("Plate"),true);
    Box(TEXT("Weight vertical guide west"),FVector(-4.35f,.14f,10.2f),FVector(.2f,.28f,4.1f),TEXT("Plate"),true);
    Box(TEXT("Weight vertical guide east"),FVector(-1.05f,.14f,10.2f),FVector(.2f,.28f,5.6f),TEXT("Plate"),true);
    MarkHardWall(Box(TEXT("45 degree counterweight rebound wall"),FVector(-.95f,.6f,7.9f),FVector(.28f,1.2f,5),TEXT("Wall"),true,45));
    Box(TEXT("Weight plate backstop"),FVector(-2.7f,.45f,12),FVector(2.2f,.9f,.25f),TEXT("Plate"),true);
    PlateLamp=Box(TEXT("X-ray power plate"),Plate,FVector(1.9f,.025f,1.9f),TEXT("Plate"))->GetStaticMeshComponent();
    Ring(TEXT("Plate power symbol"),Plate+FVector(0,.03f,0),.43f,TEXT("Ink"),24);
    Line(TEXT("Plate power symbol stem"),Plate+FVector(0,.04f,.15f),Plate+FVector(0,.04f,.55f),.09f,TEXT("Ink"));
    const FVector Cable[]={Plate+FVector(0,.03f,0),FVector(6,.12f,10.7f),FVector(6,.12f,-4.5f),FVector(-10,.12f,-4.5f)};
    for (int32 I=1;I<4;++I) Line(TEXT("X-ray power cable"),Cable[I-1],Cable[I],.07f,TEXT("Landing"));

    Box(TEXT("Security camera"),FVector(-8,2.4f,12.3f),FVector(.75f,.5f,1),TEXT("AirportCream"),false,150);
    Shape(TEXT("Security lens"),TEXT("Sphere"),FVector(-7.72f,2.4f,11.83f),FVector(.35f,.28f,.25f),TEXT("Ink"));
    CameraLamp=Shape(TEXT("Camera status lamp"),TEXT("Sphere"),FVector(-7.7f,2.64f,12),FVector(.16f),TEXT("Plate"))->GetStaticMeshComponent();
    const FVector ConeOrigin(-8,.025f,12.3f),Dir=FVector(.58f,0,-1).GetSafeNormal();
    FVector Last=ConeOrigin;
    for (int32 I=0;I<=18;++I)
    {
        const FVector Next=ConeOrigin+Dir.RotateAngleAxis(-36+I*4,FVector(0,1,0))*10;
        Line(TEXT("Security camera cone"),Last,Next,.045f,TEXT("Plate")); Last=Next;
    }
    Line(TEXT("Security camera cone"),Last,ConeOrigin,.045f,TEXT("Plate"));
    Label(TEXT("Q"),FVector(4,.12f,6.1f),38);
    Glyph(TEXT("Smoke"),FVector(3.35f,.15f,6.1f),.8f);
    Glyph(TEXT("Return"),FVector(-12.4f,.15f,3.4f),.85f,TEXT("Plate"));
    Glyph(TEXT("Shuttle"),FVector(2,.15f,2.2f));
    Glyph(TEXT("Bomb"),FVector(-7.5f,.12f,-8.6f),.9f);
}

void ABDAlphaAirportGameMode::BuildDock()
{
    Floor(TEXT("Agent dock access"),FVector(8,0,-5),FVector2D(7,3),TEXT("AirportBlue"),false);
    Floor(TEXT("Agent blast-jump dock"),FVector(9,0,-1.325f),FVector2D(3,4.35f),TEXT("AirportBlue"),false);
    Floor(TEXT("Cargo receiving island"),FVector(12.8f,0,0),FVector2D(3,3),TEXT("AirportTeal"));
    CargoAgentGate=Box(TEXT("Cargo only lane / player barrier"),FVector(12.6f,2.7f,0),FVector(3.7f,10.4f,4.2f),TEXT("Ink"),true);
    auto* Restriction=CargoAgentGate->GetStaticMeshComponent();
    Restriction->SetVisibility(false);
    Restriction->SetCollisionResponseToAllChannels(ECR_Ignore);
    Restriction->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
    for (int32 S:{-1,1}) Box(TEXT("Cargo lane warning post"),FVector(10.75f,.5f,S*1.05f),FVector(.14f,1,.14f),TEXT("Plate"));
    Line(TEXT("Cargo only warning rail"),FVector(10.75f,.45f,-1.05f),FVector(10.75f,.45f,1.05f),.07f,TEXT("Plate"));
    Floor(TEXT("Bomb landing chute"),FVector(11.05f,0,0),FVector2D(1.5f,1.8f),TEXT("AirportBlue"),false);
    MarkHardWall(Box(TEXT("Bomb catcher canopy"),FVector(11.6f,1.84f,0),FVector(.3f,.55f,1),TEXT("AirportCream"),true));
    Box(TEXT("Bomb landing backstop"),FVector(10.5f,.18f,0),FVector(.15f,.36f,1),TEXT("AirportTeal"),true);
    Box(TEXT("Blast jump takeoff"),FVector(9.8f,.075f,-1.2f),FVector(.85f,.04f,1.1f),TEXT("Plate"));
    Ring(TEXT("Blast jump takeoff ring"),FVector(9,.06f,-2.5f),.6f,TEXT("Plate"));
    Label(TEXT("RMB"),FVector(8.8f,.12f,-3.3f),26);
    Glyph(TEXT("Agent"),FVector(10.5f,.17f,1.65f),.75f,TEXT("Indicator"));
    Line(TEXT("No walking slash"),FVector(10.12f,.19f,1.28f),FVector(10.88f,.19f,2.02f),.06f,TEXT("Indicator"));
    Glyph(TEXT("Bomb"),FVector(11,.12f,.7f),.45f);
    Glyph(TEXT("Crate"),FVector(12.8f,.15f,1),.65f);
    Glyph(TEXT("Agent"),FVector(9,.13f,-4.5f));
    Glyph(TEXT("Aim"),FVector(8.1f,.14f,-2.5f),.55f);
    Glyph(TEXT("Arrow"),FVector(7.5f,.14f,-2.5f),.55f,TEXT("Light"),180);
    Glyph(TEXT("MouseRight"),FVector(9.15f,.14f,-2.5f),.65f);
    Floor(TEXT("Arrival agent dock"),FVector(30.8f,0,-4.4f),FVector2D(4,2.2f),TEXT("AirportBlue"),false);

    Shuttle=Root(TEXT("Cargo shuttle"),CarStart);
    auto Part=[&](const TCHAR* Name,const FVector& P,const FVector& Size,const TCHAR* Mat,bool Solid=false)
    {
        auto* Actor=Box(Name,CarStart+P,Size,Mat,Solid);
        Actor->AttachToActor(Shuttle,FAttachmentTransformRules::KeepWorldTransform);
        return Actor;
    };
    Part(TEXT("Shuttle deck"),FVector(0,-.16f,0),FVector(4.4f,.32f,3.6f),TEXT("AirportCream"),true);
    Part(TEXT("Shuttle cargo bay"),FVector(.45f,.012f,.3f),FVector(1.8f,.02f,1.7f),TEXT("Plate"));
    Part(TEXT("Shuttle player foothold"),FVector(-1.25f,.02f,-.4f),FVector(1.25f,.025f,2),TEXT("AirportBlue"));
    for (int32 S:{-1,1})
    {
        Part(TEXT("Shuttle side rail"),FVector(0,.3f,S*1.75f),FVector(4.4f,.6f,.13f),TEXT("AirportTeal"),true);
        for (int32 E:{-1,1})
        {
            auto* Wheel=Shape(TEXT("Shuttle wheel"),TEXT("Sphere"),CarStart+FVector(E*1.4f,-.35f,S*1.65f),FVector(.55f,.55f,.3f),TEXT("Ink"));
            Wheel->AttachToActor(Shuttle,FAttachmentTransformRules::KeepWorldTransform);
        }
    }
    ShuttleEndGate=Part(TEXT("Shuttle end gate"),FVector(2.15f,.38f,0),FVector(.15f,.76f,3.6f),TEXT("Plate"),true);
    auto* Departure=Shape(TEXT("Shuttle departure lamp"),TEXT("Sphere"),CarStart+FVector(0,.4f,1.5f),FVector(.25f),TEXT("GoalOutline"));
    Departure->AttachToActor(Shuttle,FAttachmentTransformRules::KeepWorldTransform);
    ShuttleLamp=Departure->GetStaticMeshComponent();
    for (float X=15;X<30;X+=2.4f) Box(TEXT("Apron lane dash"),FVector(X,-1.1f,0),FVector(1,.025f,.1f),TEXT("AirportCream"));
    Box(TEXT("Airport pit backdrop"),FVector(22,-3,0),FVector(29,.3f,16),TEXT("AirportTeal"));
    Glyph(TEXT("Shuttle"),FVector(22,.14f,5),1.6f);
}

AActor* ABDAlphaAirportGameMode::Engine(const TCHAR* Name,const FVector& Outlet,const FVector& Direction)
{
    const FVector Side=FVector::CrossProduct(FVector(0,1,0),Direction).GetSafeNormal();
    // Same five-ring, 48-slice closed hollow nacelle as AirportLoading.BuildEngine.
    constexpr int32 Slices=48;
    const float Radii[]={1.04f,1.32f,1.10f,.79f,.79f};
    const float Depths[]={0,-1.5f,-3.3f,-3.3f,0};
    TArray<FVector> Vertices; TArray<int32> Triangles;
    for (int32 Row=0;Row<5;++Row) for (int32 I=0;I<Slices;++I)
    {
        const float Angle=I*2*PI/Slices;
        Vertices.Add(FromUnity(Side*(FMath::Cos(Angle)*Radii[Row])+FVector(0,FMath::Sin(Angle)*Radii[Row],0)+Direction*Depths[Row]));
    }
    for (int32 Row=0;Row<5;++Row) for (int32 I=0;I<Slices;++I)
    {
        const int32 Next=(I+1)%Slices,NextRow=(Row+1)%5;
        const int32 A=Row*Slices+I,B=Row*Slices+Next,C=NextRow*Slices+I,D=NextRow*Slices+Next;
        Triangles.Append({A,C,B,B,C,D});
    }
    auto* Shell=Box(TEXT("Hollow aircraft engine nacelle"),Outlet,FVector(1),TEXT("Light"));
    Shell->GetStaticMeshComponent()->SetStaticMesh(BDAirportMesh::Build(Shell,Vertices,Triangles));
    BDVisuals::Tint(Shell->GetStaticMeshComponent(),FLinearColor(.48f,.54f,.59f));
    auto* Disk=Shape(TEXT("Dark recessed turbine inlet"),TEXT("Cylinder"),Outlet-Direction*.13f,FVector(1.53f,.16f,1.53f),TEXT("AirportGrey"));
    Disk->SetActorScale3D(FVector(1.53f,1.53f,.16f));
    Disk->SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector,FromUnity(Direction).GetSafeNormal()));
    Box(TEXT("Engine mounting pylon"),Outlet+FVector(0,1.05f,0)-Direction*1.7f,FVector(.32f,.95f,1.2f),TEXT("AirportCream"),false,Direction.X<-.5f ? 90.f : 0.f);
    AActor* Rotor=Root(Name,Outlet-Direction*.03f);
    for (int32 I=0;I<16;++I)
    {
        const float A=I*2*PI/16;
        const FVector Radial=Side*FMath::Cos(A)+FVector(0,FMath::Sin(A),0);
        auto* Blade=Line(TEXT("Turbine rotating blade"),Outlet+Radial*.19f,Outlet+Radial*.72f+Direction*.08f,.11f,TEXT("AirportCream"));
        Blade->AttachToActor(Rotor,FAttachmentTransformRules::KeepWorldTransform);
    }
    auto* Spinner=Shape(TEXT("Turbine spinner"),TEXT("Sphere"),Outlet,FVector(.43f),TEXT("AirportTeal"));
    Spinner->AttachToActor(Rotor,FAttachmentTransformRules::KeepWorldTransform);
    return Rotor;
}

void ABDAlphaAirportGameMode::BuildLoading()
{
    Floor(TEXT("Loading apron"),FVector(40,-.06f,0),FVector2D(19,16),TEXT("AirportTeal"));
    Floor(TEXT("Blast work ledge"),FVector(30,-.08f,2.6f),FVector2D(2,3),TEXT("AirportBlue"),false);
    Glyph(TEXT("Agent"),FVector(30,.14f,3.5f),.9f);
    Glyph(TEXT("Bomb"),FVector(32,.15f,4.6f),.8f);
    Wall(TEXT("Loading south wall"),FVector(40,1.94f,-8.1f),FVector(19.4f,4,.3f));
    Wall(TEXT("North curb left of turbine A"),FVector(31.8f,1.94f,8.1f),FVector(3.1f,4,.3f));
    Wall(TEXT("Turbine A viewing curb"),FVector(WindX,.24f,8.1f),FVector(3.1f,.6f,.3f));
    Wall(TEXT("North curb right of turbine A"),FVector(36.9f,1.94f,8.1f),FVector(.9f,4,.3f));
    Wall(TEXT("Loading north wall"),FVector(45.4f,1.94f,8.1f),FVector(8.5f,4,.3f));
    Wall(TEXT("Loading east wall"),FVector(49.65f,1.94f,0),FVector(.3f,4,16.5f));
    Box(TEXT("Turbine A control"),ControlA,FVector(.55f,1.3f,.75f),TEXT("Wall"),true);
    ConsoleALamp=Box(TEXT("Turbine A screen"),ControlA+FVector(0,.7f,0),FVector(.5f,.1f,.58f),TEXT("Plate"))->GetStaticMeshComponent();
    ConsoleAHint=Label(TEXT(""),ControlA+FVector(.1f,1.05f,-.85f),40);
    Label(TEXT("A"),ControlA+FVector(0,.9f,0),35);
    Line(TEXT("A control cable"),ControlA+FVector(0,.04f,0),FVector(37.15f,.14f,7.45f),.075f,TEXT("AirportBlue"));
    Line(TEXT("A control cable"),FVector(37.15f,.14f,7.45f),FVector(WindX,.14f,7.45f),.075f,TEXT("AirportBlue"));
    Box(TEXT("Turbine B control"),ControlB,FVector(.8f,1.6f,.8f),TEXT("Wall"),true);
    ConsoleBLamp=Box(TEXT("Turbine B screen"),ControlB+FVector(0,.86f,0),FVector(.6f,.1f,.6f),TEXT("Wall"))->GetStaticMeshComponent();
    ConsoleBHint=Label(TEXT(""),ControlB+FVector(0,1,-1),40);
    Label(TEXT("B"),ControlB+FVector(0,1.05f,0),35);
    BLockSymbol.Add(Glyph(TEXT("Lock"),ControlB+FVector(0,.9f,0),.4f));
    BPowerSymbol.Add(Glyph(TEXT("Power"),ControlB+FVector(0,.9f,0),.4f,TEXT("Ink")));
    const FVector Cable[]={ControlB+FVector(0,.04f,0),FVector(48.1f,.14f,5.1f),FVector(48.1f,.14f,-.2f),FVector(46,.14f,-.2f)};
    for (int32 I=1;I<4;++I) Line(TEXT("B control cable"),Cable[I-1],Cable[I],.075f,TEXT("Landing"));

    auto* BodyA=Box(TEXT("Turbine A solid casing"),EngineA+FVector(0,0,1.9f),FVector(2.7f,2.65f,3.6f),TEXT("AirportCream"),true);
    MarkHardWall(BodyA); BodyA->GetStaticMeshComponent()->SetVisibility(false);
    auto* BodyB=Box(TEXT("Turbine B solid casing"),FVector(46,1.25f,-.2f),FVector(2.8f,2.3f,2.5f),TEXT("AirportBlue"),true);
    MarkHardWall(BodyB); BodyB->GetStaticMeshComponent()->SetVisibility(false);
    Box(TEXT("Turbine B service cradle"),FVector(46,.1f,-.2f),FVector(3.2f,.28f,2.9f),TEXT("Wall"),true);
    RotorA=Engine(TEXT("Turbine A rotor"),EngineA,FVector(0,0,-1));
    RotorB=Engine(TEXT("Turbine B rotor"),EngineB,FVector(-1,0,0));
    Label(TEXT("A"),EngineA+FVector(0,1.85f,1.5f),65);
    Label(TEXT("B"),FVector(46.15f,3.15f,-.2f),65);

    // A continuous inclined slab starts below the apron, then overlaps a hold
    // 12 cm lower than its crest. No step or seam catches an upright crate.
    const float Rise=RampHeight+.065f, Run=RampEnd-RampStart;
    const float Angle=FMath::Atan2(Rise,Run);
    const FVector RampOrigin(RampX,0,(RampStart+RampEnd)*.5f);
    const FVector UnityVertices[]={FVector(RampX-1.8f,-.065f,RampStart),FVector(RampX+1.8f,-.065f,RampStart),
        FVector(RampX-1.8f,RampHeight,RampEnd),FVector(RampX+1.8f,RampHeight,RampEnd),
        FVector(RampX-1.8f,-.46f,RampStart),FVector(RampX+1.8f,-.46f,RampStart),
        FVector(RampX-1.8f,-.46f,RampEnd),FVector(RampX+1.8f,-.46f,RampEnd)};
    TArray<FVector> RampVertices; for (const FVector& V:UnityVertices) RampVertices.Add(FromUnity(V-RampOrigin));
    const TArray<int32> RampTriangles={0,2,1,1,2,3,4,5,6,5,7,6,4,0,5,5,0,1,6,7,2,7,3,2,4,6,0,6,2,0,5,1,7,7,1,3};
    auto* Ramp=Box(TEXT("Physical cargo loading ramp"),RampOrigin,FVector(1),TEXT("AirportCream"),true);
    Ramp->GetStaticMeshComponent()->SetStaticMesh(BDAirportMesh::Build(Ramp,RampVertices,RampTriangles,true));
    for (int32 S:{-1,1})
    {
        auto* Rail=Box(TEXT("Ramp side rail"),FVector(RampX+S*1.96f,1.42f,6.4f),FVector(.22f,.65f,6.8f),TEXT("Plate"),true);
        Rail->SetActorRotation(FQuat(FVector::ForwardVector,-Angle));
    }
    Box(TEXT("Ramp cargo staging"),FVector(RampX,-.037f,2.4f),FVector(2.6f,.025f,1.6f),TEXT("AirportBlue"));
    Glyph(TEXT("Crate"),FVector(RampX,.06f,2.4f),.9f);
    Floor(TEXT("Aircraft hold floor"),FVector(RampX,HoldHeight,11.5f),FVector2D(5.2f,4.4f),TEXT("AirportCream"),false);
    Box(TEXT("Aircraft pressure relief grille"),FVector(RampX,HoldHeight+.025f,10.85f),FVector(5.05f,.04f,1.3f),TEXT("Wall"));
    for (int32 I=0;I<10;++I) Box(TEXT("Aircraft vent slot"),FVector(RampX-2.05f+I*.455f,HoldHeight+.05f,10.85f),FVector(.11f,.02f,1.22f),TEXT("AirportBlue"));
    Wall(TEXT("Cargo hold left"),FVector(RampX-2.7f,3.05f,11.5f),FVector(.25f,1.4f,4.5f));
    Wall(TEXT("Cargo hold right"),FVector(RampX+2.7f,3.05f,11.5f),FVector(.25f,1.4f,4.5f));
    MarkHardWall(Box(TEXT("Cargo hold backstop"),FVector(RampX,2.84f,12.95f),FVector(5.2f,1,.2f),TEXT("Wall"),true));
    for (int32 S:{-1,1}) Box(TEXT("Aircraft cargo door frame"),FVector(RampX+S*2.08f,3.8f,9.5f),FVector(.3f,3,.35f),TEXT("AirportTeal"));
    Shape(TEXT("Aircraft nose"),TEXT("Sphere"),FVector(RampX,2.8f,16),FVector(5.2f,3.5f,5),TEXT("AirportCream"));
    Box(TEXT("Aircraft cockpit glazing"),FVector(RampX,3.3f,17.4f),FVector(3.6f,.55f,.6f),TEXT("AirportTeal"));
    for (int32 S:{-1,1}) Box(TEXT("Aircraft wing"),FVector(RampX+S*5.4f,2.1f,12),FVector(6.4f,.25f,2.6f),TEXT("AirportCream"),false,S*15);
    MarkHardWall(Box(TEXT("Cart blast safety screen"),FVector(37.3f,.415f,.85f),FVector(7.5f,.95f,.14f),TEXT("Plate"),true));
    Box(TEXT("Cart rear guide"),FVector(37.8f,.14f,-1.25f),FVector(6.5f,.28f,.14f),TEXT("Plate"),true);
    Box(TEXT("Cart alignment backstop"),FVector(40.25f,.44f,-.2f),FVector(.18f,1,2.1f),TEXT("Plate"),true);
    Box(TEXT("Cart alignment dock"),FVector(RampX,-.037f,-.2f),FVector(2,.025f,1.8f),TEXT("Plate"));
    Line(TEXT("Cart adjustment arrow"),FVector(36,.1f,-1.9f),FVector(39,.1f,-1.9f),.1f,TEXT("Light"));
    Glyph(TEXT("Bomb"),FVector(35,.15f,-1.9f),.7f);
    Glyph(TEXT("Arrow"),FVector(37.5f,.15f,-1.9f));
    Glyph(TEXT("Check"),FVector(RampX,HoldHeight+.1f,12.5f),1.15f);
    WindACountdown=Label(TEXT(""),FVector(WindX,2,4.6f),35);
    WindBCountdown=Label(TEXT(""),FVector(42,2,-.2f),35);
}

void ABDAlphaAirportGameMode::BuildActors()
{
    Crate=GetWorld()->SpawnActor<ABDCrate>(FromUnity(CargoStart),FRotator::ZeroRotator,Spawn());
    Identify(Crate,TEXT("Airport contraband delivery cargo"));
    Counterweight=GetWorld()->SpawnActor<ABDCrate>(FromUnity(WeightStart),FRotator::ZeroRotator,Spawn());
    Identify(Counterweight,TEXT("Airport counterweight"));
    Tint(Counterweight->Mesh,TEXT("AirportGrey"));
    DeflectorCart=GetWorld()->SpawnActor<ABDCrate>(FromUnity(FVector(36,.6f,-.2f)),FRotator::ZeroRotator,Spawn());
    Identify(DeflectorCart,TEXT("Airport airflow deflector cart"));
    Tint(DeflectorCart->Mesh,TEXT("AirportBlue"));
    auto* Deflector=Box(TEXT("Fixed 45 degree airflow deflector"),FVector(36,1.644f,-.2f),FVector(.192f,1.26f,1.86f),TEXT("AirportCream"),true,-45);
    // Chaos adds welded child masses even when the parent has a mass override.
    // The Unity prop's 30 kg budget already includes its plate; keep this shape
    // as a negligible-mass collider instead of silently adding 64.8 kg of metal.
    DeflectorSurface=Deflector->GetStaticMeshComponent();
    DeflectorSurface->SetMassOverrideInKg(NAME_None,.001f);
    Deflector->AttachToActor(DeflectorCart,FAttachmentTransformRules(EAttachmentRule::KeepWorld,true));
    DeflectorCart->Mesh->SetMassOverrideInKg(NAME_None,GetPhysicsSettings().CrateMass);
    // Weld the plate into the cart's compound rigid body, as in Unity. A separate
    // attached collision body overlaps its own cargo box and pins it in place.
    // The fixed plate angle redirects B while explosions translate the cart.
    for (int32 S:{-1,1}) for (int32 E:{-1,1})
    {
        auto* Wheel=Shape(TEXT("Deflector cart wheel"),TEXT("Sphere"),FVector(36+S*.66f,.144f,-.2f+E*.42f),FVector(.336f),TEXT("Wall"));
        Wheel->AttachToActor(DeflectorCart,FAttachmentTransformRules::KeepWorldTransform);
    }
    AirportPlayer=GetWorld()->SpawnActor<ABDAirportCharacter>(FromUnity(PlayerStart),FRotator::ZeroRotator,Spawn());
    Identify(AirportPlayer,TEXT("Airport courier"));
    Goal=GetWorld()->SpawnActor<ABDGoal>(FromUnity(FVector(RampX,3.18f,11.4f)),FRotator::ZeroRotator,Spawn());
    Identify(Goal,TEXT("Aircraft cargo delivery goal"));
    Goal->Extent=FVector(225,150,102);
    Goal->OnConstruction(Goal->GetActorTransform());
    Tint(Goal->Mesh,TEXT("Goal"));
    Ring(TEXT("Aircraft delivery symbol"),FVector(RampX,HoldHeight+.085f,11.4f),.65f,TEXT("GoalOutline"));
    AirportPlayer->AddTickPrerequisiteActor(this);
    AirportPlayer->GetCharacterMovement()->AddTickPrerequisiteActor(this);
    for (ABDCrate* Body : {Crate.Get(),Counterweight.Get(),DeflectorCart.Get()}) Body->AddTickPrerequisiteActor(this);
    AirportPlayer->SetRespawnCheckpoint(PlayerCheckpoint+FVector(0,0,90.f*(GetPhysicsSettings().PlayerSize-1.f)));
    Crate->SetRespawnCheckpoint(CargoCheckpoint+FVector(0,0,60.f*(GetPhysicsSettings().CrateSize-1.f)));
    Counterweight->SetRespawnCheckpoint(FromUnity(WeightStart)+FVector(0,0,60.f*(GetPhysicsSettings().CrateSize-1.f)));
    DeflectorCart->SetRespawnCheckpoint(FromUnity(FVector(36,.6f,-.2f))+FVector(0,0,60.f*(GetPhysicsSettings().CrateSize-1.f)));
}

void ABDAlphaAirportGameMode::BuildWindVisuals()
{
    IncomingWindLine=Line(TEXT("B incoming airflow"),EngineB,EngineB+FVector(-12,0,0),.16f,TEXT("Landing"));
    for (int32 I=0;I<4;++I) CrossingGlow.Add(Line(TEXT("Timed crossing highlight"),FVector::ZeroVector,FVector(.01f,0,0),.17f,TEXT("GoalOutline")));
    for (int32 I=0;I<30;++I) WindTrailsA.Add(Line(TEXT("A wind streak"),EngineA,EngineA+FVector(0,0,-.1f),.08f,TEXT("Landing")));
    for (int32 I=0;I<32;++I) WindTrailsB.Add(Line(TEXT("B wind streak"),EngineB,EngineB+FVector(-.1f,0,0),.08f,TEXT("GoalOutline")));
    for (int32 I=0;I<12;++I) WindRibbon.Add(Line(TEXT("Reflected B airflow"),EngineB,EngineB+FVector(-.1f,0,0),.10f,TEXT("AirportBlue")));
    WindPulseA=Ring(TEXT("A blast clearance pulse"),FVector(WindX,.12f,4.6f),.1f,TEXT("Landing"));
    WindPulseB=Ring(TEXT("B blast clearance pulse"),FVector(42,.12f,-.2f),.1f,TEXT("Landing"));
}

bool ABDAlphaAirportGameMode::IsWeightOnPlate() const
{
    if (!Counterweight || Counterweight->IsRecovering()) return false;
    const FVector P=ToUnity(Counterweight->GetActorLocation());
    return FMath::Abs(P.X-Plate.X)<.9f && FMath::Abs(P.Z-Plate.Z)<.9f && P.Y<1.15f;
}
bool ABDAlphaAirportGameMode::InCameraCone(const FVector& WorldTarget) const
{
    const FVector P=ToUnity(WorldTarget);
    FVector D=P-FVector(-8,0,12.3f); D.Y=0;
    return P.Y<3 && D.Size()<11.5f && FVector::DotProduct(D.GetSafeNormal(),FVector(.58f,0,-1).GetSafeNormal())>FMath::Cos(FMath::DegreesToRadians(36.f));
}
bool ABDAlphaAirportGameMode::IsCameraOccluded(const FVector& WorldTarget) const
{
    if (SmokeRemaining<=0) return false;
    const FVector Start(-8,0,12.3f);
    FVector Target=ToUnity(WorldTarget); Target.Y=0;
    FVector Center=SmokeCenter; Center.Y=0;
    const FVector Nearest=FMath::ClosestPointOnSegment(Center,Start,Target);
    return FVector::Dist(Nearest,Center)<SmokeRadius;
}
void ABDAlphaAirportGameMode::DeploySmokeTowardAim()
{
    if (!CanRun() || !AirportPlayer || SmokeRemaining>0) return;
    FVector Point=AirportPlayer->GetActorLocation()+AirportPlayer->GetActorForwardVector()*600;
    if (auto* PC=Cast<ABDPlayerController>(AirportPlayer->GetController()))
    {
        FVector Origin,Direction;
        if (PC->DeprojectMousePositionToWorld(Origin,Direction) && FMath::Abs(Direction.Z)>.001f)
        {
            const float Distance=-Origin.Z/Direction.Z;
            if (Distance>0) Point=AirportPlayer->GetActorLocation()+(Origin+Direction*Distance-AirportPlayer->GetActorLocation()).GetClampedToMaxSize(1000);
        }
    }
    Point.Z=10;
    DeploySmoke(Point);
}
void ABDAlphaAirportGameMode::DeploySmoke(const FVector& WorldPosition)
{
    if (!CanRun() || SmokeRemaining>0) return;
    ClearSmoke();
    SmokeCenter=ToUnity(WorldPosition); SmokeCenter.Y=.1f;
    SmokeRemaining=22;
    UMaterialInterface* Glass=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AlphaLook/M_AlphaGlass.M_AlphaGlass"));
    for (int32 I=0;I<13;++I)
    {
        const float A=I*2.39996f,R=SmokeRadius*FMath::Sqrt((I+.5f)/13.f);
        auto* Puff=Shape(TEXT("Smoke cloud"),TEXT("Sphere"),SmokeCenter+FVector(FMath::Cos(A)*R,.35f+(I%3)*.4f,FMath::Sin(A)*R),FVector(2.7f,1.5f,2.7f),TEXT("Smoke"));
        if (Glass)
        {
            auto* M=UMaterialInstanceDynamic::Create(Glass,Puff);
            M->SetVectorParameterValue(TEXT("Color"),FLinearColor(.5f,.4f,.7f));
            M->SetScalarParameterValue(TEXT("Opacity"),.13f);
            M->SetScalarParameterValue(TEXT("Roughness"),1.f);
            Puff->GetStaticMeshComponent()->SetMaterial(0,M);
        }
        else Puff->SetActorScale3D(FVector(2.7f,2.7f,.12f));
        SmokeActors.Add(Puff);
    }
    for (AActor* Part:Ring(TEXT("Smoke coverage"),SmokeCenter+FVector(0,.06f,0),SmokeRadius,TEXT("Smoke"))) SmokeActors.Add(Part);
}
void ABDAlphaAirportGameMode::ClearSmoke()
{
    for (AActor* Actor:SmokeActors) if (IsValid(Actor)) Actor->Destroy();
    SmokeActors.Empty(); SmokeRemaining=0;
}
void ABDAlphaAirportGameMode::UpdateSmoke(float DeltaSeconds)
{
    if (SmokeRemaining<=0) return;
    SmokeRemaining=FMath::Max(0.f,SmokeRemaining-DeltaSeconds);
    if (SmokeRemaining<=0) ClearSmoke();
}
void ABDAlphaAirportGameMode::UpdateSecurity(float DeltaSeconds)
{
    bXrayPowered=!IsWeightOnPlate();
    Tint(ScannerLamp,bXrayPowered ? TEXT("Plate") : TEXT("GoalOutline"));
    Tint(PlateLamp,bXrayPowered ? TEXT("Plate") : TEXT("GoalOutline"));
    const bool Returns=bCargoAccepted ? false : bScanDecided && BeltSegment>=0 ? bReturning : bXrayPowered;
    Diverter->SetActorRotation(FMath::RInterpTo(Diverter->GetActorRotation(),FRotator(0,Returns ? -45 : 45,0),DeltaSeconds,5));
    if (AirportPlayer->IsRecovering()) { CameraExposure=0; bCameraDetecting=false; return; }
    const FVector Position=AirportPlayer->GetActorLocation();
    const bool Occluded=IsCameraOccluded(Position);
    bool Visible=InCameraCone(Position) && !Occluded;
    if (Visible)
    {
        Visible=!TraceHardWall(FromUnity(FVector(-8,1.6f,12.3f)),Position);
    }
    bCameraDetecting=Visible;
    CameraExposure=Visible ? CameraExposure+DeltaSeconds : FMath::Max(0.f,CameraExposure-DeltaSeconds*3);
    Tint(CameraLamp,Occluded ? TEXT("GoalOutline") : Visible ? TEXT("Indicator") : TEXT("Plate"));
    if (CameraExposure>=.7f)
    {
        CameraExposure=0;
        // Detection returns the courier to the safe side of the control doorway,
        // but never moves cargo, resets a plate, or rolls back later checkpoints.
        const FVector Saved=PlayerCheckpoint;
        AirportPlayer->SetRespawnCheckpoint(FromUnity(FVector(4,.95f,3.5f))+FVector(0,0,90.f*(GetPhysicsSettings().PlayerSize-1.f)));
        AirportPlayer->Respawn();
        PlayerCheckpoint=Saved;
    }
}

void ABDAlphaAirportGameMode::BeginConveyor()
{
    const FVector P=ToUnity(Crate->GetActorLocation());
    RouteKind=0; bReturning=false; bScanDecided=false;
    BeltRoute={FVector(-10,0,FMath::Clamp(P.Z,-6.8f,-5.55f)),FVector(-10,0,-4.5f),FVector(-10,0,0)};
    BeltSegment=0;
    Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Crate->Mesh->SetSimulatePhysics(false);
    Crate->SetActorRotation(FRotator::ZeroRotator);
}
void ABDAlphaAirportGameMode::ResetCrate(ABDCrate* Box,const FVector& UnityPosition)
{
    if (!Box) return;
    Box->Mesh->SetSimulatePhysics(false);
    FVector Position=FromUnity(UnityPosition);
    // Keep designer coordinates at base size, and raise a resized crate off its floor.
    Position.Z+=60.f*(GetPhysicsSettings().CrateSize-1.f);
    Box->SetActorLocationAndRotation(Position,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    Box->Mesh->SetSimulatePhysics(true);
    Box->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Box->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Box->Mesh->WakeAllRigidBodies();
}
void ABDAlphaAirportGameMode::UpdateConveyor(float DeltaSeconds)
{
    if (!Crate || Crate->IsRecovering()) { BeltSegment=-1; return; }
    if (BeltSegment<0)
    {
        if (bCargoAccepted) return;
        const FBox B=Crate->Mesh->Bounds.GetBox();
        const FVector P=ToUnity(Crate->GetActorLocation());
        const float OverlapX=FMath::Min(B.Max.X,-860.)-FMath::Max(B.Min.X,-1140.);
        // Unity Z [-7.8,-5.4] becomes Unreal Y [540,780].
        const float OverlapDepth=FMath::Min(B.Max.Y,780.)-FMath::Max(B.Min.Y,540.);
        if (OverlapX>=25 && OverlapDepth>=25 && P.Z>-7.8f && P.Z<-5.15f && B.Min.Z<35 && B.Max.Z>40)
            BeginConveyor();
        return;
    }
    Crate->Mesh->SetSimulatePhysics(false);
    FVector Target=BeltRoute[BeltSegment]; Target.Y=.07f+Crate->Mesh->Bounds.BoxExtent.Z/100.f;
    const FVector Next=FMath::VInterpConstantTo(Crate->GetActorLocation(),FromUnity(Target),DeltaSeconds,170);
    Crate->SetActorLocationAndRotation(Next,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    if (FVector::Dist(Next,FromUnity(Target))>3.5f) return;
    ++BeltSegment;
    if (RouteKind==0 && BeltSegment==2)
    {
        bScanDecided=true; bReturning=bXrayPowered;
        if (bReturning) ++Rejections;
    }
    if (BeltSegment<BeltRoute.Num()) return;
    if (RouteKind==0)
    {
        RouteKind=bReturning ? 1 : 2;
        if (bReturning) BeltRoute={FVector(-12.8f,0,2.5f),FVector(-12.8f,0,-8.6f),FVector(-10,0,-8.6f)};
        else BeltRoute={FVector(-5,0,1.5f),FVector(5,0,0),FVector(12.8f,0,0)};
        BeltSegment=0;
        return;
    }
    BeltSegment=-1;
    Crate->Mesh->SetSimulatePhysics(true);
    Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    if (RouteKind==1) { ResetCrate(Crate,CargoStart); bReturning=false; }
    else { bCargoAccepted=true; SetCargoCheckpoint(FVector(12.8f,.64f,0)); }
}

bool ABDAlphaAirportGameMode::OnShuttle(const FVector& WorldPosition,float Margin) const
{
    if (!Shuttle) return false;
    const FVector P=ToUnity(WorldPosition),Car=ToUnity(Shuttle->GetActorLocation());
    return FMath::Abs(P.X-Car.X)<2.1f+Margin && FMath::Abs(P.Z)<1.65f+Margin && P.Y>-.3f && P.Y<3.3f;
}
bool ABDAlphaAirportGameMode::IsCargoAboard() const { return Crate && !Crate->IsRecovering() && OnShuttle(Crate->GetActorLocation(),-.3f); }
bool ABDAlphaAirportGameMode::IsPlayerAboard() const { return AirportPlayer && !AirportPlayer->IsRecovering() && OnShuttle(AirportPlayer->GetActorLocation()); }
FVector ABDAlphaAirportGameMode::GetShuttlePosition() const { return Shuttle ? Shuttle->GetActorLocation() : FVector::ZeroVector; }
void ABDAlphaAirportGameMode::UpdateShuttle(float DeltaSeconds)
{
    const bool CargoAboard=IsCargoAboard(),PlayerAboard=IsPlayerAboard();
    Tint(ShuttleLamp,bShuttleMoving ? TEXT("Plate") : TEXT("GoalOutline"));
    const bool GateClosed=bShuttleToEnd || bShuttleMoving;
    ShuttleEndGate->SetActorHiddenInGame(!GateClosed);
    ShuttleEndGate->SetActorEnableCollision(GateClosed);
    if (!bShuttleMoving)
    {
        ShuttleDwell+=DeltaSeconds;
        if (bShuttleToEnd && !CargoAboard) { ShuttleDwell=0; return; }
        const float Wait=bShuttleToEnd ? (PlayerAboard ? 4.f : 10.f) : 20.f;
        if (ShuttleDwell>=Wait) bShuttleMoving=true;
    }
    if (bShuttleMoving)
    {
        const FVector Old=Shuttle->GetActorLocation();
        const FVector Destination=FromUnity(bShuttleToEnd ? CarEnd : CarStart);
        const FVector Next=FMath::VInterpConstantTo(Old,Destination,DeltaSeconds,210);
        const FVector Delta=Next-Old;
        Shuttle->SetActorLocation(Next);
        if (CargoAboard) Crate->SetActorLocation(Crate->GetActorLocation()+Delta,false,nullptr,ETeleportType::TeleportPhysics);
        if (PlayerAboard)
        {
            AirportPlayer->SetBase(static_cast<UPrimitiveComponent*>(nullptr)); // Manual carrier offset is applied exactly once.
            AirportPlayer->AddActorWorldOffset(Delta,true);
        }
        if (FVector::Dist(Next,Destination)<2)
        { bShuttleMoving=false; ShuttleDwell=0; bShuttleToEnd=!bShuttleToEnd; }
    }
    const FVector Cargo=ToUnity(Crate->GetActorLocation());
    if (!bArrived && bCargoAccepted && Cargo.X>31.1f && Cargo.Y>.2f && Cargo.Y<1.5f) SetCargoCheckpoint(FVector(33,.64f,0));
}

float ABDAlphaAirportGameMode::AReach() const { return bEngineA ? FMath::Clamp((Now()-AInterruptedUntil)*RebuildSpeed,0.f,15.3f) : 0.f; }
float ABDAlphaAirportGameMode::BReach() const { return bEngineB ? FMath::Clamp((Now()-BInterruptedUntil)*RebuildSpeed,0.f,24.f) : 0.f; }
float ABDAlphaAirportGameMode::GetWindAClearSeconds() const { return bEngineA ? FMath::Max(0.f,AInterruptedUntil-Now()) : 0.f; }
float ABDAlphaAirportGameMode::GetWindBClearSeconds() const { return bEngineB ? FMath::Max(0.f,BInterruptedUntil-Now()) : 0.f; }
bool ABDAlphaAirportGameMode::IsCartAligned() const { return DeflectorCart && FMath::Abs(ToUnity(DeflectorCart->GetActorLocation()).X-RampX)<=1.f; }
FVector ABDAlphaAirportGameMode::ReflectionPoint() const
{
    FVector Hit,Direction;
    return TryGetDeflectedWind(Hit,Direction) ? Hit : EngineB+FVector(-12,0,0);
}
bool ABDAlphaAirportGameMode::TryGetDeflectedWind(FVector& Hit,FVector& Direction) const
{
    Hit=EngineB+FVector(-12,0,0); Direction=FVector::ZeroVector;
    if (!IsValid(DeflectorSurface) || !DeflectorCart || DeflectorCart->IsRecovering()) return false;
    // Raycast the actual transformed box, independently of its welded parent.
    // A welded BodyInstance query can also hit the cart base; the source checks
    // only the plate's BoxCollider. This slab intersection is exactly that OBB.
    const FTransform T=DeflectorSurface->GetComponentTransform();
    const FVector LocalStart=T.InverseTransformPosition(FromUnity(EngineB));
    const FVector LocalEnd=T.InverseTransformPosition(FromUnity(EngineB+FVector(-12,0,0)));
    FVector LocalHit,Normal; float HitTime=0;
    if (!FMath::LineExtentBoxIntersection(FBox(FVector(-50),FVector(50)),LocalStart,LocalEnd,
        FVector::ZeroVector,LocalHit,Normal,HitTime)) return false;
    Hit=ToUnity(T.TransformPosition(LocalHit));
    if (!WindLineClear(EngineB,Hit+FVector(.06f,0,0))) return false;
    const FVector PlateNormal=T.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
    Direction=ToUnity(FVector(-100,0,0).MirrorByVector(PlateNormal)).GetSafeNormal();
    return true;
}
float ABDAlphaAirportGameMode::BHeight(float Z) const
{
    const bool RampLane=DeflectorCart && FMath::Abs(ToUnity(DeflectorCart->GetActorLocation()).X-RampX)<1.8f;
    return EngineB.Y+(RampLane ? FMath::Clamp((Z-RampStart)/(RampEnd-RampStart),0.f,1.f)*RampHeight : 0.f);
}
bool ABDAlphaAirportGameMode::WindLineClear(const FVector& Start,const FVector& End) const
{
    return !TraceHardWall(FromUnity(Start),FromUnity(End));
}
FVector ABDAlphaAirportGameMode::WindAAt(const FVector& WorldPoint) const
{
    const FVector P=ToUnity(WorldPoint);
    if (!bEngineA || AReach()<=0 || P.X<AStart || P.X>AEnd || FMath::Abs(P.Z)>7.65f || P.Y<-.1f || P.Y>3.4f || EngineA.Z-P.Z>AReach()) return FVector::ZeroVector;
    return FromUnity(FVector(0,0,-28));
}
FVector ABDAlphaAirportGameMode::WindBAt(const FVector& WorldPoint) const
{
    if (!bEngineB || BReach()<=0) return FVector::ZeroVector;
    FVector Origin,Flow;
    const bool Reflected=TryGetDeflectedWind(Origin,Flow);
    if (!Reflected) Origin=EngineB+FVector(-12,0,0);
    const FVector P=ToUnity(WorldPoint);
    if (P.X>Origin.X+.25f && P.X<EngineB.X && FMath::Abs(P.Z-EngineB.Z)<1.25f && P.Y>-.1f && P.Y<2.8f)
    {
        if (EngineB.X-P.X<=BReach() && WindLineClear(EngineB,FVector(P.X,EngineB.Y,P.Z))) return FromUnity(FVector(-12,0,0));
    }
    if (!Reflected) return FVector::ZeroVector;
    const FVector Offset=P-Origin;
    const float Along=FVector::DotProduct(Offset,Flow),Lateral=FMath::Abs(FVector::DotProduct(Offset,FVector::CrossProduct(FVector(0,1,0),Flow)));
    if (FVector::Dist(EngineB,Origin)+Along>BReach() || Along<.85f || Along>12 || Lateral>1.85f || FMath::Abs(P.Y-BHeight(P.Z))>1.65f) return FVector::ZeroVector;
    const FVector End(P.X,BHeight(P.Z),P.Z);
    if (!WindLineClear(Origin+Flow*.15f,End)) return FVector::ZeroVector;
    const float Coverage=1-FMath::Clamp(Lateral/2.3f,0.f,1.f);
    const float Relief=1-FMath::Clamp((P.Z-10.15f)/(11.2f-10.15f),0.f,1.f);
    const bool RampLane=FMath::Abs(ToUnity(DeflectorCart->GetActorLocation()).X-RampX)<1.8f;
    return FromUnity((Flow+FVector(0,RampLane ? .4f : 0,0))*(15.67f*Coverage*Relief));
}
bool ABDAlphaAirportGameMode::BlastHitsBeam(const FVector& Blast,float Radius,const FVector& Start,const FVector& End,float Width) const
{
    FVector Nearest=FMath::ClosestPointOnSegment(Blast,Start,End);
    FVector Side=Blast-Nearest; Side.Y=0;
    Nearest+=Side.GetClampedToMaxSize(Width);
    Nearest.Y=FMath::Clamp(Blast.Y,FMath::Min(Start.Y,End.Y)-1.65,FMath::Max(Start.Y,End.Y)+1.65);
    return FVector::Dist(Blast,Nearest)<=Radius && WindLineClear(Blast,Nearest);
}
bool ABDAlphaAirportGameMode::BlastDrivesCart(const FVector& Blast,float Radius) const
{
    if (!DeflectorCart || Blast.Z<-1.4f || Blast.Z>.65f || Blast.Y>1.6f) return false;
    const FVector Center=ToUnity(DeflectorCart->Mesh->GetCenterOfMass());
    const FVector Delta=Center-Blast;
    return Delta.Size()<Radius-.05f && FMath::Abs(Delta.X)>.45f && FMath::Abs(Delta.X)>FMath::Abs(Delta.Z)
        && WindLineClear(Blast,Center);
}
void ABDAlphaAirportGameMode::OnBombExploded(ABDBomb* Bomb)
{
    Super::OnBombExploded(Bomb);
    if (!CanRun() || !Bomb) return;
    const FVector Blast=ToUnity(Bomb->GetActorLocation());
    const float Radius=Bomb->GetProfile().BlastRadius;
    if (bEngineA)
    {
        const FVector Near(FMath::Clamp(Blast.X,double(AStart),double(AEnd)),FMath::Clamp(Blast.Y,-.1,3.4),FMath::Clamp(Blast.Z,-7.65,7.65));
        if (FVector::Dist(Blast,Near)<=Radius && WindLineClear(Blast,Near))
        { AInterruptedUntil=Now()+ClearDuration; AGapCenter=Blast; PulseATime=Now(); }
    }
    if (bEngineB && !BlastDrivesCart(Blast,Radius))
    {
        FVector Origin,Flow;
        const bool Reflected=TryGetDeflectedWind(Origin,Flow);
        if (!Reflected) Origin=EngineB+FVector(-12,0,0);
        bool Hit=BlastHitsBeam(Blast,Radius,EngineB,Origin,1.25f);
        FVector Previous=Origin;
        for (int32 I=1;Reflected && !Hit && I<=24;++I)
        {
            FVector Point=Origin+Flow*(I*.5f); Point.Y=BHeight(Point.Z);
            if (!WindLineClear(Previous,Point)) break;
            Hit=BlastHitsBeam(Blast,Radius,Previous,Point,1.85f); Previous=Point;
        }
        if (Hit) { BInterruptedUntil=Now()+ClearDuration; BGapCenter=Blast; PulseBTime=Now(); }
    }
}
FVector ABDAlphaAirportGameMode::GetWindAControlPosition() const { return FromUnity(ControlA); }
FVector ABDAlphaAirportGameMode::GetWindBControlPosition() const { return FromUnity(ControlB); }
bool ABDAlphaAirportGameMode::PressEngineControl()
{
    if (!CanRun() || !bArrived || AirportPlayer->IsRecovering()) return false;
    const FVector P=ToUnity(AirportPlayer->GetActorLocation());
    if (P.X>36.75f && FVector::Dist(P,ControlA)<2.2f)
    {
        bEngineA=!bEngineA; AInterruptedUntil=bEngineA ? Now() : -100;
        if (!bEngineA) bHasShutdown=true;
        return true;
    }
    if (bHasShutdown && FVector::Dist(P,ControlB)<2.2f)
    {
        bEngineB=!bEngineB; BInterruptedUntil=bEngineB ? Now() : -100;
        if (bEngineB) bHasStartedB=true;
        return true;
    }
    return false;
}
void ABDAlphaAirportGameMode::ApplyWind(ABDCrate* Box,const FVector& Acceleration)
{
    if (!Box || Box->IsRecovering() || Acceleration.IsNearlyZero()) return;
    // Reset the crate's contact-push rest lock as well as waking Chaos. Otherwise
    // its later pre-physics Tick can disable simulation before this force runs.
    Box->ApplyExternalAcceleration(Acceleration);
}
void ABDAlphaAirportGameMode::UpdateLoading(float DeltaSeconds)
{
    const FVector P=ToUnity(AirportPlayer->GetActorLocation()),Cargo=ToUnity(Crate->GetActorLocation());
    if (!bArrived && bCargoAccepted && Cargo.X>31.1f && P.X>30.6f && AirportPlayer->GetCharacterMovement()->IsMovingOnGround()) bArrived=true;
    if (bArrived) SetCargoCheckpoint(bHasShutdown ? FVector(RampX,.64f,2.4f) : FVector(32.2f,.64f,-2.2f));
    Tint(ConsoleALamp,bEngineA ? TEXT("Plate") : TEXT("GoalOutline"));
    Tint(ConsoleBLamp,bHasShutdown ? (bEngineB ? TEXT("GoalOutline") : TEXT("Plate")) : TEXT("Wall"));
    ConsoleAHint->SetText(FText::FromString(bArrived && P.X>36.75f && FVector::Dist(P,ControlA)<2.2f ? TEXT("E") : TEXT("")));
    ConsoleBHint->SetText(FText::FromString(bHasShutdown && FVector::Dist(P,ControlB)<2.2f ? TEXT("E") : TEXT("")));
    auto ShowGlyph=[](AActor* Root,bool bVisible)
    { TArray<AActor*> Parts; Root->GetAttachedActors(Parts); for (AActor* Part:Parts) Part->SetActorHiddenInGame(!bVisible); };
    for (AActor* Icon:BLockSymbol) ShowGlyph(Icon,!bHasShutdown);
    for (AActor* Icon:BPowerSymbol) ShowGlyph(Icon,bHasShutdown);
    if (AReach()>0) RotorA->AddActorWorldRotation(FQuat(FVector::RightVector,FMath::DegreesToRadians(650*DeltaSeconds)));
    if (BReach()>0) RotorB->AddActorWorldRotation(FQuat(FVector::ForwardVector,FMath::DegreesToRadians(650*DeltaSeconds)));

    if (!AirportPlayer->IsRecovering())
    {
        const FVector A=WindAAt(AirportPlayer->GetActorLocation()),B=WindBAt(AirportPlayer->GetActorLocation());
        const FVector Drift=FVector(B.X,B.Y,0)*.4f;
        if (!Drift.IsNearlyZero()) AirportPlayer->AddActorWorldOffset(Drift*DeltaSeconds,true);
        const auto* Movement=Cast<UBDAirportMovement>(AirportPlayer->GetCharacterMovement());
        const float Speed=Movement ? Movement->ActualPlanarVelocity.Size() : AirportPlayer->GetVelocity().Size2D();
        WindPinnedTime=!A.IsNearlyZero() && Speed<80 ? WindPinnedTime+DeltaSeconds : 0;
        if (!A.IsNearlyZero() && (ToUnity(AirportPlayer->GetActorLocation()).Z<-6.65f || WindPinnedTime>.5f))
        { WindPinnedTime=0; RecoverPlayer(); }
    }
    if (BeltSegment<0)
    {
        const FVector A=WindAAt(Crate->GetActorLocation()),B=WindBAt(Crate->GetActorLocation());
        ApplyWind(Crate,A*1.12f+B);
        if (!B.IsNearlyZero() && bHasShutdown && bHasStartedB && B.Y<0 && Cargo.Z>RampStart-.8f) bHasGuidedCargo=true;
    }
    // Reassert only this prop's rail constraint after live crate settings change.
    auto& Body=DeflectorCart->Mesh->BodyInstance;
    if (!Body.bLockZRotation || !Body.bLockYTranslation)
    {
        Body.bLockXRotation=Body.bLockYRotation=Body.bLockZRotation=true;
        Body.bLockYTranslation=true;
        DeflectorCart->Mesh->SetConstraintMode(EDOFMode::SixDOF);
    }
}

void ABDAlphaAirportGameMode::ResolveBombExplosion(const FVector& Position,ABDCharacter* Holder,ABDBomb* Bomb)
{
    ResolvingBombHolder=Holder;
    Super::ResolveBombExplosion(Position,Holder,Bomb);
    ResolvingBombHolder.Reset();
}
void ABDAlphaAirportGameMode::OnPlayerBlasted(ABDCharacter* Player)
{
    Super::OnPlayerBlasted(Player);
    if (!CanRun() || Player!=AirportPlayer || !CargoAgentGate) return;
    if (ResolvingBombHolder.Get()!=Player) return;
    const FVector P=ToUnity(Player->GetActorLocation());
    if (P.X<7.4f || P.X>10.6f || P.Z<-4.6f || P.Z>.85f) return;
    bDockBlastFlight=true; DockFlightBegan=Now();
    Player->GetCapsuleComponent()->IgnoreActorWhenMoving(CargoAgentGate,true);
}
void ABDAlphaAirportGameMode::AdvancePlayerCheckpoint(int32 Stage,const FVector& Position)
{
    if (Stage<=CheckpointStage) return;
    CheckpointStage=Stage; PlayerCheckpoint=FromUnity(Position);
    if (!AirportPlayer->IsRecovering()) AirportPlayer->SetRespawnCheckpoint(PlayerCheckpoint+FVector(0,0,90.f*(GetPhysicsSettings().PlayerSize-1.f)));
}
void ABDAlphaAirportGameMode::NotifyAirportRecovery(bool bPlayer)
{
    if (bPlayer) ++PlayerRespawns; else ++CargoRespawns;
}
void ABDAlphaAirportGameMode::SetCargoCheckpoint(const FVector& Position)
{
    CargoCheckpoint=FromUnity(Position);
    Crate->SetRespawnCheckpoint(CargoCheckpoint+FVector(0,0,60.f*(GetPhysicsSettings().CrateSize-1.f)));
}
void ABDAlphaAirportGameMode::RecoverPlayer()
{
    if (!AirportPlayer || AirportPlayer->IsRecovering()) return;
    AirportPlayer->SetRespawnCheckpoint(PlayerCheckpoint+FVector(0,0,90.f*(GetPhysicsSettings().PlayerSize-1.f)));
    AirportPlayer->Respawn();
}
void ABDAlphaAirportGameMode::UpdateCheckpoints()
{
    if (IsWeightOnPlate() && !bXrayPowered) AdvancePlayerCheckpoint(1,FVector(4,.95f,3.5f));
    if (bCargoAccepted) AdvancePlayerCheckpoint(2,FVector(9,.95f,-3.2f));
    const FVector P=ToUnity(AirportPlayer->GetActorLocation());
    const bool Grounded=AirportPlayer->GetCharacterMovement()->IsMovingOnGround();
    if (bCargoAccepted && Grounded && P.X>30.7f && P.X<49 && P.Z>-6 && P.Z<13 && P.Y>.4f && P.Y<2)
        AdvancePlayerCheckpoint(3,FVector(31.8f,.95f,3.8f));
    if (bArrived && bHasShutdown) AdvancePlayerCheckpoint(4,FVector(37.2f,.95f,-3.8f));
    // Camera alarms use a one-time safe destination. Restore the persistent
    // checkpoint only after that recovery has finished.
    if (!AirportPlayer->IsRecovering()) AirportPlayer->SetRespawnCheckpoint(PlayerCheckpoint+FVector(0,0,90.f*(GetPhysicsSettings().PlayerSize-1.f)));
    const auto* Movement=Cast<UBDCharacterMovement>(AirportPlayer->GetCharacterMovement());
    if (bDockBlastFlight && (Now()-DockFlightBegan>4 || AirportPlayer->IsRecovering() || (Now()-DockFlightBegan>.6f && Grounded && (!Movement || !Movement->IsBlastLocked()))))
    {
        bDockBlastFlight=false;
        AirportPlayer->GetCapsuleComponent()->IgnoreActorWhenMoving(CargoAgentGate,false);
    }
}

void ABDAlphaAirportGameMode::UpdateWindVisuals(float DeltaSeconds)
{
    const float Time=Now();
    for (int32 I=0;I<WindTrailsA.Num();++I)
    {
        const float X=FMath::Lerp(AStart+.2f,AEnd-.2f,(I%6)/5.f);
        const float Phase=FMath::Frac(Time*1.7f+I*.153f);
        const FVector Head(X,.42f+(I/15)*.6f,FMath::Lerp(7.5f,-7.5f,Phase)),Tail=Head+FVector(0,0,.7f);
        auto* Line=WindTrailsA[I].Get();
        Line->SetActorHiddenInGame(WindAAt(FromUnity(Head)).IsNearlyZero() || WindAAt(FromUnity(Tail)).IsNearlyZero());
        PlaceLine(Line,Tail,Head,.08f);
    }
    FVector Reflection,Flow;
    const bool Reflected=TryGetDeflectedWind(Reflection,Flow);
    if (!Reflected) Reflection=EngineB+FVector(-12,0,0);
    const FVector Side=FVector::CrossProduct(FVector(0,1,0),Flow);
    const float InletLength=FVector::Dist(EngineB,Reflection);
    const FVector Front=FMath::VInterpConstantTo(EngineB,Reflection,1.f,BReach());
    FHitResult InletBlock;
    const FVector InletEnd=TraceHardWall(FromUnity(EngineB),FromUnity(Front),&InletBlock) ? ToUnity(InletBlock.ImpactPoint) : Front;
    IncomingWindLine->SetActorHiddenInGame(!bEngineB || BReach()<=.01f);
    PlaceLine(IncomingWindLine,EngineB,InletEnd,.16f);
    for (int32 I=0;I<WindTrailsB.Num();++I)
    {
        const float Phase=FMath::Frac(Time*.65f+I*.173f);
        FVector Head,Tail;
        if (I<12)
        {
            Head=FVector(FMath::Lerp(EngineB.X,Reflection.X,Phase),EngineB.Y,EngineB.Z+((I%3)-1)*.6f);
            Tail=Head+FVector(.6f,0,0);
        }
        else
        {
            const float Along=FMath::Lerp(.9f,11.7f,Phase),Offset=((I%5)-2)*.36f;
            Head=Reflection+Side*Offset+Flow*Along; Head.Y=BHeight(Head.Z);
            Tail=Reflection+Side*Offset+Flow*FMath::Max(.85f,Along-.6f); Tail.Y=BHeight(Tail.Z);
        }
        auto* Line=WindTrailsB[I].Get();
        Line->SetActorHiddenInGame((I>=12 && !Reflected) || WindBAt(FromUnity(Head)).IsNearlyZero() || WindBAt(FromUnity(Tail)).IsNearlyZero());
        PlaceLine(Line,Tail,Head,.08f);
    }
    FVector Previous=Reflection;
    bool Blocked=false;
    const float Supplied=FMath::Clamp(BReach()-InletLength,0.f,11.7f);
    for (int32 I=0;I<WindRibbon.Num();++I)
    {
        FVector Next=Reflection+Flow*FMath::Min(Supplied,(I+1)*11.7f/WindRibbon.Num());
        Next.Y=BHeight(Next.Z);
        const bool PreviouslyBlocked=Blocked;
        FHitResult Hit;
        if (TraceHardWall(FromUnity(Previous),FromUnity(Next),&Hit)) { Next=ToUnity(Hit.ImpactPoint); Blocked=true; }
        const bool Visible=bEngineB && Reflected && Supplied>0 && !PreviouslyBlocked && !Previous.Equals(Next,.01f);
        WindRibbon[I]->SetActorHiddenInGame(!Visible);
        Tint(WindRibbon[I]->GetStaticMeshComponent(),IsCartAligned() ? TEXT("GoalOutline") : TEXT("Plate"));
        PlaceLine(WindRibbon[I],Previous,Next,.07f);
        Previous=Next;
    }
    const bool Highlight=GetWindAClearSeconds()>0 || (bArrived && !bEngineA);
    const float CrossingZ=GetWindAClearSeconds()>0 ? AGapCenter.Z : 4.6f;
    const float Low=FMath::Max(-7.6f,CrossingZ-.8f),High=FMath::Min(7.6f,CrossingZ+.8f);
    const FVector Corners[]={FVector(AStart,.18f,Low),FVector(AEnd,.18f,Low),FVector(AEnd,.18f,High),FVector(AStart,.18f,High)};
    for (int32 I=0;I<4;++I) { CrossingGlow[I]->SetActorHiddenInGame(!Highlight); PlaceLine(CrossingGlow[I],Corners[I],Corners[(I+1)%4],.17f); }
    WindACountdown->GetOwner()->SetActorLocation(FromUnity((GetWindAClearSeconds()>0 ? AGapCenter : FVector(WindX,.4f,4.6f))+FVector(0,1.4f,.85f)));
    WindBCountdown->GetOwner()->SetActorLocation(FromUnity(BGapCenter+FVector(0,1.4f,.8f)));
    WindACountdown->SetText(FText::FromString(GetWindAClearSeconds()>0 ? FString::Printf(TEXT("%.1fs"),GetWindAClearSeconds()) : TEXT("")));
    WindBCountdown->SetText(FText::FromString(GetWindBClearSeconds()>0 ? FString::Printf(TEXT("%.1fs"),GetWindBClearSeconds()) : TEXT("")));
    auto Pulse=[&](TArray<TObjectPtr<AStaticMeshActor>>& Segments,const FVector& Center,float Began)
    {
        const float Age=Time-Began,Radius=.6f+Age*3.5f;
        for (int32 I=0;I<Segments.Num();++I)
        {
            Segments[I]->SetActorHiddenInGame(Age<0 || Age>=1);
            if (Age<0 || Age>=1) continue;
            const float A=I*2*PI/Segments.Num(),B=(I+1)*2*PI/Segments.Num();
            PlaceLine(Segments[I],FVector(Center.X+FMath::Cos(A)*Radius,.15f,Center.Z+FMath::Sin(A)*Radius),
                FVector(Center.X+FMath::Cos(B)*Radius,.15f,Center.Z+FMath::Sin(B)*Radius),.1f);
        }
    };
    Pulse(WindPulseA,AGapCenter,PulseATime); Pulse(WindPulseB,BGapCenter,PulseBTime);
}
void ABDAlphaAirportGameMode::UpdateCamera(float DeltaSeconds)
{
    auto* PC=Cast<ABDPlayerController>(AirportPlayer->GetController());
    if (!PC) return;
    const float X=ToUnity(AirportPlayer->GetActorLocation()).X;
    FVector Center(-2,0,1); float HalfHeight=14.2f;
    if (bArrived)
    {
        Center=!bHasShutdown ? FVector(35.8f,0,3.2f) : !bEngineB && !bHasGuidedCargo ? FVector(38.6f,0,4) : FVector(41,0,4.8f);
        HalfHeight=!bHasShutdown ? 10.3f : 10.7f;
    }
    else if (X>10.4f) { Center=FVector(22.5f,0,0); HalfHeight=12.8f; }
    CameraCenter=FMath::VInterpTo(CameraCenter,Center,DeltaSeconds,2.8f);
    CameraHalfHeight=FMath::FInterpTo(CameraHalfHeight,HalfHeight,DeltaSeconds,2.5f);
    PC->SetPlaygroundView(FromUnity(CameraCenter),CameraHalfHeight*200.f,CameraHalfHeight*200.f);
}
FString ABDAlphaAirportGameMode::GetStageStatus() const
{
    if (bArrived)
        return FString::Printf(TEXT("03  /  A %s  |  B %s"),bEngineA ? (GetWindAClearSeconds()>0 ? TEXT("CLEAR") : TEXT("ON")) : TEXT("OFF"),
            bEngineB ? (GetWindBClearSeconds()>0 ? TEXT("CLEAR") : TEXT("ON")) : bHasShutdown ? TEXT("OFF") : TEXT("LOCKED"));
    if (bCargoAccepted) return TEXT("02  /  CARGO SHUTTLE");
    return FString::Printf(TEXT("01  /  X-RAY %s  /  Q"),bXrayPowered ? TEXT("ON") : TEXT("OFF"));
}
void ABDAlphaAirportGameMode::Tick(float DeltaSeconds)
{
    if (!bReady && !TryPossessPlayer()) return;
    if (!CanRun()) return;
    // Weight/cart falls reset those props immediately, without rolling back
    // scanner/engine state. Delivery cargo keeps the ordinary delayed recovery.
    if (Counterweight->GetActorLocation().Z < -450) ResetCrate(Counterweight,WeightStart);
    if (DeflectorCart->GetActorLocation().Z < -450) ResetCrate(DeflectorCart,FVector(36,.6f,-.2f));
    if (Crate->IsRecovering() && !bCargoWasRecovering) NotifyAirportRecovery(false);
    bCargoWasRecovering=Crate->IsRecovering();
    UpdateSmoke(DeltaSeconds);
    UpdateSecurity(DeltaSeconds);
    UpdateConveyor(DeltaSeconds);
    UpdateShuttle(DeltaSeconds);
    UpdateLoading(DeltaSeconds);
    UpdateCheckpoints();
    UpdateWindVisuals(DeltaSeconds);
    UpdateCamera(DeltaSeconds);
    // The aircraft is the only finish. Being pushed into it before solving the
    // security, turbine controls and loading-airflow puzzle cannot bypass them.
    ABDGoal* SavedGoal=Goal;
    const FBox Bounds=Crate->Mesh->Bounds.GetBox();
    const bool FullyInside=Bounds.Min.X>=(RampX-2.25f)*100 && Bounds.Max.X<=(RampX+2.25f)*100
        && Bounds.Min.Y>=-1290 && Bounds.Max.Y<=-990 && Bounds.Min.Z>=(HoldHeight-.06f)*100 && Bounds.Max.Z<420;
    if (!(FullyInside && bCargoAccepted && bHasShutdown && bHasStartedB && bHasGuidedCargo)) Goal=nullptr;
    SettleSeconds=1.2f;
    Super::Tick(DeltaSeconds);
    Goal=SavedGoal;
}
void ABDAlphaAirportGameMode::DebugCompleteDelivery()
{
    if (!CanRun() || !Crate || !Goal) return;
    if (auto* Session=GetGameInstance<UBDAlphaSession>()) Session->MarkPractice();
    bCargoAccepted=bArrived=bHasShutdown=bHasStartedB=bHasGuidedCargo=true;
    bEngineA=bEngineB=false; BeltSegment=-1;
    ResetCrate(Crate,FVector(RampX,HoldHeight+.62f,11.4f));
    AirportPlayer->SetActorLocation(FromUnity(FVector(42,.95f,6)));
    CameraCenter=FVector(41,0,4.8f); CameraHalfHeight=10.7f;
}
void ABDAlphaAirportGameMode::RestartTutorial()
{
    if (auto* Session=GetGameInstance<UBDAlphaSession>()) { Session->Replay(); return; }
    Super::RestartTutorial();
}
