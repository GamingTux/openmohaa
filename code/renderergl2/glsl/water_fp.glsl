uniform sampler2D u_DiffuseMap;
#if defined(USE_MULTITEXTURE) || defined(USE_LIGHTMAP)
uniform sampler2D u_LightMap;
#endif
#if defined(USE_MULTITEXTURE)
uniform int       u_MultitextureEnv;
#endif

uniform vec3      u_ViewOrigin;
uniform vec4      u_FarplaneFogColor;

// u_WaterSunDir       = (sunDir.xyz, specBoost)
// u_WaterSunColor     = (sunSpecRgb.rgb, foamStrength)
// u_WaterSkyColor     = (zenithSkyRgb.rgb, fresnelStrength)
// u_WaterHorizonColor = (horizonSkyRgb.rgb, waveAmp)
// u_WaterParams       = (waveFreqCycles, waveSpeedCycles, refractScale, deepTintMix)
uniform vec4      u_WaterSunDir;
uniform vec4      u_WaterSunColor;
uniform vec4      u_WaterSkyColor;
uniform vec4      u_WaterHorizonColor;
uniform vec4      u_WaterParams;

varying vec2      var_DiffuseTex;
#if defined(USE_MULTITEXTURE) || defined(USE_LIGHTMAP)
varying vec2      var_TexCoord1;
#endif
varying vec4      var_Color;
varying vec4      var_WorldPosFog;
varying vec3      var_WorldNormal;
varying vec3      var_WavePhases;

void main()
{
	// 3 cascaded domain-warped organic wave octaves from VP-precomputed linear phases (0 trig, 0 dot!)
	float tri1 = abs(fract(var_WavePhases.x) - 0.5) * 4.0 - 1.0;
	float w1   = tri1 * (1.5 - 0.5 * tri1 * tri1);

	float tri2 = abs(fract(var_WavePhases.y + w1 * 0.28) - 0.5) * 4.0 - 1.0;
	float w2   = tri2 * (1.5 - 0.5 * tri2 * tri2);

	float tri3 = abs(fract(var_WavePhases.z + w1 * 0.22 - w2 * 0.26) - 0.5) * 4.0 - 1.0;
	float w3   = tri3 * (1.5 - 0.5 * tri3 * tri3);

	// View vector & distance-based attenuation (silky, tile-free horizon on Pi 5)
	vec3  toEye      = u_ViewOrigin - var_WorldPosFog.xyz;
	float invDist    = inversesqrt(max(dot(toEye, toEye), 1.0));
	vec3  V          = toEye * invDist;
	float detailFade = clamp(invDist * 850.0, 0.08, 1.0);
	float waveFade   = clamp(invDist * 1150.0, 0.06, 1.0) * clamp(abs(V.z) * 4.5 + 0.18, 0.18, 1.0);

	vec2 grad = vec2(
		w1 * 0.34 - w2 * 0.26 + w3 * 0.32 * detailFade,
		w1 * 0.22 + w2 * 0.38 - w3 * 0.24 * detailFade
	);
	float crest = clamp((w1 * 0.45 + w2 * 0.45 + w3 * 0.40) - 0.60, 0.0, 1.0);

	// Sample original MOHAA water texture(s) with subtle wave-driven UV refraction
	vec2 refractOff = grad * (u_WaterParams.z * waveFade);
	vec4 tex0  = texture2D(u_DiffuseMap, var_DiffuseTex + refractOff);
	vec4 color = tex0 * var_Color;
	float l0   = dot(tex0.rgb, vec3(0.30, 0.59, 0.11));

#if defined(USE_MULTITEXTURE)
	vec4 tex1 = texture2D(u_LightMap, var_TexCoord1 - refractOff);
	float l1  = dot(tex1.rgb, vec3(0.30, 0.59, 0.11));
	grad += vec2((l0 - l1) * 0.62, (l0 + l1 - 0.65) * 0.42) * detailFade;

	if (u_MultitextureEnv == 1)
	{
		color.rgb += tex1.rgb;
		color.a   *= tex1.a;
	}
	else
	{
		color *= tex1;
	}
#else
	float micro = (l0 - 0.42) * 0.48 * detailFade;
	grad += vec2(micro, -micro * 0.72);
#endif

	// Perturbed world normal using 0-SFU Newton-Raphson normalization (accurate to <0.5% for |grad|<=0.35)
	float above  = step(0.0, dot(var_WorldNormal, V));
	float facing = above * 2.0 - 1.0;
	vec3  N      = var_WorldNormal * facing + vec3(grad.x, grad.y, 0.0) * (u_WaterHorizonColor.w * waveFade);
	N           *= 1.5 - 0.5 * dot(N, N);

	// Fast Schlick Fresnel (^4, zero pow())
	float NdotV   = clamp(dot(N, V), 0.02, 1.0);
	float f1      = 1.0 - NdotV;
	float f2      = f1 * f1;
	float fresnel = clamp((0.05 + 0.95 * f2 * f2) * u_WaterSkyColor.w, 0.0, 0.88) * (0.35 + 0.65 * above);

	// Reflected sky/horizon gradient using scalar R.z (1 FMA instead of 3D reflect)
	float skyGrad   = clamp((2.0 * NdotV * N.z - V.z) * 1.45, 0.0, 1.0);
	vec3  reflColor = mix(u_WaterHorizonColor.rgb, u_WaterSkyColor.rgb, skyGrad);

	// Wave slope diffuse relief + subtle deep-water tint when looking steeply down
	float sunNdotL  = clamp(dot(N, u_WaterSunDir.xyz), 0.0, 1.0);
	vec3  deepWater = color.rgb * (0.72 + 0.52 * sunNdotL);
	deepWater       = mix(deepWater, deepWater * vec3(0.68, 0.88, 0.98), u_WaterParams.w * NdotV);

#if defined(USE_LIGHTMAP)
	vec3 lm    = texture2D(u_LightMap, var_TexCoord1).rgb;
	deepWater *= lm;
	reflColor *= (lm * 0.82 + vec3(0.18));
#endif

	vec3 waterRgb = mix(deepWater, reflColor, fresnel);

	// Two-lobe algebraic Blinn-Phong glint using exact scalar N·H = (N·L + N·V) / |L + V| (no 3D H vector!)
	float invH  = inversesqrt(max(2.0 + 2.0 * dot(u_WaterSunDir.xyz, V), 0.01));
	float NdotH = clamp((sunNdotL + NdotV) * invH, 0.0, 1.0);
	float b1    = max(1.0 - (1.0 - NdotH) * 5.0, 0.0);
	float b2    = b1 * b1;
	float s1    = max(1.0 - (1.0 - NdotH) * 24.0, 0.0);
	float s2    = s1 * s1;
	float specTotal = (b2 * b2 * 0.28 + s2 * s2 * 1.35 * detailFade) * u_WaterSunDir.w * (0.25 + 0.75 * above);
#if defined(USE_LIGHTMAP)
	specTotal *= max(lm.r, max(lm.g, lm.b));
#endif
	waterRgb += u_WaterSunColor.rgb * specTotal;

	// Subtle ocean/river wave-crest foam highlight
	float foamMask = crest * clamp(l0 * 1.85, 0.20, 1.0) * u_WaterSunColor.w * detailFade;
	waterRgb       = mix(waterRgb, u_WaterHorizonColor.rgb * 1.15 + vec3(0.06), min(foamMask, 0.36));

	gl_FragColor.rgb = mix(min(waterRgb, vec3(1.05)), u_FarplaneFogColor.rgb, clamp(var_WorldPosFog.w, 0.0, 1.0));
	gl_FragColor.a   = color.a;
}
