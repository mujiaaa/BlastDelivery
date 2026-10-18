#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAlphaPlayground.h"
#include "../BDAlphaSession.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDPlayerController.h"
#include "../BDLocalMultiplayer.h"
#include "../BDPuzzleActors.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "../BDAlphaViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"
#include "PhysicsEngine/BodyInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaDefaultsTest,"BlastDelivery.Alpha.DefaultContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaDefaultsTest::RunTest(const FString& Parameters)
{
    const FBDPhysicsSettings P;
    TestEqual(TEXT("Movement retains the verified Unity designer value"),P.MoveSpeed,5.2f);
    TestEqual(TEXT("Timed bomb fuse remains four seconds"),P.FuseSeconds,4.f);
    TestEqual(TEXT("Blast radius is kept in metres until simulation"),P.BlastRadius,3.6f);
    TestEqual(TEXT("Cargo mass remains thirty kilograms"),P.CrateMass,30.f);
    TestFalse(TEXT("Faster contact fuse is opt-in"),P.bFastCollisionFuse);
    TestFalse(TEXT("Goal assistance is opt-in"),P.bGoalAssist);
    TestEqual(TEXT("All twenty-three Unity playtest values are exposed"),UBDAlphaSession::Parameters().Num(),23);
    TestTrue(TEXT("Unity Y becomes Unreal Z, and depth preserves room orientation"),
        ABDAlphaPlaygroundGameMode::FromUnity(FVector(-4,.62f,1.6f)).Equals(FVector(-400,-160,62),.001f));
    TestTrue(TEXT("Dimensions stay positive when remapping axes"),
        ABDAlphaPlaygroundGameMode::SizeFromUnity(FVector(3.4f,3.f,5.3f)).Equals(FVector(340,530,300),.001f));
    TestEqual(TEXT("Half-radius explosion retains half its impulse"),ABDGameMode::BlastFalloff(180,360),.5f);
    TestEqual(TEXT("Outside the blast radius gets no impulse"),ABDGameMode::BlastFalloff(400,360),0.f);
    return true;
}

namespace BDAlphaTests
{
struct FFixture
{
    FAutomationTestBase* Test=nullptr;
    UWorld* World=nullptr;
    ABDAlphaPlaygroundGameMode* Mode=nullptr;
    ABDPlayerController* PC=nullptr;
    ABDCharacter* Player=nullptr;
    UBDAlphaSession* Session=nullptr;
    bool bFailed=false;
    double StepStart=0;
    float WorldMark=0;
    FVector Position;
    float Fuse=0;
    int32 BombCount=0;
    float Baseline=0;
    TWeakObjectPtr<ABDBomb> Bomb;
    TWeakObjectPtr<ABDBomb> OtherBomb;
    TWeakObjectPtr<ABDBarrier> TestWall;

    void Refresh()
    {
        World=GEditor->PlayWorld;
        if (!World) { Mode=nullptr; PC=nullptr; Player=nullptr; Session=nullptr; return; }
        Mode=World->GetAuthGameMode<ABDAlphaPlaygroundGameMode>();
        PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        Player=PC ? Cast<ABDCharacter>(PC->GetPawn()) : nullptr;
        Session=Cast<UBDAlphaSession>(UGameplayStatics::GetGameInstance(World));
    }
    bool Ready() const { return World && Mode && Mode->IsRoomReady() && PC && Player && Session; }
    bool After(double Seconds) const { return FPlatformTime::Seconds()-StepStart>=Seconds; }
    void Key(FKey Key,EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Event==IE_Released ? 0.f : 1.f));
        // Dispatch the actual bound action, then keep its key state for the
        // controller's release reconciliation. A direct callback cannot test this.
        PC->PlayerTick(PC->IsPaused() ? 0.f : 1.f/120.f);
    }
    void Aim(FVector Direction)
    {
        Player->FacePoint(Player->GetActorLocation()+Direction);
        FVector2D Screen;
        if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation()+Direction,Screen))
            PC->SetMouseLocation(FMath::RoundToInt(Screen.X),FMath::RoundToInt(Screen.Y));
    }
    void ClearBombs()
    {
        PC->ResetBombInput();
        for (TActorIterator<ABDBomb> It(World); It; ++It) It->Destroy();
        Bomb.Reset(); OtherBomb.Reset();
    }
    void PlacePlayer(FVector At)
    {
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    void PlaceCrate(FVector At)
    {
        Mode->Crate->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
        if (Mode->Crate->Mesh->IsSimulatingPhysics())
        {
            Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mode->Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
        Mode->Crate->Mesh->UpdateBounds();
    }
    void Shot(const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(),TEXT("BDAlphaScreenshots"))) return;
        if (!World->GetGameViewport() || !World->GetGameViewport()->Viewport) return;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Automation/AlphaShots");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FViewport* Viewport=World->GetGameViewport()->Viewport;
        TArray<FColor> Pixels;
        if (!Viewport->ReadPixels(Pixels)) return;
        const FIntPoint Size=Viewport->GetSizeXY();
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));
    }
};

class FBootPlayground : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    bool bRequested=false;
    double Started=FPlatformTime::Seconds();
public:
    explicit FBootPlayground(TSharedRef<FFixture> In):F(In) {}
    bool Update() override
    {
        F->Refresh();
        if (!bRequested && F->World && F->Session)
        {
            bRequested=true;
            F->Session->StartPlayground();
        }
        if (bRequested && F->Ready()) return true;
        if (FPlatformTime::Seconds()-Started>25.)
        {
            F->Test->AddError(TEXT("Alpha Playground did not open through its real session entry point"));
            F->bFailed=true;
            return true;
        }
        return false;
    }
};

using FAction=TFunction<void(FFixture&)>;
using FCondition=TFunction<bool(FFixture&)>;
class FStep : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Started=FPlatformTime::Seconds();
    bool bEntered=false;
public:
    FStep(TSharedRef<FFixture> In,FString Label,FAction Action,FCondition Condition)
        :F(In),Name(MoveTemp(Label)),Begin(MoveTemp(Action)),Until(MoveTemp(Condition)) {}
    bool Update() override
    {
        if (F->bFailed) return true;
        F->Refresh();
        if (!bEntered && F->Ready())
        {
            bEntered=true;
            Started=FPlatformTime::Seconds();
            F->StepStart=Started;
            F->Test->AddInfo(Name);
            UE_LOG(LogTemp,Display,TEXT("BD ALPHA TEST: %s"),*Name);
            if (Begin) Begin(*F);
        }
        if (bEntered && F->Ready() && Until(*F)) return true;
        if (FPlatformTime::Seconds()-Started>18.)
        {
            F->Test->AddError(TEXT("Timed out: ")+Name);
            F->bFailed=true;
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaPlaygroundTest,"BlastDelivery.Alpha.PlaygroundIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaPlaygroundTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>();
    F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBootPlayground>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };

    Step(TEXT("Real playground entry builds a floor, the pit, L wall and linked gate"),{},[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        X.Test->TestEqual(TEXT("Session identifies Playground"),X.Session->IsPlayground(),true);
        X.Test->TestTrue(TEXT("Player spawn uses the converted Unity location"),X.Player->GetSpawnPosition().Equals(FVector(-670,40,95),2.f));
        X.Test->TestTrue(TEXT("Crate spawn uses the converted Unity location"),X.Mode->Crate->GetSpawnPosition().Equals(FVector(-400,-160,62),2.f));
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(AlphaLayout),false);
        X.Test->TestTrue(TEXT("Courtyard supports actors"),X.World->LineTraceSingleByChannel(Hit,FVector(-550,0,300),FVector(-550,0,-600),ECC_WorldStatic,Query));
        X.Test->TestFalse(TEXT("Pit has no hidden supporting floor"),X.World->LineTraceSingleByChannel(Hit,FVector(330,535,300),FVector(330,535,-600),ECC_WorldStatic,Query));
        X.Test->TestTrue(TEXT("L wall blocks the middle route"),X.World->LineTraceSingleByChannel(Hit,FVector(-200,0,100),FVector(200,0,100),ECC_WorldStatic,Query));
        X.Test->TestTrue(TEXT("Closed gate blocks the upper route"),X.World->LineTraceSingleByChannel(Hit,FVector(-200,-585,100),FVector(200,-585,100),ECC_WorldStatic,Query));
        int32 PlateCount=0;
        for (TActorIterator<ABDPressurePlate> It(X.World); It; ++It)
        { ++PlateCount; X.Test->TestEqual(TEXT("Plate is linked to this gate"),It->Gate.Get(),X.Mode->Gate.Get()); }
        X.Test->TestEqual(TEXT("Exactly one puzzle plate"),PlateCount,1);
        X.Test->TestEqual(TEXT("Room has the default 30 kg cargo"),X.Mode->Crate->Mesh->BodyInstance.GetBodyMass(),30.f);
        X.Shot(TEXT("01-playground.png"));
        return true;
    });

    Step(TEXT("Playground reserves real camera space for the tuning sidebar"),{},[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        int32 W,H; X.PC->GetViewportSize(W,H);
        const float Fraction=1.f-350.f*FMath::Min(W/1440.f,H/900.f)/W;
        X.Test->TestTrue(TEXT("Local player viewport ends at sidebar edge"),FMath::IsNearlyEqual(float(X.PC->GetLocalPlayer()->Size.X),Fraction,.002f));
        X.Test->TestTrue(TEXT("Camera preserves Unity vertical framing"),FMath::IsNearlyEqual(X.PC->GetViewWidth(),FMath::Max(2240.f,2280.f*W*Fraction/H),2.f));
        FVector2D Center;
        X.PC->ProjectWorldLocationToScreen(FVector(0,-20,0),Center);
        X.Test->TestTrue(TEXT("Room center projects into the center of the usable playfield"),FMath::Abs(Center.X-W*Fraction*.5f)<3.f && FMath::Abs(Center.Y-H*.5f)<3.f);
        X.Session->ToggleHUD();
        return true;
    });
    Step(TEXT("Clean view restores the full camera viewport"),{},[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestTrue(TEXT("H restores the entire viewport"),FMath::IsNearlyEqual(float(X.PC->GetLocalPlayer()->Size.X),1.f,.002f));
        int32 W,H; X.PC->GetViewportSize(W,H);
        X.Test->TestTrue(TEXT("Clean view adjusts orthographic width to the full aspect"),FMath::IsNearlyEqual(X.PC->GetViewWidth(),FMath::Max(2240.f,2280.f*W/H),2.f));
        X.Session->ToggleHUD();
        return true;
    });

    Step(TEXT("All size sliders clamp and resize the actual actors without changing mass"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(-650,300,95));
        X.Session->SetParameter(TEXT("PlayerSize"),99.f);
        X.Session->SetParameter(TEXT("CrateSize"),99.f);
        X.Session->SetParameter(TEXT("BombSize"),-2.f);
        X.Test->TestEqual(TEXT("Player size maximum is 200 percent"),X.Session->GetParameter(TEXT("PlayerSize")),2.f);
        X.Test->TestEqual(TEXT("Crate size maximum is 200 percent"),X.Session->GetParameter(TEXT("CrateSize")),2.f);
        X.Test->TestEqual(TEXT("Bomb size minimum is 50 percent"),X.Session->GetParameter(TEXT("BombSize")),.5f);
    },[](FFixture& X)
    {
        if (!X.After(.15)) return false;
        X.Test->TestTrue(TEXT("Player collision radius doubles"),FMath::IsNearlyEqual(X.Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),80.f,.2f));
        X.Test->TestTrue(TEXT("Crate collision width doubles"),FMath::IsNearlyEqual(X.Mode->Crate->Mesh->Bounds.BoxExtent.X,120.f,2.f));
        X.Test->TestTrue(TEXT("Size preserves cargo mass"),FMath::IsNearlyEqual(X.Mode->Crate->Mesh->BodyInstance.GetBodyMass(),30.f,.1f));
        X.Session->ResetTuning();
        return true;
    });

    Step(TEXT("Menus pause simulation and preserve per-session Playground experiments"),[](FFixture& X)
    {
        X.Session->SetParameter(TEXT("CrateMass"),44.f);
        X.Session->SetParameter(TEXT("GoalAssistRange"),1.2f);
        X.Session->SetFeature(TEXT("fastFuse"),true);
        X.Session->SetFeature(TEXT("goalAssist"),true);
        X.WorldMark=X.World->GetTimeSeconds();
        X.Session->OpenSettings();
    },[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        X.Test->TestTrue(TEXT("Settings pauses game time"),FMath::IsNearlyEqual(X.World->GetTimeSeconds(),X.WorldMark,.03f));
        X.Test->TestTrue(TEXT("Settings preserves optional experiments"),X.Session->GetFeature(TEXT("fastFuse")) && X.Session->GetFeature(TEXT("goalAssist")));
        X.Test->TestEqual(TEXT("Settings preserves edited cargo mass"),X.Session->GetParameter(TEXT("CrateMass")),44.f);
        X.Shot(TEXT("02-settings.png"));
        X.Session->Back();
        X.Session->ResetTuning();
        X.Test->TestFalse(TEXT("Restore defaults clears collision fuse"),X.Session->GetFeature(TEXT("fastFuse")));
        X.Test->TestFalse(TEXT("Restore defaults clears goal assist"),X.Session->GetFeature(TEXT("goalAssist")));
        return true;
    });

    Step(TEXT("An off-centre explosion moves and rotates the real heavy crate"),[](FFixture& X)
    {
        X.PlaceCrate(FVector(-500,400,62));
        X.Position=X.Mode->Crate->GetActorLocation();
        X.Mode->ExplodeAt(FVector(-650,450,38),nullptr,nullptr);
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        // Loading the first explosion effect can spend >.2s on the render thread
        // without advancing physics. Observe simulated time, not asset-load time.
        if (X.World->GetTimeSeconds()-X.WorldMark<.2f) return false;
        X.Test->AddInfo(FString::Printf(TEXT("Blast observation: simulated=%.3f displacement=%.3f yaw=%.3f velocity=%s"),
            X.World->GetTimeSeconds()-X.WorldMark,FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position),
            X.Mode->Crate->GetActorRotation().Yaw,*X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().ToString()));
        X.Test->TestTrue(TEXT("Explosion produces measurable cargo movement"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position)>10.f);
        X.Test->TestTrue(TEXT("Off-centre blast produces yaw"),FMath::Abs(X.Mode->Crate->GetActorRotation().Yaw)>1.f);
        return true;
    });

    Step(TEXT("A solid wall shields a crate from a nearby explosion"),[](FFixture& X)
    {
        X.PlaceCrate(FVector(250,0,62));
        X.Position=X.Mode->Crate->GetActorLocation();
        X.Mode->ExplodeAt(FVector(-80,0,38),nullptr,nullptr);
    },[](FFixture& X)
    {
        if (!X.After(.25)) return false;
        X.Test->TestTrue(TEXT("Occluded cargo stays in place"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position)<2.f);
        return true;
    });

    Step(TEXT("A grounded player opens the gate through the actual pressure plate"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(275,-560,95));
    },[](FFixture& X)
    {
        if (!X.Mode->Gate->bOpen) return false;
        X.Test->TestEqual(TEXT("Opened gate stops blocking"),X.Mode->Gate->Collision->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        X.Player->Respawn();
        X.Mode->Crate->Respawn();
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("Independent actor respawns preserve the latched gate"),{},[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<1.f) return false;
        X.Test->AddInfo(FString::Printf(TEXT("Respawn observation: simulated=%.3f player=%s cargo=%s cargoSpawn=%s recovering=%d"),
            X.World->GetTimeSeconds()-X.WorldMark,*X.Player->GetActorLocation().ToString(),
            *X.Mode->Crate->GetActorLocation().ToString(),*X.Mode->Crate->GetSpawnPosition().ToString(),X.Mode->Crate->IsRecovering()));
        X.Test->TestTrue(TEXT("Gate remains open after both recoveries"),X.Mode->Gate->bOpen);
        X.Test->TestTrue(TEXT("Player recovers near its own spawn"),FVector::Dist2D(X.Player->GetActorLocation(),X.Player->GetSpawnPosition())<5.f);
        X.Test->TestTrue(TEXT("Cargo recovers near its own spawn"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Mode->Crate->GetSpawnPosition())<5.f);
        X.Shot(TEXT("03-gate-open.png"));
        return true;
    });

    Step(TEXT("Partial delivery never wins; a fully contained resting crate reaches results"),[](FFixture& X)
    {
        X.PlaceCrate(FVector(850,-560,62));
        X.Test->TestFalse(TEXT("Half-out cargo is not delivered"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
        X.PlaceCrate(FVector(700,-560,62));
        X.Test->TestTrue(TEXT("Fully inside cargo can qualify"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
    },[](FFixture& X)
    {
        if (!X.Mode->bCompleted) return false;
        X.Test->TestEqual(TEXT("Completion opens results"),X.Session->GetScreen(),EBDAlphaScreen::Results);
        X.Test->TestTrue(TEXT("Results freeze gameplay"),UGameplayStatics::IsGamePaused(X.World));
        X.Shot(TEXT("04-results.png"));
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaBombInputTest,"BlastDelivery.Alpha.BombAndInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaBombInputTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>();
    F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBootPlayground>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };

    Step(TEXT("Simple left press holds a burning bomb until the actual release input"),[](FFixture& X)
    {
        X.ClearBombs();
        X.Session->SetSimpleControls(true);
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.Session->SetParameter(TEXT("ThrowSpeed"),2.f);
        X.Session->SetParameter(TEXT("ThrowLift"),.5f);
        X.Session->SetParameter(TEXT("CollisionFuseMultiplier"),3.f);
        X.Session->SetFeature(TEXT("fastFuse"),true);
        X.PlacePlayer(FVector(-700,350,95));
        X.Aim(FVector(400,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Bomb=X.Mode->ActiveBomb;
        if (!X.Test->TestNotNull(TEXT("LMB creates a bomb"),X.Bomb.Get())) { X.bFailed=true; return; }
        X.Test->TestTrue(TEXT("Pressed key remains down for controller reconciliation"),X.PC->IsInputKeyDown(EKeys::LeftMouseButton));
        X.Test->TestTrue(TEXT("Simple press holds instead of immediately throwing"),X.Bomb->IsHeld());
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Held bomb disappeared before release")); X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Bomb stays in hands while key is held"),X.Bomb->IsHeld());
        X.Test->TestFalse(TEXT("Holding does not accelerate the fuse"),X.Bomb->HasCollisionFuseAccelerated());
        const float Burn=X.Fuse-X.Bomb->GetRemainingFuse();
        const float Time=X.World->GetTimeSeconds()-X.WorldMark;
        X.Test->TestTrue(TEXT("Held fuse burns at normal speed"),FMath::IsNearlyEqual(Burn,Time,.06f));
        return true;
    });

    Step(TEXT("Slider edits preserve a live bomb's profile and release uses its captured velocity"),[](FFixture& X)
    {
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.Session->SetParameter(TEXT("FuseSeconds"),7.f);
        X.Session->SetParameter(TEXT("ThrowSpeed"),12.f);
        X.Session->SetParameter(TEXT("BlastRadius"),6.f);
        X.Test->TestEqual(TEXT("Live bomb keeps its original fuse setting"),X.Bomb->GetProfile().FuseSeconds,8.f);
        X.Test->TestEqual(TEXT("Live bomb keeps its original throw setting"),X.Bomb->GetProfile().ThrowSpeed,2.f);
        X.Test->TestEqual(TEXT("Live bomb keeps its original blast setting"),X.Bomb->GetProfile().BlastRadius,3.6f);
        X.Test->TestTrue(TEXT("Editing does not refresh remaining fuse"),FMath::IsNearlyEqual(X.Fuse,X.Bomb->GetRemainingFuse(),.001f));
        X.Aim(FVector(400,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestFalse(TEXT("Released LMB lets go"),X.Bomb->IsHeld());
        X.Test->TestTrue(TEXT("Released LMB selects throw"),X.Bomb->WasThrown());
        const FVector Velocity=X.Bomb->Collision->GetPhysicsLinearVelocity();
        X.Test->TestTrue(TEXT("Real throw uses converted captured velocity"),FMath::IsNearlyEqual(Velocity.Size2D(),200.f,2.f)
            && FMath::IsNearlyEqual(Velocity.Z,50.f,2.f));
    },[](FFixture& X)
    {
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Bomb vanished before its first collision")); X.bFailed=true; return true; }
        if (!X.Bomb->HasCollisionFuseAccelerated()) return false;
        X.Test->TestEqual(TEXT("Actual first solid hit applies captured multiplier"),X.Bomb->GetFuseBurnRate(),3.f);
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("The collision-accelerated fuse actually burns three times faster"),{},[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.2f) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Accelerated bomb vanished too early")); X.bFailed=true; return true; }
        const float Rate=(X.Fuse-X.Bomb->GetRemainingFuse())/(X.World->GetTimeSeconds()-X.WorldMark);
        X.Test->TestTrue(TEXT("Observed fuse consumption matches multiplier"),FMath::IsNearlyEqual(Rate,3.f,.25f));
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.Session->OpenSettings();
        return true;
    });

    Step(TEXT("Settings pauses the same live accelerated fuse"),{},[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Paused bomb disappeared")); X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Paused fuse is preserved"),FMath::IsNearlyEqual(X.Fuse,X.Bomb->GetRemainingFuse(),.04f));
        X.Session->Back();
        return true;
    });

    Step(TEXT("Left press prioritizes a nearby bomb without spending another bomb or resetting its fuse"),[](FFixture& X)
    {
        FVector At=X.Bomb->GetActorLocation()-FVector(90,0,0); At.Z=95;
        X.PlacePlayer(At);
        X.Aim(FVector(400,0,0));
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.BombCount=X.Mode->BombsUsed;
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Test->TestEqual(TEXT("Pickup retains the same actor"),X.Mode->ActiveBomb.Get(),X.Bomb.Get());
        X.Test->TestTrue(TEXT("Nearby bomb is held after input"),X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Pickup does not increment usage"),X.Mode->BombsUsed,X.BombCount);
        X.Test->TestTrue(TEXT("Pickup preserves remaining fuse"),FMath::IsNearlyEqual(X.Fuse,X.Bomb->GetRemainingFuse(),.04f));
        X.Test->TestEqual(TEXT("Pickup retains accelerated burn rate"),X.Bomb->GetFuseBurnRate(),3.f);
    },[](FFixture& X)
    {
        if (!X.After(.15)) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Picked-up bomb vanished during short hold")); X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Pickup continues holding until release"),X.Bomb->IsHeld());
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestTrue(TEXT("Release rethrows the same bomb"),X.Bomb->WasThrown() && !X.Bomb->IsHeld());
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });

    Step(TEXT("A rethrown accelerated bomb does not stack the multiplier on another landing"),{},[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.85f) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Fuse expired earlier than the single multiplier allows")); X.bFailed=true; return true; }
        X.Test->TestEqual(TEXT("Repeat contact remains three times, never nine"),X.Bomb->GetFuseBurnRate(),3.f);
        X.ClearBombs();
        return true;
    });

    Step(TEXT("Simple right press holds and its release places without contact acceleration"),[](FFixture& X)
    {
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.Session->SetParameter(TEXT("ThrowSpeed"),5.2f);
        X.Session->SetParameter(TEXT("ThrowLift"),2.7f);
        X.Session->SetParameter(TEXT("BlastRadius"),3.6f);
        X.PlacePlayer(FVector(-700,350,95));
        X.Aim(FVector(400,0,0));
        X.Key(EKeys::RightMouseButton,IE_Pressed);
        X.Bomb=X.Mode->ActiveBomb;
        if (!X.Test->TestNotNull(TEXT("RMB creates a new bomb"),X.Bomb.Get())) { X.bFailed=true; return; }
        X.Test->TestTrue(TEXT("Simple right press holds"),X.Bomb->IsHeld());
        X.Key(EKeys::RightMouseButton,IE_Released);
        X.Test->TestFalse(TEXT("Right release places instead of throwing"),X.Bomb->WasThrown());
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Dropped bomb vanished unexpectedly")); X.bFailed=true; return true; }
        X.Test->TestFalse(TEXT("Dropping onto floor never triggers faster contact fuse"),X.Bomb->HasCollisionFuseAccelerated());
        X.Test->TestTrue(TEXT("Drop fuse still burns normally"),FMath::IsNearlyEqual(X.Fuse-X.Bomb->GetRemainingFuse(),X.World->GetTimeSeconds()-X.WorldMark,.06f));
        X.Test->TestTrue(TEXT("Dropped bomb is on the actual floor"),X.Bomb->GetActorLocation().Z<X.Bomb->GetWorldRadius()+8.f);
        return true;
    });

    Step(TEXT("Right press also picks up and holds the existing bomb; release places it again"),[](FFixture& X)
    {
        FVector At=X.Bomb->GetActorLocation()-FVector(90,0,0); At.Z=95;
        X.PlacePlayer(At);
        X.Aim(FVector(400,0,0));
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.BombCount=X.Mode->BombsUsed;
        X.Key(EKeys::RightMouseButton,IE_Pressed);
        X.Test->TestTrue(TEXT("RMB pickup holds the same actor"),X.Mode->ActiveBomb==X.Bomb.Get() && X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Right-button pickup does not spend another bomb"),X.Mode->BombsUsed,X.BombCount);
        X.Test->TestTrue(TEXT("Right-button pickup keeps the fuse"),FMath::IsNearlyEqual(X.Fuse,X.Bomb->GetRemainingFuse(),.04f));
    },[](FFixture& X)
    {
        if (!X.After(.1)) return false;
        X.Key(EKeys::RightMouseButton,IE_Released);
        X.Test->TestTrue(TEXT("Right release drops the picked-up actor"),X.Bomb.IsValid() && !X.Bomb->IsHeld() && !X.Bomb->WasThrown());
        X.ClearBombs();
        X.Session->ResetTuning();
        return true;
    });

    Step(TEXT("Advanced left click picks up a bomb and the next left click throws it"),[](FFixture& X)
    {
        X.ClearBombs();
        X.Session->SetSimpleControls(false);
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.PlacePlayer(FVector(-700,350,95));
        X.Aim(FVector(400,0,0));
        X.Bomb=X.Mode->DrawBomb(X.Player);
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Advanced pickup fixture could not create its bomb")); X.bFailed=true; return; }
        X.Bomb->Release(false,false);
        X.BombCount=X.Mode->BombsUsed;
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestTrue(TEXT("Advanced pickup stays held after that click"),X.Bomb->IsHeld());
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.1f) return false;
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Test->TestTrue(TEXT("A second Advanced left click throws without holding RMB"),X.Bomb.IsValid() && X.Bomb->WasThrown() && !X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Advanced catch and rethrow spend no extra bomb"),X.Mode->BombsUsed,X.BombCount);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.ClearBombs();
        return true;
    });

    Step(TEXT("Changing controls while paused preserves the original held gesture's release"),[](FFixture& X)
    {
        X.Session->SetSimpleControls(true);
        X.Aim(FVector(400,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Bomb=X.Mode->ActiveBomb;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Mode-change fixture did not hold a bomb")); X.bFailed=true; return; }
        X.BombCount=X.Mode->BombsUsed;
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.Session->OpenSettings();
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Session->SetSimpleControls(true); // Clicking the selected option is a no-op.
        X.Session->SetSimpleControls(false);
    },[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestTrue(TEXT("Paused mode change keeps the current bomb and fuse"),X.Bomb.IsValid() && X.Bomb->IsHeld()
            && FMath::IsNearlyEqual(X.Bomb->GetRemainingFuse(),X.Fuse,.04f));
        X.Session->Back();
        X.PC->PlayerTick(1.f/120.f);
        X.Test->TestTrue(TEXT("Resume completes the original Simple throw intent"),X.Bomb.IsValid() && X.Bomb->WasThrown() && !X.Bomb->IsHeld());
        X.Test->TestEqual(TEXT("Mode change creates no extra bomb"),X.Mode->BombsUsed,X.BombCount);
        X.ClearBombs();
        X.Session->SetSimpleControls(true);
        return true;
    });

    Step(TEXT("A real explosion displaces another live bomb without immediately detonating it"),[](FFixture& X)
    {
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.PlacePlayer(FVector(-700,-500,95));
        X.Aim(FVector(400,0,0));
        ABDBomb* Source=X.Mode->DrawBomb(X.Player);
        if (!Source) { X.Test->AddError(TEXT("Could not create source bomb")); X.bFailed=true; return; }
        Source->Release(false,false);
        Source->SetActorLocation(FVector(-700,350,42),false,nullptr,ETeleportType::TeleportPhysics);
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABDBomb* Other=X.World->SpawnActor<ABDBomb>(FVector(-550,350,42),FRotator::ZeroRotator,Spawn);
        Other->Ignite(X.Player,8.f);
        Other->Release(false,false);
        Other->SetActorLocation(FVector(-550,350,42),false,nullptr,ETeleportType::TeleportPhysics);
        Other->Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
        X.OtherBomb=Other;
        X.Position=Other->GetActorLocation();
        X.WorldMark=X.World->GetTimeSeconds();
        Source->Explode();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.05f) return false;
        if (!X.OtherBomb.IsValid()) { X.Test->AddError(TEXT("The neighbouring bomb chain-detonated or disappeared")); X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Actual blast changes another bomb's position"),FVector::Dist(X.OtherBomb->GetActorLocation(),X.Position)>10.f);
        X.Test->TestTrue(TEXT("Its own fuse remains live"),X.OtherBomb->GetRemainingFuse()>6.f);
        X.ClearBombs();
        X.Session->ResetTuning();
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaExperimentsTest,"BlastDelivery.Alpha.PlaytestExperiments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaExperimentsTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>();
    F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBootPlayground>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    auto MovingCargo=[](FFixture& X,FVector Position)
    {
        X.Mode->Crate->Mesh->SetSimulatePhysics(true);
        X.PlaceCrate(Position);
        X.Mode->Crate->SetActorRotation(FRotator::ZeroRotator,ETeleportType::TeleportPhysics);
        X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector(0,80,0));
        X.Mode->Crate->Mesh->WakeAllRigidBodies();
        X.Position=Position;
        X.WorldMark=X.World->GetTimeSeconds();
    };

    Step(TEXT("A half-size player holds and drops a double-size bomb clear of the floor"),[](FFixture& X)
    {
        X.ClearBombs();
        X.Session->ResetTuning();
        X.PlacePlayer(FVector(-700,350,95));
        X.Session->SetParameter(TEXT("PlayerSize"),.5f);
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.Aim(FVector(400,0,0));
        X.Bomb=X.Mode->DrawBomb(X.Player);
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Size test could not draw its bomb")); X.bFailed=true; return; }
        X.Test->TestTrue(TEXT("Double-size sphere radius is 76 cm"),FMath::IsNearlyEqual(X.Bomb->GetWorldRadius(),76.f,.01f));
        X.Test->TestTrue(TEXT("Small player does not hold the large sphere below ground"),X.Bomb->GetActorLocation().Z-X.Bomb->GetWorldRadius()>=-.5f);
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.Session->SetParameter(TEXT("BombSize"),.5f);
        X.Test->TestTrue(TEXT("Existing bomb sphere shrinks live to 19 cm"),FMath::IsNearlyEqual(X.Bomb->GetWorldRadius(),19.f,.01f));
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.Test->TestTrue(TEXT("Existing bomb sphere grows live to 76 cm"),FMath::IsNearlyEqual(X.Bomb->GetWorldRadius(),76.f,.01f));
        X.Test->TestTrue(TEXT("Live size edit preserves the existing fuse"),FMath::IsNearlyEqual(X.Fuse,X.Bomb->GetRemainingFuse(),.001f));
        X.Bomb->Release(false,false);
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.25f) return false;
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Large dropped bomb disappeared")); X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Dropped large bomb remains above supporting floor"),X.Bomb->GetActorLocation().Z-X.Bomb->GetWorldRadius()>=-1.f);
        X.Test->TestTrue(TEXT("Growing preserves the bomb's physical mass"),FMath::IsNearlyEqual(X.Bomb->Collision->BodyInstance.GetBodyMass(),.7f,.01f));
        return true;
    });

    Step(TEXT("Blocked bomb growth rolls back its collider and the session slider"),[](FFixture& X)
    {
        X.Session->SetParameter(TEXT("BombSize"),.5f);
        X.PlacePlayer(FVector(-700,-500,50));
        X.Bomb->SetActorLocation(FVector(-945,350,20),false,nullptr,ETeleportType::TeleportPhysics);
        X.Bomb->Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.Test->TestEqual(TEXT("Rejected growth keeps the slider's accepted value"),X.Session->GetParameter(TEXT("BombSize")),.5f);
        X.Test->TestTrue(TEXT("Rejected growth keeps the physical radius"),FMath::IsNearlyEqual(X.Bomb->GetWorldRadius(),19.f,.01f));
        X.ClearBombs();
        X.Session->ResetTuning();
    },[](FFixture& X) { return true; });

    Step(TEXT("A large held bomb resolves a wall-overlapping sweep start before release"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(-700,350,95));
        X.Session->SetParameter(TEXT("PlayerSize"),.5f);
        X.Session->SetParameter(TEXT("BombSize"),.5f);
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.PlacePlayer(FVector(-948,350,50));
        X.Aim(FVector(-400,0,0));
        X.Bomb=X.Mode->DrawBomb(X.Player);
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Held-wall fixture could not draw a bomb")); X.bFailed=true; return; }
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.Test->TestEqual(TEXT("Open side of the wall can accommodate the large bomb"),X.Session->GetParameter(TEXT("BombSize")),2.f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(AlphaHeldGrip),false,X.Bomb.Get());
        Query.AddIgnoredActor(X.Player);
        X.Test->TestFalse(TEXT("The resolved held sphere does not intersect the wall"),X.World->OverlapBlockingTestByChannel(
            X.Bomb->GetActorLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(X.Bomb->GetWorldRadius()-.5f),Query));
        X.Bomb->Release(false,false);
        X.Test->TestFalse(TEXT("Releasing the large sphere does not begin inside the wall"),X.World->OverlapBlockingTestByChannel(
            X.Bomb->GetActorLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(X.Bomb->GetWorldRadius()-.5f),Query));
        X.ClearBombs();
    },[](FFixture& X) { return true; });

    Step(TEXT("Held bomb growth is rejected when no collision-free grip fits"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(-600,350,50));
        X.Session->SetParameter(TEXT("BombSize"),.5f);
        X.Aim(FVector(0,400,0));
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABDBarrier* Left=X.World->SpawnActor<ABDBarrier>(FVector(-660,350,0),FRotator::ZeroRotator,Spawn);
        ABDBarrier* Right=X.World->SpawnActor<ABDBarrier>(FVector(-540,350,0),FRotator::ZeroRotator,Spawn);
        Left->Configure(FVector(20,400,100)); Right->Configure(FVector(20,400,100));
        X.Bomb=X.Mode->DrawBomb(X.Player);
        if (!X.Bomb.IsValid()) { Left->Destroy(); Right->Destroy(); X.Test->AddError(TEXT("Narrow-grip fixture could not draw the small bomb")); X.bFailed=true; return; }
        X.Fuse=X.Bomb->GetRemainingFuse();
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.Test->TestEqual(TEXT("No-fit held growth leaves slider unchanged"),X.Session->GetParameter(TEXT("BombSize")),.5f);
        X.Test->TestTrue(TEXT("No-fit held growth leaves collider and fuse unchanged"),FMath::IsNearlyEqual(X.Bomb->GetWorldRadius(),19.f,.01f)
            && FMath::IsNearlyEqual(X.Bomb->GetRemainingFuse(),X.Fuse,.001f));
        X.ClearBombs();
        X.Session->SetParameter(TEXT("BombSize"),2.f);
        X.BombCount=X.Mode->BombsUsed;
        X.Test->TestNull(TEXT("A new large bomb is rejected when no clear grip exists"),X.Mode->DrawBomb(X.Player));
        X.Test->TestNull(TEXT("A rejected draw leaves no active bomb"),X.Mode->ActiveBomb.Get());
        X.Test->TestEqual(TEXT("A rejected draw does not increase usage"),X.Mode->BombsUsed,X.BombCount);
        Left->Destroy(); Right->Destroy();
        X.Session->ResetTuning();
    },[](FFixture& X) { return true; });

    Step(TEXT("Walking into a resting crate cannot push it"),[](FFixture& X)
    {
        X.PlaceCrate(FVector(-450,350,61));
        X.Mode->Crate->Mesh->SetSimulatePhysics(false);
        X.Position=X.Mode->Crate->GetActorLocation();
        X.PlacePlayer(FVector(-750,350,95));
        X.Key(EKeys::D,IE_Pressed);
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<1.f) return false;
        X.Key(EKeys::D,IE_Released);
        X.Test->TestTrue(TEXT("Player reached the cargo's blocking face"),X.Player->GetActorLocation().X>-600.f);
        X.Test->TestTrue(TEXT("Walking contact leaves cargo fixed"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position)<1.f);
        return true;
    });

    Step(TEXT("A rolling bomb also cannot push resting heavy cargo"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(-700,-500,95));
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.Bomb=X.Mode->DrawBomb(X.Player);
        X.Bomb->Release(true,false);
        X.Bomb->SetActorLocation(FVector(-700,350,39),false,nullptr,ETeleportType::TeleportPhysics);
        X.Bomb->Collision->SetPhysicsLinearVelocity(FVector(600,0,0));
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.65f) return false;
        X.Test->TestTrue(TEXT("Rolling bomb hit the cargo and lost forward velocity"),X.Bomb.IsValid() && X.Bomb->Collision->GetPhysicsLinearVelocity().X<50.f);
        X.Test->TestTrue(TEXT("Bomb contact leaves cargo fixed"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position)<1.f);
        X.ClearBombs();
        X.Session->ResetTuning();
        return true;
    });

    Step(TEXT("Moving cargo physically rebounds from the L wall"),[](FFixture& X)
    {
        X.Mode->Crate->Mesh->SetSimulatePhysics(true);
        X.PlaceCrate(FVector(-230,0,61));
        X.Mode->Crate->SetActorRotation(FRotator::ZeroRotator,ETeleportType::TeleportPhysics);
        X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector(500,0,0));
        X.Mode->Crate->Mesh->WakeAllRigidBodies();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        const float Speed=X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().X;
        if (Speed>=-30.f && X.World->GetTimeSeconds()-X.WorldMark<1.2f) return false;
        X.Test->TestTrue(TEXT("A side impact reverses the incoming horizontal velocity"),Speed<-30.f);
        X.Test->TestTrue(TEXT("Wall rebound does not add translational energy"),X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size2D()<=501.f);
        return true;
    });

    Step(TEXT("Measure an unassisted moving crate near the goal"),[MovingCargo](FFixture& X)
    {
        X.Session->SetFeature(TEXT("goalAssist"),false);
        MovingCargo(X,FVector(470,-560,61));
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        X.Baseline=X.Mode->Crate->GetActorLocation().X-X.Position.X;
        return true;
    });

    Step(TEXT("Enabled goal assistance gently redirects the same moving crate inward"),[MovingCargo](FFixture& X)
    {
        X.Session->SetFeature(TEXT("goalAssist"),true);
        MovingCargo(X,FVector(470,-560,61));
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        const float Assisted=X.Mode->Crate->GetActorLocation().X-X.Position.X;
        X.Test->TestTrue(TEXT("Enabled assistance adds measurable inward displacement"),Assisted>X.Baseline+1.f);
        X.Test->TestTrue(TEXT("Assistance remains a small nudge rather than snapping into place"),Assisted<30.f);
        X.Test->AddInfo(FString::Printf(TEXT("Goal assist inward travel: disabled=%.3f cm, enabled=%.3f cm"),X.Baseline,Assisted));
        return true;
    });

    Step(TEXT("Goal assistance does not wake or pull resting cargo"),[](FFixture& X)
    {
        X.PlaceCrate(FVector(470,-560,61));
        X.Mode->Crate->Mesh->SetSimulatePhysics(false);
        X.Position=X.Mode->Crate->GetActorLocation();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        X.Test->TestFalse(TEXT("Assistance leaves the rest lock in place"),X.Mode->Crate->Mesh->IsSimulatingPhysics());
        X.Test->TestTrue(TEXT("Resting cargo receives no attraction"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Position)<.1f);
        return true;
    });

    Step(TEXT("Goal assistance refuses a route blocked by a wall"),[MovingCargo](FFixture& X)
    {
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        X.TestWall=X.World->SpawnActor<ABDBarrier>(FVector(535,-560,0),FRotator::ZeroRotator,Spawn);
        X.TestWall->Configure(FVector(20,300,100));
        MovingCargo(X,FVector(450,-560,61));
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        X.Test->TestTrue(TEXT("Blocked assistance adds no inward pull before the wall"),FMath::Abs(X.Mode->Crate->GetActorLocation().X-X.Position.X)<1.f);
        X.TestWall->Destroy(); X.TestWall.Reset();
        return true;
    });

    Step(TEXT("Goal assistance refuses to drag cargo across a gap in the floor"),[MovingCargo](FFixture& X)
    {
        // Move only the test goal temporarily. The actual room's hole and both
        // supporting platforms remain unchanged, so the safety check sees real terrain.
        X.Mode->Goal->SetActorLocation(FVector(600,535,120));
        X.Session->SetParameter(TEXT("GoalAssistRange"),3.f);
        MovingCargo(X,FVector(90,535,61));
    },[](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        X.Test->TestTrue(TEXT("Unsupported route adds no inward pull"),FMath::Abs(X.Mode->Crate->GetActorLocation().X-X.Position.X)<1.f);
        X.Mode->Goal->SetActorLocation(FVector(700,-560,120));
        X.PlaceCrate(FVector(-400,-160,62));
        X.Session->ResetTuning();
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaCharacterPresentationTest,"BlastDelivery.Alpha.CharacterPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaCharacterPresentationTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBootPlayground>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    auto Part=[](ABDCharacter* Character,const TCHAR* Name)->USceneComponent*
    {
        TInlineComponentArray<USceneComponent*> Components(Character);
        for (USceneComponent* Component:Components) if (Component->GetFName()==FName(Name)) return Component;
        return nullptr;
    };
    auto FrozenRoot=MakeShared<FTransform>();
    auto FrozenFoot=MakeShared<FTransform>();
    Step(TEXT("Walking animates Unity's visual hierarchy while retaining the physical capsule"),[Part](FFixture& X)
    {
        X.Session->ResetTuning(); X.ClearBombs();
        for (const TCHAR* Name:{TEXT("FacingVisual"),TEXT("Head"),TEXT("SoftFace"),TEXT("Tuft"),TEXT("LeftArm"),TEXT("RightArm"),TEXT("LeftFoot"),TEXT("RightFoot")})
            if (!X.Test->TestNotNull(Name,Part(X.Player,Name))) { X.bFailed=true; return; }
        X.Test->TestTrue(TEXT("Cream face matches the Unity proportions"),Part(X.Player,TEXT("SoftFace"))->GetRelativeScale3D().Equals(FVector(.27,.70,.55),.001f));
        TInlineComponentArray<UStaticMeshComponent*> Meshes(X.Player);
        for (UStaticMeshComponent* Mesh:Meshes)
            X.Test->TestEqual(TEXT("Presentation meshes never participate in collision"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        X.PlacePlayer(FVector(-750,350,95)); X.Aim(FVector(400,0,0));
        X.Position=X.Player->GetActorLocation(); X.WorldMark=X.World->GetTimeSeconds();
        X.Key(EKeys::D,IE_Pressed);
    },[Part](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.3f || X.Player->GetCharacterMovement()->IsFalling()) return false;
        const FVector L=Part(X.Player,TEXT("LeftFoot"))->GetRelativeLocation();
        const FVector R=Part(X.Player,TEXT("RightFoot"))->GetRelativeLocation();
        if (FMath::Abs(L.X)<2.f) return false;
        X.Test->TestTrue(TEXT("Actual movement advances the courier"),FVector::Dist2D(X.Player->GetActorLocation(),X.Position)>40.f);
        X.Test->TestTrue(TEXT("Feet alternate instead of sliding rigidly"),FMath::IsNearlyEqual(L.X,-R.X,.01f) && (L.Z>-75.f || R.Z>-75.f));
        X.Test->TestTrue(TEXT("Waddle and bounce affect the visual root"),Part(X.Player,TEXT("FacingVisual"))->GetRelativeLocation().Z>0.f
            && !Part(X.Player,TEXT("FacingVisual"))->GetRelativeRotation().IsNearlyZero(.1f));
        X.Test->TestEqual(TEXT("Walking animation preserves capsule radius"),X.Player->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),40.f);
        X.Test->TestEqual(TEXT("Walking animation preserves capsule half height"),X.Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),90.f);
        X.Test->TestTrue(TEXT("Animation never tilts the actor's physical up vector"),X.Player->GetActorUpVector().Equals(FVector::UpVector,.001f));
        X.Shot(TEXT("12-walking.png"));
        X.Key(EKeys::D,IE_Released);
        return true;
    });
    Step(TEXT("Pause freezes presentation and size changes scale its hierarchy once"),[Part,FrozenRoot,FrozenFoot](FFixture& X)
    {
        X.Session->OpenSettings();
        *FrozenRoot=Part(X.Player,TEXT("FacingVisual"))->GetRelativeTransform();
        *FrozenFoot=Part(X.Player,TEXT("LeftFoot"))->GetRelativeTransform();
        X.Position=X.Player->GetActorLocation();
    },[Part,FrozenRoot,FrozenFoot](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestTrue(TEXT("Paused body pose does not advance"),Part(X.Player,TEXT("FacingVisual"))->GetRelativeTransform().Equals(*FrozenRoot,.0001f));
        X.Test->TestTrue(TEXT("Paused feet do not advance"),Part(X.Player,TEXT("LeftFoot"))->GetRelativeTransform().Equals(*FrozenFoot,.0001f));
        X.Test->TestTrue(TEXT("Paused presentation does not move the capsule"),X.Player->GetActorLocation().Equals(X.Position,.001f));
        const FVector FullHead=Part(X.Player,TEXT("Head"))->GetComponentScale();
        X.Session->SetParameter(TEXT("PlayerSize"),.5f);
        X.Test->TestTrue(TEXT("Half-size scales the visible head exactly once"),Part(X.Player,TEXT("Head"))->GetComponentScale().Equals(FullHead*.5f,.001f));
        X.Test->TestEqual(TEXT("Half-size still uses the intended capsule"),X.Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),20.f);
        X.Session->SetParameter(TEXT("PlayerSize"),2.f);
        X.Test->TestTrue(TEXT("Double-size scales the visible head exactly once"),Part(X.Player,TEXT("Head"))->GetComponentScale().Equals(FullHead*2.f,.001f));
        X.Test->TestEqual(TEXT("Double-size still uses the intended capsule"),X.Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),80.f);
        X.Session->ResetTuning(); X.Session->Back();
        X.PlacePlayer(FVector(-700,350,95)); X.Aim(FVector(400,0,0));
        return true;
    });
    Step(TEXT("Holding and throwing update the hands without moving the physical grip"),[](FFixture& X)
    {
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.Bomb=X.Mode->DrawBomb(X.Player);
        if (!X.Bomb.IsValid()) { X.Test->AddError(TEXT("Pose fixture could not draw bomb")); X.bFailed=true; }
        X.WorldMark=X.World->GetTimeSeconds();
    },[Part](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.15f) return false;
        X.Test->TestTrue(TEXT("Holding brings both hands forward"),FMath::IsNearlyEqual(Part(X.Player,TEXT("LeftArm"))->GetRelativeLocation().X,57.f,.01f)
            && FMath::IsNearlyEqual(Part(X.Player,TEXT("RightArm"))->GetRelativeLocation().X,57.f,.01f));
        X.Test->TestTrue(TEXT("Visual posing leaves actual bomb grip unchanged"),X.Bomb.IsValid() && X.Bomb->GetActorLocation().Equals(X.Player->GetBombPosition(),.1f));
        X.Shot(TEXT("13-holding.png"));
        X.Player->ThrowBomb();
        return true;
    });
    Step(TEXT("Throw follow-through is a short visual-only pose"),[](FFixture& X) { X.WorldMark=X.World->GetTimeSeconds(); },[Part](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.04f) return false;
        X.Test->TestTrue(TEXT("Thrown bomb produces forward hand follow-through"),FMath::IsNearlyEqual(Part(X.Player,TEXT("LeftArm"))->GetRelativeLocation().X,60.f,.01f));
        X.Test->TestTrue(TEXT("Throw animation leaves physical actor upright"),X.Player->GetActorUpVector().Equals(FVector::UpVector,.001f));
        X.ClearBombs();
        X.Player->ApplyBlastVelocity(FVector(0,0,650));
        X.Position=X.Player->GetActorLocation();
        return true;
    });
    Step(TEXT("Blast flight spreads the arms; landing squashes only the model"),[](FFixture& X) { X.Baseline=0.f; },[Part](FFixture& X)
    {
        if (X.Baseline<.5f)
        {
            if (!X.Player->GetCharacterMovement()->IsFalling() || X.Player->GetActorLocation().Z<X.Position.Z+40.f) return false;
            X.Test->TestTrue(TEXT("Airborne arms spread outward"),FMath::IsNearlyEqual(Part(X.Player,TEXT("LeftArm"))->GetRelativeLocation().Y,-68.f,.01f)
                && FMath::IsNearlyEqual(Part(X.Player,TEXT("RightArm"))->GetRelativeLocation().Y,68.f,.01f));
            X.Baseline=1.f;
            X.Shot(TEXT("14-airborne.png"));
        }
        if (X.Player->GetCharacterMovement()->IsFalling() || Part(X.Player,TEXT("FacingVisual"))->GetRelativeScale3D().Z>.99f) return false;
        const FVector Scale=Part(X.Player,TEXT("FacingVisual"))->GetRelativeScale3D();
        X.Test->TestTrue(TEXT("Landing briefly squashes and widens the character"),Scale.Z<1.f && Scale.X>1.f && Scale.Y>1.f);
        X.Test->TestEqual(TEXT("Landing squash does not resize collision"),X.Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),90.f);
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });
    Step(TEXT("Landing and throw poses recover to neutral"),[](FFixture& X) { X.WorldMark=X.World->GetTimeSeconds(); },[Part](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.35f) return false;
        X.Test->TestTrue(TEXT("Squash settles back to normal proportions"),Part(X.Player,TEXT("FacingVisual"))->GetRelativeScale3D().Equals(FVector::OneVector,.001f));
        X.Test->TestTrue(TEXT("Hands return to their relaxed positions"),FMath::IsNearlyEqual(Part(X.Player,TEXT("LeftArm"))->GetRelativeLocation().X,12.f,.01f));
        X.Session->ResetTuning();
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaLocalMultiplayerTest,"BlastDelivery.Alpha.LocalMultiplayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaLocalMultiplayerTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBootPlayground>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    const FInputDeviceId Pad1=FInputDeviceId::CreateFromInternalId(1001);
    const FInputDeviceId Pad2=FInputDeviceId::CreateFromInternalId(1002);
    const FInputDeviceId Pad3=FInputDeviceId::CreateFromInternalId(1003);
    const FInputDeviceId Extra=FInputDeviceId::CreateFromInternalId(1004);
    auto Send=[Pad1](FFixture& X,FKey Key,float Value)
    {
        const auto Event=FInputKeyEventArgs::CreateSimulated(Key,IE_Axis,Value,1,Pad1);
        X.World->GetGameViewport()->InputAxis(Event);
    };
    auto Pad=MakeShared<TWeakObjectPtr<ABDCharacter>>();
    auto PrimaryPosition=MakeShared<FVector>();
    Step(TEXT("Gamepads join separate colored couriers without adding split-screen players"),[Pad1,Pad2,Pad3,Extra,Pad,PrimaryPosition](FFixture& X)
    {
        X.ClearBombs(); X.Session->ResetTuning(); X.Session->SetSimpleControls(true);
        auto* Router=X.PC->GetLocalMultiplayer();
        Router->SetDeviceConnected(Pad1,true); Router->SetDeviceConnected(Pad2,true); Router->SetDeviceConnected(Pad3,true); Router->SetDeviceConnected(Extra,true);
        *Pad=Router->GetPlayerForDevice(Pad1);
        if (!X.Test->TestNotNull(TEXT("First gamepad receives its own courier"),Pad->Get())) { X.bFailed=true; return; }
        X.Test->TestEqual(TEXT("Keyboard plus at most three pads"),Router->GetConnectedPlayerCount(),4);
        X.Test->TestNull(TEXT("Fifth player is not created"),Router->GetPlayerForDevice(Extra));
        X.Test->TestEqual(TEXT("Gamepad has a stable distinct suit seat"),Pad->Get()->GetLocalPlayerSeat(),1);
        X.Test->TestTrue(TEXT("Joining avoids the keyboard player's capsule"),FVector::Dist2D(Pad->Get()->GetActorLocation(),X.Player->GetActorLocation())>80.f);
        X.Test->TestEqual(TEXT("The viewport still has exactly one local player"),X.World->GetGameInstance()->GetNumLocalPlayers(),1);
        *PrimaryPosition=X.Player->GetActorLocation();
        X.WorldMark=X.World->GetTimeSeconds();
    },[](FFixture& X) { return X.World->GetTimeSeconds()-X.WorldMark>.15f; });
    Step(TEXT("Device-specific sticks move and aim only the joined player"),[Pad,Send,PrimaryPosition](FFixture& X)
    {
        X.Position=Pad->Get()->GetActorLocation(); *PrimaryPosition=X.Player->GetActorLocation();
        Send(X,EKeys::Gamepad_LeftX,1.f); Send(X,EKeys::Gamepad_RightY,1.f);
        X.WorldMark=X.World->GetTimeSeconds();
    },[Pad,Send,PrimaryPosition](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.3f) return false;
        X.Test->TestTrue(TEXT("Left stick moves the extra courier"),Pad->Get()->GetActorLocation().X>X.Position.X+30.f);
        X.Test->TestTrue(TEXT("Right stick aims independently of movement"),FVector::DotProduct(Pad->Get()->GetActorForwardVector(),FVector(0,-1,0))>.95f);
        X.Test->TestTrue(TEXT("Gamepad does not move the keyboard courier"),FVector::Dist2D(X.Player->GetActorLocation(),*PrimaryPosition)<1.f);
        Send(X,EKeys::Gamepad_LeftX,0.f); Send(X,EKeys::Gamepad_RightY,0.f);
        return true;
    });
    Step(TEXT("Each courier can hold its own live bomb; RT release throws only its owner"),[Pad,Send](FFixture& X)
    {
        X.Session->SetParameter(TEXT("FuseSeconds"),8.f);
        X.BombCount=X.Mode->BombsUsed;
        X.Bomb=X.Mode->DrawBomb(X.Player);
        Send(X,EKeys::Gamepad_RightTriggerAxis,1.f);
        X.WorldMark=X.World->GetTimeSeconds();
    },[Pad,Send](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.1f) return false;
        X.OtherBomb=X.Mode->GetBombForPlayer(Pad->Get());
        if (!X.Test->TestNotNull(TEXT("RT press holds the gamepad's bomb"),X.OtherBomb.Get())) { X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Both independently owned bombs remain held"),X.Bomb.IsValid() && X.Bomb->GetHolder()==X.Player && X.OtherBomb->GetHolder()==Pad->Get());
        X.Test->TestTrue(TEXT("Legacy ActiveBomb stays on keyboard player"),X.Mode->ActiveBomb==X.Bomb.Get());
        X.Test->TestEqual(TEXT("Global scoring counts both newly created bombs"),X.Mode->BombsUsed,X.BombCount+2);
        Send(X,EKeys::Gamepad_RightTriggerAxis,0.f);
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });
    Step(TEXT("Passing transfers ownership and preserves a bomb's live fuse"),[](FFixture& X) {},[Pad](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.05f) return false;
        X.Test->TestTrue(TEXT("RT release throws only the gamepad bomb"),X.OtherBomb.IsValid() && X.OtherBomb->WasThrown() && !X.OtherBomb->IsHeld() && X.Bomb->IsHeld());
        X.Bomb->Release(false,false);
        X.Bomb->SetActorLocation(FVector(-800,-500,42),false,nullptr,ETeleportType::TeleportPhysics);
        X.OtherBomb->SetActorLocation(X.Player->GetActorLocation()+FVector(90,0,0),false,nullptr,ETeleportType::TeleportPhysics);
        const float Fuse=X.OtherBomb->GetRemainingFuse(); const int32 Used=X.Mode->BombsUsed;
        X.Test->TestTrue(TEXT("Keyboard courier catches the other player's existing actor"),X.Mode->DrawBomb(X.Player)==X.OtherBomb.Get());
        X.Test->TestEqual(TEXT("Catch transfers the old owner's live-bomb budget"),X.Mode->GetOwnedBombCount(Pad->Get()),0);
        X.Test->TestEqual(TEXT("Catching while another owned bomb is flying keeps both owned"),X.Mode->GetOwnedBombCount(X.Player),2);
        X.Test->TestTrue(TEXT("Catch never refreshes the fuse"),FMath::IsNearlyEqual(X.OtherBomb->GetRemainingFuse(),Fuse,.001f));
        X.Test->TestEqual(TEXT("Passing does not increase bomb usage"),X.Mode->BombsUsed,Used);
        X.Test->TestNotNull(TEXT("Previous owner can now draw another bomb"),X.Mode->DrawBomb(Pad->Get()));
        return true;
    });
    Step(TEXT("Disconnect and rejoin preserve seat identity without resetting the level"),[Pad1,Pad,PrimaryPosition](FFixture& X)
    {
        *PrimaryPosition=X.Player->GetActorLocation();
        X.PC->GetLocalMultiplayer()->SetDeviceConnected(Pad1,false);
        X.Test->TestTrue(TEXT("Disconnected courier becomes hidden and non-colliding"),Pad->Get()->IsHidden() && !Pad->Get()->GetActorEnableCollision());
        X.Test->TestTrue(TEXT("Other player's caught bomb survives disconnect"),X.OtherBomb.IsValid() && X.OtherBomb->GetHolder()==X.Player);
        X.Test->TestNull(TEXT("Disconnected player's held bomb is cancelled"),X.Mode->GetBombForPlayer(Pad->Get()));
        X.PC->GetLocalMultiplayer()->SetDeviceConnected(Pad1,true);
        X.Test->TestTrue(TEXT("Rejoining uses the same courier and suit"),X.PC->GetLocalMultiplayer()->GetPlayerForDevice(Pad1)==Pad->Get() && Pad->Get()->GetLocalPlayerSeat()==1);
        X.Test->TestTrue(TEXT("Rejoined courier is active again"),!Pad->Get()->IsHidden() && Pad->Get()->GetActorEnableCollision());
        X.ClearBombs();
        Pad->Get()->SetActorLocation(FVector(-650,300,-800),false,nullptr,ETeleportType::TeleportPhysics);
        X.Baseline=0.f; X.WorldMark=X.World->GetTimeSeconds();
    },[Pad,PrimaryPosition](FFixture& X)
    {
        if (Pad->Get()->IsRecovering()) X.Baseline=1.f;
        if (X.World->GetTimeSeconds()-X.WorldMark<.8f || Pad->Get()->IsRecovering()) return false;
        X.Test->TestEqual(TEXT("Only the fallen courier entered recovery"),X.Baseline,1.f);
        X.Test->TestTrue(TEXT("Gamepad courier respawns at its own safe spawn"),FVector::Dist2D(Pad->Get()->GetActorLocation(),Pad->Get()->GetSpawnPosition())<1.f);
        X.Test->TestTrue(TEXT("Other courier and run remain intact"),FVector::Dist2D(X.Player->GetActorLocation(),*PrimaryPosition)<1.f && !X.Mode->bCompleted);
        return true;
    });
    Step(TEXT("Advanced LT holds and RT press throws without waiting for RT release"),[Pad,Send](FFixture& X)
    {
        X.Session->SetSimpleControls(false);
        Send(X,EKeys::Gamepad_LeftTriggerAxis,1.f);
        X.WorldMark=X.World->GetTimeSeconds();
    },[Pad,Send](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.1f) return false;
        X.Bomb=X.Mode->GetBombForPlayer(Pad->Get());
        if (!X.Test->TestNotNull(TEXT("Advanced LT readies its bomb"),X.Bomb.Get())) { X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Advanced LT keeps bomb in hand"),X.Bomb->IsHeld());
        Send(X,EKeys::Gamepad_RightTriggerAxis,1.f);
        X.WorldMark=X.World->GetTimeSeconds();
        return true;
    });
    Step(TEXT("Complete gamepad-input regression cleanup"),[](FFixture& X) {},[Pad1,Pad2,Pad3,Send](FFixture& X)
    {
        if (X.World->GetTimeSeconds()-X.WorldMark<.05f) return false;
        X.Test->TestTrue(TEXT("Advanced RT press throws immediately"),X.Bomb.IsValid() && X.Bomb->WasThrown() && !X.Bomb->IsHeld());
        Send(X,EKeys::Gamepad_LeftTriggerAxis,0.f); Send(X,EKeys::Gamepad_RightTriggerAxis,0.f);
        X.ClearBombs();
        auto* Router=X.PC->GetLocalMultiplayer();
        Router->SetDeviceConnected(Pad1,false); Router->SetDeviceConnected(Pad2,false); Router->SetDeviceConnected(Pad3,false);
        X.Session->SetSimpleControls(true); X.Session->ResetTuning();
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
