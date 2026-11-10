// SPDX-License-Identifier: MIT

#include "Area/AreaInfo.h"

#include "Engine/World.h"
#include "EngineUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaInfo)

const FName AAreaInfo::GeneratedTag(TEXT("AreaGenerated"));

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
