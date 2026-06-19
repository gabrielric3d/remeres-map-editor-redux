--[[
    Custom Item Lights
    Same format as client's custom_item_lights.lua -- copy between projects freely.

    Defines which items emit custom light (independent of .dat file).
    Each item ID maps to: color, intensity, and an animation pattern.

    Light Colors:
      215 = White/Cold       30  = Red
      195 = Blue/Cold        35  = Orange/Fire
      55  = Green/Toxic      210 = Warm Yellow
      180 = Purple           40  = Dark Red

    Intensity: 1-15 (higher = larger light radius)

    Pattern: sequence of {intensity, duration_ms} steps that loop forever.
      - Empty pattern = always on at base intensity (no flicker)
      - Each instance on the map gets a random time offset so they don't sync
      - Example: {{8, 1000}, {0, 2000}} = on 1s, off 2s, repeat
]]

CUSTOM_ITEM_LIGHTS = {
    -- ================================================================
    -- HOSPITAL LIGHTS (29712-29715) — color 129, intensity 6
    -- All four use the same color and base radius. They differ only in
    -- the flicker pattern, so a corridor of mixed IDs reads as the same
    -- fixture type with varying levels of decay.
    -- The `name` field is metadata only — not consumed by the engine yet,
    -- but documents intent and is available for future editor tooltips.
    -- ================================================================

    -- Mostly on. Every ~10s, a quick burst of flickers, then back to steady.
    [29712] = {
        name = "Hospital Lamp - Stable",
        color = 129, intensity = 8,
        pattern = {
            {8, 10000},  -- steady on for 10 seconds
            {0, 80},     -- quick flicker burst
            {8, 80},
            {0, 80},
            {8, 120},
            {0, 60},
            {8, 100},
        }
    },

    -- Short, frequent flickers — never fully calm.
    [29713] = {
        name = "Hospital Lamp - Nervous",
        color = 129, intensity = 8,
        pattern = {
            {8, 1200},
            {2, 80},     -- dim flicker
            {8, 800},
            {0, 60},     -- off flicker
            {8, 1500},
            {3, 100},
            {8, 600},
            {0, 80},
        }
    },

    -- Long outages, struggles back to life. Reads as "about to fail".
    [29714] = {
        name = "Hospital Lamp - Dying",
        color = 129, intensity = 8,
        pattern = {
            {8, 3000},   -- on 3s
            {0, 1500},   -- dies
            {8, 400},    -- flickers back briefly
            {0, 200},
            {8, 200},
            {0, 2500},   -- long death
            {8, 800},    -- recovers
        }
    },

    -- Calm pause, then a rapid strobe burst. Repeats.
    [29715] = {
        name = "Hospital Lamp - Strobing",
        color = 129, intensity = 8,
        pattern = {
            {8, 2500},   -- calm
            {0, 100},    -- strobe burst
            {8, 100},
            {0, 100},
            {8, 100},
            {0, 100},
            {8, 100},
            {0, 100},
            {8, 100},
        }
    },

    [29716] = {
        name = "Hospital Lamp - Big",
        color = 129, intensity = 15,
        pattern = {
            {15, 15000},  -- steady on for 10 seconds
            {0, 80},     -- quick flicker burst
            {15, 80},
            {0, 80},
        }
    },

}

return CUSTOM_ITEM_LIGHTS
