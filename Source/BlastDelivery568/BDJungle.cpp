#include "BDJungle.h"
#include "BDBomb.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDVisuals.h"
#include "BDAlphaSession.h"
#include "BDAlphaLook.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
bool JungleIsPlaying(const UWorld* World)
{
    const auto* Alpha = World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
    return !Alpha || Alpha->GetScreen() == EBDAlphaScreen::Playing;
}
}

ABDJungleSettings::ABDJungleSettings()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Settings"));
    SpawnPoints = { FVector(-400,-700,66), FVector(950,730,66), FVector(1020,-750,66) };
}
void ABDJungleSettings::BuildNavigation()
{
#if WITH_EDITOR
    FNavigationSystem::AddNavigationSystemToWorld(*GetWorld(),FNavigationSystemRunMode::EditorMode);
    if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        FlushAsyncLoading();
        // Python commandlets do not pump the editor frame that normally releases
        // the map-loading lock and consumes the bounds notifications.
        Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
        for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It) Nav->OnNavigationBoundsUpdated(*It);
        Nav->Tick(.01f);
        Nav->Build();
    }
#endif
}

bool ABDJungleCharacter::StartSlide(float Distance, float Speed)
{
    if (IsSliding() || !GetCharacterMovement()->IsMovingOnGround() || Distance <= 0.f) return false;
    SlideDirection = GetActorForwardVector().GetSafeNormal2D();
    SlideRemaining = Distance;
    SlideSpeed = FMath::Max(1.f, Speed);
    Super::MoveInRoom(FVector2D::ZeroVector);
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    return true;
}

void ABDJungleCharacter::EndSlide()
{
    if (!IsSliding()) return;
    SlideRemaining = 0.f;
    Super::MoveInRoom(FVector2D::ZeroVector);
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>()) Mode->OnSlideEnded(this);
}

void ABDJungleCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsSliding()) return;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>(); !JungleIsPlaying(GetWorld()) || (Mode && !Mode->IsGameplayInputAllowed()))
    { EndSlide(); return; }
    const float Step = FMath::Min(SlideRemaining, SlideSpeed * DeltaSeconds);
    FHitResult Hit;
    SetActorLocation(GetActorLocation() + SlideDirection * Step, true, &Hit);
    if (Hit.bBlockingHit || SlideRemaining <= Step + KINDA_SMALL_NUMBER) EndSlide();
    else SlideRemaining -= Step;
}

void ABDJungleCharacter::MoveInRoom(const FVector2D& Input)
{
    if (!IsSliding()) Super::MoveInRoom(Input);
}
void ABDJungleCharacter::FacePoint(const FVector& Point)
{
    if (!IsSliding()) Super::FacePoint(Point);
}
void ABDJungleCharacter::Respawn()
{
    EndSlide();
    Super::Respawn();
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>()) Mode->OnPlayerRespawned();
}

ABDBananaPeel::ABDBananaPeel()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PeelRoot"));
    for (int32 I=0; I<4; ++I)
    {
        auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Peel%d"),I));
        Part->SetupAttachment(RootComponent);
        BDVisuals::Shape(Part,TEXT("Sphere"));
        const float Angle = I * PI * .5f;
        Part->SetRelativeLocation(FVector(FMath::Cos(Angle)*19,FMath::Sin(Angle)*19,8));
        Part->SetRelativeScale3D(FVector(.65,.16,.12));
        Part->SetRelativeRotation(FRotator(-12,I*90,0));
        Pieces.Add(Part);
    }
    auto* Stem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stem"));
    Stem->SetupAttachment(RootComponent);
    BDVisuals::Shape(Stem,TEXT("Cone"));
    Stem->SetRelativeLocation(FVector(0,0,20));
    Stem->SetRelativeScale3D(FVector(.19,.19,.38));
    Pieces.Add(Stem);
}
void ABDBananaPeel::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    for (auto& Part : Pieces) BDVisuals::Material(Part,TEXT("JungleBanana"));
}
void ABDBananaPeel::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Player = Cast<ABDJungleCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>();
    if (!Player || !Mode || !Mode->Settings || !Mode->IsGameplayInputAllowed() || !JungleIsPlaying(GetWorld())) return;
    // Use the feet, not the old donor capsule's 66 cm centre. The Alpha console
    // can scale the player from 50 to 200 percent without disabling the peels.
    const FVector Feet = Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector P = GetActorTransform().InverseTransformPosition(Feet);
    const bool bInside = FMath::Abs(P.X)<=TriggerExtent.X && FMath::Abs(P.Y)<=TriggerExtent.Y && P.Z>=-15 && P.Z<35;
    // Every peel crossed during a slide is latched until that slide ends and the
    // player leaves it. Peels cannot chain themselves into an inescapable loop.
    if (bInside)
    {
        if (!bOccupied && !Player->IsSliding())
            bOccupied = Player->StartSlide(Mode->Settings->TileSize * Mode->Settings->SlideTiles, Mode->Settings->SlideSpeed);
        else if (Player->IsSliding()) bOccupied = true;
    }
    else if (!Player->IsSliding()) bOccupied = false;
}

ABDBananaZone::ABDBananaZone()
{
    Region = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBounds"));
    RootComponent = Region;
    Region->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YellowFloor"));
    Floor->SetupAttachment(Region);
    BDVisuals::Shape(Floor,TEXT("Cube"));
    for (int32 I=0; I<4; ++I)
    {
        auto* Border = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Border%d"),I));
        Border->SetupAttachment(Region);
        BDVisuals::Shape(Border,TEXT("Cube"));
        Borders.Add(Border);
    }
}
void ABDBananaZone::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Region->SetBoxExtent(Extent);
    Floor->SetRelativeLocation(FVector(0,0,3));
    Floor->SetRelativeScale3D(FVector(Extent.X*.02,Extent.Y*.02,.04));
    BDVisuals::Material(Floor,TEXT("JungleGrass"));
    for (int32 I=0; I<4; ++I)
    {
        const bool bX = I<2;
        Borders[I]->SetRelativeLocation(FVector(bX?(I==0?-Extent.X:Extent.X):0,bX?0:(I==2?-Extent.Y:Extent.Y),7));
        Borders[I]->SetRelativeScale3D(bX?FVector(.07,Extent.Y*.02,.05):FVector(Extent.X*.02,.07,.05));
        BDVisuals::Material(Borders[I],TEXT("JungleBanana"));
    }
}
bool ABDBananaZone::Contains(const FVector& Point) const
{
    const FVector P = GetActorTransform().InverseTransformPosition(Point);
    // Standing over the visible footprint counts, including the boundary. Blast
    // jumps cannot turn regular controls back on while over a zone.
    return FMath::Abs(P.X)<=Extent.X && FMath::Abs(P.Y)<=Extent.Y && P.Z>=-20 && P.Z<=2000;
}

ABDWildMan::ABDWildMan()
{
    AIControllerClass = AAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    WalkSpeed = ChaseSpeed;
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Ignore);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->bUseRVOAvoidance = true;
    GetCharacterMovement()->AvoidanceConsiderationRadius = 160.f;
    for (int32 I=0; I<12; ++I)
    {
        auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Hair%d"),I));
        Part->SetupAttachment(GetCapsuleComponent());
        BDVisuals::Shape(Part,I<9?TEXT("Cone"):TEXT("Sphere"));
        const float A = I*.78f;
        Part->SetRelativeLocation(I<9?FVector(FMath::Cos(A)*17,FMath::Sin(A)*19,59+(I%2)*6):FVector(22,(I-10)*13,32));
        Part->SetRelativeScale3D(I<9?FVector(.26,.25,.42):FVector(.18,.35,.18));
        Part->SetRelativeRotation(FRotator(I<9?24:0,I*45,0));
        Hair.Add(Part);
    }
}
void ABDWildMan::BeginPlay()
{
    Super::BeginPlay();
    Home = GetActorLocation();
    TArray<UStaticMeshComponent*> Parts;
    GetComponents(Parts);
    for (auto* Part : Parts)
        if (!Part->GetName().Contains(TEXT("Eye"))) BDVisuals::Material(Part,TEXT("JungleSkin"));
    for (auto& Part : Hair) BDVisuals::Material(Part,TEXT("JungleHair"));
    for (auto* Part : Parts)
        if (Part->GetName().Contains(TEXT("Body")) || Part->GetName().Contains(TEXT("Foot")))
            BDVisuals::Material(Part,TEXT("JungleLeaf"));
}
void ABDWildMan::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Food.IsValid() && Food->GetOwner()==this) Food->Destroy();
    Super::EndPlay(Reason);
}
bool ABDWildMan::IsDistracted() const { return Food.IsValid() || GetWorld()->GetTimeSeconds() < DistractedUntil; }
bool ABDWildMan::IsEating() const
{
    return EatingStarted>=0.f && Food.IsValid() && Food->GetConsumer()==this;
}
float ABDWildMan::GetEatingRemaining() const
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>();
    return IsEating() && Mode && Mode->Settings ?
        FMath::Max(0.f,FMath::Max(1.f,Mode->Settings->EatingSeconds)-(GetWorld()->GetTimeSeconds()-EatingStarted)) : 0.f;
}
void ABDWildMan::Attract(const FVector& Position, float Seconds)
{
    if (IsEating()) return;
    if (MealLifeSpan>0.f) { SetLifeSpan(MealLifeSpan); MealLifeSpan=0.f; }
    Food.Reset();
    EatingStarted = -1.f;
    Lure = Position;
    DistractedUntil = GetWorld()->GetTimeSeconds()+Seconds;
    NextDecision = 0.f;
    bChasing = false;
}
void ABDWildMan::AttractBanana(ABDBomb* Banana)
{
    if (IsEating() || !IsValid(Banana) || !Banana->IsRestingBanana() || Food.Get()==Banana) return;
    Food = Banana;
    EatingStarted = -1.f;
    NextDecision = 0.f;
    bChasing = false;
}
void ABDWildMan::ReturnHome()
{
    // A player capture must not cancel the eating window or orphan carried food.
    if (IsEating()) return;
    if (MealLifeSpan>0.f) { SetLifeSpan(MealLifeSpan); MealLifeSpan=0.f; }
    Food.Reset();
    EatingStarted = -1.f;
    bChasing = false;
    DistractedUntil = 0.f;
    if (auto* AI = Cast<AAIController>(GetController())) AI->MoveToLocation(Home,40.f);
}
void ABDWildMan::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>();
    auto* Player = Cast<ABDJungleCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    auto* AI = Cast<AAIController>(GetController());
    if (!Mode || !Mode->Settings || !Player || !AI) return;
    if (!Mode->IsGameplayInputAllowed() || !JungleIsPlaying(GetWorld())) { AI->StopMovement(); return; }
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < NextDecision) return;
    NextDecision = Now+.2f;
    if (IsEating())
    {
        AI->StopMovement();
        GetCharacterMovement()->StopMovementImmediately();
        bChasing = false;
        if (GetEatingRemaining()<=0.f)
        {
            UE_LOG(LogTemp,Display,TEXT("JUNGLE MEAL FINISHED: keeper=%d"),bGateOpener);
            Food->Destroy();
            Attract(Lure,Mode->Settings->DistractionSeconds);
        }
        return;
    }
    if (EatingStarted>=0.f) Attract(GetActorLocation(),0.f);
    // Another wild man may have claimed the same banana on this frame.
    if (Food.IsValid() && Food->IsBeingEaten()) Food.Reset();
    if (ABDBomb* Banana = Food.Get())
    {
        Lure = Banana->GetActorLocation();
        FHitResult FoodHit;
        FCollisionQueryParams FoodSight(SCENE_QUERY_STAT(JungleFood),false,this);
        FoodSight.AddIgnoredActor(Banana);
        const float Reach = GetCapsuleComponent()->GetScaledCapsuleRadius()+Banana->GetWorldRadius()+15.f;
        const bool bCanEat = Banana->IsRestingBanana() && FVector::Dist2D(GetActorLocation(),Lure)<FMath::Max(85.f,Reach) &&
            FMath::Abs(GetActorLocation().Z-Lure.Z)<GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+Banana->GetWorldRadius() &&
            !GetWorld()->LineTraceSingleByChannel(FoodHit,GetActorLocation(),Lure,ECC_Visibility,FoodSight);
        if (bCanEat && Banana->PickUpBanana(this))
        {
            AI->StopMovement();
            GetCharacterMovement()->StopMovementImmediately();
            bChasing = false;
            SetActorRotation((Lure-GetActorLocation()).GetSafeNormal2D().Rotation());
            EatingStarted = Now;
            MealLifeSpan = GetLifeSpan();
            if (MealLifeSpan>0.f) SetLifeSpan(0.f);
            Mode->OnBananaPickedUp(this);
            return;
        }
    }
    EatingStarted = -1.f;
    const bool bLured = IsDistracted();
    const float Distance = FVector::Dist2D(GetActorLocation(),Player->GetActorLocation());
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(JungleSight),false,this);
    Params.AddIgnoredActor(Player);
    const bool bVisible = !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Player->GetActorLocation(),ECC_Visibility,Params);
    bChasing = !bLured && Mode->CanCatchPlayer() && bVisible && Distance<Mode->Settings->DetectionRange;
    const float CatchRange = GetCapsuleComponent()->GetScaledCapsuleRadius()+Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+10.f;
    if (bChasing && Distance<CatchRange && FMath::Abs(Player->GetActorLocation().Z-GetActorLocation().Z)<
        FMath::Max(Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))
    { Mode->PlayerCaught(); return; }
    FVector Target = Home;
    if (bLured) Target = Lure;
    else if (bChasing) Target = Player->GetActorLocation();
    else if (!bPassageGuard) Target += FVector(FMath::Sin(Now*.35f)*PatrolRadius,0,0);
    bool bApproachingGate = Mode->WantsGateAccess(this,Food.Get());
    if (bApproachingGate)
    {
        // The closed gate has no path through it. Approach its reachable south
        // side first; opening updates the navmesh before pursuing the actual food.
        Target = Mode->GetGateApproach();
        if (Mode->TryOpenGate(this,Food.Get())) { Target = Lure; bApproachingGate = false; }
    }
    GetCharacterMovement()->MaxWalkSpeed = bLured ? LureSpeed : ChaseSpeed;
    // Unreal's navigation handles the ruin corners. Partial routes are rejected.
    // Gate interaction measures centre distance, so do not add capsule radius
    // to its arrival tolerance (also remains valid after console size changes).
    AI->MoveToLocation(Target,bApproachingGate ? 20.f : 35.f,!bApproachingGate,true,false,true,nullptr,false);
    if (GetVelocity().Size2D()>5) SetActorRotation(GetVelocity().GetSafeNormal2D().Rotation());
    if (GetActorLocation().Z < -300) SetActorLocation(Home,false,nullptr,ETeleportType::TeleportPhysics);
}

ABDJungleGameMode::ABDJungleGameMode()
{
    DefaultPawnClass = ABDJungleCharacter::StaticClass();
    HUDClass = ABDJungleHUD::StaticClass();
}
void ABDJungleGameMode::BeginPlay()
{
    Super::BeginPlay();
    BDAlphaLook::ConfigurePlaygroundLighting(GetWorld());
    for (TActorIterator<ABDJungleSettings> It(GetWorld()); It; ++It) { Settings=*It; break; }
    if (!Settings) Settings=GetWorld()->SpawnActor<ABDJungleSettings>();
    for (TActorIterator<ABDGate> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("JunglePassage"))) { Passage=*It; break; }
    if (Crate) CrateCheckpoint = Crate->GetActorTransform();
    Checkpoints = { FVector(-400,840,53), FVector(270,730,53), FVector(270,-260,53), FVector(600,-260,53), FVector(600,-730,53) };
    GraceUntil = GetWorld()->GetTimeSeconds()+4.f;
    NextSpawn = GetWorld()->GetTimeSeconds()+Settings->SpawnIntervalMin;
    StatusMessage = TEXT("Use the left peels to reach Zone A. Feed the upper wild man, then lure the lower-right gatekeeper.");
}
ABDBananaZone* ABDJungleGameMode::ZoneAt(const FVector& Point) const
{
    for (TActorIterator<ABDBananaZone> It(GetWorld()); It; ++It) if (It->Contains(Point)) return *It;
    return nullptr;
}
void ABDJungleGameMode::PrepareBomb(ABDBomb* Bomb, ABDCharacter* Holder)
{
    // The migrated bomb uses the shared Chaos body and continuous collision.
    if (Bomb && Holder && ZoneAt(Holder->GetActorLocation())) Bomb->MakeBanana();
}
void ABDJungleGameMode::ResolveBombExplosion(const FVector& Position, ABDCharacter* Holder, ABDBomb* Bomb)
{
    if (Bomb && !Bomb->IsBanana()) Super::ResolveBombExplosion(Position,Holder,Bomb);
}
void ABDJungleGameMode::OnBananaPickedUp(ABDWildMan* Eater)
{
    UE_LOG(LogTemp,Display,TEXT("JUNGLE BANANA PICKUP: keeper=%d position=%s"),Eater->bGateOpener,*Eater->GetActorLocation().ToString());
    if (bPassageCleared) return;
    if (Eater->bPassageGuard)
        StatusMessage = TEXT("Upper wild man is eating! From Zone B, throw the next banana just ABOVE the brown gate.");
    else if (Eater->bGateOpener)
        StatusMessage = TEXT("Gatekeeper is eating. Next time, put the banana on the far side, just ABOVE the brown gate.");
}
bool ABDJungleGameMode::WantsGateAccess(const ABDWildMan* Wild, const ABDBomb* Banana) const
{
    if (bCompleted || bPassageCleared || !Passage || Passage->bOpen || !Settings || !Wild ||
        !Wild->bGateOpener || Wild->IsEating() || !IsValid(Banana) || !Banana->IsRestingBanana()) return false;
    const FVector Local = Passage->GetActorTransform().InverseTransformPosition(Banana->GetActorLocation());
    return Local.Y < -Passage->Size.Y*.5f-20.f && FMath::Abs(Local.X)<=Passage->Size.X*.5f &&
        Local.Size2D()<=Settings->GateLureRange && FMath::Abs(Local.Z)<120.f;
}
FVector ABDJungleGameMode::GetGateApproach() const
{
    return Passage ? Passage->GetActorTransform().TransformPosition(FVector(0,Passage->Size.Y*.5f+80.f,66.f)) : FVector::ZeroVector;
}
bool ABDJungleGameMode::TryOpenGate(ABDWildMan* Wild, ABDBomb* Banana)
{
    if (!WantsGateAccess(Wild,Banana) || FVector::Dist2D(Wild->GetActorLocation(),GetGateApproach())>75.f) return false;
    const FVector Local = Passage->GetActorTransform().InverseTransformPosition(Wild->GetActorLocation());
    if (Local.Y<=Passage->Size.Y*.5f) return false;
    bPassageCleared = true;
    Passage->Open();
    StatusMessage = TEXT("The lower-right gatekeeper opened the gate for his banana! The crate route stays open.");
    UE_LOG(LogTemp,Display,TEXT("JUNGLE GATE OPENED: keeper=%s banana=%s"),*Wild->GetActorLocation().ToString(),*Banana->GetActorLocation().ToString());
    return true;
}
void ABDJungleGameMode::OnSlideEnded(ABDJungleCharacter* Player)
{
    if (auto* Zone = ZoneAt(Player->GetActorLocation()); Zone && Zone->ZoneId==TEXT("A"))
    {
        bReachedZoneBySlide = true;
        StatusMessage = TEXT("Throw RIGHT to distract the upper wild man. Once he picks it up, take a second banana to the gate.");
    }
}
bool ABDJungleGameMode::CanCatchPlayer() const { return !bCompleted && GetWorld()->GetTimeSeconds()>=GraceUntil; }
void ABDJungleGameMode::OnPlayerRespawned()
{
    GraceUntil = GetWorld()->GetTimeSeconds()+4.f;
    for (TActorIterator<ABDWildMan> It(GetWorld()); It; ++It) It->ReturnHome();
    if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->ResetBombInput();
}
void ABDJungleGameMode::PlayerCaught()
{
    if (!CanCatchPlayer()) return;
    ++Captures;
    if (auto* Player = Cast<ABDJungleCharacter>(UGameplayStatics::GetPlayerPawn(this,0))) Player->Respawn();
    StatusMessage = TEXT("Caught! Crate progress kept. You have four seconds of safe recovery.");
}
int32 ABDJungleGameMode::CountWildMen() const
{
    int32 Count=0; for (TActorIterator<ABDWildMan> It(GetWorld()); It; ++It) ++Count; return Count;
}
void ABDJungleGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bViewSet)
        if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
        { PC->SetPlaygroundView(FVector(0,0,40),2800.f,2460.f); bViewSet=true; }
    if (bCompleted || !Settings || !JungleIsPlaying(GetWorld())) return;
    if (IsValid(ActiveBomb) && ActiveBomb->IsHeld()) PrepareBomb(ActiveBomb,ActiveBomb->GetHolder());
    if (IsValid(ActiveBomb) && ActiveBomb->IsRestingBanana())
    {
        const FVector Position = ActiveBomb->GetActorLocation();
        for (TActorIterator<ABDWildMan> It(GetWorld()); It; ++It)
            if (FVector::Dist2D(Position,It->GetActorLocation())<=Settings->BananaAttractionRange)
                It->AttractBanana(ActiveBomb);
    }
    if (Crate && Checkpoints.IsValidIndex(CheckpointIndex+1) && Crate->Mesh->GetPhysicsLinearVelocity().Size2D()<15.f &&
        FVector::Dist2D(Crate->GetActorLocation(),Checkpoints[CheckpointIndex+1])<200.f)
    { ++CheckpointIndex; CrateCheckpoint=Crate->GetActorTransform(); }
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now<NextSpawn) return;
    NextSpawn = Now+FMath::FRandRange(FMath::Max(1.f,Settings->SpawnIntervalMin),FMath::Max(Settings->SpawnIntervalMin,Settings->SpawnIntervalMax));
    if (!bPassageCleared || CountWildMen()>=Settings->MaxWildMen || Settings->SpawnPoints.IsEmpty()) return;
    const auto* Player = UGameplayStatics::GetPlayerPawn(this,0);
    const FVector Point = Settings->SpawnPoints[FMath::RandRange(0,Settings->SpawnPoints.Num()-1)];
    if ((Player && FVector::Dist2D(Point,Player->GetActorLocation())<550.f) || (Crate && FVector::Dist2D(Point,Crate->GetActorLocation())<450.f)) return;
    FNavLocation Nav;
    auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Navigation || !Navigation->ProjectPointToNavigation(Point,Nav,FVector(100,100,200))) return;
    const float HalfHeight = GetDefault<ABDWildMan>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    auto* Wild = GetWorld()->SpawnActor<ABDWildMan>(Nav.Location+FVector(0,0,HalfHeight+2.f),FRotator::ZeroRotator);
    if (Wild) Wild->SetLifeSpan(50.f);
}
void ABDJungleGameMode::RestartTutorial()
{
    if (bCompleted) { Super::RestartTutorial(); return; }
    UGameplayStatics::SetGamePaused(this,false);
    // Carried meals have already left ActiveBomb, but retry must clear them too.
    for (TActorIterator<ABDBomb> It(GetWorld()); It; ++It) It->Destroy();
    if (Crate)
    {
        Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Crate->Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        Crate->SetActorTransform(CrateCheckpoint,false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (auto* Player = Cast<ABDJungleCharacter>(UGameplayStatics::GetPlayerPawn(this,0))) Player->Respawn();
    StatusMessage = TEXT("Retry from the last safe crate corner. The opened passage stays open.");
}

void ABDJungleHUD::DrawHUD()
{
    Super::DrawHUD();
    // Alpha supplies the shared minimal HUD, pause, settings and results layers.
    if (GetWorld()->GetGameInstance<UBDAlphaSession>()) return;
    auto* Mode = GetWorld()->GetAuthGameMode<ABDJungleGameMode>();
    if (!Canvas || !Mode) return;
    const float S=FMath::Clamp(Canvas->SizeY/900.f,.6f,1.5f);
    const FLinearColor Panel(.025f,.07f,.045f,.94f), Ink(.96f,.98f,.87f), Yellow(1,.83f,.14f);
    auto Text=[&](const FString& T,float X,float Y,float Scale,const FLinearColor& C)
    { DrawText(T,C,X,Y,GEngine->GetMediumFont(),Scale*S); };
    DrawRect(Panel,0,0,Canvas->SizeX,78*S);
    Text(TEXT("LEVEL 3   JUNGLE / BANANA TRAP"),20*S,10*S,.95f,Yellow);
    Text(Mode->StatusMessage,20*S,43*S,.62f,Ink);
    const auto* Player=Cast<ABDJungleCharacter>(GetOwningPlayerController()->GetPawn());
    const auto* Zone=Player?Mode->ZoneAt(Player->GetActorLocation()):nullptr;
    DrawRect(Panel,0,Canvas->SizeY-74*S,Canvas->SizeX,74*S);
    FString State=Zone?FString::Printf(TEXT("ZONE %s: BANANA ONLY  /  scent distracts, no crate force"),*Zone->ZoneId.ToString()):TEXT("REGULAR BOMBS  /  explosions move the crate");
    if (Player && Player->IsSliding()) State=TEXT("SLIDING  /  direction locked until five tiles or a wall; then movement returns");
    if (Mode->ActiveBomb)
        State+=Mode->ActiveBomb->IsBanana() ? (Mode->ActiveBomb->IsHeld()?TEXT("    BANANA HELD"):TEXT("    FREE BANANA: WAIT FOR PICKUP")) :
            FString::Printf(TEXT("    REGULAR FUSE %.1fs"),Mode->ActiveBomb->GetRemainingFuse());
    else State+=TEXT("    BOMB SLOT READY");
    Text(State,20*S,Canvas->SizeY-65*S,.67f,Yellow);
    Text(TEXT("WASD move   Mouse aim   Hold LMB charge / release throw   R retry checkpoint   Esc pause"),20*S,Canvas->SizeY-34*S,.61f,Ink);
    auto Label=[&](FVector P,const FString& T,const FLinearColor& C)
    {
        FVector2D Screen;
        if (GetOwningPlayerController()->ProjectWorldLocationToScreen(P,Screen))
        {
            float W,H; GetTextSize(T,W,H,GEngine->GetSmallFont(),.75f*S);
            DrawText(T,C,Screen.X-W*.5f,Screen.Y,GEngine->GetSmallFont(),.75f*S);
        }
    };
    for (TActorIterator<ABDWildMan> It(GetWorld()); It; ++It)
        Label(It->GetActorLocation()+FVector(0,0,85),It->IsEating()?FString::Printf(TEXT("EATING %.0fs"),It->GetEatingRemaining()):
            It->IsDistracted()?TEXT("BANANA!"):It->IsChasing()?TEXT("!"):It->bGateOpener?TEXT("GATEKEEPER"):TEXT("..."),Yellow);
    if (Mode->bCompleted || UGameplayStatics::IsGamePaused(this))
    {
        DrawRect(FLinearColor(0,0,0,.6f),0,0,Canvas->SizeX,Canvas->SizeY);
        Text(Mode->bCompleted?TEXT("JUNGLE DELIVERY COMPLETE!"):TEXT("PAUSED"),Canvas->SizeX*.5f-200*S,Canvas->SizeY*.5f-35*S,1.2f,Yellow);
        Text(Mode->bCompleted?TEXT("Crate delivered safely. R to play again."):TEXT("Esc resumes. R retries the last safe crate corner."),Canvas->SizeX*.5f-200*S,Canvas->SizeY*.5f+15*S,.72f,Ink);
    }
}
