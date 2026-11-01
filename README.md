# mod-global-scaling (AzerothCore 3.3.5a)
<img width="1024" height="506" alt="Banner" src="https://github.com/user-attachments/assets/f754f1db-b393-4a0c-b677-b7d8f86ca398" />


**`mod-global-scaling`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** that dynamically scales all world zones, dungeons, raids, and world bosses across **Vanilla**, **The Burning Crusade**, and **Wrath of the Lich King** up to **Level 80 endgame difficulty**.

*⚠️ **Important Note:** This module scales creatures **dynamically in memory** (on-the-fly) as they spawn and enter combat. **It makes zero permanent changes to your server database (SQL)**. If you disable the module or change your configuration, all creatures instantly revert to their native Blizzard default values without requiring a database rollback.*

Instead of using flat, one-size-fits-all multipliers, this module uses **per-expansion exponential curves**, **late-expansion catch-up modifiers**, and **individual per-instance / per-difficulty configurations** so that every piece of content in the game remains relevant, challenging, and mechanically intact at Level 80.

---

## 🎯 What to Expect (Design Philosophy & Baselines)

When installing this module with the default configuration (`mod_global_scaling.conf.dist`), players can expect the entire world of Azeroth, Outland, and Northrend to be calibrated around **Tier 10 (Patch 3.3.5a) endgame standards**:

* **Overworld Zones (Vanilla, TBC, WotLK):** All eligible creatures (excluding critters and those below the minimum native level floor) are scaled to Level 80+ (+1 for Elites/Rares, +2 for Rare Elites, +3 for Bosses), turning questing zones and outdoor elite areas into max-level endgame zones.
* **All 5-Man Dungeons (Vanilla, TBC Normal/Heroic, WotLK Normal/Heroic):** Calibrated to match **WotLK Tier 10 Heroic Dungeons** (*Forge of Souls*, *Pit of Saron*, and *Halls of Reflection* Heroic).
* **10-Man & 20-Man Raids (Zul'Gurub, AQ20, Karazhan, Zul'Aman, WotLK 10-Man):** Calibrated toward **Icecrown Citadel 10-Man Heroic / Ruby Sanctum 10-Man Heroic**.
* **25-Man & 40-Man Raids (MC, BWL, AQ40, TBC 25-Man, WotLK 25-Man):** Calibrated toward **Icecrown Citadel 25-Man Heroic / Ruby Sanctum 25-Man Heroic**.
* **World Bosses (Kazzak, Doomwalker, Azuregos, Dragons of Nightmare):** Individually overridden to Level 83 Skull Bosses tuned for full endgame raid groups.

---

## ✨ Key Features

### 1. Mathematical Exponential Scaling per Expansion
Creatures from earlier expansions have fundamentally different base stat budgets in the database compared to WotLK creatures. The module calculates a level ratio (`TargetLevel / NativeLevel`) and applies customizable **exponential curves (`std::pow`)** separately for **Vanilla**, **TBC**, and **WotLK** across 7 distinct stat categories:
* **Health (HP)** & **Mana**
* **Melee / Ranged / Off-Hand Weapon Damage** & **Attack Power**
* **Direct Spell Damage**
* **Periodic Damage (DoTs)**
* **Healing Spells** (percentage-based heals like `SPELL_EFFECT_HEAL_PCT` are automatically excluded so bosses don't overheal)
* **Armor** (automatically capped at `10,643` unless the creature's native armor is already higher) & **Spell Resistances**

### 2. Low-Level Floor & Late-Expansion Catch-Up System
* **Minimum Native Level Floor (`MinNativeLevel = 14`):** Prevents level 1–13 starter-zone creatures from exploding in stats due to extreme `80 / 1` level ratios by smoothing their baseline ratio.
* **Late-Expansion Catch-Up (`GlobalScaling.CatchUp`):** Smoothly boosts higher-level overworld mobs in Vanilla (levels 14–60) and TBC (levels 58–70) so endgame zones like Silithus, Eastern Plaguelands, or Netherstorm feel appropriately stronger than early leveling zones.

### 3. Smart Spell & Combat Engine Protections
* **No Double-Dipping on Weapon Spells:** Physical abilities based on weapon damage (`SPELL_EFFECT_WEAPON_DAMAGE`, `NORMALIZED_WEAPON_DMG`, `WEAPON_PERCENT_DAMAGE`) automatically scale via the creature's scaled Melee Damage and Attack Power and are not double-multiplied by the spell hook.
* **Player Pet & Guardian Protection:** Player-owned pets, summons, mind-controlled units, and guardians are strictly ignored so player damage is never artificially inflated.
* **Invisible Trigger Protection:** Traps, poison clouds, and invisible caster triggers (`Rank.Trigger`) scale their spell/DoT damage without having their level or visual model size altered.
* **Native Level Preservation:** Creatures whose native spawn level is already higher than the calculated target level will never be down-scaled, preserving their native `WeaponSkill` (`Level * 5`) against tanks.
* **Dynamic Combat & Respawn Tracking:** Periodic checks and combat hooks ensure that tank debuffs on pull (e.g., *Demoralizing Shout*, *Thunder Clap*), phase shifts, or creature respawns never revert a creature's melee damage or level.

### 4. Per-Instance, Per-Difficulty & Per-Rank Granularity
Every single dungeon and raid map ID in Vanilla, TBC, and WotLK is pre-configured in `mod_global_scaling.conf.dist` with 5 distinct rank tiers per difficulty (`0` = Normal / 10M, `1` = Heroic / 25M / 40M, `2` = 10M Heroic, `3` = 25M Heroic):
* `Trash` (Normal non-elite mobs & minor adds)
* `Commander` (Standard dungeon/raid Elites, `rank == 1`)
* `BossNonSkull` (Dungeon bosses & Rare-Elite bosses)
* `BossSkull` (Raid bosses & World bosses, `rank == 3`)
* `Trigger` (Environmental hazards & invisible casters)

### 5. Custom Overrides (Creatures, Spells, and Visual Model Size)
Fine-tune specific encounters without recompiling:
* **`GlobalScaling.CustomCreatures`:** Override Base Level, HP, Mana, Melee, Spell, Armor, Resistance, Gold, Heal, and DoT multipliers for specific NPC Entry IDs.
* **Spell Overrides (`CustomDamageSpells`, `CustomHealingSpells`, `CustomShieldSpells`, `CustomMeleeSpells`):** Apply custom multipliers to specific Spell IDs (e.g., *Shield Slam* or specific boss abilities that don't scale natively with Attack Power).
* **Visual Size Overrides (`SizeOverride.Maps`, `SizeOverride.Overworld` & `SizeOverride.Creatures`):** Multiplies the visual model scale (`OBJECT_FIELD_SCALE_X`) of creatures with full toggle support (`.Enable = 1/0`) and priority rules:
  * **Overworld (`SizeOverride.Overworld`):** Scales overworld creatures per continent map ID (`MapID`) across 5 distinct ranks: `Trash`, `Elite`, `RareElite`, `Rare`, and `SkullBoss`.
  * **Instances (`SizeOverride.Maps`):** Multiplies the visual model scale of trash mobs and bosses per dungeon/raid instance (`MapID`). In vanilla World of Warcraft, low-level dungeons (such as *Ragefire Chasm* or *The Deadmines*) natively feature much smaller creature and boss models compared to high-level endgame instances. This setting lets you scale up their physical presence so they look as intimidating as their new Level 80 endgame stats.
  * **Creatures (`SizeOverride.Creatures`):** Multiplies the visual size of a specific creature or boss by its `EntryID`, taking absolute priority over both map-wide and overworld settings.

---

## 🛠️ In-Game Commands

The module provides the `.gscaling` command suite:

| Command | Security | Description |
| :--- | :--- | :--- |
| `.gscaling info` | Player | Displays whether Global Scaling is enabled, active environment flags (Dungeons/Raids), base levels, and instance delay. |
| `.gscaling status` | Player | Displays detailed scaling info for your current target: Entry ID, Environment, Level (+Bonus), Expansion, Rank Multipliers, Total Multipliers, and final Health/Armor. |
| `.gscaling toggle` | Administrator | Toggles the entire Global Scaling module ON or OFF in real time and immediately updates nearby creatures (reverting or re-applying stats). |
| `.gscaling update` | Administrator | Forces an immediate re-scaling pass on all creatures within update range (or across the entire dungeon/raid). |

*(Note: Running `.reload config` in-game is also supported and will reload all multipliers from `mod_global_scaling.conf`.)*

---

## 🖼️ Showcase

<img width="3686" height="1944" alt="Sans titreaaAAAAAAAAAA" src="https://github.com/user-attachments/assets/954379ee-40b4-47da-a316-eacda42aca72" />
<img width="3686" height="1944" alt="Sans titreaaAAAAAAAAA" src="https://github.com/user-attachments/assets/70fedfdf-2dda-45ce-9de3-f3a4ba56066e" />
<img width="4096" height="2160" alt="Sans titreaaAAAAAAAA" src="https://github.com/user-attachments/assets/43590c36-2297-4594-84b2-02e3898f2874" />
<img width="4096" height="2160" alt="Sans titreaaAAAAAAA" src="https://github.com/user-attachments/assets/6c251d50-741c-4b09-a8e1-6e87ec244e3e" />
<img width="4096" height="2160" alt="Sans titreaaAAAAAA" src="https://github.com/user-attachments/assets/5789ed63-c535-4e37-bcaf-d8caf4c5a29e" />
<img width="4096" height="2160" alt="Sans titreaaAAAAA" src="https://github.com/user-attachments/assets/281b62f0-15a9-4c08-b901-d47d18b90496" />
<img width="4096" height="2160" alt="Sans titreaaAAAA" src="https://github.com/user-attachments/assets/77acdc83-a4cf-45cb-aa04-09cec617855c" />
<img width="4096" height="2160" alt="Sans titreaaAAA" src="https://github.com/user-attachments/assets/a9d464a3-4e29-4f30-971d-01858a76077c" />
<img width="4096" height="2160" alt="Sans titreaaAA" src="https://github.com/user-attachments/assets/fe432ef6-88f1-4b52-b691-96ada974bb97" />
<img width="4096" height="2160" alt="Sans titreaaA" src="https://github.com/user-attachments/assets/0246788e-6d81-46af-b097-567ee0b3c8cf" />
<img width="4096" height="2160" alt="Sans titreaa" src="https://github.com/user-attachments/assets/b9133bc2-6ca6-4de9-805d-1ab0c2431746" />
<img width="4096" height="2160" alt="Sans titrea" src="https://github.com/user-attachments/assets/d38b7734-d36f-41d5-8f79-d984c6c3fdab" />


---

## ⚙️ Installation

1. Clone or copy the `mod-global-scaling` folder into your AzerothCore `modules/` directory:
   ```bash
   cd /path/to/azerothcore-wotlk/modules
   git clone <your-repo-url> mod-global-scaling
   ```
2. Re-run **CMake** and compile your AzerothCore solution.
3. Copy `mod_global_scaling.conf.dist` to your server's `configs/modules/` directory and rename it to `mod_global_scaling.conf`.
4. Start `worldserver` and customize `mod_global_scaling.conf` to fit your server's balance preferences.
