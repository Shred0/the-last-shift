# Act 1 — The Hull

**A1.1** (`A1_AreaEntry_1`, `HomeAI`): You are inside the outer hull. Its geometry matches our own construction plans. Home battery at {battery}.

**E.1** (`Engineer` — before `A1_AreaStart_1`, trigger `AreaStart`): I am inside. This structure is identical to ours.

**A1.2** (`A1_AreaStart_1`, `HomeAI`): Identical down to the bolts, and completely dead. The tachyonic residues are here, outside the normal flow of time.

**E.2** (`Engineer` — between `A1_AreaStart_1` and `A1_AreaStart_2`; fits no separate event of the game, since `AreaStart` rows play one after another): How do I collect them?

**A1.3** (`A1_AreaStart_2`, `HomeAI`): Your suit stops time by shifting you out of it. Light stands still there and I cannot reach you: scan to see in the dark.

**E.3** (`Engineer` — between `A1_AreaStart_2` and `A1_AreaStart_3`; fits no separate event of the game, since `AreaStart` rows play one after another): And once in the scan, how do I extract them?

**A1.4** (`A1_AreaStart_3`, `HomeAI`): They show as dense clusters of points. Stay in contact with one until the suit draws it in, then return to normal time.

**E.4** (`Engineer` — before `A1_FirstEnergy_1`, trigger `FirstEnergy`): I collected the first residue, what do I do with it now?

**A1.5** (`A1_FirstEnergy_1`, `HomeAI`): Spend it at terminals to power your way through, or keep it for home. Every unit you spend here is one less for our battery.

**E.5** (`Engineer` — Before: `A1_Spend_1` / `A1_SpendDoor_1`, trigger `Spend` / `SpendDoor`, device: gate door): I am powering this terminal to move deeper into the structure.

**A1.6** (`A1_Spend_1`, `HomeAI` — trigger `Spend`, also approved for `A1_SpendDoor_1` trigger `SpendDoor`): One unit routed to the local circuit. The passage is powered; our home battery is at {battery}.

**E.6** (`Engineer` — first contact, before `A1_EntityContact_1`, trigger `EntityContact`): There was something in the scan that pushed me back!

**A1.10** (`A1_EntityContact_1`, `HomeAI`): You are back where you stopped time, and no time passed out here. Something in there forced you back: keep away from it.

**A1.7** (`A1_JumpReturn_1`, `HomeAI` — on a graze in Act 1): Curious. You stood still for {still}, yet {jump} passed out here. Home time is {home}; the battery is at {battery}.

**E.7** (`Engineer` — before `A1_JumpReturn_2`, trigger `JumpReturn`): It happened again... There really is something in there.

**A1.8** (`A1_JumpReturn_2`, `HomeAI`): Another drift: {still} while time stood still, a jump of {jump}. {home} passed at home; battery still at {battery}.

**E.8** (`A1_EntitySeen_1`, `Engineer` — CSV row `A1_EntitySeen_1`, trigger `EntitySeen`): I saw something strange move from one scan to the next.

**A1.9** (`A1_EntityFound_1`, `HomeAI`): Nothing should move while time is stopped. I will mark in red, in your scans, whatever does not stand still in time.
