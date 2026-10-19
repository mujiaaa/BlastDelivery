#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDJungle.h"
#include "../BDBomb.h"
#include "../BDCrate.h"
#include "../BDPuzzleActors.h"
#include "../BDPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

namespace BDJungleTests
{
struct FFixture
{
    FAutomationTestBase* Test=nullptr;
    UWorld* World=nullptr;
    ABDJungleGameMode* Mode=nullptr;
    ABDJungleCharacter* Player=nullptr;
    ABDPlayerController* PC=nullptr;
    ABDWildMan* Guard=nullptr;
    ABDWildMan* Keeper=nullptr;
    TWeakObjectPtr<ABDBomb> Banana;
    TWeakObjectPtr<ABDBomb> FirstMeal, SecondMeal;
    float BananaFuse=0;
    double Started=0;
    bool Failed=false;
    FVector Saved, CrateStart;
    int32 TargetIndex=0, Shots=0;
    float LastShot=-10;
    bool After(float Seconds) const { return FPlatformTime::Seconds()-Started>Seconds; }
    void Refresh()
    {
        World=GEditor->PlayWorld;
        if (!World) return;
        Mode=World->GetAuthGameMode<ABDJungleGameMode>();
        Player=Cast<ABDJungleCharacter>(UGameplayStatics::GetPlayerPawn(World,0));
        PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        for (TActorIterator<ABDWildMan> It(World); It; ++It)
        {
            if (It->bPassageGuard) Guard=*It;
            if (It->bGateOpener) Keeper=*It;
        }
    }
    void Place(FVector P)
    {
        Player->EndSlide();
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    void Aim(FVector D)
    {
        Player->FacePoint(Player->GetActorLocation()+D);
        FVector2D Screen;
        if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation()+D,Screen))
            PC->SetMouseLocation(FMath::RoundToInt(Screen.X),FMath::RoundToInt(Screen.Y));
    }
    void Key(FKey K,EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Event,Event==IE_Released?0.f:1.f));
        PC->PlayerTick(1.f/120.f);
    }
    void Shot(const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(),TEXT("BDJungleScreenshots"))) return;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Automation/JungleShots");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FViewport* View=World->GetGameViewport()->Viewport;
        TArray<FColor> Pixels;
        if (View && View->ReadPixels(Pixels))
        {
            TArray64<uint8> PNG;
            const FIntPoint Size=View->GetSizeXY();
            FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));
        }
    }
    void ClearBomb() { if (Mode->ActiveBomb) Mode->ActiveBomb->Destroy(); }
};
using FAction=TFunction<void(FFixture&)>;
using FCondition=TFunction<bool(FFixture&)>;
class FStep : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Waiting=FPlatformTime::Seconds();
    float Timeout;
    bool Entered=false;
public:
    FStep(TSharedRef<FFixture> InF,FString InName,FAction InBegin,FCondition InUntil,float InTimeout)
        :F(InF),Name(InName),Begin(MoveTemp(InBegin)),Until(MoveTemp(InUntil)),Timeout(InTimeout) {}
    virtual bool Update() override
    {
        if (F->Failed) return true;
        F->Refresh();
        if (!Entered && F->Mode && F->Player && F->PC)
        {
            Entered=true; F->Started=Waiting=FPlatformTime::Seconds();
            F->Test->AddInfo(Name); UE_LOG(LogTemp,Display,TEXT("JUNGLE TEST: %s"),*Name);
            if (Begin) Begin(*F);
        }
        if (Entered && Until(*F)) return true;
        if (FPlatformTime::Seconds()-Waiting>Timeout)
        {
            F->Test->AddError(TEXT("Timed out: ")+Name);
            if (F->Player && F->Mode->Crate)
                F->Test->AddInfo(FString::Printf(TEXT("Player=%s crate=%s slide=%f route=%d shots=%d guard=%s"),
                    *F->Player->GetActorLocation().ToString(),*F->Mode->Crate->GetActorLocation().ToString(),F->Player->GetSlideRemaining(),
                    F->TargetIndex,F->Shots,F->Guard?*F->Guard->GetActorLocation().ToString():TEXT("none")));
            F->Shot(TEXT("failure.png")); F->Failed=true; return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDJungleTest,"BlastDelivery.Jungle.MechanicsAndDelivery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDJungleTest::RunTest(const FString& Parameters)
{
    using namespace BDJungleTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/JungleBananaTrap")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until,float Timeout=20.f)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until),Timeout)); };
    Step(TEXT("Saved Jungle map has navigation, two zones, crate and separate rules"),[](FFixture& X)
    {
        X.World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(1440,1000);
    },[](FFixture& X)
    {
        if (!X.After(2)) return false;
        X.PC->SetActorTickEnabled(false);
        X.Test->TestNotNull(TEXT("Crate"),X.Mode->Crate.Get());
        X.Test->TestNotNull(TEXT("Goal"),X.Mode->Goal.Get());
        X.Test->TestNotNull(TEXT("Guardian"),X.Guard);
        X.Test->TestNotNull(TEXT("Lower-right gatekeeper"),X.Keeper);
        X.Test->TestTrue(TEXT("Long eating window"),X.Mode->Settings->EatingSeconds>=20.f);
        X.Test->TestFalse(TEXT("Passage starts guarded"),X.Mode->bPassageCleared);
        int32 Zones=0; for (TActorIterator<ABDBananaZone> It(X.World); It; ++It) ++Zones;
        X.Test->TestEqual(TEXT("Exactly two zones"),Zones,2);
        X.CrateStart=X.Mode->Crate->GetActorLocation();
        X.Mode->Settings->SpawnIntervalMin=200;
        X.Mode->Settings->SpawnIntervalMax=200;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(X.World,X.Guard->GetActorLocation(),X.Mode->Settings->ClearingCenter);
        X.Test->TestTrue(TEXT("Complete nav path around ruins into clearing"),Path && Path->IsValid() && !Path->IsPartial());
        X.Shot(TEXT("01-start.png"));
        return true;
    });
    Step(TEXT("Walk from start onto helpful peel"),[](FFixture& X){X.Aim(FVector(0,-500,0));},[](FFixture& X)
    {
        X.Player->MoveInRoom(FVector2D(0,-1));
        if (!X.Player->IsSliding()) return false;
        X.Saved=X.Player->GetActorLocation();
        return true;
    });
    Step(TEXT("First peel locks facing and moves exactly five tiles"),[](FFixture& X)
    {
        // Account for the partial first tick between the trigger and observation.
        X.Saved=X.Player->GetActorLocation()+FVector(0,800-X.Player->GetSlideRemaining(),0);
        X.Aim(FVector(500,0,0));
    },[](FFixture& X)
    {
        X.Player->MoveInRoom(FVector2D(1,0));
        if (X.Player->IsSliding()) return false;
        X.Test->TestTrue(TEXT("Exactly 800 cm"),FMath::IsNearlyEqual(FVector::Dist2D(X.Saved,X.Player->GetActorLocation()),800.f,2.f));
        X.Test->TestTrue(TEXT("No steering during slide"),FMath::Abs(X.Player->GetActorLocation().X-X.Saved.X)<1);
        X.Aim(FVector(0,-500,0)); return true;
    });
    Step(TEXT("Second lane peel reaches Zone A"),{},[](FFixture& X)
    {
        if (!X.Player->IsSliding()) X.Player->MoveInRoom(FVector2D(0,-1));
        if (!X.Mode->bReachedZoneBySlide) return false;
        X.Player->GetCharacterMovement()->StopMovementImmediately();
        X.Shot(TEXT("02-zone-a.png")); return true;
    });
    Step(TEXT("Held banana survives past the regular fuse"),[](FFixture& X)
    {
        X.Aim(FVector(600,0,0)); X.Key(EKeys::RightMouseButton,IE_Pressed);
        X.Test->TestTrue(TEXT("Zone A creates banana"),X.Mode->ActiveBomb && X.Mode->ActiveBomb->IsBanana());
        ABDBomb* First=X.Mode->ActiveBomb;
        X.Banana=First; X.BananaFuse=First->GetRemainingFuse();
        X.Player->DrawBomb(); X.Test->TestEqual(TEXT("One active bomb"),X.Mode->ActiveBomb.Get(),First);
    },[](FFixture& X)
    {
        if (!X.After(3.2f)) return false;
        X.Test->TestTrue(TEXT("Held banana persists"),X.Banana.IsValid() && X.Banana->IsHeld());
        if (X.Banana.IsValid()) X.Test->TestEqual(TEXT("Banana fuse does not count down"),X.Banana->GetRemainingFuse(),X.BananaFuse);
        return true;
    });
    Step(TEXT("LMB throws persistent banana into side clearing"),[](FFixture& X)
    {
        X.Key(EKeys::LeftMouseButton,IE_Pressed); X.Key(EKeys::LeftMouseButton,IE_Released); X.Key(EKeys::RightMouseButton,IE_Released);
    },[](FFixture& X)
    {
        if (!X.Banana.IsValid() || !X.Banana->IsRestingBanana() || !X.Guard->IsDistracted()) return false;
        X.Test->TestTrue(TEXT("Guard distracted by real thrown banana"),X.Guard->IsDistracted());
        X.Test->TestFalse(TEXT("Landing alone does not remove barricade"),X.Mode->Passage->bOpen);
        X.Test->TestTrue(TEXT("Banana did not move crate"),FVector::Dist2D(X.CrateStart,X.Mode->Crate->GetActorLocation())<1);
        return true;
    });
    Step(TEXT("Upper wild man picks up banana and immediately frees bomb slot"),{},[](FFixture& X)
    {
        if (!X.Guard->IsEating()) return false;
        X.FirstMeal=X.Banana;
        X.Test->TestTrue(TEXT("Banana remains visibly carried"),X.FirstMeal.IsValid() && X.FirstMeal->GetConsumer()==X.Guard);
        X.Test->TestNull(TEXT("Pickup frees slot before consumption"),X.Mode->ActiveBomb.Get());
        X.Test->TestFalse(TEXT("Feeding upper wild man cannot open barricade"),X.Mode->Passage->bOpen);
        X.TargetIndex=0;
        X.Shot(TEXT("03-guard-eating.png")); return true;
    });
    Step(TEXT("Walk from Zone A to Zone B during the first meal"),{},[](FFixture& X)
    {
        const TArray<FVector> Walk={FVector(600,-240,66),FVector(640,40,66)};
        FVector Delta=Walk[X.TargetIndex]-X.Player->GetActorLocation(); Delta.Z=0;
        if (Delta.Size2D()<15.f)
        {
            if (++X.TargetIndex==Walk.Num())
            {
                X.Player->ConsumeMovementInputVector();
                X.Player->GetCharacterMovement()->StopMovementImmediately();
                X.Test->TestTrue(TEXT("Upper wild man stays eating throughout traversal"),X.Guard->IsEating());
                return true;
            }
            Delta=Walk[X.TargetIndex]-X.Player->GetActorLocation(); Delta.Z=0;
        }
        const FVector D=Delta.GetSafeNormal2D(); X.Player->MoveInRoom(FVector2D(D.X,D.Y));
        return false;
    });
    Step(TEXT("Throw second banana just north of locked gate while first is carried"),[](FFixture& X)
    {
        X.Aim(FVector(270,380,66)-X.Player->GetActorLocation());
        X.Key(EKeys::RightMouseButton,IE_Pressed);
        X.SecondMeal=X.Mode->ActiveBomb;
        X.Test->TestTrue(TEXT("Second banana can be drawn during eating"),X.SecondMeal.IsValid() && X.SecondMeal->IsBanana());
        X.Key(EKeys::LeftMouseButton,IE_Pressed); X.Key(EKeys::LeftMouseButton,IE_Released); X.Key(EKeys::RightMouseButton,IE_Released);
        X.Player->DrawBomb();
        X.Test->TestEqual(TEXT("In-flight second banana still occupies slot"),X.Mode->ActiveBomb.Get(),X.SecondMeal.Get());
    },[](FFixture& X)
    {
        if (!X.SecondMeal.IsValid() || !X.SecondMeal->IsRestingBanana()) return false;
        X.Test->TestTrue(TEXT("Old banana remains with upper eater"),X.FirstMeal.IsValid() && X.FirstMeal->GetConsumer()==X.Guard);
        X.Player->DrawBomb();
        X.Test->TestEqual(TEXT("Free ground banana blocks another draw"),X.Mode->ActiveBomb.Get(),X.SecondMeal.Get());
        X.Test->TestFalse(TEXT("Gate does not open remotely on landing"),X.Mode->Passage->bOpen);
        return true;
    });
    Step(TEXT("Lower-right wild man approaches and opens gate before eating"),{},[](FFixture& X)
    {
        if (!X.Mode->bPassageCleared) return false;
        X.Test->TestTrue(TEXT("Keeper reached gate physically"),FVector::Dist2D(X.Keeper->GetActorLocation(),X.Mode->GetGateApproach())<90.f);
        X.Test->TestTrue(TEXT("Gate opens while banana is still free"),X.SecondMeal.IsValid() && X.SecondMeal->IsRestingBanana());
        X.Test->TestTrue(TEXT("Upper wild man remains occupied"),X.Guard->IsEating());
        X.Shot(TEXT("03-gate-open.png")); return true;
    });
    Step(TEXT("Keeper passes through opened gate and picks up second banana"),{},[](FFixture& X)
    {
        if (!X.Keeper->IsEating()) return false;
        X.Test->TestTrue(TEXT("Second banana belongs to keeper"),X.SecondMeal.IsValid() && X.SecondMeal->GetConsumer()==X.Keeper);
        X.Test->TestNull(TEXT("Both carried bananas leave slot free"),X.Mode->ActiveBomb.Get());
        X.Test->TestTrue(TEXT("Two meals coexist"),X.Guard->IsEating());
        X.Test->TestTrue(TEXT("Food never moves crate"),FVector::Dist2D(X.CrateStart,X.Mode->Crate->GetActorLocation())<1);
        X.Shot(TEXT("03-two-meals.png")); return true;
    });
    Step(TEXT("Old meal finishing does not clear a newer free banana"),[](FFixture& X)
    {
        X.Mode->Settings->BananaAttractionRange=0;
        X.Player->DrawBomb(); X.Banana=X.Mode->ActiveBomb;
        X.Test->TestTrue(TEXT("Third banana created while two meals coexist"),X.Banana.IsValid() && X.Banana->IsBanana());
        X.Player->DropBomb(); X.Place(X.Player->GetSpawnPosition());
    },[](FFixture& X)
    {
        if (X.FirstMeal.IsValid()) return false;
        X.Test->TestEqual(TEXT("Meal destruction preserves newer active slot"),X.Mode->ActiveBomb.Get(),X.Banana.Get());
        X.Test->TestTrue(TEXT("New free banana persists"),X.Banana.IsValid() && X.Banana->IsRestingBanana());
        X.Mode->RestartTutorial();
        X.Test->TestFalse(TEXT("Retry clears carried food too"),X.SecondMeal.IsValid());
        X.Test->TestNull(TEXT("Retry clears active slot"),X.Mode->ActiveBomb.Get());
        X.Test->TestFalse(TEXT("Retry resets keeper's meal"),X.Keeper->IsEating());
        X.Test->TestTrue(TEXT("Opened gate stays open on retry"),X.Mode->Passage->bOpen);
        X.Mode->Settings->BananaAttractionRange=2400;
        return true;
    },35.f);
    for (FVector P : {FVector(-980,-800,66),FVector(580,80,66)})
    {
        Step(TEXT("Zone boundary converts held bomb without a new fuse or active slot"),[P](FFixture& X)
        {
            X.Place(FVector(-400,840,66)); X.Player->DrawBomb();
            X.Test->TestFalse(TEXT("Outside zone creates regular bomb"),X.Mode->ActiveBomb->IsBanana());
            const float Fuse=X.Mode->ActiveBomb->GetRemainingFuse();
            ABDBomb* Same=X.Mode->ActiveBomb;
            X.Place(P); X.Player->ThrowBomb();
            X.Test->TestTrue(TEXT("Regular cannot be thrown from zone"),Same->IsBanana());
            X.Test->TestEqual(TEXT("Same bomb instance"),Same,X.Mode->ActiveBomb.Get());
            X.Test->TestEqual(TEXT("Same fuse"),Same->GetRemainingFuse(),Fuse);
            X.ClearBomb();
            X.Place(FVector(-400,840,66)); X.Player->DrawBomb();
            X.Test->TestFalse(TEXT("Leaving restores regular bomb selection"),X.Mode->ActiveBomb->IsBanana());
            X.ClearBomb();
        },[](FFixture& X){return X.After(.1f);});
    }
    Step(TEXT("Uneaten banana near crate persists without an explosion"),[](FFixture& X)
    {
        X.Mode->Settings->BananaAttractionRange=0;
        X.Player->DrawBomb(); ABDBomb* Bomb=X.Mode->ActiveBomb; Bomb->MakeBanana(); Bomb->Release(false);
        X.Banana=Bomb;
        Bomb->SetActorLocation(X.Mode->Crate->GetActorLocation()+FVector(80,0,0)); Bomb->Explode();
        X.Place(X.Player->GetSpawnPosition());
    },[](FFixture& X)
    {
        if (!X.After(3.2f)) return false;
        X.Test->TestTrue(TEXT("Unconsumed food survives fuse and explicit explode call"),X.Banana.IsValid());
        X.Test->TestEqual(TEXT("Persistent banana retains active slot"),X.Mode->ActiveBomb.Get(),X.Banana.Get());
        X.Test->TestTrue(TEXT("Crate unchanged beside banana"),FVector::Dist2D(X.CrateStart,X.Mode->Crate->GetActorLocation())<1);
        X.Test->TestTrue(TEXT("No crate impulse"),X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size2D()<1);
        X.ClearBomb(); X.Mode->Settings->BananaAttractionRange=2400;
        X.Place(FVector(270,600,66)); return true;
    });
    Step(TEXT("Slide hits solid wall, stops early and restores control"),{},[](FFixture& X)
    {
        if (!X.After(.2f)) return false;
        X.Aim(FVector(-500,0,0)); X.Player->StartSlide(800,850); X.Saved=X.Player->GetActorLocation(); return true;
    });
    Step(TEXT("Wall slide is finite"),{},[](FFixture& X)
    {
        if (X.Player->IsSliding()) return false;
        X.Test->TestTrue(TEXT("Wall stops before five tiles"),FVector::Dist2D(X.Saved,X.Player->GetActorLocation())<400);
        X.Test->TestTrue(TEXT("Movement restored"),X.Player->GetCharacterMovement()->MovementMode==MOVE_Walking);
        X.Place(FVector(-420,340,66)); X.Aim(FVector(500,0,0)); return true;
    });
    Step(TEXT("Positioning peel only slides and allows walking away"),{},[](FFixture& X)
    {
        if (!X.After(1.5f) || X.Player->IsSliding()) return false;
        X.Player->MoveInRoom(FVector2D(-1,0));
        if (!X.After(1.9f)) return false;
        X.Test->TestTrue(TEXT("Positioning peel slid right"),X.Player->GetActorLocation().X>-400);
        X.Test->TestTrue(TEXT("Movement works after positioning peel"),X.Player->GetVelocity().Size2D()>100);
        X.Player->GetCharacterMovement()->StopMovementImmediately(); return true;
    });
    Step(TEXT("Enemy chase catches player but preserves crate"),[](FFixture& X)
    {
        X.Guard->Attract(X.Guard->GetActorLocation(),0);
        X.Place(X.Guard->GetActorLocation()+FVector(240,0,0));
        X.Saved=X.Mode->Crate->GetActorLocation();
    },[](FFixture& X)
    {
        if (X.Mode->Captures==0) return false;
        X.Test->TestTrue(TEXT("Capture respawned player"),FVector::Dist2D(X.Player->GetActorLocation(),X.Player->GetSpawnPosition())<10);
        X.Test->TestTrue(TEXT("Capture keeps crate"),FVector::Dist(X.Saved,X.Mode->Crate->GetActorLocation())<1);
        X.Test->TestFalse(TEXT("Recovery grace period"),X.Mode->CanCatchPlayer());
        return true;
    });
    Step(TEXT("Deliver original crate through every turn using real bombs"),[](FFixture& X)
    {
        // AI was exercised above. Keep it distracted while deterministic physics
        // shots isolate route solvability; no crate teleports or direct impulses.
        for (TActorIterator<ABDWildMan> It(X.World); It; ++It) It->Attract(X.Mode->Settings->ClearingCenter,250);
        X.TargetIndex=0; X.Shots=0;
    },[](FFixture& X)
    {
        if (X.Mode->bCompleted) return true;
        if (X.Mode->ActiveBomb || X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size2D()>2 ||
            X.World->GetTimeSeconds()-X.LastShot<3.f) return false;
        const TArray<FVector> Route={FVector(270,840,0),FVector(270,-260,0),FVector(600,-260,0),FVector(600,-730,0),FVector(940,-730,0)};
        const FVector Box=X.Mode->Crate->GetActorLocation();
        FVector Delta=Route[X.TargetIndex]-Box; Delta.Z=0;
        if (Delta.Size2D()<65)
        {
            if (X.TargetIndex<4) { ++X.TargetIndex; X.Shot(*FString::Printf(TEXT("route-%d.png"),X.TargetIndex)); }
            else return false;
            Delta=Route[X.TargetIndex]-Box; Delta.Z=0;
        }
        const FVector D=Delta.GetSafeNormal2D();
        const float Travel=FMath::Min(190.f,float(Delta.Size2D())*.85f);
        float Low=0,High=600;
        for (int32 I=0; I<18; ++I)
        {
            const float V=(Low+High)*.5f;
            const float Dist=V/.65f-216.f/(.65f*.65f)*FMath::Loge(1+V*.65f/216.f);
            if (Dist<Travel) Low=V; else High=V;
        }
        const float Radius=360.f*(1-High/(110000.f/120.f));
        float Offset=FMath::Sqrt(FMath::Max(4900.f,Radius*Radius-32*32));
        FVector Origin=Box;
        bool bThrow=false, bValid=false;
        FCollisionQueryParams Params; Params.AddIgnoredActor(X.Player); Params.AddIgnoredActor(X.Mode->Crate);
        while (Offset>=75.f)
        {
            Origin=Box-D*Offset; Origin.Z=66;
            bThrow=X.Mode->ZoneAt(Origin)!=nullptr;
            if (bThrow) Origin-=D*500.f;
            FHitResult ThrowHit;
            const bool bThrowBlocked=bThrow && X.World->SweepSingleByChannel(ThrowHit,Origin,Origin+D*500.f,FQuat::Identity,
                ECC_Visibility,FCollisionShape::MakeSphere(18.f),Params);
            if (!bThrowBlocked && !X.Mode->ZoneAt(Origin) && !X.World->OverlapBlockingTestByChannel(Origin,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(30,64),Params))
            { bValid=true; break; }
            Offset-=10.f;
        }
        if (!bValid) { X.Test->AddError(FString::Printf(TEXT("No legal firing position: %s"),*Origin.ToString())); X.Failed=true; return true; }
        X.Place(Origin); X.Aim(D*500);
        X.Player->DrawBomb();
        X.Test->TestFalse(TEXT("Route shot is regular"),X.Mode->ActiveBomb->IsBanana());
        if (bThrow) X.Player->ThrowBomb(); else X.Player->DropBomb();
        // Retreat to an already accessible safe clearing before the fuse expires.
        X.Place(X.Player->GetSpawnPosition());
        X.LastShot=X.World->GetTimeSeconds(); ++X.Shots;
        UE_LOG(LogTemp,Display,TEXT("JUNGLE SHOT %d route%d box=%s origin=%s throw=%d"),X.Shots,X.TargetIndex,*Box.ToString(),*Origin.ToString(),bThrow);
        return false;
    },240.f);
    Step(TEXT("Goal requires full containment and settled crate"),{},[](FFixture& X)
    {
        X.Test->TestTrue(TEXT("Delivery complete"),X.Mode->bCompleted);
        X.Test->TestTrue(TEXT("Crate fully inside and stopped"),X.Mode->Goal->ContainsSettledCrate(X.Mode->Crate));
        X.Test->TestFalse(TEXT("Completion blocks gameplay"),X.Mode->IsGameplayInputAllowed());
        X.Test->TestTrue(TEXT("Used actual explosions throughout"),X.Shots>5);
        X.Shot(TEXT("04-complete.png")); return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDJungleBananaRaceTest,"BlastDelivery.Jungle.BananaRaceAndPickup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDJungleBananaRaceTest::RunTest(const FString& Parameters)
{
    using namespace BDJungleTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/JungleBananaTrap")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until,float Timeout=20.f)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until),Timeout)); };
    Step(TEXT("Throw gate bait without distracting the upper wild man"),{},[](FFixture& X)
    {
        if (!X.After(2.f)) return false;
        X.PC->SetActorTickEnabled(false);
        X.Place(FVector(640,40,66)); X.Aim(FVector(-370,340,0));
        X.Player->DrawBomb(); X.Banana=X.Mode->ActiveBomb; X.Player->ThrowBomb();
        return true;
    });
    Step(TEXT("Hungry upper wild man wins the race for gate bait"),{},[](FFixture& X)
    {
        if (!X.Guard->IsEating()) return false;
        X.Test->TestTrue(TEXT("Top wild man claims the bait"),X.Banana.IsValid() && X.Banana->GetConsumer()==X.Guard);
        X.Test->TestFalse(TEXT("Top wild man has no gate key"),X.Mode->Passage->bOpen);
        X.Test->TestNull(TEXT("Stolen food still frees slot immediately"),X.Mode->ActiveBomb.Get());
        X.Test->TestFalse(TEXT("Claimed banana cannot be picked up twice"),X.Banana->PickUpBanana(X.Keeper));
        return true;
    });
    Step(TEXT("Keeper abandons already claimed food; capture preserves meal"),{},[](FFixture& X)
    {
        if (!X.After(3.f)) return false;
        X.Test->TestFalse(TEXT("No delayed gate opening after bait is stolen"),X.Mode->bPassageCleared);
        X.Test->TestFalse(TEXT("Keeper cannot eat upper wild man's banana"),X.Keeper->IsEating());
        X.Test->TestTrue(TEXT("Upper wild man has a long meal"),X.Guard->GetEatingRemaining()>15.f);
        X.Mode->PlayerCaught();
        X.Test->TestEqual(TEXT("Capture occurred"),X.Mode->Captures,1);
        X.Test->TestTrue(TEXT("Capture keeps ongoing meal and carried food"),X.Guard->IsEating() && X.Banana.IsValid());
        X.Mode->RestartTutorial();
        X.Test->TestFalse(TEXT("Retry cleans carried banana outside active slot"),X.Banana.IsValid());
        X.Test->TestFalse(TEXT("Retry resets eating state"),X.Guard->IsEating());
        X.Test->TestFalse(TEXT("Retry does not unlock gate"),X.Mode->Passage->bOpen);
        int32 Bombs=0; for (TActorIterator<ABDBomb> It(X.World); It; ++It) ++Bombs;
        X.Test->TestEqual(TEXT("No orphaned food after retry"),Bombs,0);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDJungleRecoveryTest,"BlastDelivery.Jungle.RecoveryAndSpawnLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDJungleRecoveryTest::RunTest(const FString& Parameters)
{
    using namespace BDJungleTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/JungleBananaTrap")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>(); F->Test=this;
    auto Step=[&](const TCHAR* Name,FAction Begin,FCondition Until,float Timeout=20.f)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,Name,MoveTemp(Begin),MoveTemp(Until),Timeout)); };
    Step(TEXT("Courier entrance blocks crate shortcut without blocking player"),{},[](FFixture& X)
    {
        if (!X.After(2.f)) return false;
        X.PC->SetActorTickEnabled(false);
        X.Test->TestFalse(TEXT("Courier fits trail entrance"),X.World->OverlapBlockingTestByChannel(FVector(-990,700,66),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(30,64)));
        X.Test->TestTrue(TEXT("Crate cannot bypass guarded route"),X.World->OverlapBlockingTestByChannel(FVector(-990,700,53),FQuat::Identity,ECC_PhysicsBody,FCollisionShape::MakeBox(FVector(50))));
        X.Mode->bPassageCleared=true; X.Mode->Passage->Open();
        X.Mode->Settings->SpawnIntervalMin=.25f; X.Mode->Settings->SpawnIntervalMax=.25f;
        X.Mode->Settings->SpawnPoints={X.Mode->Settings->ClearingCenter+FVector(0,0,66)};
        X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        X.Mode->Crate->SetActorLocation(FVector(270,730,53),false,nullptr,ETeleportType::TeleportPhysics);
        return true;
    });
    Step(TEXT("Progress at a safe corner creates a retry checkpoint"),{},[](FFixture& X)
    {
        if (!X.After(3.f)) return false;
        X.Test->TestEqual(TEXT("First crate checkpoint saved"),X.Mode->CheckpointIndex,1);
        X.Saved=X.Mode->Crate->GetActorLocation();
        X.Place(FVector(1000,900,66)); X.Mode->PlayerCaught();
        X.Test->TestEqual(TEXT("Capture counted"),X.Mode->Captures,1);
        X.Test->TestTrue(TEXT("Advanced crate progress kept after capture"),FVector::Dist(X.Saved,X.Mode->Crate->GetActorLocation())<1);
        X.Mode->Crate->SetActorLocation(FVector(310,630,53),false,nullptr,ETeleportType::TeleportPhysics);
        X.Mode->RestartTutorial();
        const FVector Restored=X.Mode->Crate->GetActorLocation();
        X.Test->AddInfo(FString::Printf(TEXT("Checkpoint recovery: settled=%s restored=%s"),*X.Saved.ToString(),*Restored.ToString()));
        X.Test->TestTrue(TEXT("R restores saved corner, not level start"),FVector::Dist2D(X.Saved,Restored)<1 && FMath::Abs(X.Saved.Z-Restored.Z)<5);
        X.Test->TestTrue(TEXT("Opened route persists on retry"),X.Mode->bPassageCleared && X.Mode->Passage->bOpen);
        return true;
    });
    Step(TEXT("Random spawner uses navigable predefined point and respects cap"),{},[](FFixture& X)
    {
        if (X.Mode->CountWildMen()<3) return false;
        X.Test->TestEqual(TEXT("Maximum three active wild men"),X.Mode->CountWildMen(),3);
        for (TActorIterator<ABDWildMan> It(X.World); It; ++It)
            X.Test->TestEqual(TEXT("Enemies cannot physically pin crate"),It->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Ignore);
        return true;
    },40.f);
    Step(TEXT("Repeated spawn attempts cannot exceed maximum"),{},[](FFixture& X)
    {
        if (!X.After(2.f)) return false;
        X.Test->TestEqual(TEXT("Cap remains enforced"),X.Mode->CountWildMen(),3);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
