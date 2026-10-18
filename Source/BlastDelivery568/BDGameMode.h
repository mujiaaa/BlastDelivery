#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BDPhysicsSettings.h"
#include "BDGameMode.generated.h"

class ABDBomb;
class ABDCharacter;
class ABDCrate;
class ABDGoal;
class ABDGate;

UCLASS(Config=Game)
class BLASTDELIVERY568_API ABDGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABDGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditDefaultsOnly, Config, Category="Bomb") float FuseSeconds = 4.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Bomb") float BlastRadius = 360.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Bomb") float CrateImpulse = 34000.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Bomb") float ThrowSpeed = 520.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Bomb") float ThrowLift = 270.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Jump") float JumpHorizontalSpeed = 850.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Jump") float JumpVerticalSpeed = 650.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Goal") float SettleSeconds = 1.f;

    UPROPERTY(BlueprintReadOnly, Category="Run") int32 BombsUsed = 0;
    UPROPERTY(BlueprintReadOnly, Category="Run") float ElapsedSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Run") bool bCompleted = false;
    UPROPERTY(BlueprintReadOnly, Category="Run") TObjectPtr<ABDCrate> Crate;
    UPROPERTY(BlueprintReadOnly, Category="Run") TObjectPtr<ABDGoal> Goal;
    UPROPERTY(BlueprintReadOnly, Category="Run") TObjectPtr<ABDGate> Gate;
    UPROPERTY(BlueprintReadOnly, Category="Run") TObjectPtr<ABDBomb> ActiveBomb;

    ABDBomb* DrawBomb(ABDCharacter* Holder);
    ABDBomb* FindNearbyBomb(ABDCharacter* Player) const;
    ABDBomb* GetBombForPlayer(const ABDCharacter* Player) const;
    int32 GetOwnedBombCount(const ABDCharacter* Player) const;
    void AssignBomb(ABDBomb* Bomb,ABDCharacter* Player);
    const FBDPhysicsSettings& GetPhysicsSettings() const { return PhysicsSettings; }
    bool ApplyPhysicsSettings(const FBDPhysicsSettings& Settings);
    bool IsPlayground() const { return bAlphaPlayground; }
    float GetGoalProgress() const { return FMath::Clamp(SettledFor/FMath::Max(.01f,SettleSeconds),0.f,1.f); }
    bool bAlphaPlayground = false;
    void ClearBomb(ABDBomb* Bomb);
    void ExplodeAt(const FVector& Position, ABDCharacter* Holder, AActor* Bomb);
    virtual void RestartTutorial();
    void CompleteTutorial();
    virtual bool IsGameplayInputAllowed() const { return !bCompleted; }
    virtual void OnBombReleased(ABDBomb* Bomb, bool bThrown) {}
    virtual void OnBombExploded(ABDBomb* Bomb) {}
    virtual void OnCrateBlasted(ABDCrate* BlastedCrate) {}
    virtual void OnPlayerBlasted(ABDCharacter* Player) {}
    virtual bool RequestSceneReset() { return false; }
    virtual FVector GetHeldBlastVelocity(const ABDCharacter* Player) const;
    static float BlastFalloff(float Distance, float Radius);
    bool HasBlastLineOfSight(const FVector& From, AActor* Target, AActor* Bomb) const;
private:
    UPROPERTY() FBDPhysicsSettings PhysicsSettings;
    UPROPERTY() TMap<TObjectPtr<ABDBomb>,TObjectPtr<ABDCharacter>> BombOwners;
    UPROPERTY() TMap<TObjectPtr<ABDCharacter>,TObjectPtr<ABDBomb>> PlayerBombs;
    void RefreshPrimaryBomb();
    void SyncLegacyParameters();
    float SettledFor = 0.f;
};
