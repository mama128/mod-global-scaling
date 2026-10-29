# mod-global-scaling (AzerothCore 3.3.5a)

**`mod-global-scaling`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** that dynamically scales all world zones, dungeons, raids, and world bosses across **Vanilla**, **The Burning Crusade**, and **Wrath of the Lich King** up to **Level 80 endgame difficulty**.

Instead of using flat, one-size-fits-all multipliers, this module uses **per-expansion exponential curves**, **late-expansion catch-up modifiers**, and **individual per-instance / per-difficulty configurations** so that every piece of content in the game remains relevant, challenging, and mechanically intact at Level 80.

---

## 🎯 What to Expect (Design Philosophy & Baselines)

When installing this module with the default configuration (`mod_global_scaling.conf.dist`), players can expect the entire world of Azeroth, Outland, and Northrend to be calibrated around **Tier 10 (Patch 3.3.5a) endgame standards**:

* **Overworld Zones (Vanilla, TBC, WotLK):** All creatures (above the minimum native level floor) are scaled to **Level 80+** (+1 for Elites/Rares, +2 for Rare Elites, +3 for Bosses), turning questing zones and outdoor elite areas into max-level endgame zones.
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
* **Visual Size Overrides (`SizeOverride.Maps` & `SizeOverride.Creatures`):** Multiply the visual model scale (`OBJECT_FIELD_SCALE_X`) of bosses and trash per instance or per creature Entry ID.

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

## ⚙️ Installation

1. Clone or copy the `mod-global-scaling` folder into your AzerothCore `modules/` directory:
   ```bash
   cd /path/to/azerothcore-wotlk/modules
   git clone <your-repo-url> mod-global-scaling
   ```
2. Re-run **CMake** and compile your AzerothCore solution.
3. Copy `mod_global_scaling.conf.dist` to your server's `configs/modules/` directory and rename it to `mod_global_scaling.conf`.
4. Start `worldserver` and customize `mod_global_scaling.conf` to fit your server's balance preferences.
