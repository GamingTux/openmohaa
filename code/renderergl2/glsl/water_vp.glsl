attribute vec3 attr_Position;
attribute vec3 attr_Normal;
attribute vec4 attr_Color;
attribute vec4 attr_TexCoord0;
attribute vec4 attr_TexCoord1;

uniform vec4   u_DiffuseTexMatrix0;
uniform vec4   u_DiffuseTexMatrix1;
uniform vec4   u_DiffuseTexMatrix2;
uniform vec4   u_DiffuseTexMatrix3;
uniform vec4   u_DiffuseTexMatrix4;
uniform vec4   u_DiffuseTexMatrix5;
uniform vec4   u_DiffuseTexMatrix6;
uniform vec4   u_DiffuseTexMatrix7;

uniform vec3   u_LocalViewOrigin;

uniform int    u_TCGen0;
uniform vec3   u_TCGen0Vector0;
uniform vec3   u_TCGen0Vector1;

#if defined(USE_MULTITEXTURE) || defined(USE_LIGHTMAP)
uniform int    u_TCGen1;
uniform vec3   u_TCGen1Vector0;
uniform vec3   u_TCGen1Vector1;
uniform vec4   u_Bundle1TexMatrix0;
uniform vec4   u_Bundle1TexMatrix1;
uniform vec4   u_Bundle1TexMatrix2;
uniform vec4   u_Bundle1TexMatrix3;
uniform vec4   u_Bundle1TexMatrix4;
uniform vec4   u_Bundle1TexMatrix5;
uniform vec4   u_Bundle1TexMatrix6;
uniform vec4   u_Bundle1TexMatrix7;
#endif

#if defined(USE_DEFORM_VERTEXES)
uniform int    u_DeformGen;
uniform float  u_DeformParams[5];
#endif

uniform float  u_Time;
uniform vec4   u_WaterParams;
uniform mat4   u_ModelMatrix;
uniform mat4   u_ModelViewProjectionMatrix;
uniform vec4   u_BaseColor;
uniform vec4   u_VertColor;
uniform vec2   u_FarplaneFogParams;

uniform int    u_ColorGen;
uniform int    u_AlphaGen;
uniform float  u_PortalRange;

varying vec2   var_DiffuseTex;
#if defined(USE_MULTITEXTURE) || defined(USE_LIGHTMAP)
varying vec2   var_TexCoord1;
#endif
varying vec4   var_Color;
varying vec4   var_WorldPosFog;
varying vec3   var_WorldNormal;
varying vec3   var_WavePhases;

#if defined(USE_DEFORM_VERTEXES)
vec3 DeformPosition(const vec3 pos, const vec3 normal, const vec2 st)
{
	float base =      u_DeformParams[0];
	float amplitude = u_DeformParams[1];
	float phase =     u_DeformParams[2];
	float frequency = u_DeformParams[3];
	float spread =    u_DeformParams[4];

	if (u_DeformGen == DGEN_BULGE)
	{
		phase *= st.x;
	}
	else
	{
		phase += dot(pos.xyz, vec3(spread));
	}

	float value = phase + (u_Time * frequency);
	float func;

	if (u_DeformGen == DGEN_WAVE_SIN)
	{
		func = sin(value * 2.0 * M_PI);
	}
	else if (u_DeformGen == DGEN_WAVE_SQUARE)
	{
		func = sign(fract(0.5 - value));
	}
	else if (u_DeformGen == DGEN_WAVE_TRIANGLE)
	{
		func = abs(fract(value + 0.75) - 0.5) * 4.0 - 1.0;
	}
	else if (u_DeformGen == DGEN_WAVE_SAWTOOTH)
	{
		func = fract(value);
	}
	else if (u_DeformGen == DGEN_WAVE_INVERSE_SAWTOOTH)
	{
		func = (1.0 - fract(value));
	}
	else
	{
		func = sin(value);
	}

	return pos + normal * (base + func * amplitude);
}
#endif

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
	else if (TCGen == TCGEN_VECTOR)
	{
		tex = vec2(dot(position, TCGenVector0), dot(position, TCGenVector1));
	}

	return tex;
}

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

void main()
{
	vec3 position = attr_Position;
	vec3 normal   = attr_Normal;

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
#endif

	// Exact same clip-space transform as generic_vp.glsl to guarantee depthFunc equal match
	// with subsequent lightmap/additive stages on river/pond/canal water.
	gl_Position = u_ModelViewProjectionMatrix * vec4(position, 1.0);
	float fogFactor = (gl_Position.w - u_FarplaneFogParams.x) * u_FarplaneFogParams.y;

	vec3 worldPos = (u_ModelMatrix * vec4(position, 1.0)).xyz;
	var_WorldPosFog = vec4(worldPos, fogFactor);
	vec3 wn = (u_ModelMatrix * vec4(normal, 0.0)).xyz;
	var_WorldNormal = (dot(wn, wn) > 1e-6) ? normalize(wn) : vec3(0.0, 0.0, 1.0);

	// Precompute exact affine linear wave phases per vertex (eliminates 3x dot + 3x mad per pixel!)
	vec2 wp = worldPos.xy * u_WaterParams.x;
	float t = u_Time * u_WaterParams.y;
	var_WavePhases = vec3(
		dot(wp, vec2( 0.74,  0.43)) + t * 0.88,
		dot(wp, vec2(-0.46,  1.06)) + t * 1.24,
		dot(wp, vec2( 1.66, -1.24)) - t * 1.82
	);

	vec2 tex = GenTexCoords(u_TCGen0, position, normal, u_TCGen0Vector0, u_TCGen0Vector1);
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

#if defined(USE_MULTITEXTURE) || defined(USE_LIGHTMAP)
	vec2 tex1 = GenTexCoords(u_TCGen1, position, normal, u_TCGen1Vector0, u_TCGen1Vector1);
	vec4 bundle1TexMatrix[8];
	bundle1TexMatrix[0] = u_Bundle1TexMatrix0;
	bundle1TexMatrix[1] = u_Bundle1TexMatrix1;
	bundle1TexMatrix[2] = u_Bundle1TexMatrix2;
	bundle1TexMatrix[3] = u_Bundle1TexMatrix3;
	bundle1TexMatrix[4] = u_Bundle1TexMatrix4;
	bundle1TexMatrix[5] = u_Bundle1TexMatrix5;
	bundle1TexMatrix[6] = u_Bundle1TexMatrix6;
	bundle1TexMatrix[7] = u_Bundle1TexMatrix7;
	var_TexCoord1 = ModTexCoords(tex1, position, bundle1TexMatrix);
#endif

	vec4 color = u_VertColor * attr_Color + u_BaseColor;
	if (u_AlphaGen == AGEN_LIGHTING_SPECULAR)
	{
		vec3 viewer = u_LocalViewOrigin - position;
		vec3 lightDir = normalize(vec3(-960.0, 1980.0, 96.0) - position);
		vec3 reflected = -reflect(lightDir, normal);
		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_PORTAL)
	{
		vec3 viewer = u_LocalViewOrigin - position;
		color.a = clamp(length(viewer) / u_PortalRange, 0.0, 1.0);
	}
	var_Color = color;
}
