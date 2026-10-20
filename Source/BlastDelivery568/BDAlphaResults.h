#pragma once

#include "CoreMinimal.h"

class SWidget;
class UBDAlphaSession;

namespace BDAlphaResults
{
    // Cosmetic Slate layers use real time, so the paused results world stays frozen.
    TSharedRef<SWidget> Stars(UBDAlphaSession* Session);
    TSharedRef<SWidget> BombRain(UBDAlphaSession* Session);
}
