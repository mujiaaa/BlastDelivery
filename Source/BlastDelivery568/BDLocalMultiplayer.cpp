#include "BDLocalMultiplayer.h"
#include "BDAlphaSession.h"
#include "BDBomb.h"
#include "BDCharacter.h"
#include "BDGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
class FBDGamepadProcessor final : public IInputProcessor
{
    TWeakObjectPtr<UBDLocalMultiplayer> Router;
    bool Route(const FKeyEvent& Event,EInputEvent Type)
    {
        return Router.IsValid() && Event.GetKey().IsGamepadKey() && Router->RouteInput(
            FInputKeyEventArgs(nullptr,Event.GetInputDeviceId(),Event.GetKey(),Type,
                Type==IE_Released ? 0.f : 1.f,false,FPlatformTime::Cycles64()));
    }
public:
    explicit FBDGamepadProcessor(UBDLocalMultiplayer* InRouter):Router(InRouter) {}
    virtual void Tick(float DeltaTime,FSlateApplication& App,TSharedRef<ICursor> Cursor) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& Event) override
    { return Route(Event,Event.IsRepeat() ? IE_Repeat : IE_Pressed); }
    virtual bool HandleKeyUpEvent(FSlateApplication& App,const FKeyEvent& Event) override { return Route(Event,IE_Released); }
    virtual bool HandleAnalogInputEvent(FSlateApplication& App,const FAnalogInputEvent& Event) override
    {
        return Router.IsValid() && Event.GetKey().IsGamepadKey() && Router->RouteInput(
            FInputKeyEventArgs(nullptr,Event.GetInputDeviceId(),Event.GetKey(),IE_Axis,Event.GetAnalogValue(),false,FPlatformTime::Cycles64()));
    }
};
}

UBDLocalMultiplayer::UBDLocalMultiplayer()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
bool UBDLocalMultiplayer::IsPlayground() const
{
    const auto* Mode=GetWorld() ? GetWorld()->GetAuthGameMode<ABDGameMode>() : nullptr;
    return Mode && Mode->IsPlayground();
}
void UBDLocalMultiplayer::BeginPlay()
{
    Super::BeginPlay();
    if (!IsPlayground()) { SetComponentTickEnabled(false); return; }
    Seats.SetNum(3);
    if (const auto* Saved=GetWorld()->GetGameInstance()->GetSubsystem<UBDLocalPlayerSeats>())
        for (int32 I=0;I<3;++I) Seats[I].Device=Saved->Devices[I];
    ConnectionHandle=IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this,&UBDLocalMultiplayer::OnConnectionChanged);
    if (FSlateApplication::IsInitialized())
    {
        InputProcessor=MakeShared<FBDGamepadProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor,0);
    }
    ReconcileDevices();
}
void UBDLocalMultiplayer::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ConnectionHandle.IsValid()) IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionHandle);
    if (InputProcessor.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset();
    Super::EndPlay(Reason);
}
void UBDLocalMultiplayer::OnConnectionChanged(EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId Device)
{
    if (State==EInputDeviceConnectionState::Disconnected) SetDeviceConnected(Device,false);
    else if (State==EInputDeviceConnectionState::Connected) ReconcileDevices();
}
void UBDLocalMultiplayer::ReconcileDevices()
{
    if (!IsPlayground()) return;
    TArray<FInputDeviceId> Devices;
    IPlatformInputDeviceMapper::Get().GetAllConnectedInputDevices(Devices);
    if (const auto* Hardware=UInputDeviceSubsystem::Get())
        for (FInputDeviceId Device:Devices)
            if (Hardware->GetInputDeviceHardwareIdentifier(Device).PrimaryDeviceType==EHardwareDevicePrimaryType::Gamepad)
                SetDeviceConnected(Device,true);
    NextDeviceScan=FPlatformTime::Seconds()+.5;
}
bool UBDLocalMultiplayer::FindSpawn(int32 Seat,FVector& Position) const
{
    const auto* Primary=Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!Primary || !Mode) return false;
    const float Size=Mode->GetPhysicsSettings().PlayerSize;
    const float HalfHeight=90.f*Size;
    const FVector Origin=Primary->GetSpawnPosition();
    const float FloorZ=Primary->GetSpawnFloorHeight();
    for (int32 Ring=1;Ring<=5;++Ring)
        for (int32 Step=0;Step<12;++Step)
        {
            const float Angle=(Step+Seat*3)*PI/6.f;
            FVector Point=Origin+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(125.f*Ring);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(LocalPlayerSpawn),false);
            for (const auto& Existing:Seats) if (!Existing.bConnected && Existing.Player.IsValid()) Query.AddIgnoredActor(Existing.Player.Get());
            FHitResult Floor;
            if (!GetWorld()->LineTraceSingleByChannel(Floor,FVector(Point.X,Point.Y,FloorZ+200),FVector(Point.X,Point.Y,FloorZ-50),ECC_Visibility,Query)
                || Floor.ImpactNormal.Z<.7f || FMath::Abs(Floor.ImpactPoint.Z-FloorZ)>30.f) continue;
            Point.Z=Floor.ImpactPoint.Z+HalfHeight+2.f;
            if (GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(40.f*Size+2.f,HalfHeight),Query)) continue;
            Position=Point;
            return true;
        }
    return false;
}
void UBDLocalMultiplayer::SetDeviceConnected(FInputDeviceId Device,bool bConnected)
{
    if (!IsPlayground() || !Device.IsValid() || Seats.Num()!=3) return;
    int32 Index=Seats.IndexOfByPredicate([&](const auto& Seat){ return Seat.Device==Device.GetId(); });
    if (!bConnected)
    {
        if (Index==INDEX_NONE) return;
        auto& Seat=Seats[Index];
        Seat.bConnected=false;
        Seat.Move=Seat.Aim=FVector2D::ZeroVector;
        Seat.LeftTrigger=Seat.RightTrigger=0.f;
        Seat.bArmed=Seat.bPreviousLeft=Seat.bPreviousRight=Seat.bThrowArmed=false; Seat.HoldAction=0;
        if (Seat.Player.IsValid())
        {
            if (auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>())
                if (ABDBomb* Bomb=Mode->GetBombForPlayer(Seat.Player.Get())) if (Bomb->GetHolder()==Seat.Player.Get()) Bomb->Destroy();
            Seat.Player->SetLocalPlayerConnected(false,Seat.Player->GetActorLocation());
        }
        return;
    }
    if (Index!=INDEX_NONE && Seats[Index].bConnected) return;
    if (Index==INDEX_NONE)
    {
        // Reserve connected devices first so a disconnected earlier pad cannot
        // steal a still-connected pad's suit colour during a replay.
        TArray<FInputDeviceId> Connected;
        IPlatformInputDeviceMapper::Get().GetAllConnectedInputDevices(Connected);
        Index=Seats.IndexOfByPredicate([&](const auto& Seat)
        { return !Seat.bConnected && !Connected.Contains(FInputDeviceId::CreateFromInternalId(Seat.Device)); });
    }
    if (Index==INDEX_NONE) return;
    FVector Spawn;
    if (!FindSpawn(Index+1,Spawn)) return; // Never spawn in a wall or over the pit.
    auto& Seat=Seats[Index];
    Seat.Device=Device.GetId();
    if (auto* Saved=GetWorld()->GetGameInstance()->GetSubsystem<UBDLocalPlayerSeats>()) Saved->Devices[Index]=Seat.Device;
    if (!Seat.Player.IsValid())
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Seat.Player=GetWorld()->SpawnActor<ABDCharacter>(Spawn,FRotator::ZeroRotator,Params);
        if (!Seat.Player.IsValid()) return;
        Seat.Player->SetLocalPlayerSeat(Index+1);
        Seat.Player->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
    }
    Seat.Player->SetLocalPlayerConnected(true,Spawn);
    Seat.bConnected=true;
    Seat.bArmed=Seat.bPreviousLeft=Seat.bPreviousRight=Seat.bThrowArmed=false; Seat.HoldAction=0;
}
ABDCharacter* UBDLocalMultiplayer::GetPlayerForDevice(FInputDeviceId Device) const
{
    for (const auto& Seat:Seats) if (Seat.Device==Device.GetId() && Seat.bConnected) return Seat.Player.Get();
    return nullptr;
}
int32 UBDLocalMultiplayer::GetConnectedPlayerCount() const
{
    int32 Count=1;
    for (const auto& Seat:Seats) if (Seat.bConnected && Seat.Player.IsValid()) ++Count;
    return Count;
}
bool UBDLocalMultiplayer::RouteInput(const FInputKeyEventArgs& Event)
{
    if (!IsPlayground() || !Event.IsGamepad()) return false;
    SetDeviceConnected(Event.InputDevice,true);
    auto* Session=GetWorld()->GetGameInstance<UBDAlphaSession>();
    if (Event.Event==IE_Pressed && Event.Key==EKeys::Gamepad_Special_Right)
    {
        if (Session && (Session->GetScreen()==EBDAlphaScreen::Playing || Session->GetScreen()==EBDAlphaScreen::Pause)) Session->TogglePause();
        return true;
    }
    if (Session && Session->GetScreen()!=EBDAlphaScreen::Playing)
    {
        if (Event.Event==IE_Pressed && Event.Key==EKeys::Gamepad_FaceButton_Right) { Session->Back(); return true; }
        // Still record release/neutral while paused; menu focus uses native Slate.
    }
    for (auto& Seat:Seats)
    {
        if (Seat.Device!=Event.InputDevice.GetId() || !Seat.bConnected) continue;
        const FKey Key=Event.Key;
        if (Key==EKeys::Gamepad_LeftX) Seat.Move.X=Event.AmountDepressed;
        else if (Key==EKeys::Gamepad_LeftY) Seat.Move.Y=-Event.AmountDepressed;
        else if (Key==EKeys::Gamepad_RightX) Seat.Aim.X=Event.AmountDepressed;
        else if (Key==EKeys::Gamepad_RightY) Seat.Aim.Y=-Event.AmountDepressed;
        else if (Key==EKeys::Gamepad_LeftTriggerAxis) Seat.LeftTrigger=Event.AmountDepressed;
        else if (Key==EKeys::Gamepad_RightTriggerAxis) Seat.RightTrigger=Event.AmountDepressed;
        // Some drivers expose only the digital trigger keys.
        else if (Key==EKeys::Gamepad_LeftTrigger && Event.Event!=IE_Repeat) Seat.LeftTrigger=Event.Event==IE_Released ? 0.f : 1.f;
        else if (Key==EKeys::Gamepad_RightTrigger && Event.Event!=IE_Repeat) Seat.RightTrigger=Event.Event==IE_Released ? 0.f : 1.f;
        break;
    }
    return !Session || Session->GetScreen()==EBDAlphaScreen::Playing;
}
void UBDLocalMultiplayer::UpdateBombInput(FBDGamepadSeat& Seat)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    auto* Session=GetWorld()->GetGameInstance<UBDAlphaSession>();
    ABDCharacter* Player=Seat.Player.Get();
    if (!Mode || !Player) return;
    const bool Left=Seat.LeftTrigger>=(Seat.bPreviousLeft ? .35f : .55f);
    const bool Right=Seat.RightTrigger>=(Seat.bPreviousRight ? .35f : .55f);
    if (!Seat.bArmed)
    {
        if (Seat.RightTrigger<=.2f) Seat.bThrowArmed=true;
        if (Seat.HoldAction==1 && Seat.RightTrigger<.35f) { Player->ThrowBomb(); Seat.HoldAction=0; }
        else if (Seat.HoldAction==2 && Seat.LeftTrigger<.35f) { Player->DropBomb(); Seat.HoldAction=0; }
        else if (Seat.HoldAction==3)
        {
            if (Seat.bThrowArmed && Right && !Seat.bPreviousRight) { Player->ThrowBomb(); Seat.HoldAction=0; }
            else if (Seat.LeftTrigger<.35f) { Player->DropBomb(); Seat.HoldAction=0; }
        }
        Seat.bPreviousRight=Right;
        if (Seat.LeftTrigger<=.2f && Seat.RightTrigger<=.2f)
        {
            Seat.HoldAction=0; Seat.bPreviousLeft=Seat.bPreviousRight=false; Seat.bArmed=true;
        }
        return;
    }
    const bool LeftPressed=Left && !Seat.bPreviousLeft;
    const bool RightPressed=Right && !Seat.bPreviousRight;
    Seat.bPreviousLeft=Left; Seat.bPreviousRight=Right;
    if (Seat.HoldAction==1 || Seat.HoldAction==2)
    {
        const bool bStillHeld=Seat.HoldAction==1 ? Right : Left;
        if (!bStillHeld) { if (Seat.HoldAction==1) Player->ThrowBomb(); else Player->DropBomb(); Seat.HoldAction=0; }
        return;
    }
    const bool Simple=!Session || Session->IsSimpleControls();
    if (Simple)
    {
        if (LeftPressed || RightPressed)
            if (ABDBomb* Bomb=Mode->DrawBomb(Player)) if (Bomb->GetHolder()==Player) Seat.HoldAction=RightPressed ? 1 : 2;
        return;
    }
    ABDBomb* Bomb=Mode->GetBombForPlayer(Player);
    bool Holding=Bomb && Bomb->GetHolder()==Player;
    if (LeftPressed) { Player->DrawBomb(); Seat.HoldAction=3; Bomb=Mode->GetBombForPlayer(Player); Holding=Bomb && Bomb->GetHolder()==Player; }
    if (RightPressed)
    {
        if (Holding) { Player->ThrowBomb(); Seat.HoldAction=0; }
        else if (Mode->FindNearbyBomb(Player)) Player->DrawBomb();
    }
    else if (Seat.HoldAction==3 && !Left) { Player->DropBomb(); Seat.HoldAction=0; }
}
void UBDLocalMultiplayer::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!IsPlayground()) return;
    if (FPlatformTime::Seconds()>=NextDeviceScan) ReconcileDevices();
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    const auto* Session=GetWorld()->GetGameInstance<UBDAlphaSession>();
    const bool Playing=Mode && Mode->IsGameplayInputAllowed() && !UGameplayStatics::IsGamePaused(this)
        && (!Session || Session->GetScreen()==EBDAlphaScreen::Playing);
    for (auto& Seat:Seats)
    {
        if (!Seat.bConnected || !Seat.Player.IsValid()) continue;
        if (!Playing || Seat.Player->IsRecovering())
        {
            Seat.Player->MoveInRoom(FVector2D::ZeroVector);
            Seat.bArmed=false;
            Seat.bThrowArmed=false;
            continue;
        }
        const FVector2D Move=Seat.Move.SizeSquared()>.04f ? Seat.Move.GetClampedToMaxSize(1.f) : FVector2D::ZeroVector;
        Seat.Player->MoveInRoom(Move);
        if (Seat.Aim.SizeSquared()>=.04f)
            Seat.Player->FacePoint(Seat.Player->GetActorLocation()+FVector(Seat.Aim.X,Seat.Aim.Y,0)*100.f);
        UpdateBombInput(Seat);
    }
}
