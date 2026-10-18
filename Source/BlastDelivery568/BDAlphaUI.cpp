#include "BDAlphaUI.h"
#include "BDAlphaSession.h"
#include "BDGameMode.h"
#include "BDPuzzleActors.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
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

// The same simple striped crate symbol used by Unity's Alpha menus.
class SAlphaCrateSymbol : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAlphaCrateSymbol) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(46, 46); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& WidgetStyle, bool ParentEnabled) const override
    {
        const FVector2D S = Geometry.GetLocalSize() / FVector2D(46, 46);
        auto Rect = [&](FVector2D P, FVector2D Size, FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(Elements, Layer++, Geometry.ToPaintGeometry(Size * S,
                FSlateLayoutTransform(P * S)), &SoftBrush, ESlateDrawEffect::None, Color * WidgetStyle.GetColorAndOpacityTint());
        };
        Rect(FVector2D::ZeroVector, FVector2D(46, 46), Amber);
        Rect(FVector2D(8, 0), FVector2D(5, 46), Cream);
        Rect(FVector2D(33, 0), FVector2D(5, 46), Cream);
        return Layer;
    }
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

    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (auto* S = Session.Get())
        {
            const auto Screen = S->GetScreen();
            if (Event.GetKey() == EKeys::Gamepad_Special_Right)
            {
                if (Event.IsRepeat()) return FReply::Handled();
                if (Screen == EBDAlphaScreen::Playing || Screen == EBDAlphaScreen::Pause) S->TogglePause();
                return FReply::Handled();
            }
            if (Event.GetKey() == EKeys::Gamepad_FaceButton_Right && Screen != EBDAlphaScreen::Playing)
            {
                if (Event.IsRepeat()) return FReply::Handled();
                S->Back();
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
            | (uint32(S->bTuningVisible) << 7) | (uint32(S->GetFeature(TEXT("fastFuse"))) << 8)
            | (uint32(S->GetFeature(TEXT("goalAssist"))) << 9) | (uint32(S->IsPlayground()) << 10);
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
                    .ColorAndOpacity(Cream).Justification(ETextJustify::Center), 1000, 72)];
    }

    TSharedRef<SWidget> MainMenu()
    {
        auto PageBody = Page(TEXT("BLAST DELIVERY"));
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 302, 0, 0)
            [Sized(SNew(SAlphaCrateSymbol), 46, 46)];
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(0, FMath::Max(395.f, float(DesignSize.Y / 2 - 35)), 0, 0)
            [SNew(SBox).WidthOverride(340)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Start"), &UBDAlphaSession::ShowLevelSelect, 62, Mint)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)[Button(TEXT("Settings"), &UBDAlphaSession::OpenSettings)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)[Button(TEXT("Exit"), &UBDAlphaSession::Quit)]]];
        return PageBody;
    }

    TSharedRef<SWidget> LevelCard(bool Playground)
    {
        const auto Weak = Session;
        auto Card = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top)
                [SNew(SBox).HeightOverride(7)
                    [SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Playground ? Mint : Amber)]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 17, 0, 0)
                [Label(Playground ? TEXT("02") : TEXT("01"), 41, Ink, true)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 90, 0, 0)
                [Sized(SNew(SAlphaCrateSymbol), 46, 46)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 157, 0, 0)
                [Label(Playground ? TEXT("PLAYGROUND") : TEXT("TUTORIAL"), 13, Ink, true)];
        auto B = SNew(SAlphaButton).Height(238.f).OnClicked_Lambda([Weak, Playground]()
        {
            if (auto* S = Weak.Get()) { if (Playground) S->StartPlayground(); else S->StartTutorial(); }
            return FReply::Handled();
        })[Sized(Card, 326, 234)];
        if (!FirstFocus.IsValid()) FirstFocus = B->FocusTarget();
        return SNew(SBox).WidthOverride(350)[B];
    }

    TSharedRef<SWidget> Levels()
    {
        auto PageBody = Page(TEXT("CHOOSE A LEVEL"));
        const float Top = FMath::Max(330.f, float(DesignSize.Y / 2 - 100));
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, Top, 0, 0)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 34, 0)[LevelCard(false)]
                + SHorizontalBox::Slot().AutoWidth()[LevelCard(true)]];
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, Top + 278, 0, 0)
            [Sized(Button(TEXT("Back"), &UBDAlphaSession::ShowMainMenu, 54), 220, 54)];
        return PageBody;
    }

    TSharedRef<SWidget> Feature(FName Id, const TCHAR* Text)
    {
        const auto Weak = Session;
        return SNew(SBox).HeightOverride(38)
            [SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Cream).Padding(FMargin(10, 2))
                [SNew(SCheckBox)
                    .IsChecked_Lambda([Weak, Id]() { return Weak.IsValid() && Weak->GetFeature(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([Weak, Id](ECheckBoxState State) { if (auto* S = Weak.Get()) S->SetFeature(Id, State == ECheckBoxState::Checked); })
                    .ForegroundColor(Ink).Padding(FMargin(8, 0))
                    [Label(Text, 12, Ink, true)]]];
    }

    TSharedRef<SWidget> Settings()
    {
        const auto Weak = Session;
        auto Toggle = SNew(SAlphaButton).Height(56)
            .Text_Lambda([Weak]()
            {
                if (!Weak.IsValid()) return FText::GetEmpty();
                if (Weak->IsTutorialControlsLocked()) return Txt(TEXT("Controls: SIMPLE"));
                return Txt(Weak->IsSimpleControls() ? TEXT("Controls: SIMPLE  >") : TEXT("Controls: ADVANCED  >"));
            })
            .OnClicked_Lambda([Weak]()
            {
                if (auto* S = Weak.Get()) S->SetSimpleControls(!S->IsSimpleControls());
                return FReply::Handled();
            });
        FirstFocus = Toggle->FocusTarget();
        auto Body = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().VAlign(VAlign_Top)[Toggle]
            + SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 70, 0, 0)
                [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).AutoWrapText(true)
                    .Text_Lambda([Weak]() { return Txt(Weak.IsValid() && Weak->IsSimpleControls()
                        ? TEXT("Hold either mouse button to take / pick up a bomb.\nRelease left to throw; release right to drop.")
                        : TEXT("Hold right mouse to take / hold a bomb.\nClick left to throw or pick up; release right to drop a timed bomb.")); })]
            + SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 139, 0, 0)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("Master volume"), 14)]
                    + SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted)
                        .Text_Lambda([Weak]() { return FText::FromString(FString::Printf(TEXT("%d%%"), Weak.IsValid() ? FMath::RoundToInt(Weak->GetVolume() * 100) : 0)); })]]
            + SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 171, 0, 0)
                [SNew(SBox).HeightOverride(20)
                    [SNew(SSlider).SliderBarColor(Mint).SliderHandleColor(Mint)
                        .Value_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetVolume() : 1.f; })
                        .OnValueChanged_Lambda([Weak](float Value) { if (auto* S = Weak.Get()) S->SetVolume(Value); })]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 236, 0, 0)
                [Sized(Button(TEXT("Back"), &UBDAlphaSession::Back, 54, Mint), 220, 54)];
        auto PageBody = Page(TEXT("SETTINGS"));
        PageBody->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(0, FMath::Max(320.f, float(DesignSize.Y / 2 - 110)), 0, 0)[Sized(Body, 620, 290)];
        return PageBody;
    }

    TSharedRef<SWidget> Status(bool Completed)
    {
        const auto Weak = Session;
        auto Body = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().VAlign(VAlign_Top)
                [SNew(SBox).HeightOverride(6)[SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Completed ? Mint : Amber)]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 28, 0, 0)
                [Label(Session->IsPlayground() ? TEXT("02  /  PLAYGROUND") : TEXT("01  /  TUTORIAL"), 14, Muted)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 80, 0, 0)
                [Label(Completed ? TEXT("DELIVERY COMPLETE!") : TEXT("PAUSED"), 36, Cream, true)]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(20, 158, 20, 0)
                [SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Muted).Justification(ETextJustify::Center)
                    .Text_Lambda([Weak, Completed]()
                    {
                        if (!Weak.IsValid()) return FText::GetEmpty();
                        if (Completed)
                        {
                            const auto* Mode = Weak->GetWorld() ? Weak->GetWorld()->GetAuthGameMode<ABDGameMode>() : nullptr;
                            return FText::FromString(FString::Printf(TEXT("BOMBS USED  %d"), Mode ? Mode->BombsUsed : 0));
                        }
                        return Txt(Weak->IsSimpleControls() ? TEXT("Hold either button. Release left to throw, right to drop.")
                            : TEXT("Hold right to ready a bomb. Click left to throw."));
                    })];
        if (Completed)
        {
            auto Actions = SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("Replay"), &UBDAlphaSession::Replay, 60), 220, 60)]
                + SHorizontalBox::Slot().AutoWidth().Padding(20, 0, 0, 0)
                    [Sized(Button(TEXT("Level select"), &UBDAlphaSession::ShowLevelSelect, 60), 220, 60)];
            if (!Session->IsPlayground())
                Actions->AddSlot().AutoWidth().Padding(20, 0, 0, 0)
                    [Sized(Button(TEXT("Next level  >"), &UBDAlphaSession::NextLevel, 60, Mint), 220, 60)];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 270, 0, 0)[Actions];
            if (Session->IsPlayground())
                Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 362, 0, 0)
                    [Label(TEXT("Thanks for playing. Replay to try another setup."), 14, Muted)];
        }
        else
        {
            auto Actions = SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("Resume"), &UBDAlphaSession::Resume, 58, Mint), 250, 58)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(20, 0, 0, 0)[Sized(Button(TEXT("Replay"), &UBDAlphaSession::Replay, 58), 250, 58)]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("Level select"), &UBDAlphaSession::ShowLevelSelect, 58), 250, 58)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(20, 0, 0, 0)[Sized(Button(TEXT("Settings"), &UBDAlphaSession::OpenSettings, 58), 250, 58)]];
            Body->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 230, 0, 0)[Actions];
        }
        return Sized(SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(Teal).Padding(0)
            .OnMouseButtonDown(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseButtonUp(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseDoubleClick(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))[Body], 830, 450);
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
                        if (Name == TEXT("CollisionFuseMultiplier")) return FText::FromString(FString::Printf(TEXT("%.1fx"), V));
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
        for (const auto& P : UBDAlphaSession::Parameters())
            if (P.Group == S->TuningTab && P.Id != TEXT("CollisionFuseMultiplier") && P.Id != TEXT("GoalAssistRange"))
                Rows->AddSlot().AutoHeight()[Parameter(P)];
        if (S->TuningTab == 1 || S->TuningTab == 2)
        {
            const bool IsBomb = S->TuningTab == 1;
            const FName FeatureId = IsBomb ? TEXT("fastFuse") : TEXT("goalAssist");
            const FName ParameterId = IsBomb ? TEXT("CollisionFuseMultiplier") : TEXT("GoalAssistRange");
            Rows->AddSlot().AutoHeight().Padding(2, 3, 2, 13)
                [Feature(FeatureId, IsBomb ? TEXT("Faster fuse on impact") : TEXT("Goal assist"))];
            if (S->GetFeature(FeatureId))
                for (const auto& P : UBDAlphaSession::Parameters())
                    if (P.Id == ParameterId) { Rows->AddSlot().AutoHeight()[Parameter(P)]; break; }
        }
        Rows->AddSlot().AutoHeight().Padding(0, 3, 0, 12)
            [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).AutoWrapText(true)
                .Text(Txt(S->TuningTab == 1
                    ? TEXT("Size changes live. Fuse / blast edits apply to new bombs.")
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
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(TEXT("PLAYTEST"), 14)]
                    + SHorizontalBox::Slot().AutoWidth()
                        [Sized(SNew(SAlphaButton).Text(Txt(TEXT("-"))).Height(32)
                            .OnClicked_Lambda([Weak]() { if (auto* State = Weak.Get()) State->bTuningVisible = false; return FReply::Handled(); }), 44, 32)]]
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
        const auto Weak = Session;
        FLinearColor HelpColor = Ink;
        HelpColor.A = .94f;
        return Sized(SNew(SBorder).BorderImage(&SoftBrush).BorderBackgroundColor(HelpColor).Padding(FMargin(18, 16))
            .OnMouseButtonDown(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseButtonUp(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            .OnMouseDoubleClick(FPointerEventHandler::CreateStatic(&ConsumePanelPointer))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(TEXT("WASD  Move    /    Mouse  Aim"), 14)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                    [SNew(STextBlock).Font(Font(14)).ColorAndOpacity(FLinearColor::White)
                        .Text_Lambda([Weak]() { return Txt(Weak.IsValid() && Weak->IsSimpleControls()
                            ? TEXT("Hold L / R: ready    Release L: throw    Release R: drop")
                            : TEXT("Hold R: ready    Click L: throw / pick up    Release R: drop")); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                    [Label(TEXT("ESC  Pause    /    R  Replay    /    H  Hide UI"), 11, Muted)]], 660, 122);
    }

    TSharedRef<SWidget> Playing()
    {
        const auto Weak = Session;
        auto Overlay = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
        if (!Session->IsHUDVisible()) return Overlay;
        Overlay->AddSlot()[SNew(SAlphaGoalProgress).Session(Session.Get()).Visibility(EVisibility::HitTestInvisible)];
        const bool Sidebar = Session->IsPlayground() && Session->bTuningVisible;
        auto Controls = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [Sized(SNew(SAlphaButton).Text(Txt(TEXT("?"))).Height(48)
                    .Selected_Lambda([Weak]() { return Weak.IsValid() && Weak->bShowHelp; })
                    .OnClicked_Lambda([Weak]() { if (auto* S = Weak.Get()) S->bShowHelp = !S->bShowHelp; return FReply::Handled(); }), 50, 48)]
            + SHorizontalBox::Slot().AutoWidth()[Sized(Button(TEXT("II"), &UBDAlphaSession::TogglePause, 48), 50, 48)];
        Overlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 18, Sidebar ? 368 : 18, 0)[Controls];
        if (Sidebar)
            Overlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Fill)[TuningPanel()];
        else if (Session->IsPlayground())
            Overlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 80, 16, 0)
                [Sized(SNew(SAlphaButton).Text(Txt(TEXT("Tuning"))).Height(42).FillColor(Mint)
                    .OnClicked_Lambda([Weak]() { if (auto* S = Weak.Get()) S->bTuningVisible = true; return FReply::Handled(); }), 120, 42)];
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
        if (Screen != EBDAlphaScreen::Playing)
        {
            FLinearColor Background = StatusScreen ? Ink : Teal;
            Background.A = StatusScreen ? .8f : 1.f;
            Layers->AddSlot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Background).Padding(0)];
        }
        TSharedRef<SWidget> Board = Body;
        if (StatusScreen)
            Board = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Body];
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
