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

varying vec2      var_DiffuseTex;
#if defined(USE_MULTITEXTURE)
varying vec2      var_TexCoord1;
#endif

varying vec4      var_Color;
varying float     var_FogFactor;
#if defined(USE_SOFT_PARTICLES)
varying float     var_ParticleDepth;
#endif


void main()
{
	vec4 color  = texture2D(u_DiffuseMap, var_DiffuseTex) * var_Color;
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
