#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDMirrorLab.h"
#include "../BDAlphaSession.h"
#include "../BDAlphaPlayground.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "../BDPuzzleActors.h"
#include "../BDPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDMirrorGeometryTest,"BlastDelivery.Alpha.Mirror.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDMirrorGeometryTest::RunTest(const FString& Parameters)
{
    UWorld* World=FAutomationEditorCommonUtils::CreateNewMap();
    auto* Mirror=World->SpawnActor<ABDRotatingLaserMirror>(); Mirror->Initialize(45);
    float Distance; FVector Point,Normal;
    TestTrue(TEXT("A diagonal mirror intersects its finite optical plane"),Mirror->TryIntersectRay(FVector(-500,0,75),FVector::ForwardVector,1000,Distance,Point,Normal));
    // Unity yaw 45 gives normal (sqrt(.5),0,sqrt(.5)); an incoming +X
    // ray reflects toward Unity -Z, which maps to Unreal +Y via (x,-z,y).
    TestTrue(TEXT("Unity yaw 45 reflects rightward light toward Unity negative Z / Unreal positive Y"),
        (FVector::ForwardVector-2*FVector::DotProduct(FVector::ForwardVector,Normal)*Normal).Equals(FVector(0,1,0),.001));
    TestTrue(TEXT("Optical intersection remains at the room's 75cm beam height"),Point.Equals(FVector(0,0,75),.001));
    Mirror->Initialize(135);
    TestTrue(TEXT("The alternate quarter-turn optical plane also intersects"),Mirror->TryIntersectRay(FVector(-500,0,75),FVector::ForwardVector,1000,Distance,Point,Normal));
    TestTrue(TEXT("Unity yaw 135 reflects rightward light toward Unity positive Z / Unreal negative Y"),
        (FVector::ForwardVector-2*FVector::DotProduct(FVector::ForwardVector,Normal)*Normal).Equals(FVector(0,-1,0),.001));
    Mirror->Initialize(45);
    TestFalse(TEXT("Light outside mirror width misses"),Mirror->TryIntersectRay(FVector(-500,500,75),FVector::ForwardVector,1000,Distance,Point,Normal));
    TestFalse(TEXT("Light above mirror surface misses"),Mirror->TryIntersectRay(FVector(-500,0,300),FVector::ForwardVector,1000,Distance,Point,Normal));
    TestTrue(TEXT("One explosion activates the omnidirectional base"),Mirror->ReceiveExplosion(FVector(-200,0,25),360,1));
    TestFalse(TEXT("The same explosion cannot rotate it a second time"),Mirror->ReceiveExplosion(FVector(-200,0,25),360,1));
    TestTrue(TEXT("A separate explosion queues while the first turn animates"),Mirror->ReceiveExplosion(FVector(0,200,25),360,2));
    TestEqual(TEXT("A second independent blast is queued"),Mirror->GetPendingTurns(),1);
    Mirror->Tick(.46f); Mirror->Tick(.46f);
    TestEqual(TEXT("Two blasts settle at exactly two quarter turns"),Mirror->GetCurrentYaw(),225.f);
    TestEqual(TEXT("Two blast events cause exactly two activations"),Mirror->GetActivationCount(),2);
    TestFalse(TEXT("Out-of-range explosion does nothing"),Mirror->ReceiveExplosion(FVector(1000,0,25),360,3));
    TestEqual(TEXT("Fast perpendicular bomb trajectory hits the beam between frames"),
        ABDMirrorLaser::SegmentDistanceSquared(FVector(0,-200,75),FVector(0,200,75),FVector(-200,0,75),FVector(200,0,75)),0.f);
    Mirror->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDRotatingEmitterTest,"BlastDelivery.Alpha.Mirror.RotatingEmitter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDRotatingEmitterTest::RunTest(const FString& Parameters)
{
    UWorld* World=FAutomationEditorCommonUtils::CreateNewMap();
    auto* Emitter=World->SpawnActor<ABDRotatingLaserEmitter>(FVector(0,0,75),FRotator::ZeroRotator);
    Emitter->Initialize(FVector(0,1,0));
    TestTrue(TEXT("Central emitter initially fires screen-down"),Emitter->GetBeamDirection().Equals(FVector(0,1,0),.001));
    TestTrue(TEXT("An explosion can activate the base from the west"),Emitter->ReceiveExplosion(FVector(-200,0,25),360,1));
    TestFalse(TEXT("Duplicate notification cannot produce a second turn"),Emitter->ReceiveExplosion(FVector(-200,0,25),360,1));
    TestEqual(TEXT("Beam is disabled immediately during rotation"),Emitter->GetBeamPoints().Num(),0);
    Emitter->Tick(.2f);
    TestEqual(TEXT("No sweeping damage beam is emitted between cardinal directions"),Emitter->GetBeamPoints().Num(),0);
    Emitter->Tick(.26f);
    TestTrue(TEXT("Clockwise quarter-turn changes down to screen-left"),Emitter->GetBeamDirection().Equals(FVector(-1,0,0),.001));
    TestFalse(TEXT("Rotation finishes before emission resumes"),Emitter->IsTurning());
    TestTrue(TEXT("Settled emitter resumes optical tracing"),Emitter->GetBeamPoints().Num()>=2);
    TestTrue(TEXT("Base can be activated from the north"),Emitter->ReceiveExplosion(FVector(0,-200,25),360,2));
    TestTrue(TEXT("A distinct explosion while turning queues one additional turn"),Emitter->ReceiveExplosion(FVector(200,0,25),360,3));
    Emitter->Tick(.46f);
    TestEqual(TEXT("Queued turn keeps beam disabled"),Emitter->GetBeamPoints().Num(),0);
    Emitter->Tick(.46f);
    TestTrue(TEXT("Three independent explosions produce the right-facing orientation"),Emitter->GetBeamDirection().Equals(FVector(1,0,0),.001));
    TestTrue(TEXT("Base can be activated from the south"),Emitter->ReceiveExplosion(FVector(0,200,25),360,4));
    Emitter->Tick(.46f);
    TestTrue(TEXT("Four clockwise turns return to screen-down without drift"),Emitter->GetBeamDirection().Equals(FVector(0,1,0),.001));
    TestEqual(TEXT("One activation per unique explosion"),Emitter->GetActivationCount(),4);
    TestFalse(TEXT("Distant blast cannot rotate the central device"),Emitter->ReceiveExplosion(FVector(1000,0,25),360,5));
    // The shared optical implementation must still respect solid obstacles for blast activation.
    auto* Shield=World->SpawnActor<ABDBulletproofGlass>(FVector(-220,0,135),FRotator(0,90,0)); Shield->Configure(400);
    TestFalse(TEXT("Existing glass shields the circular switch from an otherwise in-range blast"),Emitter->ReceiveExplosion(FVector(-400,0,25),360,6));
    Shield->Destroy(); Emitter->Destroy();
    return true;
}

namespace BDMirrorTests
{
class FRun : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Stage=0;
    double Deadline=FPlatformTime::Seconds()+45;
    float WorldMark=0;
    float RenderWarmupStarted=-1;
    TWeakObjectPtr<ABDMirrorLaser> ProbeLaser;
    TWeakObjectPtr<ABDCrate> ProbeCrate;
    TWeakObjectPtr<ABDRotatingLaserMirror> ProbeMirror;
    TWeakObjectPtr<ABDBulletproofGlass> ProbeGlass;
    TWeakObjectPtr<ABDBarrier> ProbeWall;
    TWeakObjectPtr<ABDBomb> ProbeBomb;
    int32 PriorBombHits=0;
    static void Shot(UWorld* World,const TCHAR* Name)
    {
        if (!World->GetGameViewport() || !World->GetGameViewport()->Viewport) return;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Automation/AlphaShots"); IFileManager::Get().MakeDirectory(*Folder,true);
        FViewport* Viewport=World->GetGameViewport()->Viewport; TArray<FColor> Pixels;
        if (!Viewport->ReadPixels(Pixels)) return;
        const FIntPoint Size=Viewport->GetSizeXY(); TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG); FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));
    }
public:
    explicit FRun(FAutomationTestBase* In):Test(In) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds()>Deadline) { Test->AddError(FString::Printf(TEXT("Mirror integration timeout at stage %d"),Stage)); return true; }
        UWorld* World=GEditor->PlayWorld; if (!World) return false;
        auto* Session=World->GetGameInstance<UBDAlphaSession>(); if (!Session) return false;
        if (Stage==0) { Session->StartLevel(2); ++Stage; return false; }
        auto* Mode=World->GetAuthGameMode<ABDAlphaMirrorGameMode>(); if (!Mode || !Mode->IsRoomReady()) return false;
        auto* Player=Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(World,0)); if (!Player) return false;
        if (Stage==1)
        {
            if (RenderWarmupStarted<0) { RenderWarmupStarted=World->GetTimeSeconds(); return false; }
            if (World->GetTimeSeconds()-RenderWarmupStarted<1.f) return false;
            Test->TestEqual(TEXT("Square room has the requested four corner mirrors"),Mode->GetMirrors().Num(),4);
            Test->TestEqual(TEXT("All four edge sources and the central source are present"),Mode->GetLasers().Num(),5);
            Test->TestEqual(TEXT("Revised room has no internal glass walls"),Mode->GetGlassWalls().Num(),0);
            if (Mode->GetMirrors().Num()!=4 || Mode->GetLasers().Num()!=5 || !Test->TestNotNull(TEXT("Central emitter is registered"),Mode->GetCentralEmitter())) return true;
            Test->TestTrue(TEXT("Courier begins safely in the southwest corner with room to draw a larger bomb"),FVector::Dist2D(Player->GetActorLocation(),FVector(-1080,1010,95))<2.f);
            Test->TestTrue(TEXT("Enlarged delivery goal stays inside the northeast boundary"),Mode->Goal->GetActorLocation().Equals(FVector(900,-900,180),.01f));
            Test->TestTrue(TEXT("Goal volume has 1.5 times its previous dimensions"),Mode->Goal->Extent.Equals(FVector(285,285,180),.01));
            Test->TestEqual(TEXT("Mirror player size profile is 150 percent"),Mode->GetPhysicsSettings().PlayerSize,1.5f);
            Test->TestEqual(TEXT("Mirror bomb size profile is 150 percent"),Mode->GetPhysicsSettings().BombSize,1.5f);
            Test->TestEqual(TEXT("Mirror crate size profile is 150 percent"),Mode->GetPhysicsSettings().CrateSize,1.5f);
            for (FName Name:{FName(TEXT("PlayerSize")),FName(TEXT("BombSize")),FName(TEXT("CrateSize"))})
                Test->TestEqual(TEXT("Tuning console displays the authored 150 percent size"),Session->GetParameter(Name),1.5f);
            Test->TestTrue(TEXT("Courier collider is enlarged with its appearance"),FMath::IsNearlyEqual(Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),60.f,.01f));
            Test->TestTrue(TEXT("Cargo collider is a 180cm cube"),Mode->Crate->Mesh->Bounds.BoxExtent.Equals(FVector(90),.1));
            const FVector MirrorPositions[]={FVector(-600,-600,0),FVector(600,-600,0),FVector(-600,600,0),FVector(600,600,0)};
            const FVector EdgeDirections[]={FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0),FVector(1,0,0)};
            for (int32 I=0;I<4;++I)
            {
                Test->TestTrue(TEXT("Mirror occupies its inset corner"),Mode->GetMirrors()[I]->GetActorLocation().Equals(MirrorPositions[I],.01));
                Test->TestEqual(TEXT("NE begins an additional quarter turn clockwise"),Mode->GetMirrors()[I]->GetCurrentYaw(),I==0 ? 45.f : I==1 ? 225.f : 135.f);
                Test->TestTrue(TEXT("Edge laser retains its requested inward direction"),Mode->GetLasers()[I]->GetBeamDirection().Equals(EdgeDirections[I],.001));
            }
            for (ABDMirrorLaser* Laser:Mode->GetLasers())
            {
                Laser->RefreshPath();
                Test->TestFalse(TEXT("Player spawn stays more than 120cm from every initial laser"),Laser->IntersectsBeam(Player->GetActorLocation(),Player->GetActorLocation(),120));
                Test->TestFalse(TEXT("Southwest bomb placement is initially clear of laser fire"),Laser->IntersectsBeam(FVector(-860,860,57),FVector(-860,860,57),57));
                Test->TestFalse(TEXT("Diagonal central-switch approach has room for a held bomb"),Laser->IntersectsBeam(FVector(-300,300,57),FVector(-300,300,57),57));
            }
            const auto& NorthPath=Mode->GetLasers()[0]->GetBeamPoints();
            Test->TestTrue(TEXT("North source now reflects through NW, NE, SE and SW"),NorthPath.Num()>=6 && NorthPath[1].Equals(FVector(-600,-600,75),.1) && NorthPath[2].Equals(FVector(600,-600,75),.1) && NorthPath[3].Equals(FVector(600,600,75),.1) && NorthPath[4].Equals(FVector(-600,600,75),.1));
            const auto& EastPath=Mode->GetLasers()[1]->GetBeamPoints();
            Test->TestTrue(TEXT("East source is redirected upward by the changed NE mirror"),EastPath.Num()>=3 && EastPath[1].Equals(FVector(600,-600,75),.1) && EastPath.Last().Y<-1000);
            const FVector AlternateSources[]={FVector(-1050,-600,75),FVector(600,-1050,75),FVector(1050,600,75),FVector(-600,1050,75)};
            const FVector AlternateDirections[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
            auto ClickEmitter=[&](ABDMirrorLaser* Emitter)
            { return Mode->TrySwapEdgeEmitterAtRay(Emitter->GetActorLocation()+FVector(0,0,1000),FVector(0,0,-1)); };
            FCollisionQueryParams EmptyOldLocation(SCENE_QUERY_STAT(EmitterSwapNoResidue),false,Player);
            for (int32 I=0;I<4;++I)
            {
                ABDMirrorLaser* Laser=Mode->GetLasers()[I]; const FVector Original=Laser->GetActorLocation();
                UStaticMeshComponent* OriginalHousing=Laser->GetEmitterHousing();
                TArray<AActor*> Attachments; Laser->GetAttachedActors(Attachments);
                Test->TestEqual(TEXT("Edge source has exactly one attached wall mount"),Attachments.Num(),1);
                AActor* OriginalMount=Attachments.Num()==1 ? Attachments[0] : nullptr;
                Test->TestTrue(TEXT("Click swaps each edge source onto its adjacent wall"),ClickEmitter(Laser));
                Test->TestTrue(TEXT("Source arrives at its diagonal-reflection position"),Laser->GetActorLocation().Equals(AlternateSources[I],.01));
                Test->TestTrue(TEXT("Relocated source points into the room"),Laser->GetBeamDirection().Equals(AlternateDirections[I],.001));
                Test->TestTrue(TEXT("Relocation refreshes the optical path immediately"),Laser->GetBeamPoints().Num()>1 && Laser->GetBeamPoints()[0].Equals(AlternateSources[I],.01));
                FHitResult Leftover;
                Test->TestFalse(TEXT("Old casing no longer blocks the room"),World->LineTraceSingleByChannel(Leftover,Original+FVector(0,0,300),Original-FVector(0,0,55),ECC_Visibility,EmptyOldLocation));
                const FVector OldMount=Original-EdgeDirections[I]*85;
                Test->TestFalse(TEXT("Old wall mount leaves no residual collision"),World->LineTraceSingleByChannel(Leftover,OldMount+FVector(0,0,300),OldMount-FVector(0,0,55),ECC_Visibility,EmptyOldLocation));
                Test->TestTrue(TEXT("A second click returns the source to its original wall"),ClickEmitter(Laser));
                Test->TestTrue(TEXT("Round trip restores location and inward direction"),Laser->GetActorLocation().Equals(Original,.01) && Laser->GetBeamDirection().Equals(EdgeDirections[I],.001));
                Test->TestTrue(TEXT("Round trip reuses the same housing component"),Laser->GetEmitterHousing()==OriginalHousing);
                Attachments.Reset(); Laser->GetAttachedActors(Attachments);
                Test->TestTrue(TEXT("Round trip reuses one mount instead of spawning extra collision"),Attachments.Num()==1 && Attachments[0]==OriginalMount);
            }
            ABDMirrorLaser* West=Mode->GetLasers()[3]; const FVector WestStart=West->GetActorLocation();
            const FVector CrateStart=Mode->Crate->GetActorLocation();
            Mode->Crate->Mesh->SetSimulatePhysics(false);
            Mode->Crate->SetActorLocation(FVector(-600,1050,92),false,nullptr,ETeleportType::TeleportPhysics); Mode->Crate->Mesh->UpdateBounds();
            Test->TestTrue(TEXT("Occupied destination still consumes the source click"),ClickEmitter(West));
            Test->TestTrue(TEXT("Cargo blocks relocation instead of being embedded"),West->GetActorLocation().Equals(WestStart,.01));
            Mode->Crate->SetActorLocation(CrateStart,false,nullptr,ETeleportType::TeleportPhysics); Mode->Crate->Mesh->UpdateBounds(); Mode->Crate->Mesh->SetSimulatePhysics(true);
            const FVector PlayerStart=Player->GetActorLocation();
            Player->SetActorLocation(FVector(-600,1050,PlayerStart.Z),false,nullptr,ETeleportType::TeleportPhysics);
            Test->TestTrue(TEXT("Player-occupied destination consumes the click"),ClickEmitter(West));
            Test->TestTrue(TEXT("Courier blocks relocation instead of being embedded"),West->GetActorLocation().Equals(WestStart,.01));
            Player->SetActorLocation(PlayerStart,false,nullptr,ETeleportType::TeleportPhysics);
            Player->FacePoint(Player->GetActorLocation()+FVector(100,0,0));
            auto* Blocker=Mode->DrawBomb(Player);
            if (!Test->TestNotNull(TEXT("Occupancy fixture uses a real ignited bomb"),Blocker)) return true;
            Blocker->Release(false,false);
            Blocker->SetActorLocation(FVector(-600,1050,57),false,nullptr,ETeleportType::TeleportPhysics);
            Test->TestTrue(TEXT("Released occupancy bomb has an active physics collider"),Blocker->Collision->IsQueryCollisionEnabled() && Blocker->Collision->IsSimulatingPhysics());
            Test->TestTrue(TEXT("Bomb-occupied destination consumes the click"),ClickEmitter(West));
            Test->TestTrue(TEXT("Physical bomb blocks relocation instead of being embedded"),West->GetActorLocation().Equals(WestStart,.01)); Blocker->Destroy();
            // Keep later controller checks independent even when an occupancy assertion fails.
            if (!West->GetActorLocation().Equals(WestStart,.01)) ClickEmitter(West);
            Test->TestTrue(TEXT("Occupancy fixture leaves the west emitter in its initial position"),West->GetActorLocation().Equals(WestStart,.01));
            Test->TestFalse(TEXT("Central emitter is not part of the wall-swap controls"),Mode->TrySwapEdgeEmitterAtRay(FVector(0,0,1000),FVector(0,0,-1)));
            Test->TestFalse(TEXT("Horizontal cursor ray cannot select a base"),Mode->TryRotateBaseAtRay(FVector(0,0,1000),FVector(1,0,0)));
            Test->TestFalse(TEXT("Ray facing away from the floor cannot select a base"),Mode->TryRotateBaseAtRay(FVector(0,0,1000),FVector(0,0,1)));
            Test->TestFalse(TEXT("Empty floor is not a rotation button"),Mode->TryRotateBaseAtRay(FVector(300,300,1000),FVector(0,0,-1)));
            Mode->bEnableClickRotation=false;
            Test->TestFalse(TEXT("Temporary click controls can be disabled"),Mode->TryRotateBaseAtRay(FVector(0,0,1000),FVector(0,0,-1)));
            Test->TestFalse(TEXT("Temporary wall-swap controls share the disable flag"),ClickEmitter(West));
            Mode->bEnableClickRotation=true;
            Session->TogglePause();
            Test->TestFalse(TEXT("Paused room does not accept base clicks"),Mode->TryRotateBaseAtRay(FVector(0,0,1000),FVector(0,0,-1)));
            Test->TestFalse(TEXT("Paused room does not accept emitter relocation"),ClickEmitter(West));
            Session->Resume();
            Shot(World,TEXT("mirror-room.png"));
            ABDRotatingLaserMirror* SW=Mode->GetMirrors()[2];
            auto* Bomb=Mode->DrawBomb(Player); if (!Test->TestNotNull(TEXT("A real bomb can be drawn"),Bomb)) return true;
            Test->TestTrue(TEXT("New bomb has the enlarged 57cm physical radius"),FMath::IsNearlyEqual(Bomb->GetWorldRadius(),57.f,.01f));
            // Assisted placement tests the actual bomb event, not a claim that the puzzle was naturally solved.
            Bomb->Release(false,false); Bomb->SetActorLocation(FVector(-300,300,57),false,nullptr,ETeleportType::TeleportPhysics); Bomb->Explode();
            Test->TestEqual(TEXT("Real bomb AOE rotates the center emitter"),Mode->GetCentralEmitter()->GetAcceptedBlastCount(),1);
            Test->TestEqual(TEXT("Same AOE also rotates the southwest mirror"),SW->GetAcceptedBlastCount(),1);
            Test->TestEqual(TEXT("Distant northwest mirror is unchanged"),Mode->GetMirrors()[0]->GetAcceptedBlastCount(),0);
            Test->TestEqual(TEXT("Central laser switches off immediately while turning"),Mode->GetCentralEmitter()->GetBeamPoints().Num(),0);
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ProbeGlass=World->SpawnActor<ABDBulletproofGlass>(FVector(5300,0,135),FRotator(0,90,0),P); ProbeGlass->Configure();
            Test->TestEqual(TEXT("Retained glass actor still blocks blast queries"),ProbeGlass->Collision->GetCollisionResponseToChannel(ECC_Visibility),ECR_Block);
            Test->TestEqual(TEXT("Retained glass actor still blocks physics bodies"),ProbeGlass->Collision->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
            ProbeWall=World->SpawnActor<ABDBarrier>(FVector(5600,0,0),FRotator::ZeroRotator,P); ProbeWall->Configure(FVector(50,1000,270));
            ProbeLaser=World->SpawnActor<ABDMirrorLaser>(FVector(5000,0,75),FRotator::ZeroRotator,P); ProbeLaser->Configure(FVector::ForwardVector);
            ProbeLaser->RefreshPath();
            Test->TestTrue(TEXT("Laser passes through glass but stops at the hard wall"),ProbeLaser->GetBeamPoints().Last().X>5400 && ProbeLaser->GetBeamPoints().Last().X<5600);
            ProbeCrate=World->SpawnActor<ABDCrate>(FVector(5400,0,62),FRotator::ZeroRotator,P); ProbeLaser->RefreshPath();
            Test->TestTrue(TEXT("Cargo blocks the same laser before the hard wall"),ProbeLaser->GetBeamPoints().Last().X<5400);
            ProbeCrate->Destroy(); ProbeCrate.Reset();
            ProbeMirror=World->SpawnActor<ABDRotatingLaserMirror>(FVector(5450,0,0),FRotator::ZeroRotator,P); ProbeMirror->Initialize(45);
            ProbeLaser->RefreshPath();
            Test->TestTrue(TEXT("One mirror produces a reflected segment after crossing glass"),ProbeLaser->GetBeamPoints().Num()>=3);
            Test->TestTrue(TEXT("The reflected segment follows Unity yaw 45 toward positive Unreal Y"),ProbeLaser->GetBeamPoints().Last().Y>500);
            ProbeMirror->Destroy(); ProbeMirror.Reset();
            ProbeLaser->RefreshPath();
            ProbeBomb=World->SpawnActor<ABDBomb>(FVector(5200,-200,75),FRotator::ZeroRotator,P);
            ProbeLaser->Tick(0); PriorBombHits=ProbeLaser->GetBombsDestroyed();
            ProbeBomb->SetActorLocation(FVector(5200,200,75),false,nullptr,ETeleportType::TeleportPhysics); ProbeLaser->Tick(0);
            Test->TestEqual(TEXT("Fast bomb crossing is destroyed without exploding"),ProbeLaser->GetBombsDestroyed(),PriorBombHits+1);
            ProbeLaser->Destroy(); ProbeWall->Destroy(); ProbeGlass->Destroy();
            WorldMark=World->GetTimeSeconds(); ++Stage; return false;
        }
        if (Stage==2)
        {
            if (World->GetTimeSeconds()-WorldMark<.8f) return false;
            Test->TestFalse(TEXT("Mirror completes its short rotation animation"),Mode->GetMirrors()[2]->IsTurning());
            Test->TestEqual(TEXT("One blast turns SW mirror exactly ninety degrees"),Mode->GetMirrors()[2]->GetCurrentYaw(),225.f);
            Test->TestTrue(TEXT("Central beam resumes pointing screen-left"),Mode->GetCentralEmitter()->GetBeamDirection().Equals(FVector(-1,0,0),.001));
            Shot(World,TEXT("mirror-center-rotated.png"));
            // Exercise the real controller binding with an existing held bomb.
            // Only NW still needs a turn to direct all four edge beams outward.
            auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World,0));
            Player->FacePoint(Player->GetActorLocation()+FVector(100,0,0));
            auto* Held=Mode->DrawBomb(Player);
            if (!Test->TestNotNull(TEXT("Courier can hold a bomb while testing base clicks"),Held)) return true;
            const int32 BeforeClickBombs=Mode->BombsUsed;
            FVector2D Cursor=FVector2D::ZeroVector;
            ABDMirrorLaser* West=Mode->GetLasers()[3]; const FVector WestStart=West->GetActorLocation();
            if (!Test->TestTrue(TEXT("Edge housing projects to a visible mouse target"),PC && PC->ProjectWorldLocationToScreen(West->GetActorLocation(),Cursor))) return true;
            PC->SetMouseLocation(FMath::RoundToInt(Cursor.X),FMath::RoundToInt(Cursor.Y));
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f)); PC->PlayerTick(1.f/120.f);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Repeat,1.f)); PC->PlayerTick(1.f/120.f);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f)); PC->PlayerTick(1.f/120.f);
            Test->TestTrue(TEXT("Real controller click swaps the west source once, not once per held frame"),West->GetActorLocation().Equals(FVector(-600,1050,75),.01));
            Test->TestEqual(TEXT("Emitter relocation does not spend a bomb"),Mode->BombsUsed,BeforeClickBombs);
            Test->TestTrue(TEXT("Emitter click press and release preserve the held bomb"),IsValid(Held) && Held->GetHolder()==Player);
            Test->TestTrue(TEXT("Source can be restored for the remaining mirror checks"),Mode->TrySwapEdgeEmitterAtRay(West->GetActorLocation()+FVector(0,0,1000),FVector(0,0,-1)));
            Test->TestTrue(TEXT("Controller test leaves the original layout restored"),West->GetActorLocation().Equals(WestStart,.01));
            if (!Test->TestTrue(TEXT("NW base projects to a visible mouse position"),PC && PC->ProjectWorldLocationToScreen(FVector(-600,-500,32.5),Cursor))) return true;
            PC->SetMouseLocation(FMath::RoundToInt(Cursor.X),FMath::RoundToInt(Cursor.Y));
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f)); PC->PlayerTick(1.f/120.f);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Repeat,1.f)); PC->PlayerTick(1.f/120.f);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f)); PC->PlayerTick(1.f/120.f);
            Test->TestEqual(TEXT("Actual left click rotates exactly one base, not once per held frame"),Mode->GetMirrors()[0]->GetActivationCount(),1);
            Test->TestEqual(TEXT("Base click does not spend another bomb"),Mode->BombsUsed,BeforeClickBombs);
            Test->TestTrue(TEXT("Click press and release preserve the held bomb"),IsValid(Held) && Held->GetHolder()==Player);
            Held->Destroy();
            Player->SetActorLocation(FVector(-300,0,95),false,nullptr,ETeleportType::TeleportPhysics);
            Mode->GetCentralEmitter()->Tick(0);
            Test->TestTrue(TEXT("Laser contact respawns the courier independently"),Player->IsRecovering());
            Test->TestFalse(TEXT("Player hazard does not end the level"),Mode->bCompleted);
            WorldMark=World->GetTimeSeconds(); ++Stage; return false;
        }
        if (Stage==3)
        {
            if (Player->IsRecovering() || World->GetTimeSeconds()-WorldMark<1.f) return false;
            Test->TestTrue(TEXT("Courier returns to the original safe spawn"),FVector::Dist2D(Player->GetActorLocation(),Player->GetSpawnPosition())<2);
            Test->TestEqual(TEXT("Independent respawn preserves the central switch orientation"),Mode->GetCentralEmitter()->GetCurrentYaw(),180.f);
            Test->TestTrue(TEXT("Respawn preserves the larger courier collider"),FMath::IsNearlyEqual(Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),60.f,.01f));
            Test->TestEqual(TEXT("NW click has opened the inner edges"),Mode->GetMirrors()[0]->GetCurrentYaw(),135.f);
            // Assisted blocker placement verifies a real, full-size walking corridor.
            // This is not a claim of a complete start-to-finish player solution.
            Mode->Crate->Mesh->SetSimulatePhysics(false);
            Mode->Crate->SetActorLocationAndRotation(FVector(-600,850,92),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            Mode->Crate->Mesh->UpdateBounds();
            for (ABDMirrorLaser* Laser:Mode->GetLasers()) Laser->RefreshPath();
            const float Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
            const FVector Walk[]={FVector(-1080,1010,Half+3),FVector(-300,1010,Half+3),FVector(-300,300,Half+3),FVector(300,300,Half+3),FVector(300,-300,Half+3),FVector(900,-300,Half+3)};
            FCollisionQueryParams WalkQuery(SCENE_QUERY_STAT(MirrorRouteClearance),false,Player);
            for (int32 I=1;I<UE_ARRAY_COUNT(Walk);++I)
            {
                FHitResult Obstacle;
                Test->TestFalse(TEXT("Cargo shielding route fits the enlarged player capsule"),World->SweepSingleByChannel(Obstacle,Walk[I-1],Walk[I],FQuat::Identity,ECC_Visibility,FCollisionShape::MakeCapsule(Radius,Half),WalkQuery));
                // The laser is at 75cm; the enlarged capsule's lower sphere intersects it.
                for (ABDMirrorLaser* Laser:Mode->GetLasers())
                    Test->TestFalse(TEXT("Shielded route to the final delivery side is clear of active beams"),Laser->IntersectsBeam(FVector(Walk[I-1].X,Walk[I-1].Y,75),FVector(Walk[I].X,Walk[I].Y,75),Radius));
            }
            Shot(World,TEXT("mirror-shielded-route.png"));
            // Stage only the final shot, then let a real bomb propel the cargo
            // through the laser and into the enlarged goal under normal physics.
            Mode->Crate->Mesh->SetSimulatePhysics(true);
            Mode->Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector); Mode->Crate->Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            Mode->Crate->SetActorLocationAndRotation(FVector(900,-500,92),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            Mode->Crate->Mesh->UpdateBounds();
            Player->FacePoint(Player->GetActorLocation()+FVector(100,0,0));
            auto* DeliveryBomb=Mode->DrawBomb(Player); if (!Test->TestNotNull(TEXT("Final delivery uses an actual bomb"),DeliveryBomb)) return true;
            DeliveryBomb->Release(false,false); DeliveryBomb->SetActorLocation(FVector(900,-330,57),false,nullptr,ETeleportType::TeleportPhysics); DeliveryBomb->Explode();
            WorldMark=World->GetTimeSeconds(); ++Stage; return false;
        }
        if (Stage==4)
        {
            if (!Mode->bCompleted)
            {
                if (World->GetTimeSeconds()-WorldMark<10.f) return false;
                Test->AddError(FString::Printf(TEXT("Assisted final bomb shot did not settle in goal; cargo at %s"),*Mode->Crate->GetActorLocation().ToString()));
                return true;
            }
            Test->TestTrue(TEXT("Actual final bomb shot carries the enlarged crate through laser fire into the goal"),Mode->bCompleted);
            Shot(World,TEXT("mirror-results.png"));
            return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDMirrorIntegrationTest,"BlastDelivery.Alpha.Mirror.Integration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDMirrorIntegrationTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDMirrorTests::FRun>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
