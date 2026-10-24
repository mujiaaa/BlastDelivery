#include "BDTutorialGameMode.h"
#include "BDTutorialHUD.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDVisuals.h"
#include "BDAlphaSession.h"
#include "Components/BoxComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

ABDTutorialGameMode::ABDTutorialGameMode()
{
    DefaultPawnClass = nullptr; // Each stage supplies the pawn that actually plays that scene.
    HUDClass = ABDTutorialHUD::StaticClass();
    SceneOrder = { EBDTutorialScene::ChargeCorridor };
}

void ABDTutorialGameMode::BeginPlay()
{
    Super::BeginPlay();
    // The shipped flow contains only the corridor, including when an older saved
    // config still lists the five retained lessons. Their builders, graphics and
    // transitions remain available for explicit editor fixtures and future reuse.
    SceneOrder = { EBDTutorialScene::ChargeCorridor };
    // Start on the first tick, after the local controller has initialized its viewport.
}

EBDTutorialScene ABDTutorialGameMode::GetScene() const
{
    return SceneOrder.IsValidIndex(SceneIndex) ? SceneOrder[SceneIndex] : EBDTutorialScene::ChargeCorridor;
}
EBDTutorialScene ABDTutorialGameMode::GetNextScene() const
{
    return SceneOrder.IsValidIndex(SceneIndex+1) ? SceneOrder[SceneIndex+1] : GetScene();
}
float ABDTutorialGameMode::GetSlideAlpha() const
{
    const float T = FMath::Clamp(PhaseTime / FMath::Clamp(SlideDuration,.25f,.5f),0.f,1.f);
    return T*T*(3.f-2.f*T);
}
AActor* ABDTutorialGameMode::GetLandingGround() const { return Current.LandingGround; }

void ABDTutorialGameMode::SetView(const FVector& Origin)
{
    if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
        PC->SetTutorialView(Origin + FVector(200,-220,40));
}

void ABDTutorialGameMode::BuildStage(FBDTutorialStage& Stage, EBDTutorialScene Scene, const FVector& Origin)
{
    Stage.Origin = Origin;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Floor = [&](FVector Center, FVector Size)
    {
        auto* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Origin+Center, FRotator::ZeroRotator, Params);
        auto* Mesh = Actor->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        BDVisuals::Shape(Mesh,TEXT("Cube"));
        BDVisuals::Material(Mesh,TEXT("FloorLight"));
        Mesh->SetWorldScale3D(Size/100.f);
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Stage.Actors.Add(Actor);
        return Actor;
    };
    const bool bCorridor = Scene == EBDTutorialScene::ChargeCorridor;
    const bool bGap = Scene == EBDTutorialScene::CrossGap || Scene == EBDTutorialScene::Deliver;
    if (bCorridor)
    {
        auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
        if (PC) PC->SetPlaygroundView(Origin+FVector(0,0,40),1800.f,1100.f);
        const float Width = PC ? PC->GetViewWidth() : 1956.f;
        CorridorBoxX = Width*.5f; // The right edge bisects the package on entry.
        // Two ordinary short-toss blasts can deliver the package into this end zone.
        CorridorEnd = CorridorBoxX+1500.f;
        Floor(FVector((CorridorEnd-Width*.46f)*.5f,0,-35),FVector(CorridorEnd+Width*.46f,1000,70));
        auto Wall = [&](FVector P,FVector Size)
        {
            auto* A = GetWorld()->SpawnActor<ABDBarrier>(Origin+P,FRotator::ZeroRotator,Params);
            A->Configure(Size); Stage.Actors.Add(A);
        };
        const float Left = -Width*.46f;
        Wall(FVector(Left,0,0),FVector(55,1000,80));
        Wall(FVector(CorridorEnd,0,0),FVector(55,1000,80));
        Wall(FVector((CorridorEnd+Left)*.5f,-500,0),FVector(CorridorEnd-Left,55,80));
        Wall(FVector((CorridorEnd+Left)*.5f,500,0),FVector(CorridorEnd-Left,55,80));
        Stage.Box = GetWorld()->SpawnActor<ABDCrate>(Origin+FVector(CorridorBoxX,0,63),FRotator::ZeroRotator,Params);
        Stage.Actors.Add(Stage.Box);
        const float GoalStart = CorridorBoxX+850.f;
        const float GoalRight = CorridorEnd-27.5f;
        Stage.DeliveryGoal = GetWorld()->SpawnActor<ABDGoal>(Origin+FVector((GoalStart+GoalRight)*.5f,0,100),FRotator::ZeroRotator,Params);
        Stage.DeliveryGoal->Extent = FVector((GoalRight-GoalStart)*.5f,472.5f,100);
        Stage.DeliveryGoal->OnConstruction(Stage.DeliveryGoal->GetActorTransform());
        Stage.Actors.Add(Stage.DeliveryGoal);
        Stage.Player = GetWorld()->SpawnActor<ABDCharacter>(Origin+FVector(0,0,93),FRotator::ZeroRotator,Params);
        Stage.Actors.Add(Stage.Player);
        FreezeStage(Stage);
        return;
    }
    if (bGap)
    {
        Floor(FVector(-590,0,-35),FVector(820,900,70)); // Ends at -180.
        Stage.LandingGround = Floor(FVector(790,0,-35),FVector(1220,900,70)); // Begins at +180.
    }
    else Floor(FVector(200,0,-35),FVector(2400,900,70));

    if (Scene == EBDTutorialScene::MoveBox || Scene == EBDTutorialScene::Deliver)
    {
        Stage.Box = GetWorld()->SpawnActor<ABDCrate>(Origin+FVector(Scene == EBDTutorialScene::Deliver ? 550 : 150,0,63),FRotator::ZeroRotator,Params);
        Stage.Actors.Add(Stage.Box);
    }
    if (Scene == EBDTutorialScene::Deliver)
    {
        Stage.DeliveryGoal = GetWorld()->SpawnActor<ABDGoal>(Origin+GoalCenter,FRotator::ZeroRotator,Params);
        Stage.DeliveryGoal->Extent = GoalExtent;
        Stage.DeliveryGoal->OnConstruction(Stage.DeliveryGoal->GetActorTransform());
        Stage.Actors.Add(Stage.DeliveryGoal);
    }
    const FVector Start(bGap ? -330 : -450,0,93);
    Stage.Player = GetWorld()->SpawnActor<ABDCharacter>(Origin+Start,FRotator::ZeroRotator,Params);
    Stage.Actors.Add(Stage.Player);
    FreezeStage(Stage);
}

void ABDTutorialGameMode::FreezeStage(FBDTutorialStage& Stage)
{
    if (Stage.Player)
    {
        Stage.Player->GetCharacterMovement()->StopMovementImmediately();
        Stage.Player->GetCharacterMovement()->DisableMovement();
        Stage.Player->SetActorTickEnabled(false);
    }
    if (Stage.Box)
    {
        Stage.Box->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Stage.Box->Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        Stage.Box->Mesh->SetSimulatePhysics(false);
        Stage.Box->SetActorTickEnabled(false);
    }
}

void ABDTutorialGameMode::DestroyStage(FBDTutorialStage& Stage)
{
    for (AActor* Actor : Stage.Actors) if (IsValid(Actor)) Actor->Destroy();
    Stage = FBDTutorialStage();
}

void ABDTutorialGameMode::ClearTransientActors()
{
    SuccessBomb.Reset();
    bWaitingForSuccessBomb = false;
    for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<ABDBlastFlash> It(GetWorld()); It; ++It) It->Destroy();
    ActiveBomb = nullptr;
    PlacedBomb.Reset();
}

void ABDTutorialGameMode::ActivateStage()
{
    Crate = Current.Box;
    Goal = Current.DeliveryGoal;
    bBoxBlasted = bPlayerBlasted = bWasAirborne = bResetRequested = false;
    bChargeDoodleVisible = bGoalDoodleVisible = false;
    SuccessBomb.Reset();
    bWaitingForSuccessBomb = false;
    SuccessDelayStartedAt = 0.f;
    DeliverySettledFor = BlastWatchTime = PhaseTime = 0.f;
    Phase = EBDTutorialPhase::Playing;
    BoxStart = Crate ? Crate->GetActorLocation() : FVector::ZeroVector;
    if (Crate)
    {
        Crate->Mesh->SetSimulatePhysics(true);
        Crate->Mesh->WakeAllRigidBodies();
        Crate->SetActorTickEnabled(true);
    }
    if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
    {
        PC->Possess(Current.Player);
        PC->ResetBombInput();
    }
    Current.Player->SetActorTickEnabled(true);
    Current.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    SetView(Current.Origin);
    if (GetScene() == EBDTutorialScene::ChargeCorridor) UpdateCorridorView();
}

FVector ABDTutorialGameMode::GetCorridorDoodlePosition(int32 Index) const
{
    const float X = Index == 0 ? -220.f : Index == 1 ? CorridorBoxX-250.f : CorridorEnd-420.f;
    return Current.Origin+FVector(X,270,10);
}

void ABDTutorialGameMode::UpdateCorridorView()
{
    auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!PC || !Current.Player) return;
    PC->SetPlaygroundView(Current.Origin+FVector(0,0,40),1800.f,1100.f);
    const float X = FMath::Clamp(Current.Player->GetActorLocation().X-Current.Origin.X,0.f,CorridorEnd-PC->GetViewWidth()*.46f);
    PC->SetPlaygroundView(Current.Origin+FVector(X,0,40),1800.f,1100.f);
    if (PhaseTime<.15f) return; // Wait for the first camera cache to use this view.
    int32 W,H; PC->GetViewportSize(W,H);
    auto Visible = [&](AActor* Actor, bool bFully)
    {
        if (!Actor || W<=0 || H<=0) return false;
        FVector C,E; Actor->GetActorBounds(false,C,E);
        float MinX=BIG_NUMBER,MinY=BIG_NUMBER,MaxX=-BIG_NUMBER,MaxY=-BIG_NUMBER;
        for (int32 I=0;I<8;++I)
        {
            FVector2D P;
            if (!PC->ProjectWorldLocationToScreen(C+E*FVector((I&1)?1:-1,(I&2)?1:-1,(I&4)?1:-1),P)) return false;
            MinX=FMath::Min(MinX,float(P.X)); MaxX=FMath::Max(MaxX,float(P.X));
            MinY=FMath::Min(MinY,float(P.Y)); MaxY=FMath::Max(MaxY,float(P.Y));
        }
        return bFully ? MinX>=8 && MaxX<=W-8 && MinY>=8 && MaxY<=H-8 : MaxX>8 && MinX<W-8 && MaxY>8 && MinY<H-8;
    };
    bChargeDoodleVisible |= Visible(Current.Box,true);
    bGoalDoodleVisible |= Visible(Current.DeliveryGoal,false);
}

void ABDTutorialGameMode::Tick(float DeltaSeconds)
{
    // The legacy room's settle/dwell logic does not run in the five-scene tutorial.
    AGameModeBase::Tick(DeltaSeconds);
    if (!bStarted)
    {
        if (!UGameplayStatics::GetPlayerController(this,0)) return;
        bStarted = true;
        BuildStage(Current,GetScene(),FVector::ZeroVector);
        ActivateStage();
    }
    if (bResetRequested)
    {
        const FVector Origin = Current.Origin;
        ClearTransientActors();
        DestroyStage(Current);
        BuildStage(Current,GetScene(),Origin);
        ActivateStage();
        return;
    }
    PhaseTime += DeltaSeconds;
    if (GetScene() == EBDTutorialScene::ChargeCorridor) UpdateCorridorView();
    if (Phase == EBDTutorialPhase::Finished) return;
    ElapsedSeconds += DeltaSeconds;
    if (Phase == EBDTutorialPhase::Sliding)
    {
        SetView(Current.Origin + FVector(SlideWidth*GetSlideAlpha(),0,0));
        if (PhaseTime >= FMath::Clamp(SlideDuration,.25f,.5f))
        {
            DestroyStage(Current);
            Current = MoveTemp(Incoming);
            Incoming = FBDTutorialStage();
            ++SceneIndex;
            ActivateStage();
        }
        return;
    }
    if (Phase == EBDTutorialPhase::Success)
    {
        if (bWaitingForSuccessBomb)
        {
            // Keep the real bomb ticking. Cleanup/destruction is not an explosion;
            // if it disappears unexpectedly, retry this scene instead of skipping it.
            if (!SuccessBomb.IsValid()) bResetRequested = true;
            return;
        }
        if (GetWorld()->GetTimeSeconds()-SuccessDelayStartedAt >= FMath::Max(SuccessDelay,0.f))
        {
            if (SceneOrder.IsValidIndex(SceneIndex+1)) StartSlide();
            else
            {
                Phase = EBDTutorialPhase::Finished;
                bCompleted = true;
                ClearTransientActors();
                FreezeStage(Current);
            }
        }
        return;
    }
    if (!IsValid(Current.Player)) return;
    if (bBoxBlasted) BlastWatchTime += DeltaSeconds;
    switch (GetScene())
    {
    case EBDTutorialScene::MoveBox:
        // Gravity, contact, teleports and a missed/occluded blast cannot satisfy this.
        if (Crate && bBoxBlasted && BlastWatchTime < 2.f
            && FVector::Dist2D(Crate->GetActorLocation(),BoxStart) >= BoxMoveDistance
            && FVector::Dist2D(Crate->GetActorLocation(),BoxAtBlast) >= BoxMoveDistance)
            MarkSuccess();
        break;
    case EBDTutorialScene::PutDown:
        if (PlacedBomb.IsValid() && !PlacedBomb->IsHeld())
        {
            FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(TutorialBombFloor),false,PlacedBomb.Get());
            Query.AddIgnoredActor(Current.Player);
            const FVector P = PlacedBomb->GetActorLocation();
            if (GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,PlacedBomb->GetWorldRadius()+8.f),ECC_Visibility,Query)
                && Current.Actors.Contains(Hit.GetActor()) && Hit.ImpactNormal.Z > .9f)
                MarkSuccess(PlacedBomb.Get());
        }
        break;
    case EBDTutorialScene::CrossGap:
    {
        const auto* Move = Current.Player->GetCharacterMovement();
        if (bPlayerBlasted && Move->IsFalling()) bWasAirborne = true;
        if (bWasAirborne && Move->IsMovingOnGround() && Move->CurrentFloor.HitResult.GetActor() == Current.LandingGround)
            MarkSuccess();
        break;
    }
    case EBDTutorialScene::ChargeCorridor:
    case EBDTutorialScene::Deliver:
        DeliverySettledFor = Goal && Goal->ContainsSettledCrate(Crate) ? DeliverySettledFor+DeltaSeconds : 0.f;
        if (DeliverySettledFor >= GetPhysicsSettings().GoalSettleSeconds) MarkSuccess();
        break;
    default: break;
    }
}

void ABDTutorialGameMode::OnBombReleased(ABDBomb* Bomb, bool bThrown)
{
    if (!IsGameplayInputAllowed() || Bomb != ActiveBomb) return;
    if (GetScene() == EBDTutorialScene::Throw && bThrown) MarkSuccess(Bomb);
    if (GetScene() == EBDTutorialScene::PutDown && (!bThrown || Bomb->GetChargeFraction()<=.2f))
    {
        PlacedBomb = Bomb;
        // A grounded release is a valid put-down immediately. An airborne release
        // still has to reach this scene's floor before it counts.
        if (!bThrown && Current.Player && Current.Player->GetCharacterMovement()->IsMovingOnGround()) MarkSuccess(Bomb);
    }
}
void ABDTutorialGameMode::OnBombExploded(ABDBomb* Bomb)
{
    if (Phase != EBDTutorialPhase::Success || !bWaitingForSuccessBomb || SuccessBomb.Get() != Bomb) return;
    bWaitingForSuccessBomb = false;
    SuccessBomb.Reset();
    SuccessDelayStartedAt = GetWorld()->GetTimeSeconds();
    // PhaseTime continues from the action, so the checkmark/flash/chime do not replay.
}
void ABDTutorialGameMode::OnCrateBlasted(ABDCrate* BlastedCrate)
{
    if (!IsGameplayInputAllowed() || BlastedCrate != Crate) return;
    bBoxBlasted = true;
    BlastWatchTime = 0.f;
    BoxAtBlast = Crate->GetActorLocation();
}
void ABDTutorialGameMode::OnPlayerBlasted(ABDCharacter* Player)
{
    if (IsGameplayInputAllowed() && Player == Current.Player) bPlayerBlasted = true;
}
bool ABDTutorialGameMode::RequestSceneReset()
{
    if (Phase == EBDTutorialPhase::Playing) bResetRequested = true;
    return true;
}
FVector ABDTutorialGameMode::GetHeldBlastVelocity(const ABDCharacter* Player) const
{
    const bool bGap = GetScene() == EBDTutorialScene::CrossGap || GetScene() == EBDTutorialScene::Deliver;
    const auto* Session=GetGameInstance<UBDAlphaSession>();
    const bool bAssist=Session ? Session->GetFeature(TEXT("tutorialAssist")) : bAssistGapJump;
    if (bAssist && bGap && Player->GetActorLocation().X < Current.Origin.X-180.f)
    {
        const FVector Normal=Super::GetHeldBlastVelocity(Player);
        return FVector(Normal.Size2D(),0,Normal.Z);
    }
    return Super::GetHeldBlastVelocity(Player);
}

void ABDTutorialGameMode::MarkSuccess(ABDBomb* BombToObserve)
{
    if (Phase != EBDTutorialPhase::Playing) return;
    Phase = EBDTutorialPhase::Success;
    PhaseTime = 0.f;
    SuccessBomb = BombToObserve;
    bWaitingForSuccessBomb = IsValid(BombToObserve);
    SuccessDelayStartedAt = GetWorld()->GetTimeSeconds();
    // Show feedback now; scenes 1 and 3 start their slide delay only on detonation.
    Current.Player->GetCharacterMovement()->StopMovementImmediately();
    if (auto* PC = Cast<ABDPlayerController>(Current.Player->GetController())) PC->ResetBombInput();
    PlaySuccessChime();
}

void ABDTutorialGameMode::StartSlide()
{
    ClearTransientActors();
    if (GetScene() == EBDTutorialScene::ChargeCorridor)
    {
        // The corridor spans several screens; start the retained five-scene sequence
        // cleanly after its success feedback instead of sliding through its walls.
        DestroyStage(Current);
        ++SceneIndex;
        BuildStage(Current,GetScene(),FVector::ZeroVector);
        ActivateStage();
        return;
    }
    FreezeStage(Current);
    auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    SlideWidth = PC ? PC->GetViewWidth() : 3000.f;
    BuildStage(Incoming,GetNextScene(),Current.Origin+FVector(SlideWidth,0,0));
    Phase = EBDTutorialPhase::Sliding;
    PhaseTime = 0.f;
    // Moving the orthographic camera by exactly one viewport width slides both real
    // scenes together. No screen captures, UI player duplicates or loading gap.
}

void ABDTutorialGameMode::RestartTutorial()
{
    UGameplayStatics::SetGamePaused(this,false);
    ClearTransientActors();
    DestroyStage(Incoming);
    DestroyStage(Current);
    SceneIndex = BombsUsed = 0;
    ElapsedSeconds = 0.f;
    bCompleted = false;
    bStarted = true;
    BuildStage(Current,GetScene(),FVector::ZeroVector);
    ActivateStage();
}

void ABDTutorialGameMode::PlaySuccessChime()
{
    // A short ascending major chord; generated PCM works in cooked builds as well.
    constexpr int32 SampleRate = 24000;
    constexpr float Duration = .26f;
    TArray<int16> Samples;
    Samples.SetNumZeroed(FMath::CeilToInt(SampleRate*Duration));
    for (int32 I=0; I<Samples.Num(); ++I)
    {
        const float T = float(I)/SampleRate;
        float Value = 0.f;
        for (int32 Note=0; Note<3; ++Note)
        {
            const float Age = T-Note*.045f;
            if (Age < 0.f) continue;
            const float Frequencies[] = { 659.25f, 830.61f, 987.77f };
            Value += FMath::Sin(2.f*PI*Frequencies[Note]*Age)*FMath::Min(Age/.008f,1.f)*FMath::Exp(-Age*19.f);
        }
        const float EndFade = FMath::Clamp((Duration-T)/.03f,0.f,1.f);
        Samples[I] = int16(FMath::Clamp(Value*.19f*EndFade,-1.f,1.f)*32767.f);
    }
    SuccessChime = NewObject<USoundWaveProcedural>(this);
    SuccessChime->SetSampleRate(SampleRate);
    SuccessChime->NumChannels = 1;
    SuccessChime->Duration = Duration;
    SuccessChime->SoundGroup = SOUNDGROUP_UI;
    SuccessChime->bLooping = false;
    SuccessChime->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()),Samples.Num()*sizeof(int16));
    const TWeakObjectPtr<UAudioComponent> Audio = UGameplayStatics::SpawnSound2D(this,SuccessChime,.7f);
    // Procedural waves can wait forever for more PCM after an underflow. End this
    // finite cue explicitly so every completed scene releases its audio voice.
    FTimerHandle StopHandle;
    GetWorldTimerManager().SetTimer(StopHandle,FTimerDelegate::CreateLambda([Audio]()
    {
        if (Audio.IsValid()) Audio->Stop();
    }),Duration+.04f,false);
}
