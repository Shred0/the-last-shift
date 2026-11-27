// SPDX-License-Identifier: MIT

// The backdrop's dressing (Brief 28): the masses' modules, the levels of walkways above and below the open segments, the
// pillars under the walkways. Development builds only.
//  tls.Area.Backdrop <masses|layers|pillars|all> <0|1>: switches a part of the current area's recipe for the session and
//    rebuilds the area (no argument: logs the switches).
//  tls.Area.Masses [ReachM], tls.Area.Layers [Above] [Below] [SpacingM] [Scale] [PieceM], tls.Area.Pillars [EveryNth]
//    [MaxLengthM]: set the part's values of the current area's recipe for the session and rebuild the area (no argument:
//    log them with their ini line).
//  tls.BackdropCheck [Quit]: checks Brief 28's criteria 1 to 7 in the area it runs in, then shoots criterion 8's views
//    (tls.Views areabackdrop) to Saved/Screenshots/Brief28.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Run/TheLastShiftSettings.h"
#include "StaticMeshResources.h"

namespace BackdropDebug
{
	static UWorld* GameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld() && (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game))
			{
				return World;
			}
		}
		return nullptr;
	}

	static AAreaGenerator* Gen()
	{
		return AAreaGenerator::Find(GameWorld());
	}

	/** The recipe the current area is built from, writable for the session (the settings' default object). */
	static FAreaRecipe* MutableRecipe()
	{
		const AAreaGenerator* G = Gen();
		return G ? const_cast<FAreaRecipe*>(GetMutableDefault<UTheLastShiftSettings>()->FindRecipe(G->GetAct())) : nullptr;
	}

	static FString IniLine(const FAreaBackdropRecipe& B)
	{
		static const TCHAR* Families[] = { TEXT("None"), TEXT("Hull"), TEXT("Servers"), TEXT("Machinery"), TEXT("ServersAndMachinery") };
		return FString::Printf(TEXT("Backdrop=(bMasses=%s,Family=%s,MassReachM=%.1f,bLayers=%s,LayerCountAbove=%d,LayerCountBelow=%d,LayerSpacingM=%.1f,LayerScale=%.2f,LayerPieceLengthM=%.1f,bPillars=%s,PillarEveryNthFrame=%d,PillarMaxLengthM=%.1f)"),
			B.bMasses ? TEXT("True") : TEXT("False"), Families[FMath::Clamp(static_cast<int32>(B.Family), 0, 4)], B.MassReachM, B.bLayers ? TEXT("True") : TEXT("False"),
			B.LayerCountAbove, B.LayerCountBelow, B.LayerSpacingM, B.LayerScale, B.LayerPieceLengthM, B.bPillars ? TEXT("True") : TEXT("False"), B.PillarEveryNthFrame, B.PillarMaxLengthM);
	}

	static void Rebuild(const TCHAR* Command)
	{
		AAreaGenerator* G = Gen();
		if (!G)
		{
			return;
		}
		const EAreaGenerationState State = G->GetGenerationState();
		if (State == EAreaGenerationState::BuildingWorld || State == EAreaGenerationState::BuildingNavigation)
		{
			UE_LOG(LogArea, Warning, TEXT("%s: the area is being generated; the values apply at the next generation."), Command);
			return;
		}
		UE_LOG(LogArea, Display, TEXT("%s: %s; rebuilding the area (seed %d)."), Command, *IniLine(MutableRecipe()->Backdrop), G->GetSeed());
		G->Regenerate(G->GetSeed(), false);
	}

	// --- tls.BackdropCheck --------------------------------------------------------------------------------------------

	struct FStep
	{
		FString Label;
		/** Runs once; returns the seconds to wait before the next step (negative: run again after that many seconds). */
		TFunction<float()> Run;
	};

	struct FCheckRun
	{
		FTSTicker::FDelegateHandle Ticker;
		TArray<FStep> Steps;
		int32 Index = 0;
		double WaitUntil = 0.0;
		bool bQuit = false;
		int32 Passed = 0;
		int32 Failed = 0;
		FString Map;
		FAreaBackdropRecipe Saved;
		int64 Hash = 0;
		int32 Dressing = 0;
		TArray<int32> DressCounts;
	};
	static FCheckRun GCheck;

	static void Check(bool bPass, const FString& Name, const FString& Detail)
	{
		(bPass ? GCheck.Passed : GCheck.Failed) += 1;
		UE_LOG(LogArea, Display, TEXT("BackdropCheck: %s %s: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	}

	static void Note(const FString& Text)
	{
		UE_LOG(LogArea, Display, TEXT("BackdropCheck: %s"), *Text);
	}

	/** The reference layout hashes (Brief 26) of the four areas. */
	static int64 ReferenceHash(int32 Act)
	{
		switch (Act)
		{
		case 1: return static_cast<int64>(0x7f1032152054f0a3ull);
		case 2: return static_cast<int64>(0xf2a87c443cf7c500ull);
		case 3: return static_cast<int64>(0xf7db963f34395881ull);
		case 4: return static_cast<int64>(0x6cf097ae1c4679cfull);
		default: return 0;
		}
	}

	static int64 MeshTriangles(const UStaticMesh* Mesh)
	{
		const FStaticMeshRenderData* Data = Mesh ? Mesh->GetRenderData() : nullptr;
		return Data && Data->LODResources.Num() > 0 ? Data->LODResources[0].GetNumTriangles() : 0;
	}

	/** Every instance of one of the generator's decor arrays, as built: world transform and world bounds. */
	struct FInstance
	{
		FTransform T;
		FBox Bounds;
		const UInstancedStaticMeshComponent* Component = nullptr;
	};

	static TArray<FInstance> Instances(const AAreaGenerator& G, int32 Mesh)
	{
		TArray<FInstance> Out;
		for (const UInstancedStaticMeshComponent* C : G.GetDecorComponents(Mesh))
		{
			const FBox Local = C->GetStaticMesh() ? C->GetStaticMesh()->GetBoundingBox() : FBox(FVector::ZeroVector, FVector::ZeroVector);
			for (int32 I = 0; I < C->GetInstanceCount(); ++I)
			{
				FInstance Inst;
				C->GetInstanceTransform(I, Inst.T, true);
				Inst.Bounds = Local.TransformBy(Inst.T);
				Inst.Component = C;
				Out.Add(Inst);
			}
		}
		return Out;
	}

	static bool Overlaps(const FBox& A, const FBox& B)
	{
		return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y && A.Min.Z < B.Max.Z && A.Max.Z > B.Min.Z;
	}

	/** The dressing arrays of Brief 16 to 26, counted (criterion 7: the same with the parts off). */
	static TArray<int32> DressCounts(const AAreaGenerator& G)
	{
		return { G.DressWallPanels.Num(), G.DressFloorPlates.Num(), G.DressCeilingPlates.Num(), G.DressCornerPosts.Num(), G.DressCeilingBeams.Num(),
			G.DressConduits.Num(), G.DressCurbs.Num(), G.DressCatwalkFrames.Num(), G.DressGateFrames.Num(), G.DressTwinConduits.Num(), G.DressTwinBends.Num(),
			G.DressTwinCables.Num() };
	}

	static FString JoinCounts(const TArray<int32>& Counts)
	{
		FString Out;
		for (const int32 C : Counts) { Out += FString::Printf(TEXT("%s%d"), Out.IsEmpty() ? TEXT("") : TEXT(","), C); }
		return Out;
	}

	/** Criteria 1, 2, 3, 5 and 6 on the area as it is built now. */
	static void CheckBuilt(const FString& Stage)
	{
		AAreaGenerator* G = Gen();
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		const FAreaRecipe* Recipe = G ? G->GetRecipe() : nullptr;
		if (!G || !Info || !Recipe)
		{
			Check(false, Stage, TEXT("no generated area"));
			return;
		}
		const FAreaBackdropRecipe& B = Recipe->Backdrop;
		const double FT = G->FloorThickness;
		const double Side = G->DecorSideClearance;
		const double Vert = G->DecorVerticalClearance;

		// --- 1: nothing of it collides or feeds the navmesh ---
		{
			int32 Components = 0, Bad = 0;
			for (const UInstancedStaticMeshComponent* C : G->GetDecorComponents(-1))
			{
				++Components;
				Bad += C->GetCollisionEnabled() != ECollisionEnabled::NoCollision || C->CanEverAffectNavigation() ? 1 : 0;
			}
			Check(Bad == 0, Stage + TEXT(" 1 no collision, no navigation"), FString::Printf(TEXT("%d components of the backdrop's dressing, %d with collision or navigation"), Components, Bad));
			FString Why;
			Check(G->DoesPCGMatchPlan(Why), Stage + TEXT(" 1 the plan's boxes built as before"), Why.IsEmpty() ? TEXT("PCG matches the plan") : Why);
		}

		// The walkable floors and the clearance round them, computed here from the area's info (not from the generator's lists).
		TArray<FBox> Walk, Zones;
		for (const FAreaFloorRect& R : Info->FloorRects)
		{
			Walk.Add(R.Box);
			Zones.Add(FBox(R.Box.Min - FVector(Side, Side, Vert), R.Box.Max + FVector(Side, Side, Vert)));
		}
		TArray<FBox2D> ExitStrips;
		for (const FAreaSegmentRecord& S : Info->Segments)
		{
			if (!S.bExitCorridor || S.Direction.IsNearlyZero())
			{
				continue;
			}
			const FVector2D D(S.Direction);
			const FVector2D L(-D.Y, D.X);
			const FVector2D Mouth(S.Mouth);
			// The corridor's far end: its bounds' extent along its direction from the mouth.
			double Length = 0.0;
			for (const FVector2D& Q : { FVector2D(S.Bounds.Min), FVector2D(S.Bounds.Max), FVector2D(S.Bounds.Min.X, S.Bounds.Max.Y), FVector2D(S.Bounds.Max.X, S.Bounds.Min.Y) })
			{
				Length = FMath::Max(Length, FVector2D::DotProduct(Q - Mouth, D));
			}
			const double Out = Length + G->BackdropRadius + G->BackdropClearance;
			FBox2D Strip(ForceInit);
			for (const FVector2D& Q : { Mouth + D * Length + L * (0.5 * S.Width), Mouth + D * Length - L * (0.5 * S.Width), Mouth + D * Out + L * (0.5 * S.Width), Mouth + D * Out - L * (0.5 * S.Width) })
			{
				Strip += Q;
			}
			ExitStrips.Add(FBox2D(Strip.Min - FVector2D(G->BackdropClearance), Strip.Max + FVector2D(G->BackdropClearance)));
		}
		auto InExit = [&ExitStrips](const FBox& Box)
		{
			const FBox2D B2(FVector2D(Box.Min), FVector2D(Box.Max));
			for (const FBox2D& S : ExitStrips)
			{
				if (B2.Min.X < S.Max.X && B2.Max.X > S.Min.X && B2.Min.Y < S.Max.Y && B2.Max.Y > S.Min.Y) { return true; }
			}
			return false;
		};

		// The walked frames' lowest points (criterion 5, and the pillars under the walked level's own frames, which stand in its clearance by design).
		TArray<FVector> WalkedTips, LayerTips;
		for (const FTransform& T : G->DressCatwalkFrames) { WalkedTips.Add(T.GetLocation() - FVector(0.0, 0.0, G->DressFrameTipDepth * T.GetScale3D().Z)); }
		for (const FInstance& F : Instances(*G, AAreaGenerator::DecorLayerFrame)) { LayerTips.Add(F.T.GetLocation() - FVector(0.0, 0.0, G->DressFrameTipDepth * F.T.GetScale3D().Z)); }
		auto NearestTip = [](const TArray<FVector>& Tips, const FVector& P, double& OutDist)
		{
			OutDist = TNumericLimits<double>::Max();
			for (const FVector& Tip : Tips) { OutDist = FMath::Min(OutDist, FVector::Dist(Tip, P)); }
			return OutDist;
		};

		// --- 2: clear of the route and of the exit corridors ---
		int64 TrisByPart[3] = {};
		int32 ByPart[3] = {};
		{
			int32 InWalk = 0, InZone = 0, InExitStrip = 0, Total = 0;
			for (int32 Mesh = 0; Mesh < AAreaGenerator::DecorMeshCount; ++Mesh)
			{
				for (const FInstance& I : Instances(*G, Mesh))
				{
					++Total;
					const int32 Part = AAreaGenerator::GetDecorPart(Mesh);
					++ByPart[Part];
					TrisByPart[Part] += MeshTriangles(I.Component->GetStaticMesh());
					const FBox Box = I.Bounds.ExpandBy(-0.5);
					bool bWalkedPillar = false;
					if (Mesh == AAreaGenerator::DecorPillar)
					{
						double D = 0.0;
						const FVector Top = I.T.GetLocation() + FVector(0.0, 0.0, I.T.GetScale3D().Z * G->DressCornerPostSize.Z);
						bWalkedPillar = NearestTip(WalkedTips, Top, D) <= 5.0;
					}
					for (const FBox& W : Walk)
					{
						// The walkable space: the floor up to the pawn's height (2.2 m), and the floor itself.
						if (Overlaps(Box, FBox(W.Min, FVector(W.Max.X, W.Max.Y, W.Max.Z + 220.0)))) { ++InWalk; break; }
					}
					if (!bWalkedPillar)
					{
						for (const FBox& Z : Zones)
						{
							if (Overlaps(Box, Z)) { ++InZone; break; }
						}
					}
					InExitStrip += InExit(Box) ? 1 : 0;
				}
			}
			Check(InWalk + InZone + InExitStrip == 0, Stage + TEXT(" 2 clear of the route"), FString::Printf(TEXT("%d pieces: in the walkable space %d, within 2 m of its sides or 8 m above or below a floor %d (pillars under the walked level's own frames left out), in an exit corridor's line or clearance %d"),
				Total, InWalk, InZone, InExitStrip));
		}

		// --- 6: the budget ---
		// The backdrop's limit is GPU time, measured by tls.Views area (Area2_ServerHalls: the heaviest view at most 4.0 ms
		// in Normal time and Stopped with a scan, the frame of 8 scans at most 5.0 ms). The triangles are counted and reported;
		// the cap here only guards against a mistake (it was a budget of 600000 before the levels went to four each way).
		{
			constexpr int64 GuardTriangles = 1500000;
			const int64 Sum = TrisByPart[0] + TrisByPart[1] + TrisByPart[2];
			Check(Sum <= GuardTriangles, Stage + TEXT(" 6 budget"), FString::Printf(TEXT("triangles: masses %lld (%d modules), layers %lld (%d pieces), pillars %lld (%d), sum %lld (guard %lld; the limit is GPU time, tls.Views area)"),
				TrisByPart[0], ByPart[0], TrisByPart[1], ByPart[1], TrisByPart[2], ByPart[2], Sum, GuardTriangles));
		}

		// --- 3: the masses ---
		{
			const TArray<FBox>& Masses = G->GetPublishedPieces(EAreaPieceKind::Backdrop);
			int32 Modules = 0, OffFace = 0, WrongFamily = 0, BadScale = 0, OnTurnedAway = 0;
			TMap<int64, int32> OnFace;  // mass * 8 + face -> modules
			double MinF = TNumericLimits<double>::Max(), MaxF = -1.0;
			int32 ByFamily[3] = {};
			for (int32 Mesh = AAreaGenerator::DecorHullRib; Mesh <= AAreaGenerator::DecorMachRing; ++Mesh)
			{
				for (const FInstance& I : Instances(*G, Mesh))
				{
					++Modules;
					++ByFamily[Mesh / 3];
					const FVector N = I.T.GetUnitAxis(EAxis::X);
					const FVector P = I.T.GetLocation();
					const FVector S = I.T.GetScale3D();
					for (const double F : { S.Y, S.Z })
					{
						MinF = FMath::Min(MinF, F);
						MaxF = FMath::Max(MaxF, F);
						BadScale += F < G->DecorModuleMinFactor - 1e-3 || F > G->DecorModuleMaxFactor + 1e-3 ? 1 : 0;
					}
					// The face its back lies on.
					int32 Found = -1;
					for (int32 M = 0; M < Masses.Num() && Found < 0; ++M)
					{
						const FBox& Box = Masses[M];
						for (int32 Face = 0; Face < 6; ++Face)
						{
							const int32 Axis = Face / 2;
							FVector FN = FVector::ZeroVector;
							FN[Axis] = Face % 2 == 0 ? 1.0 : -1.0;
							const double Plane = Face % 2 == 0 ? Box.Max[Axis] : Box.Min[Axis];
							if (FVector::DotProduct(N, FN) < 0.999 || FMath::Abs(P[Axis] - Plane) > 1.0)
							{
								continue;
							}
							bool bInside = true;
							for (int32 A = 0; A < 3; ++A)
							{
								bInside &= A == Axis || (P[A] >= Box.Min[A] - 1.0 && P[A] <= Box.Max[A] + 1.0);
							}
							if (bInside)
							{
								Found = M * 8 + Face;
								const EAreaBackdropFamily Want = B.Family == EAreaBackdropFamily::ServersAndMachinery
									? (M % 2 == 0 ? EAreaBackdropFamily::Servers : EAreaBackdropFamily::Machinery) : B.Family;
								const int32 WantIndex = Want == EAreaBackdropFamily::Hull ? 0 : Want == EAreaBackdropFamily::Servers ? 1 : Want == EAreaBackdropFamily::Machinery ? 2 : -1;
								WrongFamily += Mesh / 3 != WantIndex ? 1 : 0;
								break;
							}
						}
					}
					if (Found < 0)
					{
						++OffFace;
						continue;
					}
					OnFace.FindOrAdd(Found) += 1;
				}
			}
			// Every face turned to the route within reach carries modules (unless all its cells in reach stand against another
			// mass or in the route's clearance), and no other face does.
			int32 Toward = 0, Dressed = 0, Missing = 0, Beyond = 0, Mismatch = 0;
			for (const AAreaGenerator::FDecorFaceRecord& F : G->DecorFaces)
			{
				const int32* Count = OnFace.Find(F.Mass * 8 + F.Face);
				const int32 Built = Count ? *Count : 0;
				if (!F.bTowardRoute)
				{
					OnTurnedAway += Built;
					continue;
				}
				++Toward;
				Beyond += F.CellsInReach == 0 ? 1 : 0;
				Dressed += Built > 0 ? 1 : 0;
				Missing += F.CellsInReach > F.SkippedMass + F.SkippedClear && Built == 0 ? 1 : 0;
				Mismatch += Built != F.Placed ? 1 : 0;
			}
			const bool bOn = B.bMasses && B.Family != EAreaBackdropFamily::None;
			Check(OffFace == 0 && WrongFamily == 0 && BadScale == 0 && OnTurnedAway == 0 && Missing == 0 && Mismatch == 0 && (!bOn || Modules > 0), Stage + TEXT(" 3 masses"),
				FString::Printf(TEXT("family %s, %d masses: faces turned to the route %d, dressed %d, beyond reach (%.0f m) %d, within reach and not dressed %d; modules %d (hull %d, servers %d, machinery %d), not on a mass's face %d, on a face turned away %d, of another family %d, built and planned differing on %d faces; factors x%.3f..x%.3f, outside x%.2f..x%.2f %d"),
					*UEnum::GetValueAsString(B.Family), Masses.Num(), Toward, Dressed, B.MassReachM, Beyond, Missing, Modules, ByFamily[0], ByFamily[1], ByFamily[2], OffFace,
					OnTurnedAway, WrongFamily, Mismatch, Modules > 0 ? MinF : 0.0, Modules > 0 ? MaxF : 0.0, G->DecorModuleMinFactor, G->DecorModuleMaxFactor, BadScale));
		}

		// --- 5: the pillars ---
		{
			const TArray<FBox>& Masses = G->GetPublishedPieces(EAreaPieceKind::Backdrop);
			TArray<FBox> Decks;
			for (const FInstance& I : Instances(*G, AAreaGenerator::DecorLayerPlate)) { Decks.Add(I.Bounds); }
			int32 Total = 0, OffTip = 0, OnGeometry = 0, AtLength = 0, CutAtClearance = 0, Floating = 0, Walked = 0;
			double WorstTip = 0.0;
			const double MaxLen = B.PillarMaxLengthM * 100.0;
			for (const FInstance& I : Instances(*G, AAreaGenerator::DecorPillar))
			{
				++Total;
				const double Len = I.T.GetScale3D().Z * G->DressCornerPostSize.Z;
				const FVector Foot = I.T.GetLocation();
				const FVector Top = Foot + FVector(0.0, 0.0, Len);
				double DW = 0.0, DL = 0.0;
				NearestTip(WalkedTips, Top, DW);
				NearestTip(LayerTips, Top, DL);
				const double D = FMath::Min(DW, DL);
				WorstTip = FMath::Max(WorstTip, D);
				OffTip += D > 5.0 ? 1 : 0;
				Walked += DW <= 5.0 ? 1 : 0;
				// What its foot stands on: the top of a mass, a level's deck or a floor at the foot's XY, or the route's clearance.
				const double Half = 0.5 * G->DressCornerPostSize.X;
				auto TopAt = [&](const FBox& Box) { return Box.Min.X < Foot.X + Half && Box.Max.X > Foot.X - Half && Box.Min.Y < Foot.Y + Half && Box.Max.Y > Foot.Y - Half && FMath::Abs(Box.Max.Z - Foot.Z) <= 1.0; };
				bool bGeometry = false, bCut = false;
				for (const FBox& Box : Masses) { bGeometry |= TopAt(Box); }
				for (const FBox& Box : Decks) { bGeometry |= Box.Min.X < Foot.X + Half && Box.Max.X > Foot.X - Half && Box.Min.Y < Foot.Y + Half && Box.Max.Y > Foot.Y - Half && FMath::Abs(Box.Max.Z - Foot.Z) <= 2.0; }
				for (const FBox& Box : Walk) { bGeometry |= TopAt(Box); }
				for (const FBox& Box : Zones) { bCut |= TopAt(Box); }
				if (bGeometry) { ++OnGeometry; }
				else if (FMath::Abs(Len - MaxLen) <= 1.0) { ++AtLength; }
				else if (bCut && DW > 5.0) { ++CutAtClearance; }
				else { ++Floating; }
			}
			const bool bOn = B.bPillars;
			Check(OffTip == 0 && Floating == 0 && (!bOn || Total > 0), Stage + TEXT(" 5 pillars"),
				FString::Printf(TEXT("%d pillars (%d under the walked level, %d under the levels): top within 5 cm of a frame's lowest point: worst %.2f cm, %d off; foot on geometry %d, at the length %.0f m %d, cut at the route's clearance %d, none of these %d"),
					Total, Walked, Total - Walked, Total > 0 ? WorstTip : 0.0, OffTip, OnGeometry, B.PillarMaxLengthM, AtLength, CutAtClearance, Floating));
		}
	}

	/** Criterion 4 on the levels as built: the levels of every open segment at the configured heights, each reaching farther out than the walked one. */
	static void CheckLevels(const FString& Stage)
	{
		AAreaGenerator* G = Gen();
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		const FAreaRecipe* Recipe = G ? G->GetRecipe() : nullptr;
		if (!G || !Info || !Recipe)
		{
			Check(false, Stage, TEXT("no generated area"));
			return;
		}
		const FAreaBackdropRecipe& B = Recipe->Backdrop;
		const int32 Above = B.bLayers ? B.LayerCountAbove : 0;
		const int32 Below = B.bLayers ? B.LayerCountBelow : 0;
		const double Spacing = B.LayerSpacingM * 100.0;
		// The heights the plates stand at (their pivot at the deck's underside), from the instances as built.
		TSet<int32> PlateHeights;
		for (const FInstance& I : Instances(*G, AAreaGenerator::DecorLayerPlate))
		{
			PlateHeights.Add(FMath::RoundToInt(I.T.GetLocation().Z + G->FloorThickness));
		}
		int32 OpenSegments = 0, MissingLevels = 0, WrongHeight = 0, NotFarther = 0, Empty = 0;
		FString Lines;
		for (int32 S = 0; S < Info->Segments.Num(); ++S)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[S];
			if (Seg.bClosed)
			{
				continue;
			}
			++OpenSegments;
			FString Line = FString::Printf(TEXT("segment %d %s:"), S, LexToString(Seg.Type));
			for (int32 K = -Below; K <= Above; ++K)
			{
				if (K == 0)
				{
					continue;
				}
				const AAreaGenerator::FDecorLevelRecord* Rec = G->DecorLevels.FindByPredicate([S, K](const AAreaGenerator::FDecorLevelRecord& R) { return R.Segment == S && R.Level == K; });
				if (!Rec)
				{
					++MissingLevels;
					Line += FString::Printf(TEXT(" [%+d missing]"), K);
					continue;
				}
				WrongHeight += FMath::Abs(Rec->Z - K * Spacing) > 0.5 || !PlateHeights.Contains(FMath::RoundToInt(Rec->Z)) ? 1 : 0;
				Empty += Rec->Plates == 0 ? 1 : 0;
				NotFarther += Rec->Reach <= Rec->WalkedReach ? 1 : 0;
				Line += FString::Printf(TEXT(" [%+d at %.0f m, turned %d, plates %d curbs %d frames %d, cut %d, reach %.0f m against the walked %.0f m]"), K, Rec->Z / 100.0, Rec->Turns * 90,
					Rec->Plates, Rec->Curbs, Rec->Frames, Rec->PiecesCut, Rec->Reach / 100.0, Rec->WalkedReach / 100.0);
			}
			Lines += Line + TEXT(" ");
		}
		// No level stands where the recipe asks for none.
		int32 Extra = 0;
		for (const AAreaGenerator::FDecorLevelRecord& R : G->DecorLevels)
		{
			Extra += R.Level > Above || R.Level < -Below ? 1 : 0;
		}
		Check(MissingLevels == 0 && WrongHeight == 0 && NotFarther == 0 && Empty == 0 && Extra == 0, Stage + TEXT(" 4 levels"),
			FString::Printf(TEXT("%d above and %d below, %.1f m apart, x%.2f: %d open segments, levels missing %d, at a wrong height %d, empty %d, not reaching farther than the walked level %d, extra %d; %s"),
				Above, Below, B.LayerSpacingM, B.LayerScale, OpenSegments, MissingLevels, WrongHeight, Empty, NotFarther, Extra, *Lines));
	}

	static void AddRebuild(const FString& Label)
	{
		GCheck.Steps.Add({ Label, [Label]()
		{
			if (AAreaGenerator* G = Gen())
			{
				Note(FString::Printf(TEXT("%s: rebuilding (seed %d): %s"), *Label, G->GetSeed(), *IniLine(G->GetRecipe()->Backdrop)));
				G->Regenerate(G->GetSeed(), false);
			}
			return 1.0f;
		} });
		GCheck.Steps.Add({ Label + TEXT(" wait"), []()
		{
			const AAreaGenerator* G = Gen();
			const EAreaGenerationState State = G ? G->GetGenerationState() : EAreaGenerationState::Failed;
			return State == EAreaGenerationState::Ready || State == EAreaGenerationState::Failed ? 1.0f : -0.5f;
		} });
	}

	static bool Tick(float)
	{
		if (FPlatformTime::Seconds() < GCheck.WaitUntil)
		{
			return true;
		}
		if (GCheck.Index >= GCheck.Steps.Num())
		{
			GCheck.Ticker.Reset();
			return false;
		}
		const FStep& Step = GCheck.Steps[GCheck.Index];
		const float Wait = Step.Run ? Step.Run() : 0.f;
		if (Wait >= 0.f)
		{
			++GCheck.Index;
		}
		GCheck.WaitUntil = FPlatformTime::Seconds() + FMath::Abs(Wait);
		return true;
	}

	static void Start(bool bQuit)
	{
		if (GCheck.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GCheck.Ticker);
		}
		GCheck = FCheckRun();
		GCheck.bQuit = bQuit;
		TArray<FStep>& S = GCheck.Steps;
		S.Add({ TEXT("wait for the area"), []()
		{
			UWorld* World = GameWorld();
			return World && Gen() && AAreaGenerator::IsAreaReady(World) ? 1.0f : -0.5f;
		} });
		S.Add({ TEXT("as loaded"), []()
		{
			AAreaGenerator* G = Gen();
			GCheck.Map = UWorld::RemovePIEPrefix(GameWorld()->GetOutermost()->GetName());
			GCheck.Saved = G->GetRecipe()->Backdrop;
			GCheck.Hash = G->GetLayoutHash();
			GCheck.Dressing = G->GetDressingInstanceCount();
			GCheck.DressCounts = DressCounts(*G);
			Note(FString::Printf(TEXT("===== %s, act %d, seed %d: %s ====="), *GCheck.Map, G->GetAct(), G->GetSeed(), *IniLine(GCheck.Saved)));
			Note(FString::Printf(TEXT("BACKDROP %s"), *G->LastBackdropReport));
			const int64 Reference = ReferenceHash(G->GetAct());
			Check(G->GetLayoutHash() == Reference, TEXT("1 layout hash"), FString::Printf(TEXT("%016llx, reference %016llx"), static_cast<uint64>(G->GetLayoutHash()), static_cast<uint64>(Reference)));
			Check(G->bLastValidationPassed && G->LastSeedsTried == 1, TEXT("1 validation at the first try"), FString::Printf(TEXT("validation %s, seeds tried %d (configured seed %d)"),
				G->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED"), G->LastSeedsTried, G->GetSeed()));
			CheckBuilt(TEXT("loaded"));
			CheckLevels(TEXT("loaded"));
			return 0.5f;
		} });
		// Criterion 4: other counts, spacing and scale for the session (the fields the ini sets), then the configured ones back.
		S.Add({ TEXT("layers changed"), []()
		{
			FAreaRecipe* R = MutableRecipe();
			R->Backdrop.LayerCountAbove = 1;
			R->Backdrop.LayerCountBelow = 3;
			R->Backdrop.LayerSpacingM = 15.f;
			R->Backdrop.LayerScale = 1.3f;
			return 0.f;
		} });
		AddRebuild(TEXT("layers 1 above, 3 below, 15 m, x1.3"));
		S.Add({ TEXT("layers changed check"), []()
		{
			CheckLevels(TEXT("changed"));
			const AAreaGenerator* G = Gen();
			Check(G->GetLayoutHash() == GCheck.Hash, TEXT("changed 1 layout hash"), FString::Printf(TEXT("%016llx"), static_cast<uint64>(G->GetLayoutHash())));
			return 0.f;
		} });
		// Criterion 7: the three parts off.
		S.Add({ TEXT("parts off"), []()
		{
			FAreaRecipe* R = MutableRecipe();
			R->Backdrop = GCheck.Saved;
			R->Backdrop.bMasses = false;
			R->Backdrop.bLayers = false;
			R->Backdrop.bPillars = false;
			return 0.f;
		} });
		AddRebuild(TEXT("parts off"));
		S.Add({ TEXT("parts off check"), []()
		{
			const AAreaGenerator* G = Gen();
			int32 Arrays = 0;
			for (int32 Mesh = 0; Mesh < AAreaGenerator::DecorMeshCount; ++Mesh) { Arrays += G->GetDecorArray(Mesh).Num(); }
			const TArray<int32> Counts = DressCounts(*G);
			Check(G->GetDecorInstanceCount() == 0 && Arrays == 0, TEXT("7 switched off: none of the parts' pieces"), FString::Printf(TEXT("instances %d, planned %d"), G->GetDecorInstanceCount(), Arrays));
			Check(G->GetDressingInstanceCount() == GCheck.Dressing && Counts == GCheck.DressCounts, TEXT("7 switched off: the dressing as before"),
				FString::Printf(TEXT("dressing instances %d (with the parts on %d); arrays %s (with the parts on %s)"), G->GetDressingInstanceCount(), GCheck.Dressing, *JoinCounts(Counts), *JoinCounts(GCheck.DressCounts)));
			Check(G->GetLayoutHash() == GCheck.Hash && G->bLastValidationPassed, TEXT("7 switched off: hash and validation"), FString::Printf(TEXT("%016llx, validation %s"),
				static_cast<uint64>(G->GetLayoutHash()), G->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED")));
			MutableRecipe()->Backdrop = GCheck.Saved;
			return 0.f;
		} });
		AddRebuild(TEXT("configured values back"));
		S.Add({ TEXT("back check"), []()
		{
			const AAreaGenerator* G = Gen();
			Check(G->GetLayoutHash() == ReferenceHash(G->GetAct()) && G->bLastValidationPassed, TEXT("back 1 hash and validation"), FString::Printf(TEXT("%016llx, validation %s"),
				static_cast<uint64>(G->GetLayoutHash()), G->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED")));
			Note(FString::Printf(TEXT("===== %s: passed %d, failed %d ====="), *GCheck.Map, GCheck.Passed, GCheck.Failed));
			// Criterion 8's shots.
			const FString Label = FString::Printf(TEXT("%s"), *FPaths::GetBaseFilename(GCheck.Map));
			GEngine->Exec(GameWorld(), *FString::Printf(TEXT("tls.Views areabackdrop %s %d"), *Label, GCheck.bQuit ? 1 : 0));
			return 0.f;
		} });
		GCheck.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaBackdropCommand(
	TEXT("tls.Area.Backdrop"),
	TEXT("tls.Area.Backdrop <masses|layers|pillars|all> <0|1>: (PIE or a package) switches a part of the backdrop's dressing (Brief 28) in the current area's recipe for the session and rebuilds the area; no argument: logs the switches and the recipe's Backdrop line."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace BackdropDebug;
		FAreaRecipe* R = MutableRecipe();
		if (!R)
		{
			UE_LOG(LogArea, Warning, TEXT("tls.Area.Backdrop: needs a generated area."));
			return;
		}
		if (Args.Num() < 2)
		{
			UE_LOG(LogArea, Display, TEXT("tls.Area.Backdrop: masses %d, layers %d, pillars %d; %s"), R->Backdrop.bMasses ? 1 : 0, R->Backdrop.bLayers ? 1 : 0, R->Backdrop.bPillars ? 1 : 0, *IniLine(R->Backdrop));
			return;
		}
		const FString Part = Args[0].ToLower();
		const bool bOn = FCString::Atoi(*Args[1]) != 0;
		if (Part == TEXT("masses") || Part == TEXT("all")) { R->Backdrop.bMasses = bOn; }
		if (Part == TEXT("layers") || Part == TEXT("all")) { R->Backdrop.bLayers = bOn; }
		if (Part == TEXT("pillars") || Part == TEXT("all")) { R->Backdrop.bPillars = bOn; }
		Rebuild(TEXT("tls.Area.Backdrop"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaMassesCommand(
	TEXT("tls.Area.Masses"),
	TEXT("tls.Area.Masses [ReachM]: (PIE or a package) the masses' faces are dressed out to this distance from the walkable floors, for the session; rebuilds the area. No argument: logs the recipe's Backdrop line."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace BackdropDebug;
		FAreaRecipe* R = MutableRecipe();
		if (!R) { return; }
		if (Args.Num() == 0) { UE_LOG(LogArea, Display, TEXT("tls.Area.Masses: %s"), *IniLine(R->Backdrop)); return; }
		R->Backdrop.MassReachM = FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 300.f);
		Rebuild(TEXT("tls.Area.Masses"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaLayersCommand(
	TEXT("tls.Area.Layers"),
	TEXT("tls.Area.Layers [Above] [Below] [SpacingM] [Scale] [PieceM]: (PIE or a package) the levels of walkways above and below the open segments, for the session; rebuilds the area. No argument: logs the recipe's Backdrop line."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace BackdropDebug;
		FAreaRecipe* R = MutableRecipe();
		if (!R) { return; }
		if (Args.Num() == 0) { UE_LOG(LogArea, Display, TEXT("tls.Area.Layers: %s"), *IniLine(R->Backdrop)); return; }
		R->Backdrop.LayerCountAbove = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 4);
		if (Args.Num() > 1) { R->Backdrop.LayerCountBelow = FMath::Clamp(FCString::Atoi(*Args[1]), 0, 4); }
		if (Args.Num() > 2) { R->Backdrop.LayerSpacingM = FMath::Clamp(FCString::Atof(*Args[2]), 4.f, 60.f); }
		if (Args.Num() > 3) { R->Backdrop.LayerScale = FMath::Clamp(FCString::Atof(*Args[3]), 1.f, 4.f); }
		if (Args.Num() > 4) { R->Backdrop.LayerPieceLengthM = FMath::Clamp(FCString::Atof(*Args[4]), 1.f, 40.f); }
		Rebuild(TEXT("tls.Area.Layers"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsAreaPillarsCommand(
	TEXT("tls.Area.Pillars"),
	TEXT("tls.Area.Pillars [EveryNthFrame] [MaxLengthM]: (PIE or a package) the pillars under the walkways, for the session; rebuilds the area. No argument: logs the recipe's Backdrop line."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace BackdropDebug;
		FAreaRecipe* R = MutableRecipe();
		if (!R) { return; }
		if (Args.Num() == 0) { UE_LOG(LogArea, Display, TEXT("tls.Area.Pillars: %s"), *IniLine(R->Backdrop)); return; }
		R->Backdrop.PillarEveryNthFrame = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 20);
		if (Args.Num() > 1) { R->Backdrop.PillarMaxLengthM = FMath::Clamp(FCString::Atof(*Args[1]), 1.f, 300.f); }
		Rebuild(TEXT("tls.Area.Pillars"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsBackdropCheckCommand(
	TEXT("tls.BackdropCheck"),
	TEXT("tls.BackdropCheck [Quit=0]: (PIE or a package, an area map) Brief 28's criteria 1 to 7 on the area it runs in: the layout hash and the validation at the first try, no collision and no navigation, nothing of the parts in the route's clearance or the exit corridors', the masses' faces and modules, the levels (also with 1 above, 3 below, 15 m, x1.3 for the session), the pillars' tops and feet, the triangles counted against a guard of 1500000 (the limit is GPU time, measured by tls.Views area), the three parts off; then criterion 8's shots (tls.Views areabackdrop) to Saved/Screenshots/Brief28."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		BackdropDebug::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

#endif
