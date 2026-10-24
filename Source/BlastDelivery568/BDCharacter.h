#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BDPhysicsSettings.h"
#include "BDCharacter.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class BLASTDELIVERY568_API ABDCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ABDCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void MoveInRoom(const FVector2D& Input);
    virtual void FacePoint(const FVector& Point);
    void DrawBomb();
    void DropBomb();
    void ThrowBomb();
    virtual void Respawn();
    bool ApplySettings(const FBDPhysicsSettings& Settings);
    void ApplyBlastVelocity(const FVector& VelocityChange);
    bool IsRecovering() const { return bRecovering || !bLocalConnected; }
    void SetLocalPlayerSeat(int32 Index);
    void SetLocalPlayerConnected(bool bConnected,const FVector& SpawnPosition);
    int32 GetLocalPlayerSeat() const { return LocalPlayerSeat; }
    FVector GetBombPosition() const;
    void SetRespawnCheckpoint(FVector WorldPosition);
    FVector GetSpawnPosition() const { return SpawnTransform.GetLocation(); }
    float GetSpawnFloorHeight() const { return SpawnFloorZ; }
    UPROPERTY(EditAnywhere, Category="Movement") float WalkSpeed = 520.f;
protected:
    USceneComponent* GetPresentationRoot() const { return VisualRoot; }
    UStaticMeshComponent* GetLeftHand() const { return LeftHand; }
    UStaticMeshComponent* GetRightHand() const { return RightHand; }
private:
    UPROPERTY(Transient) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Eyes;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> LightParts;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftFoot;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightFoot;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftHand;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightHand;
    FTransform SpawnTransform;
    FBDPhysicsSettings Profile;
    FTimerHandle RespawnTimer;
    bool bRecovering = false;
    bool bLocalConnected = true;
    int32 LocalPlayerSeat = 0;
    float SpawnFloorZ = 0.f;
    FVector2D MoveIntent = FVector2D::ZeroVector;
    float WalkPhase = 0.f;
    float LandingPose = 0.f;
    float ThrowPose = 0.f;
    bool bWasAirborne = false;
    bool bWasHolding = false;
    FQuat VisualFacing = FQuat::Identity;
    void FinishRespawn();
    void UpdatePresentation(float DeltaSeconds);
    void ResetPresentation();
    UStaticMeshComponent* AddPart(const FName Name, const FVector& Location, const FVector& Scale,
        bool bEye = false, bool bLight = false, USceneComponent* Parent = nullptr, const TCHAR* Shape = TEXT("Sphere"));
    USceneComponent* AddSoftCapsule(const FName Name, const FVector& Location, const FVector& UnityScale);
};
