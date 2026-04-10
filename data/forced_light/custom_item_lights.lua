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
    -- Item IDs: 29712-29723
    -- ================================================================
    -- WHITE LIGHTS (29712-29714)
    -- ================================================================

    -- Strong white, horror flicker: off 2s -> on 1s -> blink 3x -> repeat
    [29712] = {
        color = 129, intensity = 6,
        pattern = {
            {0, 2000},
            {8, 1000},
            {0, 150},
            {8, 150},
            {0, 150},
            {8, 150},
            {0, 150},
            {8, 150},
        }
    },

    -- Strong white, subtle flicker (hospital corridor)
    [29713] = {
        color = 129, intensity = 6,
        pattern = {
            {8, 3000},
            {3, 100},
            {8, 2000},
            {5, 80},
        }
    },

    -- Weak white, moderate flicker (hospital room)
    [29714] = {
        color = 129, intensity = 4,
        pattern = {
            {4, 2000},
            {1, 200},
            {4, 1500},
            {0, 300},
            {4, 1000},
            {2, 150},
        }
    },

    -- ================================================================
    -- RED LIGHTS (29715-29716)
    -- ================================================================

    -- Red emergency, aggressive alarm pattern
    [29715] = {
        color = 30, intensity = 6,
        pattern = {
            {6, 300},
            {0, 300},
            {6, 300},
            {0, 300},
            {6, 300},
            {0, 1500},
        }
    },

    -- Red, slow pulse
    [29716] = {
        color = 30, intensity = 5,
        pattern = {
            {5, 2000},
            {2, 500},
            {5, 1500},
            {3, 300},
        }
    },

    -- ================================================================
    -- BLUE LIGHTS (29717)
    -- ================================================================

    -- Blue cold, subtle flicker (morgue, laboratory)
    [29717] = {
        color = 195, intensity = 5,
        pattern = {
            {5, 4000},
            {2, 200},
            {5, 3000},
            {3, 150},
        }
    },

    -- ================================================================
    -- ORANGE/FIRE LIGHTS (29718-29719)
    -- ================================================================

    -- Orange fire, natural torch flicker
    [29718] = {
        color = 35, intensity = 6,
        pattern = {
            {6, 400},
            {4, 200},
            {6, 300},
            {5, 250},
            {6, 500},
            {3, 150},
            {6, 350},
            {4, 100},
        }
    },

    -- Orange fire, dying candle
    [29719] = {
        color = 35, intensity = 3,
        pattern = {
            {3, 500},
            {1, 300},
            {3, 400},
            {0, 200},
            {2, 600},
            {0, 500},
            {3, 300},
            {1, 150},
        }
    },

    -- ================================================================
    -- GREEN LIGHTS (29720)
    -- ================================================================

    -- Green toxic, slow pulse
    [29720] = {
        color = 55, intensity = 5,
        pattern = {
            {5, 2000},
            {2, 800},
            {5, 1500},
            {1, 600},
        }
    },

    -- ================================================================
    -- WARM YELLOW LIGHTS (29721)
    -- ================================================================

    -- Warm yellow, old lamp dying
    [29721] = {
        color = 210, intensity = 7,
        pattern = {
            {7, 2000},
            {0, 1500},
            {7, 800},
            {0, 200},
            {7, 200},
            {0, 3000},
        }
    },

    -- ================================================================
    -- PURPLE LIGHTS (29722-29723)
    -- ================================================================

    -- Purple mystical, slow pulse
    [29722] = {
        color = 180, intensity = 5,
        pattern = {
            {5, 3000},
            {2, 1000},
            {5, 2000},
            {3, 800},
        }
    },

    -- Purple, stable
    [29723] = { color = 180, intensity = 4, pattern = {} },
}

return CUSTOM_ITEM_LIGHTS
