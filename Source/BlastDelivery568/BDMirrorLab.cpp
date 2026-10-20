#include "BDMirrorLab.h"
#include "BDAlphaPlayground.h"
#include "BDAlphaLook.h"
#include "BDAlphaSession.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDVisuals.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FActorSpawnParameters SpawnParams()
{
    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return P;
}
FVector Unity(const FVector& V) { return ABDAlphaPlaygroundGameMode::FromUnity(V); }
FVector UnitySize(const FVector& V) { return ABDAlphaPlaygroundGameMode::SizeFromUnity(V); }
FLinearColor UnityColor(const FLinearColor& Color)
{
    auto Linear=[](float V) { return V<=.04045f ? V/12.92f : FMath::Pow((V+.055f)/1.055f,2.4f); };
    return FLinearColor(Linear(Color.R),Linear(Color.G),Linear(Color.B),Color.A);
}
void Identify(AActor* Actor,const FString& Name)
{
    Actor->Tags.Add(TEXT("AlphaMirrorLab"));
    Actor->Tags.Add(FName(*Name));
#if WITH_EDITOR
    Actor->SetActorLabel(Name);
#endif
}
UStaticMeshComponent* Part(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,
    const FVector& Location,const FVector& Size,FLinearColor Color,const TCHAR* Shape=TEXT("Cube"),bool Unlit=false)
{
    auto* Mesh=NewObject<UStaticMeshComponent>(Owner,MakeUniqueObjectName(Owner,UStaticMeshComponent::StaticClass(),FName(Name)));
    Owner->AddInstanceComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetupAttachment(Parent);
    BDVisuals::Shape(Mesh,Shape);
    Mesh->SetRelativeLocation(Location);
    Mesh->SetRelativeScale3D(Size/100.f);
    Mesh->SetCastShadow(!Unlit);
    BDVisuals::Tint(Mesh,UnityColor(Color),Unlit);
    Mesh->RegisterComponent();
    return Mesh;
}
UStaticMeshComponent* Line(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,
    FVector A,FVector B,float Width,FLinearColor Color)
{
    auto* Mesh=Part(Owner,Parent,Name,(A+B)*.5f,FVector(Width,Width,FVector::Dist(A,B)),Color,TEXT("Cylinder"),true);
    Mesh->SetRelativeRotation(FRotationMatrix::MakeFromZ(B-A).Rotator());
    return Mesh;
}
AStaticMeshActor* Box(UWorld* World,const TCHAR* Name,FVector Position,FVector Size,const TCHAR* Material,bool Solid=false)
{
    auto* Actor=World->SpawnActor<AStaticMeshActor>(Unity(Position),FRotator::ZeroRotator,SpawnParams());
    Identify(Actor,Name);
    auto* Mesh=Actor->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
    BDVisuals::Shape(Mesh,TEXT("Cube")); BDAlphaLook::Material(Mesh,Material);
    Mesh->SetWorldScale3D(UnitySize(Size)/100.f);
    if (Solid) { Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); }
    return Actor;
}
void Wall(UWorld* World,const TCHAR* Name,FVector Center,FVector Size,bool AlongX)
{
    auto* Barrier=World->SpawnActor<ABDBarrier>(Unity(FVector(Center.X,0,Center.Z)),FRotator::ZeroRotator,SpawnParams());
    Identify(Barrier,Name); Barrier->Configure(UnitySize(Size)); Barrier->Mesh->SetVisibility(false);
    const float Length=AlongX ? Size.X : Size.Z;
    const int32 Count=FMath::CeilToInt(Length/1.35f); const float Step=Length/Count;
    for (int32 Row=0;Row<2;++Row) for (int32 I=0;I<Count;++I)
    {
        const float Along=-Length*.5f+Step*(I+.5f);
        const FVector Offset=AlongX ? FVector(Along,(Row-.5f)*Size.Y*.5f,0) : FVector(0,(Row-.5f)*Size.Y*.5f,Along);
        const FVector Scale=AlongX ? FVector(Step-.035f,Size.Y*.5f-.035f,Size.Z) : FVector(Size.X,Size.Y*.5f-.035f,Step-.035f);
        Box(World,Name,Center+Offset,Scale,TEXT("Stone"));
    }
}
bool GameplayActive(UWorld* World)
{
    const auto* Mode=World ? World->GetAuthGameMode<ABDGameMode>() : nullptr;
    if (Mode && !Mode->IsGameplayInputAllowed()) return false;
    const auto* Session=World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
    return !Session || Session->GetScreen()==EBDAlphaScreen::Playing;
}
bool InsideTriangle(FVector P,FVector A,FVector B,FVector C,FVector N)
{
    return FVector::DotProduct(FVector::CrossProduct(B-A,P-A),N)>=-.00001
        && FVector::DotProduct(FVector::CrossProduct(C-B,P-B),N)>=-.00001
        && FVector::DotProduct(FVector::CrossProduct(A-C,P-C),N)>=-.00001;
}
float PointTriangleDistance(FVector P,FVector A,FVector B,FVector C,FVector N)
{
    if (N.SizeSquared()>UE_SMALL_NUMBER)
    {
        const FVector Q=P-N*(FVector::DotProduct(P-A,N)/N.SizeSquared());
        if (InsideTriangle(Q,A,B,C,N)) return FVector::DistSquared(P,Q);
    }
    return FMath::Min3(ABDMirrorLaser::SegmentDistanceSquared(P,P,A,B),
        ABDMirrorLaser::SegmentDistanceSquared(P,P,B,C),ABDMirrorLaser::SegmentDistanceSquared(P,P,C,A));
}
float SegmentTriangleDistance(FVector P,FVector Q,FVector A,FVector B,FVector C)
{
    const FVector N=FVector::CrossProduct(B-A,C-A);
    const double Denominator=FVector::DotProduct(N,Q-P);
    if (FMath::Abs(Denominator)>UE_SMALL_NUMBER)
    {
        const double T=FVector::DotProduct(N,A-P)/Denominator;
        if (T>=0 && T<=1 && InsideTriangle(P+(Q-P)*T,A,B,C,N)) return 0;
    }
    float D=FMath::Min(PointTriangleDistance(P,A,B,C,N),PointTriangleDistance(Q,A,B,C,N));
    D=FMath::Min(D,ABDMirrorLaser::SegmentDistanceSquared(P,Q,A,B));
    D=FMath::Min(D,ABDMirrorLaser::SegmentDistanceSquared(P,Q,B,C));
    return FMath::Min(D,ABDMirrorLaser::SegmentDistanceSquared(P,Q,C,A));
}
}

ABDBulletproofGlass::ABDBulletproofGlass()
{
    Collision=CreateDefaultSubobject<UBoxComponent>(TEXT("SolidTransparentWall")); SetRootComponent(Collision);
    Collision->SetCollisionProfileName(TEXT("BlockAll")); Collision->SetBoxExtent(FVector(150,10,135));
    Collision->SetMobility(EComponentMobility::Movable);
    Tags.Add(TEXT("BulletproofGlass"));
}
void ABDBulletproofGlass::Configure(float Length)
{
    for (UStaticMeshComponent* Piece:Pieces) if (IsValid(Piece)) Piece->DestroyComponent(); Pieces.Reset();
    Collision->SetBoxExtent(FVector(Length*.5f,10,135));
    auto Add=[&](const TCHAR* Name,FVector At,FVector Size,FLinearColor Color,bool Unlit=false)
    { auto* P=Part(this,Collision,Name,At,Size,Color,TEXT("Cube"),Unlit); P->SetCastShadow(false); Pieces.Add(P); return P; };
    if (auto* Glass=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AlphaLook/M_AlphaGlass.M_AlphaGlass")))
    {
        auto* Pane=Add(TEXT("ClearPane"),FVector::ZeroVector,FVector(Length,20,270),FLinearColor::White);
        auto* M=UMaterialInstanceDynamic::Create(Glass,Pane); M->SetVectorParameterValue(TEXT("Color"),FLinearColor(.96,.995,1));
        M->SetScalarParameterValue(TEXT("Opacity"),.10f); Pane->SetMaterial(0,M);
    }
    Add(TEXT("WallFootprint"),FVector(0,0,-131.8),FVector(Length,32,6.4),FLinearColor(.31,.43,.42));
    Add(TEXT("BottomEdge"),FVector(0,0,-127.5),FVector(Length,22.5,2.5),FLinearColor(.94,.98,.94),true);
    Add(TEXT("TopEdge"),FVector(0,0,135),FVector(Length,22.5,3.5),FLinearColor(.77,.88,.85));
    for (float Side:{-1.f,1.f}) Add(TEXT("PanelJoint"),FVector(Side*Length*.5f,0,0),FVector(2.7,22.5,270),FLinearColor(.77,.88,.85));
    for (int32 I=0;I<2;++I) Pieces.Add(Line(this,Collision,TEXT("PaneSheen"),FVector(-17+I*13,11.3,30),FVector(10+I*13,11.3,70),1.7,FLinearColor(.8,.9,.9)));
}

ABDRotatingLaserMirror::ABDRotatingLaserMirror()
{
    PrimaryActorTick.bCanEverTick=true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("MirrorRoot")); SetRootComponent(Root);
    Base=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixedCircularBase")); Base->SetupAttachment(Root);
    BDVisuals::Shape(Base,TEXT("Cylinder")); Base->SetRelativeLocation(FVector(0,0,12)); Base->SetRelativeScale3D(FVector(2.8,2.8,.24));
    Base->SetCollisionProfileName(TEXT("BlockAll")); Base->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Rotor=CreateDefaultSubobject<USceneComponent>(TEXT("RotatingReflector")); Rotor->SetupAttachment(Root);
    Switch=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OmnidirectionalBlastSwitch")); Switch->SetupAttachment(Root);
    BDVisuals::Shape(Switch,TEXT("Cylinder")); Switch->SetRelativeLocation(FVector(0,0,25.5)); Switch->SetRelativeScale3D(FVector(2.64,2.64,.11));
}
void ABDRotatingLaserMirror::Initialize(float InitialYawDegrees)
{
    for (UStaticMeshComponent* Piece:Pieces) if (IsValid(Piece)) Piece->DestroyComponent(); Pieces.Reset();
    AcceptedBlasts.Reset(); ActivationCount=PendingTurns=0; TurnElapsed=Flash=0; bTurning=false;
    SettledYaw=FMath::Fmod(InitialYawDegrees,360.f); TargetYaw=SettledYaw; SetVisualYaw(SettledYaw);
    BDVisuals::Tint(Base,UnityColor(FLinearColor(.22,.30,.34))); BDVisuals::Tint(Switch,UnityColor(FLinearColor(1,.53,.12)));
    auto Add=[&](USceneComponent* Parent,const TCHAR* Name,FVector At,FVector Size,FLinearColor Color,const TCHAR* Shape=TEXT("Cube"))
    { auto* P=Part(this,Parent,Name,At,Size,Color,Shape); Pieces.Add(P); return P; };
    const FLinearColor Metal(.83,.90,.88),Mint(.25,.94,.72);
    Add(GetRootComponent(),TEXT("PivotSupport"),FVector(0,0,59),FVector(30,30,60),Metal,TEXT("Cylinder"));
    for (float Side:{-1.f,1.f})
    {
        Add(Rotor,TEXT("MirrorSideFrame"),FVector(Side*118.75,0,135),FVector(7.5,16,175),Metal);
        Add(Rotor,TEXT("MirrorHorizontalFrame"),FVector(0,0,135+Side*83.75),FVector(230,16,7.5),Metal);
        auto* Face=Add(Rotor,TEXT("ReflectiveGlassFace"),FVector(0,Side*9,135),FVector(230,1.8,160),FLinearColor(.80,.95,.95));
        if (auto* M=Cast<UMaterialInstanceDynamic>(Face->GetMaterial(0)))
        { M->SetScalarParameterValue(TEXT("Metallic"),.92f); M->SetScalarParameterValue(TEXT("Roughness"),.08f); }
        Add(Rotor,TEXT("MirrorTopLight"),FVector(0,Side*7,221),FVector(235,2.5,3.5),Mint);
    }
    for (int32 Q=0;Q<4;++Q)
    {
        FVector Previous;
        for (int32 I=0;I<14;++I)
        {
            const float Angle=FMath::DegreesToRadians(Q*90.f+13.f+60.f*I/13.f);
            const FVector At(FMath::Sin(Angle)*105,-FMath::Cos(Angle)*105,32.5);
            if (I>0) Pieces.Add(Line(this,GetRootComponent(),TEXT("ClockwiseArrow"),Previous,At,8.5,FLinearColor(1,.97,.83)));
            Previous=At;
        }
        const float A=FMath::DegreesToRadians(Q*90.f+73.f);
        const FVector Radial(FMath::Sin(A),-FMath::Cos(A),0),Tangent(FMath::Cos(A),FMath::Sin(A),0);
        for (float Side:{-1.f,1.f}) Pieces.Add(Line(this,GetRootComponent(),TEXT("ClockwiseArrowHead"),Previous,Previous-Tangent*23+Radial*18*Side,8.5,FLinearColor(1,.97,.83)));
    }
}
void ABDRotatingLaserMirror::SetVisualYaw(float Yaw) { Rotor->SetRelativeRotation(FRotator(0,Yaw,0)); }
void ABDRotatingLaserMirror::BeginTurn()
{
    --PendingTurns; ++ActivationCount; TurnElapsed=0; TargetYaw=SettledYaw+90; bTurning=true;
}
bool ABDRotatingLaserMirror::RequestQuarterTurn()
{
    if (IsActorBeingDestroyed() || !GameplayActive(GetWorld())) return false;
    ++PendingTurns; Flash=1;
    if (!bTurning) BeginTurn();
    return true;
}
void ABDRotatingLaserMirror::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); if (!GameplayActive(GetWorld())) return;
    Flash=FMath::Max(0.f,Flash-DeltaSeconds/.4f);
    Switch->SetRelativeScale3D(FVector(2.64f+Flash*.08f,2.64f+Flash*.08f,.11f));
    if (!bTurning) return;
    TurnElapsed+=DeltaSeconds; const float T=FMath::Clamp(TurnElapsed/.45f,0.f,1.f);
    SetVisualYaw(FMath::Lerp(SettledYaw,TargetYaw,T*T*(3-2*T)));
    if (T>=1) { SettledYaw=FMath::Fmod(TargetYaw,360.f); bTurning=false; SetVisualYaw(SettledYaw); if (PendingTurns>0) BeginTurn(); }
}
bool ABDRotatingLaserMirror::ReceiveExplosion(const FVector& Origin,float Radius,uint64 ExplosionId,AActor* Source)
{
    if (IsActorBeingDestroyed() || !FMath::IsFinite(Radius) || Radius<=0 || Origin.ContainsNaN() || AcceptedBlasts.Contains(ExplosionId)) return false;
    const FVector Delta=GetSwitchPosition()-Origin; const double Distance=Delta.Size();
    constexpr float SwitchRadius=110.f;
    if (!FMath::IsFinite(Distance) || Distance>Radius+SwitchRadius) return false;
    const float ExposedDistance=FMath::Max(0.f,float(Distance)-SwitchRadius);
    if (ExposedDistance>.1f)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MirrorBlastShield),false,this); if (Source) Q.AddIgnoredActor(Source);
        // Dynamic players/cargo do not shield the circular switch in Unity.
        for (TActorIterator<ABDCharacter> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        for (TActorIterator<ABDCrate> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        for (TActorIterator<ABDBomb> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Delta.GetSafeNormal()*ExposedDistance,ECC_Visibility,Q)) return false;
    }
    if (!RequestQuarterTurn()) return false;
    AcceptedBlasts.Add(ExplosionId);
    return true;
}
bool ABDRotatingLaserMirror::TryIntersectRay(const FVector& Start,const FVector& InDirection,float MaxDistance,
    float& Distance,FVector& Point,FVector& Normal) const
{
    Distance=0; Point=Normal=FVector::ZeroVector;
    if (MaxDistance<=0 || InDirection.IsNearlyZero()) return false;
    const FQuat Orientation=GetActorQuat()*FQuat(FVector::UpVector,FMath::DegreesToRadians(SettledYaw));
    const FVector FaceNormal=Orientation.RotateVector(FVector(0,-1,0)),Tangent=Orientation.RotateVector(FVector::ForwardVector);
    const FVector Center=GetActorLocation()+FVector(0,0,75),Axis=InDirection.GetSafeNormal();
    const double Denominator=FVector::DotProduct(Axis,FaceNormal); if (FMath::Abs(Denominator)<.00001) return false;
    const double Along=FVector::DotProduct(Center-Start,FaceNormal)/Denominator;
    if (Along<=.5 || Along>MaxDistance) return false;
    const FVector Hit=Start+Axis*Along,Offset=Hit-Center;
    if (FMath::Abs(FVector::DotProduct(Offset,Tangent))>115 || FMath::Abs(Offset.Z)>90) return false;
    Distance=Along; Point=Hit; Normal=Denominator<0 ? FaceNormal : -FaceNormal; return true;
}

ABDMirrorLaser::ABDMirrorLaser()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("LaserRoot")); SetRootComponent(Root);
}
void ABDMirrorLaser::Configure(const FVector& InDirection)
{
    Direction=InDirection.GetSafeNormal();
    EmitterHousing=Part(this,GetRootComponent(),TEXT("EmitterHousing"),-Direction*20+FVector(0,0,10),FVector(70,95,110),FLinearColor(.22,.30,.34));
    EmitterHousing->SetWorldRotation(Direction.Rotation()); EmitterHousing->SetCollisionProfileName(TEXT("BlockAll")); EmitterHousing->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    EmitterAperture=Part(this,GetRootComponent(),TEXT("LaserAperture"),Direction*16,FVector(38),FLinearColor(1,.18,.08),TEXT("Sphere"),true);
    for (TActorIterator<ABDRotatingLaserMirror> It(GetWorld());It;++It) AddTickPrerequisiteActor(*It);
    RefreshPath();
}
bool ABDMirrorLaser::IntersectsEmitterRay(const FVector& Origin,const FVector& RayDirection,double& Distance) const
{
    if (!IsValid(EmitterHousing) || !IsValid(EmitterAperture)) return false;
    const FBox Bounds=EmitterHousing->Bounds.GetBox()+EmitterAperture->Bounds.GetBox();
    double Near=0,Far=10000;
    for (int32 I=0;I<3;++I)
    {
        if (FMath::Abs(RayDirection[I])<UE_SMALL_NUMBER)
        { if (Origin[I]<Bounds.Min[I] || Origin[I]>Bounds.Max[I]) return false; }
        else
        {
            const double A=(Bounds.Min[I]-Origin[I])/RayDirection[I],B=(Bounds.Max[I]-Origin[I])/RayDirection[I];
            Near=FMath::Max(Near,FMath::Min(A,B)); Far=FMath::Min(Far,FMath::Max(A,B));
            if (Near>Far) return false;
        }
    }
    Distance=Near; return true;
}
void ABDMirrorLaser::ResetContactHistory() { PreviousBombs.Reset(); PreviousPlayers.Reset(); }
void ABDMirrorLaser::RelocateEmitter(const FVector& Location,const FVector& NewDirection)
{
    const FVector Axis=NewDirection.GetSafeNormal();
    const float Delta=FMath::FindDeltaAngleDegrees(Direction.Rotation().Yaw,Axis.Rotation().Yaw);
    const FQuat Turn(FVector::UpVector,FMath::DegreesToRadians(Delta));
    // Housing, aperture and attached wall mount move as existing components.
    SetActorLocationAndRotation(Location,Turn*GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
    Direction=Axis; ResetContactHistory(); RefreshPath();
}
void ABDMirrorLaser::RefreshPath()
{
    if (!IsEmissionEnabled())
    {
        BeamPoints.Reset();
        for (UStaticMeshComponent* Mesh:BeamMeshes) if (IsValid(Mesh)) Mesh->SetVisibility(false);
        PreviousBombs.Reset(); PreviousPlayers.Reset();
        return;
    }
    constexpr float BeamRadius=10.f;
    FVector Start=GetActorLocation(),Axis=Direction; float Remaining=6500.f;
    BeamPoints.Reset(); BeamPoints.Add(Start);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MirrorLaserOptics),false,this);
    for (TActorIterator<ABDBulletproofGlass> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
    for (TActorIterator<ABDRotatingLaserMirror> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
    for (TActorIterator<ABDCharacter> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
    for (TActorIterator<ABDBomb> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
    for (int32 Bounce=0;Bounce<=12;++Bounce)
    {
        float Length=Remaining; FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit,Start,Start+Axis*Length,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(BeamRadius),Q)) Length=FMath::Max(0.f,Hit.Distance-2.f);
        ABDRotatingLaserMirror* Mirror=nullptr; FVector Point,Normal;
        for (TActorIterator<ABDRotatingLaserMirror> It(GetWorld());It;++It)
        {
            float D; FVector P,N;
            if (It->TryIntersectRay(Start,Axis,Length,D,P,N) && D<=Length)
            { Mirror=*It; Length=D; Point=P; Normal=N; }
        }
        if (!Mirror) { BeamPoints.Add(Start+Axis*Length); break; }
        BeamPoints.Add(Point); Remaining-=Length;
        if (Mirror->IsTurning() || Bounce==12 || Remaining<.1f) break;
        Axis=(Axis-2*FVector::DotProduct(Axis,Normal)*Normal).GetSafeNormal(); Start=Point;
    }
    const int32 Segments=BeamPoints.Num()-1;
    while (BeamMeshes.Num()<Segments) BeamMeshes.Add(Part(this,GetRootComponent(),TEXT("LaserBeam"),FVector::ZeroVector,FVector(14,14,1),FLinearColor(1,.16,.055),TEXT("Cylinder"),true));
    for (int32 I=0;I<BeamMeshes.Num();++I)
    {
        UStaticMeshComponent* Mesh=BeamMeshes[I]; Mesh->SetVisibility(I<Segments);
        if (I>=Segments) continue;
        const FVector A=BeamPoints[I],B=BeamPoints[I+1];
        Mesh->SetWorldLocation((A+B)*.5); Mesh->SetWorldRotation(FRotationMatrix::MakeFromZ(B-A).Rotator());
        Mesh->SetWorldScale3D(FVector(.14,.14,FVector::Dist(A,B)/100));
    }
}
float ABDMirrorLaser::SegmentDistanceSquared(const FVector& A,const FVector& B,const FVector& C,const FVector& D)
{
    FVector P,Q; FMath::SegmentDistToSegmentSafe(A,B,C,D,P,Q); return FVector::DistSquared(P,Q);
}
bool ABDMirrorLaser::IntersectsBeam(const FVector& From,const FVector& To,float Radius) const
{
    for (int32 I=1;I<BeamPoints.Num();++I) if (SegmentDistanceSquared(From,To,BeamPoints[I-1],BeamPoints[I])<=FMath::Square(Radius+10.f)) return true;
    return false;
}
void ABDMirrorLaser::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); if (!GameplayActive(GetWorld())) return;
    RefreshPath(); CheckContacts();
}
void ABDMirrorLaser::CheckContacts()
{
    for (TActorIterator<ABDCharacter> It(GetWorld());It;++It)
    {
        ABDCharacter* Player=*It; if (Player->IsRecovering() || Player->IsHidden()) { PreviousPlayers.Remove(Player); continue; }
        const auto* Capsule=Player->GetCapsuleComponent(); const float Radius=Capsule->GetScaledCapsuleRadius();
        const float Half=FMath::Max(0.f,Capsule->GetScaledCapsuleHalfHeight()-Radius);
        const FVector C=Capsule->GetComponentLocation(); const FCapsuleHistory Current{C-FVector(0,0,Half),C+FVector(0,0,Half)};
        bool Hit=IntersectsBeam(Current.Lower,Current.Upper,Radius);
        if (!Hit) if (const auto* Old=PreviousPlayers.Find(Player))
        {
            // A respawn is a teleport, not a movement segment crossing the room.
            if (FVector::DistSquared(Old->Lower,Current.Lower)<FMath::Square(2000.f))
                for (int32 I=1;I<BeamPoints.Num() && !Hit;++I)
                    Hit=FMath::Min(SegmentTriangleDistance(BeamPoints[I-1],BeamPoints[I],Old->Lower,Old->Upper,Current.Upper),
                        SegmentTriangleDistance(BeamPoints[I-1],BeamPoints[I],Old->Lower,Current.Upper,Current.Lower))<=FMath::Square(Radius+10.f);
        }
        if (Hit) { PreviousPlayers.Remove(Player); Player->Respawn(); ++PlayerHits; }
        else PreviousPlayers.Add(Player,Current);
    }
    for (auto It=PreviousPlayers.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    for (TActorIterator<ABDBomb> It(GetWorld());It;++It)
    {
        ABDBomb* Bomb=*It; if (Bomb->IsActorBeingDestroyed()) continue;
        const FVector Current=Bomb->GetActorLocation(); const FVector* Old=PreviousBombs.Find(Bomb);
        if (IntersectsBeam(Old ? *Old : Current,Current,Bomb->GetWorldRadius()))
        { PreviousBombs.Remove(Bomb); Bomb->Destroy(); ++BombsDestroyed; }
        else PreviousBombs.Add(Bomb,Current);
    }
    for (auto It=PreviousBombs.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}

ABDRotatingLaserEmitter::ABDRotatingLaserEmitter()
{
    Base=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixedCircularBase")); Base->SetupAttachment(GetRootComponent());
    BDVisuals::Shape(Base,TEXT("Cylinder")); Base->SetRelativeLocation(FVector(0,0,-63)); Base->SetRelativeScale3D(FVector(2.8,2.8,.24));
    Base->SetCollisionProfileName(TEXT("BlockAll")); Base->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Rotor=CreateDefaultSubobject<USceneComponent>(TEXT("RotatingEmitter")); Rotor->SetupAttachment(GetRootComponent());
    Switch=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OmnidirectionalBlastSwitch")); Switch->SetupAttachment(GetRootComponent());
    BDVisuals::Shape(Switch,TEXT("Cylinder")); Switch->SetRelativeLocation(FVector(0,0,-49.5)); Switch->SetRelativeScale3D(FVector(2.64,2.64,.11));
}
void ABDRotatingLaserEmitter::Initialize(const FVector& InitialDirection)
{
    for (UStaticMeshComponent* Piece:Pieces) if (IsValid(Piece)) Piece->DestroyComponent(); Pieces.Reset();
    AcceptedBlasts.Reset(); ActivationCount=PendingTurns=0; TurnElapsed=Flash=0; bTurning=false;
    FVector Axis(InitialDirection.X,InitialDirection.Y,0); Axis=Axis.GetSafeNormal();
    if (Axis.IsNearlyZero()) Axis=FVector(0,1,0);
    SettledYaw=FMath::Fmod(Axis.Rotation().Yaw+360.f,360.f); TargetYaw=SettledYaw;
    SetBeamDirection(Axis); Rotor->SetRelativeRotation(FRotator(0,SettledYaw,0));
    BDVisuals::Tint(Base,UnityColor(FLinearColor(.22,.30,.34))); BDVisuals::Tint(Switch,UnityColor(FLinearColor(1,.53,.12)));
    auto* Housing=Part(this,Rotor,TEXT("RotatingLaserHousing"),FVector(-20,0,10),FVector(70,95,110),FLinearColor(.22,.30,.34));
    Housing->SetCollisionProfileName(TEXT("BlockAll")); Housing->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Pieces.Add(Housing);
    Pieces.Add(Part(this,Rotor,TEXT("LaserAperture"),FVector(16,0,0),FVector(38),FLinearColor(1,.18,.08),TEXT("Sphere"),true));
    Pieces.Add(Part(this,GetRootComponent(),TEXT("PivotSupport"),FVector(0,0,-25),FVector(30,30,50),FLinearColor(.83,.90,.88),TEXT("Cylinder")));
    // Fixed arrows remain readable from all four sides while only the laser housing turns.
    for (int32 Q=0;Q<4;++Q)
    {
        FVector Previous;
        for (int32 I=0;I<14;++I)
        {
            const float Angle=FMath::DegreesToRadians(Q*90.f+13.f+60.f*I/13.f);
            const FVector At(FMath::Sin(Angle)*105,-FMath::Cos(Angle)*105,-42.5);
            if (I>0) Pieces.Add(Line(this,GetRootComponent(),TEXT("ClockwiseArrow"),Previous,At,8.5,FLinearColor(1,.97,.83)));
            Previous=At;
        }
        const float A=FMath::DegreesToRadians(Q*90.f+73.f);
        const FVector Radial(FMath::Sin(A),-FMath::Cos(A),0),Tangent(FMath::Cos(A),FMath::Sin(A),0);
        for (float Side:{-1.f,1.f}) Pieces.Add(Line(this,GetRootComponent(),TEXT("ClockwiseArrowHead"),Previous,Previous-Tangent*23+Radial*18*Side,8.5,FLinearColor(1,.97,.83)));
    }
    for (TActorIterator<ABDRotatingLaserMirror> It(GetWorld());It;++It) AddTickPrerequisiteActor(*It);
    RefreshPath();
}
void ABDRotatingLaserEmitter::BeginTurn()
{
    --PendingTurns; ++ActivationCount; TurnElapsed=0; TargetYaw=SettledYaw+90; bTurning=true;
}
bool ABDRotatingLaserEmitter::RequestQuarterTurn()
{
    if (IsActorBeingDestroyed() || !GameplayActive(GetWorld())) return false;
    ++PendingTurns; Flash=1;
    if (!bTurning) BeginTurn();
    RefreshPath();
    return true;
}
void ABDRotatingLaserEmitter::Tick(float DeltaSeconds)
{
    if (GameplayActive(GetWorld()))
    {
        Flash=FMath::Max(0.f,Flash-DeltaSeconds/.4f);
        Switch->SetRelativeScale3D(FVector(2.64f+Flash*.08f,2.64f+Flash*.08f,.11f));
        if (bTurning)
        {
            TurnElapsed+=DeltaSeconds; const float T=FMath::Clamp(TurnElapsed/.45f,0.f,1.f);
            Rotor->SetRelativeRotation(FRotator(0,FMath::Lerp(SettledYaw,TargetYaw,T*T*(3-2*T)),0));
            if (T>=1)
            {
                SettledYaw=FMath::Fmod(TargetYaw,360.f); bTurning=false;
                Rotor->SetRelativeRotation(FRotator(0,SettledYaw,0)); SetBeamDirection(FRotator(0,SettledYaw,0).Vector());
                if (PendingTurns>0) BeginTurn();
            }
        }
    }
    // Emission is suspended throughout the turn, avoiding a sweeping room-wide hazard.
    Super::Tick(DeltaSeconds);
}
bool ABDRotatingLaserEmitter::ReceiveExplosion(const FVector& Origin,float Radius,uint64 ExplosionId,AActor* Source)
{
    if (IsActorBeingDestroyed() || !FMath::IsFinite(Radius) || Radius<=0 || Origin.ContainsNaN() || AcceptedBlasts.Contains(ExplosionId)) return false;
    const FVector Delta=GetSwitchPosition()-Origin; const double Distance=Delta.Size();
    constexpr float SwitchRadius=110.f;
    if (!FMath::IsFinite(Distance) || Distance>Radius+SwitchRadius) return false;
    const float ExposedDistance=FMath::Max(0.f,float(Distance)-SwitchRadius);
    if (ExposedDistance>.1f)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(RotatingEmitterBlastShield),false,this); if (Source) Q.AddIgnoredActor(Source);
        for (TActorIterator<ABDCharacter> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        for (TActorIterator<ABDCrate> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        for (TActorIterator<ABDBomb> It(GetWorld());It;++It) Q.AddIgnoredActor(*It);
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Delta.GetSafeNormal()*ExposedDistance,ECC_Visibility,Q)) return false;
    }
    if (!RequestQuarterTurn()) return false;
    AcceptedBlasts.Add(ExplosionId);
    return true;
}

ABDAlphaMirrorGameMode::ABDAlphaMirrorGameMode() { bAlphaPlayground=true; DefaultPawnClass=nullptr; }
void ABDAlphaMirrorGameMode::BeginPlay()
{
    Super::BeginPlay();
    TArray<AActor*> Existing;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->IsA<ABDCharacter>() || It->IsA<ABDBomb>() || It->IsA<ABDCrate>() || It->IsA<ABDBarrier>()
            || It->IsA<ABDPressurePlate>() || It->IsA<ABDGoal>() || It->IsA<AStaticMeshActor>() || It->IsA<APlayerStart>()) Existing.Add(*It);
    for (AActor* Actor:Existing) Actor->Destroy();
    Crate=nullptr; Goal=nullptr; Gate=nullptr; ActiveBomb=nullptr;
    BDAlphaLook::ConfigurePlaygroundLighting(GetWorld()); BuildRoom(); TryPossessPlayer();
}
void ABDAlphaMirrorGameMode::Tick(float DeltaSeconds)
{
    if (!bRoomReady && !TryPossessPlayer()) return;
    Super::Tick(DeltaSeconds);
}
bool ABDAlphaMirrorGameMode::TryPossessPlayer()
{
    auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!PC || !MirrorPlayer) return false;
    if (PC->GetPawn()!=MirrorPlayer) PC->Possess(MirrorPlayer);
    PC->ResetBombInput(); PC->SetPlaygroundView(FVector(0,0,0),2800,2800);
    MirrorPlayer->FacePoint(MirrorPlayer->GetActorLocation()+FVector(100,0,0)); bRoomReady=true; return true;
}
void ABDAlphaMirrorGameMode::OnBombExploded(ABDBomb* Bomb)
{
    if (!IsValid(Bomb) || !GameplayActive(GetWorld())) return;
    const uint64 Id=NextExplosionId++;
    for (ABDRotatingLaserMirror* Mirror:Mirrors) if (IsValid(Mirror)) Mirror->ReceiveExplosion(Bomb->GetActorLocation(),Bomb->GetProfile().BlastRadius*100.f,Id,Bomb);
    if (IsValid(CentralEmitter)) CentralEmitter->ReceiveExplosion(Bomb->GetActorLocation(),Bomb->GetProfile().BlastRadius*100.f,Id,Bomb);
}
bool ABDAlphaMirrorGameMode::TryRotateBaseAtRay(const FVector& Origin,const FVector& Direction)
{
    if (!bEnableClickRotation || !GameplayActive(GetWorld()) || Origin.ContainsNaN() || Direction.ContainsNaN()) return false;
    const FVector Axis=Direction.GetSafeNormal();
    if (FMath::Abs(Axis.Z)<UE_SMALL_NUMBER) return false;
    AActor* Selected=nullptr; FVector Target; double Nearest=TNumericLimits<double>::Max();
    auto Consider=[&](AActor* Device,const FVector& Center)
    {
        if (!IsValid(Device)) return;
        const double Along=(Center.Z+7.5-Origin.Z)/Axis.Z;
        if (!FMath::IsFinite(Along) || Along<0 || Along>=Nearest) return;
        const FVector Hit=Origin+Axis*Along;
        if (FVector::DistSquared2D(Hit,Center)>FMath::Square(140.f)) return;
        Selected=Device; Target=Hit; Nearest=Along;
    };
    for (ABDRotatingLaserMirror* Mirror:Mirrors) if (IsValid(Mirror)) Consider(Mirror,Mirror->GetSwitchPosition());
    if (IsValid(CentralEmitter)) Consider(CentralEmitter,CentralEmitter->GetSwitchPosition());
    if (!Selected) return false;
    // Keep distant/wall-occluded scenery from acting like a clickable base.
    // The selected device's own housing must not hide its integrated control.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MirrorBaseClick),false,Selected);
    FHitResult Obstruction;
    if (GetWorld()->LineTraceSingleByChannel(Obstruction,Origin,Target,ECC_Visibility,Q)) return false;
    if (auto* Mirror=Cast<ABDRotatingLaserMirror>(Selected)) return Mirror->RequestQuarterTurn();
    return CentralEmitter==Selected && CentralEmitter->RequestQuarterTurn();
}
bool ABDAlphaMirrorGameMode::TrySwapEdgeEmitterAtRay(const FVector& Origin,const FVector& Direction)
{
    if (!bEnableClickRotation || !GameplayActive(GetWorld()) || Origin.ContainsNaN() || Direction.ContainsNaN()) return false;
    const FVector Axis=Direction.GetSafeNormal(); if (Axis.IsNearlyZero()) return false;
    int32 Selected=INDEX_NONE; double Nearest=TNumericLimits<double>::Max();
    for (int32 I=0;I<FMath::Min(4,Lasers.Num());++I)
    {
        double Distance;
        if (IsValid(Lasers[I]) && Lasers[I]->IntersectsEmitterRay(Origin,Axis,Distance) && Distance<Nearest)
        { Selected=I; Nearest=Distance; }
    }
    if (Selected==INDEX_NONE || !EdgeMounts.IsValidIndex(Selected) || !IsValid(EdgeMounts[Selected])) return false;
    ABDMirrorLaser* Laser=Lasers[Selected]; auto* Mount=EdgeMounts[Selected].Get();
    FCollisionQueryParams ClickQuery(SCENE_QUERY_STAT(MirrorEmitterClick),false,Laser); ClickQuery.AddIgnoredActor(Mount);
    FHitResult Obstruction;
    if (GetWorld()->LineTraceSingleByChannel(Obstruction,Origin,Origin+Axis*Nearest,ECC_Visibility,ClickQuery)) return false;
    const FVector Old=Laser->GetActorLocation(),Facing=Laser->GetBeamDirection();
    const float Sign=Selected%2==0 ? 1.f : -1.f;
    const FVector Destination(Sign*Old.Y,Sign*Old.X,Old.Z),NewFacing(Sign*Facing.Y,Sign*Facing.X,0);
    const FQuat Turn(FVector::UpVector,FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Facing.Rotation().Yaw,NewFacing.Rotation().Yaw)));
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    FCollisionQueryParams Occupancy(SCENE_QUERY_STAT(MirrorEmitterDestination),false,Laser); Occupancy.AddIgnoredActor(Mount);
    for (UStaticMeshComponent* Body:{Laser->GetEmitterHousing(),Mount->GetStaticMeshComponent()})
    {
        const FVector Center=Destination+Turn.RotateVector(Body->Bounds.Origin-Old);
        const FVector Extent=Turn.RotateVector(Body->Bounds.BoxExtent).GetAbs()+FVector(3);
        // Use object channels so the mount may deliberately overlap its wall,
        // while characters, cargo and physical bombs can never be embedded.
        if (GetWorld()->OverlapAnyTestByObjectType(Center,FQuat::Identity,Objects,FCollisionShape::MakeBox(Extent),Occupancy)) return true;
        // Held bombs temporarily disable their physics collider but still occupy space.
        const FBox TargetBounds(Center-Extent,Center+Extent);
        for (TActorIterator<ABDBomb> It(GetWorld());It;++It)
            if (!It->IsActorBeingDestroyed() && It->IsHeld() && TargetBounds.ComputeSquaredDistanceToPoint(It->GetActorLocation())<=FMath::Square(It->GetWorldRadius())) return true;
    }
    Laser->RelocateEmitter(Destination,NewFacing);
    for (ABDMirrorLaser* Other:Lasers) if (IsValid(Other)) { Other->ResetContactHistory(); Other->RefreshPath(); }
    return true;
}
void ABDAlphaMirrorGameMode::BuildRoom()
{
    UWorld* World=GetWorld();
    Box(World,TEXT("Mirror grid floor"),FVector(0,-1,0),FVector(24,2,24),TEXT("Floor"),true);
    for (int32 R=0;R<8;++R) for (int32 C=0;C<8;++C)
    {
        auto* Tile=Box(World,TEXT("Mirror grid tile"),FVector(-10.5+C*3,.006,-10.5+R*3),FVector(2.955,.012,2.955),TEXT("FloorLight"));
        if ((R+C)%2) BDVisuals::Tint(Tile->GetStaticMeshComponent(),FLinearColor::LerpUsingHSV(BDAlphaLook::Color(TEXT("FloorLight")),FLinearColor::White,.055f));
    }
    Box(World,TEXT("Mirror backdrop"),FVector(0,-3.3,0),FVector(80,.04,80),TEXT("Backdrop"));
    Wall(World,TEXT("Mirror north boundary"),FVector(0,1.6,12.3),FVector(24.6,3.2,.6),true);
    Wall(World,TEXT("Mirror south boundary"),FVector(0,1,-12.3),FVector(24.6,2,.6),true);
    Wall(World,TEXT("Mirror west boundary"),FVector(-12.3,1.6,0),FVector(.6,3.2,24),false);
    Wall(World,TEXT("Mirror east boundary"),FVector(12.3,1.6,0),FVector(.6,3.2,24),false);
    // Screen-space NW, NE, SW, SE: backslash, backslash, slash, slash.
    // NE starts one quarter-turn clockwise from the original sketch.
    // The room intentionally keeps all four sources from the player's sketch.
    const FVector Positions[]={FVector(-6,0,6),FVector(6,0,6),FVector(-6,0,-6),FVector(6,0,-6)};
    for (int32 I=0;I<4;++I)
    {
        auto* Mirror=World->SpawnActor<ABDRotatingLaserMirror>(Unity(Positions[I]),FRotator::ZeroRotator,SpawnParams());
        Identify(Mirror,FString::Printf(TEXT("Mirror %c"),TCHAR('A'+I))); Mirror->Initialize(I==0 ? 45 : I==1 ? 225 : 135); Mirrors.Add(Mirror);
    }
    // Bring the housing forward enough to remain visible over the near wall.
    // A solid rear mounting arm keeps it connected to that wall so the new
    // visual clearance does not create a walkable shortcut behind the source.
    const FVector Emitters[]={FVector(-6,.75,10.5),FVector(10.5,.75,6),FVector(6,.75,-10.5),FVector(-10.5,.75,-6)};
    const FVector Directions[]={FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0),FVector(1,0,0)};
    for (int32 I=0;I<4;++I)
    {
        const FVector Outward(-Directions[I].X,0,Directions[I].Y);
        FVector MountPosition=Emitters[I]+Outward*1.1f; MountPosition.Y=.7f;
        const FVector MountSize=I%2 ? FVector(1.4,1.4,.65) : FVector(.65,1.4,1.4);
        auto* Mount=Box(World,TEXT("Laser wall mount"),MountPosition,MountSize,TEXT("Stone"),true);
        BDVisuals::Tint(Mount->GetStaticMeshComponent(),UnityColor(FLinearColor(.22f,.30f,.34f)));
        auto* Laser=World->SpawnActor<ABDMirrorLaser>(Unity(Emitters[I]),FRotator::ZeroRotator,SpawnParams());
        Identify(Laser,FString::Printf(TEXT("Mirror laser %d"),I+1)); Laser->Configure(Directions[I]); Lasers.Add(Laser);
        Mount->AttachToActor(Laser,FAttachmentTransformRules::KeepWorldTransform); EdgeMounts.Add(Mount);
    }
    CentralEmitter=World->SpawnActor<ABDRotatingLaserEmitter>(FVector(0,0,75),FRotator::ZeroRotator,SpawnParams());
    Identify(CentralEmitter,TEXT("Central rotating laser")); CentralEmitter->Initialize(FVector(0,1,0)); Lasers.Add(CentralEmitter);
    Goal=World->SpawnActor<ABDGoal>(Unity(FVector(9,1.8,9)),FRotator::ZeroRotator,SpawnParams());
    Identify(Goal,TEXT("Mirror delivery goal")); Goal->Extent=FVector(285,285,180); Goal->OnConstruction(Goal->GetActorTransform());
    BDAlphaLook::Material(Goal->Mesh,TEXT("Goal")); Goal->Mesh->SetRelativeScale3D(FVector(5.7,5.7,.05)); Goal->Mesh->SetRelativeLocation(FVector(0,0,-177.5));
    for (float Side:{-1.f,1.f})
    {
        Box(World,TEXT("Mirror goal outline"),FVector(9+Side*2.835,.06,9),FVector(.1275,.025,5.67),TEXT("GoalOutline"));
        Box(World,TEXT("Mirror goal outline"),FVector(9,.06,9+Side*2.835),FVector(5.67,.025,.1275),TEXT("GoalOutline"));
    }
    for (int32 I=0;I<48;++I)
    {
        const float A=I*2*PI/48,B=(I+1)*2*PI/48;
        Line(Goal,Goal->GetRootComponent(),TEXT("DeliveryRing"),FVector(FMath::Cos(A)*108,FMath::Sin(A)*108,-173),FVector(FMath::Cos(B)*108,FMath::Sin(B)*108,-173),10.5,FLinearColor(.62,1,.8));
    }
    Crate=World->SpawnActor<ABDCrate>(Unity(FVector(-7.8,.62,-9)),FRotator::ZeroRotator,SpawnParams()); Identify(Crate,TEXT("Mirror cargo"));
    TInlineComponentArray<UStaticMeshComponent*> CrateMeshes(Crate);
    for (auto* Mesh:CrateMeshes) BDAlphaLook::Material(Mesh,Mesh==Crate->Mesh ? TEXT("Crate") : TEXT("CrateBand"));
    MirrorPlayer=World->SpawnActor<ABDCharacter>(Unity(FVector(-10.8,.95,-10.1)),FRotator::ZeroRotator,SpawnParams()); Identify(MirrorPlayer,TEXT("Mirror courier"));
}
