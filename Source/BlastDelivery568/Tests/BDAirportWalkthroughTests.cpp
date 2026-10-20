#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAirport.h"
#include "../BDAlphaSession.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDPlayerController.h"
#include "../BDPuzzleActors.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

// Like the donor's AirportWalkthrough, this is an ASSISTED action chain:
// player standing/aiming and dropped-bomb placement are staged. Cargo, weight
// and cart are NEVER teleported, reset, or given synthetic impulses. Every bomb
// burns its real fuse, and the shuttle, conveyor and wind use production code.
namespace BDAirportWalkthrough
{
using FMode=ABDAlphaAirportGameMode;
class FRun final : public IAutomationLatentCommand
{
    struct FStep { FString Name; TFunction<void()> Begin; TFunction<bool(float)> Tick; float Limit; };
    FAutomationTestBase* Test;
    UWorld* World=nullptr;
    FMode* Mode=nullptr;
    UBDAlphaSession* Session=nullptr;
    ABDPlayerController* PC=nullptr;
    TArray<FStep> Steps;
    int32 Index=0;
    bool bRequested=false,bEntered=false,bFailed=false,bReleased=false;
    double Deadline=0.;
    float Began=0,NextTrace=0;
    FVector Aim,DeliveredCargo;
    TWeakObjectPtr<ABDBomb> CurrentBomb;
    TWeakObjectPtr<ABDCrate> OriginalCargo,OriginalWeight,OriginalCart;
    FVector CargoP() const { return FMode::ToUnity(Mode->Crate->GetActorLocation()); }
    FVector WeightP() const { return FMode::ToUnity(Mode->Counterweight->GetActorLocation()); }
    FVector CartP() const { return FMode::ToUnity(Mode->DeflectorCart->GetActorLocation()); }
    FVector PlayerP() const { return FMode::ToUnity(Mode->AirportPlayer->GetActorLocation()); }
    void Stand(const FVector& Position,const FVector& Target)
    {
        auto* Player=Mode->AirportPlayer.Get();
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->ConsumeMovementInputVector();
        Player->SetActorLocation(FMode::FromUnity(Position),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        Aim=FMode::FromUnity(Target);
        Player->FacePoint(Aim);
    }
    bool Check(const TCHAR* Name,bool bPassed)
    {
        if (!Test->TestTrue(Name,bPassed)) bFailed=true;
        return bPassed;
    }
    void Draw()
    {
        Mode->AirportPlayer->DrawBomb();
        CurrentBomb=Mode->GetBombForPlayer(Mode->AirportPlayer);
        Check(TEXT("Action creates a real held bomb"),CurrentBomb.IsValid() && CurrentBomb->GetHolder()==Mode->AirportPlayer);
    }
    void PlaceTimedBomb(const FVector& Position)
    {
        Draw(); if (bFailed) return;
        CurrentBomb->Release(false);
        CurrentBomb->SetActorLocation(FMode::FromUnity(Position),false,nullptr,ETeleportType::TeleportPhysics);
        CurrentBomb->Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
        CurrentBomb->Collision->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    }
    void Capture(const TCHAR* Name)
    {
        if (FParse::Param(FCommandLine::Get(),TEXT("BDAirportScreenshots")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AirportRepair")/Name,true,false);
    }
    void Add(const TCHAR* Name,TFunction<void()> Begin,TFunction<bool(float)> Tick,float Limit=35.f)
    { Steps.Add({Name,MoveTemp(Begin),MoveTemp(Tick),Limit}); }
    bool Done()
    {
        if(IsValid(PC)) PC->SetActorTickEnabled(true);
        return true;
    }
    FString Diagnostic() const
    {
        const ABDBomb* Bomb=CurrentBomb.Get();
        return FString::Printf(TEXT("stage=%d screen=%d paused=%d belt=%d reject=%d checkpoint=%d playerRecovering=%d cargoRecovering=%d cargoVelocity=%s weightVelocity=%s bomb=%s fuse=%.2f holder=%s"),
            Mode->GetAirportStage(),int32(Session->GetScreen()),UGameplayStatics::IsGamePaused(World),
            Mode->GetBeltSegment(),Mode->GetRejectionCount(),Mode->GetCheckpointStage(),Mode->AirportPlayer->IsRecovering(),Mode->Crate->IsRecovering(),
            *Mode->Crate->Mesh->GetPhysicsLinearVelocity().ToString(),*Mode->Counterweight->Mesh->GetPhysicsLinearVelocity().ToString(),
            Bomb ? *FMode::ToUnity(Bomb->GetActorLocation()).ToString() : TEXT("gone"),Bomb ? Bomb->GetRemainingFuse() : -1.f,
            Bomb && Bomb->GetHolder() ? *Bomb->GetHolder()->GetName() : TEXT("none"));
    }
    void Configure()
    {
        Add(TEXT("Load and reject cargo using a timed blast"),[this]
        { Stand(FVector(-10,.95,-10),FVector(-10,.95,-7)); PlaceTimedBomb(FVector(-10,.4,-9.8)); },[this](float T)
        {
            if(T>4.4f) Stand(FVector(4,.95,3),FVector(-10,.95,-4.5));
            if(Mode->GetRejectionCount()<1 || Mode->GetBeltSegment()>=0) return false;
            Check(TEXT("Powered scanner returns original cargo"),!Mode->IsCargoAccepted() && CargoP().X < -9);
            Capture(TEXT("01-rejected.png")); return true;
        });
        Add(TEXT("Smoke and a physical bank shot move counterweight"),[this]
        {
            Mode->DeploySmoke(FMode::FromUnity(FVector(-5.2,.1,9.1)));
            Stand(FVector(-9,.95,7),FVector(-6,.95,7)); PlaceTimedBomb(FVector(-7.95,.4,7));
            Stand(FVector(-9,.95,3),FVector(-6,.95,7));
        },[this](float T)
        {
            if(T<6.2f || (T<9.f && !(WeightP().X>-6 && WeightP().Z>7.15))) return false;
            Check(TEXT("Bank wall redirects counterweight after real blast"),WeightP().X>-6 && WeightP().Z>7.15);
            return true;
        });
        Add(TEXT("Second real blast holds weight on pressure plate"),[this]
        {
            Stand(FVector(-5,.95,8.2),FVector(-2.7,.95,10.7));
            PlaceTimedBomb(WeightP()+FVector(0,-.2,-1.2));
        },[this](float T)
        {
            if(T<6.f || (T<9.f && !Mode->IsWeightOnPlate())) return false;
            Check(TEXT("Weight stays on plate and powers X-ray off"),Mode->IsWeightOnPlate() && !Mode->IsXrayPowered());
            Capture(TEXT("02-weight.png")); return true;
        });
        Add(TEXT("Reload original cargo with a real blast"),[this]
        { Stand(FVector(-10,.95,-10),FVector(-10,.95,-7)); PlaceTimedBomb(FVector(-10,.4,-9.8)); },[this](float T)
        {
            if(T>4.4f) Stand(FVector(4,.95,3),FVector(12.8,.6,0));
            if(!Mode->IsCargoAccepted()) return false;
            Check(TEXT("Cargo passes scanner and reaches receiving island"),CargoP().X>12);
            Capture(TEXT("03-received.png")); return true;
        });
        Add(TEXT("Cook and throw from player dock onto cargo dock"),[this]
        { Stand(FVector(9.8,.95,0),FVector(16,.95,0)); Draw(); bReleased=false; },[this](float T)
        {
            if(!bReleased && T>=2.6f) { Mode->AirportPlayer->ThrowBomb(); bReleased=true; }
            if(T<4.8f || (T<7.f && !Mode->IsCargoAboard())) return false;
            Check(TEXT("A real cooked throw loads cargo onto shuttle"),Mode->IsCargoAboard());
            return true;
        });
        Add(TEXT("Held bomb blast jumps courier onto shuttle"),[this]
        { Stand(FVector(10.2,.95,-1.2),FVector(0,.95,-1.2)); Draw(); },[this](float T)
        {
            if(T<6.2f || (T<8.f && !Mode->IsPlayerAboard())) return false;
            Check(TEXT("Player boards by actual held recoil"),Mode->IsPlayerAboard());
            Capture(TEXT("04-boarded.png")); return true;
        });
        Add(TEXT("Ride production shuttle across the gap"),[]{},[this](float)
        {
            if(FMode::ToUnity(Mode->GetShuttlePosition()).X<28.45f || Mode->IsShuttleMoving()) return false;
            Check(TEXT("Both actors remain aboard at destination"),Mode->IsCargoAboard() && Mode->IsPlayerAboard());
            return true;
        });
        Add(TEXT("Unload cargo with a real timed blast"),[this]
        {
            const FVector Car=FMode::ToUnity(Mode->GetShuttlePosition());
            Stand(Car+FVector(-1.65,.95,-.9),Car+FVector(5,0,0)); PlaceTimedBomb(Car+FVector(-1.75,.4,0));
        },[this](float T)
        {
            if(T<5.8f || (T<8.f && CargoP().X<=31.1f)) return false;
            Check(TEXT("Cargo actually blasts off shuttle onto apron"),CargoP().X>31.1f);
            return true;
        });
        Add(TEXT("Blast jump onto left apron"),[this]
        {
            const FVector Car=FMode::ToUnity(Mode->GetShuttlePosition());
            Stand(Car+FVector(.8,.95,-.5),Car+FVector(-4,0,-10)); Draw();
        },[this](float T)
        {
            if(T<7.f || Mode->GetAirportStage()!=2) return false;
            Check(TEXT("Both actors reach left approach before wind puzzle"),PlayerP().X<33.15f && CargoP().X<33.15f && !Mode->bCompleted);
            DeliveredCargo=CargoP(); Capture(TEXT("05-apron.png")); return true;
        });
        Add(TEXT("Clear A wind with a naturally detonated bomb"),[this]
        { Stand(FVector(31.2,.95,4.6),FVector(34.925,.4,4.6)); PlaceTimedBomb(FVector(34.925,.4,4.6)); },[this](float T)
        {
            if(T<4.15f) return false;
            Check(TEXT("Bomb creates temporary connected A-wind clearance"),Mode->GetWindAClearSeconds()>2.8f);
            return true;
        });
        Add(TEXT("Walk through cleared wind to A control"),[this]
        { Stand(FVector(32.45,.95,4.6),FVector(37.1,.65,5.6)); },[this](float T)
        {
            if(T<1.3f) { Mode->AirportPlayer->MoveInRoom(FVector2D(1,0)); return false; }
            Mode->AirportPlayer->MoveInRoom(FVector2D::ZeroVector);
            if(T<1.4f) return false;
            Check(TEXT("Player crosses A wind by movement, not teleport"),PlayerP().X>36.75f);
            Check(TEXT("Nearby E interaction shuts A down"),Mode->PressEngineControl() && Mode->HasShutdownA() && !Mode->IsEngineARunning());
            Capture(TEXT("06-a-off.png")); return true;
        });
        Add(TEXT("Blast cargo north clear of cart lane"),[this]
        {
            Stand(DeliveredCargo+FVector(0,.4,-3.5),DeliveredCargo);
            PlaceTimedBomb(DeliveredCargo+FVector(0,-.2,-2.2));
        },[this](float T)
        {
            if(T<6.1f || (T<9.f && !(CargoP().Z>1.5f && CargoP().Z<3.2f))) return false;
            Check(TEXT("Northward cargo blast clears cart working lane"),CargoP().Z>1.5f && CargoP().Z<3.2f); return true;
        });
        Add(TEXT("Blast cargo east toward ramp"),[this]
        {
            const FVector Cargo=CargoP(); Stand(Cargo+FVector(-1.4,.4,1.4),Cargo);
            PlaceTimedBomb(Cargo+FVector(-1,-.2,0));
        },[](float T){return T>=6.1f;});
        Add(TEXT("Use weaker blast to align cargo at ramp"),[this]
        {
            const FVector Cargo=CargoP(); Stand(Cargo+FVector(-3,.4,1.4),Cargo);
            PlaceTimedBomb(Cargo+FVector(-2,-.2,0));
        },[this](float T)
        {
            if(T<6.1f) return false;
            Check(TEXT("Bombs stage cargo while cart remains misaligned"),FMath::Abs(CargoP().X-39.3f)<1.4f && CargoP().Z>1.5f && CargoP().Z<3.2f && !Mode->IsCartAligned());
            Capture(TEXT("07-ramp-staging.png")); return true;
        });
        Add(TEXT("Start B from its own control"),[this]
        {
            Stand(FVector(45.7,.95,5.1),FVector(47,.8,5.1));
            Check(TEXT("B starts while A stays off"),Mode->PressEngineControl() && Mode->IsEngineBRunning() && !Mode->IsEngineARunning());
        },[](float T){return T>=.5f;});
        Add(TEXT("Real blast aligns deflector cart"),[this]
        {
            const FVector Cart=CartP(); Stand(Cart+FVector(-3,.4,0),Cart);
            PlaceTimedBomb(Cart+FVector(-1.8,-.2,0));
        },[this](float T)
        {
            if(T<6.2f) return false;
            Check(TEXT("Cart reaches airflow alignment through bomb impulse"),Mode->IsCartAligned());
            Check(TEXT("Cart blast does not interrupt engine B"),Mode->GetWindBClearSeconds()<=0.f);
            Capture(TEXT("08-b-loading.png")); return true;
        });
        Add(TEXT("Live reflected wind delivers and settles original cargo"),[]{},[this](float)
        {
            if(!Mode->bCompleted) return false;
            Check(TEXT("Actual airflow guides entire cargo into aircraft"),Mode->HasGuidedCargo() && Mode->Goal->ContainsCrate(Mode->Crate));
            Session->ShowResults();
            Check(TEXT("Completed chain enters shared results"),Session->GetScreen()==EBDAlphaScreen::Results);
            Capture(TEXT("09-complete.png")); return true;
        },30.f);
        Add(TEXT("Render completed shared result overlay"),[]{},[](float){return true;});
    }
public:
    explicit FRun(FAutomationTestBase* In):Test(In) {}
    bool Update() override
    {
        World=GEditor->PlayWorld;
        Session=World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
        Mode=World ? World->GetAuthGameMode<FMode>() : nullptr;
        if(Deadline==0.) Deadline=FPlatformTime::Seconds()+420.;
        if(FPlatformTime::Seconds()>Deadline) { Test->AddError(TEXT("Assisted Airport action chain timed out")); return Done(); }
        if(!bRequested && Session) { bRequested=true; Session->ResetTuning(); Session->StartLevel(3); return false; }
        if(!Mode || !Mode->IsRoomReady() || !Session) return false;
        if(!PC)
        {
            PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
            if(!PC) return false;
            // Standing/aim are the documented assistance, not arbitrary live
            // mouse input from another window. Character simulation still ticks.
            PC->SetActorTickEnabled(false); Session->MarkPractice(); Configure();
            OriginalCargo=Mode->Crate; OriginalWeight=Mode->Counterweight; OriginalCart=Mode->DeflectorCart;
            Aim=Mode->AirportPlayer->GetActorLocation()+FVector(500,0,0);
            Test->AddInfo(TEXT("ASSISTED: player standing/aim and drop placement staged; cargo/weight/cart always natural, real fuses and forces."));
        }
        if(bFailed || Index>=Steps.Num()) return Done();
        if(Mode->Crate!=OriginalCargo.Get() || Mode->Counterweight!=OriginalWeight.Get() || Mode->DeflectorCart!=OriginalCart.Get())
        { Test->AddError(TEXT("Action chain replaced an original puzzle body")); return Done(); }
        if(Mode->Crate->IsRecovering() || Mode->Counterweight->IsRecovering() || Mode->DeflectorCart->IsRecovering())
        { Test->AddError(TEXT("A puzzle body fell out during the natural action chain: ")+Diagnostic()); return Done(); }
        auto& Step=Steps[Index];
        Mode->AirportPlayer->FacePoint(Aim);
        if(!bEntered)
        {
            // The original helper also allows player setbacks. Wait for the
            // production recovery instead of staging a new action mid-respawn.
            // Puzzle bodies remain untouched and keep their physical progress.
            if(Mode->AirportPlayer->IsRecovering()) return false;
            Began=World->GetTimeSeconds(); NextTrace=Began; bEntered=true;
            Test->AddInfo(FString::Printf(TEXT("AIRPORT_ACTION %02d %s; %s"),Index+1,*Step.Name,*Diagnostic()));
            Step.Begin(); if(bFailed) return false;
        }
        const float T=World->GetTimeSeconds()-Began;
        if(World->GetTimeSeconds()>=NextTrace)
        {
            UE_LOG(LogTemp,Display,TEXT("AIRPORT_ACTION_TRACE step=%d t=%.2f cargo=%s weight=%s cart=%s player=%s"),Index+1,T,*CargoP().ToString(),*WeightP().ToString(),*CartP().ToString(),*PlayerP().ToString());
            NextTrace=World->GetTimeSeconds()+1.f;
        }
        if(Step.Tick(T)) { ++Index; bEntered=false; }
        else if(T>Step.Limit)
        { Test->AddError(TEXT("Action chain did not progress: ")+Step.Name+TEXT("; ")+Diagnostic()); bFailed=true; }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportWalkthroughTest,"BlastDelivery.Alpha.AirportWalkthrough.RealActionChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportWalkthroughTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDAirportWalkthrough::FRun>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
