attribute vec4 attr_TexCoord0;
#if defined(USE_LIGHTMAP) || defined(USE_TCGEN)
attribute vec4 attr_TexCoord1;
#endif
attribute vec4 attr_Color;

attribute vec3 attr_Position;
attribute vec3 attr_Normal;
attribute vec4 attr_Tangent;

#if defined(USE_VERTEX_ANIMATION)
attribute vec3 attr_Position2;
attribute vec3 attr_Normal2;
attribute vec4 attr_Tangent2;
#elif defined(USE_BONE_ANIMATION)
attribute vec4 attr_BoneIndexes;
attribute vec4 attr_BoneWeights;
#endif

#if defined(USE_LIGHT) && !defined(USE_LIGHT_VECTOR)
attribute vec3 attr_LightDirection;
#endif

#if defined(USE_DELUXEMAP)
uniform vec4   u_EnableTextures; // x = normal, y = deluxe, z = specular, w = cube
#endif

#if (defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)) || defined(USE_WORLD_RELIEF)
uniform vec3   u_ViewOrigin;
#endif

#if defined(USE_WORLD_RELIEF)
uniform vec3   u_WorldReliefRight;
uniform vec3   u_WorldReliefUp;
uniform vec4   u_WorldReliefLight;
#endif

#if defined(USE_SUN_SHADOW)
uniform mat4   u_SunShadowMvp;
uniform vec4   u_SunShadowDir;
#endif

#if defined(USE_TCGEN)
uniform int    u_TCGen0;
uniform vec3   u_TCGen0Vector0;
uniform vec3   u_TCGen0Vector1;
uniform vec3   u_LocalViewOrigin;
#endif

#if defined(USE_TCMOD)
uniform vec4   u_DiffuseTexMatrix0;
uniform vec4   u_DiffuseTexMatrix1;
uniform vec4   u_DiffuseTexMatrix2;
uniform vec4   u_DiffuseTexMatrix3;
uniform vec4   u_DiffuseTexMatrix4;
uniform vec4   u_DiffuseTexMatrix5;
uniform vec4   u_DiffuseTexMatrix6;
uniform vec4   u_DiffuseTexMatrix7;
#endif

uniform mat4   u_ModelViewProjectionMatrix;
uniform vec4   u_BaseColor;
uniform vec4   u_VertColor;
uniform vec2   u_FarplaneFogParams;
uniform vec4   u_WindParams;

#if defined(USE_MODELMATRIX)
uniform mat4   u_ModelMatrix;
#endif

#if defined(USE_VERTEX_ANIMATION)
uniform float  u_VertexLerp;
#elif defined(USE_BONE_ANIMATION)
uniform mat4 u_BoneMatrix[MAX_GLSL_BONES];
#endif

#if defined(USE_LIGHT_VECTOR)
uniform vec4   u_LightOrigin;
uniform float  u_LightRadius;
uniform vec3   u_DirectedLight;
uniform vec3   u_AmbientLight;
#endif

#if defined(USE_PRIMARY_LIGHT) || defined(USE_SHADOWMAP)
uniform vec4  u_PrimaryLightOrigin;
uniform float u_PrimaryLightRadius;
#endif

varying vec4   var_TexCoords;

varying vec4   var_Color;
varying float  var_FogFactor;
#if defined(USE_LIGHT_VECTOR) && !defined(USE_FAST_LIGHT)
varying vec4   var_ColorAmbient;
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
varying vec4   var_Normal;
varying vec4   var_Tangent;
varying vec4   var_Bitangent;
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
varying vec4   var_LightDir;
#endif

#if defined(USE_PRIMARY_LIGHT) || defined(USE_SHADOWMAP)
varying vec4   var_PrimaryLightDir;
#endif

#if defined(USE_WORLD_RELIEF)
varying vec4   var_ReliefLight;  // xy = screen-space tangent light * distFade, z = base N·H, w = distFade
varying vec3   var_ReliefHalf;   // xy = screen-space tangent half-vec * distFade, z = grazing Fresnel * distFade
#endif

#if defined(USE_SUN_SHADOW)
varying vec4   var_SunShadowPos; // xyz = shadow map uv + depth [0..1], w = max(dot(N, sunDir), 0.0)
#endif

#if defined(USE_TCGEN)
vec2 GenTexCoords(int TCGen, vec3 position, vec3 normal, vec3 TCGenVector0, vec3 TCGenVector1)
{
	vec2 tex = attr_TexCoord0.st;

	if (TCGen == TCGEN_LIGHTMAP)
	{
		tex = attr_TexCoord1.st;
	}
	else if (TCGen == TCGEN_ENVIRONMENT_MAPPED)
	{
		vec3 viewer = normalize(u_LocalViewOrigin - position);
		vec2 ref = reflect(viewer, normal).yz;
		tex.s = ref.x * -0.5 + 0.5;
		tex.t = ref.y *  0.5 + 0.5;
	}
	else if (TCGen == TCGEN_ENVIRONMENT_MAPPED2)
	{
		vec3 viewer = normalize(u_LocalViewOrigin - position);
		float d = dot(normal, viewer);
		vec3 reflected = (d > 0.0) ? viewer : (viewer - 2.0 * d * normal);
		tex.s = 0.5 + reflected.y * 0.5;
		tex.t = 0.5 - reflected.z * 0.5;
	}
	else if (TCGen == TCGEN_SUN_REFLECTION)
	{
		vec3 viewer = normalize(position - u_LocalViewOrigin);
		float d = dot(viewer, normal);
		vec3 reflected = (d > 0.0) ? viewer : (viewer - 2.0 * d * normal);
		vec3 sunAxis2 = cross(TCGenVector0, TCGenVector1);
		vec3 sunRef = TCGenVector0 * reflected.x + TCGenVector1 * reflected.z - sunAxis2 * reflected.y;
		tex = sunRef.yz + vec2(0.5);
	}
	else if (TCGen == TCGEN_VECTOR)
	{
		tex = vec2(dot(position, TCGenVector0), dot(position, TCGenVector1));
	}

	return tex;
}
#endif

#if defined(USE_TCMOD)
vec2 ModTexCoords(vec2 st, vec3 position, vec4 texMatrix[8])
{
	vec2 st2 = st;
	vec2 offsetPos = vec2(position.x + position.z, position.y);

	st2 = vec2(st2.x * texMatrix[0].x + st2.y * texMatrix[0].y + texMatrix[0].z,
	           st2.x * texMatrix[1].x + st2.y * texMatrix[1].y + texMatrix[1].z);
	st2 += texMatrix[0].w * sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(texMatrix[1].w * 2.0 * M_PI));

	st2 = vec2(st2.x * texMatrix[2].x + st2.y * texMatrix[2].y + texMatrix[2].z,
	           st2.x * texMatrix[3].x + st2.y * texMatrix[3].y + texMatrix[3].z);
	st2 += texMatrix[2].w * sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(texMatrix[3].w * 2.0 * M_PI));

	st2 = vec2(st2.x * texMatrix[4].x + st2.y * texMatrix[4].y + texMatrix[4].z,
	           st2.x * texMatrix[5].x + st2.y * texMatrix[5].y + texMatrix[5].z);
	st2 += texMatrix[4].w * sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(texMatrix[5].w * 2.0 * M_PI));

	st2 = vec2(st2.x * texMatrix[6].x + st2.y * texMatrix[6].y + texMatrix[6].z,
	           st2.x * texMatrix[7].x + st2.y * texMatrix[7].y + texMatrix[7].z);
	st2 += texMatrix[6].w * sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(texMatrix[7].w * 2.0 * M_PI));

	return st2;
}
#endif


float CalcLightAttenuation(float point, float normDist)
{
	// zero light at 1.0, approximating q3 style
	// also don't attenuate directional light
	float attenuation = (0.5 * normDist - 1.5) * point + 1.0;

	// clamp attenuation
	#if defined(NO_LIGHT_CLAMP)
	attenuation = max(attenuation, 0.0);
	#else
	attenuation = clamp(attenuation, 0.0, 1.0);
	#endif

	return attenuation;
}


void main()
{
#if defined(USE_VERTEX_ANIMATION)
	vec3 position  = mix(attr_Position,    attr_Position2,    u_VertexLerp);
	vec3 normal    = mix(attr_Normal,      attr_Normal2,      u_VertexLerp);
  #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 tangent   = mix(attr_Tangent.xyz, attr_Tangent2.xyz, u_VertexLerp);
  #endif
#elif defined(USE_BONE_ANIMATION)
	mat4 vtxMat  = u_BoneMatrix[int(attr_BoneIndexes.x)] * attr_BoneWeights.x;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.y)] * attr_BoneWeights.y;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.z)] * attr_BoneWeights.z;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.w)] * attr_BoneWeights.w;
	mat3 nrmMat = mat3(cross(vtxMat[1].xyz, vtxMat[2].xyz), cross(vtxMat[2].xyz, vtxMat[0].xyz), cross(vtxMat[0].xyz, vtxMat[1].xyz));

	vec3 position  = vec3(vtxMat * vec4(attr_Position, 1.0));
	vec3 normal    = normalize(nrmMat * attr_Normal);
  #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 tangent   = normalize(nrmMat * attr_Tangent.xyz);
  #endif
#else
	vec3 position  = attr_Position;
	vec3 normal    = attr_Normal;
  #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 tangent   = attr_Tangent.xyz;
  #endif
#endif

#if defined(USE_TCGEN)
	vec2 texCoords = GenTexCoords(u_TCGen0, position, normal, u_TCGen0Vector0, u_TCGen0Vector1);
#else
	vec2 texCoords = attr_TexCoord0.st;
#endif

#if defined(USE_TCMOD)
	vec4 diffuseTexMatrix[8];
	diffuseTexMatrix[0] = u_DiffuseTexMatrix0;
	diffuseTexMatrix[1] = u_DiffuseTexMatrix1;
	diffuseTexMatrix[2] = u_DiffuseTexMatrix2;
	diffuseTexMatrix[3] = u_DiffuseTexMatrix3;
	diffuseTexMatrix[4] = u_DiffuseTexMatrix4;
	diffuseTexMatrix[5] = u_DiffuseTexMatrix5;
	diffuseTexMatrix[6] = u_DiffuseTexMatrix6;
	diffuseTexMatrix[7] = u_DiffuseTexMatrix7;
	var_TexCoords.xy = ModTexCoords(texCoords, position, diffuseTexMatrix);
#else
	var_TexCoords.xy = texCoords;
#endif

	if (u_WindParams.x > 0.0)
	{
		float wPhase = u_WindParams.y + u_WindParams.z + dot(position.xy, vec2(0.032, 0.024)) + position.z * 0.018;
		if (u_WindParams.w > 1.5)
		{
			float pin = clamp(attr_TexCoord0.t, 0.0, 1.0);
			float wave = sin(wPhase * 2.1 + attr_TexCoord0.t * 4.2) + 0.35 * sin(wPhase * 4.5 + attr_TexCoord0.s * 3.0);
			vec3 windDir = (dot(normal, normal) > 0.01) ? normalize(normal) : vec3(1.0, 0.4, 0.0);
			position += windDir * (wave * u_WindParams.x * pin);
		}
		else
		{
			float sway = sin(wPhase) + 0.35 * sin(wPhase * 2.3 + 1.1);
			float flutter = sin(wPhase * 3.7 + position.x * 0.11) * 0.25;
			float heightMask = clamp((position.z + 32.0) * 0.010, 0.20, 1.0);
			position.x += (sway + flutter) * u_WindParams.x * heightMask;
			position.y += (sway * 0.65 - flutter) * u_WindParams.x * heightMask;
		}
	}

	gl_Position = u_ModelViewProjectionMatrix * vec4(position, 1.0);
	var_FogFactor = (gl_Position.w - u_FarplaneFogParams.x) * u_FarplaneFogParams.y;

#if defined(USE_MODELMATRIX)
	position  = (u_ModelMatrix * vec4(position, 1.0)).xyz;
	normal    = (u_ModelMatrix * vec4(normal,   0.0)).xyz;
  #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	tangent   = (u_ModelMatrix * vec4(tangent,  0.0)).xyz;
  #endif
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 bitangent = cross(normal, tangent) * attr_Tangent.w;
#endif

#if defined(USE_LIGHT_VECTOR)
	vec3 L = u_LightOrigin.xyz - (position * u_LightOrigin.w);
#elif defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 L = attr_LightDirection;
  #if defined(USE_MODELMATRIX)
	L = (u_ModelMatrix * vec4(L, 0.0)).xyz;
  #endif
#endif

#if defined(USE_LIGHTMAP)
	var_TexCoords.zw = attr_TexCoord1.st;
#endif

	var_Color = u_VertColor * attr_Color + u_BaseColor;

#if defined(USE_LIGHT_VECTOR)
  #if defined(USE_FAST_LIGHT)
	float sqrLightDist = dot(L, L);
	float NL = clamp(dot(normalize(normal), L) / sqrt(sqrLightDist), 0.0, 1.0);
	float attenuation = CalcLightAttenuation(u_LightOrigin.w, u_LightRadius * u_LightRadius / sqrLightDist);

	var_Color.rgb *= u_DirectedLight * (attenuation * NL) + u_AmbientLight;
  #else
	var_ColorAmbient.rgb = u_AmbientLight * var_Color.rgb;
	var_Color.rgb *= u_DirectedLight;
    #if defined(USE_PBR)
	var_ColorAmbient.rgb *= var_ColorAmbient.rgb;
    #endif
  #endif
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT) && defined(USE_PBR)
	var_Color.rgb *= var_Color.rgb;
#endif

#if defined(USE_PRIMARY_LIGHT) || defined(USE_SHADOWMAP)
	var_PrimaryLightDir.xyz = u_PrimaryLightOrigin.xyz - (position * u_PrimaryLightOrigin.w);
	var_PrimaryLightDir.w = u_PrimaryLightRadius * u_PrimaryLightRadius;
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
  #if defined(USE_LIGHT_VECTOR)
	var_LightDir = vec4(L, u_LightRadius * u_LightRadius);
  #else
	var_LightDir = vec4(L, 0.0);
  #endif
  #if defined(USE_DELUXEMAP)
	var_LightDir -= u_EnableTextures.y * var_LightDir;
  #endif
#endif

#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	vec3 viewDir = u_ViewOrigin - position;
	// store view direction in tangent space to save on varyings
	var_Normal    = vec4(normal,    viewDir.x);
	var_Tangent   = vec4(tangent,   viewDir.y);
	var_Bitangent = vec4(bitangent, viewDir.z);
#elif defined(USE_WORLD_RELIEF)
	vec3 relN = (dot(normal, normal) > 1e-6) ? normalize(normal) : vec3(0.0, 0.0, 1.0);
	float distFade = clamp(1.0 - max(gl_Position.w - 140.0, 0.0) * 0.0016, 0.0, 1.0);
	float relScale = u_WorldReliefLight.w * distFade;

	// Project world light and Blinn-Phong half vector onto the surface tangent plane per-vertex
	vec3 Ltan = u_WorldReliefLight.xyz - relN * dot(u_WorldReliefLight.xyz, relN);
	vec2 scrL = vec2(dot(u_WorldReliefRight, Ltan), dot(u_WorldReliefUp, Ltan));

	vec3 relV = normalize(u_ViewOrigin - position);
	vec3 relH = normalize(u_WorldReliefLight.xyz + relV);
	vec3 Htan = relH - relN * dot(relH, relN);
	vec2 scrH = vec2(dot(u_WorldReliefRight, Htan), dot(u_WorldReliefUp, Htan));
	float oneMinusNV = 1.0 - max(dot(relN, relV), 0.0);
	float fresnel = oneMinusNV * oneMinusNV * oneMinusNV * distFade;

	var_ReliefLight = vec4(scrL * (relScale * 1.55), max(dot(relN, relH), 0.0), distFade);
	var_ReliefHalf  = vec3(scrH * (relScale * 0.58), fresnel);
#endif

#if defined(USE_SUN_SHADOW)
	vec3 shN = (dot(normal, normal) > 1e-6) ? normalize(normal) : vec3(0.0, 0.0, 1.0);
	float sunNL = max(dot(shN, u_SunShadowDir.xyz), 0.0);
	float slopeBias = u_SunShadowDir.w * (0.45 + 0.85 * (1.0 - sunNL));
	var_SunShadowPos = vec4((u_SunShadowMvp * vec4(position + shN * slopeBias, 1.0)).xyz, sunNL);
#endif
}
