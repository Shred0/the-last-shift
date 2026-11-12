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
		TSoftObjectPtr<UDataTable> SavedMessages;
		TSoftObjectPtr<UDataTable> SavedEndings;
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

	static void SetChannels(UWorld* World, UDataTable* Table)
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
			}
		}
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
		G.Messages.Reset(BuildAIMessages());
		G.Endings.Reset(BuildEndings());
		Settings->AIMessagesTable = TSoftObjectPtr<UDataTable>(G.Messages.Get());
		Settings->EndingsTable = TSoftObjectPtr<UDataTable>(G.Endings.Get());
		G.bInstalled = true;
		SetChannels(World, G.Messages.Get());
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
		G.bInstalled = false;
		SetChannels(World, Settings->LoadAIMessagesTable());
		G.Messages.Reset();
		G.Endings.Reset();
		UE_LOG(LogTheLastShift, Log, TEXT("Narrative fixture removed: messages %s, endings %s."), *GetNameSafe(Settings->LoadAIMessagesTable()),
			*GetNameSafe(Settings->LoadEndingsTable()));
	}

	bool IsInstalled()
	{
		return G.bInstalled;
	}
}

#endif // !UE_BUILD_SHIPPING
