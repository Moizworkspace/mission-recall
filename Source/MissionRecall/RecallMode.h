#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RecallHUD.h"
#include "RecallProgress.h"
#include "RecallMode.generated.h"

class ACameraActor;
class UAnimSequence;
class UAudioComponent;
class USkeletalMeshComponent;
class USoundWaveProcedural;
class UStaticMeshComponent;

UCLASS()
class ARecallMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARecallMode();
    virtual void StartPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaTime) override;

    ERecallScreen GetScreen() const { return Screen; }
    FRecallHUDData GetHUDData() const;
    FString HandleHUDAction(ERecallAction Action, const FString& Student = FString());
    FString Readout(FString& Controls, float& Progress, bool bSecondPlayer = false) const;
    void GetWorldMarkers(TArray<TPair<FVector, FString>>& Markers, bool bSecondPlayer = false) const;

private:
    enum class EPhase { Briefing, Landing, EVA, Rover, Lab };
    struct FStation
    {
        FString Name;
        FVector Position;
        float Radius;
        float Duration;
        bool Done = false;
    };
    struct FPilot
    {
        FVector Position = FVector::ZeroVector;
        EPhase Phase = EPhase::Briefing;
        float CameraYaw = 20;
        float CameraPitch = -15;
        float BodyYaw = 0;
        float VerticalSpeed = 0;
        float StepDistance = 0;
        float Fuel = 100;
        float Oxygen = 100;
        bool bSuited = false;
        bool bFreeFlight = false;
        bool bFirstPerson = false;
        bool bJetpack = false;
        bool bMoving = false;
        bool bInteractHeld = false;
        bool bInteractPressed = false;
        float DriveForward = 0;
        float DriveRight = 0;
        bool bBrake = false;
        int32 NearbyStation = INDEX_NONE;
    };

    static constexpr float LandingDeadline = 35;
    static constexpr int32 FirstExperiment = 2;
    static constexpr int32 Airlock = 5;
    static constexpr int32 FirstLabExperiment = 6;
    static constexpr int32 LabExit = 8;
    inline static const FVector LabCenter{0, 0, 15000};
    inline static const FVector LabBounds{470, 330, 0};
    inline static const FVector SiteBounds{299000, 299000, 8000};

    AActor* NewBody(const FVector& Position);
    UStaticMeshComponent* Shape(AActor* Body, bool Round, const FVector& Position, const FVector& Scale);
    void BuildSite();
    void EnsureSite();
    void BuildTerrain(AActor* Terrain);
    void BuildAvatarAsset();
    void BuildCompanionAppearance();
    void UpdateSiteLocations();
    void UpdateResearchVisuals();
    float GroundAt(double X, double Y) const;
    FVector OnGround(FVector Position) const;
    FVector StationLocation(int32 Index) const;
    void AnimateAvatar(int32 Index, bool bWorking);

    void SetScreen(ERecallScreen NewScreen);
    void ApplyInputMode();
    void ResetMission();
    void BeginMission(const FString& Student);
    void FinishAttempt(bool bCompleted);
    bool SaveMission();
    FString RestoreMission(const FString& Student);
    void ChooseSuit(bool bEquip);
    void ToggleSplitScreen();
    void UpdateLanding(float Dt, float Forward, float Right, bool bFast);
    void UpdatePilot(int32 Index, APlayerController& Player, float Dt);
    void UpdateRover(float Dt);
    void UpdateCompanion(float Dt);
    void UpdateViews();
    void UpdateView(ACameraActor& Camera, int32 Index);
    void FindInteraction(FPilot& Pilot) const;
    void UpdateInteraction(float Dt);
    bool AllOutdoorResearchDone() const;
    bool AllResearchDone() const;
    int32 PanelsRepaired() const;
    FString Checklist() const;
    void ShowNotice(const FString& Text);
    void InitAudio();
    void UpdateAudio(float Dt);
    USoundWaveProcedural* MakeTone(float Hz, float Seconds, int32 Kind, int32 Index);
    void PlayTone(int32 Index, float Volume);

    UPROPERTY()
    TObjectPtr<ACameraActor> View;
    UPROPERTY()
    TObjectPtr<ACameraActor> CoopView;
    UPROPERTY()
    TObjectPtr<APlayerController> CoopController;
    UPROPERTY()
    TObjectPtr<AActor> Capsule;
    UPROPERTY()
    TObjectPtr<AActor> Rover;
    UPROPERTY()
    TObjectPtr<AActor> Astronaut;
    UPROPERTY()
    TObjectPtr<AActor> Companion;
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> AstronautMesh;
    UPROPERTY()
    TObjectPtr<UAnimSequence> AstronautIdle;
    UPROPERTY()
    TObjectPtr<UAnimSequence> AstronautWalk;
    UPROPERTY()
    TObjectPtr<UAnimSequence> AstronautFloat;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> PrimaryLegs;
    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> CompanionHips;
    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> CompanionKnees;
    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> CompanionAnkles;
    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> CompanionShoulders;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> RoverWheels;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> PlantPetals;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PlantStem;
    UPROPERTY()
    TArray<TObjectPtr<AActor>> OutdoorStations;
    UPROPERTY()
    TArray<TObjectPtr<USoundWaveProcedural>> Tones;
    UPROPERTY()
    TArray<TObjectPtr<UAudioComponent>> AudioPlayers;

    ERecallScreen Screen = ERecallScreen::MainMenu;
    FPilot Pilots[2];
    FRecallScorecard Attempt;
    TArray<FStation> Stations;
    TArray<TPair<FVector, float>> Rocks;
    TArray<TArray<uint8>> TonePCM;
    FVector CapsuleLocation = FVector(0, 0, 6500);
    FVector2D LateralVelocity = FVector2D::ZeroVector;
    FVector2D LandingMouseStick = FVector2D::ZeroVector;
    FString Notice;
    float Elapsed = 0;
    float Descent = 0;
    float Tilt = 0;
    float Hold = 0;
    float RoverSpeed = 0;
    float RoverHeading = 0;
    float Survey = 0;
    float SaveTimer = 0;
    float NoticeSeconds = 0;
    float FootstepTimer = 0;
    int32 ActiveStation = INDEX_NONE;
    int32 ActivePilot = INDEX_NONE;
    int32 RoverDriver = INDEX_NONE;
    int32 RepairsRequired = 0;
    bool bHasPlayed = false;
};
