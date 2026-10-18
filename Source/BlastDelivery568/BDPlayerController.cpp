#include "BDPlayerController.h"
#include "BDCharacter.h"
#include "BDBomb.h"
#include "BDGameMode.h"
#include "BDAlphaSession.h"
#include "BDAlphaUI.h"
#include "BDAlphaViewportClient.h"
#include "BDLocalMultiplayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWeakWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"

ABDPlayerController::ABDPlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    LocalMultiplayer=CreateDefaultSubobject<UBDLocalMultiplayer>(TEXT("LocalGamepads"));
}
void ABDPlayerController::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("TutorialCamera"))) { RoomCamera = *It; break; }
    if (RoomCamera)
    {
        // Apply this to the saved map's camera too; no map regeneration is required.
        const FRotator Rotation(OverheadPitch, -90.f, 0.f);
        RoomCamera->SetActorLocationAndRotation(ViewCenter - Rotation.Vector() * CameraDistance, Rotation);
        UCameraComponent* Camera = RoomCamera->GetCameraComponent();
        Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
        Camera->SetConstraintAspectRatio(false);
        Camera->bOverrideAspectRatioAxisConstraint = true;
        Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
        Camera->SetAutoCalculateOrthoPlanes(false);
        Camera->SetUpdateOrthoPlanes(false);
        Camera->SetOrthoNearClipPlane(1.f);
        Camera->SetOrthoFarClipPlane(6000.f);
        UpdateCameraFraming();
        SetViewTarget(RoomCamera);
    }
    FInputModeGameOnly Mode;
    Mode.SetConsumeCaptureMouseDown(false);
    SetInputMode(Mode);
    if (GetWorld()->GetGameViewport())
    {
        // NoCapture skips ordinary mouse-down dispatch once the initial capture is
        // released. Temporary capture forwards every press, including RMB + LMB.
        GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
        GetWorld()->GetGameViewport()->SetMouseLockMode(EMouseLockMode::DoNotLock);
        GetWorld()->GetGameViewport()->SetHideCursorDuringCapture(false);
    }
    if (auto* Session=GetGameInstance<UBDAlphaSession>())
    {
        if (IsLocalController() && GetWorld()->GetGameViewport())
        {
            AlphaWidget=BDAlphaUI::Create(Session);
            ViewportWidget=SNew(SWeakWidget).PossiblyNullContent(AlphaWidget.ToSharedRef());
            GetWorld()->GetGameViewport()->AddViewportWidgetContent(ViewportWidget.ToSharedRef(),100);
        }
        Session->OnLevelReady();
    }
}
void ABDPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ViewportWidget.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
        GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(ViewportWidget.ToSharedRef());
    ViewportWidget.Reset(); AlphaWidget.Reset();
    Super::EndPlay(Reason);
}
void ABDPlayerController::RefreshAlphaInput()
{
    bShowMouseCursor=true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if (auto* Session=GetGameInstance<UBDAlphaSession>())
        if (Session->GetScreen()==EBDAlphaScreen::Playing && FSlateApplication::IsInitialized())
            FSlateApplication::Get().SetAllUserFocusToGameViewport();
}
void ABDPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ABDPlayerController::PressBomb);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ABDPlayerController::ReleaseBomb).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ABDPlayerController::LeftPressed);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ABDPlayerController::LeftReleased).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ABDPlayerController::TogglePause).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ABDPlayerController::Restart).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ABDPlayerController::ToggleHUD);
    InputComponent->BindKey(EKeys::Gamepad_Special_Right,IE_Pressed,this,&ABDPlayerController::TogglePause).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Gamepad_FaceButton_Right,IE_Pressed,this,&ABDPlayerController::GamepadBack).bExecuteWhenPaused=true;
}
void ABDPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    UpdateCameraFraming();
    if (auto* Session=GetGameInstance<UBDAlphaSession>())
    {
        const auto* Game=GetWorld()->GetAuthGameMode<ABDGameMode>();
        if (Game && Game->bCompleted) Session->ShowResults();
        if (Session->GetScreen()!=EBDAlphaScreen::Playing) return;
    }
    if (IsPaused()) return;
    auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    auto* Courier = Cast<ABDCharacter>(GetPawn());
    if (!Courier || !Mode || !Mode->IsGameplayInputAllowed()) return;
    const FVector2D Input((IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f),
                          (IsInputKeyDown(EKeys::S) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::W) ? 1.f : 0.f));
    Courier->MoveInRoom(Input);
    UpdateMouseAim(Courier);
    // Reconcile a release during pause or a focus change without advancing the fuse.
    if (bBombButtonDown && !IsInputKeyDown(EKeys::RightMouseButton)) ReleaseBomb();
    if (bLeftBombButtonDown && !IsInputKeyDown(EKeys::LeftMouseButton)) LeftReleased();
}
void ABDPlayerController::UpdateCameraFraming()
{
    if (!RoomCamera) return;
    int32 W, H;
    GetViewportSize(W,H);
    const float Aspect = W > 0 && H > 0 ? float(W)*UBDAlphaViewportClient::PlayfieldFraction(GetWorld(),W,H)/H : 16.f/9.f;
    RoomCamera->GetCameraComponent()->SetOrthoWidth(FMath::Max(MinimumViewWidth, MinimumViewHeight * Aspect));
}
void ABDPlayerController::UpdateMouseAim(ABDCharacter* Courier)
{
    // Intersect the cursor ray with the character's horizontal aiming plane.
    // This stays aligned with its on-screen position at any camera pitch/height,
    // including a blast jump, and avoids aiming at the invisible tall wall colliders.
    FVector RayStart, RayDirection;
    if (DeprojectMousePositionToWorld(RayStart,RayDirection) && FMath::Abs(RayDirection.Z) > SMALL_NUMBER)
    {
        const float Distance = (Courier->GetActorLocation().Z - RayStart.Z) / RayDirection.Z;
        if (Distance >= 0.f) Courier->FacePoint(RayStart + Distance * RayDirection);
    }
}
void ABDPlayerController::PressBomb()
{
    if (!CanUseGameplayInput()) return;
    if (bLeftBombButtonDown) return;
    bBombButtonDown = true;
    if (auto* Courier = Cast<ABDCharacter>(GetPawn()))
    {
        UpdateMouseAim(Courier);
        Courier->DrawBomb();
    }
}
void ABDPlayerController::ReleaseBomb()
{
    const bool bOwned=bBombButtonDown;
    if (IsPaused()) return; // Keep ownership until resume, then reconcile the release.
    bBombButtonDown = false;
    if (bOwned && CanUseGameplayInput()) if (auto* Courier = Cast<ABDCharacter>(GetPawn())) Courier->DropBomb();
}
void ABDPlayerController::LeftPressed()
{
    if (!CanUseGameplayInput()) return;
    auto* Courier=Cast<ABDCharacter>(GetPawn());
    auto* Game=GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!Courier || !Game) return;
    UpdateMouseAim(Courier);
    const auto* Session=GetGameInstance<UBDAlphaSession>();
    const bool bSimple=!Session || Session->IsSimpleControls();
    if (bSimple)
    {
        if (bBombButtonDown || bLeftBombButtonDown) return;
        bLeftBombButtonDown=true;
        Courier->DrawBomb();
    }
    // A bomb caught with an Advanced left click remains held without RMB.
    // The next click must throw that same bomb, just like a bomb readied by RMB.
    else if (bBombButtonDown || (Game->GetBombForPlayer(Courier) && Game->GetBombForPlayer(Courier)->GetHolder()==Courier)) Courier->ThrowBomb();
    else if (Game->FindNearbyBomb(Courier)) Courier->DrawBomb();
}
void ABDPlayerController::LeftReleased()
{
    if (IsPaused()) return;
    const bool bOwned=bLeftBombButtonDown;
    bLeftBombButtonDown=false;
    if (bOwned && CanUseGameplayInput()) ThrowBomb();
}
void ABDPlayerController::ThrowBomb()
{
    if (CanUseGameplayInput())
        if (auto* Courier = Cast<ABDCharacter>(GetPawn()))
        {
            // Input callbacks run before PlayerTick's usual aim update.
            UpdateMouseAim(Courier);
            Courier->ThrowBomb();
        }
}
void ABDPlayerController::TogglePause()
{
    if (auto* Session=GetGameInstance<UBDAlphaSession>()) { Session->TogglePause(); return; }
    const bool bWasPaused = IsPaused();
    SetPause(!bWasPaused);
    if (bWasPaused && !IsInputKeyDown(EKeys::RightMouseButton))
    {
        bBombButtonDown = false;
        if (CanUseGameplayInput()) if (auto* Courier = Cast<ABDCharacter>(GetPawn())) Courier->DropBomb();
    }
}
void ABDPlayerController::Restart()
{
    if (auto* Session=GetGameInstance<UBDAlphaSession>())
    {
        if (Session->GetScreen()==EBDAlphaScreen::Playing || Session->GetScreen()==EBDAlphaScreen::Pause || Session->GetScreen()==EBDAlphaScreen::Results) Session->Replay();
        return;
    }
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->RestartTutorial();
}
bool ABDPlayerController::CanUseGameplayInput() const
{
    if (const auto* Session=GetGameInstance<UBDAlphaSession>())
        if (Session->GetScreen()!=EBDAlphaScreen::Playing) return false;
    const auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>();
    return !IsPaused() && Mode && Mode->IsGameplayInputAllowed();
}
void ABDPlayerController::ResetBombInput()
{
    bBombButtonDown = false;
    bLeftBombButtonDown = false;
    FlushPressedKeys();
}
void ABDPlayerController::ToggleHUD()
{
    if (auto* Session=GetGameInstance<UBDAlphaSession>()) Session->ToggleHUD();
}
void ABDPlayerController::GamepadBack()
{
    if (auto* Session=GetGameInstance<UBDAlphaSession>())
        if (Session->GetScreen()!=EBDAlphaScreen::Playing) Session->Back();
}
void ABDPlayerController::SetPlaygroundView(const FVector& Center, float Width, float Height)
{
    OverheadPitch=-57.5f;
    SetTutorialView(Center);
    MinimumViewWidth=Width;
    MinimumViewHeight=Height;
    UpdateCameraFraming();
}
void ABDPlayerController::SetTutorialView(const FVector& Center)
{
    if (!RoomCamera) RoomCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (!RoomCamera) return;
    ViewCenter = Center;
    MinimumViewWidth = 2800.f;
    MinimumViewHeight = 1650.f;
    const FRotator Rotation(OverheadPitch, -90.f, 0.f);
    RoomCamera->SetActorLocationAndRotation(Center - Rotation.Vector()*CameraDistance, Rotation);
    auto* Camera = RoomCamera->GetCameraComponent();
    Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
    Camera->SetConstraintAspectRatio(false);
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
    Camera->SetAutoCalculateOrthoPlanes(false);
    Camera->SetUpdateOrthoPlanes(false);
    Camera->SetOrthoNearClipPlane(1.f);
    Camera->SetOrthoFarClipPlane(6000.f);
    UpdateCameraFraming();
    SetViewTarget(RoomCamera);
}
float ABDPlayerController::GetViewWidth() const
{
    return RoomCamera ? RoomCamera->GetCameraComponent()->OrthoWidth : 3000.f;
}
