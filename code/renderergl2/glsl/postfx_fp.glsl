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
uniform vec2      u_PostFxSharpen; // x = CAS sharpen, y = underwater phase (>0 when submerged)
uniform vec4      u_MotionBlur;
uniform vec4      u_SunShafts;     // xy = sun bloom UV, z = intensity (>0 when facing sun)
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
	if (u_PostFxSharpen.y > 0.001)
	{
		float wt = u_PostFxSharpen.y;
		uv += vec2(sin(uv.y * 26.0 + wt) * 0.0024, cos(uv.x * 22.0 + wt * 0.85) * 0.0018);
	}
	vec2 cUv   = clamp(uv, uvMin, uvMax);
	vec3 c     = texture2D(u_TextureMap, cUv).rgb;
	vec2 vel   = u_MotionBlur.xy;

	// Subtle center-protected 4-tap camera motion blur (uniform early-out: 0 taps when still/aiming)
	if (abs(vel.x) + abs(vel.y) > 0.0001)
	{
		vec2 mid   = (uvMin + uvMax) * 0.5;
		vec2 rel   = (cUv - mid) * u_MotionBlur.zw;
		float edge = clamp(dot(rel, rel) * 0.72, 0.0, 1.0);
		vec2 stepV = vel * (0.22 + 0.78 * edge);

		vec3 s1 = texture2D(u_TextureMap, clamp(cUv - stepV,        uvMin, uvMax)).rgb;
		vec3 s2 = texture2D(u_TextureMap, clamp(cUv - stepV * 0.44, uvMin, uvMax)).rgb;
		vec3 s3 = texture2D(u_TextureMap, clamp(cUv + stepV * 0.44, uvMin, uvMax)).rgb;
		vec3 s4 = texture2D(u_TextureMap, clamp(cUv + stepV,        uvMin, uvMax)).rgb;
		c = c * 0.30 + (s2 + s3) * 0.23 + (s1 + s4) * 0.12;
	}

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
	// Ultra-fast branchless Pi 5 V3D combine: CAS sharpen + motion blur + bloom + radial godrays + underwater + filmic S-curve
	// u_Color = (bloomIntensity, c0, c1, c2), u_ColorGrade = (tintR, tintG, tintB, saturation)
	vec3 scene = SampleSharpened(var_TexCoords);
	vec3 bloom = texture2D(u_LevelsMap, var_BloomCoords).rgb;
	vec3 col   = scene + bloom * (u_Color.x * (vec3(1.0) - scene * 0.32));
	if (u_SunShafts.z > 0.001)
	{
		vec2  rayDelta = (u_SunShafts.xy - var_BloomCoords) * 0.14;
		vec3  shafts   = vec3(0.0);
		vec2  sUv      = var_BloomCoords;
		sUv += rayDelta; shafts += texture2D(u_LevelsMap, clamp(sUv, vec2(0.0), vec2(1.0))).rgb * 0.30;
		sUv += rayDelta; shafts += texture2D(u_LevelsMap, clamp(sUv, vec2(0.0), vec2(1.0))).rgb * 0.25;
		sUv += rayDelta; shafts += texture2D(u_LevelsMap, clamp(sUv, vec2(0.0), vec2(1.0))).rgb * 0.20;
		sUv += rayDelta; shafts += texture2D(u_LevelsMap, clamp(sUv, vec2(0.0), vec2(1.0))).rgb * 0.15;
		sUv += rayDelta; shafts += texture2D(u_LevelsMap, clamp(sUv, vec2(0.0), vec2(1.0))).rgb * 0.10;
		col += shafts * (vec3(1.0, 0.90, 0.72) * u_SunShafts.z);
	}
	vec3 x     = min(col, vec3(1.15));
	col        = x * (vec3(u_Color.y) + x * (vec3(u_Color.z) + x * u_Color.w));
	float luma = dot(col, vec3(0.299, 0.587, 0.114));
	col        = mix(vec3(luma), col, u_ColorGrade.w) * u_ColorGrade.rgb;
	if (u_PostFxSharpen.y > 0.001)
	{
		float wt = u_PostFxSharpen.y;
		float caust = 0.045 * (0.5 + 0.5 * sin(var_TexCoords.x * 38.0 + wt * 1.4) * cos(var_TexCoords.y * 30.0 - wt)) * clamp(var_TexCoords.y, 0.0, 1.0);
		col = col * vec3(0.72, 0.96, 1.06) + vec3(0.02, 0.10, 0.13) + vec3(caust * 0.65, caust, caust * 1.15);
	}
	gl_FragColor = vec4(col, 1.0);

#elif defined(POSTFX_COMBINE_NOBLOOM)
	// Ultra-fast branchless Pi 5 V3D CAS sharpen + motion blur + underwater + filmic S-curve without bloom texture fetch
	// u_Color = (0, c0, c1, c2), u_ColorGrade = (tintR, tintG, tintB, saturation)
	vec3 x     = min(SampleSharpened(var_TexCoords), vec3(1.15));
	vec3 col   = x * (vec3(u_Color.y) + x * (vec3(u_Color.z) + x * u_Color.w));
	float luma = dot(col, vec3(0.299, 0.587, 0.114));
	col        = mix(vec3(luma), col, u_ColorGrade.w) * u_ColorGrade.rgb;
	if (u_PostFxSharpen.y > 0.001)
	{
		float wt = u_PostFxSharpen.y;
		float caust = 0.045 * (0.5 + 0.5 * sin(var_TexCoords.x * 38.0 + wt * 1.4) * cos(var_TexCoords.y * 30.0 - wt)) * clamp(var_TexCoords.y, 0.0, 1.0);
		col = col * vec3(0.72, 0.96, 1.06) + vec3(0.02, 0.10, 0.13) + vec3(caust * 0.65, caust, caust * 1.15);
	}
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
