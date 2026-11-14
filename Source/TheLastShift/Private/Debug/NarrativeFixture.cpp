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

	static void SetChannels(UWorld* World, UDataTable* Table, UDataTable* TwinTable)
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
			}
		}
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
		G.Messages.Reset(BuildAIMessages());
		G.Endings.Reset(BuildEndings());
		G.Logs.Reset(BuildLogs(8));
		G.Twin.Reset(BuildTwinAI());
		Settings->AIMessagesTable = TSoftObjectPtr<UDataTable>(G.Messages.Get());
		Settings->EndingsTable = TSoftObjectPtr<UDataTable>(G.Endings.Get());
		Settings->LogsTable = TSoftObjectPtr<UDataTable>(G.Logs.Get());
		Settings->TwinAITable = TSoftObjectPtr<UDataTable>(G.Twin.Get());
		G.bInstalled = true;
		SetChannels(World, G.Messages.Get(), G.Twin.Get());
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
		G.bInstalled = false;
		SetChannels(World, Settings->LoadAIMessagesTable(), Settings->LoadTwinAITable());
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
