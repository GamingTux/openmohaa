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
// tr_marks.c -- polygon projection on the world polygons

#include "tr_local.h"
//#include "assert.h"

#define MAX_VERTS_ON_POLY		64

#define MARKER_OFFSET			0	// 1

/*
=============
R_ChopPolyBehindPlane

Out must have space for two more vertexes than in
=============
*/
#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
static void R_ChopPolyBehindPlane( int numInPoints, vec3_t inPoints[MAX_VERTS_ON_POLY],
								   int *numOutPoints, vec3_t outPoints[MAX_VERTS_ON_POLY], 
							vec3_t normal, vec_t dist, vec_t epsilon) {
	float		dists[MAX_VERTS_ON_POLY+4] = { 0 };
	int			sides[MAX_VERTS_ON_POLY+4] = { 0 };
	int			counts[3];
	float		dot;
	int			i, j;
	float		*p1, *p2, *clip;
	float		d;

	// don't clip if it might overflow
	if ( numInPoints >= MAX_VERTS_ON_POLY - 2 ) {
		*numOutPoints = 0;
		return;
	}

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	for ( i = 0 ; i < numInPoints ; i++ ) {
		dot = DotProduct( inPoints[i], normal );
		dot -= dist;
		dists[i] = dot;
		if ( dot > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( dot < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	*numOutPoints = 0;

	if ( !counts[0] ) {
		return;
	}
	if ( !counts[1] ) {
		*numOutPoints = numInPoints;
		Com_Memcpy( outPoints, inPoints, numInPoints * sizeof(vec3_t) );
		return;
	}

	for ( i = 0 ; i < numInPoints ; i++ ) {
		p1 = inPoints[i];
		clip = outPoints[ *numOutPoints ];
		
		if ( sides[i] == SIDE_ON ) {
			VectorCopy( p1, clip );
			(*numOutPoints)++;
			continue;
		}
	
		if ( sides[i] == SIDE_FRONT ) {
			VectorCopy( p1, clip );
			(*numOutPoints)++;
			clip = outPoints[ *numOutPoints ];
		}

		if ( sides[i+1] == SIDE_ON || sides[i+1] == sides[i] ) {
			continue;
		}
			
		// generate a split point
		p2 = inPoints[ (i+1) % numInPoints ];

		d = dists[i] - dists[i+1];
		if ( d == 0 ) {
			dot = 0;
		} else {
			dot = dists[i] / d;
		}

		// clip xyz

		for (j=0 ; j<3 ; j++) {
			clip[j] = p1[j] + dot * ( p2[j] - p1[j] );
		}

		(*numOutPoints)++;
	}
}

/*
=================
R_BoxSurfaces_r

=================
*/
void R_BoxSurfaces_r(mnode_t *node, vec3_t mins, vec3_t maxs, surfaceType_t **list, int listsize, int *listlength, vec3_t dir) {

	int			s, c;
	msurface_t	*surf;
	int *mark;

	// do the tail recursion in a loop
	while ( node->contents == -1 ) {
		s = BoxOnPlaneSide( mins, maxs, node->plane );
		if (s == 1) {
			node = node->children[0];
		} else if (s == 2) {
			node = node->children[1];
		} else {
			R_BoxSurfaces_r(node->children[0], mins, maxs, list, listsize, listlength, dir);
			node = node->children[1];
		}
	}

	// add the individual surfaces
	mark = tr.world->marksurfaces + node->firstmarksurface;
	c = node->nummarksurfaces;
	while (c--) {
		int *surfViewCount;
		//
		if (*listlength >= listsize) break;
		//
		surfViewCount = &tr.world->surfacesViewCount[*mark];
		surf = tr.world->surfaces + *mark;
		// check if the surface has NOIMPACT or NOMARKS set
		if ( ( surf->shader->surfaceFlags & ( SURF_NOIMPACT | SURF_NOMARKS ) )
			|| ( surf->shader->contentFlags & CONTENTS_FOG ) ) {
			*surfViewCount = tr.viewCount;
		}
		// extra check for surfaces to avoid list overflows
		else if (*(surf->data) == SF_FACE) {
			// the face plane should go through the box
			s = BoxOnPlaneSide( mins, maxs, &surf->cullinfo.plane );
			if (s == 1 || s == 2) {
				*surfViewCount = tr.viewCount;
			} else if (DotProduct(surf->cullinfo.plane.normal, dir) > -0.5) {
			// don't add faces that make sharp angles with the projection direction
				*surfViewCount = tr.viewCount;
			}
		}
		else if (*(surf->data) != SF_GRID &&
			 *(surf->data) != SF_TRIANGLES)
			*surfViewCount = tr.viewCount;
		// check the viewCount because the surface may have
		// already been added if it spans multiple leafs
		if (*surfViewCount != tr.viewCount) {
			*surfViewCount = tr.viewCount;
			list[*listlength] = surf->data;
			(*listlength)++;
		}
		mark++;
	}
}

/*
=================
R_AddMarkFragments

=================
*/
void R_AddMarkFragments(int numClipPoints, vec3_t clipPoints[2][MAX_VERTS_ON_POLY],
				   int numPlanes, vec3_t *normals, float *dists,
				   int maxPoints, vec3_t pointBuffer,
				   int maxFragments, markFragment_t *fragmentBuffer,
				   int *returnedPoints, int *returnedFragments,
				   vec3_t mins, vec3_t maxs) {
	int pingPong, i;
	markFragment_t	*mf;

	// chop the surface by all the bounding planes of the to be projected polygon
	pingPong = 0;

	for ( i = 0 ; i < numPlanes ; i++ ) {

		R_ChopPolyBehindPlane( numClipPoints, clipPoints[pingPong],
						   &numClipPoints, clipPoints[!pingPong],
							normals[i], dists[i], 0.5 );
		pingPong ^= 1;
		if ( numClipPoints == 0 ) {
			break;
		}
	}
	// completely clipped away?
	if ( numClipPoints == 0 ) {
		return;
	}

	// add this fragment to the returned list
	if ( numClipPoints + (*returnedPoints) > maxPoints ) {
		return;	// not enough space for this polygon
	}
	/*
	// all the clip points should be within the bounding box
	for ( i = 0 ; i < numClipPoints ; i++ ) {
		int j;
		for ( j = 0 ; j < 3 ; j++ ) {
			if (clipPoints[pingPong][i][j] < mins[j] - 0.5) break;
			if (clipPoints[pingPong][i][j] > maxs[j] + 0.5) break;
		}
		if (j < 3) break;
	}
	if (i < numClipPoints) return;
	*/

	mf = fragmentBuffer + (*returnedFragments);
	mf->firstPoint = (*returnedPoints);
	mf->numPoints = numClipPoints;
	mf->iIndex = 0; // OPENMOHAA-specific stuff
	Com_Memcpy( pointBuffer + (*returnedPoints) * 3, clipPoints[pingPong], numClipPoints * sizeof(vec3_t) );

	(*returnedPoints) += numClipPoints;
	(*returnedFragments)++;
}

/*
=================
R_AddMarkFragmentsToTerrain
Projects decals/blood pools/craters onto OpenMoHAA 8x8 heightmap terrain patches
=================
*/
static void R_AddMarkFragmentsToTerrain(
	cTerraPatchUnpacked_t *pTerPatch,
	int numPlanes,
	vec3_t *normals,
	float *dists,
	int maxPoints,
	vec3_t pointBuffer,
	int maxFragments,
	markFragment_t *fragmentBuffer,
	int *returnedPoints,
	int *returnedFragments,
	vec3_t mins,
	vec3_t maxs
) {
	int i, j, iType, iFirstFragment, patchIndex;
	int iMin, iMax, jMin, jMax;
	vec3_t v[4];
	vec3_t clipPoints[2][MAX_VERTS_ON_POLY];

	if (pTerPatch->shader
		&& (pTerPatch->shader->surfaceFlags & (SURF_NOIMPACT | SURF_NOMARKS))) {
		return;
	}

	// Fast AABB overlap check against 512x512 terrain patch
	if (maxs[0] < pTerPatch->x0 || mins[0] > pTerPatch->x0 + 512.0f
		|| maxs[1] < pTerPatch->y0 || mins[1] > pTerPatch->y0 + 512.0f
		|| maxs[2] < pTerPatch->z0 - 32.0f || mins[2] > pTerPatch->zmax + 32.0f) {
		return;
	}

	// Clamp to only the 64x64 terrain cells overlapping the mark bounds
	iMin = (int)floorf((mins[0] - pTerPatch->x0) * (1.0f / 64.0f));
	iMax = (int)floorf((maxs[0] - pTerPatch->x0) * (1.0f / 64.0f));
	jMin = (int)floorf((mins[1] - pTerPatch->y0) * (1.0f / 64.0f));
	jMax = (int)floorf((maxs[1] - pTerPatch->y0) * (1.0f / 64.0f));
	if (iMin < 0) iMin = 0;
	if (iMax > 7) iMax = 7;
	if (jMin < 0) jMin = 0;
	if (jMax > 7) jMax = 7;
	if (iMin > iMax || jMin > jMax) {
		return;
	}

	patchIndex     = (int)(pTerPatch - tr.world->terraPatches);
	iFirstFragment = *returnedFragments;

	// Widen projection depth planes slightly for sloping terrain cells (matches GL1)
	dists[numPlanes - 2] -= 96.0f;
	dists[numPlanes - 1] -= 108.0f;

	for (j = jMin; j <= jMax && *returnedFragments < maxFragments; j++) {
		for (i = iMin; i <= iMax && *returnedFragments < maxFragments; i++) {
			const unsigned char *pubHeight = pTerPatch->heightmap + j * 9 + i;
			iType = ri.CM_TerrainSquareType(patchIndex, i, j);
			if (!iType) {
				continue;
			}

			v[0][0] = (float)(i << 6) + pTerPatch->x0;
			v[0][1] = (float)(j << 6) + pTerPatch->y0;
			v[0][2] = (float)(pubHeight[0] << 1) + pTerPatch->z0;

			v[1][0] = v[0][0] + 64.0f;
			v[1][1] = v[0][1];
			v[1][2] = (float)(pubHeight[1] << 1) + pTerPatch->z0;

			v[2][0] = v[0][0] + 64.0f;
			v[2][1] = v[0][1] + 64.0f;
			v[2][2] = (float)(pubHeight[10] << 1) + pTerPatch->z0;

			v[3][0] = v[0][0];
			v[3][1] = v[0][1] + 64.0f;
			v[3][2] = (float)(pubHeight[9] << 1) + pTerPatch->z0;

			if (((j & 0xFF) + (i & 0xFF)) & 1) {
				if (iType != 3) {
					VectorCopy(v[0], clipPoints[0][0]);
					VectorCopy(v[3], clipPoints[0][1]);
					VectorCopy(v[1], clipPoints[0][2]);
					R_AddMarkFragments(3, clipPoints, numPlanes, normals, dists,
						maxPoints, pointBuffer, maxFragments, fragmentBuffer,
						returnedPoints, returnedFragments, mins, maxs);
				}
				if (iType != 6 && *returnedFragments < maxFragments) {
					VectorCopy(v[2], clipPoints[0][0]);
					VectorCopy(v[1], clipPoints[0][1]);
					VectorCopy(v[3], clipPoints[0][2]);
					R_AddMarkFragments(3, clipPoints, numPlanes, normals, dists,
						maxPoints, pointBuffer, maxFragments, fragmentBuffer,
						returnedPoints, returnedFragments, mins, maxs);
				}
			} else {
				if (iType != 5) {
					VectorCopy(v[3], clipPoints[0][0]);
					VectorCopy(v[2], clipPoints[0][1]);
					VectorCopy(v[0], clipPoints[0][2]);
					R_AddMarkFragments(3, clipPoints, numPlanes, normals, dists,
						maxPoints, pointBuffer, maxFragments, fragmentBuffer,
						returnedPoints, returnedFragments, mins, maxs);
				}
				if (iType != 4 && *returnedFragments < maxFragments) {
					VectorCopy(v[1], clipPoints[0][0]);
					VectorCopy(v[0], clipPoints[0][1]);
					VectorCopy(v[2], clipPoints[0][2]);
					R_AddMarkFragments(3, clipPoints, numPlanes, normals, dists,
						maxPoints, pointBuffer, maxFragments, fragmentBuffer,
						returnedPoints, returnedFragments, mins, maxs);
				}
			}
		}
	}

	for (i = iFirstFragment; i < *returnedFragments; i++) {
		fragmentBuffer[i].iIndex = patchIndex + 1;
	}

	dists[numPlanes - 2] += 96.0f;
	dists[numPlanes - 1] += 108.0f;
}

/*
=================
R_MarkFragments

=================
*/
int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer, float fRadiusSquared ) {
	int				numsurfaces, numPlanes;
	int				i, j, k, m, n;
	surfaceType_t	*surfaces[64];
	vec3_t			mins, maxs;
	int				returnedFragments;
	int				returnedPoints;
	vec3_t			normals[MAX_VERTS_ON_POLY+2];
	float			dists[MAX_VERTS_ON_POLY+2];
	vec3_t			clipPoints[2][MAX_VERTS_ON_POLY];
	int				numClipPoints;
	float			*v;
	srfBspSurface_t	*cv;
	glIndex_t		*tri;
	srfVert_t		*dv;
	vec3_t			normal;
	vec3_t			projectionDir;
	vec3_t			v1, v2;

	if (numPoints <= 0) {
		return 0;
	}

	//increment view count for double check prevention
	tr.viewCount++;

	//
	VectorNormalize2( projection, projectionDir );
	// find all the brushes that are to be considered
	ClearBounds( mins, maxs );
	for ( i = 0 ; i < numPoints ; i++ ) {
		vec3_t	temp;

		AddPointToBounds( points[i], mins, maxs );
		VectorAdd( points[i], projection, temp );
		AddPointToBounds( temp, mins, maxs );
		// make sure we get all the leafs (also the one(s) in front of the hit surface)
		VectorMA( points[i], -20, projectionDir, temp );
		AddPointToBounds( temp, mins, maxs );
	}

	if (numPoints > MAX_VERTS_ON_POLY) numPoints = MAX_VERTS_ON_POLY;
	// create the bounding planes for the to be projected polygon
	for ( i = 0 ; i < numPoints ; i++ ) {
		VectorSubtract(points[(i+1)%numPoints], points[i], v1);
		VectorAdd(points[i], projection, v2);
		VectorSubtract(points[i], v2, v2);
		CrossProduct(v1, v2, normals[i]);
		VectorNormalizeFast(normals[i]);
		dists[i] = DotProduct(normals[i], points[i]);
	}
	// add near and far clipping planes for projection
	VectorCopy(projectionDir, normals[numPoints]);
	dists[numPoints] = DotProduct(normals[numPoints], points[0]) - 32;
	VectorCopy(projectionDir, normals[numPoints+1]);
	VectorInverse(normals[numPoints+1]);
	dists[numPoints+1] = DotProduct(normals[numPoints+1], points[0]) - 20;
	numPlanes = numPoints + 2;

	numsurfaces = 0;
	R_BoxSurfaces_r(tr.world->nodes, mins, maxs, surfaces, 64, &numsurfaces, projectionDir);
	//assert(numsurfaces <= 64);
	//assert(numsurfaces != 64);

	returnedPoints = 0;
	returnedFragments = 0;

	for ( i = 0 ; i < numsurfaces ; i++ ) {

		if (*surfaces[i] == SF_GRID) {

			cv = (srfBspSurface_t *) surfaces[i];
			for ( m = 0 ; m < cv->height - 1 ; m++ ) {
				for ( n = 0 ; n < cv->width - 1 ; n++ ) {
					// We triangulate the grid and chop all triangles within
					// the bounding planes of the to be projected polygon.
					// LOD is not taken into account, not such a big deal though.
					//
					// It's probably much nicer to chop the grid itself and deal
					// with this grid as a normal SF_GRID surface so LOD will
					// be applied. However the LOD of that chopped grid must
					// be synced with the LOD of the original curve.
					// One way to do this; the chopped grid shares vertices with
					// the original curve. When LOD is applied to the original
					// curve the unused vertices are flagged. Now the chopped curve
					// should skip the flagged vertices. This still leaves the
					// problems with the vertices at the chopped grid edges.
					//
					// To avoid issues when LOD applied to "hollow curves" (like
					// the ones around many jump pads) we now just add a 2 unit
					// offset to the triangle vertices.
					// The offset is added in the vertex normal vector direction
					// so all triangles will still fit together.
					// The 2 unit offset should avoid pretty much all LOD problems.
					vec3_t fNormal;

					numClipPoints = 3;

					dv = cv->verts + m * cv->width + n;

					VectorCopy(dv[0].xyz, clipPoints[0][0]);
					R_VaoUnpackNormal(fNormal, dv[0].normal);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, fNormal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					R_VaoUnpackNormal(fNormal, dv[cv->width].normal);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, fNormal, clipPoints[0][1]);
					VectorCopy(dv[1].xyz, clipPoints[0][2]);
					R_VaoUnpackNormal(fNormal, dv[1].normal);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, fNormal, clipPoints[0][2]);
					// check the normal of this triangle
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.1) {
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
										   numPlanes, normals, dists,
										   maxPoints, pointBuffer,
										   maxFragments, fragmentBuffer,
										   &returnedPoints, &returnedFragments, mins, maxs);

						if ( returnedFragments == maxFragments ) {
							return returnedFragments;	// not enough space for more fragments
						}
					}

					VectorCopy(dv[1].xyz, clipPoints[0][0]);
					R_VaoUnpackNormal(fNormal, dv[1].normal);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, fNormal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					R_VaoUnpackNormal(fNormal, dv[cv->width].normal);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, fNormal, clipPoints[0][1]);
					VectorCopy(dv[cv->width+1].xyz, clipPoints[0][2]);
					R_VaoUnpackNormal(fNormal, dv[cv->width + 1].normal);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, fNormal, clipPoints[0][2]);
					// check the normal of this triangle
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.05) {
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
										   numPlanes, normals, dists,
										   maxPoints, pointBuffer,
										   maxFragments, fragmentBuffer,
										   &returnedPoints, &returnedFragments, mins, maxs);

						if ( returnedFragments == maxFragments ) {
							return returnedFragments;	// not enough space for more fragments
						}
					}
				}
			}
		}
		else if (*surfaces[i] == SF_FACE) {

			srfBspSurface_t *surf = ( srfBspSurface_t * ) surfaces[i];

			// check the normal of this face
			if (DotProduct(surf->cullPlane.normal, projectionDir) > -0.5) {
				continue;
			}

			for(k = 0, tri = surf->indexes; k < surf->numIndexes; k += 3, tri += 3)
			{
				for(j = 0; j < 3; j++)
				{
					v = surf->verts[tri[j]].xyz;
					VectorMA(v, MARKER_OFFSET, surf->cullPlane.normal, clipPoints[0][j]);
				}

				// add the fragments of this face
				R_AddMarkFragments( 3 , clipPoints,
								   numPlanes, normals, dists,
								   maxPoints, pointBuffer,
								   maxFragments, fragmentBuffer,
								   &returnedPoints, &returnedFragments, mins, maxs);
				if ( returnedFragments == maxFragments ) {
					return returnedFragments;	// not enough space for more fragments
				}
			}
		}
		else if(*surfaces[i] == SF_TRIANGLES && r_marksOnTriangleMeshes->integer) {

			srfBspSurface_t *surf = (srfBspSurface_t *) surfaces[i];

			for(k = 0, tri = surf->indexes; k < surf->numIndexes; k += 3, tri += 3)
			{
				for(j = 0; j < 3; j++)
				{
					vec3_t fNormal;
					v = surf->verts[tri[j]].xyz;
					R_VaoUnpackNormal(fNormal, surf->verts[tri[j]].normal);
					VectorMA(v, MARKER_OFFSET, fNormal, clipPoints[0][j]);
				}

				// add the fragments of this face
				R_AddMarkFragments(3, clipPoints,
								   numPlanes, normals, dists,
								   maxPoints, pointBuffer,
								   maxFragments, fragmentBuffer, &returnedPoints, &returnedFragments, mins, maxs);
				if(returnedFragments == maxFragments)
				{
					return returnedFragments;	// not enough space for more fragments
				}
			}
		}
	}

	// Project marks onto outdoor terrain patches (beach sand, grass, hills, snow)
	if (tr.world && tr.world->terraPatches && tr.world->numTerraPatches > 0
		&& returnedFragments < maxFragments
		&& fRadiusSquared >= 16.0f) {
		int p;
		for (p = 0; p < tr.world->numTerraPatches && returnedFragments < maxFragments; p++) {
			R_AddMarkFragmentsToTerrain(
				&tr.world->terraPatches[p],
				numPlanes, normals, dists,
				maxPoints, pointBuffer,
				maxFragments, fragmentBuffer,
				&returnedPoints, &returnedFragments,
				mins, maxs
			);
		}
	}

	return returnedFragments;
}

int R_MarkFragmentsForInlineModel(clipHandle_t bmodel, const vec3_t vAngles, const vec3_t vOrigin, int numPoints,
	const vec3_t* points, const vec3_t projection, int maxPoints, vec3_t pointBuffer,
	int maxFragments, markFragment_t* fragmentBuffer, float fRadiusSquared)
{
	// FIXME: unimplemented (GL2)
	return 0;
}

/*
=================
R_AddStaticCorpseBloodPools
Remaster: one-time per-map projection of wet blood pools beneath pre-placed static corpse props
(e.g. static/body_us*.tik, static/body_german*.tik on Omaha Beach & campaign maps).
Live-toggled by cg_gore / F8 with zero dynamic memory leaks.
=================
*/
#define MAX_STATIC_CORPSE_MARKS 1024
#define MAX_STATIC_CORPSE_VERTS 8192

static srfMarkFragment_t s_staticCorpseMarks[MAX_STATIC_CORPSE_MARKS];
static vec3_t            s_staticCorpseCenters[MAX_STATIC_CORPSE_MARKS];
static polyVert_t        s_staticCorpseVerts[MAX_STATIC_CORPSE_VERTS];
static int               s_numStaticCorpseMarks = 0;
static int               s_numStaticCorpseVerts = 0;
static const cStaticModelUnpacked_t *s_builtStaticModelsPtr = NULL;
static int               s_builtStaticModelsCount = -1;
static char              s_builtWorldName[MAX_QPATH];

static qboolean R_IsStaticCorpseModelName(const char *name)
{
	if (!name || !name[0]) {
		return qfalse;
	}
	if (Q_stristr(name, "body_us") || Q_stristr(name, "body_german")
		|| Q_stristr(name, "body_rus") || Q_stristr(name, "body_brit")
		|| Q_stristr(name, "body_italian") || Q_stristr(name, "body_allied")
		|| Q_stristr(name, "body_axis") || Q_stristr(name, "body_ranger")
		|| Q_stristr(name, "body_airborne") || Q_stristr(name, "dead_body")
		|| Q_stristr(name, "corpse")) {
		return qtrue;
	}
	return qfalse;
}

static void R_BuildStaticCorpseBloodPools(void)
{
	int i;
	const vec3_t upDir = { 0.0f, 0.0f, 1.0f };

	s_numStaticCorpseMarks   = 0;
	s_numStaticCorpseVerts   = 0;
	s_builtStaticModelsPtr   = tr.world->staticModels;
	s_builtStaticModelsCount = tr.world->numStaticModels;
	Q_strncpyz(s_builtWorldName, tr.world->baseName, sizeof(s_builtWorldName));

	if (!tr.world->staticModels || tr.world->numStaticModels <= 0 || !tr.bloodPoolShader) {
		return;
	}

	for (i = 0; i < tr.world->numStaticModels && s_numStaticCorpseMarks < MAX_STATIC_CORPSE_MARKS; i++) {
		const cStaticModelUnpacked_t *sm = &tr.world->staticModels[i];
		vec3_t         center, quad[4], projection, axU, axV, vLight, litPos, waterProbe;
		vec3_t         pointBuf[128];
		markFragment_t fragBuf[32];
		trace_t        trWater;
		float          ang, ca, sa, radius, inv2R;
		int            numFrags, f;
		byte           rgba[4];
		unsigned int   h;

		if (!R_IsStaticCorpseModelName(sm->model)) {
			continue;
		}

		// Compute world-space center beneath the corpse torso/bounds (handles offset model pivots)
		VectorCopy(sm->origin, center);
		if (sm->tiki && sm->tiki->a) {
			float  fScale = sm->tiki->load_scale * sm->scale;
			vec3_t localPt;

			localPt[0] = (sm->tiki->load_origin[0] + 0.5f * (sm->tiki->a->mins[0] + sm->tiki->a->maxs[0])) * fScale;
			localPt[1] = (sm->tiki->load_origin[1] + 0.5f * (sm->tiki->a->mins[1] + sm->tiki->a->maxs[1])) * fScale;
			localPt[2] = (sm->tiki->load_origin[2] + sm->tiki->a->mins[2] + 2.0f) * fScale;

			VectorMA(center, localPt[0], sm->axis[0], center);
			VectorMA(center, localPt[1], sm->axis[1], center);
			VectorMA(center, localPt[2], sm->axis[2], center);
		}

		// Skip corpses submerged in deep water (still allow shallow Omaha shoreline surf)
		VectorCopy(center, waterProbe);
		waterProbe[2] += 22.0f;
		ri.CM_BoxTrace(&trWater, waterProbe, waterProbe, vec3_origin, vec3_origin, 0, CONTENTS_WATER, qfalse);
		if (trWater.startsolid) {
			continue;
		}

		// Deterministic natural angle & radius per pre-placed corpse
		h      = (unsigned int)(i * 2654435761u) ^ (unsigned int)((int)center[0] * 374761393u);
		ang    = (float)(h & 0x3FFu) * (6.2831853f / 1024.0f);
		radius = 16.0f + (float)((h >> 10) & 0xFFu) * (7.5f / 255.0f);
		ca     = cosf(ang);
		sa     = sinf(ang);

		VectorSet(axU, ca * radius, sa * radius, 0.0f);
		VectorSet(axV, -sa * radius, ca * radius, 0.0f);

		// Clockwise quad from +Z so CrossProduct(edge, -projection) normals point INWARD (SIDE_FRONT)
		quad[0][0] = center[0] - axU[0] - axV[0];
		quad[0][1] = center[1] - axU[1] - axV[1];
		quad[0][2] = center[2] + 6.0f;

		quad[1][0] = center[0] - axU[0] + axV[0];
		quad[1][1] = center[1] - axU[1] + axV[1];
		quad[1][2] = center[2] + 6.0f;

		quad[2][0] = center[0] + axU[0] + axV[0];
		quad[2][1] = center[1] + axU[1] + axV[1];
		quad[2][2] = center[2] + 6.0f;

		quad[3][0] = center[0] + axU[0] - axV[0];
		quad[3][1] = center[1] + axU[1] - axV[1];
		quad[3][2] = center[2] + 6.0f;

		VectorSet(projection, 0.0f, 0.0f, -26.0f);

		numFrags = R_MarkFragments(
			4,
			(const vec3_t *)quad,
			projection,
			128,
			pointBuf[0],
			32,
			fragBuf,
			radius * radius
		);
		if (numFrags <= 0) {
			continue;
		}

		VectorCopy(center, litPos);
		litPos[2] += 4.0f;
		R_GetLightingForDecal(vLight, upDir, litPos);
		for (f = 0; f < 3; f++) {
			float c = vLight[f] * 0.90f;
			if (c < 76.0f) c = 76.0f;
			else if (c > 235.0f) c = 235.0f;
			rgba[f] = (byte)c;
		}
		rgba[3] = 224;

		inv2R = 0.5f / (radius * radius);
		for (f = 0; f < numFrags; f++) {
			const markFragment_t *mf = &fragBuf[f];
			srfMarkFragment_t    *dst;
			int                   k;

			if (mf->numPoints < 3) {
				continue;
			}
			if (s_numStaticCorpseMarks >= MAX_STATIC_CORPSE_MARKS
				|| s_numStaticCorpseVerts + mf->numPoints > MAX_STATIC_CORPSE_VERTS) {
				break;
			}

			dst              = &s_staticCorpseMarks[s_numStaticCorpseMarks];
			dst->surfaceType = SF_MARK_FRAG;
			dst->hShader     = tr.bloodPoolShader->index;
			dst->iIndex      = mf->iIndex;
			dst->numVerts    = mf->numPoints;
			dst->verts       = &s_staticCorpseVerts[s_numStaticCorpseVerts];
			VectorCopy(center, s_staticCorpseCenters[s_numStaticCorpseMarks]);

			for (k = 0; k < mf->numPoints; k++) {
				polyVert_t *pv = &dst->verts[k];
				const vec_t *pt = pointBuf[mf->firstPoint + k];
				vec3_t delta;

				VectorCopy(pt, pv->xyz);
				if (mf->iIndex <= 0) {
					pv->xyz[2] += 0.15f;
				}
				VectorSubtract(pt, center, delta);
				pv->st[0] = 0.5f + DotProduct(delta, axU) * inv2R;
				pv->st[1] = 0.5f + DotProduct(delta, axV) * inv2R;
				pv->modulate[0] = rgba[0];
				pv->modulate[1] = rgba[1];
				pv->modulate[2] = rgba[2];
				pv->modulate[3] = rgba[3];
			}

			s_numStaticCorpseVerts += mf->numPoints;
			s_numStaticCorpseMarks++;
		}
	}
}

void R_AddStaticCorpseBloodPools(void)
{
	static cvar_t *r_goreCvar = NULL;
	int i;

	if (!tr.world || (tr.refdef.rdflags & RDF_NOWORLDMODEL) || !tr.bloodPoolShader) {
		return;
	}

	if (!r_goreCvar) {
		r_goreCvar = ri.Cvar_Get("cg_gore", "1", CVAR_ARCHIVE);
	}
	if (!r_goreCvar || r_goreCvar->integer <= 0) {
		return;
	}

	if (s_builtStaticModelsPtr != tr.world->staticModels
		|| s_builtStaticModelsCount != tr.world->numStaticModels
		|| Q_stricmp(s_builtWorldName, tr.world->baseName) != 0) {
		R_BuildStaticCorpseBloodPools();
	}

	for (i = 0; i < s_numStaticCorpseMarks; i++) {
		vec3_t d;
		VectorSubtract(s_staticCorpseCenters[i], tr.viewParms.ori.origin, d);
		if (DotProduct(d, d) > 2400.0f * 2400.0f) {
			continue;
		}
		R_AddDrawSurf(&s_staticCorpseMarks[i].surfaceType, tr.bloodPoolShader, 0, 0, 0, 0);
	}
}
