#include "RecallMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

AActor* ARecallMode::NewBody(const FVector& Position)
{
    AActor* Body = GetWorld()->SpawnActor<AActor>();
    USceneComponent* Root = NewObject<USceneComponent>(Body);
    Body->SetRootComponent(Root);
    Body->AddInstanceComponent(Root);
    Root->RegisterComponent();
    Body->SetActorLocation(Position);
    return Body;
}

UStaticMeshComponent* ARecallMode::Shape(AActor* Body, bool Round, const FVector& Position, const FVector& Scale)
{
    const TCHAR* Path = Round ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Cube.Cube");
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Body);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Path));
    Mesh->SetupAttachment(Body->GetRootComponent());
    Mesh->SetRelativeLocation(Position);
    Mesh->SetRelativeScale3D(Scale);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

float ARecallMode::GroundAt(double X, double Y) const
{
    const double MetersX = X / 100;
    const double MetersY = Y / 100;
    double Height = 3.5 * FMath::Sin(MetersX * .012) * FMath::Sin(MetersY * .017) +
        1.3 * FMath::Sin(MetersX * .044 + MetersY * .027);
    static const FVector2D Craters[] = {{160, 90}, {-180, 230}, {340, -270}, {-450, -330},
        {70, 490}, {650, 400}, {-760, 90}};
    for (const FVector2D& Center : Craters)
    {
        const double DistanceSquared = FVector2D::DistSquared(FVector2D(MetersX, MetersY), Center);
        if (DistanceSquared < FMath::Square(245.0))
        {
            const double Rim = FMath::Sqrt(DistanceSquared) - 53;
            Height += -10 * FMath::Exp(-DistanceSquared / 1800) + 3.5 * FMath::Exp(-Rim * Rim / 100);
        }
    }
    return 100 * Height * FMath::Clamp(FVector2D(MetersX, MetersY).Size() / 38, 0.0, 1.0);
}

FVector ARecallMode::OnGround(FVector Position) const
{
    Position.Z = GroundAt(Position.X, Position.Y);
    return Position;
}

void ARecallMode::BuildTerrain(AActor* Terrain)
{
    UMaterialInterface* Regolith = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MissionRecall/M_LunarRegolith.M_LunarRegolith"));
    constexpr int32 TileCount = 12;
    constexpr double TileWidth = 50000;
    for (int32 Row = 0; Row < TileCount; ++Row)
    {
        for (int32 Column = 0; Column < TileCount; ++Column)
        {
            constexpr int32 Quads = 32;
            constexpr int32 Side = Quads + 1;
            TArray<FVector> Vertices, Normals;
            TArray<FVector2D> UV;
            TArray<int32> Indices;
            Vertices.Reserve(Side * Side);
            Normals.Reserve(Side * Side);
            UV.Reserve(Side * Side);
            Indices.Reserve(Quads * Quads * 6);
            for (int32 Y = 0; Y <= Quads; ++Y)
            {
                for (int32 X = 0; X <= Quads; ++X)
                {
                    const double WorldX = (Column - TileCount / 2 + double(X) / Quads) * TileWidth;
                    const double WorldY = (Row - TileCount / 2 + double(Y) / Quads) * TileWidth;
                    Vertices.Add(OnGround(FVector(WorldX, WorldY, 0)));
                    Normals.Add(FVector(GroundAt(WorldX - 100, WorldY) - GroundAt(WorldX + 100, WorldY),
                        GroundAt(WorldX, WorldY - 100) - GroundAt(WorldX, WorldY + 100), 200).GetSafeNormal());
                    UV.Emplace(WorldX / 35000, WorldY / 35000);
                    if (X < Quads && Y < Quads)
                    {
                        const int32 Corner = Y * Side + X;
                        Indices.Append({Corner, Corner + Side, Corner + 1, Corner + 1, Corner + Side, Corner + Side + 1});
                    }
                }
            }
            UProceduralMeshComponent* Tile = NewObject<UProceduralMeshComponent>(Terrain);
            Tile->SetupAttachment(Terrain->GetRootComponent());
            Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Tile->SetCastShadow(false);
            Tile->SetCullDistance(165000);
            Tile->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, {}, false);
            Tile->SetMaterial(0, Regolith);
            Terrain->AddInstanceComponent(Tile);
            Tile->RegisterComponent();
        }
    }
}

void ARecallMode::BuildSite()
{
    AActor* Terrain = NewBody(FVector::ZeroVector);
    BuildTerrain(Terrain);

    UMaterialInterface* Hardware = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/MissionRecall/M_MissionHardware.M_MissionHardware"));
    const auto Color = [Hardware](UStaticMeshComponent* Part, const FLinearColor& Tint)
    {
        if (Hardware)
        {
            UMaterialInstanceDynamic* Material = Part->CreateDynamicMaterialInstance(0, Hardware);
            Material->SetVectorParameterValue(TEXT("SurfaceColor"), Tint);
        }
    };
    const FLinearColor Shield(0.035f, 0.04f, 0.048f);
    const FLinearColor Hull(0.77f, 0.8f, 0.82f);
    const FLinearColor Window(0.028f, 0.087f, 0.15f);
    const FLinearColor GoldFoil(0.68f, 0.36f, 0.055f);
    FRandomStream Scatter(714);
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for (int32 Index = 0; Index < 250; ++Index)
    {
        const float ScatterRadius = Index < 150 ? 25000.0f : 120000.0f;
        const float X = Scatter.FRandRange(-ScatterRadius, ScatterRadius);
        const float Y = Scatter.FRandRange(-ScatterRadius, ScatterRadius);
        if (FVector2D(X, Y).Size() < 2800)
        {
            continue;
        }
        const float Size = Scatter.FRandRange(0.25f, 2.5f);
        const FVector Position = OnGround(FVector(X, Y, 0));
        Rocks.Emplace(Position, Size * 50);
        UStaticMeshComponent* Rock = Shape(Terrain, false, Position, FVector(Size, Size * 0.77f, Size * 0.4f));
        Rock->SetStaticMesh(Sphere);
        Rock->SetCullDistance(60000);
        Rock->SetRelativeRotation(FRotator(16, Index * 31, 20));
        const float Gray = Scatter.FRandRange(0.09f, 0.27f);
        Color(Rock, FLinearColor(Gray, Gray * 0.97f, Gray * 0.92f));
    }

    Capsule = NewBody(FVector::ZeroVector);
    Color(Shape(Capsule, true, FVector(0, 0, 45), FVector(13, 13, 0.9)), Shield);
    for (int32 Ring = 0; Ring < 18; ++Ring)
    {
        const float Diameter = 12.8f - Ring * 0.4f;
        Color(Shape(Capsule, true, FVector(0, 0, 110 + 40 * Ring),
            FVector(Diameter, Diameter, 0.41)), Ring == 17 ? Shield : Hull);
    }
    for (float Side : {-1.0f, 1.0f})
    {
        Color(Shape(Capsule, false, FVector(0, Side * 576, 210), FVector(1.8, 0.55, 1.5)), Shield);
        for (float End : {-1.0f, 1.0f})
        {
            const FVector Position(End * 375, Side * 264, 495);
            UStaticMeshComponent* Pane = Shape(Capsule, false, Position, FVector(0.2, 1.25, 0.97));
            Pane->SetRelativeRotation(FRotator(15 * End, Side * End * 35, 0));
            Color(Pane, Window);
        }
    }
    Color(Shape(Capsule, false, FVector(-546, 0, 245), FVector(.27, 1.58, 2.3)), Window);
    for (int32 Step = 0; Step < 5; ++Step)
    {
        Color(Shape(Capsule, false, FVector(-726 + Step * 34, 0, 18 + Step * 30), FVector(.58, 1.55, .19)), Hull);
    }

    Astronaut = NewBody(FVector::ZeroVector);
    Companion = NewBody(FVector::ZeroVector);
    Color(Shape(Astronaut, false, FVector(0, 0, 116), FVector(.34, .47, .63)), Hull);
    UStaticMeshComponent* Head = Shape(Astronaut, false, FVector(0, 0, 163), FVector(.29, .30, .36));
    Head->SetStaticMesh(Sphere);
    Color(Head, Hull);
    Color(Shape(Astronaut, false, FVector(14, 0, 164), FVector(.05, .25, .17)), Window);
    Color(Shape(Astronaut, false, FVector(-22, 0, 118), FVector(.22, .44, .58)), Hull);
    for (float Side : {-1.0f, 1.0f})
    {
        Color(Shape(Astronaut, false, FVector(0, Side * 29, 109), FVector(.15, .15, .54)), Hull);
        UStaticMeshComponent* Leg = Shape(Astronaut, false, FVector(0, Side * 12, 42), FVector(.22, .19, .84));
        Color(Leg, Hull);
        PrimaryLegs.Add(Leg);
    }
    BuildAvatarAsset();

    Rover = NewBody(FVector::ZeroVector);
    Color(Shape(Rover, false, FVector(0, 0, 63), FVector(3.1, 1.6, 0.34)), Hull);
    Color(Shape(Rover, false, FVector(106, 0, 97), FVector(.78, 1.65, .43)), Shield);
    Color(Shape(Rover, false, FVector(-116, 0, 95), FVector(.64, 1.72, .49)), GoldFoil);
    for (float Side : {-1.0f, 1.0f})
    {
        Color(Shape(Rover, false, FVector(17, Side * 42, 93), FVector(.68, .62, .22)), Shield);
        Color(Shape(Rover, false, FVector(-20, Side * 42, 120), FVector(.16, .68, .54)), Window);
        Color(Shape(Rover, false, FVector(151, Side * 66, 91), FVector(.055, .24, .18)), Hull);
    }
    for (float Longitudinal : {-105.0f, 105.0f})
    {
        for (float Side : {-108.0f, 108.0f})
        {
            UStaticMeshComponent* Wheel = Shape(Rover, true, FVector(Longitudinal, Side, 37), FVector(0.74, 0.74, 0.38));
            Wheel->SetRelativeRotation(FRotator(0, 0, 90));
            Color(Wheel, Shield);
            RoverWheels.Add(Wheel);
            Color(Shape(Rover, false, FVector(Longitudinal, Side * .77f, 47), FVector(.58, .48, .28)), Hull);
        }
    }

    Stations = {
        {TEXT("Port service panel"), FVector(0, -630, 0), 350, 3},
        {TEXT("Starboard service panel"), FVector(0, 630, 0), 350, 3},
        {TEXT("Regolith sampling"), FVector(1800, -1000, 0), 280, 30},
        {TEXT("Seismic monitoring"), FVector(-2400, 2200, 0), 280, 30},
        {TEXT("Science lander"), FVector(3000, 3200, 0), 280, 30},
        {TEXT("Capsule airlock"), FVector(-760, 0, 0), 280, 0},
        {TEXT("Plant growth chamber"), LabCenter + FVector(345, -238, 0), 195, 30},
        {TEXT("Life-support console"), LabCenter + FVector(345, 238, 0), 195, 30},
        {TEXT("Exit airlock"), LabCenter + FVector(-460, 0, 0), 210, 0}
    };

    for (int32 Index = FirstExperiment; Index < Airlock; ++Index)
    {
        AActor* Station = NewBody(StationLocation(Index));
        OutdoorStations.Add(Station);
        Color(Shape(Station, false, FVector(0, 0, 9), FVector(2.3, 2, .18)), Shield);
        if (Index == FirstExperiment)
        {
            for (int32 Sample = 0; Sample < 4; ++Sample)
            {
                Color(Shape(Station, true, FVector(-35 + Sample * 29, 0, 37), FVector(.2, .2, .45)), Hull);
            }
        }
        else
        {
            Color(Shape(Station, false, FVector(0, 0, 95), FVector(.74, .70, 1.15)), GoldFoil);
            Color(Shape(Station, true, FVector(-17, 0, 173), FVector(.055, .055, 1.7)), Hull);
            UStaticMeshComponent* Dish = Shape(Station, false, FVector(0, 0, 228), FVector(.72, .72, .13));
            Dish->SetStaticMesh(Sphere);
            Dish->SetRelativeRotation(FRotator(24, 0, 12));
            Color(Dish, Hull);
        }
        if (Index == Airlock - 1)
        {
            Color(Shape(Station, true, FVector(225, 0, 110), FVector(3.3, 3.3, 1.2)), GoldFoil);
            Color(Shape(Station, false, FVector(225, 0, 240), FVector(1.65, 1.65, 1.4)), Hull);
            for (float Side : {-1.0f, 1.0f})
            {
                for (float End : {-1.0f, 1.0f})
                {
                    UStaticMeshComponent* Leg = Shape(Station, true, FVector(225 + End * 111, Side * 116, 67),
                        FVector(.13, .13, 1.58));
                    Leg->SetRelativeRotation(FRotator(End * 24, 0, Side * 28));
                    Color(Leg, Hull);
                    Color(Shape(Station, true, FVector(225 + End * 139, Side * 142, 10), FVector(.73, .73, .2)), GoldFoil);
                }
            }
        }
    }

    AActor* SurveyDisplay = NewBody(OnGround(FVector(-11000, 16000, 0)));
    Color(Shape(SurveyDisplay, false, FVector(0, 0, 155), FVector(5, 5, 3.2)), Window);
    if (UStaticMesh* Globe = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MissionRecall/Moon/Moon/StaticMeshes/Moon.Moon")))
    {
        const FBoxSphereBounds Bounds = Globe->GetBounds();
        const double Scale = 155 / FMath::Max(1.0, double(Bounds.SphereRadius));
        UStaticMeshComponent* Model = Shape(SurveyDisplay, false, FVector(0, 0, 485) - Bounds.Origin * Scale, FVector(Scale));
        Model->SetStaticMesh(Globe);
    }

    AActor* Lab = NewBody(LabCenter);
    Color(Shape(Lab, false, FVector(0, 0, -15), FVector(11.3, 7.6, .3)), Window);
    Color(Shape(Lab, false, FVector(0, 0, 310), FVector(11.3, 7.6, .2)), Hull);
    for (float Side : {-1.0f, 1.0f})
    {
        Color(Shape(Lab, false, FVector(0, Side * 380, 151), FVector(11.5, .2, 3.1)), Hull);
        Color(Shape(Lab, false, FVector(Side * 560, 0, 151), FVector(.2, 7.7, 3.1)), Hull);
        Color(Shape(Lab, false, FVector(405, Side * 245, 59), FVector(1.5, 1.6, 1.2)), Window);
        Color(Shape(Lab, false, FVector(405, Side * 245, 122), FVector(1.58, 1.65, .09)), Hull);
    }
    Color(Shape(Lab, false, FVector(-547, 0, 130), FVector(.1, 1.7, 2.6)), Window);
    Color(Shape(Lab, false, FVector(547, 0, 191), FVector(.13, 2.9, 1.76)), Shield);
    Color(Shape(Lab, false, FVector(470, 240, 180), FVector(.1, 1.35, .78)), Window);
    Color(Shape(Lab, true, FVector(415, -245, 140), FVector(.56, .56, .28)), FLinearColor(.27f, .15f, .09f));
    PlantStem = Shape(Lab, true, FVector(415, -245, 169), FVector(.075, .075, .30));
    Color(PlantStem, FLinearColor(.14f, .44f, .08f));
    for (int32 Petal = 0; Petal < 6; ++Petal)
    {
        UStaticMeshComponent* Flower = Shape(Lab, false, FVector(415, -245, 188), FVector(.16, .16, .095));
        Flower->SetStaticMesh(Sphere);
        Color(Flower, FLinearColor(.92f, .28f, .012f));
        PlantPetals.Add(Flower);
    }
    APointLight* LabLight = GetWorld()->SpawnActor<APointLight>(LabCenter + FVector(0, 0, 275), FRotator::ZeroRotator);
    UPointLightComponent* LabSource = LabLight->FindComponentByClass<UPointLightComponent>();
    LabSource->SetMobility(EComponentMobility::Movable);
    LabSource->SetIntensity(4600);
    LabSource->SetAttenuationRadius(1300);
    LabSource->SetCastShadows(false);

    const auto AddLight = [this](const FRotator& Rotation, float Intensity, bool Shadows)
    {
        ADirectionalLight* Light = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, Rotation);
        UDirectionalLightComponent* Source = Light->FindComponentByClass<UDirectionalLightComponent>();
        Source->SetMobility(EComponentMobility::Movable);
        Source->SetIntensity(Intensity);
        Source->SetCastShadows(Shadows);
    };
    AddLight(FRotator(-35, -35, 0), 3.1f, true);
    AddLight(FRotator(-62, 145, 0), 0.65f, false);
    View = GetWorld()->SpawnActor<ACameraActor>();
    View->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureMethod = true;
    View->GetCameraComponent()->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    View->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureBias = true;
    View->GetCameraComponent()->PostProcessSettings.AutoExposureBias = 0;
    View->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    View->GetCameraComponent()->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
}

FVector ARecallMode::StationLocation(int32 Index) const
{
    return Index <= Airlock ? OnGround(CapsuleLocation + Stations[Index].Position) : Stations[Index].Position;
}

void ARecallMode::UpdateSiteLocations()
{
    for (int32 Index = FirstExperiment; Index < Airlock; ++Index)
    {
        OutdoorStations[Index - FirstExperiment]->SetActorLocation(StationLocation(Index));
    }
}

void ARecallMode::UpdateResearchVisuals()
{
    const float Growth = Stations[FirstLabExperiment].Done ? 1 : ActiveStation == FirstLabExperiment ? Hold / 30 : 0;
    const float StemHeight = 24 + 46 * Growth;
    PlantStem->SetRelativeLocation(FVector(415, -245, 154 + StemHeight / 2));
    PlantStem->SetRelativeScale3D(FVector(.075, .075, StemHeight / 100));
    for (int32 Index = 0; Index < PlantPetals.Num(); ++Index)
    {
        const float Angle = 2 * PI * Index / PlantPetals.Num();
        PlantPetals[Index]->SetVisibility(Growth > .4f);
        PlantPetals[Index]->SetRelativeLocation(FVector(415 + FMath::Cos(Angle) * 12, -245 + FMath::Sin(Angle) * 12, 154 + StemHeight));
    }
}


