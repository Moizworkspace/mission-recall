#include "RecallMode.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
    constexpr int32 ToneSampleRate = 22050;
    constexpr int32 WarningTone = 0;
    constexpr int32 EngineTone = 1;
    constexpr int32 FootstepTone = 2;
    constexpr int32 RadioTone = 3;
    constexpr int32 ToneCount = 4;

    USoundWaveProcedural* NewTone(UObject* Owner, const TArray<uint8>& Pcm)
    {
        USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Owner);
        Wave->NumChannels = 1;
        Wave->SetSampleRate(ToneSampleRate);
        Wave->Duration = static_cast<float>(Pcm.Num()) / (sizeof(int16) * ToneSampleRate);
        Wave->bLooping = false;
        Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
        return Wave;
    }
}

void ARecallMode::InitAudio()
{
    if (!GetWorld()->GetAudioDeviceRaw())
    {
        return;
    }

    TonePCM.SetNum(ToneCount);
    Tones.SetNum(ToneCount);
    Tones[WarningTone] = MakeTone(640, 1.4f, 1, WarningTone);
    Tones[EngineTone] = MakeTone(65, 0.8f, 2, EngineTone);
    Tones[FootstepTone] = MakeTone(100, 0.1f, 2, FootstepTone);
    Tones[RadioTone] = MakeTone(950, 0.15f, 0, RadioTone);

    for (USoundWaveProcedural* Wave : Tones)
    {
        UAudioComponent* Player = UGameplayStatics::CreateSound2D(this, Wave, 1, 1, 0, nullptr, false, false);
        if (!Player)
        {
            AudioPlayers.Reset();
            return;
        }
        Player->bStopWhenOwnerDestroyed = true;
        AudioPlayers.Add(Player);
    }
}

USoundWaveProcedural* ARecallMode::MakeTone(float Hz, float Seconds, int32 Kind, int32 Index)
{
    TArray<int16> Samples;
    Samples.SetNumUninitialized(static_cast<int32>(ToneSampleRate * Seconds));
    for (int32 Sample = 0; Sample < Samples.Num(); ++Sample)
    {
        const float Time = static_cast<float>(Sample) / ToneSampleRate;
        const float Frequency = Hz + (Kind == 1 ? 180 * FMath::Sin(Time * 7) : 0);
        const float Sine = FMath::Sin(2 * PI * Frequency * Time);
        const float Signal = Kind == 2 ? 0.5f * (Sine + FMath::Sin(Time * 1777)) : Sine;
        const float Envelope = FMath::Min(1.f, Time * 30) * FMath::Min(1.f, (Seconds - Time) * 30);
        Samples[Sample] = static_cast<int16>(Signal * Envelope * 5500);
    }

    TArray<uint8>& Pcm = TonePCM[Index];
    Pcm.Reset();
    Pcm.Append(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
    return NewTone(this, Pcm);
}

void ARecallMode::PlayTone(int32 Index, float Volume)
{
    if (!AudioPlayers.IsValidIndex(Index))
    {
        return;
    }

    UAudioComponent* Player = AudioPlayers[Index];
    if (Player->IsPlaying())
    {
        return;
    }

    // Procedural waves consume their PCM queues on the audio thread. Each playback
    // gets a fresh queue, including when a landing warning was stopped by a pause.
    USoundWaveProcedural* Wave = NewTone(this, TonePCM[Index]);
    Tones[Index] = Wave;
    Player->SetSound(Wave);
    Player->SetVolumeMultiplier(Volume);
    Player->Play();
    Player->StopDelayed(Wave->Duration);
}

void ARecallMode::UpdateAudio(float Dt)
{
    if (AudioPlayers.Num() != ToneCount)
    {
        return;
    }

    const bool bLanding = Pilots[0].Phase == EPhase::Landing;
    if (bLanding)
    {
        PlayTone(WarningTone, 0.22f);
    }
    else
    {
        AudioPlayers[WarningTone]->Stop();
    }

    if (bLanding || (RoverDriver != INDEX_NONE && RoverSpeed > 100))
    {
        PlayTone(EngineTone, 0.12f);
    }
    else
    {
        AudioPlayers[EngineTone]->Stop();
    }

    const auto IsWalking = [this](const FPilot& Pilot)
    {
        return Pilot.bMoving && Pilot.bSuited && (Pilot.Phase == EPhase::Lab ||
            (Pilot.Phase == EPhase::EVA &&
             FMath::IsNearlyEqual(Pilot.Position.Z, static_cast<double>(GroundAt(Pilot.Position.X, Pilot.Position.Y)), 2.0)));
    };
    FootstepTimer = FMath::Min(FootstepTimer + Dt, 0.55f);
    if (FootstepTimer >= 0.55f && (IsWalking(Pilots[0]) || (CoopController && IsWalking(Pilots[1]))))
    {
        PlayTone(FootstepTone, 0.12f);
        FootstepTimer = 0;
    }
}
