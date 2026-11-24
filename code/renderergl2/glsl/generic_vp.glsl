attribute vec3 attr_Position;
attribute vec3 attr_Normal;

#if defined(USE_VERTEX_ANIMATION)
attribute vec3 attr_Position2;
attribute vec3 attr_Normal2;
#elif defined(USE_BONE_ANIMATION)
attribute vec4 attr_BoneIndexes;
attribute vec4 attr_BoneWeights;
#endif

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

#if defined(USE_TCGEN) || defined(USE_RGBAGEN)
uniform vec3   u_LocalViewOrigin;
#endif

#if defined(USE_TCGEN)
uniform int    u_TCGen0;
uniform vec3   u_TCGen0Vector0;
uniform vec3   u_TCGen0Vector1;
#endif

#if defined(USE_MULTITEXTURE)
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

#if defined(USE_FOG)
uniform vec4   u_FogDistance;
uniform vec4   u_FogDepth;
uniform float  u_FogEyeT;
uniform vec4   u_FogColorMask;
#endif

#if defined(USE_DEFORM_VERTEXES)
uniform int    u_DeformGen;
uniform float  u_DeformParams[5];
uniform float  u_Time;
#endif

uniform mat4   u_ModelViewProjectionMatrix;
uniform vec4   u_BaseColor;
uniform vec4   u_VertColor;
uniform vec2   u_FarplaneFogParams;
uniform vec4   u_WindParams;

#if defined(USE_RGBAGEN)
uniform int    u_ColorGen;
uniform int    u_AlphaGen;
uniform vec3   u_AmbientLight;
uniform vec3   u_DirectedLight;
uniform vec3   u_ModelLightDir;
uniform float  u_PortalRange;
#endif

#if defined(USE_VERTEX_ANIMATION)
uniform float  u_VertexLerp;
#elif defined(USE_BONE_ANIMATION)
uniform mat4 u_BoneMatrix[MAX_GLSL_BONES];
#endif

varying vec2   var_DiffuseTex;
#if defined(USE_MULTITEXTURE)
varying vec2   var_TexCoord1;
#endif
varying vec4   var_Color;
varying float  var_FogFactor;
#if defined(USE_SOFT_PARTICLES)
varying float  var_ParticleDepth;
#endif

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
	else // if (u_DeformGen <= DGEN_WAVE_INVERSE_SAWTOOTH)
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
	else // if (u_DeformGen == DGEN_BULGE)
	{
		func = sin(value);
	}

	return pos + normal * (base + func * amplitude);
}
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

#if defined(USE_RGBAGEN)
vec4 CalcColor(vec3 position, vec3 normal)
{
	vec4 color = u_VertColor * attr_Color + u_BaseColor;
	
	if (u_ColorGen == CGEN_LIGHTING_DIFFUSE)
	{
		float incoming = clamp(dot(normal, u_ModelLightDir), 0.0, 1.0);

		color.rgb = clamp(u_DirectedLight * incoming + u_AmbientLight, 0.0, 1.0);
	}
	
	vec3 viewer = u_LocalViewOrigin - position;

	if (u_AlphaGen == AGEN_LIGHTING_SPECULAR)
	{
		vec3 lightDir = normalize(vec3(-960.0, 1980.0, 96.0) - position);
		vec3 reflected = -reflect(lightDir, normal);
		
		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_PORTAL)
	{
		color.a = clamp(length(viewer) / u_PortalRange, 0.0, 1.0);
	}
	
	return color;
}
#endif

#if defined(USE_FOG)
float CalcFog(vec3 position)
{
	float s = dot(vec4(position, 1.0), u_FogDistance) * 8.0;
	float t = dot(vec4(position, 1.0), u_FogDepth);

	float eyeOutside = float(u_FogEyeT < 0.0);
	float fogged = float(t >= eyeOutside);

	t += 1e-6;
	t *= fogged / (t - u_FogEyeT * eyeOutside);

	return s * t;
}
#endif

void main()
{
#if defined(USE_VERTEX_ANIMATION)
	vec3 position  = mix(attr_Position, attr_Position2, u_VertexLerp);
	vec3 normal    = mix(attr_Normal,   attr_Normal2,   u_VertexLerp);
#elif defined(USE_BONE_ANIMATION)
	mat4 vtxMat  = u_BoneMatrix[int(attr_BoneIndexes.x)] * attr_BoneWeights.x;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.y)] * attr_BoneWeights.y;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.z)] * attr_BoneWeights.z;
	     vtxMat += u_BoneMatrix[int(attr_BoneIndexes.w)] * attr_BoneWeights.w;
	mat3 nrmMat = mat3(cross(vtxMat[1].xyz, vtxMat[2].xyz), cross(vtxMat[2].xyz, vtxMat[0].xyz), cross(vtxMat[0].xyz, vtxMat[1].xyz));

	vec3 position  = vec3(vtxMat * vec4(attr_Position, 1.0));
	vec3 normal    = normalize(nrmMat * attr_Normal);
#else
	vec3 position  = attr_Position;
	vec3 normal    = attr_Normal;
#endif

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
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
#if defined(USE_SOFT_PARTICLES)
	var_ParticleDepth = gl_Position.w;
#endif

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

#if defined(USE_MULTITEXTURE)
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

#if defined(USE_RGBAGEN)
	var_Color = CalcColor(position, normal);
#else
	var_Color = u_VertColor * attr_Color + u_BaseColor;
#endif

#if defined(USE_FOG)
	var_Color *= vec4(1.0) - u_FogColorMask * sqrt(clamp(CalcFog(position), 0.0, 1.0));
#endif
}
