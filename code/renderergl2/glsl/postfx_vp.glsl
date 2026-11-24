attribute vec3 attr_Position;
attribute vec4 attr_TexCoord0;

uniform mat4   u_ModelViewProjectionMatrix;

varying vec2   var_TexCoords;
#if defined(POSTFX_COMBINE)
varying vec2   var_BloomCoords;
#endif

void main()
{
	gl_Position = u_ModelViewProjectionMatrix * vec4(attr_Position, 1.0);
	var_TexCoords = attr_TexCoord0.st;
#if defined(POSTFX_COMBINE)
	// Maps output quad clip coords cleanly to 0..1 across the bloom FBO regardless of r_renderScale
	var_BloomCoords = gl_Position.xy * 0.5 + vec2(0.5);
#endif
}
