#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BDPlayerController.generated.h"

class ACameraActor;
class ABDCharacter;
class SWidget;
class UBDLocalMultiplayer;

UCLASS()
class BLASTDELIVERY568_API ABDPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ABDPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void TogglePause();
    void Restart();
    void PressBomb();
    void ReleaseBomb();
    void ThrowBomb();
    void SetTutorialView(const FVector& Center);
    void SetPlaygroundView(const FVector& Center, float Width, float Height);
    void RefreshAlphaInput();
    float GetViewWidth() const;
    void ResetBombInput();
    UBDLocalMultiplayer* GetLocalMultiplayer() const { return LocalMultiplayer; }
private:
    bool CanUseGameplayInput() const;
    void UpdateCameraFraming();
    void UpdateMouseAim(ABDCharacter* Courier);
    UPROPERTY(EditDefaultsOnly, Category="Camera") float OverheadPitch = -65.f;
    UPROPERTY(EditDefaultsOnly, Category="Camera") float CameraDistance = 3100.f;
    // The small left offset leaves room for the existing tutorial HUD.
    UPROPERTY(EditDefaultsOnly, Category="Camera") FVector ViewCenter = FVector(-250, 0, 65);
    UPROPERTY(EditDefaultsOnly, Category="Camera") float MinimumViewWidth = 2600.f;
    UPROPERTY(EditDefaultsOnly, Category="Camera") float MinimumViewHeight = 2350.f;
    UPROPERTY() TObjectPtr<ACameraActor> RoomCamera;
    UPROPERTY() TObjectPtr<UBDLocalMultiplayer> LocalMultiplayer;
    bool bBombButtonDown = false;
    bool bLeftBombButtonDown = false;
    bool bMirrorBaseClickDown = false;
    void LeftPressed();
    void LeftReleased();
    void ToggleHUD();
    void GamepadBack();
    void ToggleTuning();
    void DebugComplete();
    void AirportSmoke();
    void AirportEngine();
    TSharedPtr<SWidget> AlphaWidget;
    TSharedPtr<SWidget> ViewportWidget;
};
