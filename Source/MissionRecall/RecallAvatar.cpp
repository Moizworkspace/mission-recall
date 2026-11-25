#include "RecallMode.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
constexpr double StrideLength = 180.0;

constexpr TCHAR AstronautPackage[] = TEXT("/Game/MissionRecall/Astronaut/SK_Astronaut/SkeletalMeshes/");

template<typename T>
T* LoadAstronautAsset(const TCHAR* Name)
{
    const FString ObjectPath = FString::Printf(TEXT("%s%s.%s"), AstronautPackage, Name, Name);
    return LoadObject<T>(nullptr, *ObjectPath);
}
}

void ARecallMode::BuildCompanionAppearance()
{
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/MissionRecall/M_MissionHardware.M_MissionHardware"));
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    const auto Finish = [this, Base](const FLinearColor& Tint)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Companion.Get());
        if (Material)
        {
            Material->SetVectorParameterValue(TEXT("SurfaceColor"), Tint);
        }
        return Material;
    };
    UMaterialInterface* Armor = Finish(FLinearColor(0.66f, 0.73f, 0.78f));
    UMaterialInterface* Trim = Finish(FLinearColor(0.23f, 0.3f, 0.36f));
    UMaterialInterface* Joints = Finish(FLinearColor(0.027f, 0.038f, 0.055f));
    UMaterialInterface* Skin = Finish(FLinearColor(0.39f, 0.19f, 0.125f));
    UMaterialInterface* Hair = Finish(FLinearColor(0.042f, 0.025f, 0.017f));
    UMaterialInterface* Indicator = Finish(FLinearColor(0.055f, 0.65f, 0.76f));

    const auto Attach = [this, Sphere](USceneComponent* Parent, UMaterialInterface* Material,
        bool bRounded, const FVector& Position, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator)
    {
        UStaticMeshComponent* Part = Shape(Companion, false, Position, Scale);
        if (bRounded && Sphere)
        {
            Part->SetStaticMesh(Sphere);
        }
        if (Material)
        {
            Part->SetMaterial(0, Material);
        }
        if (Parent != Companion->GetRootComponent())
        {
            Part->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
        }
        Part->SetRelativeRotation(Rotation);
    };
    const auto Pivot = [this](USceneComponent* Parent, const FVector& Position)
    {
        USceneComponent* Joint = NewObject<USceneComponent>(Companion.Get());
        Joint->SetupAttachment(Parent);
        Joint->SetRelativeLocation(Position);
        Companion->AddInstanceComponent(Joint);
        Joint->RegisterComponent();
        return Joint;
    };

    // The supplied sleepy.jpg and sleepy3.jpg guide the silver armor, inset dark joints, and open collar.
    USceneComponent* Body = Companion->GetRootComponent();
    Attach(Body, Joints, true, FVector(0, 0, 112), FVector(0.43, 0.52, 0.77));
    Attach(Body, Armor, true, FVector(11, 0, 124), FVector(0.38, 0.52, 0.44));
    Attach(Body, Trim, true, FVector(18, 0, 112), FVector(0.24, 0.36, 0.2));
    for (int32 Plate = 0; Plate < 3; ++Plate)
    {
        Attach(Body, Armor, false, FVector(20 - Plate * 2, 0, 107 - Plate * 7),
            FVector(0.09, 0.33 - 0.04 * Plate, 0.046), FRotator(7, 0, 0));
    }
    Attach(Body, Joints, true, FVector(0, 0, 149), FVector(0.42, 0.43, 0.18));
    Attach(Body, Trim, true, FVector(-8, 0, 149), FVector(0.28, 0.48, 0.18));
    Attach(Body, Trim, true, FVector(0, 0, 83), FVector(0.42, 0.47, 0.14));
    Attach(Body, Joints, false, FVector(-21, 0, 121), FVector(0.22, 0.46, 0.43));
    Attach(Body, Armor, false, FVector(-32, 0, 124), FVector(0.09, 0.4, 0.31));
    Attach(Body, Indicator, false, FVector(31, 5, 130), FVector(0.025, 0.105, 0.026));

    Attach(Body, Skin, true, FVector(1, 0, 167), FVector(0.282, 0.295, 0.383));
    Attach(Body, Skin, true, FVector(6, 0, 157), FVector(0.207, 0.249, 0.199));
    Attach(Body, Skin, true, FVector(15, 0, 165), FVector(0.089, 0.074, 0.102));
    Attach(Body, Hair, true, FVector(-3, 0, 180), FVector(0.268, 0.31, 0.173));
    Attach(Body, Hair, true, FVector(-9, 0, 169), FVector(0.15, 0.264, 0.19));

    for (const double Side : {-1.0, 1.0})
    {
        Attach(Body, Skin, true, FVector(0, Side * 14, 166), FVector(0.071, 0.074, 0.126));
        Attach(Body, Hair, true, FVector(13, Side * 6, 171), FVector(0.039, 0.067, 0.019));
        Attach(Body, Joints, true, FVector(14, Side * 6, 168), FVector(0.036, 0.044, 0.054));

        USceneComponent* Shoulder = Pivot(Body, FVector(0, Side * 27, 133));
        CompanionShoulders.Add(Shoulder);
        Attach(Shoulder, Armor, true, FVector(0, Side * 2, 0), FVector(0.33, 0.34, 0.28));
        Attach(Shoulder, Joints, true, FVector(0, Side * 7, -19), FVector(0.21, 0.2, 0.45), FRotator(0, 0, Side * 10));
        Attach(Shoulder, Trim, true, FVector(1, Side * 8, -32), FVector(0.19, 0.18, 0.19));
        Attach(Shoulder, Armor, true, FVector(4, Side * 9, -45), FVector(0.22, 0.18, 0.27), FRotator(12, 0, 0));
        Attach(Shoulder, Joints, true, FVector(6, Side * 10, -60), FVector(0.14, 0.14, 0.19));

        USceneComponent* Hip = Pivot(Body, FVector(0, Side * 13, 81));
        USceneComponent* Knee = Pivot(Hip, FVector(0, 0, -34));
        USceneComponent* Ankle = Pivot(Knee, FVector(1, 0, -38));
        CompanionHips.Add(Hip);
        CompanionKnees.Add(Knee);
        CompanionAnkles.Add(Ankle);
        Attach(Hip, Joints, true, FVector(0, 0, -14), FVector(0.28, 0.26, 0.46));
        Attach(Hip, Armor, true, FVector(9, Side * 2, -14), FVector(0.18, 0.23, 0.4));
        Attach(Knee, Trim, true, FVector(10, 0, 0), FVector(0.14, 0.21, 0.165));
        Attach(Knee, Joints, true, FVector(0, 0, -17), FVector(0.22, 0.22, 0.42));
        Attach(Knee, Armor, true, FVector(6, 0, -17), FVector(0.20, 0.22, 0.33));
        Attach(Ankle, Joints, false, FVector(7, 0, 0), FVector(0.37, 0.23, 0.18));
        Attach(Ankle, Trim, true, FVector(15, 0, 4), FVector(0.2, 0.20, 0.12));
    }
}

void ARecallMode::BuildAvatarAsset()
{
    BuildCompanionAppearance();
    USkeletalMesh* Mesh = LoadAstronautAsset<USkeletalMesh>(TEXT("SK_Astronaut"));
    AstronautIdle = LoadAstronautAsset<UAnimSequence>(TEXT("SK_Astronautidle"));
    AstronautWalk = LoadAstronautAsset<UAnimSequence>(TEXT("SK_Astronautmoon_walk"));
    AstronautFloat = LoadAstronautAsset<UAnimSequence>(TEXT("SK_Astronautfloating"));
    if (!Mesh)
    {
        return;
    }

    const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
    const double Scale = 190.0 / FMath::Max(2.0 * Bounds.BoxExtent.Z, 1.0);
    AstronautMesh = NewObject<USkeletalMeshComponent>(Astronaut);
    AstronautMesh->SetupAttachment(Astronaut->GetRootComponent());
    AstronautMesh->SetSkeletalMeshAsset(Mesh);
    AstronautMesh->SetRelativeLocation(FVector(0, 0, (Bounds.BoxExtent.Z - Bounds.Origin.Z) * Scale));
    AstronautMesh->SetRelativeRotation(FRotator(0, -90, 0));
    AstronautMesh->SetRelativeScale3D(FVector(Scale));
    AstronautMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AstronautMesh->SetVisibility(false);
    Astronaut->AddInstanceComponent(AstronautMesh);
    AstronautMesh->RegisterComponent();
}

void ARecallMode::AnimateAvatar(int32 Index, bool bWorking)
{
    const FPilot& Pilot = Pilots[Index];
    AActor* Body = Index == 0 ? Astronaut.Get() : Companion.Get();
    if (!Body)
    {
        return;
    }

    Body->SetActorLocationAndRotation(Pilot.Position, FRotator(bWorking ? -19 : 0, Pilot.BodyYaw, 0));

    const bool bAirborne = Pilot.Phase == EPhase::EVA && Pilot.Position.Z > GroundAt(Pilot.Position.X, Pilot.Position.Y) + 2;
    const bool bWalking = Pilot.bMoving && !bAirborne && !bWorking;
    const double WalkCycle = FMath::Fmod(Pilot.StepDistance, StrideLength) / StrideLength;

    if (Index == 0 && AstronautMesh)
    {
        if (AstronautMesh->GetVisibleFlag() != Pilot.bSuited)
        {
            TInlineComponentArray<UStaticMeshComponent*> Parts(Body);
            for (UStaticMeshComponent* Part : Parts)
            {
                Part->SetVisibility(!Pilot.bSuited);
            }
            AstronautMesh->SetVisibility(Pilot.bSuited);
        }
        if (Pilot.bSuited)
        {
            UAnimSequence* Desired = bWalking ? AstronautWalk.Get() : bAirborne ? AstronautFloat.Get() : AstronautIdle.Get();
            if (Desired)
            {
                UAnimSingleNodeInstance* Animation = AstronautMesh->GetSingleNodeInstance();
                if (!Animation || Animation->GetCurrentAsset() != Desired)
                {
                    AstronautMesh->PlayAnimation(Desired, true);
                    Animation = AstronautMesh->GetSingleNodeInstance();
                    if (Animation && bWalking)
                    {
                        Animation->SetPlaying(false);
                    }
                }
                if (bWalking && Animation)
                {
                    Animation->SetPosition(static_cast<float>(WalkCycle * Desired->GetPlayLength()), false);
                }
            }
            return;
        }
    }

    if (Index == 1)
    {
        for (int32 Side = 0; Side < CompanionHips.Num(); ++Side)
        {
            const double Phase = WalkCycle * 2 * PI + Side * PI;
            const double Swing = bWalking ? FMath::Sin(Phase) : 0;
            const double Bend = bWalking ? 32 * FMath::Max(0.0, FMath::Cos(Phase)) : 0;
            CompanionHips[Side]->SetRelativeRotation(FRotator(24 * Swing, 0, 0));
            CompanionKnees[Side]->SetRelativeRotation(FRotator(-Bend, 0, 0));
            CompanionAnkles[Side]->SetRelativeRotation(FRotator(Bend - 24 * Swing, 0, 0));
            CompanionShoulders[Side]->SetRelativeRotation(FRotator(bWorking ? -40 : -16 * Swing, 0, 0));
        }
        return;
    }

    for (int32 Side = 0; Side < PrimaryLegs.Num(); ++Side)
    {
        const double Offset = Side == 0 ? -12 : 12;
        const double Phase = WalkCycle * 2 * PI + Side * PI;
        const double Swing = bWalking ? 21 * FMath::Sin(Phase) : 0;
        const double Lift = bWalking ? 14 * FMath::Max(0.0, FMath::Cos(Phase)) : 0;
        PrimaryLegs[Side]->SetRelativeLocationAndRotation(FVector(Swing / 2, Offset, 42 + Lift / 2),
            FRotator(Swing, 0, 0));
    }
}
