#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAirport.h"
#include "../BDAlphaSession.h"
#include "../BDBomb.h"
#include "../BDCharacter.h"
#include "../BDCrate.h"
#include "../BDJungle.h"
#include "../BDMirrorLab.h"
#include "../BDPlayerController.h"
#include "../BDPuzzleActors.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace BDAirportIsolationTests
{
// The existing Mirror and Jungle suites cover their puzzle solutions. These
// guards exercise the missing boundary: visit Airport first, leave through the
// shared session, and then use each existing level's own rules with the same
// shared physics profile. Fixtures modify PIE instances only, never map assets.
class FRoundTrip final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    const int32 Destination;
    int32 Stage = 0;
    double Started = 0., WallMark = 0.;
    float WorldMark = -1.f, FrozenWorldTime = 0.f, FrozenElapsed = 0.f;
    float FrozenFuse = 0.f, FrozenSlide = 0.f, InitialMirrorYaw = 0.f;
    int32 InitialCheckpoint = 0;
    FVector FrozenPosition = FVector::ZeroVector, CratePosition = FVector::ZeroVector;
    TWeakObjectPtr<ABDBomb> Bomb;
    TWeakObjectPtr<ABDCrate> OriginalCrate;
    TWeakObjectPtr<ABDGameMode> OriginalMode;
    TWeakObjectPtr<ABDRotatingLaserMirror> Mirror;

    bool DestinationReady(ABDGameMode* Mode) const
    {
        if (Destination == 2)
        {
            const auto* MirrorMode = Cast<ABDAlphaMirrorGameMode>(Mode);
            return MirrorMode && MirrorMode->IsRoomReady();
        }
        const auto* JungleMode = Cast<ABDJungleGameMode>(Mode);
        return JungleMode && JungleMode->Settings && JungleMode->Passage;
    }

    void AssertProfile(ABDGameMode* Mode, ABDCharacter* Player)
    {
        Test->TestTrue(TEXT("Existing level retains shared crate mass across Airport travel"),
            FMath::IsNearlyEqual(Mode->Crate->Mesh->GetMass(), 37.f, .05f));
        Test->TestTrue(TEXT("Existing level retains shared walking speed in centimetres"),
            FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed,
                Mode->GetPhysicsSettings().MoveSpeed * 100.f, .05f));
        Test->TestTrue(TEXT("Existing level retains shared blast radius in centimetres"),
            FMath::IsNearlyEqual(Mode->BlastRadius, 420.f, .05f));
        Test->TestTrue(TEXT("Existing level still owns a normal shared delivery crate"),
            Mode->Crate->GetClass() == ABDCrate::StaticClass());
    }

public:
    FRoundTrip(FAutomationTestBase* InTest, int32 InDestination)
        : Test(InTest), Destination(InDestination) {}

    bool Update() override
    {
        if (Started <= 0.) Started = FPlatformTime::Seconds();
        if (FPlatformTime::Seconds() - Started > 100.)
        {
            Test->AddError(FString::Printf(TEXT("Airport isolation timed out: destination %d, stage %d"), Destination, Stage));
            return true;
        }
        UWorld* World = GEditor->PlayWorld;
        if (!World) return false;
        auto* Session = World->GetGameInstance<UBDAlphaSession>();
        if (!Session) return false;
        auto* Mode = World->GetAuthGameMode<ABDGameMode>();

        if (Stage == 0)
        {
            Session->StartLevel(3);
            Stage = 1;
            return false;
        }
        if (Stage == 1)
        {
            const auto* Airport = Cast<ABDAlphaAirportGameMode>(Mode);
            if (!Airport || !Airport->IsRoomReady()) return false;
            // Non-geometric tuning avoids changing either authored layout.
            Session->SetParameter(TEXT("CrateMass"), 37.f);
            Session->SetParameter(TEXT("FuseSeconds"), 7.f);
            Session->SetParameter(TEXT("BlastRadius"), 4.2f);
            Session->StartLevel(Destination);
            Stage = 2;
            return false;
        }
        if (!DestinationReady(Mode)) return false;
        auto* Player = Cast<ABDCharacter>(UGameplayStatics::GetPlayerPawn(World, 0));
        auto* PC = Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
        if (!Player || !PC || !Mode->Crate) return false;

        if (Stage == 2)
        {
            if (WorldMark < 0.f) WorldMark = World->GetTimeSeconds();
            if (World->GetTimeSeconds() - WorldMark < .4f || !Player->GetCharacterMovement()->IsMovingOnGround()) return false;
            Test->TestEqual(TEXT("Level selection reaches the requested existing level"), Session->GetCurrentLevelIndex(), Destination);
            Test->TestEqual(TEXT("Existing level starts in common gameplay state"), Session->GetScreen(), EBDAlphaScreen::Playing);
            Test->TestFalse(TEXT("Airport game mode is not retained after travel"), Mode->IsA<ABDAlphaAirportGameMode>());
            AssertProfile(Mode, Player);
            OriginalMode = Mode;
            OriginalCrate = Mode->Crate;
            // Keep desktop cursor movement from changing the controlled slide
            // direction. Pause/session calls below remain the real public flow.
            PC->SetActorTickEnabled(false);
            Bomb = Mode->DrawBomb(Player);
            if (!Test->TestNotNull(TEXT("Existing level can draw a normal timed bomb after Airport"), Bomb.Get())) return true;
            Test->TestFalse(TEXT("Airport-specific bomb conversion does not leak into existing spawn areas"), Bomb->IsBanana());
            Test->TestTrue(TEXT("Bomb receives the same cross-level fuse profile"), FMath::IsNearlyEqual(Bomb->GetProfile().FuseSeconds, 7.f));
            Test->TestTrue(TEXT("Bomb keeps the shared physical radius"),
                FMath::IsNearlyEqual(Bomb->GetWorldRadius(), Mode->GetPhysicsSettings().BombRadius * Mode->GetPhysicsSettings().BombSize * 100.f, .01f));

            if (auto* MirrorMode = Cast<ABDAlphaMirrorGameMode>(Mode))
            {
                Test->TestTrue(TEXT("Mirror uses its ordinary courier, not an Airport pawn"), Player->GetClass() == ABDCharacter::StaticClass());
                if (!Test->TestTrue(TEXT("Four-corner mirror layout remains available after Airport"), MirrorMode->GetMirrors().Num() == 4)) return true;
                Mirror = MirrorMode->GetMirrors()[0];
                InitialMirrorYaw = Mirror->GetCurrentYaw();
                Test->TestTrue(TEXT("A blast event still starts the mirror's quarter turn"),
                    Mirror->ReceiveExplosion(Mirror->GetSwitchPosition() + FVector(0, 0, 50), Mode->BlastRadius, 100001));
            }
            else
            {
                auto* JungleMode = CastChecked<ABDJungleGameMode>(Mode);
                auto* JunglePlayer = Cast<ABDJungleCharacter>(Player);
                if (!Test->TestNotNull(TEXT("Jungle keeps its sliding courier subclass"), JunglePlayer)) return true;
                InitialCheckpoint = JungleMode->CheckpointIndex;
                JunglePlayer->FacePoint(JunglePlayer->GetActorLocation() + FVector(0, -200, 0));
                Test->TestTrue(TEXT("Jungle slide can still start after leaving Airport"), JunglePlayer->StartSlide(120.f, 150.f));
                FrozenSlide = JunglePlayer->GetSlideRemaining();
                Test->TestFalse(TEXT("Merely visiting Airport does not open the Jungle passage"), JungleMode->Passage->bOpen || JungleMode->bPassageCleared);
            }
            FrozenWorldTime = World->GetTimeSeconds();
            FrozenElapsed = Mode->ElapsedSeconds;
            FrozenFuse = Bomb->GetRemainingFuse();
            FrozenPosition = Player->GetActorLocation();
            Session->TogglePause();
            WallMark = FPlatformTime::Seconds();
            Stage = 3;
            return false;
        }
        if (Stage == 3)
        {
            // Use real wall time so a paused-world timer cannot make this test
            // vacuously pass without spending any time in the paused state.
            if (FPlatformTime::Seconds() - WallMark < .85) return false;
            Test->TestTrue(TEXT("Common pause pauses the actual existing world"), UGameplayStatics::IsGamePaused(World));
            Test->TestTrue(TEXT("Pause freezes world time"), FMath::IsNearlyEqual(World->GetTimeSeconds(), FrozenWorldTime, .001f));
            Test->TestTrue(TEXT("Pause freezes the level's elapsed-time score"), FMath::IsNearlyEqual(Mode->ElapsedSeconds, FrozenElapsed, .001f));
            if (!Test->TestNotNull(TEXT("Held bomb survives pause"), Bomb.Get())) return true;
            Test->TestTrue(TEXT("Pause freezes a held bomb fuse"), FMath::IsNearlyEqual(Bomb->GetRemainingFuse(), FrozenFuse, .001f));
            Test->TestTrue(TEXT("Pause freezes character motion"), Player->GetActorLocation().Equals(FrozenPosition, .01f));
            if (Mirror.IsValid())
            {
                Test->TestTrue(TEXT("Paused mirror does not finish its rotation on wall time"), Mirror->IsTurning());
                Test->TestEqual(TEXT("Paused mirror retains its settled orientation"), Mirror->GetCurrentYaw(), InitialMirrorYaw);
            }
            else
                Test->TestTrue(TEXT("Pause does not consume Jungle slide distance"),
                    FMath::IsNearlyEqual(CastChecked<ABDJungleCharacter>(Player)->GetSlideRemaining(), FrozenSlide, .001f));
            Session->OpenSettings();
            Test->TestTrue(TEXT("Settings opened from pause remain paused"), UGameplayStatics::IsGamePaused(World));
            Session->Back();
            Test->TestEqual(TEXT("Leaving settings returns to pause, not a restarted level"), Session->GetScreen(), EBDAlphaScreen::Pause);
            Session->Resume();
            Test->TestFalse(TEXT("Resume unpauses the actual world"), UGameplayStatics::IsGamePaused(World));
            WorldMark = World->GetTimeSeconds();
            Stage = 4;
            return false;
        }
        if (Stage == 4)
        {
            if (World->GetTimeSeconds() - WorldMark < 1.f) return false;
            if (!Test->TestNotNull(TEXT("Normal fuse remains live after resume"), Bomb.Get())) return true;
            Test->TestTrue(TEXT("Normal fuse resumes burning only after resume"), Bomb->GetRemainingFuse() < FrozenFuse - .5f);
            if (Mirror.IsValid())
            {
                Test->TestFalse(TEXT("Mirror resumes and completes its rotation"), Mirror->IsTurning());
                Test->TestTrue(TEXT("Mirror completes exactly one quarter turn"),
                    FMath::IsNearlyEqual(Mirror->GetCurrentYaw(), FMath::Fmod(InitialMirrorYaw + 90.f, 360.f), .01f));
            }
            else
            {
                auto* JunglePlayer = CastChecked<ABDJungleCharacter>(Player);
                Test->TestFalse(TEXT("Jungle resumes and finishes its short slide"), JunglePlayer->IsSliding());
                Test->TestTrue(TEXT("Jungle slide made progress along its fixed route"), Player->GetActorLocation().Y < FrozenPosition.Y - 80.f);
            }
            Bomb->Destroy();
            Bomb.Reset();
            CratePosition = Mode->Crate->GetActorLocation();
            Player->Respawn();
            Test->TestTrue(TEXT("Existing level uses shared independent delayed respawn"), Player->IsRecovering());
            Session->TogglePause();
            FrozenWorldTime = World->GetTimeSeconds();
            WallMark = FPlatformTime::Seconds();
            Stage = 5;
            return false;
        }
        if (Stage == 5)
        {
            if (FPlatformTime::Seconds() - WallMark < Mode->GetPhysicsSettings().RespawnDelay + .25f) return false;
            Test->TestTrue(TEXT("Pause freezes independent respawn timer"), Player->IsRecovering());
            Test->TestTrue(TEXT("Respawn pause does not advance gameplay time"), FMath::IsNearlyEqual(World->GetTimeSeconds(), FrozenWorldTime, .001f));
            Test->TestTrue(TEXT("Player recovery does not replace the level or cargo"), Mode == OriginalMode.Get() && Mode->Crate == OriginalCrate.Get());
            Test->TestFalse(TEXT("Player recovery does not trigger level completion"), Mode->bCompleted);
            Session->Resume();
            WorldMark = World->GetTimeSeconds();
            Stage = 6;
            return false;
        }
        if (Stage == 6)
        {
            if (Player->IsRecovering() || World->GetTimeSeconds() - WorldMark < Mode->GetPhysicsSettings().RespawnDelay + .2f) return false;
            Test->TestTrue(TEXT("Courier independently returns to its saved spawn"), FVector::Dist2D(Player->GetActorLocation(), Player->GetSpawnPosition()) < 3.f);
            Test->TestTrue(TEXT("Courier recovery preserves the current cargo position"), FVector::Dist2D(Mode->Crate->GetActorLocation(), CratePosition) < 3.f);
            AssertProfile(Mode, Player);
            if (Mirror.IsValid())
            {
                Test->TestEqual(TEXT("Respawn preserves the mirror's accepted blast history"), Mirror->GetAcceptedBlastCount(), 1);
                Test->TestEqual(TEXT("Respawn does not replay the mirror activation"), Mirror->GetActivationCount(), 1);
            }
            else
            {
                auto* JungleMode = CastChecked<ABDJungleGameMode>(Mode);
                Test->TestFalse(TEXT("Jungle respawn still grants capture grace"), JungleMode->CanCatchPlayer());
                Test->TestEqual(TEXT("Jungle respawn preserves checkpoint progress"), JungleMode->CheckpointIndex, InitialCheckpoint);
                Test->TestFalse(TEXT("Jungle respawn does not open the gate"), JungleMode->Passage->bOpen || JungleMode->bPassageCleared);
                ABDBananaZone* Zone = nullptr;
                for (TActorIterator<ABDBananaZone> It(World); It; ++It)
                    if (It->ZoneId == TEXT("A")) { Zone = *It; break; }
                if (!Test->TestNotNull(TEXT("Authored Jungle banana zone remains present"), Zone)) return true;
                FVector Position = Zone->GetActorLocation();
                Position.Z = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
                Player->GetCharacterMovement()->StopMovementImmediately();
                Player->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
                Bomb = Mode->DrawBomb(Player);
                if (!Test->TestNotNull(TEXT("Jungle banana remains drawable with the shared bomb interface"), Bomb.Get())) return true;
                Test->TestTrue(TEXT("Jungle's zone conversion remains active after Airport"), Bomb->IsBanana());
                const float BananaFuse = Bomb->GetRemainingFuse();
                Bomb->Tick(8.f);
                Test->TestTrue(TEXT("Jungle banana keeps its non-burning fuse rule"), FMath::IsNearlyEqual(Bomb->GetRemainingFuse(), BananaFuse, .001f));
                Bomb->Explode();
                Test->TestTrue(TEXT("Jungle banana refuses the shared explosion path"), Bomb.IsValid());
                Test->TestEqual(TEXT("Harmless banana still occupies its owner's bomb slot"), Mode->GetOwnedBombCount(Player), 1);
                if (Bomb.IsValid()) Bomb->Destroy();
                Bomb.Reset();
            }
            PC->SetActorTickEnabled(true);
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportMirrorIsolation, "BlastDelivery.Alpha.Isolation.AirportToMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportMirrorIsolation::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDAirportIsolationTests::FRoundTrip>(this, 2));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAirportJungleIsolation, "BlastDelivery.Alpha.Isolation.AirportToJungle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBDAirportJungleIsolation::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Tutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<BDAirportIsolationTests::FRoundTrip>(this, 4));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
