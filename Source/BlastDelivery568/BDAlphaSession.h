#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "BDPhysicsSettings.h"
#include "BDAlphaSession.generated.h"

UENUM(BlueprintType)
enum class EBDAlphaScreen : uint8 { MainMenu, LevelSelect, Playing, Pause, Settings, Results };

// Session-only state. Never writes tuning into assets or the teammate's maps.
UCLASS()
class BLASTDELIVERY568_API UBDAlphaSession : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    static constexpr int32 LevelCount = 5;
    static FString LevelTitle(int32 Index);
    int32 GetCurrentLevelIndex() const { return CurrentLevelIndex; }
    bool IsTutorial() const { return CurrentLevelIndex == 0; }
    bool HasNextLevel() const { return CurrentLevelIndex + 1 < LevelCount; }
    void StartLevel(int32 Index);
    bool IsTuningVisible() const;
    void ToggleTuning();
    void MarkPractice() { bPracticeRun = true; }
    int32 GetResultBombsUsed() const { return ResultBombs; }
    float GetResultElapsedSeconds() const { return ResultTime; }
    int32 GetResultStarMask() const { return ResultStars; }
    int32 GetSavedStarMask(int32 Level) const;
    float GetResultTimeTarget() const;
    int32 GetResultBombTarget() const;
    bool AreResultsPractice() const { return bResultPractice; }
    int32 GetResultsRevision() const { return ResultsRevision; }
    EBDAlphaScreen GetScreen() const { return Screen; }
    bool IsPlayground() const { return bPlayground; }
    bool IsSimpleControls() const { return bSimpleControls; }
    bool IsTutorialControlsLocked() const;
    bool IsHUDVisible() const { return bShowHUD; }
    float GetVolume() const { return Volume; }
    FBDPhysicsSettings GetPlaygroundSettings() const;
    static const TArray<FBDTuningParameter>& Parameters();
    float GetParameter(FName Id) const;
    void SetParameter(FName Id, float Value);
    bool GetFeature(FName Id) const;
    void SetFeature(FName Id, bool Value);
    void ResetTuning();
    void ShowMainMenu();
    void ShowLevelSelect();
    void StartTutorial();
    void StartPlayground();
    void Replay();
    void NextLevel();
    void OpenSettings();
    void Back();
    void TogglePause();
    void Resume();
    void ShowResults();
    void Quit();
    void SetSimpleControls(bool Value);
    void SetVolume(float Value);
    void ToggleHUD() { bShowHUD = !bShowHUD; }
    void OnLevelReady();
    void SetScreen(EBDAlphaScreen Value);
    bool bShowHelp = false;
    bool bTuningVisible = false;
    int32 TuningTab = 0;
    FString TuningNotice;
private:
    bool UsesMirrorSizes() const;
    FBDPhysicsSettings GetAuthoredSettings() const;
    int32 CurrentLevelIndex = 0;
    int32 ResultBombs = 0, ResultStars = 0, ResultsRevision = 0;
    float ResultTime = 0.f;
    bool bPracticeRun = false, bResultPractice = false;
    UPROPERTY() FBDPhysicsSettings PlaygroundSettings;
    // Only these three values are level-specific; the remaining physics stay shared.
    UPROPERTY() FVector MirrorSizes = FVector(1.5);
    UPROPERTY() EBDAlphaScreen Screen = EBDAlphaScreen::MainMenu;
    EBDAlphaScreen ReturnScreen = EBDAlphaScreen::MainMenu;
    bool bPlayground = false;
    bool bSimpleControls = true;
    bool bShowHUD = true;
    bool bLoadingLevel = false;
    float Volume = 1.f;
    bool bTutorialAssist = true;
};
