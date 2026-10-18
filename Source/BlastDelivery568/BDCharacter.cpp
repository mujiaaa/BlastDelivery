#include "BDCharacter.h"
#include "BDBomb.h"
#include "BDCharacterMovement.h"
#include "BDAlphaLook.h"
#include "BDGameMode.h"
#include "BDVisuals.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ABDCharacter::ABDCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UBDCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
    // Swept movement blocks cargo without imparting contact impulses.
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->bOrientRotationToMovement = false;
    Move->bEnablePhysicsInteraction = false;
    Move->PushForceFactor = Move->InitialPushForceFactor = Move->TouchForceFactor = 0.f;
    Move->RepulsionForce = Move->StandingDownwardForceScale = 0.f;
    Move->GravityScale = 1.f;
    Move->MaxStepHeight = 12.f;
    Move->PerchRadiusThreshold = 12.f;
    Move->bCanWalkOffLedges = true;
    Move->MaxSimulationTimeStep = 1.f / 60.f;
    Move->MaxSimulationIterations = 12;
    // Unity's visual-only hierarchy: local (right,up,forward) becomes UE
    // (forward,right,up). Collision and aiming remain on the unchanged capsule.
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FacingVisual"));
    VisualRoot->SetupAttachment(GetCapsuleComponent());
    AddSoftCapsule(TEXT("RoundSuit"),FVector(0,0,-15),FVector(.83,.57,.75));
    AddPart(TEXT("Head"),FVector(0,0,42),FVector(.96));
    AddPart(TEXT("SoftFace"),FVector(34,0,43),FVector(.27,.70,.55),false,true);
    AddPart(TEXT("LeftEye"),FVector(47.5,-15,48),FVector(.045,.095,.19),true);
    AddPart(TEXT("RightEye"),FVector(47.5,15,48),FVector(.045,.095,.19),true);
    auto* Tuft=AddSoftCapsule(TEXT("Tuft"),FVector(0,-10,91),FVector(.13,.16,.13));
    Tuft->SetRelativeRotation(FQuat(FVector::ForwardVector,FMath::DegreesToRadians(35.f)));
    LeftHand=AddPart(TEXT("LeftArm"),FVector(12,-43,2),FVector(.30));
    RightHand=AddPart(TEXT("RightArm"),FVector(12,43,2),FVector(.30));
    LeftFoot=AddPart(TEXT("LeftFoot"),FVector(0,-24,-76),FVector(.45,.30,.22));
    RightFoot=AddPart(TEXT("RightFoot"),FVector(0,24,-76),FVector(.45,.30,.22));
}
UStaticMeshComponent* ABDCharacter::AddPart(const FName Name, const FVector& Location, const FVector& Scale,
    bool bEye, bool bLight, USceneComponent* Parent, const TCHAR* Shape)
{
    auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(Parent ? Parent : VisualRoot.Get());
    BDVisuals::Shape(Part, Shape);
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(Scale);
    if (bEye) Eyes.Add(Part); else if (bLight) LightParts.Add(Part); else BodyParts.Add(Part);
    return Part;
}
USceneComponent* ABDCharacter::AddSoftCapsule(const FName Name,const FVector& Location,const FVector& UnityScale)
{
    auto* Root=CreateDefaultSubobject<USceneComponent>(Name);
    Root->SetupAttachment(VisualRoot);
    Root->SetRelativeLocation(Location);
    // Unity's primitive capsule is one metre wide and two metres high: a
    // one-metre cylinder with hemispherical ends. Reproduce it with stock meshes.
    const FVector Scale(UnityScale.Z,UnityScale.X,UnityScale.Y);
    AddPart(FName(*(Name.ToString()+TEXT("Middle"))),FVector::ZeroVector,Scale,false,false,Root,TEXT("Cylinder"));
    AddPart(FName(*(Name.ToString()+TEXT("Top"))),FVector(0,0,50.f*UnityScale.Y),Scale,false,false,Root);
    AddPart(FName(*(Name.ToString()+TEXT("Bottom"))),FVector(0,0,-50.f*UnityScale.Y),Scale,false,false,Root);
    return Root;
}
void ABDCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Profile = Mode->GetPhysicsSettings();
    // Saved maps used a shorter capsule. Correct existing feet penetrating the floor.
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CharacterInitialFloor), false, this);
    if (GetWorld()->LineTraceSingleByChannel(Floor, GetActorLocation()+FVector(0,0,100),
        GetActorLocation()-FVector(0,0,300), ECC_Visibility, Query) && Floor.ImpactNormal.Z > .7f)
    {
        const float MinimumZ = Floor.ImpactPoint.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f;
        if (GetActorLocation().Z < MinimumZ) SetActorLocation(FVector(GetActorLocation().X,GetActorLocation().Y,MinimumZ));
    }
    ApplySettings(Profile);
    SpawnTransform = GetActorTransform();
    SpawnFloorZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    const bool bAlphaPalette=Mode && Mode->IsPlayground();
    auto ApplyMaterial=[bAlphaPalette](UStaticMeshComponent* Part,const TCHAR* Name)
    {
        if (bAlphaPalette) BDAlphaLook::Material(Part,Name);
        else BDVisuals::Material(Part,Name);
    };
    for (auto& Part : BodyParts) ApplyMaterial(Part,TEXT("Player"));
    for (auto& Eye : Eyes) ApplyMaterial(Eye,TEXT("Ink"));
    // The old tutorial palette has no Light asset; the new face uses its own
    // Alpha material in either room, without replacing any tutorial material.
    for (auto& Part : LightParts) BDAlphaLook::Material(Part,TEXT("Light"));
    ResetPresentation();
}
bool ABDCharacter::ApplySettings(const FBDPhysicsSettings& Settings)
{
    const float Size = FMath::Clamp(Settings.PlayerSize, .5f, 2.f);
    const float Previous = GetActorScale3D().X;
    const FVector Position = GetActorLocation() + FVector(0,0,90.f*(Size-Previous));
    if (Size > Previous + KINDA_SMALL_NUMBER && GetWorld() && bLocalConnected)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayerResize), false, this);
        for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It) if (It->IsHeld()) Query.AddIgnoredActor(*It);
        if (GetWorld()->OverlapBlockingTestByChannel(Position, GetActorQuat(), ECC_Pawn,
            FCollisionShape::MakeCapsule(FMath::Max(1.f,40.f*Size-.5f),90.f*Size-.5f), Query)) return false;
    }
    SetActorScale3D(FVector(Size));
    SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    Profile = Settings;
    Profile.PlayerSize = Size;
    WalkSpeed = Profile.MoveSpeed * 100.f;
    CastChecked<UBDCharacterMovement>(GetCharacterMovement())->Configure(Profile);
    return true;
}
void ABDCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsRecovering()) return;
    if (GetActorLocation().Z < Profile.FallKillHeight*100.f) { Respawn(); return; }
    const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!Mode || !Mode->IsGameplayInputAllowed() || UGameplayStatics::IsGamePaused(this)) return;
    UpdatePresentation(DeltaSeconds);
    const bool bRecoiling = CastChecked<UBDCharacterMovement>(GetCharacterMovement())->IsBlastLocked();
    const FVector Velocity = bRecoiling ? GetVelocity() : FVector(MoveIntent.X,MoveIntent.Y,0.f)*WalkSpeed;
    if (Velocity.SizeSquared2D() < 1.f) return;
    for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It)
    {
        if (It->IsHeld()) continue;
        const FVector Delta = It->GetActorLocation() - GetActorLocation();
        const float Reach = GetCapsuleComponent()->GetScaledCapsuleRadius() + It->GetWorldRadius() + 5.f;
        if (Delta.Size2D() <= Reach && FMath::Abs(Delta.Z) < GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+It->GetWorldRadius()
            && FVector::DotProduct(Velocity, Delta.GetSafeNormal2D()) > 0.f)
        {
            FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BombPushReach), false, this);
            Query.AddIgnoredActor(*It);
            if (!GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), It->GetActorLocation(), ECC_Visibility, Query))
                It->Push(Velocity, Delta.GetSafeNormal2D(), DeltaSeconds);
        }
    }
}
void ABDCharacter::ResetPresentation()
{
    WalkPhase=LandingPose=ThrowPose=0.f;
    bWasAirborne=bWasHolding=false;
    VisualFacing=GetActorQuat();
    VisualRoot->SetRelativeLocation(FVector::ZeroVector);
    VisualRoot->SetRelativeScale3D(FVector::OneVector);
    VisualRoot->SetWorldRotation(VisualFacing);
    LeftFoot->SetRelativeLocation(FVector(0,-24,-76));
    RightFoot->SetRelativeLocation(FVector(0,24,-76));
    LeftHand->SetRelativeLocation(FVector(12,-43,2));
    RightHand->SetRelativeLocation(FVector(12,43,2));
}
void ABDCharacter::UpdatePresentation(float DeltaSeconds)
{
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    const ABDBomb* Bomb=Mode ? Mode->GetBombForPlayer(this) : nullptr;
    const float Walking=FMath::Clamp(GetVelocity().Size2D()/FMath::Max(1.f,WalkSpeed),0.f,1.f);
    const bool bAirborne=GetCharacterMovement()->IsFalling();
    const bool bHolding=IsValid(Bomb) && Bomb->GetHolder()==this;
    if (bWasAirborne && !bAirborne) LandingPose=1.f;
    if (bWasHolding && !bHolding && IsValid(Bomb) && Bomb->WasThrown()) ThrowPose=1.f;
    bWasAirborne=bAirborne;
    bWasHolding=bHolding;
    WalkPhase+=DeltaSeconds*(5.f+Walking*8.f);
    LandingPose=FMath::Max(0.f,LandingPose-DeltaSeconds*4.f);
    ThrowPose=FMath::Max(0.f,ThrowPose-DeltaSeconds*4.f);
    const float Wobble=FMath::Sin(WalkPhase)*Walking;
    VisualFacing=FQuat::Slerp(VisualFacing,GetActorQuat(),1.f-FMath::Exp(-12.f*DeltaSeconds)).GetNormalized();
    const float Lean=(bAirborne ? -18.f : bHolding ? -7.f : Walking*9.f)-ThrowPose*18.f;
    const float Roll=bAirborne ? FMath::Sin(WalkPhase*.5f)*12.f : -Wobble*8.f;
    const FQuat Pose=FQuat(FVector::RightVector,FMath::DegreesToRadians(Lean))
        *FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Roll));
    VisualRoot->SetWorldRotation(VisualFacing*Pose);
    VisualRoot->SetRelativeLocation(FVector(0,0,bAirborne ? 0.f : FMath::Abs(Wobble)*6.5f-LandingPose*8.f));
    VisualRoot->SetRelativeScale3D(FVector(1.f+LandingPose*.14f,1.f+LandingPose*.20f,1.f-LandingPose*.25f));
    LeftFoot->SetRelativeLocation(FVector(Wobble*13.f,-24.f,-76.f+FMath::Max(0.f,Wobble)*10.f));
    RightFoot->SetRelativeLocation(FVector(-Wobble*13.f,24.f,-76.f+FMath::Max(0.f,-Wobble)*10.f));
    const float ArmsForward=bHolding ? 57.f : ThrowPose>0.f ? 60.f : 12.f;
    const float ArmHeight=bAirborne ? 45.f : bHolding ? 10.f : 2.f;
    LeftHand->SetRelativeLocation(FVector(ArmsForward,bAirborne ? -68.f : -43.f,ArmHeight-Wobble*7.f));
    RightHand->SetRelativeLocation(FVector(ArmsForward,bAirborne ? 68.f : 43.f,ArmHeight+Wobble*7.f));
}
void ABDCharacter::MoveInRoom(const FVector2D& Input)
{
    if (IsRecovering()) return;
    const FVector2D Clamped = Input.GetClampedToMaxSize(1.f);
    MoveIntent = Clamped;
    AddMovementInput(FVector(Clamped.X,Clamped.Y,0.f).GetSafeNormal(), Clamped.Size());
}
void ABDCharacter::FacePoint(const FVector& Point)
{
    FVector Direction = Point - GetActorLocation();
    Direction.Z = 0.f;
    if (!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
}
FVector ABDCharacter::GetBombPosition() const
{
    float Radius = Profile.BombRadius * Profile.BombSize * 100.f;
    if (const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (const ABDBomb* Bomb=Mode->GetBombForPlayer(this)) if (Bomb->GetHolder()==this) Radius=Bomb->GetWorldRadius();
    FVector Grip=GetActorLocation()+GetActorForwardVector()*(GetCapsuleComponent()->GetScaledCapsuleRadius()+Radius+12.f)
        +FVector(0,0,15.f*Profile.PlayerSize);
    const float Feet=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Grip.Z=FMath::Max(Grip.Z,Feet+Radius+4.f);
    return Grip;
}
void ABDCharacter::ApplyBlastVelocity(const FVector& VelocityChange)
{
    if (IsRecovering()) return;
    auto* Move = CastChecked<UBDCharacterMovement>(GetCharacterMovement());
    FVector NewVelocity = Move->Velocity + FVector(VelocityChange.X,VelocityChange.Y,0.f);
    NewVelocity.Z = FMath::Max(Move->Velocity.Z,VelocityChange.Z);
    Move->StartBlastLock();
    LaunchCharacter(NewVelocity, true, true);
}
void ABDCharacter::DrawBomb()
{
    if (!IsRecovering()) if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->DrawBomb(this);
}
void ABDCharacter::DropBomb()
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (ABDBomb* Bomb=Mode->GetBombForPlayer(this)) if (!IsRecovering() && Bomb->GetHolder()==this) Bomb->Release(false);
}
void ABDCharacter::ThrowBomb()
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
    {
        if (IsRecovering() || !Mode->IsGameplayInputAllowed()) return;
        if (ABDBomb* Bomb=Mode->GetBombForPlayer(this)) if (Bomb->GetHolder()==this) Bomb->Release(true);
    }
}
void ABDCharacter::Respawn()
{
    if (IsRecovering()) return;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
    {
        if (Mode->RequestSceneReset()) return;
        if (ABDBomb* Bomb=Mode->GetBombForPlayer(this)) if (Bomb->GetHolder()==this) Bomb->Release(false,false);
    }
    bRecovering = true;
    MoveIntent = FVector2D::ZeroVector;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    GetWorldTimerManager().SetTimer(RespawnTimer,this,&ABDCharacter::FinishRespawn,FMath::Max(.01f,Profile.RespawnDelay),false);
}
void ABDCharacter::FinishRespawn()
{
    FVector Position = SpawnTransform.GetLocation();
    Position.Z = SpawnFloorZ + GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    SetActorLocationAndRotation(Position,SpawnTransform.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    auto* Move = CastChecked<UBDCharacterMovement>(GetCharacterMovement());
    Move->ClearBlastLock();
    Move->StopMovementImmediately();
    Move->SetMovementMode(MOVE_Falling);
    bRecovering = false;
    ResetPresentation();
}
void ABDCharacter::SetLocalPlayerSeat(int32 Index)
{
    LocalPlayerSeat=FMath::Clamp(Index,0,3);
    const FLinearColor Colors[]={FLinearColor(.08f,.78f,.68f),FLinearColor(1.f,.36f,.28f),
        FLinearColor(.33f,.57f,1.f),FLinearColor(1.f,.78f,.18f)};
    auto Linear=[](float V){ return V<=.04045f ? V/12.92f : FMath::Pow((V+.055f)/1.055f,2.4f); };
    const FLinearColor C=Colors[LocalPlayerSeat];
    for (auto& Part:BodyParts) BDVisuals::Tint(Part,FLinearColor(Linear(C.R),Linear(C.G),Linear(C.B)));
    GetCharacterMovement()->bRunPhysicsWithNoController=LocalPlayerSeat>0;
}
void ABDCharacter::SetLocalPlayerConnected(bool bConnected,const FVector& SpawnPosition)
{
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    bLocalConnected=bConnected;
    bRecovering=false;
    MoveIntent=FVector2D::ZeroVector;
    auto* Move=CastChecked<UBDCharacterMovement>(GetCharacterMovement());
    Move->StopMovementImmediately();
    Move->ClearBlastLock();
    SetActorHiddenInGame(!bConnected);
    SetActorEnableCollision(bConnected);
    if (!bConnected) { Move->DisableMovement(); return; }
    SetActorLocation(SpawnPosition,false,nullptr,ETeleportType::TeleportPhysics);
    SpawnTransform=GetActorTransform();
    SpawnFloorZ=SpawnPosition.Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Move->SetMovementMode(MOVE_Falling);
    ResetPresentation();
}
