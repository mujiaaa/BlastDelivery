#pragma once

#include "CoreMinimal.h"
#include "BDGameMode.h"
#include "GameFramework/Actor.h"
#include "BDMirrorLab.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USceneComponent;
class AStaticMeshActor;

// Glass is a real solid wall for gameplay. Only the optical path explicitly
// ignores it; visibility queries (including explosion shielding) still hit it.
UCLASS()
class BLASTDELIVERY568_API ABDBulletproofGlass : public AActor
{
    GENERATED_BODY()
public:
    ABDBulletproofGlass();
    void Configure(float Length=300.f);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Collision;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
};

UCLASS()
class BLASTDELIVERY568_API ABDRotatingLaserMirror : public AActor
{
    GENERATED_BODY()
public:
    ABDRotatingLaserMirror();
    virtual void Tick(float DeltaSeconds) override;
    void Initialize(float InitialYawDegrees=45.f);
    bool RequestQuarterTurn();
    bool ReceiveExplosion(const FVector& Origin,float Radius,uint64 ExplosionId,AActor* Source=nullptr);
    bool TryIntersectRay(const FVector& Start,const FVector& Direction,float MaxDistance,
        float& Distance,FVector& Point,FVector& Normal) const;
    float GetCurrentYaw() const { return SettledYaw; }
    bool IsTurning() const { return bTurning; }
    int32 GetActivationCount() const { return ActivationCount; }
    int32 GetAcceptedBlastCount() const { return AcceptedBlasts.Num(); }
    int32 GetPendingTurns() const { return PendingTurns; }
    FVector GetSwitchPosition() const { return GetActorLocation()+FVector(0,0,25); }
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Rotor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Base;
private:
    void BeginTurn();
    void SetVisualYaw(float Yaw);
    TSet<uint64> AcceptedBlasts;
    float SettledYaw=45.f;
    float TargetYaw=45.f;
    float TurnElapsed=0.f;
    float Flash=0.f;
    bool bTurning=false;
    int32 ActivationCount=0;
    int32 PendingTurns=0;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Switch;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
};

UCLASS()
class BLASTDELIVERY568_API ABDMirrorLaser : public AActor
{
    GENERATED_BODY()
public:
    ABDMirrorLaser();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(const FVector& InDirection);
    void RefreshPath();
    FVector GetBeamDirection() const { return Direction; }
    const TArray<FVector>& GetBeamPoints() const { return BeamPoints; }
    int32 GetBombsDestroyed() const { return BombsDestroyed; }
    int32 GetPlayerHits() const { return PlayerHits; }
    static float SegmentDistanceSquared(const FVector& A,const FVector& B,const FVector& C,const FVector& D);
    bool IntersectsBeam(const FVector& From,const FVector& To,float Radius) const;
    bool IntersectsEmitterRay(const FVector& Origin,const FVector& RayDirection,double& Distance) const;
    void RelocateEmitter(const FVector& Location,const FVector& NewDirection);
    void ResetContactHistory();
    UStaticMeshComponent* GetEmitterHousing() const { return EmitterHousing; }
protected:
    void SetBeamDirection(const FVector& InDirection) { Direction=InDirection.GetSafeNormal(); }
    virtual bool IsEmissionEnabled() const { return true; }
private:
    void CheckContacts();
    FVector Direction=FVector::ForwardVector;
    TArray<FVector> BeamPoints;
    TMap<TWeakObjectPtr<ABDBomb>,FVector> PreviousBombs;
    struct FCapsuleHistory { FVector Lower; FVector Upper; };
    TMap<TWeakObjectPtr<ABDCharacter>,FCapsuleHistory> PreviousPlayers;
    int32 BombsDestroyed=0;
    int32 PlayerHits=0;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BeamMeshes;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> EmitterHousing;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> EmitterAperture;
};

// The central source shares the same optical/damage rules as the edge sources.
// Its base accepts explosions, not contact/weight, and its beam is off while
// the visible housing turns so rotation never sweeps a lethal beam over players.
UCLASS()
class BLASTDELIVERY568_API ABDRotatingLaserEmitter : public ABDMirrorLaser
{
    GENERATED_BODY()
public:
    ABDRotatingLaserEmitter();
    void Initialize(const FVector& InitialDirection=FVector(0,1,0));
    bool RequestQuarterTurn();
    virtual void Tick(float DeltaSeconds) override;
    bool ReceiveExplosion(const FVector& Origin,float Radius,uint64 ExplosionId,AActor* Source=nullptr);
    bool IsTurning() const { return bTurning; }
    float GetCurrentYaw() const { return SettledYaw; }
    int32 GetActivationCount() const { return ActivationCount; }
    int32 GetAcceptedBlastCount() const { return AcceptedBlasts.Num(); }
    int32 GetPendingTurns() const { return PendingTurns; }
    FVector GetSwitchPosition() const { return GetActorLocation()+FVector(0,0,-50); }
protected:
    virtual bool IsEmissionEnabled() const override { return !bTurning; }
private:
    void BeginTurn();
    TSet<uint64> AcceptedBlasts;
    float SettledYaw=90.f, TargetYaw=90.f, TurnElapsed=0.f, Flash=0.f;
    bool bTurning=false;
    int32 ActivationCount=0, PendingTurns=0;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Rotor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Base;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Switch;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
};

// Confirmed square-room sketch: four inset corner mirrors, four edge sources,
// and a fifth blast-rotatable source at the centre. No internal glass walls.
UCLASS()
class BLASTDELIVERY568_API ABDAlphaMirrorGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDAlphaMirrorGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnBombExploded(ABDBomb* Bomb) override;
    bool IsRoomReady() const { return bRoomReady; }
    const TArray<TObjectPtr<ABDRotatingLaserMirror>>& GetMirrors() const { return Mirrors; }
    const TArray<TObjectPtr<ABDMirrorLaser>>& GetLasers() const { return Lasers; }
    const TArray<TObjectPtr<ABDBulletproofGlass>>& GetGlassWalls() const { return GlassWalls; }
    ABDRotatingLaserEmitter* GetCentralEmitter() const { return CentralEmitter; }
    bool TryRotateBaseAtRay(const FVector& Origin,const FVector& Direction);
    // True consumes a hit even if an occupied destination prevents movement.
    bool TrySwapEdgeEmitterAtRay(const FVector& Origin,const FVector& Direction);
    UPROPERTY(EditAnywhere,Category="Mirror Lab|Temporary Debug") bool bEnableClickRotation=true;
private:
    void BuildRoom();
    bool TryPossessPlayer();
    bool bRoomReady=false;
    uint64 NextExplosionId=1;
    UPROPERTY() TObjectPtr<ABDCharacter> MirrorPlayer;
    UPROPERTY() TArray<TObjectPtr<ABDRotatingLaserMirror>> Mirrors;
    UPROPERTY() TArray<TObjectPtr<ABDMirrorLaser>> Lasers;
    UPROPERTY() TArray<TObjectPtr<ABDBulletproofGlass>> GlassWalls;
    UPROPERTY() TObjectPtr<ABDRotatingLaserEmitter> CentralEmitter;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> EdgeMounts;
};
