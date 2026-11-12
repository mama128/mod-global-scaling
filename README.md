# mod-global-scaling (AzerothCore 3.3.5a)
<img width="1024" height="506" alt="Banner" src="https://github.com/user-attachments/assets/f754f1db-b393-4a0c-b677-b7d8f86ca398" />


**`mod-global-scaling`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** that dynamically scales all world zones, dungeons, raids, and world bosses across **Vanilla**, **The Burning Crusade**, and **Wrath of the Lich King** up to **Level 80 endgame difficulty**.



Instead of using flat, one-size-fits-all multipliers, this module uses **per-expansion exponential curves**, **late-expansion catch-up modifiers**, and **individual per-instance / per-difficulty configurations** so that every piece of content in the game remains relevant, challenging, and mechanically intact at Level 80. 

**For the customization of combat beyond level and statistics, see** https://github.com/mama128/mod-epic-bosses

---

## 🛡️ 100% Non-Intrusive & Safe Architecture

*⚠️ **Important Note:** This module scales creatures **dynamically in memory** (on-the-fly) as they spawn and enter combat. **It makes zero permanent changes to your server database (SQL)**. If you disable the module or change your configuration, all creatures instantly revert to their native Blizzard default values without requiring a database rollback.*

---

## 🎯 What to Expect (Design Philosophy & Baselines)

When installing this module with the default configuration (`mod_global_scaling.conf.dist`), players can expect the entire world of Azeroth, Outland, and Northrend to be calibrated around **Tier 10 (Patch 3.3.5a) endgame standards** :

* **Overworld Zones (Vanilla, TBC, WotLK):** All eligible creatures (excluding critters and those below the minimum native level floor) are scaled to Level 80+ (+1 for Elites/Rares, +2 for Rare Elites, +3 for Bosses), turning questing zones and outdoor elite areas into max-level endgame zones.
* **All 5-Man Dungeons (Vanilla, TBC Normal/Heroic, WotLK Normal/Heroic):** Calibrated toward **WotLK Tier 10 Heroic Dungeons** (*Forge of Souls*, *Pit of Saron*, and *Halls of Reflection* Heroic).
* **10-Man & 20-Man Raids (Zul'Gurub, AQ20, Karazhan, Zul'Aman, WotLK 10-Man):** Calibrated toward **Icecrown Citadel 10-Man Heroic / Ruby Sanctum 10-Man Heroic**.
* **25-Man & 40-Man Raids (MC, BWL, AQ40, TBC 25-Man, WotLK 25-Man):** Calibrated toward **Icecrown Citadel 25-Man Heroic / Ruby Sanctum 25-Man Heroic**.
* **World Bosses (Kazzak, Doomwalker, Azuregos, Dragons of Nightmare):** Individually overridden to Level 83 Skull Bosses tuned for full endgame raid groups.


### Balancing is still underway, however, it is possible that the value I have chosen does not align with the level of difficulty you wish to take on. I focus on ICC-Halion gear level. You're free to balance however you want with the .conf file. Too easy? Increase the numbers. Too hard? Decrease the numbers, create your own perfect version of the game !

---

## ✨ Key Features

### 1. Mathematical Exponential Scaling per Expansion
Creatures from earlier expansions have fundamentally different base stat budgets in the database compared to WotLK creatures. The module calculates a level ratio (`TargetLevel / NativeLevel`) and applies customizable **exponential curves (`std::pow`)** separately for **Vanilla**, **TBC**, and **WotLK** across 7 distinct stat categories:
* **Health (HP)** & **Mana**
* **Melee / Ranged / Off-Hand Weapon Damage** & **Attack Power Ability**
* **Direct Spell Damage**
* **Periodic Damage (DoTs)**
* **Healing Spells** (percentage-based heals like `SPELL_EFFECT_HEAL_PCT` are automatically excluded so bosses don't overheal)
* **Armor** (automatically capped at `10,643` unless the creature's native armor is already higher) & **Spell Resistances**
* **Gold**

### 2. Low-Level Floor & Late-Expansion Catch-Up System
* **Minimum Native Level Floor (`MinNativeLevel = 14`):** Prevents level 1–13 starter-zone creatures from exploding in stats due to extreme `80 / 1` level ratios by smoothing their baseline ratio.
* **Late-Expansion Catch-Up (`GlobalScaling.CatchUp`):** Smoothly boosts higher-level overworld mobs in Vanilla (levels 14–60) and TBC (levels 58–70) so endgame zones like Silithus, Eastern Plaguelands, or Netherstorm feel appropriately stronger than early leveling zones.

### 3. Built-in C++ Combat Safeguards (Anti-One-Shot & Anti-Double-Dipping)
* **No Double-Dipping on Weapon Spells:** Physical abilities based on weapon damage (`SPELL_EFFECT_WEAPON_DAMAGE`, `NORMALIZED_WEAPON_DMG`, `WEAPON_PERCENT_DAMAGE`) automatically scale via the creature's scaled Melee Damage and Attack Power and are not double-multiplied by the spell hook.
* **Player Pet & Guardian Protection:** Player-owned pets, summons, mind-controlled units, and guardians are strictly ignored so player damage is never artificially inflated.
* **Invisible Trigger Protection:** Traps, poison clouds, and invisible caster triggers (`Rank.Trigger`) scale their spell/DoT damage without having their level or visual model size altered.
* **Native Level Preservation:** Creatures whose native spawn level is already higher than the calculated target level will never be down-scaled, preserving their native `WeaponSkill` (`Level * 5`) against tanks.
* **Dynamic Combat & Respawn Tracking:** Periodic checks and combat hooks ensure that tank debuffs on pull (e.g., *Demoralizing Shout*, *Thunder Clap*), phase shifts, or creature respawns never revert a creature's melee damage or level.
* **Enemy Pet & Summon Compensation:** In AzerothCore, `Guardian::InitStatsForLevel` automatically scales summoned enemy pets (such as Murloc *Crawlers*) to their master's level (80) using a linear formula. The module detects level-scaled summons, bypasses redundant Catch-Up bonuses, and divides out the core's linear factor so enemy pets deal balanced melee and spell damage.
* **Reflected Spell Protection:** Automatically detects and ignores high-rank player spells reflected back by low-level creatures (e.g., *Obsidian Sentinel* or *Thorns*/*Fire Shield* effects) when `spellInfo->SpellLevel > nativeLevel + 8` or when `SPELL_AURA_REFLECT_SPELLS` is active.
* **Percentage-Based Damage Exclusion:** Spells dealing damage based on max health percentage (`SPELL_AURA_PERIODIC_DAMAGE_PERCENT` / `SPELL_EFFECT_HEALTH_LEECH`) are automatically excluded from spell multipliers.
* **Mana Burn & Power Drain Protection:** Direct and periodic mana burn/drain effects (`SPELL_EFFECT_POWER_BURN`, `SPELL_EFFECT_POWER_DRAIN`, `SPELL_AURA_PERIODIC_MANA_LEECH`) are excluded from spell damage multipliers unless manually overridden in `CustomDamageSpells`. This preserves their native tooltip behavior (e.g., `0.5` damage per point of mana drained) and prevents double-dipping against level 80 player mana pools.
* **Master & Difficulty Entry Support:** Automatically checks both `creature->GetEntry()` (Master Wowhead ID) and `cInfo->Entry` (`difficulty_entry_1/2/3`) for `CustomCreatures`, `ExcludedCreatures`, `ForceBossCreatures`, and `SizeOverride.Creatures`.
* **Corpse & Loot Protection:** Strict `IsAlive() && !isDead()` checks across all events and periodic timers prevent dead creatures from being reset to 1 HP and breaking skinning or loot tables.

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
  * **Automatic Friendly & Reputation NPC Protection (`SizeOverride.AffectFriendlyNPCs = 0`):** By default, friendly Alliance/Horde NPCs, Sanctuary/City citizens, Quest Givers, Vendors, Trainers, Guards, and Reputation-gated factions (e.g., *Timbermaw Hold*, *Sons of Hodir*, *The Oracles*, *Sporeggar*, *Aldor/Scryers*) keep their original `1.0x` visual size while still receiving full level 80 combat stat scaling. Hostile monsters—including enemies killed for reputation—are resized normally.
* 
### 6. Automatic Optional & Mini-Boss Promotion (`ForceBossCreatures`)
Many Vanilla optional bosses and mini-bosses were originally flagged by Blizzard as standard Elites (`rank = 1`) rather than Dungeon Bosses in `creature_template`.
* Using `GlobalScaling.ForceBossCreatures`, you can list their Entry IDs so the module automatically promotes them to **`BossNonSkull` (Level 83)** with full boss stat multipliers and **Boss visual scale** — without modifying your SQL database.
 
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

---

## Troubleshooting & FAQ

### Enemy-Summoned Pets & Minions (High Ability Damage)
* **The Issue:** A tiny handful of low-level NPCs summon true "pet" units (such as the Westfall Murloc's summoned *Crawler* pet) whose special abilities are hardcoded in the DBC to scale off **Attack Power (AP)** or **Spell Power (SP)**. Because non-player creatures in WoW do not natively build SP/AP gear stats and native AP is preserved to prevent poison/ability double-dipping their pet specific spells may hit for rather high values even though their health and white melee hits scale properly. *(Note: This only affects true pet-type summons, not standard boss adds).*
* **The Fix:** Boost the pet's `Spell` or `Damage` multiplier directly in `GlobalScaling.CustomCreatures` (or target its specific ability via `GlobalScaling.CustomDamageSpells`).

* **Console Warning: `Missing property GlobalScaling... in config file`**
  Make sure you copied any newly added configuration lines (`GlobalScaling.ForceBossCreatures` and `GlobalScaling.SizeOverride.AffectFriendlyNPCs`) into your active **`configs/modules/mod_global_scaling.conf`** file inside your build directory (not just `mod_global_scaling.conf.dist` in the source folder).
* **How do I check which multipliers a mob is using in-game?**
  Select the creature and type `.gscaling status`. It will display both its Master Entry (`creature->GetEntry()`) and Difficulty Entry (`cInfo->Entry`), its active environment (`Overworld`, `Dungeon`, `Raid`, or `[CUSTOM MOB]`), and its final calculated multipliers.
### Why do creatures on the Isle of Quel'Danas have lower health compared to the rest of TBC, and how do I fix it?
  Due to internal DBC/level scaling differences, outdoor mobs on Quel'Danas may scale slightly behind standard Outland zones. If you increase the global TBC exponent (`Exponent.TBC.HP`) to fix Quel'Danas, it will make the rest of TBC too difficult. Instead, target Quel'Danas mobs specifically by adding their Entry IDs to **`GlobalScaling.CustomCreatures`** with a slightly higher HP multiplier (e.g., `1.4`) to bring them up to ~11k HP without affecting the rest of the expansion:
  ```ini
  GlobalScaling.CustomCreatures = "QUELDANAS_MOB_ENTRY 80 1.4 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0"
 ```
### How do I reload changes made to `mod_global_scaling.conf` without restarting the server?**
  Run `.reload config` followed by `.gscaling update` in-game to immediately apply your new multipliers and visual sizes to all nearby creatures.

### Why do certain bosses (e.g., Noxxion, Lord Incendius, Pandemonius) deal massive melee auto-attack damage?

**Symptom:**
Specific bosses deal 2.5x to 3x more melee damage than other bosses in the same instance, even though they share the exact same map `Damage` multiplier.

**Cause:**
In AzerothCore (`creature_template.dmgschool != 0`), a few elemental bosses deal pure elemental damage (Nature, Fire, Frost, or Shadow) on their melee auto-attacks instead of physical damage (`SPELL_SCHOOL_NORMAL`).
* Standard physical bosses have their auto-attacks mitigated by **~65% to 70%** against a level 80 tank's armor, so instance `Damage` multipliers are calibrated higher to compensate for armor reduction.
* Elemental melee auto-attacks **completely bypass armor (0% physical mitigation)**. When they inherit the standard instance `Damage` multiplier, the tank takes the raw unmitigated value on every swing.

**Solution:**
To preserve their unique elemental mechanics (encouraging players to use Nature/Fire/Frost/Shadow Resistance buffs, totems, auras, or gear) without letting them one-shot tanks, assign them a dedicated `Dmg` multiplier in `GlobalScaling.CustomCreatures` inside `mod_global_scaling.conf` (typically **40% to 50%** of the instance's standard boss `Damage` multiplier):

```ini
# Format: "EntryID BaseLvl HP Mana Dmg Spell Armor Res Gold Heal DoT"
# Example for Noxxion (13282), Lord Incendius (9017), Pyroguard Emberseer (9816),
# Hydrospawn (13280), Baron Geddon (12056), Viscidus (15299), Pandemonius N/H (18341, 20604), and Hydross (21216):
GlobalScaling.CustomCreatures = "13282 83 19.0 7.0 12.0 5.5 1.0 1.0 2.0 5.5 3.0,9017 83 17.0 7.0 10.5 4.5 1.0 1.0 2.0 4.5 2.6,9816 83 16.0 7.0 9.5 4.0 1.0 1.0 2.0 4.0 2.5,13280 83 16.0 7.0 10.0 4.0 1.0 1.0 2.0 4.0 2.5,12056 83 85.0 5.5 7.5 2.5 1.0 1.0 2.5 2.5 1.8,15299 83 55.0 5.5 6.5 2.0 1.0 1.0 2.5 2.0 1.5,18341 83 12.0 7.0 2.8 3.0 1.0 1.0 2.0 3.0 2.0,20604 83 9.0 7.0 2.6 3.0 1.0 1.0 2.0 3.0 2.0,21216 83 16.0 1.5 2.2 2.0 1.0 1.0 2.5 2.0 1.5"
