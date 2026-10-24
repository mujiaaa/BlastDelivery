#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDJungle.h"
#include "../BDJungleProps.h"
#include "../BDTutorialGameMode.h"
#include "../BDAlphaSession.h"
#include "../BDBomb.h"
#include "../BDPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace BDRedesignTests
{
struct FFixture
{
    FAutomationTestBase* Test=nullptr;
    UWorld* World=nullptr;
    ABDGameMode* Mode=nullptr;
    ABDCharacter* Player=nullptr;
    ABDPlayerController* PC=nullptr;
    ABDWildMan* Keeper=nullptr;
    ABDBananaTree* Tree=nullptr;
    ABDBananaPeel* Peel=nullptr;
    TWeakObjectPtr<ABDBomb> Banana;
    FVector Saved, Direction;
    double Started=0;
    float Mark=0;
    float PatrolDistance=0;
    FVector PatrolMin,PatrolMax;
    bool Failed=false;
    bool CorridorOnly=false;
    bool After(float S) const { return FPlatformTime::Seconds()-Started>S; }
    ABDJungleGameMode* Jungle() const { return Cast<ABDJungleGameMode>(Mode); }
    ABDTutorialGameMode* Tutorial() const { return Cast<ABDTutorialGameMode>(Mode); }
    void Refresh()
    {
        World=GEditor->PlayWorld;
        if(!World)return;
        Mode=World->GetAuthGameMode<ABDGameMode>();PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
        Player=PC?Cast<ABDCharacter>(PC->GetPawn()):nullptr;Keeper=nullptr;Tree=nullptr;Peel=nullptr;
        for(TActorIterator<ABDWildMan> It(World);It;++It)Keeper=*It;
        for(TActorIterator<ABDBananaTree> It(World);It;++It)if(It->bHasBananas && It->GetActorLocation().X<0)Tree=*It;
        for(TActorIterator<ABDBananaPeel> It(World);It;++It)if(It->GetActorLocation().Y>0)Peel=*It;
    }
    void Place(FVector P)
    {
        Player->MoveInRoom(FVector2D::ZeroVector);Player->GetCharacterMovement()->StopMovementImmediately();
        P.Z=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3;
        Player->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    void Walk(FVector P,float Speed=.45f)
    {const FVector D=(P-Player->GetActorLocation()).GetSafeNormal2D();Player->FacePoint(P);Player->MoveInRoom(FVector2D(D.X,D.Y)*Speed);}
    bool WalkAndStop(FVector P)
    {
        if(FVector::Dist2D(Player->GetActorLocation(),P)>8.f){Walk(P,.3f);return false;}
        Player->MoveInRoom(FVector2D::ZeroVector);return Player->GetVelocity().Size2D()<5.f;
    }
    void QuickToss()
    {
        Player->FacePoint(Player->GetActorLocation()+FVector(1000,0,0));
        FVector2D Aim;PC->ProjectWorldLocationToScreen(Player->GetActorLocation()+FVector(500,0,0),Aim);
        PC->SetMouseLocation(FMath::RoundToInt(Aim.X),FMath::RoundToInt(Aim.Y));
        for(EInputEvent Event:{IE_Pressed,IE_Released})
        {PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,Event,Event==IE_Pressed?1.f:0.f));PC->PlayerTick(1.f/120.f);}
    }
    void Shot(const TCHAR* Name)
    {
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Automation/RedesignShots");IFileManager::Get().MakeDirectory(*Folder,true);
        FViewport* View=World->GetGameViewport()->Viewport;TArray<FColor> Pixels;
        if(View && View->ReadPixels(Pixels))
        {TArray64<uint8> PNG;const FIntPoint S=View->GetSizeXY();FImageUtils::PNGCompressImageArray(S.X,S.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));}
    }
    template<class T> int32 Count()const{int32 N=0;for(TActorIterator<T> It(World);It;++It)++N;return N;}
};
using FAction=TFunction<void(FFixture&)>;
using FCondition=TFunction<bool(FFixture&)>;
class FStep:public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;FString Name;FAction Begin;FCondition Until;double Waiting=0;bool Entered=false;
public:
    FStep(TSharedRef<FFixture> In,FString N,FAction B,FCondition U):F(In),Name(N),Begin(MoveTemp(B)),Until(MoveTemp(U)){}
    virtual bool Update()override
    {
        if(F->Failed)return true;if(Waiting==0)Waiting=FPlatformTime::Seconds();F->Refresh();
        if(F->CorridorOnly && F->Tutorial() && F->Tutorial()->GetScene()!=EBDTutorialScene::ChargeCorridor)
        {F->Test->AddError(TEXT("An archived lesson appeared in the active tutorial flow"));F->Failed=true;return true;}
        if(!Entered && F->Mode && F->Player && F->PC)
        {Entered=true;F->Started=Waiting=FPlatformTime::Seconds();F->Test->AddInfo(Name);UE_LOG(LogTemp,Display,TEXT("REDESIGN TEST: %s"),*Name);if(Begin)Begin(*F);}
        if(Entered && IsValid(F->World) && IsValid(F->Mode) && IsValid(F->Player) && IsValid(F->PC) && Until(*F))return true;
        if(FPlatformTime::Seconds()-Waiting>20)
        {
            F->Test->AddError(TEXT("Timed out: ")+Name);F->Failed=true;
            if(F->Player)F->Test->AddInfo(FString::Printf(TEXT("Player=%s"),*F->Player->GetActorLocation().ToString()));
            if(F->Keeper)F->Test->AddInfo(FString::Printf(TEXT("Keeper=%s distracted=%d eating=%d"),*F->Keeper->GetActorLocation().ToString(),F->Keeper->IsDistracted(),F->Keeper->IsEating()));
            if(F->Banana.IsValid())F->Test->AddInfo(FString::Printf(TEXT("Food=%s resting=%d"),*F->Banana->GetActorLocation().ToString(),F->Banana->IsRestingBanana()));
            F->Shot(TEXT("failure.png"));return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDChargeCorridorTest,"BlastDelivery.Redesign.Corridor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FBDChargeCorridorTest::RunTest(const FString& Parameters)
{
    using namespace BDRedesignTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>();F->Test=this;F->CorridorOnly=true;
    auto Step=[&](const TCHAR* N,FAction B,FCondition U){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,N,MoveTemp(B),MoveTemp(U)));};
    Step(TEXT("Tutorial selection starts corridor"),[](FFixture& X){X.World->GetGameInstance<UBDAlphaSession>()->StartTutorial();},[](FFixture& X)
    {return X.After(1) && X.Tutorial() && X.World->GetGameInstance<UBDAlphaSession>()->GetScreen()==EBDAlphaScreen::Playing;});
    Step(TEXT("Entry framing"),[](FFixture& X){X.World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(1280,800);X.Tutorial()->RestartTutorial();},[](FFixture& X)
    {
        if(!X.After(.8f))return false;X.PC->SetActorTickEnabled(false);
        X.Test->TestEqual(TEXT("Only one active scene"),X.Tutorial()->SceneOrder.Num(),1);X.Test->TestEqual(TEXT("Corridor is the first and only scene"),X.Tutorial()->GetScene(),EBDTutorialScene::ChargeCorridor);
        X.Test->TestEqual(TEXT("Configured defaults also contain only the corridor"),GetDefault<ABDTutorialGameMode>()->SceneOrder.Num(),1);
        FVector2D P;X.PC->ProjectWorldLocationToScreen(X.Mode->Crate->GetActorLocation(),P);
        X.Test->TestTrue(TEXT("Half box at right edge"),FMath::Abs(P.X-1280)<5);X.Test->TestFalse(TEXT("Charge hidden initially"),X.Tutorial()->IsChargeDoodleVisible());
        X.Test->TestFalse(TEXT("Goal hidden initially"),X.Tutorial()->IsGoalDoodleVisible());X.Saved=X.Tutorial()->GetCorridorDoodlePosition(0);X.Shot(TEXT("01-corridor-entry.png"));return true;
    });
    Step(TEXT("Walk to reveal charge instruction"),{},[](FFixture& X)
    {if(!X.Tutorial()->IsChargeDoodleVisible()){X.Walk(FVector(250,0,93));return false;}X.Player->MoveInRoom(FVector2D::ZeroVector);X.Shot(TEXT("02-corridor-charge.png"));return true;});
    Step(TEXT("Backtracking keeps instructions"),{},[](FFixture& X)
    {
        if(X.Player->GetActorLocation().X>8){X.Walk(FVector(0,0,93));return false;}X.Player->MoveInRoom(FVector2D::ZeroVector);
        X.Test->TestTrue(TEXT("Charge stays unlocked"),X.Tutorial()->IsChargeDoodleVisible());X.Test->TestEqual(TEXT("Fixed world anchor"),X.Tutorial()->GetCorridorDoodlePosition(0),X.Saved);
        X.Place(FVector(X.Tutorial()->GetCorridorEnd()-1600,-220,93));return true;
    });
    Step(TEXT("Goal visibility reveals delivery instruction"),{},[](FFixture& X)
    {if(!X.Tutorial()->IsGoalDoodleVisible()){X.Walk(FVector(X.Tutorial()->GetCorridorEnd()-900,-220,93));return false;}X.Player->MoveInRoom(FVector2D::ZeroVector);X.Shot(TEXT("03-corridor-goal.png"));return true;});
    Step(TEXT("Goal instruction persists after backtracking"),[](FFixture& X){X.Place(FVector(0,0,93));},[](FFixture& X)
    {if(!X.After(.3f))return false;X.Test->TestTrue(TEXT("Delivery doodle remains revealed"),X.Tutorial()->IsGoalDoodleVisible());X.Place(FVector(X.Tutorial()->GetCorridorEnd()-900,0,93));return true;});
    Step(TEXT("Partial goal overlap cannot win"),[](FFixture& X)
    {X.Mode->Crate->Mesh->SetSimulatePhysics(false);X.Mode->Crate->SetActorLocation(X.Mode->Goal->GetActorLocation()+FVector(-X.Mode->Goal->Extent.X+20,0,-39),false,nullptr,ETeleportType::TeleportPhysics);},[](FFixture& X)
    {if(!X.After(1.2f))return false;X.Test->TestEqual(TEXT("Partial overlap still playing"),X.Tutorial()->Phase,EBDTutorialPhase::Playing);X.Tutorial()->RestartTutorial();return true;});
    Step(TEXT("Walk from entry to first bomb position"),{},[](FFixture& X)
    {
        if(!X.WalkAndStop(X.Mode->Crate->GetActorLocation()-FVector(270,0,0)))return false;
        X.Test->TestEqual(TEXT("Goal fills corridor width"),X.Mode->Goal->Extent.Y,472.5);
        X.Saved=X.Mode->Crate->GetActorLocation();X.QuickToss();return true;
    });
    Step(TEXT("Retreat from first blast"),[](FFixture& X){X.Direction=X.Player->GetActorLocation()+FVector(0,330,0);},[](FFixture& X){return X.WalkAndStop(X.Direction);});
    Step(TEXT("First bomb advances package but leaves second push"),{},[](FFixture& X)
    {
        if(X.Mode->ActiveBomb || X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().Size()>5.f || !X.After(1))return false;
        X.Test->AddInfo(FString::Printf(TEXT("First bomb advanced %.1f cm"),X.Mode->Crate->GetActorLocation().X-X.Saved.X));
        X.Test->TestTrue(TEXT("First bomb moves package"),X.Mode->Crate->GetActorLocation().X>X.Saved.X+300);
        X.Test->TestEqual(TEXT("Second bomb still needed"),X.Tutorial()->Phase,EBDTutorialPhase::Playing);return true;
    });
    Step(TEXT("Walk around package for second throw"),{},[](FFixture& X){return X.WalkAndStop(X.Mode->Crate->GetActorLocation()+FVector(-270,330,0));});
    Step(TEXT("Line up and throw second bomb"),{},[](FFixture& X)
    {if(!X.WalkAndStop(X.Mode->Crate->GetActorLocation()-FVector(270,0,0)))return false;X.QuickToss();return true;});
    Step(TEXT("Retreat from second blast"),[](FFixture& X){X.Direction=X.Player->GetActorLocation()+FVector(0,330,0);},[](FFixture& X){return X.WalkAndStop(X.Direction);});
    Step(TEXT("Settled delivery shows success"),{},[](FFixture& X){if(X.Tutorial()->Phase!=EBDTutorialPhase::Success || X.Tutorial()->GetPhaseTime()<.18f)return false;X.Shot(TEXT("04-corridor-success.png"));return true;});
    Step(TEXT("Corridor delivery finishes the tutorial"),{},[](FFixture& X)
    {
        if(X.Tutorial()->Phase!=EBDTutorialPhase::Finished)return false;
        X.Test->TestTrue(TEXT("Tutorial completed after corridor"),X.Mode->bCompleted);
        X.Test->TestEqual(TEXT("Completion remains on corridor index zero"),X.Tutorial()->SceneIndex,0);
        X.Test->TestEqual(TEXT("Two-bomb delivery preserved"),X.Mode->BombsUsed,2);
        X.Saved=X.Mode->Crate->GetActorLocation();return true;
    });
    Step(TEXT("No archived scene or pending transition after completion"),{},[](FFixture& X)
    {
        if(!X.After(2.f))return false;
        X.Test->TestEqual(TEXT("Finished phase persists"),X.Tutorial()->Phase,EBDTutorialPhase::Finished);
        X.Test->TestFalse(TEXT("Finished tutorial disables gameplay"),X.Mode->IsGameplayInputAllowed());
        X.Test->TestEqual(TEXT("Only corridor player remains"),X.Count<ABDCharacter>(),1);
        X.Test->TestEqual(TEXT("Delivered package stays in corridor"),X.Mode->Crate->GetActorLocation(),X.Saved);
        X.PC->SetActorTickEnabled(true);return true;
    });
    Step(TEXT("Normal controller flow opens tutorial results"),{},[](FFixture& X)
    {
        auto* Session=X.World->GetGameInstance<UBDAlphaSession>();
        if(Session->GetScreen()!=EBDAlphaScreen::Results)return false;
        X.Test->TestTrue(TEXT("Results belong to tutorial"),Session->IsTutorial());X.Shot(TEXT("14-corridor-complete.png"));return true;
    });
    Step(TEXT("Replay from results returns directly to corridor"),[](FFixture& X){X.World->GetGameInstance<UBDAlphaSession>()->Replay();},[](FFixture& X)
    {
        if(!X.After(1.f) || X.World->GetGameInstance<UBDAlphaSession>()->GetScreen()!=EBDAlphaScreen::Playing)return false;
        X.Test->TestEqual(TEXT("Replay starts corridor index zero"),X.Tutorial()->SceneIndex,0);
        X.Test->TestEqual(TEXT("Replay remains a single scene"),X.Tutorial()->SceneOrder.Num(),1);
        X.Test->TestEqual(TEXT("Replay is playable"),X.Tutorial()->Phase,EBDTutorialPhase::Playing);
        X.Test->TestFalse(TEXT("Replay clears completion"),X.Mode->bCompleted);
        X.Test->TestFalse(TEXT("Replay resets charge reveal"),X.Tutorial()->IsChargeDoodleVisible());
        X.Test->TestFalse(TEXT("Replay resets goal reveal"),X.Tutorial()->IsGoalDoodleVisible());return true;
    });
    Step(TEXT("Restart key while playing returns directly to corridor"),[](FFixture& X)
    {
        X.Player->DrawBomb();
        X.Test->TestNotNull(TEXT("Restart has a live bomb to clear"),X.Mode->ActiveBomb.Get());
        X.Test->TestEqual(TEXT("Restart begins after using a bomb"),X.Mode->BombsUsed,1);
        X.PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::R,IE_Pressed,1.f));X.PC->PlayerTick(1.f/120.f);
        X.PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::R,IE_Released,0.f));
    },[](FFixture& X)
    {
        if(!X.After(1.f) || X.World->GetGameInstance<UBDAlphaSession>()->GetScreen()!=EBDAlphaScreen::Playing)return false;
        X.Test->TestEqual(TEXT("Restart key starts corridor"),X.Tutorial()->GetScene(),EBDTutorialScene::ChargeCorridor);
        X.Test->TestEqual(TEXT("Restart key stays at index zero"),X.Tutorial()->SceneIndex,0);
        X.Test->TestEqual(TEXT("Restart key is playable"),X.Tutorial()->Phase,EBDTutorialPhase::Playing);
        X.Test->TestNull(TEXT("Restart key clears the live bomb"),X.Mode->ActiveBomb.Get());
        X.Test->TestEqual(TEXT("Restart key resets bombs"),X.Mode->BombsUsed,0);return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDCompactJungleTest,"BlastDelivery.Redesign.Jungle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FBDCompactJungleTest::RunTest(const FString& Parameters)
{
    using namespace BDRedesignTests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/JungleBananaTrap")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F=MakeShared<FFixture>();F->Test=this;
    auto Step=[&](const TCHAR* N,FAction B,FCondition U){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F,N,MoveTemp(B),MoveTemp(U)));};
    Step(TEXT("Level selection loads Jungle"),[](FFixture& X){X.World->GetGameInstance<UBDAlphaSession>()->StartLevel(4);},[](FFixture& X)
    {return X.After(1) && X.Jungle() && X.World->GetGameInstance<UBDAlphaSession>()->GetScreen()==EBDAlphaScreen::Playing;});
    Step(TEXT("Sketch layout and distribution"),[](FFixture& X){X.World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(1280,800);},[](FFixture& X)
    {
        if(!X.After(1))return false;X.PC->SetActorTickEnabled(false);X.Test->TestEqual(TEXT("One keeper"),X.Count<ABDWildMan>(),1);X.Test->TestEqual(TEXT("Four trees"),X.Count<ABDBananaTree>(),4);
        int32 Top=0,Bottom=0;for(TActorIterator<ABDBananaTree> It(X.World);It;++It)X.Test->TestEqual(TEXT("Only lower trees have fruit"),It->bHasBananas,It->GetActorLocation().Y>0);
        for(TActorIterator<ABDBananaPeel> It(X.World);It;++It)It->GetActorLocation().Y<0?++Top:++Bottom;
        X.Test->TestTrue(TEXT("More peels above"),Top>Bottom && Bottom>0);X.Test->TestTrue(TEXT("Banana package"),X.Mode->Crate->IsA<ABDBananaPackage>());
        X.Test->TestFalse(TEXT("Door initially locked"),X.Jungle()->Passage->bOpen);X.Shot(TEXT("05-jungle-layout.png"));return true;
    });
    Step(TEXT("Unlured keeper walks around upper room"),[](FFixture& X)
    {X.Saved=X.PatrolMin=X.PatrolMax=X.Keeper->GetActorLocation();X.PatrolDistance=0;},[](FFixture& X)
    {
        const FVector P=X.Keeper->GetActorLocation();X.PatrolDistance+=FVector::Dist2D(P,X.Saved);X.Saved=P;
        X.PatrolMin=X.PatrolMin.ComponentMin(P);X.PatrolMax=X.PatrolMax.ComponentMax(P);
        if(!X.After(12.f))return false;
        X.Test->AddInfo(FString::Printf(TEXT("Patrol distance %.1f cm, span %s"),X.PatrolDistance,*(X.PatrolMax-X.PatrolMin).ToString()));
        X.Test->TestTrue(TEXT("Keeper walks between destinations"),X.PatrolDistance>600.f);
        X.Test->TestTrue(TEXT("Patrol covers both axes"),X.PatrolMax.X-X.PatrolMin.X>100 && X.PatrolMax.Y-X.PatrolMin.Y>80);
        X.Test->TestFalse(TEXT("Patrol needs no food"),X.Keeper->IsDistracted());
        X.Shot(TEXT("13-jungle-patrol.png"));X.Place(X.Tree->GetActorLocation()+FVector(-155,-80,93));return true;
    });
    Step(TEXT("Bomb harvests tree"),{},[](FFixture& X)
    {
        if(!X.After(.3f))return false;X.Player->FacePoint(X.Tree->GetActorLocation());X.Player->DrawBomb();
        if(!X.Test->TestNotNull(TEXT("Bomb drawn"),X.Mode->ActiveBomb.Get())){X.Failed=true;return true;}
        X.Test->TestFalse(TEXT("Regular explosive by tree"),X.Mode->ActiveBomb->IsBanana());X.Player->DropBomb();X.Mode->ActiveBomb->Explode();X.Place(FVector(-720,390,93));return true;
    });
    Step(TEXT("Fruit falls and leaves bomb slot free"),{},[](FFixture& X)
    {
        for(TActorIterator<ABDBomb> It(X.World);It;++It)if(It->IsRestingBanana())X.Banana=*It;if(!X.Banana.IsValid())return false;
        X.Test->TestTrue(TEXT("Tree harvested"),X.Tree->HasDroppedFruit());X.Test->TestNull(TEXT("Free bomb slot"),X.Mode->ActiveBomb.Get());X.Test->TestFalse(TEXT("Distant fruit cannot unlock"),X.Jungle()->Passage->bOpen);
        X.Saved=X.Banana->GetActorLocation();X.Direction=(FVector(0,165,24)-X.Saved).GetSafeNormal2D();X.Place(X.Saved-X.Direction*68);X.Shot(TEXT("06-jungle-fruit.png"));return true;
    });
    Step(TEXT("Player pushes banana to door"),{},[](FFixture& X)
    {
        if(!X.Banana.IsValid())return false;if(X.Banana->GetActorLocation().Y>205){X.Player->FacePoint(X.Player->GetActorLocation()+X.Direction);X.Player->MoveInRoom(FVector2D(X.Direction.X,X.Direction.Y)*.35f);return false;}
        X.Player->MoveInRoom(FVector2D::ZeroVector);X.Test->TestTrue(TEXT("Banana pushed 200 cm"),FVector::Dist2D(X.Saved,X.Banana->GetActorLocation())>200);X.Place(FVector(-620,390,93));return true;
    });
    Step(TEXT("Smell cue"),{},[](FFixture& X){if(!X.Keeper->IsSmelling() || !X.After(.2f))return false;X.Shot(TEXT("07-jungle-smell.png"));return true;});
    Step(TEXT("Keeper unlocks and swings door"),{},[](FFixture& X)
    {auto* D=Cast<ABDJungleDoor>(X.Jungle()->Passage);if(!D || !D->IsOpening() || D->GetOpeningProgress()<.55f)return false;X.Test->TestFalse(TEXT("Collision retained during opening"),D->bOpen);X.Test->TestTrue(TEXT("Keeper at door"),FVector::Dist2D(X.Keeper->GetActorLocation(),X.Jungle()->GetGateApproach())<90);X.Shot(TEXT("08-jungle-door.png"));return true;});
    Step(TEXT("Keeper crosses doorway and eats"),{},[](FFixture& X)
    {
        if(!X.Keeper->IsEating() || X.Keeper->GetEatingRemaining()>X.Jungle()->Settings->EatingSeconds-.3f)return false;
        X.Test->TestTrue(TEXT("Door remains open"),X.Jungle()->Passage->bOpen);X.Test->TestTrue(TEXT("Keeper in lower room"),X.Keeper->GetActorLocation().Y>30);
        X.Test->TestTrue(TEXT("Food belongs to keeper"),X.Banana.IsValid() && X.Banana->GetConsumer()==X.Keeper);
        // Three short straight segments connect the lower package, doorway and goal.
        const FVector Route[]={{-330,180,63},{0,180,63},{0,-200,63},{705,-455,63}};
        FCollisionQueryParams Query(SCENE_QUERY_STAT(JungleCargoRoute),false,X.Mode->Crate);
        Query.AddIgnoredActor(X.Player);Query.AddIgnoredActor(X.Keeper);
        for(int32 I=1;I<4;++I)
        {
            FHitResult Hit;
            X.Test->TestFalse(TEXT("Open route fits the full package"),X.World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_PhysicsBody,FCollisionShape::MakeBox(FVector(60)),Query));
        }
        X.Shot(TEXT("09-jungle-eating.png"));X.Place(X.Peel->GetActorLocation());X.Mark=X.World->GetTimeSeconds();return true;
    });
    Step(TEXT("One-second slip preserves peel"),{},[](FFixture& X)
    {
        auto* P=Cast<ABDJungleCharacter>(X.Player);const float Age=X.World->GetTimeSeconds()-X.Mark;if(Age<.5f)return false;if(P->IsSliding()){if(Age<.6f)X.Shot(TEXT("10-jungle-slip.png"));return false;}
        X.Test->TestTrue(TEXT("Recovery about one second"),Age>.85f && Age<1.35f);X.Test->TestFalse(TEXT("Peel remains"),X.Peel->IsConsumed());X.Test->TestTrue(TEXT("Walking restored"),P->GetCharacterMovement()->IsMovingOnGround());
        X.Place(FVector(-620,390,93));X.Mode->Crate->Mesh->SetSimulatePhysics(true);X.Mode->Crate->SetActorLocation(X.Peel->GetActorLocation()+FVector(-120,0,61),false,nullptr,ETeleportType::TeleportPhysics);X.Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector(220,0,0));return true;
    });
    Step(TEXT("Box consumes peel and leaves skid"),{},[](FFixture& X)
    {if(!X.Peel->IsConsumed())return false;X.Test->TestTrue(TEXT("Box slides forward"),X.Mode->Crate->Mesh->GetPhysicsLinearVelocity().X>400);X.Test->TestEqual(TEXT("Skid mark"),X.Count<ABDJungleSkidMark>(),1);return true;});
    Step(TEXT("Skid remains after the box passes"),{},[](FFixture& X){if(!X.After(.3f))return false;X.Shot(TEXT("11-jungle-skid.png"));return true;});
    Step(TEXT("Restart restores puzzle"),[](FFixture& X){X.Mode->RestartTutorial();},[](FFixture& X)
    {
        if(!X.After(.5f))return false;X.Test->TestFalse(TEXT("Door reset"),X.Jungle()->Passage->bOpen);X.Test->TestFalse(TEXT("Fruit restored"),X.Tree->HasDroppedFruit());X.Test->TestFalse(TEXT("Peel restored"),X.Peel->IsConsumed());X.Test->TestEqual(TEXT("Skids cleared"),X.Count<ABDJungleSkidMark>(),0);
        X.Mode->Crate->Mesh->SetSimulatePhysics(false);X.Mode->Crate->SetActorLocation(X.Mode->Goal->GetActorLocation()+FVector(0,0,-39),false,nullptr,ETeleportType::TeleportPhysics);return true;
    });
    Step(TEXT("Settled delivery completes Jungle"),{},[](FFixture& X){return X.Mode->bCompleted;});
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
