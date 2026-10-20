#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAlphaPlayground.h"
#include "../BDAlphaSession.h"
#include "../BDBomb.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDPlayerController.h"
#include "../BDThrowPreview.h"
#include "../BDTutorialGameMode.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SWindow.h"
#include <limits>

namespace BDChargedThrowTests
{
void Capture(UWorld* World, const TCHAR* Name)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("BDAlphaScreenshots"))) return;
    if (!World || !World->GetGameViewport()) return;
    const auto Window = World->GetGameViewport()->GetWindow();
    TArray<FColor> Pixels;
    FIntVector Size = FIntVector::ZeroValue;
    // The scene viewport excludes Slate; capture the composed window so the
    // tuning panel, checkbox states, and HUD are present in visual QA images.
    const bool bCaptured = Window.IsValid() && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size);
    if (!bCaptured)
    {
        FViewport* Viewport = World->GetGameViewport()->Viewport;
        if (!Viewport || !Viewport->ReadPixels(Pixels)) return;
        const FIntPoint ViewSize = Viewport->GetSizeXY();
        Size = FIntVector(ViewSize.X, ViewSize.Y, 0);
    }
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/ChargedThrowShots");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
    FFileHelper::SaveArrayToFile(PNG, *(Folder / Name));
}

TSharedPtr<SWidget> FindTag(const TSharedRef<SWidget>& Widget, FName Tag)
{
    if (!Widget->GetVisibility().IsVisible()) return nullptr;
    if (Widget->GetTag() == Tag) return Widget;
    FChildren* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Found = FindTag(Children->GetChildAt(I), Tag)) return Found;
    return nullptr;
}

struct FFixture
{
    FAutomationTestBase* Test = nullptr;
    UWorld* World = nullptr;
    ABDAlphaPlaygroundGameMode* Mode = nullptr;
    ABDPlayerController* PC = nullptr;
    ABDCharacter* Player = nullptr;
    UBDAlphaSession* Session = nullptr;
    bool bFailed = false;
    double StepStart = 0.;
    uint64 StepFrame = 0, ExplosionFrame = 0;
    float WorldMark = 0.f, Fuse = 0.f, Charge = 0.f, Radius = 0.f, Mass = 0.f;
    int32 BombCount = 0;
    FVector Scale = FVector::OneVector, Position = FVector::ZeroVector;
    TWeakObjectPtr<ABDBomb> Bomb;

    void Refresh()
    {
        World = GEditor->PlayWorld;
        Mode = World ? World->GetAuthGameMode<ABDAlphaPlaygroundGameMode>() : nullptr;
        PC = World ? Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        Player = PC ? Cast<ABDCharacter>(PC->GetPawn()) : nullptr;
        Session = World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
    }
    bool Ready() const { return World && Mode && Mode->IsRoomReady() && PC && Player && Session; }
    bool After(double Seconds) const
    {
        return GFrameCounter >= StepFrame + 3 && FPlatformTime::Seconds() - StepStart >= Seconds;
    }
    bool Require(bool Value, const TCHAR* Message)
    {
        if (!Test->TestTrue(Message, Value)) bFailed = true;
        return Value;
    }
    bool RequireBomb() { return Require(Bomb.IsValid(), TEXT("The live bomb survives this test step")); }
    void Key(FKey Key, EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.f : 1.f));
        // Exercise the real binding and key state used by release reconciliation.
        PC->PlayerTick(PC->IsPaused() ? 0.f : 1.f / 120.f);
    }
    void ClearBombs()
    {
        Key(EKeys::LeftMouseButton, IE_Released);
        Key(EKeys::RightMouseButton, IE_Released);
        PC->ResetBombInput();
        for (TActorIterator<ABDBomb> It(World); It; ++It) It->Destroy();
        Bomb.Reset();
    }
    void PlacePlayer()
    {
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(FVector(-700, 350, 95), false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        Player->FacePoint(Player->GetActorLocation() + FVector(400, 0, 0));
        FVector2D Screen;
        if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation() + FVector(400, 0, 0), Screen))
            PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
    }
    void Press()
    {
        Key(EKeys::LeftMouseButton, IE_Pressed);
        Bomb = Mode->GetBombForPlayer(Player);
        RequireBomb();
    }
    void ReleaseAndCheckLaunch()
    {
        if (!RequireBomb()) return;
        const FVector Expected = Bomb->GetThrowVelocity(Player->GetActorForwardVector());
        Key(EKeys::LeftMouseButton, IE_Released);
        Test->TestTrue(TEXT("LMB release throws the same bomb"), Bomb->WasThrown() && !Bomb->IsHeld());
        Test->TestTrue(TEXT("Actual launch uses the same velocity as the trajectory prediction"),
            Bomb->Collision->GetPhysicsLinearVelocity().Equals(Expected, .1f));
    }
    bool ClickFeature(const TCHAR* Id)
    {
        const auto Window = World->GetGameViewport()->GetWindow();
        const auto Widget = Window.IsValid()
            ? FindTag(Window.ToSharedRef(), FName(*FString::Printf(TEXT("AlphaFeature.%s"), Id))) : nullptr;
        if (!Require(Widget.IsValid() && Widget->GetType() == FName(TEXT("SCheckBox")),
            TEXT("Preview option is a visible native checkbox"))) return false;
        auto& App = FSlateApplication::Get();
        FWidgetPath Path;
        if (!Require(App.GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path),
            TEXT("Checkbox has a live Slate path"))) return false;
        const FGeometry Geometry = Widget->GetCachedGeometry();
        const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f);
        TSet<FKey> Pressed; Pressed.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0, Point, Point, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
        Widget->OnMouseEnter(Geometry, Down);
        // Default SCheckBox toggles on release only while holding capture.
        // Apply the handler replies, including in an inactive offscreen window,
        // instead of merely checking that both native handlers return Handled.
        const bool bAllowInactiveInput = App.GetHandleDeviceInputWhenApplicationNotActive();
        App.SetHandleDeviceInputWhenApplicationNotActive(true);
        const FReply DownReply = Widget->OnMouseButtonDown(Geometry, Down);
        App.ProcessReply(Path, DownReply, &Path, &Down);
        const bool bCaptured = Widget->HasMouseCapture();
        Pressed.Reset();
        const FPointerEvent Up(0, Point, Point, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
        const FReply UpReply = Widget->OnMouseButtonUp(Geometry, Up);
        App.ProcessReply(Path, UpReply, &Path, &Up);
        App.SetHandleDeviceInputWhenApplicationNotActive(bAllowInactiveInput);
        Widget->OnMouseLeave(Up);
        return Require(bCaptured && DownReply.IsEventHandled() && UpReply.IsEventHandled() && !Widget->HasMouseCapture(),
            TEXT("Preview checkbox captures, consumes, and releases the mouse gesture"));
    }
};

class FBoot final : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    bool bRequested = false;
    double Started = FPlatformTime::Seconds();
public:
    explicit FBoot(TSharedRef<FFixture> In) : F(In) {}
    bool Update() override
    {
        F->Refresh();
        if (!bRequested && F->World && F->Session) { bRequested = true; F->Session->StartPlayground(); }
        if (bRequested && F->Ready()) return true;
        if (FPlatformTime::Seconds() - Started > 25.)
        {
            F->Require(false, TEXT("Charged-throw fixture opens the real Playground"));
            return true;
        }
        return false;
    }
};

using FAction = TFunction<void(FFixture&)>;
using FCondition = TFunction<bool(FFixture&)>;
class FStep final : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Started = FPlatformTime::Seconds();
    bool bEntered = false;
public:
    FStep(TSharedRef<FFixture> In, FString Label, FAction Action, FCondition Condition)
        : F(In), Name(MoveTemp(Label)), Begin(MoveTemp(Action)), Until(MoveTemp(Condition)) {}
    bool Update() override
    {
        if (F->bFailed) return true;
        F->Refresh();
        if (!bEntered && F->Ready())
        {
            bEntered = true; Started = FPlatformTime::Seconds();
            F->StepStart = Started; F->StepFrame = GFrameCounter;
            F->Test->AddInfo(Name);
            if (Begin) Begin(*F);
        }
        if (F->bFailed) return true;
        if (bEntered && F->Ready() && Until(*F)) return true;
        if (FPlatformTime::Seconds() - Started > 18.)
        {
            F->Test->AddError(TEXT("Charged throw timed out: ") + Name); F->bFailed = true; return true;
        }
        return false;
    }
};

// Exercise only the lesson affected by removal of RMB placement. Keeping this
// separate avoids depending on the unrelated gap-jump and delivery solutions.
class FPutDown final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Stage = 0;
    double Started = FPlatformTime::Seconds();
    uint64 Frame = 0;
    TWeakObjectPtr<ABDBomb> Bomb;
    float Fuse = 0.f;
    void Next() { ++Stage; Started = FPlatformTime::Seconds(); Frame = GFrameCounter; }
public:
    explicit FPutDown(FAutomationTestBase* In) : Test(In) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 20.)
        {
            Test->AddError(FString::Printf(TEXT("LMB placement lesson timed out at stage %d"), Stage));
            return true;
        }
        UWorld* World = GEditor->PlayWorld;
        auto* Mode = World ? World->GetAuthGameMode<ABDTutorialGameMode>() : nullptr;
        auto* PC = World ? Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        auto* Player = PC ? Cast<ABDCharacter>(PC->GetPawn()) : nullptr;
        auto* Session = World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
        if (!Mode || !PC || !Player || !Session) return false;
        auto Key = [PC](FKey Key, EInputEvent Event)
        {
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.f : 1.f));
            PC->PlayerTick(1.f / 120.f);
        };
        auto Aim = [PC, Player]()
        {
            const FVector Target = Player->GetActorLocation() + FVector(400, 0, 0);
            Player->FacePoint(Target);
            FVector2D Screen;
            if (PC->ProjectWorldLocationToScreen(Target, Screen)) PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
        };
        switch (Stage)
        {
        case 0:
        {
            Session->SetScreen(EBDAlphaScreen::Playing);
            Mode->SceneOrder = {EBDTutorialScene::PutDown};
            Mode->RestartTutorial();
            FBDPhysicsSettings P = Mode->GetPhysicsSettings(); P.FuseSeconds = 8.f;
            if (!Test->TestTrue(TEXT("Placement fixture accepts its longer observation fuse"), Mode->ApplyPhysicsSettings(P))) return true;
            Next(); return false;
        }
        case 1:
            if (!Player->GetCharacterMovement()->IsMovingOnGround()) return false;
            Test->TestEqual(TEXT("The isolated lesson is PutDown"), Mode->GetScene(), EBDTutorialScene::PutDown);
            Aim();
            Key(EKeys::RightMouseButton, IE_Pressed); Key(EKeys::RightMouseButton, IE_Released);
            Test->TestNull(TEXT("RMB cannot bypass the placement lesson"), Mode->ActiveBomb.Get());
            Key(EKeys::LeftMouseButton, IE_Released);
            Test->TestEqual(TEXT("A release without a bomb cannot complete placement"), Mode->Phase, EBDTutorialPhase::Playing);
            Key(EKeys::LeftMouseButton, IE_Pressed); Bomb = Mode->ActiveBomb;
            if (!Test->TestNotNull(TEXT("LMB creates the lesson's live bomb"), Bomb.Get())) return true;
            Test->TestEqual(TEXT("Holding alone does not complete placement"), Mode->Phase, EBDTutorialPhase::Playing);
            Fuse = Bomb->GetRemainingFuse(); Key(EKeys::LeftMouseButton, IE_Released);
            Test->TestTrue(TEXT("The quick tap is a real low-charge throw"), Bomb->WasThrown() && !Bomb->IsHeld() && Bomb->GetChargeFraction() <= .2f);
            Test->TestEqual(TEXT("An airborne short toss waits for actual floor contact"), Mode->Phase, EBDTutorialPhase::Playing);
            Next(); return false;
        case 2:
            if (Mode->Phase != EBDTutorialPhase::Success) return false;
            if (!Test->TestNotNull(TEXT("Successful placement keeps its original burning actor"), Bomb.Get())) return true;
            Test->TestTrue(TEXT("LMB tap completes placement only near the scene floor"),
                Bomb->GetActorLocation().Z - Mode->GetSceneOrigin().Z <= Bomb->GetWorldRadius() + 8.f);
            Test->TestTrue(TEXT("Placement preserves the fuse instead of restarting it"), Bomb->GetRemainingFuse() > 0.f && Bomb->GetRemainingFuse() < Fuse);
            Test->TestEqual(TEXT("Placement uses exactly one bomb"), Mode->BombsUsed, 1);
            Next(); return false;
        case 3:
            if (GFrameCounter < Frame + 3 || FPlatformTime::Seconds() - Started < .2) return false;
            Capture(World, TEXT("03-tutorial-lmb-placement.png"));
            Mode->RestartTutorial(); Bomb.Reset(); Next(); return false;
        case 4:
            if (!Player->GetCharacterMovement()->IsMovingOnGround()) return false;
            Aim(); Key(EKeys::LeftMouseButton, IE_Pressed); Bomb = Mode->ActiveBomb;
            if (!Test->TestNotNull(TEXT("Charged negative case creates a bomb"), Bomb.Get())) return true;
            Next(); return false;
        case 5:
            if (!Test->TestNotNull(TEXT("Negative-case fuse survives the short charge"), Bomb.Get())) return true;
            if (Bomb->GetChargeFraction() <= .25f) return false;
            Key(EKeys::LeftMouseButton, IE_Released); Next(); return false;
        case 6:
            if (!Test->TestNotNull(TEXT("Charged throw reaches the floor before expiry"), Bomb.Get())) return true;
            if (Bomb->GetActorLocation().Z - Mode->GetSceneOrigin().Z > Bomb->GetWorldRadius() + 8.f) return false;
            Test->TestEqual(TEXT("A stronger throw does not satisfy the low-charge placement lesson"), Mode->Phase, EBDTutorialPhase::Playing);
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDChargedThrowDefaultsTest, "BlastDelivery.ChargedThrow.DefaultContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDChargedThrowDefaultsTest::RunTest(const FString& Parameters)
{
    const FBDPhysicsSettings P;
    TestEqual(TEXT("Full charge takes 2.4 seconds"), P.ChargeSeconds, 2.4f);
    TestEqual(TEXT("Tap horizontal speed is 0.8 metres per second"), P.MinThrowSpeed, .8f);
    TestEqual(TEXT("Tap lift is 0.3 metres per second"), P.MinThrowLift, .3f);
    TestEqual(TEXT("Full-charge horizontal speed is ten metres per second"), P.ThrowSpeed, 10.f);
    TestEqual(TEXT("Full-charge lift is 2.7 metres per second"), P.ThrowLift, 2.7f);
    TestEqual(TEXT("Default throw curve is linear"), P.ChargeThrowExponent, 1.f);
    TestEqual(TEXT("Default radius curve is linear"), P.ChargeRadiusExponent, 1.f);
    TestEqual(TEXT("Default impulse curve is linear"), P.ChargeImpulseExponent, 1.f);
    TestEqual(TEXT("Maximum charge doubles radius"), P.MaxChargeRadiusMultiplier, 2.f);
    TestEqual(TEXT("Maximum charge doubles impulse"), P.MaxChargeImpulseMultiplier, 2.f);
    TestEqual(TEXT("Maximum charge doubles the visible shell"), P.MaxChargeVisualScale, 2.f);
    TestTrue(TEXT("All three independent previews start enabled"), P.bShowTrajectory && P.bShowLandingPoint && P.bShowBlastRadius);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDChargedThrowTutorialTest, "BlastDelivery.ChargedThrow.TutorialPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDChargedThrowTutorialTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDChargedThrowTests::FPutDown>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDChargedThrowIntegrationTest, "BlastDelivery.ChargedThrow.InputPhysicsAndTuning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDChargedThrowIntegrationTest::RunTest(const FString& Parameters)
{
    using namespace BDChargedThrowTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    const auto F = MakeShared<FFixture>(); F->Test = this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    auto Step = [&](const TCHAR* Name, FAction Begin, FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F, Name, MoveTemp(Begin), MoveTemp(Until))); };

    Step(TEXT("RMB is inert in either control mode; a quick LMB tap launches at minimum power"), [](FFixture& X)
    {
        X.ClearBombs(); X.Session->ResetTuning(); X.PlacePlayer();
        X.Session->SetParameter(TEXT("FuseSeconds"), 8.f);
        X.Session->SetParameter(TEXT("ChargeSeconds"), 2.4f);
        X.Session->SetFeature(TEXT("fastFuse"), true);
        const int32 Used = X.Mode->BombsUsed;
        for (bool bSimple : {true, false})
        {
            X.Session->SetSimpleControls(bSimple);
            X.Key(EKeys::RightMouseButton, IE_Pressed); X.Key(EKeys::RightMouseButton, IE_Released);
            X.Test->TestNull(TEXT("Right mouse cannot create a bomb"), X.Mode->GetBombForPlayer(X.Player));
            X.Test->TestEqual(TEXT("Right mouse never spends a bomb"), X.Mode->BombsUsed, Used);
        }
        X.Press();
        if (!X.RequireBomb()) return;
        X.Test->TestTrue(TEXT("LMB press immediately ignites and holds"), X.Bomb->IsHeld() && X.Bomb->GetRemainingFuse() > 0.f);
        X.Test->TestEqual(TEXT("An immediate tap starts uncharged"), X.Bomb->GetChargeFraction(), 0.f);
        X.Test->TestTrue(TEXT("Tap velocity uses the minimum designer endpoints"),
            X.Bomb->GetThrowVelocity(FVector::ForwardVector).Equals(FVector(80, 0, 30), .01f));
        const FBDThrowPreview Preview = BDThrowPreview::Calculate(X.Bomb.Get(), X.Player);
        X.Test->TestTrue(TEXT("The tap preview predicts a floor contact before the fuse expires"),
            Preview.bHit && !Preview.bFuseExpired && Preview.ContactNormal.Z > .9f);
        if (!X.Require(Preview.Points.Num() > 2, TEXT("Trajectory contains a simulated flight path"))) return;
        X.Test->TestTrue(TEXT("Preview begins at the actual physical grip"), Preview.Points[0].Equals(X.Bomb->GetActorLocation(), .01f));
        X.Test->TestTrue(TEXT("Preview travels in the launch direction"),
            FVector::DotProduct(Preview.Points[1] - Preview.Points[0], X.Player->GetActorForwardVector()) > 0.f);
        X.Position = Preview.ContactPoint + Preview.ContactNormal * X.Bomb->GetWorldRadius();
        X.ReleaseAndCheckLaunch();
        X.Test->TestEqual(TEXT("One tap spends exactly one bomb"), X.Mode->BombsUsed, Used + 1);
    }, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        // The first-contact fuse latch samples a real Chaos collision, without
        // guessing when the airborne actor reaches the predicted landing height.
        if (!X.Bomb->HasCollisionFuseAccelerated()) return false;
        X.Test->TestTrue(TEXT("Actual first contact lands within 25 cm of the preview"),
            FVector::Dist(X.Bomb->GetActorLocation(), X.Position) < 25.f);
        return true;
    });
    Step(TEXT("Holding increases throw power while the fuse continues to burn"), [](FFixture& X)
    {
        X.Session->SetFeature(TEXT("fastFuse"), false);
        X.ClearBombs(); X.Session->SetSimpleControls(true); X.Press();
        if (!X.RequireBomb()) return;
        X.Fuse = X.Bomb->GetRemainingFuse(); X.Radius = X.Bomb->GetWorldRadius();
        X.Mass = X.Bomb->Collision->GetMass(); X.Scale = X.Bomb->Mesh->GetRelativeScale3D();
        X.WorldMark = X.World->GetTimeSeconds();
    }, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.World->GetTimeSeconds() - X.WorldMark < .65f) return false;
        X.Charge = X.Bomb->GetChargeFraction();
        X.Test->TestTrue(TEXT("Holding builds partial charge while the fuse burns"), X.Charge > .2f && X.Charge < .9f
            && X.Bomb->GetRemainingFuse() < X.Fuse - .2f);
        const auto& P = X.Bomb->GetProfile();
        const FVector Expected(FMath::Lerp(P.MinThrowSpeed, P.ThrowSpeed, X.Charge) * 100.f, 0,
            FMath::Lerp(P.MinThrowLift, P.ThrowLift, X.Charge) * 100.f);
        X.Test->TestTrue(TEXT("Partial charge interpolates launch speed and lift"), X.Bomb->GetThrowVelocity(FVector::ForwardVector).Equals(Expected, .01f));
        return true;
    });
    Step(TEXT("Partial charge and its previews render beside the Bomb tuning controls"), [](FFixture& X)
    {
        X.Session->TuningTab = 1; X.Session->ToggleTuning();
    }, [](FFixture& X)
    {
        if (!X.After(.2)) return false;
        if (!X.RequireBomb()) return true;
        Capture(X.World, TEXT("01-partial-charge-tuning.png"));
        X.Session->ToggleTuning();
        X.Charge = X.Bomb->GetChargeFraction();
        X.ReleaseAndCheckLaunch(); X.Fuse = X.Bomb->GetRemainingFuse(); X.WorldMark = X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("Flight freezes charge; pickup preserves fuse and charge and resumes accumulation"), {}, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.World->GetTimeSeconds() - X.WorldMark < .15f) return false;
        X.Test->TestEqual(TEXT("Flying cannot gain charge"), X.Bomb->GetChargeFraction(), X.Charge);
        X.Test->TestTrue(TEXT("Flying still consumes the original fuse"), X.Bomb->GetRemainingFuse() < X.Fuse);
        X.Bomb->SetActorLocation(X.Player->GetActorLocation() + FVector(90, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        const auto Original = X.Bomb; X.Fuse = X.Bomb->GetRemainingFuse(); X.BombCount = X.Mode->BombsUsed;
        X.Press();
        if (!X.RequireBomb()) return true;
        X.Test->TestTrue(TEXT("LMB catches the original actor"), X.Bomb == Original && X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Pickup retains accumulated charge"), X.Bomb->GetChargeFraction(), X.Charge);
        X.Test->TestEqual(TEXT("Pickup retains the exact remaining fuse"), X.Bomb->GetRemainingFuse(), X.Fuse);
        X.Test->TestEqual(TEXT("Pickup does not spend another bomb"), X.Mode->BombsUsed, X.BombCount);
        X.Key(EKeys::RightMouseButton, IE_Pressed); X.Key(EKeys::RightMouseButton, IE_Released);
        X.Test->TestTrue(TEXT("Right mouse cannot release a held bomb"), X.Bomb->IsHeld());
        return true;
    });

    Step(TEXT("Maximum charge scales the blast and matching physical shell while mass stays fixed"), {}, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.Bomb->GetChargeFraction() < 1.f) return false;
        const FBDPhysicsSettings Base = X.Bomb->GetProfile(), Blast = X.Bomb->GetExplosionProfile();
        X.Test->TestTrue(TEXT("Pickup resumes charge instead of leaving it frozen"), X.Bomb->GetChargeFraction() > X.Charge);
        X.Test->TestEqual(TEXT("Full charge doubles the base radius"), Blast.BlastRadius, Base.BlastRadius * 2.f);
        X.Test->TestEqual(TEXT("Full charge doubles the base impulse"), Blast.BlastImpulse, Base.BlastImpulse * 2.f);
        X.Test->TestEqual(TEXT("Derived blast does not overwrite base radius"), Base.BlastRadius, 3.6f);
        X.Test->TestEqual(TEXT("Derived blast does not overwrite base impulse"), Base.BlastImpulse, 340.f);
        X.Test->TestTrue(TEXT("Full charge reaches maximum throw endpoints"), X.Bomb->GetThrowVelocity(FVector::ForwardVector).Equals(FVector(1000, 0, 270), .01f));
        X.Test->TestTrue(TEXT("Visible shell grows from its base scale exactly once"), X.Bomb->Mesh->GetRelativeScale3D().Equals(X.Scale * 2.f, .001f));
        X.Test->TestEqual(TEXT("In open space the collider grows with the visible shell"), X.Bomb->GetWorldRadius(), X.Radius * 2.f);
        X.Test->TestTrue(TEXT("Physical and visible radii agree at full charge"),
            FMath::IsNearlyEqual(double(X.Bomb->GetWorldRadius()), X.Bomb->Mesh->GetComponentScale().X * 50., .01));
        X.Test->TestTrue(TEXT("Charge never changes physical bomb mass"), FMath::IsNearlyEqual(X.Bomb->Collision->GetMass(), X.Mass, .001f));
        return true;
    });
    Step(TEXT("Full charge and its previews render beside the Bomb tuning controls"), [](FFixture& X)
    {
        X.Session->TuningTab = 1; X.Session->ToggleTuning();
    }, [](FFixture& X)
    {
        if (!X.After(.2)) return false;
        if (!X.RequireBomb()) return true;
        Capture(X.World, TEXT("02-full-charge-tuning.png"));
        X.Session->ToggleTuning();
        const FBDPhysicsSettings Blast = X.Bomb->GetExplosionProfile();
        for (int32 I = 0; I < 2; ++I)
        {
            X.ReleaseAndCheckLaunch();
            X.Bomb->SetActorLocation(X.Player->GetActorLocation() + FVector(90, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
            X.Press();
            if (!X.RequireBomb()) return true;
            X.Test->TestEqual(TEXT("Repeated pickup cannot compound blast radius"), X.Bomb->GetExplosionProfile().BlastRadius, Blast.BlastRadius);
            X.Test->TestEqual(TEXT("Repeated pickup cannot compound impulse"), X.Bomb->GetExplosionProfile().BlastImpulse, Blast.BlastImpulse);
        }
        X.ClearBombs(); X.Press(); X.WorldMark = X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("Releasing LMB in paused settings preserves the pending throw"), {}, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.World->GetTimeSeconds() - X.WorldMark < .15f) return false;
        X.Charge = X.Bomb->GetChargeFraction(); X.Fuse = X.Bomb->GetRemainingFuse(); X.BombCount = X.Mode->BombsUsed;
        X.Session->OpenSettings(); X.Key(EKeys::LeftMouseButton, IE_Released);
        return true;
    });
    Step(TEXT("Pause freezes both clocks; resume reconciles the release exactly once"), {}, [](FFixture& X)
    {
        if (!X.After(.25)) return false;
        if (!X.RequireBomb()) return true;
        X.Test->TestTrue(TEXT("Paused release does not throw into the paused world"), X.PC->IsPaused() && X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Pause freezes charge"), X.Bomb->GetChargeFraction(), X.Charge);
        X.Test->TestEqual(TEXT("Pause freezes fuse"), X.Bomb->GetRemainingFuse(), X.Fuse);
        X.Session->Back(); X.PC->PlayerTick(1.f / 120.f);
        X.Test->TestTrue(TEXT("Resume throws without another click"), !X.Bomb->IsHeld() && X.Bomb->WasThrown());
        X.Test->TestEqual(TEXT("Resume preserves the frozen charge"), X.Bomb->GetChargeFraction(), X.Charge);
        X.PC->PlayerTick(1.f / 120.f);
        X.Test->TestEqual(TEXT("Reconciliation creates no extra bomb"), X.Mode->BombsUsed, X.BombCount);
        X.ClearBombs(); X.Session->TuningTab = 1; X.Session->ToggleTuning();
        return true;
    });

    Step(TEXT("Preview checkboxes are independent and do not leak a click to bomb input"), {}, [](FFixture& X)
    {
        if (!X.After(.25)) return false;
        const int32 Used = X.Mode->BombsUsed;
        const TCHAR* Ids[] = {TEXT("trajectory"), TEXT("landingPoint"), TEXT("blastPreview")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(Ids); ++I)
        {
            if (!X.ClickFeature(Ids[I])) return true;
            for (int32 J = 0; J < UE_ARRAY_COUNT(Ids); ++J)
                X.Test->TestEqual(TEXT("Each checkbox changes only its own preview"), X.Session->GetFeature(Ids[J]), J > I);
        }
        const auto& P = X.Mode->GetPhysicsSettings();
        X.Test->TestTrue(TEXT("All checkbox values reach the live profile"), !P.bShowTrajectory && !P.bShowLandingPoint && !P.bShowBlastRadius);
        X.Test->TestEqual(TEXT("UI clicks never create bombs"), X.Mode->BombsUsed, Used);
        X.Test->TestNull(TEXT("No bomb is held after preview clicks"), X.Mode->GetBombForPlayer(X.Player));
        return true;
    });

    Step(TEXT("Charge tuning clamps invalid ranges and rejects nonfinite values"), [](FFixture& X)
    {
        struct FBound { const TCHAR* Id; float Min, Max; };
        const FBound Bounds[] = {
            {TEXT("ChargeSeconds"), .25f, 6.f}, {TEXT("MinThrowSpeed"), 0.f, 2.f}, {TEXT("MinThrowLift"), 0.f, .5f},
            {TEXT("ChargeThrowExponent"), .25f, 3.f}, {TEXT("ChargeRadiusExponent"), .25f, 3.f}, {TEXT("ChargeImpulseExponent"), .25f, 3.f},
            {TEXT("MaxChargeRadiusMultiplier"), 1.f, 3.f}, {TEXT("MaxChargeImpulseMultiplier"), 1.f, 3.f}, {TEXT("MaxChargeVisualScale"), 1.f, 2.f}
        };
        for (const auto& B : Bounds)
        {
            X.Session->SetParameter(B.Id, -100.f);
            X.Test->TestEqual(FString::Printf(TEXT("%s clamps to its lower bound"), B.Id), X.Session->GetParameter(B.Id), B.Min);
            X.Session->SetParameter(B.Id, 100.f);
            X.Test->TestEqual(FString::Printf(TEXT("%s clamps to its upper bound"), B.Id), X.Session->GetParameter(B.Id), B.Max);
            X.Session->SetParameter(B.Id, std::numeric_limits<float>::quiet_NaN());
            X.Test->TestEqual(FString::Printf(TEXT("%s ignores NaN"), B.Id), X.Session->GetParameter(B.Id), B.Max);
        }
        X.Session->ResetTuning(); X.Session->ToggleTuning();
        X.Test->TestTrue(TEXT("Restore defaults restores every preview"), X.Session->GetFeature(TEXT("trajectory"))
            && X.Session->GetFeature(TEXT("landingPoint")) && X.Session->GetFeature(TEXT("blastPreview")));
        X.Session->SetParameter(TEXT("ChargeSeconds"), .25f); X.Session->SetParameter(TEXT("FuseSeconds"), 1.5f);
        X.PlacePlayer(); X.Press();
        if (!X.RequireBomb()) return;
        X.Bomb->MakeBanana(); X.Scale = X.Bomb->Mesh->GetRelativeScale3D(); X.Fuse = X.Bomb->GetRemainingFuse();
        X.WorldMark = X.World->GetTimeSeconds();
    }, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.World->GetTimeSeconds() - X.WorldMark < .35f) return false;
        X.Test->TestEqual(TEXT("Banana bait does not accumulate bomb charge"), X.Bomb->GetChargeFraction(), 0.f);
        X.Test->TestTrue(TEXT("Banana mesh never grows with held duration"), X.Bomb->Mesh->GetRelativeScale3D().Equals(X.Scale, .001f));
        X.Test->TestEqual(TEXT("Banana fuse does not burn"), X.Bomb->GetRemainingFuse(), X.Fuse);
        X.Test->TestEqual(TEXT("Banana has no charged blast multiplier"), X.Bomb->GetExplosionProfile().BlastRadius, X.Bomb->GetProfile().BlastRadius);
        X.Test->TestTrue(TEXT("Banana preserves its established throw velocity"),
            X.Bomb->GetThrowVelocity(FVector::ForwardVector).Equals(FVector(520, 0, 270), .01f));
        X.ReleaseAndCheckLaunch(); X.ClearBombs(); X.PlacePlayer(); X.Press();
        if (!X.RequireBomb() || !X.Require(X.Mode->Crate != nullptr, TEXT("Blast fixture has its physical crate"))) return true;
        X.Position = X.Bomb->GetActorLocation() + FVector(450, 0, 0);
        X.Mode->Crate->Mesh->SetSimulatePhysics(true); X.Mode->Crate->Mesh->SetEnableGravity(false);
        X.Mode->Crate->SetActorLocation(X.Position, false, nullptr, ETeleportType::TeleportPhysics);
        X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        X.Mode->Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        X.WorldMark = X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("Continuing to hold reaches full charge but does not stop the live fuse"), {}, [](FFixture& X)
    {
        if (!X.RequireBomb()) return true;
        if (X.Bomb->GetChargeFraction() < 1.f) return false;
        X.Test->TestTrue(TEXT("Full-charge bomb stays in hand awaiting release or detonation"), X.Bomb->IsHeld());
        const float Distance = FVector::Dist(X.Bomb->GetActorLocation(), X.Mode->Crate->GetActorLocation());
        X.Test->TestTrue(TEXT("Crate is outside the base blast but inside the charged blast"),
            Distance > X.Bomb->GetProfile().BlastRadius * 100.f && Distance < X.Bomb->GetExplosionProfile().BlastRadius * 100.f);
        X.Fuse = X.Bomb->GetRemainingFuse();
        return true;
    });
    Step(TEXT("A fully charged held bomb detonates naturally and applies its expanded blast"), {}, [](FFixture& X)
    {
        if (X.Bomb.IsValid()) return false;
        // Wait for Chaos to apply the explosion impulse; the distant crate must
        // move even though an uncharged explosion cannot reach its centre.
        if (X.ExplosionFrame == 0) X.ExplosionFrame = GFrameCounter;
        if (GFrameCounter < X.ExplosionFrame + 2) return false;
        X.Test->TestTrue(TEXT("The original fuse expires while LMB remains held"),
            X.World->GetTimeSeconds() - X.WorldMark < 2.5f && X.Fuse > 0.f);
        X.Test->TestTrue(TEXT("Charged explosion physically propels cargo beyond the original radius"),
            X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().X > 100.f || X.Mode->Crate->GetActorLocation().X > X.Position.X + 5.f);
        X.Test->TestNull(TEXT("Detonation clears the player's live bomb"), X.Mode->GetBombForPlayer(X.Player));
        X.Mode->Crate->Mesh->SetEnableGravity(true); X.ClearBombs(); X.Session->ResetTuning();
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
