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

// tr_sphere_shade.cpp -- sphere shade

#include "tr_local.h"
#include "../renderercommon/tr_threadpool.h"

typedef struct gatheredLight_s {
    spherel_t       *pLight;
    float            fIntensity;
    gatheredLight_s *pPrev;
    gatheredLight_s *pNext;
} gatheredLight_t;

vec3_t          spheredef[6];
suninfo_t       s_sun;
static vec3_t   ambientlight;
static qboolean bEntityOverbright;
static int      iEntityLightingMax;
static int      light_reference_count = 0;

const float one_half_root  = 0.70710678118654752440084436210485f;
const float one_third_root = 0.57735026918962576450914878050196f;

static vec3_t offsets[26] = {
    {1.0,             0.0,             0.0            },
    {-1.0,            0.0,             0.0            },
    {0.0,             1.0,             0.0            },
    {0.0,             -1.0,            0.0            },
    {0.0,             0.0,             1.0            },
    {0.0,             0.0,             -1.0           },
    {one_half_root,   one_half_root,   0.0            },
    {one_half_root,   -one_half_root,  0.0            },
    {-one_half_root,  one_half_root,   0.0            },
    {-one_half_root,  -one_half_root,  0.0            },
    {one_half_root,   0.0,             one_half_root  },
    {one_half_root,   0.0,             -one_half_root },
    {-one_half_root,  0.0,             one_half_root  },
    {-one_half_root,  0.0,             -one_half_root },
    {0.0,             one_half_root,   one_half_root  },
    {0.0,             one_half_root,   -one_half_root },
    {0.0,             -one_half_root,  one_half_root  },
    {0.0,             -one_half_root,  -one_half_root },
    {one_third_root,  one_third_root,  one_third_root },
    {one_third_root,  one_third_root,  -one_third_root},
    {one_third_root,  -one_third_root, one_third_root },
    {one_third_root,  -one_third_root, -one_third_root},
    {-one_third_root, one_third_root,  one_third_root },
    {-one_third_root, one_third_root,  -one_third_root},
    {-one_third_root, -one_third_root, one_third_root },
    {-one_third_root, -one_third_root, -one_third_root}
};

int compare_light_intensities(const void *p1, const void *p2)
{
    float ia = ((const reallightinfo_t *)p1)->fIntensity;
    float ib = ((const reallightinfo_t *)p2)->fIntensity;
    return (ib > ia) - (ib < ia);
}

static void RB_OptimizeLightsFor(sphereor_t *pSphere)
{
    static int iCubeBuildInfo[24][9] = {
        {0, 1, 2, 1,  1,  0, 6,  10, 18},
        {0, 1, 2, 1,  -1, 0, 6,  11, 19},
        {0, 1, 2, -1, 1,  0, 7,  10, 20},
        {0, 1, 2, -1, -1, 0, 7,  11, 21},
        {0, 1, 2, -1, -1, 1, 8,  12, 22},
        {0, 1, 2, -1, 1,  1, 8,  13, 23},
        {0, 1, 2, 1,  -1, 1, 9,  12, 24},
        {0, 1, 2, 1,  1,  1, 9,  13, 25},
        {1, 0, 2, 1,  1,  2, 6,  14, 18},
        {1, 0, 2, 1,  -1, 2, 6,  15, 19},
        {1, 0, 2, -1, -1, 3, 7,  16, 20},
        {1, 0, 2, -1, 1,  3, 7,  17, 21},
        {1, 0, 2, -1, 1,  2, 8,  14, 22},
        {1, 0, 2, -1, -1, 2, 8,  15, 23},
        {1, 0, 2, 1,  -1, 3, 9,  16, 24},
        {1, 0, 2, 1,  1,  3, 9,  17, 25},
        {2, 1, 0, 1,  1,  4, 14, 10, 18},
        {2, 1, 0, -1, -1, 5, 15, 11, 19},
        {2, 1, 0, -1, 1,  4, 16, 10, 20},
        {2, 1, 0, 1,  -1, 5, 17, 11, 21},
        {2, 1, 0, 1,  -1, 4, 14, 12, 22},
        {2, 1, 0, -1, 1,  5, 15, 13, 23},
        {2, 1, 0, -1, -1, 4, 16, 12, 24},
        {2, 1, 0, 1,  1,  5, 17, 13, 25}
    };
    int    i, j;
    int    nCubeMapLights;
    float  fMaxIntensity;
    vec3_t cubecolor[26];
    vec3_t colorout;
    float(*cube)[3][4];
    reallightinfo_t *pLight;

    nCubeMapLights = 0;

    if (pSphere->numRealLights > 2) {
        qsort(
            pSphere->light,
            pSphere->numRealLights,
            sizeof(reallightinfo_t),
            compare_light_intensities
        );

        fMaxIntensity = pSphere->light[0].fIntensity;
        for (i = 0; i < pSphere->numRealLights && i < r_entlight_maxcalc->integer; i++) {
            pLight = &pSphere->light[i];

            if (pLight->fIntensity < r_entlight_cubelevel->value
                || pLight->fIntensity < fMaxIntensity * r_entlight_cubefraction->value) {
                break;
            }
        }

        nCubeMapLights = pSphere->numRealLights - i;
        if (nCubeMapLights <= 1) {
            nCubeMapLights = 0;
        }
    }

    pSphere->bUsesCubeMap = nCubeMapLights > 0;
    if (pSphere->bUsesCubeMap) {
        for (i = 0; i < 26; i++) {
            float  fDot;
            vec3_t v;

            colorout[0] = pSphere->ambient.level[0];
            colorout[1] = pSphere->ambient.level[1];
            colorout[2] = pSphere->ambient.level[2];

            for (j = pSphere->numRealLights - nCubeMapLights; j < pSphere->numRealLights;
                 j++) {
                pLight = &pSphere->light[j];

                switch (pLight->eType) {
                case LIGHT_SPOT:
                    {
                        fDot = DotProduct(pLight->vDirection, offsets[i]);
                        if (fDot > 0) {
                            float fProjSquared;
                            float fDistSquared;

                            VectorSubtract(pLight->vOrigin, pSphere->origin, v);
                            fProjSquared  = Square(DotProduct(v, pLight->vDirection));
                            fDistSquared  = VectorLengthSquared(v);
                            fMaxIntensity = (pLight->fSpotConst - fDistSquared / fProjSquared) * pLight->fSpotScale;
                            if (fMaxIntensity > 0) {
                                fDot /= fDistSquared;
                                if (fMaxIntensity < 1) {
                                    fDot *= fMaxIntensity;
                                }

                                VectorMA(colorout, fDot, pLight->color, colorout);
                            }
                        }
                    }
                    break;
                case LIGHT_SPOT_FAST:
                    fDot = DotProduct(pLight->vDirection, offsets[i]);
                    if (fDot > 0) {
                        float fDistSquared;

                        VectorSubtract(pLight->vOrigin, pSphere->origin, v);
                        fDistSquared = VectorLengthSquared(v);

                        fDot /= fDistSquared;
                        VectorMA(colorout, fDot, pLight->color, colorout);
                    }
                    break;
                case LIGHT_DIRECTIONAL:
                    fDot = DotProduct(pLight->vDirection, offsets[i]);
                    if (fDot > 0) {
                        VectorMA(colorout, fDot, pLight->color, colorout);
                    }
                    break;
                case LIGHT_POINT:
                    VectorSubtract(pLight->vOrigin, pSphere->origin, v);
                    fDot = DotProduct(v, offsets[i]);
                    if (fDot > 0) {
                        fDot /= VectorLengthSquared(v);
                        VectorMA(colorout, fDot, pLight->color, colorout);
                    }
                    break;
                default:
                    assert(!"unhandled light type");
                    break;
                }
            }

            cubecolor[i][0] = Q_min(colorout[0], 255);
            cubecolor[i][1] = Q_min(colorout[1], 255);
            cubecolor[i][2] = Q_min(colorout[2], 255);
        }

        cube = pSphere->cubemap;

        for (i = 0; i < 24; i++) {
            for (j = 0; j < 3; j++) {
                vec4_t f;

                f[0] = cubecolor[iCubeBuildInfo[i][5]][j];
                f[1] = cubecolor[iCubeBuildInfo[i][6]][j];
                f[2] = cubecolor[iCubeBuildInfo[i][7]][j];
                f[3] = cubecolor[iCubeBuildInfo[i][8]][j];

                cube[i][j][0] = f[0];
                cube[i][j][1] = (f[1] - f[0]) * iCubeBuildInfo[i][3];
                cube[i][j][2] = (f[2] - f[0]) * iCubeBuildInfo[i][4];
                cube[i][j][3] = (f[0] - f[1] - f[2] + f[3]) * iCubeBuildInfo[i][3] * iCubeBuildInfo[i][4];
            }
        }

        pSphere->numRealLights -= nCubeMapLights;
    }

    for (i = 0; i < pSphere->numRealLights; i++) {
        float  fDistParallel;
        float  fDistPerp;
        float  fDistSquared;
        float  fRadiusSquared;
        float  fSlope;
        vec3_t v;

        pLight = &pSphere->light[i];

        switch (pLight->eType) {
        case LIGHT_SPOT:
            VectorSubtract(pLight->vOrigin, pSphere->origin, v);
            fDistSquared   = VectorLengthSquared(v);
            fSlope         = DotProduct(v, pLight->vDirection);
            fDistPerp      = sqrt(Q_max(0.0f, fDistSquared - Square(fSlope)));
            fRadiusSquared = Square(pSphere->radius);
            fDistParallel  = fDistPerp + fSlope * (pLight->fSpotSlope * 0.9f);
            if (Square(fDistParallel) >= fRadiusSquared + Square(pLight->fSpotSlope * 0.9f) * fRadiusSquared) {
                pLight->eType = LIGHT_SPOT_FAST;
            }
            break;
        case LIGHT_POINT:
            if (pLight->fDist > 1e-4f
                && (pLight->fDist - pSphere->radius) * r_entlight_errbound->value * pLight->fDist
                    > pSphere->radius * pLight->fIntensity) {
                pLight->eType = LIGHT_DIRECTIONAL;

                VectorScale(pLight->color, 1.0f / pLight->fDist, pLight->color);
                VectorNormalize2(pLight->vOrigin, pLight->vDirection);
            }
            break;
        default:
            break;
        }
    }
}

static void RB_Light_CubeMap_Range(int start, int end, void *ctx)
{
    int              i, j;
    unsigned char   *colors = (unsigned char *)ctx;
    float           *normal = (float *)tess.normal + start * 4;
    float           *xyz    = (float *)tess.xyz + start * 4;
    unsigned char   *color  = colors + start * 4;
    vec3_t           colorout;
    reallightinfo_t *pLight;

    for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
        unsigned int components[3];
        float        ax = Q_fabs(normal[0]);
        float        ay = Q_fabs(normal[1]);
        float        az = Q_fabs(normal[2]);

        if (ay >= ax && ay >= az) {
            components[0] = 1;
            components[1] = 0;
            components[2] = 2;
        } else if (ax >= az) {
            components[0] = 0;
            components[1] = 1;
            components[2] = 2;
        } else {
            components[0] = 2;
            components[1] = 1;
            components[2] = 0;
        }

        unsigned int cubeIndex =
            (normal[2] < 0 ? 1 : 0) | (normal[1] < 0 ? 2 : 0) | (normal[0] < 0 ? 4 : 0) | (components[0] << 3);
        float newnorms[3];
        float mapping[3];
        float invNorm0;
        float(*cube)[3][4];

        newnorms[0] = normal[components[0]];
        newnorms[1] = normal[components[1]];
        newnorms[2] = normal[components[2]];

        invNorm0   = (Q_fabs(newnorms[0]) > 1e-5f) ? (1.f / newnorms[0]) : 0.f;
        mapping[0] = invNorm0 * newnorms[1];
        mapping[1] = invNorm0 * newnorms[2];
        mapping[2] = mapping[0] * mapping[1];

        cube = &backEnd.currentSphere->cubemap[cubeIndex];
        colorout[0] =
            (*cube)[0][0] + (*cube)[0][1] * mapping[0] + (*cube)[0][2] * mapping[1] + (*cube)[0][3] * mapping[2];
        colorout[1] =
            (*cube)[1][0] + (*cube)[1][1] * mapping[0] + (*cube)[1][2] * mapping[1] + (*cube)[1][3] * mapping[2];
        colorout[2] =
            (*cube)[2][0] + (*cube)[2][1] * mapping[0] + (*cube)[2][2] * mapping[1] + (*cube)[2][3] * mapping[2];

        for (j = 0; j < backEnd.currentSphere->numRealLights; j++) {
            float  fDistSquared;
            float  fProjSquared;
            float  fMaxIntensity;
            float  fDot;
            vec3_t v;

            pLight = &backEnd.currentSphere->light[j];

            switch (pLight->eType) {
            case LIGHT_DIRECTIONAL:
                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    VectorMA(colorout, fDot, pLight->color, colorout);
                }
                break;
            case LIGHT_SPOT:
                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    VectorSubtract(pLight->vOrigin, xyz, v);

                    fProjSquared  = DotProduct(v, pLight->vDirection) * DotProduct(v, pLight->vDirection);
                    fDistSquared  = VectorLengthSquared(v);
                    fMaxIntensity = (pLight->fSpotConst - fDistSquared / fProjSquared) * pLight->fSpotScale;
                    if (fMaxIntensity > 0) {
                        fDot /= fDistSquared;
                        if (fMaxIntensity < 1) {
                            fDot *= fMaxIntensity;
                        }

                        VectorMA(colorout, fDot, pLight->color, colorout);
                    }
                }
                break;
            case LIGHT_SPOT_FAST:
                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    VectorSubtract(pLight->vOrigin, xyz, v);
                    fDistSquared = VectorLengthSquared(v);
                    VectorMA(colorout, fDot / fDistSquared, pLight->color, colorout);
                }
                break;
            case LIGHT_POINT:
                VectorSubtract(pLight->vOrigin, xyz, v);
                fDot = DotProduct(v, normal);
                if (fDot > 0) {
                    fDistSquared = VectorLengthSquared(v);
                    VectorMA(colorout, fDot / fDistSquared, pLight->color, colorout);
                }
                break;
            }
        }

        color[0] = Q_min(colorout[0], 255);
        color[1] = Q_min(colorout[1], 255);
        color[2] = Q_min(colorout[2], 255);
        color[3] = 0xff;
    }
}

static void RB_Light_CubeMap(unsigned char *colors)
{
    R_ParallelFor(tess.numVertexes, 128, RB_Light_CubeMap_Range, colors);
}

static void RB_Light_Real_Range(int start, int end, void *ctx)
{
    int              i, j;
    unsigned char   *colors = (unsigned char *)ctx;
    float           *normal = (float *)tess.normal + start * 4;
    float           *xyz    = (float *)tess.xyz + start * 4;
    unsigned char   *color  = colors + start * 4;
    vec3_t           v;
    vec3_t           colorout;
    float            fDot;
    reallightinfo_t *pLight;

    if (backEnd.currentSphere->numRealLights != 1) {
        for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
            colorout[0] = colorout[1] = colorout[2] = 0;

            for (j = 0; j < backEnd.currentSphere->numRealLights; j++) {
                float fDistSquared;
                float fProjSquared;
                float fMaxIntensity;

                pLight = &backEnd.currentSphere->light[j];

                switch (pLight->eType) {
                case LIGHT_DIRECTIONAL:
                    fDot = DotProduct(pLight->vDirection, normal);
                    if (fDot > 0) {
                        VectorMA(colorout, fDot, pLight->color, colorout);
                    }
                    break;
                case LIGHT_SPOT:
                    fDot = DotProduct(pLight->vDirection, normal);
                    if (fDot > 0) {
                        VectorSubtract(pLight->vOrigin, xyz, v);

                        fProjSquared  = Square(DotProduct(v, pLight->vDirection));
                        fDistSquared  = VectorLengthSquared(v);
                        fMaxIntensity = (pLight->fSpotConst - fDistSquared / fProjSquared) * pLight->fSpotScale;
                        if (fMaxIntensity > 0) {
                            fDot /= fDistSquared;
                            if (fMaxIntensity < 1) {
                                fDot *= fMaxIntensity;
                            }

                            VectorMA(colorout, fDot, pLight->color, colorout);
                        }
                    }
                    break;
                case LIGHT_SPOT_FAST:
                    fDot = DotProduct(pLight->vDirection, normal);
                    if (fDot > 0) {
                        VectorSubtract(pLight->vOrigin, xyz, v);
                        fDistSquared = VectorLengthSquared(v);
                        VectorMA(colorout, fDot / fDistSquared, pLight->color, colorout);
                    }
                    break;
                case LIGHT_POINT:
                    VectorSubtract(pLight->vOrigin, xyz, v);
                    fDot = DotProduct(v, normal);
                    if (fDot > 0) {
                        fDistSquared = VectorLengthSquared(v);
                        VectorMA(colorout, fDot / fDistSquared, pLight->color, colorout);
                    }
                    break;
                }
            }

            color[0] = Q_clamp_int((int)colorout[0] + backEnd.currentSphere->ambient.level[0], 0, 255);
            color[1] = Q_clamp_int((int)colorout[1] + backEnd.currentSphere->ambient.level[1], 0, 255);
            color[2] = Q_clamp_int((int)colorout[2] + backEnd.currentSphere->ambient.level[2], 0, 255);
            color[3] = 0xff;
        }
    } else {
        pLight = &backEnd.currentSphere->light[0];

        switch (backEnd.currentSphere->light[0].eType) {
        case LIGHT_DIRECTIONAL:
            for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    VectorScale(pLight->color, fDot, colorout);

                    color[0] = Q_clamp_int((int)colorout[0] + backEnd.currentSphere->ambient.level[0], 0, 255);
                    color[1] = Q_clamp_int((int)colorout[1] + backEnd.currentSphere->ambient.level[1], 0, 255);
                    color[2] = Q_clamp_int((int)colorout[2] + backEnd.currentSphere->ambient.level[2], 0, 255);
                    color[3] = 0xff;
                } else {
                    color[0] = backEnd.currentSphere->ambient.level[0];
                    color[1] = backEnd.currentSphere->ambient.level[1];
                    color[2] = backEnd.currentSphere->ambient.level[2];
                    color[3] = backEnd.currentSphere->ambient.level[3];
                }
            }
            break;
        case LIGHT_SPOT:
            for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
                float fDistSquared;
                float fProjSquared;
                float fMaxIntensity;

                color[0] = backEnd.currentSphere->ambient.level[0];
                color[1] = backEnd.currentSphere->ambient.level[1];
                color[2] = backEnd.currentSphere->ambient.level[2];
                color[3] = backEnd.currentSphere->ambient.level[3];

                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    VectorSubtract(pLight->vOrigin, xyz, v);

                    fProjSquared  = Square(DotProduct(v, pLight->vDirection));
                    fDistSquared  = VectorLengthSquared(v);
                    fMaxIntensity = (pLight->fSpotConst - fDistSquared / fProjSquared) * pLight->fSpotScale;
                    if (fMaxIntensity > 0) {
                        fDot /= fDistSquared;
                        if (fMaxIntensity < 1) {
                            fDot *= fMaxIntensity;
                        }

                        VectorScale(pLight->color, fDot, colorout);

                        color[0] = Q_clamp_int((int)colorout[0] + backEnd.currentSphere->ambient.level[0], 0, 255);
                        color[1] = Q_clamp_int((int)colorout[1] + backEnd.currentSphere->ambient.level[1], 0, 255);
                        color[2] = Q_clamp_int((int)colorout[2] + backEnd.currentSphere->ambient.level[2], 0, 255);
                        color[3] = 0xff;
                    }
                }
            }
            break;
        case LIGHT_SPOT_FAST:
            for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
                color[0] = backEnd.currentSphere->ambient.level[0];
                color[1] = backEnd.currentSphere->ambient.level[1];
                color[2] = backEnd.currentSphere->ambient.level[2];
                color[3] = backEnd.currentSphere->ambient.level[3];

                fDot = DotProduct(pLight->vDirection, normal);
                if (fDot > 0) {
                    float fDistSquared;

                    VectorSubtract(pLight->vOrigin, xyz, v);
                    fDistSquared = VectorLengthSquared(v);
                    VectorScale(pLight->color, fDot / fDistSquared, colorout);

                    color[0] = Q_clamp_int((int)colorout[0] + backEnd.currentSphere->ambient.level[0], 0, 255);
                    color[1] = Q_clamp_int((int)colorout[1] + backEnd.currentSphere->ambient.level[1], 0, 255);
                    color[2] = Q_clamp_int((int)colorout[2] + backEnd.currentSphere->ambient.level[2], 0, 255);
                    color[3] = 0xff;
                }
            }
            break;
        case LIGHT_POINT:
            for (i = start; i < end; i++, xyz += 4, normal += 4, color += 4) {
                VectorSubtract(pLight->vOrigin, xyz, v);
                fDot = DotProduct(v, normal);
                if (fDot > 0) {
                    float fDistSquared;

                    fDistSquared = VectorLengthSquared(v);
                    VectorScale(pLight->color, fDot / fDistSquared, colorout);

                    color[0] = Q_clamp_int((int)colorout[0] + backEnd.currentSphere->ambient.level[0], 0, 255);
                    color[1] = Q_clamp_int((int)colorout[1] + backEnd.currentSphere->ambient.level[1], 0, 255);
                    color[2] = Q_clamp_int((int)colorout[2] + backEnd.currentSphere->ambient.level[2], 0, 255);
                    color[3] = 0xff;
                } else {
                    color[0] = backEnd.currentSphere->ambient.level[0];
                    color[1] = backEnd.currentSphere->ambient.level[1];
                    color[2] = backEnd.currentSphere->ambient.level[2];
                    color[3] = backEnd.currentSphere->ambient.level[3];
                }
            }
            break;
        }
    }
}

void RB_Light_Real(unsigned char *colors)
{
    if (backEnd.currentSphere->bUsesCubeMap) {
        RB_Light_CubeMap(colors);
        return;
    }

    if (!backEnd.currentSphere->numRealLights) {
        unsigned char *color = colors;
        for (int i = 0; i < tess.numVertexes; i++, color += 4) {
            color[0] = backEnd.currentSphere->ambient.level[0];
            color[1] = backEnd.currentSphere->ambient.level[1];
            color[2] = backEnd.currentSphere->ambient.level[2];
            color[3] = backEnd.currentSphere->ambient.level[3];
        }
        return;
    }

    R_ParallelFor(tess.numVertexes, 128, RB_Light_Real_Range, colors);
}

static void RB_Sphere_Light_Sun(trRefEntity_t *ent, sphereor_t *pSphere, qboolean allowDebugLines)
{
    int      curleaf;
    qboolean hitSun;
    vec3_t   end;
    mnode_t *leaf;
    trace_t  trace;

    if (!s_sun.exists) {
        return;
    }

    if (tr.world->vis && pSphere->leaves[0] && tr.sSunLight.leaf == (struct mnode_s *)-1) {
        vec3_t transpos;
        vec3_t temppos;

        for (curleaf = 0; curleaf < 8; curleaf++) {
            leaf = pSphere->leaves[curleaf];
            if (!leaf) {
                break;
            }

            if (leaf->numlights && leaf->lights[0] == &tr.sSunLight) {
                break;
            }
        }

        if (!leaf || (leaf->lights && leaf->lights[0] != &tr.sSunLight)) {
            if (allowDebugLines && r_light_sun_line->integer) {
                VectorMA(pSphere->worldOrigin, 16384.0, s_sun.direction, end);

                VectorCopy(ent->e.origin, temppos);
                MatrixTransformVectorRight(ent->e.axis, temppos, transpos);

                GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE);

                qglPushMatrix();

                qglLoadMatrixf(backEnd.ori.modelMatrix);
                // Clear the depth range
                qglDepthRange(0.0, 0.0);

                qglDisable(GL_TEXTURE_2D);

                qglBegin(GL_LINES);
                qglColor3f(0.0, 0.0, 0.0);
                qglVertex3fv(pSphere->origin);
                qglColor3f(0.5, 0.5, 0.5);
                qglVertex3fv(transpos);
                qglEnd();

                qglEnable(GL_TEXTURE_2D);

                // Bring the depth range back
                qglDepthRange(0.0, 1.0);

                qglPopMatrix();
            }

            return;
        }
    }

    VectorMA(pSphere->traceOrigin, 16384.0, s_sun.direction, end);
    ri.CM_BoxTrace(
        &trace, pSphere->traceOrigin, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID | CONTENTS_FENCE, 0
    );

    hitSun = (trace.surfaceFlags >> 2) & 1;
    if (allowDebugLines && r_light_sun_line->integer) {
        vec3_t transpos;
        vec3_t temppos;

        VectorSubtract(trace.endpos, ent->e.origin, temppos);
        MatrixTransformVectorRight(ent->e.axis, temppos, transpos);

        GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE);

        qglPushMatrix();

        qglLoadMatrixf(backEnd.ori.modelMatrix);
        // Clear the depth range
        qglDepthRange(0.0, 0.0);

        qglDisable(GL_TEXTURE_2D);

        qglBegin(GL_LINES);

        if (hitSun) {
            qglColor3f(1.0, 1.0, 0.0);
        } else {
            qglColor3f(0.0, 0.0, 0.0);
        }

        qglVertex3fv(pSphere->origin);
        qglColor3f(0.5, 0.5, 0.5);
        qglVertex3fv(transpos);
        qglEnd();

        qglEnable(GL_TEXTURE_2D);

        // Bring the depth range back
        qglDepthRange(0.0, 1.0);

        qglPopMatrix();
    }

    if (hitSun) {
        vec3_t           sunnormal;
        reallightinfo_t *pLight;

        MatrixTransformVectorRight(ent->e.axis, s_sun.direction, sunnormal);

        pLight = &pSphere->light[pSphere->numRealLights];
        VectorScale(s_sun.color, r_entlight_scale->value, pLight->color);
        //VectorScale(s_sun.color, r_entlight_scale->value * tr.overbrightMult, pLight->color);

        VectorCopy(sunnormal, pLight->vDirection);
        pLight->eType      = LIGHT_DIRECTIONAL;
        pLight->fIntensity = pLight->color[0] * 0.299f + pLight->color[1] * 0.587f + pLight->color[2] * 0.114f;

        pSphere->numRealLights++;
    }
}

static qboolean RB_Sphere_CalculateSphereOrigin(trRefEntity_t *ent, sphereor_t *pSphere)
{
    vec3_t        *axis;
    vec3_t         sphereOrigin;
    trRefEntity_t *refent;
    trRefEntity_t *newref;

    if (!ent || !ent->e.tiki) {
        return qfalse;
    }
    axis = ent->e.axis;

    pSphere->radius = ent->e.radius;
    VectorCopy(ent->e.lightingOrigin, pSphere->worldOrigin);

    VectorSubtract(pSphere->worldOrigin, ent->e.origin, sphereOrigin);
    MatrixTransformVectorRight(axis, sphereOrigin, pSphere->origin);

    for (refent = ent;; refent = newref) {
        if (refent->e.parentEntity == ENTITYNUM_NONE) {
            break;
        }

        newref = &backEnd.refdef.entities[refent->e.parentEntity];
        if (refent == newref) {
            break;
        }
    }

    VectorCopy(refent->e.lightingOrigin, pSphere->traceOrigin);

    return qtrue;
}

static bool RB_Sphere_SetupGlobals(trRefEntity_t *ent, sphereor_t *pSphere)
{
    int i;

    if ((backEnd.refdef.rdflags & RDF_NOWORLDMODEL) && !(backEnd.refdef.rdflags & RDF_HUD)) {
        pSphere->TessFunction = &RB_Light_Fullbright;
        return false;
    }

    if (backEnd.refdef.rdflags & RDF_HUD) {
        if (!backEnd.refdef.vieworg[0] && !backEnd.refdef.vieworg[1] && !backEnd.refdef.vieworg[2]) {
            pSphere->TessFunction = &RB_Light_Fullbright;
            return false;
        }

        VectorCopy(backEnd.refdef.vieworg, pSphere->worldOrigin);
        VectorClear(pSphere->origin);
        pSphere->radius = 2.0;
    } else if (!RB_Sphere_CalculateSphereOrigin(ent, pSphere)) {
        pSphere->TessFunction = &RB_CalcLightGridColor;

        if (ent && !ent->bLightGridCalculated) {
            RB_SetupEntityGridLighting();
        }

        return false;
    }

    for (i = 0; i < 8; i++) {
        pSphere->leaves[i] = 0;
    }

    R_SphereInLeafs(
        pSphere->worldOrigin, pSphere->radius, pSphere->leaves, 8
    );

    pSphere->TessFunction = &RB_Light_Real;
    return true;
}

static bool RB_Sphere_ResetPointColors(sphereor_t *pSphere)
{
    vec3_t light_offset, amb;

    R_GetLightingGridValue(pSphere->worldOrigin, light_offset);
    light_offset[0] = ambientlight[0] + light_offset[0] * 0.18;
    light_offset[1] = ambientlight[1] + light_offset[1] * 0.18;
    light_offset[2] = ambientlight[2] + light_offset[2] * 0.18;
    if (tr.refdef.rdflags & RDF_FULLBRIGHT) {
        float fMin = tr.identityLight * 20.0;

        if (fMin <= light_offset[0] || fMin <= light_offset[1] || fMin <= light_offset[2]) {
            light_offset[0] += fMin;
            light_offset[1] += fMin;
            light_offset[2] += fMin;
        }
    }

    VectorScale(light_offset, tr.overbrightMult * r_entlight_scale->value, amb);
    pSphere->ambient.level[0] = Q_min(amb[0], 0xff);
    pSphere->ambient.level[1] = Q_min(amb[1], 0xff);
    pSphere->ambient.level[2] = Q_min(amb[2], 0xff);
    pSphere->ambient.level[3] = 0xff;

    return true;
}

static void RB_Sphere_DrawDebugLine(trRefEntity_t *ent, sphereor_t *pSphere, const spherel_t *thislight, float falloff, const vec3_t origin, qboolean allowDebugLines)
{
    int    i;
    vec3_t newColor;
    vec3_t fakeLine, fakeLine2;

    if (!allowDebugLines || !r_light_lines->integer) {
        return;
    }

    qglPushMatrix();

    qglLoadMatrixf(backEnd.ori.modelMatrix);
    // Clear the depth range
    qglDepthRange(0.0, 0.0);

    qglDisable(GL_TEXTURE_2D);

    if (r_light_lines->integer == 2) {
        newColor[0] = thislight->color[0] * (falloff * r_entlight_scale->value / 255.0);
        newColor[1] = thislight->color[1] * (falloff * r_entlight_scale->value / 255.0);
        newColor[2] = thislight->color[2] * (falloff * r_entlight_scale->value / 255.0);

        if (newColor[0] > 1 || newColor[1] > 1 || newColor[2] > 1) {
            NormalizeColor(newColor, newColor);
        }
    } else if (thislight->spot_light) {
        newColor[0] = 1.0;
        newColor[1] = 0.7;
        newColor[2] = 0.7;
    } else {
        newColor[0] = 1.0;
        newColor[1] = 1.0;
        newColor[2] = 1.0;
    }

    qglColor3f(newColor[0], newColor[1], newColor[2]);

    qglBegin(GL_LINES);
    qglVertex3fv(origin);
    qglVertex3fv(pSphere->origin);

    for (i = 0; i < 5; i += 2) {
        VectorMA(origin, 16.0, spheredef[i], fakeLine);
        VectorMA(origin, -16.0, spheredef[i], fakeLine2);

        qglVertex3fv(fakeLine);
        qglVertex3fv(fakeLine2);
    }

    if (thislight->spot_light) {
        qglColor3f(0.7, 1.0, 0.7);

        MatrixTransformVectorRight(ent->e.axis, thislight->spot_dir, fakeLine);
        VectorMA(origin, 32.0, fakeLine, fakeLine);

        qglVertex3fv(origin);
        qglVertex3fv(fakeLine);
    }

    qglEnd();
    qglEnable(GL_TEXTURE_2D);
    // Restore depth
    qglDepthRange(0.0, 1.0);
    qglPopMatrix();
}

static void RB_Sphere_AddSpotLight(trRefEntity_t *ent, sphereor_t *pSphere, const spherel_t *thislight, qboolean allowDebugLines)
{
    vec3_t lightorigin;
    vec3_t newlight;
    vec3_t lightline;
    vec3_t newdir;
    float  falloff;
    float  dot;
    float  radiusByDistance;
    float  sampleRadius;

    radiusByDistance = thislight->spot_radiusbydistance;
    if (radiusByDistance < 0) {
        vec3_t proj;
        vec3_t delta;
        float  distfromcentroid;

        dot =
            -(DotProduct(pSphere->worldOrigin, thislight->spot_dir)
              - DotProduct(thislight->origin, thislight->spot_dir));
        VectorMA(thislight->origin, dot, thislight->spot_dir, proj);
        VectorSubtract(proj, thislight->origin, delta);

        distfromcentroid = VectorLengthSquared(delta);
        if (distfromcentroid > radiusByDistance * radiusByDistance) {
            VectorScale(delta, -radiusByDistance / sqrt(distfromcentroid), delta);
        }

        VectorSubtract(thislight->origin, ent->e.origin, lightorigin);
        VectorAdd(lightorigin, delta, lightorigin);
    } else {
        VectorSubtract(thislight->origin, ent->e.origin, lightorigin);
    }

    MatrixTransformVectorRight(ent->e.axis, lightorigin, lightline);
    VectorSubtract(lightline, pSphere->origin, newlight);

    falloff = thislight->intensity * 7500.0 / Q_max(VectorLengthSquared(newlight), 1.0f);
    if (falloff >= 5.0) {
        reallightinfo_t *pLight;
        float            fMinDist;
        float            fRadByDistSquared;

        MatrixTransformVectorRight(ent->e.axis, thislight->spot_dir, newdir);
        dot = -DotProduct(newlight, newdir);
        if (dot > 0) {
            fMinDist = sqrt(
                Square(newlight[0] + newdir[0] * dot) + Square(newlight[1] + newdir[1] * dot)
                + Square(newlight[2] + newdir[2] * dot)
            );

            if (fMinDist < radiusByDistance * dot && pSphere->numRealLights < MAX_REAL_LIGHTS) {
                pLight = &pSphere->light[pSphere->numRealLights];

                pLight->eType = LIGHT_SPOT;
                VectorScale(thislight->color, thislight->intensity * 7500.0 * tr.overbrightMult, pLight->color);
                pLight->fDist = VectorLength(newlight);
                pLight->fIntensity =
                    (thislight->color[0] * 0.299f + thislight->color[1] * 0.587f + thislight->color[2] * 0.114f)
                    * (thislight->intensity * 7500.0 * tr.overbrightMult);

                sampleRadius = pLight->fDist - pSphere->radius;
                if (sampleRadius > 0) {
                    pLight->fIntensity /= (sampleRadius * sampleRadius);
                }

                VectorCopy(lightline, pLight->vOrigin);
                VectorNegate(newdir, pLight->vDirection);
                fRadByDistSquared  = radiusByDistance * radiusByDistance;
                pLight->fSpotConst = fRadByDistSquared + 1.0;
                pLight->fSpotScale = 1.0 / (fRadByDistSquared * 0.19);
                pLight->fSpotSlope = radiusByDistance;

                pSphere->numRealLights++;
            }

            RB_Sphere_DrawDebugLine(ent, pSphere, thislight, falloff, lightline, allowDebugLines);
        }
    }
}

static void RB_Sphere_AddLight(trRefEntity_t *ent, sphereor_t *pSphere, const spherel_t *thislight, qboolean allowDebugLines)
{
    vec3_t lightorigin;
    vec3_t newlight;
    vec3_t lightline;
    float  intensity;
    float  falloff;

    if (thislight->intensity > 0 && (ent->e.renderfx & RF_INVISIBLE)) {
        return;
    }

    if (thislight->intensity < 0 && !(ent->e.renderfx & RF_INVISIBLE)) {
        return;
    }

    if (thislight->needs_trace) {
        trace_t trace;
        ri.CM_BoxTrace(
            &trace,
            pSphere->traceOrigin,
            thislight->origin,
            vec3_origin,
            vec3_origin,
            0,
            CONTENTS_SOLID,
            0
        );

        if (trace.fraction < 1) {
            return;
        }
    }

    if (!thislight->spot_light) {
        reallightinfo_t *pLight;
        float            fDist;
        float            fRadius;

        VectorSubtract(thislight->origin, ent->e.origin, lightorigin);
        MatrixTransformVectorRight(ent->e.axis, lightorigin, lightline);
        VectorSubtract(lightline, pSphere->origin, newlight);

        intensity = fabs(thislight->intensity) * 7500.0;
        falloff   = 1.f / Q_max(VectorLengthSquared(newlight), 1.0f) * intensity;
        if (falloff >= 5.0) {
            RB_Sphere_DrawDebugLine(ent, pSphere, thislight, falloff, lightline, allowDebugLines);

            if (pSphere->numRealLights < MAX_REAL_LIGHTS) {
                pLight = &pSphere->light[pSphere->numRealLights];

                fDist = VectorLength(newlight);
                VectorScale(
                    thislight->color, falloff * tr.overbrightMult * r_entlight_scale->value * fDist, pLight->color
                );

                VectorCopy(lightline, pLight->vOrigin);
                pLight->fDist      = fDist;
                pLight->eType      = LIGHT_POINT;
                pLight->fIntensity = pLight->color[0] * 0.299f + pLight->color[1] * 0.587f + pLight->color[2] * 0.114f;

                fRadius = fDist - pSphere->radius;
                if (fRadius > 0) {
                    pLight->fIntensity /= fRadius;
                }

                pSphere->numRealLights++;
            }
        }
    } else {
        RB_Sphere_AddSpotLight(ent, pSphere, thislight, allowDebugLines);
    }
}

static void RB_Sphere_BuildStaticLights(trRefEntity_t *ent, sphereor_t *pSphere, qboolean allowDebugLines)
{
    int              i, k;
    int              curleaf;
    mnode_t         *leaf;
    const spherel_t *seenLights[256];
    int              numSeen = 0;

    if (tr.world->vis) {
        int cntarea;

        for (curleaf = 0; curleaf < 8; curleaf++) {
            leaf = pSphere->leaves[curleaf];
            if (!leaf) {
                break;
            }

            cntarea = leaf->numlights;

            if (cntarea) {
                for (i = (leaf->lights[0] == &tr.sSunLight ? 1 : 0); i < cntarea; i++) {
                    spherel_t *pLight = leaf->lights[i];
                    byte       mask   = backEnd.refdef.areamask[pLight->leaf->area >> 3];

                    if (!(mask & (1 << (pLight->leaf->area & 7)))) {
                        qboolean alreadySeen = qfalse;
                        for (k = 0; k < numSeen; k++) {
                            if (seenLights[k] == pLight) {
                                alreadySeen = qtrue;
                                break;
                            }
                        }
                        if (!alreadySeen) {
                            if (numSeen < 256) {
                                seenLights[numSeen++] = pLight;
                            }
                            RB_Sphere_AddLight(ent, pSphere, pLight, allowDebugLines);
                        }
                    }
                }
            }
        }
    } else {
        for (i = 0; i < tr.numSLights; i++) {
            RB_Sphere_AddLight(ent, pSphere, &tr.sLights[i], allowDebugLines);
        }
    }
}

static void RB_Sphere_BuildDLightsFor(trRefEntity_t *ent, sphereor_t *pSphere)
{
    int              i;
    float            length;
    vec3_t           lightorigin;
    vec3_t           delta;
    float            scale;
    float            fRadius;
    reallightinfo_t *pLight;

    pSphere->TessFunction = RB_Light_Real;

    for (i = 0; i < backEnd.refdef.num_dlights && pSphere->numRealLights < MAX_REAL_LIGHTS; i++) {
        VectorSubtract(backEnd.refdef.dlights[i].origin, pSphere->worldOrigin, delta);
        length = VectorLength(delta);

        fRadius = backEnd.refdef.dlights[i].radius + pSphere->radius;
        if (fRadius < length) {
            continue;
        }

        VectorSubtract(backEnd.refdef.dlights[i].origin, ent->e.origin, lightorigin);
        scale  = r_entlight_scale->value * tr.overbrightMult * 7500.0 * backEnd.refdef.dlights[i].radius
               / Q_max(length, 1.0f);
        pLight = &pSphere->light[pSphere->numRealLights];
        pLight->color[0] = scale * backEnd.refdef.dlights[i].color[0];
        pLight->color[1] = scale * backEnd.refdef.dlights[i].color[1];
        pLight->color[2] = scale * backEnd.refdef.dlights[i].color[2];
        MatrixTransformVectorRight(ent->e.axis, lightorigin, pLight->vOrigin);

        pLight->eType      = LIGHT_POINT;
        pLight->fDist      = length;
        pLight->fIntensity = pLight->color[0] * 0.299f + pLight->color[1] * 0.587f + pLight->color[2] * 0.114f;

        if (pLight->fDist - pSphere->radius > 0.f) {
            pLight->fIntensity /= pLight->fDist - pSphere->radius;
        }

        pSphere->numRealLights++;
    }
}

void RB_Sphere_BuildDLights()
{
    RB_Sphere_BuildDLightsFor(backEnd.currentEntity, backEnd.currentSphere);
}

void RB_Sphere_SetupEntityFor(trRefEntity_t *ent, sphereor_t *pSphere)
{
    if (r_light_nolight->integer) {
        pSphere->TessFunction = &RB_Light_Fullbright;
    } else if (RB_Sphere_SetupGlobals(ent, pSphere) && RB_Sphere_ResetPointColors(pSphere)) {
        pSphere->numRealLights = 0;
        RB_Sphere_Light_Sun(ent, pSphere, qfalse);
        RB_Sphere_BuildStaticLights(ent, pSphere, qfalse);
        RB_Sphere_BuildDLightsFor(ent, pSphere);
        RB_OptimizeLightsFor(pSphere);
    }
}

void RB_Sphere_SetupEntity()
{
    if (r_light_nolight->integer) {
        backEnd.currentSphere->TessFunction = &RB_Light_Fullbright;
    } else if (RB_Sphere_SetupGlobals(backEnd.currentEntity, backEnd.currentSphere) && RB_Sphere_ResetPointColors(backEnd.currentSphere)) {
        backEnd.currentSphere->numRealLights = 0;
        RB_Sphere_Light_Sun(backEnd.currentEntity, backEnd.currentSphere, qtrue);
        RB_Sphere_BuildStaticLights(backEnd.currentEntity, backEnd.currentSphere, qtrue);
        RB_Sphere_BuildDLightsFor(backEnd.currentEntity, backEnd.currentSphere);
        backEnd.pc.c_characterlights += backEnd.currentSphere->numRealLights;
        RB_OptimizeLightsFor(backEnd.currentSphere);
    }
}

void RB_Grid_SetupEntity()
{
    trRefEntity_t *refent;
    trRefEntity_t *newref;

    for (refent = backEnd.currentEntity;; refent = newref) {
        if (refent->e.parentEntity == ENTITYNUM_NONE) {
            break;
        }

        newref = &backEnd.refdef.entities[refent->e.parentEntity];
        if (refent == newref) {
            break;
        }
    }

    VectorCopy(refent->e.lightingOrigin, backEnd.currentSphere->traceOrigin);
    RB_SetupEntityGridLighting();
}

void RB_Grid_SetupStaticModel()
{
    RB_SetupStaticModelGridLighting(&tr.refdef, backEnd.currentStaticModel, backEnd.currentStaticModel->origin);
}

void RB_Light_Fullbright(unsigned char *colors)
{
    int i;

    for (i = 0; i < tess.numVertexes; i++) {
        colors[i * 4]     = 0xFF;
        colors[i * 4 + 1] = 0xFF;
        colors[i * 4 + 2] = 0xFF;
        colors[i * 4 + 3] = 0xFF;
    }
}

void R_Sphere_InitLights()
{
    const char *ents;
    const char *ret;
    qboolean    bFlareDirSet;
    qboolean    bIsWorld;
    qboolean    bWorldProcessed;

    ents            = ri.CM_EntityString();
    bFlareDirSet    = qfalse;
    bIsWorld        = qfalse;
    bWorldProcessed = qfalse;

    s_sun.szFlareName[0] = 0;
    s_sun.exists         = qfalse;

    for (int i = 0; i < MAX_SPHERE_LIGHTS; i++) {
        backEnd.spheres[i].TessFunction = &RB_Light_Real;
    }

    VectorClear(ambientlight);
    backEnd.spareSphere.TessFunction = &RB_Light_Real;
    backEnd.currentSphere            = &backEnd.spareSphere;
    bEntityOverbright                = qfalse;
    iEntityLightingMax               = tr.identityLightByte;
    s_sun.color[0] = s_sun.color[1] = s_sun.color[2] = tr.overbrightMult * 70;

    while (ents) {
        ret = COM_Parse((char **)&ents);
        if (*ret == '{' || *ret == '}') {
            continue;
        }

        if (!bWorldProcessed && !strcmp(ret, "classname")) {
            //
            // Added in 2.0
            //  Make sure that the world's properties are not overridden
            //  by some random entities
            //
            if (!strcmp(COM_Parse((char **)&ents), "worldspawn")) {
                bIsWorld = qtrue;
            } else {
                bWorldProcessed = qtrue;
            }

            continue;
        }

        if (!strcmp(ret, "suncolor") || !strcmp(ret, "sunlight")) {
            if (bWorldProcessed) {
                ri.Printf(PRINT_WARNING, "Multiple suncolors defined in map\n");
                continue;
            }

            sscanf(COM_Parse((char **)&ents), "%f %f %f", &s_sun.color[0], &s_sun.color[1], &s_sun.color[2]);
            s_sun.color[0] *= tr.overbrightMult;
            s_sun.color[1] *= tr.overbrightMult;
            s_sun.color[2] *= tr.overbrightMult;
            s_sun.exists = qtrue;
        } else if (!strcmp(ret, "sundirection")) {
            vec3_t dir;

            if (bWorldProcessed) {
                ri.Printf(PRINT_WARNING, "Multiple sundirections defined in map\n");
                continue;
            }

            sscanf(COM_Parse((char **)&ents), "%f %f %f", &dir[0], &dir[1], &dir[2]);
            AngleVectorsLeft(dir, s_sun.direction, 0, 0);

            s_sun.exists = qtrue;
            if (!bFlareDirSet) {
                s_sun.flaredirection[0] = s_sun.direction[0];
                s_sun.flaredirection[1] = s_sun.direction[1];
                s_sun.flaredirection[2] = s_sun.direction[2];
            }
        } else if (!strcmp(ret, "sunflaredirection")) {
            vec3_t dir;

            if (bWorldProcessed) {
                continue;
            }

            sscanf(COM_Parse((char **)&ents), "%f %f %f", &dir[0], &dir[1], &dir[2]);
            AngleVectorsLeft(dir, s_sun.flaredirection, 0, 0);
            bFlareDirSet = qtrue;
        } else if (!strcmp(ret, "sunflarename")) {
            if (bWorldProcessed) {
                continue;
            }

            Q_strncpyz(s_sun.szFlareName, COM_Parse((char **)&ents), sizeof(s_sun.szFlareName));
        } else if (!strcmp(ret, "ambientlight")) {
            if (bWorldProcessed) {
                ri.Printf(PRINT_WARNING, "Multiple ambientlights defined in map\n");
                continue;
            }
            sscanf(COM_Parse((char **)&ents), "%f %f %f", &ambientlight[0], &ambientlight[1], &ambientlight[2]);
        } else if (!strcmp(ret, "overbright")) {
            if (bWorldProcessed) {
                continue;
            }

            ret = COM_Parse((char **)&ents);
            if (strcmp(ret, "world") && strcmp(ret, "none")) {
                bEntityOverbright  = qtrue;
                iEntityLightingMax = 0xFF;
            }
        } else {
            COM_Parse((char **)&ents);
        }
    }

    if (s_sun.exists && !s_sun.szFlareName[0]) {
        Q_strncpyz(s_sun.szFlareName, "sun", sizeof(s_sun.szFlareName));
    }
}

static qboolean R_CheckAddLightToList(spherel_t *pLight, const vec3_t vPos, float *pfFalloff)
{
    vec3_t vDelta;
    float  fDistSq;

    if (pLight->intensity < 0) {
        return qfalse;
    }

    VectorSubtract(pLight->origin, vPos, vDelta);
    fDistSq    = VectorLengthSquared(vDelta);
    *pfFalloff = pLight->intensity * 7500.0f / Q_max(fDistSq, 1.0f);

    return *pfFalloff >= 5.0;
}

static void R_InsertLightIntoList(spherel_t *pLight, float fIntensity, gatheredLight_t **pLightsList)
{
    gatheredLight_t *pCurrLight;
    gatheredLight_t *pLastLight;

    if (pLightsList[0]->pPrev->fIntensity >= fIntensity) {
        return;
    }

    if (fIntensity > pLightsList[0]->fIntensity) {
        pLightsList[0]             = pLightsList[0]->pPrev;
        pLightsList[0]->pLight     = pLight;
        pLightsList[0]->fIntensity = fIntensity;
        return;
    }

    pCurrLight = pLightsList[0]->pNext;
    if (!pCurrLight) {
        return;
    }

    while (pCurrLight->fIntensity >= fIntensity) {
        pCurrLight = pCurrLight->pNext;
        if (!pCurrLight) {
            return;
        }
    }

    pLastLight                   = pLightsList[0]->pPrev->pPrev;
    pCurrLight->pPrev->pNext     = pLightsList[0]->pPrev;
    pLightsList[0]->pPrev->pPrev = pCurrLight->pPrev;
    pLightsList[0]->pPrev->pNext = pCurrLight;
    pCurrLight->pPrev            = pLightsList[0]->pPrev;
    pLastLight->pNext            = pLightsList[0];
    pLightsList[0]->pPrev        = pLastLight;
    pCurrLight                   = pCurrLight->pPrev;
    pCurrLight->fIntensity       = fIntensity;
    pCurrLight->pLight           = pLight;
}

#define MAX_GATHERED_LIGHTS 8

int R_GatherLightSources(const vec3_t vPos, vec3_t *pvLightPos, vec3_t *pvLightIntensity, int iMaxLights)
{
    int              i, j;
    int              iLightCount;
    vec3_t           vEnd;
    vec3_t           vDelta;
    spherel_t       *pCurrLight;
    gatheredLight_t  lights[MAX_GATHERED_LIGHTS];
    gatheredLight_t *pLightsHead;
    gatheredLight_t *pLightsCurr;
    float            fFalloff;
    trace_t          trace;
    mnode_t         *leaf;

    fFalloff = 0;
    leaf     = NULL;

    if (iMaxLights <= 0 || !r_drawspherelights->integer) {
        return 0;
    }

    memset(lights, 0, sizeof(lights));

    for (j = 0; j < MAX_GATHERED_LIGHTS - 2; j++) {
        lights[j + 1].pNext = &lights[j + 2];
        lights[j + 1].pPrev = &lights[j];
    }

    lights[0].pNext                       = &lights[1];
    lights[0].pPrev                       = &lights[MAX_GATHERED_LIGHTS - 1];
    lights[MAX_GATHERED_LIGHTS - 1].pNext = &lights[0];
    lights[MAX_GATHERED_LIGHTS - 1].pPrev = &lights[MAX_GATHERED_LIGHTS - 2];
    pLightsHead                           = &lights[0];

    if (tr.world->vis) {
        leaf = R_PointInLeaf(vPos);
        if (leaf->area == -1 || !leaf->numlights) {
            leaf = NULL;
        }
    }

    if (leaf) {
        if (leaf->numlights) {
            light_reference_count++;

            for (i = (leaf->lights[0] == &tr.sSunLight ? 1 : 0); i < leaf->numlights; i++) {
                pCurrLight = leaf->lights[i];
                if (pCurrLight->leaf != (mnode_t *)-1) {
                    byte mask = backEnd.refdef.areamask[pCurrLight->leaf->area >> 3];

                    if (!(mask & (1 << (pCurrLight->leaf->area & 7)))
                        && pCurrLight->reference_count != light_reference_count) {
                        if (R_CheckAddLightToList(pCurrLight, vPos, &fFalloff)) {
                            R_InsertLightIntoList(pCurrLight, fFalloff, &pLightsHead);
                        }

                        pCurrLight->reference_count = light_reference_count;
                    }
                }
            }
        }
    } else {
        for (i = 0; i < tr.numSLights; i++) {
            pCurrLight = &tr.sLights[i];

            if (R_CheckAddLightToList(pCurrLight, vPos, &fFalloff)) {
                R_InsertLightIntoList(pCurrLight, fFalloff, &pLightsHead);
            }
        }
    }

    iLightCount = 0;

    if (s_sun.exists) {
        if (leaf && leaf->lights[0] == &tr.sSunLight) {
            VectorMA(vPos, 16384.0, s_sun.direction, vEnd);
            ri.CM_BoxTrace(&trace, vPos, vEnd, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);

            if (trace.surfaceFlags & SURF_SKY) {
                VectorMA(vPos, 16384.0, s_sun.direction, pvLightPos[0]);
                pvLightIntensity[0][0] = s_sun.color[0] * (1.0 / 510.0);
                pvLightIntensity[0][1] = s_sun.color[1] * (1.0 / 510.0);
                pvLightIntensity[0][2] = s_sun.color[2] * (1.0 / 510.0);
                iLightCount            = 1;
            }
        }
    }

    pLightsCurr = pLightsHead;

    for (; iLightCount < iMaxLights && iLightCount < MAX_GATHERED_LIGHTS && pLightsCurr->pLight;
         iLightCount++, pLightsCurr = pLightsCurr->pNext) {
        VectorCopy(pLightsCurr->pLight->origin, pvLightPos[iLightCount]);
        vDelta[0] = pLightsCurr->fIntensity / 200.0 * pLightsCurr->pLight->color[0];
        vDelta[1] = pLightsCurr->fIntensity / 200.0 * pLightsCurr->pLight->color[1];
        vDelta[2] = pLightsCurr->fIntensity / 200.0 * pLightsCurr->pLight->color[2];

        if (vDelta[0] > 1.0 || vDelta[1] > 1.0 || vDelta[2] > 1.0) {
            NormalizeColor(vDelta, vDelta);
        }

        VectorCopy(vDelta, pvLightIntensity[iLightCount]);
        pLightsCurr = pLightsCurr->pNext;
        if (pLightsCurr == pLightsHead) {
            break;
        }
    }

    return iLightCount;
}
