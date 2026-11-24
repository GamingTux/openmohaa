attribute vec3 attr_Position;
attribute vec3 attr_Normal;
attribute vec4 attr_Color;
attribute vec4 attr_TexCoord0;
#if defined(USE_TCGEN)
attribute vec4 attr_TexCoord1;
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

#if defined(USE_TCGEN)
uniform int    u_TCGen0;
uniform vec3   u_TCGen0Vector0;
uniform vec3   u_TCGen0Vector1;
#endif

uniform mat4   u_ModelViewProjectionMatrix;
uniform vec3   u_LocalViewOrigin;
uniform vec4   u_BaseColor;
uniform vec4   u_VertColor;
uniform vec2   u_FarplaneFogParams;
uniform vec4   u_WindParams;

// Dominant light 0 evaluated per-pixel (direction/half-vector precomputed here to eliminate FP SFU stalls).
// Secondary fill lights (2nd, 3rd & 4th) evaluated per-vertex to keep fragment shader ultra-lean on V3D 7.1.
uniform int    u_ModelLightCount;
uniform vec4   u_ModelLightPos0;
uniform vec4   u_ModelLightPos1;
uniform vec4   u_ModelLightPos2;
uniform vec4   u_ModelLightPos3;
uniform vec4   u_ModelLightColor1;
uniform vec4   u_ModelLightColor2;
uniform vec4   u_ModelLightColor3;

#if defined(USE_SUN_SHADOW)
uniform mat4   u_SunShadowMvp;
uniform vec4   u_SunShadowDir;
#endif

varying vec2   var_DiffuseTex;
varying vec4   var_Color;
varying vec3   var_LocalNormal;
varying vec4   var_Light0;
#if defined(USE_SPECULAR)
varying vec4   var_ViewFog;
varying vec3   var_Half0;
#else
varying float  var_FogFactor;
#endif
#if defined(USE_SUN_SHADOW)
varying vec4   var_SunShadowPos;
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

void main()
{
	vec3 position = attr_Position;
	vec3 normal   = attr_Normal;

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

	gl_Position     = u_ModelViewProjectionMatrix * vec4(position, 1.0);
	float fogFactor = (gl_Position.w - u_FarplaneFogParams.x) * u_FarplaneFogParams.y;

#if defined(USE_TCGEN)
	vec2 tex = GenTexCoords(u_TCGen0, position, normal, u_TCGen0Vector0, u_TCGen0Vector1);
#else
	vec2 tex = attr_TexCoord0.st;
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
	var_DiffuseTex = ModTexCoords(tex, position, diffuseTexMatrix);
#else
	var_DiffuseTex = tex;
#endif

	vec3 vN = (dot(normal, normal) > 1e-6) ? normalize(normal) : vec3(0.0, 0.0, 1.0);
	vec4 baseCol = u_VertColor * attr_Color + u_BaseColor;

	// Precompute dominant Light 0 direction + attenuation once per vertex
	vec3  toL0  = u_ModelLightPos0.xyz - position * u_ModelLightPos0.w;
	float invD0 = inversesqrt(max(dot(toL0, toL0), 1.0));
	vec3  L0    = toL0 * invD0;
	float att0  = mix(1.0, invD0, u_ModelLightPos0.w);

	// Evaluate secondary fill lights (1..3) per-vertex for full multi-light fill at 0 FP cost
	if (u_ModelLightCount > 1)
	{
		vec3  toL1  = u_ModelLightPos1.xyz - position * u_ModelLightPos1.w;
		float invD1 = inversesqrt(max(dot(toL1, toL1), 1.0));
		float att1  = mix(1.0, invD1, u_ModelLightPos1.w);
		baseCol.rgb += u_ModelLightColor1.rgb * (att1 * max(dot(vN, toL1 * invD1), 0.0));

		if (u_ModelLightCount > 2)
		{
			vec3  toL2  = u_ModelLightPos2.xyz - position * u_ModelLightPos2.w;
			float invD2 = inversesqrt(max(dot(toL2, toL2), 1.0));
			float att2  = mix(1.0, invD2, u_ModelLightPos2.w);
			baseCol.rgb += u_ModelLightColor2.rgb * (att2 * max(dot(vN, toL2 * invD2), 0.0));

			if (u_ModelLightCount > 3)
			{
				vec3  toL3  = u_ModelLightPos3.xyz - position * u_ModelLightPos3.w;
				float invD3 = inversesqrt(max(dot(toL3, toL3), 1.0));
				float att3  = mix(1.0, invD3, u_ModelLightPos3.w);
				baseCol.rgb += u_ModelLightColor3.rgb * (att3 * max(dot(vN, toL3 * invD3), 0.0));
			}
		}
	}

	var_Color       = baseCol;
	var_LocalNormal = vN;
	var_Light0      = vec4(L0, att0);

#if defined(USE_SPECULAR)
	vec3 V = normalize(u_LocalViewOrigin - position);
	var_ViewFog = vec4(V, fogFactor);
	var_Half0   = normalize(L0 + V);
#else
	var_FogFactor = fogFactor;
#endif

#if defined(USE_SUN_SHADOW)
	float sunNL = max(dot(vN, u_SunShadowDir.xyz), 0.0);
	float slopeBias = u_SunShadowDir.w * (0.45 + 0.85 * (1.0 - sunNL));
	var_SunShadowPos = vec4((u_SunShadowMvp * vec4(position + vN * slopeBias, 1.0)).xyz, sunNL);
#endif
}
