uniform sampler2D u_DiffuseMap;

#if defined(USE_ALPHATEST)
uniform int       u_AlphaTest;
#endif
uniform vec4      u_FarplaneFogColor;

uniform vec3      u_ModelLocalUp;
uniform vec4      u_ModelLightColor0;

#if defined(USE_SPECULAR)
uniform vec3      u_ModelScreenRight;
uniform vec3      u_ModelScreenUp;
// u_ModelMaterial  = (matteSpec, metalSpecDelta, matteFresnel, metalFresnelDelta)
uniform vec4      u_ModelMaterial;
// u_ModelMaterial2 = (matteShinScale, metalShinScaleDelta, metalTintMix, bumpStrength)
uniform vec4      u_ModelMaterial2;
#endif

varying vec2      var_DiffuseTex;
varying vec4      var_Color;
varying vec3      var_LocalNormal;
varying vec4      var_Light0;
#if defined(USE_SPECULAR)
varying vec4      var_ViewFog;
varying vec3      var_Half0;
#else
varying float     var_FogFactor;
#endif

#if defined(USE_SUN_SHADOW)
uniform sampler2DShadow u_SunShadowMap;
uniform vec4      u_SunShadowParams;
varying vec4      var_SunShadowPos;

float CalcModelSunDark()
{
	if (u_SunShadowParams.x <= 0.001)
		return 0.0;
	vec3 sp = var_SunShadowPos.xyz;
	float subScale = u_SunShadowParams.w;
	vec2 edgeXY = min(sp.xy, vec2(subScale) - sp.xy) - vec2(0.004);
	float edgeFade = clamp(min(edgeXY.x, edgeXY.y) * 10.5, 0.0, 1.0)
	               * step(0.001, sp.z) * step(sp.z, 0.999);
	float nlMask = clamp(var_SunShadowPos.w * 3.8, 0.0, 1.0);
	float shadowWeight = edgeFade * nlMask * u_SunShadowParams.x;
	if (shadowWeight <= 0.004)
		return 0.0;
	float zRef = sp.z - u_SunShadowParams.y;
	float r1 = u_SunShadowParams.z;
	float r2 = r1 * 0.42;
	float pcf = shadow2D(u_SunShadowMap, vec3(sp.xy + vec2( r1,  r2), zRef))
	          + shadow2D(u_SunShadowMap, vec3(sp.xy + vec2(-r1, -r2), zRef));
	if (subScale >= 0.85)
	{
		pcf += shadow2D(u_SunShadowMap, vec3(sp.xy + vec2(-r2,  r1), zRef))
		     + shadow2D(u_SunShadowMap, vec3(sp.xy + vec2( r2, -r1), zRef));
		return shadowWeight * (1.0 - pcf * 0.25);
	}
	return shadowWeight * (1.0 - pcf * 0.5);
}
#endif

void main()
{
	vec4 texColor = texture2D(u_DiffuseMap, var_DiffuseTex);
	float alpha = texColor.a * var_Color.a;

#if defined(USE_ALPHATEST)
	if (u_AlphaTest == 3)
	{
		if (alpha < 0.5)
			discard;
	}
	else if (u_AlphaTest == 1)
	{
		if (alpha == 0.0)
			discard;
	}
	else if (u_AlphaTest == 2)
	{
		if (alpha >= 0.5)
			discard;
	}
#endif

#if defined(USE_SUN_SHADOW)
	// Evaluate sun shadow BEFORE lighting/specular math so only 1 scalar stays live
	float sunDark = CalcModelSunDark();
#endif

	// Single SFU normalize for the entire fragment shader (preserves crisp highlights on low-poly barrels)
	vec3 N = normalize(var_LocalNormal);

#if defined(USE_SPECULAR)
	// Division-free metallic material separation (0 SFU): separates gunmetal steel from wood/paint
	float maxC = max(texColor.r, max(texColor.g, texColor.b));
	float minC = min(texColor.r, min(texColor.g, texColor.b));
	float metalMask = clamp((0.31 * max(maxC, 0.24) - (maxC - minC)) * 14.0, 0.0, 1.0);

#if defined(USE_DERIVATIVES)
	// Ultra-lean tangent-projected micro-relief without a 2nd normalize()
	float lum = dot(texColor.rgb, vec3(0.30, 0.59, 0.11));
	vec2 dL   = clamp(vec2(dFdx(lum), dFdy(lum)), vec2(-0.28), vec2(0.28));
	vec3 grad = dL.x * u_ModelScreenRight + dL.y * u_ModelScreenUp;
	grad     -= N * dot(grad, N);
	N        -= grad * (u_ModelMaterial2.w * (0.35 + 0.65 * metalMask));
#endif

	// Dominant Light 0 diffuse + algebraic Blinn-Phong specular using VP-precomputed L0, att0, H0, V (0 SFU!)
	float att0   = var_Light0.w;
	float NdotL0 = clamp(dot(N, var_Light0.xyz), 0.0, 1.0);
	vec3 diffSum = u_ModelLightColor0.rgb * (att0 * NdotL0);

	float shinScale = u_ModelMaterial2.x + u_ModelMaterial2.y * metalMask;
	float NdotH0    = clamp(dot(N, var_Half0), 0.0, 1.0);
	float k0        = max(1.0 - (1.0 - NdotH0) * shinScale, 0.0);
	float k02       = k0 * k0;
	vec3 specSum    = u_ModelLightColor0.rgb * (min(att0, 1.4) * k02 * k02 * min(NdotL0 * 7.0, 1.0));

	float specStrength    = u_ModelMaterial.x + u_ModelMaterial.y * metalMask;
	float fresnelStrength = u_ModelMaterial.z + u_ModelMaterial.w * metalMask;
	vec3  specTint        = mix(vec3(1.0), texColor.rgb * 1.6 + vec3(0.30), u_ModelMaterial2.z * metalMask);
	specSum              *= specStrength * specTint;

	// Hemisphere ambient + reflect-free scalar sky Fresnel reflection (0 reflect, 0 pow)
	vec3  V       = var_ViewFog.xyz;
	float NdotUp  = dot(N, u_ModelLocalUp);
	float NdotV   = clamp(dot(N, V), 0.0, 1.0);
	vec3  ambLit  = var_Color.rgb * (NdotUp * 0.13 + 0.99);

	float reflUp  = 2.0 * NdotV * NdotUp - dot(V, u_ModelLocalUp);
	float skyMask = clamp(reflUp * 1.04 + 0.19, 0.0, 1.0);
	float f1      = 1.0 - NdotV;
	float f2      = f1 * f1;
	float envFac  = (f2 * f2) * fresnelStrength + metalMask * specStrength * 0.28 * skyMask;
	vec3  envRefl = (var_Color.rgb * 0.85 + diffSum * 0.35) * ((0.16 + 0.84 * skyMask) * envFac) * specTint;

#if defined(USE_SUN_SHADOW)
	float sunVis    = 1.0 - sunDark;
	vec3 litDiffuse = min(ambLit * (1.0 - sunDark * 0.30) + diffSum * sunVis, vec3(1.12));
	vec3 finalRgb   = texColor.rgb * litDiffuse + specSum * (sunVis * sunVis) + envRefl * (0.55 + 0.45 * sunVis);
#else
	vec3 litDiffuse = min(ambLit + diffSum, vec3(1.12));
	vec3 finalRgb   = texColor.rgb * litDiffuse + specSum + envRefl;
#endif

	// Zero-division quadratic soft highlight rolloff (eliminates 3x RGB rcp SFU ops!)
	vec3 over = max(finalRgb - vec3(0.92), vec3(0.0));
	finalRgb -= over * min(over * 0.35, vec3(0.32));
	float fogFac = var_ViewFog.w;
#else
	// Ultra-fast matte per-pixel variant for skin, hair, hands, uniforms, tires, canvas (1 SFU total!)
	vec3 diffSum = u_ModelLightColor0.rgb * (var_Light0.w * clamp(dot(N, var_Light0.xyz), 0.0, 1.0));
	float hemi   = dot(N, u_ModelLocalUp) * 0.13 + 0.99;
#if defined(USE_SUN_SHADOW)
	vec3 litDiffuse = min(var_Color.rgb * (hemi * (1.0 - sunDark * 0.30)) + diffSum * (1.0 - sunDark), vec3(1.12));
#else
	vec3 litDiffuse = min(var_Color.rgb * hemi + diffSum, vec3(1.12));
#endif
	vec3 finalRgb = texColor.rgb * litDiffuse;
	float fogFac  = var_FogFactor;
#endif

	gl_FragColor.rgb = mix(finalRgb, u_FarplaneFogColor.rgb, clamp(fogFac, 0.0, 1.0));
	gl_FragColor.a   = alpha;
}
