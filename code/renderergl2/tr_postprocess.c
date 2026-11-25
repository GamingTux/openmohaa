/*
===========================================================================
Copyright (C) 2011 Andrei Drexler, Richard Allen, James Canete

This file is part of Reaction source code.

Reaction source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Reaction source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Reaction source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

void RB_ToneMap(FBO_t *hdrFbo, ivec4_t hdrBox, FBO_t *ldrFbo, ivec4_t ldrBox, int autoExposure)
{
	ivec4_t srcBox, dstBox;
	vec4_t color;
	static int lastFrameCount = 0;

	if (autoExposure)
	{
		if (lastFrameCount == 0 || tr.frameCount < lastFrameCount || tr.frameCount - lastFrameCount > 5)
		{
			// determine average log luminance
			FBO_t *srcFbo, *dstFbo, *tmp;
			int size = 256;

			lastFrameCount = tr.frameCount;

			VectorSet4(dstBox, 0, 0, size, size);

			FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

			srcFbo = tr.textureScratchFbo[0];
			dstFbo = tr.textureScratchFbo[1];

			// downscale to 1x1 texture
			while (size > 1)
			{
				VectorSet4(srcBox, 0, 0, size, size);
				//size >>= 2;
				size >>= 1;
				VectorSet4(dstBox, 0, 0, size, size);

				if (size == 1)
					dstFbo = tr.targetLevelsFbo;

				//FBO_Blit(targetFbo, srcBox, NULL, tr.textureScratchFbo[nextScratch], dstBox, &tr.calclevels4xShader[1], NULL, 0);
				FBO_FastBlit(srcFbo, srcBox, dstFbo, dstBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);

				tmp = srcFbo;
				srcFbo = dstFbo;
				dstFbo = tmp;
			}
		}

		// blend with old log luminance for gradual change
		VectorSet4(srcBox, 0, 0, 0, 0);

		color[0] = 
		color[1] =
		color[2] = 1.0f;
		if (glRefConfig.textureFloat)
			color[3] = 0.03f;
		else
			color[3] = 0.1f;

		FBO_Blit(tr.targetLevelsFbo, srcBox, NULL, tr.calcLevelsFbo, NULL,  NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}

	// tonemap
	color[0] =
	color[1] =
	color[2] = pow(2, r_cameraExposure->value - autoExposure); //exp2(r_cameraExposure->value);
	color[3] = 1.0f;

	if (autoExposure)
		GL_BindToTMU(tr.calcLevelsImage,  TB_LEVELSMAP);
	else
		GL_BindToTMU(tr.fixedLevelsImage, TB_LEVELSMAP);

	FBO_Blit(hdrFbo, hdrBox, NULL, ldrFbo, ldrBox, &tr.tonemapShader, color, 0);
}

/*
=============
RB_BokehBlur


Blurs a part of one framebuffer to another.

Framebuffers can be identical. 
=============
*/
void RB_BokehBlur(FBO_t *src, ivec4_t srcBox, FBO_t *dst, ivec4_t dstBox, float blur)
{
//	ivec4_t srcBox, dstBox;
	vec4_t color;
	
	blur *= 10.0f;

	if (blur < 0.004f)
		return;

	if (glRefConfig.framebufferObject)
	{
		// bokeh blur
		if (blur > 0.0f)
		{
			ivec4_t quarterBox;

			quarterBox[0] = 0;
			quarterBox[1] = tr.quarterFbo[0]->height;
			quarterBox[2] = tr.quarterFbo[0]->width;
			quarterBox[3] = -tr.quarterFbo[0]->height;

			// create a quarter texture
			//FBO_Blit(NULL, NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(src, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

#ifndef HQ_BLUR
		if (blur > 1.0f)
		{
			// create a 1/16th texture
			//FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
#endif

		if (blur > 0.0f && blur <= 1.0f)
		{
			// Crossfade original with quarter texture
			VectorSet4(color, 1, 1, 1, blur);

			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
#ifndef HQ_BLUR
		// ok blur, but can see some pixelization
		else if (blur > 1.0f && blur <= 2.0f)
		{
			// crossfade quarter texture with 1/16th texture
			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, NULL, 0);

			VectorSet4(color, 1, 1, 1, blur - 1.0f);

			FBO_Blit(tr.textureScratchFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
		else if (blur > 2.0f)
		{
			// blur 1/16th texture then replace
			int i;

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = ((blur - 2.0f) / 2.0f) / 3.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 0.5f;
				color[3] = 1.0f;

				if (i != 0)
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
				else
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, 0);
			}

			FBO_Blit(tr.textureScratchFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#else // higher quality blur, but slower
		else if (blur > 1.0f)
		{
			// blur quarter texture then replace
			int i;

			src = tr.quarterFbo[0];
			dst = tr.quarterFbo[1];

			VectorSet4(color, 0.5f, 0.5f, 0.5f, 1);

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = (blur - 1.0f) / 2.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 1.0f;
				if (i != 0)
					color[3] = 1.0f;
				else
					color[3] = 0.5f;

				FBO_Blit(tr.quarterFbo[0], NULL, blurTexScale, tr.quarterFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
			}

			FBO_Blit(tr.quarterFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#endif
	}
}


static void RB_RadialBlur(FBO_t *srcFbo, FBO_t *dstFbo, int passes, float stretch, float x, float y, float w, float h, float xcenter, float ycenter, float alpha)
{
	ivec4_t srcBox, dstBox;
	int srcWidth, srcHeight;
	vec4_t color;
	const float inc = 1.f / passes;
	const float mul = powf(stretch, inc);
	float scale;

	alpha *= inc;
	VectorSet4(color, alpha, alpha, alpha, 1.0f);

	srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
	srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

	VectorSet4(srcBox, 0, 0, srcWidth, srcHeight);

	VectorSet4(dstBox, x, y, w, h);
	FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

	--passes;
	scale = mul;
	while (passes > 0)
	{
		float iscale = 1.f / scale;
		float s0 = xcenter * (1.f - iscale);
		float t0 = (1.0f - ycenter) * (1.f - iscale);

		srcBox[0] = s0 * srcWidth;
		srcBox[1] = t0 * srcHeight;
		srcBox[2] = iscale * srcWidth;
		srcBox[3] = iscale * srcHeight;
			
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

		scale *= mul;
		--passes;
	}
}


static qboolean RB_UpdateSunFlareVis(void)
{
	GLuint sampleCount = 0;
	if (!glRefConfig.occlusionQuery)
		return qtrue;

	tr.sunFlareQueryIndex ^= 1;
	if (!tr.sunFlareQueryActive[tr.sunFlareQueryIndex])
		return qtrue;

	/* debug code */
	if (0)
	{
		int iter;
		for (iter=0 ; ; ++iter)
		{
			GLint available = 0;
			qglGetQueryObjectiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available)
				break;
		}

		ri.Printf(PRINT_DEVELOPER, "Waited %d iterations\n", iter);
	}
	
	// Note: On desktop OpenGL this is a sample count (glRefConfig.occlusionQueryTarget == GL_SAMPLES_PASSED)
	// but on OpenGL ES this is a boolean (glRefConfig.occlusionQueryTarget == GL_ANY_SAMPLES_PASSED)
	qglGetQueryObjectuiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT, &sampleCount);
	return sampleCount > 0;
}

void RB_SunRays(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox)
{
	vec4_t color;
	float dot;
	const float cutoff = 0.25f;
	qboolean colorize = qtrue;

//	float w, h, w2, h2;
	mat4_t mvp;
	vec4_t pos, hpos;

	dot = DotProduct(tr.sunDirection, backEnd.viewParms.ori.axis[0]);
	if (dot < cutoff)
		return;

	if (!RB_UpdateSunFlareVis())
		return;

	// From RB_DrawSun()
	{
		float dist;
		mat4_t trans, model;

		Mat4Translation( backEnd.viewParms.ori.origin, trans );
		Mat4Multiply( backEnd.viewParms.world.modelMatrix, trans, model );
		Mat4Multiply(backEnd.viewParms.projectionMatrix, model, mvp);

		dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)

		VectorScale( tr.sunDirection, dist, pos );
	}

	// project sun point
	//Mat4Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelMatrix, mvp);
	Mat4Transform(mvp, pos, hpos);

	// transform to UV coords
	hpos[3] = 0.5f / hpos[3];

	pos[0] = 0.5f + hpos[0] * hpos[3];
	pos[1] = 0.5f + hpos[1] * hpos[3];

	// initialize quarter buffers
	{
		float mul = 1.f;
		ivec4_t rayBox, quarterBox;
		int srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
		int srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

		VectorSet4(color, mul, mul, mul, 1);

		rayBox[0] = srcBox[0] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / srcHeight;
		rayBox[2] = srcBox[2] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / srcHeight;

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// first, downsample the framebuffer
		if (colorize)
		{
			FBO_FastBlit(srcFbo, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			FBO_Blit(tr.sunRaysFbo, rayBox, NULL, tr.quarterFbo[0], quarterBox, NULL, color, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		}
		else
		{
			FBO_FastBlit(tr.sunRaysFbo, rayBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

	// radial blur passes, ping-ponging between the two quarter-size buffers
	{
		const float stretch_add = 2.f/3.f;
		float stretch = 1.f + stretch_add;
		int i;
		for (i=0; i<2; ++i)
		{
			RB_RadialBlur(tr.quarterFbo[i&1], tr.quarterFbo[(~i) & 1], 5, stretch, 0.f, 0.f, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height, pos[0], pos[1], 1.125f);
			stretch += stretch_add;
		}
	}
	
	// add result back on top of the main buffer
	{
		float mul = 1.f;

		VectorSet4(color, mul, mul, mul, 1);

		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_BlurAxis(FBO_t *srcFbo, FBO_t *dstFbo, float strength, qboolean horizontal)
{
	float dx, dy;
	float xmul, ymul;
	float weights[3] = {
		0.227027027f,
		0.316216216f,
		0.070270270f,
	};
	float offsets[3] = {
		0.f,
		1.3846153846f,
		3.2307692308f,
	};

	xmul = horizontal;
	ymul = 1.f - xmul;

	xmul *= strength;
	ymul *= strength;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, weights[0], weights[0], weights[0], 1.0f);
		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, 0, 0, dstFbo->width, dstFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

		VectorSet4(color, weights[1], weights[1], weights[1], 1.0f);
		dx = offsets[1] * xmul;
		dy = offsets[1] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

		VectorSet4(color, weights[2], weights[2], weights[2], 1.0f);
		dx = offsets[2] * xmul;
		dy = offsets[2] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_HBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qtrue);
}

static void RB_VBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qfalse);
}

void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur)
{
	//float mul = 1.f;
	float factor = Com_Clamp(0.f, 1.f, blur);

	if (factor <= 0.f)
		return;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, 1, 1, 1, 1);

		// first, downsample the framebuffer
		FBO_FastBlit(srcFbo, NULL, tr.quarterFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// set the alpha channel
		qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		FBO_BlitFromTexture(tr.whiteImage, NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, color, GLS_DEPTHTEST_DISABLE);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// blur the tiny buffer horizontally and vertically
		RB_HBlur(tr.textureScratchFbo[0], tr.textureScratchFbo[1], factor);
		RB_VBlur(tr.textureScratchFbo[1], tr.textureScratchFbo[0], factor);

		// finally, merge back to framebuffer
		VectorSet4(srcBox, 0, 0, tr.textureScratchFbo[0]->width, tr.textureScratchFbo[0]->height);
		VectorSet4(dstBox, 0, 0, glConfig.vidWidth,              glConfig.vidHeight);
		color[3] = factor;
		FBO_Blit(tr.textureScratchFbo[0], srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
}

static void RB_GetMapColorGrade(vec4_t grade)
{
	// Neutral identity: (tintR, tintG, tintB, saturation)
	VectorSet4(grade, 1.0f, 1.0f, 1.0f, 1.0f);

	if (!r_mapAtmosphere || r_mapAtmosphere->integer <= 0 || !tr.world || !tr.world->baseName[0])
		return;

	const char *m = tr.world->baseName;

	// Mission 1: North Africa (Arzew night stealth vs golden desert)
	if (!Q_stricmpn(m, "m1l1", 4) || !Q_stricmpn(m, "m1l2b", 5)) {
		VectorSet4(grade, 0.97f, 0.99f, 1.05f, 1.06f);
	} else if (!Q_stricmpn(m, "m1", 2)) {
		VectorSet4(grade, 1.05f, 1.01f, 0.94f, 1.07f);
	}
	// Mission 2: Norway / Trondheim U-Boot Pen (cold steel industrial)
	else if (!Q_stricmpn(m, "m2", 2)) {
		VectorSet4(grade, 0.95f, 1.00f, 1.05f, 0.96f);
	}
	// Mission 3: Omaha Beach (Saving Private Ryan desaturated coastal mist) & Bocage
	else if (!Q_stricmpn(m, "m3l1", 4)) {
		VectorSet4(grade, 0.98f, 1.00f, 1.03f, 0.89f);
	} else if (!Q_stricmpn(m, "m3", 2)) {
		VectorSet4(grade, 1.02f, 1.02f, 0.96f, 1.04f);
	}
	// Mission 4: Behind Enemy Lines (moody French countryside dusk)
	else if (!Q_stricmpn(m, "m4", 2)) {
		VectorSet4(grade, 1.03f, 0.99f, 0.96f, 1.02f);
	}
	// Mission 5: Day of the Tiger (crisp snowy winter & Sniper's Last Stand)
	else if (!Q_stricmpn(m, "m5", 2)) {
		VectorSet4(grade, 0.95f, 0.99f, 1.06f, 0.94f);
	}
	// Mission 6: Return to Schmerzen (gritty dramatic late-war contrast)
	else if (!Q_stricmpn(m, "m6", 2)) {
		VectorSet4(grade, 1.01f, 0.99f, 0.97f, 0.95f);
	}
	// Multiplayer favorites
	else if (Q_stristr(m, "mohdm1") || Q_stristr(m, "mohdm4")) {
		VectorSet4(grade, 1.04f, 1.01f, 0.95f, 1.05f);
	} else if (Q_stristr(m, "mohdm5")) {
		VectorSet4(grade, 0.95f, 0.99f, 1.06f, 0.94f);
	} else if (Q_stristr(m, "mohdm3") || Q_stristr(m, "mohdm7")) {
		VectorSet4(grade, 0.98f, 1.00f, 1.04f, 1.03f);
	} else {
		// Subtle default cinematic polish for training / obj / custom maps
		VectorSet4(grade, 1.01f, 1.01f, 0.99f, 1.03f);
	}
}

static void RB_ComputeMotionBlurParams(const vec4_t srcUvBounds, vec4_t outParams, float *outBlurRatio)
{
	static qboolean s_hasPrev = qfalse;
	static vec3_t   s_prevFwd = { 1.0f, 0.0f, 0.0f };
	static vec3_t   s_prevOrg = { 0.0f, 0.0f, 0.0f };
	static double   s_prevTime = 0.0;
	static float    s_smoothU = 0.0f;
	static float    s_smoothV = 0.0f;

	float strength = r_motionBlur ? Com_Clamp(0.0f, 2.0f, r_motionBlur->value) : 0.0f;
	float spanU = MAX(0.001f, srcUvBounds[2] - srcUvBounds[0]);
	float spanV = MAX(0.001f, srcUvBounds[3] - srcUvBounds[1]);

	VectorSet4(outParams, 0.0f, 0.0f, 2.0f / spanU, 2.0f / spanV);
	if (outBlurRatio)
		*outBlurRatio = 0.0f;

	if (strength <= 0.001f || (backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HUD)))
	{
		s_hasPrev = qfalse;
		s_smoothU = 0.0f;
		s_smoothV = 0.0f;
		return;
	}

	{
		vec3_t curFwd, curLeft, curUp, curOrg;
		double nowTime = backEnd.refdef.floatTime;
		double dt = nowTime - s_prevTime;
		float rawU = 0.0f, rawV = 0.0f;

		VectorCopy(backEnd.refdef.viewaxis[0], curFwd);
		VectorCopy(backEnd.refdef.viewaxis[1], curLeft);
		VectorCopy(backEnd.refdef.viewaxis[2], curUp);
		VectorCopy(backEnd.refdef.vieworg, curOrg);

		if (s_hasPrev && dt > 0.0005 && dt < 0.20)
		{
			vec3_t deltaOrg;
			float fwdDot = DotProduct(s_prevFwd, curFwd);
			float moveSq;

			VectorSubtract(curOrg, s_prevOrg, deltaOrg);
			moveSq = DotProduct(deltaOrg, deltaOrg);

			// Reject teleports, level loads, respawns, and abrupt cutscene camera cuts
			if (fwdDot > 0.80f && moveSq < (256.0f * 256.0f))
			{
				float fovX = Com_Clamp(20.0f, 130.0f, backEnd.refdef.fov_x > 1.0f ? backEnd.refdef.fov_x : 80.0f);
				float fovY = Com_Clamp(15.0f, 110.0f, backEnd.refdef.fov_y > 1.0f ? backEnd.refdef.fov_y : 60.0f);
				float tanHalfX = tanf(DEG2RAD(fovX) * 0.5f);
				float tanHalfY = tanf(DEG2RAD(fovY) * 0.5f);
				float dLeft = DotProduct(s_prevFwd, curLeft);
				float dUp   = DotProduct(s_prevFwd, curUp);

				// Keep sniper scopes & binoculars razor sharp
				float zoomFade = Com_Clamp(0.0f, 1.0f, (fovX - 35.0f) / 25.0f);
				// Normalize toward a 60 Hz shutter so lower framerates don't over-blur
				float fpsNorm  = Com_Clamp(0.40f, 1.15f, (float)(0.01667 / dt));
				float shutter  = 0.20f * strength * zoomFade * fpsNorm;

				rawU = -(dLeft / (2.0f * MAX(0.1f, tanHalfX))) * shutter;
				rawV =  (dUp   / (2.0f * MAX(0.1f, tanHalfY))) * shutter;
			}
			else
			{
				s_smoothU = 0.0f;
				s_smoothV = 0.0f;
			}
		}
		else
		{
			// Pause menu / first frame / long hitch: clear immediately so paused frames never stay blurred
			s_smoothU = 0.0f;
			s_smoothV = 0.0f;
		}

		VectorCopy(curFwd, s_prevFwd);
		VectorCopy(curOrg, s_prevOrg);
		s_prevTime = nowTime;
		s_hasPrev = qtrue;

		// Asymmetric smoothing: silky ramp-up, crisp stop when camera halts
		{
			float rawMag = sqrtf(rawU * rawU + rawV * rawV);
			float oldMag = sqrtf(s_smoothU * s_smoothU + s_smoothV * s_smoothV);
			float alpha  = (rawMag >= oldMag) ? 0.58f : 0.72f;
			float mag;
			float maxRad = 0.0060f * Com_Clamp(0.5f, 1.6f, strength);

			s_smoothU = s_smoothU * (1.0f - alpha) + rawU * alpha;
			s_smoothV = s_smoothV * (1.0f - alpha) + rawV * alpha;

			mag = sqrtf(s_smoothU * s_smoothU + s_smoothV * s_smoothV);

			// Deadzone: exact 0.0 when standing still or micro-aiming -> triggers GPU early-out
			if (mag < 0.00045f)
			{
				s_smoothU = 0.0f;
				s_smoothV = 0.0f;
				return;
			}

			// Soft ceiling so fast flicks stay silky smooth without discrete ghosting
			if (mag > maxRad)
			{
				float s = maxRad / mag;
				s_smoothU *= s;
				s_smoothV *= s;
				mag = maxRad;
			}

			// Scale into active viewport UV sub-rect (matches native 100% and DRS identically)
			outParams[0] = s_smoothU * spanU;
			outParams[1] = s_smoothV * spanV;
			if (outBlurRatio)
				*outBlurRatio = Com_Clamp(0.0f, 1.0f, (mag - 0.00045f) / 0.0038f);
		}
	}
}

void RB_ModernPostProcess(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox)
{
	int bloomMode = (r_bloom && tr.bloomFbo[0] && tr.bloomFbo[1]) ? r_bloom->integer : 0;
	int toneMode  = r_filmicTonemap ? r_filmicTonemap->integer : 0;
	int sharpMode = r_sharpen ? r_sharpen->integer : 0;
	vec4_t combineParams;
	vec4_t srcUvBounds;
	vec4_t motionParams;
	vec2_t sharpParams;
	float toneMix;
	float invSrcW, invSrcH;
	float scaleX, sharpAmt, blurRatio;
	shaderProgram_t *combineProg;

	if (!srcFbo)
		return;

	invSrcW = 1.0f / (float)MAX(1, srcFbo->width);
	invSrcH = 1.0f / (float)MAX(1, srcFbo->height);
	if (srcBox)
	{
		srcUvBounds[0] = ((float)srcBox[0] + 0.5f) * invSrcW;
		srcUvBounds[1] = ((float)srcBox[1] + 0.5f) * invSrcH;
		srcUvBounds[2] = ((float)(srcBox[0] + srcBox[2]) - 0.5f) * invSrcW;
		srcUvBounds[3] = ((float)(srcBox[1] + srcBox[3]) - 0.5f) * invSrcH;
	}
	else
	{
		srcUvBounds[0] = 0.5f * invSrcW;
		srcUvBounds[1] = 0.5f * invSrcH;
		srcUvBounds[2] = 1.0f - 0.5f * invSrcW;
		srcUvBounds[3] = 1.0f - 0.5f * invSrcH;
	}

	if (bloomMode > 0)
	{
		ivec4_t bloomBox;
		vec4_t brightParams;
		vec2_t srcStep = { 1.5f, 1.5f };

		VectorSet4(bloomBox, 0, 0, tr.bloomFbo[0]->width, tr.bloomFbo[0]->height);

		GLSL_BindProgram(&tr.postFxShader[0]);
		GLSL_SetUniformVec4(&tr.postFxShader[0], UNIFORM_POSTFXBOUNDS, srcUvBounds);

		if (bloomMode >= 2)
		{
			vec2_t blurH1 = { 1.20f, 0.0f };
			vec2_t blurV1 = { 0.0f, 1.20f };
			vec2_t blurH2 = { 2.15f, 0.0f };
			vec2_t blurV2 = { 0.0f, 2.15f };

			VectorSet4(brightParams, 0.48f, 1.92f, 0.45f, 1.0f);

			// 6x6 bilinear downsample + soft-quadratic bright-pass into L2-resident 1/6-res bloom FBO
			FBO_Blit(srcFbo, srcBox, srcStep, tr.bloomFbo[0], bloomBox, &tr.postFxShader[0], brightParams, 0);

			// Two wide separable 9-tap Gaussian blurs (5 hardware bilinear taps each) at 1/6 res
			FBO_Blit(tr.bloomFbo[0], bloomBox, blurH1, tr.bloomFbo[1], bloomBox, &tr.postFxShader[1], NULL, 0);
			FBO_Blit(tr.bloomFbo[1], bloomBox, blurV1, tr.bloomFbo[0], bloomBox, &tr.postFxShader[1], NULL, 0);
			FBO_Blit(tr.bloomFbo[0], bloomBox, blurH2, tr.bloomFbo[1], bloomBox, &tr.postFxShader[1], NULL, 0);
			FBO_Blit(tr.bloomFbo[1], bloomBox, blurV2, tr.bloomFbo[0], bloomBox, &tr.postFxShader[1], NULL, 0);
		}
		else
		{
			vec2_t blurH = { 1.35f, 0.0f };
			vec2_t blurV = { 0.0f, 1.35f };

			VectorSet4(brightParams, 0.58f, 2.38f, 0.38f, 1.0f);

			// 6x6 bilinear downsample + soft-quadratic bright-pass into L2-resident 1/6-res bloom FBO
			FBO_Blit(srcFbo, srcBox, srcStep, tr.bloomFbo[0], bloomBox, &tr.postFxShader[0], brightParams, 0);

			// Single wide separable 9-tap Gaussian blur (H + V) at 1/6 res (3 tiny L2 passes total)
			FBO_Blit(tr.bloomFbo[0], bloomBox, blurH, tr.bloomFbo[1], bloomBox, &tr.postFxShader[1], NULL, 0);
			FBO_Blit(tr.bloomFbo[1], bloomBox, blurV, tr.bloomFbo[0], bloomBox, &tr.postFxShader[1], NULL, 0);
		}
	}

	// Pre-blend strictly-monotonic zero-division cubic filmic S-curve coefficients on CPU:
	// f(x) = x * (0.760 + x * (0.947 - 0.740 * x)) blended linearly with identity x
	toneMix = (toneMode >= 2) ? 1.00f : ((toneMode > 0) ? 0.72f : 0.0f);
	combineParams[0] = (bloomMode >= 2) ? 0.84f : ((bloomMode > 0) ? 0.56f : 0.0f);
	combineParams[1] = (1.0f - toneMix) + toneMix * 0.760f;
	combineParams[2] = toneMix * 0.947f;
	combineParams[3] = toneMix * -0.740f;

	scaleX = (srcBox && dstBox && dstBox[2] > 0) ? Com_Clamp(0.5f, 1.0f, (float)srcBox[2] / (float)dstBox[2]) : 1.0f;
	if (sharpMode <= 0)
	{
		sharpAmt = 0.0f;
	}
	else if (sharpMode >= 2)
	{
		sharpAmt = Com_Clamp(0.0f, 0.95f, 0.45f + (1.0f - scaleX) * 1.40f);
	}
	else
	{
		sharpAmt = Com_Clamp(0.0f, 0.68f, 0.24f + (1.0f - scaleX) * 1.25f);
	}
	RB_ComputeMotionBlurParams(srcUvBounds, motionParams, &blurRatio);

	// Back off CAS sharpening smoothly during fast camera turns (avoids ringing & keeps total taps <= 5)
	sharpAmt *= (1.0f - blurRatio);
	if (sharpAmt < 0.02f)
		sharpAmt = 0.0f;

	sharpParams[0] = sharpAmt;
	sharpParams[1] = (r_underwaterFx && r_underwaterFx->integer > 0 && (backEnd.refdef.rdflags & RDF_UNDERWATER))
		? (1.0f + (float)fmod(backEnd.refdef.floatTime * 2.6, 62.831853))
		: 0.0f;

	combineProg = (bloomMode > 0 && tr.bloomImage[0]) ? &tr.postFxShader[2] : &tr.postFxShader[3];
	{
		vec4_t grade;
		vec4_t shaftParams = { 0.5f, 0.5f, 0.0f, 0.0f };

		// Subtle 1/6-res radial sun shafts when facing the sun (0 taps when facing away)
		if (bloomMode > 0 && tr.bloomImage[0] && r_sunShafts && r_sunShafts->integer > 0
			&& !(backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HUD | RDF_UNDERWATER)))
		{
			vec3_t sDir;
			VectorCopy(tr.sunDirection, sDir);
			if (VectorNormalize(sDir) < 0.01f) {
				VectorSet(sDir, 0.42f, 0.30f, 0.85f);
				VectorNormalize(sDir);
			}
			{
				float sunDot = DotProduct(sDir, backEnd.viewParms.ori.axis[0]);
				if (sunDot > 0.25f) {
					mat4_t trans, model, mvp;
					vec4_t pos, hpos;
					float dist = backEnd.viewParms.zFar * 0.55f;
					Mat4Translation(backEnd.viewParms.ori.origin, trans);
					Mat4Multiply(backEnd.viewParms.world.modelMatrix, trans, model);
					Mat4Multiply(backEnd.viewParms.projectionMatrix, model, mvp);
					VectorScale(sDir, dist, pos);
					pos[3] = 1.0f;
					Mat4Transform(mvp, pos, hpos);
					if (hpos[3] > 0.001f) {
						float u = 0.5f + 0.5f * (hpos[0] / hpos[3]);
						float v = 0.5f + 0.5f * (hpos[1] / hpos[3]);
						float fade = Com_Clamp(0.0f, 1.0f, (sunDot - 0.25f) / 0.60f);
						float edgeFade = 1.0f - Com_Clamp(0.0f, 1.0f, (fabsf(u - 0.5f) - 0.55f) * 1.4f);
						shaftParams[0] = Com_Clamp(-0.2f, 1.2f, u);
						shaftParams[1] = Com_Clamp(-0.2f, 1.2f, v);
						shaftParams[2] = fade * fade * edgeFade * ((r_sunShafts->integer >= 2) ? 0.68f : 0.44f);
					}
				}
			}
		}

		RB_GetMapColorGrade(grade);
		GLSL_BindProgram(combineProg);
		GLSL_SetUniformVec4(combineProg, UNIFORM_POSTFXBOUNDS, srcUvBounds);
		GLSL_SetUniformVec2(combineProg, UNIFORM_POSTFXSHARPEN, sharpParams);
		GLSL_SetUniformVec4(combineProg, UNIFORM_POSTFXGRADE, grade);
		GLSL_SetUniformVec4(combineProg, UNIFORM_POSTFXMOTION, motionParams);
		GLSL_SetUniformVec4(combineProg, UNIFORM_POSTFXSHAFTS, shaftParams);
	}

	if (bloomMode > 0 && tr.bloomImage[0])
	{
		GL_BindToTMU(tr.bloomImage[0], TB_LEVELSMAP);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, combineProg, combineParams, 0);
	}
	else
	{
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, combineProg, combineParams, 0);
	}
}

void RB_OnChipFilmicTonemap(ivec4_t dstBox)
{
	int toneMode = r_filmicTonemap ? r_filmicTonemap->integer : 0;
	int atmoMode = r_mapAtmosphere ? r_mapAtmosphere->integer : 0;
	float toneMix;
	vec4_t combineParams;
	vec4_t grade;
	vec4_t quadVerts[4];
	vec2_t texCoords[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
	mat4_t projection;
	int x0, y0, x1, y1;

	if ((toneMode <= 0 && atmoMode <= 0) || !glRefConfig.framebufferFetch || !tr.postFxShader[4].program)
		return;

	FBO_Bind(NULL);
	qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	Mat4Ortho(0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1, projection);

	x0 = dstBox ? dstBox[0] : 0;
	y0 = dstBox ? (glConfig.vidHeight - (dstBox[1] + dstBox[3])) : 0;
	x1 = dstBox ? (dstBox[0] + dstBox[2]) : glConfig.vidWidth;
	y1 = dstBox ? (glConfig.vidHeight - dstBox[1]) : glConfig.vidHeight;

	VectorSet4(quadVerts[0], x0, y0, 0.0f, 1.0f);
	VectorSet4(quadVerts[1], x1, y0, 0.0f, 1.0f);
	VectorSet4(quadVerts[2], x1, y1, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], x0, y1, 0.0f, 1.0f);

	toneMix = (toneMode >= 2) ? 1.00f : ((toneMode > 0) ? 0.72f : 0.0f);
	combineParams[0] = 0.0f;
	combineParams[1] = (1.0f - toneMix) + toneMix * 0.760f;
	combineParams[2] = toneMix * 0.947f;
	combineParams[3] = toneMix * -0.740f;
	RB_GetMapColorGrade(grade);

	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(&tr.postFxShader[4]);
	GLSL_SetUniformMat4(&tr.postFxShader[4], UNIFORM_MODELVIEWPROJECTIONMATRIX, projection);
	GLSL_SetUniformVec4(&tr.postFxShader[4], UNIFORM_COLOR, combineParams);
	GLSL_SetUniformVec4(&tr.postFxShader[4], UNIFORM_POSTFXGRADE, grade);
	RB_InstantQuad2(quadVerts, texCoords);
}
