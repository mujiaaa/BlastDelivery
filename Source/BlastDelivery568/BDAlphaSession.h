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
    EBDAlphaScreen GetScreen() const { return Screen; }
    bool IsPlayground() const { return bPlayground; }
    bool IsSimpleControls() const { return bSimpleControls; }
    bool IsTutorialControlsLocked() const;
    bool IsHUDVisible() const { return bShowHUD; }
    float GetVolume() const { return Volume; }
    const FBDPhysicsSettings& GetPlaygroundSettings() const { return PlaygroundSettings; }
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
    bool bTuningVisible = true;
    int32 TuningTab = 0;
    FString TuningNotice;
private:
    UPROPERTY() FBDPhysicsSettings PlaygroundSettings;
    UPROPERTY() EBDAlphaScreen Screen = EBDAlphaScreen::MainMenu;
    EBDAlphaScreen ReturnScreen = EBDAlphaScreen::MainMenu;
    bool bPlayground = false;
    bool bSimpleControls = true;
    bool bShowHUD = true;
    bool bLoadingLevel = false;
    float Volume = 1.f;
    bool bTutorialAssist = true;
};
