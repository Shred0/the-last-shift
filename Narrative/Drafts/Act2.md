# Act 2 — The Server Halls

**A2.1** (`A2_AreaEntry_1`, `HomeAI`): You are in the server halls. Their population's minds are stored in these racks, frozen in stasis. Battery at {battery}.

**E.1** (`Engineer` — Before: `A2_SpendLog_1` (currently `A2_Spend_1` in CSV until Brief 22), trigger `SpendLog`, device: log terminal): There is a terminal here among the servers, I am using one unit to read their logs.

**A2.2** (`A2_SpendLog_1` / currently `A2_Spend_1` in CSV, `HomeAI` — trigger `SpendLog` / `Spend`, approved): One unit spent at the console. Remember why you are here: our home battery is at {battery}.

**E.2** (`A2_EntitySeen_1`, `Engineer` — CSV row `A2_EntitySeen_1`, trigger `EntitySeen`): There is a humanoid figure moving in the scans while time stands still.

**A2.3** (`A2_EntityFound_1`, `HomeAI`): Nothing should move there. I am isolating its trace: from now on your scans will mark in red what does not stand still.

**A2.4** (`A2_JumpReturn_1`, `HomeAI`): You stood still for {still}, yet {jump} passed here. I switched off the weather and the sea at home. Time: {home}; battery: {battery}.

**E.3** (`Engineer` — first contact, before `A2_EntityContact_1`, trigger `EntityContact`): That figure managed to touch me! It is what made me jump forward!

**A2.5** (`A2_EntityContact_1`, `HomeAI`): That contact bent our clock by months. Whatever inhabits that dimension, do not let it reach you again.

**E.4** (`Engineer` — before `A2_JumpReturn_2`, trigger `JumpReturn`): It caught me again... How much time did we lose?

**A2.6** (`A2_JumpReturn_2`, `HomeAI`): Another {jump} lost while you stood still for {still}. I switched off another sector at home. Time: {home}; battery still at {battery}.
