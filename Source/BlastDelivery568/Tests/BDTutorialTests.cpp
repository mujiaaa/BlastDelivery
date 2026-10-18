#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDGameMode.h"
#include "../BDAlphaSession.h"
#include "../BDPlayerController.h"
#include "../BDPuzzleActors.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"

namespace BDTests
{
struct FFixture
{
    FAutomationTestBase* Test=nullptr;
    UWorld* World=nullptr;
    ABDGameMode* Mode=nullptr;
    ABDCharacter* Courier=nullptr;
    ABDPlayerController* Controller=nullptr;
    UBDAlphaSession* Session=nullptr;
    FVector SavedPosition, SavedDirection;
    TWeakObjectPtr<ABDBomb> SavedBomb;
    TWeakObjectPtr<ABDCrate> RouteCrate;
    float SavedFuse=0, SavedTime=0, NearSpeed=0, WorldMark=0;
    double StepStart=0;
    bool bFailed=false;
    bool bObservedCrateRecovery=false;
    int32 RouteLeg=0;
    void Refresh()
    {
        World=GEditor->PlayWorld;
        if (!World) return;
        Mode=World->GetAuthGameMode<ABDGameMode>();
        Courier=Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(World,0));
        Controller=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        Session=World->GetGameInstance<UBDAlphaSession>();
    }
    bool Ready() const { return World && Mode && Mode->IsPlayground() && Courier && Controller && Session && Mode->Crate && Mode->Goal && Mode->Gate; }
    bool After(double Seconds) const { return FPlatformTime::Seconds()-StepStart>=Seconds; }
    bool Simulated(float Seconds) const { return World->GetTimeSeconds()-WorldMark>=Seconds; }
    void Key(FKey Key,EInputEvent Event) const
    {
        Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Event==IE_Released ? 0.f : 1.f));
        Controller->PlayerTick(Controller->IsPaused() ? 0.f : 1.f/120.f);
    }
    void Aim(FVector Direction) const
    {
        Courier->FacePoint(Courier->GetActorLocation()+Direction);
        FVector2D Screen;
        if (Controller->ProjectWorldLocationToScreen(Courier->GetActorLocation()+Direction,Screen))
            Controller->SetMouseLocation(FMath::RoundToInt(Screen.X),FMath::RoundToInt(Screen.Y));
    }
    void PlacePlayer(FVector Position,FRotator Rotation=FRotator::ZeroRotator) const
    {
        Courier->MoveInRoom(FVector2D::ZeroVector);
        Courier->GetCharacterMovement()->StopMovementImmediately();
        Courier->SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
        Courier->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        Aim(Rotation.Vector()*500.f);
    }
    void PlaceCrate(FVector Position) const
    {
        Mode->Crate->Mesh->SetSimulatePhysics(true);
        Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Mode->Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        Mode->Crate->SetActorLocationAndRotation(Position,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        Mode->Crate->Mesh->WakeAllRigidBodies();
    }
    void ClearBomb() const { if (IsValid(Mode->ActiveBomb)) Mode->ActiveBomb->Destroy(); Controller->ResetBombInput(); }
    void SetFuse(float Seconds) const
    {
        FBDPhysicsSettings Settings=Mode->GetPhysicsSettings();
        Settings.FuseSeconds=Seconds;
        Test->TestTrue(TEXT("Physics profile update accepted"),Mode->ApplyPhysicsSettings(Settings));
    }
    void PrepareRouteBlast()
    {
        if (Mode->bCompleted) return;
        const FVector Cargo=Mode->Crate->GetActorLocation();
        if (RouteLeg==0 && FMath::Abs(Cargo.Y+560.f)<35.f) RouteLeg=1;
        const FVector Target=RouteLeg==0 ? FVector(-400,-560,Cargo.Z) : FVector(700,-560,Cargo.Z);
        const FVector Delta=Target-Cargo;
        const float Distance=Delta.Size2D();
        if (Distance<8.f) return;
        const FVector Direction=Delta.GetSafeNormal2D();
        const auto& Profile=Mode->GetPhysicsSettings();
        // Estimate a bounded shot from the current mass/damping and .07 floor friction.
        // The crate itself is never teleported during this closed-loop route check.
        const float DesiredStep=FMath::Min(Distance,145.f);
        const float K=FMath::Max(Profile.CrateDamping,.01f), A=.07f*981.f;
        float Low=0.f, High=Profile.BlastImpulse*100.f/Profile.CrateMass;
        for (int32 I=0;I<24;++I)
        {
            const float Speed=(Low+High)*.5f;
            const float Travel=Speed/K-A/(K*K)*FMath::Loge(1.f+K*Speed/A);
            if (Travel<DesiredStep) Low=Speed; else High=Speed;
        }
        const float Radius=Profile.BlastRadius*100.f;
        const float Radial=FMath::Clamp(Radius*(1.f-High*Profile.CrateMass/(Profile.BlastImpulse*100.f)),105.f,Radius-3.f);
        const float Height=Cargo.Z-(Profile.BombRadius*100.f+3.f);
        const float Planar=FMath::Sqrt(FMath::Max(105.f*105.f,Radial*Radial-Height*Height));
        const FVector BombPosition=Cargo-Direction*Planar;
        FVector PlayerPosition=BombPosition-Direction*90.f;
        PlayerPosition.Z=93.f;
        PlacePlayer(PlayerPosition,Direction.Rotation());
        Key(EKeys::RightMouseButton,IE_Pressed);
        Key(EKeys::RightMouseButton,IE_Released);
        if (Test->TestNotNull(TEXT("Route shot creates a real dropped bomb"),Mode->ActiveBomb.Get()))
        {
            Test->TestFalse(TEXT("Route shot was released before exploding"),Mode->ActiveBomb->IsHeld());
            Mode->ActiveBomb->Explode();
        }
        Test->AddInfo(FString::Printf(TEXT("Route leg %d: cargo %s, target %s"),RouteLeg,*Cargo.ToString(),*Target.ToString()));
    }
};
using FAction=TFunction<void(FFixture&)>;
using FCondition=TFunction<bool(FFixture&)>;
class FBoot : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    bool bRequested=false;
    double Started=FPlatformTime::Seconds();
public:
    explicit FBoot(TSharedRef<FFixture> In):F(In) {}
    bool Update() override
    {
        F->Refresh();
        if (!bRequested && F->World && F->Session) { bRequested=true; F->Session->SetSimpleControls(true); F->Session->StartPlayground(); }
        if (bRequested && F->Ready()) return true;
        if (FPlatformTime::Seconds()-Started>25.) { F->Test->AddError(TEXT("Alpha Playground failed to open")); F->bFailed=true; return true; }
        return false;
    }
};
class FStep : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Started=FPlatformTime::Seconds();
    bool bEntered=false;
    TWeakObjectPtr<UWorld> EnteredWorld;
public:
    FStep(TSharedRef<FFixture> In,FString Label,FAction Action,FCondition Condition)
        :F(In),Name(MoveTemp(Label)),Begin(MoveTemp(Action)),Until(MoveTemp(Condition)) {}
    bool Update() override
    {
        if (F->bFailed) return true;
        F->Refresh();
        if (!bEntered && F->Ready())
        {
            bEntered=true; Started=FPlatformTime::Seconds(); F->StepStart=Started; F->WorldMark=F->World->GetTimeSeconds();
            EnteredWorld=F->World;
            F->Test->AddInfo(Name); UE_LOG(LogTemp,Display,TEXT("BD TEST: %s"),*Name);
            if (Begin) Begin(*F);
        }
        if (bEntered && F->Ready() && EnteredWorld.Get()!=F->World)
        {
            EnteredWorld=F->World; Started=FPlatformTime::Seconds(); F->StepStart=Started; F->WorldMark=F->World->GetTimeSeconds();
        }
        if (bEntered && F->Ready() && Until(*F)) return true;
        if (FPlatformTime::Seconds()-Started>20.)
        {
            if (F->Ready()) F->Test->AddInfo(FString::Printf(TEXT("State at timeout: player=%s crate=%s velocity=%s recovering=%d observedRecovery=%d"),
                *F->Courier->GetActorLocation().ToString(),*F->Mode->Crate->GetActorLocation().ToString(),
                *F->Mode->Crate->Mesh->GetPhysicsLinearVelocity().ToString(),F->Mode->Crate->IsRecovering(),F->bObservedCrateRecovery));
            F->Test->AddError(TEXT("Timed out: ")+Name); F->bFailed=true; return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDTutorialTest,"BlastDelivery.Tutorial.GameplayAndPuzzle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDTutorialTest::RunTest(const FString& Parameters)
{
    using namespace BDTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    Step(TEXT("Alpha room defaults and continuous WASD movement"),{},[](FFixture& X)
    {
        if (!X.Simulated(.6f)) return false;
        X.Test->TestFalse(TEXT("Gate starts locked"),X.Mode->Gate->bOpen);
        X.Test->TestEqual(TEXT("Default fuse"),X.Mode->GetPhysicsSettings().FuseSeconds,4.f);
        X.Test->TestEqual(TEXT("Default cargo mass"),X.Mode->Crate->Mesh->BodyInstance.GetBodyMass(),30.f);
        X.Test->TestTrue(TEXT("Character capsule is 180 cm high"),FMath::IsNearlyEqual(X.Courier->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),90.f));
        X.SavedPosition=X.Courier->GetActorLocation(); X.Key(EKeys::D,IE_Pressed); return true;
    });
    Step(TEXT("Actual D key moves right"),{},[](FFixture& X)
    {
        if (!X.Simulated(.45f)) return false;
        X.Key(EKeys::D,IE_Released);
        X.Test->TestTrue(TEXT("D moves continuously right"),X.Courier->GetActorLocation().X>X.SavedPosition.X+70.f);
        X.PlacePlayer(FVector(-700,300,93)); X.Key(EKeys::RightMouseButton,IE_Pressed); return true;
    });
    Step(TEXT("RMB hold burns fuse without releasing"),{},[](FFixture& X)
    {
        if (!X.Simulated(.25f)) return false;
        if (!X.Test->TestNotNull(TEXT("Held bomb exists"),X.Mode->ActiveBomb.Get())) { X.bFailed=true; return true; }
        X.Test->TestTrue(TEXT("Bomb is held"),X.Mode->ActiveBomb->IsHeld());
        X.Test->TestTrue(TEXT("Fuse already burning"),X.Mode->ActiveBomb->GetRemainingFuse()<X.Mode->GetPhysicsSettings().FuseSeconds-.1f);
        X.SavedFuse=X.Mode->ActiveBomb->GetRemainingFuse(); X.SavedTime=X.Mode->ElapsedSeconds;
        X.SavedPosition=X.Mode->Crate->GetActorLocation(); X.Controller->TogglePause(); return true;
    });
    Step(TEXT("Pause freezes fuse, timer and cargo"),{},[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        X.Test->TestEqual(TEXT("Frozen fuse"),X.Mode->ActiveBomb->GetRemainingFuse(),X.SavedFuse);
        X.Test->TestEqual(TEXT("Frozen timer"),X.Mode->ElapsedSeconds,X.SavedTime);
        X.Test->TestTrue(TEXT("Frozen cargo"),X.Mode->Crate->GetActorLocation().Equals(X.SavedPosition,.01f));
        X.Controller->TogglePause(); X.Key(EKeys::RightMouseButton,IE_Released); return true;
    });
    Step(TEXT("Nearby live bomb is picked up without an extra resource or fuse reset"),{},[](FFixture& X)
    {
        if (!X.Simulated(.2f)) return false;
        X.SavedBomb=X.Mode->ActiveBomb; X.SavedFuse=X.Mode->ActiveBomb->GetRemainingFuse();
        const int32 Count=X.Mode->BombsUsed;
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Test->TestEqual(TEXT("Pickup keeps same actor"),X.Mode->ActiveBomb.Get(),X.SavedBomb.Get());
        X.Test->TestTrue(TEXT("Simple LMB holds picked-up bomb"),X.Mode->ActiveBomb->IsHeld());
        X.Test->TestEqual(TEXT("Pickup does not spend another bomb"),X.Mode->BombsUsed,Count);
        X.Test->TestTrue(TEXT("Pickup preserves fuse"),X.Mode->ActiveBomb->GetRemainingFuse()<=X.SavedFuse);
        X.SavedPosition=X.Mode->ActiveBomb->GetActorLocation(); X.SavedDirection=X.Courier->GetActorForwardVector();
        X.Key(EKeys::LeftMouseButton,IE_Released); return true;
    });
    Step(TEXT("LMB release throws toward current aim with the original fuse"),{},[](FFixture& X)
    {
        if (!X.Simulated(.3f)) return false;
        X.Test->TestFalse(TEXT("Released bomb detached"),X.Mode->ActiveBomb->IsHeld());
        X.Test->TestTrue(TEXT("Throw moved forward"),FVector::DotProduct(X.Mode->ActiveBomb->GetActorLocation()-X.SavedPosition,X.SavedDirection)>90.f);
        X.Test->TestTrue(TEXT("Throw preserves burning fuse"),X.Mode->ActiveBomb->GetRemainingFuse()<X.SavedFuse-.1f);
        X.PlacePlayer(FVector(-800,-300,93)); return true;
    });
    Step(TEXT("Real fuse detonation frees unlimited supply"),{},[](FFixture& X)
    {
        if (IsValid(X.Mode->ActiveBomb)) return false;
        X.Test->TestTrue(TEXT("Thrown actor really destroyed"),!X.SavedBomb.IsValid());
        X.PlacePlayer(FVector(-650,-160,93)); X.SavedPosition=X.Mode->Crate->GetActorLocation();
        X.Key(EKeys::D,IE_Pressed); return true;
    });
    Step(TEXT("Walking cannot push cargo"),{},[](FFixture& X)
    {
        if (!X.Simulated(1.f)) return false;
        X.Key(EKeys::D,IE_Released);
        X.Test->TestTrue(TEXT("Cargo unchanged by walking"),FVector::Dist2D(X.SavedPosition,X.Mode->Crate->GetActorLocation())<.5f);
        X.PlacePlayer(FVector(-650,-160,93)); X.Courier->ApplyBlastVelocity(FVector(1100,0,0)); return true;
    });
    Step(TEXT("Launched player cannot push cargo"),{},[](FFixture& X)
    {
        if (!X.Simulated(.9f)) return false;
        X.Test->TestTrue(TEXT("Cargo unchanged by player impact"),FVector::Dist2D(X.SavedPosition,X.Mode->Crate->GetActorLocation())<.5f);
        X.PlacePlayer(FVector(-750,-160,93)); X.Key(EKeys::RightMouseButton,IE_Pressed); X.Key(EKeys::RightMouseButton,IE_Released);
        X.Mode->ActiveBomb->SetActorLocation(FVector(-540,-160,40),false,nullptr,ETeleportType::TeleportPhysics);
        X.Mode->ActiveBomb->Collision->SetPhysicsLinearVelocity(FVector(250,0,0)); return true;
    });
    Step(TEXT("Rolling bomb cannot move resting cargo"),{},[](FFixture& X)
    {
        if (!X.Simulated(.7f)) return false;
        X.Test->TestTrue(TEXT("Bomb reached the crate side"),X.Mode->ActiveBomb->GetActorLocation().X>-535.f);
        X.Test->TestTrue(TEXT("Cargo remains fixed under ball contact"),FVector::Dist2D(X.SavedPosition,X.Mode->Crate->GetActorLocation())<.5f);
        X.ClearBomb(); X.PlacePlayer(FVector(-800,400,93));
        X.Mode->ExplodeAt(X.Mode->Crate->GetActorLocation()-FVector(150,0,0),nullptr,nullptr); return true;
    });
    Step(TEXT("Near explosion releases cargo lock"),{},[](FFixture& X)
    {
        if (!X.Simulated(.12f)) return false;
        X.NearSpeed=X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size2D();
        X.Test->TestTrue(TEXT("Explosion creates real velocity"),X.NearSpeed>100.f);
        X.Mode->Crate->Respawn(); return true;
    });
    Step(TEXT("Independent cargo reset completes before distance comparison"),{},[](FFixture& X)
    {
        if (!X.Simulated(.85f) || X.Mode->Crate->IsRecovering()) return false;
        X.Mode->ExplodeAt(X.Mode->Crate->GetActorLocation()-FVector(300,0,0),nullptr,nullptr); return true;
    });
    Step(TEXT("Explosion falloff reduces distant impulse"),{},[](FFixture& X)
    {
        if (!X.Simulated(.12f)) return false;
        X.Test->TestTrue(TEXT("Far impact is weaker"),X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size2D()<X.NearSpeed*.6f);
        X.Mode->Crate->Respawn(); X.PlacePlayer(FVector(-160,-560,93),FRotator(0,180,0));
        X.Key(EKeys::RightMouseButton,IE_Pressed); X.Mode->ActiveBomb->Explode(); X.Key(EKeys::RightMouseButton,IE_Released); return true;
    });
    Step(TEXT("Closed gate blocks a blast jump"),{},[](FFixture& X)
    {
        if (!X.Simulated(1.5f)) return false;
        X.Test->TestTrue(TEXT("Player cannot jump through gate"),X.Courier->GetActorLocation().X<-60.f);
        X.PlacePlayer(FVector(-180,0,93),FRotator(0,180,0));
        X.Key(EKeys::RightMouseButton,IE_Pressed); X.Mode->ActiveBomb->Explode(); X.Key(EKeys::RightMouseButton,IE_Released); return true;
    });
    Step(TEXT("L wall blocks a blast jump"),{},[](FFixture& X)
    {
        if (!X.Simulated(1.5f)) return false;
        X.Test->TestTrue(TEXT("Player remains west of hard wall"),X.Courier->GetActorLocation().X<-100.f);
        X.PlacePlayer(FVector(-800,300,93)); X.PlaceCrate(FVector(-150,-560,63));
        X.Mode->Crate->ApplyBlast(FVector(-300,-560,63),X.Mode->GetPhysicsSettings().BlastImpulse*100.f); return true;
    });
    Step(TEXT("Closed gate stops and rebounds real cargo"),{},[](FFixture& X)
    {
        if (!X.Simulated(.75f)) return false;
        X.Test->TestTrue(TEXT("Cargo remains west of locked gate"),X.Mode->Crate->GetActorLocation().X<-85.f);
        X.PlaceCrate(FVector(70,510,63));
        X.Mode->Crate->ApplyBlast(FVector(-100,510,63),X.Mode->GetPhysicsSettings().BlastImpulse*100.f); return true;
    });
    Step(TEXT("Ordinary horizontal cargo blast falls into actual pit"),{},[](FFixture& X)
    {
        if (!X.Simulated(3.f) || X.Mode->Crate->IsRecovering()) return false;
        X.Test->TestTrue(TEXT("Cargo independently respawned"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Mode->Crate->GetSpawnPosition())<1.f);
        X.PlacePlayer(FVector(10,510,93),FRotator(0,180,0)); return true;
    });
    Step(TEXT("Hold a real fuse at takeoff marker"),{},[](FFixture& X)
    {
        if (!X.Simulated(.2f)) return false;
        X.Key(EKeys::RightMouseButton,IE_Pressed); return true;
    });
    Step(TEXT("Held explosion crosses pit and lands safely"),{},[](FFixture& X)
    {
        if (X.Mode->ActiveBomb || !X.Courier->GetCharacterMovement()->IsMovingOnGround() || X.Courier->GetActorLocation().X<540.f) return false;
        X.Test->TestFalse(TEXT("Landing alone does not open gate"),X.Mode->Gate->bOpen);
        X.Key(EKeys::RightMouseButton,IE_Released); X.Key(EKeys::W,IE_Pressed); return true;
    });
    Step(TEXT("Walk north along east corridor"),{},[](FFixture& X)
    {
        if (X.Courier->GetActorLocation().Y>-425.f) return false;
        X.Key(EKeys::W,IE_Released); return true;
    });
    Step(TEXT("Brake before turning toward the plate"),{},[](FFixture& X)
    {
        if (!X.Simulated(.7f)) return false;
        X.Test->TestTrue(TEXT("Stopped in plate lane"),FMath::Abs(X.Courier->GetActorLocation().Y+560.f)<75.f);
        X.Key(EKeys::A,IE_Pressed); return true;
    });
    Step(TEXT("Walk onto plate to open gate"),{},[](FFixture& X)
    {
        if (!X.Mode->Gate->bOpen) return false;
        X.Key(EKeys::A,IE_Released);
        X.Test->TestTrue(TEXT("Real grounded plate activation"),X.Courier->GetCharacterMovement()->IsMovingOnGround());
        X.SavedPosition=X.Mode->Crate->GetActorLocation(); X.PlacePlayer(FVector(330,510,93)); return true;
    });
    Step(TEXT("Player fall keeps crate and open gate"),{},[](FFixture& X)
    {
        if (!X.Simulated(2.f) || X.Courier->IsRecovering()) return false;
        X.Test->TestTrue(TEXT("Player independently returned"),FVector::Dist2D(X.Courier->GetActorLocation(),X.Courier->GetSpawnPosition())<1.f);
        X.Test->TestTrue(TEXT("Crate preserved"),FVector::Dist2D(X.SavedPosition,X.Mode->Crate->GetActorLocation())<1.f);
        X.Test->TestTrue(TEXT("Gate persists"),X.Mode->Gate->bOpen);
        X.SavedPosition=X.Courier->GetActorLocation();
        X.bObservedCrateRecovery=false;
        X.PlaceCrate(FVector(330,510,63)); return true;
    });
    Step(TEXT("Cargo fall keeps player and open gate"),{},[](FFixture& X)
    {
        // Chaos caps physics substeps during render hitches: two world seconds
        // do not guarantee the body has reached the fall threshold. Observe the
        // real recovery transition, within FStep's unchanged 20-second timeout.
        if (X.Mode->Crate->IsRecovering())
        {
            if (!X.bObservedCrateRecovery)
                X.Test->AddInfo(FString::Printf(TEXT("Cargo entered recovery after %.3f world seconds at %s"),
                    X.World->GetTimeSeconds()-X.WorldMark,*X.Mode->Crate->GetActorLocation().ToString()));
            X.bObservedCrateRecovery=true;
            return false;
        }
        if (!X.bObservedCrateRecovery) return false;
        X.Test->AddInfo(FString::Printf(TEXT("Cargo exited recovery after %.3f world seconds at %s (spawn %s)"),
            X.World->GetTimeSeconds()-X.WorldMark,*X.Mode->Crate->GetActorLocation().ToString(),*X.Mode->Crate->GetSpawnPosition().ToString()));
        X.Test->TestTrue(TEXT("Crate independently returned"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),X.Mode->Crate->GetSpawnPosition())<1.f);
        X.Test->TestTrue(TEXT("Player preserved"),FVector::Dist2D(X.SavedPosition,X.Courier->GetActorLocation())<1.f);
        X.Test->TestTrue(TEXT("Gate persists"),X.Mode->Gate->bOpen);
        X.RouteCrate=X.Mode->Crate; X.RouteLeg=0; return true;
    });
    for (int32 Shot=0;Shot<16;++Shot)
        Step(TEXT("Bomb-only delivery through upper gate passage"),[](FFixture& X){X.PrepareRouteBlast();},[](FFixture& X)
        {
            X.Test->TestEqual(TEXT("Delivery uses original cargo actor"),X.Mode->Crate.Get(),X.RouteCrate.Get());
            return X.Mode->bCompleted || X.Simulated(2.8f);
        });
    Step(TEXT("Full physical delivery and replay"),{},[](FFixture& X)
    {
        X.Test->TestTrue(TEXT("Bomb-only route reaches settled goal"),X.Mode->bCompleted);
        X.Test->TestTrue(TEXT("Delivered crate fully contained"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
        X.Session->Replay(); return true;
    });
    Step(TEXT("Replay restores gate, cargo, counters and timer"),{},[](FFixture& X)
    {
        if (!X.Simulated(.5f)) return false;
        X.Test->TestFalse(TEXT("Replay closes gate"),X.Mode->Gate->bOpen);
        X.Test->TestFalse(TEXT("Replay clears completion"),X.Mode->bCompleted);
        X.Test->TestNull(TEXT("Replay clears bombs"),X.Mode->ActiveBomb.Get());
        X.Test->TestEqual(TEXT("Replay resets usage"),X.Mode->BombsUsed,0);
        X.Test->TestTrue(TEXT("Timer restarted"),X.Mode->ElapsedSeconds<2.f);
        X.PlaceCrate(FVector(550,-560,63)); return true;
    });
    Step(TEXT("Partial overlap is not delivery"),{},[](FFixture& X)
    {
        if (!X.Simulated(1.2f)) return false;
        X.Test->TestFalse(TEXT("Half-entry rejected"),X.Mode->bCompleted);
        X.PlaceCrate(FVector(700,-560,63));
        X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector(200,0,0));
        X.Test->TestFalse(TEXT("Moving crate rejected"),X.Mode->Goal->ContainsSettledCrate(X.Mode->Crate)); return true;
    });
    Step(TEXT("Passing through does not instantly win"),{},[](FFixture& X)
    {
        if (!X.Simulated(.2f)) return false;
        X.Test->TestFalse(TEXT("Dwell required"),X.Mode->bCompleted);
        X.PlaceCrate(FVector(550,-560,63)); return true;
    });
    Step(TEXT("Leaving goal resets dwell"),{},[](FFixture& X)
    {
        if (!X.Simulated(.2f)) return false;
        X.Test->TestFalse(TEXT("Exit prevents completion"),X.Mode->bCompleted);
        X.PlaceCrate(FVector(700,-560,63)); return true;
    });
    Step(TEXT("Full containment and settled dwell completes delivery"),{},[](FFixture& X)
    {
        if (!X.Mode->bCompleted) return false;
        X.Test->TestTrue(TEXT("At least one second of settled dwell"),X.Simulated(X.Mode->GetPhysicsSettings().GoalSettleSeconds-.03f));
        X.SavedTime=X.Mode->ElapsedSeconds; X.Courier->DrawBomb();
        X.Test->TestNull(TEXT("Completion prevents new bombs"),X.Mode->ActiveBomb.Get()); return true;
    });
    Step(TEXT("Completion freezes result time"),{},[](FFixture& X)
    {
        if (!X.After(.3)) return false;
        X.Test->TestEqual(TEXT("Final time retained"),X.Mode->ElapsedSeconds,X.SavedTime); return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDBombReuseAndCameraTest,"BlastDelivery.Tutorial.BombReuseAndCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDBombReuseAndCameraTest::RunTest(const FString& Parameters)
{
    using namespace BDTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    Step(TEXT("Orthographic room camera and complete mouse capture"),[](FFixture& X)
    { X.World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(1280,800); },[](FFixture& X)
    {
        if (!X.Simulated(.6f)) return false;
        X.Test->TestEqual(TEXT("Mouse-down capture"),X.World->GetGameViewport()->GetMouseCaptureMode(),EMouseCaptureMode::CaptureDuringMouseDown);
        auto* Camera=Cast<ACameraActor>(X.Controller->GetViewTarget());
        if (!X.Test->TestNotNull(TEXT("Fixed room camera"),Camera)) {X.bFailed=true;return true;}
        X.Test->TestTrue(TEXT("Playground preserves the Unity Alpha pitch of -57.5 degrees"),FMath::IsNearlyEqual(Camera->GetActorRotation().Pitch,-57.5,.01));
        X.Test->TestTrue(TEXT("Axes stay aligned"),FMath::IsNearlyEqual(Camera->GetActorRotation().Yaw,-90.,.01));
        X.Test->TestTrue(TEXT("Orthographic projection"),Camera->GetCameraComponent()->ProjectionMode==ECameraProjectionMode::Orthographic);
        int32 W,H; X.Controller->GetViewportSize(W,H);
        for (double PX:{-1030.,1030.}) for(double PY:{-830.,830.}) for(double PZ:{0.,320.})
        {
            FVector2D Screen;
            const bool bProjected=X.Controller->ProjectWorldLocationToScreen(FVector(PX,PY,PZ),Screen);
            X.Test->TestTrue(TEXT("Actual room boundaries stay in frame"),bProjected && Screen.X>0 && Screen.X<W && Screen.Y>0 && Screen.Y<H);
        }
        X.SetFuse(.65f); return true;
    });
    for(int32 Cycle=0;Cycle<6;++Cycle)
    {
        const int32 ReleaseMode=Cycle%3;
        const FKey Button=ReleaseMode==2 ? EKeys::LeftMouseButton : EKeys::RightMouseButton;
        Step(TEXT("Fresh Simple hold after previous bomb destruction"),[Cycle,Button](FFixture& X)
        {
            X.PlacePlayer(FVector(-680,300,93)); X.Key(Button,Cycle>=3 ? IE_DoubleClick : IE_Pressed);
        },[Cycle,ReleaseMode,Button](FFixture& X)
        {
            if (!X.Simulated(.12f)) return false;
            if (!X.Test->TestNotNull(TEXT("Fresh bomb exists"),X.Mode->ActiveBomb.Get())) {X.bFailed=true;return true;}
            X.Test->TestEqual(TEXT("Each creation counted once"),X.Mode->BombsUsed,Cycle+1);
            X.Test->TestTrue(TEXT("Pressed button holds"),X.Mode->ActiveBomb->IsHeld());
            X.SavedFuse=X.Mode->ActiveBomb->GetRemainingFuse();
            X.Test->TestTrue(TEXT("Fuse burns while held"),X.SavedFuse>0 && X.SavedFuse<X.Mode->GetPhysicsSettings().FuseSeconds);
            X.Key(Button,IE_Pressed);
            X.Test->TestEqual(TEXT("Repeated press creates nothing"),X.Mode->BombsUsed,Cycle+1);
            if(ReleaseMode!=0) X.Key(Button,IE_Released); return true;
        });
        Step(TEXT("Release retains the bomb and its fuse"),{},[ReleaseMode](FFixture& X)
        {
            if(!X.Simulated(.12f)) return false;
            if(!X.Test->TestNotNull(TEXT("Bomb survives release"),X.Mode->ActiveBomb.Get())) {X.bFailed=true;return true;}
            X.Test->TestEqual(TEXT("Hold versus release state"),X.Mode->ActiveBomb->IsHeld(),ReleaseMode==0);
            X.Test->TestTrue(TEXT("Fuse not reset"),X.Mode->ActiveBomb->GetRemainingFuse()<X.SavedFuse);
            return true;
        });
        Step(TEXT("Detonation frees the slot without spawning on button release"),{},[Cycle,Button](FFixture& X)
        {
            if(IsValid(X.Mode->ActiveBomb)) return false;
            X.Test->TestEqual(TEXT("No hold auto-repeat"),X.Mode->BombsUsed,Cycle+1);
            X.Key(Button,IE_Released);
            X.Test->TestNull(TEXT("Release after explosion does not draw replacement"),X.Mode->ActiveBomb.Get()); return true;
        });
    }
    Step(TEXT("Explicit destruction clears only that active bomb"),[](FFixture& X)
    {
        X.SetFuse(4.f); X.PlacePlayer(FVector(-680,300,93)); X.Key(EKeys::RightMouseButton,IE_Pressed);
    },[](FFixture& X)
    {
        if(!X.Simulated(.12f)) return false;
        if(!X.Test->TestNotNull(TEXT("Bomb before destruction"),X.Mode->ActiveBomb.Get())) {X.bFailed=true;return true;}
        ABDBomb* Old=X.Mode->ActiveBomb; Old->Destroy();
        X.Test->TestNull(TEXT("EndPlay clears reference"),X.Mode->ActiveBomb.Get());
        X.Key(EKeys::RightMouseButton,IE_Released); X.Key(EKeys::LeftMouseButton,IE_Pressed);
        X.Test->TestNotNull(TEXT("Replacement can be created"),X.Mode->ActiveBomb.Get());
        X.Mode->ClearBomb(Old);
        X.Test->TestNotNull(TEXT("Old cleanup cannot remove new reference"),X.Mode->ActiveBomb.Get());
        X.Test->TestEqual(TEXT("Both actual creations counted"),X.Mode->BombsUsed,8);
        X.ClearBomb(); return true;
    });
    Step(TEXT("Cursor aiming follows tilted camera"),[](FFixture& X)
    { X.PlacePlayer(FVector(-680,300,93)); X.Aim(FVector(400,-200,0)); },[](FFixture& X)
    {
        if(!X.Simulated(.2f)) return false;
        X.Test->TestTrue(TEXT("Facing matches cursor"),FVector::DotProduct(X.Courier->GetActorForwardVector(),FVector(400,-200,0).GetSafeNormal())>.999);
        X.Key(EKeys::LeftMouseButton,IE_Pressed);
        if(!X.Test->TestNotNull(TEXT("Held aim-check bomb"),X.Mode->ActiveBomb.Get())) {X.bFailed=true;return true;}
        FVector2D Screen;
        if(X.Controller->ProjectWorldLocationToScreen(X.Courier->GetActorLocation()+FVector(300,200,0),Screen))
            X.Controller->SetMouseLocation(FMath::RoundToInt(Screen.X),FMath::RoundToInt(Screen.Y));
        X.Key(EKeys::LeftMouseButton,IE_Released);
        X.Test->TestTrue(TEXT("Release uses latest cursor direction"),FVector::DotProduct(
            X.Mode->ActiveBomb->Collision->GetPhysicsLinearVelocity().GetSafeNormal2D(),FVector(300,200,0).GetSafeNormal())>.999);
        X.ClearBomb(); return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
