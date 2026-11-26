// SPDX-License-Identifier: MIT

#include "Debug/NarrativeFixture.h"

#if !UE_BUILD_SHIPPING

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Narrative/NarrativeRows.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace NarrativeFixture
{
	struct FState
	{
		bool bInstalled = false;
		TStrongObjectPtr<UDataTable> Messages;
		TStrongObjectPtr<UDataTable> Endings;
		TStrongObjectPtr<UDataTable> Logs;
		TStrongObjectPtr<UDataTable> Twin;
		TStrongObjectPtr<UDataTable> Scenes;
		TStrongObjectPtr<UDataTable> Prologue;
		TStrongObjectPtr<UDataTable> Engineer;
		TSoftObjectPtr<UDataTable> SavedEngineer;
		TSoftObjectPtr<UDataTable> SavedScenes;
		TSoftObjectPtr<UDataTable> SavedPrologue;
		TSoftObjectPtr<UDataTable> SavedMessages;
		TSoftObjectPtr<UDataTable> SavedEndings;
		TSoftObjectPtr<UDataTable> SavedLogs;
		TSoftObjectPtr<UDataTable> SavedTwin;
	};
	static FState G;

	static void AddMessage(UDataTable* Table, ETextTrigger Trigger, int32 Act, int32 Order, const FString& Text)
	{
		FAIMessageRow Row;
		Row.Trigger = Trigger;
		Row.Act = Act;
		Row.Order = Order;
		Row.Text = FText::FromString(Text);
		Table->AddRow(FName(*FString::Printf(TEXT("A%d_%s_%d"), Act, LexToString(Trigger), Order)), Row);
	}

	UDataTable* BuildAIMessages()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureAIMessages")));
		Table->RowStruct = FAIMessageRow::StaticStruct();
		for (int32 Order = 1; Order <= 3; ++Order)
		{
			AddMessage(Table, ETextTrigger::AreaStart, 1, Order, FString::Printf(TEXT("Fixture area start line %d of 3 for act 1, a neutral sentence for the tests."), Order));
		}
		AddMessage(Table, ETextTrigger::FirstEnergy, 1, 1, TEXT("Fixture first energy line for act 1, a neutral sentence for the tests."));
		for (int32 Act = 1; Act <= 4; ++Act)
		{
			for (int32 Order = 1; Order <= 2; ++Order)
			{
				AddMessage(Table, ETextTrigger::JumpReturn, Act, Order, FString::Printf(
					TEXT("Fixture return %d of act %d: still for {still}, a jump of {jump}, {home} at home, battery {battery}."), Order, Act));
			}
			AddMessage(Table, ETextTrigger::AreaEntry, Act, 1, FString::Printf(TEXT("Fixture entry line of act %d: {home} at home, battery {battery}, a neutral test."), Act));
			AddMessage(Table, ETextTrigger::Spend, Act, 1, FString::Printf(TEXT("Fixture spend line of act %d: one unit spent at a terminal, battery {battery}."), Act));
			AddMessage(Table, ETextTrigger::EntityFound, Act, 1, FString::Printf(TEXT("Fixture entity found line of act %d: a neutral sentence, battery {battery}."), Act));
			AddMessage(Table, ETextTrigger::EntitySeen, Act, 1, FString::Printf(TEXT("Fixture engineer line of act %d: the engineer saw it, a neutral sentence that stays whole at any battery, {battery}."), Act));
			AddMessage(Table, ETextTrigger::EntityContact, Act, 1, FString::Printf(TEXT("Fixture entity contact line of act %d: the home AI on the first contact, battery {battery}."), Act));
		}
		return Table;
	}

	UDataTable* BuildEndings()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureEndings")));
		Table->RowStruct = FEndingRow::StaticStruct();
		for (const TCHAR* Name : { TEXT("Inject"), TEXT("Open"), TEXT("Loss") })
		{
			FEndingRow Row;
			Row.Text = FText::FromString(FString::Printf(TEXT("Fixture ending %s, a neutral sentence shown in full by the tests."), Name));
			Table->AddRow(Name, Row);
		}
		return Table;
	}

	UDataTable* BuildLogs(int32 LogCount)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureLogs")));
		Table->RowStruct = FLogRow::StaticStruct();
		for (int32 Order = 1; Order <= FMath::Clamp(LogCount, 1, 99); ++Order)
		{
			FLogRow Row;
			Row.Order = Order;
			Row.Text = FText::FromString(FString::Printf(TEXT("Fixture log %d of the tests. A neutral sentence long enough to wrap on the grid of cells. ")
				TEXT("A second neutral sentence closes entry %d."), Order, Order));
			Table->AddRow(FName(*FString::Printf(TEXT("L_%d"), Order)), Row);
		}
		return Table;
	}

	UDataTable* BuildTwinAI()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureTwinAI")));
		Table->RowStruct = FTwinAIRow::StaticStruct();
		for (int32 Part = 1; Part <= 3; ++Part)
		{
			for (int32 Order = 1; Order <= 2; ++Order)
			{
				FTwinAIRow Row;
				Row.Part = Part;
				Row.Order = Order;
				Row.Text = FText::FromString(FString::Printf(TEXT("Fixture twin part %d row %d: a neutral sentence typed as barcodes for the tests."), Part, Order));
				Table->AddRow(FName(*FString::Printf(TEXT("T_%d_%d"), Part, Order)), Row);
			}
		}
		return Table;
	}

	static const TCHAR* EndingMessage = TEXT("Fixture last message of the home AI before the choice: the harvest and the structure are waiting for an answer.");

	FString GetEndingMessageText()
	{
		return EndingMessage;
	}

	static void AddSceneRow(UDataTable* Table, FName Scene, int32 Order, ESceneVoice Voice, FName Cue, const FString& Text)
	{
		FSceneRow Row;
		Row.Scene = Scene;
		Row.Order = Order;
		Row.Voice = Voice;
		Row.Cue = Cue;
		Row.Text = FText::FromString(Text);
		Table->AddRow(FName(*FString::Printf(TEXT("%s_%d"), *Scene.ToString(), Order)), Row);
	}

	UDataTable* BuildScenes(bool bPrologue)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureScenes")));
		Table->RowStruct = FSceneRow::StaticStruct();
		if (bPrologue)
		{
			const FName P(TEXT("Prologue"));
			AddSceneRow(Table, P, 1, ESceneVoice::Paradise, NAME_None, TEXT("Fixture prologue line 1, a voice of the simulated world, dark on white."));
			AddSceneRow(Table, P, 2, ESceneVoice::Paradise, NAME_None, TEXT("Fixture prologue line 2, a second voice of the simulated world."));
			AddSceneRow(Table, P, 3, ESceneVoice::HomeAI, NAME_None, TEXT("Fixture prologue line 3, the home AI after the cut to black. This neutral sentence ")
				TEXT("is long enough to wrap onto a second line of the column. A third sentence makes sure that the block holds every one of its lines."));
			AddSceneRow(Table, P, 4, ESceneVoice::HomeAI, TEXT("FixtureCue"), TEXT("Fixture prologue line 4, the home AI, with a cue name on its row."));
			AddSceneRow(Table, P, 5, ESceneVoice::HomeAI, NAME_None, TEXT("Fixture prologue line 5, the home AI, a neutral sentence for the tests."));
			AddSceneRow(Table, P, 6, ESceneVoice::HomeAI, NAME_None, TEXT("Fixture prologue line 6, the last line of the prologue."));
		}
		AddSceneRow(Table, TEXT("Ending"), 1, ESceneVoice::HomeAI, NAME_None, EndingMessage);
		// Brief 22: the four voices one after another (scene Voices: tls.SceneTest part G, tls.Scene.Voices).
		const FName V(TEXT("Voices"));
		AddSceneRow(Table, V, 1, ESceneVoice::Paradise, NAME_None, TEXT("Fixture voice line 1, Paradise: a voice of the simulated world, dark on the white of the paradise."));
		AddSceneRow(Table, V, 2, ESceneVoice::EngineerParadise, NAME_None, TEXT("Fixture voice line 2, EngineerParadise: the engineer inside the paradise, where the articles and the long words always stay whole."));
		AddSceneRow(Table, V, 3, ESceneVoice::HomeAI, NAME_None, TEXT("Fixture voice line 3, HomeAI: the home AI after the cut to black, with the articles and the long words of the tests."));
		AddSceneRow(Table, V, 4, ESceneVoice::Engineer, NAME_None, TEXT("Fixture voice line 4, Engineer: the engineer on black, a neutral sentence for the tests."));
		return Table;
	}

	UDataTable* BuildPrologue(const TArray<FName>& Without)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixturePrologue")));
		Table->RowStruct = FPrologueRow::StaticStruct();
		auto Add = [Table, &Without](FName Group, int32 Order)
		{
			if (Without.Contains(Group))
			{
				return;
			}
			FPrologueRow Row;
			Row.Group = Group;
			Row.Order = Order;
			Row.Text = FText::FromString(FString::Printf(TEXT("Fixture prologue %s line %d, a neutral sentence for the tests."), *Group.ToString(), Order));
			Table->AddRow(FName(*FString::Printf(TEXT("%s_%d"), *Group.ToString(), Order)), Row);
		};
		// Added out of order on purpose: the rows are said in ascending Order.
		Add(PrologueGroups::Wake, 2);
		Add(PrologueGroups::Wake, 1);
		Add(PrologueGroups::Hologram, 1);
		Add(PrologueGroups::Flight, 1);
		Add(PrologueGroups::Flight, 2);
		Add(PrologueGroups::Door, 2);
		Add(PrologueGroups::Door, 1);
		Add(PrologueGroups::Hangar, 1);
		Add(PrologueGroups::Hatch, 1);
		Add(PrologueGroups::Hatch, 2);
		Add(PrologueGroups::Light, 2);
		Add(PrologueGroups::Light, 1);
		return Table;
	}

	FString GetPrologueLineText(FName Group, int32 Order)
	{
		return FString::Printf(TEXT("Fixture prologue %s line %d, a neutral sentence for the tests."), *Group.ToString(), Order);
	}

	void SetPrologueGroups(const TArray<FName>& Without)
	{
		if (!G.bInstalled)
		{
			return;
		}
		G.Prologue.Reset(BuildPrologue(Without));
		GetMutableDefault<UTheLastShiftSettings>()->PrologueTable = TSoftObjectPtr<UDataTable>(G.Prologue.Get());
		FString Left;
		for (const FName& Group : Without)
		{
			Left += (Left.IsEmpty() ? TEXT("") : TEXT(", ")) + Group.ToString();
		}
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture: prologue lines now %s (%d rows, without: %s)."), *GetNameSafe(G.Prologue.Get()), G.Prologue->GetRowMap().Num(),
			Left.IsEmpty() ? TEXT("none") : *Left);
	}

	void SetPrologueRows(bool bPrologue)
	{
		if (!G.bInstalled)
		{
			return;
		}
		G.Scenes.Reset(BuildScenes(bPrologue));
		GetMutableDefault<UTheLastShiftSettings>()->ScenesTable = TSoftObjectPtr<UDataTable>(G.Scenes.Get());
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture: scenes now %s (%d rows, %s prologue)."), *GetNameSafe(G.Scenes.Get()), G.Scenes->GetRowMap().Num(),
			bPrologue ? TEXT("with a") : TEXT("no"));
	}

	static void SetChannels(UWorld* World, UDataTable* Table, UDataTable* TwinTable, UDataTable* EngineerTable)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UTextChannelComponent* Channel = It->FindComponentByClass<UTextChannelComponent>())
			{
				Channel->SetMessageTable(Table);
				Channel->SetTwinTable(TwinTable);
				Channel->SetEngineerTable(EngineerTable);
			}
		}
	}

	FName GetEngineerLineName(FName Before, int32 Order)
	{
		return FName(*FString::Printf(TEXT("E_%s_%d"), *Before.ToString(), Order));
	}

	FString GetEngineerLineText(FName Before, int32 Order)
	{
		return FString::Printf(TEXT("Fixture engineer line %d before %s: the engineer answers first, a neutral sentence for the tests."), Order, *Before.ToString());
	}

	static UDataTable* BuildEngineerLines(const TArray<FEngineerLine>& Lines)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsFixtureEngineerLines")));
		Table->RowStruct = FEngineerLineRow::StaticStruct();
		for (const FEngineerLine& Line : Lines)
		{
			FEngineerLineRow Row;
			Row.Before = Line.Before;
			Row.Order = Line.Order;
			Row.Text = FText::FromString(GetEngineerLineText(Line.Before, Line.Order));
			Table->AddRow(GetEngineerLineName(Line.Before, Line.Order), Row);
		}
		return Table;
	}

	void SetEngineerLines(UWorld* World, const TArray<FEngineerLine>& Lines)
	{
		if (!G.bInstalled)
		{
			return;
		}
		G.Engineer.Reset(BuildEngineerLines(Lines));
		GetMutableDefault<UTheLastShiftSettings>()->EngineerLinesTable = TSoftObjectPtr<UDataTable>(G.Engineer.Get());
		SetChannels(World, G.Messages.Get(), G.Twin.Get(), G.Engineer.Get());
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture: engineer lines now %s (%d rows)."), *GetNameSafe(G.Engineer.Get()), G.Engineer->GetRowMap().Num());
	}

	FString GetAddedMessageText(ETextTrigger Trigger, int32 Act, int32 Order)
	{
		return FString::Printf(TEXT("Fixture %s line %d of act %d, a neutral sentence added by a test, battery {battery}."), LexToString(Trigger), Order, Act);
	}

	void AddMessageRow(ETextTrigger Trigger, int32 Act, int32 Order)
	{
		if (!G.bInstalled || !G.Messages)
		{
			return;
		}
		AddMessage(G.Messages.Get(), Trigger, Act, Order, GetAddedMessageText(Trigger, Act, Order));
	}

	void SetLogCount(int32 LogCount)
	{
		if (!G.bInstalled)
		{
			return;
		}
		G.Logs.Reset(BuildLogs(LogCount));
		GetMutableDefault<UTheLastShiftSettings>()->LogsTable = TSoftObjectPtr<UDataTable>(G.Logs.Get());
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture: logs now %s (%d rows)."), *GetNameSafe(G.Logs.Get()), G.Logs->GetRowMap().Num());
	}

	void Install(UWorld* World)
	{
		if (G.bInstalled)
		{
			return;
		}
		UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
		G.SavedMessages = Settings->AIMessagesTable;
		G.SavedEndings = Settings->EndingsTable;
		G.SavedLogs = Settings->LogsTable;
		G.SavedTwin = Settings->TwinAITable;
		G.SavedScenes = Settings->ScenesTable;
		G.Scenes.Reset(BuildScenes(true));
		Settings->ScenesTable = TSoftObjectPtr<UDataTable>(G.Scenes.Get());
		G.SavedPrologue = Settings->PrologueTable;
		G.Prologue.Reset(BuildPrologue());
		Settings->PrologueTable = TSoftObjectPtr<UDataTable>(G.Prologue.Get());
		G.Messages.Reset(BuildAIMessages());
		G.Endings.Reset(BuildEndings());
		G.Logs.Reset(BuildLogs(8));
		G.Twin.Reset(BuildTwinAI());
		Settings->AIMessagesTable = TSoftObjectPtr<UDataTable>(G.Messages.Get());
		Settings->EndingsTable = TSoftObjectPtr<UDataTable>(G.Endings.Get());
		Settings->LogsTable = TSoftObjectPtr<UDataTable>(G.Logs.Get());
		Settings->TwinAITable = TSoftObjectPtr<UDataTable>(G.Twin.Get());
		// No engineer line unless a test sets some: the tests never see the rows of Narrative/.
		G.SavedEngineer = Settings->EngineerLinesTable;
		G.Engineer.Reset(BuildEngineerLines({}));
		Settings->EngineerLinesTable = TSoftObjectPtr<UDataTable>(G.Engineer.Get());
		G.bInstalled = true;
		SetChannels(World, G.Messages.Get(), G.Twin.Get(), G.Engineer.Get());
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture installed: messages %s (%d rows), endings %s (%d rows); game tables %s, %s set aside."),
			*GetNameSafe(Settings->LoadAIMessagesTable()), G.Messages->GetRowMap().Num(), *GetNameSafe(Settings->LoadEndingsTable()), G.Endings->GetRowMap().Num(),
			*G.SavedMessages.ToString(), *G.SavedEndings.ToString());
	}

	void Restore(UWorld* World)
	{
		if (!G.bInstalled)
		{
			return;
		}
		UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
		Settings->AIMessagesTable = G.SavedMessages;
		Settings->EndingsTable = G.SavedEndings;
		Settings->LogsTable = G.SavedLogs;
		Settings->TwinAITable = G.SavedTwin;
		Settings->ScenesTable = G.SavedScenes;
		G.Scenes.Reset();
		Settings->PrologueTable = G.SavedPrologue;
		G.Prologue.Reset();
		Settings->EngineerLinesTable = G.SavedEngineer;
		G.bInstalled = false;
		SetChannels(World, Settings->LoadAIMessagesTable(), Settings->LoadTwinAITable(), Settings->LoadEngineerLinesTable());
		G.Engineer.Reset();
		G.Messages.Reset();
		G.Endings.Reset();
		G.Logs.Reset();
		G.Twin.Reset();
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture removed: messages %s, endings %s."), *GetNameSafe(Settings->LoadAIMessagesTable()),
			*GetNameSafe(Settings->LoadEndingsTable()));
	}

	bool IsInstalled()
	{
		return G.bInstalled;
	}
}

#endif // !UE_BUILD_SHIPPING
