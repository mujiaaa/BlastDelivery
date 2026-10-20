#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

// All meshes ship with Unreal. Palette assets are made by Scripts/create_tutorial.py.
namespace BDVisuals
{
// New Alpha-only bases expose parameters; legacy tutorial materials stay intact.
inline UMaterialInstanceDynamic* Tint(UStaticMeshComponent* Mesh, FLinearColor Color, bool bUnlit=false)
{
    if (!Mesh) return nullptr;
    const TCHAR* Path=bUnlit ? TEXT("/Game/AlphaLook/M_AlphaUnlit.M_AlphaUnlit")
        : TEXT("/Game/AlphaLook/M_AlphaLit.M_AlphaLit");
    UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,Path);
    if (!Base) return nullptr;
    UMaterialInstanceDynamic* Material=UMaterialInstanceDynamic::Create(Base,Mesh);
    Material->SetVectorParameterValue(TEXT("Color"),Color);
    Material->SetScalarParameterValue(TEXT("Emission"),bUnlit ? 1.f : 0.f);
    Material->SetScalarParameterValue(TEXT("Roughness"),.82f);
    Mesh->SetMaterial(0,Material);
    return Material;
}
inline void Material(UStaticMeshComponent* Mesh, const TCHAR* Name)
{
    if (!IsValid(Mesh)) return;
    const FString Path = FString::Printf(TEXT("/Game/Materials/M_%s.M_%s"), Name, Name);
    if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Path))
        Mesh->SetMaterial(0, Mat);
}
inline void Shape(UStaticMeshComponent* Mesh, const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *Path));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
}
}
