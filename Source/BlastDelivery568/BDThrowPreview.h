#pragma once

#include "CoreMinimal.h"

class ABDBomb;
class ABDCharacter;

// First physical contact only; a fuse that expires in flight ends the preview.
struct FBDThrowPreview
{
    TArray<FVector> Points;
    FVector ContactPoint = FVector::ZeroVector;
    FVector ContactNormal = FVector::UpVector;
    bool bHit = false;
    bool bFuseExpired = false;
    float FlightSeconds = 0.f;
};

namespace BDThrowPreview
{
    FBDThrowPreview Calculate(const ABDBomb* Bomb, const ABDCharacter* Player);
}
