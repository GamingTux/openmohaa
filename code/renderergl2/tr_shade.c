/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_shade.c

#include "tr_local.h" 
#include <time.h>

static void RB_SetWindUniform( shaderProgram_t *sp );

static int64_t R_ShadeNowUsec( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000000LL + (int64_t)( ts.tv_nsec / 1000 );
}

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


/*
==================
R_DrawElements

==================
*/

void R_DrawElements( int numIndexes, int firstIndex )
{
	if (tess.useCacheVao)
	{
		VaoCache_DrawElements(numIndexes, firstIndex);
	}
	else if (qglDrawElementsBaseVertex
		&& ((glRefConfig.persistentRing && glState.currentVao == tess.vao)
			|| (tr.staticModelVao && glState.currentVao == tr.staticModelVao)))
	{
		qglDrawElementsBaseVertex(GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, BUFFER_OFFSET(firstIndex * sizeof(glIndex_t)), tess.baseVertex);
	}
	else
	{
		qglDrawElements(GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, BUFFER_OFFSET(firstIndex * sizeof(glIndex_t)));
	}
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;


/*
=================
R_BindAnimatedImageToTMU

=================
*/
static void R_BindAnimatedImageToTMU( textureBundle_t *bundle, int tmu ) {
	int64_t index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		GL_BindToTMU(tr.scratchImage[bundle->videoMapHandle], tmu);
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_BindToTMU( bundle->image[0], tmu);
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}

	// Windows x86 doesn't load renderer DLL with 64 bit modulus
	//index %= bundle->numImageAnimations;
	while ( index >= bundle->numImageAnimations ) {
		index -= bundle->numImageAnimations;
	}

	GL_BindToTMU( bundle->image[ index ], tmu );
}


/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) {
	GL_BindToTMU( tr.whiteImage, TB_COLORMAP );

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	{
		shaderProgram_t *sp = &tr.textureColorShader;
		vec4_t color;

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);
		
		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
		VectorSet4(color, 1, 1, 1, 1);
		GLSL_SetUniformVec4(sp, UNIFORM_COLOR, color);
		GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);

		R_DrawElements(input->numIndexes, input->firstIndex);
	}

	qglDepthRange( 0, 1 );
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	if (!input) return;

	const int vertexCount = input->numVertexes;
	if (vertexCount <= 0) return;
	if (!input->shader || input->shader->isSky) return;   // do not draw over sky/clouds
	if (input->shader->numDeforms > 0) return;  // skip vertex-deformed surfaces

	// Skip animated entities that are interpolating; otherwise normals will appear to "swim".
	if (backEnd.currentEntity != &tr.worldEntity) {
		const trRefEntity_t* refEnt = backEnd.currentEntity;
		if (refEnt && refEnt->e.backlerp > 0) return;
	}

	const float       lineLength = 6.0f;

	// xyz is vec4_t (x,y,z,1)
	const float*      positionsXYZW   = (const float*)input->xyz;
	const int16_t*    packedNormals   = (const int16_t*)input->normal;
	const float*      floatNormals    = (const float*)input->normal;

	const vao_t*      boundVao        = glState.currentVao;
	qboolean          usePacked       = qfalse;

	if (boundVao) {
		const vaoAttrib_t* a = &boundVao->attribs[ATTR_INDEX_NORMAL];
		usePacked = (a->type == GL_SHORT && a->normalized);
	}
	else {
		const float sx = packedNormals[0] * (1.0f / 32767.0f);
		const float sy = packedNormals[1] * (1.0f / 32767.0f);
		const float sz = packedNormals[2] * (1.0f / 32767.0f);

		const float lp = sx * sx + sy * sy + sz * sz;
		const float lf = floatNormals[0] * floatNormals[0]
			+ floatNormals[1] * floatNormals[1]
			+ floatNormals[2] * floatNormals[2];

		usePacked = fabsf(lp - 1.0f) <= fabsf(lf - 1.0f);
	}

	const GLsizeiptr floatsPerVertex = 6;
	const GLsizeiptr maxFloatCount = floatsPerVertex * (GLsizeiptr)vertexCount;
	const GLsizeiptr maxByteCount = (GLsizeiptr)sizeof(float) * maxFloatCount;

	float* lineVertices = (float*)ri.Hunk_AllocateTempMemory((int)maxByteCount);
	float* writePtr = lineVertices;

	// Build the line list
	for (int i = 0; i < vertexCount; ++i, positionsXYZW += 4) {
		vec3_t n;

		if (usePacked) {
			R_VaoUnpackNormal(n, (int16_t*)packedNormals);
			packedNormals += 4;
		}
		else {
			n[0] = floatNormals[0];
			n[1] = floatNormals[1];
			n[2] = floatNormals[2];
			floatNormals += 4;
		}

		// endpoints in object/world space
		const float startX = positionsXYZW[0];
		const float startY = positionsXYZW[1];
		const float startZ = positionsXYZW[2];

		const float endX = startX + n[0] * lineLength;
		const float endY = startY + n[1] * lineLength;
		const float endZ = startZ + n[2] * lineLength;


		// emit line
		*writePtr++ = startX; *writePtr++ = startY; *writePtr++ = startZ;
		*writePtr++ = endX;   *writePtr++ = endY;   *writePtr++ = endZ;
	}

	const GLsizei numVerts = (GLsizei)((writePtr - lineVertices) / 3);
	if (numVerts == 0) {
		ri.Hunk_FreeTempMemory(lineVertices);
		return;
	}

	// save bindings
	GLint prevProg = 0, prevVao = 0, prevArrayBuf = 0;
	qglGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
	qglGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	qglGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);

	GL_BindToTMU(tr.whiteImage, TB_COLORMAP);
	GL_State(GLS_POLYMODE_LINE);
	GLboolean prevDepthMask;
	qglGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
	qglDepthMask(GL_FALSE);


	R_BindNullVao();

	// temp VAO/VBO
	GLuint vao = 0, vbo = 0;
	qglGenVertexArrays(1, &vao);
	qglBindVertexArray(vao);

	qglGenBuffers(1, &vbo);
	qglBindBuffer(GL_ARRAY_BUFFER, vbo);
	qglBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * numVerts, lineVertices, GL_STREAM_DRAW);

	shaderProgram_t* program = &tr.textureColorShader;
	GLSL_BindProgram(program);
	GLSL_SetUniformMat4(program, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);

	vec4_t white;
	VectorSet4(white, 1, 1, 1, 1);
	GLSL_SetUniformVec4(program, UNIFORM_COLOR, white);

	// position at location 0 in this backend
	qglEnableVertexAttribArray(ATTR_INDEX_POSITION);
	qglVertexAttribPointer(ATTR_INDEX_POSITION, 3, GL_FLOAT, GL_FALSE, 0, (const void*)0);

	qglDrawArrays(GL_LINES, 0, numVerts);

	qglDisableVertexAttribArray(ATTR_INDEX_POSITION);

	// restore
	qglDepthMask(prevDepthMask);
	qglBindBuffer(GL_ARRAY_BUFFER, prevArrayBuf);
	qglBindVertexArray(prevVao);
	qglDeleteBuffers(1, &vbo);
	qglDeleteVertexArrays(1, &vao);

	if (prevProg) qglUseProgram((GLuint)prevProg);
	else          qglUseProgram(0);

	ri.Hunk_FreeTempMemory(lineVertices);
}

/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum, int cubemapIndex ) {

	shader_t *state = (shader->remappedShader) ? shader->remappedShader : shader;

	tess.numIndexes = 0;
	tess.firstIndex = 0;
	tess.baseVertex = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.cubemapIndex = cubemapIndex;
	tess.dlightBits = 0;		// will be OR'd in by surface functions
	tess.pshadowBits = 0;       // will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;
	tess.useInternalVao = qtrue;
	tess.useCacheVao = qfalse;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if (tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime) {
		tess.shaderTime = tess.shader->clampTime;
	}

	if (backEnd.viewParms.flags & VPF_SHADOWMAP)
	{
		tess.currentStageIteratorFunc = RB_StageIteratorGeneric;
	}
}



extern float EvalWaveForm( const waveForm_t *wf );
extern float EvalWaveFormClamped( const waveForm_t *wf );


static void ComputeTexMods( shaderStage_t *pStage, int bundleNum, vec4_t outMatrix[8])
{
	int tm;
	float matrix[6];
	float tmpmatrix[6];
	float currentmatrix[6];
	float turb[2];
	textureBundle_t *bundle = &pStage->bundle[bundleNum];
	qboolean hasTurb = qfalse;

	currentmatrix[0] = 1.0f; currentmatrix[2] = 0.0f; currentmatrix[4] = 0.0f;
	currentmatrix[1] = 0.0f; currentmatrix[3] = 1.0f; currentmatrix[5] = 0.0f;

	for ( tm = 0; tm < bundle->numTexMods ; tm++ ) {
		switch ( bundle->texMods[tm].type )
		{
			
		case TMOD_NONE:
			matrix[0] = 1.0f; matrix[2] = 0.0f; matrix[4] = 0.0f;
			matrix[1] = 0.0f; matrix[3] = 1.0f; matrix[5] = 0.0f;
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentFactors(&bundle->texMods[tm].wave, &turb[0], &turb[1]);
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexMatrix( backEnd.currentEntity->e.shaderTexCoord, matrix );
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexMatrix( bundle->texMods[tm].scroll,
									 matrix );
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexMatrix( bundle->texMods[tm].scale,
								  matrix );
			break;
		
		case TMOD_STRETCH:
			RB_CalcStretchTexMatrix( &bundle->texMods[tm].wave, 
								   matrix );
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexMatrix( &bundle->texMods[tm],
									 matrix );
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexMatrixEx( bundle->texMods[tm].rotateSpeed,
									  bundle->texMods[tm].rotateCoef,
									  bundle->texMods[tm].rotateStart,
									  matrix );
			break;

		case TMOD_OFFSET:
			RB_CalcOffsetTexMatrix( bundle->texMods[tm].scroll, matrix );
			break;

		case TMOD_PARALLAX:
			RB_CalcParallaxTexMatrix( bundle->texMods[tm].rate, matrix );
			break;

		case TMOD_WAVETRANS:
			RB_CalcTransWaveTexMatrix( &bundle->texMods[tm].wave, matrix );
			break;

		case TMOD_WAVETRANT:
			RB_CalcTransWaveTTexMatrix( &bundle->texMods[tm].wave, matrix );
			break;

		case TMOD_BULGETRANS:
		case TMOD_MACRO:
			matrix[0] = 1.0f; matrix[2] = 0.0f; matrix[4] = 0.0f;
			matrix[1] = 0.0f; matrix[3] = 1.0f; matrix[5] = 0.0f;
			break;

		default:
			ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name );
			break;
		}

		switch ( bundle->texMods[tm].type )
		{	
		case TMOD_TURBULENT:
			outMatrix[tm*2+0][0] = 1; outMatrix[tm*2+0][1] = 0; outMatrix[tm*2+0][2] = 0;
			outMatrix[tm*2+1][0] = 0; outMatrix[tm*2+1][1] = 1; outMatrix[tm*2+1][2] = 0;

			outMatrix[tm*2+0][3] = turb[0];
			outMatrix[tm*2+1][3] = turb[1];

			hasTurb = qtrue;
			break;

		case TMOD_NONE:
		case TMOD_ENTITY_TRANSLATE:
		case TMOD_SCROLL:
		case TMOD_SCALE:
		case TMOD_STRETCH:
		case TMOD_TRANSFORM:
		case TMOD_ROTATE:
		default:
			outMatrix[tm*2+0][0] = matrix[0]; outMatrix[tm*2+0][1] = matrix[2]; outMatrix[tm*2+0][2] = matrix[4];
			outMatrix[tm*2+1][0] = matrix[1]; outMatrix[tm*2+1][1] = matrix[3]; outMatrix[tm*2+1][2] = matrix[5];

			outMatrix[tm*2+0][3] = 0;
			outMatrix[tm*2+1][3] = 0;

			tmpmatrix[0] = matrix[0] * currentmatrix[0] + matrix[2] * currentmatrix[1];
			tmpmatrix[1] = matrix[1] * currentmatrix[0] + matrix[3] * currentmatrix[1];

			tmpmatrix[2] = matrix[0] * currentmatrix[2] + matrix[2] * currentmatrix[3];
			tmpmatrix[3] = matrix[1] * currentmatrix[2] + matrix[3] * currentmatrix[3];

			tmpmatrix[4] = matrix[0] * currentmatrix[4] + matrix[2] * currentmatrix[5] + matrix[4];
			tmpmatrix[5] = matrix[1] * currentmatrix[4] + matrix[3] * currentmatrix[5] + matrix[5];

			currentmatrix[0] = tmpmatrix[0];
			currentmatrix[1] = tmpmatrix[1];
			currentmatrix[2] = tmpmatrix[2];
			currentmatrix[3] = tmpmatrix[3];
			currentmatrix[4] = tmpmatrix[4];
			currentmatrix[5] = tmpmatrix[5];
			break;
		}
	}

	// if turb isn't used, only one matrix is needed
	if ( !hasTurb ) {
		tm = 0;

		outMatrix[tm*2+0][0] = currentmatrix[0]; outMatrix[tm*2+0][1] = currentmatrix[2]; outMatrix[tm*2+0][2] = currentmatrix[4];
		outMatrix[tm*2+1][0] = currentmatrix[1]; outMatrix[tm*2+1][1] = currentmatrix[3]; outMatrix[tm*2+1][2] = currentmatrix[5];

		outMatrix[tm*2+0][3] = 0;
		outMatrix[tm*2+1][3] = 0;
		tm++;
	}

	for ( ; tm < TR_MAX_TEXMODS ; tm++ ) {
		outMatrix[tm*2+0][0] = 1; outMatrix[tm*2+0][1] = 0; outMatrix[tm*2+0][2] = 0;
		outMatrix[tm*2+1][0] = 0; outMatrix[tm*2+1][1] = 1; outMatrix[tm*2+1][2] = 0;

		outMatrix[tm*2+0][3] = 0;
		outMatrix[tm*2+1][3] = 0;
	}
}


static void ComputeDeformValues(int *deformGen, vec5_t deformParams)
{
	// u_DeformGen
	*deformGen = DGEN_NONE;
	if(!ShaderRequiresCPUDeforms(tess.shader))
	{
		deformStage_t  *ds;

		// only support the first one
		ds = &tess.shader->deforms[0];

		switch (ds->deformation)
		{
			case DEFORM_WAVE:
				*deformGen = ds->deformationWave.func;

				deformParams[0] = ds->deformationWave.base;
				deformParams[1] = ds->deformationWave.amplitude;
				deformParams[2] = ds->deformationWave.phase;
				deformParams[3] = ds->deformationWave.frequency;
				deformParams[4] = ds->deformationSpread;
				break;

			case DEFORM_BULGE:
				*deformGen = DGEN_BULGE;

				deformParams[0] = 0;
				deformParams[1] = ds->bulgeHeight; // amplitude
				deformParams[2] = ds->bulgeWidth;  // phase
				deformParams[3] = ds->bulgeSpeed;  // frequency
				deformParams[4] = 0;
				break;

			default:
				break;
		}
	}
}


static void ProjectDlightTexture( void ) {
	int		l;
	vec3_t	origin;
	float	scale;
	float	radius;
	int deformGen;
	vec5_t deformParams;
	shaderStage_t *pStage = tess.xstages[0];
	unsigned int atestBits = pStage ? (pStage->stateBits & GLS_ATEST_BITS) : 0;
	int alphaTest = 0;
	float dlightScale;
	vec2_t fogParams = { 0.0f, 0.0f };

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	ComputeDeformValues(&deformGen, deformParams);

	if ( atestBits == GLS_ATEST_GT_0 ) {
		alphaTest = 1;
	} else if ( atestBits == GLS_ATEST_LT_80 ) {
		alphaTest = 2;
	} else if ( atestBits == GLS_ATEST_GE_80 ) {
		alphaTest = 3;
	}

	if ( !backEnd.projection2D && !backEnd.depthFill
		&& !( backEnd.viewParms.flags & VPF_DEPTHSHADOW )
		&& !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		&& !tess.shader->isSky
		&& ( !pStage || !pStage->noFog )
		&& backEnd.viewParms.farplane_distance > 0.0f
		&& !r_farplane_nofog->integer )
	{
		float fogRange = backEnd.viewParms.farplane_distance - backEnd.viewParms.farplane_bias;
		fogParams[0] = backEnd.viewParms.farplane_bias;
		fogParams[1] = ( fogRange > 1.0f ) ? ( 1.0f / fogRange ) : 0.0f;
	}

	dlightScale = (375.0f / 255.0f) * tr.overbrightMult * tr.identityLight;

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;
		shaderProgram_t *sp;
		vec4_t vector;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this light
		}

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		sp = &tr.dlightShader[deformGen == DGEN_NONE ? 0 : 1];

		backEnd.pc.c_dlightDraws++;

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);

		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);

		GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);
		
		GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
		if (deformGen != DGEN_NONE)
		{
			GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
			GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
		}

		vector[0] = dl->color[0] * dlightScale;
		vector[1] = dl->color[1] * dlightScale;
		vector[2] = dl->color[2] * dlightScale;
		vector[3] = 1.0f;
		GLSL_SetUniformVec4(sp, UNIFORM_COLOR, vector);

		vector[0] = origin[0];
		vector[1] = origin[1];
		vector[2] = origin[2];
		vector[3] = scale;
		GLSL_SetUniformVec4(sp, UNIFORM_DLIGHTINFO, vector);
		GLSL_SetUniformVec2(sp, UNIFORM_FARPLANEFOGPARAMS, fogParams);
		GLSL_SetUniformVec3(sp, UNIFORM_LOCALVIEWORIGIN, backEnd.ori.viewOrigin);

		{
			vec4_t dlightSpec;
			if ( r_modernDlights && r_modernDlights->integer > 0 ) {
				float modeScale = ( r_modernDlights->integer >= 2 ) ? 1.45f : 1.0f;
				float reliefStr = ( tess.shader->worldRelief[0] > 0.0f ) ? tess.shader->worldRelief[0] : 1.40f;
				float specStr   = ( tess.shader->worldRelief[0] > 0.0f ) ? ( tess.shader->worldRelief[1] * 1.9f ) : 0.16f;
				float shinScale = ( tess.shader->worldRelief[0] > 0.0f ) ? tess.shader->worldRelief[2] : 7.0f;
				vec3_t worldRight, localRight, localUp;

				VectorNegate( backEnd.viewParms.ori.axis[1], worldRight );
				if ( backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity ) {
					MatrixTransformVectorRight( backEnd.currentEntity->e.axis, worldRight, localRight );
					MatrixTransformVectorRight( backEnd.currentEntity->e.axis, backEnd.viewParms.ori.axis[2], localUp );
				} else {
					VectorCopy( worldRight, localRight );
					VectorCopy( backEnd.viewParms.ori.axis[2], localUp );
				}
				if ( VectorNormalize( localRight ) < 1e-6f ) {
					VectorSet( localRight, 1.0f, 0.0f, 0.0f );
				}
				if ( VectorNormalize( localUp ) < 1e-6f ) {
					VectorSet( localUp, 0.0f, 0.0f, 1.0f );
				}

				VectorSet4( dlightSpec, 1.0f, specStr * modeScale, shinScale, reliefStr * 0.72f * modeScale );
				GLSL_SetUniformVec3( sp, UNIFORM_WORLDRELIEFRIGHT, localRight );
				GLSL_SetUniformVec3( sp, UNIFORM_WORLDRELIEFUP, localUp );
			} else {
				VectorSet4( dlightSpec, 0.0f, 0.0f, 7.0f, 0.0f );
			}
			GLSL_SetUniformVec4( sp, UNIFORM_SPECULARSCALE, dlightSpec );
		}

		if ( pStage && pStage->bundle[TB_DIFFUSEMAP].image[0] ) {
			R_BindAnimatedImageToTMU( &pStage->bundle[TB_DIFFUSEMAP], TB_DIFFUSEMAP );
		} else {
			GL_BindToTMU( tr.whiteImage, TB_DIFFUSEMAP );
		}

		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		GL_State( atestBits | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, alphaTest);

		R_DrawElements(tess.numIndexes, tess.firstIndex);

		backEnd.pc.c_totalIndexes += tess.numIndexes;
		backEnd.pc.c_dlightIndexes += tess.numIndexes;
		backEnd.pc.c_dlightVertexes += tess.numVertexes;
	}
}


static qboolean RB_UseModelPerPixelLighting( const shaderStage_t *pStage )
{
	return ( r_modelPerPixelLighting && r_modelPerPixelLighting->integer > 0
		&& pStage != NULL
		&& pStage->rgbGen == CGEN_LIGHTING_SPHERICAL
		&& pStage->bundle[1].image[0] == NULL
		&& !backEnd.depthFill
		&& !backEnd.projection2D
		&& !backEnd.currentStaticModel
		&& backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity
		&& backEnd.currentSphere && backEnd.currentSphere->TessFunction == RB_Light_Real
		&& !tess.fogNum
		&& !( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) )
		&& r_drawspherelights->integer
		&& !r_light_nolight->integer
		&& !r_fastentlight->integer
		&& !r_lightmap->integer );
}

static qboolean RB_UseModernWater( int stage, const shaderStage_t *pStage )
{
	return ( r_modernWater && r_modernWater->integer > 0
		&& pStage != NULL
		&& tess.shader != NULL
		&& tess.shader->waterType > 0
		&& stage == tess.shader->waterStage
		&& !backEnd.depthFill
		&& !backEnd.projection2D
		&& !glState.vertexAnimation
		&& !glState.boneAnimation
		&& !( pStage->stateBits & GLS_ATEST_BITS )
		&& !r_lightmap->integer );
}

static void RB_SetupWaterUniforms( shaderProgram_t *sp )
{
	int waterType = tess.shader->waterType;
	float modeBoost = ( r_modernWater->integer >= 2 ) ? 1.25f : 1.0f;
	float invSunScale = 1.0f / ( 255.0f * ( tr.overbrightMult > 0.01f ? tr.overbrightMult : 1.0f ) );
	vec3_t sunDir, sunCol, horizonCol, skyCol, specCol;
	vec4_t uSunDir, uSunColor, uSkyColor, uHorizonColor, uParams;
	float sunLum, dayFactor, maxSun;
	float fresnelStrength, waveAmp, specBoost, waveFreq, waveSpeed, refractScale, deepTintMix, foamStrength;

	if ( s_sun.exists ) {
		VectorCopy( s_sun.direction, sunDir );
		sunCol[0] = Com_Clamp( 0.0f, 1.0f, s_sun.color[0] * invSunScale );
		sunCol[1] = Com_Clamp( 0.0f, 1.0f, s_sun.color[1] * invSunScale );
		sunCol[2] = Com_Clamp( 0.0f, 1.0f, s_sun.color[2] * invSunScale );
	} else {
		VectorCopy( tr.sunDirection, sunDir );
		VectorSet( sunCol, 0.30f, 0.32f, 0.34f );
	}

	if ( VectorNormalize( sunDir ) < 1e-4f ) {
		VectorSet( sunDir, 0.44f, 0.32f, 0.84f );
	}
	if ( sunDir[2] < 0.0f ) {
		VectorNegate( sunDir, sunDir );
	}
	if ( sunDir[2] > 0.92f ) {
		// Overhead overcast sun (e.g., Omaha m3l1a 270 360 0): angle slightly toward horizon
		// so wave slopes catch natural directional shading and silver-grey highlights.
		VectorSet( sunDir, 0.44f, 0.32f, 0.84f );
		VectorNormalize( sunDir );
	} else if ( sunDir[2] < 0.22f ) {
		sunDir[2] = 0.22f;
		VectorNormalize( sunDir );
	}

	sunLum = sunCol[0] * 0.30f + sunCol[1] * 0.59f + sunCol[2] * 0.11f;
	dayFactor = Com_Clamp( 0.26f, 1.15f, sunLum * 2.85f );

	if ( backEnd.viewParms.farplane_distance > 1.0f ) {
		horizonCol[0] = Com_Clamp( 0.04f, 0.92f, backEnd.viewParms.farplane_color[0] * tr.identityLight );
		horizonCol[1] = Com_Clamp( 0.04f, 0.92f, backEnd.viewParms.farplane_color[1] * tr.identityLight );
		horizonCol[2] = Com_Clamp( 0.04f, 0.92f, backEnd.viewParms.farplane_color[2] * tr.identityLight );

		skyCol[0] = Com_Clamp( 0.03f, 0.90f, horizonCol[0] * 0.72f + sunCol[0] * 0.18f );
		skyCol[1] = Com_Clamp( 0.03f, 0.90f, horizonCol[1] * 0.80f + sunCol[1] * 0.18f );
		skyCol[2] = Com_Clamp( 0.04f, 0.92f, horizonCol[2] * 0.90f + sunCol[2] * 0.22f );
	} else {
		horizonCol[0] = Com_Clamp( 0.07f, 0.75f, sunCol[0] * 1.45f + 0.04f );
		horizonCol[1] = Com_Clamp( 0.09f, 0.78f, sunCol[1] * 1.55f + 0.05f );
		horizonCol[2] = Com_Clamp( 0.12f, 0.82f, sunCol[2] * 1.65f + 0.07f );

		skyCol[0] = horizonCol[0] * 0.68f;
		skyCol[1] = horizonCol[1] * 0.76f;
		skyCol[2] = horizonCol[2] * 0.88f;
	}

	maxSun = MAX( sunCol[0], MAX( sunCol[1], sunCol[2] ) );
	if ( maxSun > 0.01f ) {
		specCol[0] = ( sunCol[0] / maxSun ) * 0.65f + horizonCol[0] * 0.35f;
		specCol[1] = ( sunCol[1] / maxSun ) * 0.65f + horizonCol[1] * 0.35f;
		specCol[2] = ( sunCol[2] / maxSun ) * 0.65f + horizonCol[2] * 0.35f;
	} else {
		VectorSet( specCol, 0.75f, 0.80f, 0.85f );
	}
	VectorScale( specCol, dayFactor, specCol );

	switch ( waterType ) {
	case 2: // flowing river / stream (usually followed by a multiplicative $lightmap pass)
		VectorScale( horizonCol, 1.25f, horizonCol );
		VectorScale( skyCol,     1.20f, skyCol );
		fresnelStrength = 0.72f * modeBoost;
		waveAmp         = 0.26f * modeBoost;
		specBoost       = 0.72f * modeBoost;
		waveFreq        = 0.0085f;
		waveSpeed       = 0.34f;
		refractScale    = 0.020f;
		deepTintMix     = 0.30f;
		foamStrength    = 0.12f * modeBoost;
		break;
	case 3: // calm pond (followed by a multiplicative $lightmap pass)
		VectorScale( horizonCol, 1.15f, horizonCol );
		VectorScale( skyCol,     1.10f, skyCol );
		fresnelStrength = 0.72f * modeBoost;
		waveAmp         = 0.14f * modeBoost;
		specBoost       = 0.55f * modeBoost;
		waveFreq        = 0.0075f;
		waveSpeed       = 0.13f;
		refractScale    = 0.012f;
		deepTintMix     = 0.28f;
		foamStrength    = 0.0f;
		break;
	case 4: // indoor U-boat pen / harbor basin
		VectorScale( horizonCol, 0.28f, horizonCol );
		VectorScale( skyCol,     0.22f, skyCol );
		VectorSet( sunDir,  0.22f, 0.18f, 0.96f );
		VectorNormalize( sunDir );
		VectorSet( specCol, 0.52f, 0.54f, 0.50f );
		fresnelStrength = 0.48f * modeBoost;
		waveAmp         = 0.16f * modeBoost;
		specBoost       = 0.38f * modeBoost;
		waveFreq        = 0.0070f;
		waveSpeed       = 0.12f;
		refractScale    = 0.016f;
		deepTintMix     = 0.35f;
		foamStrength    = 0.0f;
		break;
	case 5: // murky canal sludge (additive stage followed by $lightmap)
		VectorScale( horizonCol, 0.45f, horizonCol );
		VectorScale( skyCol,     0.38f, skyCol );
		fresnelStrength = 0.32f * modeBoost;
		waveAmp         = 0.18f * modeBoost;
		specBoost       = 0.32f * modeBoost;
		waveFreq        = 0.0075f;
		waveSpeed       = 0.16f;
		refractScale    = 0.016f;
		deepTintMix     = 0.20f;
		foamStrength    = 0.0f;
		break;
	case 1: // open ocean / sea / shoreline
	default:
		fresnelStrength = 0.78f * modeBoost;
		waveAmp         = 0.32f * modeBoost;
		specBoost       = 0.68f * modeBoost;
		waveFreq        = 0.0040f;
		waveSpeed       = 0.22f;
		refractScale    = 0.022f;
		deepTintMix     = 0.42f;
		foamStrength    = 0.22f * modeBoost;
		break;
	}

	VectorSet4( uSunDir,       sunDir[0],     sunDir[1],     sunDir[2],     specBoost );
	VectorSet4( uSunColor,     specCol[0],    specCol[1],    specCol[2],    foamStrength );
	VectorSet4( uSkyColor,     skyCol[0],     skyCol[1],     skyCol[2],     fresnelStrength );
	VectorSet4( uHorizonColor, horizonCol[0], horizonCol[1], horizonCol[2], waveAmp );
	VectorSet4( uParams,       waveFreq,      waveSpeed,     refractScale,  deepTintMix );

	GLSL_SetUniformVec4( sp, UNIFORM_WATERSUNDIR,       uSunDir );
	GLSL_SetUniformVec4( sp, UNIFORM_WATERSUNCOLOR,     uSunColor );
	GLSL_SetUniformVec4( sp, UNIFORM_WATERSKYCOLOR,     uSkyColor );
	GLSL_SetUniformVec4( sp, UNIFORM_WATERHORIZONCOLOR, uHorizonColor );
	GLSL_SetUniformVec4( sp, UNIFORM_WATERPARAMS,       uParams );
}

static void RB_SetupWorldReliefUniforms( shaderProgram_t *sp, const shaderStage_t *pStage )
{
	vec4_t reliefLight, reliefParams;
	vec3_t worldRight, worldUp, lightDir;
	float modeScale;

	if ( sp->uniforms[UNIFORM_WORLDRELIEFLIGHT] < 0 ) {
		return;
	}

	if ( !r_worldMicroRelief || r_worldMicroRelief->integer <= 0
		|| backEnd.projection2D
		|| r_lightmap->integer
		|| tess.shader == NULL
		|| tess.shader->worldRelief[0] <= 0.0f
		|| ( pStage->stateBits & ( GLS_ATEST_BITS | GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) )
	{
		VectorSet4( reliefLight,  0.0f, 0.0f, 1.0f, 0.0f );
		VectorSet4( reliefParams, 0.0f, 0.0f, 5.5f, 0.0f );
		GLSL_SetUniformVec4( sp, UNIFORM_WORLDRELIEFLIGHT,  reliefLight );
		GLSL_SetUniformVec4( sp, UNIFORM_WORLDRELIEFPARAMS, reliefParams );
		return;
	}

	modeScale = ( r_worldMicroRelief->integer >= 2 ) ? 1.45f : 1.0f;

	VectorNegate( backEnd.viewParms.ori.axis[1], worldRight );
	if ( VectorNormalize( worldRight ) < 1e-6f ) {
		VectorSet( worldRight, 1.0f, 0.0f, 0.0f );
	}
	VectorCopy( backEnd.viewParms.ori.axis[2], worldUp );
	if ( VectorNormalize( worldUp ) < 1e-6f ) {
		VectorSet( worldUp, 0.0f, 0.0f, 1.0f );
	}

	if ( s_sun.exists ) {
		VectorCopy( s_sun.direction, lightDir );
	} else {
		VectorCopy( tr.sunDirection, lightDir );
	}
	if ( VectorNormalize( lightDir ) < 1e-4f ) {
		VectorSet( lightDir, 0.42f, 0.30f, 0.85f );
	}
	if ( lightDir[2] < 0.0f ) {
		VectorNegate( lightDir, lightDir );
	}
	// Blend slight angled overhead bias so both walls and floors always catch consistent 3D relief
	lightDir[0] += 0.28f;
	lightDir[1] += 0.20f;
	lightDir[2] += 0.55f;
	VectorNormalize( lightDir );

	VectorSet4( reliefLight,  lightDir[0], lightDir[1], lightDir[2], tess.shader->worldRelief[0] * modeScale );
	VectorSet4( reliefParams, tess.shader->worldRelief[1] * modeScale, tess.shader->worldFresnel * modeScale, tess.shader->worldRelief[2], tess.shader->worldRelief[3] );

	GLSL_SetUniformVec3( sp, UNIFORM_WORLDRELIEFRIGHT,  worldRight );
	GLSL_SetUniformVec3( sp, UNIFORM_WORLDRELIEFUP,     worldUp );
	GLSL_SetUniformVec4( sp, UNIFORM_WORLDRELIEFLIGHT,  reliefLight );
	GLSL_SetUniformVec4( sp, UNIFORM_WORLDRELIEFPARAMS, reliefParams );
}

static void RB_SetupSunShadowUniforms( shaderProgram_t *sp, const shaderStage_t *pStage, qboolean isModel )
{
	vec4_t shadowParams, sunDirParam;
	float texelSize, normalBias, depthBias, pcfRadius, subScale;
	int shadowRes;

	if ( sp->uniforms[UNIFORM_SUNSHADOWPARAMS] < 0 ) {
		return;
	}

	if ( tr.piSunShadowImage ) {
		GL_BindToTMU( tr.piSunShadowImage, TB_SHADOWMAP2 );
	}

	if ( !tr.piSunShadowImage
		|| !backEnd.refdef.piSunShadowActive
		|| !r_dynamicSunShadows || r_dynamicSunShadows->integer <= 0
		|| backEnd.projection2D
		|| backEnd.depthFill
		|| ( backEnd.viewParms.flags & VPF_DEPTHSHADOW )
		|| ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_HUD ) )
		|| backEnd.viewParms.isPortalSky
		|| r_lightmap->integer
		|| ( pStage && ( pStage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) )
	{
		VectorSet4( shadowParams, 0.0f, 0.0f, 0.0f, 0.0f );
		GLSL_SetUniformVec4( sp, UNIFORM_SUNSHADOWPARAMS, shadowParams );
		return;
	}

	shadowRes  = ( backEnd.refdef.piSunShadowRes > 0 ) ? backEnd.refdef.piSunShadowRes : tr.piSunShadowImage->width;
	subScale   = (float)shadowRes / (float)tr.piSunShadowImage->width;
	texelSize  = ( 2.0f * backEnd.refdef.piSunShadowRadius ) / (float)shadowRes;
	normalBias = texelSize * ( isModel ? 1.25f : 1.45f );
	depthBias  = isModel ? 0.00055f : 0.00045f;
	pcfRadius  = ( ( subScale >= 0.85f ) ? 1.45f : 0.72f ) / (float)tr.piSunShadowImage->width;

	if ( isModel ) {
		mat4_t modelSunMvp;
		vec3_t localSunDir;

		Mat4Multiply( backEnd.refdef.piSunShadowMvp, backEnd.ori.transformMatrix, modelSunMvp );
		GLSL_SetUniformMat4( sp, UNIFORM_SUNSHADOWMVP, modelSunMvp );

		MatrixTransformVectorRight( backEnd.ori.axis, backEnd.refdef.piSunShadowDir, localSunDir );
		if ( VectorNormalize( localSunDir ) < 1e-6f ) {
			VectorSet( localSunDir, 0.0f, 0.0f, 1.0f );
		}
		VectorSet4( sunDirParam, localSunDir[0], localSunDir[1], localSunDir[2], normalBias );
	} else {
		GLSL_SetUniformMat4( sp, UNIFORM_SUNSHADOWMVP, backEnd.refdef.piSunShadowMvp );
		VectorSet4( sunDirParam,
			backEnd.refdef.piSunShadowDir[0],
			backEnd.refdef.piSunShadowDir[1],
			backEnd.refdef.piSunShadowDir[2],
			normalBias );
	}

	VectorSet4( shadowParams, backEnd.refdef.piSunShadowStrength, depthBias, pcfRadius, subScale );
	GLSL_SetUniformVec4( sp, UNIFORM_SUNSHADOWDIR,    sunDirParam );
	GLSL_SetUniformVec4( sp, UNIFORM_SUNSHADOWPARAMS, shadowParams );
}

static qboolean RB_GetSoftParticleFadeMask( const shaderStage_t *pStage, vec2_t fadeMask )
{
	unsigned int srcBlend = pStage->stateBits & GLS_SRCBLEND_BITS;
	unsigned int dstBlend = pStage->stateBits & GLS_DSTBLEND_BITS;

	if ( srcBlend == GLS_SRCBLEND_SRC_ALPHA ) {
		// Standard alpha blend (SRC_ALPHA, 1-SRC_ALPHA) and alpha-additive (SRC_ALPHA, ONE)
		fadeMask[0] = 0.0f;
		fadeMask[1] = 1.0f;
		return qtrue;
	}
	if ( srcBlend == GLS_SRCBLEND_ONE && ( dstBlend == GLS_DSTBLEND_ONE || dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) ) {
		// Additive (ONE, ONE) and screen (ONE, 1-SRC_COLOR)
		fadeMask[0] = 1.0f;
		fadeMask[1] = 0.0f;
		return qtrue;
	}
	if ( srcBlend == GLS_SRCBLEND_ONE && dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) {
		// Premultiplied alpha (ONE, 1-SRC_ALPHA)
		fadeMask[0] = 1.0f;
		fadeMask[1] = 1.0f;
		return qtrue;
	}
	return qfalse;
}

static qboolean RB_UseSoftParticles( const shaderStage_t *pStage )
{
	vec2_t mask;

	if ( !r_softParticles || r_softParticles->integer <= 0
		|| !glRefConfig.framebufferObject
		|| !tr.softDepthFbo || !tr.softDepthImage
		|| backEnd.projection2D
		|| backEnd.depthFill
		|| ( backEnd.viewParms.flags & VPF_DEPTHSHADOW )
		|| ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		|| ( backEnd.currentEntity && ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) )
		|| glState.vertexAnimation
		|| glState.boneAnimation
		|| !tess.shader || tess.shader->softParticle <= 0.0f
		|| !pStage
		|| ( pStage->stateBits & ( GLS_DEPTHMASK_TRUE | GLS_ATEST_BITS ) )
		|| pStage->rgbGen == CGEN_LIGHTING_SPHERICAL
		|| pStage->rgbGen == CGEN_LIGHTING_DIFFUSE
		|| pStage->alphaGen == AGEN_LIGHTING_SPECULAR
		|| pStage->alphaGen == AGEN_PORTAL
		|| ( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) )
		|| r_lightmap->integer )
	{
		return qfalse;
	}

	if ( !RB_GetSoftParticleFadeMask( pStage, mask ) ) {
		return qfalse;
	}

	// Balanced Pi 5 TBDR optimization: skip triggering a mid-frame depth-blit tile flush
	// if all particles in this batch are far from the camera where soft fade is invisible.
	if ( r_softParticles->integer == 1 && !backEnd.softDepthCaptured && tess.numVertexes > 0 ) {
		int step = MAX( 1, tess.numVertexes / 8 );
		int i;
		qboolean anyNear = qfalse;
		const float maxDistSq = 1400.0f * 1400.0f;

		for ( i = 0; i < tess.numVertexes; i += step ) {
			vec3_t delta;
			VectorSubtract( tess.xyz[i], backEnd.ori.viewOrigin, delta );
			if ( VectorLengthSquared( delta ) < maxDistSq ) {
				anyNear = qtrue;
				break;
			}
		}
		if ( !anyNear ) {
			return qfalse;
		}
	}

	return qtrue;
}

static image_t *RB_EnsureSoftParticleDepth( void )
{
	FBO_t *oldFbo;
	ivec4_t srcBox, dstBox;

	if ( backEnd.softDepthCaptured ) {
		return backEnd.softDepthBoundImage;
	}

	if ( !glRefConfig.framebufferObject || !tr.softDepthFbo || !tr.softDepthImage ) {
		return NULL;
	}

	oldFbo = glState.currentFBO;
	VectorSet4( srcBox,
		backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportHeight );

	if ( oldFbo != NULL && oldFbo == tr.renderFbo && !tr.msaaResolveFbo ) {
		// Fast Pi 5 path: discard previous 1/4-res soft-depth contents on-chip before blit
		if ( glRefConfig.invalidateFramebuffer ) {
			const GLenum depthAtt = GL_DEPTH_ATTACHMENT;
			FBO_Bind( tr.softDepthFbo );
			qglInvalidateFramebuffer_ext( GL_FRAMEBUFFER, 1, &depthAtt );
		}
		VectorSet4( dstBox, 0, 0, tr.softDepthFbo->width, tr.softDepthFbo->height );
		FBO_FastBlit( oldFbo, srcBox, tr.softDepthFbo, dstBox, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
		FBO_Bind( oldFbo );
		qglViewport( srcBox[0], srcBox[1], srcBox[2], srcBox[3] );
		qglScissor( srcBox[0], srcBox[1], srcBox[2], srcBox[3] );
		backEnd.softDepthBoundImage = tr.softDepthImage;
	} else if ( oldFbo == NULL && tr.renderDepthImage ) {
		// Direct-to-FBO-0 fallback when PostFX is disabled
		qglCopyTextureSubImage2DEXT( tr.renderDepthImage->texnum, GL_TEXTURE_2D, 0,
			0, 0, srcBox[0], srcBox[1], srcBox[2], srcBox[3] );
		backEnd.softDepthBoundImage = tr.renderDepthImage;
	} else {
		backEnd.softDepthBoundImage = NULL;
	}

	backEnd.softDepthCaptured = qtrue;
	return backEnd.softDepthBoundImage;
}

static void RB_SetupSoftParticleUniforms( shaderProgram_t *sp, const shaderStage_t *pStage, image_t *depthImg )
{
	vec4_t softParams, softZ;
	vec2_t fadeMask;
	float zNear = MAX( 0.5f, r_znear->value );
	float zFar  = ( backEnd.viewParms.zFar > zNear + 1.0f ) ? backEnd.viewParms.zFar : 4096.0f;
	float softRange = tess.shader->softParticle * ( ( r_softParticles->integer >= 2 ) ? 1.55f : 1.0f );

	if ( depthImg == tr.softDepthImage ) {
		VectorSet4( softParams,
			(float)backEnd.viewParms.viewportX,
			(float)backEnd.viewParms.viewportY,
			1.0f / (float)MAX( 1, backEnd.viewParms.viewportWidth ),
			1.0f / (float)MAX( 1, backEnd.viewParms.viewportHeight ) );
	} else {
		VectorSet4( softParams,
			(float)backEnd.viewParms.viewportX,
			(float)backEnd.viewParms.viewportY,
			1.0f / (float)MAX( 1, depthImg->width ),
			1.0f / (float)MAX( 1, depthImg->height ) );
	}

	VectorSet4( softZ, zFar * zNear, zFar, zFar - zNear, 1.0f / MAX( 1.0f, softRange ) );
	RB_GetSoftParticleFadeMask( pStage, fadeMask );

	GLSL_SetUniformVec4( sp, UNIFORM_SOFTPARTICLEPARAMS, softParams );
	GLSL_SetUniformVec4( sp, UNIFORM_SOFTPARTICLEZ,      softZ );
	GLSL_SetUniformVec2( sp, UNIFORM_SOFTPARTICLEMASK,   fadeMask );
	GL_BindToTMU( depthImg, TB_SHADOWMAP );
}


static void ComputeShaderColors( shaderStage_t *pStage, vec4_t baseColor, vec4_t vertColor, int blend )
{
	qboolean isBlend = ((blend & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_DST_COLOR)
		|| ((blend & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_ONE_MINUS_DST_COLOR)
		|| ((blend & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_SRC_COLOR)
		|| ((blend & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR);

	qboolean is2DDraw = backEnd.currentEntity == &backEnd.entity2D;

	float overbright = (isBlend || is2DDraw) ? 1.0f : (float)(1 << tr.overbrightBits);

	fog_t *fog;

	baseColor[0] = 
	baseColor[1] =
	baseColor[2] =
	baseColor[3] = 1.0f;

	vertColor[0] =
	vertColor[1] =
	vertColor[2] =
	vertColor[3] = 0.0f;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
		case CGEN_EXACT_VERTEX:
		case CGEN_EXACT_VERTEX_LIT:
			baseColor[0] = 
			baseColor[1] =
			baseColor[2] = 
			baseColor[3] = 0.0f;

			vertColor[0] =
			vertColor[1] =
			vertColor[2] = overbright;
			vertColor[3] = 1.0f;
			break;
		case CGEN_CONST:
			baseColor[0] = pStage->constantColor[0] / 255.0f;
			baseColor[1] = pStage->constantColor[1] / 255.0f;
			baseColor[2] = pStage->constantColor[2] / 255.0f;
			baseColor[3] = pStage->constantColor[3] / 255.0f;
			break;
		case CGEN_VERTEX:
		case CGEN_VERTEX_LIT:
			baseColor[0] =
			baseColor[1] =
			baseColor[2] =
			baseColor[3] = 0.0f;

			vertColor[0] =
			vertColor[1] =
			vertColor[2] =
			vertColor[3] = 1.0f;
			break;
		case CGEN_ONE_MINUS_VERTEX:
			baseColor[0] = 
			baseColor[1] =
			baseColor[2] = 1.0f;

			vertColor[0] =
			vertColor[1] =
			vertColor[2] = -1.0f;
			break;
		case CGEN_FOG:
			fog = tr.world->fogs + tess.fogNum;

			baseColor[0] = ((unsigned char *)(&fog->colorInt))[0] / 255.0f;
			baseColor[1] = ((unsigned char *)(&fog->colorInt))[1] / 255.0f;
			baseColor[2] = ((unsigned char *)(&fog->colorInt))[2] / 255.0f;
			baseColor[3] = ((unsigned char *)(&fog->colorInt))[3] / 255.0f;
			break;
		case CGEN_WAVEFORM:
			baseColor[0] = 
			baseColor[1] = 
			baseColor[2] = RB_CalcWaveColorSingle( &pStage->rgbWave );
			break;
		case CGEN_ENTITY:
			if (backEnd.currentEntity)
			{
				baseColor[0] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[0] / 255.0f;
				baseColor[1] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[1] / 255.0f;
				baseColor[2] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[2] / 255.0f;
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;
			}
			break;
		case CGEN_ONE_MINUS_ENTITY:
			if (backEnd.currentEntity)
			{
				baseColor[0] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[0] / 255.0f;
				baseColor[1] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[1] / 255.0f;
				baseColor[2] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[2] / 255.0f;
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;
			}
			break;
		case CGEN_IDENTITY:
		case CGEN_LIGHTING_DIFFUSE:
			baseColor[0] =
			baseColor[1] =
			baseColor[2] = overbright;
			break;
		case CGEN_IDENTITY_LIGHTING:
		case CGEN_BAD:
			break;
		//
		// OPENMOHAA-specific stuff
		//=========================
		case CGEN_MULTIPLY_BY_WAVEFORM:
		{
			float glow = RB_CalcWaveColorSingle( &pStage->rgbWave );
			baseColor[0] = (pStage->colorConst[0] / 255.0f) * glow;
			baseColor[1] = (pStage->colorConst[1] / 255.0f) * glow;
			baseColor[2] = (pStage->colorConst[2] / 255.0f) * glow;
			break;
		}
		case CGEN_GLOBAL_COLOR:
			baseColor[0] = backEnd.color2D[0] / 255.0f;
			baseColor[1] = backEnd.color2D[1] / 255.0f;
			baseColor[2] = backEnd.color2D[2] / 255.0f;
			baseColor[3] = backEnd.color2D[3] / 255.0f;
			break;
		case CGEN_LIGHTING_GRID:
			if (backEnd.currentStaticModel) {
				const byte *c = (const byte *)&backEnd.currentStaticModel->iGridLighting;
				baseColor[0] = c[0] / 255.0f;
				baseColor[1] = c[1] / 255.0f;
				baseColor[2] = c[2] / 255.0f;
				baseColor[3] = c[3] / 255.0f;
			} else if (backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity) {
				const byte *c = (const byte *)&backEnd.currentEntity->iGridLighting;
				baseColor[0] = c[0] / 255.0f;
				baseColor[1] = c[1] / 255.0f;
				baseColor[2] = c[2] / 255.0f;
				baseColor[3] = c[3] / 255.0f;
			}
			break;
		case CGEN_LIGHTING_SPHERICAL:
			if ( RB_UseModelPerPixelLighting( pStage )
				&& !backEnd.currentSphere->bUsesCubeMap
				&& backEnd.currentSphere->numRealLights <= 4 )
			{
				baseColor[0] = backEnd.currentSphere->ambient.level[0] / 255.0f;
				baseColor[1] = backEnd.currentSphere->ambient.level[1] / 255.0f;
				baseColor[2] = backEnd.currentSphere->ambient.level[2] / 255.0f;
				baseColor[3] = 1.0f;

				vertColor[0] =
				vertColor[1] =
				vertColor[2] =
				vertColor[3] = 0.0f;
				break;
			}
			// fall through
		case CGEN_STATIC:
		case CGEN_DOT:
		case CGEN_ONE_MINUS_DOT:
		case CGEN_SCOORD:
		case CGEN_TCOORD:
			baseColor[0] =
			baseColor[1] =
			baseColor[2] =
			baseColor[3] = 0.0f;

			vertColor[0] =
			vertColor[1] =
			vertColor[2] =
			vertColor[3] = 1.0f;
			break;
		//=========================
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
		case AGEN_SKIP:
			break;
		case AGEN_CONST:
			baseColor[3] = pStage->constantColor[3] / 255.0f;
			vertColor[3] = 0.0f;
			break;
		case AGEN_WAVEFORM:
			baseColor[3] = RB_CalcWaveAlphaSingle( &pStage->alphaWave );
			vertColor[3] = 0.0f;
			break;
		case AGEN_ENTITY:
			if (backEnd.currentEntity)
			{
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;
			}
			vertColor[3] = 0.0f;
			break;
		case AGEN_ONE_MINUS_ENTITY:
			if (backEnd.currentEntity)
			{
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;
			}
			vertColor[3] = 0.0f;
			break;
		case AGEN_GLOBAL_ALPHA:
			baseColor[3] = backEnd.color2D[3] / 255.0f;
			vertColor[3] = 0.0f;
			break;
		case AGEN_SKYALPHA:
			baseColor[3] = tr.refdef.sky_alpha;
			vertColor[3] = 0.0f;
			break;
		case AGEN_ONE_MINUS_SKYALPHA:
			baseColor[3] = 1.0f - tr.refdef.sky_alpha;
			vertColor[3] = 0.0f;
			break;
		case AGEN_TIKI_DIST_FADE:
		case AGEN_ONE_MINUS_TIKI_DIST_FADE:
		{
			float alpha = 1.0f;
			vec3_t org;
			qboolean hasOrigin = qfalse;

			if (backEnd.currentStaticModel) {
				VectorSubtract(backEnd.currentStaticModel->origin, backEnd.viewParms.ori.origin, org);
				hasOrigin = qtrue;
			} else if (backEnd.currentEntity) {
				VectorSubtract(backEnd.currentEntity->e.origin, backEnd.viewParms.ori.origin, org);
				hasOrigin = qtrue;
			}

			if (hasOrigin) {
				float lenSqr = VectorLengthSquared(org);
				float fDistNearSqr = tess.shader->fDistNear * tess.shader->fDistNear;
				float fDistFar = tess.shader->fDistNear + tess.shader->fDistRange;
				float fDistFarSqr = fDistFar * fDistFar;
				if (lenSqr <= fDistNearSqr) {
					alpha = 0.0f;
				} else if (lenSqr >= fDistFarSqr) {
					alpha = 1.0f;
				} else {
					alpha = (VectorLength(org) - tess.shader->fDistNear) / tess.shader->fDistRange;
				}
				if (pStage->alphaGen == AGEN_TIKI_DIST_FADE) {
					alpha = 1.0f - alpha;
				}
			}

			baseColor[3] = alpha;
			vertColor[3] = 0.0f;
			break;
		}
		case AGEN_VERTEX:
		case AGEN_DIST_FADE:
		case AGEN_ONE_MINUS_DIST_FADE:
		case AGEN_DOT:
		case AGEN_ONE_MINUS_DOT:
		case AGEN_DOT_VIEW:
		case AGEN_ONE_MINUS_DOT_VIEW:
		case AGEN_HEIGHT_FADE:
		case AGEN_SCOORD:
		case AGEN_TCOORD:
			baseColor[3] = 0.0f;
			vertColor[3] = 1.0f;
			break;
		case AGEN_ONE_MINUS_VERTEX:
			baseColor[3] = 1.0f;
			vertColor[3] = -1.0f;
			break;
		case AGEN_IDENTITY:
		case AGEN_LIGHTING_SPECULAR:
		case AGEN_PORTAL:
			// Done entirely in vertex program
			baseColor[3] = 1.0f;
			vertColor[3] = 0.0f;
			break;
	}

}

static qboolean RB_StageNeedsCpuColors( shaderStage_t *pStage )
{
	if ( !pStage ) {
		return qfalse;
	}

	switch ( pStage->rgbGen ) {
	case CGEN_LIGHTING_SPHERICAL:
	case CGEN_STATIC:
	case CGEN_DOT:
	case CGEN_ONE_MINUS_DOT:
	case CGEN_SCOORD:
	case CGEN_TCOORD:
		return qtrue;
	default:
		break;
	}

	switch ( pStage->alphaGen ) {
	case AGEN_DIST_FADE:
	case AGEN_ONE_MINUS_DIST_FADE:
	case AGEN_DOT:
	case AGEN_ONE_MINUS_DOT:
	case AGEN_DOT_VIEW:
	case AGEN_ONE_MINUS_DOT_VIEW:
	case AGEN_HEIGHT_FADE:
	case AGEN_SCOORD:
	case AGEN_TCOORD:
		return qtrue;
	default:
		break;
	}

	return qfalse;
}

static void RB_ComputeStageCpuColors( shaderStage_t *pStage )
{
	int i;

	if ( !pStage || !tess.useInternalVao || tess.numVertexes <= 0 ) {
		return;
	}

	switch ( pStage->rgbGen )
	{
	case CGEN_LIGHTING_SPHERICAL:
	case CGEN_STATIC:
		if ( !r_drawspherelights->integer ) {
			break;
		}
		if ( !backEnd.currentStaticModel ) {
			if ( backEnd.currentSphere && backEnd.currentSphere->TessFunction ) {
				if ( RB_UseModelPerPixelLighting( pStage ) ) {
					if ( backEnd.currentSphere->bUsesCubeMap ) {
						byte tempColors[SHADER_MAX_VERTEXES][4];
						RB_Light_CubeMapAmbientOnly( (unsigned char *)tempColors );
						for ( i = 0; i < tess.numVertexes; i++ ) {
							tess.color[i][0] = (uint16_t)tempColors[i][0] * 257;
							tess.color[i][1] = (uint16_t)tempColors[i][1] * 257;
							tess.color[i][2] = (uint16_t)tempColors[i][2] * 257;
							tess.color[i][3] = (uint16_t)tempColors[i][3] * 257;
						}
					} else if ( backEnd.currentSphere->numRealLights > 4 ) {
						byte tempColors[SHADER_MAX_VERTEXES][4];
						RB_Light_Residual( (unsigned char *)tempColors, 4 );
						for ( i = 0; i < tess.numVertexes; i++ ) {
							tess.color[i][0] = (uint16_t)tempColors[i][0] * 257;
							tess.color[i][1] = (uint16_t)tempColors[i][1] * 257;
							tess.color[i][2] = (uint16_t)tempColors[i][2] * 257;
							tess.color[i][3] = (uint16_t)tempColors[i][3] * 257;
						}
					}
					// Otherwise ambient is passed directly via u_BaseColor -> zero CPU RGB loop!
				} else {
					byte tempColors[SHADER_MAX_VERTEXES][4];
					backEnd.currentSphere->TessFunction( (unsigned char *)tempColors );
					for ( i = 0; i < tess.numVertexes; i++ ) {
						tess.color[i][0] = (uint16_t)tempColors[i][0] * 257;
						tess.color[i][1] = (uint16_t)tempColors[i][1] * 257;
						tess.color[i][2] = (uint16_t)tempColors[i][2] * 257;
						tess.color[i][3] = (uint16_t)tempColors[i][3] * 257;
					}
				}
			}
		} else if ( backEnd.currentStaticModel->useSpecialLighting ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				int j;
				vec3_t colorout;
				vec3_t norm;
				int r, g, b;

				colorout[0] = (float)(tess.color[i][0] / 257);
				colorout[1] = (float)(tess.color[i][1] / 257);
				colorout[2] = (float)(tess.color[i][2] / 257);

				R_VaoUnpackNormal( norm, tess.normal[i] );

				for ( j = 0; j < backEnd.currentStaticModel->numdlights; j++ ) {
					float ooLightDistSquared;
					float dot;
					vec3_t diff;
					dlight_t *dl;

					dl = &backEnd.refdef.dlights[backEnd.currentStaticModel->dlights[j].index];
					VectorSubtract( backEnd.currentStaticModel->dlights[j].transformed, tess.xyz[i], diff );

					dot = DotProduct( diff, norm );
					if ( dot >= 0.0f ) {
						float ooLen = 1.0f / VectorLengthSquared( diff );
						ooLightDistSquared = dot * (7500.0f * dl->radius * ooLen * sqrtf( ooLen ));
						colorout[0] += dl->color[0] * ooLightDistSquared;
						colorout[1] += dl->color[1] * ooLightDistSquared;
						colorout[2] += dl->color[2] * ooLightDistSquared;
					}
				}

				r = (int)colorout[0];
				g = (int)colorout[1];
				b = (int)colorout[2];

				if ( r > 0xFF || g > 0xFF || b > 0xFF ) {
					float t = 255.0f / (float)Q_max( r, Q_max( g, b ) );
					r = (int)((float)r * t);
					g = (int)((float)g * t);
					b = (int)((float)b * t);
				}

				tess.color[i][0] = (uint16_t)Q_clamp_int( r, 0, 255 ) * 257;
				tess.color[i][1] = (uint16_t)Q_clamp_int( g, 0, 255 ) * 257;
				tess.color[i][2] = (uint16_t)Q_clamp_int( b, 0, 255 ) * 257;
			}
		}
		break;

	case CGEN_DOT:
	case CGEN_ONE_MINUS_DOT:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			vec3_t viewInModel, norm;
			float f;
			int c;

			R_VaoUnpackNormal( norm, tess.normal[i] );
			VectorSubtract( backEnd.ori.viewOrigin, tess.xyz[i], viewInModel );
			VectorNormalizeFast( viewInModel );

			f = Square( DotProduct( norm, viewInModel ) );
			if ( pStage->rgbGen == CGEN_ONE_MINUS_DOT ) {
				f = 1.0f - f;
			}
			f = ((pStage->alphaMax - pStage->alphaMin) * f + pStage->alphaMin) * 255.0f;
			c = Q_clamp_int( (int)f, 0, 255 ) * 257;
			tess.color[i][0] = tess.color[i][1] = tess.color[i][2] = (uint16_t)c;
		}
		break;

	case CGEN_SCOORD:
	case CGEN_TCOORD:
	{
		int coordIdx = (pStage->rgbGen == CGEN_TCOORD) ? 1 : 0;
		int minCap = pStage->alphaConstMin;
		int maxCap = (pStage->alphaConst < 0) ? 255 : pStage->alphaConst;
		for ( i = 0; i < tess.numVertexes; i++ ) {
			float f = tess.texCoords[i][coordIdx];
			int c;
			f = ((pStage->alphaMax - pStage->alphaMin) * f + pStage->alphaMin) * 255.0f;
			c = Q_clamp_int( (int)f, minCap, maxCap ) * 257;
			tess.color[i][0] = tess.color[i][1] = tess.color[i][2] = (uint16_t)c;
		}
		break;
	}
	default:
		break;
	}

	switch ( pStage->alphaGen )
	{
	case AGEN_DIST_FADE:
	case AGEN_ONE_MINUS_DIST_FADE:
	{
		qboolean isOneMinus = (pStage->alphaGen == AGEN_ONE_MINUS_DIST_FADE);
		vec3_t localView;
		if ( backEnd.currentStaticModel ) {
			vec3_t v;
			VectorSubtract( backEnd.viewParms.ori.origin, backEnd.currentStaticModel->origin, v );
			localView[0] = DotProduct( v, backEnd.currentStaticModel->axis[0] );
			localView[1] = DotProduct( v, backEnd.currentStaticModel->axis[1] );
			localView[2] = DotProduct( v, backEnd.currentStaticModel->axis[2] );
		} else if ( backEnd.currentEntity ) {
			vec3_t v;
			VectorSubtract( backEnd.viewParms.ori.origin, backEnd.currentEntity->e.origin, v );
			localView[0] = DotProduct( v, backEnd.currentEntity->e.axis[0] );
			localView[1] = DotProduct( v, backEnd.currentEntity->e.axis[1] );
			localView[2] = DotProduct( v, backEnd.currentEntity->e.axis[2] );
		} else {
			VectorCopy( backEnd.viewParms.ori.origin, localView );
		}

		for ( i = 0; i < tess.numVertexes; i++ ) {
			vec3_t org;
			float len, a;
			VectorSubtract( tess.xyz[i], localView, org );
			len = (VectorLength( org ) - tess.shader->fDistNear) / tess.shader->fDistRange;
			len = Q_clamp_float( len, 0.0f, 1.0f );
			a = isOneMinus ? len : (1.0f - len);
			tess.color[i][3] = (uint16_t)(a * 65535.0f);
		}
		break;
	}

	case AGEN_DOT:
	case AGEN_ONE_MINUS_DOT:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			vec3_t viewInModel, norm;
			float f;
			int c;

			R_VaoUnpackNormal( norm, tess.normal[i] );
			VectorSubtract( backEnd.ori.viewOrigin, tess.xyz[i], viewInModel );
			VectorNormalizeFast( viewInModel );

			f = Square( DotProduct( norm, viewInModel ) );
			if ( pStage->alphaGen == AGEN_ONE_MINUS_DOT ) {
				f = 1.0f - f;
			}
			f = ((pStage->alphaMax - pStage->alphaMin) * f + pStage->alphaMin) * 255.0f;
			c = Q_clamp_int( (int)f, 0, 255 ) * 257;
			tess.color[i][0] = tess.color[i][1] = tess.color[i][2] = tess.color[i][3] = (uint16_t)c;
		}
		break;

	case AGEN_DOT_VIEW:
	case AGEN_ONE_MINUS_DOT_VIEW:
	{
		vec3_t viewInModel;
		VectorCopy( tr.refdef.viewaxis[0], viewInModel );
		VectorNormalizeFast( viewInModel );
		for ( i = 0; i < tess.numVertexes; i++ ) {
			vec3_t norm;
			float f;
			int c;

			R_VaoUnpackNormal( norm, tess.normal[i] );
			f = fabsf( DotProduct( viewInModel, norm ) );
			if ( pStage->alphaGen == AGEN_ONE_MINUS_DOT_VIEW ) {
				f = 1.0f - f;
			}
			f = ((pStage->alphaMax - pStage->alphaMin) * f + pStage->alphaMin) * 255.0f;
			c = Q_clamp_int( (int)f, 0, 255 ) * 257;
			tess.color[i][3] = (uint16_t)c;
		}
		break;
	}

	case AGEN_HEIGHT_FADE:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			float dist = fabsf( backEnd.ori.viewOrigin[2] - tess.xyz[i][2] );
			float alpha;
			dist = Q_clamp_float( dist, pStage->alphaMin, pStage->alphaMax );
			alpha = 1.0f - (dist - pStage->alphaMin) / (pStage->alphaMax - pStage->alphaMin);
			tess.color[i][3] = (uint16_t)(alpha * 65535.0f);
		}
		break;

	case AGEN_SCOORD:
	case AGEN_TCOORD:
	{
		int coordIdx = (pStage->alphaGen == AGEN_TCOORD) ? 1 : 0;
		int minCap = pStage->alphaConstMin;
		int maxCap = (pStage->alphaConst < 0) ? 255 : pStage->alphaConst;
		for ( i = 0; i < tess.numVertexes; i++ ) {
			float f = tess.texCoords[i][coordIdx];
			int c;
			f = ((pStage->alphaMax - pStage->alphaMin) * f + pStage->alphaMin) * 255.0f;
			c = Q_clamp_int( (int)f, minCap, maxCap ) * 257;
			tess.color[i][3] = (uint16_t)c;
		}
		break;
	}
	default:
		break;
	}
}


static void ComputeFogValues(vec4_t fogDistanceVector, vec4_t fogDepthVector, float *eyeT)
{
	// from RB_CalcFogTexCoords()
	fog_t  *fog;
	vec3_t  local;

	if (!tess.fogNum)
		return;

	fog = tr.world->fogs + tess.fogNum;

	VectorSubtract( backEnd.ori.origin, backEnd.viewParms.ori.origin, local );
	fogDistanceVector[0] = -backEnd.ori.modelMatrix[2];
	fogDistanceVector[1] = -backEnd.ori.modelMatrix[6];
	fogDistanceVector[2] = -backEnd.ori.modelMatrix[10];
	fogDistanceVector[3] = DotProduct( local, backEnd.viewParms.ori.axis[0] );

	// scale the fog vectors based on the fog's thickness
	VectorScale4(fogDistanceVector, fog->tcScale, fogDistanceVector);

	// rotate the gradient vector for this orientation
	if ( fog->hasSurface ) {
		fogDepthVector[0] = fog->surface[0] * backEnd.ori.axis[0][0] + 
			fog->surface[1] * backEnd.ori.axis[0][1] + fog->surface[2] * backEnd.ori.axis[0][2];
		fogDepthVector[1] = fog->surface[0] * backEnd.ori.axis[1][0] + 
			fog->surface[1] * backEnd.ori.axis[1][1] + fog->surface[2] * backEnd.ori.axis[1][2];
		fogDepthVector[2] = fog->surface[0] * backEnd.ori.axis[2][0] + 
			fog->surface[1] * backEnd.ori.axis[2][1] + fog->surface[2] * backEnd.ori.axis[2][2];
		fogDepthVector[3] = -fog->surface[3] + DotProduct( backEnd.ori.origin, fog->surface );

		*eyeT = DotProduct( backEnd.ori.viewOrigin, fogDepthVector ) + fogDepthVector[3];
	} else {
		*eyeT = 1;	// non-surface fog always has eye inside
	}
}


static void ComputeFogColorMask( shaderStage_t *pStage, vec4_t fogColorMask )
{
	switch(pStage->adjustColorsForFog)
	{
		case ACFF_MODULATE_RGB:
			fogColorMask[0] =
			fogColorMask[1] =
			fogColorMask[2] = 1.0f;
			fogColorMask[3] = 0.0f;
			break;
		case ACFF_MODULATE_ALPHA:
			fogColorMask[0] =
			fogColorMask[1] =
			fogColorMask[2] = 0.0f;
			fogColorMask[3] = 1.0f;
			break;
		case ACFF_MODULATE_RGBA:
			fogColorMask[0] =
			fogColorMask[1] =
			fogColorMask[2] =
			fogColorMask[3] = 1.0f;
			break;
		default:
			fogColorMask[0] =
			fogColorMask[1] =
			fogColorMask[2] =
			fogColorMask[3] = 0.0f;
			break;
	}
}


static void ForwardDlight( void ) {
	int		l;
	//vec3_t	origin;
	//float	scale;
	float	radius;

	int deformGen;
	vec5_t deformParams;
	
	vec4_t fogDistanceVector, fogDepthVector = {0, 0, 0, 0};
	float eyeT = 0;

	shaderCommands_t *input = &tess;
	shaderStage_t *pStage = tess.xstages[0];

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}
	
	ComputeDeformValues(&deformGen, deformParams);

	ComputeFogValues(fogDistanceVector, fogDepthVector, &eyeT);

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;
		shaderProgram_t *sp;
		vec4_t vector;
		vec4_t texMatrix[8];

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this light
		}

		dl = &backEnd.refdef.dlights[l];
		//VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		//scale = 1.0f / radius;

		//if (pStage->glslShaderGroup == tr.lightallShader)
		{
			int index = pStage->glslShaderIndex;

			index &= ~LIGHTDEF_LIGHTTYPE_MASK;
			index |= LIGHTDEF_USE_LIGHT_VECTOR;

			sp = &tr.lightallShader[index];
		}

		backEnd.pc.c_lightallDraws++;

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);

		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		GLSL_SetUniformVec3(sp, UNIFORM_LOCALVIEWORIGIN, backEnd.ori.viewOrigin);

		GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);

		GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
		if (deformGen != DGEN_NONE)
		{
			GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
			GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
		}

		if ( input->fogNum ) {
			vec4_t fogColorMask;

			GLSL_SetUniformVec4(sp, UNIFORM_FOGDISTANCE, fogDistanceVector);
			GLSL_SetUniformVec4(sp, UNIFORM_FOGDEPTH, fogDepthVector);
			GLSL_SetUniformFloat(sp, UNIFORM_FOGEYET, eyeT);

			ComputeFogColorMask(pStage, fogColorMask);

			GLSL_SetUniformVec4(sp, UNIFORM_FOGCOLORMASK, fogColorMask);
		}

		{
			vec4_t baseColor;
			vec4_t vertColor;

			ComputeShaderColors(pStage, baseColor, vertColor, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

			GLSL_SetUniformVec4(sp, UNIFORM_BASECOLOR, baseColor);
			GLSL_SetUniformVec4(sp, UNIFORM_VERTCOLOR, vertColor);
		}

		if (pStage->alphaGen == AGEN_PORTAL)
		{
			GLSL_SetUniformFloat(sp, UNIFORM_PORTALRANGE, tess.shader->portalRange);
		}

		GLSL_SetUniformInt(sp, UNIFORM_COLORGEN, pStage->rgbGen);
		GLSL_SetUniformInt(sp, UNIFORM_ALPHAGEN, pStage->alphaGen);

		GLSL_SetUniformVec3(sp, UNIFORM_DIRECTEDLIGHT, dl->color);

		VectorSet(vector, 0, 0, 0);
		GLSL_SetUniformVec3(sp, UNIFORM_AMBIENTLIGHT, vector);

		VectorCopy(dl->origin, vector);
		vector[3] = 1.0f;
		GLSL_SetUniformVec4(sp, UNIFORM_LIGHTORIGIN, vector);

		GLSL_SetUniformFloat(sp, UNIFORM_LIGHTRADIUS, radius);

		GLSL_SetUniformVec4(sp, UNIFORM_NORMALSCALE, pStage->normalScale);
		GLSL_SetUniformVec4(sp, UNIFORM_SPECULARSCALE, pStage->specularScale);
		
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);
		{
			vec4_t fogColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			vec2_t fogParams = { 0.0f, 0.0f };
			if ( !backEnd.projection2D && !backEnd.depthFill
				&& !(backEnd.viewParms.flags & VPF_DEPTHSHADOW)
				&& !(backEnd.refdef.rdflags & RDF_NOWORLDMODEL)
				&& !tess.shader->isSky && !pStage->noFog
				&& backEnd.viewParms.farplane_distance > 0.0f
				&& !r_farplane_nofog->integer )
			{
				float fogRange = backEnd.viewParms.farplane_distance - backEnd.viewParms.farplane_bias;
				fogColor[3] = 1.0f; // additive dlight fades toward black
				fogParams[0] = backEnd.viewParms.farplane_bias;
				fogParams[1] = (fogRange > 1.0f) ? (1.0f / fogRange) : 0.0f;
			}
			GLSL_SetUniformVec4(sp, UNIFORM_FARPLANEFOGCOLOR, fogColor);
			GLSL_SetUniformVec2(sp, UNIFORM_FARPLANEFOGPARAMS, fogParams);
		}

		GLSL_SetUniformMat4(sp, UNIFORM_MODELMATRIX, backEnd.ori.transformMatrix);

		if (pStage->bundle[TB_DIFFUSEMAP].image[0])
			R_BindAnimatedImageToTMU( &pStage->bundle[TB_DIFFUSEMAP], TB_DIFFUSEMAP);

		// bind textures that are sampled and used in the glsl shader, and
		// bind whiteImage to textures that are sampled but zeroed in the glsl shader
		//
		// alternatives:
		//  - use the last bound texture
		//     -> costs more to sample a higher res texture then throw out the result
		//  - disable texture sampling in glsl shader with #ifdefs, as before
		//     -> increases the number of shaders that must be compiled
		//

		if (pStage->bundle[TB_NORMALMAP].image[0])
		{
			R_BindAnimatedImageToTMU( &pStage->bundle[TB_NORMALMAP], TB_NORMALMAP);
		}
		else if (r_normalMapping->integer)
			GL_BindToTMU( tr.whiteImage, TB_NORMALMAP );

		if (pStage->bundle[TB_SPECULARMAP].image[0])
		{
			R_BindAnimatedImageToTMU( &pStage->bundle[TB_SPECULARMAP], TB_SPECULARMAP);
		}
		else if (r_specularMapping->integer)
			GL_BindToTMU( tr.whiteImage, TB_SPECULARMAP );

		{
			vec4_t enableTextures;

			VectorSet4(enableTextures, 0.0f, 0.0f, 0.0f, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_ENABLETEXTURES, enableTextures);
		}

		if (r_dlightMode->integer >= 2)
			GL_BindToTMU(tr.shadowCubemaps[l], TB_SHADOWMAP);

		ComputeTexMods( pStage, TB_DIFFUSEMAP, texMatrix );
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX0, texMatrix[0]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX1, texMatrix[1]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX2, texMatrix[2]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX3, texMatrix[3]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX4, texMatrix[4]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX5, texMatrix[5]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX6, texMatrix[6]);
		GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX7, texMatrix[7]);

		GLSL_SetUniformInt(sp, UNIFORM_TCGEN0, pStage->bundle[0].tcGen);

		//
		// draw
		//

		R_DrawElements(input->numIndexes, input->firstIndex);

		backEnd.pc.c_totalIndexes += tess.numIndexes;
		backEnd.pc.c_dlightIndexes += tess.numIndexes;
		backEnd.pc.c_dlightVertexes += tess.numVertexes;
	}
}


static void ProjectPshadowVBOGLSL( void ) {
	int		l;
	vec3_t	origin;
	float	radius;

	int deformGen;
	vec5_t deformParams;

	shaderCommands_t *input = &tess;

	if ( !backEnd.refdef.num_pshadows ) {
		return;
	}
	
	ComputeDeformValues(&deformGen, deformParams);

	for ( l = 0 ; l < backEnd.refdef.num_pshadows ; l++ ) {
		pshadow_t	*ps;
		shaderProgram_t *sp;
		vec4_t vector;

		if ( !( tess.pshadowBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this shadow
		}

		ps = &backEnd.refdef.pshadows[l];
		VectorCopy( ps->lightOrigin, origin );
		radius = ps->lightRadius;

		sp = &tr.pshadowShader;

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);

		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);

		VectorCopy(origin, vector);
		vector[3] = 1.0f;
		GLSL_SetUniformVec4(sp, UNIFORM_LIGHTORIGIN, vector);

		VectorScale(ps->lightViewAxis[0], 1.0f / ps->viewRadius, vector);
		GLSL_SetUniformVec3(sp, UNIFORM_LIGHTFORWARD, vector);

		VectorScale(ps->lightViewAxis[1], 1.0f / ps->viewRadius, vector);
		GLSL_SetUniformVec3(sp, UNIFORM_LIGHTRIGHT, vector);

		VectorScale(ps->lightViewAxis[2], 1.0f / ps->viewRadius, vector);
		GLSL_SetUniformVec3(sp, UNIFORM_LIGHTUP, vector);

		GLSL_SetUniformFloat(sp, UNIFORM_LIGHTRADIUS, radius);
	  
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
		GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);

		GL_BindToTMU( tr.pshadowMaps[l], TB_DIFFUSEMAP );

		//
		// draw
		//

		R_DrawElements(input->numIndexes, input->firstIndex);

		backEnd.pc.c_totalIndexes += tess.numIndexes;
		//backEnd.pc.c_dlightIndexes += tess.numIndexes;
	}
}



/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t		*fog;
	vec4_t  color;
	vec4_t	fogDistanceVector, fogDepthVector = {0, 0, 0, 0};
	float	eyeT = 0;
	shaderProgram_t *sp;

	int deformGen;
	vec5_t deformParams;

	ComputeDeformValues(&deformGen, deformParams);

	{
		int index = 0;

		if (deformGen != DGEN_NONE)
			index |= FOGDEF_USE_DEFORM_VERTEXES;

		if (glState.vertexAnimation)
			index |= FOGDEF_USE_VERTEX_ANIMATION;
		else if (glState.boneAnimation)
			index |= FOGDEF_USE_BONE_ANIMATION;
		
		sp = &tr.fogShader[index];
	}

	backEnd.pc.c_fogDraws++;

	GLSL_BindProgram(sp);

	fog = tr.world->fogs + tess.fogNum;

	GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);

	GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);

	if (glState.boneAnimation)
	{
		GLSL_SetUniformMat4BoneMatrix(sp, UNIFORM_BONEMATRIX, glState.boneMatrix, glState.boneAnimation);
	}
	
	GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
	if (deformGen != DGEN_NONE)
	{
		GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
		GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
	}

	color[0] = ((unsigned char *)(&fog->colorInt))[0] / 255.0f;
	color[1] = ((unsigned char *)(&fog->colorInt))[1] / 255.0f;
	color[2] = ((unsigned char *)(&fog->colorInt))[2] / 255.0f;
	color[3] = ((unsigned char *)(&fog->colorInt))[3] / 255.0f;
	GLSL_SetUniformVec4(sp, UNIFORM_COLOR, color);

	ComputeFogValues(fogDistanceVector, fogDepthVector, &eyeT);

	GLSL_SetUniformVec4(sp, UNIFORM_FOGDISTANCE, fogDistanceVector);
	GLSL_SetUniformVec4(sp, UNIFORM_FOGDEPTH, fogDepthVector);
	GLSL_SetUniformFloat(sp, UNIFORM_FOGEYET, eyeT);

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}
	GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);

	R_DrawElements(tess.numIndexes, tess.firstIndex);
}


static unsigned int RB_CalcShaderVertexAttribs( shaderCommands_t *input )
{
	unsigned int vertexAttribs = input->shader->vertexAttribs;

	if(glState.vertexAnimation)
	{
		vertexAttribs |= ATTR_POSITION2;
		if (vertexAttribs & ATTR_NORMAL)
		{
			vertexAttribs |= ATTR_NORMAL2;
			vertexAttribs |= ATTR_TANGENT2;
		}
	}

	return vertexAttribs;
}

static void RB_SetFarplaneFogUniforms( shaderProgram_t *sp, const shaderStage_t *pStage, unsigned int stateBits )
{
	vec4_t fogColor;
	vec2_t fogParams;

	if ( !backEnd.projection2D
		&& !backEnd.depthFill
		&& !( backEnd.viewParms.flags & VPF_DEPTHSHADOW )
		&& !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		&& !tess.shader->isSky
		&& ( !pStage || !pStage->noFog )
		&& backEnd.viewParms.farplane_distance > 0.0f
		&& !r_farplane_nofog->integer )
	{
		unsigned int blendBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
		float fogRange = backEnd.viewParms.farplane_distance - backEnd.viewParms.farplane_bias;

		fogColor[3] = 1.0f;

		if ( blendBits ) {
			unsigned int blendSrc = stateBits & GLS_SRCBLEND_BITS;
			unsigned int blendDst = stateBits & GLS_DSTBLEND_BITS;

			if ( ( blendSrc == GLS_SRCBLEND_ONE && blendDst == GLS_DSTBLEND_ONE )
				|| ( blendSrc == GLS_SRCBLEND_ZERO && blendDst == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR )
				|| ( blendSrc == GLS_SRCBLEND_SRC_ALPHA && blendDst == GLS_DSTBLEND_ONE )
				|| ( blendSrc == GLS_SRCBLEND_DST_COLOR && blendDst == GLS_DSTBLEND_ONE )
				|| ( blendSrc == GLS_SRCBLEND_ONE_MINUS_DST_COLOR && blendDst == GLS_DSTBLEND_ONE ) )
			{
				// Additive / subtractive-source blend: fade toward black
				fogColor[0] = 0.0f;
				fogColor[1] = 0.0f;
				fogColor[2] = 0.0f;
			}
			else if ( ( blendSrc == GLS_SRCBLEND_DST_COLOR && blendDst == GLS_DSTBLEND_ZERO )
				|| ( blendSrc == GLS_SRCBLEND_ZERO && blendDst == GLS_DSTBLEND_SRC_COLOR ) )
			{
				// Multiplicative filter blend: fade toward white
				fogColor[0] = 1.0f;
				fogColor[1] = 1.0f;
				fogColor[2] = 1.0f;
			}
			else if ( ( blendSrc == GLS_SRCBLEND_SRC_ALPHA && blendDst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
				|| ( blendSrc == GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA && blendDst == GLS_DSTBLEND_SRC_ALPHA ) )
			{
				// Standard alpha blend: fade toward global farplane fog color
				fogColor[0] = backEnd.viewParms.farplane_color[0] * tr.identityLight;
				fogColor[1] = backEnd.viewParms.farplane_color[1] * tr.identityLight;
				fogColor[2] = backEnd.viewParms.farplane_color[2] * tr.identityLight;
			}
			else
			{
				// Unfoggable blend mode (matches renderergl1)
				fogColor[0] = 0.0f;
				fogColor[1] = 0.0f;
				fogColor[2] = 0.0f;
				fogColor[3] = 0.0f;
			}
		} else {
			fogColor[0] = backEnd.viewParms.farplane_color[0] * tr.identityLight;
			fogColor[1] = backEnd.viewParms.farplane_color[1] * tr.identityLight;
			fogColor[2] = backEnd.viewParms.farplane_color[2] * tr.identityLight;
		}

		if ( fogColor[3] > 0.0f ) {
			fogParams[0] = backEnd.viewParms.farplane_bias;
			fogParams[1] = ( fogRange > 1.0f ) ? ( 1.0f / fogRange ) : 0.0f;
		} else {
			fogParams[0] = 0.0f;
			fogParams[1] = 0.0f;
		}
	} else {
		fogColor[0] = 0.0f;
		fogColor[1] = 0.0f;
		fogColor[2] = 0.0f;
		fogColor[3] = 0.0f;
		fogParams[0] = 0.0f;
		fogParams[1] = 0.0f;
	}

	GLSL_SetUniformVec4( sp, UNIFORM_FARPLANEFOGCOLOR, fogColor );
	GLSL_SetUniformVec2( sp, UNIFORM_FARPLANEFOGPARAMS, fogParams );
}

static void RB_PackModelLight( const reallightinfo_t *pLight, const sphereor_t *pSphere, vec4_t lpos, vec4_t lcol )
{
	switch ( pLight->eType ) {
	case LIGHT_DIRECTIONAL:
		VectorCopy( pLight->vDirection, lpos );
		VectorNormalize( lpos );
		lpos[3] = 0.0f;
		lcol[0] = pLight->color[0] * (1.0f / 255.0f);
		lcol[1] = pLight->color[1] * (1.0f / 255.0f);
		lcol[2] = pLight->color[2] * (1.0f / 255.0f);
		lcol[3] = 0.0f;
		break;
	case LIGHT_POINT:
		VectorCopy( pLight->vOrigin, lpos );
		lpos[3] = 1.0f;
		lcol[0] = pLight->color[0] * (1.0f / 255.0f);
		lcol[1] = pLight->color[1] * (1.0f / 255.0f);
		lcol[2] = pLight->color[2] * (1.0f / 255.0f);
		lcol[3] = 0.0f;
		break;
	case LIGHT_SPOT_FAST:
	case LIGHT_SPOT:
	default:
	{
		vec3_t toL;
		float distSq, atten;

		VectorSubtract( pLight->vOrigin, pSphere->origin, toL );
		distSq = VectorLengthSquared( toL );
		if ( distSq < 1.0f ) {
			distSq = 1.0f;
		}
		VectorCopy( pLight->vDirection, lpos );
		VectorNormalize( lpos );
		lpos[3] = 0.0f;

		if ( pLight->eType == LIGHT_SPOT ) {
			float proj = DotProduct( toL, lpos );
			float projSq = proj * proj;
			float cone = ( pLight->fSpotConst - distSq / ( projSq > 1e-4f ? projSq : 1e-4f ) ) * pLight->fSpotScale;
			if ( cone < 0.15f ) {
				cone = ( proj > 0.0f ) ? 0.15f : 0.0f;
			} else if ( cone > 1.0f ) {
				cone = 1.0f;
			}
			atten = cone / distSq;
		} else {
			atten = 1.0f / distSq;
		}

		lcol[0] = pLight->color[0] * ( atten * (1.0f / 255.0f) );
		lcol[1] = pLight->color[1] * ( atten * (1.0f / 255.0f) );
		lcol[2] = pLight->color[2] * ( atten * (1.0f / 255.0f) );
		lcol[3] = 0.0f;
		break;
	}
	}
}

static void RB_SetWindUniform( shaderProgram_t *sp )
{
	vec4_t windParams = { 0.0f, 0.0f, 0.0f, 0.0f };

	if ( !sp || sp->uniforms[UNIFORM_WINDPARAMS] < 0 ) {
		return;
	}

	if ( r_foliageWind && r_foliageWind->integer > 0
		&& tess.shader && tess.shader->windType > 0
		&& !backEnd.projection2D
		&& !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		float instPhase = 0.0f;
		if ( backEnd.currentStaticModel ) {
			instPhase = backEnd.currentStaticModel->origin[0] * 0.017f
				+ backEnd.currentStaticModel->origin[1] * 0.013f;
		} else if ( backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity ) {
			instPhase = backEnd.currentEntity->e.origin[0] * 0.017f
				+ backEnd.currentEntity->e.origin[1] * 0.013f;
		}

		windParams[0] = ( tess.shader->windType == 2 ) ? 3.2f : 2.1f;
		windParams[1] = (float)backEnd.refdef.floatTime * 1.85f;
		windParams[2] = instPhase;
		windParams[3] = (float)tess.shader->windType;
	}

	GLSL_SetUniformVec4( sp, UNIFORM_WINDPARAMS, windParams );
}

static void RB_IterateStagesGeneric( shaderCommands_t *input )
{
	int stage;
	
	vec4_t fogDistanceVector, fogDepthVector = {0, 0, 0, 0};
	float eyeT = 0;

	int deformGen;
	vec5_t deformParams;

	qboolean renderToCubemap = tr.renderCubeFbo && glState.currentFBO == tr.renderCubeFbo;

	ComputeDeformValues(&deformGen, deformParams);

	ComputeFogValues(fogDistanceVector, fogDepthVector, &eyeT);

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = input->xstages[stage];
		shaderProgram_t *sp;
		vec4_t texMatrix[8];
		qboolean useModelPerPixel = qfalse;
		qboolean useModernWater = qfalse;
		qboolean useSoftParticles = qfalse;
		image_t *softDepthImg = NULL;

		if ( !pStage )
		{
			break;
		}

		if ( pStage->alphaGen == AGEN_SKYALPHA && tr.refdef.sky_alpha < 0.01f ) {
			continue;
		} else if ( pStage->alphaGen == AGEN_ONE_MINUS_SKYALPHA && tr.refdef.sky_alpha > 0.99f ) {
			continue;
		}

		if ( stage > 0 && tess.useInternalVao && RB_StageNeedsCpuColors( pStage ) ) {
			RB_ComputeStageCpuColors( pStage );
			RB_UpdateTessVao( RB_CalcShaderVertexAttribs( input ) );
		}

		if (backEnd.depthFill)
		{
			if (pStage->glslShaderGroup == tr.lightallShader)
			{
				int index = 0;

				if (backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity)
				{
					if (glState.boneAnimation)
					{
						index |= LIGHTDEF_ENTITY_BONE_ANIMATION;
					}
					else
					{
						index |= LIGHTDEF_ENTITY_VERTEX_ANIMATION;
					}
				}

				if (pStage->stateBits & GLS_ATEST_BITS)
				{
					index |= LIGHTDEF_USE_TCGEN_AND_TCMOD | LIGHTDEF_USE_ALPHATEST;
				}

				sp = &pStage->glslShaderGroup[index];
			}
			else
			{
				int shaderAttribs = 0;

				if (tess.shader->numDeforms && !ShaderRequiresCPUDeforms(tess.shader))
				{
					shaderAttribs |= GENERICDEF_USE_DEFORM_VERTEXES;
				}

				if (glState.vertexAnimation)
				{
					shaderAttribs |= GENERICDEF_USE_VERTEX_ANIMATION;
				}
				else if (glState.boneAnimation)
				{
					shaderAttribs |= GENERICDEF_USE_BONE_ANIMATION;
				}

				if (pStage->stateBits & GLS_ATEST_BITS)
				{
					shaderAttribs |= GENERICDEF_USE_TCGEN_AND_TCMOD | GENERICDEF_USE_ALPHATEST;
				}

				sp = &tr.genericShader[shaderAttribs];
			}
		}
		else if ( RB_UseModelPerPixelLighting( pStage ) )
		{
			int index = 0;
			useModelPerPixel = qtrue;

			if ( pStage->bundle[0].tcGen != TCGEN_TEXTURE || pStage->bundle[0].numTexMods )
			{
				index |= MODELDEF_USE_TCGEN_AND_TCMOD;
			}

			if ( pStage->stateBits & GLS_ATEST_BITS )
			{
				index |= MODELDEF_USE_ALPHATEST;
			}

			if ( tess.shader->modelMaterialMode != 3
				&& ( pStage->stateBits & GLS_DSTBLEND_BITS ) != GLS_DSTBLEND_ONE )
			{
				index |= MODELDEF_USE_SPECULAR;
			}

			if ( tr.piSunShadowImage
				&& backEnd.refdef.piSunShadowActive
				&& r_dynamicSunShadows && r_dynamicSunShadows->integer > 0
				&& !backEnd.viewParms.isPortalSky
				&& ( pStage->stateBits & GLS_DSTBLEND_BITS ) != GLS_DSTBLEND_ONE
				&& tr.modelLightShader[index | MODELDEF_USE_SUN_SHADOW].program )
			{
				index |= MODELDEF_USE_SUN_SHADOW;
			}

			sp = &tr.modelLightShader[index];
			backEnd.pc.c_lightallDraws++;
		}
		else if ( RB_UseModernWater( stage, pStage ) )
		{
			int index = 0;
			useModernWater = qtrue;

			if ( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) )
			{
				index |= WATERDEF_USE_DEFORM_VERTEXES;
			}

			if ( pStage->bundle[1].image[0] != 0 )
			{
				if ( pStage->bundle[1].isLightmap )
				{
					index |= WATERDEF_USE_LIGHTMAP;
				}
				else
				{
					index |= WATERDEF_USE_MULTITEXTURE;
				}
			}

			sp = &tr.waterShader[index];
			backEnd.pc.c_genericDraws++;
		}
		else if (pStage->glslShaderGroup == tr.lightallShader)
		{
			int index = pStage->glslShaderIndex;

			if (backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity)
			{
				if (glState.boneAnimation)
				{
					index |= LIGHTDEF_ENTITY_BONE_ANIMATION;
				}
				else
				{
					index |= LIGHTDEF_ENTITY_VERTEX_ANIMATION;
				}
			}

			if (r_sunlightMode->integer && (backEnd.viewParms.flags & VPF_USESUNLIGHT) && (index & LIGHTDEF_LIGHTTYPE_MASK))
			{
				index |= LIGHTDEF_USE_SHADOWMAP;
			}

			if (r_lightmap->integer && ((index & LIGHTDEF_LIGHTTYPE_MASK) == LIGHTDEF_USE_LIGHTMAP))
			{
				index = LIGHTDEF_USE_TCGEN_AND_TCMOD;
			}

			if (pStage->stateBits & GLS_ATEST_BITS)
			{
				index |= LIGHTDEF_USE_ALPHATEST;
			}

			if (tr.piSunShadowImage
				&& backEnd.refdef.piSunShadowActive
				&& r_dynamicSunShadows && r_dynamicSunShadows->integer > 0
				&& !backEnd.viewParms.isPortalSky
				&& ((index & LIGHTDEF_LIGHTTYPE_MASK) == LIGHTDEF_USE_LIGHTMAP)
				&& !(index & LIGHTDEF_USE_SHADOWMAP)
				&& (pStage->stateBits & GLS_DSTBLEND_BITS) != GLS_DSTBLEND_ONE
				&& pStage->glslShaderGroup[index | LIGHTDEF_USE_PISUNSHADOW].program)
			{
				index |= LIGHTDEF_USE_PISUNSHADOW;
			}

			sp = &pStage->glslShaderGroup[index];

			backEnd.pc.c_lightallDraws++;
		}
		else
		{
			if ( RB_UseSoftParticles( pStage ) )
			{
				softDepthImg = RB_EnsureSoftParticleDepth();
				if ( softDepthImg )
				{
					int index = 0;
					useSoftParticles = qtrue;
					if ( pStage->bundle[1].image[0] != 0 )
					{
						index |= SOFTPARTDEF_USE_MULTITEXTURE | SOFTPARTDEF_USE_TCGEN_AND_TCMOD;
					}
					else if ( pStage->bundle[0].tcGen != TCGEN_TEXTURE || pStage->bundle[0].numTexMods )
					{
						index |= SOFTPARTDEF_USE_TCGEN_AND_TCMOD;
					}
					if ( tess.fogNum && pStage->adjustColorsForFog )
					{
						index |= SOFTPARTDEF_USE_FOG;
					}
					sp = &tr.softParticleShader[index];
				}
				else
				{
					sp = GLSL_GetGenericShaderProgram(stage);
				}
			}
			else
			{
				sp = GLSL_GetGenericShaderProgram(stage);
			}

			backEnd.pc.c_genericDraws++;
		}

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);

		if ( backEnd.depthFill && ( backEnd.viewParms.flags & VPF_PISUNSHADOW ) )
		{
			unsigned int atestBits = pStage->stateBits & GLS_ATEST_BITS;

			GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
			if ( glState.vertexAnimation ) {
				GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);
			}
			if ( glState.boneAnimation ) {
				GLSL_SetUniformMat4BoneMatrix(sp, UNIFORM_BONEMATRIX, glState.boneMatrix, glState.boneAnimation);
			}
			GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
			if ( deformGen != DGEN_NONE ) {
				GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
				GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
			}

			GL_State( pStage->stateBits & ( GLS_ATEST_BITS | GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_BITS ) );

			if ( atestBits ) {
				vec4_t baseColor, vertColor;

				if ( atestBits == GLS_ATEST_GT_0 )
					GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 1);
				else if ( atestBits == GLS_ATEST_LT_80 )
					GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 2);
				else
					GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 3);

				ComputeShaderColors(pStage, baseColor, vertColor, pStage->stateBits);
				GLSL_SetUniformVec4(sp, UNIFORM_BASECOLOR, baseColor);
				GLSL_SetUniformVec4(sp, UNIFORM_VERTCOLOR, vertColor);

				if ( sp->uniforms[UNIFORM_DIFFUSETEXMATRIX0] >= 0 ) {
					ComputeTexMods(pStage, TB_DIFFUSEMAP, texMatrix);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX0, texMatrix[0]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX1, texMatrix[1]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX2, texMatrix[2]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX3, texMatrix[3]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX4, texMatrix[4]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX5, texMatrix[5]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX6, texMatrix[6]);
					GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX7, texMatrix[7]);
					GLSL_SetUniformInt(sp, UNIFORM_TCGEN0, pStage->bundle[0].tcGen);
				}

				if ( pStage->bundle[TB_COLORMAP].image[0] != 0 )
					R_BindAnimatedImageToTMU( &pStage->bundle[TB_COLORMAP], TB_COLORMAP );
				else
					GL_BindToTMU( tr.whiteImage, TB_COLORMAP );
			} else {
				GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);
			}

			R_DrawElements(input->numIndexes, input->firstIndex);
			break;
		}

		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		GLSL_SetUniformVec3(sp, UNIFORM_LOCALVIEWORIGIN, backEnd.ori.viewOrigin);

		GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);

		if (glState.boneAnimation)
		{
			GLSL_SetUniformMat4BoneMatrix(sp, UNIFORM_BONEMATRIX, glState.boneMatrix, glState.boneAnimation);
		}
		
		GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
		if (deformGen != DGEN_NONE)
		{
			GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
			GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
		}
		else if (useModernWater)
		{
			GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
		}

		if (useModernWater)
		{
			RB_SetupWaterUniforms(sp);
		}
		if (useSoftParticles)
		{
			RB_SetupSoftParticleUniforms(sp, pStage, softDepthImg);
		}

		if ( input->fogNum ) {
			GLSL_SetUniformVec4(sp, UNIFORM_FOGDISTANCE, fogDistanceVector);
			GLSL_SetUniformVec4(sp, UNIFORM_FOGDEPTH, fogDepthVector);
			GLSL_SetUniformFloat(sp, UNIFORM_FOGEYET, eyeT);
		}

		if ( backEnd.depthFill && ( backEnd.viewParms.flags & VPF_PISUNSHADOW ) )
		{
			GL_State( pStage->stateBits & ( GLS_ATEST_BITS | GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_BITS ) );
		}
		else
		{
			GL_State( pStage->stateBits );
		}
		if ((pStage->stateBits & GLS_ATEST_BITS) == GLS_ATEST_GT_0)
		{
			GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 1);
		}
		else if ((pStage->stateBits & GLS_ATEST_BITS) == GLS_ATEST_LT_80)
		{
			GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 2);
		}
		else if ((pStage->stateBits & GLS_ATEST_BITS) == GLS_ATEST_GE_80)
		{
			GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 3);
		}
		else
		{
			GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);
		}

		RB_SetFarplaneFogUniforms(sp, pStage, pStage->stateBits);

		if ( useModelPerPixel )
		{
			static const int posUniforms[4] = { UNIFORM_MODELLIGHTPOS0,   UNIFORM_MODELLIGHTPOS1,   UNIFORM_MODELLIGHTPOS2,   UNIFORM_MODELLIGHTPOS3 };
			static const int colUniforms[4] = { UNIFORM_MODELLIGHTCOLOR0, UNIFORM_MODELLIGHTCOLOR1, UNIFORM_MODELLIGHTCOLOR2, UNIFORM_MODELLIGHTCOLOR3 };
			const sphereor_t *pSphere = backEnd.currentSphere;
			int numLights = pSphere->numRealLights;
			int order[4] = { 0, 1, 2, 3 };
			int l, k;
			vec3_t worldUp = { 0.0f, 0.0f, 1.0f };
			vec3_t localUp;

			if ( numLights > 4 ) {
				numLights = 4;
			}

			// Sort up to 4 lights by intensity so the 2 strongest get per-pixel lighting/specular
			// and lights 3..4 get per-vertex fill in modellight_vp.glsl
			for ( l = 0; l < numLights - 1; l++ ) {
				for ( k = l + 1; k < numLights; k++ ) {
					if ( pSphere->light[order[k]].fIntensity > pSphere->light[order[l]].fIntensity ) {
						int tmp = order[l];
						order[l] = order[k];
						order[k] = tmp;
					}
				}
			}

			GLSL_SetUniformInt( sp, UNIFORM_MODELLIGHTCOUNT, numLights );

			if ( numLights == 0 ) {
				vec4_t zpos = { 0.0f, 0.0f, 1.0f, 0.0f };
				vec4_t zcol = { 0.0f, 0.0f, 0.0f, 0.0f };
				GLSL_SetUniformVec4( sp, posUniforms[0], zpos );
				GLSL_SetUniformVec4( sp, colUniforms[0], zcol );
			} else {
				for ( l = 0; l < numLights; l++ ) {
					vec4_t lpos, lcol;
					RB_PackModelLight( &pSphere->light[order[l]], pSphere, lpos, lcol );
					GLSL_SetUniformVec4( sp, posUniforms[l], lpos );
					GLSL_SetUniformVec4( sp, colUniforms[l], lcol );
				}
			}

			MatrixTransformVectorRight( backEnd.currentEntity->e.axis, worldUp, localUp );
			if ( VectorNormalize( localUp ) < 1e-6f ) {
				VectorSet( localUp, 0.0f, 0.0f, 1.0f );
			}
			GLSL_SetUniformVec3( sp, UNIFORM_MODELLOCALUP, localUp );

			if ( sp->uniforms[UNIFORM_MODELMATERIAL] >= 0 ) {
				vec3_t worldRight, screenRight, screenUp;
				vec4_t mat, mat2;
				float boost = (r_modelPerPixelLighting->integer >= 2) ? 1.45f : 1.0f;

				VectorNegate( backEnd.viewParms.ori.axis[1], worldRight );
				MatrixTransformVectorRight( backEnd.currentEntity->e.axis, worldRight, screenRight );
				if ( VectorNormalize( screenRight ) < 1e-6f ) {
					VectorSet( screenRight, 1.0f, 0.0f, 0.0f );
				}
				MatrixTransformVectorRight( backEnd.currentEntity->e.axis, backEnd.viewParms.ori.axis[2], screenUp );
				if ( VectorNormalize( screenUp ) < 1e-6f ) {
					VectorSet( screenUp, 0.0f, 0.0f, 1.0f );
				}
				GLSL_SetUniformVec3( sp, UNIFORM_MODELSCREENRIGHT, screenRight );
				GLSL_SetUniformVec3( sp, UNIFORM_MODELSCREENUP, screenUp );

				mat[0] = tess.shader->modelMaterial[0] * boost;
				mat[1] = tess.shader->modelMaterial[1] * boost;
				mat[2] = tess.shader->modelMaterial[2] * boost;
				mat[3] = tess.shader->modelMaterial[3] * boost;
				mat2[0] = tess.shader->modelMaterial2[0];
				mat2[1] = tess.shader->modelMaterial2[1];
				mat2[2] = tess.shader->modelMaterial2[2];
				mat2[3] = tess.shader->modelMaterial2[3] * boost;
				GLSL_SetUniformVec4( sp, UNIFORM_MODELMATERIAL, mat );
				GLSL_SetUniformVec4( sp, UNIFORM_MODELMATERIAL2, mat2 );
			}
		}


		{
			vec4_t baseColor;
			vec4_t vertColor;

			ComputeShaderColors(pStage, baseColor, vertColor, pStage->stateBits);

			if ( r_emissiveGlow && r_emissiveGlow->integer > 0
				&& pStage->isEmissiveGlow > 0
				&& !backEnd.projection2D && !backEnd.depthFill )
			{
				const float rMul = ( pStage->isEmissiveGlow >= 2 ) ? 1.50f : 1.26f;
				const float gMul = ( pStage->isEmissiveGlow >= 2 ) ? 1.42f : 1.21f;
				const float bMul = ( pStage->isEmissiveGlow >= 2 ) ? 1.30f : 1.13f;
				baseColor[0] *= rMul; baseColor[1] *= gMul; baseColor[2] *= bMul;
				vertColor[0] *= rMul; vertColor[1] *= gMul; vertColor[2] *= bMul;
			}

			GLSL_SetUniformVec4(sp, UNIFORM_BASECOLOR, baseColor);
			GLSL_SetUniformVec4(sp, UNIFORM_VERTCOLOR, vertColor);
		}

		if (pStage->rgbGen == CGEN_LIGHTING_DIFFUSE)
		{
			vec4_t vec;

			VectorScale(backEnd.currentEntity->ambientLight, 1.0f / 255.0f, vec);
			GLSL_SetUniformVec3(sp, UNIFORM_AMBIENTLIGHT, vec);

			VectorScale(backEnd.currentEntity->directedLight, 1.0f / 255.0f, vec);
			GLSL_SetUniformVec3(sp, UNIFORM_DIRECTEDLIGHT, vec);
			
			VectorCopy(backEnd.currentEntity->lightDir, vec);
			vec[3] = 0.0f;
			GLSL_SetUniformVec4(sp, UNIFORM_LIGHTORIGIN, vec);
			GLSL_SetUniformVec3(sp, UNIFORM_MODELLIGHTDIR, backEnd.currentEntity->modelLightDir);

			GLSL_SetUniformFloat(sp, UNIFORM_LIGHTRADIUS, 0.0f);
		}

		if (pStage->alphaGen == AGEN_PORTAL)
		{
			GLSL_SetUniformFloat(sp, UNIFORM_PORTALRANGE, tess.shader->portalRange);
		}

		GLSL_SetUniformInt(sp, UNIFORM_COLORGEN, pStage->rgbGen);
		GLSL_SetUniformInt(sp, UNIFORM_ALPHAGEN, pStage->alphaGen);

		if ( input->fogNum )
		{
			vec4_t fogColorMask;

			ComputeFogColorMask(pStage, fogColorMask);

			GLSL_SetUniformVec4(sp, UNIFORM_FOGCOLORMASK, fogColorMask);
		}

		if (r_lightmap->integer)
		{
			vec4_t st[2];
			VectorSet4(st[0], 1.0f, 0.0f, 0.0f, 0.0f);
			VectorSet4(st[1], 0.0f, 1.0f, 0.0f, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX0, st[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX1, st[1]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX2, st[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX3, st[1]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX4, st[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX5, st[1]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX6, st[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX7, st[1]);

			GLSL_SetUniformInt(sp, UNIFORM_TCGEN0, TCGEN_LIGHTMAP);
		}
		else if (sp->uniforms[UNIFORM_DIFFUSETEXMATRIX0] >= 0)
		{
			ComputeTexMods(pStage, TB_DIFFUSEMAP, texMatrix);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX0, texMatrix[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX1, texMatrix[1]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX2, texMatrix[2]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX3, texMatrix[3]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX4, texMatrix[4]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX5, texMatrix[5]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX6, texMatrix[6]);
			GLSL_SetUniformVec4(sp, UNIFORM_DIFFUSETEXMATRIX7, texMatrix[7]);

			GLSL_SetUniformInt(sp, UNIFORM_TCGEN0, pStage->bundle[0].tcGen);
			if (pStage->bundle[0].tcGen == TCGEN_VECTOR)
			{
				vec3_t vec;

				VectorCopy(pStage->bundle[0].tcGenVectors[0], vec);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN0VECTOR0, vec);
				VectorCopy(pStage->bundle[0].tcGenVectors[1], vec);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN0VECTOR1, vec);
			}
			else if (pStage->bundle[0].tcGen == TCGEN_SUN_REFLECTION)
			{
				vec3_t sunAxis0, sunAxis1;
				const float *sunDir = s_sun.exists ? s_sun.direction : tr.sunDirection;

				VectorCopy(sunDir, sunAxis0);
				VectorSet(sunAxis1, sunDir[1], -sunDir[0], 0.0f);
				if (VectorNormalize(sunAxis1) < 1e-6f)
				{
					VectorSet(sunAxis1, 1.0f, 0.0f, 0.0f);
				}
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN0VECTOR0, sunAxis0);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN0VECTOR1, sunAxis1);
			}
		}

		if (sp->uniforms[UNIFORM_TCGEN1] >= 0)
		{
			vec4_t texMatrix1[8];
			ComputeTexMods(pStage, 1, texMatrix1);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX0, texMatrix1[0]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX1, texMatrix1[1]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX2, texMatrix1[2]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX3, texMatrix1[3]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX4, texMatrix1[4]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX5, texMatrix1[5]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX6, texMatrix1[6]);
			GLSL_SetUniformVec4(sp, UNIFORM_BUNDLE1TEXMATRIX7, texMatrix1[7]);

			GLSL_SetUniformInt(sp, UNIFORM_TCGEN1, pStage->bundle[1].tcGen);
			if (pStage->bundle[1].tcGen == TCGEN_VECTOR)
			{
				vec3_t vec;

				VectorCopy(pStage->bundle[1].tcGenVectors[0], vec);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN1VECTOR0, vec);
				VectorCopy(pStage->bundle[1].tcGenVectors[1], vec);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN1VECTOR1, vec);
			}
			else if (pStage->bundle[1].tcGen == TCGEN_SUN_REFLECTION)
			{
				vec3_t sunAxis0, sunAxis1;
				const float *sunDir = s_sun.exists ? s_sun.direction : tr.sunDirection;

				VectorCopy(sunDir, sunAxis0);
				VectorSet(sunAxis1, sunDir[1], -sunDir[0], 0.0f);
				if (VectorNormalize(sunAxis1) < 1e-6f)
				{
					VectorSet(sunAxis1, 1.0f, 0.0f, 0.0f);
				}
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN1VECTOR0, sunAxis0);
				GLSL_SetUniformVec3(sp, UNIFORM_TCGEN1VECTOR1, sunAxis1);
			}

			GLSL_SetUniformInt(sp, UNIFORM_MULTITEXTUREENV, (pStage->multitextureEnv == GL_ADD) ? 1 : 0);
		}

		GLSL_SetUniformMat4(sp, UNIFORM_MODELMATRIX, backEnd.ori.transformMatrix);

		GLSL_SetUniformVec4(sp, UNIFORM_NORMALSCALE, pStage->normalScale);

		{
			vec4_t specularScale;
			Vector4Copy(pStage->specularScale, specularScale);

			if (renderToCubemap)
			{
				// force specular to nonmetal if rendering cubemaps
				if (r_pbr->integer)
					specularScale[1] = 0.0f;
			}

			GLSL_SetUniformVec4(sp, UNIFORM_SPECULARSCALE, specularScale);
		}

		//GLSL_SetUniformFloat(sp, UNIFORM_MAPLIGHTSCALE, backEnd.refdef.mapLightScale);

		//
		// do multitexture
		//
		if ( backEnd.depthFill )
		{
			if (!(pStage->stateBits & GLS_ATEST_BITS))
				GL_BindToTMU( tr.whiteImage, TB_COLORMAP );
			else if ( pStage->bundle[TB_COLORMAP].image[0] != 0 )
				R_BindAnimatedImageToTMU( &pStage->bundle[TB_COLORMAP], TB_COLORMAP );
		}
		else if ( useModelPerPixel )
		{
			if ( pStage->bundle[TB_DIFFUSEMAP].image[0] )
				R_BindAnimatedImageToTMU( &pStage->bundle[TB_DIFFUSEMAP], TB_DIFFUSEMAP );
			else
				GL_BindToTMU( tr.whiteImage, TB_DIFFUSEMAP );

			RB_SetupSunShadowUniforms( sp, pStage, qtrue );
		}
		else if ( useModernWater )
		{
			if ( pStage->bundle[0].image[0] )
				R_BindAnimatedImageToTMU( &pStage->bundle[0], TB_DIFFUSEMAP );
			else
				GL_BindToTMU( tr.whiteImage, TB_DIFFUSEMAP );

			if ( pStage->bundle[1].image[0] )
				R_BindAnimatedImageToTMU( &pStage->bundle[1], TB_LIGHTMAP );
		}
		else if ( pStage->glslShaderGroup == tr.lightallShader )
		{
			int i;
			vec4_t enableTextures;

			if (r_sunlightMode->integer && (backEnd.viewParms.flags & VPF_USESUNLIGHT) && (pStage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK))
			{
				// FIXME: screenShadowImage is NULL if no framebuffers
				if (tr.screenShadowImage)
					GL_BindToTMU(tr.screenShadowImage, TB_SHADOWMAP);
				GLSL_SetUniformVec3(sp, UNIFORM_PRIMARYLIGHTAMBIENT, backEnd.refdef.sunAmbCol);
				if (r_pbr->integer)
				{
					vec3_t color;

					color[0] = backEnd.refdef.sunCol[0] * backEnd.refdef.sunCol[0];
					color[1] = backEnd.refdef.sunCol[1] * backEnd.refdef.sunCol[1];
					color[2] = backEnd.refdef.sunCol[2] * backEnd.refdef.sunCol[2];
					GLSL_SetUniformVec3(sp, UNIFORM_PRIMARYLIGHTCOLOR, color);
				}
				else
				{
					GLSL_SetUniformVec3(sp, UNIFORM_PRIMARYLIGHTCOLOR, backEnd.refdef.sunCol);
				}
				GLSL_SetUniformVec4(sp, UNIFORM_PRIMARYLIGHTORIGIN,  backEnd.refdef.sunDir);
			}

			VectorSet4(enableTextures, 0, 0, 0, 0);
			if ((r_lightmap->integer == 1 || r_lightmap->integer == 2) && pStage->bundle[TB_LIGHTMAP].image[0])
			{
				for (i = 0; i < NUM_TEXTURE_BUNDLES; i++)
				{
					if (i == TB_COLORMAP)
						R_BindAnimatedImageToTMU( &pStage->bundle[TB_LIGHTMAP], i);
					else
						GL_BindToTMU( tr.whiteImage, i );
				}
			}
			else if (r_lightmap->integer == 3 && pStage->bundle[TB_DELUXEMAP].image[0])
			{
				for (i = 0; i < NUM_TEXTURE_BUNDLES; i++)
				{
					if (i == TB_COLORMAP)
						R_BindAnimatedImageToTMU( &pStage->bundle[TB_DELUXEMAP], i);
					else
						GL_BindToTMU( tr.whiteImage, i );
				}
			}
			else
			{
				qboolean light = (pStage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) != 0;
				qboolean fastLight = !(r_normalMapping->integer || r_specularMapping->integer);

				if (pStage->bundle[TB_DIFFUSEMAP].image[0])
					R_BindAnimatedImageToTMU( &pStage->bundle[TB_DIFFUSEMAP], TB_DIFFUSEMAP);

				if (pStage->bundle[TB_LIGHTMAP].image[0])
					R_BindAnimatedImageToTMU( &pStage->bundle[TB_LIGHTMAP], TB_LIGHTMAP);

				// bind textures that are sampled and used in the glsl shader, and
				// bind whiteImage to textures that are sampled but zeroed in the glsl shader
				//
				// alternatives:
				//  - use the last bound texture
				//     -> costs more to sample a higher res texture then throw out the result
				//  - disable texture sampling in glsl shader with #ifdefs, as before
				//     -> increases the number of shaders that must be compiled
				//
				if (light && !fastLight)
				{
					if (pStage->bundle[TB_NORMALMAP].image[0])
					{
						R_BindAnimatedImageToTMU( &pStage->bundle[TB_NORMALMAP], TB_NORMALMAP);
						enableTextures[0] = 1.0f;
					}
					else if (r_normalMapping->integer)
						GL_BindToTMU( tr.whiteImage, TB_NORMALMAP );

					if (pStage->bundle[TB_DELUXEMAP].image[0])
					{
						R_BindAnimatedImageToTMU( &pStage->bundle[TB_DELUXEMAP], TB_DELUXEMAP);
						enableTextures[1] = 1.0f;
					}
					else if (r_deluxeMapping->integer)
						GL_BindToTMU( tr.whiteImage, TB_DELUXEMAP );

					if (pStage->bundle[TB_SPECULARMAP].image[0])
					{
						R_BindAnimatedImageToTMU( &pStage->bundle[TB_SPECULARMAP], TB_SPECULARMAP);
						enableTextures[2] = 1.0f;
					}
					else if (r_specularMapping->integer)
						GL_BindToTMU( tr.whiteImage, TB_SPECULARMAP );
				}

				enableTextures[3] = (r_cubeMapping->integer && !(tr.viewParms.flags & VPF_NOCUBEMAPS) && input->cubemapIndex) ? 1.0f : 0.0f;
			}

			GLSL_SetUniformVec4(sp, UNIFORM_ENABLETEXTURES, enableTextures);
			RB_SetupWorldReliefUniforms(sp, pStage);
			RB_SetupSunShadowUniforms(sp, pStage, qfalse);
		}
		else if ( pStage->bundle[1].image[0] != 0 )
		{
			R_BindAnimatedImageToTMU( &pStage->bundle[0], 0 );
			R_BindAnimatedImageToTMU( &pStage->bundle[1], 1 );
		}
		else 
		{
			//
			// set state
			//
			R_BindAnimatedImageToTMU( &pStage->bundle[0], 0 );
		}

		//
		// testing cube map
		//
		if (!(tr.viewParms.flags & VPF_NOCUBEMAPS) && input->cubemapIndex && r_cubeMapping->integer)
		{
			vec4_t vec;
			cubemap_t *cubemap = &tr.cubemaps[input->cubemapIndex - 1];

			// FIXME: cubemap image could be NULL if cubemap isn't renderer or loaded
			if (cubemap->image)
				GL_BindToTMU( cubemap->image, TB_CUBEMAP);

			VectorSubtract(cubemap->origin, backEnd.viewParms.ori.origin, vec);
			vec[3] = 1.0f;

			VectorScale4(vec, 1.0f / cubemap->parallaxRadius, vec);

			GLSL_SetUniformVec4(sp, UNIFORM_CUBEMAPINFO, vec);
		}

		//
		// draw
		//
		R_DrawElements(input->numIndexes, input->firstIndex);

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap ) )
		{
			break;
		}

		if (backEnd.depthFill)
			break;
	}
}


static void RB_RenderShadowmap( shaderCommands_t *input )
{
	int deformGen;
	vec5_t deformParams;

	ComputeDeformValues(&deformGen, deformParams);

	{
		shaderProgram_t *sp = &tr.shadowmapShader[0];

		if (glState.vertexAnimation)
		{
			sp = &tr.shadowmapShader[SHADOWMAPDEF_USE_VERTEX_ANIMATION];
		}
		else if (glState.boneAnimation)
		{
			sp = &tr.shadowmapShader[SHADOWMAPDEF_USE_BONE_ANIMATION];
		}

		vec4_t vector;

		GLSL_BindProgram(sp);
		RB_SetWindUniform(sp);

		GLSL_SetUniformMat4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);

		GLSL_SetUniformMat4(sp, UNIFORM_MODELMATRIX, backEnd.ori.transformMatrix);

		GLSL_SetUniformFloat(sp, UNIFORM_VERTEXLERP, glState.vertexAttribsInterpolation);

		if (glState.boneAnimation)
		{
			GLSL_SetUniformMat4BoneMatrix(sp, UNIFORM_BONEMATRIX, glState.boneMatrix, glState.boneAnimation);
		}

		GLSL_SetUniformInt(sp, UNIFORM_DEFORMGEN, deformGen);
		if (deformGen != DGEN_NONE)
		{
			GLSL_SetUniformFloat5(sp, UNIFORM_DEFORMPARAMS, deformParams);
			GLSL_SetUniformFloat(sp, UNIFORM_TIME, tess.shaderTime);
		}

		VectorCopy(backEnd.viewParms.ori.origin, vector);
		vector[3] = 1.0f;
		GLSL_SetUniformVec4(sp, UNIFORM_LIGHTORIGIN, vector);
		GLSL_SetUniformFloat(sp, UNIFORM_LIGHTRADIUS, backEnd.viewParms.zFar);

		GL_State( 0 );
		GLSL_SetUniformInt(sp, UNIFORM_ALPHATEST, 0);

		//
		// do multitexture
		//
		//if ( pStage->glslShaderGroup )
		{
			//
			// draw
			//

			R_DrawElements(input->numIndexes, input->firstIndex);
		}
	}
}



/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	shaderCommands_t *input;
	unsigned int vertexAttribs = 0;

	input = &tess;
	
	if (!input->numVertexes || !input->numIndexes)
	{
		return;
	}

	if (tess.useInternalVao)
	{
		RB_DeformTessGeometry();
	}

	vertexAttribs = RB_CalcShaderVertexAttribs( input );

	if (tess.useInternalVao)
	{
		int64_t tVbo = ( r_speeds->integer == 8 ) ? R_ShadeNowUsec() : 0;
		if ( backEnd.depthFill && ( backEnd.viewParms.flags & VPF_PISUNSHADOW ) )
		{
			unsigned int shadowAttribs = ATTR_POSITION;
			shaderStage_t *st0 = tess.xstages[0];

			if ( glState.vertexAnimation ) {
				shadowAttribs |= ATTR_POSITION2;
			} else if ( glState.boneAnimation ) {
				shadowAttribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
			}
			if ( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) ) {
				shadowAttribs |= ATTR_NORMAL;
			}
			if ( st0 && ( st0->stateBits & GLS_ATEST_BITS ) ) {
				shadowAttribs |= ATTR_TEXCOORD | ATTR_COLOR;
				if ( st0->bundle[0].tcGen != TCGEN_TEXTURE ) {
					shadowAttribs |= ATTR_NORMAL;
				}
				if ( st0->alphaGen != AGEN_IDENTITY ) {
					RB_ComputeStageCpuColors( st0 );
				}
			}
			RB_UpdateTessVao( vertexAttribs & shadowAttribs );
		}
		else
		{
			if ( !backEnd.depthFill
				|| ( tess.xstages[0] && ( tess.xstages[0]->stateBits & GLS_ATEST_BITS ) && tess.xstages[0]->alphaGen != AGEN_IDENTITY ) )
			{
				RB_ComputeStageCpuColors(tess.xstages[0]);
			}
			RB_UpdateTessVao(vertexAttribs);
		}
		if ( tVbo ) {
			backEnd.pc.usec_vbo += (int)( R_ShadeNowUsec() - tVbo );
		}
	}
	else
	{
		backEnd.pc.c_staticVaoDraws++;
	}

	//
	// log this call
	//
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	if (input->shader->cullType == CT_TWO_SIDED)
	{
		GL_Cull( CT_TWO_SIDED );
	}
	else
	{
		qboolean cullFront = (input->shader->cullType == CT_FRONT_SIDED);

		if ( ( backEnd.viewParms.flags & VPF_DEPTHSHADOW ) && !( backEnd.viewParms.flags & VPF_PISUNSHADOW ) )
			cullFront = !cullFront;

		if ( backEnd.viewParms.isMirror )
			cullFront = !cullFront;

		if ( backEnd.currentEntity && backEnd.currentEntity->mirrored )
			cullFront = !cullFront;

		if (cullFront)
			GL_Cull( CT_FRONT_SIDED );
		else
			GL_Cull( CT_BACK_SIDED );
	}

	// set polygon offset if necessary
	if ( input->shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
	}

	//
	// render depth if in depthfill mode
	//
	if (backEnd.depthFill)
	{
		RB_IterateStagesGeneric( input );

		//
		// reset polygon offset
		//
		if ( input->shader->polygonOffset )
		{
			qglDisable( GL_POLYGON_OFFSET_FILL );
		}

		return;
	}

	//
	// render shadowmap if in shadowmap mode
	//
	if (backEnd.viewParms.flags & VPF_SHADOWMAP)
	{
		if ( input->shader->sort == SS_OPAQUE )
		{
			RB_RenderShadowmap( input );
		}
		//
		// reset polygon offset
		//
		if ( input->shader->polygonOffset )
		{
			qglDisable( GL_POLYGON_OFFSET_FILL );
		}

		return;
	}

	//
	//
	// call shader function
	//
	{
		int64_t tDraw = ( r_speeds->integer == 8 ) ? R_ShadeNowUsec() : 0;
		RB_IterateStagesGeneric( input );
		if ( tDraw ) {
			backEnd.pc.usec_draw += (int)( R_ShadeNowUsec() - tDraw );
		}
	}

	//
	// pshadows!
	//
	if (glRefConfig.framebufferObject && r_shadows->integer == 4 && tess.pshadowBits
		&& tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		ProjectPshadowVBOGLSL();
	}


	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && r_lightmap->integer == 0
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		if (tess.shader->numUnfoggedPasses == 1 && tess.xstages[0]->glslShaderGroup == tr.lightallShader
			&& (tess.xstages[0]->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) && r_dlightMode->integer)
		{
			ForwardDlight();
		}
		else
		{
			ProjectDlightTexture();
		}
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	//
	// reset polygon offset
	//
	if ( input->shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	if (input->numIndexes == 0 || input->numVertexes == 0) {
		return;
	}

	if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit");
	}	
	if (input->xyz[SHADER_MAX_VERTEXES-1][0] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit");
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	if (tess.useCacheVao)
	{
		// upload indexes now
		VaoCache_Commit();
	}

	//
	// update performance counters
	//
	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	tess.currentStageIteratorFunc();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) {
		DrawTris (input);
	}
	if ( r_shownormals->integer ) {
		DrawNormals (input);
	}
	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.firstIndex = 0;
	tess.useCacheVao = qfalse;
	tess.useInternalVao = qfalse;

	GLimp_LogComment( "----------\n" );
}
