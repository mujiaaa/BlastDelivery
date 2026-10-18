#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BDLocalMultiplayer.generated.h"

class ABDCharacter;
struct FInputKeyEventArgs;
class IInputProcessor;

// Device reservations survive level replay without creating additional views.
UCLASS()
class BLASTDELIVERY568_API UBDLocalPlayerSeats : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    TArray<int32> Devices={-1,-1,-1};
};

struct FBDGamepadSeat
{
    int32 Device=-1;
    bool bConnected=false;
    TWeakObjectPtr<ABDCharacter> Player;
    FVector2D Move=FVector2D::ZeroVector;
    FVector2D Aim=FVector2D::ZeroVector;
    float LeftTrigger=0.f,RightTrigger=0.f;
    bool bArmed=false,bPreviousLeft=false,bPreviousRight=false;
    bool bThrowArmed=false;
    // The initiating trigger keeps ownership even if controls change in Settings.
    uint8 HoldAction=0; // 1=Simple throw, 2=Simple place, 3=Advanced LT hold.
};

// A single camera and HUD; independent gamepad pawns occupy the same world.
UCLASS(ClassGroup=(Input))
class BLASTDELIVERY568_API UBDLocalMultiplayer : public UActorComponent
{
    GENERATED_BODY()
public:
    UBDLocalMultiplayer();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    bool RouteInput(const FInputKeyEventArgs& Event);
    void SetDeviceConnected(FInputDeviceId Device,bool bConnected);
    ABDCharacter* GetPlayerForDevice(FInputDeviceId Device) const;
    int32 GetConnectedPlayerCount() const;
private:
    TArray<FBDGamepadSeat> Seats;
    FDelegateHandle ConnectionHandle;
    TSharedPtr<IInputProcessor> InputProcessor;
    double NextDeviceScan=0;
    void ReconcileDevices();
    void OnConnectionChanged(EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId Device);
    bool IsPlayground() const;
    bool FindSpawn(int32 Seat,FVector& Position) const;
    void UpdateBombInput(FBDGamepadSeat& Seat);
};
