#include "BDAlphaUI.h"
#include "BDAlphaSession.h"
#include "BDAlphaResults.h"
#include "BDAirport.h"
#include "BDGameMode.h"
#include "BDPuzzleActors.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// Unity's IMGUI palette is authored in display RGB. Slate expects linear colors.
const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(6, 13, 18));
const FLinearColor Teal = FLinearColor::FromSRGBColor(FColor(14, 59, 69));
const FLinearColor Cream = FLinearColor::FromSRGBColor(FColor(255, 227, 168));
const FLinearColor Mint = FLinearColor::FromSRGBColor(FColor(64, 230, 184));
const FLinearColor Amber = FLinearColor::FromSRGBColor(FColor(255, 153, 41));
const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(204, 230, 235));
const FSlateRoundedBoxBrush SoftBrush(FLinearColor::White, 1.f);

FText Txt(const TCHAR* Value) { return FText::FromString(Value); }
FReply ConsumePanelPointer(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FSlateFontInfo Font(int32 Size, bool Bold = false)
{
    return FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular", Size);
}

TSharedRef<STextBlock> Label(const FString& Value, int32 Size = 14, FLinearColor Color = FLinearColor::White, bool Bold = false)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(Font(Size, Bold)).ColorAndOpacity(Color);
}

// Keep layout fixed while color and render scale respond to real Slate hover/press/focus.
// Slate delta time is independent of the paused game world.
class SAlphaButton : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaButton) : _Text(), _Selected(false), _Height(62.f), _FontSize(15), _FillColor(Cream) {}
        SLATE_ATTRIBUTE(FText, Text)
        SLATE_ATTRIBUTE(bool, Selected)
        SLATE_ARGUMENT(float, Height)
        SLATE_ARGUMENT(int32, FontSize)
        SLATE_ARGUMENT(FLinearColor, FillColor)
        SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Selected = Args._Selected;
        FillColor = Color = Args._FillColor;
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 1.f));
        Style.SetHovered(Style.Normal);
        Style.SetPressed(Style.Normal);
        Style.SetDisabled(Style.Normal);
        // SButton adds style padding to ContentPadding. Keep the style neutral so
        // compact 35–44 px buttons retain enough space for the font's full line height.
        Style.SetNormalPadding(FMargin(0));
        Style.SetPressedPadding(FMargin(0));
        TSharedRef<SWidget> Content = Args._Content.Widget;
        if (Content == SNullWidget::NullWidget)
            Content = SNew(STextBlock).Text(Args._Text).Font(Font(Args._FontSize, true))
                .Justification(ETextJustify::Center).ColorAndOpacity(Ink);
        ChildSlot
        [
            SNew(SBox).HeightOverride(Args._Height)
            [
                SAssignNew(Button, SButton).ButtonStyle(&Style).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    .ContentPadding(FMargin(12, 2)).IsFocusable(true).OnClicked(Args._OnClicked)
                [ Content ]
            ]
        ];
        SetRenderTransformPivot(FVector2D(.5, .5));
        SetRenderTransform(FSlateRenderTransform(FScale2D(1.f)));
        Button->SetBorderBackgroundColor(FillColor);
    }

    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        // Mouse-click/default focus must not leave a button looking hovered
        // after the pointer leaves. Keyboard and gamepad navigation still get
        // the same visible feedback as hovering.
        const TOptional<EFocusCause> Focus = Button->HasAnyUserFocus();
        const bool Hover = Button->IsEnabled() && (Button->IsHovered()
            || (Focus.IsSet() && Focus.GetValue() == EFocusCause::Navigation));
        const bool Press = Button->IsEnabled() && Button->IsPressed();
        const float Target = Press ? .96f : Hover ? 1.05f : 1.f;
        Scale = FMath::FInterpTo(Scale, Target, Delta, 19.f);
        const FLinearColor Base = Selected.Get() ? Mint : FillColor;
        const FLinearColor HoverColor = Base.Equals(Mint, .001f)
            ? FMath::Lerp(Mint, FLinearColor::White, .25f)
            : FMath::Lerp(Base, Mint, .65f);
        const FLinearColor TargetColor = Press ? FMath::Lerp(HoverColor, Teal, .2f)
            : Hover ? HoverColor : Base;
        Color = FMath::Lerp(Color, TargetColor, 1.f - FMath::Exp(-16.f * Delta));
        Color.A = 1.f;
        Button->SetBorderBackgroundColor(Color);
        // A FVector2D constructor translates; FScale2D actually scales around
        // the centered pivot without changing layout or moving nearby controls.
        SetRenderTransform(FSlateRenderTransform(FScale2D(Scale)));
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& WidgetStyle, bool ParentEnabled) const override
    {
        Layer = SCompoundWidget::OnPaint(Args, Geometry, Culling, Elements, Layer, WidgetStyle, ParentEnabled);
        // Selection is persistent and immediate; hover/focus animation must not be
        // mistaken for the currently active control mode or tuning tab.
        if (Selected.Get())
        {
            const FVector2D Size = Geometry.GetLocalSize();
            const TArray<FVector2D> Underline = {
                FVector2D(12.f, Size.Y - 4.f), FVector2D(Size.X - 12.f, Size.Y - 4.f) };
            FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(),
                Underline, ESlateDrawEffect::None, Teal * WidgetStyle.GetColorAndOpacityTint(), true, 2.f);
        }
        return Layer;
    }

    TSharedPtr<SWidget> FocusTarget() const { return Button; }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
private:
    FButtonStyle Style;
    TSharedPtr<SButton> Button;
    TAttribute<bool> Selected;
    float Scale = 1.f;
    FLinearColor FillColor = Cream;
    FLinearColor Color = Cream;
};

enum class EAlphaSymbol : uint8 { Bomb, Controls, CrateGoal, Mirror, Airport, Jungle, Camera, Smoke, Scanner, Shuttle };

// Native vector strokes keep icons legible at every window scale and do not rely
// on an installed symbol font. Each level has its own recognizable silhouette.
struct FAlphaIconPaint
{
    FSlateWindowElementList& Elements;
    int32& Layer;
    const FGeometry& Geometry;
    FVector2D Scale;
    FLinearColor Tint;
    void Path(std::initializer_list<FVector2D> Points, FLinearColor Color, float Width = 4.f) const
    {
        TArray<FVector2D> P;
        for (const auto& V : Points) P.Add(V * Scale);
        FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), P,
            ESlateDrawEffect::None, Color * Tint, true, Width * FMath::Min(Scale.X, Scale.Y));
    }
    void Ring(FVector2D Center, float Radius, FLinearColor Color, float Width = 4.f) const
    {
        TArray<FVector2D> P;
        for (int32 I = 0; I <= 32; ++I)
        {
            const float A = 2 * PI * I / 32;
            P.Add((Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius) * Scale);
        }
        FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), P,
            ESlateDrawEffect::None, Color * Tint, true, Width * FMath::Min(Scale.X, Scale.Y));
    }
    void Star(FVector2D Center, float Radius, bool Filled, FLinearColor Color) const
    {
        TArray<FVector2D> P;
        for (int32 I = 0; I <= 10; ++I)
        {
            const float A = -PI / 2 + (I % 10) * PI / 5;
            P.Add((Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius * (I % 2 ? .45f : 1.f)) * Scale);
        }
        if (Filled)
        {
            TArray<FSlateVertex> V;
            TArray<SlateIndex> Indices;
            V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
                FVector2f(Center * Scale), FVector2f(.5f, .5f), (Color * Tint).ToFColor(true)));
            for (const auto& Point : P)
                V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
                    FVector2f(Point), FVector2f(.5f, .5f), (Color * Tint).ToFColor(true)));
            for (int32 I = 1; I <= 10; ++I) { Indices.Add(0); Indices.Add(I); Indices.Add(I + 1); }
            const auto White = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
            FSlateDrawElement::MakeCustomVerts(Elements, ++Layer, White, V, Indices, nullptr, 0, 0);
        }
        else FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), P,
            ESlateDrawEffect::None, Color * Tint, true, 2.f * FMath::Min(Scale.X, Scale.Y));
    }
    void Symbol(EAlphaSymbol Symbol, FLinearColor Main, FLinearColor Accent) const
    {
        switch (Symbol)
        {
        case EAlphaSymbol::Bomb:
            Ring({46, 57}, 27, Main, 5); Path({{39, 30}, {40, 23}, {51, 23}, {53, 30}}, Main);
            Path({{46, 23}, {49, 13}, {61, 10}, {68, 16}}, Main);
            Path({{73, 4}, {73, 13}, {82, 9}}, Accent, 3);
            Path({{29, 52}, {32, 44}, {39, 40}}, Accent, 3); break;
        case EAlphaSymbol::Controls:
            Path({{31, 76}, {23, 65}, {23, 30}, {29, 16}, {41, 11}, {56, 11}, {68, 17}, {73, 30}, {73, 65}, {65, 77}, {31, 76}}, Main);
            Path({{24, 38}, {72, 38}}, Main, 3); Path({{48, 12}, {48, 36}}, Main, 3);
            Path({{34, 22}, {34, 30}}, Accent, 5); Path({{61, 22}, {61, 30}}, Accent, 5);
            Path({{38, 57}, {48, 48}, {58, 57}}, Accent, 3); break;
        case EAlphaSymbol::CrateGoal:
            Path({{10, 28}, {46, 28}, {46, 66}, {10, 66}, {10, 28}}, Main);
            Path({{20, 29}, {20, 65}}, Accent, 3); Path({{36, 29}, {36, 65}}, Accent, 3);
            Path({{53, 46}, {69, 46}, {63, 40}}, Main, 3); Path({{69, 46}, {63, 52}}, Main, 3);
            Path({{58, 69}, {85, 69}, {91, 81}, {52, 81}, {58, 69}}, Accent, 4); break;
        case EAlphaSymbol::Mirror:
            Path({{9, 56}, {48, 56}, {48, 13}}, Accent, 5);
            Path({{32, 72}, {64, 40}}, Main, 6);
            Path({{39, 75}, {69, 45}}, Main, 2);
            Path({{43, 19}, {48, 12}, {53, 19}}, Accent, 3);
            Ring({16, 56}, 6, Main, 3); break;
        case EAlphaSymbol::Airport:
            Path({{48, 7}, {54, 16}, {54, 32}, {81, 46}, {81, 54}, {54, 44}, {54, 61}, {63, 68}, {63, 74},
                {48, 69}, {33, 74}, {33, 68}, {42, 61}, {42, 44}, {15, 54}, {15, 46}, {42, 32}, {42, 16}, {48, 7}}, Main, 3);
            Path({{12, 85}, {84, 85}}, Accent, 4);
            for (int32 I = 0; I < 5; ++I) Ring({20.f + I * 14, 85}, 3, Accent, 2); break;
        case EAlphaSymbol::Jungle:
            Path({{17, 17}, {14, 34}, {23, 55}, {43, 69}, {63, 72}, {81, 59}, {67, 64}, {51, 59}, {35, 44}, {23, 23}, {17, 17}}, Main, 4);
            Path({{20, 20}, {15, 10}}, Main, 5);
            Path({{54, 42}, {57, 22}, {76, 15}, {82, 14}, {79, 30}, {66, 40}, {54, 42}, {73, 24}}, Accent, 3); break;
        case EAlphaSymbol::Camera:
            Path({{14, 26}, {61, 26}, {69, 48}, {26, 57}, {14, 26}}, Main);
            Path({{24, 56}, {17, 74}, {8, 74}}, Main);
            Path({{70, 41}, {87, 32}, {87, 63}, {74, 54}}, Accent, 3); break;
        case EAlphaSymbol::Smoke:
            Path({{29, 71}, {22, 62}, {24, 49}, {33, 45}, {32, 34}, {41, 23}, {55, 22}, {65, 33},
                {65, 43}, {78, 48}, {80, 62}, {70, 72}, {29, 71}}, Main, 4);
            Path({{36, 57}, {46, 49}, {56, 52}}, Accent, 3); break;
        case EAlphaSymbol::Scanner:
            Path({{19, 80}, {19, 17}, {77, 17}, {77, 80}}, Main, 5);
            Path({{30, 42}, {66, 42}}, Accent, 4); Path({{30, 57}, {66, 57}}, Accent, 4);
            Path({{35, 74}, {61, 74}}, Main, 4); break;
        case EAlphaSymbol::Shuttle:
            Path({{12, 30}, {78, 30}, {85, 42}, {85, 67}, {12, 67}, {12, 30}}, Main);
            Path({{22, 40}, {22, 54}, {62, 54}, {62, 40}, {22, 40}}, Accent, 3);
            Ring({28, 74}, 7, Main, 3); Ring({70, 74}, 7, Main, 3); break;
        }
    }
};

class SAlphaSymbol : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaSymbol) : _Symbol(EAlphaSymbol::Bomb), _Dark(false) {}
        SLATE_ARGUMENT(EAlphaSymbol, Symbol)
        SLATE_ARGUMENT(bool, Dark)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Symbol = Args._Symbol; Dark = Args._Dark;
        static const TCHAR* Names[] = {TEXT("Bomb"), TEXT("Controls"), TEXT("CrateGoal"), TEXT("Mirror"), TEXT("Airport"),
            TEXT("Jungle"), TEXT("Camera"), TEXT("Smoke"), TEXT("Scanner"), TEXT("Shuttle")};
        SetTag(FName(*FString::Printf(TEXT("AlphaSymbol.%s"), Names[uint8(Symbol)])));
        SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64, 64); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& WidgetStyle, bool ParentEnabled) const override
    {
        FAlphaIconPaint{Elements, Layer, Geometry, Geometry.GetLocalSize() / FVector2D(96, 96), WidgetStyle.GetColorAndOpacityTint()}
            .Symbol(Symbol, Dark ? Ink : Cream, Dark ? Teal : Amber);
        return Layer;
    }
private:
    EAlphaSymbol Symbol;
    bool Dark = false;
};

// Match the donor menu's large contour without obscuring the playable room.
// This is decoration only; title/buttons are painted in the following overlay slot.
class SAlphaMenuBomb : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaMenuBomb) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        SetTag(TEXT("AlphaMenu.BombOutline"));
        SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& E, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const FVector2D Size = G.GetLocalSize();
        const float Radius = FMath::Min(340.f, float(Size.Y) * .38f);
        const FVector2D Origin(Size.X / 2, Size.Y / 2 + 50);
        const FLinearColor Stroke = FMath::Lerp(Teal, Muted, .18f);
        const FLinearColor Ember = Amber.CopyWithNewOpacity(.7f);
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        TArray<FVector2D> Circle;
        Circle.Reserve(181);
        for (int32 I = 0; I <= 180; ++I)
        {
            const float A = 2 * PI * I / 180;
            Circle.Add(Origin + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
        }
        FSlateDrawElement::MakeLines(E, ++Layer, G.ToPaintGeometry(), Circle,
            ESlateDrawEffect::None, Stroke * Tint, true, 3.f);

        const FVector2D Fuse = Origin + FVector2D(.67f, -.742f) * Radius;
        const FVector2D Bend = Fuse + FVector2D(22, -54);
        const FVector2D Tip = Bend + FVector2D(49, 4);
        const FVector2D Spark = Tip + FVector2D(20, -49);
        const FAlphaIconPaint P{E, Layer, G, FVector2D(1, 1), Tint};
        P.Path({Fuse, Bend, Tip, Spark}, Stroke, 3.f);
        P.Path({Spark + FVector2D(0, -12), Spark + FVector2D(0, -29)}, Ember, 3.f);
        P.Path({Spark + FVector2D(13, -4), Spark + FVector2D(29, -12)}, Ember, 3.f);
        P.Path({Spark + FVector2D(16, 11), Spark + FVector2D(31, 18)}, Ember, 3.f);
        return Layer;
    }
};

class SAlphaSavedStars : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaSavedStars) : _Session(nullptr), _Level(0) {}
        SLATE_ARGUMENT(UBDAlphaSession*, Session)
        SLATE_ARGUMENT(int32, Level)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Session = Args._Session; Level = Args._Level; SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(70, 16); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& E, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const int32 Mask = Session.IsValid() ? Session->GetSavedStarMask(Level) : 0;
        const FAlphaIconPaint P{E, Layer, G, G.GetLocalSize() / FVector2D(70, 16), Style.GetColorAndOpacityTint()};
        for (int32 I = 0; I < 3; ++I) P.Star({11.f + I * 24, 8}, 7, (Mask & (1 << I)) != 0, Teal);
        return Layer;
    }
private:
    TWeakObjectPtr<UBDAlphaSession> Session;
    int32 Level = 0;
};

class SAlphaAirportFeedback : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaAirportFeedback) : _Session(nullptr) {}
        SLATE_ARGUMENT(UBDAlphaSession*, Session)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Session = Args._Session; SetCanTick(true); SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(230, 91); }
    virtual void Tick(const FGeometry& G, double Time, float Delta) override
    { SLeafWidget::Tick(G, Time, Delta); Invalidate(EInvalidateWidgetReason::Paint); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& E, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const auto* S = Session.Get();
        const auto* Airport = S && S->GetWorld() ? S->GetWorld()->GetAuthGameMode<ABDAlphaAirportGameMode>() : nullptr;
        if (!Airport) return Layer;
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        auto Rect = [&](FVector2D At, FVector2D Size, FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(E, ++Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(At)),
                &SoftBrush, ESlateDrawEffect::None, Color * Tint);
        };
        auto Icon = [&](EAlphaSymbol Symbol, FVector2D At, float Size, FLinearColor Color)
        {
            const FGeometry Child = G.MakeChild(FVector2D(Size, Size), FSlateLayoutTransform(At));
            FAlphaIconPaint{E, Layer, Child, FVector2D(Size / 96.f), Tint}.Symbol(Symbol, Color, Color);
        };
        FLinearColor Background = Ink; Background.A = .86f;
        Rect({0, 0}, {230, 91}, Background);
        const int32 Stage = Airport->GetAirportStage();
        const EAlphaSymbol Phases[] = {EAlphaSymbol::Scanner, EAlphaSymbol::Shuttle, EAlphaSymbol::Airport};
        for (int32 I = 0; I < 3; ++I)
        {
            FLinearColor Color = I < Stage ? Mint : I == Stage ? Cream : Muted.CopyWithNewOpacity(.3f);
            if (I == 0 && Stage == 0) Color = Airport->IsXrayPowered() ? Amber : Mint;
            Icon(Phases[I], {20.f + I * 78, 5}, 34, Color);
            if (I < 2) Rect({61.f + I * 78, 21}, {22, 2}, I < Stage ? Mint : Muted.CopyWithNewOpacity(.3f));
        }
        const float Exposure = FMath::Clamp(Airport->GetCameraExposureFraction(), 0.f, 1.f);
        const float Smoke = FMath::Clamp(Airport->GetSmokeFraction(), 0.f, 1.f);
        Icon(EAlphaSymbol::Camera, {8, 52}, 27, Exposure > 0 ? Amber : Muted);
        Rect({40, 65}, {58, 5}, Teal);
        if (Exposure > 0) Rect({40, 65}, {58 * Exposure, 5}, Amber);
        Icon(EAlphaSymbol::Smoke, {122, 52}, 27, Smoke > 0 ? Mint : Muted.CopyWithNewOpacity(.4f));
        Rect({154, 65}, {62, 5}, Teal);
        if (Smoke > 0) Rect({154, 65}, {62 * Smoke, 5}, Mint);
        return Layer;
    }
private:
    TWeakObjectPtr<UBDAlphaSession> Session;
};

class SAlphaGoalProgress : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaGoalProgress) {} SLATE_ARGUMENT(UBDAlphaSession*, Session) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Session = Args._Session; SetCanTick(true); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SLeafWidget::Tick(Geometry, Time, Delta);
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const override
    {
        const auto* S = Session.Get();
        const auto* Mode = S && S->GetWorld() ? S->GetWorld()->GetAuthGameMode<ABDGameMode>() : nullptr;
        const float Progress = Mode ? Mode->GetGoalProgress() : 0.f;
        if (!Mode || !Mode->Goal || Progress <= 0.f || Progress >= 1.f) return Layer;
        auto* PC = UGameplayStatics::GetPlayerController(S, 0);
        FVector2D Screen;
        int32 W = 0, H = 0;
        if (!PC || !PC->ProjectWorldLocationToScreen(Mode->Goal->GetActorLocation(), Screen)) return Layer;
        PC->GetViewportSize(W, H);
        if (W <= 0 || H <= 0) return Layer;
        const FVector2D Center = Screen * Geometry.GetLocalSize() / FVector2D(W, H);
        FSlateDrawElement::MakeBox(Elements, ++Layer, Geometry.ToPaintGeometry(FVector2D(88, 12),
            FSlateLayoutTransform(Center - FVector2D(44, 6))), &SoftBrush, ESlateDrawEffect::None, Ink);
        FSlateDrawElement::MakeBox(Elements, ++Layer, Geometry.ToPaintGeometry(FVector2D(80 * Progress, 4),
            FSlateLayoutTransform(Center - FVector2D(40, 2))), &SoftBrush, ESlateDrawEffect::None, Mint);
        return Layer;
    }
private:
    TWeakObjectPtr<UBDAlphaSession> Session;
};

class SAlphaRoot : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaRoot) {}
        SLATE_ARGUMENT(UBDAlphaSession*, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Session = Args._Session;
        SetVisibility(EVisibility::SelfHitTestInvisible);
        ChildSlot [ SAssignNew(Content, SBox).Visibility(EVisibility::SelfHitTestInvisible) ];
        Rebuild();
    }

    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        if (!Session.IsValid()) return;
        const FVector2D View = Geometry.GetLocalSize();
        const float UIScale = FMath::Min(View.X / 1440.f, View.Y / 900.f);
        bool Resized = false;
        if (UIScale > SMALL_NUMBER)
        {
            const FVector2D NewSize = View / UIScale;
            Resized = !NewSize.Equals(DesignSize, .5f);
            if (Resized) DesignSize = NewSize;
        }
        const uint32 NewKey = StateKey();
        if (Resized || NewKey != PreviousKey) Rebuild();
        if (NeedsFocus && FirstFocus.IsValid())
        {
            NeedsFocus = false;
            FSlateApplication::Get().SetAllUserFocus(FirstFocus.Pin(), EFocusCause::SetDirectly);
        }
    }

    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        // Keep G available while a tuning slider or tab owns keyboard focus.
        // Handling it here prevents a second toggle through the controller.
        if (auto* S = Session.Get())
            if (Event.GetKey() == EKeys::G &&
                (S->GetScreen() == EBDAlphaScreen::Playing || S->GetScreen() == EBDAlphaScreen::Settings || S->GetScreen() == EBDAlphaScreen::Pause))
            {
                if (!Event.IsRepeat()) S->ToggleTuning();
                return FReply::Handled();
            }
        return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
    }

    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (auto* S = Session.Get())
        {
            const auto Screen = S->GetScreen();
            if (Event.GetKey() == EKeys::Gamepad_Special_Right)
            {
                if (Event.IsRepeat()) return FReply::Handled();
                if (S->IsTuningVisible()) { S->ToggleTuning(); return FReply::Handled(); }
                if (Screen == EBDAlphaScreen::Playing || Screen == EBDAlphaScreen::Pause) S->TogglePause();
                return FReply::Handled();
            }
            if (Event.GetKey() == EKeys::Gamepad_FaceButton_Right
                && (Screen != EBDAlphaScreen::Playing || S->IsTuningVisible()))
            {
                if (Event.IsRepeat()) return FReply::Handled();
                if (S->IsTuningVisible()) S->ToggleTuning(); else S->Back();
                return FReply::Handled();
            }
        }
        return SCompoundWidget::OnKeyDown(Geometry, Event);
    }

private:
    TWeakObjectPtr<UBDAlphaSession> Session;
    TSharedPtr<SBox> Content;
    TWeakPtr<SWidget> FirstFocus;
    uint32 PreviousKey = MAX_uint32;
    bool NeedsFocus = false;
    FVector2D DesignSize = FVector2D(1440, 900);
    float TuningScroll[3] = {0.f, 0.f, 0.f};

    uint32 StateKey() const
    {
        const auto* S = Session.Get();
        return uint32(S->GetScreen()) | (uint32(S->TuningTab) << 3)
            | (uint32(S->IsHUDVisible()) << 5) | (uint32(S->bShowHelp) << 6)
            | (uint32(S->IsTuningVisible()) << 7) | (uint32(S->GetFeature(TEXT("fastFuse"))) << 8)
            | (uint32(S->GetFeature(TEXT("goalAssist"))) << 9) | (uint32(S->GetCurrentLevelIndex()) << 10)
            | (uint32(S->GetResultsRevision()) << 13);
    }

    TSharedRef<SWidget> Button(const TCHAR* Text, void (UBDAlphaSession::*Action)(),
        float Height = 62.f, FLinearColor Fill = Cream)
    {
        const auto Weak = Session;
        auto B = SNew(SAlphaButton).Text(Txt(Text)).Height(Height).FillColor(Fill)
            .OnClicked_Lambda([Weak, Action]() { if (auto* S = Weak.Get()) (S->*Action)(); return FReply::Handled(); });
        if (!FirstFocus.IsValid()) FirstFocus = B->FocusTarget();
        return B;
    }

    TSharedRef<SBox> Sized(TSharedRef<SWidget> Child, float Width, float Height)
    {
        return SNew(SBox).WidthOverride(Width).HeightOverride(Height)[Child];
    }

    TSharedRef<SOverlay> Page(const TCHAR* Title)
    {
        return SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 166, 0, 0)
                [Sized(SNew(STextBlock).Text(Txt(Title)).Font(Font(36, true))
                    .Visibility(EVisibility::HitTestInvisible)
                    .ColorAndOpacity(Cream).Justification(ETextJustify::Center), 1000, 72)];
    }

    TSharedRef<SWidget> MainMenu()
    {
        auto PageBody = Page(TEXT("BLAST DELIVERY"));
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(0, FMath::Max(395.f, float(DesignSize.Y / 2 - 35)), 0, 0)
            [SNew(SBox).WidthOverride(340)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Play"), &UBDAlphaSession::ShowLevelSelect, 62, Mint)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)[Button(TEXT("Settings"), &UBDAlphaSession::OpenSettings)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)[Button(TEXT("Quit"), &UBDAlphaSession::Quit)]]];
        return SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot()[SNew(SAlphaMenuBomb)]
            + SOverlay::Slot()[PageBody];
    }

    TSharedRef<SWidget> LevelCard(int32 Index)
    {
        const auto Weak = Session;
        static const EAlphaSymbol Symbols[] = {EAlphaSymbol::Controls, EAlphaSymbol::CrateGoal, EAlphaSymbol::Mirror,
            EAlphaSymbol::Airport, EAlphaSymbol::Jungle};
        auto Card = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top)
                [SNew(SBox).HeightOverride(7)
                    [SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Index % 2 ? Mint : Amber)]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 17, 0, 0)
                [Label(FString::Printf(TEXT("%02d"), Index + 1), 36, Ink, true)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 67, 0, 0)
                [Sized(SNew(SAlphaSymbol).Symbol(Symbols[Index]).Dark(true), 68, 68)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 145, 0, 0)
                [Label(UBDAlphaSession::LevelTitle(Index).ToUpper(), 13, Ink, true)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 177, 0, 0)
                [Sized(SNew(SAlphaSavedStars).Session(Session.Get()).Level(Index), 70, 16)];
        auto B = SNew(SAlphaButton).Height(200.f).OnClicked_Lambda([Weak, Index]()
        {
            if (auto* S = Weak.Get()) S->StartLevel(Index);
            return FReply::Handled();
        })[Sized(Card, 256, 196)];
        if (!FirstFocus.IsValid()) FirstFocus = B->FocusTarget();
        return SNew(SBox).WidthOverride(280)[B];
    }

    TSharedRef<SWidget> Levels()
    {
        auto PageBody = Page(TEXT("CHOOSE A LEVEL"));
        const float Top = FMath::Max(280.f, float(DesignSize.Y / 2 - 170));
        for (int32 Row = 0; Row < 2; ++Row)
        {
            auto Cards = SNew(SHorizontalBox);
            for (int32 Index = Row * 3; Index < FMath::Min((Row + 1) * 3, UBDAlphaSession::LevelCount); ++Index)
                Cards->AddSlot().AutoWidth().Padding(Index % 3 ? 24 : 0, 0, 0, 0)[LevelCard(Index)];
            PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, Top + Row * 224, 0, 0)[Cards];
        }
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, Top + 462, 0, 0)
            [Sized(Button(TEXT("Back"), &UBDAlphaSession::ShowMainMenu, 54), 220, 54)];
        return PageBody;
    }

    TSharedRef<SWidget> Feature(FName Id, const TCHAR* Text)
    {
        const auto Weak = Session;
        auto Check = SNew(SCheckBox)
            .IsChecked_Lambda([Weak, Id]() { return Weak.IsValid() && Weak->GetFeature(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Weak, Id](ECheckBoxState State) { if (auto* S = Weak.Get()) S->SetFeature(Id, State == ECheckBoxState::Checked); })
            .ForegroundColor(Ink).Padding(FMargin(8, 0))
            [Label(Text, 12, Ink, true)];
        Check->SetTag(FName(*FString::Printf(TEXT("AlphaFeature.%s"), *Id.ToString())));
        return SNew(SBox).HeightOverride(38)
            [SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Cream).Padding(FMargin(10, 2))
                [Check]];
    }

    TSharedRef<SWidget> Settings()
    {
        const auto Weak = Session;
        auto Body = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center).Padding(0, 16, 0, 0)
                [Label(TEXT("Hold LMB to charge; release to throw."), 14, Muted)]
            + SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 86, 0, 0)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("Master volume"), 14)]
                    + SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted)
                        .Text_Lambda([Weak]() { return FText::FromString(FString::Printf(TEXT("%d%%"), Weak.IsValid() ? FMath::RoundToInt(Weak->GetVolume() * 100) : 0)); })]]
            + SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 118, 0, 0)
                [SNew(SBox).HeightOverride(20)
                    [SNew(SSlider).SliderBarColor(Mint).SliderHandleColor(Mint)
                        .Value_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetVolume() : 1.f; })
                        .OnValueChanged_Lambda([Weak](float Value) { if (auto* S = Weak.Get()) S->SetVolume(Value); })]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 178, 0, 0)
                [Sized(Button(TEXT("Physics tuning (G)"), &UBDAlphaSession::ToggleTuning, 54), 320, 54)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 254, 0, 0)
                [Sized(Button(TEXT("Back"), &UBDAlphaSession::Back, 54, Mint), 220, 54)];
        auto PageBody = Page(TEXT("SETTINGS"));
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(0, FMath::Max(320.f, float(DesignSize.Y / 2 - 110)), 0, 0)[Sized(Body, 620, 310)];
        return PageBody;
    }

    TSharedRef<SWidget> Status(bool Completed)
    {
        const auto Weak = Session;
        auto Body = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().VAlign(VAlign_Top)
                [SNew(SBox).HeightOverride(6)[SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Completed ? Mint : Amber)]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 26, 0, 0)
                [Label(FString::Printf(TEXT("%02d  /  %s"), Session->GetCurrentLevelIndex() + 1,
                    *UBDAlphaSession::LevelTitle(Session->GetCurrentLevelIndex()).ToUpper()), 14, Muted)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 74, 0, 0)
                [Label(Completed ? TEXT("DELIVERY COMPLETE!") : TEXT("PAUSED"), 36, Cream, true)];
        if (Completed)
        {
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 153, 0, 0)
                [Sized(BDAlphaResults::Stars(Session.Get()), 470, 158)];
            auto Metrics = SNew(SHorizontalBox);
            const FString Time = FString::Printf(TEXT("%d:%02d"),
                FMath::FloorToInt(Session->GetResultElapsedSeconds()) / 60,
                FMath::FloorToInt(Session->GetResultElapsedSeconds()) % 60);
            const TCHAR* MetricNames[] = {TEXT("DELIVERED"), TEXT("TIME"), TEXT("BOMBS USED")};
            const FString Values[] = {TEXT("1 / 1"), Time, FString::FromInt(Session->GetResultBombsUsed())};
            const int32 TargetSeconds = FMath::FloorToInt(Session->GetResultTimeTarget());
            const FString Criteria[] = {TEXT("DELIVER TO GOAL"), FString::Printf(TEXT("TARGET  %d:%02d"), TargetSeconds / 60, TargetSeconds % 60),
                FString::Printf(TEXT("TARGET  %d OR FEWER"), Session->GetResultBombTarget())};
            for (int32 I = 0; I < 3; ++I)
                Metrics->AddSlot().FillWidth(1)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Label(Values[I], 25, Cream, true)]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 7, 0, 0)[Label(MetricNames[I], 11, Muted)]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 7, 0, 0)[Label(Criteria[I], 10, Muted)]];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 321, 0, 0)[Sized(Metrics, 700, 91)];
            auto Actions = SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("Replay"), &UBDAlphaSession::Replay, 60), 220, 60)]
                + SHorizontalBox::Slot().AutoWidth().Padding(20, 0, 0, 0)
                    [Sized(Session->HasNextLevel()
                        ? Button(TEXT("Next level  >"), &UBDAlphaSession::NextLevel, 60, Mint)
                        : Button(TEXT("Level select"), &UBDAlphaSession::ShowLevelSelect, 60, Mint), 220, 60)]
                + SHorizontalBox::Slot().AutoWidth().Padding(20, 0, 0, 0)
                    [Sized(Button(TEXT("Exit"), &UBDAlphaSession::ShowLevelSelect, 60), 220, 60)];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 441, 0, 0)[Actions];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 523, 0, 0)
                [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted)
                    .Text_Lambda([Weak]() { return Txt(Weak.IsValid() && Weak->AreResultsPractice()
                        ? TEXT("PRACTICE RUN") : TEXT("")); })];
        }
        else
        {
            auto Actions = SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Resume"), &UBDAlphaSession::Resume, 60, Mint)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Button(TEXT("Restart"), &UBDAlphaSession::Replay, 60)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Button(TEXT("Settings"), &UBDAlphaSession::OpenSettings, 60)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Button(TEXT("Exit"), &UBDAlphaSession::ShowLevelSelect, 60)];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 181, 0, 0)[Sized(Actions, 340, 288)];
        }
        return Sized(SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Teal).Padding(0)
            .OnMouseButtonDown(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseButtonUp(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseDoubleClick(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))[Body],
            Completed ? 880 : 700, Completed ? 570 : 520);
    }
    TSharedRef<SWidget> Parameter(const FBDTuningParameter& P)
    {
        const auto Weak = Session;
        const FName Id = P.Id;
        const float Min = P.Min, Max = P.Max;
        return SNew(SBox).HeightOverride(54)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)[Label(P.Label, 11, Muted)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text_Lambda([Weak, Id]()
                    {
                        const float V = Weak.IsValid() ? Weak->GetParameter(Id) : 0.f;
                        const FString Name = Id.ToString();
                        if (Name.EndsWith(TEXT("Size"))) return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100)));
                        if (Name.EndsWith(TEXT("Multiplier")) || Name == TEXT("MaxChargeVisualScale"))
                            return FText::FromString(FString::Printf(TEXT("%.2fx"), V));
                        return FText::FromString(V >= 100.f ? FString::Printf(TEXT("%.0f"), V) : FString::Printf(TEXT("%.2f"), V));
                    })]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                [SNew(SBox).HeightOverride(20)
                    [SNew(SSlider).SliderBarColor(Mint).SliderHandleColor(Mint).StepSize(.01f)
                        .Value_Lambda([Weak, Id, Min, Max]() { return Weak.IsValid() ? FMath::Clamp((Weak->GetParameter(Id) - Min) / (Max - Min), 0.f, 1.f) : 0.f; })
                        .OnValueChanged_Lambda([Weak, Id, Min, Max](float V) { if (auto* S = Weak.Get()) S->SetParameter(Id, FMath::Lerp(Min, Max, V)); })]]];
    }

    TSharedRef<SWidget> TuningPanel()
    {
        const auto Weak = Session;
        const auto* S = Session.Get();
        auto Tabs = SNew(SHorizontalBox);
        const TCHAR* Names[] = {TEXT("Player"), TEXT("Bomb"), TEXT("Crate")};
        for (int32 I = 0; I < 3; ++I)
            Tabs->AddSlot().FillWidth(1).Padding(I == 0 ? 0 : 4, 0, I == 2 ? 0 : 4, 0)
                [SNew(SAlphaButton).Height(40).FontSize(15).Text(Txt(Names[I]))
                    .Selected_Lambda([Weak, I]() { return Weak.IsValid() && Weak->TuningTab == I; })
                    .OnClicked_Lambda([Weak, I]() { if (auto* State = Weak.Get()) State->TuningTab = I; return FReply::Handled(); })];
        auto Rows = SNew(SVerticalBox);
        auto Section = [&](const TCHAR* Title)
        { Rows->AddSlot().AutoHeight().Padding(0, 8, 0, 8)[Label(Title, 11, Cream, true)]; };
        auto AddFeature = [&](FName Id, const TCHAR* Title)
        { Rows->AddSlot().AutoHeight().Padding(2, 0, 2, 8)[Feature(Id, Title)]; };
        auto AddParameter = [&](FName Id)
        {
            for (const auto& P : UBDAlphaSession::Parameters())
                if (P.Id == Id) { Rows->AddSlot().AutoHeight()[Parameter(P)]; break; }
        };
        if (S->TuningTab == 1)
        {
            Section(TEXT("AIM PREVIEW"));
            AddFeature(TEXT("trajectory"), TEXT("Throw trajectory"));
            AddFeature(TEXT("landingPoint"), TEXT("Landing point"));
            AddFeature(TEXT("blastPreview"), TEXT("Blast radius preview"));
            Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).AutoWrapText(true)
                    .Text(Txt(TEXT("First impact estimate; bombs may roll.")))];
        }
        for (const auto& P : UBDAlphaSession::Parameters())
            if (P.Group == S->TuningTab && P.Id != TEXT("CollisionFuseMultiplier") && P.Id != TEXT("GoalAssistRange"))
            {
                if (S->TuningTab == 1)
                {
                    if (P.Id == TEXT("BombSize")) Section(TEXT("BOMB"));
                    else if (P.Id == TEXT("ChargeSeconds")) Section(TEXT("CHARGED THROW"));
                    else if (P.Id == TEXT("BlastRadius")) Section(TEXT("CHARGED EXPLOSION"));
                    else if (P.Id == TEXT("MaxChargeVisualScale")) Section(TEXT("APPEARANCE"));
                }
                Rows->AddSlot().AutoHeight()[Parameter(P)];
                if (S->TuningTab == 1 && P.Id == TEXT("FuseSeconds"))
                {
                    AddFeature(TEXT("fastFuse"), TEXT("Short fuse on impact"));
                    if (S->GetFeature(TEXT("fastFuse"))) AddParameter(TEXT("CollisionFuseMultiplier"));
                }
            }
        if (S->TuningTab == 2)
        {
            AddFeature(TEXT("goalAssist"), TEXT("Goal assist"));
            if (S->GetFeature(TEXT("goalAssist"))) AddParameter(TEXT("GoalAssistRange"));
        }
        Rows->AddSlot().AutoHeight().Padding(0, 3, 0, 12)
            [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).AutoWrapText(true)
                .Text(Txt(S->TuningTab == 1
                    ? TEXT("Size changes live. Charge / fuse / blast edits apply to new bombs.")
                    : TEXT("Changes apply immediately.")))];
        auto Scroll = SNew(SScrollBox).ScrollBarVisibility(EVisibility::Visible)
            .OnUserScrolled_Lambda([this, Tab = S->TuningTab](float Offset) { TuningScroll[Tab] = Offset; })
            + SScrollBox::Slot()[Rows];
        Scroll->SetScrollOffset(TuningScroll[S->TuningTab]);
        return Sized(SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Ink).Padding(FMargin(18, 12, 18, 23))
            .OnMouseButtonDown(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseButtonUp(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseDoubleClick(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(TEXT("PHYSICS TUNING [G]"), 14)]
                    + SHorizontalBox::Slot().AutoWidth()
                        [Sized(SNew(SAlphaButton).Text(Txt(TEXT("X"))).Height(32)
                            .OnClicked_Lambda([Weak]() { if (auto* State = Weak.Get()) State->ToggleTuning(); return FReply::Handled(); }), 44, 32)]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 22)[Tabs]
                + SVerticalBox::Slot().FillHeight(1)[Scroll]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 7)
                    [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Amber).AutoWrapText(true)
                        .Text_Lambda([Weak]() { return Weak.IsValid() ? FText::FromString(Weak->TuningNotice) : FText::GetEmpty(); })
                        .Visibility_Lambda([Weak]() { return Weak.IsValid() && !Weak->TuningNotice.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 23, 0, 0)[Button(TEXT("Restore defaults"), &UBDAlphaSession::ResetTuning, 44)]],
            350, DesignSize.Y);
    }

    TSharedRef<SWidget> Help()
    {
        const bool bAirport = Session->GetCurrentLevelIndex() == 3;
        FLinearColor HelpColor = Ink;
        HelpColor.A = .94f;
        return Sized(SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(HelpColor).Padding(FMargin(18, 16))
            .OnMouseButtonDown(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseButtonUp(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseDoubleClick(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(TEXT("WASD  Move    /    Mouse  Aim"), 14)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                    [Label(TEXT("Hold LMB: pick up / charge    Release LMB: throw"), 14)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                    [Label(TEXT("ESC  Pause    /    R  Replay    /    H  Hide UI    /    G  Tuning"), 11, Muted)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, bAirport ? 14 : 0, 0, 0)
                    [SNew(STextBlock).Text(Txt(TEXT("Q  Smoke bomb    /    E  Activate engine")))
                        .Font(Font(12)).ColorAndOpacity(Mint)
                        .Visibility(bAirport ? EVisibility::HitTestInvisible : EVisibility::Collapsed)]], 660, bAirport ? 160 : 122);
    }

    TSharedRef<SWidget> Playing()
    {
        const auto Weak = Session;
        auto Overlay = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
        if (!Session->IsHUDVisible()) return Overlay;
        Overlay->AddSlot()[SNew(SAlphaGoalProgress).Session(Session.Get()).Visibility(EVisibility::HitTestInvisible)];
        if (Session->GetCurrentLevelIndex() == 3)
            Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(18, 18, 0, 0)
                [Sized(SNew(SAlphaAirportFeedback).Session(Session.Get()), 230, 91)];
        const bool Sidebar = Session->IsTuningVisible();
        auto Controls = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [Sized(SNew(SAlphaButton).Text(Txt(TEXT("?"))).Height(48)
                    .Selected_Lambda([Weak]() { return Weak.IsValid() && Weak->bShowHelp; })
                    .OnClicked_Lambda([Weak]() { if (auto* S = Weak.Get()) S->bShowHelp = !S->bShowHelp; return FReply::Handled(); }), 50, 48)]
            + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("II"), &UBDAlphaSession::TogglePause, 48), 50, 48)];
        Overlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 18, Sidebar ? 368 : 18, 0)[Controls];
        if (Session->bShowHelp)
            Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(20, 0, 0, 20)[Help()];
        return Overlay;
    }
    void Rebuild()
    {
        if (!Session.IsValid()) return;
        PreviousKey = StateKey();
        FirstFocus.Reset();
        const EBDAlphaScreen Screen = Session->GetScreen();
        TSharedRef<SWidget> Body = SNullWidget::NullWidget;
        switch (Screen)
        {
        case EBDAlphaScreen::MainMenu: Body = MainMenu(); break;
        case EBDAlphaScreen::LevelSelect: Body = Levels(); break;
        case EBDAlphaScreen::Settings: Body = Settings(); break;
        case EBDAlphaScreen::Pause: Body = Status(false); break;
        case EBDAlphaScreen::Results: Body = Status(true); break;
        case EBDAlphaScreen::Playing: Body = Playing(); break;
        }
        auto Layers = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
        const bool StatusScreen = Screen == EBDAlphaScreen::Pause || Screen == EBDAlphaScreen::Results;
        if (Screen != EBDAlphaScreen::Playing && Screen != EBDAlphaScreen::MainMenu)
        {
            FLinearColor Background = StatusScreen ? Ink : Teal;
            Background.A = StatusScreen ? .8f : 1.f;
            Layers->AddSlot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .Visibility(EVisibility::HitTestInvisible).BorderBackgroundColor(Background).Padding(0)];
        }
        auto Board = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
        if (StatusScreen && Session->IsTuningVisible())
            Board->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
                [SNew(SBox).WidthOverride(DesignSize.X - 350).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    .Visibility(EVisibility::SelfHitTestInvisible)[Body]];
        else if (StatusScreen)
            Board->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Body];
        else if (Screen == EBDAlphaScreen::Settings && Session->IsTuningVisible())
            Board->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
                [SNew(SBox).WidthOverride(DesignSize.X - 350).Visibility(EVisibility::SelfHitTestInvisible)[Body]];
        else Board->AddSlot()[Body];
        // The console remains available even when H hides the gameplay HUD.
        if (Session->IsTuningVisible())
            Board->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Fill)[TuningPanel()];
        // HitTestInvisible keeps Replay / Next / Menu clickable through the pile.
        if (Screen == EBDAlphaScreen::Results)
            Board->AddSlot()[BDAlphaResults::BombRain(Session.Get())];
        Layers->AddSlot()
        [ SNew(SScaleBox).Visibility(EVisibility::SelfHitTestInvisible).Stretch(EStretch::ScaleToFit)
            [SNew(SBox).Visibility(EVisibility::SelfHitTestInvisible).WidthOverride(DesignSize.X).HeightOverride(DesignSize.Y)[Board]] ];
        Content->SetContent(Layers);
        // Rebuild runs in Tick, after the normal prepass. Measure the new subtree
        // now so its first paint never arranges zero-size labels on top of itself.
        Content->SlatePrepass(GetPrepassLayoutScaleMultiplier());
        NeedsFocus = Screen != EBDAlphaScreen::Playing;
    }
};
}

TSharedRef<SWidget> BDAlphaUI::Create(UBDAlphaSession* Session)
{
    return SNew(SAlphaRoot).Session(Session);
}
