uniform sampler2D u_DiffuseMap;

uniform vec4      u_Color;
uniform vec4      u_DlightInfo;
uniform int       u_AlphaTest;
// u_SpecularScale = (modeFlag, specStrength, shinScale, reliefStrength)
uniform vec4      u_SpecularScale;
uniform vec3      u_WorldReliefRight;
uniform vec3      u_WorldReliefUp;

varying vec2      var_Tex1;
varying vec3      var_LightDist;
varying vec3      var_Normal;
varying vec3      var_ViewDir;
varying float     var_FogFactor;


void main()
{
	vec3 toL = var_LightDist;
	float distSq = dot(toL, toL);
	float invR = u_DlightInfo.w;
	if (distSq * (invR * invR) >= 1.0)
		discard;

	vec4 color = texture2D(u_DiffuseMap, var_Tex1);

	float alpha = color.a;
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

	float invDist = inversesqrt(max(distSq, 1.0));
	float normDist = distSq * invDist * invR;
	float atten = 1.0 - normDist;
	vec3 dlight = u_Color.rgb * (atten * atten);
	float fogFade = 1.0 - clamp(var_FogFactor, 0.0, 1.0);

	if (u_SpecularScale.x > 0.5)
	{
		// Evaluate screen-space luminance gradient before normal-based culling
		float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
		float dLx = clamp(dFdx(lum), -0.30, 0.30);
		float dLy = clamp(dFdy(lum), -0.30, 0.30);

		vec3 L = toL * invDist;
		vec3 N = normalize(var_Normal);
		float NdotL = dot(N, L);
		if (NdotL <= -0.06)
			discard;

		// Soft-wrap diffuse + tangent-projected micro-relief perturbation (0 extra texture taps)
		vec3 grad = dLx * u_WorldReliefRight + dLy * u_WorldReliefUp;
		grad -= N * dot(grad, N);
		vec3 Nb = normalize(N - grad * u_SpecularScale.w);

		float baseDiff = clamp((NdotL + 0.08) * 0.926, 0.0, 1.0);
		float bumpDiff = clamp((dot(Nb, L) + 0.08) * 0.926, 0.0, 1.0);
		float diff = mix(baseDiff, bumpDiff, 0.85);

		// Fast algebraic Blinn-Phong specular glint (zero pow())
		vec3 V = normalize(var_ViewDir);
		vec3 H = normalize(L + V);
		float k = max(1.0 - (1.0 - max(dot(Nb, H), 0.0)) * u_SpecularScale.z, 0.0);
		float k2 = k * k;
		float spec = (k2 * k2) * u_SpecularScale.y * min(baseDiff * 4.0, 1.0);
		vec3 specTint = mix(vec3(1.0), color.rgb * 1.4 + vec3(0.25), 0.45);

		// Smooth highlight shoulder preserves warm muzzle-flash / explosion hues
		float maxC = max(dlight.r, max(dlight.g, dlight.b));
		dlight /= (1.0 + maxC * 0.25);

		gl_FragColor.rgb = (color.rgb * diff + specTint * spec) * dlight * fogFade;
	}
	else
	{
		float maxC = max(dlight.r, max(dlight.g, dlight.b));
		if (maxC > 1.0)
		{
			dlight /= maxC;
		}
		gl_FragColor.rgb = color.rgb * dlight * fogFade;
	}

	gl_FragColor.a = alpha;
}
