#include "RecallMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

void ARecallMode::UpdateLanding(float Dt, float Forward, float Right, bool bFast)
{
    Elapsed += Dt;
    Attempt.FlightSeconds = Elapsed;
    const float Height = FMath::Max(0.0, (CapsuleLocation.Z - GroundAt(CapsuleLocation.X, CapsuleLocation.Y)) / 100);
    const float Remaining = LandingDeadline - Elapsed;
    const float Required = Height / FMath::Max(0.05f, Remaining);
    float Guided = FMath::Max(0.65f, 1.85f * Height / FMath::Max(0.2f, Remaining));
    if (Height < 6)
    {
        Guided = FMath::Min(Guided, 1.3f);
    }
    Descent = FMath::Max(bFast ? 5.2f : Guided, Required);
    LateralVelocity = FMath::Lerp(LateralVelocity, FVector2D(Forward * 9, Right * 7), 1 - FMath::Exp(-5 * Dt));
    Tilt = FMath::Lerp(Tilt, Forward * 0.27f, 1 - FMath::Exp(-6 * Dt));
    CapsuleLocation += FVector(LateralVelocity.X, LateralVelocity.Y, -Descent) * (Dt * 100);
    const float Ground = GroundAt(CapsuleLocation.X, CapsuleLocation.Y);
    if (CapsuleLocation.Z <= Ground + FMath::Sin(FMath::Abs(Tilt)) * 650 || Remaining <= 0)
    {
        Attempt.ImpactSpeed = Descent;
        const double Lateral = LateralVelocity.Size();
        const float Pitch = FMath::Abs(Tilt);
        if (Descent <= 1.8f && Lateral <= 2.5 && Pitch <= 0.12f)
        {
            Attempt.Grade = TEXT("PERFECT");
        }
        else if (Descent <= 3.2f && Lateral <= 4.5 && Pitch <= 0.25f)
        {
            Attempt.Grade = TEXT("GOOD");
            RepairsRequired = 1;
        }
        else
        {
            Attempt.Grade = TEXT("BAD");
            RepairsRequired = 2;
        }
        CapsuleLocation.Z = Ground;
        Tilt = 0;
        Pilots[0].Position = OnGround(CapsuleLocation + FVector(-900, -600, 0));
        Pilots[1].Position = OnGround(CapsuleLocation + FVector(-1200, -800, 0));
        Pilots[0].Phase = Pilots[1].Phase = EPhase::EVA;
        Pilots[1].bSuited = true;
        Pilots[0].CameraYaw = 38;
        Pilots[0].CameraPitch = -12;
        Rover->SetActorLocation(OnGround(CapsuleLocation + FVector(1300, -800, 0)));
        UpdateSiteLocations();
        if (RepairsRequired == 0)
        {
            FinishAttempt(true);
            SetScreen(ERecallScreen::Scorecard);
        }
        else
        {
            SetScreen(ERecallScreen::SuitChoice);
        }
        SaveMission();
        PlayTone(3, 0.45f);
    }
    Capsule->SetActorLocationAndRotation(CapsuleLocation, FRotator(FMath::RadiansToDegrees(Tilt), 0, 0));
}

void ARecallMode::UpdatePilot(int32 Index, APlayerController& Player, float Dt)
{
    FPilot& Pilot = Pilots[Index];
    const auto Down = [&Player](const FKey& Key) { return Player.IsInputKeyDown(Key); };
    const auto Pressed = [&Player](const FKey& Key) { return Player.WasInputKeyJustPressed(Key); };
    const bool bGamepad = Index == 1;
    const float Forward = bGamepad ? Player.GetInputAnalogKeyState(EKeys::Gamepad_LeftY) : float(Down(EKeys::W));
    const float Right = bGamepad ? Player.GetInputAnalogKeyState(EKeys::Gamepad_LeftX)
        : float(Down(EKeys::D)) - float(Down(EKeys::Q) || Down(EKeys::A));
    Pilot.DriveForward = FMath::Max(0.0f, Forward);
    Pilot.DriveRight = Right;
    Pilot.bBrake = bGamepad ? Forward < -0.35f : Down(EKeys::S);
    Pilot.bInteractHeld = Down(bGamepad ? EKeys::Gamepad_FaceButton_Left : EKeys::E);
    Pilot.bInteractPressed = Pressed(bGamepad ? EKeys::Gamepad_FaceButton_Left : EKeys::E);
    const FKey Jump = bGamepad ? EKeys::Gamepad_FaceButton_Bottom : EKeys::SpaceBar;
    const FKey Jetpack = bGamepad ? EKeys::Gamepad_FaceButton_Top : EKeys::F;
    const FKey ViewKey = bGamepad ? EKeys::Gamepad_Special_Left : EKeys::V;
    float MouseX = 0;
    float MouseY = 0;
    if (bGamepad)
    {
        MouseX = Player.GetInputAnalogKeyState(EKeys::Gamepad_RightX) * Dt * 500;
        MouseY = Player.GetInputAnalogKeyState(EKeys::Gamepad_RightY) * Dt * 500;
    }
    else
    {
        Player.GetInputMouseDelta(MouseX, MouseY);
    }
    const bool bMouseSteering = !bGamepad && Pilot.Phase == EPhase::Landing && Down(EKeys::RightMouseButton);
    if (!bMouseSteering)
    {
        Pilot.CameraYaw = FMath::UnwindDegrees(Pilot.CameraYaw + MouseX * 0.23f);
        const float MinPitch = Pilot.Phase == EPhase::Landing || Pilot.Phase == EPhase::Rover ? -52 : -75;
        const float MaxPitch = Pilot.Phase == EPhase::Landing || Pilot.Phase == EPhase::Rover ? 7 : 72;
        Pilot.CameraPitch = FMath::Clamp(Pilot.CameraPitch - MouseY * 0.23f, MinPitch, MaxPitch);
    }
    if (Pressed(ViewKey))
    {
        if (Pilot.Phase == EPhase::Landing || Pilot.Phase == EPhase::Rover)
        {
            Pilot.CameraYaw = 0;
            Pilot.CameraPitch = -15;
        }
        else
        {
            Pilot.bFirstPerson = !Pilot.bFirstPerson;
        }
    }
    if (Pilot.Phase == EPhase::Landing)
    {
        if (Index == 0)
        {
            LandingMouseStick = bMouseSteering ? FVector2D(
                FMath::Clamp(LandingMouseStick.X + MouseX * 0.018, -1.0, 1.0),
                FMath::Clamp(LandingMouseStick.Y - MouseY * 0.018, -1.0, 1.0)) : FVector2D::ZeroVector;
            const float ForeInput = FMath::Clamp(Forward + float(LandingMouseStick.Y), -1.0f, 1.0f);
            const float SideInput = FMath::Clamp(Right + float(LandingMouseStick.X), -1.0f, 1.0f);
            UpdateLanding(Dt, ForeInput, SideInput, Pilot.bBrake || Down(EKeys::LeftMouseButton));
        }
        return;
    }
    if (Pilot.Phase == EPhase::Rover)
    {
        if (Pilot.bInteractPressed)
        {
            const FRotationMatrix Heading(FRotator(0, RoverHeading, 0));
            Pilot.Position = OnGround(Rover->GetActorLocation() + Heading.GetUnitAxis(EAxis::Y) * 260);
            Pilot.CameraYaw = FMath::UnwindDegrees(RoverHeading + Pilot.CameraYaw);
            Pilot.Phase = EPhase::EVA;
            RoverDriver = INDEX_NONE;
            RoverSpeed = 0;
            SaveMission();
        }
        return;
    }
    if (Pilot.Phase == EPhase::Briefing)
    {
        return;
    }
    if (!Pilot.bSuited && !Pilot.bFreeFlight)
    {
        return;
    }
    if (!Pilot.bSuited && Pilot.bInteractPressed)
    {
        SetScreen(ERecallScreen::SuitChoice);
        return;
    }

    const bool bInLab = Pilot.Phase == EPhase::Lab;
    const FRotationMatrix Facing(FRotator(0, Pilot.CameraYaw, 0));
    const float WalkForward = bGamepad ? Forward : Forward - float(Pilot.bBrake);
    const FVector Walk = (Facing.GetUnitAxis(EAxis::X) * WalkForward +
        Facing.GetUnitAxis(EAxis::Y) * Right).GetClampedToMaxSize(1);
    const bool bCrouching = Down(bGamepad ? EKeys::Gamepad_FaceButton_Right : EKeys::C);
    const float Speed = Pilot.bSuited ? (bCrouching ? 100 : 165) : 500;
    const FVector Previous = Pilot.Position;
    FVector Next = Previous + Walk * (Speed * Dt);
    Pilot.bMoving = !Walk.IsNearlyZero(0.05);
    if (bInLab)
    {
        Next.X = FMath::Clamp(Next.X, LabCenter.X - LabBounds.X, LabCenter.X + LabBounds.X);
        Next.Y = FMath::Clamp(Next.Y, LabCenter.Y - LabBounds.Y, LabCenter.Y + LabBounds.Y);
    }
    else
    {
        Next.X = FMath::Clamp(Next.X, -SiteBounds.X, SiteBounds.X);
        Next.Y = FMath::Clamp(Next.Y, -SiteBounds.Y, SiteBounds.Y);
        FVector Radial(Next.X - CapsuleLocation.X, Next.Y - CapsuleLocation.Y, 0);
        if (Next.Z < CapsuleLocation.Z + 815 && Radial.SizeSquared() < FMath::Square(660.0))
        {
            const FVector Outside = CapsuleLocation + Radial.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector) * 660;
            Next.X = Outside.X;
            Next.Y = Outside.Y;
        }
        for (const TPair<FVector, float>& Rock : Rocks)
        {
            if (Next.Z > Rock.Key.Z + Rock.Value)
            {
                continue;
            }
            Radial = FVector(Next.X - Rock.Key.X, Next.Y - Rock.Key.Y, 0);
            if (Radial.SizeSquared() < FMath::Square(Rock.Value + 35))
            {
                const FVector Outside = Rock.Key + Radial.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector) * (Rock.Value + 35);
                Next.X = Outside.X;
                Next.Y = Outside.Y;
            }
        }

        const float Ground = GroundAt(Next.X, Next.Y);
        if (Pilot.bSuited)
        {
            if (Pressed(Jetpack))
            {
                Pilot.bJetpack = !Pilot.bJetpack;
            }
            const bool bWasGrounded = FMath::Abs(Previous.Z - GroundAt(Previous.X, Previous.Y)) < 2;
            const bool bJump = bWasGrounded && Pressed(Jump);
            const float Thrust = bGamepad ? Player.GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) : float(Down(Jump));
            const bool bThrusting = Pilot.bJetpack && Pilot.Fuel > 0 && Thrust > 0.1f;
            if (bJump)
            {
                Pilot.VerticalSpeed = 240;
            }
            if (bThrusting)
            {
                Pilot.VerticalSpeed += 420 * Thrust * Dt;
                Pilot.Fuel = FMath::Max(0.0f, Pilot.Fuel - 6 * Dt);
            }
            else if (bWasGrounded)
            {
                Pilot.Fuel = FMath::Min(100.0f, Pilot.Fuel + 2 * Dt);
            }
            Pilot.VerticalSpeed = FMath::Min(Pilot.VerticalSpeed - 162 * Dt, 700.0f);
            Next.Z += Pilot.VerticalSpeed * Dt;
            if (Next.Z <= Ground || (bWasGrounded && !bJump && !bThrusting && FMath::Abs(Previous.Z - Ground) < 42))
            {
                Next.Z = Ground;
                Pilot.VerticalSpeed = 0;
            }
            Pilot.Oxygen = FMath::Max(0.0f, Pilot.Oxygen - 0.012f * Dt);
        }
        else
        {
            const float Up = float(Down(Jump)) - float(Down(EKeys::LeftControl) || Pilot.bBrake);
            const float Aim = FMath::Sin(FMath::DegreesToRadians(Pilot.CameraPitch)) * FMath::Max(0.0f, WalkForward);
            Next.Z = FMath::Clamp(Previous.Z + (Up * 400 + Aim * 500) * Dt, double(Ground) + 35, double(Ground) + SiteBounds.Z);
        }
        Pilot.bMoving |= Next.Z > Ground + 15;
    }
    Pilot.Position = Next;
    if (!Walk.IsNearlyZero(0.05))
    {
        Pilot.BodyYaw = FMath::FixedTurn(Pilot.BodyYaw, Walk.Rotation().Yaw, 560 * Dt);
        Pilot.StepDistance += FVector::Dist2D(Previous, Next);
    }
    FindInteraction(Pilot);
    if (!bInLab && Pilot.bSuited && RepairsRequired == 0 && Pilot.bInteractPressed &&
        Pilot.NearbyStation == INDEX_NONE && RoverDriver == INDEX_NONE &&
        FVector::Dist(Pilot.Position, Rover->GetActorLocation()) <= 450)
    {
        Pilot.Phase = EPhase::Rover;
        Pilot.CameraYaw = 0;
        Pilot.CameraPitch = -15;
        RoverDriver = Index;
        SaveMission();
    }
}

void ARecallMode::UpdateRover(float Dt)
{
    if (RoverDriver == INDEX_NONE)
    {
        return;
    }
    FPilot& Pilot = Pilots[RoverDriver];
    const float Target = Pilot.bBrake ? 0 : 850 * Pilot.DriveForward;
    const float Rate = Pilot.bBrake ? 600 : Pilot.DriveForward > 0 ? 190 : 90;
    RoverSpeed = FMath::FInterpConstantTo(RoverSpeed, Target, Dt, Rate);
    RoverHeading = FMath::UnwindDegrees(RoverHeading + Pilot.DriveRight * Dt * 62 * (RoverSpeed / 850));
    const FRotator Heading(0, RoverHeading, 0);
    const FVector Previous = Rover->GetActorLocation();
    FVector Next = Previous + Heading.Vector() * (RoverSpeed * Dt);
    Next.X = FMath::Clamp(Next.X, -SiteBounds.X, SiteBounds.X);
    Next.Y = FMath::Clamp(Next.Y, -SiteBounds.Y, SiteBounds.Y);
    if (FVector::DistSquared2D(Next, CapsuleLocation) < FMath::Square(780.0))
    {
        Next = Previous;
        RoverSpeed = 0;
    }
    Next = OnGround(Next);
    const float Distance = FVector::Dist2D(Next, Previous);
    Rover->SetActorLocationAndRotation(Next, Heading);
    Pilot.Position = Next;
    const float PreviousSurvey = Survey;
    Survey += Distance / 100;
    if (PreviousSurvey < 100 && Survey >= 100)
    {
        ShowNotice(TEXT("Rover survey complete. Drive back to the capsule when the surface experiments are finished."));
        SaveMission();
    }
    for (UStaticMeshComponent* Wheel : RoverWheels)
    {
        Wheel->AddLocalRotation(FRotator(0, FMath::RadiansToDegrees(Distance / 37), 0));
    }
}

void ARecallMode::ToggleSplitScreen()
{
    if (CoopController)
    {
        if (RoverDriver == 1)
        {
            RoverDriver = INDEX_NONE;
            RoverSpeed = 0;
        }
        UGameplayStatics::RemovePlayer(CoopController, true);
        CoopController = nullptr;
        if (CoopView)
        {
            CoopView->Destroy();
            CoopView = nullptr;
        }
        if (bHasPlayed)
        {
            Pilots[1].Phase = EPhase::EVA;
            Pilots[1].Position = OnGround(CapsuleLocation + FVector(-1200, -800, 0));
        }
        ApplyInputMode();
        return;
    }
    CoopController = UGameplayStatics::CreatePlayer(this, 1, true);
    if (!CoopController)
    {
        ShowNotice(TEXT("Could not start a second local player."));
        return;
    }
    CoopView = GetWorld()->SpawnActor<ACameraActor>();
    CoopView->GetCameraComponent()->PostProcessSettings = View->GetCameraComponent()->PostProcessSettings;
    CoopController->SetViewTarget(CoopView);
    ApplyInputMode();
    ShowNotice(TEXT("Split-screen enabled. Player two uses the gamepad."));
}

void ARecallMode::UpdateCompanion(float Dt)
{
    FPilot& Pilot = Pilots[1];
    Pilot.bMoving = false;
    if (!bHasPlayed || Pilots[0].Phase == EPhase::Lab)
    {
        return;
    }
    FVector Direction = Pilots[0].Position - Pilot.Position;
    Direction.Z = 0;
    if (Direction.SizeSquared() <= FMath::Square(400.0))
    {
        return;
    }
    const FVector Move = Direction.GetSafeNormal() * Dt * 165;
    FVector Target = Pilot.Position + Move;
    if (FVector::DistSquared2D(Target, CapsuleLocation) < FMath::Square(775.0))
    {
        const FVector Escape = (Pilot.Position - CapsuleLocation).GetSafeNormal2D();
        Target = Pilot.Position + FVector::CrossProduct(Escape, FVector::UpVector) * Dt * 165;
    }
    Pilot.BodyYaw = FMath::FixedTurn(Pilot.BodyYaw, (Target - Pilot.Position).Rotation().Yaw, 460 * Dt);
    Pilot.StepDistance += FVector::Dist2D(Target, Pilot.Position);
    Pilot.Position = OnGround(Target);
    Pilot.bMoving = true;
}

void ARecallMode::FindInteraction(FPilot& Pilot) const
{
    Pilot.NearbyStation = INDEX_NONE;
    if (!Pilot.bSuited || (Pilot.Phase != EPhase::EVA && Pilot.Phase != EPhase::Lab))
    {
        return;
    }
    const bool bInLab = Pilot.Phase == EPhase::Lab;
    double NearestDistance = UE_DOUBLE_BIG_NUMBER;
    for (int32 Index = bInLab ? FirstLabExperiment : 0; Index < (bInLab ? Stations.Num() : FirstLabExperiment); ++Index)
    {
        const FStation& Station = Stations[Index];
        const bool bRepair = Index < FirstExperiment;
        if (Station.Done || bRepair != (RepairsRequired > 0) || (Index == Airlock && !AllOutdoorResearchDone()))
        {
            continue;
        }
        const double Distance = FVector::Dist(StationLocation(Index), Pilot.Position);
        if (Distance <= Station.Radius && Distance < NearestDistance)
        {
            NearestDistance = Distance;
            Pilot.NearbyStation = Index;
        }
    }
}

void ARecallMode::UpdateInteraction(float Dt)
{
    const int32 NumPlayers = CoopController ? 2 : 1;
    for (int32 PlayerIndex = 0; PlayerIndex < NumPlayers; ++PlayerIndex)
    {
        FPilot& Pilot = Pilots[PlayerIndex];
        const int32 Index = Pilot.NearbyStation;
        if ((Index == Airlock || Index == LabExit) && Pilot.bInteractPressed)
        {
            Pilot.Phase = Index == Airlock ? EPhase::Lab : EPhase::EVA;
            Pilot.Position = Index == Airlock ? LabCenter + FVector(-320, 0, 0)
                : OnGround(CapsuleLocation + FVector(-900, 0, 0));
            Pilot.bFirstPerson = Index == Airlock;
            Pilot.CameraYaw = 0;
            Pilot.CameraPitch = -4;
            Pilot.NearbyStation = INDEX_NONE;
            SaveMission();
        }
    }

    int32 Worker = INDEX_NONE;
    for (int32 Index = 0; Index < NumPlayers; ++Index)
    {
        const FPilot& Pilot = Pilots[Index];
        if ((Pilot.Phase == EPhase::EVA || Pilot.Phase == EPhase::Lab) && Pilot.bSuited && Pilot.bInteractHeld &&
            !Pilot.bMoving && Pilot.NearbyStation != INDEX_NONE && Stations[Pilot.NearbyStation].Duration > 0)
        {
            Worker = Index;
            if (Worker == ActivePilot)
            {
                break;
            }
        }
    }
    const int32 StationIndex = Worker == INDEX_NONE ? INDEX_NONE : Pilots[Worker].NearbyStation;
    if (StationIndex != ActiveStation || Worker != ActivePilot)
    {
        Hold = 0;
    }
    ActiveStation = StationIndex;
    ActivePilot = Worker;
    if (Worker == INDEX_NONE)
    {
        return;
    }
    FPilot& Pilot = Pilots[Worker];
    Pilot.BodyYaw = (StationLocation(StationIndex) - Pilot.Position).Rotation().Yaw;
    FStation& Station = Stations[StationIndex];
    Hold += Dt;
    if (Hold + KINDA_SMALL_NUMBER < Station.Duration)
    {
        return;
    }
    Station.Done = true;
    Pilot.NearbyStation = ActiveStation = ActivePilot = INDEX_NONE;
    Hold = 0;
    if (StationIndex < FirstExperiment && --RepairsRequired == 0)
    {
        FinishAttempt(true);
        SetScreen(ERecallScreen::Scorecard);
    }
    else if (StationIndex >= FirstExperiment)
    {
        const TCHAR* Finding = TEXT("Habitat pressure, oxygen and water recycling checks passed.");
        switch (StationIndex)
        {
        case 2: Finding = TEXT("Regolith sample sealed. Grain sizes and mineral content recorded."); break;
        case 3: Finding = TEXT("Seismometer deployed. Vibration baseline recorded."); break;
        case 4: Finding = TEXT("Lander calibrated. Radiation and temperature logged."); break;
        case 6: Finding = TEXT("Plant study complete. Roots need water, nutrients and controlled light."); break;
        }
        ShowNotice(AllResearchDone() ? TEXT("Mission complete. All lunar research logged.") : Finding);
    }
    PlayTone(3, 0.45f);
    SaveMission();
}

bool ARecallMode::AllOutdoorResearchDone() const
{
    return Stations[2].Done && Stations[3].Done && Stations[4].Done && Survey >= 100;
}

bool ARecallMode::AllResearchDone() const
{
    return AllOutdoorResearchDone() && Stations[6].Done && Stations[7].Done;
}

int32 ARecallMode::PanelsRepaired() const
{
    return Stations.Num() >= FirstExperiment ? int32(Stations[0].Done) + int32(Stations[1].Done) : 0;
}

void ARecallMode::UpdateViews()
{
    UpdateResearchVisuals();
    const bool bFirstOnSurface = Pilots[0].Phase == EPhase::EVA && bHasPlayed;
    const bool bSecondOnSurface = Pilots[1].Phase == EPhase::EVA && bHasPlayed;
    Astronaut->SetActorHiddenInGame(!bFirstOnSurface);
    Companion->SetActorHiddenInGame(!bSecondOnSurface);
    APlayerController* Controllers[] = {GetWorld()->GetFirstPlayerController(), CoopController.Get()};
    AActor* Avatars[] = {Astronaut.Get(), Companion.Get()};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        AnimateAvatar(Index, ActivePilot == Index && Hold > 0);
        if (APlayerController* Controller = Controllers[Index])
        {
            if (Pilots[Index].bFirstPerson)
            {
                Controller->HiddenActors.AddUnique(Avatars[Index]);
            }
            else
            {
                Controller->HiddenActors.Remove(Avatars[Index]);
            }
        }
    }
    if (View)
    {
        UpdateView(*View, 0);
    }
    if (CoopView)
    {
        UpdateView(*CoopView, 1);
    }
}

void ARecallMode::UpdateView(ACameraActor& Camera, int32 Index)
{
    const FPilot& Pilot = Pilots[Index];
    const bool bDriving = Pilot.Phase == EPhase::Rover;
    const bool bInLab = Pilot.Phase == EPhase::Lab;
    const bool bOnFoot = bInLab || Pilot.Phase == EPhase::EVA;
    const bool bEyes = bOnFoot && (Pilot.bFirstPerson || bInLab);
    const FRotator Look(Pilot.CameraPitch, Pilot.CameraYaw + (bDriving ? RoverHeading : 0), 0);
    FVector Focus = CapsuleLocation + FVector(0, 0, 390);
    float Arm = 2100;
    if (bDriving)
    {
        Focus = Rover->GetActorLocation() + FVector(0, 0, 160);
        Arm = 780;
    }
    else if (bOnFoot)
    {
        Focus = Pilot.Position + FVector(0, 0, ActivePilot == Index && Hold > 0 ? 122 : 166);
        Arm = bEyes ? 0 : 495;
    }
    FVector CameraPosition = Focus - Look.Vector() * Arm;
    if (!bInLab)
    {
        CameraPosition.Z = FMath::Max(CameraPosition.Z, GroundAt(CameraPosition.X, CameraPosition.Y) + 55);
    }
    Camera.GetCameraComponent()->SetFieldOfView(bEyes ? 88 : 78);
    Camera.SetActorLocationAndRotation(CameraPosition, Arm > 0 ? (Focus - CameraPosition).Rotation() : Look);
}

FString ARecallMode::Checklist() const
{
    if (RepairsRequired > 0)
    {
        return FString::Printf(TEXT("Service panels remaining: %d"), RepairsRequired);
    }
    if (AllResearchDone())
    {
        return TEXT("All lunar research complete");
    }
    const int32 Outdoor = int32(Stations[2].Done) + int32(Stations[3].Done) + int32(Stations[4].Done);
    const int32 Indoor = int32(Stations[6].Done) + int32(Stations[7].Done);
    return FString::Printf(TEXT("Surface experiments %d/3     Rover survey %.0f/100 m     Lab experiments %d/2"),
        Outdoor, FMath::Min(Survey, 100.0f), Indoor);
}

FString ARecallMode::Readout(FString& Controls, float& Progress, bool bSecondPlayer) const
{
    Progress = -1;
    const FPilot& Pilot = Pilots[bSecondPlayer ? 1 : 0];
    if (Pilot.Phase == EPhase::Briefing)
    {
        Controls = FString();
        return TEXT("LUNAR LANDING TRAINING");
    }
    if (Pilot.Phase == EPhase::Landing)
    {
        Controls = bSecondPlayer ? TEXT("Right stick: orbit") :
            TEXT("Mouse: orbit   Right mouse: steer   Left mouse or S: fast descent   W/Q/D: steer");
        const double Altitude = (CapsuleLocation.Z - GroundAt(CapsuleLocation.X, CapsuleLocation.Y)) / 100;
        const FString Flight = FString::Printf(TEXT("Altitude %5.1f m   Descent %4.1f m/s   Lateral %4.1f m/s   %4.1f s left"),
            Altitude, Descent, LateralVelocity.Size(), FMath::Max(0.0f, LandingDeadline - Elapsed));
        return NoticeSeconds > 0 ? Flight + TEXT("\n") + Notice : Flight;
    }
    if (Pilot.Phase == EPhase::Rover)
    {
        Controls = bSecondPlayer ? TEXT("Left stick: drive   X: exit   Right stick: orbit") :
            TEXT("W: accelerate   Q/D: steer   S: brake   E: exit   Mouse: orbit   V: reset camera");
        return NoticeSeconds > 0 ? Checklist() + TEXT("\n") + Notice : Checklist();
    }
    Controls = bSecondPlayer ? TEXT("Left stick: move   Right stick: look   A: jump   X: interact   Y: jetpack") :
        TEXT("W/Q/S/D: move   Space: jump   I: suit   F: jetpack   V: camera   E: interact");
    if (!Pilot.bSuited)
    {
        Controls = TEXT("W/Q/D: fly   Space: rise   S or Left Ctrl: descend   I: suit   V: camera");
    }
    FString Objective = Checklist();
    if (Pilot.NearbyStation != INDEX_NONE)
    {
        const FStation& Station = Stations[Pilot.NearbyStation];
        Objective += FString::Printf(TEXT("\n%s %s: %s"), Station.Duration > 0 ? TEXT("Hold") : TEXT("Press"),
            bSecondPlayer ? TEXT("X") : TEXT("E"), *Station.Name);
        if (Station.Duration > 0)
        {
            Progress = ActivePilot == int32(bSecondPlayer) ? Hold / Station.Duration : 0;
        }
    }
    else if (Pilot.bFreeFlight)
    {
        Objective += TEXT("\nWear a suit to repair the capsule and conduct research.");
    }
    else if (NoticeSeconds > 0)
    {
        Objective += TEXT("\n") + Notice;
    }
    FString Equipment;
    if (Pilot.bSuited && Pilot.Phase != EPhase::Lab)
    {
        Equipment = FString::Printf(TEXT("   O2 %.0f%%   Jetpack %s %.0f%%"), Pilot.Oxygen,
            Pilot.bJetpack ? TEXT("ON") : TEXT("OFF"), Pilot.Fuel);
    }
    return FString::Printf(TEXT("%s landing   Impact %.1f m/s   %d points%s\n%s"),
        *Attempt.Grade, Attempt.ImpactSpeed, Attempt.Points, *Equipment, *Objective);
}

void ARecallMode::GetWorldMarkers(TArray<TPair<FVector, FString>>& Markers, bool bSecondPlayer) const
{
    const FPilot& Pilot = Pilots[bSecondPlayer ? 1 : 0];
    if (Pilot.Phase == EPhase::Landing || Pilot.Phase == EPhase::Briefing)
    {
        return;
    }
    const bool bInLab = Pilot.Phase == EPhase::Lab;
    const int32 Begin = bInLab ? FirstLabExperiment : RepairsRequired > 0 ? 0 : FirstExperiment;
    const int32 End = bInLab ? Stations.Num() : RepairsRequired > 0 ? FirstExperiment : FirstLabExperiment;
    for (int32 Index = Begin; Index < End; ++Index)
    {
        if (!Stations[Index].Done && (Index != Airlock || AllOutdoorResearchDone()))
        {
            const FVector Location = StationLocation(Index);
            const int32 Meters = FMath::RoundToInt(FVector::Dist2D(Pilot.Position, Location) / 100);
            Markers.Emplace(Location + FVector(0, 0, bInLab ? 160 : 260),
                FString::Printf(TEXT("%s  %dm"), *Stations[Index].Name, Meters));
        }
    }
    if (!bInLab && RepairsRequired == 0 && Pilot.Phase != EPhase::Rover)
    {
        Markers.Emplace(Rover->GetActorLocation() + FVector(0, 0, 240), TEXT("Lunar rover"));
    }
}
