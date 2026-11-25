#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RecallProgress.h"
#include "Styling/SlateBrush.h"
#include "RecallHUD.generated.h"

class ARecallMode;
class SBox;
class SEditableTextBox;
class SWidget;
class UTexture2D;

enum class ERecallScreen : uint8 { MainMenu, Gameplay, Pause, SuitChoice, Scorecard };
enum class ERecallAction : uint8
{
    Start, Continue, Resume, EndAttempt, Fly, Equip, Explore, Retry, MainMenu, Save, Load,
    ToggleFullscreen, CycleQuality, Exit
};

struct FRecallHUDData
{
    FString Student;
    FString Grade;
    FString Status;
    int32 Points = 0;
    int32 PanelsDone = 0;
    int32 PanelsRequired = 0;
    float FlightTime = 0;
    float RepairTime = 0;
    float TotalTime = 0;
    float Impact = 0;
    bool bCompleted = false;
};

UCLASS()
class ARecallHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    bool IsOverlayVisible() const;
    bool HandleBack();
    void OpenStartPrompt();

private:
    enum class EPanel : uint8 { None, StartName, ContinueName, Settings, Scores };
    enum class EScoreSort : uint8 { Points, Total, Repair, Latest, Student };

    ARecallMode* GetMission() const;
    void DrawGame(const ARecallMode& Mission);
    void RefreshOverlay();
    void OpenPanel(EPanel Panel);
    void OpenScores();
    void SortScores();
    void SubmitName();
    void RunAction(ERecallAction Action);
    TSharedRef<SWidget> BuildMainMenu(bool bInteractive);
    TSharedRef<SWidget> BuildNamePrompt();
    TSharedRef<SWidget> BuildSettings();
    TSharedRef<SWidget> BuildScores();
    TSharedRef<SWidget> BuildPause();
    TSharedRef<SWidget> BuildSuitChoice();
    TSharedRef<SWidget> BuildScorecard(const FRecallHUDData& Data);

    UPROPERTY()
    TObjectPtr<UTexture2D> MainMenuTexture;

    FSlateBrush MenuBrush;
    TSharedPtr<SBox> OverlayHost;
    TSharedPtr<SEditableTextBox> NameBox;
    FString StudentName;
    FString MenuMessage;
    TArray<FRecallScorecard> Scores;
    ERecallScreen VisibleScreen = ERecallScreen::MainMenu;
    EPanel OpenedPanel = EPanel::None;
    EPanel ReturnFromScores = EPanel::None;
    EScoreSort ScoreSort = EScoreSort::Points;
    int32 ScorePage = 0;
};
