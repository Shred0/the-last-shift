// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Area/AreaTypes.h"
#include "Engine/DeveloperSettings.h"
#include "Narrative/NarrativeRows.h"
#include "TextChannel/TextDegradation.h"
#include "TheLastShiftSettings.generated.h"

class UDataTable;
class UWorld;

/** Per-act size of a full time jump (a contact with response Jump or RewindAndJump). A graze is GrazeJumpFraction of it. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FActTuning
{
	GENERATED_BODY()

	/** Home time added by a full jump (600 s = ten minutes; 31 557 600 s = one year of 365.25 days). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Act", meta = (ClampMin = "0.0", ClampMax = "1000000000000.0", Units = "s"))
	double JumpHomeSeconds = 0.0;

	/** Home battery fraction lost by a full jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Act", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double JumpBatteryDrop = 0.0;
};

/**
 * Every colour a scan can show, in one place. A scan's own points, the scan-ready cue, residues, the entity and the
 * interactables (terminals, doors) read their colour from here when they draw; nothing copies it. The values live in
 * Config/DefaultGame.ini; the C++ defaults are only a neutral fallback for a missing ini entry.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FScanColors
{
	GENERATED_BODY()

	/** Captured points of a scan (the lidar's own colour). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Scan = FLinearColor::White;

	/** The scan-ready ring once a scan can be requested. Never the scan colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor ReadyCue = FLinearColor::White;

	/** Residue points and the residue's collect ring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Residue = FLinearColor::White;

	/** Every point of the entity (figure and trail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Entity = FLinearColor::White;

	/** Surfaces of what the player can use (terminals, closed doors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Interactable = FLinearColor::White;
};

/**
 * How scan points behind geometry are shown, in one place. The scan director pushes these values to the lidar; nothing
 * copies them. The values live in Config/DefaultGame.ini; the C++ defaults are only a fallback for a missing ini entry
 * (points behind geometry hidden).
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FScanOcclusion
{
	GENERATED_BODY()

	/** Points behind geometry are drawn at this factor of their own intensity; 0 hides them, 1 shows them like the points in front (as if the geometry were not there). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedIntensity = 0.f;

	/** Link time: after each scan starts, every alive point is at full intensity for this long, hidden or not. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Occlusion", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float LinkSeconds = 1.5f;

	/** Occlusion fade time: after the link time, hidden points go down to OccludedIntensity over this long. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Occlusion", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float FadeSeconds = 0.75f;
};

/**
 * Neighbour lines between scan points, in one place (Brief 13). The scan director pushes these values to the lidar; nothing
 * copies them. Points show through geometry; lines show only on surfaces in the line of sight, and they are what tells a
 * wall from what is behind it. The lines grow over ScanOcclusion.LinkSeconds. The values live in Config/DefaultGame.ini;
 * the C++ defaults are only a fallback for a missing ini entry (lines off).
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FScanLinks
{
	GENERATED_BODY()

	/** Each point is linked to at most this many of its nearest points of the same scan and channel; 0 = no lines. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0", ClampMax = "8"))
	int32 Neighbours = 0;

	/** Longest link between two points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "1.0", ClampMax = "1000.0", Units = "cm"))
	float MaxDistanceCm = 50.f;

	/** Width of a line on screen, in pixels, at any depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float PixelWidth = 1.5f;

	/** Brightness of a line as a factor of its points'. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Intensity = 0.5f;

	/** Largest angle between the surface normals of two linked points (no link across a corner or a depth step); 180 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float MaxNormalAngle = 180.f;

	/** Most links one scan keeps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0", ClampMax = "262144"))
	int32 MaxLinksPerScan = 65536;

	/** Lift of a line's ends off their surface, as a fraction of a capture texel's width at the point's depth from the scan; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LiftTexels = 0.f;

	/** Most a line's end is lifted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Links", meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "cm"))
	float LiftMaxCm = 30.f;
};

/**
 * Every colour of the suit's text, in one place: the home AI's messages, the twin AI's messages, the translation of the
 * logs at a terminal, and the voices of the full-screen text scenes (prologue, endings). Barcode bars take the colour of
 * their voice. None of them is a scan colour. The values live in Config/DefaultGame.ini; the C++ defaults are only a
 * fallback for a missing ini entry.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FSuitTextColors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor HomeAI = FLinearColor(0.93f, 0.82f, 0.55f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor TwinAI = FLinearColor(1.f, 0.6f, 0.12f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor LogTranslation = FLinearColor(0.85f, 0.88f, 0.8f, 0.95f);

	/** The engineer's reply in an ending (a text scene), on SceneBackground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor Engineer = FLinearColor(0.92f, 0.92f, 0.9f, 1.f);

	/** The voices of the simulated world in a text scene: dark text on a white background. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor ParadiseText = FLinearColor(0.12f, 0.12f, 0.12f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor ParadiseBackground = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Background of the home AI's and the engineer's lines in a text scene. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Colors")
	FLinearColor SceneBackground = FLinearColor(0.f, 0.f, 0.f, 1.f);
};

/**
 * The degradation of the home AI's text by the battery, in one place: the battery thresholds of the five levels and the
 * typing speeds. The text channel, the travel screen's line and the text scenes read them from here; nothing copies them.
 * The values live in Config/DefaultGame.ini.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FTextDegradationTuning
{
	GENERATED_BODY()

	/** Below this battery fraction a message loses its articles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoArticlesBelow = 0.70f;

	/** Below this battery fraction a message is lowercase and abbreviated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AbbreviatedBelow = 0.40f;

	/** Below this battery fraction only fragments of a message are left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScatteredBelow = 0.10f;

	/** Below this battery fraction a message is discarded (Silent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SilentBelow = 0.03f;

	/** Typing speed of the home AI's messages at a full battery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float CharsPerSecondFull = 40.f;

	/** Typing speed at an empty battery; in between it follows the battery fraction linearly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Degradation", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float CharsPerSecondEmpty = 8.f;
};

/** Where a log terminal shows its entries. */
UENUM(BlueprintType)
enum class ELogDisplay : uint8
{
	/** On the terminal's surface in the world. */
	Surface,
	/** In a panel of the suit on screen, while the player is at the terminal. */
	Panel
};

/** A real step of building an area behind the travel screen, in the order they happen. */
UENUM(BlueprintType)
enum class EAreaLoadStep : uint8
{
	/** The route and everything the player interacts with are planned and placed (during the map load). */
	RoutePlanned,
	/** The PCG graphs have built the geometry and it matches the plan. */
	GeometryBuilt,
	/** The navmesh is built; validation and the player's release follow in the same frame. */
	NavigationReady
};

/**
 * Where the travel screen's progress bar stands after each step of an area build (0..1). The map's arrival is StdCore's
 * own step (TransitionMapLoadedProgress in [/Script/StdCore.StdProjectSettings]); the bar is full when the area releases
 * the player. Weights follow the measured time of each step; they move nothing on their own.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FTravelProgressFractions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoutePlanned = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GeometryBuilt = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NavigationReady = 0.9f;

	float GetFraction(EAreaLoadStep Step) const
	{
		switch (Step)
		{
		case EAreaLoadStep::RoutePlanned: return RoutePlanned;
		case EAreaLoadStep::GeometryBuilt: return GeometryBuilt;
		case EAreaLoadStep::NavigationReady: return NavigationReady;
		}
		return 0.f;
	}
};

/**
 * The twin structure's hologram in the prologue's monitor room: a still cloud of points on an octahedron, in the scan
 * colour. The values live in Config/DefaultGame.ini.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FHologramTuning
{
	GENERATED_BODY()

	/** Distance between two opposite vertices of the octahedron. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hologram", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float Size = 400.f;

	/** Points of the cloud, on the octahedron's edges and faces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hologram", meta = (ClampMin = "50", ClampMax = "10000"))
	int32 Points = 1500;

	/** Side of each point (a small unlit cube). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hologram", meta = (ClampMin = "0.5", ClampMax = "20.0", Units = "cm"))
	float PointSize = 3.f;

	/** Of the points, the share laid along the twelve edges (the rest fill the eight faces). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hologram", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeShare = 0.35f;
};

/**
 * Project settings of the game (Project Settings > Game > The Last Shift), stored as text in Config/DefaultGame.ini.
 * Sole owner of the run's map order, of every area's recipe and seed, of the per-act jump tuning, of the scan colours, of
 * the suit's text colours and log and twin AI typing, of the home AI's text degradation (thresholds and typing speeds),
 * of the text scenes' typing and ending condition, and of which narrative tables the game reads: the area generator, the run subsystem, the home state subsystem and the home AI
 * read them from here and copy nothing.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "The Last Shift"))
class THELASTSHIFT_API UTheLastShiftSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UTheLastShiftSettings* Get() { return GetDefault<UTheLastShiftSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The four area maps, in the order they are travelled. A map's act is its position in this list (1-4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run")
	TArray<TSoftObjectPtr<UWorld>> AreaMaps;

	/** One recipe per act, same order as AreaMaps. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run")
	TArray<FAreaRecipe> AreaRecipes;

	/** One seed per act, same order as AreaMaps. Used in the game only if tls.AreaValidate passes for it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0", ClampMax = "999999"))
	TArray<int32> AreaSeeds;

	/** Recipe of any area map outside AreaMaps (AreaTest). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test")
	FAreaRecipe TestRecipe;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test", meta = (ClampMin = "0", ClampMax = "999999"))
	int32 TestSeed = 1;

	/** One entry per act, same order as AreaMaps: what a full time jump costs in that act. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Home")
	TArray<FActTuning> ActTuning;

	/** Jump tuning of any map outside AreaMaps (TestLidar, AreaTest). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test")
	FActTuning TestActTuning;

	/** A graze (a voluntary Resume with the figure within the entity's GrazeRadius) jumps by this fraction of the act's values. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Home", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GrazeJumpFraction = 0.25f;

	/** Every colour a scan can show: the one owner of those colours (see FScanColors). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability")
	FScanColors ScanColors;

	/**
	 * Custom depth stencil value of the meshes shown as interactables (terminals, closed doors) and the lidar colour
	 * channel the scan director gives ScanColors.Interactable. The one owner of that number.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability", meta = (ClampMin = "1", ClampMax = "4"))
	int32 InteractableScanChannel = 1;

	/** Scan points behind geometry: occluded intensity, link time, fade time. The one owner of those values. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability")
	FScanOcclusion ScanOcclusion;

	/** Neighbour lines between scan points: neighbours, distance, width, intensity, angle limit, cap. The one owner of those values. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability")
	FScanLinks ScanLinks;

	/** The travel screen stays black at least this long, even when the next area is ready sooner. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	float TravelHoldSeconds = 4.f;

	/** The travel screen's progress bar after each step of an area build (reported by the run subsystem). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Travel")
	FTravelProgressFractions TravelProgress;

	/**
	 * The home AI's messages (FAIMessageRow), imported from Narrative/DT_AIMessages.csv. Also the travel screen's label:
	 * the AreaEntry row of the destination act (none: no label).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> AIMessagesTable;

	/** The ending texts (FEndingRow: Inject, Open, Loss), imported from Narrative/DT_Endings.csv. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> EndingsTable;

	/** The other humanity's logs (FLogRow), imported from Narrative/DT_Logs.csv. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> LogsTable;

	/** The twin AI's messages (FTwinAIRow), imported from Narrative/DT_TwinAI.csv. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> TwinAITable;

	/** Where log terminals show their entries: on their surface, or in a suit panel on screen. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Twin Writing")
	ELogDisplay LogDisplay = ELogDisplay::Surface;

	/** Speed at which the suit types the translation of a log entry over its barcodes. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Twin Writing", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float LogCharsPerSecond = 40.f;

	/** Fixed typing speed of the twin AI's messages (never slowed by the home battery). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Twin Writing", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float TwinCharsPerSecond = 40.f;

	/** In a twin AI message the translation follows this many characters behind the barcodes, and catches up at the end. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Twin Writing", meta = (ClampMin = "0", ClampMax = "200"))
	int32 TranslationLagChars = 10;

	/** Every colour of the suit's text: the one owner of those colours (see FSuitTextColors). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Twin Writing")
	FSuitTextColors TextColors;

	/** The battery thresholds of the five levels of the home AI's text and its typing speeds: their one owner. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Text Channel")
	FTextDegradationTuning TextDegradation;

	/** The lines of the full-screen text scenes (FSceneRow: the prologue, the home AI's last message), imported from Narrative/DT_Scenes.csv. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> ScenesTable;

	/** Typing speed of every line of a text scene, whatever its voice. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scenes", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float SceneCharsPerSecond = 40.f;

	/**
	 * The ending "Open the structure" is offered only when the suit's archive holds at least this many parts of the twin
	 * AI. 0 = every part of the twin AI's table.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scenes", meta = (ClampMin = "0", ClampMax = "99"))
	int32 OpenEndingTwinParts = 0;

	// --- The prologue's maps ---

	/**
	 * On: "New game" goes from the text scene to PrologueHomeMap, then PrologueArrivalMap, then area 1. Off (or a map
	 * unset): from the text scene straight to area 1.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	bool bPrologueMaps = true;

	/**
	 * The home: print room, one corridor to the hangar, the monitor room with the hologram off the corridor's side, the
	 * hangar entered from its side, and the ship. Outside the run (act 0).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	TSoftObjectPtr<UWorld> PrologueHomeMap;

	/** The arrival hangar of the twin structure, its hatch and the door to area 1. Outside the run (act 0). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	TSoftObjectPtr<UWorld> PrologueArrivalMap;

	/** The home AI's lines in the prologue's maps and on the flight (FPrologueRow), imported from Narrative/DT_Prologue.csv. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> PrologueTable;

	/** Normal-time seconds the arrival hangar's hatch takes to close once the player has left the ship. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float HatchCloseSeconds = 4.f;

	/**
	 * Normal-time seconds between the home AI's Door lines in the arrival hangar (or, with none, its Hatch lines, or, with
	 * neither, the hatch closing) and the exit door opening. Stands still while Stopped and behind the pause menu.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	float ExitDoorDelaySeconds = 2.f;

	/** Normal-time seconds the home hangar's entry door takes to close behind the player once wholly inside the hangar. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float HomeDoorCloseSeconds = 2.f;

	/** Normal-time seconds the home hangar's hatch takes to open once the entry door is closed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float HomeHatchOpenSeconds = 4.f;

	/** Normal-time seconds the home ship's hatch takes to open once the hangar's hatch is open. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float ShipHatchOpenSeconds = 3.f;

	/** In PrologueHomeMap the flashlight works (true) or is off with its input ignored (false). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	bool bFlashlightAtHome = false;

	/** In PrologueArrivalMap the flashlight works as in the areas; this is whether it is on when the map starts. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	bool bFlashlightOnAtArrival = false;

	/** The twin structure's hologram in the monitor room. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Prologue")
	FHologramTuning Hologram;

	/** bPrologueMaps, with both prologue maps set. */
	bool ArePrologueMapsOn() const { return bPrologueMaps && !PrologueHomeMap.IsNull() && !PrologueArrivalMap.IsNull(); }

	/** 1 for the prologue's home map, 2 for its arrival map (PIE prefix ignored), 0 for any other map. */
	int32 GetPrologueStageOfMap(const FString& PackageName) const;

	/** HatchCloseSeconds, clamped. */
	float GetHatchCloseSeconds() const { return FMath::Clamp(HatchCloseSeconds, 0.5f, 30.f); }

	/** ExitDoorDelaySeconds, clamped. */
	float GetExitDoorDelaySeconds() const { return FMath::Clamp(ExitDoorDelaySeconds, 0.f, 30.f); }

	/** HomeDoorCloseSeconds, clamped. */
	float GetHomeDoorCloseSeconds() const { return FMath::Clamp(HomeDoorCloseSeconds, 0.5f, 30.f); }

	/** HomeHatchOpenSeconds, clamped. */
	float GetHomeHatchOpenSeconds() const { return FMath::Clamp(HomeHatchOpenSeconds, 0.5f, 30.f); }

	/** ShipHatchOpenSeconds, clamped. */
	float GetShipHatchOpenSeconds() const { return FMath::Clamp(ShipHatchOpenSeconds, 0.5f, 30.f); }

	/**
	 * The flashlight of a map, by its prologue stage (GetPrologueStageOfMap): returns whether it works there, and sets
	 * bInOutOnAtStart to whether it starts on. The prologue's maps decide both here (bFlashlightAtHome,
	 * bFlashlightOnAtArrival); every other map (stage 0) keeps the pawn's own default, passed in.
	 */
	bool GetMapFlashlight(int32 PrologueStage, bool& bInOutOnAtStart) const;

	/** Hologram, every value clamped. */
	FHologramTuning GetHologramTuning() const;

	/** PrologueTable, loaded (null when unset or missing). */
	UDataTable* LoadPrologueTable() const;

	/** Level of the home AI's text at a battery fraction, with the thresholds of TextDegradation (clamped). */
	ETextDegradation GetTextLevel(double BatteryFraction) const;

	/** Typing speed of the home AI's text at a battery fraction (TextDegradation's speeds, clamped, linear in between). */
	float GetTextCharsPerSecond(double BatteryFraction) const;

	/** SceneCharsPerSecond, clamped. */
	float GetSceneCharsPerSecond() const { return FMath::Clamp(SceneCharsPerSecond, 1.f, 500.f); }

	/** Twin AI parts the archive must hold for "Open the structure": OpenEndingTwinParts, or every part of the twin table for 0. */
	int32 GetOpenEndingTwinParts() const;

	/** Text and background colour of a scene line spoken by Voice (from TextColors). */
	void GetSceneVoiceColors(ESceneVoice Voice, FLinearColor& OutText, FLinearColor& OutBackground) const;

	/** ScenesTable, loaded (null when unset or missing). */
	UDataTable* LoadScenesTable() const;

	/** LogsTable, loaded (null when unset or missing). */
	UDataTable* LoadLogsTable() const;

	/** TwinAITable, loaded (null when unset or missing). */
	UDataTable* LoadTwinAITable() const;

	/** TextColors of the project settings. */
	static const FSuitTextColors& GetTextColors() { return Get()->TextColors; }

	/** LogCharsPerSecond, clamped. */
	float GetLogCharsPerSecond() const { return FMath::Clamp(LogCharsPerSecond, 1.f, 500.f); }

	/** TwinCharsPerSecond, clamped. */
	float GetTwinCharsPerSecond() const { return FMath::Clamp(TwinCharsPerSecond, 1.f, 500.f); }

	/** TranslationLagChars, clamped. */
	int32 GetTranslationLagChars() const { return FMath::Clamp(TranslationLagChars, 0, 200); }

	/** AIMessagesTable, loaded (null when unset or missing). */
	UDataTable* LoadAIMessagesTable() const;

	/** EndingsTable, loaded (null when unset or missing). */
	UDataTable* LoadEndingsTable() const;

	/** Text of the ending row RowName (Inject, Open, Loss), in full; empty when there is none. */
	FText GetEndingText(FName RowName) const;

	/** Act of the map named PackageName (1-based position in AreaMaps, PIE prefix ignored); 0 when it is not an area map. */
	int32 GetActOfMap(const FString& PackageName) const;

	/** Recipe of Act (1-4), or the test recipe for 0; null when the act has none. */
	const FAreaRecipe* FindRecipe(int32 Act) const;

	/** Seed of Act (1-4), or the test seed for 0; -1 when the act has none. */
	int32 GetSeed(int32 Act) const;

	/** Jump tuning of Act (1-4), or the test tuning for 0, with every value clamped; false when the act has none. */
	bool GetActTuning(int32 Act, FActTuning& OutTuning) const;

	/** GrazeJumpFraction, clamped to [0, 1]. */
	float GetGrazeJumpFraction() const { return FMath::Clamp(GrazeJumpFraction, 0.f, 1.f); }

	/** ScanColors of the project settings. */
	static const FScanColors& GetScanColors() { return Get()->ScanColors; }

	/** ScanOcclusion of the project settings (clamped where it is pushed, by the lidar). */
	static const FScanOcclusion& GetScanOcclusion() { return Get()->ScanOcclusion; }

	/** ScanLinks of the project settings (clamped where it is pushed, by the lidar). */
	static const FScanLinks& GetScanLinks() { return Get()->ScanLinks; }

	/** InteractableScanChannel of the project settings, clamped to [1, 4]. */
	static int32 GetInteractableScanChannel() { return FMath::Clamp(Get()->InteractableScanChannel, 1, 4); }

	/** The map a run starts in: the first entry of AreaMaps (null when the list is empty). */
	TSoftObjectPtr<UWorld> GetFirstAreaMap() const { return AreaMaps.Num() > 0 ? AreaMaps[0] : TSoftObjectPtr<UWorld>(); }
};
