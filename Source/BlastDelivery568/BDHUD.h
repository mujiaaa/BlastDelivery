#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BDHUD.generated.h"

UCLASS()
class BLASTDELIVERY568_API ABDHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void DrawAlphaGuides();
    void DrawThrowGuide(const class ABDCharacter* Player, const class ABDBomb* Bomb, float Width);
    void WorldLine(const FVector& A, const FVector& B, const FLinearColor& Color, float Width);
    void Label(const FString& Text, const FVector& Location, const FLinearColor& Color);
};
