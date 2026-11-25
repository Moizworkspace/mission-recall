#include "RecallProgress.h"

#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include <iomanip>
#include <locale>
#include <sstream>

namespace
{
    constexpr int32 StationCount = 9;
    constexpr int32 EvaPhase = 2;
    constexpr int32 LabPhase = 4;
    constexpr TCHAR ScoreHeader[] = TEXT("Attempt,Student,Status,Landing,Score,FlightSeconds,RepairSeconds,TotalSeconds,ImpactSpeed,RepairPanels,TimeUTC,Rubric");
    FCriticalSection ProgressFileLock;

    bool IsNameCharacter(TCHAR Character)
    {
        return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') ||
               (Character >= '0' && Character <= '9') || Character == ' ' || Character == '_' || Character == '-';
    }

    FString ProgressDirectory()
    {
        return FPaths::ProjectSavedDir() / TEXT("MissionRecall");
    }

    FString ScorePath()
    {
        return ProgressDirectory() / TEXT("student_scorecards.csv");
    }

    FString SavePath(const FString& Student)
    {
        // Prefix the normalized name so Windows device names are valid student names.
        return ProgressDirectory() / (TEXT("student-") + Student.ToLower() + TEXT(".sav"));
    }

    TArray<FString> LegacyDirectories()
    {
        const FString LegacyProject = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../SleepyMoon"));
        return {
            LegacyProject / TEXT("profiles"),
            LegacyProject / TEXT("build-vs/Debug/profiles"),
            LegacyProject / TEXT("build-vs/Release/profiles"),
            LegacyProject / TEXT("build/profiles"),
            LegacyProject / TEXT("build/RelWithDebInfo/profiles"),
            LegacyProject / TEXT("build/Release/profiles")
        };
    }

    bool CommitTemporaryFile(const FString& Temporary, const FString& Destination, bool bBackupDestination)
    {
        IFileManager& Files = IFileManager::Get();
        const FString Backup = Destination + TEXT(".bak");
        if (bBackupDestination && Files.Copy(*Backup, *Destination) != COPY_OK)
        {
            Files.Delete(*Temporary);
            return false;
        }

        const bool bCommitted = Files.Move(*Destination, *Temporary, true, false, false, true);
        if (!bCommitted)
        {
            // Preserve a recoverable file even on platforms where replace unlinks first.
            if (!Files.FileExists(*Destination) && Files.FileExists(*Backup))
            {
                Files.Copy(*Destination, *Backup);
            }
            Files.Delete(*Temporary);
        }
        return bCommitted;
    }

    bool SplitCsvLine(const FString& Line, TArray<FString>& Fields)
    {
        FString Field;
        bool bQuoted = false;
        for (int32 Index = 0; Index < Line.Len(); ++Index)
        {
            const TCHAR Character = Line[Index];
            if (Character == '"')
            {
                if (bQuoted && Index + 1 < Line.Len() && Line[Index + 1] == '"')
                {
                    Field.AppendChar('"');
                    ++Index;
                }
                else
                {
                    bQuoted = !bQuoted;
                }
            }
            else if (Character == ',' && !bQuoted)
            {
                Fields.Add(MoveTemp(Field));
                Field.Reset();
            }
            else
            {
                Field.AppendChar(Character);
            }
        }
        Fields.Add(MoveTemp(Field));
        return !bQuoted;
    }

    FString QuoteCsv(const FString& Field)
    {
        return TEXT("\"") + Field.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
    }

    FString NormalizeTimestamp(const FString& Timestamp)
    {
        FString Iso = Timestamp;
        if (Iso.Len() == 23 && Iso.EndsWith(TEXT(" UTC")))
        {
            Iso[10] = 'T';
            Iso = Iso.Left(19) + TEXT("Z");
        }
        FDateTime Date;
        return FDateTime::ParseIso8601(*Iso, Date) ? Date.ToIso8601() : Iso;
    }

    bool IsNonnegative(float Value)
    {
        return FMath::IsFinite(Value) && Value >= 0;
    }

    bool ValidScorecard(const FRecallScorecard& Scorecard)
    {
        return !Scorecard.Id.IsEmpty() && !Scorecard.Student.IsEmpty() && Scorecard.Student.Len() <= 24 &&
            IsNonnegative(Scorecard.FlightSeconds) && IsNonnegative(Scorecard.RepairSeconds) &&
            IsNonnegative(Scorecard.TotalSeconds) && IsNonnegative(Scorecard.ImpactSpeed) &&
            Scorecard.RepairPanels >= 0 && Scorecard.RepairPanels <= 2 && Scorecard.Points >= 0 && Scorecard.Points <= 1000;
    }

    bool ReadScore(const FString& Line, FRecallScorecard& OutScore)
    {
        TArray<FString> Fields;
        if (!SplitCsvLine(Line, Fields) || Fields.Num() != 12 ||
            !LexTryParseString(OutScore.Points, *Fields[4]) || !LexTryParseString(OutScore.FlightSeconds, *Fields[5]) ||
            !LexTryParseString(OutScore.RepairSeconds, *Fields[6]) || !LexTryParseString(OutScore.TotalSeconds, *Fields[7]) ||
            !LexTryParseString(OutScore.ImpactSpeed, *Fields[8]) || !LexTryParseString(OutScore.RepairPanels, *Fields[9]))
        {
            return false;
        }
        OutScore.Id = MoveTemp(Fields[0]);
        OutScore.Student = MoveTemp(Fields[1]);
        OutScore.Status = MoveTemp(Fields[2]);
        OutScore.Grade = MoveTemp(Fields[3]);
        OutScore.UpdatedUtc = NormalizeTimestamp(Fields[10]);
        return ValidScorecard(OutScore);
    }

    bool ReadScoreFile(const FString& Path, TArray<FRecallScorecard>& Scores, bool bSkipInvalidRows)
    {
        TArray<FString> Lines;
        if (!IFileManager::Get().FileExists(*Path) || !FFileHelper::LoadFileToStringArray(Lines, *Path) ||
            Lines.IsEmpty() || Lines[0] != ScoreHeader)
        {
            return false;
        }

        TArray<FRecallScorecard> Loaded;
        for (int32 Index = 1; Index < Lines.Num(); ++Index)
        {
            if (Lines[Index].IsEmpty())
            {
                continue;
            }
            FRecallScorecard Scorecard;
            if (ReadScore(Lines[Index], Scorecard))
            {
                Loaded.Add(MoveTemp(Scorecard));
            }
            else if (!bSkipInvalidRows)
            {
                return false;
            }
        }
        Scores = MoveTemp(Loaded);
        return true;
    }

    bool ReadNativeScores(TArray<FRecallScorecard>& Scores, bool& bReadPrimary)
    {
        const FString Path = ScorePath();
        const FString Backup = Path + TEXT(".bak");
        IFileManager& Files = IFileManager::Get();
        bReadPrimary = ReadScoreFile(Path, Scores, false);
        if (bReadPrimary || (!Files.FileExists(*Path) && !Files.FileExists(*Backup)))
        {
            return true;
        }
        return ReadScoreFile(Backup, Scores, false);
    }

    void ReadOlderScoreFile(const FString& Directory, TArray<FRecallScorecard>& Scores)
    {
        const FString Path = Directory / TEXT("scores.txt");
        FString Contents;
        if (!IFileManager::Get().FileExists(*Path) || !FFileHelper::LoadFileToString(Contents, *Path))
        {
            return;
        }

        std::istringstream Stream(TCHAR_TO_UTF8(*Contents));
        Stream.imbue(std::locale::classic());
        std::string Name;
        float Total;
        float Landing;
        bool bRepaired;
        int32 Row = 0;
        while (Stream >> std::quoted(Name) >> Total >> Landing >> bRepaired)
        {
            FRecallScorecard Scorecard;
            Scorecard.Id = FString::Printf(TEXT("legacy:%s:%d"), *Directory, ++Row);
            Scorecard.Student = UTF8_TO_TCHAR(Name.c_str());
            Scorecard.Status = TEXT("Legacy completed");
            Scorecard.UpdatedUtc = TEXT("Legacy version");
            Scorecard.TotalSeconds = Total;
            Scorecard.FlightSeconds = Landing;
            Scorecard.RepairSeconds = FMath::Max(0.f, Total - Landing);
            if (ValidScorecard(Scorecard))
            {
                Scores.Add(MoveTemp(Scorecard));
            }
        }
    }

    FString LegacySaveName(const FString& Student)
    {
        FString Name;
        uint32 Hash = 2166136261u;
        const FString Lower = Student.ToLower();
        for (TCHAR Character : Lower)
        {
            Name.AppendChar(Character == ' ' || Character == '-' ? '_' : Character);
            Hash = (Hash ^ static_cast<uint8>(Character)) * 16777619u;
        }
        return FString::Printf(TEXT("%s_%x.sav"), *Name, Hash);
    }

    bool ValidSnapshot(const FRecallSnapshot& Snapshot)
    {
        return ValidScorecard(Snapshot.Scorecard) && Snapshot.StationsDone.Num() == StationCount &&
            Snapshot.Phase >= EvaPhase && Snapshot.Phase <= LabPhase && IsNonnegative(Snapshot.Survey) &&
            FMath::IsFinite(Snapshot.RoverHeading) && FMath::IsFinite(Snapshot.CameraYaw) &&
            FMath::IsFinite(Snapshot.CameraPitch) && !Snapshot.CapsuleLocation.ContainsNaN() &&
            !Snapshot.SurfacePosition.ContainsNaN() && !Snapshot.CoopPosition.ContainsNaN() &&
            !Snapshot.LabPosition.ContainsNaN() && !Snapshot.RoverLocation.ContainsNaN() &&
            !(Snapshot.Suited && Snapshot.FreeFlight);
    }

    bool LoadNativeSave(const FString& Path, const FString& Student, FRecallSnapshot& Snapshot)
    {
        TArray<uint8> Bytes;
        if (!IFileManager::Get().FileExists(*Path) || !FFileHelper::LoadFileToArray(Bytes, *Path))
        {
            return false;
        }
        const URecallMissionSave* Saved = Cast<URecallMissionSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!Saved || !Saved->Snapshot.Scorecard.Student.Equals(Student, ESearchCase::IgnoreCase) ||
            !ValidSnapshot(Saved->Snapshot))
        {
            return false;
        }
        Snapshot = Saved->Snapshot;
        return true;
    }

    struct FLegacyPilot
    {
        bool bWearingSuit = false;
        bool bFreeFlight = false;
        bool bAboard = false;
        bool bInCabin = false;
    };

    bool LoadLegacySave(const FString& Path, const FString& Student, FRecallSnapshot& Snapshot)
    {
        FString Contents;
        if (!FFileHelper::LoadFileToString(Contents, *Path))
        {
            return false;
        }
        std::istringstream Stream(TCHAR_TO_UTF8(*Contents));
        Stream.imbue(std::locale::classic());
        std::string Version;
        std::string Name;
        std::string Status;
        int64 Attempt = 0;
        int32 Stage = 0;
        int32 Grade = 0;
        int32 RepairsRequired = 0;
        bool bPort = false;
        bool bStarboard = false;
        bool bComplete = false;
        bool bFinished = false;
        FRecallSnapshot Imported;
        FRecallScorecard& Scorecard = Imported.Scorecard;
        if (!(Stream >> Version) || Version != "SLEEPY4" ||
            !(Stream >> std::quoted(Name) >> Attempt >> std::quoted(Status) >> Stage >> Grade >> RepairsRequired >>
                bPort >> bStarboard >> bComplete >> bFinished >> Scorecard.TotalSeconds >> Scorecard.FlightSeconds >>
                Scorecard.RepairSeconds >> Scorecard.ImpactSpeed) ||
            Attempt == 0 || Stage < 2 || Stage > 5 || Grade < 1 || Grade > 3 || RepairsRequired != Grade - 1 ||
            Scorecard.FlightSeconds > 35.3f)
        {
            return false;
        }

        Scorecard.Student = UTF8_TO_TCHAR(Name.c_str());
        Scorecard.Id = LexToString(Attempt);
        Scorecard.Grade = Grade == 1 ? TEXT("PERFECT") : Grade == 2 ? TEXT("GOOD") : TEXT("BAD");
        Scorecard.Status = bComplete ? TEXT("Completed") : bFinished ? TEXT("Ended early") : TEXT("In progress");
        Scorecard.RepairPanels = static_cast<int32>(bPort) + static_cast<int32>(bStarboard);
        if (!Scorecard.Student.Equals(Student, ESearchCase::IgnoreCase) || bComplete != (Scorecard.RepairPanels >= RepairsRequired))
        {
            return false;
        }
        FRecallProgress::UpdateScore(Scorecard, bComplete);

        // The levels have different terrain, repair offsets and lab bounds. Resume near the
        // corresponding Unreal site instead of transferring world positions from raylib.
        float Ignored;
        for (int32 Index = 0; Index < 5; ++Index)
        {
            Stream >> Ignored;
        }
        FLegacyPilot Pilots[2];
        for (FLegacyPilot& Pilot : Pilots)
        {
            for (int32 Index = 0; Index < 5; ++Index)
            {
                Stream >> Ignored;
            }
            Stream >> Pilot.bWearingSuit >> Pilot.bFreeFlight >> Pilot.bAboard;
        }
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Stream >> Ignored;
        }
        if (!Stream || (Pilots[0].bWearingSuit && Pilots[0].bFreeFlight))
        {
            return false;
        }

        bool Science[5] = {};
        std::string Extension;
        if (Stream >> Extension)
        {
            if (Extension != "RECALL1" || !(Stream >> Science[0] >> Science[1] >> Science[2] >> Science[3] >> Science[4] >>
                Imported.Survey >> Pilots[0].bInCabin >> Pilots[1].bInCabin))
            {
                return false;
            }
        }
        Imported.StationsDone = {bPort, bStarboard, Science[0], Science[1], Science[2], false, Science[3], Science[4], false};
        Imported.SurfacePosition = FVector(-900, -600, 0);
        Imported.CoopPosition = Imported.SurfacePosition + FVector(300, 200, 0);
        Imported.RoverLocation = FVector(4500, 4500, 0);
        Imported.LabPosition = FVector(-320, 0, 15000);
        Imported.Suited = Pilots[0].bWearingSuit;
        Imported.FreeFlight = Pilots[0].bFreeFlight;
        if (Pilots[0].bInCabin && bComplete && Science[0] && Science[1] && Science[2] && Imported.Survey >= 100)
        {
            Imported.Phase = LabPhase;
            Imported.FirstPerson = true;
        }
        if (!ValidSnapshot(Imported))
        {
            return false;
        }
        Snapshot = MoveTemp(Imported);
        return true;
    }
}

FString FRecallProgress::NormalizeStudentName(const FString& Name)
{
    FString Normalized;
    for (TCHAR Character : Name.TrimStartAndEnd())
    {
        if (IsNameCharacter(Character))
        {
            Normalized.AppendChar(Character);
            if (Normalized.Len() == 24)
            {
                break;
            }
        }
    }
    Normalized.TrimStartAndEndInline();
    return Normalized;
}

FRecallScorecard FRecallProgress::StartAttempt(const FString& Student)
{
    FRecallScorecard Scorecard;
    Scorecard.Student = NormalizeStudentName(Student);
    if (!Scorecard.Student.IsEmpty())
    {
        Scorecard.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        RecordScore(Scorecard);
    }
    return Scorecard;
}

void FRecallProgress::UpdateScore(FRecallScorecard& Scorecard, bool bCompleted)
{
    const int32 LandingPoints = Scorecard.Grade == TEXT("PERFECT") ? 750 :
        Scorecard.Grade == TEXT("GOOD") ? 500 : Scorecard.Grade == TEXT("BAD") ? 200 : 0;
    if (!bCompleted)
    {
        Scorecard.Points = LandingPoints / 2;
        return;
    }
    const float EffectiveSeconds = FMath::Clamp(Scorecard.RepairSeconds, 0.f, 250.f / 3.f + 1.f);
    const int32 RepairBonus = Scorecard.Grade == TEXT("PERFECT") ? 250 : FMath::Max(0, 250 - FMath::FloorToInt(EffectiveSeconds * 3));
    Scorecard.Points = FMath::Clamp(LandingPoints + RepairBonus, 0, 1000);
}

bool FRecallProgress::RecordScore(FRecallScorecard& Scorecard)
{
    if (!ValidScorecard(Scorecard))
    {
        return false;
    }

    FScopeLock Lock(&ProgressFileLock);
    IFileManager& Files = IFileManager::Get();
    if (!Files.MakeDirectory(*ProgressDirectory(), true))
    {
        return false;
    }
    TArray<FRecallScorecard> Scores;
    bool bPreviousScoreFileValid = false;
    if (!ReadNativeScores(Scores, bPreviousScoreFileValid))
    {
        return false;
    }

    FRecallScorecard Updated = Scorecard;
    Updated.UpdatedUtc = FDateTime::UtcNow().ToIso8601();
    if (FRecallScorecard* Existing = Scores.FindByPredicate([&Scorecard](const FRecallScorecard& Row) { return Row.Id == Scorecard.Id; }))
    {
        *Existing = Updated;
    }
    else
    {
        Scores.Add(Updated);
    }

    FString Csv = FString(ScoreHeader) + TEXT("\n");
    for (const FRecallScorecard& Row : Scores)
    {
        Csv += FString::Printf(TEXT("%s,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%d,%s,%s\n"),
            *QuoteCsv(Row.Id), *QuoteCsv(Row.Student), *QuoteCsv(Row.Status), *QuoteCsv(Row.Grade),
            Row.Points, Row.FlightSeconds, Row.RepairSeconds, Row.TotalSeconds, Row.ImpactSpeed, Row.RepairPanels,
            *QuoteCsv(Row.UpdatedUtc), TEXT("\"Perfect 750; good 500; bad 200; repair bonus up to 250\""));
    }
    const FString Path = ScorePath();
    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Csv, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Files.Delete(*Temporary, false, false, true);
        return false;
    }
    if (!CommitTemporaryFile(Temporary, Path, bPreviousScoreFileValid))
    {
        return false;
    }
    Scorecard.UpdatedUtc = MoveTemp(Updated.UpdatedUtc);
    return true;
}

TArray<FRecallScorecard> FRecallProgress::ReadScores()
{
    FScopeLock Lock(&ProgressFileLock);
    TArray<FRecallScorecard> Scores;
    bool bReadPrimary = false;
    ReadNativeScores(Scores, bReadPrimary);

    TMap<FString, FRecallScorecard> Legacy;
    for (const FString& Directory : LegacyDirectories())
    {
        TArray<FRecallScorecard> Rows;
        const FString Path = Directory / TEXT("student_scorecards.csv");
        if (!ReadScoreFile(Path, Rows, true) && !ReadScoreFile(Path + TEXT(".bak"), Rows, true))
        {
            ReadOlderScoreFile(Directory, Rows);
        }
        for (FRecallScorecard& Row : Rows)
        {
            const FRecallScorecard* Existing = Legacy.Find(Row.Id);
            if (!Existing || Existing->UpdatedUtc < Row.UpdatedUtc)
            {
                const FString Id = Row.Id;
                Legacy.Add(Id, MoveTemp(Row));
            }
        }
    }

    for (const FRecallScorecard& Scorecard : Scores)
    {
        Legacy.Remove(Scorecard.Id);
    }
    for (TPair<FString, FRecallScorecard>& Pair : Legacy)
    {
        Scores.Add(MoveTemp(Pair.Value));
    }
    return Scores;
}

bool FRecallProgress::Save(const FRecallSnapshot& Snapshot)
{
    if (!ValidSnapshot(Snapshot) || NormalizeStudentName(Snapshot.Scorecard.Student) != Snapshot.Scorecard.Student)
    {
        return false;
    }
    URecallMissionSave* SaveGame = NewObject<URecallMissionSave>();
    SaveGame->Snapshot = Snapshot;
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(SaveGame, Bytes))
    {
        return false;
    }

    FScopeLock Lock(&ProgressFileLock);
    IFileManager& Files = IFileManager::Get();
    const FString Path = SavePath(Snapshot.Scorecard.Student);
    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!Files.MakeDirectory(*ProgressDirectory(), true) ||
        !FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *Temporary))
    {
        Files.Delete(*Temporary, false, false, true);
        return false;
    }
    FRecallSnapshot Previous;
    return CommitTemporaryFile(Temporary, Path, LoadNativeSave(Path, Snapshot.Scorecard.Student, Previous));
}

bool FRecallProgress::Load(const FString& Student, FRecallSnapshot& Snapshot)
{
    const FString Name = NormalizeStudentName(Student);
    if (Name.IsEmpty())
    {
        return false;
    }
    FScopeLock Lock(&ProgressFileLock);
    IFileManager& Files = IFileManager::Get();
    const FString Path = SavePath(Name);
    const FString Backup = Path + TEXT(".bak");
    if (Files.FileExists(*Path) || Files.FileExists(*Backup))
    {
        return LoadNativeSave(Path, Name, Snapshot) || LoadNativeSave(Backup, Name, Snapshot);
    }

    const FString LegacyName = LegacySaveName(Name);
    FDateTime Latest = FDateTime::MinValue();
    bool bFound = false;
    for (const FString& Directory : LegacyDirectories())
    {
        const FString Candidate = Directory / LegacyName;
        if (Files.FileExists(*Candidate))
        {
            const FDateTime Modified = Files.GetTimeStamp(*Candidate);
            if ((!bFound || Modified > Latest) && LoadLegacySave(Candidate, Name, Snapshot))
            {
                bFound = true;
                Latest = Modified;
            }
        }
    }
    return bFound;
}
