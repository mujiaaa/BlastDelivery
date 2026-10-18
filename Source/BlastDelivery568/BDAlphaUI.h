#pragma once

#include "CoreMinimal.h"

class SWidget;
class UBDAlphaSession;

namespace BDAlphaUI
{
    // The controller owns the viewport attachment; the widget keeps a weak session reference.
    TSharedRef<SWidget> Create(UBDAlphaSession* Session);
}
