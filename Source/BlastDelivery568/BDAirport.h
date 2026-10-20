#pragma once

#include "CoreMinimal.h"
#include "BDGameMode.h"
#include "BDAirport.generated.h"

class AStaticMeshActor;
class UStaticMeshComponent;
class UTextRenderComponent;
class ABDPlayerController;

// AirportLevel/AirportLoading from the supplied Unity project. Layout numbers
// remain in Unity metres here; the actor/physics boundary converts to UE cm.
UCLASS()
class BLASTDELIVERY568_API ABDAlphaAirportGameMode : public ABDGameMode
{
    GENERATED_BODY()
public:
    ABDAlphaAirportGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnBombExploded(ABDBomb* Bomb) override;
    virtual void OnPlayerBlasted(ABDCharacter* Player) override;
    virtual void ResolveBombExplosion(const FVector& Position,ABDCharacter* Holder,ABDBomb* Bomb) override;
    virtual void RestartTutorial() override;
    virtual bool SupportsAlphaGuides() const override { return true; }
    virtual bool SupportsGoalAssist() const override { return true; }
    virtual bool HasBlastLineOfSight(const FVector& From,AActor* Target,AActor* Bomb) const override;

    void DeploySmokeTowardAim();
    void DeploySmoke(const FVector& WorldPosition);
    bool PressEngineControl();
    void DebugCompleteDelivery();
    bool IsRoomReady() const { return bReady; }
    int32 GetAirportStage() const { return bArrived ? 2 : bCargoAccepted ? 1 : 0; }
    bool IsCargoAccepted() const { return bCargoAccepted; }
    bool IsXrayPowered() const { return bXrayPowered; }
    bool IsWeightOnPlate() const;
    bool IsCameraOccluded(const FVector& WorldTarget) const;
    bool InCameraCone(const FVector& WorldTarget) const;
    bool IsShuttleMoving() const { return bShuttleMoving; }
    bool IsCargoAboard() const;
    bool IsPlayerAboard() const;
    bool HasShutdownA() const { return bHasShutdown; }
    bool HasStartedB() const { return bHasStartedB; }
    bool HasGuidedCargo() const { return bHasGuidedCargo; }
    bool IsEngineARunning() const { return bEngineA; }
    bool IsEngineBRunning() const { return bEngineB; }
    bool IsCartAligned() const;
    float GetWindAClearSeconds() const;
    float GetWindBClearSeconds() const;
    float GetSmokeSeconds() const { return SmokeRemaining; }
    float GetCameraExposure() const { return CameraExposure; }
    float GetSmokeFraction() const { return FMath::Clamp(SmokeRemaining/22.f,0.f,1.f); }
    float GetCameraExposureFraction() const { return FMath::Clamp(CameraExposure/.7f,0.f,1.f); }
    bool IsCameraDetectingPlayer() const { return bCameraDetecting; }
    bool TryGetDeflectedWind(FVector& UnityHit,FVector& UnityDirection) const;
    bool IsDockBlastFlight() const { return bDockBlastFlight; }
    int32 GetPlayerRespawns() const { return PlayerRespawns; }
    int32 GetCargoRespawns() const { return CargoRespawns; }
    void NotifyAirportRecovery(bool bPlayer);
    FVector WindAAt(const FVector& WorldPoint) const;
    FVector WindBAt(const FVector& WorldPoint) const;
    FString GetStageStatus() const;
    int32 GetRejectionCount() const { return Rejections; }
    int32 GetBeltSegment() const { return BeltSegment; }
    int32 GetCheckpointStage() const { return CheckpointStage; }
    FVector GetPlayerCheckpoint() const { return PlayerCheckpoint; }
    FVector GetCargoCheckpoint() const { return CargoCheckpoint; }
    FVector GetShuttlePosition() const;
    FVector GetWindAControlPosition() const;
    FVector GetWindBControlPosition() const;
    static FVector FromUnity(const FVector& P) { return FVector(P.X,-P.Z,P.Y)*100.f; }
    static FVector ToUnity(const FVector& P) { return FVector(P.X,P.Z,-P.Y)/100.f; }

    UPROPERTY(VisibleAnywhere) TObjectPtr<ABDCrate> Counterweight;
    UPROPERTY(VisibleAnywhere) TObjectPtr<ABDCrate> DeflectorCart;
    UPROPERTY(VisibleAnywhere) TObjectPtr<ABDCharacter> AirportPlayer;

private:
    UPROPERTY() TArray<TObjectPtr<AActor>> RoomActors;
    UPROPERTY() TArray<TObjectPtr<AActor>> SmokeActors;
    UPROPERTY() TObjectPtr<AActor> Shuttle;
    UPROPERTY() TObjectPtr<AStaticMeshActor> ShuttleEndGate;
    UPROPERTY() TObjectPtr<AStaticMeshActor> CargoAgentGate;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> DeflectorSurface;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HardWalls;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ShuttleLamp;
    UPROPERTY() TObjectPtr<AStaticMeshActor> IncomingWindLine;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CrossingGlow;
    UPROPERTY() TArray<TObjectPtr<AActor>> BLockSymbol;
    UPROPERTY() TArray<TObjectPtr<AActor>> BPowerSymbol;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ScannerLamp;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> PlateLamp;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CameraLamp;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ConsoleALamp;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ConsoleBLamp;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Diverter;
    UPROPERTY() TObjectPtr<AActor> RotorA;
    UPROPERTY() TObjectPtr<AActor> RotorB;
    UPROPERTY() TObjectPtr<UTextRenderComponent> ConsoleAHint;
    UPROPERTY() TObjectPtr<UTextRenderComponent> ConsoleBHint;
    UPROPERTY() TObjectPtr<UTextRenderComponent> WindACountdown;
    UPROPERTY() TObjectPtr<UTextRenderComponent> WindBCountdown;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WindTrailsA;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WindTrailsB;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WindRibbon;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WindPulseA;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WindPulseB;

    bool bReady=false, bCargoAccepted=false, bXrayPowered=true;
    bool bReturning=false, bScanDecided=false, bShuttleMoving=false, bShuttleToEnd=true;
    bool bArrived=false, bEngineA=true, bEngineB=false, bHasShutdown=false;
    bool bHasStartedB=false, bHasGuidedCargo=false, bDockBlastFlight=false;
    bool bCameraDetecting=false, bCargoWasRecovering=false;
    int32 PlayerRespawns=0, CargoRespawns=0;
    int32 BeltSegment=-1, RouteKind=0, Rejections=0, CheckpointStage=0;
    float CameraExposure=0, SmokeRemaining=0, ShuttleDwell=0, WindPinnedTime=0;
    float AInterruptedUntil=-100, BInterruptedUntil=-100, DockFlightBegan=-100;
    float PulseATime=-100, PulseBTime=-100;
    FVector SmokeCenter=FVector::ZeroVector;
    FVector PlayerCheckpoint, CargoCheckpoint, AGapCenter, BGapCenter;
    FVector CameraCenter=FVector(-2,0,1);
    float CameraHalfHeight=14.2f;
    TArray<FVector> BeltRoute;
    TWeakObjectPtr<ABDCharacter> ResolvingBombHolder;

    void RemoveMapGameplay();
    void BuildSecurity();
    void BuildDock();
    void BuildLoading();
    void BuildActors();
    void BuildWindVisuals();
    bool TryPossessPlayer();
    bool CanRun() const;
    void UpdateSecurity(float DeltaSeconds);
    void UpdateConveyor(float DeltaSeconds);
    void BeginConveyor();
    void UpdateShuttle(float DeltaSeconds);
    void UpdateLoading(float DeltaSeconds);
    void UpdateCheckpoints();
    void UpdateCamera(float DeltaSeconds);
    void UpdateWindVisuals(float DeltaSeconds);
    void UpdateSmoke(float DeltaSeconds);
    void ClearSmoke();
    void AdvancePlayerCheckpoint(int32 Stage, const FVector& UnityPosition);
    void SetCargoCheckpoint(const FVector& UnityPosition);
    void RecoverPlayer();
    bool OnShuttle(const FVector& WorldPosition, float MarginMetres=0) const;
    void ResetCrate(ABDCrate* Box, const FVector& UnityPosition);
    void ApplyWind(ABDCrate* Box, const FVector& WorldAcceleration);
    bool WindLineClear(const FVector& UnityStart, const FVector& UnityEnd) const;
    bool TraceHardWall(const FVector& WorldStart,const FVector& WorldEnd,FHitResult* Nearest=nullptr) const;
    AStaticMeshActor* MarkHardWall(AStaticMeshActor* Actor);
    bool BlastHitsBeam(const FVector& UnityBlast, float Radius, const FVector& Start, const FVector& End, float Width) const;
    bool BlastDrivesCart(const FVector& UnityBlast, float Radius) const;
    float AReach() const;
    float BReach() const;
    float BHeight(float UnityZ) const;
    FVector ReflectionPoint() const;
    float Now() const;

    AStaticMeshActor* Box(const TCHAR* Name, const FVector& P, const FVector& Size,
        const TCHAR* Material, bool bSolid=false, float UnityYaw=0);
    AStaticMeshActor* Shape(const TCHAR* Name, const TCHAR* ShapeName, const FVector& P,
        const FVector& Size, const TCHAR* Material);
    AActor* Root(const TCHAR* Name, const FVector& UnityPosition);
    void Floor(const TCHAR* Name, const FVector& P, const FVector2D& Size, const TCHAR* Material, bool bGrid=true);
    void Wall(const TCHAR* Name, const FVector& P, const FVector& Size);
    void Belt(const FVector& A,const FVector& B,const TCHAR* Material,float Width=2.1f);
    AStaticMeshActor* Line(const TCHAR* Name,const FVector& A,const FVector& B,float Width,const TCHAR* Material);
    void PlaceLine(AStaticMeshActor* Actor,const FVector& A,const FVector& B,float Width);
    TArray<TObjectPtr<AStaticMeshActor>> Ring(const TCHAR* Name,const FVector& P,float Radius,const TCHAR* Material,int32 Segments=32);
    UTextRenderComponent* Label(const FString& Text,const FVector& P,float Size=40.f);
    AActor* Glyph(const TCHAR* Kind,const FVector& P,float Size=1.f,const TCHAR* Material=TEXT("Light"),float Yaw=0);
    AActor* Engine(const TCHAR* Name,const FVector& UnityOutlet,const FVector& UnityDirection);
};
