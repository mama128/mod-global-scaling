/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#include "GlobalScaling.hpp"
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cmath>
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <utility>
#include <atomic>
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Map.h"
#include "Chat.h"
#include "SpellInfo.h"
#include "Log.h"

using namespace Acore::ChatCommands;

namespace
{
    class GlobalScalingBasicEvent : public BasicEvent
    {
    public:
        GlobalScalingBasicEvent(std::function<void()> callback, std::function<void()> abortCallback = nullptr) 
            : _callback(std::move(callback)), _abortCallback(std::move(abortCallback)) {}

        bool Execute(uint64, uint32) override
        {
            if (_callback)
                _callback();
            return true; 
        }

        void Abort(uint64) override
        {
            if (_abortCallback)
                _abortCallback();
        }

        bool IsDeletable() const override { return true; } 

    private:
        std::function<void()> _callback;
        std::function<void()> _abortCallback;
    };
}

namespace GlobalScaleConfig
{
    std::atomic<bool> isConfigInit(false);
    std::shared_mutex ConfigRWLock; 
    std::shared_mutex CacheMutex;

    bool Enable = true;
    bool AffectDungeons = true;
    bool AffectRaids = true;
    uint32 BaseLevel = 80;
    uint32 DungeonBaseLevel = 80;
    uint32 RaidBaseLevel = 80;
    uint32 InstanceScalingDelay = 1000;
    uint32 PeriodicCheckInterval = 1000;
    uint32 MinNativeLevel = 14;
    float UpdateDistance = 150.0f;

    bool CatchUpEnable = true;
    bool CatchUpAffectInstances = false;
    uint32 CatchUpVanillaMinLevel = 14;
    uint32 CatchUpVanillaMaxLevel = 60;
    float CatchUpVanillaMaxBonus = 1.8f;
    uint32 CatchUpTBCMinLevel = 58;
    uint32 CatchUpTBCMaxLevel = 70;
    float CatchUpTBCMaxBonus = 1.45f;

    float MultGeneral = 1.0f, MultOverworld = 1.0f;

    std::set<uint32> ExcludedMaps;
    std::set<uint32> ExcludedCreatures;
    std::set<uint32> ForceBossCreatures;
    
    std::map<uint32, CustomScaleData> CustomCreatures;
    std::map<uint32, float> CustomHealingSpells;
    std::map<uint32, float> CustomDamageSpells;
    std::map<uint32, float> CustomShieldSpells;
    std::map<uint32, float> CustomMeleeSpells; 
    
    struct MapSizeData {
        float TrashScale;
        float BossScale;
    };

    struct OverworldSizeData {
        float TrashScale;
        float EliteScale;
        float RareEliteScale;
        float RareScale;
        float SkullBossScale;
    };

    bool SizeOverrideMapsEnable = true;
    bool SizeOverrideOverworldEnable = true;

    std::map<uint32, MapSizeData> SizeOverrideMaps;
    std::map<uint32, OverworldSizeData> SizeOverrideOverworld;
    std::map<uint32, float> SizeOverrideCreatures;

    struct InstanceRankData {
        RankData Trash;
        RankData Commander;
        RankData BossNonSkull;
        RankData BossSkull;
        RankData Trigger;
    };

    std::unordered_map<uint64, InstanceRankData> CustomInstanceMaps;

    RankData OverworldRanks[6]; 
    StatMults ExpansionMult[3];
    StatExponents ExpansionExponents[3];
    
    struct CachedBaseStats
    {
        uint32 Entry;
        uint32 NativeLevel; 
        uint32 SpawnLevel; 
        float MinDmg;
        float MaxDmg;
        float MinOffHandDmg;
        float MaxOffHandDmg;
        float MinRangedDmg;
        float MaxRangedDmg;
        uint32 MeleeAP;
        uint32 RangedAP;
        uint32 OriginalMaxHealth;
        uint32 ScaledMaxHealth;
        uint32 OriginalMaxMana;
        uint32 OriginalArmor; 
        uint32 OriginalResistances[6];
        uint32 OriginalStats[2]; 
        float OriginalScale; 
        float ScaledMinDmg;
        float IntrinsicSpellScale;
        float IntrinsicDoTScale;
        float IntrinsicHealScale;
    };

    struct CreatureScaleContext
    {
        StatMults Mults;
        RankData Rank;
        StatExponents Exponents;
        uint32 Expansion;
        bool IsDungeon;
        bool IsRaid;
    };

    std::unordered_set<uint64> scaledCreatures;
    std::unordered_set<uint64> activePeriodicChecks;
    std::unordered_map<uint64, CachedBaseStats> creatureBaseStatsCache;

    void SchedulePeriodicCheck(Creature* creature); 
    bool ApplyScalingToCreature(Creature* creature, bool damageOnly = false);

    std::set<uint32> ParseUInt32Set(std::string const& str)
    {
        std::set<uint32> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            std::string subItem;
            while (ss2 >> subItem)
            {
                if (!subItem.empty())
                {
                    result.insert(static_cast<uint32>(std::stoul(subItem)));
                }
            }
        }
        return result;
    }

    std::map<uint32, CustomScaleData> ParseCustomData(std::string const& str)
    {
        std::map<uint32, CustomScaleData> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 id, baseLvl;
            float hp, mana, dmg, spell, armor, res, gold, heal, dot;
            
            if (ss2 >> id >> baseLvl >> hp >> mana >> dmg >> spell >> armor >> res >> gold >> heal >> dot)
            {
                result[id] = { baseLvl, {hp, mana, dmg, spell, armor, res, gold, dot, heal} };
            }
        }
        return result;
    }

    std::map<uint32, float> ParseSpellMults(std::string const& str)
    {
        std::map<uint32, float> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 spellId;
            float mult;
            if (ss2 >> spellId >> mult)
            {
                result[spellId] = mult;
            }
        }
        return result;
    }

    std::map<uint32, MapSizeData> ParseMapSizeData(std::string const& str)
    {
        std::map<uint32, MapSizeData> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 mapId;
            float trashMult, bossMult;
            if (ss2 >> mapId >> trashMult >> bossMult)
            {
                result[mapId] = { trashMult, bossMult };
            }
        }
        return result;
    }

    std::map<uint32, OverworldSizeData> ParseOverworldSizeData(std::string const& str)
    {
        std::map<uint32, OverworldSizeData> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 mapId;
            float trash, elite, rareElite, rare, skull;
            if (ss2 >> mapId >> trash >> elite >> rareElite >> rare >> skull)
            {
                result[mapId] = { trash, elite, rareElite, rare, skull };
            }
        }
        return result;
    }

    void Load(bool reload)
    {
        std::unique_lock<std::shared_mutex> lock(ConfigRWLock);
        
        if (!reload && isConfigInit.load())
            return;

        Enable = sConfigMgr->GetOption<bool>("GlobalScaling.Enable", true);
        AffectDungeons = sConfigMgr->GetOption<bool>("GlobalScaling.AffectDungeons", true);
        AffectRaids = sConfigMgr->GetOption<bool>("GlobalScaling.AffectRaids", true);
        BaseLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.BaseLevel", 80);
        DungeonBaseLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.Dungeon.BaseLevel", 80);
        RaidBaseLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.Raid.BaseLevel", 80);
        InstanceScalingDelay = sConfigMgr->GetOption<uint32>("GlobalScaling.Instance.ScalingDelay", 1000);
        PeriodicCheckInterval = sConfigMgr->GetOption<uint32>("GlobalScaling.PeriodicCheckInterval", 1000);
        MinNativeLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.MinNativeLevel", 14);
        UpdateDistance = sConfigMgr->GetOption<float>("GlobalScaling.UpdateDistance", 150.0f);

        CatchUpEnable = sConfigMgr->GetOption<bool>("GlobalScaling.CatchUp.Enable", true);
        CatchUpAffectInstances = sConfigMgr->GetOption<bool>("GlobalScaling.CatchUp.AffectInstances", false);
        
        CatchUpVanillaMinLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.CatchUp.Vanilla.MinLevel", 14);
        CatchUpVanillaMaxLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.CatchUp.Vanilla.MaxLevel", 60);
        CatchUpVanillaMaxBonus = sConfigMgr->GetOption<float>("GlobalScaling.CatchUp.Vanilla.MaxBonus", 1.8f);

        CatchUpTBCMinLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.CatchUp.TBC.MinLevel", 58);
        CatchUpTBCMaxLevel = sConfigMgr->GetOption<uint32>("GlobalScaling.CatchUp.TBC.MaxLevel", 70);
        CatchUpTBCMaxBonus = sConfigMgr->GetOption<float>("GlobalScaling.CatchUp.TBC.MaxBonus", 1.45f);

        SizeOverrideMapsEnable = sConfigMgr->GetOption<bool>("GlobalScaling.SizeOverride.Maps.Enable", true);
        SizeOverrideOverworldEnable = sConfigMgr->GetOption<bool>("GlobalScaling.SizeOverride.Overworld.Enable", true);

        ExpansionExponents[0] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.HP", 2.05f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.Mana", 1.70f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.Damage", 1.76f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.Spell", 1.60f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.Armor", 1.55f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.Heal", 1.60f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.Vanilla.DoT", 1.60f)
        };
        ExpansionExponents[1] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.HP", 1.65f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.Mana", 1.40f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.Damage", 1.55f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.Spell", 1.45f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.Armor", 1.35f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.Heal", 1.45f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.TBC.DoT", 1.45f)
        };
        ExpansionExponents[2] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.HP", 2.60f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.Mana", 1.80f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.Damage", 2.20f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.Spell", 2.20f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.Armor", 1.20f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.Heal", 2.20f),
            sConfigMgr->GetOption<float>("GlobalScaling.Exponent.WotLK.DoT", 2.20f)
        };

        ExcludedMaps = ParseUInt32Set(sConfigMgr->GetOption<std::string>("GlobalScaling.ExcludedMaps", ""));
        ExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>("GlobalScaling.ExcludedCreatures", ""));
        ForceBossCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>("GlobalScaling.ForceBossCreatures", "7023"));
        CustomCreatures = ParseCustomData(sConfigMgr->GetOption<std::string>("GlobalScaling.CustomCreatures", ""));
        CustomHealingSpells = ParseSpellMults(sConfigMgr->GetOption<std::string>("GlobalScaling.CustomHealingSpells", ""));
        CustomDamageSpells = ParseSpellMults(sConfigMgr->GetOption<std::string>("GlobalScaling.CustomDamageSpells", ""));
        CustomShieldSpells = ParseSpellMults(sConfigMgr->GetOption<std::string>("GlobalScaling.CustomShieldSpells", ""));
        CustomMeleeSpells = ParseSpellMults(sConfigMgr->GetOption<std::string>("GlobalScaling.CustomMeleeSpells", "")); 

        SizeOverrideMaps = ParseMapSizeData(sConfigMgr->GetOption<std::string>("GlobalScaling.SizeOverride.Maps", ""));
        SizeOverrideOverworld = ParseOverworldSizeData(sConfigMgr->GetOption<std::string>("GlobalScaling.SizeOverride.Overworld", ""));
        SizeOverrideCreatures = ParseSpellMults(sConfigMgr->GetOption<std::string>("GlobalScaling.SizeOverride.Creatures", ""));

        auto loadRank = [](std::string const& base, RankData& rank) {
            rank.LevelBonus = sConfigMgr->GetOption<int32>(base + ".LevelBonus", 0);
            rank.Stats.HP = sConfigMgr->GetOption<float>(base + ".HP", 1.0f);
            rank.Stats.Mana = sConfigMgr->GetOption<float>(base + ".Mana", 1.0f);
            rank.Stats.Damage = sConfigMgr->GetOption<float>(base + ".Damage", 1.0f);
            rank.Stats.Spell = sConfigMgr->GetOption<float>(base + ".Spell", 1.0f);
            rank.Stats.Armor = sConfigMgr->GetOption<float>(base + ".Armor", 1.0f);
            rank.Stats.Resistance = sConfigMgr->GetOption<float>(base + ".Resistance", 1.0f);
            rank.Stats.Gold = sConfigMgr->GetOption<float>(base + ".Gold", 1.0f);
            rank.Stats.Heal = sConfigMgr->GetOption<float>(base + ".Heal", 1.0f);
            rank.Stats.DoT = sConfigMgr->GetOption<float>(base + ".DoT", 1.0f);
        };

        CustomInstanceMaps.clear();
        
        const std::vector<std::pair<uint32, uint32>> activeInstances = {
            {389, 0}, {36, 0}, {43, 0}, {33, 0}, {34, 0}, {48, 0}, {47, 0}, {90, 0}, {189, 0}, {129, 0}, {70, 0}, {209, 0}, {349, 0}, {109, 0}, {230, 0}, {229, 0}, {429, 0}, {289, 0}, {329, 0},
            {309, 0}, {509, 0}, {409, 1}, {469, 1}, {531, 1},
            {543, 0}, {543, 1}, {542, 0}, {542, 1}, {540, 0}, {540, 1}, {547, 0}, {547, 1}, {546, 0}, {546, 1}, {545, 0}, {545, 1}, {557, 0}, {557, 1}, {558, 0}, {558, 1}, {556, 0}, {556, 1}, {555, 0}, {555, 1}, {554, 0}, {554, 1}, {553, 0}, {553, 1}, {552, 0}, {552, 1}, {560, 0}, {560, 1}, {269, 0}, {269, 1}, {585, 0}, {585, 1},
            {532, 0}, {568, 0}, {565, 1}, {544, 1}, {548, 1}, {550, 1}, {534, 1}, {564, 1}, {580, 1},
            {574, 0}, {574, 1}, {576, 0}, {576, 1}, {601, 0}, {601, 1}, {619, 0}, {619, 1}, {608, 0}, {608, 1}, {600, 0}, {600, 1}, {604, 0}, {604, 1}, {599, 0}, {599, 1}, {602, 0}, {602, 1}, {575, 0}, {575, 1}, {578, 0}, {578, 1}, {595, 0}, {595, 1}, {650, 0}, {650, 1}, {632, 0}, {632, 1}, {658, 0}, {658, 1}, {668, 0}, {668, 1},
            {533, 0}, {533, 1}, {590, 0}, {590, 1}, {615, 0}, {615, 1}, {624, 0}, {624, 1}, {603, 0}, {603, 1}, {249, 0}, {249, 1},
            {649, 0}, {649, 1}, {649, 2}, {649, 3}, {631, 0}, {631, 1}, {631, 2}, {631, 3}, {724, 0}, {724, 1}, {724, 2}, {724, 3}
        };

        for (const auto& pair : activeInstances) 
        {
            uint32 map = pair.first;
            uint32 diff = pair.second;

            InstanceRankData ir;
            std::string basePath = "GlobalScaling.Instance." + std::to_string(map) + "." + std::to_string(diff) + ".Rank";
            
            loadRank(basePath + ".Trash", ir.Trash);
            loadRank(basePath + ".Commander", ir.Commander);
            loadRank(basePath + ".BossNonSkull", ir.BossNonSkull);
            loadRank(basePath + ".BossSkull", ir.BossSkull);
            loadRank(basePath + ".Trigger", ir.Trigger);
            
            uint64 key = (static_cast<uint64>(map) << 32) | static_cast<uint64>(diff);
            CustomInstanceMaps[key] = ir;
        }

        MultGeneral = sConfigMgr->GetOption<float>("GlobalScaling.Mult.General", 1.0f);
        MultOverworld = sConfigMgr->GetOption<float>("GlobalScaling.Mult.Overworld", 1.0f);

        ExpansionMult[0] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.HP", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Mana", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Damage", 1.05f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Spell", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Armor", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Resistance", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Gold", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.DoT", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.Vanilla.Heal", 1.0f)
        };
        ExpansionMult[1] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.HP", 1.05f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Mana", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Damage", 1.15f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Spell", 1.15f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Armor", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Resistance", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Gold", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.DoT", 1.15f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.TBC.Heal", 1.15f)
        };
        ExpansionMult[2] = {
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.HP", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Mana", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Damage", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Spell", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Armor", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Resistance", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Gold", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.DoT", 1.0f),
            sConfigMgr->GetOption<float>("GlobalScaling.Expansion.WotLK.Heal", 1.0f)
        };

        loadRank("GlobalScaling.Rank.Normal", OverworldRanks[0]);
        loadRank("GlobalScaling.Rank.Elite", OverworldRanks[1]);
        loadRank("GlobalScaling.Rank.RareElite", OverworldRanks[2]);
        loadRank("GlobalScaling.Rank.Boss", OverworldRanks[3]);
        loadRank("GlobalScaling.Rank.Rare", OverworldRanks[4]);
        loadRank("GlobalScaling.Rank.Trigger", OverworldRanks[5]);

        if (reload)
        {
            std::unique_lock<std::shared_mutex> cacheLock(CacheMutex);
            scaledCreatures.clear();
        }

        isConfigInit.store(true);
    }

    void EnsureConfigLoaded()
    {
        if (!isConfigInit.load())
            Load(false);
    }

    bool IsPlayerOwnedOrControlled(Unit const* unit)
    {
        if (!unit) return false;
        if (unit->IsPlayer()) return true;

        if (Unit const* owner = unit->GetCharmerOrOwner())
        {
            if (owner->IsPlayer()) return true;
            if (owner != unit) return IsPlayerOwnedOrControlled(owner);
        }
        return false;
    }

    CreatureScaleContext GetCreatureScaleContext(Creature const* creature, CreatureTemplate const* cInfo)
    {
        CreatureScaleContext ctx;
        uint32 mapId = creature->GetMap()->GetId();
        uint32 difficulty = static_cast<uint32>(creature->GetMap()->GetSpawnMode());
        uint32 masterEntry = creature->GetEntry();
        uint32 templateEntry = cInfo->Entry;

        ctx.Expansion = cInfo->expansion > 2 ? 2 : cInfo->expansion;
        ctx.Exponents = ExpansionExponents[ctx.Expansion];

        bool isTrigger = creature->IsTrigger() || ((cInfo->flags_extra & 0x00000080) != 0);
        bool isForcedBoss = ForceBossCreatures.count(masterEntry) > 0 || ForceBossCreatures.count(templateEntry) > 0;
        bool isBoss = isForcedBoss || creature->IsDungeonBoss() || ((cInfo->type_flags & 0x00000004) != 0) || (cInfo->rank == 2);
        bool isSkullBoss = (cInfo->rank == 3) || creature->isWorldBoss();

        auto itCreature = CustomCreatures.find(masterEntry);
        if (itCreature == CustomCreatures.end() && templateEntry != masterEntry)
            itCreature = CustomCreatures.find(templateEntry);

        if (itCreature != CustomCreatures.end())
        {
            ctx.Mults.HP         = MultGeneral * itCreature->second.Stats.HP;
            ctx.Mults.Mana       = MultGeneral * itCreature->second.Stats.Mana;
            ctx.Mults.Damage     = MultGeneral * itCreature->second.Stats.Damage;
            ctx.Mults.Spell      = MultGeneral * itCreature->second.Stats.Spell;
            ctx.Mults.Armor      = MultGeneral * itCreature->second.Stats.Armor; 
            ctx.Mults.Resistance = MultGeneral * itCreature->second.Stats.Resistance; 
            ctx.Mults.Gold       = MultGeneral * itCreature->second.Stats.Gold;
            ctx.Mults.DoT        = MultGeneral * itCreature->second.Stats.DoT;
            ctx.Mults.Heal       = MultGeneral * itCreature->second.Stats.Heal;
            ctx.Rank = { 0, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };
            
            ctx.IsRaid = creature->GetMap()->IsRaid();
            ctx.IsDungeon = !ctx.IsRaid && creature->GetMap()->IsDungeon();
            return ctx;
        }

        uint64 key = (static_cast<uint64>(mapId) << 32) | static_cast<uint64>(difficulty);
        auto itMap = CustomInstanceMaps.find(key);
        
        if (itMap == CustomInstanceMaps.end()) {
            key = (static_cast<uint64>(mapId) << 32) | 0ULL; 
            itMap = CustomInstanceMaps.find(key);
        }

        if (itMap == CustomInstanceMaps.end()) {
            key = (static_cast<uint64>(mapId) << 32) | 1ULL; 
            itMap = CustomInstanceMaps.find(key);
        }

        if (itMap != CustomInstanceMaps.end())
        {
            ctx.IsRaid = creature->GetMap()->IsRaid();
            ctx.IsDungeon = !ctx.IsRaid;

            if (isTrigger) ctx.Rank = itMap->second.Trigger;
            else if (isSkullBoss) ctx.Rank = itMap->second.BossSkull;
            else if (isBoss) ctx.Rank = itMap->second.BossNonSkull;
            else if (cInfo->rank == 1) ctx.Rank = itMap->second.Commander;
            else ctx.Rank = itMap->second.Trash;
            
            ctx.Mults.HP         = MultGeneral * ctx.Rank.Stats.HP;
            ctx.Mults.Mana       = MultGeneral * ctx.Rank.Stats.Mana;
            ctx.Mults.Damage     = MultGeneral * ctx.Rank.Stats.Damage;
            ctx.Mults.Spell      = MultGeneral * ctx.Rank.Stats.Spell;
            ctx.Mults.Armor      = ctx.Rank.Stats.Armor; 
            ctx.Mults.Resistance = ctx.Rank.Stats.Resistance; 
            ctx.Mults.Gold       = MultGeneral * ctx.Rank.Stats.Gold;
            ctx.Mults.DoT        = MultGeneral * ctx.Rank.Stats.DoT;
            ctx.Mults.Heal       = MultGeneral * ctx.Rank.Stats.Heal;
            
            return ctx;
        }

        ctx.IsDungeon = false;
        ctx.IsRaid = false;

        if (isTrigger) ctx.Rank = OverworldRanks[5];
        else if (isSkullBoss) ctx.Rank = OverworldRanks[3];
        else if (isBoss) ctx.Rank = OverworldRanks[2];
        else if (cInfo->rank == 4) ctx.Rank = OverworldRanks[4]; 
        else if (cInfo->rank == 1) ctx.Rank = OverworldRanks[1];
        else ctx.Rank = OverworldRanks[0];

        ctx.Mults.HP         = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].HP         * ctx.Rank.Stats.HP;
        ctx.Mults.Mana       = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].Mana       * ctx.Rank.Stats.Mana;
        ctx.Mults.Damage     = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].Damage     * ctx.Rank.Stats.Damage;
        ctx.Mults.Spell      = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].Spell      * ctx.Rank.Stats.Spell;
        ctx.Mults.Armor      = ExpansionMult[ctx.Expansion].Armor * ctx.Rank.Stats.Armor; 
        ctx.Mults.Resistance = ExpansionMult[ctx.Expansion].Resistance * ctx.Rank.Stats.Resistance; 
        ctx.Mults.Gold       = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].Gold       * ctx.Rank.Stats.Gold;
        ctx.Mults.DoT        = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].DoT        * ctx.Rank.Stats.DoT;
        ctx.Mults.Heal       = MultGeneral * MultOverworld * ExpansionMult[ctx.Expansion].Heal       * ctx.Rank.Stats.Heal;

        return ctx;
    }

    bool ApplyScalingToCreature(Creature* creature, bool damageOnly)
    {
        if (!creature || !creature->IsInWorld() || !creature->IsAlive() || creature->isDead())
            return false;

        EnsureConfigLoaded();
        std::shared_lock<std::shared_mutex> configLock(ConfigRWLock);

        CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
        if (!cInfo || creature->IsCritter())
            return false;

        uint32 masterEntry = creature->GetEntry();
        if (ExcludedCreatures.count(masterEntry) || ExcludedCreatures.count(cInfo->Entry) || ExcludedMaps.count(creature->GetMap()->GetId()))
            return false;

        if (IsPlayerOwnedOrControlled(creature))
            return false;

        float initialHealthPct = creature->GetHealthPct();
        float initialManaPct = 0.0f;
        if (creature->GetMaxPower(POWER_MANA) > 0)
            initialManaPct = (static_cast<float>(creature->GetPower(POWER_MANA)) / creature->GetMaxPower(POWER_MANA)) * 100.0f;

        CreatureScaleContext ctx = GetCreatureScaleContext(creature, cInfo);
        
        bool isEligible = Enable;
        if (ctx.IsDungeon && !AffectDungeons) isEligible = false;
        if (ctx.IsRaid && !AffectRaids) isEligible = false;
        
        uint64 creatureGuid = creature->GetGUID().GetRawValue();

        if (!isEligible) 
        {
            bool needsRevert = false;
            CachedBaseStats baseStats;

            {
                std::shared_lock<std::shared_mutex> lock(CacheMutex);
                if (scaledCreatures.count(creatureGuid) > 0)
                {
                    needsRevert = true;
                    baseStats = creatureBaseStatsCache[creatureGuid];
                }
            }

            if (needsRevert && creature->IsAlive() && !creature->isDead())
            {
                creature->SetLevel(baseStats.SpawnLevel);
                creature->SetMaxHealth(baseStats.OriginalMaxHealth);
                creature->SetHealth(std::max<uint32>(1, static_cast<uint32>(baseStats.OriginalMaxHealth * (initialHealthPct / 100.0f))));
                
                creature->SetMaxPower(POWER_MANA, baseStats.OriginalMaxMana);
                if (baseStats.OriginalMaxMana > 0)
                    creature->SetPower(POWER_MANA, static_cast<uint32>(baseStats.OriginalMaxMana * (initialManaPct / 100.0f)));

                creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, baseStats.MinDmg);
                creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, baseStats.MaxDmg);
                creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, baseStats.MinOffHandDmg);
                creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, baseStats.MaxOffHandDmg);
                creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, baseStats.MinRangedDmg);
                creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, baseStats.MaxRangedDmg);

                creature->SetFloatValue(UNIT_FIELD_MINDAMAGE, baseStats.MinDmg);
                creature->SetFloatValue(UNIT_FIELD_MAXDAMAGE, baseStats.MaxDmg);
                creature->SetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, baseStats.MinOffHandDmg);
                creature->SetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, baseStats.MaxOffHandDmg);
                creature->SetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, baseStats.MinRangedDmg);
                creature->SetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, baseStats.MaxRangedDmg);
                
                creature->SetUInt32Value(UNIT_FIELD_ATTACK_POWER, baseStats.MeleeAP);
                creature->SetUInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, baseStats.RangedAP);
                creature->SetUInt32Value(UNIT_FIELD_RESISTANCES, baseStats.OriginalArmor);

                for (int i = 0; i < 6; ++i)
                    creature->SetUInt32Value(UNIT_FIELD_RESISTANCES + 1 + i, baseStats.OriginalResistances[i]);

                creature->SetObjectScale(baseStats.OriginalScale);

                std::unique_lock<std::shared_mutex> lock(CacheMutex);
                scaledCreatures.erase(creatureGuid);
            }
            return needsRevert;
        }

        uint32 baseLevel = ctx.IsRaid ? RaidBaseLevel : (ctx.IsDungeon ? DungeonBaseLevel : BaseLevel);
        auto itCustom = CustomCreatures.find(masterEntry);
        if (itCustom == CustomCreatures.end() && cInfo->Entry != masterEntry)
            itCustom = CustomCreatures.find(cInfo->Entry);

        if (itCustom != CustomCreatures.end() && itCustom->second.BaseLevel > 0)
        {
            baseLevel = itCustom->second.BaseLevel;
        }

        int32 targetLevel = std::clamp<int32>(static_cast<int32>(baseLevel) + ctx.Rank.LevelBonus, 1, 255);

        bool alreadyScaled = false;
        CachedBaseStats baseStats;

        {
            std::unique_lock<std::shared_mutex> lock(CacheMutex);
            auto it = creatureBaseStatsCache.find(creatureGuid);
            
            if (it != creatureBaseStatsCache.end() && it->second.Entry == cInfo->Entry)
            {
                alreadyScaled = (scaledCreatures.count(creatureGuid) > 0);
                if (!alreadyScaled)
                    scaledCreatures.insert(creatureGuid);

                baseStats = it->second;
            }
            else
            {
                alreadyScaled = false;
                baseStats.Entry = cInfo->Entry;
                
                uint32 tMinLvl = cInfo->minlevel > 0 ? cInfo->minlevel : 1;
                baseStats.NativeLevel = tMinLvl; 
                baseStats.SpawnLevel = creature->GetLevel(); 
                
                baseStats.MinDmg = creature->GetFloatValue(UNIT_FIELD_MINDAMAGE);
                baseStats.MaxDmg = creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE);
                baseStats.MinOffHandDmg = creature->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE);
                baseStats.MaxOffHandDmg = creature->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE);
                baseStats.MinRangedDmg = creature->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE);
                baseStats.MaxRangedDmg = creature->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE);
                baseStats.MeleeAP = creature->GetUInt32Value(UNIT_FIELD_ATTACK_POWER);
                baseStats.RangedAP = creature->GetUInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER);
                
                baseStats.OriginalMaxHealth = creature->GetMaxHealth();
                baseStats.ScaledMaxHealth = creature->GetMaxHealth();
                baseStats.OriginalMaxMana = creature->GetMaxPower(POWER_MANA);
                
                baseStats.ScaledMinDmg = baseStats.MinDmg;
                baseStats.OriginalArmor = creature->GetArmor(); 

                for (int i = 0; i < 6; ++i)
                    baseStats.OriginalResistances[i] = creature->GetUInt32Value(UNIT_FIELD_RESISTANCES + 1 + i);

                baseStats.OriginalStats[0] = creature->GetUInt32Value(UNIT_FIELD_STAT0);
                baseStats.OriginalStats[1] = creature->GetUInt32Value(UNIT_FIELD_STAT1);

                baseStats.OriginalScale = creature->GetFloatValue(OBJECT_FIELD_SCALE_X);

                baseStats.IntrinsicSpellScale = 1.0f;
                baseStats.IntrinsicDoTScale = 1.0f;
                baseStats.IntrinsicHealScale = 1.0f;
                
                creatureBaseStatsCache[creatureGuid] = baseStats;
                scaledCreatures.insert(creatureGuid); 
            }
        }

        if (static_cast<int32>(baseStats.SpawnLevel) > targetLevel)
        {
            targetLevel = static_cast<int32>(baseStats.SpawnLevel);
        }

        uint32 currentSpawnLevel = baseStats.SpawnLevel;

        bool isSummon = creature->IsSummon() || creature->IsPet() || creature->IsGuardian();
        bool isTrigger = creature->IsTrigger() || ((cInfo->flags_extra & 0x00000080) != 0);

        float hpNativeLevel = static_cast<float>(baseStats.NativeLevel);
        float dmgNativeLevel = static_cast<float>(baseStats.NativeLevel);
        float spellNativeLevel = static_cast<float>(baseStats.NativeLevel);

        if (isSummon)
        {
            uint32 hpBaseline = currentSpawnLevel > baseStats.NativeLevel ? currentSpawnLevel : baseStats.NativeLevel;
            hpNativeLevel = static_cast<float>(hpBaseline);
        }

        if (hpNativeLevel < MinNativeLevel)
        {
            float progress = hpNativeLevel / static_cast<float>(MinNativeLevel);
            hpNativeLevel = static_cast<float>(MinNativeLevel) * (0.3f + 0.7f * progress);
        }
        if (dmgNativeLevel < MinNativeLevel)
        {
            float progress = dmgNativeLevel / static_cast<float>(MinNativeLevel);
            dmgNativeLevel = static_cast<float>(MinNativeLevel) * (0.3f + 0.7f * progress);
        }
        if (spellNativeLevel < MinNativeLevel)
        {
            float progress = spellNativeLevel / static_cast<float>(MinNativeLevel);
            spellNativeLevel = static_cast<float>(MinNativeLevel) * (0.3f + 0.7f * progress);
        }

        float hpLevelRatio = std::max<float>(1.0f, static_cast<float>(targetLevel) / hpNativeLevel);
        float dmgLevelRatio = std::max<float>(1.0f, static_cast<float>(targetLevel) / dmgNativeLevel);
        float spellLevelRatio = std::max<float>(1.0f, static_cast<float>(targetLevel) / spellNativeLevel);

        float catchUpMod = 1.0f;
        if (CatchUpEnable && !isTrigger) 
        {
            bool applyCatchUp = true;
            if ((ctx.IsDungeon || ctx.IsRaid) && !CatchUpAffectInstances)
            {
                applyCatchUp = false;
            }

            if (applyCatchUp)
            {
                if (ctx.Expansion == 0) 
                {
                    if (baseStats.NativeLevel >= CatchUpVanillaMinLevel && baseStats.NativeLevel <= CatchUpVanillaMaxLevel)
                    {
                        float range = static_cast<float>(CatchUpVanillaMaxLevel - CatchUpVanillaMinLevel);
                        float catchUpProgress = static_cast<float>(baseStats.NativeLevel - CatchUpVanillaMinLevel) / range; 
                        catchUpProgress = std::clamp(catchUpProgress, 0.0f, 1.0f);
                        catchUpMod = 1.0f + (std::pow(catchUpProgress, 0.8f) * (CatchUpVanillaMaxBonus - 1.0f)); 
                    }
                    else if (baseStats.NativeLevel > CatchUpVanillaMaxLevel)
                    {
                        catchUpMod = CatchUpVanillaMaxBonus;
                    }
                }
                else if (ctx.Expansion == 1) 
                {
                    if (baseStats.NativeLevel >= CatchUpTBCMinLevel && baseStats.NativeLevel <= CatchUpTBCMaxLevel)
                    {
                        float range = static_cast<float>(CatchUpTBCMaxLevel - CatchUpTBCMinLevel);
                        float catchUpProgress = static_cast<float>(CatchUpTBCMaxLevel - baseStats.NativeLevel) / range; 
                        catchUpProgress = std::clamp(catchUpProgress, 0.0f, 1.0f);
                        catchUpMod = 1.0f + (catchUpProgress * (CatchUpTBCMaxBonus - 1.0f)); 
                    }
                    else if (baseStats.NativeLevel < CatchUpTBCMinLevel)
                    {
                        catchUpMod = 1.0f;
                    }
                }
            }
        }

        float hpCatchUpMod = isSummon && (currentSpawnLevel > baseStats.NativeLevel) ? 1.0f : catchUpMod;

        float intrinsicHpScale    = std::pow(hpLevelRatio, ctx.Exponents.HP) * hpCatchUpMod;
        float intrinsicManaScale  = std::pow(hpLevelRatio, ctx.Exponents.Mana) * hpCatchUpMod;
        float intrinsicDmgScale   = std::pow(dmgLevelRatio, ctx.Exponents.Damage) * catchUpMod;
        float intrinsicArmorScale = std::pow(hpLevelRatio, ctx.Exponents.Armor);

        if (isSummon && currentSpawnLevel > baseStats.NativeLevel)
        {
			float templateMinDmg = baseStats.MinDmg + (static_cast<float>(baseStats.MeleeAP) / 14.0f) * 2.0f;
            if (templateMinDmg > 0.0f && baseStats.MinDmg > templateMinDmg * 1.8f)
            {
                float coreLinearFactor = std::max<float>(1.0f, static_cast<float>(currentSpawnLevel) / std::max<float>(1.0f, static_cast<float>(baseStats.NativeLevel)));
                intrinsicDmgScale = std::max<float>(1.0f, intrinsicDmgScale / coreLinearFactor);
            }
        }
        
        float intrinsicSpellScale = std::pow(spellLevelRatio, ctx.Exponents.Spell) * catchUpMod;
        float intrinsicDoTScale   = std::pow(spellLevelRatio, ctx.Exponents.DoT) * catchUpMod;
        float intrinsicHealScale  = std::pow(spellLevelRatio, ctx.Exponents.Heal) * catchUpMod;

        if (!alreadyScaled || baseStats.IntrinsicSpellScale != intrinsicSpellScale)
        {
            std::unique_lock<std::shared_mutex> lock(CacheMutex);
            creatureBaseStatsCache[creatureGuid].IntrinsicSpellScale = intrinsicSpellScale;
            creatureBaseStatsCache[creatureGuid].IntrinsicDoTScale = intrinsicDoTScale;
            creatureBaseStatsCache[creatureGuid].IntrinsicHealScale = intrinsicHealScale;
            
            baseStats.IntrinsicSpellScale = intrinsicSpellScale;
            baseStats.IntrinsicDoTScale = intrinsicDoTScale;
            baseStats.IntrinsicHealScale = intrinsicHealScale;
        }

        if (!isTrigger)
            creature->SetLevel(static_cast<uint8>(targetLevel));

        if (!alreadyScaled || !damageOnly)
        {
            if (ctx.Mults.Armor != 1.0f || intrinsicArmorScale != 1.0f)
            {
                float calcArmor = static_cast<float>(baseStats.OriginalArmor) * intrinsicArmorScale * ctx.Mults.Armor;
                
                uint32 maxArmor = 10643;
                if (baseStats.OriginalArmor > maxArmor) 
                    maxArmor = baseStats.OriginalArmor; 

                uint32 newArmor = (calcArmor >= static_cast<float>(maxArmor)) ? maxArmor : static_cast<uint32>(calcArmor);
                creature->SetUInt32Value(UNIT_FIELD_RESISTANCES, newArmor);
            }

            if (ctx.Mults.Resistance > 0.0f)
            {
                for (int i = 0; i < 6; ++i)
                {
                    uint32 origRes = baseStats.OriginalResistances[i];
                    if (origRes > 0)
                    {
                        float calcRes = static_cast<float>(origRes) * ctx.Mults.Resistance;
                        creature->SetUInt32Value(UNIT_FIELD_RESISTANCES + 1 + i, static_cast<uint32>(calcRes));
                    }
                }
            }

            uint32 baseHealth = baseStats.OriginalMaxHealth;
            uint32 baseMana = baseStats.OriginalMaxMana;

            float calcHealth = static_cast<float>(baseHealth) * intrinsicHpScale * ctx.Mults.HP;
            uint32 newMaxHealth = (calcHealth >= 2100000000.0f) ? 2100000000 : static_cast<uint32>(calcHealth);
            
            if (newMaxHealth > 0 && creature->IsAlive() && !creature->isDead())
            {
                creature->SetMaxHealth(newMaxHealth);
                {
                    std::unique_lock<std::shared_mutex> lock(CacheMutex);
                    creatureBaseStatsCache[creatureGuid].ScaledMaxHealth = newMaxHealth;
                }

                creature->SetHealth(std::max<uint32>(1, static_cast<uint32>(newMaxHealth * (initialHealthPct / 100.0f))));
            }

            if (baseMana > 0)
            {
                float calcMana = static_cast<float>(baseMana) * intrinsicManaScale * ctx.Mults.Mana; 
                uint32 newMaxMana = (calcMana >= 2100000000.0f) ? 2100000000 : static_cast<uint32>(calcMana);
                if (newMaxMana > 0)
                {
                    creature->SetMaxPower(POWER_MANA, newMaxMana);
                    
                    if (!alreadyScaled && initialManaPct < 1.0f)
                        creature->SetPower(POWER_MANA, newMaxMana);
                    else if (alreadyScaled && initialManaPct <= 0.0f && !creature->IsInCombat())
                        creature->SetPower(POWER_MANA, newMaxMana); 
                    else
                        creature->SetPower(POWER_MANA, static_cast<uint32>(newMaxMana * (initialManaPct / 100.0f)));
                }
            }
        }

        if (ctx.Mults.Damage != 1.0f || intrinsicDmgScale != 1.0f)
        {
            float totalDmgMult = intrinsicDmgScale * ctx.Mults.Damage;

            float minDmg       = baseStats.MinDmg * totalDmgMult;
            float maxDmg       = baseStats.MaxDmg * totalDmgMult;
            float minOffDmg    = baseStats.MinOffHandDmg * totalDmgMult;
            float maxOffDmg    = baseStats.MaxOffHandDmg * totalDmgMult;
            float minRangedDmg = baseStats.MinRangedDmg * totalDmgMult;
            float maxRangedDmg = baseStats.MaxRangedDmg * totalDmgMult;

            {
                std::unique_lock<std::shared_mutex> lock(CacheMutex);
                creatureBaseStatsCache[creatureGuid].ScaledMinDmg = minDmg;
            }

            float baseAttSpeed   = static_cast<float>(creature->GetAttackTime(BASE_ATTACK)) / 1000.0f;
            float offAttSpeed    = static_cast<float>(creature->GetAttackTime(OFF_ATTACK)) / 1000.0f;
            float rangedAttSpeed = static_cast<float>(creature->GetAttackTime(RANGED_ATTACK)) / 1000.0f;

            float meleeAPBonus   = (static_cast<float>(baseStats.MeleeAP) / 14.0f) * baseAttSpeed;
            float offAPBonus     = (static_cast<float>(baseStats.MeleeAP) / 14.0f) * offAttSpeed * 0.5f;
            float rangedAPBonus  = (static_cast<float>(baseStats.RangedAP) / 14.0f) * rangedAttSpeed;

            creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, std::max(1.0f, minDmg - meleeAPBonus));
            creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, std::max(2.0f, maxDmg - meleeAPBonus));
            creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, std::max(1.0f, minOffDmg - offAPBonus));
            creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, std::max(2.0f, maxOffDmg - offAPBonus));
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, std::max(1.0f, minRangedDmg - rangedAPBonus));
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, std::max(2.0f, maxRangedDmg - rangedAPBonus));

            creature->SetUInt32Value(UNIT_FIELD_ATTACK_POWER, baseStats.MeleeAP);
            creature->SetUInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, baseStats.RangedAP);

            creature->SetFloatValue(UNIT_FIELD_MINDAMAGE, minDmg);
            creature->SetFloatValue(UNIT_FIELD_MAXDAMAGE, maxDmg);
            creature->SetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, minOffDmg);
            creature->SetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, maxOffDmg);
            creature->SetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, minRangedDmg);
            creature->SetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, maxRangedDmg);
        }

        if (!damageOnly && !isTrigger)
        {
            float scaleMult = 1.0f;
            auto itSizeCreature = SizeOverrideCreatures.find(masterEntry);
            if (itSizeCreature == SizeOverrideCreatures.end() && cInfo->Entry != masterEntry)
                itSizeCreature = SizeOverrideCreatures.find(cInfo->Entry);
            
            if (itSizeCreature != SizeOverrideCreatures.end())
            {
                scaleMult = itSizeCreature->second;
            }
            else
            {
                uint32 mapId = creature->GetMap()->GetId();
                if (creature->GetMap()->IsDungeon() || creature->GetMap()->IsRaid())
                {
                    if (SizeOverrideMapsEnable)
                    {
                        auto itSizeMap = SizeOverrideMaps.find(mapId);
                        if (itSizeMap != SizeOverrideMaps.end())
                        {
                            bool isForcedBossSize = ForceBossCreatures.count(masterEntry) > 0 || ForceBossCreatures.count(cInfo->Entry) > 0;
                            bool isAnyBoss = isForcedBossSize || creature->IsDungeonBoss() || ((cInfo->type_flags & 0x00000004) != 0) || (cInfo->rank == 2) || (cInfo->rank == 3) || creature->isWorldBoss();
                            scaleMult = isAnyBoss ? itSizeMap->second.BossScale : itSizeMap->second.TrashScale;
                        }
                    }
                }
                else
                {
                    if (SizeOverrideOverworldEnable)
                    {
                        auto itSizeOW = SizeOverrideOverworld.find(mapId);
                        if (itSizeOW != SizeOverrideOverworld.end())
                        {
                            if (creature->isWorldBoss() || cInfo->rank == 3)
                                scaleMult = itSizeOW->second.SkullBossScale;
                            else if (cInfo->rank == 4) // Rare
                                scaleMult = itSizeOW->second.RareScale;
                            else if (cInfo->rank == 2) // RareElite
                                scaleMult = itSizeOW->second.RareEliteScale;
                            else if (cInfo->rank == 1) // Elite
                                scaleMult = itSizeOW->second.EliteScale;
                            else
                                scaleMult = itSizeOW->second.TrashScale;
                        }
                    }
                }
            }

            float targetScale = baseStats.OriginalScale * scaleMult;
            if (std::abs(creature->GetFloatValue(OBJECT_FIELD_SCALE_X) - targetScale) > 0.001f)
            {
                creature->SetObjectScale(targetScale);
            }
        }

        return true;
    }
    
    void SchedulePeriodicCheck(Creature* creature)
    {
        if (!creature) return;
        uint64 guid = creature->GetGUID().GetRawValue();

        {
            std::shared_lock<std::shared_mutex> lock(CacheMutex);
            if (activePeriodicChecks.count(guid) > 0) return;
        }

        {
            std::unique_lock<std::shared_mutex> lock(CacheMutex);
            activePeriodicChecks.insert(guid);
        }

        creature->m_Events.AddEvent(new GlobalScalingBasicEvent(
            [creature, guid]()
            {
                {
                    std::unique_lock<std::shared_mutex> lock(CacheMutex);
                    activePeriodicChecks.erase(guid);
                }

                if (creature && creature->IsInWorld())
                {
                    if (!creature->IsAlive() || creature->isDead())
                    {
                        {
                            std::unique_lock<std::shared_mutex> lock(CacheMutex);
                            scaledCreatures.erase(guid);
                        }
                        SchedulePeriodicCheck(creature);
                        return;
                    }

                    bool isScaled = false;
                    bool entryChanged = false;
                    bool statsReverted = false;
                    
                    {
                        std::shared_lock<std::shared_mutex> lock(CacheMutex);
                        isScaled = (scaledCreatures.count(guid) > 0);

                        if (isScaled && creatureBaseStatsCache.find(guid) != creatureBaseStatsCache.end())
                        {
                            CachedBaseStats const& baseStats = creatureBaseStatsCache[guid];
                            
                            if (baseStats.Entry != creature->GetCreatureTemplate()->Entry)
                            {
                                entryChanged = true;
                            }
                            else if ((creature->GetMaxHealth() == baseStats.OriginalMaxHealth && baseStats.OriginalMaxHealth != baseStats.ScaledMaxHealth) ||
                                     (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) <= baseStats.MinDmg * 1.05f && baseStats.ScaledMinDmg > baseStats.MinDmg * 1.1f) ||
                                     (!creature->IsTrigger() && creature->GetLevel() == baseStats.SpawnLevel && creature->GetLevel() < BaseLevel))
                            {
                                statsReverted = true;
                            }
                        }
                    }

                    if (entryChanged)
                    {
                        std::unique_lock<std::shared_mutex> lock(CacheMutex);
                        scaledCreatures.erase(guid);
                        creatureBaseStatsCache.erase(guid);
                        isScaled = false;
                    }
                    else if (statsReverted)
                    {
                        std::unique_lock<std::shared_mutex> lock(CacheMutex);
                        scaledCreatures.erase(guid);
                        isScaled = false;
                    }

                    bool isEnabled;
                    {
                        std::shared_lock<std::shared_mutex> configLock(ConfigRWLock);
                        isEnabled = Enable;
                    }

                    if (isEnabled && !isScaled) {
                        ApplyScalingToCreature(creature, false);
                    } else if (!isEnabled && isScaled) {
                        ApplyScalingToCreature(creature, false); 
                    } else if (isEnabled && isScaled) {
                        if (creature->IsInCombat()) {
                            ApplyScalingToCreature(creature, true);
                        } else if (!IsPlayerOwnedOrControlled(creature) && creature->IsAlive() && !creature->isDead()) {
                            if (creature->GetHealth() < creature->GetMaxHealth())
                            {
                                if (creature->IsSummon() || creature->IsPet() || creature->IsGuardian() || !creature->GetCharmerOrOwnerGUID().IsEmpty())
                                {
                                    uint32 regenAmount = std::max<uint32>(1u, creature->GetMaxHealth() / 3);
                                    uint32 newHealth = creature->GetHealth() + regenAmount;
                                    if (newHealth > creature->GetMaxHealth()) newHealth = creature->GetMaxHealth();
                                    creature->SetHealth(newHealth);
                                }
                            }
                            if (creature->GetMaxPower(POWER_MANA) > 0 && creature->GetPower(POWER_MANA) < creature->GetMaxPower(POWER_MANA))
                            {
                                uint32 manaRegen = std::max<uint32>(1u, creature->GetMaxPower(POWER_MANA) / 3);
                                uint32 newMana = creature->GetPower(POWER_MANA) + manaRegen;
                                if (newMana > creature->GetMaxPower(POWER_MANA)) newMana = creature->GetMaxPower(POWER_MANA);
                                creature->SetPower(POWER_MANA, newMana);
                            }
                        }
                    }

                    SchedulePeriodicCheck(creature);
                }
            },
            [guid]() 
            { 
                std::unique_lock<std::shared_mutex> lock(CacheMutex);
                activePeriodicChecks.erase(guid); 
            }
        ), creature->m_Events.CalculateTime(PeriodicCheckInterval));
    }
}

Global_Scaling_WorldScript::Global_Scaling_WorldScript() : WorldScript("Global_Scaling_WorldScript") { }

void Global_Scaling_WorldScript::OnBeforeConfigLoad(bool /*reload*/) 
{ 
    GlobalScaleConfig::isConfigInit.store(false); 
}

void Global_Scaling_WorldScript::OnAfterConfigLoad(bool reload) 
{ 
    GlobalScaleConfig::Load(reload); 
}

Global_Scaling_CreatureScript::Global_Scaling_CreatureScript() : AllCreatureScript("Global_Scaling_CreatureScript") { }

void Global_Scaling_CreatureScript::OnCreatureAddWorld(Creature* creature)
{
    if (!creature) return;

    GlobalScaleConfig::EnsureConfigLoaded(); 

    uint64 guid = creature->GetGUID().GetRawValue();
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        if (GlobalScaleConfig::scaledCreatures.count(guid) == 0 && 
            GlobalScaleConfig::creatureBaseStatsCache.find(guid) == GlobalScaleConfig::creatureBaseStatsCache.end())
        {
            CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
            if (cInfo)
            {
                GlobalScaleConfig::CachedBaseStats baseStats;
                baseStats.Entry = cInfo->Entry;
                
                uint32 tMinLvl = cInfo->minlevel > 0 ? cInfo->minlevel : 1;
                baseStats.NativeLevel = tMinLvl; 
                baseStats.SpawnLevel = creature->GetLevel(); 
                
                baseStats.MinDmg = creature->GetFloatValue(UNIT_FIELD_MINDAMAGE);
                baseStats.MaxDmg = creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE);
                baseStats.MinOffHandDmg = creature->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE);
                baseStats.MaxOffHandDmg = creature->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE);
                baseStats.MinRangedDmg = creature->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE);
                baseStats.MaxRangedDmg = creature->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE);
                baseStats.MeleeAP = creature->GetUInt32Value(UNIT_FIELD_ATTACK_POWER);
                baseStats.RangedAP = creature->GetUInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER);
                
                baseStats.OriginalMaxHealth = creature->GetMaxHealth();
                baseStats.ScaledMaxHealth = creature->GetMaxHealth();
                baseStats.OriginalMaxMana = creature->GetMaxPower(POWER_MANA);
                
                baseStats.ScaledMinDmg = baseStats.MinDmg;
                baseStats.OriginalArmor = creature->GetArmor(); 

                for (int i = 0; i < 6; ++i)
                    baseStats.OriginalResistances[i] = creature->GetUInt32Value(UNIT_FIELD_RESISTANCES + 1 + i);

                baseStats.OriginalStats[0] = creature->GetUInt32Value(UNIT_FIELD_STAT0);
                baseStats.OriginalStats[1] = creature->GetUInt32Value(UNIT_FIELD_STAT1);

                baseStats.OriginalScale = creature->GetFloatValue(OBJECT_FIELD_SCALE_X);

                baseStats.IntrinsicSpellScale = 1.0f;
                baseStats.IntrinsicDoTScale = 1.0f;
                baseStats.IntrinsicHealScale = 1.0f;
                
                GlobalScaleConfig::creatureBaseStatsCache[guid] = baseStats;
            }
        }
    }

    bool isDungeon = creature->GetMap()->IsNonRaidDungeon();
    bool isRaid = creature->GetMap()->IsRaid();
    
    bool affectDungeons, affectRaids;
    uint32 instanceDelay;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        affectDungeons = GlobalScaleConfig::AffectDungeons;
        affectRaids = GlobalScaleConfig::AffectRaids;
        instanceDelay = GlobalScaleConfig::InstanceScalingDelay;
    }

    if (isDungeon && !affectDungeons) return;
    if (isRaid && !affectRaids) return;

    if (creature->IsInWorld() && creature->IsAlive() && !creature->isDead())
    {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false);
    }

    bool isSummon = creature->IsSummon() || creature->IsPet() || creature->IsGuardian() || creature->IsTrigger() || !creature->GetCharmerOrOwnerGUID().IsEmpty();

    uint32 delay = (isDungeon || isRaid) ? instanceDelay : 100;

    if (isSummon || creature->IsInCombat())
        delay = 250;

    creature->m_Events.AddEvent(new GlobalScalingBasicEvent(
        [creature]()
        {
            if (creature && creature->IsInWorld() && creature->IsAlive() && !creature->isDead())
            {
                GlobalScaleConfig::ApplyScalingToCreature(creature, false);
                GlobalScaleConfig::SchedulePeriodicCheck(creature);
            }
        }
    ), creature->m_Events.CalculateTime(delay));
}

void Global_Scaling_CreatureScript::OnCreatureRemoveWorld(Creature* creature)
{
    if (!creature) return;
    uint64 guid = creature->GetGUID().GetRawValue();
    std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
    GlobalScaleConfig::scaledCreatures.erase(guid);
    GlobalScaleConfig::activePeriodicChecks.erase(guid);
    GlobalScaleConfig::creatureBaseStatsCache.erase(guid); 
}

Global_Scaling_PlayerScript::Global_Scaling_PlayerScript() : PlayerScript("Global_Scaling_PlayerScript") { }

void Global_Scaling_PlayerScript::OnPlayerMapChanged(Player* player)
{
    if (!player) return;
    
    GlobalScaleConfig::EnsureConfigLoaded();
    bool enable, affectDungeons, affectRaids;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        enable = GlobalScaleConfig::Enable;
        affectDungeons = GlobalScaleConfig::AffectDungeons;
        affectRaids = GlobalScaleConfig::AffectRaids;
    }

    if (!enable) return;

    Map* map = player->GetMap();
    if (!map) return;

    bool isDungeon = map->IsNonRaidDungeon();
    bool isRaid = map->IsRaid();
    
    if (isDungeon && !affectDungeons) return;
    if (isRaid && !affectRaids) return;
    if (!isDungeon && !isRaid) return;

    player->m_Events.AddEvent(new GlobalScalingBasicEvent(
        [player]()
        {
            if (!player || !player->IsInWorld()) return;
            Map* curMap = player->GetMap();
            if (!curMap || (!curMap->IsNonRaidDungeon() && !curMap->IsRaid())) return;
            
            float distance = 5000.0f;
            std::list<Creature*> creatures;
            Acore::AnyUnitInObjectRangeCheck check(player, distance);
            Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(player, creatures, check);
            Cell::VisitObjects(player, searcher, distance);

            for (Creature* creature : creatures)
            {
                if (creature && creature->IsAlive() && !creature->isDead())
                    GlobalScaleConfig::ApplyScalingToCreature(creature, false);
            }
        }
    ), player->m_Events.CalculateTime(1500));
}

global_scaling_commandscript::global_scaling_commandscript() : CommandScript("global_scaling_commandscript") { }

ChatCommandTable global_scaling_commandscript::GetCommands() const
{
    static ChatCommandTable scalingCommandTable =
    {
        { "toggle", HandleScalingToggleCommand,    SEC_ADMINISTRATOR, Console::No },
        { "info",   HandleScalingInfoCommand,      SEC_PLAYER,        Console::No },
        { "status", HandleScalingStatusCommand,    SEC_PLAYER,        Console::No },
        { "update", HandleScalingUpdateCommand,    SEC_ADMINISTRATOR, Console::No }
    };
    
    static ChatCommandTable commandTable = { { "gscaling", scalingCommandTable } };
    return commandTable;
}

bool global_scaling_commandscript::HandleScalingToggleCommand(ChatHandler* handler)
{
    GlobalScaleConfig::EnsureConfigLoaded();
    bool newState;
    {
        std::unique_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        GlobalScaleConfig::Enable = !GlobalScaleConfig::Enable;
        newState = GlobalScaleConfig::Enable;
    }

    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (player)
    {
        Map* map = player->GetMap();
        if (map)
        {
            float dist = 150.0f;
            {
                std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
                dist = GlobalScaleConfig::UpdateDistance;
            }
            float distance = (map->IsNonRaidDungeon() || map->IsRaid()) ? 5000.0f : dist;
            std::list<Creature*> creatures;
            Acore::AnyUnitInObjectRangeCheck check(player, distance);
            Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(player, creatures, check);
            Cell::VisitObjects(player, searcher, distance);
            
            for (Creature* creature : creatures)
            {
                if (creature && creature->IsAlive() && !creature->isDead())
                    GlobalScaleConfig::ApplyScalingToCreature(creature, false);
            }
        }
    }

    handler->PSendSysMessage("Global Scaling is now: {}", newState ? "ENABLED" : "DISABLED");
    return true;
}

bool global_scaling_commandscript::HandleScalingInfoCommand(ChatHandler* handler)
{
    GlobalScaleConfig::EnsureConfigLoaded();
    
    bool enable, dungeons, raids;
    uint32 owLvl, dunLvl, raidLvl, minLvl, delay;

    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        enable = GlobalScaleConfig::Enable;
        dungeons = GlobalScaleConfig::AffectDungeons;
        raids = GlobalScaleConfig::AffectRaids;
        owLvl = GlobalScaleConfig::BaseLevel;
        dunLvl = GlobalScaleConfig::DungeonBaseLevel;
        raidLvl = GlobalScaleConfig::RaidBaseLevel;
        minLvl = GlobalScaleConfig::MinNativeLevel;
        delay = GlobalScaleConfig::InstanceScalingDelay;
    }

    handler->PSendSysMessage("--- Global Scaling Module Info ---");
    handler->PSendSysMessage("Status: {} | Dungeons: {} | Raids: {}", 
        enable ? "ENABLED" : "DISABLED", 
        dungeons ? "YES" : "NO", 
        raids ? "YES" : "NO");
    handler->PSendSysMessage("Base Levels -> OW: {} | Dungeon: {} | Raid: {}", owLvl, dunLvl, raidLvl);
    handler->PSendSysMessage("Min Native Level Floor: {} | Instance Scaling Delay: {} ms", minLvl, delay);
    return true;
}

bool global_scaling_commandscript::HandleScalingStatusCommand(ChatHandler* handler)
{
    Creature* creature = handler->getSelectedCreature();
    if (!creature) 
    {
        handler->PSendSysMessage("You must select a creature.");
        return false;
    }

    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if (!cInfo) return false;

    GlobalScaleConfig::EnsureConfigLoaded();

    uint32 masterEntry = creature->GetEntry();
    GlobalScaleConfig::CreatureScaleContext ctx;
    bool isCustom = false;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        ctx = GlobalScaleConfig::GetCreatureScaleContext(creature, cInfo);
        isCustom = (GlobalScaleConfig::CustomCreatures.find(masterEntry) != GlobalScaleConfig::CustomCreatures.end()) ||
                   (GlobalScaleConfig::CustomCreatures.find(cInfo->Entry) != GlobalScaleConfig::CustomCreatures.end());
    }

    handler->PSendSysMessage("--- Target Scaling Status ---");
    handler->PSendSysMessage("Name: {} (Entry: {} | DiffEntry: {})", creature->GetName(), masterEntry, cInfo->Entry);
    
    std::string env = ctx.IsRaid ? "Raid" : (ctx.IsDungeon ? "Dungeon" : "Overworld");
    if (isCustom) env += " [CUSTOM MOB]";

    handler->PSendSysMessage("Environment: {} | Level: {} (Bonus: {}) | Exp: {}", env, creature->GetLevel(), ctx.Rank.LevelBonus, ctx.Expansion);
    handler->PSendSysMessage("Effective Rank Mults -> HP: {:.2f} | Melee: {:.2f} | Spell: {:.2f} | Armor: {:.2f}", ctx.Rank.Stats.HP, ctx.Rank.Stats.Damage, ctx.Rank.Stats.Spell, ctx.Rank.Stats.Armor);
    handler->PSendSysMessage("Total Mults -> HP: {:.2f} | Melee: {:.2f} | Spell: {:.2f} | Armor: {:.2f}", ctx.Mults.HP, ctx.Mults.Damage, ctx.Mults.Spell, ctx.Mults.Armor);
    handler->PSendSysMessage("Final Stats -> Health: {} | Armor: {} | AP: {}", creature->GetMaxHealth(), (uint32)creature->GetArmor(), creature->GetUInt32Value(UNIT_FIELD_ATTACK_POWER));
    return true;
}

bool global_scaling_commandscript::HandleScalingUpdateCommand(ChatHandler* handler)
{
    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!player)
    {
        handler->PSendSysMessage("Update command requires to be run in-game by a player.");
        return true;
    }

    Map* map = player->GetMap();
    if (!map) return false;

    uint32 updatedCount = 0;
    float dist = 150.0f;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        dist = GlobalScaleConfig::UpdateDistance;
    }

    float distance = (map->IsNonRaidDungeon() || map->IsRaid()) ? 5000.0f : dist;
    std::list<Creature*> creatures;
    
    Acore::AnyUnitInObjectRangeCheck check(player, distance);
    Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(player, creatures, check);
    Cell::VisitObjects(player, searcher, distance);
    
    for (Creature* creature : creatures)
    {
        if (creature && creature->IsAlive() && !creature->isDead() && GlobalScaleConfig::ApplyScalingToCreature(creature, false))
            updatedCount++;
    }
    
    handler->PSendSysMessage("Instantly updated {} creatures.", updatedCount);
    return true;
}

Global_Scaling_UnitScript::Global_Scaling_UnitScript() : UnitScript("Global_Scaling_UnitScript") { }

void Global_Scaling_UnitScript::OnUnitEnterCombat(Unit* unit, Unit*)
{
    if (!unit) return;
    Creature* creature = unit->ToCreature();
    if (!creature || !creature->IsAlive() || creature->isDead()) return;

    GlobalScaleConfig::EnsureConfigLoaded();

    uint64 guid = creature->GetGUID().GetRawValue();
    bool isScaled = false;
    bool entryChanged = false;
    bool statsReverted = false;
    
    uint32 baseLvl = 80;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        baseLvl = GlobalScaleConfig::BaseLevel;
    }

    {
        std::shared_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        isScaled = (GlobalScaleConfig::scaledCreatures.count(guid) > 0);

        if (isScaled && GlobalScaleConfig::creatureBaseStatsCache.find(guid) != GlobalScaleConfig::creatureBaseStatsCache.end())
        {
            GlobalScaleConfig::CachedBaseStats const& baseStats = GlobalScaleConfig::creatureBaseStatsCache[guid];
            
            if (baseStats.Entry != creature->GetCreatureTemplate()->Entry)
            {
                entryChanged = true;
            }
            else if ((creature->GetMaxHealth() == baseStats.OriginalMaxHealth && baseStats.OriginalMaxHealth != baseStats.ScaledMaxHealth) ||
                     (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) <= baseStats.MinDmg * 1.05f && baseStats.ScaledMinDmg > baseStats.MinDmg * 1.1f) ||
                     (!creature->IsTrigger() && creature->GetLevel() == baseStats.SpawnLevel && creature->GetLevel() < baseLvl))
            {
                statsReverted = true;
            }
        }
    }

    if (entryChanged)
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        GlobalScaleConfig::scaledCreatures.erase(guid);
        GlobalScaleConfig::creatureBaseStatsCache.erase(guid);
        isScaled = false;
    }
    else if (statsReverted)
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        GlobalScaleConfig::scaledCreatures.erase(guid);
        isScaled = false;
    }

    bool isEnabled;
    {
        std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);
        isEnabled = GlobalScaleConfig::Enable;
    }

    if (isEnabled && !isScaled) {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false);
    } else if (!isEnabled && isScaled) {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false); 
    } else if (isEnabled && isScaled) {
        GlobalScaleConfig::ApplyScalingToCreature(creature, true);
    }

    GlobalScaleConfig::SchedulePeriodicCheck(creature);
}

void Global_Scaling_UnitScript::ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo)
{
    if (!attacker || !target || attacker == target || damage <= 0) return;
    
    Creature* creature = attacker->ToCreature();
    if (!creature)
    {
        if (Unit* owner = attacker->GetCharmerOrOwner())
            creature = owner->ToCreature();
    }
    
    if (!creature || creature->IsCritter()) return;

    GlobalScaleConfig::EnsureConfigLoaded();

    uint64 guid = creature->GetGUID().GetRawValue();
    bool isScaled = false;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        isScaled = GlobalScaleConfig::scaledCreatures.count(guid) > 0;
    }
    if (!isScaled && creature->IsAlive() && !creature->isDead())
    {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false);
    }

    std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);

    if (!GlobalScaleConfig::Enable || GlobalScaleConfig::IsPlayerOwnedOrControlled(creature)) return;

    float intrinsicSpellScale = 1.0f;
    uint32 nativeLevel = 1;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        auto itCache = GlobalScaleConfig::creatureBaseStatsCache.find(guid);
        if (itCache != GlobalScaleConfig::creatureBaseStatsCache.end())
        {
            intrinsicSpellScale = itCache->second.IntrinsicSpellScale;
            nativeLevel = itCache->second.NativeLevel;
        }
    }

    bool hasCustomOverride = false;
    float customSpellMult = 1.0f;
    if (spellInfo)
    {
        auto itDmg = GlobalScaleConfig::CustomDamageSpells.find(spellInfo->Id);
        if (itDmg != GlobalScaleConfig::CustomDamageSpells.end())
        {
            customSpellMult = itDmg->second;
            hasCustomOverride = true;
        }
        else
        {
            auto itShield = GlobalScaleConfig::CustomShieldSpells.find(spellInfo->Id);
            if (itShield != GlobalScaleConfig::CustomShieldSpells.end())
            {
                customSpellMult = itShield->second;
                hasCustomOverride = true;
            }
            else
            {
                auto itMelee = GlobalScaleConfig::CustomMeleeSpells.find(spellInfo->Id);
                if (itMelee != GlobalScaleConfig::CustomMeleeSpells.end())
                {
                    customSpellMult = itMelee->second;
                    hasCustomOverride = true; 
                }
            }
        }
    }

    bool isWeaponBasedSpell = false;
    int32 nativeSpellDmg = 0;

    if (spellInfo)
    {
        if (!hasCustomOverride)
        {
            if (nativeLevel < 72 && spellInfo->SpellLevel > nativeLevel + 8)
                return;

            if (spellInfo->SpellFamilyName != SPELLFAMILY_GENERIC &&
                (creature->HasAuraType(SPELL_AURA_REFLECT_SPELLS) || creature->HasAuraType(SPELL_AURA_REFLECT_SPELLS_SCHOOL)))
                return;
        }

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_HEALTH_LEECH ||
                spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
            {
                return;
            }

            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)
            {
                isWeaponBasedSpell = true;
                break;
            }

            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SCHOOL_DAMAGE ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_HEALTH_LEECH)
            {
                int32 bp = spellInfo->Effects[i].BasePoints + 1;
                int32 maxBp = spellInfo->Effects[i].BasePoints + static_cast<int32>(spellInfo->Effects[i].DieSides);
                int32 effDmg = std::max(bp, maxBp);

                if (spellInfo->Effects[i].RealPointsPerLevel > 0.0f)
                {
                    int32 calcLvl = static_cast<int32>(nativeLevel);
                    if (spellInfo->MaxLevel > 0 && calcLvl > static_cast<int32>(spellInfo->MaxLevel))
                        calcLvl = static_cast<int32>(spellInfo->MaxLevel);
                    else if (calcLvl < static_cast<int32>(spellInfo->BaseLevel))
                        calcLvl = static_cast<int32>(spellInfo->BaseLevel);
                    calcLvl -= static_cast<int32>(spellInfo->SpellLevel);
                    if (calcLvl > 0)
                        effDmg += static_cast<int32>(calcLvl * spellInfo->Effects[i].RealPointsPerLevel);
                }

                if (effDmg > 0)
                    nativeSpellDmg += effDmg;
            }
        }
    }

    GlobalScaleConfig::CreatureScaleContext ctx = GlobalScaleConfig::GetCreatureScaleContext(creature, creature->GetCreatureTemplate());
    float dynamicScale = isWeaponBasedSpell ? 1.0f : (intrinsicSpellScale > 1.0f ? intrinsicSpellScale : ctx.Mults.Damage);
    float rankMult = ctx.Mults.Spell;
    float totalSpellMult = hasCustomOverride ? customSpellMult : (dynamicScale * rankMult);

    if ((!isWeaponBasedSpell || hasCustomOverride) && totalSpellMult != 1.0f)
    {
        if (!hasCustomOverride && nativeSpellDmg > 0 && damage > nativeSpellDmg * 2)
            damage = static_cast<int32>(nativeSpellDmg * totalSpellMult);
        else
            damage = static_cast<int32>(damage * totalSpellMult);
    }
}

void Global_Scaling_UnitScript::ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo)
{
    if (!attacker || !target || attacker == target || damage <= 0) return;
    
    Creature* creature = attacker->ToCreature();
    if (!creature)
    {
        if (Unit* owner = attacker->GetCharmerOrOwner())
            creature = owner->ToCreature();
    }
    
    if (!creature || creature->IsCritter()) return;

    GlobalScaleConfig::EnsureConfigLoaded();

    uint64 guid = creature->GetGUID().GetRawValue();
    bool isScaled = false;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        isScaled = GlobalScaleConfig::scaledCreatures.count(guid) > 0;
    }
    if (!isScaled && creature->IsAlive() && !creature->isDead())
    {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false);
    }

    std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);

    if (!GlobalScaleConfig::Enable || GlobalScaleConfig::IsPlayerOwnedOrControlled(creature)) return;

    float intrinsicDoTScale = 1.0f;
    uint32 nativeLevel = 1;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        auto itCache = GlobalScaleConfig::creatureBaseStatsCache.find(guid);
        if (itCache != GlobalScaleConfig::creatureBaseStatsCache.end())
        {
            intrinsicDoTScale = itCache->second.IntrinsicDoTScale;
            nativeLevel = itCache->second.NativeLevel;
        }
    }

    bool hasCustomOverride = false;
    float customSpellMult = 1.0f;
    if (spellInfo)
    {
        auto itDmg = GlobalScaleConfig::CustomDamageSpells.find(spellInfo->Id);
        if (itDmg != GlobalScaleConfig::CustomDamageSpells.end())
        {
            customSpellMult = itDmg->second;
            hasCustomOverride = true;
        }
        else
        {
            auto itMelee = GlobalScaleConfig::CustomMeleeSpells.find(spellInfo->Id);
            if (itMelee != GlobalScaleConfig::CustomMeleeSpells.end())
            {
                customSpellMult = itMelee->second;
                hasCustomOverride = true; 
            }
        }
    }

    bool isWeaponBasedSpell = false;
    uint32 nativeTickDmg = 0;
    
    if (spellInfo)
    {
        if (!hasCustomOverride && nativeLevel < 72 && spellInfo->SpellLevel > nativeLevel + 8)
            return;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
            {
                return;
            }

            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)
            {
                isWeaponBasedSpell = true;
                break;
            }

            if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_LEECH)
            {
                int32 bp = spellInfo->Effects[i].BasePoints + 1;
                int32 maxBp = spellInfo->Effects[i].BasePoints + static_cast<int32>(spellInfo->Effects[i].DieSides);
                int32 effDmg = std::max(bp, maxBp);

                if (spellInfo->Effects[i].RealPointsPerLevel > 0.0f)
                {
                    int32 calcLvl = static_cast<int32>(nativeLevel);
                    if (spellInfo->MaxLevel > 0 && calcLvl > static_cast<int32>(spellInfo->MaxLevel))
                        calcLvl = static_cast<int32>(spellInfo->MaxLevel);
                    else if (calcLvl < static_cast<int32>(spellInfo->BaseLevel))
                        calcLvl = static_cast<int32>(spellInfo->BaseLevel);
                    calcLvl -= static_cast<int32>(spellInfo->SpellLevel);
                    if (calcLvl > 0)
                        effDmg += static_cast<int32>(calcLvl * spellInfo->Effects[i].RealPointsPerLevel);
                }

                if (effDmg > 0)
                    nativeTickDmg += static_cast<uint32>(effDmg);
            }
        }
    }

    GlobalScaleConfig::CreatureScaleContext ctx = GlobalScaleConfig::GetCreatureScaleContext(creature, creature->GetCreatureTemplate());
    float dynamicScale = isWeaponBasedSpell ? 1.0f : (intrinsicDoTScale > 1.0f ? intrinsicDoTScale : ctx.Mults.Damage);
    float rankMult = ctx.Mults.DoT;
    float totalSpellMult = hasCustomOverride ? customSpellMult : (dynamicScale * rankMult);

    if ((!isWeaponBasedSpell || hasCustomOverride) && totalSpellMult != 1.0f)
    {
        if (!hasCustomOverride && nativeTickDmg > 0 && damage > nativeTickDmg * 2)
            damage = static_cast<uint32>(nativeTickDmg * totalSpellMult);
        else
            damage = static_cast<uint32>(damage * totalSpellMult);
    }
}

void Global_Scaling_UnitScript::ModifyHealReceived(Unit* target, Unit* healer, uint32& addhealth, SpellInfo const* spellInfo)
{
    if (!healer || !target) return;
    if (spellInfo && spellInfo->HasEffect(SPELL_EFFECT_HEAL_PCT)) return;
    
    Creature* creature = healer->ToCreature();
    if (!creature)
    {
        if (Unit* owner = healer->GetCharmerOrOwner())
            creature = owner->ToCreature();
    }
    
    if (!creature || creature->IsCritter()) return;

    GlobalScaleConfig::EnsureConfigLoaded();

    uint64 guid = creature->GetGUID().GetRawValue();
    bool isScaled = false;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        isScaled = GlobalScaleConfig::scaledCreatures.count(guid) > 0;
    }
    if (!isScaled && creature->IsAlive() && !creature->isDead())
    {
        GlobalScaleConfig::ApplyScalingToCreature(creature, false);
    }

    std::shared_lock<std::shared_mutex> configLock(GlobalScaleConfig::ConfigRWLock);

    if (!GlobalScaleConfig::Enable || GlobalScaleConfig::IsPlayerOwnedOrControlled(creature)) return;

    float intrinsicHealScale = 1.0f;
    {
        std::unique_lock<std::shared_mutex> lock(GlobalScaleConfig::CacheMutex);
        auto itCache = GlobalScaleConfig::creatureBaseStatsCache.find(guid);
        if (itCache != GlobalScaleConfig::creatureBaseStatsCache.end())
            intrinsicHealScale = itCache->second.IntrinsicHealScale;
    }

    GlobalScaleConfig::CreatureScaleContext ctx = GlobalScaleConfig::GetCreatureScaleContext(creature, creature->GetCreatureTemplate());
    
    float dynamicScale = intrinsicHealScale > 1.0f ? intrinsicHealScale : ctx.Mults.Damage;

    float totalHealMult = dynamicScale * ctx.Mults.Heal;

    if (spellInfo)
    {
        auto it = GlobalScaleConfig::CustomHealingSpells.find(spellInfo->Id);
        if (it != GlobalScaleConfig::CustomHealingSpells.end())
            totalHealMult = it->second;
    }

    if (totalHealMult != 1.0f)
        addhealth = static_cast<uint32>(addhealth * totalHealMult);
}
