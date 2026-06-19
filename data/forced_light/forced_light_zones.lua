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
        name = "Cemetery Main Floor",
        fromPos = Position(3821, 3216, 7), 
        toPos = Position(4268, 3697, 0),
        ambient = 100,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Cemetery Underground Floor",
        fromPos = Position(3821, 3216, 8), 
        toPos = Position(4268, 3697, 15),
        ambient = 70,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Hospital Main Floor",
        fromPos = Position(3945, 3085, 7), 
        toPos = Position(4040, 3184, 0),
        ambient = 80,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Hospital Inside Floor",
        fromPos = Position(3891, 2796, 7), 
        toPos = Position(4144, 3066, 0),
        ambient = 40,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Circus Outside Floor",
        fromPos = Position(4335, 2683, 7), 
        toPos = Position(4604, 2963, 0),
        ambient = 100,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Circus Inside Floor",
        fromPos = Position(4626, 2623, 7), 
        toPos = Position(5128, 3144, 0),
        ambient = 45,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Mansion Sewer Floor",
        fromPos = Position(5265, 3090, 7), 
        toPos = Position(5325, 3111, 7),
        ambient = 40,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Mansion Outside Floor",
        fromPos = Position(5235, 2870, 7), 
        toPos = Position(5382, 3089, 0),
        ambient = 100,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Mansion Underground Floor",
        fromPos = Position(5239, 2734, 8), 
        toPos = Position(5379, 2901, 8),
        ambient = 100,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Mansion Inside Floor",
        fromPos = Position(5191, 2637, 7), 
        toPos = Position(5365, 2800, 0),
        ambient = 60,
        ambientColor = 129,
        playersInside = {},
    },
    {
        name = "Nilfheim Outside",
        fromPos = Position(4328, 3629, 7),
        toPos = Position(4893, 3949, 0),
        ambient = 60,
        ambientColor = 215,
        playersInside = {},
    },
}

return DARKNESS_ZONES
