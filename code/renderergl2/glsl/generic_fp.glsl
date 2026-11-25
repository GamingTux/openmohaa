uniform sampler2D u_DiffuseMap;
#if defined(USE_MULTITEXTURE)
uniform sampler2D u_LightMap;
uniform int       u_MultitextureEnv;
#endif

uniform int       u_AlphaTest;
uniform vec4      u_FarplaneFogColor;
#if defined(USE_SOFT_PARTICLES)
uniform sampler2D u_ScreenDepthMap;
uniform vec4      u_SoftParticleParams;
uniform vec4      u_SoftParticleZ;
uniform vec2      u_SoftParticleMask;
#endif
#if defined(USE_WET_BLOOD)
uniform vec3      u_LocalViewOrigin;
uniform vec4      u_SunShadowDir;
#endif

varying vec2      var_DiffuseTex;
#if defined(USE_MULTITEXTURE)
varying vec2      var_TexCoord1;
#endif

varying vec4      var_Color;
varying float     var_FogFactor;
#if defined(USE_SOFT_PARTICLES)
varying float     var_ParticleDepth;
#endif
#if defined(USE_WET_BLOOD)
varying vec3      var_WorldPos;
#endif


void main()
{
	vec4 texSample = texture2D(u_DiffuseMap, var_DiffuseTex);
	vec4 color  = texSample * var_Color;
#if defined(USE_WET_BLOOD)
	{
		// Liquid surface-tension meniscus normal + glossy Fresnel & sun glint (0-SFU pow for V3D 7.1)
		float texA = texSample.a;
		float hX   = texture2D(u_DiffuseMap, var_DiffuseTex + vec2(0.014, 0.0)).a;
		float hY   = texture2D(u_DiffuseMap, var_DiffuseTex + vec2(0.0, 0.014)).a;

		vec3 dp1  = dFdx(var_WorldPos);
		vec3 dp2  = dFdy(var_WorldPos);
		vec2 duv1 = dFdx(var_DiffuseTex);
		vec2 duv2 = dFdy(var_DiffuseTex);
		vec3 geoN = cross(dp1, dp2);
		geoN *= inversesqrt(max(dot(geoN, geoN), 1e-10));

		vec3 V = u_LocalViewOrigin - var_WorldPos;
		V *= inversesqrt(max(dot(V, V), 1e-10));
		if (dot(geoN, V) < 0.0)
			geoN = -geoN;

		vec3 dp2perp = cross(dp2, geoN);
		vec3 dp1perp = cross(geoN, dp1);
		vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
		float invMax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-10));
		T *= invMax;
		B *= invMax;

		float sx = clamp((texA - hX) * 1.65, -0.48, 0.48);
		float sy = clamp((texA - hY) * 1.65, -0.48, 0.48);
		vec3 N = geoN + T * sx + B * sy;
		N *= inversesqrt(max(dot(N, N), 1e-10));

		// Gloss dries out naturally as the decal fades (var_Color.a) and stays grounded in shadow
		float wetness   = smoothstep(0.24, 0.78, texA) * clamp(var_Color.a * 1.35, 0.0, 1.0);
		float lightLuma = clamp(dot(var_Color.rgb, vec3(0.30, 0.59, 0.11)), 0.10, 1.0);

		vec3 L  = u_SunShadowDir.xyz;
		vec3 H1 = L + V;
		H1 *= inversesqrt(max(dot(H1, H1), 1e-10));
		float nh1 = max(dot(N, H1), 0.0);
		float p2  = nh1 * nh1;
		float p4  = p2 * p2;
		float p16 = p4 * p4;
		float spec1 = p16 * p16 * p16;

		vec3 H2 = vec3(-0.22, 0.18, 0.96) + V;
		H2 *= inversesqrt(max(dot(H2, H2), 1e-10));
		float nh2 = max(dot(N, H2), 0.0);
		float q2  = nh2 * nh2;
		float spec2 = (q2 * q2) * (q2 * q2);

		float ndv = clamp(dot(N, V), 0.0, 1.0);
		float om  = 1.0 - ndv;
		float fres = 0.02 + 0.30 * (om * om * om * om);

		color.rgb *= mix(1.0, 0.78, wetness * ndv);
		vec3 glintTint = mix(vec3(0.95, 0.84, 0.80), vec3(0.82, 0.88, 0.96), 0.35);
		color.rgb += glintTint * ((spec1 * 0.68 + spec2 * 0.15 + fres * 0.18) * lightLuma * wetness);
	}
#endif
#if defined(USE_MULTITEXTURE)
	vec4 color1 = texture2D(u_LightMap, var_TexCoord1);
	if (u_MultitextureEnv == 1)
	{
		color.rgb += color1.rgb;
		color.a   *= color1.a;
	}
	else
	{
		color *= color1;
	}
#endif

	float alpha = color.a;
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
	
	gl_FragColor.rgb = mix(color.rgb, u_FarplaneFogColor.rgb, clamp(var_FogFactor, 0.0, 1.0));

	gl_FragColor.a = alpha;

#if defined(USE_SOFT_PARTICLES)
	{
		vec2 depthUV = (gl_FragCoord.xy - u_SoftParticleParams.xy) * u_SoftParticleParams.zw;
		float rawZ = texture2D(u_ScreenDepthMap, depthUV).r;
		float sceneZ = u_SoftParticleZ.x / max(u_SoftParticleZ.y - rawZ * u_SoftParticleZ.z, 0.0001);
		float softFade = clamp((sceneZ - var_ParticleDepth) * u_SoftParticleZ.w, 0.0, 1.0);
		softFade = softFade * (2.0 - softFade);
		gl_FragColor.rgb *= mix(1.0, softFade, u_SoftParticleMask.x);
		gl_FragColor.a   *= mix(1.0, softFade, u_SoftParticleMask.y);
	}
#endif
}
