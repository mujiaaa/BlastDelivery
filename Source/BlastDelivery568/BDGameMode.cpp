#include "BDGameMode.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDHUD.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDAlphaSession.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

ABDGameMode::ABDGameMode()
{
    DefaultPawnClass = ABDCharacter::StaticClass();
    PlayerControllerClass = ABDPlayerController::StaticClass();
    HUDClass = ABDHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
}
void ABDGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Session = GetGameInstance<UBDAlphaSession>())
        PhysicsSettings = Session->GetPlaygroundSettings();
    SyncLegacyParameters();
    GetWorldSettings()->bGlobalGravitySet = true;
    GetWorldSettings()->GlobalGravityZ = -981.f;
    for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It) { Crate = *It; break; }
    for (TActorIterator<ABDGoal> It(GetWorld()); It; ++It) { Goal = *It; break; }
    for (TActorIterator<ABDGate> It(GetWorld()); It; ++It) { Gate = *It; break; }
    ApplyPhysicsSettings(PhysicsSettings);
}
void ABDGameMode::SyncLegacyParameters()
{
    FuseSeconds=PhysicsSettings.FuseSeconds;
    BlastRadius=PhysicsSettings.BlastRadius*100.f;
    CrateImpulse=PhysicsSettings.BlastImpulse*100.f;
    ThrowSpeed=PhysicsSettings.ThrowSpeed*100.f;
    ThrowLift=PhysicsSettings.ThrowLift*100.f;
    JumpHorizontalSpeed=PhysicsSettings.HeldRecoilSpeed*100.f;
    JumpVerticalSpeed=PhysicsSettings.HeldBlastLift*100.f;
    SettleSeconds=PhysicsSettings.GoalSettleSeconds;
}
bool ABDGameMode::ApplyPhysicsSettings(const FBDPhysicsSettings& Settings)
{
    const FBDPhysicsSettings Previous=PhysicsSettings;
    bool bAccepted=true;
    for (TActorIterator<ABDCharacter> It(GetWorld()); It; ++It) bAccepted=It->ApplySettings(Settings) && bAccepted;
    for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It) bAccepted=It->ApplySettings(Settings) && bAccepted;
    for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It) bAccepted=It->ApplySize(Settings.BombSize) && bAccepted;
    if (!bAccepted)
    {
        for (TActorIterator<ABDCharacter> It(GetWorld()); It; ++It) It->ApplySettings(Previous);
        for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It) It->ApplySettings(Previous);
        for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It) It->ApplySize(Previous.BombSize);
        return false;
    }
    PhysicsSettings=Settings;
    SyncLegacyParameters();
    return true;
}
void ABDGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bCompleted) return;
    ElapsedSeconds += DeltaSeconds;
    SettledFor = Goal && Goal->ContainsSettledCrate(Crate) ? SettledFor + DeltaSeconds : 0.f;
    if (SettledFor >= SettleSeconds) CompleteTutorial();
}
float ABDGameMode::BlastFalloff(float Distance, float Radius)
{
    return Radius > 0.f ? FMath::Clamp(1.f - Distance / Radius, 0.f, 1.f) : 0.f;
}
ABDBomb* ABDGameMode::DrawBomb(ABDCharacter* Holder)
{
    // EndPlay/Explode normally clear this immediately. Also discard a stale or
    // destroying actor before enforcing the single-live-bomb rule.
    if (!IsValid(ActiveBomb) || ActiveBomb->IsActorBeingDestroyed()) ActiveBomb = nullptr;
    if (!IsGameplayInputAllowed() || !IsValid(Holder) || Holder->IsRecovering() || UGameplayStatics::IsGamePaused(this)) return nullptr;
    if (ABDBomb* Held=GetBombForPlayer(Holder)) if (Held->GetHolder()==Holder) return Held;
    if (ABDBomb* Nearby=FindNearbyBomb(Holder))
    {
        if (Nearby->TryPickup(Holder)) return Nearby;
    }
    if (GetOwnedBombCount(Holder)>0) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABDBomb* Bomb = GetWorld()->SpawnActor<ABDBomb>(Holder->GetBombPosition(), FRotator::ZeroRotator, Params);
    if (Bomb)
    {
        if (!Bomb->Ignite(Holder, FuseSeconds))
        {
            Bomb->Destroy();
            return nullptr;
        }
        PrepareBomb(Bomb,Holder);
        AssignBomb(Bomb,Holder);
        ++BombsUsed;
    }
    return Bomb;
}
ABDBomb* ABDGameMode::GetBombForPlayer(const ABDCharacter* Player) const
{
    if (!Player) return nullptr;
    if (const auto* Found=PlayerBombs.Find(const_cast<ABDCharacter*>(Player)))
        if (IsValid(*Found) && !(*Found)->IsActorBeingDestroyed()) return Found->Get();
    // Preserve the primary-player alias consumed by the existing tutorial and HUD.
    if (Player==UGameplayStatics::GetPlayerPawn(this,0) && IsValid(ActiveBomb) && !ActiveBomb->IsActorBeingDestroyed()) return ActiveBomb;
    return nullptr;
}
int32 ABDGameMode::GetOwnedBombCount(const ABDCharacter* Player) const
{
    int32 Count=0;
    for (const auto& Pair:BombOwners)
        if (Pair.Value==Player && IsValid(Pair.Key) && !Pair.Key->IsActorBeingDestroyed()) ++Count;
    return Count;
}
void ABDGameMode::RefreshPrimaryBomb()
{
    auto* Primary=Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    const auto* Found=PlayerBombs.Find(Primary);
    ActiveBomb=Found && IsValid(*Found) ? Found->Get() : nullptr;
}
void ABDGameMode::AssignBomb(ABDBomb* Bomb,ABDCharacter* Player)
{
    if (const auto* Previous=BombOwners.Find(Bomb))
    {
        ABDCharacter* Old=Previous->Get();
        if (auto* Current=PlayerBombs.Find(Old))
            if (*Current==Bomb) PlayerBombs.Remove(Old);
    }
    BombOwners.Add(Bomb,Player);
    PlayerBombs.Add(Player,Bomb);
    // Catching a live bomb transfers ownership, but any other live bomb from
    // either player continues to exist and keeps its original fuse/profile.
    for (const auto& Pair:BombOwners)
        if (IsValid(Pair.Key) && IsValid(Pair.Value) && !PlayerBombs.Contains(Pair.Value)) PlayerBombs.Add(Pair.Value,Pair.Key);
    RefreshPrimaryBomb();
}
ABDBomb* ABDGameMode::FindNearbyBomb(ABDCharacter* Player) const
{
    if (!Player) return nullptr;
    ABDBomb* Closest=nullptr;
    float Best=PhysicsSettings.PickupRadius*100.f;
    for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It)
    {
        if (It->IsHeld() || It->IsBeingEaten() || It->IsActorBeingDestroyed()) continue;
        const float Distance=FVector::Dist(Player->GetActorLocation(),It->GetActorLocation());
        if (Distance <= Best && HasBlastLineOfSight(Player->GetActorLocation(),*It,Player))
        { Closest=*It; Best=Distance; }
    }
    return Closest;
}
void ABDGameMode::ClearBomb(ABDBomb* Bomb)
{
    BombOwners.Remove(Bomb);
    for (auto It=PlayerBombs.CreateIterator();It;++It) if (It.Value()==Bomb) It.RemoveCurrent();
    for (const auto& Pair:BombOwners)
        if (IsValid(Pair.Key) && !Pair.Key->IsActorBeingDestroyed() && IsValid(Pair.Value) && !PlayerBombs.Contains(Pair.Value)) PlayerBombs.Add(Pair.Value,Pair.Key);
    RefreshPrimaryBomb();
}
bool ABDGameMode::HasBlastLineOfSight(const FVector& From, AActor* Target, AActor* Bomb) const
{
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BlastOcclusion), false, Bomb);
    Params.AddIgnoredActor(Target);
    return !GetWorld()->LineTraceSingleByChannel(Hit, From, Target->GetActorLocation(), ECC_Visibility, Params);
}
void ABDGameMode::ExplodeAt(const FVector& Position, ABDCharacter* Holder, AActor* Bomb)
{
    if (bCompleted) return;
    const ABDBomb* Source=Cast<ABDBomb>(Bomb);
    const FBDPhysicsSettings Profile=Source ? Source->GetExplosionProfile() : PhysicsSettings;
    const float Radius=Profile.BlastRadius*100.f;
    // Resting cargo remains queryable while locked against contact pushing.
    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BlastBodies), false, Bomb);
    GetWorld()->OverlapMultiByObjectType(Overlaps, Position, FQuat::Identity, Objects,
        FCollisionShape::MakeSphere(Radius), Params);
    TSet<UPrimitiveComponent*> Applied;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        UPrimitiveComponent* Body = Overlap.GetComponent();
        AActor* Target = Overlap.GetActor();
        if (!Body || !Target || Applied.Contains(Body)) continue;
        Applied.Add(Body);
        const float Strength = Profile.BlastImpulse*100.f * BlastFalloff(FVector::Dist(Position, Body->GetComponentLocation()), Radius);
        if (Strength <= 0.f || !HasBlastLineOfSight(Position, Target, Bomb)) continue;
        if (auto* HeavyCrate = Cast<ABDCrate>(Target))
        {
            HeavyCrate->ApplyBlast(Position, Strength);
            OnCrateBlasted(HeavyCrate);
        }
        else if (Body->IsSimulatingPhysics()) Body->AddImpulse((Body->GetComponentLocation()-Position).GetSafeNormal()*Strength);
    }
    for (TActorIterator<ABDCharacter> It(GetWorld()); It; ++It)
    {
        ABDCharacter* Player=*It;
        if (Holder == Player)
        {
            Player->ApplyBlastVelocity(GetHeldBlastVelocity(Player));
            OnPlayerBlasted(Player);
        }
        else
        {
            const float Falloff = BlastFalloff(FVector::Dist(Position,Player->GetActorLocation()), Radius);
            if (Falloff > 0.f && HasBlastLineOfSight(Position, Player, Bomb))
            {
                Player->ApplyBlastVelocity((Player->GetActorLocation()-Position).GetSafeNormal2D()*Profile.PlayerBlastSpeed*100.f*Falloff
                    + FVector(0,0,Profile.GroundBlastLift*100.f*Falloff));
                OnPlayerBlasted(Player);
            }
        }
    }
    if (auto* Flash=GetWorld()->SpawnActor<ABDBlastFlash>(Position,FRotator::ZeroRotator)) Flash->Configure(Radius);
}
FVector ABDGameMode::GetHeldBlastVelocity(const ABDCharacter* Player) const
{
    const ABDBomb* Held=GetBombForPlayer(Player);
    const FBDPhysicsSettings Profile=IsValid(Held) ? Held->GetExplosionProfile() : PhysicsSettings;
    return -Player->GetActorForwardVector()*Profile.HeldRecoilSpeed*100.f + FVector(0,0,Profile.HeldBlastLift*100.f);
}
void ABDGameMode::RestartTutorial()
{
    UGameplayStatics::SetGamePaused(this, false);
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}
void ABDGameMode::CompleteTutorial()
{
    bCompleted = true;
    for (TActorIterator<ABDBomb> It(GetWorld());It;++It) It->Destroy();
    for (TActorIterator<ABDCharacter> It(GetWorld());It;++It) It->GetCharacterMovement()->StopMovementImmediately();
    if (Crate && Crate->Mesh->IsSimulatingPhysics())
    {
        Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        Crate->Mesh->SetSimulatePhysics(false);
    }
}

void ABDGameMode::ResolveBombExplosion(const FVector& Position, ABDCharacter* Holder, ABDBomb* Bomb)
{
    ExplodeAt(Position,Holder,Bomb);
}
