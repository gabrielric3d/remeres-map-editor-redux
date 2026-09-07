#include "rendering/postprocess/post_process_manager.h"

#include <string>

namespace {

	// -------------------------------------------------------------------------
	// "Flat Colors" — color-family quantizer.
	//
	// Collapses every shade of a color into a single flat tone: all the browns
	// in the frame become one brown, all the purples become one purple, and so
	// on. Works entirely in HSV:
	//
	//   1. Hue is snapped to one of HUE_STEPS buckets -> that is the "family".
	//   2. Saturation is snapped to SAT_LEVELS steps and floored, so a washed
	//      out tan and a deep brown end up with the same intensity.
	//   3. Value is snapped to VALUE_LEVELS steps (or forced to a single flat
	//      value when FORCE_FLAT_VALUE is on), which is what removes the
	//      light/dark shading inside a family.
	//
	// Two escapes keep the editor usable:
	//   * Pixels below BLACK_CUTOFF stay pure black, so the void outside the
	//     map does not turn into a colored slab.
	//   * Pixels below GRAY_CUTOFF saturation are treated as achromatic (grid
	//     lines, the brush cursor, text, selection boxes) and only get their
	//     brightness quantized, never a hue.
	//
	// No textures, no time dependency, so the animation timer does not need to
	// stay running for this effect.
	// -------------------------------------------------------------------------
	const char* flat_colors_body = R"(
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D u_Texture; // map framebuffer
uniform float u_var0;        // global fade 0..1

// Anything darker than this is "void" and is left black.
const float BLACK_CUTOFF = 0.06;
// Anything less saturated than this is treated as gray (UI, grid, cursor).
const float GRAY_CUTOFF = 0.12;
// A flattened family never goes darker / duller than these.
const float VALUE_FLOOR = 0.18;
const float SAT_FLOOR = 0.35;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec4 src = texture(u_Texture, vTexCoord);
    float fade = clamp(u_var0, 0.0, 1.0);

    vec3 hsv = rgb2hsv(src.rgb);
    vec3 flattened;

    if (hsv.z < BLACK_CUTOFF) {
        // Void / unmapped area: keep it black so map bounds stay readable.
        flattened = vec3(0.0);
    } else if (hsv.y < GRAY_CUTOFF) {
        // Achromatic pixel: quantize brightness, never assign it a hue.
        flattened = vec3(floor(hsv.z * GRAY_LEVELS + 0.5) / GRAY_LEVELS);
    } else {
        // Snap the hue to a family bucket. The +0.5 rounds to the nearest
        // bucket center; fract() wraps the top bucket back onto the first.
        float h = fract(floor(hsv.x * HUE_STEPS + 0.5) / HUE_STEPS);

        float s = clamp(ceil(hsv.y * SAT_LEVELS) / SAT_LEVELS, SAT_FLOOR, 1.0);

#if FORCE_FLAT_VALUE
        float v = FLAT_VALUE;
#else
        float v = clamp(floor(hsv.z * VALUE_LEVELS + 0.5) / VALUE_LEVELS, VALUE_FLOOR, 1.0);
#endif

        flattened = hsv2rgb(vec3(h, s, v));
    }

    FragColor = vec4(mix(src.rgb, flattened, fade), src.a);
}
)";

	std::string BuildFlatSource(int hue_steps, int sat_levels, int value_levels, int gray_levels, bool force_flat_value, const char* flat_value) {
		std::string source = "#version 450 core\n";
		source += "#define HUE_STEPS " + std::to_string(hue_steps) + ".0\n";
		source += "#define SAT_LEVELS " + std::to_string(sat_levels) + ".0\n";
		source += "#define VALUE_LEVELS " + std::to_string(value_levels) + ".0\n";
		source += "#define GRAY_LEVELS " + std::to_string(gray_levels) + ".0\n";
		source += std::string("#define FORCE_FLAT_VALUE ") + (force_flat_value ? "1" : "0") + "\n";
		source += std::string("#define FLAT_VALUE ") + flat_value + "\n";
		source += flat_colors_body;
		return source;
	}

	struct FlatColorsRegister {
		FlatColorsRegister() {
			PostProcessManager& manager = PostProcessManager::Instance();

			// Readable: 12 hue families, shading reduced to 4 steps. Light and
			// dark variants of a family stay distinguishable.
			manager.Register(ShaderNames::FLAT_COLORS, BuildFlatSource(12, 3, 4, 4, false, "0.72"));

			// Strong: 8 families, 2 shading steps. Roughly "one light and one
			// dark tone per color".
			manager.Register(ShaderNames::FLAT_COLORS_STRONG, BuildFlatSource(8, 2, 2, 3, false, "0.72"));

			// Extreme: 6 families, no shading at all. Exactly one flat tone per
			// color family across the whole viewport.
			manager.Register(ShaderNames::FLAT_COLORS_EXTREME, BuildFlatSource(6, 1, 1, 2, true, "0.72"));
		}
	} flat_colors_register;

} // namespace
