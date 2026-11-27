// SPDX-License-Identifier: MIT

// Brief 36B: tls.Chamber.Build, an editor tool that lays the battery's access hall into the level open in the editor
// (Epilogue_Chamber), from the plan of the modelling session's blockout (TheLastShift-Sources/Models/M2/Hall_Blockout.blend
// and scripts/blockout.py; Blender metres turned into Unreal centimetres: X = 100 x, Y = -100 y, a yaw a = -a). The pieces
// it places are ordinary placed actors, tagged Chamber and saved with the map: the map stays hand-built (nothing is
// generated at load). It removes the old room's pieces first: every static mesh actor and every light of the level; the
// game's actors (the chamber, its terminal, the lock's door, the monitor room, the light group, the player start) are kept
// and moved. Run it in the editor with the map open and no play session, then save the map.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && WITH_EDITOR

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/LightGroupDevice.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/ChamberBattery.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Epilogue/EpilogueChamber.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Prologue/PrologueEntryDoor.h"
#include "Prologue/PrologueMonitorRoom.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "TheLastShift.h"

namespace ChamberBuild
{
	// ------------------------------------------------------------ the plan (cm)
	static constexpr double Half = 2000.0;           // inside 40 x 40 m
	static constexpr double High = 2100.0;           // six wall panels of 3.5 m
	static constexpr double PanelH = 350.0;
	static constexpr double Rows = 6;
	static constexpr double WellApothem = 385.0;     // the well's and the shaft's panel faces (a 24-gon; the blockout's 7.4 m well,
	                                                 // its faces 15 cm outside the parapet's inner face so that the ribs clear the spines)
	static constexpr int32 WellSides = 24;
	static constexpr double WellDepth = 1400.0;
	static constexpr double ShaftHeight = 700.0;
	static constexpr double ParapetOuter = 420.0;
	static constexpr double GalleryZ = 700.0;
	static constexpr double LockFront = 2040.0;      // the lock's inner face toward the hall
	static constexpr double LockBack = 2440.0;
	static constexpr double LockHalf = 200.0;
	static constexpr double FrameHalf = 125.0;       // SM_KitDoorFrame: 250 x 325, 52 deep (x 1.12 through its wall)
	static constexpr double FrameHeight = 325.0;
	static constexpr double FrameGap = 2.0;          // pieces next to an opening stop 2 cm short of the frame
	static constexpr double DeckDepth = 30.0;        // the walkway's decks: floor plates x 3 in thickness
	static constexpr double RibTip = 10.0;           // the wall panels' ribs stand 10 cm in front of their slab's face
	static constexpr double Slope = 0.25;            // 1 in 4
	static constexpr double DuctZ = 1575.0;
	static constexpr double SocketX = 370.0;
	static constexpr double DuctRingRadius = 6000.0;

	struct FBuild
	{
		UWorld* World = nullptr;
		int32 Count = 0;
		TMap<FString, int32> ByMesh;

		UStaticMesh* Mesh(const TCHAR* Folder, const TCHAR* Name) const
		{
			return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/TheLastShift/Art/%s/%s.%s"), Folder, Name, Name));
		}

		AStaticMeshActor* Put(UStaticMesh* M, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FString& Folder, const FString& Label, bool bMovable = false)
		{
			if (!M)
			{
				return nullptr;
			}
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Loc, Rot, P);
			if (!A)
			{
				return nullptr;
			}
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(bMovable ? EComponentMobility::Movable : EComponentMobility::Static);
			C->SetStaticMesh(M);
			A->SetActorScale3D(Scale);
			A->Tags.AddUnique(TEXT("Chamber"));
			A->SetActorLabel(Label);
			A->SetFolderPath(*(FString(TEXT("Chamber/")) + Folder));
			++Count;
			ByMesh.FindOrAdd(M->GetName()) += 1;
			return A;
		}
	};

	/** Blender metres (x, y) to Unreal centimetres. */
	static FVector B2U(double X, double Y, double Z) { return FVector(100.0 * X, -100.0 * Y, 100.0 * Z); }
	static double YawB(double Radians) { return -FMath::RadiansToDegrees(Radians); }

	/** A wall of panels along one side of the hall, its face at the side's line, from A to B (cm along the wall), rows R0 to R1. */
	static void WallRun(FBuild& Bd, UStaticMesh* Panel, int32 Side, double From, double To, double Z0, double Z1, const FString& Name)
	{
		// Side 0: right (X = +Half, facing -X), 1: left (X = -Half), 2: far (Y = -Half), 3: entry (Y = +Half).
		const double Len = To - From;
		if (Len < 1.0 || Z1 - Z0 < 1.0)
		{
			return;
		}
		const int32 N = FMath::Max(1, FMath::RoundToInt(Len / 200.0));
		const double W = Len / N;
		for (int32 R = 0; Z0 + R * PanelH < Z1 - 0.5; ++R)
		{
			const double Z = Z0 + R * PanelH;
			const double H = FMath::Min(PanelH, Z1 - Z);
			for (int32 I = 0; I < N; ++I)
			{
				const double C = From + W * (I + 0.5);
				FVector L;
				double Yaw = 0.0;
				switch (Side)
				{
				case 0: L = FVector(Half, C, Z); Yaw = 180.0; break;
				case 1: L = FVector(-Half, C, Z); Yaw = 0.0; break;
				case 2: L = FVector(C, -Half, Z); Yaw = 90.0; break;
				default: L = FVector(C, Half, Z); Yaw = -90.0; break;
				}
				Bd.Put(Panel, L, FRotator(0.0, Yaw, 0.0), FVector(1.0, W / 100.0, H / PanelH), TEXT("Walls"), FString::Printf(TEXT("%s_%d_%02d"), *Name, R, I));
			}
		}
	}

	/** A plate cell [X0, X1] x [Y0, Y1] (top at Z, or its face down at Z when bDown). */
	static void Plate(FBuild& Bd, UStaticMesh* M, double X0, double X1, double Y0, double Y1, double Z, bool bDown, const FString& Folder, const FString& Label, double Thick = 1.0)
	{
		const FVector Scale((X1 - X0) / 200.0, (Y1 - Y0) / 100.0, Thick);
		const FVector Centre(0.5 * (X0 + X1), 0.5 * (Y0 + Y1), bDown ? Z + 10.0 * Thick : Z - 10.0 * Thick);
		Bd.Put(M, Centre, bDown ? FRotator(0.0, 0.0, 180.0) : FRotator::ZeroRotator, Scale, Folder, Label);
	}

	/** Cells of a slab round a round hole: whole cells where they are clear of it, split down to 25 cm cells at its edge. */
	static void HoledSlab(FBuild& Bd, UStaticMesh* M, double X0, double X1, double Y0, double Y1, double Z, bool bDown, double Hole, double Cover, const FString& Folder, int32& Index)
	{
		double DMin2 = 0.0;
		const double NX = FMath::Clamp(0.0, X0, X1);
		const double NY = FMath::Clamp(0.0, Y0, Y1);
		DMin2 = NX * NX + NY * NY;
		const double FX = FMath::Max(FMath::Abs(X0), FMath::Abs(X1));
		const double FY = FMath::Max(FMath::Abs(Y0), FMath::Abs(Y1));
		const double DMax = FMath::Sqrt(FX * FX + FY * FY);
		const double DMin = FMath::Sqrt(DMin2);
		if (DMax <= Cover)
		{
			return;       // all of it inside what the hole's own pieces cover
		}
		if (DMin >= Hole)
		{
			Plate(Bd, M, X0, X1, Y0, Y1, Z, bDown, Folder, FString::Printf(TEXT("%s_%03d"), bDown ? TEXT("Ceiling") : TEXT("Floor"), Index++));
			return;
		}
		if (X1 - X0 <= 25.5)
		{
			return;       // its uncovered part lies within 28 cm of the hole: under the parapet (floor), in the shaft's slab's shadow (ceiling)
		}
		const double MX = 0.5 * (X0 + X1);
		const double MY = 0.5 * (Y0 + Y1);
		HoledSlab(Bd, M, X0, MX, Y0, MY, Z, bDown, Hole, Cover, Folder, Index);
		HoledSlab(Bd, M, MX, X1, Y0, MY, Z, bDown, Hole, Cover, Folder, Index);
		HoledSlab(Bd, M, X0, MX, MY, Y1, Z, bDown, Hole, Cover, Folder, Index);
		HoledSlab(Bd, M, MX, X1, MY, Y1, Z, bDown, Hole, Cover, Folder, Index);
	}

	/** A ring of panels facing the axis: the well (below the floor) and the shaft (above the ceiling). */
	static void Ring(FBuild& Bd, UStaticMesh* Panel, double Z0, int32 RowCount, const FString& Name)
	{
		const double Step = 2.0 * PI / WellSides;
		const double Width = 2.0 * WellApothem * FMath::Tan(Step * 0.5);
		for (int32 R = 0; R < RowCount; ++R)
		{
			for (int32 K = 0; K < WellSides; ++K)
			{
				const double A = Step * (K + 0.5);
				const FVector L(WellApothem * FMath::Cos(A), WellApothem * FMath::Sin(A), Z0 + R * PanelH);
				Bd.Put(Panel, L, FRotator(0.0, FMath::RadiansToDegrees(A) + 180.0, 0.0), FVector(1.0, Width / 100.0, 1.0), TEXT("Well"),
					FString::Printf(TEXT("%s_%d_%02d"), *Name, R, K));
			}
		}
	}

	/** A sloped deck of floor plates, 1 in 4, along X (bAlongY false) or Y, its top from (S0, Z0) to (S1, Z1). */
	static void Flight(FBuild& Bd, UStaticMesh* M, bool bAlongY, double S0, double S1, double Z0, double Z1, double C0, double C1, const FString& Label)
	{
		const int32 N = 4;
		const double Run = (S1 - S0) / N;
		const double Rise = (Z1 - Z0) / N;
		const double Len = FMath::Sqrt(Run * Run + Rise * Rise);
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
		for (int32 I = 0; I < N; ++I)
		{
			const double S = S0 + Run * (I + 0.5);
			const double Z = Z0 + Rise * (I + 0.5);
			const FRotator Rot(Pitch, bAlongY ? 90.0 : 0.0, 0.0);
			const FVector Up = Rot.RotateVector(FVector::UpVector);
			const FVector Top = bAlongY ? FVector(0.5 * (C0 + C1), S, Z) : FVector(S, 0.5 * (C0 + C1), Z);
			Bd.Put(M, Top - Up * DeckDepth, Rot, FVector(Len / 200.0, (C1 - C0) / 100.0, DeckDepth / 10.0), TEXT("Walkway"), FString::Printf(TEXT("%s_%d"), *Label, I));
		}
	}

	/** The wall pieces of a wall with a door frame in its middle (the frame centred on X = 0), between X -Extent and +Extent. */
	static void FramedWall(FBuild& Bd, UStaticMesh* Panel, double Extent, double WallY, double Yaw, double Z, const FString& Name)
	{
		const double Side = Extent - FrameHalf - FrameGap;
		for (const double Sign : { -1.0, 1.0 })
		{
			const double C = Sign * (FrameHalf + FrameGap + 0.5 * Side);
			Bd.Put(Panel, FVector(C, WallY, Z), FRotator(0.0, Yaw, 0.0), FVector(1.0, Side / 100.0, 1.0), TEXT("Lock"), FString::Printf(TEXT("%s_Side%s"), *Name, Sign < 0 ? TEXT("L") : TEXT("R")));
		}
		const double TopZ = Z + FrameHeight + FrameGap;
		Bd.Put(Panel, FVector(0.0, WallY, TopZ), FRotator(0.0, Yaw, 0.0), FVector(1.0, 2.0 * FrameHalf / 100.0, (Z + PanelH - TopZ) / PanelH), TEXT("Lock"), Name + TEXT("_Transom"));
	}

	static void Build(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = InWorld;
		if (!World || World->WorldType != EWorldType::Editor)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::Editor)
				{
					World = Ctx.World();
				}
			}
		}
		if (!World || World->WorldType != EWorldType::Editor || !World->GetOutermost()->GetName().EndsWith(TEXT("Epilogue_Chamber")))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Chamber.Build: open Epilogue_Chamber in the editor (no play session) first."));
			return;
		}
		FBuild Bd;
		Bd.World = World;

		// --- The old room's pieces go; the game's actors stay ---
		int32 Removed = 0;
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (Cast<AStaticMeshActor>(*It) || Cast<ALight>(*It) || (It->Tags.Contains(TEXT("Chamber")) && !Cast<AEpilogueChamber>(*It) && !Cast<AChoiceTerminal>(*It)
				&& !Cast<ADoorDevice>(*It) && !Cast<APrologueMonitorRoom>(*It) && !Cast<ALightGroupDevice>(*It) && !Cast<APlayerStart>(*It) && !Cast<AChamberBattery>(*It) && !Cast<APrologueSlidingPanel>(*It)))
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* A : Doomed)
		{
			Removed += World->DestroyActor(A) ? 1 : 0;
		}

		UStaticMesh* WallPanel = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitWallPanel"));
		UStaticMesh* FloorPlate = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitFloorPlate"));
		UStaticMesh* Post = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitCornerPost"));
		UStaticMesh* Beam = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitCeilingBeam"));
		UStaticMesh* DoorFrame = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitDoorFrame"));
		UStaticMesh* Railing = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitRailing"));
		UStaticMesh* RailingRamp = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitRailingRamp"));
		UStaticMesh* Cover = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitTrenchCover"));
		UStaticMesh* Bollard = Bd.Mesh(TEXT("Kit"), TEXT("SM_KitBollard"));

		// --- The hall: walls, floor, ceiling ---
		for (int32 Side = 0; Side < 3; ++Side)
		{
			WallRun(Bd, WallPanel, Side, -Half, Half, 0.0, High, Side == 0 ? TEXT("WallRight") : (Side == 1 ? TEXT("WallLeft") : TEXT("WallFar")));
		}
		// The entry wall: its third row holds the lock's opening (the frame centred on X = 0 at the gallery's height).
		WallRun(Bd, WallPanel, 3, -Half, Half, 0.0, GalleryZ, TEXT("WallEntry"));
		WallRun(Bd, WallPanel, 3, -Half, Half, GalleryZ + PanelH, High, TEXT("WallEntryUp"));
		WallRun(Bd, WallPanel, 3, -Half, -FrameHalf - FrameGap - 73.0, GalleryZ, GalleryZ + PanelH, TEXT("WallEntryL"));
		WallRun(Bd, WallPanel, 3, FrameHalf + FrameGap + 73.0, Half, GalleryZ, GalleryZ + PanelH, TEXT("WallEntryR"));
		for (const double Sign : { -1.0, 1.0 })
		{
			const double C = Sign * (FrameHalf + FrameGap + 36.5);
			Bd.Put(WallPanel, FVector(C, Half, GalleryZ), FRotator(0.0, -90.0, 0.0), FVector(1.0, 0.73, 1.0), TEXT("Walls"), FString::Printf(TEXT("WallEntry_Jamb%s"), Sign < 0 ? TEXT("L") : TEXT("R")));
		}
		Bd.Put(WallPanel, FVector(0.0, Half, GalleryZ + FrameHeight + FrameGap), FRotator(0.0, -90.0, 0.0), FVector(1.0, 2.5, (PanelH - FrameHeight - FrameGap) / PanelH), TEXT("Walls"), TEXT("WallEntry_Transom"));
		int32 FloorIndex = 0;
		int32 CeilingIndex = 0;
		for (int32 IX = 0; IX < 10; ++IX)
		{
			for (int32 IY = 0; IY < 20; ++IY)
			{
				const double X0 = -Half + IX * 400.0;
				const double Y0 = -Half + IY * 200.0;
				HoledSlab(Bd, FloorPlate, X0, X0 + 400.0, Y0, Y0 + 200.0, 0.0, false, WellApothem, ParapetOuter, TEXT("Floor"), FloorIndex);
				HoledSlab(Bd, FloorPlate, X0, X0 + 400.0, Y0, Y0 + 200.0, High, true, WellApothem, WellApothem + 10.0, TEXT("Ceiling"), CeilingIndex);
			}
		}

		// --- The well and the shaft, closed at their ends, unlit ---
		Ring(Bd, WallPanel, -WellDepth, 4, TEXT("Well"));
		Ring(Bd, WallPanel, High, 2, TEXT("Shaft"));
		for (int32 IX = 0; IX < 2; ++IX)
		{
			for (int32 IY = 0; IY < 4; ++IY)
			{
				Plate(Bd, FloorPlate, -400.0 + 400.0 * IX, 400.0 * IX, -400.0 + 200.0 * IY, -200.0 + 200.0 * IY, -WellDepth, false, TEXT("Well"), FString::Printf(TEXT("WellBottom_%d%d"), IX, IY));
				Plate(Bd, FloorPlate, -400.0 + 400.0 * IX, 400.0 * IX, -400.0 + 200.0 * IY, -200.0 + 200.0 * IY, High + ShaftHeight, true, TEXT("Well"), FString::Printf(TEXT("ShaftTop_%d%d"), IX, IY));
			}
		}

		// --- The lock: 4 x 4 m and one panel high, behind the entry wall, its floor at the gallery's ---
		Plate(Bd, FloorPlate, -LockHalf, LockHalf, Half - RibTip, 2235.0, GalleryZ, false, TEXT("Lock"), TEXT("LockFloor_0"));
		Plate(Bd, FloorPlate, -LockHalf, LockHalf, 2235.0, 2480.0, GalleryZ, false, TEXT("Lock"), TEXT("LockFloor_1"));
		Plate(Bd, FloorPlate, -LockHalf, LockHalf, LockFront, 0.5 * (LockFront + LockBack), GalleryZ + PanelH, true, TEXT("Lock"), TEXT("LockCeiling_0"));
		Plate(Bd, FloorPlate, -LockHalf, LockHalf, 0.5 * (LockFront + LockBack), LockBack, GalleryZ + PanelH, true, TEXT("Lock"), TEXT("LockCeiling_1"));
		for (const double Sign : { -1.0, 1.0 })
		{
			for (int32 I = 0; I < 2; ++I)
			{
				Bd.Put(WallPanel, FVector(Sign * LockHalf, LockFront + 100.0 + 200.0 * I, GalleryZ), FRotator(0.0, Sign < 0 ? 0.0 : 180.0, 0.0), FVector(1.0, 2.0, 1.0), TEXT("Lock"),
					FString::Printf(TEXT("LockWall%s_%d"), Sign < 0 ? TEXT("L") : TEXT("R"), I));
			}
		}
		FramedWall(Bd, WallPanel, LockHalf, LockFront, 90.0, GalleryZ, TEXT("LockFront"));
		FramedWall(Bd, WallPanel, LockHalf, LockBack, -90.0, GalleryZ, TEXT("LockBack"));
		Bd.Put(DoorFrame, FVector(0.0, Half + 15.0, GalleryZ), FRotator(0.0, 90.0, 0.0), FVector(1.12, 1.0, 1.0), TEXT("Lock"), TEXT("LockFrame_Hall"));
		Bd.Put(DoorFrame, FVector(0.0, LockBack + 5.0, GalleryZ), FRotator(0.0, 90.0, 0.0), FVector(1.12, 1.0, 1.0), TEXT("Lock"), TEXT("LockFrame_Door"));

		// --- The gallery and the walkway: decks of floor plates on posts, railings on every free edge ---
		const double DeckWall = Half - RibTip;
		for (int32 IX = 0; IX < 2; ++IX)
		{
			for (int32 IY = 0; IY < 2; ++IY)
			{
				const double Y0 = 1600.0 + IY * 0.5 * (DeckWall - 1600.0);
				Plate(Bd, FloorPlate, -300.0 + 300.0 * IX, 300.0 * IX, Y0, Y0 + 0.5 * (DeckWall - 1600.0), GalleryZ, false, TEXT("Walkway"), FString::Printf(TEXT("Gallery_%d%d"), IX, IY), DeckDepth / 10.0);
			}
		}
		Plate(Bd, FloorPlate, -DeckWall, -1700.0, 1700.0, DeckWall, 350.0, false, TEXT("Walkway"), TEXT("Landing"), DeckDepth / 10.0);
		Flight(Bd, FloorPlate, false, -1700.0, -300.0, 350.0, GalleryZ, 1700.0, DeckWall, TEXT("Flight1"));
		Flight(Bd, FloorPlate, true, 300.0, 1700.0, 0.0, 350.0, -DeckWall, -1700.0, TEXT("Flight2"));
		const FVector Posts[] = { { -285.0, 1615.0, GalleryZ }, { 115.0, 1615.0, GalleryZ }, { 285.0, 1615.0, GalleryZ }, { 285.0, 1860.0, GalleryZ }, { -1715.0, 1715.0, 350.0 },
			{ -1300.0, 1715.0, 450.0 }, { -900.0, 1715.0, 550.0 }, { -500.0, 1715.0, 650.0 }, { -1715.0, 1300.0, 250.0 }, { -1715.0, 900.0, 150.0 } };
		int32 PostIndex = 0;
		for (const FVector& P : Posts)
		{
			// Every 4 m under the decks, from the floor to the deck's underside (the deck's own slope taken at the post).
			Bd.Put(Post, FVector(P.X, P.Y, 0.0), FRotator::ZeroRotator, FVector(1.5, 1.5, (P.Z - DeckDepth - 2.0) / 100.0), TEXT("Walkway"), FString::Printf(TEXT("Post_%02d"), PostIndex++));
		}
		const double R = 3.5;     // a railing's half thickness: its outer face on the deck's edge
		Bd.Put(Railing, FVector(-300.0, 1600.0 + R, GalleryZ), FRotator::ZeroRotator, FVector(1.0, 1.0, 1.0), TEXT("Railings"), TEXT("Railing_GalleryFront_0"));
		Bd.Put(Railing, FVector(100.0, 1600.0 + R, GalleryZ), FRotator::ZeroRotator, FVector(0.5, 1.0, 1.0), TEXT("Railings"), TEXT("Railing_GalleryFront_1"));
		Bd.Put(Railing, FVector(300.0 - R, 1600.0, GalleryZ), FRotator(0.0, 90.0, 0.0), FVector((DeckWall - 1600.0) / 400.0, 1.0, 1.0), TEXT("Railings"), TEXT("Railing_GalleryRight"));
		Bd.Put(Railing, FVector(-300.0 + R, 1600.0, GalleryZ), FRotator(0.0, 90.0, 0.0), FVector(0.25, 1.0, 1.0), TEXT("Railings"), TEXT("Railing_GalleryLeft"));
		for (int32 I = 0; I < 4; ++I)
		{
			// The flights' 14 m runs are 3.5 railings: four of them at 0.875 in length and height (the slope kept, 96 cm tall).
			Bd.Put(RailingRamp, FVector(-1700.0 + 350.0 * I, 1700.0 + R, 350.0 + 87.5 * I), FRotator::ZeroRotator, FVector(0.875, 1.0, 0.875), TEXT("Railings"), FString::Printf(TEXT("Railing_Flight1_%d"), I));
			Bd.Put(RailingRamp, FVector(-1700.0 - R, 300.0 + 350.0 * I, 87.5 * I), FRotator(0.0, 90.0, 0.0), FVector(0.875, 1.0, 0.875), TEXT("Railings"), FString::Printf(TEXT("Railing_Flight2_%d"), I));
		}

		// --- The battery: 12 tiers A and B in turn from the well's bottom to the shaft's top, rings, spines, ducts ---
		UStaticMesh* TierA = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryTier"));
		UStaticMesh* TierAGlow = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryTierGlow"));
		UStaticMesh* TierB = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryTierB"));
		UStaticMesh* TierBGlow = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryTierBGlow"));
		UStaticMesh* RingMesh = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryRing"));
		UStaticMesh* Spine = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatterySpine"));
		UStaticMesh* SpineGlow = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatterySpineGlow"));
		UStaticMesh* Duct = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryDuct"));
		UStaticMesh* Socket = Bd.Mesh(TEXT("Prologue"), TEXT("SM_BatteryDuctSocket"));
		for (int32 K = 0; K < 12; ++K)
		{
			const double Z = -WellDepth + 350.0 * K;
			const bool bA = K % 2 == 0;
			const FRotator Rot(0.0, bA ? 0.0 : -15.0, 0.0);
			Bd.Put(bA ? TierA : TierB, FVector(0.0, 0.0, Z), Rot, FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("Tier%s_%02d"), bA ? TEXT("A") : TEXT("B"), K));
			Bd.Put(bA ? TierAGlow : TierBGlow, FVector(0.0, 0.0, Z), Rot, FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("Tier%sGlow_%02d"), bA ? TEXT("A") : TEXT("B"), K));
			if (K > 0)
			{
				Bd.Put(RingMesh, FVector(0.0, 0.0, Z - 25.0), FRotator(0.0, -11.0 * K, 0.0), FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("Ring_%02d"), K), true);
			}
			for (const double D : { 45.0, 135.0, 225.0, 315.0 })
			{
				Bd.Put(Spine, FVector(0.0, 0.0, Z), FRotator(0.0, -D, 0.0), FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("Spine_%03d_%02d"), static_cast<int32>(D), K));
				Bd.Put(SpineGlow, FVector(0.0, 0.0, Z), FRotator(0.0, -D, 0.0), FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("SpineGlow_%03d_%02d"), static_cast<int32>(D), K));
			}
		}
		const double SocketTierZ = -WellDepth + 350.0 * 8;
		Bd.Put(Socket, FVector(0.0, 0.0, SocketTierZ), FRotator::ZeroRotator, FVector::OneVector, TEXT("Battery"), TEXT("DuctSocket_E"));
		Bd.Put(Socket, FVector(0.0, 0.0, SocketTierZ), FRotator(0.0, 180.0, 0.0), FVector::OneVector, TEXT("Battery"), TEXT("DuctSocket_W"));
		for (int32 K = 0; K < 5; ++K)
		{
			for (const bool bEast : { true, false })
			{
				const double S = bEast ? 400.0 * K : -2000.0 + 400.0 * K;
				const double A = S / DuctRingRadius;
				const double X = (bEast ? SocketX : -SocketX) + DuctRingRadius * FMath::Sin(A);
				const double Yb = DuctRingRadius * (1.0 - FMath::Cos(A));
				Bd.Put(Duct, FVector(X, -Yb, DuctZ), FRotator(0.0, YawB(A), 0.0), FVector::OneVector, TEXT("Battery"), FString::Printf(TEXT("Duct%s_%d"), bEast ? TEXT("E") : TEXT("W"), K));
			}
		}

		// --- At the well: the parapet, the portal and the terminal on its platform ---
		UStaticMesh* Parapet = Bd.Mesh(TEXT("Prologue"), TEXT("SM_ContainmentParapet"));
		for (int32 K = 0; K < 8; ++K)
		{
			Bd.Put(Parapet, FVector::ZeroVector, FRotator(0.0, -45.0 * K, 0.0), FVector::OneVector, TEXT("Floor"), FString::Printf(TEXT("Parapet_%d"), K));
		}
		const FVector PortalAt = B2U(0.0, -(4.22 + 1.0), 0.0);
		Bd.Put(Bd.Mesh(TEXT("Prologue"), TEXT("SM_TerminalPortal")), PortalAt, FRotator(0.0, 90.0, 0.0), FVector::OneVector, TEXT("Floor"), TEXT("TerminalPortal"));

		// --- On the floor: trench covers flush in it, eight bollards ---
		struct FSpoke { const TCHAR* Name; double Deg; double Len; };
		const FSpoke Spokes[] = {
			{ TEXT("BankS"), -14.0, 19.0 / FMath::Cos(FMath::DegreesToRadians(14.0)) }, { TEXT("BankN"), 14.0, 19.0 / FMath::Cos(FMath::DegreesToRadians(14.0)) },
			{ TEXT("TankE"), FMath::RadiansToDegrees(FMath::Atan2(17.7, 12.5)), FMath::Sqrt(12.5 * 12.5 + 17.7 * 17.7) - 1.6 },
			{ TEXT("TankW"), FMath::RadiansToDegrees(FMath::Atan2(17.7, -12.5)), FMath::Sqrt(12.5 * 12.5 + 17.7 * 17.7) - 1.6 },
			{ TEXT("FarWall"), 93.0, 19.95 / FMath::Sin(FMath::DegreesToRadians(93.0)) }, { TEXT("Racks"), 160.0, 19.0 / FMath::Cos(FMath::DegreesToRadians(20.0)) },
			{ TEXT("SwitchgearW"), 230.0, 19.0 / FMath::Sin(FMath::DegreesToRadians(50.0)) }, { TEXT("SwitchgearE"), 306.0, 19.0 / FMath::Sin(FMath::DegreesToRadians(54.0)) },
		};
		for (const FSpoke& Sp : Spokes)
		{
			const double A = FMath::DegreesToRadians(Sp.Deg);
			for (const bool bIn : { true, false })
			{
				double Rr = bIn ? 4.35 : 9.45;
				const double End = bIn ? 8.35 : Sp.Len;
				int32 K = 0;
				while (End - Rr > 0.3)
				{
					const double L = FMath::Min(4.0, End - Rr);
					Bd.Put(Cover, B2U(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.0), FRotator(0.0, YawB(A), 0.0), FVector(L / 4.0, 1.0, 1.0), TEXT("FloorKit"),
						FString::Printf(TEXT("Cover_%s_%s_%02d"), Sp.Name, bIn ? TEXT("In") : TEXT("Out"), K++));
					Rr += L;
				}
			}
		}
		const int32 RingN = 14;
		const double RV = 2.0 / FMath::Sin(PI / RingN);
		const double Mitre = 0.3 * FMath::Tan(PI / RingN);
		for (int32 K = 0; K < RingN; ++K)
		{
			const double A0 = 2.0 * PI * K / RingN;
			const double A1 = 2.0 * PI * (K + 1) / RingN;
			const FVector2D P0(RV * FMath::Cos(A0), RV * FMath::Sin(A0));
			const FVector2D P1(RV * FMath::Cos(A1), RV * FMath::Sin(A1));
			const FVector2D D = (P1 - P0).GetSafeNormal();
			const FVector2D S = P0 + D * Mitre;
			Bd.Put(Cover, B2U(S.X, S.Y, 0.0), FRotator(0.0, YawB(FMath::Atan2(D.Y, D.X)), 0.0), FVector((4.0 - 2.0 * Mitre) / 4.0, 1.0, 1.0), TEXT("FloorKit"), FString::Printf(TEXT("Cover_Ring_%02d"), K));
		}
		for (int32 K = 0; K < 8; ++K)
		{
			// Every other one of the blockout's 16 (turned 20 degrees): 1.26 m and more clear of the way to the terminal.
			const double A = FMath::DegreesToRadians(20.0 + 45.0 * K);
			Bd.Put(Bollard, B2U(9.75 * FMath::Cos(A), 9.75 * FMath::Sin(A), 0.0), FRotator(0.0, YawB(A), 0.0), FVector::OneVector, TEXT("FloorKit"), FString::Printf(TEXT("Bollard_%d"), K));
		}

		// --- Along the walls ---
		UStaticMesh* Tank = Bd.Mesh(TEXT("Prologue"), TEXT("SM_CoolantTank"));
		for (const double X : { -16.5, -12.5, 12.5, 16.5 })
		{
			Bd.Put(Tank, B2U(X, 17.7, 0.0), FRotator::ZeroRotator, FVector::OneVector, TEXT("Walls_Far"), FString::Printf(TEXT("CoolantTank_%+.0f"), X));
		}
		UStaticMesh* Rack = Bd.Mesh(TEXT("Prologue"), TEXT("SM_ComputeRack"));
		for (int32 K = 0; K < 18; ++K)
		{
			Bd.Put(Rack, FVector(-Half + RibTip + 60.0, -(240.0 + 80.0 * K), 0.0), FRotator::ZeroRotator, FVector::OneVector, TEXT("Walls_Left"), FString::Printf(TEXT("Rack_%02d"), K));
		}
		UStaticMesh* Cabinet = Bd.Mesh(TEXT("Prologue"), TEXT("SM_SwitchgearCabinet"));
		for (int32 K = 0; K < 11; ++K)
		{
			Bd.Put(Cabinet, FVector(100.0 * (-18.0 + 2.0 * K), Half - RibTip - 50.0, 0.0), FRotator(0.0, -90.0, 0.0), FVector::OneVector, TEXT("Walls_Entry"), FString::Printf(TEXT("Cabinet_S%02d"), K));
		}
		for (int32 K = 0; K < 8; ++K)
		{
			Bd.Put(Cabinet, FVector(100.0 * (4.2 + 2.0 * K), Half - RibTip - 50.0, 0.0), FRotator(0.0, -90.0, 0.0), FVector::OneVector, TEXT("Walls_Entry"), FString::Printf(TEXT("Cabinet_SE%02d"), K));
		}
		for (int32 K = 0; K < 2; ++K)
		{
			Bd.Put(Cabinet, FVector(-Half + RibTip + 50.0, 1800.0 - 200.0 * K, 0.0), FRotator::ZeroRotator, FVector::OneVector, TEXT("Walls_Left"), FString::Printf(TEXT("Cabinet_W%02d"), K));
		}
		UStaticMesh* Bank = Bd.Mesh(TEXT("Prologue"), TEXT("SM_ControlBank"));
		for (int32 K = 0; K < 8; ++K)
		{
			Bd.Put(Bank, FVector(Half - RibTip - 50.0, -(K - 3.5) * 340.0, 0.0), FRotator(0.0, 180.0, 0.0), FVector::OneVector, TEXT("Monitoring"), FString::Printf(TEXT("ControlBank_%d"), K));
		}
		UStaticMesh* MonitorWall = Bd.Mesh(TEXT("Prologue"), TEXT("SM_MonitorWall"));
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			for (int32 K = -1; K <= 1; ++K)
			{
				// Home's monitor wall (three rows of eight) twice over the banks: its back on the panels' ribs.
				Bd.Put(MonitorWall, FVector(Half - RibTip - 40.0, 815.0 * K, 230.0 + 400.0 * Layer), FRotator(0.0, 180.0, 0.0), FVector::OneVector, TEXT("Monitoring"),
					FString::Printf(TEXT("MonitorWall_%d_%+d"), Layer, K));
			}
		}

		// --- Overhead: the service crane from the kit's ceiling beam ---
		for (const double Sign : { -1.0, 1.0 })
		{
			for (int32 K = 0; K < 4; ++K)
			{
				Bd.Put(Beam, FVector(Sign * 1955.0, -1500.0 + 1000.0 * K, 1910.0), FRotator::ZeroRotator, FVector(2.0, 1000.0 / 300.0, 2.0), TEXT("Crane"),
					FString::Printf(TEXT("CraneRail%s_%d"), Sign < 0 ? TEXT("L") : TEXT("R"), K));
			}
		}
		for (int32 K = 0; K < 4; ++K)
		{
			Bd.Put(Beam, FVector(-1466.25 + 977.5 * K, Half - 600.0, 1990.0), FRotator(0.0, 90.0, 0.0), FVector(2.5, 977.5 / 300.0, 2.5), TEXT("Crane"), FString::Printf(TEXT("CraneBridge_%d"), K));
		}

		// --- Lights: home's values (5000 lm, 22 m), at most 10, in the light group ---
		const FVector LightPlaces[] = { { 1100.0, 1100.0, 900.0 }, { -1100.0, 1100.0, 900.0 }, { 1100.0, -1100.0, 900.0 }, { -1100.0, -1100.0, 900.0 },
			{ 0.0, 1300.0, 1300.0 }, { 1600.0, 0.0, 800.0 }, { -1550.0, -900.0, 600.0 }, { 0.0, -1600.0, 900.0 }, { 0.0, 800.0, 1800.0 }, { 0.0, 2240.0, 980.0 } };
		TArray<ALight*> NewLights;
		int32 LightIndex = 0;
		for (const FVector& L : LightPlaces)
		{
			FActorSpawnParameters P;
			APointLight* Light = World->SpawnActor<APointLight>(L, FRotator::ZeroRotator, P);
			UPointLightComponent* C = Cast<UPointLightComponent>(Light->GetLightComponent());
			C->SetMobility(EComponentMobility::Movable);
			C->bUseInverseSquaredFalloff = true;
			C->SetIntensityUnits(ELightUnits::Lumens);
			C->SetIntensity(5000.f);
			C->SetAttenuationRadius(2200.f);
			C->SetLightFalloffExponent(8.f);
			C->SetCastShadows(false);
			Light->Tags.AddUnique(TEXT("Chamber"));
			Light->SetActorLabel(FString::Printf(TEXT("Light_%d"), LightIndex++));
			Light->SetFolderPath(TEXT("Chamber/Lights"));
			NewLights.Add(Light);
		}

		// --- The game's actors ---
		AEpilogueChamber* Chamber = nullptr;
		for (TActorIterator<AEpilogueChamber> It(World); It; ++It) { Chamber = *It; }
		AChoiceTerminal* Terminal = nullptr;
		for (TActorIterator<AChoiceTerminal> It(World); It; ++It) { Terminal = *It; }
		APrologueEntryDoor* Door = nullptr;
		for (TActorIterator<APrologueEntryDoor> It(World); It; ++It) { Door = *It; }
		APrologueMonitorRoom* Monitors = nullptr;
		for (TActorIterator<APrologueMonitorRoom> It(World); It; ++It) { Monitors = *It; }
		ALightGroupDevice* Group = nullptr;
		for (TActorIterator<ALightGroupDevice> It(World); It; ++It) { Group = *It; }
		APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Start = *It; }
		AChamberBattery* Battery = nullptr;
		for (TActorIterator<AChamberBattery> It(World); It; ++It) { Battery = *It; }
		APrologueSlidingPanel* Panel = nullptr;
		for (TActorIterator<APrologueSlidingPanel> It(World); It; ++It) { Panel = *It; }
		if (!Battery)
		{
			Battery = World->SpawnActor<AChamberBattery>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
		}
		Battery->SetActorLocation(FVector(0.0, 0.0, 150.0));
		Battery->Tags.AddUnique(TEXT("Chamber"));
		Battery->SetActorLabel(TEXT("Chamber_Battery"));
		Battery->SetFolderPath(TEXT("Chamber/Game"));
		if (!Panel)
		{
			Panel = World->SpawnActor<APrologueSlidingPanel>(FVector(0.0, Half + 10.0, GalleryZ), FRotator(0.0, 90.0, 0.0));
		}
		Panel->SetActorLocationAndRotation(FVector(0.0, Half + 10.0, GalleryZ), FRotator(0.0, 90.0, 0.0));
		// 3 cm taller than the frame's 3 m opening: raised, the panel's underside stands 3 cm above the lintel's (no face in one plane).
		Panel->OpeningSize = FVector(20.0, 200.0, 303.0);
		Panel->Slide = EProloguePanelSlide::Up;
		Panel->PanelOverlap = 0.f;
		Panel->PanelMesh = Bd.Mesh(TEXT("Prologue"), TEXT("SM_DoorPanel"));
		Panel->bBlockWhenOpen = false;
		Panel->Tags.AddUnique(TEXT("Chamber"));
		Panel->SetActorLabel(TEXT("Chamber_LockPanel"));
		Panel->SetFolderPath(TEXT("Chamber/Game"));
		Panel->RerunConstructionScripts();
		if (Door)
		{
			Door->SetActorLocationAndRotation(FVector(0.0, LockBack + 5.0, GalleryZ), FRotator(0.0, 90.0, 0.0));
			Door->Tags.AddUnique(TEXT("Chamber"));
			Door->SetActorLabel(TEXT("Chamber_LockDoor"));
			Door->SetFolderPath(TEXT("Chamber/Game"));
		}
		if (Terminal)
		{
			// On the portal's platform (its top 15 cm up), between the posts, its front toward the entry wall.
			Terminal->SetActorLocationAndRotation(PortalAt + FVector(0.0, -65.0, 15.0), FRotator(0.0, 90.0, 0.0));
			Terminal->Tags.AddUnique(TEXT("Chamber"));
			Terminal->SetFolderPath(TEXT("Chamber/Game"));
		}
		if (Start)
		{
			// Just inside the lock's panel, turned a little toward the hall's monitoring side: from the gallery, three steps on, everything is in view.
			Start->SetActorLocationAndRotation(FVector(0.0, 2080.0, GalleryZ + 100.0), FRotator(0.0, -80.0, 0.0));
			Start->Tags.AddUnique(TEXT("Chamber"));
			Start->SetFolderPath(TEXT("Chamber/Game"));
		}
		if (Monitors)
		{
			Monitors->bBatteryScreens = true;
			Monitors->Tags.AddUnique(TEXT("Chamber"));
			Monitors->SetActorLabel(TEXT("Chamber_Monitors"));
			Monitors->SetFolderPath(TEXT("Chamber/Game"));
		}
		if (Group)
		{
			Group->Lights.Reset();
			for (ALight* L : NewLights)
			{
				Group->Lights.Add(L);
			}
			Group->Tags.AddUnique(TEXT("Chamber"));
			Group->SetFolderPath(TEXT("Chamber/Game"));
		}
		if (Chamber)
		{
			Chamber->Terminal = Terminal;
			Chamber->EntryDoor = Door;
			Chamber->LockPanel = Panel;
			Chamber->Battery = Battery;
			Chamber->Tags.AddUnique(TEXT("Chamber"));
			Chamber->SetFolderPath(TEXT("Chamber/Game"));
		}
		World->GetCurrentLevel()->MarkPackageDirty();

		FString Meshes;
		Bd.ByMesh.KeySort(TLess<FString>());
		for (const TPair<FString, int32>& P : Bd.ByMesh)
		{
			Meshes += FString::Printf(TEXT("%s%s %d"), Meshes.IsEmpty() ? TEXT("") : TEXT(", "), *P.Key, P.Value);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Chamber.Build: removed %d actors of the old room; placed %d pieces (%s), %d lights; chamber %s, terminal %s, door %s, panel %s, battery %s, monitors %s, light group %s, start %s. Save the map."),
			Removed, Bd.Count, *Meshes, NewLights.Num(), *GetNameSafe(Chamber), *GetNameSafe(Terminal), *GetNameSafe(Door), *GetNameSafe(Panel), *GetNameSafe(Battery), *GetNameSafe(Monitors),
			*GetNameSafe(Group), *GetNameSafe(Start));
	}

	static FAutoConsoleCommandWithWorldAndArgs GBuildCmd(
		TEXT("tls.Chamber.Build"),
		TEXT("tls.Chamber.Build: (editor, Epilogue_Chamber open, no play session) removes the old room's pieces and lays the battery's access hall from the blockout's plan: placed actors tagged Chamber, the game's actors moved. Save the map after."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Build));
}

#endif
