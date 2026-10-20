#include "BDAlphaResults.h"
#include "BDAlphaSession.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
const FLinearColor ResultInk = FLinearColor::FromSRGBColor(FColor(6, 13, 18));
const FLinearColor Teal = FLinearColor::FromSRGBColor(FColor(14, 59, 69));
const FLinearColor Cream = FLinearColor::FromSRGBColor(FColor(255, 227, 168));
const FLinearColor Mint = FLinearColor::FromSRGBColor(FColor(64, 230, 184));
const FLinearColor Amber = FLinearColor::FromSRGBColor(FColor(255, 153, 41));
// Let Slate round by each element's own half-height. A fixed radius larger than
// the painted bounds can discard the entire rounded-box fill on UE's shader.
const FSlateRoundedBoxBrush CircleBrush(FLinearColor::White);
const FSlateRoundedBoxBrush BoxBrush(FLinearColor::White, 3.f);
constexpr float BodyRadius = 77.f;

float Noise(int32 Index, int32 Salt)
{
    uint32 V = uint32(Index) * 747796405u + uint32(Salt) * 2891336453u;
    V = ((V >> ((V >> 28u) + 4u)) ^ V) * 277803737u;
    return float((V >> 22u) ^ V) / float(MAX_uint32);
}
float Smooth(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
FLinearColor Alpha(FLinearColor C, float A) { C.A *= A; return C; }

void Circle(FSlateWindowElementList& Elements, int32& Layer, const FGeometry& G,
    FVector2D Center, FVector2D Radius, FLinearColor Color)
{
    FSlateDrawElement::MakeBox(Elements, ++Layer, G.ToPaintGeometry(Radius * 2,
        FSlateLayoutTransform(Center - Radius)), &CircleBrush, ESlateDrawEffect::None, Color);
}

// Clip each triangle against the fill line, keeping the star silhouette fixed.
// This fills from its base upwards instead of scaling a smaller star in the slot.
void Star(FSlateWindowElementList& Elements, int32& Layer, const FGeometry& G,
    FVector2D Center, float Radius, float Fill, FLinearColor Color)
{
    if (Fill <= 0.f) return;
    const float Cut = Center.Y + Radius - Radius * 2 * FMath::Clamp(Fill, 0.f, 1.f);
    TArray<FVector2D> Points;
    for (int32 I = 0; I < 10; ++I)
    {
        const float Angle = -PI * .5f + I * PI * .2f;
        Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius * (I % 2 ? .46f : 1.f));
    }
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;
    const FColor Tint = Color.ToFColor(true);
    for (int32 I = 0; I < 10; ++I)
    {
        const TArray<FVector2D> Input = {Center, Points[I], Points[(I + 1) % 10]};
        TArray<FVector2D> Clipped;
        for (int32 J = 0; J < 3; ++J)
        {
            const FVector2D A = Input[J], B = Input[(J + 1) % 3];
            const bool InsideA = A.Y >= Cut, InsideB = B.Y >= Cut;
            if (InsideA) Clipped.Add(A);
            if (InsideA != InsideB) Clipped.Add(FMath::Lerp(A, B, (Cut - A.Y) / (B.Y - A.Y)));
        }
        const SlateIndex Base = SlateIndex(Vertices.Num());
        for (const FVector2D& P : Clipped)
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),
                FVector2f(P), FVector2f(.5f, .5f), Tint));
        for (int32 J = 1; J + 1 < Clipped.Num(); ++J)
        {
            Indices.Add(Base); Indices.Add(Base + J); Indices.Add(Base + J + 1);
        }
    }
    if (Indices.IsEmpty()) return;
    const FSlateResourceHandle White = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
    FSlateDrawElement::MakeCustomVerts(Elements, ++Layer, White, Vertices, Indices, nullptr, 0, 0);
}

class SAlphaResultsEffect : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaResultsEffect) : _Session(nullptr), _Rain(false) {}
        SLATE_ARGUMENT(UBDAlphaSession*, Session)
        SLATE_ARGUMENT(bool, Rain)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Session = Args._Session; bRain = Args._Rain;
        SetCanTick(true);
        SetVisibility(EVisibility::HitTestInvisible);
        SetClipping(EWidgetClipping::ClipToBounds);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SLeafWidget::Tick(Geometry, Time, Delta);
        if (!Session.IsValid()) return;
        if (Revision != Session->GetResultsRevision())
        {
            Revision = Session->GetResultsRevision(); Elapsed = 0;
        }
        Elapsed += FMath::Max(0.f, Delta);
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        if (!Session.IsValid()) return Layer;
        return bRain ? PaintRain(Elements, Layer, G) : PaintStars(Elements, Layer, G);
    }
private:
    TWeakObjectPtr<UBDAlphaSession> Session;
    int32 Revision = -1;
    double Elapsed = 0;
    bool bRain = false;

    int32 PaintStars(FSlateWindowElementList& E, int32 Layer, const FGeometry& G) const
    {
        const FVector2D Size = G.GetLocalSize();
        const float Radius = FMath::Min(float(Size.Y * .33), float(Size.X / 7));
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector2D Center(Size.X * (I + .5) / 3, Size.Y * .5);
            const bool Earned = (Session->GetResultStarMask() & (1 << I)) != 0;
            const float Phase = float(Elapsed) - (.30f + I * .58f);
            const float Fill = Earned ? Smooth(Phase / .68f) : 0.f;
            const float Age = Phase - .68f;
            const float Pop = Earned && Age >= 0 ? FMath::Sin(Age * 16) * FMath::Exp(-Age * 5) * .18f : 0.f;
            const float R = Radius * (1 + Pop);
            if (Fill > 0)
            {
                const float Flash = Age >= 0 ? FMath::Exp(-Age * 4) : 0;
                for (int32 Glow = 4; Glow > 0; --Glow)
                    Circle(E, Layer, G, Center, FVector2D(R * (1 + Glow * .20)), Alpha(Amber, Fill * (.025f + Flash * .06f)));
            }
            Star(E, Layer, G, Center + FVector2D(0, 5), R, 1, Alpha(ResultInk, .45f));
            Star(E, Layer, G, Center, R, 1, Alpha(Cream, .30f));
            Star(E, Layer, G, Center, R * .86f, 1, Teal);
            Star(E, Layer, G, Center, R, Fill, Amber);
            Star(E, Layer, G, Center, R * .82f, Fill, Cream);
            if (!Earned) continue;
            for (int32 P = 0; P < 13; ++P)
            {
                const float Travel = (Phase - P * .037f) / .40f;
                if (Travel < 0 || Travel > 1) continue;
                const float Side = P % 2 == 0 ? -1.f : 1.f;
                const FVector2D A = Center + FVector2D(Side * R * (1.4f + Noise(P, I + 11) * .5f), R);
                const FVector2D B = Center + FVector2D(Side * R * 1.4, -R);
                const FVector2D C = Center + FVector2D((Noise(P, I + 17) - .5f) * R * .7, R * .2);
                const float T = Smooth(Travel), Inv = 1 - T;
                const FVector2D Point = A * Inv * Inv + B * 2 * Inv * T + C * T * T;
                Star(E, Layer, G, Point, R * .12f, 1, Alpha(P % 3 ? Cream : Mint, FMath::Sin(Travel * PI)));
            }
            if (Age >= 0 && Age < .75f)
            {
                const float T = Age / .75f;
                for (int32 P = 0; P < 8; ++P)
                {
                    const float A = P * PI * .25f;
                    const FVector2D Point = Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R * (1.12 + T * .48);
                    Star(E, Layer, G, Point, R * .14f, 1, Alpha(Cream, (1 - T) * (1 - T)));
                }
            }
        }
        return Layer;
    }

    void PaintBomb(FSlateWindowElementList& E, int32& Layer, const FGeometry& G,
        FVector2D Center, float Rotation, float Squash) const
    {
        const FVector2D Radii(BodyRadius * (1 + Squash), BodyRadius * (1 - Squash));
        const float A = FMath::DegreesToRadians(Rotation);
        const auto Rotate = [Center, A](FVector2D P)
        { return Center + FVector2D(P.X * FMath::Cos(A) - P.Y * FMath::Sin(A), P.X * FMath::Sin(A) + P.Y * FMath::Cos(A)); };
        Circle(E, Layer, G, Center + FVector2D(5, 9), Radii, Alpha(ResultInk, .4f));
        // Fuse and cap are behind the ball, giving the falling silhouettes a clear spin.
        const TArray<FVector2D> Fuse = {Rotate(FVector2D(0, -65)), Rotate(FVector2D(4, -91)),
            Rotate(FVector2D(20, -108)), Rotate(FVector2D(27, -130))};
        FSlateDrawElement::MakeLines(E, ++Layer, G.ToPaintGeometry(), Fuse, ESlateDrawEffect::None, Cream, true, 9.f);
        Circle(E, Layer, G, Rotate(FVector2D(0, -67)), FVector2D(25, 19), Cream);
        Circle(E, Layer, G, Center, Radii, Cream);
        Circle(E, Layer, G, Center, Radii - FVector2D(5, 5), ResultInk);
        Circle(E, Layer, G, Center + FVector2D(-5, -6), (Radii - FVector2D(12, 12)), Teal);
        Circle(E, Layer, G, Center + FVector2D(-25, -29), FVector2D(16, 10), Alpha(Cream, .75f));
        Circle(E, Layer, G, Center + FVector2D(22, 28), FVector2D(25, 7), Alpha(ResultInk, .28f));
        Star(E, Layer, G, Rotate(FVector2D(27, -130)), 13.f, 1.f, Amber);
    }

    int32 PaintRain(FSlateWindowElementList& E, int32 Layer, const FGeometry& G) const
    {
        const int32 Count = FMath::Max(0, Session->GetResultBombsUsed());
        if (Count == 0 || Elapsed < .12) return Layer;
        const FVector2D Size = G.GetLocalSize();
        const float Pitch = BodyRadius * 2.12f;
        const int32 Columns = FMath::Clamp(FMath::FloorToInt((Size.X - 42) / Pitch - .5), 1, Count);
        const float Left = (Size.X - (Columns + .5) * Pitch) * .5;
        const float Speed = FMath::Clamp(FMath::Sqrt(Count / 45.f), 1.f, 9.f);
        const float FallDuration = FMath::Sqrt(2 * (Size.Y + 70) / 1450) / Speed;
        const float SettleDuration = .62f / Speed;
        const double Interval = (.80 + FMath::Min(3.2, FMath::Sqrt(double(Count)) * .08)) / Count;
        const int32 Spawned = FMath::Min(Count, FMath::FloorToInt((Elapsed - .12) / Interval) + 1);
        // Each counted bomb is emitted. Once rows overflow above the screen they
        // are analytically asleep; no actors or thousands of Slate widgets accumulate.
        const int32 VisibleRows = FMath::CeilToInt((Size.Y + BodyRadius * 2) / (BodyRadius * 1.78f));
        const int32 Visible = FMath::Min(Spawned, VisibleRows * Columns);
        for (int32 I = 0; I < Visible; ++I)
        {
            const int32 Row = I / Columns;
            // Reverse alternating rows keeps every slot unique while mixing the rain.
            const int32 Column = Row % 2 ? Columns - 1 - I % Columns : I % Columns;
            const FVector2D Rest(Left + (Column + .5 + (Row % 2) * .5) * Pitch,
                Size.Y - 7 - BodyRadius - Row * BodyRadius * 1.78f);
            const float Age = float(Elapsed - (.12 + I * Interval));
            const float Fall = FMath::Clamp(Age / FallDuration, 0.f, 1.f);
            const float Bounce = FMath::Clamp((Age - FallDuration) / SettleDuration, 0.f, 1.f);
            const float SourceX = FMath::Clamp(float(Rest.X + (Noise(I, 1) - .5f) * 350), BodyRadius + 8, float(Size.X) - BodyRadius - 8);
            FVector2D Position(FMath::Lerp(double(SourceX), Rest.X, Smooth(Fall)),
                FMath::Lerp(FMath::Min(double(-BodyRadius * 3), Rest.Y - BodyRadius * 4), Rest.Y, double(Fall * Fall)));
            float Rotation = FMath::Lerp((Noise(I, 2) - .5f) * 170, (Noise(I, 3) - .5f) * 32, Smooth(Fall));
            float Squash = 0;
            if (Fall >= 1 && Bounce < 1)
            {
                const float Envelope = FMath::Square(1 - Bounce);
                Position.Y -= FMath::Abs(FMath::Sin(Bounce * PI * 3)) * BodyRadius * 1.2 * Envelope;
                Position.X += FMath::Sin(Bounce * PI * 4) * BodyRadius * .22 * Envelope;
                Rotation += FMath::Sin(Bounce * PI * 4) * 12 * Envelope;
                Squash = FMath::Exp(-Bounce * 20) * .18f;
            }
            Position.X = FMath::Clamp(Position.X, double(BodyRadius + 4), Size.X - BodyRadius - 4);
            PaintBomb(E, Layer, G, Position, Rotation, Squash);
        }
        return Layer;
    }
};
}

TSharedRef<SWidget> BDAlphaResults::Stars(UBDAlphaSession* Session)
{
    return SNew(SAlphaResultsEffect).Session(Session).Rain(false);
}
TSharedRef<SWidget> BDAlphaResults::BombRain(UBDAlphaSession* Session)
{
    return SNew(SAlphaResultsEffect).Session(Session).Rain(true);
}
