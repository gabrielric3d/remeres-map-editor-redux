#include "rendering/postprocess/post_process_manager.h"

namespace {

	const char* nevasca_frag_source = R"(
#version 450 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D u_Tex0; // map framebuffer (also bound as u_Texture)
uniform sampler2D u_Tex1; // nevasca.png (snowflake texture)
uniform sampler2D u_Tex2; // clouds.png (wind gust texture)

uniform vec2 u_Resolution;
uniform float u_Time;
uniform float u_var0; // global fade 0..1

const float SEG_MIN          = 15.0;
const float SEG_MAX          = 120.0;

// Set to a value in [0..1] to force a fixed intensity, or -1.0 for the dynamic cycle.
const float FORCE_INTENSITY  = -1.0;

float h1(float x) { return fract(sin(x * 127.1) * 43758.5453); }
float h2(vec2 p)  { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2  h2v(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5,  183.3)))) * 43758.5453);
}

float segDuration(float i) {
    return mix(SEG_MIN, SEG_MAX, h1(i * 7.13));
}

float segTarget(float i) {
    float r = h1(i * 31.7 + 0.5);
    float biased = r * r * r;
    if (r > 0.85) {
        biased = mix(0.75, 1.0, (r - 0.85) / 0.15);
    }
    return biased;
}

float currentIntensity() {
    if (FORCE_INTENSITY >= 0.0) {
        return clamp(FORCE_INTENSITY, 0.0, 1.0);
    }

    float t = u_Time;
    float segStart = 0.0;
    float i = 0.0;
    float prevTarget = segTarget(0.0);
    float currTarget = prevTarget;
    float dur = segDuration(0.0);

    for (int k = 0; k < 64; k++) {
        if (t < segStart + dur) break;
        segStart += dur;
        i += 1.0;
        prevTarget = currTarget;
        currTarget = segTarget(i);
        dur = segDuration(i);
    }

    float localT = clamp((t - segStart) / dur, 0.0, 1.0);
    float easeT = smoothstep(0.0, 1.0, localT);
    return mix(prevTarget, currTarget, easeT);
}

const float FAR_SCALE      = 3.5;
const float MID_SCALE      = 2.0;
const float NEAR_SCALE     = 1.1;

const float FAR_SPEED      = 0.018;
const float MID_SPEED      = 0.040;
const float NEAR_SPEED     = 0.075;

const float FAR_SWAY       = 0.008;
const float MID_SWAY       = 0.014;
const float NEAR_SWAY      = 0.022;

const vec2  FALL_DIR       = vec2(-0.25, -1.0);

const float SNOW_THRESHOLD = 0.70;
const float SNOW_SOFTNESS  = 0.20;
const float EDGE_FADE      = 0.08;

float sampleSnowLayer(vec2 screenUv, float scale, float speed, float sway,
                      float intensity, float layerSeed) {
    float aspect = u_Resolution.x / u_Resolution.y;
    vec2 uv = vec2(screenUv.x * aspect, screenUv.y) * scale;

    float fallSpeed = speed * mix(0.6, 1.5, intensity);
    uv += normalize(FALL_DIR) * u_Time * fallSpeed * scale;

    uv.x += sin(uv.y * 1.1 + u_Time * 0.6 + layerSeed) * sway * scale * mix(0.4, 1.2, intensity);

    uv += vec2(layerSeed * 7.3, layerSeed * 4.1);

    uv = fract(uv);

    float lum = dot(texture(u_Tex1, uv).rgb, vec3(0.299, 0.587, 0.114));
    float mask = smoothstep(SNOW_THRESHOLD, SNOW_THRESHOLD + SNOW_SOFTNESS, lum);

    float edgeX = min(uv.x, 1.0 - uv.x);
    float edgeY = min(uv.y, 1.0 - uv.y);
    float edge = min(edgeX, edgeY);
    mask *= smoothstep(0.0, EDGE_FADE, edge);

    float intensityGate = mix(0.25, 1.0, intensity);
    return mask * intensityGate;
}

const float GUST_PERIOD          = 10.0;
const float GUST_TRAVEL_TIME     = 5.0;
const float GUST_WIDTH           = 1.6;
const float GUST_HEIGHT          = 1.2;
const float GUST_THRESHOLD       = 0.45;
const float GUST_SOFTNESS        = 0.35;
const float GUST_MAX_OPACITY     = 0.45;
const vec3  GUST_COLOR           = vec3(0.95, 0.97, 1.00);

float gustHash(float i, float salt) {
    return fract(sin(i * 127.1 + salt * 311.7) * 43758.5453);
}

const float W_LEFT_RIGHT = 3.0;
const float W_RIGHT_LEFT = 2.0;
const float W_TOP_BOTTOM = 1.0;
const float W_BOTTOM_TOP = 0.5;
const float W_DIAG_TL_BR = 1.5;
const float W_DIAG_TR_BL = 1.5;

int pickVariation(float r) {
    float total = W_LEFT_RIGHT + W_RIGHT_LEFT + W_TOP_BOTTOM + W_BOTTOM_TOP + W_DIAG_TL_BR + W_DIAG_TR_BL;
    float t = r * total;
    if (t < W_LEFT_RIGHT) return 0;
    t -= W_LEFT_RIGHT;
    if (t < W_RIGHT_LEFT) return 1;
    t -= W_RIGHT_LEFT;
    if (t < W_TOP_BOTTOM) return 2;
    t -= W_TOP_BOTTOM;
    if (t < W_BOTTOM_TOP) return 3;
    t -= W_BOTTOM_TOP;
    if (t < W_DIAG_TL_BR) return 4;
    return 5;
}

vec2 variationCenter(int variation, float progress) {
    float halfW = GUST_WIDTH  * 0.5;
    float halfH = GUST_HEIGHT * 0.5;
    if (variation == 0) return vec2(mix(-halfW, 1.0 + halfW, progress), 0.5);
    if (variation == 1) return vec2(mix(1.0 + halfW, -halfW, progress), 0.5);
    if (variation == 2) return vec2(0.5, mix(-halfH, 1.0 + halfH, progress));
    if (variation == 3) return vec2(0.5, mix(1.0 + halfH, -halfH, progress));
    if (variation == 4) return vec2(mix(-halfW, 1.0 + halfW, progress),
                                    mix(-halfH, 1.0 + halfH, progress));
    return vec2(mix(1.0 + halfW, -halfW, progress),
                mix(-halfH, 1.0 + halfH, progress));
}

vec4 sampleMap(vec2 uv, float intensity) {
    float blurAmount = smoothstep(0.35, 1.0, intensity);
    if (blurAmount <= 0.0) {
        return texture(u_Tex0, uv);
    }

    float px = 1.8 * blurAmount / u_Resolution.x;
    float py = 1.8 * blurAmount / u_Resolution.y;

    vec4 c = texture(u_Tex0, uv) * 0.36;
    c += texture(u_Tex0, uv + vec2( px,  0.0)) * 0.16;
    c += texture(u_Tex0, uv + vec2(-px,  0.0)) * 0.16;
    c += texture(u_Tex0, uv + vec2(0.0,  py)) * 0.16;
    c += texture(u_Tex0, uv + vec2(0.0, -py)) * 0.16;
    return c;
}

void main() {
    float intensity = currentIntensity();
    vec4 mapColor = sampleMap(vTexCoord, intensity);
    float mapLum = dot(mapColor.rgb, vec3(0.299, 0.587, 0.114));

    vec2 screenUv = gl_FragCoord.xy / u_Resolution;
    screenUv.y = 1.0 - screenUv.y;

    float farSnow  = sampleSnowLayer(screenUv, FAR_SCALE,  FAR_SPEED,  FAR_SWAY,  intensity, 1.0);
    float midSnow  = sampleSnowLayer(screenUv, MID_SCALE,  MID_SPEED,  MID_SWAY,  intensity, 2.0);
    float nearSnow = sampleSnowLayer(screenUv, NEAR_SCALE, NEAR_SPEED, NEAR_SWAY, intensity, 3.0);

    float distantSnowMask = clamp(farSnow * 0.45 + midSnow * 0.75, 0.0, 1.0);
    float nearSnowMask    = clamp(nearSnow * 1.0, 0.0, 1.0);

    float gustMask = 0.0;
    float gustIndex = floor(u_Time / GUST_PERIOD);
    float phase = mod(u_Time, GUST_PERIOD);

    float travelBase = mix(1.15, 0.65, intensity);
    float travelTime = GUST_TRAVEL_TIME * travelBase * mix(0.85, 1.15, gustHash(gustIndex, 7.0));

    float dynamicSkip = mix(0.30, 0.0, smoothstep(0.1, 0.95, intensity));
    float skipRoll = gustHash(gustIndex, 91.0);
    bool gust_active = (skipRoll >= dynamicSkip) && (phase <= travelTime);

    if (gust_active) {
        float progress = phase / travelTime;
        int variation = pickVariation(gustHash(gustIndex, 13.0));
        vec2 gustCenter = variationCenter(variation, progress);

        vec2 local;
        local.x = (screenUv.x - (gustCenter.x - GUST_WIDTH  * 0.5)) / GUST_WIDTH;
        local.y = (screenUv.y - (gustCenter.y - GUST_HEIGHT * 0.5)) / GUST_HEIGHT;

        if (local.x >= 0.0 && local.x <= 1.0 && local.y >= 0.0 && local.y <= 1.0) {
            float gustLum = dot(texture(u_Tex2, local).rgb, vec3(0.299, 0.587, 0.114));
            float gustShape = smoothstep(GUST_THRESHOLD, GUST_THRESHOLD + GUST_SOFTNESS, gustLum);

            vec2 centered = local * 2.0 - 1.0;
            float dist = length(centered);
            float shapeMask = 1.0 - smoothstep(0.55, 1.0, dist);

            float edgeFade = smoothstep(0.0, 0.15, progress) * (1.0 - smoothstep(0.85, 1.0, progress));

            float gustScale = mix(0.70, 1.0, intensity);

            gustMask = gustShape * shapeMask * edgeFade * GUST_MAX_OPACITY * gustScale;
        }
    }

    float fade = clamp(u_var0, 0.0, 1.0);
    distantSnowMask *= fade;
    nearSnowMask    *= fade;
    gustMask        *= fade;

    vec3  TINT_COLOR     = vec3(0.85, 0.92, 1.00);
    float tintStrength   = mix(0.03, 0.14, intensity) * fade;
    float darkenAmount   = mix(0.0, 0.18, intensity) * fade;
    float darken         = 1.0 - darkenAmount;

    float lumMask = 1.0 - smoothstep(0.35, 0.75, mapLum);
    float maskedTintStrength = tintStrength * lumMask;

    vec3 tinted = mix(mapColor.rgb, mapColor.rgb * TINT_COLOR, maskedTintStrength);
    tinted *= darken;

    vec3  WHITEOUT_COLOR     = vec3(0.90, 0.94, 1.00);
    float whiteoutBase       = smoothstep(0.20, 1.0, intensity) * 0.42 * fade;
    float whiteoutStrength   = whiteoutBase * mix(0.4, 1.0, lumMask);
    tinted = mix(tinted, WHITEOUT_COLOR, whiteoutStrength);

    vec2 vigUv = screenUv * 2.0 - 1.0;
    vigUv.x *= u_Resolution.x / u_Resolution.y;
    float vigDist = length(vigUv);
    float vigInner = mix(0.40, 0.10, intensity);
    float vigOuter = mix(1.60, 1.20, intensity);
    float vigMask = smoothstep(vigInner, vigOuter, vigDist);
    vigMask = vigMask * vigMask * (3.0 - 2.0 * vigMask);
    float vigAmount = vigMask * smoothstep(0.10, 0.90, intensity) * 0.80 * fade;
    tinted = mix(tinted, WHITEOUT_COLOR, vigAmount);

    vec3 snowColor = vec3(0.96, 0.98, 1.00);

    vec3 distantSnowRGB = snowColor * distantSnowMask;
    vec3 withDistantSnow = 1.0 - (1.0 - tinted) * (1.0 - distantSnowRGB);

    withDistantSnow = mix(withDistantSnow, WHITEOUT_COLOR,
                          whiteoutStrength * 0.65);

    vec3 gustLayer = GUST_COLOR * gustMask;
    vec3 withGust = 1.0 - (1.0 - withDistantSnow) * (1.0 - gustLayer);

    vec3 nearSnowRGB = snowColor * nearSnowMask;
    vec3 finalRGB = 1.0 - (1.0 - withGust) * (1.0 - nearSnowRGB);

    FragColor = vec4(finalRGB, mapColor.a);
}
)";

	struct NevascaRegister {
		NevascaRegister() {
			PostProcessManager::Instance().Register(ShaderNames::NEVASCA, nevasca_frag_source);
			PostProcessManager::Instance().RegisterAuxTexture(ShaderNames::NEVASCA, "u_Tex1", "shaders/nevasca.png");
			PostProcessManager::Instance().RegisterAuxTexture(ShaderNames::NEVASCA, "u_Tex2", "shaders/clouds.png");
		}
	} nevasca_register;

} // namespace
