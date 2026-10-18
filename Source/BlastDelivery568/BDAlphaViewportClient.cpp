#include "BDAlphaViewportClient.h"
#include "BDAlphaSession.h"
#include "BDPlayerController.h"
#include "BDLocalMultiplayer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

float UBDAlphaViewportClient::PlayfieldFraction(const UWorld* World, float Width, float Height)
{
    const auto* Session=World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
    if (!Session || !Session->IsPlayground() || !Session->IsHUDVisible() || !Session->bTuningVisible
        || Session->GetScreen()!=EBDAlphaScreen::Playing || Width<=0.f || Height<=0.f) return 1.f;
    const float Scale=FMath::Min(Width/1440.f,Height/900.f);
    return FMath::Clamp(1.f-350.f*Scale/Width,.5f,1.f);
}
void UBDAlphaViewportClient::LayoutPlayers()
{
    Super::LayoutPlayers();
    if (!Viewport || !GetGameInstance()) return;
    const FIntPoint Size=Viewport->GetSizeXY();
    if (ULocalPlayer* Player=GetGameInstance()->GetFirstGamePlayer())
        Player->Size.X=PlayfieldFraction(GetWorld(),Size.X,Size.Y);
}
bool UBDAlphaViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
    if (auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)))
        if (PC->GetLocalMultiplayer() && PC->GetLocalMultiplayer()->RouteInput(EventArgs)) return true;
    return Super::InputKey(EventArgs);
}
bool UBDAlphaViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
    if (auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)))
        if (PC->GetLocalMultiplayer() && PC->GetLocalMultiplayer()->RouteInput(EventArgs)) return true;
    return Super::InputAxis(EventArgs);
}

void UBDAlphaViewportClient::LostFocus(FViewport* InViewport)
{
    // Moving focus to our own Slate sliders is not leaving the game window.
    // Only app/window deactivation should pause a running Alpha session.
    if (!GIsAutomationTesting && !FParse::Param(FCommandLine::Get(), TEXT("BDAlphaAutomation"))
        && FSlateApplication::IsInitialized())
    {
        auto& App = FSlateApplication::Get();
        const auto ActiveWindow = App.GetActiveTopLevelWindow();
        const bool OutsideGame = !App.IsActive() || (ActiveWindow.IsValid() && ActiveWindow != GetWindow());
        if (OutsideGame)
            if (auto* Session = GetWorld() ? GetWorld()->GetGameInstance<UBDAlphaSession>() : nullptr)
                if (Session->GetScreen() == EBDAlphaScreen::Playing) Session->TogglePause();
    }
    Super::LostFocus(InViewport);
}
