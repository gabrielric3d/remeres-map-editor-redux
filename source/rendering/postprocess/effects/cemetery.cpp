#include "rendering/postprocess/post_process_manager.h"

namespace {

	// Texture-less haunted graveyard atmosphere:
	//   1. Global desaturation — pulls every pixel toward gray.
	//   2. Cold neutral-gray tint applied to the whole frame.
	//   3. Slight global darkening.
	//   4. Dark vignette that closes in at the edges.
	// No time-dependent effects, so the map_display animation timer does not
	// need to stay running for this shader.
	const char* cemetery_frag_source = R"(
#version 450 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D u_Tex0; // map framebuffer (also bound as u_Texture)

uniform vec2 u_Resolution;
uniform float u_Time;
uniform float u_var0; // global fade 0..1

// -------- Desaturation (applied to the entire frame) --------
// 0 = no change, 1 = fully black & white. ~0.55 reads as "washed out / gray"
// while still letting the underlying palette show through.
const float DESATURATION     = 0.55;

// -------- Tint (applied to the entire frame) --------
const vec3  TINT_COLOR       = vec3(0.85, 0.86, 0.88);  // cold neutral gray
const float TINT_STRENGTH    = 0.18;

// -------- Global darkening --------
const float DARKEN           = 0.15;  // 0 = unchanged

// -------- Vignette --------
const vec3  VIG_COLOR        = vec3(0.04, 0.04, 0.05);  // near-black gray
const float VIG_INNER        = 0.40;
const float VIG_OUTER        = 1.35;
const float VIG_AMOUNT       = 0.82;

void main() {
    vec4 mapColor = texture(u_Tex0, vTexCoord);

    vec2 screenUv = gl_FragCoord.xy / u_Resolution;
    screenUv.y = 1.0 - screenUv.y;

    float fade = clamp(u_var0, 0.0, 1.0);

    // -------- Global desaturation --------
    vec3 grayed = vec3(dot(mapColor.rgb, vec3(0.299, 0.587, 0.114)));
    vec3 tinted = mix(mapColor.rgb, grayed, DESATURATION * fade);

    // -------- Tint (applied to everything) --------
    tinted = mix(tinted, tinted * TINT_COLOR, TINT_STRENGTH * fade);

    // -------- Global darkening --------
    tinted *= 1.0 - DARKEN * fade;

    // -------- Vignette --------
    vec2 vigUv = screenUv * 2.0 - 1.0;
    vigUv.x *= u_Resolution.x / u_Resolution.y;
    float vigDist = length(vigUv);
    float vigMask = smoothstep(VIG_INNER, VIG_OUTER, vigDist);
    vigMask = vigMask * vigMask * (3.0 - 2.0 * vigMask);  // smootherstep
    tinted = mix(tinted, VIG_COLOR, vigMask * VIG_AMOUNT * fade);

    FragColor = vec4(tinted, mapColor.a);
}
)";

	struct CemeteryRegister {
		CemeteryRegister() {
			PostProcessManager::Instance().Register(ShaderNames::CEMETERY, cemetery_frag_source);
		}
	} cemetery_register;

} // namespace
