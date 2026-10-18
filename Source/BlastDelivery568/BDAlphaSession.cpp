#include "BDAlphaSession.h"
#include "BDGameMode.h"
#include "BDPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "AudioDevice.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
constexpr const TCHAR* AlphaPreferences = TEXT("BlastDelivery.AlphaPreferences");
bool UseSavedControlPreference()
{
    return !GIsAutomationTesting && !FParse::Param(FCommandLine::Get(), TEXT("BDAlphaAutomation"));
}
}

void UBDAlphaSession::Init()
{
    Super::Init();
    PlaygroundSettings = FBDPhysicsSettings();
    if (GConfig && UseSavedControlPreference())
        GConfig->GetBool(AlphaPreferences, TEXT("SimpleControls"), bSimpleControls, GGameUserSettingsIni);
    if (FParse::Param(FCommandLine::Get(),TEXT("BDAlphaAutomation"))) Screen=EBDAlphaScreen::Playing;
}

const TArray<FBDTuningParameter>& UBDAlphaSession::Parameters()
{
    static const TArray<FBDTuningParameter> Values = {
        {TEXT("PlayerSize"),TEXT("Character size"),0,.5f,2.f,&FBDPhysicsSettings::PlayerSize},
        {TEXT("MoveSpeed"),TEXT("Move speed"),0,2.5f,8.5f,&FBDPhysicsSettings::MoveSpeed},
        {TEXT("MovementAcceleration"),TEXT("Acceleration"),0,6.f,28.f,&FBDPhysicsSettings::MovementAcceleration},
        {TEXT("MovementBraking"),TEXT("Braking"),0,4.f,24.f,&FBDPhysicsSettings::MovementBraking},
        {TEXT("AirSteering"),TEXT("Air steering"),0,.5f,5.f,&FBDPhysicsSettings::AirSteering},
        {TEXT("HeldRecoilSpeed"),TEXT("Held-blast recoil"),0,4.f,14.f,&FBDPhysicsSettings::HeldRecoilSpeed},
        {TEXT("HeldBlastLift"),TEXT("Held-blast lift"),0,3.f,10.f,&FBDPhysicsSettings::HeldBlastLift},
        {TEXT("BombSize"),TEXT("Bomb size"),1,.5f,2.f,&FBDPhysicsSettings::BombSize},
        {TEXT("FuseSeconds"),TEXT("Fuse duration"),1,1.5f,8.f,&FBDPhysicsSettings::FuseSeconds},
        {TEXT("ThrowSpeed"),TEXT("Throw speed"),1,2.f,12.f,&FBDPhysicsSettings::ThrowSpeed},
        {TEXT("ThrowLift"),TEXT("Throw lift"),1,.5f,6.f,&FBDPhysicsSettings::ThrowLift},
        {TEXT("BlastRadius"),TEXT("Blast radius"),1,1.5f,6.f,&FBDPhysicsSettings::BlastRadius},
        {TEXT("BlastImpulse"),TEXT("Crate blast impulse"),1,140.f,650.f,&FBDPhysicsSettings::BlastImpulse},
        {TEXT("PlayerBlastSpeed"),TEXT("Ground-blast recoil"),1,8.f,26.f,&FBDPhysicsSettings::PlayerBlastSpeed},
        {TEXT("CollisionFuseMultiplier"),TEXT("Collision fuse speed"),1,2.f,20.f,&FBDPhysicsSettings::CollisionFuseMultiplier},
        {TEXT("CrateSize"),TEXT("Crate size"),2,.5f,2.f,&FBDPhysicsSettings::CrateSize},
        {TEXT("CrateMass"),TEXT("Mass"),2,10.f,60.f,&FBDPhysicsSettings::CrateMass},
        {TEXT("CrateDamping"),TEXT("Linear damping"),2,0.f,3.f,&FBDPhysicsSettings::CrateDamping},
        {TEXT("CrateWallRestitution"),TEXT("Wall rebound"),2,0.f,1.f,&FBDPhysicsSettings::CrateWallRestitution},
        {TEXT("CrateAngularDamping"),TEXT("Angular damping"),2,0.f,6.f,&FBDPhysicsSettings::CrateAngularDamping},
        {TEXT("CrateImpactTorqueScale"),TEXT("Impact rotation"),2,0.f,1.f,&FBDPhysicsSettings::CrateImpactTorqueScale},
        {TEXT("CrateMaxAngularSpeed"),TEXT("Maximum spin speed"),2,.1f,6.f,&FBDPhysicsSettings::CrateMaxAngularSpeed},
        {TEXT("GoalAssistRange"),TEXT("Goal assist range"),2,.1f,3.f,&FBDPhysicsSettings::GoalAssistRange}
    };
    return Values;
}
float UBDAlphaSession::GetParameter(FName Id) const
{
    for (const auto& P : Parameters()) if (P.Id == Id) return PlaygroundSettings.*(P.Field);
    return 0.f;
}
void UBDAlphaSession::SetParameter(FName Id, float Value)
{
    if (!bPlayground || !FMath::IsFinite(Value)) return;
    for (const auto& P : Parameters()) if (P.Id == Id)
    {
        FBDPhysicsSettings Candidate = PlaygroundSettings;
        Candidate.*(P.Field) = FMath::Clamp(Value,P.Min,P.Max);
        auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABDGameMode>() : nullptr;
        if (Mode && !Mode->ApplyPhysicsSettings(Candidate))
        {
            TuningNotice = TEXT("Move into an open area to grow.");
            return;
        }
        PlaygroundSettings = Candidate;
        TuningNotice.Reset();
        return;
    }
}
bool UBDAlphaSession::GetFeature(FName Id) const
{
    if (Id == TEXT("fastFuse")) return PlaygroundSettings.bFastCollisionFuse;
    if (Id == TEXT("goalAssist")) return PlaygroundSettings.bGoalAssist;
    if (Id == TEXT("tutorialAssist")) return bTutorialAssist;
    return false;
}
void UBDAlphaSession::SetFeature(FName Id, bool Value)
{
    if (Id == TEXT("tutorialAssist")) { bTutorialAssist = Value; return; }
    if (!bPlayground) return;
    if (Id == TEXT("fastFuse")) PlaygroundSettings.bFastCollisionFuse = Value;
    else if (Id == TEXT("goalAssist")) PlaygroundSettings.bGoalAssist = Value;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ApplyPhysicsSettings(PlaygroundSettings);
}
void UBDAlphaSession::ResetTuning()
{
    if (!bPlayground) return;
    const FBDPhysicsSettings Defaults;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (!Mode->ApplyPhysicsSettings(Defaults)) { TuningNotice=TEXT("Move into an open area to restore sizes."); return; }
    PlaygroundSettings = Defaults;
    TuningNotice.Reset();
}
void UBDAlphaSession::SetScreen(EBDAlphaScreen Value)
{
    Screen = Value;
    if (GetWorld()) UGameplayStatics::SetGamePaused(this,Value != EBDAlphaScreen::Playing);
    if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
        PC->RefreshAlphaInput();
}
void UBDAlphaSession::ShowMainMenu() { bShowHelp=false; SetScreen(EBDAlphaScreen::MainMenu); }
void UBDAlphaSession::ShowLevelSelect() { bShowHelp=false; SetScreen(EBDAlphaScreen::LevelSelect); }
void UBDAlphaSession::StartTutorial()
{
    bPlayground=false; bSimpleControls=true; bShowHelp=false; bShowHUD=true; bLoadingLevel=true;
    SetScreen(EBDAlphaScreen::Playing);
    UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/FiveSceneTutorial"));
}
void UBDAlphaSession::StartPlayground()
{
    bPlayground=true; bShowHelp=false; bShowHUD=true; bLoadingLevel=true;
    SetScreen(EBDAlphaScreen::Playing);
    UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/Tutorial"),true,TEXT("game=/Script/BlastDelivery568.BDAlphaPlaygroundGameMode"));
}
void UBDAlphaSession::Replay() { if (bPlayground) StartPlayground(); else StartTutorial(); }
void UBDAlphaSession::NextLevel() { if (!bPlayground) StartPlayground(); else ShowLevelSelect(); }
void UBDAlphaSession::OpenSettings() { ReturnScreen=Screen; SetScreen(EBDAlphaScreen::Settings); }
void UBDAlphaSession::Back()
{
    if (Screen == EBDAlphaScreen::Settings) SetScreen(ReturnScreen);
    else if (Screen == EBDAlphaScreen::Pause) Resume();
    else if (Screen == EBDAlphaScreen::LevelSelect) ShowMainMenu();
    else if (Screen == EBDAlphaScreen::Results) ShowLevelSelect();
}
void UBDAlphaSession::TogglePause()
{
    if (Screen == EBDAlphaScreen::Playing) SetScreen(EBDAlphaScreen::Pause);
    else if (Screen == EBDAlphaScreen::Pause) Resume();
    else Back();
}
void UBDAlphaSession::Resume() { SetScreen(EBDAlphaScreen::Playing); }
void UBDAlphaSession::ShowResults() { if (Screen==EBDAlphaScreen::Playing) SetScreen(EBDAlphaScreen::Results); }
void UBDAlphaSession::Quit()
{
    UKismetSystemLibrary::QuitGame(this,UGameplayStatics::GetPlayerController(this,0),EQuitPreference::Quit,false);
}
void UBDAlphaSession::SetSimpleControls(bool Value)
{
    if (IsTutorialControlsLocked()) return;
    if (bSimpleControls==Value) return;
    bSimpleControls=Value;
    // Only an explicit settings change persists. StartTutorial's temporary
    // Simple mode must not replace the player's saved preference.
    if (GConfig && UseSavedControlPreference())
    {
        GConfig->SetBool(AlphaPreferences, TEXT("SimpleControls"), Value, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    // A held gesture still belongs to the button that began it. In particular,
    // changing modes while paused must not erase a pending Simple left release:
    // the controller completes its original throw/drop intent after resuming.
}
bool UBDAlphaSession::IsTutorialControlsLocked() const
{
    return !bPlayground && (Screen==EBDAlphaScreen::Playing || Screen==EBDAlphaScreen::Pause
        || (Screen==EBDAlphaScreen::Settings && (ReturnScreen==EBDAlphaScreen::Playing || ReturnScreen==EBDAlphaScreen::Pause)));
}
void UBDAlphaSession::SetVolume(float Value)
{
    Volume=FMath::Clamp(Value,0.f,1.f);
    if (GetWorld()) if (FAudioDevice* Device=GetWorld()->GetAudioDeviceRaw()) Device->SetTransientPrimaryVolume(Volume);
}
void UBDAlphaSession::OnLevelReady()
{
    bLoadingLevel=false;
    if (const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>()) bPlayground=Mode->IsPlayground();
    SetVolume(Volume);
    SetScreen(Screen);
}
