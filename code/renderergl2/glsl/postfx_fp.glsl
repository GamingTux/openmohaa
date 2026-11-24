uniform sampler2D u_TextureMap;
#if defined(POSTFX_COMBINE)
uniform sampler2D u_LevelsMap;
#endif

uniform vec2      u_InvTexRes;
uniform vec4      u_Color;
#if defined(POSTFX_BRIGHT) || defined(POSTFX_COMBINE) || defined(POSTFX_COMBINE_NOBLOOM)
uniform vec4      u_PostFxBounds;
#endif
#if defined(POSTFX_COMBINE) || defined(POSTFX_COMBINE_NOBLOOM)
uniform vec2      u_PostFxSharpen;
#endif
#if defined(POSTFX_COMBINE) || defined(POSTFX_COMBINE_NOBLOOM) || defined(POSTFX_ONCHIP_FILMIC)
uniform vec4      u_ColorGrade;
#endif

varying vec2      var_TexCoords;
#if defined(POSTFX_COMBINE)
varying vec2      var_BloomCoords;
#endif

#if defined(POSTFX_COMBINE) || defined(POSTFX_COMBINE_NOBLOOM)
vec3 SampleSharpened(vec2 uv)
{
	vec2 uvMin = u_PostFxBounds.xy;
	vec2 uvMax = u_PostFxBounds.zw;
	vec2 cUv   = clamp(uv, uvMin, uvMax);
	vec3 c     = texture2D(u_TextureMap, cUv).rgb;
	float sharp = u_PostFxSharpen.x;

	if (sharp <= 0.001)
		return c;

	vec2 d = u_InvTexRes;
	vec3 n = texture2D(u_TextureMap, clamp(cUv + vec2( 0.0,  d.y), uvMin, uvMax)).rgb;
	vec3 s = texture2D(u_TextureMap, clamp(cUv + vec2( 0.0, -d.y), uvMin, uvMax)).rgb;
	vec3 e = texture2D(u_TextureMap, clamp(cUv + vec2( d.x,  0.0), uvMin, uvMax)).rgb;
	vec3 w = texture2D(u_TextureMap, clamp(cUv + vec2(-d.x,  0.0), uvMin, uvMax)).rgb;

	// Contrast-Adaptive Sharpening (zero-div, zero-sqrt, anti-halo clamped for V3D 7.1)
	vec3 mn  = min(c, min(min(n, s), min(e, w)));
	vec3 mx  = max(c, max(max(n, s), max(e, w)));
	vec3 avg = (n + s + e + w) * 0.25;
	vec3 amp = clamp(min(mn, vec3(1.0) - mx) * 2.0, 0.0, 1.0);

	return clamp(c + (c - avg) * (amp * sharp), 0.0, 1.25);
}
#endif

void main()
{
#if defined(POSTFX_BRIGHT)
	// Fast 6x6 bilinear downsample + branchless zero-division soft-quadratic bright extraction
	// u_Color = (threshStart, invRange, satBoost, gain)
	vec2 d = u_InvTexRes;
	vec2 uvMin = u_PostFxBounds.xy;
	vec2 uvMax = u_PostFxBounds.zw;
	vec3 c = (texture2D(u_TextureMap, clamp(var_TexCoords + vec2(-d.x, -d.y), uvMin, uvMax)).rgb
	        + texture2D(u_TextureMap, clamp(var_TexCoords + vec2( d.x, -d.y), uvMin, uvMax)).rgb
	        + texture2D(u_TextureMap, clamp(var_TexCoords + vec2(-d.x,  d.y), uvMin, uvMax)).rgb
	        + texture2D(u_TextureMap, clamp(var_TexCoords + vec2( d.x,  d.y), uvMin, uvMax)).rgb) * 0.25;

	float peak = max(c.r, max(c.g, c.b));
	float lum  = dot(c, vec3(0.25, 0.58, 0.17));
	float br   = mix(lum, peak, 0.55);
	float soft = clamp((br - u_Color.x) * u_Color.y, 0.0, 1.0);
	vec3 avg   = c * (soft * soft * u_Color.w);
	float aLum = dot(avg, vec3(0.299, 0.587, 0.114));
	gl_FragColor = vec4(mix(vec3(aLum), avg, 1.0 + u_Color.z), 1.0);

#elif defined(POSTFX_BLUR)
	// Separable 9-tap Gaussian blur using 5 hardware bilinear taps (V3D 7.1 ultra-fast)
	vec2 o1 = u_InvTexRes * 1.384615;
	vec2 o2 = u_InvTexRes * 3.230769;
	vec3 sum = texture2D(u_TextureMap, var_TexCoords).rgb * 0.227027;
	sum += (texture2D(u_TextureMap, var_TexCoords + o1).rgb + texture2D(u_TextureMap, var_TexCoords - o1).rgb) * 0.316216;
	sum += (texture2D(u_TextureMap, var_TexCoords + o2).rgb + texture2D(u_TextureMap, var_TexCoords - o2).rgb) * 0.070270;
	gl_FragColor = vec4(sum, 1.0);

#elif defined(POSTFX_COMBINE)
	// Ultra-fast branchless Pi 5 V3D combine: CAS sharpen + energy-balanced bloom + zero-div cubic filmic S-curve + map color grade
	// u_Color = (bloomIntensity, c0, c1, c2), u_ColorGrade = (tintR, tintG, tintB, saturation)
	vec3 scene = SampleSharpened(var_TexCoords);
	vec3 bloom = texture2D(u_LevelsMap, var_BloomCoords).rgb;
	vec3 col   = scene + bloom * (u_Color.x * (vec3(1.0) - scene * 0.32));
	vec3 x     = min(col, vec3(1.15));
	col        = x * (vec3(u_Color.y) + x * (vec3(u_Color.z) + x * u_Color.w));
	float luma = dot(col, vec3(0.299, 0.587, 0.114));
	col        = mix(vec3(luma), col, u_ColorGrade.w) * u_ColorGrade.rgb;
	gl_FragColor = vec4(col, 1.0);

#elif defined(POSTFX_COMBINE_NOBLOOM)
	// Ultra-fast branchless Pi 5 V3D CAS sharpen + filmic S-curve + map color grade without bloom texture fetch
	// u_Color = (0, c0, c1, c2), u_ColorGrade = (tintR, tintG, tintB, saturation)
	vec3 x     = min(SampleSharpened(var_TexCoords), vec3(1.15));
	vec3 col   = x * (vec3(u_Color.y) + x * (vec3(u_Color.z) + x * u_Color.w));
	float luma = dot(col, vec3(0.299, 0.587, 0.114));
	col        = mix(vec3(luma), col, u_ColorGrade.w) * u_ColorGrade.rgb;
	gl_FragColor = vec4(col, 1.0);

#elif defined(POSTFX_ONCHIP_FILMIC)
	// Single-pass on-chip TBDR Filmic S-curve + map color grade via GL_EXT_shader_framebuffer_fetch (0 offscreen FBO, 0 RAM traffic!)
	// u_Color = (0, c0, c1, c2), u_ColorGrade = (tintR, tintG, tintB, saturation)
	vec3 x     = min(gl_FragColor.rgb, vec3(1.15));
	vec3 col   = x * (vec3(u_Color.y) + x * (vec3(u_Color.z) + x * u_Color.w));
	float luma = dot(col, vec3(0.299, 0.587, 0.114));
	col        = mix(vec3(luma), col, u_ColorGrade.w) * u_ColorGrade.rgb;
	gl_FragColor = vec4(col, 1.0);
#endif
}
