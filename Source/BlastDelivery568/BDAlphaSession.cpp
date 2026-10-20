#include "BDAlphaSession.h"
#include "BDGameMode.h"
#include "BDAlphaMenu.h"
#include "TimerManager.h"
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
constexpr float TimeTargets[] = {120.f,180.f,240.f,420.f,420.f};
constexpr int32 BombTargets[] = {8,12,20,24,24};
static_assert(UE_ARRAY_COUNT(TimeTargets)==UBDAlphaSession::LevelCount);
static_assert(UE_ARRAY_COUNT(BombTargets)==UBDAlphaSession::LevelCount);
bool UseSavedControlPreference()
{
    return !GIsAutomationTesting && !FParse::Param(FCommandLine::Get(), TEXT("BDAlphaAutomation"));
}
}

void UBDAlphaSession::Init()
{
    Super::Init();
    PlaygroundSettings = FBDPhysicsSettings();
    MirrorSizes = FVector(1.5);
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
bool UBDAlphaSession::UsesMirrorSizes() const
{
    // The last selected level must not change the main-menu room or its settings.
    const EBDAlphaScreen Context = Screen == EBDAlphaScreen::Settings ? ReturnScreen : Screen;
    return CurrentLevelIndex == 2 && (Context == EBDAlphaScreen::Playing
        || Context == EBDAlphaScreen::Pause || Context == EBDAlphaScreen::Results);
}
FBDPhysicsSettings UBDAlphaSession::GetPlaygroundSettings() const
{
    FBDPhysicsSettings Settings = PlaygroundSettings;
    if (UsesMirrorSizes())
    {
        Settings.PlayerSize = MirrorSizes.X;
        Settings.BombSize = MirrorSizes.Y;
        Settings.CrateSize = MirrorSizes.Z;
    }
    return Settings;
}
FBDPhysicsSettings UBDAlphaSession::GetAuthoredSettings() const
{
    FBDPhysicsSettings Defaults;
    if (UsesMirrorSizes()) Defaults.PlayerSize = Defaults.BombSize = Defaults.CrateSize = 1.5f;
    return Defaults;
}
float UBDAlphaSession::GetParameter(FName Id) const
{
    const FBDPhysicsSettings Settings = GetPlaygroundSettings();
    for (const auto& P : Parameters()) if (P.Id == Id) return Settings.*(P.Field);
    return 0.f;
}
void UBDAlphaSession::SetParameter(FName Id, float Value)
{
    if (!FMath::IsFinite(Value)) return;
    for (const auto& P : Parameters()) if (P.Id == Id)
    {
        FBDPhysicsSettings Candidate = GetPlaygroundSettings();
        Candidate.*(P.Field) = FMath::Clamp(Value,P.Min,P.Max);
        auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABDGameMode>() : nullptr;
        if (Mode && !Mode->ApplyPhysicsSettings(Candidate))
        {
            TuningNotice = TEXT("Move into an open area to grow.");
            return;
        }
        bPracticeRun = true;
        // Commit only the edited field so a shared mass/fuse edit cannot copy
        // Mirror's sizes into the other levels' settings.
        if (UsesMirrorSizes() && P.Field == &FBDPhysicsSettings::PlayerSize) MirrorSizes.X = Candidate.PlayerSize;
        else if (UsesMirrorSizes() && P.Field == &FBDPhysicsSettings::BombSize) MirrorSizes.Y = Candidate.BombSize;
        else if (UsesMirrorSizes() && P.Field == &FBDPhysicsSettings::CrateSize) MirrorSizes.Z = Candidate.CrateSize;
        else PlaygroundSettings.*(P.Field) = Candidate.*(P.Field);
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
    bPracticeRun = true;
    if (Id == TEXT("fastFuse")) PlaygroundSettings.bFastCollisionFuse = Value;
    else if (Id == TEXT("goalAssist")) PlaygroundSettings.bGoalAssist = Value;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>()) Mode->ApplyPhysicsSettings(GetPlaygroundSettings());
}
void UBDAlphaSession::ResetTuning()
{
    bPracticeRun = true;
    const FBDPhysicsSettings Defaults = GetAuthoredSettings();
    if (auto* Mode = GetWorld()->GetAuthGameMode<ABDGameMode>())
        if (!Mode->ApplyPhysicsSettings(Defaults)) { TuningNotice=TEXT("Move into an open area to restore sizes."); return; }
    PlaygroundSettings = FBDPhysicsSettings();
    if (UsesMirrorSizes()) MirrorSizes = FVector(1.5);
    TuningNotice.Reset();
}
void UBDAlphaSession::SetScreen(EBDAlphaScreen Value)
{
    Screen = Value;
    if (GetWorld()) UGameplayStatics::SetGamePaused(this,Value != EBDAlphaScreen::Playing && Value != EBDAlphaScreen::MainMenu);
    if (auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
        PC->RefreshAlphaInput();
}
FString UBDAlphaSession::LevelTitle(int32 Index)
{
    static const TCHAR* Titles[] = {TEXT("Tutorial"),TEXT("Playground"),TEXT("Mirror Lab"),TEXT("Airport"),TEXT("Jungle")};
    return Index >= 0 && Index < LevelCount ? Titles[Index] : TEXT("");
}
bool UBDAlphaSession::IsTuningVisible() const
{
    return bTuningVisible && (Screen==EBDAlphaScreen::Playing || Screen==EBDAlphaScreen::Settings || Screen==EBDAlphaScreen::Pause);
}
void UBDAlphaSession::ToggleTuning()
{
    if (Screen!=EBDAlphaScreen::Playing && Screen!=EBDAlphaScreen::Settings && Screen!=EBDAlphaScreen::Pause) return;
    bTuningVisible=!bTuningVisible;
    if (auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->RefreshAlphaInput();
}
void UBDAlphaSession::ShowMainMenu()
{
    bShowHelp=false; bTuningVisible=false;
    SetScreen(EBDAlphaScreen::MainMenu);
    if (!GetWorld()->GetAuthGameMode<ABDAlphaMenuGameMode>() && !bLoadingLevel)
    {
        bLoadingLevel=true;
        UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/Tutorial"),true,TEXT("game=/Script/BlastDelivery568.BDAlphaMenuGameMode"));
    }
}
void UBDAlphaSession::ShowLevelSelect() { bShowHelp=false; bTuningVisible=false; SetScreen(EBDAlphaScreen::LevelSelect); }
void UBDAlphaSession::StartTutorial() { StartLevel(0); }
void UBDAlphaSession::StartPlayground() { StartLevel(1); }
void UBDAlphaSession::StartLevel(int32 Index)
{
    if (Index<0 || Index>=LevelCount) return;
    CurrentLevelIndex=Index; bPlayground=Index==1;
    if (Index==0) bSimpleControls=true;
    bShowHelp=false; bShowHUD=true; bTuningVisible=false; bLoadingLevel=true;
    bPracticeRun=false;
    SetScreen(EBDAlphaScreen::Playing);
    const FBDPhysicsSettings Settings = GetPlaygroundSettings();
    const FBDPhysicsSettings Defaults = GetAuthoredSettings();
    for (const auto& P : Parameters()) if (!FMath::IsNearlyEqual(Settings.*P.Field,Defaults.*P.Field)) bPracticeRun=true;
    bPracticeRun |= Settings.bFastCollisionFuse || Settings.bGoalAssist;
    const TCHAR* Maps[] = {TEXT("/Game/Maps/FiveSceneTutorial"),TEXT("/Game/Maps/Tutorial"),TEXT("/Game/Maps/Tutorial"),TEXT("/Game/Maps/Tutorial"),TEXT("/Game/Maps/JungleBananaTrap")};
    const TCHAR* Modes[] = {TEXT(""),TEXT("game=/Script/BlastDelivery568.BDAlphaPlaygroundGameMode"),TEXT("game=/Script/BlastDelivery568.BDAlphaMirrorGameMode"),TEXT("game=/Script/BlastDelivery568.BDAlphaAirportGameMode"),TEXT("game=/Script/BlastDelivery568.BDJungleGameMode")};
    UGameplayStatics::OpenLevel(this,FName(Maps[Index]),true,Modes[Index]);
}
void UBDAlphaSession::Replay() { StartLevel(CurrentLevelIndex); }
void UBDAlphaSession::NextLevel() { if (HasNextLevel()) StartLevel(CurrentLevelIndex+1); else ShowLevelSelect(); }
void UBDAlphaSession::OpenSettings() { ReturnScreen=Screen; SetScreen(EBDAlphaScreen::Settings); }
void UBDAlphaSession::Back()
{
    if (bTuningVisible) { ToggleTuning(); return; }
    if (Screen == EBDAlphaScreen::Settings) SetScreen(ReturnScreen);
    else if (Screen == EBDAlphaScreen::Pause) Resume();
    else if (Screen == EBDAlphaScreen::LevelSelect) ShowMainMenu();
    else if (Screen == EBDAlphaScreen::Results) ShowLevelSelect();
}
void UBDAlphaSession::TogglePause()
{
    if (bTuningVisible) { ToggleTuning(); return; }
    if (Screen == EBDAlphaScreen::Playing) SetScreen(EBDAlphaScreen::Pause);
    else if (Screen == EBDAlphaScreen::Pause) Resume();
    else Back();
}
void UBDAlphaSession::Resume() { SetScreen(EBDAlphaScreen::Playing); }
int32 UBDAlphaSession::GetSavedStarMask(int32 Level) const
{
    if (Level<0 || Level>=LevelCount || !GConfig || !UseSavedControlPreference()) return 0;
    int32 Saved=0;
    GConfig->GetInt(AlphaPreferences,*FString::Printf(TEXT("Level%dStars"),Level),Saved,GGameUserSettingsIni);
    return Saved & 7;
}
float UBDAlphaSession::GetResultTimeTarget() const
{
    return TimeTargets[FMath::Clamp(CurrentLevelIndex,0,LevelCount-1)];
}
int32 UBDAlphaSession::GetResultBombTarget() const
{
    return BombTargets[FMath::Clamp(CurrentLevelIndex,0,LevelCount-1)];
}
void UBDAlphaSession::ShowResults()
{
    if (Screen!=EBDAlphaScreen::Playing) return;
    const auto* Mode=GetWorld()->GetAuthGameMode<ABDGameMode>();
    if (!Mode || !Mode->bCompleted) return;
    ResultBombs=Mode->BombsUsed; ResultTime=Mode->ElapsedSeconds;
    // Provisional playtest targets; always award the delivery star.
    ResultStars=1 | (ResultTime<=GetResultTimeTarget()?2:0) | (ResultBombs<=GetResultBombTarget()?4:0);
    bResultPractice=bPracticeRun; ++ResultsRevision; bTuningVisible=false;
    if (!bResultPractice && GConfig && UseSavedControlPreference())
    {
        const FString Key=FString::Printf(TEXT("Level%dStars"),CurrentLevelIndex);
        int32 Best=0; GConfig->GetInt(AlphaPreferences,*Key,Best,GGameUserSettingsIni);
        GConfig->SetInt(AlphaPreferences,*Key,Best|ResultStars,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
    SetScreen(EBDAlphaScreen::Results);
}
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
    return IsTutorial() && (Screen==EBDAlphaScreen::Playing || Screen==EBDAlphaScreen::Pause
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
    if (Screen==EBDAlphaScreen::MainMenu && !GetWorld()->GetAuthGameMode<ABDAlphaMenuGameMode>())
        GetWorld()->GetTimerManager().SetTimerForNextTick(this,&UBDAlphaSession::ShowMainMenu);
    SetVolume(Volume);
    SetScreen(Screen);
}
