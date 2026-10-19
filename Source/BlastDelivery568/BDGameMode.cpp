#include "BDGameMode.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDHUD.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
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
    for (TActorIterator<ABDCrate> It(GetWorld()); It; ++It) { Crate = *It; break; }
    for (TActorIterator<ABDGoal> It(GetWorld()); It; ++It) { Goal = *It; break; }
    for (TActorIterator<ABDGate> It(GetWorld()); It; ++It) { Gate = *It; break; }
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
    if (!IsGameplayInputAllowed() || ActiveBomb || !IsValid(Holder) || UGameplayStatics::IsGamePaused(this)) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ActiveBomb = GetWorld()->SpawnActor<ABDBomb>(Holder->GetBombPosition(), FRotator::ZeroRotator, Params);
    if (ActiveBomb)
    {
        ActiveBomb->Ignite(Holder, FuseSeconds);
        PrepareBomb(ActiveBomb, Holder);
        ++BombsUsed;
    }
    return ActiveBomb;
}
void ABDGameMode::ClearBomb(ABDBomb* Bomb)
{
    if (ActiveBomb == Bomb) ActiveBomb = nullptr;
}
void ABDGameMode::ResolveBombExplosion(const FVector& Position, ABDCharacter* Holder, ABDBomb* Bomb)
{
    ExplodeAt(Position, Holder, Bomb);
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
    // Iterate movable primitive bodies once. Query-only characters/bombs never transfer contact impulses.
    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BlastBodies), false, Bomb);
    GetWorld()->OverlapMultiByObjectType(Overlaps, Position, FQuat::Identity, Objects,
        FCollisionShape::MakeSphere(BlastRadius), Params);
    TSet<UPrimitiveComponent*> Applied;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        UPrimitiveComponent* Body = Overlap.GetComponent();
        AActor* Target = Overlap.GetActor();
        if (!Body || !Target || !Body->IsSimulatingPhysics() || Applied.Contains(Body)) continue;
        Applied.Add(Body);
        const float Strength = CrateImpulse * BlastFalloff(FVector::Dist(Position, Body->GetComponentLocation()), BlastRadius);
        if (Strength <= 0.f || !HasBlastLineOfSight(Position, Target, Bomb)) continue;
        if (auto* HeavyCrate = Cast<ABDCrate>(Target))
        {
            HeavyCrate->ApplyBlast(Position, Strength);
            OnCrateBlasted(HeavyCrate);
        }
        else Body->AddImpulse((Body->GetComponentLocation()-Position).GetSafeNormal()*Strength);
    }
    if (auto* Player = Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        if (Holder == Player)
        {
            Player->LaunchCharacter(GetHeldBlastVelocity(Player), true, true);
            OnPlayerBlasted(Player);
        }
        else
        {
            const float Falloff = BlastFalloff(FVector::Dist(Position,Player->GetActorLocation()), BlastRadius);
            if (Falloff > 0.f && HasBlastLineOfSight(Position, Player, Bomb))
            {
                Player->LaunchCharacter((Player->GetActorLocation()-Position).GetSafeNormal2D()*JumpHorizontalSpeed*Falloff
                    + FVector(0,0,JumpVerticalSpeed*Falloff), true, true);
                OnPlayerBlasted(Player);
            }
        }
    }
    GetWorld()->SpawnActor<ABDBlastFlash>(Position, FRotator::ZeroRotator);
}
FVector ABDGameMode::GetHeldBlastVelocity(const ABDCharacter* Player) const
{
    return -Player->GetActorForwardVector()*JumpHorizontalSpeed + FVector(0,0,JumpVerticalSpeed);
}
void ABDGameMode::RestartTutorial()
{
    UGameplayStatics::SetGamePaused(this, false);
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}
void ABDGameMode::CompleteTutorial()
{
    bCompleted = true;
    if (IsValid(ActiveBomb) && !ActiveBomb->IsBanana()) ActiveBomb->Destroy();
    if (auto* Player = Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
        Player->GetCharacterMovement()->StopMovementImmediately();
}
