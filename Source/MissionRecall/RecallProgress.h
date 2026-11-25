#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RecallProgress.generated.h"

USTRUCT()
struct FRecallScorecard
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString Id;
    UPROPERTY(SaveGame) FString Student;
    UPROPERTY(SaveGame) FString Grade = TEXT("UNRATED");
    UPROPERTY(SaveGame) FString Status = TEXT("In progress");
    UPROPERTY(SaveGame) FString UpdatedUtc;
    UPROPERTY(SaveGame) float FlightSeconds = 0;
    UPROPERTY(SaveGame) float RepairSeconds = 0;
    UPROPERTY(SaveGame) float TotalSeconds = 0;
    UPROPERTY(SaveGame) float ImpactSpeed = 0;
    UPROPERTY(SaveGame) int32 RepairPanels = 0;
    UPROPERTY(SaveGame) int32 Points = 0;
};

USTRUCT()
struct FRecallSnapshot
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FRecallScorecard Scorecard;
    UPROPERTY(SaveGame) FVector CapsuleLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector SurfacePosition = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector CoopPosition = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector LabPosition = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector RoverLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) float RoverHeading = 0;
    UPROPERTY(SaveGame) float Survey = 0;
    UPROPERTY(SaveGame) float CameraYaw = 20;
    UPROPERTY(SaveGame) float CameraPitch = -12;
    UPROPERTY(SaveGame) float Fuel = 100;
    UPROPERTY(SaveGame) float Oxygen = 100;
    UPROPERTY(SaveGame) float CoopYaw = 20;
    UPROPERTY(SaveGame) float CoopPitch = -12;
    UPROPERTY(SaveGame) int32 Phase = 2;
    UPROPERTY(SaveGame) int32 CoopPhase = 2;
    UPROPERTY(SaveGame) TArray<bool> StationsDone;
    UPROPERTY(SaveGame) bool Suited = false;
    UPROPERTY(SaveGame) bool FreeFlight = false;
    UPROPERTY(SaveGame) bool FirstPerson = false;
    UPROPERTY(SaveGame) bool Jetpack = false;
    UPROPERTY(SaveGame) bool CoopFirstPerson = false;
};

UCLASS()
class URecallMissionSave : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame) FRecallSnapshot Snapshot;
};

class FRecallProgress
{
public:
    static FString NormalizeStudentName(const FString& Name);
    static FRecallScorecard StartAttempt(const FString& Student);
    static void UpdateScore(FRecallScorecard& Scorecard, bool bCompleted);
    static bool RecordScore(FRecallScorecard& Scorecard);
    static TArray<FRecallScorecard> ReadScores();
    static bool Save(const FRecallSnapshot& Snapshot);
    static bool Load(const FString& Student, FRecallSnapshot& Snapshot);
};
