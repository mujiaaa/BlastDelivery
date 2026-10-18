#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "BDAlphaViewportClient.generated.h"

// Reserve real camera space for the playtest panel, matching Unity's camera.rect.
UCLASS()
class BLASTDELIVERY568_API UBDAlphaViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual void LayoutPlayers() override;
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
    virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
    virtual void LostFocus(FViewport* InViewport) override;
    static float PlayfieldFraction(const UWorld* World, float Width, float Height);
};
