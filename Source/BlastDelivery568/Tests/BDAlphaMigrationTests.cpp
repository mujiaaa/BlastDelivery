#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAlphaSession.h"
#include "../BDAlphaMenu.h"
#include "../BDAlphaPlayground.h"
#include "../BDMirrorLab.h"
#include "../BDAirport.h"
#include "../BDJungle.h"
#include "../BDTutorialGameMode.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDPlayerController.h"
#include "../BDPuzzleActors.h"
#include "../BDVisuals.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"

namespace BDAlphaMigrationTests
{
class FRun : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Level=0,Stage=0;
    double Deadline=FPlatformTime::Seconds()+180.;
    float WorldMark=0;
    int32 Used=0;
    FVector MenuPosition=FVector::ZeroVector;
    TWeakObjectPtr<ABDCharacter> Courier;
    TWeakObjectPtr<ABDBomb> Bomb;
    TWeakObjectPtr<AStaticMeshActor> Floor;
    TWeakObjectPtr<ABDBarrier> Wall;
    static void Key(ABDPlayerController* PC,FKey K,EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Event,Event==IE_Released ? 0.f : 1.f));
        PC->PlayerTick(PC->IsPaused() ? 0.f : 1.f/120.f);
    }
    bool Ready(ABDGameMode* Mode,ABDPlayerController* PC) const
    {
        if (!Mode || !PC || !PC->GetPawn() || !Mode->IsGameplayInputAllowed()) return false;
        switch (Level)
        {
        case 0: return Cast<ABDTutorialGameMode>(Mode)!=nullptr;
        case 1: return Cast<ABDAlphaPlaygroundGameMode>(Mode) && Cast<ABDAlphaPlaygroundGameMode>(Mode)->IsRoomReady();
        case 2: return Cast<ABDAlphaMirrorGameMode>(Mode) && Cast<ABDAlphaMirrorGameMode>(Mode)->IsRoomReady();
        case 3: return Cast<ABDAlphaAirportGameMode>(Mode) && Cast<ABDAlphaAirportGameMode>(Mode)->IsRoomReady();
        case 4: return Cast<ABDJungleGameMode>(Mode)!=nullptr;
        default: return false;
        }
    }
    void ClearProbe()
    {
        if (Bomb.IsValid()) Bomb->Destroy(); Bomb.Reset();
        if (Courier.IsValid()) Courier->Destroy(); Courier.Reset();
        if (Wall.IsValid()) Wall->Destroy(); Wall.Reset();
        if (Floor.IsValid()) Floor->Destroy(); Floor.Reset();
    }
public:
    explicit FRun(FAutomationTestBase* In):Test(In) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds()>Deadline)
        { Test->AddError(FString::Printf(TEXT("Alpha migration test timed out in level %d, stage %d"),Level,Stage)); ClearProbe(); return true; }
        UWorld* World=GEditor->PlayWorld; if (!World) return false;
        auto* Session=World->GetGameInstance<UBDAlphaSession>(); if (!Session) return false;
        auto* Mode=World->GetAuthGameMode<ABDGameMode>();
        auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        if (Stage==0) { Session->StartLevel(Level); Stage=1; return false; }
        if (Stage<=3 && !Ready(Mode,PC)) return false;
        if (Stage==1)
        {
            Test->AddInfo(FString::Printf(TEXT("Real collision fuse regression in %s"),*UBDAlphaSession::LevelTitle(Level)));
            Session->ToggleTuning();
            Key(PC,EKeys::Gamepad_FaceButton_Right,IE_Pressed);
            Key(PC,EKeys::Gamepad_FaceButton_Right,IE_Released);
            Test->TestFalse(TEXT("Controller Back closes a live console without leaving the level"),Session->IsTuningVisible());
            Test->TestEqual(TEXT("Closing console keeps gameplay active"),Session->GetScreen(),EBDAlphaScreen::Playing);
            if (Level==0 && Mode->Crate)
            {
                const FVector Initial=Mode->Crate->GetActorLocation();
                Key(PC,EKeys::Backslash,IE_Pressed); Key(PC,EKeys::Backslash,IE_Released);
                Test->TestTrue(TEXT("Tutorial ignores the delivery debug shortcut"),Mode->Crate->GetActorLocation().Equals(Initial,.01));
            }
            Session->SetFeature(TEXT("fastFuse"),true);
            Session->SetParameter(TEXT("CollisionFuseMultiplier"),8);
            Session->SetParameter(TEXT("FuseSeconds"),8);
            Test->TestTrue(TEXT("Every level accepts the global fast impact fuse setting"),Mode->GetPhysicsSettings().bFastCollisionFuse);
            // A small isolated physical fixture avoids triggering the authored
            // tutorial/airport/jungle goals while testing each actual game mode.
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Floor=World->SpawnActor<AStaticMeshActor>(FVector(20000,20000,-50),FRotator::ZeroRotator,P);
            auto* Mesh=Floor->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
            BDVisuals::Shape(Mesh,TEXT("Cube")); Mesh->SetWorldScale3D(FVector(20,20,1));
            Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Wall=World->SpawnActor<ABDBarrier>(FVector(20230,20000,0),FRotator::ZeroRotator,P);
            Wall->Configure(FVector(30,1000,300));
            Courier=World->SpawnActor<ABDCharacter>(FVector(20000,20000,95),FRotator::ZeroRotator,P);
            Courier->FacePoint(FVector(21000,20000,95));
            Bomb=Mode->DrawBomb(Courier.Get());
            if (!Test->TestNotNull(TEXT("Fresh bomb is created using this level's physics profile"),Bomb.Get())) { ClearProbe(); return true; }
            Test->TestEqual(TEXT("New bomb captures the fast-fuse multiplier"),Bomb->GetFuseBurnRate(),1.f);
            Bomb->Release(true,false);
            Session->SetFeature(TEXT("fastFuse"),false);
            Test->TestTrue(TEXT("Existing bomb retains its captured feature when the console changes"),Bomb->GetProfile().bFastCollisionFuse);
            WorldMark=World->GetTimeSeconds(); Stage=2; return false;
        }
        if (Stage==2)
        {
            if (!Bomb.IsValid()) { Test->AddError(TEXT("Thrown collision-fuse fixture vanished before its first impact")); ClearProbe(); return true; }
            if (!Bomb->HasCollisionFuseAccelerated())
            {
                if (World->GetTimeSeconds()-WorldMark<1.5f) return false;
                Test->AddError(TEXT("Real wall/ground collision failed to accelerate the bomb fuse")); ClearProbe(); return true;
            }
            Test->TestEqual(TEXT("First physical impact multiplies fuse burning by eight"),Bomb->GetFuseBurnRate(),8.f);
            Bomb->Collision->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Bomb->SetActorLocation(Courier->GetActorLocation()+FVector(90,0,15),false,nullptr,ETeleportType::TeleportPhysics);
            const float Remaining=Bomb->GetRemainingFuse(); Used=Mode->BombsUsed;
            Test->TestTrue(TEXT("The live thrown bomb can be picked up again"),Bomb->TryPickup(Courier.Get()));
            Test->TestTrue(TEXT("Pickup preserves remaining fuse exactly"),FMath::IsNearlyEqual(Bomb->GetRemainingFuse(),Remaining,.001f));
            Test->TestEqual(TEXT("Pickup preserves the accelerated burn rate"),Bomb->GetFuseBurnRate(),8.f);
            Test->TestEqual(TEXT("Pickup does not count a new bomb"),Mode->BombsUsed,Used);
            Bomb->Release(false,false);
            Test->TestEqual(TEXT("Putting the caught bomb down does not reset acceleration"),Bomb->GetFuseBurnRate(),8.f);
            Bomb->Destroy(); Bomb.Reset();
            Session->SetFeature(TEXT("fastFuse"),true);
            Bomb=Mode->DrawBomb(Courier.Get());
            if (!Test->TestNotNull(TEXT("Another fresh bomb can be drawn"),Bomb.Get())) { ClearProbe(); return true; }
            Bomb->Release(false,false);
            WorldMark=World->GetTimeSeconds(); Stage=3; return false;
        }
        if (Stage==3)
        {
            if (World->GetTimeSeconds()-WorldMark<.3f) return false;
            if (!Test->TestNotNull(TEXT("Dropped bomb remains present after contacting the floor"),Bomb.Get())) { ClearProbe(); return true; }
            Test->TestFalse(TEXT("A fresh right-click drop does not trigger throw-only fast fuse"),Bomb->HasCollisionFuseAccelerated());
            Test->TestEqual(TEXT("Dropped bomb uses its normal burn rate"),Bomb->GetFuseBurnRate(),1.f);
            ClearProbe();
            if (++Level<UBDAlphaSession::LevelCount) { Stage=0; return false; }
            Session->ResetTuning(); Session->StartLevel(2); Stage=4; return false;
        }
        if (Stage==4)
        {
            auto* Mirrors=Cast<ABDAlphaMirrorGameMode>(Mode);
            if (!Mirrors || !Mirrors->IsRoomReady() || !PC || Session->GetScreen()!=EBDAlphaScreen::Playing) return false;
            Key(PC,EKeys::Backslash,IE_Pressed); Key(PC,EKeys::Backslash,IE_Released);
            Stage=5; return false;
        }
        if (Stage==5)
        {
            if (Session->GetScreen()!=EBDAlphaScreen::Results) return false;
            Test->TestTrue(TEXT("Debug shortcut reaches real delivery results"),Mode && Mode->bCompleted);
            Test->TestTrue(TEXT("Debug completion is recorded as a practice run"),Session->AreResultsPractice());
            Test->TestTrue(TEXT("Successful delivery always earns its completion star"),(Session->GetResultStarMask()&1)!=0);
            Session->ShowMainMenu(); Stage=6; return false;
        }
        if (Stage==6)
        {
            if (!Cast<ABDAlphaMenuGameMode>(Mode) || !PC || !PC->GetPawn() || Session->GetScreen()!=EBDAlphaScreen::MainMenu
                || World->GetTimeSeconds()<.3f) return false;
            auto* Player=Cast<ABDCharacter>(PC->GetPawn()); if (!Player) return false;
            Session->SetSimpleControls(true); MenuPosition=Player->GetActorLocation();
            Key(PC,EKeys::D,IE_Pressed); WorldMark=World->GetTimeSeconds(); Stage=7; return false;
        }
        if (Stage==7)
        {
            if (World->GetTimeSeconds()-WorldMark<.3f) return false;
            auto* Player=Cast<ABDCharacter>(PC->GetPawn());
            Test->TestTrue(TEXT("Main menu WASD moves a real physical courier"),Player->GetActorLocation().X>MenuPosition.X+30.f);
            Test->TestEqual(TEXT("Lobby is not counted as a timed delivery attempt"),Mode->ElapsedSeconds,0.f);
            Key(PC,EKeys::D,IE_Released);
            FVector2D Aim; PC->ProjectWorldLocationToScreen(Player->GetActorLocation()+FVector(250,0,0),Aim);
            PC->SetMouseLocation(FMath::RoundToInt(Aim.X),FMath::RoundToInt(Aim.Y));
            Key(PC,EKeys::LeftMouseButton,IE_Pressed); Bomb=Mode->GetBombForPlayer(Player);
            if (!Test->TestNotNull(TEXT("Main menu left press holds a live bomb"),Bomb.Get())) return true;
            Test->TestTrue(TEXT("Simple mode keeps the bomb held until release"),Bomb->IsHeld());
            Key(PC,EKeys::LeftMouseButton,IE_Released);
            Test->TestTrue(TEXT("Main menu left release throws the same bomb"),Bomb->WasThrown() && !Bomb->IsHeld());
            Bomb->Destroy(); Bomb.Reset(); PC->ResetBombInput();
            Key(PC,EKeys::RightMouseButton,IE_Pressed); Bomb=Mode->GetBombForPlayer(Player);
            if (!Test->TestNotNull(TEXT("Main menu right press holds another bomb"),Bomb.Get())) return true;
            Key(PC,EKeys::RightMouseButton,IE_Released);
            Test->TestTrue(TEXT("Main menu right release drops instead of throws"),!Bomb->IsHeld() && !Bomb->WasThrown());
            Bomb->Destroy(); Bomb.Reset(); PC->ResetBombInput();
            Test->TestFalse(TEXT("Main menu interaction remains unpaused"),UGameplayStatics::IsGamePaused(World));
            return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaMigrationTest,"BlastDelivery.Alpha.Migration.LivePhysicsAndMenu",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAlphaMigrationTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDAlphaMigrationTests::FRun>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
