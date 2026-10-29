/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#include "GlobalScaling.hpp"

void Addmod_global_scalingScripts()
{
    new Global_Scaling_WorldScript();
    new Global_Scaling_CreatureScript();
    new Global_Scaling_PlayerScript();
    new global_scaling_commandscript();
    new Global_Scaling_UnitScript();
}