#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAirport.h"
#include "../BDAirportCharacter.h"
#include "../BDAlphaSession.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDPuzzleActors.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace BDAirportTests
{
using FMode=ABDAlphaAirportGameMode;
struct FFixture
{
    FAutomationTestBase* Test=nullptr;
    UWorld* World=nullptr;
    FMode* Mode=nullptr;
    UBDAlphaSession* Session=nullptr;
    bool Failed=false;
    float Mark=0,Scalar=0;
    FVector Position;
    void Refresh()
    {
        World=GEditor->PlayWorld;
        Mode=World ? World->GetAuthGameMode<FMode>() : nullptr;
        Session=World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
    }
    bool Ready() const { return World && Mode && Mode->IsRoomReady() && Session; }
    bool After(float Seconds) const { return World->GetTimeSeconds()-Mark>Seconds; }
    void PlacePlayer(const FVector& UnityPosition)
    {
        Mode->AirportPlayer->GetCharacterMovement()->StopMovementImmediately();
        Mode->AirportPlayer->SetActorLocation(FMode::FromUnity(UnityPosition),false,nullptr,ETeleportType::TeleportPhysics);
    }
    void PlaceCrate(ABDCrate* Crate,const FVector& UnityPosition)
    {
        Crate->Mesh->SetSimulatePhysics(false);
        Crate->SetActorLocationAndRotation(FMode::FromUnity(UnityPosition),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        Crate->Mesh->SetSimulatePhysics(true);
        Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        Crate->Mesh->UpdateBounds();
    }
    void Explode(const FVector& UnityPosition)
    {
        ABDBomb* Bomb=Mode->DrawBomb(Mode->AirportPlayer);
        if (!Test->TestNotNull(TEXT("A real timed bomb is available"),Bomb)) { Failed=true; return; }
        Bomb->Release(false);
        Bomb->SetActorLocation(FMode::FromUnity(UnityPosition),false,nullptr,ETeleportType::TeleportPhysics);
        Bomb->Explode();
    }
    void CartDiagnostic(const TCHAR* Phase,const FVector& Blast)
    {
        auto* Cart=Mode->DeflectorCart.Get();
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(AirportCartProbe),false,Cart);
        World->LineTraceSingleByChannel(Hit,FMode::FromUnity(Blast),Cart->GetActorLocation(),ECC_Visibility,Query);
        UE_LOG(LogTemp,Display,TEXT("AIRPORT_CART %s position=%s velocity=%s mass=%.3f sim=%d channel=%d LOS=%d blocker=%s"),
            Phase,*FMode::ToUnity(Cart->GetActorLocation()).ToString(),*Cart->Mesh->GetPhysicsLinearVelocity().ToString(),
            Cart->Mesh->GetMass(),Cart->Mesh->IsSimulatingPhysics(),int32(Cart->Mesh->GetCollisionObjectType()),
            Mode->HasBlastLineOfSight(FMode::FromUnity(Blast),Cart,nullptr),Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("none"));
    }
};
class FBoot final : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    bool Requested=false;
    double Began=FPlatformTime::Seconds();
public:
    explicit FBoot(TSharedRef<FFixture> In):F(In) {}
    bool Update() override
    {
        F->Refresh();
        if (!Requested && F->Session)
        { Requested=true; F->Session->ResetTuning(); F->Session->StartLevel(3); }
        if (Requested && F->Ready()) { F->Session->MarkPractice(); return true; }
        if (FPlatformTime::Seconds()-Began>35)
        { F->Test->AddError(TEXT("Airport did not open through Alpha level selection")); F->Failed=true; return true; }
        return false;
    }
};
using FAction=TFunction<void(FFixture&)>;
using FCondition=TFunction<bool(FFixture&)>;
class FStep final : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    bool Entered=false;
    double Began=FPlatformTime::Seconds();
public:
    FStep(TSharedRef<FFixture> In,const TCHAR* Label,FAction Action,FCondition Condition)
        :F(In),Name(Label),Begin(MoveTemp(Action)),Until(MoveTemp(Condition)) {}
    bool Update() override
    {
        if (F->Failed) return true;
        F->Refresh();
        if (!Entered && F->Ready())
        {
            Entered=true; Began=FPlatformTime::Seconds(); F->Mark=F->World->GetTimeSeconds();
            F->Test->AddInfo(Name); if (Begin) Begin(*F);
        }
        if (Entered && F->Ready() && Until(*F)) return true;
        if (FPlatformTime::Seconds()-Began>40)
        { F->Test->AddError(TEXT("Airport timed out: ")+Name); F->Failed=true; return true; }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportIntegration,"BlastDelivery.Alpha.AirportIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportIntegration::RunTest(const FString& Parameters)
{
    using namespace BDAirportTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };

    Step(TEXT("Airport keeps source scale, live wind and real empty shuttle gap"),{},[](FFixture& X)
    {
        if (!X.After(.3f)) return false;
        X.Test->TestEqual(TEXT("Fourth Alpha entry is Airport"),X.Session->GetCurrentLevelIndex(),3);
        X.Test->TestTrue(TEXT("Delivery box uses source position in centimetres"),X.Mode->Crate->GetActorLocation().Equals(FMode::FromUnity(FVector(-10,.6f,-8.6f)),8));
        X.Test->TestTrue(TEXT("Security initially rejects cargo"),X.Mode->IsXrayPowered());
        X.Test->TestTrue(TEXT("A blows before the player reaches the apron"),X.Mode->WindAAt(FMode::FromUnity(FVector(34.9f,.8f,-6))).Y>2000);
        X.Test->TestFalse(TEXT("Cannot operate distant locked consoles"),X.Mode->PressEngineControl());
        FHitResult Hit; FCollisionQueryParams Q;
        X.Test->TestFalse(TEXT("There is no invisible bridge between shuttle docks"),X.World->LineTraceSingleByChannel(Hit,FVector(2300,0,200),FVector(2300,0,-150),ECC_Visibility,Q));
        // Keep the camera itself outside the smoke volume; a cloud surrounding
        // its lens correctly occludes every direction and is not this fixture.
        X.Mode->DeploySmoke(FMode::FromUnity(FVector(-3.5f,.1f,10.6f)));
        X.Test->TestTrue(TEXT("Smoke occludes the camera-to-weight line"),X.Mode->IsCameraOccluded(FMode::FromUnity(FVector(-2.7f,.95f,10.7f))));
        X.Test->TestFalse(TEXT("Smoke is spatial, not a global camera disable"),X.Mode->IsCameraOccluded(FMode::FromUnity(FVector(-10,.95f,5))));
        X.PlaceCrate(X.Mode->Crate,FVector(-10,.64f,-6.7f));
        return true;
    });
    Step(TEXT("Powered scanner returns real cargo along reject branch"),{},[](FFixture& X)
    {
        if (X.Mode->GetRejectionCount()<1 || X.Mode->GetBeltSegment()>=0) return false;
        X.Test->TestFalse(TEXT("Rejected shipment is not accepted"),X.Mode->IsCargoAccepted());
        X.Test->TestTrue(TEXT("Reject conveyor returns box to loading pad"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),FMode::FromUnity(FVector(-10,.64f,-8.6f)))<8);
        X.PlaceCrate(X.Mode->Counterweight,FVector(-2.7f,.64f,10.7f));
        return true;
    });
    Step(TEXT("Weight plate disables scanner and diverts new shipment"),{},[](FFixture& X)
    {
        if (!X.After(.3f)) return false;
        X.Test->TestFalse(TEXT("Actual counterweight turns X-ray off"),X.Mode->IsXrayPowered());
        X.PlaceCrate(X.Mode->Crate,FVector(-10,.64f,-6.7f));
        return true;
    });
    Step(TEXT("Accepted conveyor feeds the cargo-only dock"),{},[](FFixture& X)
    {
        if (!X.Mode->IsCargoAccepted()) return false;
        X.Test->TestTrue(TEXT("Cargo reaches receiving island"),FVector::Dist2D(X.Mode->Crate->GetActorLocation(),FMode::FromUnity(FVector(12.8f,.64f,0)))<8);
        X.Test->TestTrue(TEXT("Security progress advances the fall checkpoint"),X.Mode->GetCheckpointStage()>=2);
        X.PlaceCrate(X.Mode->Crate,FVector(16.8f,.64f,0));
        X.PlacePlayer(FVector(15.3f,.95f,-.4f));
        X.Position=X.Mode->GetShuttlePosition();
        return true;
    });
    Step(TEXT("Shuttle physically carries its cargo and player"),{},[](FFixture& X)
    {
        if (!X.After(5.4f)) return false;
        X.Test->TestTrue(TEXT("Boarded shuttle departed"),X.Mode->IsShuttleMoving() && X.Mode->GetShuttlePosition().X>X.Position.X+120);
        X.Test->TestTrue(TEXT("Moving carrier retains real cargo"),X.Mode->IsCargoAboard());
        X.Test->TestTrue(TEXT("Moving carrier retains courier"),X.Mode->IsPlayerAboard());
        X.PlaceCrate(X.Mode->Crate,FVector(32.2f,.64f,-2.2f));
        X.PlacePlayer(FVector(31.8f,.95f,3.8f));
        return true;
    });
    Step(TEXT("Real bomb clears A; pause freezes clearance countdown"),{},[](FFixture& X)
    {
        if (!X.After(.5f)) return false;
        X.Test->TestEqual(TEXT("Both actors reaching apron advances loading stage"),X.Mode->GetAirportStage(),2);
        X.PlacePlayer(FVector(31,.95f,-4));
        X.Explode(FVector(34.925f,.4f,4.6f));
        X.Test->TestTrue(TEXT("Explosion opens the complete A stream"),X.Mode->GetWindAClearSeconds()>3.4f && X.Mode->WindAAt(FMode::FromUnity(FVector(34.9f,.8f,-6))).IsNearlyZero());
        X.Scalar=X.Mode->GetWindAClearSeconds(); X.Session->TogglePause();
        return true;
    });
    Step(TEXT("Pause does not spend the temporary wind opening"),{},[](FFixture& X)
    {
        X.Test->TestTrue(TEXT("Gap time frozen in pause"),FMath::IsNearlyEqual(X.Scalar,X.Mode->GetWindAClearSeconds(),.03f));
        X.Session->Resume();
        X.PlacePlayer(FVector(38,.95f,5.6f));
        X.Test->TestTrue(TEXT("Right-side E control shuts down A"),X.Mode->PressEngineControl() && X.Mode->HasShutdownA() && !X.Mode->IsEngineARunning());
        X.PlacePlayer(FVector(45.7f,.95f,5.1f));
        X.Test->TestTrue(TEXT("Unlocked B starts independently"),X.Mode->PressEngineControl() && X.Mode->IsEngineBRunning() && !X.Mode->IsEngineARunning());
        X.PlaceCrate(X.Mode->Crate,FVector(39.3f,.64f,2.4f));
        X.PlacePlayer(FVector(32,.95f,-3));
        X.CartDiagnostic(TEXT("before blast"),FVector(34.2f,.4f,-.2f));
        X.Explode(FVector(34.2f,.4f,-.2f));
        X.CartDiagnostic(TEXT("immediately after blast"),FVector(34.2f,.4f,-.2f));
        return true;
    });
    Step(TEXT("Deflector blast preserves B and moves its reflected lane"),{},[](FFixture& X)
    {
        if (!X.After(2.4f)) return false;
        X.CartDiagnostic(TEXT("after 2.4 seconds"),FVector(34.2f,.4f,-.2f));
        X.Test->TestTrue(TEXT("A real blast drives rail cart towards ramp"),X.Mode->DeflectorCart->GetActorLocation().X>3700);
        X.Test->TestTrue(TEXT("Cart manipulation does not clear B"),X.Mode->GetWindBClearSeconds()<=0 && X.Mode->IsEngineBRunning());
        // Isolate the ramp/crest fixture from blast balance and yaw drift. The
        // subsequent ascent and delivery use only the live airflow and collisions.
        X.PlaceCrate(X.Mode->DeflectorCart,FVector(39.3f,.6f,-.2f));
        X.PlaceCrate(X.Mode->Crate,FVector(39.3f,.64f,2.4f));
        return true;
    });
    Step(TEXT("Real redirected airflow pushes cargo up ramp into aircraft"),{},[](FFixture& X)
    {
        if (!X.Mode->bCompleted)
        {
            if (X.After(25))
            {
                X.Test->AddError(FString::Printf(TEXT("Ramp did not complete: cargo=%s wind=%s"),*FMode::ToUnity(X.Mode->Crate->GetActorLocation()).ToString(),*X.Mode->WindBAt(X.Mode->Crate->GetActorLocation()).ToString()));
                return true;
            }
            return false;
        }
        if (X.Session->GetScreen()!=EBDAlphaScreen::Results) return false;
        X.Test->TestTrue(TEXT("Physical loading stage was actually used"),X.Mode->HasGuidedCargo());
        X.Test->TestTrue(TEXT("Whole crate fits final aircraft goal"),X.Mode->Goal->ContainsCrate(X.Mode->Crate));
        X.Test->TestEqual(TEXT("Airport routes into common results screen"),X.Session->GetScreen(),EBDAlphaScreen::Results);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportCartPhysics,"BlastDelivery.Alpha.AirportCartPhysics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportCartPhysics::RunTest(const FString& Parameters)
{
    using namespace BDAirportTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,TEXT("Rested cart accepts a physical blast"),FAction(),[](FFixture& X)
    {
        if (!X.After(.6f)) return false;
        X.PlacePlayer(FVector(32,.95f,-3));
        X.Position=X.Mode->DeflectorCart->GetActorLocation();
        X.Test->TestTrue(TEXT("Compound cart keeps configured crate mass including its plate"),FMath::IsNearlyEqual(X.Mode->DeflectorCart->Mesh->GetMass(),X.Mode->GetPhysicsSettings().CrateMass,.01f));
        X.CartDiagnostic(TEXT("focused before blast"),FVector(34.2f,.4f,-.2f));
        X.Explode(FVector(34.2f,.4f,-.2f));
        X.CartDiagnostic(TEXT("focused after blast"),FVector(34.2f,.4f,-.2f));
        return true;
    }));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,TEXT("Cart translates on its guide after explosion"),FAction(),[](FFixture& X)
    {
        if (!X.After(2.4f)) return false;
        X.CartDiagnostic(TEXT("focused end"),FVector(34.2f,.4f,-.2f));
        X.Test->TestTrue(TEXT("Rail cart moves at least one metre toward ramp"),X.Mode->DeflectorCart->GetActorLocation().X>X.Position.X+100);
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
// These are isolated contract fixtures, not the action-chain walkthrough.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportSourceContracts,"BlastDelivery.Alpha.AirportSourceContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportSourceContracts::RunTest(const FString& Parameters)
{
    using namespace BDAirportTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBoot>(F));
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until))); };
    Step(TEXT("Airport-specific walk, shielding and actual deflector geometry"),{},[](FFixture& X)
    {
        if (!X.After(.3f)) return false;
        X.Test->TestTrue(TEXT("Airport pawn uses isolated movement component"),X.Mode->AirportPlayer->GetCharacterMovement()->IsA<UBDAirportMovement>());
        X.Test->TestEqual(TEXT("Low source rails can be stepped over"),X.Mode->AirportPlayer->GetCharacterMovement()->MaxStepHeight,32.f);
        X.Test->TestTrue(TEXT("Own trajectory and fuse guides are enabled"),X.Mode->SupportsAlphaGuides());
        auto* Probe=X.World->SpawnActor<AStaticMeshActor>();
        Probe->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Probe->SetActorLocation(FMode::FromUnity(FVector(-6.4f,.14f,6.3f)));
        FHitResult Hit; FCollisionQueryParams Q;
        const FVector Start=FMode::FromUnity(FVector(-6.4f,.14f,5.6f));
        X.Test->TestTrue(TEXT("Low rail remains a physical obstacle"),X.World->LineTraceSingleByChannel(Hit,Start,Probe->GetActorLocation(),ECC_Visibility,Q));
        X.Test->TestTrue(TEXT("Layer-zero rail does not shield explosions"),X.Mode->HasBlastLineOfSight(Start,Probe,nullptr));
        Probe->SetActorLocation(FMode::FromUnity(FVector(-3,1,-12)));
        X.Test->TestFalse(TEXT("Layer-eight perimeter wall shields explosions"),X.Mode->HasBlastLineOfSight(FMode::FromUnity(FVector(-3,1,-10)),Probe,nullptr));
        Probe->Destroy();
        FVector Point,Flow;
        X.Test->TestTrue(TEXT("Engine ray actually intersects compound deflector plate"),X.Mode->TryGetDeflectedWind(Point,Flow));
        X.Test->TestTrue(TEXT("Plate normal reflects left jet north"),Flow.Z>.99f && FMath::Abs(Flow.X)<.01f);
        X.PlaceCrate(X.Mode->DeflectorCart,FVector(36,.6f,3));
        X.Test->TestFalse(TEXT("Plate outside the jet produces no reflected branch"),X.Mode->TryGetDeflectedWind(Point,Flow));
        X.PlaceCrate(X.Mode->DeflectorCart,FVector(36,.6f,-.2f));
        X.PlacePlayer(FVector(9.8f,.95f,-1.2f));
        X.Explode(FVector(9.8f,.4f,-2.2f));
        X.Test->TestFalse(TEXT("Ground blast does not bypass cargo-only gate"),X.Mode->IsDockBlastFlight());
        X.Mode->AirportPlayer->GetCharacterMovement()->StopMovementImmediately();
        ABDBomb* Held=X.Mode->DrawBomb(X.Mode->AirportPlayer);
        if (X.Test->TestNotNull(TEXT("Self-blast bomb available"),Held)) Held->Explode();
        X.Test->TestTrue(TEXT("Held self-blast opens temporary dock passage"),X.Mode->IsDockBlastFlight());
        X.PlacePlayer(FVector(4,.95f,7));
        return true;
    });
    Step(TEXT("Ambient-wind recovery discards cached personal momentum"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(34.9f,.95f,6));
        X.Mode->AirportPlayer->GetCharacterMovement()->Velocity=FVector(300,0,0);
    },[](FFixture& X)
    {
        if (!X.After(.15f)) return false;
        X.Mode->AirportPlayer->Respawn();
        return true;
    });
    Step(TEXT("Recovered courier has no retained exhaust or pre-death momentum"),{},[](FFixture& X)
    {
        if (!X.After(.8f)) return false;
        X.Test->TestFalse(TEXT("Wind recovery completed"),X.Mode->AirportPlayer->IsRecovering());
        X.Test->TestTrue(TEXT("No cached velocity is restored after respawn"),X.Mode->AirportPlayer->GetCharacterMovement()->Velocity.Size2D()<10.f);
        return true;
    });
    Step(TEXT("Camera capture cancels held bomb and preserves fixed safe destination"),[](FFixture& X)
    {
        X.PlacePlayer(FVector(-4,.95f,8));
        X.Mode->DrawBomb(X.Mode->AirportPlayer);
    },[](FFixture& X)
    {
        if (!X.After(.9f)) return false;
        X.Test->TestTrue(TEXT("Camera detected and began recovery"),X.Mode->GetPlayerRespawns()>0);
        X.Test->TestEqual(TEXT("Held bomb is destroyed, not dropped live"),X.Mode->GetOwnedBombCount(X.Mode->AirportPlayer),0);
        return true;
    });
    Step(TEXT("Camera recovery ends at control-room safe point"),{},[](FFixture& X)
    {
        if (!X.After(.8f)) return false;
        X.Test->TestTrue(TEXT("Safe destination survives recovery delay"),FVector::Dist2D(X.Mode->AirportPlayer->GetActorLocation(),FMode::FromUnity(FVector(4,.95f,3.5f)))<10);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
