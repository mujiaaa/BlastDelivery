#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;
class UWorld;

// The Unity Alpha palette is isolated from the teammate's tutorial materials.
namespace BDAlphaLook
{
    FLinearColor Color(const TCHAR* Name);
    void Material(UStaticMeshComponent* Mesh, const TCHAR* Name);
    void ConfigurePlaygroundLighting(UWorld* World);
}
