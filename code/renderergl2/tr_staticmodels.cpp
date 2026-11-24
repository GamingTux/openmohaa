/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// tr_staticmodels.cpp -- static model rendering

#include "tr_local.h"
#include "../corepp/tiki.h"
#include "../renderercommon/tr_threadpool.h"
#include <stddef.h>

#define MAX_STATIC_MODELS_SURFS    16384
#define MAX_DISTINCT_STATIC_MODELS 1000

typedef struct {
    float   xyz[3];
    int16_t normal[4];
    float   st[2];
    uint8_t color[4];
} staticModelVaoVert_t;

static int R_BakeStaticSurfLod(const skelSurfaceGame_t *surf, int targetVerts, glIndex_t *outIndices)
{
    int           i, j, emitted = 0;
    int           indexes = surf->numTriangles * 3;
    skelIndex_t  *triangles = surf->pTriangles;
    skelIndex_t  *collapse_map = surf->pCollapse;
    short         collapse[4096];

    if (!triangles || indexes <= 0 || surf->numVerts <= 0) {
        return 0;
    }

    if (targetVerts >= surf->numVerts || !collapse_map || surf->numVerts > (int)ARRAY_LEN(collapse)) {
        for (j = 0; j < indexes; j++) {
            outIndices[j] = (glIndex_t)triangles[j];
        }
        return indexes;
    }

    if (targetVerts < 3) {
        targetVerts = 3;
    }

    for (i = 0; i < targetVerts; i++) {
        collapse[i] = (short)i;
    }
    for (i = targetVerts; i < surf->numVerts; i++) {
        collapse[i] = collapse[collapse_map[i]];
    }

    for (j = 0; j < indexes; j += 3) {
        short a = collapse[triangles[j]];
        short b = collapse[triangles[j + 1]];
        short c = collapse[triangles[j + 2]];
        if (a == b || b == c || c == a) {
            break;
        }
        outIndices[emitted++] = (glIndex_t)a;
        outIndices[emitted++] = (glIndex_t)b;
        outIndices[emitted++] = (glIndex_t)c;
    }

    return emitted;
}

static int R_StaticSurfHash(const skelSurfaceGame_t *surf, int mask)
{
    uintptr_t p = (uintptr_t)surf;
    return (int)(((p >> 4) ^ (p >> 11) ^ (p >> 19)) & (uintptr_t)mask);
}

const staticModelSurfIbo_t *R_FindStaticModelSurfIbo(const skelSurfaceGame_t *surf)
{
    int mask, slot, probe;

    if (!tr.staticModelSurfIbos || tr.staticModelSurfIboCap <= 0 || !surf) {
        return NULL;
    }

    mask = tr.staticModelSurfIboCap - 1;
    slot = R_StaticSurfHash(surf, mask);
    for (probe = 0; probe < tr.staticModelSurfIboCap; probe++) {
        const staticModelSurfIbo_t *entry = &tr.staticModelSurfIbos[slot];
        if (!entry->surf) {
            return NULL;
        }
        if (entry->surf == surf) {
            return entry;
        }
        slot = (slot + 1) & mask;
    }
    return NULL;
}

void R_FreeStaticModelVao(void)
{
    if (tr.staticModelVao) {
        vao_t *vao = tr.staticModelVao;
        if (glState.currentVao == vao) {
            R_BindNullVao();
        }
        if (vao->vao) {
            qglDeleteVertexArrays(1, &vao->vao);
        }
        if (vao->vertexesVBO) {
            qglDeleteBuffers(1, &vao->vertexesVBO);
        }
        if (vao->indexesIBO) {
            qglDeleteBuffers(1, &vao->indexesIBO);
        }
        ri.Free(vao);
        tr.staticModelVao = NULL;
    }

    if (tr.staticModelSurfIbos) {
        ri.Free(tr.staticModelSurfIbos);
        tr.staticModelSurfIbos = NULL;
    }
    tr.numStaticModelSurfIbos = 0;
    tr.staticModelSurfIboCap  = 0;
}

void R_BuildStaticModelVao(void)
{
    int                   i, j, k, l;
    int                   totalVerts = 0;
    int                   totalIndexes = 0;
    int                   uniqueSurfs = 0;
    int                   cap = 256;
    int                   mask;
    int                   curVert = 0;
    int                   curIndex = 0;
    staticModelVaoVert_t *verts = NULL;
    glIndex_t            *indices = NULL;
    vao_t                *vao = NULL;
    size_t                totalDataBytes;

    R_FreeStaticModelVao();

    if (!tr.world || tr.world->numStaticModels <= 0 || !glRefConfig.vertexArrayObject || !qglDrawElementsBaseVertex) {
        return;
    }

    // Pass 1: count vertices per valid instance and collect unique surface count
    for (i = 0; i < tr.world->numStaticModels; i++) {
        cStaticModelUnpacked_t *pSM = &tr.world->staticModels[i];
        int                     instVerts = 0;
        qboolean                valid = qtrue;

        pSM->staticVaoBaseVertex = -1;
        if (!pSM->tiki) {
            continue;
        }

        for (j = 0; j < pSM->tiki->numMeshes; j++) {
            skelHeaderGame_t *skelmodel = ri.TIKI_GetSkel(pSM->tiki->mesh[j]);
            skelSurfaceGame_t *surf;
            if (!skelmodel) {
                continue;
            }
            surf = skelmodel->pSurfaces;
            for (k = 0; k < skelmodel->numSurfaces; k++, surf = surf->pNext) {
                if (!surf->pStaticXyz || !surf->pStaticNormal || !surf->pStaticTexCoords) {
                    valid = qfalse;
                    break;
                }
                instVerts += surf->numVerts;
                uniqueSurfs++;
            }
            if (!valid) {
                break;
            }
        }

        if (valid && instVerts > 0) {
            pSM->staticVaoBaseVertex = totalVerts;
            totalVerts += instVerts;
        }
    }

    if (totalVerts <= 0) {
        return;
    }

    while (cap < uniqueSurfs * 2 && cap < 65536) {
        cap <<= 1;
    }
    if (cap < 256) {
        cap = 256;
    }

    tr.staticModelSurfIbos = (staticModelSurfIbo_t *)ri.Malloc(sizeof(staticModelSurfIbo_t) * cap);
    Com_Memset(tr.staticModelSurfIbos, 0, sizeof(staticModelSurfIbo_t) * cap);
    tr.staticModelSurfIboCap  = cap;
    tr.numStaticModelSurfIbos = 0;
    mask = cap - 1;

    // Register unique surfaces & compute upper-bound index count (LOD0 + LOD1 + LOD2)
    for (i = 0; i < tr.world->numStaticModels; i++) {
        cStaticModelUnpacked_t *pSM = &tr.world->staticModels[i];
        if (pSM->staticVaoBaseVertex < 0 || !pSM->tiki) {
            continue;
        }
        for (j = 0; j < pSM->tiki->numMeshes; j++) {
            skelHeaderGame_t *skelmodel = ri.TIKI_GetSkel(pSM->tiki->mesh[j]);
            skelSurfaceGame_t *surf;
            if (!skelmodel) {
                continue;
            }
            surf = skelmodel->pSurfaces;
            for (k = 0; k < skelmodel->numSurfaces; k++, surf = surf->pNext) {
                int slot = R_StaticSurfHash(surf, mask);
                int probe;
                qboolean found = qfalse;

                for (probe = 0; probe < cap; probe++) {
                    staticModelSurfIbo_t *e = &tr.staticModelSurfIbos[slot];
                    if (e->surf == surf) {
                        found = qtrue;
                        break;
                    }
                    if (!e->surf) {
                        int triIdx = surf->numTriangles * 3;
                        e->surf = surf;
                        // Temporarily store whether LOD1/LOD2 should be baked in minVerts[1/2]
                        e->minVerts[0] = (skelmodel->pLOD && surf->pCollapse && surf->numVerts >= 24 && surf->numVerts <= 4096) ? 1 : 0;
                        e->minVerts[1] = (skelmodel->pLOD && surf->pCollapse && surf->numVerts >= 40 && surf->numVerts <= 4096) ? 1 : 0;
                        totalIndexes += triIdx;
                        if (e->minVerts[0]) {
                            totalIndexes += triIdx;
                        }
                        if (e->minVerts[1]) {
                            totalIndexes += triIdx;
                        }
                        tr.numStaticModelSurfIbos++;
                        found = qtrue;
                        break;
                    }
                    slot = (slot + 1) & mask;
                }
                if (!found) {
                    pSM->staticVaoBaseVertex = -1;
                    break;
                }
            }
        }
    }

    if (totalIndexes <= 0) {
        R_FreeStaticModelVao();
        return;
    }

    verts   = (staticModelVaoVert_t *)ri.Malloc((size_t)totalVerts * sizeof(staticModelVaoVert_t));
    indices = (glIndex_t *)ri.Malloc((size_t)totalIndexes * sizeof(glIndex_t));

    // Bake unique LOD0 / LOD1 (~55%) / LOD2 (~28%) index slices
    for (i = 0; i < cap; i++) {
        staticModelSurfIbo_t *e = &tr.staticModelSurfIbos[i];
        const skelSurfaceGame_t *surf = e->surf;
        qboolean wantLod1, wantLod2;

        if (!surf) {
            continue;
        }
        wantLod1 = (e->minVerts[0] != 0);
        wantLod2 = (e->minVerts[1] != 0);

        e->firstIndex[0] = curIndex;
        e->numIndexes[0] = R_BakeStaticSurfLod(surf, surf->numVerts, indices + curIndex);
        curIndex += e->numIndexes[0];
        e->minVerts[0] = wantLod1 ? ((surf->numVerts * 72) / 100) : 0;

        if (wantLod1) {
            int rc1 = Q_max(3, (surf->numVerts * 55) / 100);
            e->firstIndex[1] = curIndex;
            e->numIndexes[1] = R_BakeStaticSurfLod(surf, rc1, indices + curIndex);
            curIndex += e->numIndexes[1];
            e->minVerts[1] = wantLod2 ? ((surf->numVerts * 38) / 100) : 0;
        } else {
            e->firstIndex[1] = e->firstIndex[0];
            e->numIndexes[1] = 0;
            e->minVerts[1]   = 0;
        }

        if (wantLod2) {
            int rc2 = Q_max(3, (surf->numVerts * 28) / 100);
            e->firstIndex[2] = curIndex;
            e->numIndexes[2] = R_BakeStaticSurfLod(surf, rc2, indices + curIndex);
            curIndex += e->numIndexes[2];
            e->minVerts[2] = 0;
        } else {
            e->firstIndex[2] = e->firstIndex[1];
            e->numIndexes[2] = 0;
            e->minVerts[2]   = 0;
        }
    }

    // Fill interleaved static model vertex buffer with per-instance baked colors
    totalDataBytes = (size_t)tr.world->numStaticModelData * sizeof(color4ub_t);
    for (i = 0; i < tr.world->numStaticModels; i++) {
        cStaticModelUnpacked_t *pSM = &tr.world->staticModels[i];
        int                     ofsStaticData = 0;

        if (pSM->staticVaoBaseVertex < 0 || !pSM->tiki) {
            continue;
        }

        curVert = pSM->staticVaoBaseVertex;
        for (j = 0; j < pSM->tiki->numMeshes; j++) {
            skelHeaderGame_t *skelmodel = ri.TIKI_GetSkel(pSM->tiki->mesh[j]);
            skelSurfaceGame_t *surf;
            if (!skelmodel) {
                continue;
            }
            surf = skelmodel->pSurfaces;
            for (k = 0; k < skelmodel->numSurfaces; k++, ofsStaticData += surf->numVerts, surf = surf->pNext) {
                const color4ub_t *inColors = NULL;
                if (tr.world->staticModelData && pSM->firstVertexData >= 0) {
                    size_t byteOfs = (size_t)pSM->firstVertexData + (size_t)ofsStaticData * sizeof(color4ub_t);
                    if (byteOfs + (size_t)surf->numVerts * sizeof(color4ub_t) <= totalDataBytes) {
                        inColors = (const color4ub_t *)&tr.world->staticModelData[byteOfs];
                    }
                }

                for (l = 0; l < surf->numVerts; l++) {
                    staticModelVaoVert_t *v = &verts[curVert++];
                    v->xyz[0] = surf->pStaticXyz[l][0];
                    v->xyz[1] = surf->pStaticXyz[l][1];
                    v->xyz[2] = surf->pStaticXyz[l][2];
                    R_VaoPackNormal(v->normal, surf->pStaticNormal[l]);
                    v->st[0]  = surf->pStaticTexCoords[l][0][0];
                    v->st[1]  = surf->pStaticTexCoords[l][0][1];
                    if (inColors) {
                        v->color[0] = inColors[l][0];
                        v->color[1] = inColors[l][1];
                        v->color[2] = inColors[l][2];
                        v->color[3] = inColors[l][3];
                    } else {
                        v->color[0] = 255;
                        v->color[1] = 255;
                        v->color[2] = 255;
                        v->color[3] = 255;
                    }
                }
            }
        }
    }

    R_IssuePendingRenderCommands();

    vao = (vao_t *)ri.Malloc(sizeof(vao_t));
    Com_Memset(vao, 0, sizeof(vao_t));
    Q_strncpyz(vao->name, "staticModels_VAO", sizeof(vao->name));

    vao->attribs[ATTR_INDEX_POSITION  ].enabled    = 1;
    vao->attribs[ATTR_INDEX_NORMAL    ].enabled    = 1;
    vao->attribs[ATTR_INDEX_TEXCOORD  ].enabled    = 1;
    vao->attribs[ATTR_INDEX_LIGHTCOORD].enabled    = 1;
    vao->attribs[ATTR_INDEX_COLOR     ].enabled    = 1;

    vao->attribs[ATTR_INDEX_POSITION  ].count      = 3;
    vao->attribs[ATTR_INDEX_NORMAL    ].count      = 4;
    vao->attribs[ATTR_INDEX_TEXCOORD  ].count      = 2;
    vao->attribs[ATTR_INDEX_LIGHTCOORD].count      = 2;
    vao->attribs[ATTR_INDEX_COLOR     ].count      = 4;

    vao->attribs[ATTR_INDEX_POSITION  ].type       = GL_FLOAT;
    vao->attribs[ATTR_INDEX_NORMAL    ].type       = GL_SHORT;
    vao->attribs[ATTR_INDEX_TEXCOORD  ].type       = GL_FLOAT;
    vao->attribs[ATTR_INDEX_LIGHTCOORD].type       = GL_FLOAT;
    vao->attribs[ATTR_INDEX_COLOR     ].type       = GL_UNSIGNED_BYTE;

    vao->attribs[ATTR_INDEX_POSITION  ].normalized = GL_FALSE;
    vao->attribs[ATTR_INDEX_NORMAL    ].normalized = GL_TRUE;
    vao->attribs[ATTR_INDEX_TEXCOORD  ].normalized = GL_FALSE;
    vao->attribs[ATTR_INDEX_LIGHTCOORD].normalized = GL_FALSE;
    vao->attribs[ATTR_INDEX_COLOR     ].normalized = GL_TRUE;

    vao->attribs[ATTR_INDEX_POSITION  ].offset     = (int)offsetof(staticModelVaoVert_t, xyz);
    vao->attribs[ATTR_INDEX_NORMAL    ].offset     = (int)offsetof(staticModelVaoVert_t, normal);
    vao->attribs[ATTR_INDEX_TEXCOORD  ].offset     = (int)offsetof(staticModelVaoVert_t, st);
    vao->attribs[ATTR_INDEX_LIGHTCOORD].offset     = (int)offsetof(staticModelVaoVert_t, st);
    vao->attribs[ATTR_INDEX_COLOR     ].offset     = (int)offsetof(staticModelVaoVert_t, color);

    vao->attribs[ATTR_INDEX_POSITION  ].stride     = (int)sizeof(staticModelVaoVert_t);
    vao->attribs[ATTR_INDEX_NORMAL    ].stride     = (int)sizeof(staticModelVaoVert_t);
    vao->attribs[ATTR_INDEX_TEXCOORD  ].stride     = (int)sizeof(staticModelVaoVert_t);
    vao->attribs[ATTR_INDEX_LIGHTCOORD].stride     = (int)sizeof(staticModelVaoVert_t);
    vao->attribs[ATTR_INDEX_COLOR     ].stride     = (int)sizeof(staticModelVaoVert_t);

    qglGenVertexArrays(1, &vao->vao);
    qglBindVertexArray(vao->vao);

    vao->vertexesSize = totalVerts * (int)sizeof(staticModelVaoVert_t);
    qglGenBuffers(1, &vao->vertexesVBO);
    qglBindBuffer(GL_ARRAY_BUFFER, vao->vertexesVBO);
    qglBufferData(GL_ARRAY_BUFFER, vao->vertexesSize, verts, GL_STATIC_DRAW);

    vao->indexesSize = curIndex * (int)sizeof(glIndex_t);
    qglGenBuffers(1, &vao->indexesIBO);
    qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vao->indexesIBO);
    qglBufferData(GL_ELEMENT_ARRAY_BUFFER, vao->indexesSize, indices, GL_STATIC_DRAW);

    Vao_SetVertexPointers(vao);
    glState.currentVao = vao;
    R_BindNullVao();
    GL_CheckErrors();

    tr.staticModelVao = vao;

    ri.Free(verts);
    ri.Free(indices);

    ri.Printf(PRINT_DEVELOPER, "StaticModel VAO: %d verts (%.2f MB), %d indices (%.2f MB), %d unique surfs\n",
        totalVerts, vao->vertexesSize / (1024.0f * 1024.0f),
        curIndex, vao->indexesSize / (1024.0f * 1024.0f),
        tr.numStaticModelSurfIbos);
}

int             g_nStaticSurfaces;
staticSurface_t g_staticSurfaces[MAX_STATIC_MODELS_SURFS];
qboolean        g_bInfostaticmodels = qfalse;

/*
==============
R_InitStaticModels
==============
*/
void R_InitStaticModels(void)
{
    cStaticModelUnpacked_t *pSM;
    char                    szTemp[1024];
    skelBoneCache_t         bones[128];
    float                   radius;
    int                     i, j, k, l;

    g_bInfostaticmodels = qfalse;

    if (tr.overbrightShift) {
        for (i = 0; i < tr.world->numStaticModelData; i++) {
            int r, g, b;

            r = (int)((float)tr.world->staticModelData[i * 4] * tr.overbrightMult);
            g = (int)((float)tr.world->staticModelData[i * 4 + 1] * tr.overbrightMult);
            b = (int)((float)tr.world->staticModelData[i * 4 + 2] * tr.overbrightMult);

            if (r > 0xFF || g > 0xFF || b > 0xFF) {
                float t;

                t = 255.0 / (float)Q_max(r, Q_max(g, b));

                r = (int)((float)r * t);
                g = (int)((float)g * t);
                b = (int)((float)b * t);
            }

            tr.world->staticModelData[i * 4]     = r;
            tr.world->staticModelData[i * 4 + 1] = g;
            tr.world->staticModelData[i * 4 + 2] = b;
        }
    }

    for (i = 0; i < tr.world->numStaticModels; i++) {
        vec3_t mins, maxs;

        pSM = &tr.world->staticModels[i];

        pSM->bRendered = qfalse;
        AngleVectorsLeft(pSM->angles, pSM->axis[0], pSM->axis[1], pSM->axis[2]);

        if (!strnicmp(pSM->model, "models", 6)) {
            Q_strncpyz(szTemp, pSM->model, sizeof(szTemp));
        } else {
            Com_sprintf(szTemp, sizeof(szTemp), "models/%s", pSM->model);
        }

        ri.FS_CanonicalFilename(szTemp);
        //const bool exists = ri.TIKI_FindTiki(szTemp) != NULL;
        pSM->tiki = ri.TIKI_RegisterTikiFlags(szTemp, qfalse);

        if (!pSM->tiki) {
            ri.Printf(PRINT_WARNING, "^~^~^: Warning: Cannot Load Static Model %s\n", szTemp);
            continue;
        }

        pSM->radius = ri.TIKI_GlobalRadius(pSM->tiki);

        //
        // register shaders
        //
        for (j = 0; j < pSM->tiki->num_surfaces; j++) {
            dtikisurface_t *surf = &pSM->tiki->surfaces[j];

            for (k = 0; k < surf->numskins; k++) {
                if (surf->shader[k][0]) {
                    shader_t *sh = R_FindShader(surf->shader[k], -1, !(surf->flags & TIKI_SURF_NOMIPMAPS));
                    surf->hShader[k] = sh->index;
                } else {
                    surf->hShader[k] = 0;
                }
            }
        }

        // prepare the skeleton frame for the static model
        ri.TIKI_GetSkelAnimFrame(pSM->tiki, bones, &radius, &mins, &maxs);
        pSM->cull_radius = radius * pSM->tiki->load_scale * pSM->scale;

        // Suggestion:
        // It would be cool to have animated static model in the future

        // Removed in 2.0
        //  This only leads to issues where pStaticXyz is not initialized
        //  and it can occurs if the TIKI model is registered before the static model
        //if (exists) {
        //    continue;
        //}

        for (j = 0; j < pSM->tiki->numMeshes; j++) {
            skelHeaderGame_t  *skelmodel = ri.TIKI_GetSkel(pSM->tiki->mesh[j]);
            skelSurfaceGame_t *surf;

            if (!skelmodel) {
                ri.Printf(PRINT_WARNING, "^~^~^: Warning: Missing mesh in Static Model %s\n", skelmodel->name);
                continue;
            }

            surf = skelmodel->pSurfaces;

            for (k = 0; k < skelmodel->numSurfaces; k++, surf = surf->pNext) {
                byte             *buf;
                byte             *p;
                skelWeight_t     *weight;
                skeletorVertex_t *vert;

                if (surf->pStaticXyz) {
                    continue;
                }

                // allocate static vectors
                p = buf =
                    (byte *)ri.TIKI_Alloc((sizeof(vec4_t) + sizeof(vec4_t) + sizeof(vec2_t) * 2) * surf->numVerts);
                surf->pStaticXyz = (vec4_t *)p;
                p += sizeof(vec4_t) * surf->numVerts;
                surf->pStaticNormal = (vec4_t *)p;
                p += sizeof(vec4_t) * surf->numVerts;
                surf->pStaticTexCoords = (vec2_t(*)[2])p;

                vert = surf->pVerts;

                for (l = 0; l < surf->numVerts; l++) {
                    int              channel;
                    skelBoneCache_t *bone;

                    weight = (skelWeight_t *)((byte *)vert + sizeof(skeletorVertex_t)
                                              + vert->numMorphs * sizeof(skeletorMorph_t));

                    if (j > 0) {
                        channel = ri.TIKI_GetLocalChannel(pSM->tiki, skelmodel->pBones[weight->boneIndex].channel);
                    } else {
                        channel = weight->boneIndex;
                    }

                    bone = &bones[channel];

                    surf->pStaticXyz[l][0] =
                        ((weight->offset[0] * bone->matrix[0][0] + weight->offset[1] * bone->matrix[1][0]
                          + weight->offset[2] * bone->matrix[2][0])
                         + bone->offset[0])
                        * weight->boneWeight;
                    surf->pStaticXyz[l][1] =
                        ((weight->offset[0] * bone->matrix[0][1] + weight->offset[1] * bone->matrix[1][1]
                          + weight->offset[2] * bone->matrix[2][1])
                         + bone->offset[1])
                        * weight->boneWeight;
                    surf->pStaticXyz[l][2] =
                        ((weight->offset[0] * bone->matrix[0][2] + weight->offset[1] * bone->matrix[1][2]
                          + weight->offset[2] * bone->matrix[2][2])
                         + bone->offset[2])
                        * weight->boneWeight;
                    surf->pStaticXyz[l][3] = 0.f;

                    surf->pStaticNormal[l][0] = vert->normal[0] * bone->matrix[0][0]
                                              + vert->normal[1] * bone->matrix[1][0]
                                              + vert->normal[2] * bone->matrix[2][0];
                    surf->pStaticNormal[l][1] = vert->normal[0] * bone->matrix[0][1]
                                              + vert->normal[1] * bone->matrix[1][1]
                                              + vert->normal[2] * bone->matrix[2][1];
                    surf->pStaticNormal[l][2] = vert->normal[0] * bone->matrix[0][2]
                                              + vert->normal[1] * bone->matrix[1][2]
                                              + vert->normal[2] * bone->matrix[2][2];
                    surf->pStaticNormal[l][3] = 0.f;

                    surf->pStaticTexCoords[l][0][0] = vert->texCoords[0];
                    surf->pStaticTexCoords[l][0][1] = vert->texCoords[1];
                    surf->pStaticTexCoords[l][1][0] = vert->texCoords[0];
                    surf->pStaticTexCoords[l][1][1] = vert->texCoords[1];

                    vert = (skeletorVertex_t *)((byte *)vert + sizeof(skeletorVertex_t)
                                                + sizeof(skeletorMorph_t) * vert->numMorphs
                                                + sizeof(skelWeight_t) * vert->numWeights);
                }
            }
        }
    }

    R_BuildStaticModelVao();

    tr.refdef.numStaticModels    = tr.world->numStaticModels;
    tr.refdef.staticModels       = tr.world->staticModels;
    tr.refdef.numStaticModelData = tr.world->numStaticModelData;
    tr.refdef.staticModelData    = tr.world->staticModelData;
}

static int R_CullStaticModelBox(const cStaticModelUnpacked_t *SM, dtiki_t *tiki, float fScale, const vec3_t vLocalOrg)
{
    vec3_t   bounds[2];
    vec3_t   transformed[8];
    vec3_t   v;
    int      i, j;
    int      anyBack = 0;
    int      front, back;
    cplane_t *frust;

    if (r_nocull->integer) {
        return CULL_CLIP;
    }

    for (i = 0; i < 3; i++) {
        bounds[0][i] = vLocalOrg[i] + tiki->a->mins[i] * fScale;
        bounds[1][i] = vLocalOrg[i] + tiki->a->maxs[i] * fScale;
    }

    for (i = 0; i < 8; i++) {
        v[0] = bounds[i & 1][0];
        v[1] = bounds[(i >> 1) & 1][1];
        v[2] = bounds[(i >> 2) & 1][2];

        VectorCopy(SM->origin, transformed[i]);
        VectorMA(transformed[i], v[0], SM->axis[0], transformed[i]);
        VectorMA(transformed[i], v[1], SM->axis[1], transformed[i]);
        VectorMA(transformed[i], v[2], SM->axis[2], transformed[i]);
    }

    for (i = 0; i < tr.viewParms.fog.extrafrustums + 4; i++) {
        frust = &tr.viewParms.frustum[i];
        front = back = 0;
        for (j = 0; j < 8; j++) {
            if (DotProduct(transformed[j], frust->normal) > frust->dist) {
                front = 1;
                if (back) {
                    break;
                }
            } else {
                back = 1;
            }
        }
        if (!front) {
            return CULL_OUT;
        }
        anyBack |= back;
    }

    return anyBack ? CULL_CLIP : CULL_IN;
}

typedef struct {
    byte *visFlags;
} staticModelCullCtx_t;

static void R_ParallelCullStaticModels(int start, int end, void *ctx)
{
    staticModelCullCtx_t *cullCtx = (staticModelCullCtx_t *)ctx;
    byte                 *visFlags = cullCtx->visFlags;

    for (int i = start; i < end; i++) {
        cStaticModelUnpacked_t *SM = &tr.world->staticModels[i];

        if (tr.world->numVisStaticModels && SM->visCount != tr.viewCount) {
            visFlags[i] = 0;
            continue;
        }

        dtiki_t *tiki = SM->tiki;
        if (!tiki) {
            visFlags[i] = 0;
            continue;
        }

        if ( tr.viewParms.flags & VPF_PISUNSHADOW ) {
            float minRad = ( r_dynamicSunShadows && r_dynamicSunShadows->integer >= 2 ) ? 16.0f : 26.0f;
            if ( SM->cull_radius < minRad ) {
                visFlags[i] = 0;
                continue;
            }
        }

        float  tiki_scale = tiki->load_scale * SM->scale;
        vec3_t tiki_localorigin;
        vec3_t tiki_worldorigin;
        VectorScale(tiki->load_origin, tiki_scale, tiki_localorigin);
        tiki_worldorigin[0] = SM->origin[0] + tiki_localorigin[0] * SM->axis[0][0]
                            + tiki_localorigin[1] * SM->axis[1][0] + tiki_localorigin[2] * SM->axis[2][0];
        tiki_worldorigin[1] = SM->origin[1] + tiki_localorigin[0] * SM->axis[0][1]
                            + tiki_localorigin[1] * SM->axis[1][1] + tiki_localorigin[2] * SM->axis[2][1];
        tiki_worldorigin[2] = SM->origin[2] + tiki_localorigin[0] * SM->axis[0][2]
                            + tiki_localorigin[1] * SM->axis[1][2] + tiki_localorigin[2] * SM->axis[2][2];

        if ( tr.viewParms.flags & VPF_PISUNSHADOW ) {
            // Cull static models behind the camera whose sun-extruded shadow footprint also stays behind the camera
            vec3_t camDelta;
            float  fwdDot;
            VectorSubtract( tiki_worldorigin, tr.refdef.vieworg, camDelta );
            fwdDot = DotProduct( camDelta, tr.refdef.viewaxis[0] );
            if ( fwdDot < -160.0f - SM->cull_radius ) {
                float dz      = Q_max( 0.0f, camDelta[2] + SM->cull_radius );
                float drop    = Q_min( dz, 500.0f ) / Q_max( 0.25f, tr.refdef.piSunShadowDir[2] );
                float projFwd = fwdDot - drop * DotProduct( tr.refdef.piSunShadowDir, tr.refdef.viewaxis[0] );
                if ( projFwd < -140.0f - SM->cull_radius ) {
                    visFlags[i] = 0;
                    continue;
                }
            }
        }

        int iRadiusCull = R_CullPointAndRadius(tiki_worldorigin, SM->cull_radius);
        if (iRadiusCull == CULL_OUT) {
            visFlags[i] = 0;
            continue;
        }

        if (iRadiusCull == CULL_CLIP && R_CullStaticModelBox(SM, tiki, tiki_scale, tiki_localorigin) == CULL_OUT) {
            visFlags[i] = 0;
            continue;
        }

        if (tr.viewParms.isPortal) {
            SM->lodpercentage[1] = R_CalcLod(tiki_worldorigin, SM->cull_radius / SM->scale);
        } else {
            SM->lodpercentage[0] = R_CalcLod(tiki_worldorigin, SM->cull_radius / SM->scale);
        }

        if (!SM->bLightGridCalculated && !(tr.viewParms.flags & VPF_PISUNSHADOW)) {
            RB_SetupStaticModelGridLighting(&tr.refdef, SM, SM->origin);
        }

        visFlags[i] = 1;
    }
}

/*
==============
R_CullStaticModel
==============
*/
static int R_CullStaticModel(dtiki_t *tiki, float fScale, const vec3_t vLocalOrg)
{
    vec3_t bounds[2];
    int    i;
    int    cull;

    for (i = 0; i < 3; i++) {
        bounds[0][i] = vLocalOrg[i] + tiki->a->mins[i] * fScale;
        bounds[1][i] = vLocalOrg[i] + tiki->a->maxs[i] * fScale;
    }

    cull = R_CullLocalBox(bounds);

    if (r_showcull->integer & 4) {
        float  fR, fG, fB;
        vec3_t vAngles;

        switch (cull) {
        case CULL_CLIP:
            fR = 1.0f;
            fG = 1.0f;
            fB = 0.0f;
            break;
        case CULL_IN:
            fR = 0.0f;
            fG = 1.0f;
            fB = 0.0f;
            break;
        case CULL_OUT:
            fR = 1.0f;
            fG = 0.2f;
            fB = 0.2f;
            for (i = 0; i < 2; i++) {
                bounds[0][i] = bounds[0][i] - 16.0;
                bounds[1][i] = bounds[1][i] + 16.0;
            }
            break;
        default:
            break;
        }

        MatrixToEulerAngles(tr.ori.axis, vAngles);
        R_DebugRotatedBBox(tr.ori.origin, vAngles, bounds[0], bounds[1], fR, fG, fB, 0.5);
    }

    switch (cull) {
    case CULL_CLIP:
        tr.pc.c_box_cull_md3_clip++;
        break;
    case CULL_IN:
        tr.pc.c_box_cull_md3_in++;
        break;
    case CULL_OUT:
        tr.pc.c_box_cull_md3_out++;
        break;
    }

    return cull;
}

/*
==============
R_AddStaticModelSurfaces
==============
*/
void R_AddStaticModelSurfaces(void)
{
    cStaticModelUnpacked_t *SM;
    int                     i, j, k;
    int                     ofsStaticData;
    int                     iRadiusCull;
    dtiki_t                *tiki;
    float                   tiki_scale;
    vec3_t                  tiki_localorigin;
    vec3_t                  tiki_worldorigin;

    if (!tr.world->numStaticModels) {
        return;
    }

    tr.shiftedIsStatic = (1 << QSORT_STATICMODEL_SHIFT);

    if (!(r_showcull->integer & 12) && !r_showstaticlod->integer && !r_showstaticbboxes->integer) {
        byte *visFlags = (byte *)ri.Hunk_AllocateTempMemory(tr.world->numStaticModels);
        staticModelCullCtx_t cullCtx = { visFlags };

        R_ParallelFor(tr.world->numStaticModels, 32, R_ParallelCullStaticModels, &cullCtx);

        for (i = 0; i < tr.world->numStaticModels; i++) {
            if (!visFlags[i]) {
                continue;
            }

            SM   = &tr.world->staticModels[i];
            tiki = SM->tiki;

            tr.currentEntityNum = i;
            tr.shiftedEntityNum = i << QSORT_REFENTITYNUM_SHIFT;
            ofsStaticData       = 0;

            tiki_scale = tiki->load_scale * SM->scale;
            VectorScale(tiki->load_origin, tiki_scale, tiki_localorigin);
            tiki_worldorigin[0] = SM->origin[0] + tiki_localorigin[0] * SM->axis[0][0]
                                + tiki_localorigin[1] * SM->axis[1][0] + tiki_localorigin[2] * SM->axis[2][0];
            tiki_worldorigin[1] = SM->origin[1] + tiki_localorigin[0] * SM->axis[0][1]
                                + tiki_localorigin[1] * SM->axis[1][1] + tiki_localorigin[2] * SM->axis[2][1];
            tiki_worldorigin[2] = SM->origin[2] + tiki_localorigin[0] * SM->axis[0][2]
                                + tiki_localorigin[1] * SM->axis[1][2] + tiki_localorigin[2] * SM->axis[2][2];

            dtikisurface_t *dsurf = tiki->surfaces;
            for (int mesh = 0; mesh < tiki->numMeshes; mesh++) {
                skelHeaderGame_t  *skelmodel = ri.TIKI_GetSkel(tiki->mesh[mesh]);
                skelSurfaceGame_t *surface;
                staticSurface_t   *s_surface;
                shader_t          *shader;
                float              fDist;
                vec3_t             vDelta;

                if (!skelmodel) {
                    continue;
                }

                surface = skelmodel->pSurfaces;
                for (j = 0; j < skelmodel->numSurfaces;
                     j++, ofsStaticData += surface->numVerts, surface = surface->pNext, dsurf++) {
                    shader = tr.shaders[dsurf->hShader[0]];

                    if ( (tr.viewParms.flags & VPF_PISUNSHADOW) && !R_ShaderCastsSunShadow(shader) ) {
                        continue;
                    }

                    if (g_nStaticSurfaces >= MAX_STATIC_MODELS_SURFS) {
                        ri.Printf(
                            PRINT_DEVELOPER,
                            "^~^~^ ERROR: MAX_STATIC_MODELS_SURFS exceeded - surface of '%s' skipped\n",
                            tiki->a->name
                        );
                        continue;
                    }

                    s_surface                = &g_staticSurfaces[g_nStaticSurfaces++];
                    s_surface->ident         = SF_TIKI_STATIC;
                    s_surface->ofsStaticData = ofsStaticData;
                    s_surface->surface       = surface;
                    s_surface->meshNum       = mesh;

                    if (shader->numUnfoggedPasses == 1 && !r_nocull->integer) {
                        switch (shader->stages[0]->alphaGen) {
                        case AGEN_DIST_FADE:
                            if (R_DistanceCullPointAndRadius(
                                    shader->fDistNear + shader->fDistRange, tiki_worldorigin, SM->cull_radius
                                )
                                == CULL_OUT) {
                                continue;
                            }
                            break;
                        case AGEN_ONE_MINUS_DIST_FADE:
                            if (R_DistanceCullPointAndRadius(shader->fDistNear, tiki_worldorigin, SM->cull_radius)
                                == CULL_IN) {
                                continue;
                            }
                            break;
                        case AGEN_TIKI_DIST_FADE:
                            fDist = (shader->fDistNear + shader->fDistRange);
                            VectorSubtract(tiki_worldorigin, tr.viewParms.ori.origin, vDelta);
                            if (VectorLengthSquared(vDelta) >= Square(fDist)) {
                                continue;
                            }
                            break;
                        case AGEN_ONE_MINUS_TIKI_DIST_FADE:
                            fDist = (shader->fDistNear + shader->fDistRange);
                            VectorSubtract(tiki_worldorigin, tr.viewParms.ori.origin, vDelta);
                            if (VectorLengthSquared(vDelta) <= Square(shader->fDistNear)) {
                                continue;
                            }
                            break;
                        }
                    }

                    SM->bRendered = qtrue;
                    R_AddDrawSurf((surfaceType_t *)s_surface, shader, 0, 0, 0, 0);
                }
            }
        }

        ri.Hunk_FreeTempMemory(visFlags);
        tr.shiftedIsStatic = 0;
        return;
    }

    for (i = 0; i < tr.world->numStaticModels; i++) {
        SM = &tr.world->staticModels[i];

        if (tr.world->numVisStaticModels && SM->visCount != tr.viewCount) {
            continue;
        }

        tiki = SM->tiki;

        if (!tiki) {
            continue;
        }

        tr.currentEntityNum = i;
        tr.shiftedEntityNum = i << QSORT_REFENTITYNUM_SHIFT;

        R_RotateForStaticModel(SM, &tr.viewParms, &tr.ori);

        ofsStaticData = 0;

        // get the world position
        tiki_scale = tiki->load_scale * SM->scale;
        VectorScale(tiki->load_origin, tiki_scale, tiki_localorigin);
        R_LocalPointToWorld(tiki_localorigin, tiki_worldorigin);

        iRadiusCull = R_CullPointAndRadius(tiki_worldorigin, SM->cull_radius);

        if (r_showcull->integer & 8) {
            switch (iRadiusCull) {
            case CULL_IN:
                R_DebugCircle(tiki_worldorigin, SM->cull_radius * 1.2, 0.0, 1.0, 0.0, 0.5, 0);
                break;
            case CULL_OUT:
                R_DebugCircle(tiki_worldorigin, SM->cull_radius * 1.4 + 16.0, 1.0, 0.2, 0.2, 0.5, 0);
                break;
            }
        }

        if (iRadiusCull != CULL_OUT
            && (iRadiusCull != CULL_CLIP || R_CullStaticModel(SM->tiki, tiki_scale, tiki_localorigin) != CULL_OUT)) {
            dtikisurface_t *dsurf;

            if (tr.viewParms.isPortal) {
                SM->lodpercentage[1] = R_CalcLod(tiki_worldorigin, SM->cull_radius / SM->scale);
            } else {
                SM->lodpercentage[0] = R_CalcLod(tiki_worldorigin, SM->cull_radius / SM->scale);
            }

            //
            // draw all meshes
            //
            dsurf = tiki->surfaces;
            for (int mesh = 0; mesh < tiki->numMeshes; mesh++) {
                skelHeaderGame_t  *skelmodel = ri.TIKI_GetSkel(tiki->mesh[mesh]);
                skelSurfaceGame_t *surface;
                staticSurface_t   *s_surface;
                shader_t          *shader;
                float              fDist;
                vec3_t             vDelta;

                if (!skelmodel) {
                    continue;
                }

                //
                // draw all surfaces
                //
                surface = skelmodel->pSurfaces;
                for (j = 0; j < skelmodel->numSurfaces;
                     j++, ofsStaticData += surface->numVerts, surface = surface->pNext, dsurf++) {
                    shader = tr.shaders[dsurf->hShader[0]];

                    if ( (tr.viewParms.flags & VPF_PISUNSHADOW) && !R_ShaderCastsSunShadow(shader) ) {
                        continue;
                    }

                    if (g_nStaticSurfaces >= MAX_STATIC_MODELS_SURFS) {
                        ri.Printf(
                            PRINT_DEVELOPER,
                            "^~^~^ ERROR: MAX_STATIC_MODELS_SURFS exceeded - surface of '%s' skipped\n",
                            tiki->a->name
                        );
                        continue;
                    }

                    s_surface                = &g_staticSurfaces[g_nStaticSurfaces++];
                    s_surface->ident         = SF_TIKI_STATIC;
                    s_surface->ofsStaticData = ofsStaticData;
                    s_surface->surface       = surface;
                    s_surface->meshNum       = mesh;

                    if (shader->numUnfoggedPasses == 1 && !r_nocull->integer) {
                        switch (shader->stages[0]->alphaGen) {
                        case AGEN_DIST_FADE:
                            if (R_DistanceCullPointAndRadius(
                                    shader->fDistNear + shader->fDistRange, tiki_worldorigin, SM->cull_radius
                                )
                                == CULL_OUT) {
                                continue;
                            }
                            break;
                        case AGEN_ONE_MINUS_DIST_FADE:
                            if (R_DistanceCullPointAndRadius(shader->fDistNear, tiki_worldorigin, SM->cull_radius)
                                == CULL_IN) {
                                continue;
                            }
                            break;
                        case AGEN_TIKI_DIST_FADE:
                            fDist = (shader->fDistNear + shader->fDistRange);
                            VectorSubtract(tiki_worldorigin, tr.viewParms.ori.origin, vDelta);
                            if (VectorLengthSquared(vDelta) >= Square(fDist)) {
                                continue;
                            }
                            break;
                        case AGEN_ONE_MINUS_TIKI_DIST_FADE:
                            fDist = (shader->fDistNear + shader->fDistRange);
                            VectorSubtract(tiki_worldorigin, tr.viewParms.ori.origin, vDelta);
                            if (VectorLengthSquared(vDelta) <= Square(shader->fDistNear)) {
                                continue;
                            }
                            break;
                        }
                    }

                    SM->bRendered = qtrue;
                    R_AddDrawSurf((surfaceType_t *)s_surface, shader, 0, 0, 0, 0);

                    if (r_showstaticlod->integer) {
                        vec3_t org;
                        int    render_count, total_tris;

                        VectorCopy(SM->origin, org);
                        org[2] += 100.0;
                        R_DrawDebugNumber(org, SM->lodpercentage[0], r_showstaticlod->value * 2, 1.0, 1.0, 0.0, 3);

                        org[2] += 125.0;
                        R_CountTikiLodTris(tiki, SM->lodpercentage[0], &render_count, &total_tris);
                        R_DrawDebugNumber(org, render_count, r_showstaticlod->value * 2, 1.0, 1.0, 0.0, 0);
                    }

                    if (r_showstaticbboxes->integer) {
                        vec3_t vMins, vMaxs;

                        for (k = 0; k < 3; k++) {
                            vMins[k] = tiki->a->mins[k] * tiki->load_scale * SM->scale;
                            vMaxs[k] = tiki->a->maxs[k] * tiki->load_scale * SM->scale;
                        }

                        R_DebugRotatedBBox(SM->origin, SM->angles, vMins, vMaxs, 1.0, 0.0, 1.0, 0.75);
                    }
                }
            }
        }
    }

    tr.shiftedIsStatic = 0;
}

/*
==============
RB_Static_BuildDLights
==============
*/
void RB_Static_BuildDLights()
{
    int i;

    backEnd.currentStaticModel->useSpecialLighting = backEnd.refdef.num_dlights > 0;
    backEnd.currentStaticModel->numdlights         = 0;

    if (!backEnd.currentStaticModel->useSpecialLighting) {
        return;
    }

    for (i = 0; i < backEnd.refdef.num_dlights && backEnd.currentStaticModel->numdlights != 32; i++) {
        vec3_t lightorigin, delta;

        VectorCopy(backEnd.refdef.dlights[i].origin, lightorigin);
        VectorSubtract(lightorigin, backEnd.currentStaticModel->origin, delta);
        if (backEnd.refdef.dlights[i].radius * 2.0 >= VectorLength(delta)) {
            MatrixTransformVectorRight(
                backEnd.currentStaticModel->axis,
                delta,
                backEnd.currentStaticModel->dlights[backEnd.currentStaticModel->numdlights].transformed
            );

            backEnd.currentStaticModel->dlights[backEnd.currentStaticModel->numdlights++].index = i;
        }
    }

    if (!backEnd.currentStaticModel->numdlights) {
        backEnd.currentStaticModel->useSpecialLighting = qfalse;
    }
}

/*
==============
R_InfoStaticModels_f
==============
*/
void R_InfoStaticModels_f(void)
{
    g_bInfostaticmodels = qtrue;
}

/*
==============
R_PrintInfoStaticModels
==============
*/
void R_PrintInfoStaticModels()
{
    int                     iRenderCount;
    int                     i, j;
    cStaticModelUnpacked_t *SM;
    dtiki_t                *tikis[MAX_DISTINCT_STATIC_MODELS];
    int                     tiki_count[MAX_DISTINCT_STATIC_MODELS];
    cStaticModelUnpacked_t *pSM[MAX_DISTINCT_STATIC_MODELS];
    int                     count;

    ri.Printf(PRINT_ALL, "Static model info:\n");
    ri.Printf(PRINT_ALL, "------------------\n");

    count        = 0;
    iRenderCount = 0;

    for (i = 0; i < tr.world->numStaticModels; i++) {
        SM = &tr.world->staticModels[i];
        if (!SM->bRendered) {
            continue;
        }

        SM->bRendered = qfalse;
        iRenderCount++;

        if (!SM->tiki) {
            ri.Printf(PRINT_ALL, "ERROR: static model with no tiki\n");
            continue;
        }

        for (j = 0; j < count; j++) {
            if (SM->tiki == tikis[j]) {
                tiki_count[j]++;
                break;
            }
        }

        if (count >= MAX_DISTINCT_STATIC_MODELS) {
            ri.Printf(PRINT_ALL, "R_PrintInfoStaticModels: MAX_DISTINCT_STATIC_MODELS exceeded - increase and recompile\n");
            break;
        }

        if (j == count) {
            tikis[count]      = SM->tiki;
            pSM[count]        = SM;
            tiki_count[count] = 1;

            count++;
        }
    }

    ri.Printf(PRINT_ALL, "Total static models rendered: %d\n", iRenderCount);

    for (i = 0; i < count; i++) {
        skelHeaderGame_t *skelmodel = ri.TIKI_GetSkel(tikis[i]->mesh[0]);

        ri.Printf(PRINT_ALL, 
            "model: %s, version: %d, count: %d,\n culling min %.1f %.1f %.1f, max %.1f %.1f %.1f, radius %.1f\n",
            tikis[i]->a->name,
            skelmodel ? skelmodel->version : -1,
            tiki_count[i],
            tikis[i]->a->mins[0],
            tikis[i]->a->mins[1],
            tikis[i]->a->mins[2],
            tikis[i]->a->maxs[0],
            tikis[i]->a->maxs[1],
            tikis[i]->a->maxs[2],
            pSM[i]->cull_radius
        );
    }
}
