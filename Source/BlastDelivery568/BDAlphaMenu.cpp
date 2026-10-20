#include "BDAlphaMenu.h"
#include "BDCharacter.h"
#include "BDBomb.h"
#include "BDCrate.h"
#include "BDPlayerController.h"
#include "BDPuzzleActors.h"
#include "BDAlphaLook.h"
#include "BDVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

ABDAlphaMenuGameMode::ABDAlphaMenuGameMode() { bAlphaPlayground=true; }
void ABDAlphaMenuGameMode::BeginPlay()
{
    Super::BeginPlay();
    TArray<AActor*> Old;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->IsA<ABDCharacter>() || It->IsA<ABDBomb>() || It->IsA<ABDCrate>() || It->IsA<ABDBarrier>()
            || It->IsA<ABDPressurePlate>() || It->IsA<ABDGoal>() || It->IsA<AStaticMeshActor>() || It->IsA<APlayerStart>()) Old.Add(*It);
    for (AActor* Actor:Old) Actor->Destroy();
    Crate=nullptr; Goal=nullptr; Gate=nullptr; ActiveBomb=nullptr;
    BDAlphaLook::ConfigurePlaygroundLighting(GetWorld());
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Floor=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,-50),FRotator::ZeroRotator,P);
    auto* Mesh=Floor->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
    BDVisuals::Shape(Mesh,TEXT("Cube")); Mesh->SetWorldScale3D(FVector(200,200,1));
    BDVisuals::Tint(Mesh,FLinearColor(.028f,.10f,.13f),true);
    Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    for (int32 I=0;I<4;++I)
    {
        auto* Wall=GetWorld()->SpawnActor<ABDBarrier>(); Wall->Mesh->SetVisibility(false); Walls.Add(Wall);
    }
    MenuPlayer=GetWorld()->SpawnActor<ABDCharacter>(FVector(-350,0,95),FRotator(0,0,0),P);
    if (MenuPlayer) MenuPlayer->SetRespawnCheckpoint(MenuPlayer->GetActorLocation());
}
void ABDAlphaMenuGameMode::Tick(float DeltaSeconds)
{
    // The room is a live toy, not a timed delivery attempt.
    auto* PC=Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!PC || !MenuPlayer) return;
    if (!bPossessed)
    {
        if (APawn* Previous=PC->GetPawn()) if (Previous!=MenuPlayer) Previous->Destroy();
        PC->Possess(MenuPlayer); PC->SetPlaygroundView(FVector(0,0,90),0.f,600.f); bPossessed=true;
    }
    int32 W,H; PC->GetViewportSize(W,H);
    const float Aspect=W>0&&H>0?float(W)/H:16.f/9.f;
    const float HalfWidth=PC->GetViewWidth()*.5f;
    const float HalfDepth=(PC->GetViewWidth()/Aspect*.5f)/FMath::Sin(FMath::DegreesToRadians(57.5f));
    for (int32 I=0;I<Walls.Num();++I)
    {
        const bool Side=I<2; const float Sign=(I%2)?1.f:-1.f;
        Walls[I]->SetActorLocation(Side?FVector(Sign*HalfWidth,0,0):FVector(0,Sign*HalfDepth,0));
        Walls[I]->Configure(Side?FVector(30,2*HalfDepth+100,1400):FVector(2*HalfWidth+100,30,1400));
    }
}
