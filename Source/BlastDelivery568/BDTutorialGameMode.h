#pragma once

#include "CoreMinimal.h"
#include "BDGameMode.h"
#include "BDTutorialGameMode.generated.h"

class AStaticMeshActor;
class USoundWaveProcedural;

UENUM(BlueprintType)
enum class EBDTutorialScene : uint8 { Throw, MoveBox, PutDown, CrossGap, Deliver, ChargeCorridor };

UENUM(BlueprintType)
enum class EBDTutorialPhase : uint8 { Playing, Success, Sliding, Finished };

// A scene owns every playable actor it creates, including its real pawn.
USTRUCT()
struct FBDTutorialStage
{
    GENERATED_BODY()
    UPROPERTY() TArray<TObjectPtr<AActor>> Actors;
    UPROPERTY() TObjectPtr<ABDCharacter> Player;
    UPROPERTY() TObjectPtr<ABDCrate> Box;
    UPROPERTY() TObjectPtr<ABDGoal> DeliveryGoal;
    UPROPERTY() TObjectPtr<AStaticMeshActor> LandingGround;
    FVector Origin = FVector::ZeroVector;
};

UCLASS(Config=Game)
class BLASTDELIVERY568_API ABDTutorialGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDTutorialGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsGameplayInputAllowed() const override { return Phase == EBDTutorialPhase::Playing && !bResetRequested; }
    virtual void RestartTutorial() override;
    virtual void OnBombReleased(ABDBomb* Bomb, bool bThrown) override;
    virtual void OnBombExploded(ABDBomb* Bomb) override;
    virtual void OnCrateBlasted(ABDCrate* BlastedCrate) override;
    virtual void OnPlayerBlasted(ABDCharacter* Player) override;
    virtual bool RequestSceneReset() override;
    virtual FVector GetHeldBlastVelocity(const ABDCharacter* Player) const override;

    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Sequence") TArray<EBDTutorialScene> SceneOrder;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Timing", meta=(ClampMin="0.0")) float SuccessDelay = .5f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Timing", meta=(ClampMin="0.25", ClampMax="0.5")) float SlideDuration = .35f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Success") float BoxMoveDistance = 20.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Layout") FVector GoalExtent = FVector(360,340,100);
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Layout") FVector GoalCenter = FVector(1000,0,100);
    // Forgiving first-platform jumps follow the rightward arc in the wordless prompt.
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Layout") bool bAssistGapJump = true;
    UPROPERTY(BlueprintReadOnly, Category="Tutorial") EBDTutorialPhase Phase = EBDTutorialPhase::Playing;
    UPROPERTY(BlueprintReadOnly, Category="Tutorial") int32 SceneIndex = 0;

    EBDTutorialScene GetScene() const;
    EBDTutorialScene GetNextScene() const;
    float GetPhaseTime() const { return PhaseTime; }
    float GetSlideAlpha() const;
    FVector GetSceneOrigin() const { return Current.Origin; }
    AActor* GetLandingGround() const;
    bool IsChargeDoodleVisible() const { return bChargeDoodleVisible; }
    bool IsGoalDoodleVisible() const { return bGoalDoodleVisible; }
    FVector GetCorridorDoodlePosition(int32 Index) const;
    float GetCorridorEnd() const { return CorridorEnd; }
private:
    bool bChargeDoodleVisible = false;
    bool bGoalDoodleVisible = false;
    float CorridorBoxX = 900.f;
    float CorridorEnd = 2400.f;
    void UpdateCorridorView();
    UPROPERTY() FBDTutorialStage Current;
    UPROPERTY() FBDTutorialStage Incoming;
    UPROPERTY() TObjectPtr<USoundWaveProcedural> SuccessChime;
    TWeakObjectPtr<ABDBomb> PlacedBomb;
    TWeakObjectPtr<ABDBomb> SuccessBomb;
    FVector BoxStart = FVector::ZeroVector;
    FVector BoxAtBlast = FVector::ZeroVector;
    float PhaseTime = 0.f;
    float SuccessDelayStartedAt = 0.f;
    float SlideWidth = 0.f;
    float BlastWatchTime = 0.f;
    float DeliverySettledFor = 0.f;
    bool bBoxBlasted = false;
    bool bPlayerBlasted = false;
    bool bWasAirborne = false;
    bool bResetRequested = false;
    bool bStarted = false;
    bool bWaitingForSuccessBomb = false;

    void BuildStage(FBDTutorialStage& Stage, EBDTutorialScene Scene, const FVector& Origin);
    void DestroyStage(FBDTutorialStage& Stage);
    void FreezeStage(FBDTutorialStage& Stage);
    void ActivateStage();
    void ClearTransientActors();
    void MarkSuccess(ABDBomb* BombToObserve = nullptr);
    void StartSlide();
    void PlaySuccessChime();
    void SetView(const FVector& Origin);
};
