#include "BDTutorialHUD.h"
#include "BDAlphaSession.h"
#include "BDBomb.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"
#include "Kismet/GameplayStatics.h"

void ABDTutorialHUD::Line(FVector2D A, FVector2D B, FLinearColor Color, float Width)
{
    A = Origin+A*Scale;
    B = Origin+B*Scale;
    DrawLine(A.X,A.Y,B.X,B.Y,Color,Width*Scale*StrokeWidth/3.f);
}
void ABDTutorialHUD::Polygon(const TArray<FVector2D>& Points, FLinearColor Color)
{
    for (int32 I=1; I+1<Points.Num(); ++I)
    {
        FCanvasTriangleItem Tri(Origin+Points[0]*Scale,Origin+Points[I]*Scale,Origin+Points[I+1]*Scale,GWhiteTexture);
        Tri.SetColor(Color);
        Tri.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Tri);
    }
}
void ABDTutorialHUD::Circle(FVector2D Center, float Radius, FLinearColor Color, bool bFill)
{
    TArray<FVector2D> Points;
    for (int32 I=0; I<=32; ++I)
    {
        const float Angle = 2.f*PI*I/32.f;
        Points.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
        if (!bFill && I>0) Line(Points[I-1],Points[I],Color);
    }
    if (bFill) Polygon(Points,Color);
}
void ABDTutorialHUD::Arrow(FVector2D Start, FVector2D Control, FVector2D End, FLinearColor Color)
{
    FVector2D Last = Start;
    for (int32 I=1; I<=24; ++I)
    {
        const float T = I/24.f;
        const FVector2D P = (1-T)*(1-T)*Start+2*(1-T)*T*Control+T*T*End;
        Line(Last,P,Color);
        Last = P;
    }
    const FVector2D Direction = (End-Control).GetSafeNormal();
    const FVector2D Normal(-Direction.Y,Direction.X);
    Line(End,End-Direction*11+Normal*6,Color);
    Line(End,End-Direction*11-Normal*6,Color);
}
void ABDTutorialHUD::Mouse(FVector2D P, bool bRight)
{
    // Split mouse silhouette, with only the requested top button filled.
    const float X = bRight ? 0.f : -24.f;
    Polygon({P+FVector2D(X+2,-6),P+FVector2D(X+2,-26),P+FVector2D(X+7,-34),
        P+FVector2D(X+17,-34),P+FVector2D(X+22,-26),P+FVector2D(X+22,-6)},Accent);
    TArray<FVector2D> Outline = { {-24,15},{-24,-21},{-21,-31},{-13,-38},{0,-40},
        {13,-38},{21,-31},{24,-21},{24,15},{20,26},{12,33},{0,36},{-12,33},{-20,26},{-24,15} };
    for (int32 I=1; I<Outline.Num(); ++I) Line(P+Outline[I-1],P+Outline[I],Ink);
    Line(P+FVector2D(-24,-4),P+FVector2D(24,-4),Ink);
    Line(P+FVector2D(0,-39),P+FVector2D(0,-4),Ink);
    Line(P+FVector2D(0,-26),P+FVector2D(0,-17),Ink,5.f);
}
void ABDTutorialHUD::Bomb(FVector2D P, float Radius, FLinearColor Color)
{
    Circle(P,Radius,Color);
    Line(P+FVector2D(3,-Radius),P+FVector2D(5,-Radius-8),Color);
    Line(P+FVector2D(5,-Radius-8),P+FVector2D(12,-Radius-11),Color);
    Circle(P+FVector2D(14,-Radius-12),2.f,Accent,true);
}
void ABDTutorialHUD::Box(FVector2D P, float Size, FLinearColor Color)
{
    const FVector2D A = P+FVector2D(-Size*.5f,-Size*.5f), B = A+FVector2D(Size,0);
    const FVector2D C = A+FVector2D(Size,Size), D = A+FVector2D(0,Size), Depth(8,-7);
    Line(A,B,Color); Line(B,C,Color); Line(C,D,Color); Line(D,A,Color);
    Line(A,A+Depth,Color); Line(A+Depth,B+Depth,Color); Line(B,B+Depth,Color);
    Line(B+Depth,C+Depth,Color); Line(C,C+Depth,Color);
    Line(P+FVector2D(-Size*.17f,-Size*.5f),P+FVector2D(-Size*.17f,Size*.5f),Color,2.f);
}
void ABDTutorialHUD::Burst(FVector2D P, float Radius, FLinearColor Color)
{
    for (int32 I=0; I<8; ++I)
    {
        const float A = 2.f*PI*I/8.f;
        const FVector2D D(FMath::Cos(A),FMath::Sin(A));
        Line(P+D*Radius,P+D*(Radius+8),Color,2.5f);
    }
}
void ABDTutorialHUD::Check(FVector2D P, float Size, FLinearColor Color)
{
    Line(P+FVector2D(-.48f,0)*Size,P+FVector2D(-.12f,.34f)*Size,Color,Size*.12f);
    Line(P+FVector2D(-.12f,.34f)*Size,P+FVector2D(.56f,-.42f)*Size,Color,Size*.12f);
}

void ABDTutorialHUD::Instruction(EBDTutorialScene Scene, int32 Index, float Offset, float Flash)
{
    Origin = FVector2D(Canvas->SizeX*OverlayAnchor.X-260*Scale+Offset,Canvas->SizeY*OverlayAnchor.Y);
    const FLinearColor Success(.1f,1.f,.3f);
    Ink = FMath::Lerp(FLinearColor(.92f,.96f,.95f),Success,Flash);
    Accent = FMath::Lerp(FLinearColor(.14f,.85f,.94f),Success,Flash);
    // Same linear RGB as /Game/Materials/M_Goal (create_tutorial.py palette).
    Green = FMath::Lerp(FLinearColor(.06f,.65f,.27f),Success,Flash);
    DrawRect(FLinearColor(.018f,.035f,.045f,.92f),Origin.X,Origin.Y,520*Scale,112*Scale);
    const FLinearColor Ghost(Ink.R,Ink.G,Ink.B,.28f);
    const auto* Mode = GetWorld()->GetAuthGameMode<ABDTutorialGameMode>();
    switch (Scene)
    {
    case EBDTutorialScene::Throw:
        Mouse(FVector2D(150,54),false);
        Arrow(FVector2D(190,60),FVector2D(240,10),FVector2D(298,42),Ink);
        Bomb(FVector2D(338,52),19,Ink);
        Line(FVector2D(298,65),FVector2D(313,59),Ink,2);
        Line(FVector2D(303,75),FVector2D(319,68),Ink,2);
        break;
    case EBDTutorialScene::MoveBox:
        Mouse(FVector2D(65,54),false);
        Arrow(FVector2D(104,60),FVector2D(145,8),FVector2D(202,40),Ink);
        Bomb(FVector2D(238,61),17,Ink);
        Burst(FVector2D(238,61),28,Accent);
        Box(FVector2D(322,60),34,Ghost);
        Arrow(FVector2D(339,34),FVector2D(375,15),FVector2D(410,35),Ink);
        Box(FVector2D(442,60),34,Ink);
        break;
    case EBDTutorialScene::PutDown:
    {
        Mouse(FVector2D(128,58),true);
        // A contact dot holds on RMB, then lifts into two short release strokes.
        // Keep RMB cyan throughout, and keep the single bomb on the ground.
        const bool bHoldingCue = Mode && Mode->Phase == EBDTutorialPhase::Playing
            && FMath::Fmod(Mode->GetPhaseTime(),2.8f) < 1.2f;
        if (bHoldingCue) Circle(FVector2D(140,37),3.f,Ink,true);
        else
        {
            Line(FVector2D(141,14),FVector2D(144,7),Ink,2.f);
            Line(FVector2D(151,21),FVector2D(158,16),Ink,2.f);
        }
        // This is an input-to-result arrow, ending before the stationary bomb.
        Arrow(FVector2D(176,62),FVector2D(239,26),FVector2D(308,58),Ink);
        Bomb(FVector2D(364,66),18,Ink);
        Line(FVector2D(333,85),FVector2D(395,85),Ink);
        // Short contact/placement marks emphasize "here" when RMB is released.
        Line(FVector2D(358,83),FVector2D(370,83),Ink,2.f);
        if (!bHoldingCue)
        {
            Line(FVector2D(338,73),FVector2D(344,78),Ink,2.f);
            Line(FVector2D(384,78),FVector2D(390,73),Ink,2.f);
        }
        break;
    }
    case EBDTutorialScene::CrossGap:
        Mouse(FVector2D(65,58),true);
        Arrow(FVector2D(108,62),FVector2D(155,24),FVector2D(201,51),Ink);
        Bomb(FVector2D(242,67),15,Ink);
        Burst(FVector2D(242,67),22,Ink);
        // A small airborne figure belongs inside this instruction panel only.
        // Raised arms and bent legs read as a blast launch above the left ledge.
        Circle(FVector2D(320,35),7,Ink);
        Line(FVector2D(317,44),FVector2D(309,60),Ink,2.5f);
        Line(FVector2D(315,48),FVector2D(302,44),Ink,2.5f);
        Line(FVector2D(302,44),FVector2D(299,35),Ink,2.5f);
        Line(FVector2D(315,48),FVector2D(329,44),Ink,2.5f);
        Line(FVector2D(329,44),FVector2D(332,35),Ink,2.5f);
        Line(FVector2D(309,60),FVector2D(298,67),Ink,2.5f);
        Line(FVector2D(298,67),FVector2D(292,62),Ink,2.5f);
        Line(FVector2D(309,60),FVector2D(317,69),Ink,2.5f);
        Line(FVector2D(317,69),FVector2D(324,65),Ink,2.5f);
        Arrow(FVector2D(346,47),FVector2D(395,0),FVector2D(449,75),Ink);
        Line(FVector2D(281,88),FVector2D(335,88),Ink);
        Line(FVector2D(414,88),FVector2D(478,88),Ink);
        break;
    case EBDTutorialScene::Deliver:
        Box(FVector2D(162,31),27,Ink);
        Arrow(FVector2D(198,30),FVector2D(225,49),FVector2D(206,67),Ink);
        Polygon({{127,65},{197,65},{211,91},{113,91}},FLinearColor(Green.R,Green.G,Green.B,.4f));
        Line(FVector2D(127,65),FVector2D(197,65),Green,4);
        Line(FVector2D(197,65),FVector2D(211,91),Green,4);
        Line(FVector2D(211,91),FVector2D(113,91),Green,4);
        Line(FVector2D(113,91),FVector2D(127,65),Green,4);
        Box(FVector2D(162,77),19,Ghost);
        Arrow(FVector2D(250,68),FVector2D(296,20),FVector2D(347,47),Ink);
        Check(FVector2D(391,58),48,Green);
        break;
    }
    const int32 Count = Mode ? Mode->SceneOrder.Num() : 5;
    for (int32 I=0; I<Count; ++I)
        Circle(FVector2D(260+(I-(Count-1)*.5f)*13,105),2.5f,I<=Index ? Accent : Ghost,true);
}

void ABDTutorialHUD::DrawHUD()
{
    Super::DrawHUD();
    if (const auto* Session=GetGameInstance<UBDAlphaSession>())
        if (Session->GetScreen()!=EBDAlphaScreen::Playing || !Session->IsHUDVisible()) return;
    const auto* Mode = GetWorld()->GetAuthGameMode<ABDTutorialGameMode>();
    if (!Canvas || !Mode) return;
    // Width and height both constrain scaling, including narrow/resized windows.
    Scale = FMath::Max(.1f,FMath::Min(Canvas->SizeX/580.f,Canvas->SizeY/800.f)*IconScale);
    Scale = FMath::Min(Scale,Canvas->SizeX/540.f);
    const float Slide = Mode->Phase == EBDTutorialPhase::Sliding ? Mode->GetSlideAlpha() : 0.f;
    const float Offset = -Slide*Canvas->SizeX;
    const bool bFeedback = Mode->Phase == EBDTutorialPhase::Success || Mode->Phase == EBDTutorialPhase::Finished;
    const float Flash = Mode->Phase == EBDTutorialPhase::Success ? FMath::Clamp(1.f-Mode->GetPhaseTime()/.4f,0.f,1.f) : 0.f;
    Instruction(Mode->GetScene(),Mode->SceneIndex,Offset,Flash);
    if (Mode->Phase == EBDTutorialPhase::Sliding)
        Instruction(Mode->GetNextScene(),Mode->SceneIndex+1,(1.f-Slide)*Canvas->SizeX,0.f);
    if (bFeedback)
    {
        Origin = FVector2D(Canvas->SizeX*CheckAnchor.X,Canvas->SizeY*CheckAnchor.Y);
        const float T = FMath::Clamp(Mode->GetPhaseTime()/.2f,0.f,1.f);
        const float U = T-1.f;
        const float Pop = Mode->Phase == EBDTutorialPhase::Finished ? 1.f : 1.f+2.70158f*U*U*U+1.70158f*U*U;
        const float Size = CheckSize*Pop;
        Circle(FVector2D::ZeroVector,Size*.75f,FLinearColor(.015f,.07f,.035f,.86f),true);
        Check(FVector2D::ZeroVector,Size,FLinearColor(.1f,1.f,.3f));
    }
    if ((Mode->Phase == EBDTutorialPhase::Playing || Mode->Phase == EBDTutorialPhase::Success) && IsValid(Mode->ActiveBomb))
    {
        FVector2D P;
        if (GetOwningPlayerController()->ProjectWorldLocationToScreen(Mode->ActiveBomb->GetActorLocation()+FVector(0,0,42),P))
        {
            Origin = P;
            const float Fraction = FMath::Clamp(Mode->ActiveBomb->GetRemainingFuse()/Mode->FuseSeconds,0.f,1.f);
            FVector2D Last(0,-12);
            for (int32 I=1; I<=32; ++I)
            {
                const float A = -PI*.5f+2.f*PI*Fraction*I/32.f;
                const FVector2D Next(FMath::Cos(A)*12,FMath::Sin(A)*12);
                Line(Last,Next,FLinearColor(1.f,.65f,.1f),2);
                Last = Next;
            }
            if (Mode->GetScene() == EBDTutorialScene::CrossGap && Fraction < .3f)
                Burst(FVector2D::ZeroVector,20,Accent);
        }
    }
    if (UGameplayStatics::IsGamePaused(this))
    {
        DrawRect(FLinearColor(0,0,0,.3f),0,0,Canvas->SizeX,Canvas->SizeY);
        const float X = Canvas->SizeX*.5f, Y = Canvas->SizeY*.55f;
        DrawRect(FLinearColor::White,X-18*Scale,Y-25*Scale,12*Scale,50*Scale);
        DrawRect(FLinearColor::White,X+6*Scale,Y-25*Scale,12*Scale,50*Scale);
    }
}
