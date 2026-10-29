/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#ifndef GLOBAL_SCALING_HPP
#define GLOBAL_SCALING_HPP

#include "ScriptMgr.h"
#include "Config.h"
#include "Creature.h"
#include "Player.h"
#include "Chat.h"

class SpellInfo;

struct StatMults
{
    float HP;
    float Mana;
    float Damage;
    float Spell;
    float Armor;
    float Resistance;
    float Gold;
    float DoT;  
    float Heal; 
};

struct StatExponents
{
    float HP;
    float Mana;
    float Damage;
    float Spell;
    float Armor;
    float Heal;
    float DoT;
};

struct RankData
{
    int32 LevelBonus;
    StatMults Stats;
};

struct CustomScaleData
{
    uint32 BaseLevel; 
    StatMults Stats;
};

class Global_Scaling_WorldScript : public WorldScript
{
public:
    Global_Scaling_WorldScript();
    void OnBeforeConfigLoad(bool reload) override;
    void OnAfterConfigLoad(bool reload) override;
};

class Global_Scaling_CreatureScript : public AllCreatureScript
{
public:
    Global_Scaling_CreatureScript();
    void OnCreatureAddWorld(Creature* creature) override;
    void OnCreatureRemoveWorld(Creature* creature) override;
};

class Global_Scaling_PlayerScript : public PlayerScript
{
public:
    Global_Scaling_PlayerScript();
    void OnPlayerMapChanged(Player* player) override;
};

class global_scaling_commandscript : public CommandScript
{
public:
    global_scaling_commandscript();
    Acore::ChatCommands::ChatCommandTable GetCommands() const override;

    static bool HandleScalingToggleCommand(ChatHandler* handler);
    static bool HandleScalingInfoCommand(ChatHandler* handler);
    static bool HandleScalingStatusCommand(ChatHandler* handler);
    static bool HandleScalingUpdateCommand(ChatHandler* handler);
};

class Global_Scaling_UnitScript : public UnitScript
{
public:
    Global_Scaling_UnitScript();
    void OnUnitEnterCombat(Unit* unit, Unit* victim) override;
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override;
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& addhealth, SpellInfo const* spellInfo) override;
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override;
};

#endif // GLOBAL_SCALING_HPP