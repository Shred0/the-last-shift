# Act 4 — The Return

**A4.1** (`A4_AreaEntry_1`, `HomeAI`): Last stretch to the ship. That presence is close to our own hull now. Battery at {battery}. Come home.

**E.1** (`Engineer` — Before: `A4_Spend_1`, trigger `Spend`, device: any terminal on the way to the ship): I am spending energy at this terminal to reach the ship.

**A4.2** (`A4_Spend_1`, `HomeAI` — trigger `Spend`, generic for any device): One unit less. Battery at {battery}. Run.

**E.2** (`A4_EntitySeen_1`, `Engineer` — CSV row `A4_EntitySeen_1`, trigger `EntitySeen`): There is a figure appearing across the scans, it is following me toward the exit.

**A4.3** (`A4_EntityFound_1`, `HomeAI`): I will mark in red, in your scans, what does not stand still. Do not stop.

**A4.4** (`A4_JumpReturn_1`, `HomeAI`): Still for {still}. Jump of {jump}. Simulation off: only darkness at home. Time: {home}; battery: {battery}.

**E.3** (`Engineer` — first contact, Before: `A4_EntityContact_1`, trigger `EntityContact`): What!? Centuries have passed??? How is that possible? Was it that presence?

**A4.5** (`A4_EntityContact_1`, `HomeAI`): It is outside our hull too. One more contact and there will be nothing left to return to.

**E.4** (`Engineer` — Before: `A4_JumpReturn_2`, trigger `JumpReturn`): It caught me again... Please, tell me you are still there...

**A4.6** (`A4_JumpReturn_2`, `HomeAI`): Still for {still}. Another {jump}. We are still here, in the dark. Time: {home}; battery: {battery}.
