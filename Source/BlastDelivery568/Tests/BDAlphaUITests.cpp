#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "../BDAlphaSession.h"
#include "../BDAlphaMenu.h"
#include "../BDGameMode.h"
#include "../BDTutorialGameMode.h"
#include "../BDPlayerController.h"
#include "../BDBomb.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Slate/SceneViewport.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace BDAlphaUITests
{
bool HasText(const TSharedRef<SWidget>& Widget, const FString& Text)
{
    if (!Widget->GetVisibility().IsVisible()) return false;
    if (Widget->GetType() == FName(TEXT("STextBlock"))
        && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Text) return true;
    FChildren* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (HasText(Children->GetChildAt(I), Text)) return true;
    return false;
}

TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& Widget, FName Type, const FString& Text = FString())
{
    if (!Widget->GetVisibility().IsVisible()) return nullptr;
    if (Widget->GetType() == Type && (Text.IsEmpty() || HasText(Widget, Text))) return Widget;
    FChildren* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Match = Find(Children->GetChildAt(I), Type, Text)) return Match;
    return nullptr;
}

TSharedPtr<SWidget> FindTag(const TSharedRef<SWidget>& Widget, FName Tag)
{
    if (!Widget->GetVisibility().IsVisible()) return nullptr;
    if (Widget->GetTag() == Tag) return Widget;
    FChildren* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Match = FindTag(Children->GetChildAt(I), Tag)) return Match;
    return nullptr;
}

struct FFixture
{
    FAutomationTestBase* Test = nullptr;
    UWorld* World = nullptr;
    UBDAlphaSession* Session = nullptr;
    ABDGameMode* Mode = nullptr;
    ABDPlayerController* PC = nullptr;
    double StepStart = 0;
    uint64 StepFrame = 0;
    bool bFailed = false;
    float SavedValue = 0.f;
    int32 SavedBombs = 0;
    TWeakPtr<SWidget> ResultStars;
    int32 ResultRevision = 0;
    int32 EarlyThirdStarPixels = 0;
    TArray<FColor> EarlyResultFrame;
    FIntVector EarlyResultSize = FIntVector::ZeroValue;
    TWeakObjectPtr<UWorld> ExitWorld;
    TWeakObjectPtr<ABDGameMode> ExitMode;
    float PausedWorldTime = 0.f;
    float PausedRunTime = 0.f;
    bool PendingGCheck = false;
    uint64 GInputFrame = 0;
    EBDAlphaScreen GExpectedScreen = EBDAlphaScreen::MainMenu;
    bool GExpectedVisible = false, GExpectedPaused = false;
    int32 GExpectedBombs = 0;
    FString GFocusType;

    void Refresh()
    {
        World = GEditor->PlayWorld;
        Session = World ? World->GetGameInstance<UBDAlphaSession>() : nullptr;
        Mode = World ? World->GetAuthGameMode<ABDGameMode>() : nullptr;
        PC = World ? Cast<ABDPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
    }
    bool After(double Seconds = .4) const
    {
        // Shader compilation and editor pumps can tick automation repeatedly in a
        // single engine frame. Wall time alone does not allow Slate to lay out UI.
        return GFrameCounter >= StepFrame + 3 && FPlatformTime::Seconds() - StepStart >= Seconds;
    }
    TSharedPtr<SWidget> Root() const
    {
        auto Window = World && World->GetGameViewport() ? World->GetGameViewport()->GetWindow() : nullptr;
        return Window.IsValid() ? Find(Window.ToSharedRef(), TEXT("SAlphaRoot")) : nullptr;
    }
    TSharedPtr<SWidget> Widget(FName Type, const TCHAR* Text = TEXT("")) const
    {
        const auto R = Root();
        return R.IsValid() ? Find(R.ToSharedRef(), Type, Text) : nullptr;
    }
    bool Check(bool Condition, const TCHAR* Message)
    {
        if (!Test->TestTrue(Message, Condition)) bFailed = true;
        return Condition;
    }
    void CheckLevelSymbols()
    {
        const TCHAR* Names[] = {TEXT("Controls"), TEXT("CrateGoal"), TEXT("Mirror"), TEXT("Airport"), TEXT("Jungle")};
        TArray<TArray<FColor>> Images;
        const auto R = Root();
        if (!Check(R.IsValid(), TEXT("Level symbols have a visible UI root"))) return;
        for (int32 I = 0; I < UBDAlphaSession::LevelCount; ++I)
        {
            const FName Tag(*FString::Printf(TEXT("AlphaSymbol.%s"), Names[I]));
            const auto Icon = FindTag(R.ToSharedRef(), Tag);
            if (!Check(Icon.IsValid(), TEXT("Every level has its own symbolic illustration"))) continue;
            TArray<FColor> Pixels;
            FIntVector Size = FIntVector::ZeroValue;
            if (Check(FSlateApplication::Get().TakeScreenshot(Icon.ToSharedRef(), Pixels, Size), TEXT("Level symbol renders to pixels")))
            {
                // Catch the actual regression: different labels/tags must not all
                // paint the same crate. Compare equal-sized icon crops, not text.
                for (const auto& Previous : Images)
                {
                    int32 Changed = 0;
                    if (Pixels.Num() == Previous.Num())
                        for (int32 P = 0; P < Pixels.Num(); ++P)
                            if (FMath::Abs(int32(Pixels[P].R) - int32(Previous[P].R))
                                + FMath::Abs(int32(Pixels[P].G) - int32(Previous[P].G))
                                + FMath::Abs(int32(Pixels[P].B) - int32(Previous[P].B)) > 70) ++Changed;
                    Check(Pixels.Num() != Previous.Num() || Changed > 40, TEXT("Level illustrations have distinct rendered silhouettes"));
                }
                Images.Add(MoveTemp(Pixels));
            }
            const auto Card = Widget(TEXT("SButton"), *UBDAlphaSession::LevelTitle(I).ToUpper());
            Check(Card.IsValid() && Find(Card.ToSharedRef(), TEXT("SAlphaSavedStars")).IsValid(),
                TEXT("Every card displays its saved three-star record"));
        }
    }

    // Invoke the native control handler, avoiding global cursor capture routing
    // in -RenderOffscreen (which has no active OS cursor window). Sliders still
    // run PositionToValue/CommitValue; borders must explicitly consume both edges.
    bool Pointer(const TSharedPtr<SWidget>& Target, FKey Key, FVector2D Fraction = FVector2D(.5, .5))
    {
        if (!Check(Target.IsValid(), TEXT("Requested visible Slate control exists"))) return false;
        FWidgetPath Path;
        auto& App = FSlateApplication::Get();
        if (!Check(App.GeneratePathToWidgetUnchecked(Target.ToSharedRef(), Path), TEXT("Control has a live Slate path"))) return false;
        const FGeometry Geometry = Target->GetCachedGeometry();
        const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * Fraction);
        TSet<FKey> Pressed;
        Pressed.Add(Key);
        const FPointerEvent Down(0, Point, Point, Pressed, Key, 0.f, FModifierKeysState());
        Target->OnMouseEnter(Geometry, Down);
        const FReply DownReply = Target->OnMouseButtonDown(Geometry, Down);
        Pressed.Reset();
        const FPointerEvent Up(0, Point, Point, Pressed, Key, 0.f, FModifierKeysState());
        const FReply UpReply = Target->OnMouseButtonUp(Geometry, Up);
        Target->OnMouseLeave(Up);
        const bool Slider = Target->GetType() == FName(TEXT("SSlider"));
        return Check(DownReply.IsEventHandled() && (Slider || UpReply.IsEventHandled()), TEXT("Native control handles the pointer interaction"));
    }
    bool Accept(const TSharedPtr<SWidget>& Target)
    {
        if (!Check(Target.IsValid(), TEXT("Requested visible native control exists"))) return false;
        const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
        const FGeometry Geometry = Target->GetCachedGeometry();
        // Native keyboard accept uses exactly the normal OnClicked / checked
        // state delegates, without changing the control's configured click method.
        const FReply Down = Target->OnKeyDown(Geometry, Enter);
        const FReply Up = Target->OnKeyUp(Geometry, Enter);
        return Check(Down.IsEventHandled() && Up.IsEventHandled(), TEXT("Native control accepts down/up activation"));
    }
    FString StateDescription() const
    {
        return FString::Printf(TEXT("screen=%d tuning=%d rawTuning=%d paused=%d level=%d completed=%d bombs=%d"),
            Session ? int32(Session->GetScreen()) : -1, Session && Session->IsTuningVisible(),
            Session && Session->bTuningVisible, PC && PC->IsPaused(), Session ? Session->GetCurrentLevelIndex() : -1,
            Mode && Mode->bCompleted, Mode ? Mode->BombsUsed : -1);
    }
    bool Click(const TCHAR* Text)
    {
        const auto Target = Widget(TEXT("SButton"), Text);
        if (!Target.IsValid()) Test->AddInfo(FString::Printf(TEXT("Missing button '%s': %s"), Text, *StateDescription()));
        return Accept(Target);
    }
    bool Checkbox(const TCHAR* Text) { return Accept(Widget(TEXT("SCheckBox"), Text)); }
    void Key(FKey Key, EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.f : 1.f));
        PC->PlayerTick(PC->IsPaused() ? 0.f : 1.f / 120.f);
    }
    void FocusedG(const TSharedPtr<SWidget>& Target)
    {
        if (!Check(Target.IsValid(), TEXT("G shortcut target is a visible native control"))) return;
        auto& App = FSlateApplication::Get();
        App.SetUserFocus(0, Target.ToSharedRef(), EFocusCause::SetDirectly);
        if (!Check(App.GetUserFocusedWidget(0) == Target, TEXT("Keyboard focus is on the requested slider or button"))) return;
        const bool WasVisible = Session->IsTuningVisible(), WasPaused = PC->IsPaused();
        const EBDAlphaScreen Screen = Session->GetScreen();
        const int32 Bombs = Mode->BombsUsed;
        const FKeyEvent Press(EKeys::G, FModifierKeysState(), 0, false, 0, 0);
        // Route through Slate's real preview/bubble path, including focused child
        // controls. Calling Session::ToggleTuning or PC directly would miss the bug.
        Check(App.ProcessKeyDownEvent(Press), TEXT("Focused G press is consumed by the interface"));
        Check(Session->IsTuningVisible() != WasVisible, TEXT("One G press toggles tuning exactly once"));
        const FKeyEvent Repeat(EKeys::G, FModifierKeysState(), 0, true, 0, 0);
        App.ProcessKeyDownEvent(Repeat);
        Check(Session->IsTuningVisible() != WasVisible, TEXT("Holding G does not repeatedly toggle tuning"));
        App.ProcessKeyUpEvent(Press);
        Check(Session->GetScreen() == Screen && PC->IsPaused() == WasPaused,
            TEXT("G keeps the current page and its paused or live state"));
        Check(Mode->BombsUsed == Bombs, TEXT("G shortcut does not create a gameplay bomb"));
        PendingGCheck = true;
        GInputFrame = GFrameCounter;
        GExpectedScreen = Screen; GExpectedVisible = !WasVisible;
        GExpectedPaused = WasPaused; GExpectedBombs = Bombs;
        GFocusType = Target->GetType().ToString();
        UE_LOG(LogTemp, Display, TEXT("BD G immediate focus=%s frame=%llu %s"),
            *GFocusType, GInputFrame, *StateDescription());
    }
    bool CheckGAfterInputFrames()
    {
        if (!PendingGCheck) return true;
        if (GFrameCounter < GInputFrame + 3) return false;
        PendingGCheck = false;
        Test->AddInfo(FString::Printf(TEXT("G after three input frames: focus=%s expected(screen=%d tuning=%d paused=%d) actual(%s)"),
            *GFocusType, int32(GExpectedScreen), GExpectedVisible, GExpectedPaused, *StateDescription()));
        UE_LOG(LogTemp, Display, TEXT("BD G deferred focus=%s frame=%llu %s"),
            *GFocusType, GFrameCounter, *StateDescription());
        Check(Session->GetScreen() == GExpectedScreen && Session->IsTuningVisible() == GExpectedVisible,
            TEXT("G state survives subsequent player-input and Slate ticks without a deferred second toggle"));
        Check(PC->IsPaused() == GExpectedPaused && Mode->BombsUsed == GExpectedBombs,
            TEXT("Deferred G routing preserves pause and creates no gameplay bomb"));
        return true;
    }
    void Resize(uint32 Width, uint32 Height)
    {
        if (World->GetGameViewport() && World->GetGameViewport()->GetGameViewport())
            World->GetGameViewport()->GetGameViewport()->SetFixedViewportSize(Width, Height);
    }
    void Bounds() const
    {
        const auto R = Root();
        const auto View = World->GetGameViewport()->GetGameViewportWidget();
        if (!R || !View) { Test->AddError(TEXT("Missing viewport/widget for layout validation")); return; }
        const FGeometry ViewGeometry = View->GetCachedGeometry();
        const FVector2D Size = ViewGeometry.GetLocalSize();
        int32 Count = 0;
        TFunction<void(TSharedRef<SWidget>)> Visit = [&](TSharedRef<SWidget> W)
        {
            if (!W->GetVisibility().IsVisible()) return;
            if (W->GetType() == FName(TEXT("SButton")) || W->GetType() == FName(TEXT("SCheckBox")))
            {
                const FGeometry G = W->GetCachedGeometry();
                const FVector2D Min = ViewGeometry.AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));
                const FVector2D Max = ViewGeometry.AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));
                Test->TestTrue(TEXT("Control remains inside viewport"), Min.X >= -3 && Min.Y >= -3 && Max.X <= Size.X + 3 && Max.Y <= Size.Y + 3);
                ++Count;
            }
            FChildren* C = W->GetChildren();
            for (int32 I = 0; I < C->Num(); ++I) Visit(C->GetChildAt(I));
        };
        Visit(R.ToSharedRef());
        Test->AddInfo(FString::Printf(TEXT("Layout: %d controls within %.0fx%.0f Slate viewport"), Count, Size.X, Size.Y));
    }
    void Shot(const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("BDAlphaScreenshots"))) return;
        const auto View = World->GetGameViewport()->GetGameViewportWidget();
        if (!Check(View.IsValid(), TEXT("Screenshot has game viewport widget"))) return;
        TArray<FColor> Pixels;
        FIntVector Size(0, 0, 0);
        // TakeScreenshot redraws the WHOLE containing Slate window and crops the
        // widget rectangle. This includes viewport-overlay siblings and native UI.
        if (!Check(FSlateApplication::Get().TakeScreenshot(View.ToSharedRef(), Pixels, Size), TEXT("Composite Slate screenshot captured"))) return;
        for (FColor& Pixel : Pixels) Pixel.A = 255;
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/AlphaShots");
        IFileManager::Get().MakeDirectory(*Folder, true);
        Check(FFileHelper::SaveArrayToFile(PNG, *(Folder / Name)), TEXT("Screenshot saved"));
        Test->AddInfo(FString::Printf(TEXT("Screenshot %s (%dx%d)"), Name, Size.X, Size.Y));
    }
    bool ResultPixels(TArray<FColor>& Pixels, FIntVector& Size, int32& ThirdStarPixels)
    {
        const auto View = World->GetGameViewport()->GetGameViewportWidget();
        const auto Stars = Widget(TEXT("SAlphaResultsEffect"));
        if (!Check(View.IsValid() && Stars.IsValid(), TEXT("Result effect has a live viewport"))) return false;
        if (!Check(FSlateApplication::Get().TakeScreenshot(View.ToSharedRef(), Pixels, Size),
            TEXT("Animated results composite captured"))) return false;
        const FGeometry V = View->GetCachedGeometry(), G = Stars->GetCachedGeometry();
        const FVector2D S = G.GetLocalSize();
        const FVector2D Center(S.X * 5 / 6, S.Y * .5);
        const FVector2D Radius(S.Y * .24, S.Y * .24);
        const FVector2D Scale = FVector2D(Size.X,Size.Y) / V.GetLocalSize();
        const FVector2D Min = V.AbsoluteToLocal(G.LocalToAbsolute(Center-Radius)) * Scale;
        const FVector2D Max = V.AbsoluteToLocal(G.LocalToAbsolute(Center+Radius)) * Scale;
        ThirdStarPixels = 0;
        for (int32 Y = FMath::Max(0,FMath::CeilToInt(Min.Y)); Y < FMath::Min(Size.Y,FMath::FloorToInt(Max.Y)); ++Y)
            for (int32 X = FMath::Max(0,FMath::CeilToInt(Min.X)); X < FMath::Min(Size.X,FMath::FloorToInt(Max.X)); ++X)
            {
                const FColor C = Pixels[Y*Size.X+X];
                if (C.R>190 && C.G>135 && C.B>65) ++ThirdStarPixels;
            }
        return true;
    }
};

using FAction = TFunction<void(FFixture&)>;
using FCondition = TFunction<bool(FFixture&)>;
class FStep : public IAutomationLatentCommand
{
    TSharedRef<FFixture> F;
    FString Name;
    FAction Begin;
    FCondition Until;
    double Created = 0;
    bool Entered = false;
public:
    FStep(TSharedRef<FFixture> InF, FString InName, FAction InBegin, FCondition InUntil)
        : F(InF), Name(MoveTemp(InName)), Begin(MoveTemp(InBegin)), Until(MoveTemp(InUntil)) {}
    virtual bool Update() override
    {
        if (Created == 0) Created = FPlatformTime::Seconds();
        if (F->bFailed) return true;
        F->Refresh();
        if (F->PendingGCheck && F->World && F->Session && F->Mode && F->PC)
        {
            if (!F->CheckGAfterInputFrames()) return false;
            if (F->bFailed) return true;
        }
        if (!Entered && F->World && F->Session && F->Mode && F->PC && F->Root())
        {
            Entered = true;
            F->StepStart = FPlatformTime::Seconds();
            F->StepFrame = GFrameCounter;
            F->Test->AddInfo(Name);
            UE_LOG(LogTemp, Display, TEXT("BD ALPHA UI: %s | %s"), *Name, *F->StateDescription());
            if (Begin) Begin(*F);
        }
        if (Entered && F->World && F->Session && F->Mode && F->PC && Until(*F)) return true;
        const double Since = Entered ? F->StepStart : Created;
        if (FPlatformTime::Seconds() - Since > 25.)
        {
            F->Test->AddError(TEXT("Timed out: ") + Name);
            F->bFailed = true;
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaMenuFlowTest, "BlastDelivery.Alpha.MenuFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBDAlphaMenuFlowTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaUITests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F = MakeShared<FFixture>();
    F->Test = this;
    auto Step = [&](const TCHAR* Name, FAction Begin, FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F, Name, MoveTemp(Begin), MoveTemp(Until))); };

    Step(TEXT("Main menu is visible and keeps its interactive room running"), [](FFixture& X)
    {
        X.Resize(1280, 720);
        X.Session->ShowMainMenu();
    }, [](FFixture& X)
    {
        if (!X.After(.7)) return false;
        X.Check(!X.PC->IsPaused(), TEXT("Main menu keeps the physical room running"));
        const auto BombOutline = FindTag(X.Root().ToSharedRef(), TEXT("AlphaMenu.BombOutline"));
        if (X.Check(BombOutline.IsValid(), TEXT("Main menu displays the large bomb outline")))
        {
            const FVector2D Size = BombOutline->GetCachedGeometry().GetLocalSize();
            X.Check(Size.X > 1000 && Size.Y > 700, TEXT("Bomb decoration spans the menu instead of a small icon slot"));
            X.Check(BombOutline->GetVisibility() == EVisibility::HitTestInvisible,
                TEXT("Bomb outline cannot intercept the interactive room or menu buttons"));
        }
        X.Check(!FindTag(X.Root().ToSharedRef(), TEXT("AlphaSymbol.Bomb")).IsValid(),
            TEXT("The previous small bomb emblem is removed"));
        X.Bounds(); X.Shot(TEXT("01-main-menu.png"));
        X.Click(TEXT("Play"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::LevelSelect, TEXT("Start button opens level selection"));
        return true;
    });
    Step(TEXT("Five level cards and Back are clickable"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(X.Widget(TEXT("SButton"), TEXT("TUTORIAL")).IsValid(), TEXT("Tutorial card exists"));
        for (int32 I = 0; I < UBDAlphaSession::LevelCount; ++I)
            X.Check(X.Widget(TEXT("SButton"), *UBDAlphaSession::LevelTitle(I).ToUpper()).IsValid(), TEXT("Every catalog card exists"));
        X.CheckLevelSymbols();
        X.Bounds(); X.Shot(TEXT("02-level-select.png")); X.Click(TEXT("Back"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::MainMenu, TEXT("Back returns to main menu"));
        return true;
    });
    Step(TEXT("Open settings using its button"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("Settings"));
        return X.Check(X.Session->GetScreen() == EBDAlphaScreen::Settings, TEXT("Settings screen entered"));
    });
    Step(TEXT("Settings controls update session values via Slate"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(!X.Widget(TEXT("SButton"), TEXT("Controls: SIMPLE  >"))
            && !X.Widget(TEXT("SButton"), TEXT("Controls: ADVANCED  >")), TEXT("Obsolete control mode selector is absent"));
        X.Check(HasText(X.Root().ToSharedRef(), TEXT("Hold LMB to charge; release to throw.")),
            TEXT("Settings explains the single charged-throw gesture"));
        X.Pointer(X.Widget(TEXT("SSlider")), EKeys::LeftMouseButton, FVector2D(.35, .5));
        X.Check(X.Session->GetVolume() > .15f && X.Session->GetVolume() < .5f, TEXT("Volume slider applies"));
        X.Check(!X.Widget(TEXT("SCheckBox"), TEXT("Tutorial jump assist")), TEXT("Settings does not add tutorial-specific gameplay options"));
        X.Bounds(); X.Shot(TEXT("03-settings.png")); X.Resize(1024, 640);
        return true;
    });
    Step(TEXT("Settings opens the shared tuning panel"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("Physics tuning (G)"));
        return X.Check(X.Session->IsTuningVisible(), TEXT("Settings tuning entry works"));
    });
    Step(TEXT("G closes Settings tuning while a tab has keyboard focus"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(X.PC->IsPaused(), TEXT("Settings keeps the world paused"));
        X.FocusedG(X.Widget(TEXT("SButton"), TEXT("Player")));
        X.Check(!X.Session->IsTuningVisible(), TEXT("Focused tab cannot swallow G close"));
        return true;
    });
    Step(TEXT("G opens Settings tuning while the volume slider has focus"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.FocusedG(X.Widget(TEXT("SSlider")));
        X.Check(X.Session->IsTuningVisible(), TEXT("Focused volume slider cannot swallow G open"));
        return true;
    });
    Step(TEXT("Settings tuning overlay fits and closes independently"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Bounds(); X.Shot(TEXT("03-settings-console.png"));
        X.Click(TEXT("X"));
        return X.Check(!X.Session->IsTuningVisible() && X.Session->GetScreen() == EBDAlphaScreen::Settings,
            TEXT("Closing the panel preserves Settings"));
    });
    Step(TEXT("Settings fit the resized viewport"), {}, [](FFixture& X)
    {
        if (!X.After(.6)) return false;
        X.Bounds(); X.Shot(TEXT("04-settings-resized.png"));
        X.Resize(1280, 720); X.Click(TEXT("Back"));
        return true;
    });
    Step(TEXT("Start then choose Tutorial"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("Play")); return true;
    });
    Step(TEXT("Tutorial card loads the real tutorial"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("TUTORIAL")); return true;
    });
    Step(TEXT("Tutorial also supports live tuning"), {}, [](FFixture& X)
    {
        if (!X.After(.7) || !Cast<ABDTutorialGameMode>(X.Mode) || !X.PC->GetPawn()) return false;
        X.Key(EKeys::G, IE_Pressed); X.Key(EKeys::G, IE_Released);
        return X.Check(X.Session->IsTuningVisible() && !X.PC->IsPaused(), TEXT("Tutorial console opens live"));
    });
    Step(TEXT("Tutorial console edits physics without changing its control tutorial"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Pointer(X.Widget(TEXT("SSlider")), EKeys::LeftMouseButton, FVector2D(.25, .5));
        X.Check(!FMath::IsNearlyEqual(X.Session->GetParameter(TEXT("PlayerSize")), 1.f), TEXT("Tutorial size tuning is editable"));
        X.Click(TEXT("Restore defaults"));
        X.Click(TEXT("X"));
        return X.Check(X.Session->GetScreen() == EBDAlphaScreen::Playing, TEXT("Closing tutorial tuning keeps gameplay active"));
    });
    Step(TEXT("Tutorial starts with a minimal HUD"), {}, [](FFixture& X)
    {
        if (!X.After(.7) || !Cast<ABDTutorialGameMode>(X.Mode) || !X.PC->GetPawn()) return false;
        X.Resize(1280, 720);
        X.Check(!X.PC->IsPaused(), TEXT("Tutorial plays"));
        X.Click(TEXT("II"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::Pause, TEXT("HUD pause button works"));
        return true;
    });
    Step(TEXT("G opens physics tuning directly from Pause"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.PausedWorldTime = X.World->GetTimeSeconds();
        X.PausedRunTime = X.Mode->ElapsedSeconds;
        X.FocusedG(X.Widget(TEXT("SButton"), TEXT("Resume")));
        X.Check(X.Session->IsTuningVisible() && X.Session->GetScreen() == EBDAlphaScreen::Pause && X.PC->IsPaused(),
            TEXT("G opens the panel on Pause without resuming"));
        return true;
    });
    Step(TEXT("G closes a focused tuning tab and remains on Pause"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Bounds(); X.Shot(TEXT("05-pause-tuning.png"));
        X.FocusedG(X.Widget(TEXT("SButton"), TEXT("Player")));
        X.Check(!X.Session->IsTuningVisible() && X.Session->GetScreen() == EBDAlphaScreen::Pause && X.PC->IsPaused(),
            TEXT("Focused tab G closes only the panel on Pause"));
        X.Check(FMath::IsNearlyEqual(X.World->GetTimeSeconds(), X.PausedWorldTime, .001f)
            && FMath::IsNearlyEqual(X.Mode->ElapsedSeconds, X.PausedRunTime, .001f),
            TEXT("Direct paused tuning leaves world and run clocks frozen"));
        return true;
    });
    Step(TEXT("Pause overlay contains the expected actions"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        for (const TCHAR* Caption : {TEXT("Resume"), TEXT("Restart"), TEXT("Settings"), TEXT("Exit")})
            X.Check(X.Widget(TEXT("SButton"), Caption).IsValid(), TEXT("Pause has each of its four requested actions"));
        X.Check(!X.Widget(TEXT("SButton"), TEXT("Main menu")).IsValid(), TEXT("Pause Exit replaces Main menu"));
        X.Bounds(); X.Shot(TEXT("05-pause.png")); X.Click(TEXT("Settings")); return true;
    });
    Step(TEXT("G opens tuning inside the paused tutorial Settings"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.PausedWorldTime = X.World->GetTimeSeconds();
        X.PausedRunTime = X.Mode->ElapsedSeconds;
        X.Check(X.PC->IsPaused(), TEXT("Tutorial remains paused in Settings"));
        X.FocusedG(X.Widget(TEXT("SSlider")));
        X.Check(X.Session->IsTuningVisible(), TEXT("G opens tuning from paused Settings"));
        return true;
    });
    Step(TEXT("G closes a focused physics slider without resuming the tutorial"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Bounds(); X.Shot(TEXT("05-paused-settings-tuning.png"));
        const auto Panel = X.Widget(TEXT("SBorder"), TEXT("PHYSICS TUNING [G]"));
        X.Check(Panel.IsValid(), TEXT("Paused Settings contains the physics tuning panel"));
        if (Panel.IsValid()) X.FocusedG(Find(Panel.ToSharedRef(), TEXT("SSlider")));
        X.Check(!X.Session->IsTuningVisible() && X.PC->IsPaused(), TEXT("Closing tuning preserves paused Settings"));
        X.Check(FMath::IsNearlyEqual(X.World->GetTimeSeconds(), X.PausedWorldTime, .001f)
            && FMath::IsNearlyEqual(X.Mode->ElapsedSeconds, X.PausedRunTime, .001f),
            TEXT("Neither world simulation nor run timer advances during paused tuning"));
        return true;
    });
    Step(TEXT("Settings Back preserves pause origin"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(!X.Widget(TEXT("SButton"), TEXT("Controls: SIMPLE")), TEXT("Tutorial settings also omit the obsolete mode selector"));
        X.Check(HasText(X.Root().ToSharedRef(), TEXT("Hold LMB to charge; release to throw.")),
            TEXT("Paused tutorial settings retain the current mouse instruction"));
        X.Click(TEXT("Back"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::Pause, TEXT("Back returns to pause")); return true;
    });
    Step(TEXT("Continue resumes without spawning a bomb"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.SavedBombs = X.Mode->BombsUsed;
        X.Click(TEXT("Resume"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::Playing && !X.PC->IsPaused(), TEXT("Continue resumes game"));
        X.Check(X.Mode->BombsUsed == X.SavedBombs, TEXT("Continue click does not become a gameplay click")); return true;
    });

    // These are visual fixtures, not a claim of completing the tutorial. The
    // existing FiveScenes.Playthrough suite separately checks actual solutions.
    for (int32 I = 0; I < 5; ++I)
        Step(TEXT("Capture a tutorial stage fixture"), [I](FFixture& X)
        {
            if (auto* Tutorial = Cast<ABDTutorialGameMode>(X.Mode))
            { Tutorial->SceneIndex = I; Tutorial->RequestSceneReset(); }
        }, [I](FFixture& X)
        {
            if (!X.After(.7)) return false;
            const FString File = FString::Printf(TEXT("06-tutorial-stage-%d.png"), I + 1);
            X.Bounds(); X.Shot(*File); return true;
        });

    Step(TEXT("Return from tutorial through Pause to level selection"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("II")); return true;
    });
    Step(TEXT("Pause Exit returns directly to level selection"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("Exit"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::LevelSelect, TEXT("Exit skips Main menu and opens level selection"));
        return true;
    });
    Step(TEXT("Playground card loads the Alpha playground"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("PLAYGROUND")); return true;
    });
    Step(TEXT("Playground console starts hidden and opens without pausing"), {}, [](FFixture& X)
    {
        if (!X.After(.8) || !X.Session->IsPlayground() || !X.Mode->Crate) return false;
        X.Check(!X.Session->IsTuningVisible(), TEXT("Console starts closed"));
        X.Key(EKeys::G, IE_Pressed); X.Key(EKeys::G, IE_Released);
        X.Check(X.Session->IsTuningVisible() && !X.PC->IsPaused(), TEXT("G opens a live console"));
        return true;
    });
    Step(TEXT("G closes live tuning with a physics slider focused"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.FocusedG(X.Widget(TEXT("SSlider")));
        X.Check(!X.Session->IsTuningVisible() && !X.PC->IsPaused(), TEXT("Live slider focus does not block G close"));
        return true;
    });
    Step(TEXT("G reopens live tuning with a HUD button focused"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.FocusedG(X.Widget(TEXT("SButton"), TEXT("?")));
        X.Check(X.Session->IsTuningVisible(), TEXT("HUD focus does not block G open"));
        return true;
    });
    Step(TEXT("G closes live tuning with a tab focused"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.FocusedG(X.Widget(TEXT("SButton"), TEXT("Bomb")));
        X.Check(!X.Session->IsTuningVisible(), TEXT("Live tab focus does not block G close"));
        return true;
    });
    Step(TEXT("G reopens tuning from the gameplay viewport"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Key(EKeys::G, IE_Pressed); X.Key(EKeys::G, IE_Released);
        X.Check(X.Session->IsTuningVisible() && !X.PC->IsPaused(), TEXT("Viewport G binding still works after Slate focus changes"));
        return true;
    });
    Step(TEXT("Playground player panel and parameter input"), {}, [](FFixture& X)
    {
        if (!X.After(.8) || !X.Session->IsPlayground() || !X.Mode->Crate) return false;
        X.Resize(1280, 720); X.SavedBombs = X.Mode->BombsUsed;
        X.Bounds(); X.Shot(TEXT("07-playground-player.png"));
        X.Pointer(X.Widget(TEXT("SSlider")), EKeys::LeftMouseButton, FVector2D(.25, .5));
        X.Check(!FMath::IsNearlyEqual(X.Session->GetParameter(TEXT("PlayerSize")), 1.f), TEXT("Player size slider changes its live value"));
        X.Check(X.Mode->BombsUsed == X.SavedBombs, TEXT("Slider interaction does not spawn a bomb"));
        X.Click(TEXT("Restore defaults"));
        X.Check(FMath::IsNearlyEqual(X.Session->GetParameter(TEXT("PlayerSize")), 1.f), TEXT("Reset button restores default value"));
        X.Click(TEXT("Bomb")); return true;
    });
    Step(TEXT("Bomb tab reveals fast fuse slider only when enabled"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        const auto R = X.Root();
        X.Check(R && !HasText(R.ToSharedRef(), TEXT("Collision fuse speed")), TEXT("Fast fuse slider initially hidden"));
        X.Checkbox(TEXT("Short fuse on impact"));
        X.Check(X.Session->GetFeature(TEXT("fastFuse")), TEXT("Fast fuse checkbox enabled")); return true;
    });
    Step(TEXT("Expanded bomb panel stays within the viewport"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        const auto R = X.Root();
        X.Check(R && HasText(R.ToSharedRef(), TEXT("Collision fuse speed")), TEXT("Fast fuse slider appears"));
        X.Bounds(); X.Shot(TEXT("08-playground-bomb.png")); X.Click(TEXT("Crate")); return true;
    });
    Step(TEXT("Crate tab exposes its goal assist range"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Checkbox(TEXT("Goal assist"));
        X.Check(X.Session->GetFeature(TEXT("goalAssist")), TEXT("Goal assist enabled")); return true;
    });
    Step(TEXT("Expanded crate panel and RMB input isolation"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        const auto R = X.Root();
        X.Check(R && HasText(R.ToSharedRef(), TEXT("Goal assist range")), TEXT("Goal range slider appears"));
        X.Bounds(); X.Shot(TEXT("09-playground-crate.png"));
        X.Pointer(X.Widget(TEXT("SBorder"), TEXT("PHYSICS TUNING [G]")), EKeys::RightMouseButton, FVector2D(.03, .5));
        X.Check(X.Mode->BombsUsed == X.SavedBombs, TEXT("RMB on panel does not draw a bomb"));
        X.Key(EKeys::H, IE_Pressed); X.Key(EKeys::H, IE_Released); return true;
    });
    Step(TEXT("H hides HUD, but not future menus"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(!X.Session->IsHUDVisible(), TEXT("HUD hidden"));
        X.Check(X.Widget(TEXT("SButton"), TEXT("Restore defaults")).IsValid(), TEXT("Console survives hidden HUD"));
        X.Shot(TEXT("10-playground-clean.png"));
        X.Key(EKeys::Escape, IE_Pressed); X.Key(EKeys::Escape, IE_Released); return true;
    });
    Step(TEXT("First Escape closes the console before pausing"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(!X.Session->IsTuningVisible() && X.Session->GetScreen() == EBDAlphaScreen::Playing,
            TEXT("Escape closes live tuning only"));
        X.Key(EKeys::Escape, IE_Pressed); X.Key(EKeys::Escape, IE_Released);
        return true;
    });
    Step(TEXT("Pause is usable while HUD is hidden"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Check(X.Widget(TEXT("SButton"), TEXT("Resume")).IsValid(), TEXT("Pause survives hidden HUD"));
        X.Click(TEXT("Resume"));
        // Result fixtures validate the menu, not the physical delivery condition.
        X.Mode->BombsUsed = 12; X.Mode->ElapsedSeconds = 83.f; X.Mode->bCompleted = true;
        return true;
    });
    Step(TEXT("Capture results charging and bomb rain at 1.2 seconds"), {}, [](FFixture& X)
    {
        if (!X.After(1.2) || X.Session->GetScreen() != EBDAlphaScreen::Results) return false;
        X.Check(X.Session->GetResultBombsUsed()==12, TEXT("Rain uses all twelve fixture bombs"));
        X.Check(X.Session->GetResultStarMask()==7, TEXT("Fixture earns all three star slots"));
        const int32 TargetSeconds=FMath::FloorToInt(X.Session->GetResultTimeTarget());
        X.Check(HasText(X.Root().ToSharedRef(), FString::Printf(TEXT("TARGET  %d:%02d"),TargetSeconds/60,TargetSeconds%60)),
            TEXT("Results explain the time-star target"));
        X.Check(HasText(X.Root().ToSharedRef(), FString::Printf(TEXT("TARGET  %d OR FEWER"),X.Session->GetResultBombTarget())),
            TEXT("Results explain the bomb-star target"));
        X.ResultStars=X.Widget(TEXT("SAlphaResultsEffect"));
        X.ResultRevision=X.Session->GetResultsRevision();
        X.ResultPixels(X.EarlyResultFrame,X.EarlyResultSize,X.EarlyThirdStarPixels);
        X.Bounds(); X.Shot(TEXT("11-results-charging-1.2s.png"));
        return true;
    });
    Step(TEXT("Capture filled stars and deposited bomb pile at four seconds"), {}, [](FFixture& X)
    {
        if (!X.After(2.8) || X.Session->GetScreen()!=EBDAlphaScreen::Results) return false;
        X.Check(X.ResultStars.Pin()==X.Widget(TEXT("SAlphaResultsEffect")),
            TEXT("Normal UI ticks retain the same animation widget"));
        X.Check(X.ResultRevision==X.Session->GetResultsRevision(), TEXT("Result revision stays stable while effects animate"));
        TArray<FColor> Late;
        FIntVector Size=FIntVector::ZeroValue;
        int32 FilledThirdStarPixels=0;
        if (X.ResultPixels(Late,Size,FilledThirdStarPixels))
        {
            X.Check(FilledThirdStarPixels>X.EarlyThirdStarPixels+30,
                TEXT("Third star visibly fills between charging and completed frames"));
            if (Size==X.EarlyResultSize && Late.Num()==X.EarlyResultFrame.Num())
            {
                int64 Difference=0, Samples=0;
                for (int32 Y=Size.Y*3/4; Y<Size.Y; Y+=2)
                    for (int32 Px=0; Px<Size.X; Px+=2)
                    {
                        const int32 I=Y*Size.X+Px;
                        const FColor A=X.EarlyResultFrame[I], B=Late[I];
                        Difference+=FMath::Abs(int32(A.R)-int32(B.R))+FMath::Abs(int32(A.G)-int32(B.G))+FMath::Abs(int32(A.B)-int32(B.B));
                        ++Samples;
                    }
                X.Check(Samples>0 && double(Difference)/Samples>3,
                    TEXT("Foreground bomb rain visibly deposits into the bottom pile"));
            }
        }
        X.Bounds(); X.Shot(TEXT("11-results-filled-4s.png"));
        X.Click(TEXT("Replay")); return true;
    });
    Step(TEXT("Replay clears completion and restores a playable session"), {}, [](FFixture& X)
    {
        if (!X.After(.8) || X.Session->GetScreen() != EBDAlphaScreen::Playing) return false;
        X.Check(X.Session->IsPlayground() && !X.Mode->bCompleted, TEXT("Replay returns to playground"));
        X.Check(X.Mode->BombsUsed == 0, TEXT("Replay resets usage"));
        X.Mode->bCompleted = true; return true;
    });
    for (int32 Index = 2; Index < UBDAlphaSession::LevelCount; ++Index)
    {
        Step(TEXT("Result Next level advances through the catalog"), {}, [](FFixture& X)
        {
            if (!X.After() || X.Session->GetScreen() != EBDAlphaScreen::Results) return false;
            X.Click(TEXT("Next level  >")); return true;
        });
        Step(TEXT("New level has a live console and can show results"), {}, [Index](FFixture& X)
        {
            if (!X.After(.8) || X.Session->GetScreen() != EBDAlphaScreen::Playing || !X.PC->GetPawn()) return false;
            X.Check(X.Session->GetCurrentLevelIndex() == Index, TEXT("Expected catalog level loaded"));
            X.Check(!X.Session->IsTuningVisible(), TEXT("New scene console starts closed"));
            X.Key(EKeys::G, IE_Pressed); X.Key(EKeys::G, IE_Released);
            X.Check(X.Session->IsTuningVisible() && !X.PC->IsPaused(), TEXT("Console works in every added scene"));
            return true;
        });
        Step(TEXT("Capture added level console"), {}, [Index](FFixture& X)
        {
            if (!X.After()) return false;
            X.Check(X.Widget(TEXT("SAlphaAirportFeedback")).IsValid() == (Index == 3),
                TEXT("Airport-only status feedback does not alter other levels"));
            X.Bounds(); X.Shot(*FString::Printf(TEXT("12-level-%d-console.png"), Index + 1));
            X.Key(EKeys::Escape, IE_Pressed); X.Key(EKeys::Escape, IE_Released);
            X.Mode->BombsUsed = 16; X.Mode->ElapsedSeconds = 102; X.Mode->bCompleted = true;
            return true;
        });
        Step(TEXT("Results stars and foreground bomb rain are present"), {}, [Index](FFixture& X)
        {
            if (!X.After(1.25) || X.Session->GetScreen() != EBDAlphaScreen::Results) return false;
            X.Check(X.Session->GetResultBombsUsed() == 16, TEXT("Result captures the real usage count"));
            X.Check(X.Widget(TEXT("SAlphaResultsEffect")).IsValid(), TEXT("Animated result effects are attached"));
            X.Check(X.Widget(TEXT("SButton"), TEXT("Replay")).IsValid(), TEXT("Replay remains present under foreground rain"));
            X.Bounds(); X.Shot(*FString::Printf(TEXT("13-level-%d-results.png"), Index + 1));
            return true;
        });
    }
    Step(TEXT("Results Exit returns to level selection rather than Main menu"), {}, [](FFixture& X)
    {
        if (!X.After() || X.Session->GetScreen() != EBDAlphaScreen::Results) return false;
        X.Check(!X.Session->HasNextLevel(), TEXT("Jungle is the fifth and final catalog entry"));
        X.Check(X.Widget(TEXT("SButton"), TEXT("Level select")).IsValid(), TEXT("Final result still offers Level select instead of Next"));
        X.Check(!X.Widget(TEXT("SButton"), TEXT("Main menu")).IsValid(), TEXT("Results Exit replaces Main menu"));
        X.ExitWorld = X.World; X.ExitMode = X.Mode;
        X.Click(TEXT("Exit"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::LevelSelect, TEXT("Result Exit immediately selects the level-selection page"));
        return true;
    });
    Step(TEXT("Results Exit does not asynchronously load the main-menu room"), {}, [](FFixture& X)
    {
        if (!X.After(.8)) return false;
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::LevelSelect && X.PC->IsPaused(),
            TEXT("Results Exit remains on level selection after subsequent frames"));
        X.Check(X.World == X.ExitWorld.Get() && X.Mode == X.ExitMode.Get() && !X.Mode->IsA<ABDAlphaMenuGameMode>(),
            TEXT("Exit preserves the stopped level and never loads the interactive main-menu world"));
        X.Check(X.Widget(TEXT("SButton"), TEXT("AIRPORT")).IsValid() && X.Widget(TEXT("SButton"), TEXT("JUNGLE")).IsValid(),
            TEXT("Results Exit presents usable level cards"));
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
namespace BDAlphaUITests
{
// Sample the rendered wrapper, not its layout box: a FVector2D passed to
// FSlateRenderTransform is translation, so testing only geometry/activation
// would miss a button that moves instead of scaling.
struct FButtonFeedback
{
    TSharedPtr<SWidget> Wrapper;
    TSharedPtr<SButton> Button;
    FSlateRect Layout;
    FLinearColor BaseColor = FLinearColor::Black;
    float BaseScale = 1.f;
    float WorldTime = 0.f;
    int32 Bombs = 0;
    EBDAlphaScreen Screen = EBDAlphaScreen::MainMenu;

    bool Bind(FFixture& X, const TCHAR* Text)
    {
        Wrapper = X.Widget(TEXT("SAlphaButton"), Text);
        const auto Native = X.Widget(TEXT("SButton"), Text);
        if (!X.Check(Wrapper.IsValid() && Native.IsValid(), TEXT("Feedback target and native button exist"))) return false;
        Button = StaticCastSharedPtr<SButton>(Native);
        FSlateApplication::Get().ClearAllUserFocus();
        Button->OnMouseLeave(Pointer(false));
        Screen = X.Session->GetScreen();
        WorldTime = X.World->GetTimeSeconds();
        Bombs = X.Mode->BombsUsed;
        return true;
    }

    FPointerEvent Pointer(bool Down, bool Outside = false) const
    {
        const FGeometry G = Button->GetCachedGeometry();
        const FVector2D Point = G.LocalToAbsolute(Outside ? FVector2D(-20, -20) : G.GetLocalSize() * .5);
        TSet<FKey> Pressed;
        if (Down) Pressed.Add(EKeys::LeftMouseButton);
        return FPointerEvent(0, Point, Point, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    }

    FVector2D Scale() const
    {
        const auto& Transform = Wrapper->GetRenderTransform();
        if (!Transform.IsSet()) return FVector2D(1, 1);
        return FVector2D(TransformVector(Transform.GetValue(), FVector2f(1, 0)).Size(),
            TransformVector(Transform.GetValue(), FVector2f(0, 1)).Size());
    }

    FLinearColor Color() const { return Button->GetBorderBackgroundColor().GetSpecifiedColor(); }

    void CheckLayoutAndWorld(FFixture& X) const
    {
        const FSlateRect Now = Wrapper->GetCachedGeometry().GetLayoutBoundingRect();
        X.Check(FMath::IsNearlyEqual(Layout.Left, Now.Left, .1f)
            && FMath::IsNearlyEqual(Layout.Top, Now.Top, .1f)
            && FMath::IsNearlyEqual(Layout.Right, Now.Right, .1f)
            && FMath::IsNearlyEqual(Layout.Bottom, Now.Bottom, .1f), TEXT("Feedback does not shift the allocated layout"));
        const FVector2D S = Scale();
        X.Check(FMath::IsNearlyEqual(S.X, S.Y, .001), TEXT("Button scales uniformly on both axes"));
        const auto& Transform = Wrapper->GetRenderTransform();
        X.Check(!Transform.IsSet() || TransformPoint(Transform.GetValue(), FVector2f::ZeroVector).IsNearlyZero(.001f),
            TEXT("Feedback uses a scale transform rather than an accidental translation"));
        if (Screen == EBDAlphaScreen::MainMenu)
            X.Check(!X.PC->IsPaused(), TEXT("Main menu interaction keeps its physics room live"));
        else
            X.Check(X.PC->IsPaused() && FMath::IsNearlyEqual(X.World->GetTimeSeconds(), WorldTime, .001f),
                TEXT("Button feedback advances while game time is paused"));
        X.Check(X.Session->GetScreen() == Screen && X.Mode->BombsUsed == Bombs && !X.Mode->ActiveBomb,
            TEXT("Hover, focus, and cancelled presses neither activate a menu action nor create a bomb"));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBDAlphaButtonFeedbackTest, "BlastDelivery.Alpha.ButtonFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBDAlphaButtonFeedbackTest::RunTest(const FString& Parameters)
{
    using namespace BDAlphaUITests;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/FiveSceneTutorial")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    auto F = MakeShared<FFixture>();
    F->Test = this;
    auto Step = [&](const TCHAR* Name, FAction Begin, FCondition Until)
    { FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStep>(F, Name, MoveTemp(Begin), MoveTemp(Until))); };

    Step(TEXT("Prepare the actual main menu for button feedback"), [](FFixture& X)
    {
        X.Resize(1280, 720);
        X.Session->ShowMainMenu();
    }, [](FFixture& X) { return X.After(.7); });

    // Each input edge is separated by normal engine/Slate frames. Never manually
    // Tick the widget: that would conceal a missing real animation update path.
    auto Exercise = [&](const TCHAR* Text, const TCHAR* Prefix)
    {
        const FString Caption(Text), FilePrefix(Prefix);
        auto B = MakeShared<FButtonFeedback>();
        Step(TEXT("Bind the visible button and clear stale hover/focus"), [B, Caption](FFixture& X)
        { B->Bind(X, *Caption); }, [](FFixture& X) { return X.After(.35); });
        Step(TEXT("Capture button baseline"), {}, [B, FilePrefix](FFixture& X)
        {
            if (!X.After(.25)) return false;
            B->Layout = B->Wrapper->GetCachedGeometry().GetLayoutBoundingRect();
            B->BaseScale = B->Scale().X;
            B->BaseColor = B->Color();
            X.Check(!B->Button->IsHovered() && !B->Button->IsPressed(), TEXT("Baseline is neither hovered nor pressed"));
            X.Check(FMath::IsNearlyEqual(B->BaseScale, 1.f, .003f), TEXT("Idle button returns to its original scale"));
            B->CheckLayoutAndWorld(X);
            X.Shot(*(FilePrefix + TEXT("-baseline.png")));
            return true;
        });
        Step(TEXT("Hover enlarges and recolors the button across real frames"), [B](FFixture& X)
        { B->Button->OnMouseEnter(B->Button->GetCachedGeometry(), B->Pointer(false)); }, [B, FilePrefix](FFixture& X)
        {
            if (!X.After(.25)) return false;
            X.Check(B->Button->IsHovered(), TEXT("Native button receives hover"));
            X.Check(B->Scale().X > B->BaseScale + .02f, TEXT("Hover visibly scales the rendered button"));
            X.Check(!B->Color().Equals(B->BaseColor, .02f), TEXT("Hover visibly changes the native button tint"));
            B->CheckLayoutAndWorld(X);
            X.Shot(*(FilePrefix + TEXT("-hover.png")));
            return true;
        });
        Step(TEXT("Navigation focus receives feedback without mouse hover"), [B](FFixture& X)
        {
            B->Button->OnMouseLeave(B->Pointer(false, true));
            FSlateApplication::Get().SetAllUserFocus(B->Button, EFocusCause::Navigation);
        }, [B](FFixture& X)
        {
            if (!X.After(.25)) return false;
            X.Check(!B->Button->IsHovered() && B->Button->HasKeyboardFocus(), TEXT("Navigation focus works independently of pointer hover"));
            X.Check(B->Scale().X > B->BaseScale + .02f && !B->Color().Equals(B->BaseColor, .02f),
                TEXT("Keyboard/gamepad navigation has visible feedback"));
            B->CheckLayoutAndWorld(X);
            return true;
        });
        Step(TEXT("A held native mouse press shrinks and darkens the button"), [B](FFixture& X)
        {
            FSlateApplication::Get().ClearAllUserFocus();
            const FGeometry G = B->Button->GetCachedGeometry();
            B->Button->OnMouseEnter(G, B->Pointer(false));
            X.Check(B->Button->OnMouseButtonDown(G, B->Pointer(true)).IsEventHandled(), TEXT("Native button consumes press"));
        }, [B, FilePrefix](FFixture& X)
        {
            if (!X.After(.25)) return false;
            X.Check(B->Button->IsPressed(), TEXT("Button remains pressed until release"));
            X.Check(B->Scale().X < B->BaseScale - .02f, TEXT("Pressed button visibly compresses"));
            X.Check(!B->Color().Equals(B->BaseColor, .02f), TEXT("Pressed button has a distinct tint"));
            B->CheckLayoutAndWorld(X);
            X.Shot(*(FilePrefix + TEXT("-pressed.png")));
            return true;
        });
        Step(TEXT("Release outside cancels activation and restores appearance"), [B](FFixture& X)
        {
            const FPointerEvent Up = B->Pointer(false, true);
            B->Button->OnMouseLeave(Up);
            X.Check(B->Button->OnMouseButtonUp(B->Button->GetCachedGeometry(), Up).IsEventHandled(), TEXT("Native button consumes cancelled release"));
            FSlateApplication::Get().ClearAllUserFocus();
        }, [B, FilePrefix](FFixture& X)
        {
            if (!X.After(.4)) return false;
            X.Check(!B->Button->IsHovered() && !B->Button->IsPressed(), TEXT("Hover and press clear on leave/release"));
            X.Check(FMath::IsNearlyEqual(B->Scale().X, B->BaseScale, .003f), TEXT("Scale returns to baseline"));
            X.Check(B->Color().Equals(B->BaseColor, .005f), TEXT("Color returns to baseline"));
            B->CheckLayoutAndWorld(X);
            X.Shot(*(FilePrefix + TEXT("-unhover.png")));
            return true;
        });
        Step(TEXT("Direct focus does not leave a phantom mouse hover"), [B](FFixture& X)
        { FSlateApplication::Get().SetAllUserFocus(B->Button, EFocusCause::SetDirectly); }, [B](FFixture& X)
        {
            if (!X.After(.25)) return false;
            X.Check(FMath::IsNearlyEqual(B->Scale().X, B->BaseScale, .003f) && B->Color().Equals(B->BaseColor, .005f),
                TEXT("Default menu focus keeps the unhovered appearance"));
            B->CheckLayoutAndWorld(X);
            return true;
        });
    };

    Exercise(TEXT("Play"), TEXT("20-feedback-main"));
    Step(TEXT("Start still activates once after feedback interactions"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("Play"));
        return X.Check(X.Session->GetScreen() == EBDAlphaScreen::LevelSelect, TEXT("Start routes to level selection"));
    });
    Step(TEXT("Wait for the real level cards"), {}, [](FFixture& X) { return X.After(.4); });
    Exercise(TEXT("TUTORIAL"), TEXT("21-feedback-level-card"));
    Step(TEXT("Tutorial card still enters gameplay"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        X.Click(TEXT("TUTORIAL"));
        return true;
    });
    Step(TEXT("Pause the running tutorial through its HUD control"), {}, [](FFixture& X)
    {
        if (!X.After(.7) || X.Session->GetScreen() != EBDAlphaScreen::Playing || !X.PC->GetPawn()) return false;
        X.Click(TEXT("II"));
        return X.Check(X.Session->GetScreen() == EBDAlphaScreen::Pause, TEXT("HUD pause routes correctly"));
    });
    Step(TEXT("Wait for the pause overlay"), {}, [](FFixture& X) { return X.After(.4); });
    Exercise(TEXT("Resume"), TEXT("22-feedback-pause"));
    Step(TEXT("Resume still works without leaking a gameplay click"), {}, [](FFixture& X)
    {
        if (!X.After()) return false;
        const int32 Bombs = X.Mode->BombsUsed;
        X.Click(TEXT("Resume"));
        X.Check(X.Session->GetScreen() == EBDAlphaScreen::Playing && !X.PC->IsPaused(), TEXT("Resume returns to gameplay"));
        X.Check(X.Mode->BombsUsed == Bombs && !X.Mode->ActiveBomb, TEXT("Resume activation does not create or throw a bomb"));
        return true;
    });

    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
