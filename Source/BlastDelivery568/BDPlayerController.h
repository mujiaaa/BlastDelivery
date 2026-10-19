#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BDPlayerController.generated.h"

class ACameraActor;
class ABDCharacter;

UCLASS()
class BLASTDELIVERY568_API ABDPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ABDPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    void TogglePause();
    void Restart();
    void PressBomb();
    void ReleaseBomb();
    void ThrowBomb();
    void SetTutorialView(const FVector& Center);
    void SetRoomView(const FVector& Center, float Width, float Height);
    float GetViewWidth() const;
    void ResetBombInput();
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
    bool bBombButtonDown = false;
};
