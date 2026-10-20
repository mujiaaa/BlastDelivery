#pragma once

#include "CoreMinimal.h"
#include "BDHUD.h"
#include "BDTutorialGameMode.h"
#include "BDTutorialHUD.generated.h"

UCLASS(Config=Game)
class BLASTDELIVERY568_API ABDTutorialHUD : public ABDHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    // Normalized screen anchor; icon coordinates below are in a 520 x 112 design panel.
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Icons") FVector2D OverlayAnchor = FVector2D(.5f,.035f);
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Icons") float IconScale = 1.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Icons") float StrokeWidth = 3.f;
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Feedback") FVector2D CheckAnchor = FVector2D(.5f,.43f);
    UPROPERTY(EditDefaultsOnly, Config, Category="Tutorial|Feedback") float CheckSize = 82.f;
private:
    FVector2D Origin;
    float Scale = 1.f;
    FLinearColor Ink;
    FLinearColor Accent;
    FLinearColor Green;
    void Line(FVector2D A, FVector2D B, FLinearColor Color, float Width = 3.f);
    void Polygon(const TArray<FVector2D>& Points, FLinearColor Color);
    void Circle(FVector2D Center, float Radius, FLinearColor Color, bool bFill = false);
    void Arrow(FVector2D Start, FVector2D Control, FVector2D End, FLinearColor Color);
    void Mouse(FVector2D P, bool bRight);
    void Bomb(FVector2D P, float Radius, FLinearColor Color);
    void Box(FVector2D P, float Size, FLinearColor Color);
    void Burst(FVector2D P, float Radius, FLinearColor Color);
    void Check(FVector2D P, float Size, FLinearColor Color);
    void Instruction(EBDTutorialScene Scene, int32 Index, float Offset, float Flash);
};
