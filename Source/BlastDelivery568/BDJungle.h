#pragma once

#include "CoreMinimal.h"
#include "BDCharacter.h"
#include "BDGameMode.h"
#include "GameFramework/HUD.h"
#include "BDJungle.generated.h"

class UBoxComponent;
class ABDGate;
class ABDWildMan;
class AAIController;

// The map owns its tuning; tutorial defaults are never modified.
UCLASS()
class BLASTDELIVERY568_API ABDJungleSettings : public AActor
{
    GENERATED_BODY()
public:
    ABDJungleSettings();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Jungle") void BuildNavigation();
    UPROPERTY(EditAnywhere, Category="Jungle|Slide") float TileSize = 160.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Slide") int32 SlideTiles = 5;
    UPROPERTY(EditAnywhere, Category="Jungle|Slide") float SlideSpeed = 850.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") float DetectionRange = 440.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") float DistractionSeconds = 20.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men", meta=(ClampMin="1.0")) float EatingSeconds = 12.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") float BananaAttractionRange = 2400.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") float SpawnIntervalMin = 32.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") float SpawnIntervalMax = 48.f;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") int32 MaxWildMen = 1;
    UPROPERTY(EditAnywhere, Category="Jungle|Wild men") TArray<FVector> SpawnPoints;
    UPROPERTY(EditAnywhere, Category="Jungle|Puzzle") FVector ClearingCenter = FVector(-400,-760,0);
    UPROPERTY(EditAnywhere, Category="Jungle|Puzzle") float GateLureRange = 400.f;
};

UCLASS()
class BLASTDELIVERY568_API ABDJungleCharacter : public ABDCharacter
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual void MoveInRoom(const FVector2D& Input) override;
    virtual void FacePoint(const FVector& Point) override;
    virtual void Respawn() override;
    bool StartSlide(float Distance, float Speed);
    bool IsSliding() const { return SlipTime > 0.f; }
    void EndSlide();
    float GetSlideRemaining() const { return SlideRemaining; }
private:
    FVector SlideDirection = FVector::ZeroVector;
    float SlideRemaining = 0.f;
    float SlideSpeed = 850.f;
    float SlipTime = 0.f;
};

UCLASS()
class BLASTDELIVERY568_API ABDBananaPeel : public AActor
{
    GENERATED_BODY()
public:
    ABDBananaPeel();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, Category="Peel") bool bHelpful = true;
    UPROPERTY(EditAnywhere, Category="Peel") FVector2D TriggerExtent = FVector2D(80,32);
    bool IsConsumed() const { return bConsumed; }
    void ResetPeel();
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
    bool bOccupied = false;
    bool bConsumed = false;
};

UCLASS()
class BLASTDELIVERY568_API ABDBananaZone : public AActor
{
    GENERATED_BODY()
public:
    ABDBananaZone();
    virtual void OnConstruction(const FTransform& Transform) override;
    bool Contains(const FVector& Point) const;
    UPROPERTY(EditAnywhere, Category="Zone") FName ZoneId = TEXT("A");
    UPROPERTY(EditAnywhere, Category="Zone") FVector Extent = FVector(180,190,240);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Region;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Borders;
};

UCLASS()
class BLASTDELIVERY568_API ABDWildMan : public ABDCharacter
{
    GENERATED_BODY()
public:
    ABDWildMan();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    void Attract(const FVector& Position, float Seconds);
    void AttractBanana(ABDBomb* Banana);
    void ReturnHome();
    bool IsDistracted() const;
    bool IsEating() const;
    float GetEatingRemaining() const;
    bool IsChasing() const { return bChasing; }
    bool IsSmelling() const { return ScentTime > 0.f; }
    void ResetPuzzle();
    UPROPERTY(EditAnywhere, Category="Wild man") bool bPassageGuard = false;
    UPROPERTY(EditAnywhere, Category="Wild man") bool bGateOpener = false;
    UPROPERTY(EditAnywhere, Category="Wild man") float PatrolRadius = 320.f;
    UPROPERTY(EditAnywhere, Category="Wild man") float PatrolSpeed = 145.f;
    UPROPERTY(EditAnywhere, Category="Wild man") float ChaseSpeed = 210.f;
    UPROPERTY(EditAnywhere, Category="Wild man") float LureSpeed = 280.f;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Hair;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> KeyParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Scent;
    float ScentTime = 0.f;
    FVector Home, Lure;
    float DistractedUntil = 0.f;
    float NextDecision = 0.f;
    FVector PatrolTarget = FVector::ZeroVector;
    bool bHasPatrolTarget = false;
    float PatrolWaitUntil = 0.f;
    void Patrol(AAIController* AI, float Now);
    TWeakObjectPtr<ABDBomb> Food;
    float EatingStarted = -1.f;
    float MealLifeSpan = 0.f;
    bool bChasing = false;
};

UCLASS()
class BLASTDELIVERY568_API ABDJungleGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDJungleGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PrepareBomb(ABDBomb* Bomb, ABDCharacter* Holder) override;
    virtual void ResolveBombExplosion(const FVector& Position, ABDCharacter* Holder, ABDBomb* Bomb) override;
    void OnBananaPickedUp(ABDWildMan* Eater);
    bool WantsGateAccess(const ABDWildMan* Wild, const ABDBomb* Banana) const;
    FVector GetGateApproach() const;
    bool TryOpenGate(ABDWildMan* Wild, ABDBomb* Banana);
    virtual void RestartTutorial() override;
    ABDBananaZone* ZoneAt(const FVector& Point) const;
    void OnSlideEnded(ABDJungleCharacter* Player);
    void PlayerCaught();
    void OnPlayerRespawned();
    bool CanCatchPlayer() const;
    int32 CountWildMen() const;
    UPROPERTY() TObjectPtr<ABDJungleSettings> Settings;
    UPROPERTY() TObjectPtr<ABDGate> Passage;
    UPROPERTY(BlueprintReadOnly, Category="Jungle") bool bReachedZoneBySlide = false;
    UPROPERTY(BlueprintReadOnly, Category="Jungle") bool bPassageCleared = false;
    UPROPERTY(BlueprintReadOnly, Category="Jungle") int32 Captures = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jungle") int32 CheckpointIndex = 0;
    FString StatusMessage;
private:
    float GraceUntil = 0.f;
    float NextSpawn = 0.f;
    bool bViewSet = false;
    FTransform CrateCheckpoint;
    TArray<FVector> Checkpoints;
};

UCLASS()
class BLASTDELIVERY568_API ABDJungleHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
