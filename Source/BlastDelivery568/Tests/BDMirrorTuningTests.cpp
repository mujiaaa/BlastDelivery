#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAlphaSession.h"
#include "../BDAlphaMenu.h"
#include "../BDAlphaPlayground.h"
#include "../BDMirrorLab.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDBomb.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

namespace BDMirrorTuningTests
{
class FRun final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Stage = 0;
    double Started = 0.;
    TWeakObjectPtr<ABDGameMode> TravelFrom;

    void Sizes(UBDAlphaSession* Session, ABDGameMode* Mode, ABDCharacter* Player, FVector Expected)
    {
        const auto& Physics = Mode->GetPhysicsSettings();
        const FVector Displayed(Session->GetParameter(TEXT("PlayerSize")),
            Session->GetParameter(TEXT("BombSize")), Session->GetParameter(TEXT("CrateSize")));
        Test->TestTrue(TEXT("Tuning displays this level's actual sizes"), Displayed.Equals(Expected, .001));
        Test->TestTrue(TEXT("Live physics uses the same three sizes"),
            FVector(Physics.PlayerSize, Physics.BombSize, Physics.CrateSize).Equals(Expected, .001));
        Test->TestTrue(TEXT("Player collision is resized with the displayed value"),
            FMath::IsNearlyEqual(double(Player->GetCapsuleComponent()->GetScaledCapsuleRadius()), 40. * Expected.X, .05));
        if (Mode->Crate)
            Test->TestTrue(TEXT("Crate geometry is resized with the displayed value"),
                Mode->Crate->GetActorScale3D().Equals(FVector(1.2 * Expected.Z), .001));
    }
    void Mass(UBDAlphaSession* Session, ABDGameMode* Mode, float Expected)
    {
        Test->TestEqual(TEXT("Shared mass survives level travel"), Session->GetParameter(TEXT("CrateMass")), Expected);
        Test->TestEqual(TEXT("Live profile retains shared mass"), Mode->GetPhysicsSettings().CrateMass, Expected);
        if (Mode->Crate)
            Test->TestTrue(TEXT("Physical crate uses the shared mass"),
                FMath::IsNearlyEqual(Mode->Crate->Mesh->GetMass(), Expected, .05f));
    }
    void Travel(UBDAlphaSession* Session, ABDGameMode* Mode, int32 Level)
    {
        TravelFrom = Mode;
        Session->StartLevel(Level);
        ++Stage;
    }
public:
    explicit FRun(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (Started == 0.) Started = FPlatformTime::Seconds();
        if (FPlatformTime::Seconds() - Started > 55.)
        {
            Test->AddError(FString::Printf(TEXT("Mirror tuning travel timed out at stage %d"), Stage));
            return true;
        }
        UWorld* World = GEditor->PlayWorld;
        if (!World) return false;
        auto* Session = World->GetGameInstance<UBDAlphaSession>();
        auto* Mode = World->GetAuthGameMode<ABDGameMode>();
        if (!Session || !Mode) return false;
        if (Stage == 0) { Travel(Session, Mode, 1); return false; }
        if (Mode == TravelFrom.Get()) return false;

        if (Stage == 1 || Stage == 4)
        {
            const auto* Playground = Cast<ABDAlphaPlaygroundGameMode>(Mode);
            if (!Playground || !Playground->IsRoomReady()) return false;
        }
        else if (Stage == 5)
        {
            if (!Mode->IsA<ABDAlphaMenuGameMode>()) return false;
        }
        else
        {
            const auto* Mirror = Cast<ABDAlphaMirrorGameMode>(Mode);
            if (!Mirror || !Mirror->IsRoomReady()) return false;
        }
        auto* Player = Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(World, 0));
        if (!Player || Player->IsActorBeingDestroyed() || Player->IsRecovering()) return false;
        if (Stage != 5 && !Test->TestNotNull(TEXT("Delivery room owns its physical crate"), Mode->Crate.Get())) return true;
        UE_LOG(LogTemp, Display, TEXT("Mirror tuning stage %d: screen=%d level=%d sizes=(%.2f,%.2f,%.2f) mass=%.1f"),
            Stage, int32(Session->GetScreen()), Session->GetCurrentLevelIndex(),
            Session->GetParameter(TEXT("PlayerSize")), Session->GetParameter(TEXT("BombSize")),
            Session->GetParameter(TEXT("CrateSize")), Session->GetParameter(TEXT("CrateMass")));

        switch (Stage)
        {
        case 1:
            Sizes(Session, Mode, Player, FVector(1));
            Session->SetParameter(TEXT("CrateMass"), 37.f);
            Mass(Session, Mode, 37.f);
            Travel(Session, Mode, 2);
            break;
        case 2:
        {
            Sizes(Session, Mode, Player, FVector(1.5));
            Mass(Session, Mode, 37.f);
            // Face away from the nearby cargo so a real held bomb has clearance.
            Player->FacePoint(Player->GetActorLocation() + FVector(0, -200, 0));
            auto* Bomb = Mode->DrawBomb(Player);
            if (!Test->TestNotNull(TEXT("Mirror can draw its enlarged bomb"), Bomb)) return true;
            Test->TestTrue(TEXT("Bomb collision uses Mirror's 150 percent size"),
                FMath::IsNearlyEqual(Bomb->GetWorldRadius(), Mode->GetPhysicsSettings().BombRadius * 150.f, .01f));
            Bomb->Destroy();
            Session->SetParameter(TEXT("PlayerSize"), 1.7f);
            Sizes(Session, Mode, Player, FVector(1.7, 1.5, 1.5));
            Session->SetFeature(TEXT("fastFuse"), true);
            Sizes(Session, Mode, Player, FVector(1.7, 1.5, 1.5));
            Test->TestTrue(TEXT("Feature edit applies without replacing Mirror sizes"), Mode->GetPhysicsSettings().bFastCollisionFuse);
            TravelFrom = Mode; Session->Replay(); ++Stage;
            break;
        }
        case 3:
            Sizes(Session, Mode, Player, FVector(1.7, 1.5, 1.5));
            Mass(Session, Mode, 37.f);
            Test->TestTrue(TEXT("Replay retains the shared feature toggle"), Mode->GetPhysicsSettings().bFastCollisionFuse);
            Travel(Session, Mode, 1);
            break;
        case 4:
            Sizes(Session, Mode, Player, FVector(1));
            Mass(Session, Mode, 37.f);
            TravelFrom = Mode; Session->ShowMainMenu(); ++Stage;
            break;
        case 5:
            Sizes(Session, Mode, Player, FVector(1));
            Mass(Session, Mode, 37.f);
            Session->OpenSettings();
            Sizes(Session, Mode, Player, FVector(1));
            Test->TestEqual(TEXT("Menu settings keep the shared crate mass"), Session->GetParameter(TEXT("CrateMass")), 37.f);
            Session->Back();
            Travel(Session, Mode, 2);
            break;
        case 6:
            Sizes(Session, Mode, Player, FVector(1.7, 1.5, 1.5));
            Mass(Session, Mode, 37.f);
            Session->OpenSettings(); Session->ResetTuning();
            Sizes(Session, Mode, Player, FVector(1.5));
            Mass(Session, Mode, FBDPhysicsSettings().CrateMass);
            Test->TestFalse(TEXT("Restore defaults also restores the shared feature"), Mode->GetPhysicsSettings().bFastCollisionFuse);
            Session->Back();
            TravelFrom = Mode; Session->Replay(); ++Stage;
            break;
        case 7:
            Sizes(Session, Mode, Player, FVector(1.5));
            // Only exercise result classification here, not the puzzle solution.
            Mode->CompleteTutorial(); Session->ShowResults();
            Test->TestEqual(TEXT("Completion reaches the results state"), Session->GetScreen(), EBDAlphaScreen::Results);
            Test->TestFalse(TEXT("Authored 150 percent Mirror defaults are not a practice run"), Session->AreResultsPractice());
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDMirrorTuningTest, "BlastDelivery.Alpha.Mirror.TuningProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDMirrorTuningTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDMirrorTuningTests::FRun>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
