--[[
    Forced Light Zones

    Defines zones where light is forced.
    Same format as server's forced_light_zones.lua -- copy between projects freely.

    Two modes:
      1) Rectangular: fromPos + toPos
      2) Circular:    center + radius + floor
    ambient: 0-255 (base ambient light, lower = darker)
    ambientColor: ambient light color (8-bit color index)
]]

DARKNESS_ZONES = {
    {
        name = "Hospital Main Floor",
        fromPos = Position(1269, 931, 7),
        toPos = Position(1398, 1171, 7),
        ambient = 5,
        ambientColor = 129,
    },
    {
        name = "Circus Outside",
        fromPos = Position(189, 9, 7),
        toPos = Position(391, 252, 7),
        ambient = 20,
        ambientColor = 129,
    },
    {
        name = "Circus Inside",
        fromPos = Position(450, 8, 7),
        toPos = Position(939, 526, 7),
        ambient = 45,
        ambientColor = 129,
    },
    {
        name = "Test",
        fromPos = Position(776, 927, 7),
        toPos = Position(836, 971, 7),
        ambient = 25,
        ambientColor = 129,
    },
    -- Example with radius (circular zone):
    -- {
    --     name = "Dark Cave",
    --     center = Position(1000, 1000, 10),
    --     radius = 40,
    --     floor = 10,
    --     ambient = 3,
    --     ambientColor = 215,
    -- },
}

return DARKNESS_ZONES
