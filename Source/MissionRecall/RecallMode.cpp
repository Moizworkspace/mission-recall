#include "RecallMode.h"

#include "Camera/CameraActor.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, MissionRecall, "MissionRecall");

ARecallMode::ARecallMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = ARecallHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
}

void ARecallMode::StartPlay()
{
    Super::StartPlay();
    SetScreen(ERecallScreen::MainMenu);
}

void ARecallMode::EnsureSite()
{
    if (!View)
    {
        BuildSite();
        InitAudio();
    }
}

void ARecallMode::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!Attempt.Id.IsEmpty())
    {
        if (Attempt.Status == TEXT("In progress"))
        {
            FinishAttempt(false);
        }
        SaveMission();
    }
    Super::EndPlay(Reason);
}

void ARecallMode::SetScreen(ERecallScreen NewScreen)
{
    Screen = NewScreen;
    ApplyInputMode();
    if (Screen != ERecallScreen::Gameplay)
    {
        for (UAudioComponent* Audio : AudioPlayers)
        {
            Audio->Stop();
        }
    }
}

void ARecallMode::ApplyInputMode()
{
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        const bool bShowCursor = Screen != ERecallScreen::Gameplay;
        Controller->bShowMouseCursor = bShowCursor;
        if (bShowCursor)
        {
            FInputModeGameAndUI Input;
            Input.SetHideCursorDuringCapture(false);
            Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            Controller->SetInputMode(Input);
        }
        else
        {
            Controller->SetInputMode(FInputModeGameOnly());
        }
    }
    if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
    {
        Viewport->SetForceDisableSplitscreen(Screen == ERecallScreen::MainMenu || !CoopController);
    }
}

void ARecallMode::ResetMission()
{
    for (FPilot& Pilot : Pilots)
    {
        Pilot = FPilot();
    }
    CapsuleLocation = OnGround(FVector::ZeroVector) + FVector(0, 0, 6500);
    LateralVelocity = FVector2D::ZeroVector;
    LandingMouseStick = FVector2D::ZeroVector;
    Elapsed = Descent = Tilt = Hold = RoverSpeed = RoverHeading = Survey = SaveTimer = 0;
    NoticeSeconds = FootstepTimer = 0;
    RepairsRequired = 0;
    ActiveStation = ActivePilot = RoverDriver = INDEX_NONE;
    Attempt = FRecallScorecard();
    bHasPlayed = false;
    for (FStation& Station : Stations)
    {
        Station.Done = false;
    }
    Capsule->SetActorLocationAndRotation(CapsuleLocation, FRotator::ZeroRotator);
    Rover->SetActorLocationAndRotation(OnGround(FVector(1300, -800, 0)), FRotator::ZeroRotator);
    UpdateSiteLocations();
    UpdateViews();
}

void ARecallMode::BeginMission(const FString& Student)
{
    if (Attempt.Status == TEXT("In progress") && !Attempt.Id.IsEmpty())
    {
        FinishAttempt(false);
        SaveMission();
    }
    EnsureSite();
    ResetMission();
    Attempt = FRecallProgress::StartAttempt(Student);
    Pilots[0].Phase = Pilots[1].Phase = EPhase::Landing;
    SetScreen(ERecallScreen::Gameplay);
    ShowNotice(TEXT("Release steering and fast descent before touchdown for a perfect landing."));
}

void ARecallMode::FinishAttempt(bool bCompleted)
{
    if (Attempt.Id.IsEmpty() || Attempt.Status != TEXT("In progress"))
    {
        return;
    }
    Attempt.Status = bCompleted ? TEXT("Completed") : TEXT("Ended early");
    Attempt.RepairPanels = PanelsRepaired();
    FRecallProgress::UpdateScore(Attempt, bCompleted);
    FRecallProgress::RecordScore(Attempt);
}

bool ARecallMode::SaveMission()
{
    if (Attempt.Id.IsEmpty())
    {
        return false;
    }
    Attempt.RepairPanels = PanelsRepaired();
    FRecallProgress::UpdateScore(Attempt, Attempt.Status == TEXT("Completed"));
    const bool bScoreSaved = FRecallProgress::RecordScore(Attempt);
    if (Pilots[0].Phase == EPhase::Landing || Pilots[0].Phase == EPhase::Briefing)
    {
        return bScoreSaved;
    }
    FRecallSnapshot Save;
    Save.Scorecard = Attempt;
    Save.CapsuleLocation = CapsuleLocation;
    Save.SurfacePosition = Pilots[0].Position;
    Save.LabPosition = Pilots[0].Phase == EPhase::Lab ? Pilots[0].Position : LabCenter;
    Save.CoopPosition = Pilots[1].Position;
    Save.CoopPhase = static_cast<int32>(Pilots[1].Phase);
    Save.CoopYaw = Pilots[1].CameraYaw;
    Save.CoopPitch = Pilots[1].CameraPitch;
    Save.CoopFirstPerson = Pilots[1].bFirstPerson;
    Save.RoverLocation = Rover->GetActorLocation();
    Save.RoverHeading = RoverHeading;
    Save.Survey = Survey;
    Save.Phase = static_cast<int32>(Pilots[0].Phase);
    Save.Suited = Pilots[0].bSuited;
    Save.FreeFlight = Pilots[0].bFreeFlight;
    Save.FirstPerson = Pilots[0].bFirstPerson;
    Save.Jetpack = Pilots[0].bJetpack;
    Save.Fuel = Pilots[0].Fuel;
    Save.Oxygen = Pilots[0].Oxygen;
    Save.CameraYaw = Pilots[0].CameraYaw;
    Save.CameraPitch = Pilots[0].CameraPitch;
    for (const FStation& Station : Stations)
    {
        Save.StationsDone.Add(Station.Done);
    }
    return FRecallProgress::Save(Save) && bScoreSaved;
}

FString ARecallMode::RestoreMission(const FString& Student)
{
    FRecallSnapshot Save;
    if (!FRecallProgress::Load(Student, Save) || Save.Phase < static_cast<int32>(EPhase::EVA) ||
        Save.Phase > static_cast<int32>(EPhase::Lab) || Save.StationsDone.Num() != LabExit + 1)
    {
        return TEXT("No landed mission was found for that student.");
    }
    const bool bSameAttempt = Attempt.Id == Save.Scorecard.Id;
    if (!bSameAttempt && Attempt.Status == TEXT("In progress") && !Attempt.Id.IsEmpty())
    {
        FinishAttempt(false);
        SaveMission();
    }
    EnsureSite();
    ResetMission();
    Attempt = Save.Scorecard;
    if (Attempt.Status == TEXT("Ended early"))
    {
        Attempt.Id = FRecallProgress::StartAttempt(Student).Id;
        Attempt.Status = TEXT("In progress");
        FRecallProgress::UpdateScore(Attempt, false);
        FRecallProgress::RecordScore(Attempt);
    }
    Elapsed = Attempt.FlightSeconds;
    CapsuleLocation = OnGround(Save.CapsuleLocation);
    Capsule->SetActorLocation(CapsuleLocation);
    RoverHeading = Save.RoverHeading;
    Rover->SetActorLocationAndRotation(OnGround(Save.RoverLocation), FRotator(0, RoverHeading, 0));
    Survey = Save.Survey;
    for (int32 Index = 0; Index < Stations.Num(); ++Index)
    {
        Stations[Index].Done = Save.StationsDone[Index];
    }
    const int32 Penalty = Attempt.Grade == TEXT("GOOD") ? 1 : Attempt.Grade == TEXT("BAD") ? 2 : 0;
    RepairsRequired = FMath::Max(0, Penalty - PanelsRepaired());
    FPilot& Pilot = Pilots[0];
    Pilot.Phase = static_cast<EPhase>(Save.Phase);
    Pilot.Position = Pilot.Phase == EPhase::Lab ? Save.LabPosition : Save.SurfacePosition;
    if (Pilot.Phase == EPhase::Rover)
    {
        RoverDriver = 0;
        Pilot.Position = Rover->GetActorLocation();
    }
    Pilot.bSuited = Save.Suited;
    Pilot.bFreeFlight = Save.FreeFlight;
    Pilot.bFirstPerson = Save.FirstPerson;
    Pilot.bJetpack = Save.Jetpack;
    Pilot.Fuel = FMath::Clamp(Save.Fuel, 0.0f, 100.0f);
    Pilot.Oxygen = FMath::Clamp(Save.Oxygen, 0.0f, 100.0f);
    Pilot.CameraYaw = Save.CameraYaw;
    Pilot.CameraPitch = Save.CameraPitch;
    Pilots[1].Position = OnGround(Save.CoopPosition);
    Pilots[1].Phase = EPhase::EVA;
    Pilots[1].bSuited = true;
    if (CoopController && Save.CoopPhase == static_cast<int32>(EPhase::Lab))
    {
        Pilots[1].Phase = EPhase::Lab;
        Pilots[1].Position = Save.CoopPosition;
    }
    else if (CoopController && RoverDriver == INDEX_NONE && Save.CoopPhase == static_cast<int32>(EPhase::Rover))
    {
        Pilots[1].Phase = EPhase::Rover;
        Pilots[1].Position = Rover->GetActorLocation();
        RoverDriver = 1;
    }
    Pilots[1].CameraYaw = Save.CoopYaw;
    Pilots[1].CameraPitch = Save.CoopPitch;
    Pilots[1].bFirstPerson = Save.CoopFirstPerson;
    bHasPlayed = true;
    UpdateSiteLocations();
    UpdateViews();
    SetScreen(Pilot.bSuited || Pilot.bFreeFlight ? ERecallScreen::Gameplay : ERecallScreen::SuitChoice);
    ShowNotice(FString::Printf(TEXT("Mission loaded for %s"), *Attempt.Student));
    return FString();
}

void ARecallMode::ChooseSuit(bool bEquip)
{
    FPilot& Pilot = Pilots[0];
    if (Pilot.Phase != EPhase::EVA)
    {
        return;
    }
    if (!bHasPlayed && !bEquip)
    {
        Pilot.Position.Z += 300;
    }
    if (!bHasPlayed)
    {
        Pilots[1].Position = OnGround(Pilot.Position + FVector(-300, -200, 0));
        Pilots[1].Phase = EPhase::EVA;
        Pilots[1].bSuited = true;
    }
    bHasPlayed = true;
    Pilot.bSuited = bEquip;
    Pilot.bFreeFlight = !bEquip;
    Pilot.bFirstPerson = false;
    Pilot.VerticalSpeed = 0;
    SetScreen(ERecallScreen::Gameplay);
    ShowNotice(bEquip ? TEXT("EVA suit equipped.") : TEXT("Wear a suit to repair the capsule and conduct research."));
    SaveMission();
}

FRecallHUDData ARecallMode::GetHUDData() const
{
    FRecallHUDData Data;
    Data.Student = Attempt.Student;
    Data.Grade = Attempt.Grade;
    Data.Status = Attempt.Status;
    Data.Points = Attempt.Points;
    Data.PanelsDone = PanelsRepaired();
    Data.PanelsRequired = Data.PanelsDone + RepairsRequired;
    Data.FlightTime = Attempt.FlightSeconds;
    Data.RepairTime = Attempt.RepairSeconds;
    Data.TotalTime = Attempt.TotalSeconds;
    Data.Impact = Attempt.ImpactSpeed;
    Data.bCompleted = Attempt.Status == TEXT("Completed");
    return Data;
}

FString ARecallMode::HandleHUDAction(ERecallAction Action, const FString& Student)
{
    switch (Action)
    {
    case ERecallAction::Start:
    case ERecallAction::Continue:
    {
        const FString Name = FRecallProgress::NormalizeStudentName(Student);
        if (Name.IsEmpty())
        {
            return TEXT("Enter a student name.");
        }
        if (Action == ERecallAction::Continue)
        {
            return RestoreMission(Name);
        }
        BeginMission(Name);
        break;
    }
    case ERecallAction::Resume:
        SetScreen(ERecallScreen::Gameplay);
        break;
    case ERecallAction::Explore:
        SetScreen(!Pilots[0].bSuited && !Pilots[0].bFreeFlight ? ERecallScreen::SuitChoice : ERecallScreen::Gameplay);
        break;
    case ERecallAction::EndAttempt:
        FinishAttempt(false);
        SaveMission();
        SetScreen(ERecallScreen::Scorecard);
        break;
    case ERecallAction::Fly:
    case ERecallAction::Equip:
        ChooseSuit(Action == ERecallAction::Equip);
        break;
    case ERecallAction::Retry:
    {
        const FString Name = Attempt.Student;
        BeginMission(Name);
        break;
    }
    case ERecallAction::MainMenu:
        FinishAttempt(false);
        SaveMission();
        ResetMission();
        SetScreen(ERecallScreen::MainMenu);
        break;
    case ERecallAction::Save:
        if (Pilots[0].Phase == EPhase::Landing)
        {
            return TEXT("Land the capsule before saving your position.");
        }
        if (!SaveMission())
        {
            return TEXT("Mission could not be saved.");
        }
        ShowNotice(FString::Printf(TEXT("Mission saved for %s"), *Attempt.Student));
        break;
    case ERecallAction::Load:
        return RestoreMission(Attempt.Student);
    case ERecallAction::ToggleFullscreen:
        if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
        {
            Settings->SetFullscreenMode(Settings->GetFullscreenMode() == EWindowMode::Windowed
                ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
            Settings->ApplySettings(false);
        }
        break;
    case ERecallAction::CycleQuality:
        if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
        {
            const int32 Quality = (Settings->GetViewDistanceQuality() + 1) % 4;
            Settings->SetViewDistanceQuality(Quality);
            Settings->ApplyNonResolutionSettings();
            Settings->SaveSettings();
            return FString::Printf(TEXT("Draw distance: %s"), Quality == 0 ? TEXT("Low")
                : Quality == 1 ? TEXT("Medium") : Quality == 2 ? TEXT("High") : TEXT("Epic"));
        }
        break;
    case ERecallAction::Exit:
        UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
        break;
    }
    return FString();
}

void ARecallMode::ShowNotice(const FString& Message)
{
    Notice = Message;
    NoticeSeconds = 7;
}

void ARecallMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    APlayerController* Player = GetWorld()->GetFirstPlayerController();
    if (!Player)
    {
        return;
    }
    if (View && Player->GetViewTarget() != View)
    {
        Player->SetViewTarget(View);
        ApplyInputMode();
    }
    const float Dt = FMath::Min(DeltaTime, 0.05f);
    const auto Pressed = [Player](const FKey& Key) { return Player->WasInputKeyJustPressed(Key); };
    if (Pressed(EKeys::Escape))
    {
        ARecallHUD* HUD = Cast<ARecallHUD>(Player->GetHUD());
        if (!HUD || !HUD->HandleBack())
        {
            if (Screen == ERecallScreen::Gameplay)
            {
                SetScreen(ERecallScreen::Pause);
            }
            else if (Screen == ERecallScreen::Pause)
            {
                SetScreen(ERecallScreen::Gameplay);
            }
        }
        return;
    }
    if (Screen == ERecallScreen::SuitChoice)
    {
        if (Attempt.Status == TEXT("In progress"))
        {
            Attempt.TotalSeconds += Dt;
            Attempt.RepairSeconds += Dt;
        }
        if (Pressed(EKeys::E) || Pressed(EKeys::F))
        {
            ChooseSuit(Pressed(EKeys::E));
        }
        return;
    }
    if (Screen == ERecallScreen::MainMenu && Pressed(EKeys::Enter))
    {
        if (ARecallHUD* HUD = Cast<ARecallHUD>(Player->GetHUD()))
        {
            HUD->OpenStartPrompt();
        }
        return;
    }
    if (Screen != ERecallScreen::Gameplay)
    {
        return;
    }
    if (Pressed(EKeys::I) && Pilots[0].Phase == EPhase::EVA)
    {
        SetScreen(ERecallScreen::SuitChoice);
        return;
    }
    if (Pressed(EKeys::F2))
    {
        ToggleSplitScreen();
    }
    if (Pressed(EKeys::F5) || Pressed(EKeys::F9))
    {
        const FString Result = HandleHUDAction(Pressed(EKeys::F5) ? ERecallAction::Save : ERecallAction::Load);
        if (!Result.IsEmpty())
        {
            ShowNotice(Result);
        }
        return;
    }
    if (Attempt.Status == TEXT("In progress"))
    {
        Attempt.TotalSeconds += Dt;
        if (RepairsRequired > 0)
        {
            Attempt.RepairSeconds += Dt;
        }
    }
    NoticeSeconds = FMath::Max(0.0f, NoticeSeconds - Dt);
    SaveTimer += Dt;
    if (SaveTimer >= 5)
    {
        SaveTimer = 0;
        SaveMission();
    }
    UpdatePilot(0, *Player, Dt);
    if (CoopController)
    {
        UpdatePilot(1, *CoopController, Dt);
    }
    else
    {
        UpdateCompanion(Dt);
    }
    if (Screen == ERecallScreen::Gameplay)
    {
        UpdateRover(Dt);
        UpdateInteraction(Dt);
        UpdateAudio(Dt);
    }
    UpdateViews();
}
