How to use the new options in your config file:

You can now define specific maps or creature entries to blacklist separating them with commas. For example:

GlobalScaling.ExcludedMaps = "533, 615, 631"  # e.g., Naxxramas, Ulduar, Icecrown Citadel map IDs
GlobalScaling.ExcludedCreatures = "12345, 67890" # Specific mob entry IDs you want to skip


CUSTOM OVERRIDES (SPECIFIC MAPS & CREATURES)
# Format: "Entry BaseLevel HP Mana Damage Spell Armor Res Gold,Entry2..."