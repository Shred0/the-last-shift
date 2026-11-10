// SPDX-License-Identifier: MIT

#include "Area/AreaInfo.h"

#include "Area/AreaGeometry.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Residue/Residue.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaInfo)

const FName AAreaInfo::GeneratedTag(TEXT("AreaGenerated"));

namespace AreaInfoHash
{
	static void AddTransform(FString& Out, const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		const FVector S = T.GetScale3D();
		Out += FString::Printf(TEXT("|%d,%d,%d|%d,%d,%d|%d,%d,%d"),
			FMath::RoundToInt(L.X), FMath::RoundToInt(L.Y), FMath::RoundToInt(L.Z),
			FMath::RoundToInt(R.Pitch * 10.0), FMath::RoundToInt(R.Yaw * 10.0), FMath::RoundToInt(R.Roll * 10.0),
			FMath::RoundToInt(S.X * 100.0), FMath::RoundToInt(S.Y * 100.0), FMath::RoundToInt(S.Z * 100.0));
	}

	static uint64 Fnv1a(const FString& Text, uint64 Hash)
	{
		for (const TCHAR Char : Text)
		{
			Hash ^= static_cast<uint64>(Char);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}
}

AAreaInfo* AAreaInfo::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AAreaInfo> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

int64 AAreaInfo::ComputeLayoutHash(const UWorld* World, int32& OutActorCount)
{
	OutActorCount = 0;
	if (!World)
	{
		return 0;
	}
	TArray<FString> Lines;
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor || !Actor->ActorHasTag(GeneratedTag) || Actor->IsA<AAreaInfo>())
		{
			continue;
		}
		++OutActorCount;
		FString Line = Actor->GetClass()->GetName();
		AreaInfoHash::AddTransform(Line, Actor->GetActorTransform());
		if (const AAreaGeometry* Geometry = Cast<AAreaGeometry>(Actor))
		{
			for (const UInstancedStaticMeshComponent* Component : Geometry->GetAllComponents())
			{
				Line += FString::Printf(TEXT("|%s:%d"), *Component->GetName(), Component->GetInstanceCount());
				for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
				{
					FTransform Instance;
					Component->GetInstanceTransform(Index, Instance, false);
					AreaInfoHash::AddTransform(Line, Instance);
				}
			}
		}
		else if (const AResidue* Residue = Cast<AResidue>(Actor))
		{
			Line += FString::Printf(TEXT("|regen=%d"), Residue->bRegenerates ? 1 : 0);
		}
		else if (const ADoorDevice* Door = Cast<ADoorDevice>(Actor))
		{
			Line += FString::Printf(TEXT("|powered=%d"), FMath::RoundToInt(Door->PoweredSeconds * 100.f));
		}
		else if (const APowerTerminal* Terminal = Cast<APowerTerminal>(Actor))
		{
			Line += FString::Printf(TEXT("|targets=%d"), Terminal->Targets.Num());
		}
		Lines.Add(MoveTemp(Line));
	}
	if (const AAreaInfo* Info = Find(World))
	{
		++OutActorCount;
		Lines.Add(FString::Printf(TEXT("AreaInfo|seed=%d|segments=%d|route=%d"), Info->Seed, Info->Segments.Num(), FMath::RoundToInt(Info->GetRouteLength())));
	}
	Lines.Sort();
	uint64 Hash = 14695981039346656037ull;
	for (const FString& Line : Lines)
	{
		Hash = AreaInfoHash::Fnv1a(Line, Hash);
		Hash = AreaInfoHash::Fnv1a(TEXT("\n"), Hash);
	}
	return static_cast<int64>(Hash);
}

float AAreaInfo::GetRouteLength() const
{
	double Length = 0.0;
	for (int32 Index = 1; Index < RoutePoints.Num(); ++Index)
	{
		Length += FVector::Dist(RoutePoints[Index - 1], RoutePoints[Index]);
	}
	return static_cast<float>(Length);
}

FVector AAreaInfo::GetRoutePointAtDistance(float Distance, int32& OutSegment) const
{
	OutSegment = -1;
	if (RoutePoints.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	double Walked = 0.0;
	for (int32 Index = 1; Index < RoutePoints.Num(); ++Index)
	{
		const double Piece = FVector::Dist(RoutePoints[Index - 1], RoutePoints[Index]);
		if (Walked + Piece >= Distance || Index == RoutePoints.Num() - 1)
		{
			OutSegment = RoutePointSegments.IsValidIndex(Index - 1) ? RoutePointSegments[Index - 1] : -1;
			const double Alpha = Piece > 0.0 ? FMath::Clamp((Distance - Walked) / Piece, 0.0, 1.0) : 0.0;
			return FMath::Lerp(RoutePoints[Index - 1], RoutePoints[Index], Alpha);
		}
		Walked += Piece;
	}
	OutSegment = RoutePointSegments.Num() > 0 ? RoutePointSegments[0] : -1;
	return RoutePoints[0];
}

FVector AAreaInfo::GetRouteDirectionAtDistance(float Distance) const
{
	double Walked = 0.0;
	for (int32 Index = 1; Index < RoutePoints.Num(); ++Index)
	{
		const double Piece = FVector::Dist(RoutePoints[Index - 1], RoutePoints[Index]);
		if ((Walked + Piece >= Distance || Index == RoutePoints.Num() - 1) && Piece > 0.0)
		{
			return (RoutePoints[Index] - RoutePoints[Index - 1]).GetSafeNormal2D();
		}
		Walked += Piece;
	}
	return FVector::ForwardVector;
}

float AAreaInfo::ProjectToRoute(const FVector& Location, int32& OutSegment, float& OutOffset) const
{
	OutSegment = -1;
	OutOffset = -1.f;
	double Best = TNumericLimits<double>::Max();
	double BestDistance = 0.0;
	double Walked = 0.0;
	for (int32 Index = 1; Index < RoutePoints.Num(); ++Index)
	{
		const FVector A(RoutePoints[Index - 1].X, RoutePoints[Index - 1].Y, 0.0);
		const FVector B(RoutePoints[Index].X, RoutePoints[Index].Y, 0.0);
		const FVector P(Location.X, Location.Y, 0.0);
		const FVector Closest = FMath::ClosestPointOnSegment(P, A, B);
		const double D = FVector::Dist(P, Closest);
		if (D < Best)
		{
			Best = D;
			BestDistance = Walked + FVector::Dist(A, Closest);
			OutSegment = RoutePointSegments.IsValidIndex(Index - 1) ? RoutePointSegments[Index - 1] : -1;
		}
		Walked += FVector::Dist(A, B);
	}
	OutOffset = Best < TNumericLimits<double>::Max() ? static_cast<float>(Best) : -1.f;
	return static_cast<float>(BestDistance);
}

int32 AAreaInfo::FindFloorRect(const FVector& Location, float Tolerance) const
{
	for (int32 Index = 0; Index < FloorRects.Num(); ++Index)
	{
		const FBox& Box = FloorRects[Index].Box;
		if (Location.X >= Box.Min.X - Tolerance && Location.X <= Box.Max.X + Tolerance && Location.Y >= Box.Min.Y - Tolerance && Location.Y <= Box.Max.Y + Tolerance)
		{
			return Index;
		}
	}
	return -1;
}

int32 AAreaInfo::FindSegmentOfType(EAreaSegmentType Type) const
{
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		if (Segments[Index].Type == Type)
		{
			return Index;
		}
	}
	return -1;
}
