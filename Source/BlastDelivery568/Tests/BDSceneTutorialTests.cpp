#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDTutorialGameMode.h"
#include "../BDTutorialHUD.h"
#include "../BDPlayerController.h"
#include "../BDCharacter.h"
#include "../BDBomb.h"
#include "../BDCrate.h"
#include "../BDPuzzleActors.h"
#include "../BDAlphaSession.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"

namespace BDSceneTests
{
struct FFixture
{
    FAutomationTestBase* Test = nullptr;
    UWorld* World = nullptr;
    ABDTutorialGameMode* Mode = nullptr;
    ABDPlayerController* PC = nullptr;
    ABDCharacter* Player = nullptr;
    double StepStart = 0;
    float WorldMark = 0;
    float ExpectedExplosionAt = 0;
    float SavedFuse = 0;
    FVector SavedPosition;
    TWeakObjectPtr<ABDCrate> PreviousCrate;
    bool bRetreatBraking = false;
    bool bFailed = false;
    void Refresh()
    {
        World = GEditor->PlayWorld;
        if (!World) return;
        Mode = World->GetAuthGameMode<ABDTutorialGameMode>();
        PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        Player = PC ? Cast<ABDCharacter>(PC->GetPawn()) : nullptr;
    }
    bool After(double Seconds) const { return FPlatformTime::Seconds()-StepStart >= Seconds; }
    void Key(FKey Key, EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Event==IE_Released ? 0.f : 1.f));
        // InputKey queues events. Dispatch a controller tick before inspecting state.
        // A zero delta means paused to APlayerController and suppresses press binds.
        PC->PlayerTick(PC->IsPaused() ? 0.f : 1.f/120.f);
    }
    void Aim(FVector Direction)
    {
        Player->FacePoint(Player->GetActorLocation()+Direction);
        FVector2D P;
        if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation()+Direction,P))
            PC->SetMouseLocation(FMath::RoundToInt(P.X),FMath::RoundToInt(P.Y));
    }
    void PlacePlayer(FVector Local)
    {
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(Mode->GetSceneOrigin()+Local,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    void SetFuse(float Seconds)
    {
        FBDPhysicsSettings Settings=Mode->GetPhysicsSettings();
        Settings.FuseSeconds=Seconds;
        Test->TestTrue(TEXT("Physics profile update accepted"),Mode->ApplyPhysicsSettings(Settings));
    }
    void RestartSceneSequence()
    {
        // Test the five-stage state machine directly; session Replay intentionally reloads the map.
        if (auto* Session=World->GetGameInstance<UBDAlphaSession>()) Session->SetScreen(EBDAlphaScreen::Playing);
        Mode->RestartTutorial();
    }
    void DescribeScene(const TCHAR* Label)
    {
        if (!Mode || !Player) return;
        const FVector Origin=Mode->GetSceneOrigin();
        Test->AddInfo(FString::Printf(TEXT("%s: Scene=%d Phase=%d Origin=%s PlayerLocal=%s PlayerVelocity=%s BombLocal=%s"),
            Label,Mode->SceneIndex,int32(Mode->Phase),*Origin.ToString(),
            *(Player->GetActorLocation()-Origin).ToString(),*Player->GetVelocity().ToString(),
            IsValid(Mode->ActiveBomb) ? *(Mode->ActiveBomb->GetActorLocation()-Origin).ToString() : TEXT("none")));
        if (IsValid(Mode->Crate))
        {
            FVector Center, Extent;
            Mode->Crate->GetActorBounds(false,Center,Extent);
            Test->AddInfo(FString::Printf(TEXT("Crate=%s Local=%s Velocity=%s AngularVelocity=%s BoundsCenterLocal=%s BoundsExtent=%s OriginalCrate=%d"),
                *Mode->Crate->GetName(),*(Mode->Crate->GetActorLocation()-Origin).ToString(),
                *Mode->Crate->Mesh->GetPhysicsLinearVelocity().ToString(),
                *Mode->Crate->Mesh->GetPhysicsAngularVelocityInRadians().ToString(),
                *(Center-Origin).ToString(),*Extent.ToString(),Mode->Crate==PreviousCrate.Get()));
        }
        if (IsValid(Mode->Goal)) Test->AddInfo(FString::Printf(TEXT("GoalLocal=%s Extent=%s Contains=%d Settled=%d"),
            *(Mode->Goal->GetActorLocation()-Origin).ToString(),*Mode->Goal->Extent.ToString(),
            Mode->Goal->ContainsCrate(Mode->Crate),Mode->Goal->ContainsSettledCrate(Mode->Crate)));
    }
    void Shot(const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(),TEXT("BDTutorialScreenshots"))) return;
        const FString Folder = FPaths::ProjectSavedDir()/TEXT("Automation/SceneShots");
        IFileManager::Get().MakeDirectory(*Folder,true);
        // Capture this game viewport explicitly: the editor also renders viewports
        // and may consume a global FScreenshotRequest before PIE does.
        FViewport* Viewport = World->GetGameViewport()->Viewport;
        TArray<FColor> Pixels;
        if (Viewport && Viewport->ReadPixels(Pixels))
        {
            const FIntPoint Size = Viewport->GetSizeXY();
            TArray64<uint8> PNG;
            FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));
        }
    }
    template<class T> int32 Count() const
    {
        int32 N=0;
        for (TActorIterator<T> It(World); It; ++It) ++N;
        return N;
    }
};
using FAction = TFunction<void(FFixture&)>;
using FCondition = TFunction<bool(FFixture&)>;
class FStep : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Started = FPlatformTime::Seconds();
    bool bEntered = false;
public:
    FStep(TSharedRef<FFixture> InF, FString InName, FAction InBegin, FCondition InUntil)
        : F(InF),Name(MoveTemp(InName)),Begin(MoveTemp(InBegin)),Until(MoveTemp(InUntil)) {}
    virtual bool Update() override
    {
        if (F->bFailed) return true;
        F->Refresh();
        if (!bEntered && F->World && F->Mode && F->PC && F->Player)
        {
            bEntered = true;
            Started = FPlatformTime::Seconds();
            F->StepStart = Started;
            F->Test->AddInfo(Name);
            UE_LOG(LogTemp,Display,TEXT("BD SCENE TEST: %s"),*Name);
            if (Begin) Begin(*F);
        }
        if (bEntered && F->World && F->Mode && F->PC && F->Player && Until(*F)) return true;
        if (FPlatformTime::Seconds()-Started > 15.)
        {
            F->Test->AddError(TEXT("Timed out: ")+Name);
            F->DescribeScene(TEXT("Timeout"));
            F->bFailed = true;
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDFiveSceneTest,"BlastDelivery.FiveScenes.Playthrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDFiveSceneTest::RunTest(const FString& Parameters)
{
    using namespace BDSceneTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F = MakeShared<FFixture>();
    F->Test = this;
    auto Step = [&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    auto ObserveBombBeforeSliding = [&](int32 Index)
    {
        Step(TEXT("Keep successful scene visible until its real bomb detonates"),{},[Index](FFixture& X)
        {
            if (X.Mode->SceneIndex!=Index || X.Mode->Phase!=EBDTutorialPhase::Success)
            {
                X.Test->AddError(TEXT("Scene advanced before the successful bomb exploded"));
                X.bFailed = true;
                return true;
            }
            if (IsValid(X.Mode->ActiveBomb)) return false;
            ABDBlastFlash* Explosion = nullptr;
            for (TActorIterator<ABDBlastFlash> It(X.World); It; ++It) { Explosion=*It; break; }
            if (!X.Test->TestNotNull(TEXT("Actual explosion, not cleanup"),Explosion))
            {
                X.bFailed = true;
                return true;
            }
            X.WorldMark = X.World->GetTimeSeconds()-Explosion->GetGameTimeSinceCreation();
            X.Test->TestTrue(TEXT("Bomb finished its remaining fuse"),X.WorldMark+.02f >= X.ExpectedExplosionAt);
            X.Test->TestTrue(TEXT("Success animation clock did not restart on explosion"),X.Mode->GetPhaseTime()>.5f);
            X.Shot(Index==0 ? TEXT("01-throw-explosion.png") : TEXT("05-put-down-explosion.png"));
            return true;
        });
        Step(TEXT("Hold the scene after the explosion before sliding"),{},[Index](FFixture& X)
        {
            if (X.World->GetTimeSeconds()-X.WorldMark < .25f) return false;
            X.Test->TestEqual(TEXT("Checkmark still showing after explosion"),X.Mode->Phase,EBDTutorialPhase::Success);
            X.Test->TestEqual(TEXT("Current scene is still visible"),X.Mode->SceneIndex,Index);
            return true;
        });
        Step(TEXT("Slide begins 0.5 seconds after explosion and blocks gameplay input"),{},[Index](FFixture& X)
        {
            // Observe the transition as soon as it starts; waiting for a screenshot
            // fraction can miss this short phase when an offscreen render hitches.
            if (X.Mode->Phase != EBDTutorialPhase::Sliding) return false;
            X.Test->TestTrue(TEXT("Post-explosion delay respected"),X.World->GetTimeSeconds()-X.WorldMark >= .5f);
            X.Test->TestEqual(TEXT("Sliding from the correct scene"),X.Mode->SceneIndex,Index);
            X.Test->TestFalse(TEXT("Input disabled"),X.Mode->IsGameplayInputAllowed());
            X.Key(EKeys::LeftMouseButton,IE_DoubleClick);
            X.Key(EKeys::LeftMouseButton,IE_Released);
            X.Test->TestNull(TEXT("No bomb during transition"),X.Mode->ActiveBomb.Get());
            if (Index==0) X.Shot(TEXT("03-slide.png"));
            return true;
        });
    };

    Step(TEXT("Scene one is a clean playable ground with the wordless HUD"),[](FFixture& X)
    {
        // The new entry corridor has its own visibility/persistence playthrough.
        // Exercise the five retained lessons independently of that prerequisite.
        X.Mode->SceneOrder = {EBDTutorialScene::Throw,EBDTutorialScene::MoveBox,EBDTutorialScene::PutDown,EBDTutorialScene::CrossGap,EBDTutorialScene::Deliver};
        X.Mode->RestartTutorial();
        X.World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(1280,800);
        if (auto* Session=X.World->GetGameInstance<UBDAlphaSession>())
        {
            Session->SetSimpleControls(true);
            Session->SetScreen(EBDAlphaScreen::Playing);
        }
    },[](FFixture& X)
    {
        if (!X.After(.6)) return false;
        X.PC->SetActorTickEnabled(false); // Deterministic aiming; input callbacks still run.
        X.Test->TestEqual(TEXT("Five configured scenes"),X.Mode->SceneOrder.Num(),5);
        X.Test->TestEqual(TEXT("First scene"),X.Mode->GetScene(),EBDTutorialScene::Throw);
        X.Test->TestEqual(TEXT("One actual player"),X.Count<ABDCharacter>(),1);
        X.Test->TestNull(TEXT("No crate"),X.Mode->Crate.Get());
        X.Test->TestNull(TEXT("No goal"),X.Mode->Goal.Get());
        X.Test->TestTrue(TEXT("Wordless HUD selected"),X.PC->GetHUD()->IsA<ABDTutorialHUD>());
        X.Test->TestEqual(TEXT("Shared Alpha fuse is four seconds"),X.Mode->GetPhysicsSettings().FuseSeconds,4.f);
        X.Shot(TEXT("01-throw.png"));
        return true;
    });
    Step(TEXT("Simple LMB press holds without throwing or completing the lesson"),[](FFixture& X)
    {
        X.Aim(FVector(500,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
    },[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestTrue(TEXT("Left button keeps the bomb held"),X.Mode->ActiveBomb && X.Mode->ActiveBomb->IsHeld());
        X.Test->TestEqual(TEXT("Pressing alone does not complete throw"),X.Mode->Phase,EBDTutorialPhase::Playing);
        X.Test->TestEqual(TEXT("Only one bomb created while held"),X.Mode->BombsUsed,1);
        return true;
    });
    Step(TEXT("Simple LMB release throws the same burning bomb"),[](FFixture& X)
    {
        X.SavedFuse=X.Mode->ActiveBomb ? X.Mode->ActiveBomb->GetRemainingFuse() : 0.f;
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.WorldMark = X.World->GetTimeSeconds();
        X.Test->TestEqual(TEXT("One successful throw"),X.Mode->BombsUsed,1);
        X.Test->TestTrue(TEXT("Success follows valid throw"),X.Mode->Phase == EBDTutorialPhase::Success);
        X.Test->TestTrue(TEXT("Bomb is in flight"),X.Mode->ActiveBomb && !X.Mode->ActiveBomb->IsHeld());
        if (X.Mode->ActiveBomb) X.Test->TestTrue(TEXT("Release preserves remaining fuse"),X.Mode->ActiveBomb->GetRemainingFuse()<=X.SavedFuse);
        if (X.Mode->ActiveBomb) X.ExpectedExplosionAt = X.WorldMark+X.Mode->ActiveBomb->GetRemainingFuse();
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Test->TestEqual(TEXT("Feedback blocks additional input"),X.Mode->BombsUsed,1);
    },[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestEqual(TEXT("Immediate checkmark remains while fuse burns"),X.Mode->Phase,EBDTutorialPhase::Success);
        X.Shot(TEXT("02-success.png"));
        return true;
    });
    ObserveBombBeforeSliding(0);
    Step(TEXT("Scene two rejects movement without an explosion"),{},[](FFixture& X)
    {
        if (X.Mode->SceneIndex!=1 || !X.Mode->IsGameplayInputAllowed()) return false;
        X.Test->TestEqual(TEXT("Old player cleaned up"),X.Count<ABDCharacter>(),1);
        X.Test->TestNotNull(TEXT("One crate"),X.Mode->Crate.Get());
        X.Test->TestNull(TEXT("No goal"),X.Mode->Goal.Get());
        X.Shot(TEXT("04-move-box.png"));
        X.Mode->Crate->SetActorLocation(X.Mode->Crate->GetActorLocation()+FVector(50,0,0),false,nullptr,ETeleportType::TeleportPhysics);
        return true;
    });
    Step(TEXT("Unrelated displacement never completes scene two"),{},[](FFixture& X)
    {
        if (!X.After(.25)) return false;
        X.Test->TestEqual(TEXT("Still waiting for an explosion"),X.Mode->Phase,EBDTutorialPhase::Playing);
        X.Mode->RequestSceneReset();
        X.PreviousCrate = X.Mode->Crate;
        return true;
    });
    Step(TEXT("Scene reset restores crate and removes previous actors"),{},[](FFixture& X)
    {
        if (X.Mode->Crate==X.PreviousCrate.Get() || !X.After(.3)) return false;
        X.Test->TestEqual(TEXT("Exactly one fresh crate"),X.Count<ABDCrate>(),1);
        X.Test->TestTrue(TEXT("Crate reset to start"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Mode->GetSceneOrigin()+FVector(150,0,63))<1);
        X.Aim(FVector(500,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        return true;
    });
    Step(TEXT("Charge long enough to reach the scene-two box"),{},[](FFixture& X)
    {
        if (!X.After(1.f)) return false;
        X.Key(EKeys::LeftMouseButton,IE_Released);
        return true;
    });
    Step(TEXT("A real thrown bomb explodes and moves the box"),{},[](FFixture& X)
    {
        if (X.Mode->Phase != EBDTutorialPhase::Success) return false;
        X.Test->TestTrue(TEXT("Physical crate movement"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Mode->Crate->GetSpawnPosition())>=X.Mode->BoxMoveDistance);
        return true;
    });
    Step(TEXT("Scene three clears the box and requires hold then ground placement"),{},[](FFixture& X)
    {
        if (X.Mode->SceneIndex!=2 || !X.Mode->IsGameplayInputAllowed()) return false;
        X.Test->TestEqual(TEXT("No boxes remain"),X.Count<ABDCrate>(),0);
        X.Shot(TEXT("05-put-down.png"));
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestEqual(TEXT("Release without hold does not succeed"),X.Mode->Phase,EBDTutorialPhase::Playing);
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        return true;
    });
    Step(TEXT("Holding alone does not complete placement; pause preserves fuse"),{},[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestTrue(TEXT("LMB holds a bomb"),X.Mode->ActiveBomb && X.Mode->ActiveBomb->IsHeld());
        X.Test->TestEqual(TEXT("Holding is not putting down"),X.Mode->Phase,EBDTutorialPhase::Playing);
        if (!X.Mode->ActiveBomb) { X.bFailed=true; return true; }
        X.SavedFuse = X.Mode->ActiveBomb->GetRemainingFuse();
        X.PC->TogglePause();
        return true;
    });
    Step(TEXT("Release during pause is reconciled on resume"),{},[](FFixture& X)
    {
        if (!X.After(.2)) return false;
        X.Test->TestEqual(TEXT("Paused fuse"),X.Mode->ActiveBomb->GetRemainingFuse(),X.SavedFuse);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.PC->TogglePause();
        X.PC->PlayerTick(1.f/120.f);
        X.Test->TestTrue(TEXT("Paused release becomes a short toss on resume"),X.Mode->ActiveBomb && !X.Mode->ActiveBomb->IsHeld());
        return true;
    });
    Step(TEXT("LMB release puts bomb on the actual floor"),{},[](FFixture& X)
    {
        if (X.Mode->Phase!=EBDTutorialPhase::Success) return false;
        X.Test->TestTrue(TEXT("Released bomb remains live"),X.Mode->ActiveBomb && !X.Mode->ActiveBomb->IsHeld());
        if (X.Mode->ActiveBomb) X.ExpectedExplosionAt = X.World->GetTimeSeconds()+X.Mode->ActiveBomb->GetRemainingFuse();
        return true;
    });
    ObserveBombBeforeSliding(2);
    Step(TEXT("Scene four has a genuine gap; a failed jump resets cleanly"),{},[](FFixture& X)
    {
        if (X.Mode->SceneIndex!=3 || !X.Mode->IsGameplayInputAllowed()) return false;
        X.Shot(TEXT("06-cross-gap.png"));
        X.Test->TestNull(TEXT("Gap scene has no crate"),X.Mode->Crate.Get());
        X.Test->TestNull(TEXT("Gap scene has no goal"),X.Mode->Goal.Get());
        FHitResult Hit;
        const FVector O = X.Mode->GetSceneOrigin();
        X.Test->TestFalse(TEXT("No hidden floor in gap"),X.World->LineTraceSingleByChannel(Hit,O+FVector(0,0,20),O-FVector(0,0,250),ECC_Visibility));
        X.PlacePlayer(FVector(0,0,X.Mode->GetPhysicsSettings().FallKillHeight*100.f-100.f));
        return true;
    });
    Step(TEXT("Fall rebuilds same stage without credit"),{},[](FFixture& X)
    {
        if (!X.After(.4)) return false;
        X.Test->TestEqual(TEXT("Stage retained after fall"),X.Mode->SceneIndex,3);
        X.Test->TestTrue(TEXT("Player restored"),FVector::Dist2D(X.Player->GetActorLocation(),X.Mode->GetSceneOrigin()+FVector(-330,0,93))<1);
        X.Test->TestNull(TEXT("No stale bomb"),X.Mode->ActiveBomb.Get());
        X.Aim(FVector(500,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        return true;
    });
    Step(TEXT("Held explosion crosses gap and lands on destination ground"),{},[](FFixture& X)
    {
        if (X.Mode->Phase!=EBDTutorialPhase::Success) return false;
        X.Test->TestTrue(TEXT("Grounded on other side"),X.Player->GetCharacterMovement()->IsMovingOnGround());
        X.Test->TestTrue(TEXT("Correct landing floor"),X.Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor()==X.Mode->GetLandingGround());
        X.Key(EKeys::LeftMouseButton,IE_Released);
        return true;
    });
    Step(TEXT("Final scene: large goal and crate start on second platform"),{},[](FFixture& X)
    {
        if (X.Mode->SceneIndex!=4 || !X.Mode->IsGameplayInputAllowed()) return false;
        X.Test->TestNotNull(TEXT("Final crate"),X.Mode->Crate.Get());
        X.Test->TestNotNull(TEXT("Final goal"),X.Mode->Goal.Get());
        X.Test->TestFalse(TEXT("No automatic win on spawn"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
        X.Test->TestTrue(TEXT("Goal is generous"),X.Mode->Goal->Extent.X>=300 && X.Mode->Goal->Extent.Y>=300);
        X.Shot(TEXT("07-deliver.png"));
        X.Aim(FVector(500,0,0));
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        return true;
    });
    Step(TEXT("Cross to final box using real held explosion"),{},[](FFixture& X)
    {
        if (X.Player->GetActorLocation().X-X.Mode->GetSceneOrigin().X<200 || !X.Player->GetCharacterMovement()->IsMovingOnGround()) return false;
        X.Key(EKeys::LeftMouseButton,IE_Released);
        return true;
    });
    Step(TEXT("Walk around the package after the charged blast jump"),{},[](FFixture& X)
    {
        const FVector Target=X.Mode->GetSceneOrigin()+FVector(280,220,93);
        const FVector Delta=Target-X.Player->GetActorLocation();
        const bool bArrived=Delta.Size2D()<12.f;
        const FVector Direction=Delta.GetSafeNormal2D();
        X.Player->MoveInRoom(bArrived ? FVector2D::ZeroVector : FVector2D(Direction.X,Direction.Y)*.4f);
        return bArrived && X.Player->GetVelocity().Size2D()<5.f;
    });
    Step(TEXT("Line up behind the original package"),{},[](FFixture& X)
    {
        const FVector Target=X.Mode->GetSceneOrigin()+FVector(280,0,93);
        const FVector Delta=Target-X.Player->GetActorLocation();
        const bool bArrived=Delta.Size2D()<10.f;
        const FVector Direction=Delta.GetSafeNormal2D();
        X.Player->MoveInRoom(bArrived ? FVector2D::ZeroVector : FVector2D(Direction.X,Direction.Y)*.3f);
        return bArrived && X.Player->GetVelocity().Size2D()<5.f;
    });
    Step(TEXT("Quick LMB toss places a delivery bomb behind the package"),{},[](FFixture& X)
    {
        // The cursor used for the jump is behind the player after landing. Aim again
        // before pressing: the input callback correctly derives facing from that cursor.
        X.Aim(FVector(500,0,0));
        X.PreviousCrate=X.Mode->Crate;
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        ABDBomb* Bomb=X.Mode->ActiveBomb;
        if (!X.Test->TestNotNull(TEXT("Delivery uses a real dropped bomb"),Bomb))
        {
            X.bFailed=true;
            return true;
        }
        X.Test->TestFalse(TEXT("Delivery bomb is released"),Bomb->IsHeld());
        X.Test->TestTrue(TEXT("Bomb lies between the player and the original crate"),
            Bomb->GetActorLocation().X>X.Player->GetActorLocation().X &&
            Bomb->GetActorLocation().X<X.Mode->Crate->GetActorLocation().X);
        X.Test->TestTrue(TEXT("Original crate is inside the delivery blast radius"),
            FVector::Dist(Bomb->GetActorLocation(),X.Mode->Crate->GetActorLocation())<Bomb->GetProfile().BlastRadius*100.f);
        X.bRetreatBraking=false;
        X.DescribeScene(TEXT("Delivery bomb placed"));
        return true;
    });
    Step(TEXT("Walk aside on the landing platform before the delivery fuse expires"),{},[](FFixture& X)
    {
        // A normal walking input moves aside; neither the player nor crate is
        // teleported. Reduce speed so braking stays comfortably inside the platform.
        const float LocalY=X.Player->GetActorLocation().Y-X.Mode->GetSceneOrigin().Y;
        X.bRetreatBraking|=LocalY>=330.f;
        X.Player->MoveInRoom(FVector2D(0,X.bRetreatBraking ? 0.f : .4f));
        if (!X.bRetreatBraking || X.Player->GetVelocity().Size2D()>5.f) return false;
        X.Test->TestTrue(TEXT("Player remains on landing ground"),X.Player->GetCharacterMovement()->IsMovingOnGround());
        X.Test->TestTrue(TEXT("Player is clear of the pit and platform edge"),
            X.Player->GetActorLocation().X-X.Mode->GetSceneOrigin().X>220.f && FMath::Abs(LocalY)<400.f);
        X.Test->TestNotNull(TEXT("Retreat finishes before the natural fuse expires"),X.Mode->ActiveBomb.Get());
        X.DescribeScene(TEXT("Delivery player safely aside"));
        return true;
    });
    Step(TEXT("Explosion delivers original box wholly inside the goal"),{},[](FFixture& X)
    {
        if (X.Mode->Phase!=EBDTutorialPhase::Success) return false;
        X.Test->TestTrue(TEXT("Delivery uses the original crate without a scene reset"),X.Mode->Crate==X.PreviousCrate.Get());
        X.Test->TestTrue(TEXT("Full containment"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
        X.Test->TestTrue(TEXT("Crate settled before success"),X.Mode->Goal->ContainsSettledCrate(X.Mode->Crate));
        X.DescribeScene(TEXT("Delivery complete"));
        return true;
    });
    Step(TEXT("Final success stays on scene five and freezes input"),{},[](FFixture& X)
    {
        if (!X.Mode->bCompleted) return false;
        X.Test->TestEqual(TEXT("No nonexistent sixth scene"),X.Mode->SceneIndex,4);
        X.Test->TestFalse(TEXT("Completion blocks input"),X.Mode->IsGameplayInputAllowed());
        X.Shot(TEXT("08-complete.png"));
        return true;
    });
    Step(TEXT("Restart after completion clears all previous state"),[](FFixture& X){ X.RestartSceneSequence(); },[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        X.Test->TestEqual(TEXT("Back to first scene"),X.Mode->SceneIndex,0);
        X.Test->TestEqual(TEXT("Bomb count reset"),X.Mode->BombsUsed,0);
        X.Test->TestEqual(TEXT("Only one player"),X.Count<ABDCharacter>(),1);
        X.Test->TestEqual(TEXT("No old crates"),X.Count<ABDCrate>(),0);
        X.Test->TestEqual(TEXT("No old goals"),X.Count<ABDGoal>(),0);
        X.Test->TestTrue(TEXT("Input enabled"),X.Mode->IsGameplayInputAllowed());
        return true;
    });
    Step(TEXT("Restart while waiting for a bomb cancels the old transition"),[](FFixture& X)
    {
        X.SavedFuse = X.Mode->GetPhysicsSettings().FuseSeconds;
        X.SetFuse(.25f);
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestEqual(TEXT("Waiting with success feedback"),X.Mode->Phase,EBDTutorialPhase::Success);
        X.RestartSceneSequence();
    },[](FFixture& X)
    {
        if (!X.After(.9)) return false;
        X.Test->TestEqual(TEXT("No stale transition after restart"),X.Mode->SceneIndex,0);
        X.Test->TestEqual(TEXT("Restart remains playable"),X.Mode->Phase,EBDTutorialPhase::Playing);
        X.Test->TestNull(TEXT("Old observed bomb removed"),X.Mode->ActiveBomb.Get());
        X.SetFuse(X.SavedFuse);
        return true;
    });
    Step(TEXT("Unexpected bomb removal retries the scene instead of impersonating an explosion"),[](FFixture& X)
    {
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Key(EKeys::LeftMouseButton,IE_Released);
        if (X.Mode->ActiveBomb) X.Mode->ActiveBomb->Destroy();
    },[](FFixture& X)
    {
        if (!X.After(.7)) return false;
        X.Test->TestEqual(TEXT("Same scene after non-explosion destruction"),X.Mode->SceneIndex,0);
        X.Test->TestEqual(TEXT("Missing bomb does not strand success state"),X.Mode->Phase,EBDTutorialPhase::Playing);
        X.Test->TestNull(TEXT("No stale bomb"),X.Mode->ActiveBomb.Get());
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
