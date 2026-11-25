#include "RecallHUD.h"

#include "RecallMode.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/SlateDelegates.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FLinearColor Accent = FLinearColor(FColor(121, 210, 246));
    const FLinearColor TextColor = FLinearColor(FColor(239, 247, 253));
    const FLinearColor Muted = FLinearColor(FColor(166, 187, 205));
    const FLinearColor Gold = FLinearColor(FColor(255, 181, 97));
    const FLinearColor Green = FLinearColor(FColor(143, 226, 174));
    const FLinearColor Panel = FLinearColor(FColor(10, 24, 38));
    constexpr int32 RowsPerPage = 7;

    class SRecallOverlay : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRecallOverlay) {}
            SLATE_DEFAULT_SLOT(FArguments, Content)
            SLATE_EVENT(FOnKeyDown, OnKeyDown)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            KeyDown = InArgs._OnKeyDown;
            ChildSlot[InArgs._Content.Widget];
        }

        virtual bool SupportsKeyboardFocus() const override { return true; }

        virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
        {
            return KeyDown.IsBound() ? KeyDown.Execute(Geometry, Event) : FReply::Unhandled();
        }

    private:
        FOnKeyDown KeyDown;
    };

    const FSlateBrush* WhiteBrush()
    {
        return FCoreStyle::Get().GetBrush("WhiteBrush");
    }

    TSharedRef<STextBlock> Label(const FText& Text, int32 Size, FLinearColor Color = TextColor, bool bBold = false)
    {
        return SNew(STextBlock)
            .Text(Text)
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size))
            .ColorAndOpacity(Color)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis);
    }

    TSharedRef<SWidget> Button(const FText& Text, FOnClicked Clicked, bool bEnabled = true, FLinearColor Color = TextColor)
    {
        return SNew(SButton)
            .IsEnabled(bEnabled)
            .OnClicked(MoveTemp(Clicked))
            .ContentPadding(FMargin(14, 9))
            .HAlign(HAlign_Center)
            [Label(Text, 14, Color, true)];
    }

    TSharedRef<SWidget> ArtworkButton(const FText& Text, FOnClicked Clicked)
    {
        static const FButtonStyle Style = FButtonStyle()
            .SetNormal(FSlateColorBrush(FLinearColor::Transparent))
            .SetHovered(FSlateColorBrush(FLinearColor(0.11f, 0.48f, 0.85f, 0.2f)))
            .SetPressed(FSlateColorBrush(FLinearColor(0.05f, 0.3f, 0.6f, 0.35f)))
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return SNew(SButton)
            .ButtonStyle(&Style)
            .ToolTipText(Text)
            .OnClicked(MoveTemp(Clicked));
    }

    TSharedRef<SWidget> Modal(TSharedRef<SWidget> Content, float Width, float Dim = 0.83f)
    {
        return SNew(SOverlay)
            + SOverlay::Slot()
            [SNew(SBorder).Padding(0).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0, 0, 0, Dim))]
            + SOverlay::Slot().Padding(20).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(SBox).WidthOverride(Width)
                    [SNew(SBorder).Padding(24).BorderImage(WhiteBrush()).BorderBackgroundColor(Panel)[Content]]
                ]
            ];
    }

    FLinearColor GradeColor(const FString& Grade)
    {
        if (Grade.Equals(TEXT("Perfect"), ESearchCase::IgnoreCase))
        {
            return Green;
        }
        return Grade.Equals(TEXT("Bad"), ESearchCase::IgnoreCase) ? Gold : Accent;
    }

    TSharedRef<SWidget> ScoreRow(const TArray<FText>& Values, FLinearColor Background, FLinearColor Grade, FLinearColor Status)
    {
        static constexpr int32 Widths[] = {38, 196, 105, 79, 91, 91, 91, 158};
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        for (int32 Column = 0; Column < UE_ARRAY_COUNT(Widths); ++Column)
        {
            const FLinearColor Color = Column == 2 ? Grade : Column == 7 ? Status : TextColor;
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(Widths[Column])
                [Label(Values[Column], 12, Color)]
            ];
        }
        return SNew(SBorder).Padding(FMargin(8, 7)).BorderImage(WhiteBrush()).BorderBackgroundColor(Background)[Row];
    }
}

ARecallMode* ARecallHUD::GetMission() const
{
    UWorld* World = GetWorld();
    return World ? World->GetAuthGameMode<ARecallMode>() : nullptr;
}

bool ARecallHUD::IsOverlayVisible() const
{
    const ARecallMode* Mission = GetMission();
    return OpenedPanel != EPanel::None || (Mission && Mission->GetScreen() != ERecallScreen::Gameplay);
}

void ARecallHUD::DrawHUD()
{
    Super::DrawHUD();
    const ARecallMode* Mission = GetMission();
    if (!Mission || !Canvas || !GEngine)
    {
        return;
    }

    const ERecallScreen Screen = Mission->GetScreen();
    if (GetOwningPlayerController() == GetWorld()->GetFirstPlayerController() && FSlateApplication::IsInitialized())
    {
        if (!OverlayHost.IsValid())
        {
            if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
            {
                MainMenuTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MissionRecall/T_MainMenu.T_MainMenu"), nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (MainMenuTexture)
                {
                    MenuBrush.SetResourceObject(MainMenuTexture);
                    MenuBrush.SetImageSize(FVector2f(1041, 565));
                    MenuBrush.DrawAs = ESlateBrushDrawType::Image;
                }
                OverlayHost = SNew(SBox);
                Viewport->AddViewportWidgetContent(OverlayHost.ToSharedRef(), 20);
                VisibleScreen = Screen;
                RefreshOverlay();
            }
        }
        else if (VisibleScreen != Screen)
        {
            VisibleScreen = Screen;
            OpenedPanel = EPanel::None;
            MenuMessage.Reset();
            RefreshOverlay();
        }
    }

    if (Screen == ERecallScreen::Gameplay)
    {
        DrawGame(*Mission);
    }
}

void ARecallHUD::DrawGame(const ARecallMode& Mission)
{
    FString Controls;
    float Progress = -1;
    const bool bSecondPlayer = GetOwningPlayerController() != GetWorld()->GetFirstPlayerController();
    const FString Readout = Mission.Readout(Controls, Progress, bSecondPlayer);
    const FRecallHUDData Data = Mission.GetHUDData();
    const FLinearColor Background(0.008f, 0.016f, 0.028f, 0.91f);
    const float Scale = FMath::Clamp(Canvas->ClipX / 890.0f, 0.62f, 1.0f);
    const float Width = FMath::Min(850.0f * Scale, Canvas->ClipX - 24.0f);
    UFont* Font = GEngine->GetSmallFont();
    DrawRect(Background, 12, 12, Width, 99 * Scale);
    DrawRect(Accent, 12, 12, 3, 99 * Scale);
    const FString Title = bSecondPlayer ? TEXT("COPILOT") : Data.Student.IsEmpty() ? TEXT("MISSION RECALL") : Data.Student;
    DrawText(Title, Accent, 23, 20, GEngine->GetMediumFont(), Scale);
    DrawText(Readout, TextColor, 23, 48 * Scale, Font, 1.12f * Scale);
    if (Progress >= 0)
    {
        const float ProgressWidth = FMath::Min(300.0f, Width - 22.0f);
        DrawRect(Panel, 23, 96 * Scale, ProgressWidth, 5);
        DrawRect(Accent, 23, 96 * Scale, ProgressWidth * FMath::Clamp(Progress, 0.0f, 1.0f), 5);
    }

    float TextWidth = 0, TextHeight = 0;
    GetTextSize(Controls, TextWidth, TextHeight, Font);
    const float ControlScale = FMath::Min(Scale, (Width - 22.0f) / FMath::Max(1.0f, TextWidth));
    const float Bottom = Canvas->ClipY - 38;
    DrawRect(Background, 12, Bottom, Width, 27);
    DrawText(Controls, TextColor, 23, Bottom + 6, Font, ControlScale);

    TArray<TPair<FVector, FString>> Markers;
    Mission.GetWorldMarkers(Markers, bSecondPlayer);
    for (const TPair<FVector, FString>& Marker : Markers)
    {
        const FVector Screen = Project(Marker.Key);
        if (Screen.Z > 0 && Screen.X > 15 && Screen.X < Canvas->ClipX - 15 && Screen.Y > 20 && Screen.Y < Bottom - 5)
        {
            GetTextSize(Marker.Value, TextWidth, TextHeight, Font, Scale);
            const float Left = FMath::Clamp(float(Screen.X) - TextWidth * 0.5f, 7.0f, FMath::Max(7.0f, Canvas->ClipX - TextWidth - 7.0f));
            DrawRect(Background, Left - 5, Screen.Y - 3, TextWidth + 10, TextHeight + 6);
            DrawText(Marker.Value, Accent, Left, Screen.Y, Font, Scale);
        }
    }
}

void ARecallHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    OpenedPanel = EPanel::None;
    if (OverlayHost.IsValid())
    {
        OverlayHost->SetContent(SNullWidget::NullWidget);
        if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
        {
            Viewport->RemoveViewportWidgetContent(OverlayHost.ToSharedRef());
        }
        OverlayHost.Reset();
    }
    NameBox.Reset();
    Super::EndPlay(EndPlayReason);
}

void ARecallHUD::RefreshOverlay()
{
    if (!OverlayHost.IsValid())
    {
        return;
    }

    NameBox.Reset();
    TSharedRef<SWidget> Content = SNullWidget::NullWidget;
    if (OpenedPanel == EPanel::Scores)
    {
        Content = BuildScores();
    }
    else if (VisibleScreen == ERecallScreen::MainMenu)
    {
        Content = BuildMainMenu(OpenedPanel == EPanel::None);
        if (OpenedPanel != EPanel::None)
        {
            TSharedRef<SWidget> Popup = OpenedPanel == EPanel::Settings ? BuildSettings() : BuildNamePrompt();
            Content = SNew(SOverlay) + SOverlay::Slot()[Content] + SOverlay::Slot()[Popup];
        }
    }
    else if (VisibleScreen == ERecallScreen::Pause)
    {
        Content = BuildPause();
    }
    else if (VisibleScreen == ERecallScreen::SuitChoice)
    {
        Content = BuildSuitChoice();
    }
    else if (VisibleScreen == ERecallScreen::Scorecard)
    {
        if (const ARecallMode* Mission = GetMission())
        {
            Content = BuildScorecard(Mission->GetHUDData());
        }
    }
    if (VisibleScreen == ERecallScreen::Gameplay && OpenedPanel == EPanel::None)
    {
        OverlayHost->SetVisibility(EVisibility::Collapsed);
        OverlayHost->SetContent(Content);
        return;
    }
    TSharedRef<SRecallOverlay> Overlay = SNew(SRecallOverlay)
        .OnKeyDown_Lambda([this](const FGeometry&, const FKeyEvent& Event)
        {
            const FKey Key = Event.GetKey();
            const bool bSuitShortcut = VisibleScreen == ERecallScreen::SuitChoice && (Key == EKeys::E || Key == EKeys::F);
            const bool bPauseSetting = VisibleScreen == ERecallScreen::Pause && (Key == EKeys::F3 || Key == EKeys::F4);
            if (Event.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::Enter || bSuitShortcut || bPauseSetting))
            {
                return FReply::Handled();
            }
            if (Key == EKeys::Escape)
            {
                if (HandleBack())
                {
                    return FReply::Handled();
                }
                if (VisibleScreen == ERecallScreen::Pause)
                {
                    RunAction(ERecallAction::Resume);
                    return FReply::Handled();
                }
            }
            if (Key == EKeys::Enter && VisibleScreen == ERecallScreen::MainMenu && OpenedPanel == EPanel::None)
            {
                OpenStartPrompt();
                return FReply::Handled();
            }
            if (bSuitShortcut)
            {
                RunAction(Key == EKeys::E ? ERecallAction::Equip : ERecallAction::Fly);
                return FReply::Handled();
            }
            if (bPauseSetting)
            {
                RunAction(Key == EKeys::F3 ? ERecallAction::CycleQuality : ERecallAction::ToggleFullscreen);
                return FReply::Handled();
            }
            return FReply::Unhandled();
        })
        [Content];
    OverlayHost->SetVisibility(EVisibility::SelfHitTestInvisible);
    OverlayHost->SetContent(Overlay);
    if (NameBox.IsValid())
    {
        FSlateApplication::Get().SetKeyboardFocus(NameBox, EFocusCause::SetDirectly);
    }
    else
    {
        FSlateApplication::Get().SetKeyboardFocus(Overlay, EFocusCause::SetDirectly);
    }
}

void ARecallHUD::OpenPanel(EPanel PanelToOpen)
{
    OpenedPanel = PanelToOpen;
    MenuMessage.Reset();
    RefreshOverlay();
}

bool ARecallHUD::HandleBack()
{
    if (OpenedPanel == EPanel::None)
    {
        return false;
    }
    OpenPanel(OpenedPanel == EPanel::Scores ? ReturnFromScores : EPanel::None);
    return true;
}

void ARecallHUD::OpenStartPrompt()
{
    if (VisibleScreen == ERecallScreen::MainMenu && OpenedPanel == EPanel::None)
    {
        OpenPanel(EPanel::StartName);
    }
}

void ARecallHUD::RunAction(ERecallAction Action)
{
    ARecallMode* Mission = GetMission();
    if (!Mission)
    {
        return;
    }
    MenuMessage = Mission->HandleHUDAction(Action, StudentName);
    if (Mission->GetScreen() != VisibleScreen)
    {
        VisibleScreen = Mission->GetScreen();
        OpenedPanel = EPanel::None;
        if (Action == ERecallAction::MainMenu)
        {
            StudentName.Reset();
        }
        RefreshOverlay();
    }
}

void ARecallHUD::SubmitName()
{
    StudentName = FRecallProgress::NormalizeStudentName(StudentName);
    if (StudentName.IsEmpty())
    {
        MenuMessage = TEXT("Enter a student name.");
        return;
    }
    RunAction(OpenedPanel == EPanel::ContinueName ? ERecallAction::Continue : ERecallAction::Start);
}

TSharedRef<SWidget> ARecallHUD::BuildMainMenu(bool bInteractive)
{
    const FOnClicked Start = FOnClicked::CreateWeakLambda(this, [this]
    {
        OpenStartPrompt();
        return FReply::Handled();
    });
    const FOnClicked Continue = FOnClicked::CreateWeakLambda(this, [this]
    {
        OpenPanel(EPanel::ContinueName);
        return FReply::Handled();
    });
    const FOnClicked Settings = FOnClicked::CreateWeakLambda(this, [this]
    {
        OpenPanel(EPanel::Settings);
        return FReply::Handled();
    });
    const FOnClicked Exit = FOnClicked::CreateWeakLambda(this, [this]
    {
        RunAction(ERecallAction::Exit);
        return FReply::Handled();
    });

    if (!MainMenuTexture)
    {
        TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Label(INVTEXT("MISSION RECALL"), 38, TextColor, true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 22)[Label(INVTEXT("made by moiz"), 16, Accent)];
        if (bInteractive)
        {
            Menu->AddSlot().AutoHeight().Padding(0, 4)[Button(INVTEXT("Start Game"), Start)];
            Menu->AddSlot().AutoHeight().Padding(0, 4)[Button(INVTEXT("Continue"), Continue)];
            Menu->AddSlot().AutoHeight().Padding(0, 4)[Button(INVTEXT("Settings"), Settings)];
            Menu->AddSlot().AutoHeight().Padding(0, 4)[Button(INVTEXT("Exit"), Exit)];
        }
        return Modal(Menu, 425, 1);
    }

    // The artwork and button bounds use top-left coordinates in the source image.
    const FVector2D TopLeft = FVector2D::ZeroVector;
    TSharedRef<SConstraintCanvas> Art = SNew(SConstraintCanvas)
        + SConstraintCanvas::Slot().Alignment(TopLeft).Offset(FMargin(0, 0, 1041, 565))
        [SNew(SImage).Image(&MenuBrush).Visibility(EVisibility::HitTestInvisible)];
    if (bInteractive)
    {
        Art->AddSlot().Alignment(TopLeft).Offset(FMargin(46, 241, 251, 45))[ArtworkButton(INVTEXT("Start Game"), Start)];
        Art->AddSlot().Alignment(TopLeft).Offset(FMargin(52, 302, 239, 45))[ArtworkButton(INVTEXT("Continue"), Continue)];
        Art->AddSlot().Alignment(TopLeft).Offset(FMargin(52, 361, 234, 45))[ArtworkButton(INVTEXT("Settings"), Settings)];
        Art->AddSlot().Alignment(TopLeft).Offset(FMargin(52, 419, 229, 45))[ArtworkButton(INVTEXT("Exit"), Exit)];
    }
    return SNew(SBorder).Padding(0).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor::Black)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(1041).HeightOverride(565)[Art]]];
}

TSharedRef<SWidget> ARecallHUD::BuildNamePrompt()
{
    const bool bContinue = OpenedPanel == EPanel::ContinueName;
    const FText ConfirmText = bContinue ? INVTEXT("Continue") : INVTEXT("Start Game");
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Label(ConfirmText, 28, TextColor, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 7)[Label(INVTEXT("Student name"), 15, Accent, true)]
        + SVerticalBox::Slot().AutoHeight()
        [
            SAssignNew(NameBox, SEditableTextBox)
                .Text(FText::FromString(StudentName))
                .HintText(INVTEXT("Enter your name"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
                .ClearKeyboardFocusOnCommit(false)
                .OnTextChanged_Lambda([this](const FText& TypedText)
                {
                    const FString Typed = TypedText.ToString();
                    StudentName.Reset();
                    for (TCHAR Letter : Typed)
                    {
                        if (StudentName.Len() >= 24)
                        {
                            break;
                        }
                        if ((Letter < 128 && FChar::IsAlnum(Letter)) || Letter == TEXT(' ') || Letter == TEXT('-') || Letter == TEXT('_'))
                        {
                            StudentName.AppendChar(Letter);
                        }
                    }
                    MenuMessage.Reset();
                    if (StudentName != Typed && NameBox.IsValid())
                    {
                        NameBox->SetText(FText::FromString(StudentName));
                    }
                })
                .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
                {
                    if (Commit == ETextCommit::OnEnter)
                    {
                        SubmitName();
                    }
                })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(MenuMessage); }).ColorAndOpacity(Gold)]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
            [Button(ConfirmText, FOnClicked::CreateWeakLambda(this, [this] { SubmitName(); return FReply::Handled(); }), true, Accent)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(6, 0, 0, 0)
            [Button(INVTEXT("Back"), FOnClicked::CreateWeakLambda(this, [this] { HandleBack(); return FReply::Handled(); }))]
        ], 430);
}

TSharedRef<SWidget> ARecallHUD::BuildSettings()
{
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[Label(INVTEXT("Settings"), 29, TextColor, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("Toggle fullscreen"), FOnClicked::CreateWeakLambda(this, [this] { RunAction(ERecallAction::ToggleFullscreen); return FReply::Handled(); }))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("Change graphics quality"), FOnClicked::CreateWeakLambda(this, [this] { RunAction(ERecallAction::CycleQuality); return FReply::Handled(); }))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(MenuMessage); }).ColorAndOpacity(Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("All Student Scorecards"), FOnClicked::CreateWeakLambda(this, [this] { OpenScores(); return FReply::Handled(); }), true, Green)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
        [Button(INVTEXT("Back"), FOnClicked::CreateWeakLambda(this, [this] { HandleBack(); return FReply::Handled(); }))], 430);
}

void ARecallHUD::OpenScores()
{
    Scores = FRecallProgress::ReadScores();
    ReturnFromScores = OpenedPanel;
    ScorePage = 0;
    SortScores();
    OpenPanel(EPanel::Scores);
}

void ARecallHUD::SortScores()
{
    Scores.StableSort([this](const FRecallScorecard& Left, const FRecallScorecard& Right)
    {
        switch (ScoreSort)
        {
        case EScoreSort::Points: return Left.Points > Right.Points;
        case EScoreSort::Total: return Left.TotalSeconds > Right.TotalSeconds;
        case EScoreSort::Repair: return Left.RepairSeconds > Right.RepairSeconds;
        case EScoreSort::Latest: return Left.UpdatedUtc.Compare(Right.UpdatedUtc) > 0;
        case EScoreSort::Student: return Left.Student.Compare(Right.Student, ESearchCase::IgnoreCase) < 0;
        }
        return false;
    });
}

TSharedRef<SWidget> ARecallHUD::BuildScores()
{
    const int32 PageCount = FMath::Max(1, FMath::DivideAndRoundUp(Scores.Num(), RowsPerPage));
    TSharedRef<SHorizontalBox> SortButtons = SNew(SHorizontalBox);
    const FText SortNames[] = {INVTEXT("Best score"), INVTEXT("Most total time"), INVTEXT("Most repair time"), INVTEXT("Latest"), INVTEXT("Student name")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(SortNames); ++Index)
    {
        const EScoreSort Sort = static_cast<EScoreSort>(Index);
        SortButtons->AddSlot().FillWidth(1).Padding(0, 0, 6, 0)
        [Button(SortNames[Index], FOnClicked::CreateWeakLambda(this, [this, Sort]
        {
            ScoreSort = Sort;
            ScorePage = 0;
            SortScores();
            RefreshOverlay();
            return FReply::Handled();
        }), true, Sort == ScoreSort ? Green : TextColor)];
    }

    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ScoreRow({INVTEXT("#"), INVTEXT("Student"), INVTEXT("Landing"), INVTEXT("Score"), INVTEXT("Flight (s)"), INVTEXT("Repair (s)"), INVTEXT("Total (s)"), INVTEXT("Status")}, FLinearColor(0.038f, 0.083f, 0.13f), Accent, Accent)];
    const int32 First = ScorePage * RowsPerPage;
    for (int32 Index = First; Index < FMath::Min(First + RowsPerPage, Scores.Num()); ++Index)
    {
        const FRecallScorecard& Row = Scores[Index];
        const auto Seconds = [](float Value) { return FText::FromString(FString::Printf(TEXT("%.1f"), Value)); };
        Rows->AddSlot().AutoHeight()[ScoreRow({FText::AsNumber(Index + 1), FText::FromString(Row.Student), FText::FromString(Row.Grade),
            FText::AsNumber(Row.Points), Seconds(Row.FlightSeconds), Seconds(Row.RepairSeconds), Seconds(Row.TotalSeconds), FText::FromString(Row.Status)},
            Index % 2 == 0 ? FLinearColor(0.019f, 0.035f, 0.056f) : Panel, GradeColor(Row.Grade),
            Row.Status.Equals(TEXT("Completed"), ESearchCase::IgnoreCase) ? Green : Gold)];
    }
    if (Scores.IsEmpty())
    {
        Rows->AddSlot().AutoHeight().Padding(8, 16)[Label(INVTEXT("No student attempts yet."), 16, Muted)];
    }

    const auto Page = [this](int32 Direction)
    {
        return FOnClicked::CreateWeakLambda(this, [this, Direction]
        {
            ScorePage += Direction;
            RefreshOverlay();
            return FReply::Handled();
        });
    };
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Label(INVTEXT("Student scorecards"), 30, TextColor, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 12)[SortButtons]
        + SVerticalBox::Slot().AutoHeight()[Rows]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 7, 0)[Button(INVTEXT("Previous"), Page(-1), ScorePage > 0)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 13, 0)[Button(INVTEXT("Next"), Page(1), ScorePage + 1 < PageCount)]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [Label(FText::Format(INVTEXT("Page {0} of {1}   |   {2} attempts"), ScorePage + 1, PageCount, Scores.Num()), 14, Muted)]
            + SHorizontalBox::Slot().AutoWidth()[Button(INVTEXT("Back"), FOnClicked::CreateWeakLambda(this, [this] { HandleBack(); return FReply::Handled(); }))]
        ], 920, 1);
}

TSharedRef<SWidget> ARecallHUD::BuildPause()
{
    const auto Action = [this](ERecallAction Choice)
    {
        return FOnClicked::CreateWeakLambda(this, [this, Choice] { RunAction(Choice); return FReply::Handled(); });
    };
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[Label(INVTEXT("Paused"), 31, TextColor, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[Button(INVTEXT("Resume"), Action(ERecallAction::Resume), true, Accent)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[Button(INVTEXT("End Attempt / Final Scorecard"), Action(ERecallAction::EndAttempt), true, Gold)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("All Student Scorecards"), FOnClicked::CreateWeakLambda(this, [this] { OpenScores(); return FReply::Handled(); }), true, Green)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Label(INVTEXT("F3 graphics quality   |   F4 fullscreen"), 14, Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(MenuMessage); }).ColorAndOpacity(Muted)], 500);
}

TSharedRef<SWidget> ARecallHUD::BuildSuitChoice()
{
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Label(INVTEXT("Choose how to explore"), 29, TextColor, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)[Label(INVTEXT("Wear the suit before fixing the shuttle."), 18, Gold)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 21)[Label(INVTEXT("Use the suit to walk or fly freely without it."), 15, Muted)]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
            [Button(INVTEXT("Fly without suit / F"), FOnClicked::CreateWeakLambda(this, [this] { RunAction(ERecallAction::Fly); return FReply::Handled(); }), true, Gold)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(6, 0, 0, 0)
            [Button(INVTEXT("Equip suit / E"), FOnClicked::CreateWeakLambda(this, [this] { RunAction(ERecallAction::Equip); return FReply::Handled(); }), true, Accent)]
        ], 555, 0.7f);
}

TSharedRef<SWidget> ARecallHUD::BuildScorecard(const FRecallHUDData& Data)
{
    const FLinearColor Grade = GradeColor(Data.Grade);
    FText Title = INVTEXT("Attempt ended");
    if (Data.bCompleted)
    {
        Title = Data.Grade.Equals(TEXT("Perfect"), ESearchCase::IgnoreCase) ? INVTEXT("Perfect landing!") : INVTEXT("Shuttle repaired");
    }
    return Modal(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Label(Title, 34, Grade, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 16)[Label(FText::FromString(Data.Student), 21)]
        + SVerticalBox::Slot().AutoHeight()[Label(FText::Format(INVTEXT("Final score  {0} / 1000"), Data.Points), 27, Green, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 5)
        [Label(FText::FromString(FString::Printf(TEXT("%s landing   |   Impact %.2f m/s"), *Data.Grade, Data.Impact)), 17, Grade)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [Label(FText::FromString(FString::Printf(TEXT("Flight %.2f s   |   Repairs %.2f s   |   Total %.2f s"), Data.FlightTime, Data.RepairTime, Data.TotalTime)), 16, Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [Label(FText::Format(INVTEXT("Panels repaired {0} / {1}   |   {2}"), Data.PanelsDone, Data.PanelsRequired, FText::FromString(Data.Status)), 16, Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(Data.bCompleted ? INVTEXT("Explore") : INVTEXT("Retry as same student"), FOnClicked::CreateWeakLambda(this, [this, bCompleted = Data.bCompleted]
        {
            RunAction(bCompleted ? ERecallAction::Explore : ERecallAction::Retry);
            return FReply::Handled();
        }), true, Accent)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("All Student Scorecards"), FOnClicked::CreateWeakLambda(this, [this] { OpenScores(); return FReply::Handled(); }), true, Green)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [Button(INVTEXT("Next Student / Main Menu"), FOnClicked::CreateWeakLambda(this, [this] { RunAction(ERecallAction::MainMenu); return FReply::Handled(); }))], 590);
}
