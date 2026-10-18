#include "BDAlphaLook.h"
#include "BDVisuals.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Light.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace BDAlphaLook
{
FLinearColor Color(const TCHAR* Name)
{
    // These are the exact sRGB inspector values in the saved Unity materials.
    struct FPaletteEntry { const TCHAR* Key; FLinearColor Value; };
    const FPaletteEntry Palette[] = {
        {TEXT("Floor"),{.62f,.48f,.31f}}, {TEXT("FloorLight"),{.81f,.68f,.45f}},
        {TEXT("Stone"),{.72f,.58f,.40f}}, {TEXT("Wall"),{.27f,.32f,.39f}},
        {TEXT("Gate"),{.19f,.29f,.33f}}, {TEXT("Pit"),{.20f,.27f,.29f}},
        {TEXT("Backdrop"),{.045f,.31f,.38f}}, {TEXT("Player"),{.08f,.78f,.68f}},
        {TEXT("Light"),{.92f,.97f,.85f}}, {TEXT("Ink"),{.045f,.07f,.11f}},
        {TEXT("Crate"),{.84f,.39f,.12f}}, {TEXT("CrateBand"),{1.f,.64f,.25f}},
        {TEXT("Goal"),{.12f,.62f,.40f}}, {TEXT("GoalOutline"),{.45f,1.f,.73f}},
        {TEXT("GoalSymbol"),{.6f,1.f,.82f}}, {TEXT("Plate"),{1.f,.62f,.14f}},
        {TEXT("PlateActive"),{.18f,.95f,.57f}}, {TEXT("Indicator"),{1.f,.50f,.12f}},
        {TEXT("IndicatorActive"),{.22f,1.f,.62f}}, {TEXT("Takeoff"),{1.f,.60f,.13f}},
        {TEXT("Landing"),{.23f,.88f,.68f}}, {TEXT("BombGlow"),{1.f,.61f,.17f}}
    };
    for (const auto& Entry : Palette)
        if (FCString::Strcmp(Name,Entry.Key)==0)
        {
            auto Linear=[](float V) { return V<=.04045f ? V/12.92f : FMath::Pow((V+.055f)/1.055f,2.4f); };
            return FLinearColor(Linear(Entry.Value.R),Linear(Entry.Value.G),Linear(Entry.Value.B));
        }
    return FLinearColor::White;
}

void Material(UStaticMeshComponent* Mesh, const TCHAR* Name)
{
    if (!Mesh) return;
    const FString Path=FString::Printf(TEXT("/Game/AlphaLook/M_Alpha_%s.M_Alpha_%s"),Name,Name);
    if (UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,*Path)) Mesh->SetMaterial(0,Material);
    else BDVisuals::Material(Mesh,Name); // Existing project remains playable before asset generation.
}

void ConfigurePlaygroundLighting(UWorld* World)
{
    if (!World) return;
    TArray<AActor*> Previous;
    for (TActorIterator<AActor> It(World);It;++It)
        if (It->IsA<ALight>() || It->IsA<ASkyLight>() || It->IsA<APostProcessVolume>()) Previous.Add(*It);
    for (AActor* Actor : Previous) Actor->Destroy();

    ADirectionalLight* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1600),FRotator(-55,-118,0));
    Sun->Tags.Add(TEXT("AlphaPlaygroundLight"));
    UDirectionalLightComponent* Key=Sun->GetComponent();
    Key->SetMobility(EComponentMobility::Movable);
    // ADirectionalLight's root defaults to -46 degrees. Spawn transforms compose
    // with that template rotation, so explicitly set the final world rotation.
    Sun->SetActorRotation(FRotator(-55,-118,0));
    Key->SetLightColor(FLinearColor(1.f,.93f,.80f));
    // Unity Standard's unit directional light uses a different BRDF scale.
    Key->SetIntensity(1.35f*PI);
    Key->SetLightSourceAngle(3.5f);
    Key->SetShadowAmount(.7f);
    Key->SetCastShadows(true);

    ASkyLight* Sky=World->SpawnActor<ASkyLight>();
    Sky->Tags.Add(TEXT("AlphaPlaygroundLight"));
    USkyLightComponent* Ambient=Sky->GetLightComponent();
    Ambient->SetMobility(EComponentMobility::Movable);
    Ambient->SourceType=SLS_SpecifiedCubemap;
    Ambient->SetCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube")));
    Ambient->SetLightColor(FLinearColor(.56f,.65f,.73f));
    Ambient->SetIntensity(1.25f);
    Ambient->SetLowerHemisphereColor(FLinearColor(.56f,.65f,.73f));
    Ambient->bLowerHemisphereIsBlack=false;
    Ambient->SetCastShadows(false);

    APostProcessVolume* Post=World->SpawnActor<APostProcessVolume>();
    Post->Tags.Add(TEXT("AlphaPlaygroundLight"));
    Post->bUnbound=true;
    Post->Priority=100.f;
    FPostProcessSettings& S=Post->Settings;
    S.bOverride_AutoExposureMethod=true; S.AutoExposureMethod=AEM_Manual;
    S.bOverride_AutoExposureApplyPhysicalCameraExposure=true; S.AutoExposureApplyPhysicalCameraExposure=false;
    S.bOverride_AutoExposureBias=true; S.AutoExposureBias=0.f;
    S.bOverride_BloomIntensity=true; S.BloomIntensity=0.f;
    S.bOverride_MotionBlurAmount=true; S.MotionBlurAmount=0.f;
    S.bOverride_VignetteIntensity=true; S.VignetteIntensity=0.f;
    // Unity's source camera has HDR disabled and no cinematic color grading.
    S.bOverride_ToneCurveAmount=true; S.ToneCurveAmount=0.f;
    S.bOverride_ExpandGamut=true; S.ExpandGamut=0.f;
    S.bOverride_AmbientOcclusionIntensity=true; S.AmbientOcclusionIntensity=.18f;
    S.bOverride_AmbientOcclusionRadius=true; S.AmbientOcclusionRadius=35.f;
}
}
