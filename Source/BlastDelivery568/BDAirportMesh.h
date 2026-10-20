#pragma once
#include "CoreMinimal.h"
class UStaticMesh;

// Airport procedural geometry only; no imported packages or shared materials.
namespace BDAirportMesh
{
    UStaticMesh* Build(UObject* Outer,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,bool bConvexCollision=false);
}
