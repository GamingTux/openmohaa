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

// DESCRIPTION:
// wall marks

#include "cg_local.h"

/*
===================================================================

MARK POLYS

===================================================================
*/

#define MAX_MARK_FRAGMENTS 128
#define MAX_MARK_POINTS    384

static vec3_t   cg_vEntAngles;
static vec3_t   cg_vEntOrigin;
static vec3_t   cg_fEntAxis[3];
static qboolean cg_bEntAnglesSet;

static int      cg_iLastEntIndex;
static int      cg_iLastEntTime;
static qboolean cg_bLastEntValid;

markPoly_t *cg_freeMarkPolys; // single linked list
markObj_t   cg_activeMarkObjs;
markObj_t  *cg_freeMarkObjs;
treadMark_t cg_treadMarks[MAX_TREAD_MARKS];

markPoly_t *cg_markPolys;
int         cg_iNumMarkPolys;
markObj_t  *cg_markObjs;
int         cg_iNumFreeMarkObjs;
int         cg_iMinFreeMarkObjs;
qboolean    cg_bMarksInitialized;
cvar_t     *cg_treadmark_test;

vec3_t vec_upwards = {0, 0, 1};

typedef struct cg_impactmarkinfo_s {
    vec3_t axis[3];
    vec3_t origin;
    float  fSCenter;
    float  fTCenter;
    float  texCoordScaleS;
    float  texCoordScaleT;
    byte   colors[4];
    int    leafnum;
} cg_impactmarkinfo_t;

typedef struct cg_treadmarkinfo_s {
    treadMark_t *pTread;
    vec3_t       vDirection;
    vec3_t       vRight;
    float        fStartDist;
    float        fStartTex;
    float        fStartAlpha;
    float        fLeftStartDist;
    float        fRightStartDist;
    float        fRightCenterDist;
    float        fLeftTexScale;
    float        fRightTexScale;
    float        fCenterTexScale;
    float        fLeftAlphaScale;
    float        fRightAlphaScale;
    float        fCenterAlphaScale;
    float        fOOWidth;
    float        fOODoubleWidth;
    byte         colors[4];
    int          leafnum;
} cg_treadmarkinfo_t;

int CG_GetMarkFragments(
    int             numVerts,
    const vec3_t   *pVerts,
    const vec3_t    vProjection,
    const vec3_t   *pPointBuffer,
    markFragment_t *pFragmentBuffer,
    float           fRadiusSquared
)
{
    int             i, j;
    int             iNumEnts;
    int             iCurrPoints, iCurrFragments;
    int             iCurrMaxPoints, iCurrMaxFragments;
    int             iNewPoints, iNewFragments;
    clipHandle_t    cmodel;
    vec3_t          vProjectionDir;
    vec3_t          vMins, vMaxs;
    vec3_t          vTemp;
    vec3_t          vAngles, vOrigin;
    const vec3_t   *pCurrPoint;
    centity_t      *pEntList[64];
    entityState_t  *pEnt;
    markFragment_t *pCurrFragment;
    markFragment_t *pFragment;

    iNewFragments = cgi.R_MarkFragments(
        numVerts,
        pVerts,
        vProjection,
        MAX_MARK_POINTS,
        (float *)pPointBuffer,
        MAX_MARK_FRAGMENTS,
        pFragmentBuffer,
        fRadiusSquared
    );

    if (iNewFragments > MAX_MARK_FRAGMENTS) {
        return iNewFragments;
    }

    iNewPoints = 0;
    for (i = 0, pFragment = pFragmentBuffer; i < iNewFragments; i++, pFragment++) {
        iNewPoints += pFragment->numPoints;
    }

    if (iNewPoints >= MAX_MARK_POINTS) {
        return iNewFragments;
    }

    iCurrPoints       = iNewPoints;
    iCurrFragments    = iNewFragments;
    iCurrMaxPoints    = MAX_MARK_POINTS - iNewPoints;
    iCurrMaxFragments = MAX_MARK_FRAGMENTS - iNewFragments;
    pCurrPoint        = &pPointBuffer[iNewPoints];
    pCurrFragment     = &pFragmentBuffer[iNewFragments];

    VectorNormalize2(vProjection, vProjectionDir);
    ClearBounds(vMins, vMaxs);

    for (i = 0; i < numVerts; i++) {
        AddPointToBounds(pVerts[i], vMins, vMaxs);
        VectorAdd(pVerts[i], vProjection, vTemp);
        AddPointToBounds(vTemp, vMins, vMaxs);
        VectorMA(pVerts[i], -20.0f, vProjectionDir, vTemp);
        AddPointToBounds(vTemp, vMins, vMaxs);
    }

    iNumEnts = CG_GetBrushEntitiesInBounds(ARRAY_LEN(pEntList), pEntList, vMins, vMaxs);
    if (!iNumEnts) {
        return iNewFragments;
    }

    for (i = 0; i < iNumEnts; i++) {
        pEnt = &pEntList[i]->currentState;
        VectorCopy(pEntList[i]->lerpAngles, vAngles);
        VectorCopy(pEntList[i]->lerpOrigin, vOrigin);

        cmodel = cgi.CM_InlineModel(pEnt->modelindex);

        iNewFragments = cgi.R_MarkFragmentsForInlineModel(
            cmodel,
            vAngles,
            vOrigin,
            numVerts,
            pVerts,
            vProjection,
            iCurrMaxPoints,
            (float *)pCurrPoint,
            iCurrMaxFragments,
            pCurrFragment,
            fRadiusSquared
        );

        iCurrFragments += iNewFragments;
        if (iCurrFragments >= MAX_MARK_FRAGMENTS) {
            break;
        }

        iNewPoints = 0;
        for (j = 0, pFragment = pCurrFragment; j < iNewFragments; j++, pFragment++) {
            pFragment->firstPoint += iCurrPoints;
            pFragment->iIndex = -pEnt->number;
            iNewPoints += pFragment->numPoints;
        }

        iCurrPoints += iNewPoints;
        if (iCurrPoints >= MAX_MARK_POINTS) {
            break;
        }

        pCurrPoint += iNewPoints;
        iCurrMaxPoints -= iNewPoints;
        iCurrMaxFragments -= iNewFragments;
        pCurrFragment += iNewFragments;
    }

    return iCurrFragments;
}

static qboolean CG_GetMarkInlineModelOrientation(int iIndex)
{
    centity_t *pCEnt;

    if (iIndex == cg_iLastEntIndex && cg_iLastEntTime == cg.time) {
        return cg_bLastEntValid;
    }

    pCEnt = &cg_entities[-iIndex];
    if (pCEnt->currentValid && pCEnt->currentState.modelindex < cgs.numInlineModels) {
        VectorCopy(pCEnt->lerpAngles, cg_vEntAngles);
        VectorCopy(pCEnt->lerpOrigin, cg_vEntOrigin);
        cg_bLastEntValid = qtrue;

        if (cg_vEntAngles[0] || cg_vEntAngles[1] || cg_vEntAngles[2]) {
            AngleVectorsLeft(cg_vEntAngles, cg_fEntAxis[0], cg_fEntAxis[1], cg_fEntAxis[2]);
            cg_bEntAnglesSet = qtrue;
        } else if (cg_bEntAnglesSet) {
            AxisClear(cg_fEntAxis);
            cg_bEntAnglesSet = qfalse;
        }

        cg_iLastEntIndex = iIndex;
        cg_iLastEntTime  = cg.time;
        return cg_bLastEntValid;
    }

    cg_bLastEntValid = qfalse;
    VectorClear(cg_vEntAngles);
    VectorClear(cg_vEntOrigin);

    if (cg_bEntAnglesSet) {
        AxisClear(cg_fEntAxis);
        cg_bEntAnglesSet = qfalse;
    }

    return qfalse;
}

static void CG_FragmentPosToWorldPos(const vec3_t vFrom, vec3_t vTo)
{
    if (cg_bEntAnglesSet) {
        VectorMA(cg_vEntOrigin, vFrom[0], cg_fEntAxis[0], vTo);
        VectorMA(vTo, vFrom[1], cg_fEntAxis[1], vTo);
        VectorMA(vTo, vFrom[2], cg_fEntAxis[2], vTo);
    } else {
        vTo[0] = cg_vEntOrigin[0] + *vFrom;
        vTo[1] = cg_vEntOrigin[1] + vFrom[1];
        vTo[2] = cg_vEntOrigin[2] + vFrom[2];
    }
}

qboolean CG_UpdateMarkPosition(markObj_t *pMark)
{
    vec3_t v;
    vec3_t pt;
    int    iIndex;

    iIndex = pMark->markPolys->iIndex;
    if (!CG_GetMarkInlineModelOrientation(iIndex)) {
        return qfalse;
    }

    VectorCopy(pMark->markPolys->verts[0].xyz, v);

    if (cg_bEntAnglesSet) {
        VectorScale(cg_fEntAxis[0], v[0], pt);
        VectorMA(pt, v[1], cg_fEntAxis[1], pt);
        VectorMA(pt, v[2], cg_fEntAxis[2], pt);
        VectorAdd(pt, cg_vEntOrigin, pt);
    } else {
        VectorAdd(v, cg_vEntOrigin, pt);
    }

    pMark->leafnum = cgi.CM_PointLeafnum(pt);

    return qtrue;
}

void CG_AddFragmentToScene(int iIndex, qhandle_t hShader, int iNumVerts, polyVert_t *pVerts)
{
    if (!iIndex) {
        cgi.R_AddPolyToScene(hShader, iNumVerts, pVerts, 0);
        return;
    }

    if (iIndex > 0) {
        cgi.R_AddTerrainMarkToScene(iIndex, hShader, iNumVerts, pVerts, 0);
        return;
    }

    if (cg_bEntAnglesSet) {
        int         i;
        vec3_t      vTmp;
        polyVert_t *pCurrVert;

        for (i = 0; i < iNumVerts; i++) {
            pCurrVert = &pVerts[i];

            vTmp[0] = cg_fEntAxis[0][0] * pCurrVert->xyz[0];
            vTmp[1] = cg_fEntAxis[0][1] * pCurrVert->xyz[0];
            vTmp[2] = cg_fEntAxis[0][2] * pCurrVert->xyz[0];
            VectorMA(vTmp, pCurrVert->xyz[1], cg_fEntAxis[1], vTmp);
            VectorMA(vTmp, pCurrVert->xyz[2], cg_fEntAxis[2], vTmp);
            VectorAdd(vTmp, cg_vEntOrigin, pCurrVert->xyz);
        }
    } else {
        int         i;
        polyVert_t *pCurrVert;

        for (i = 0; i < iNumVerts; i++) {
            pCurrVert = &pVerts[i];
            VectorAdd(pCurrVert->xyz, cg_vEntOrigin, pCurrVert->xyz);
        }
    }

    cgi.R_AddPolyToScene(hShader, iNumVerts, pVerts, 0);
}

/*
===================
CG_InitMarkPolys

This is called at startup and for tournement restarts
===================
*/
void CG_InitMarks(void)
{
    int i;
    int iMaxMarks;

    iMaxMarks = cg_maxMarks->integer;
    if (iMaxMarks < 32) {
        iMaxMarks = 32;
    }

    cg_iMinFreeMarkObjs = iMaxMarks / 32;
    if (cg_iMinFreeMarkObjs < 8) {
        cg_iMinFreeMarkObjs = 8;
    }
    if (cg_iMinFreeMarkObjs > 32) {
        cg_iMinFreeMarkObjs = 32;
    }

    cg_iNumMarkPolys = 7 * iMaxMarks / 4;

    if (cg_markPolys) {
        cgi.Free(cg_markPolys);
    }

    cg_markPolys = (markPoly_t *)cgi.Malloc(sizeof(markPoly_t) * cg_iNumMarkPolys);

    if (!cg_markPolys) {
        cgi.Error(ERR_DROP, "CG_InitMarks: Could not allocate array for mark polys");
    }

    if (cg_markObjs) {
        cgi.Free(cg_markObjs);
    }

    cg_markObjs = (markObj_t *)cgi.Malloc(sizeof(markObj_t) * iMaxMarks);

    if (!cg_markObjs) {
        cgi.Error(ERR_DROP, "CG_InitMarks: Could not allocate array for mark objects");
    }

    memset(cg_markPolys, 0, sizeof(markPoly_t) * cg_iNumMarkPolys);

    cg_freeMarkPolys = cg_markPolys;
    for (i = 0; i < cg_iNumMarkPolys - 1; i++) {
        cg_markPolys[i].nextPoly = &cg_markPolys[i + 1];
    }
    cg_markPolys[i].nextPoly = NULL;

    cg_activeMarkObjs.nextMark = &cg_activeMarkObjs;
    cg_activeMarkObjs.prevMark = &cg_activeMarkObjs;
    memset(cg_markObjs, 0, sizeof(markObj_t) * iMaxMarks);
    cg_freeMarkObjs = cg_markObjs;

    for (i = 0; i < iMaxMarks - 1; i++) {
        cg_markObjs[i].nextMark = &cg_markObjs[i + 1];
    }
    cg_markObjs[i].nextMark = NULL;

    cg_iNumFreeMarkObjs  = iMaxMarks;
    cg_bMarksInitialized = qtrue;
}

/*
==================
CG_FreeMarkPoly
==================
*/
void CG_FreeMarkPoly(markPoly_t *le)
{
    if (!cg_bMarksInitialized) {
        return;
    }

    le->nextPoly     = cg_freeMarkPolys;
    cg_freeMarkPolys = le;
}

/*
==================
CG_FreeMarkObj
==================
*/
void CG_FreeMarkObj(markObj_t *pMark)
{
    markPoly_t *pPoly;
    markPoly_t *pNextPoly;

    assert(pMark != &cg_activeMarkObjs);

    for (pPoly = pMark->markPolys; pPoly; pPoly = pNextPoly) {
        pNextPoly = pPoly->nextPoly;
        CG_FreeMarkPoly(pPoly);
    }

    pMark->prevMark->nextMark = pMark->nextMark;
    pMark->nextMark->prevMark = pMark->prevMark;
    pMark->nextMark           = cg_freeMarkObjs;
    cg_freeMarkObjs           = pMark;
    cg_iNumFreeMarkObjs++;
}

/*
==================
CG_FreeBestMarkObj
==================
*/
void CG_FreeBestMarkObj(qboolean bAllowFade)
{
    markObj_t *pMark;

    for (pMark = cg_activeMarkObjs.prevMark; pMark != &cg_activeMarkObjs; pMark = pMark->prevMark) {
        if (pMark->lastVisTime < cg.time - 250) {
            CG_FreeMarkObj(pMark);
            return;
        }
    }

    if (!cg_iNumFreeMarkObjs || !bAllowFade) {
        CG_FreeMarkObj(cg_activeMarkObjs.prevMark);
        return;
    }

    for (pMark = cg_activeMarkObjs.prevMark; pMark != &cg_activeMarkObjs; pMark = pMark->prevMark) {
        if (!pMark->alphaFade || pMark->time > cg.time - 9000) {
            break;
        }
    }

    pMark->time = cg.time - 9000;
    pMark->alphaFade = qtrue;
}

/*
===================
CG_AllocMark

Will allways succeed, even if it requires freeing an old active mark
===================
*/
markObj_t *CG_AllocMark(int iNumPolys)
{
    int         iPolyCount;
    markPoly_t *pPoly;
    markObj_t  *pMark;

    if (!cg_bMarksInitialized) {
        return NULL;
    }

    if (iNumPolys < 1) {
        return NULL;
    }

    if (iNumPolys > cg_iNumMarkPolys) {
        // Added in OPM
        //  Make sure to not over allocate polys
        return NULL;
    }

    if (cg_iNumFreeMarkObjs <= cg_iMinFreeMarkObjs) {
        CG_FreeBestMarkObj(1);
    }

    pMark           = cg_freeMarkObjs;
    cg_freeMarkObjs = cg_freeMarkObjs->nextMark;

    memset(pMark, 0, sizeof(markObj_t));
    pMark->lastVisTime = cg.time;

    for (iPolyCount = 0; iPolyCount < iNumPolys; iPolyCount++) {
        while (!cg_freeMarkPolys) {
            CG_FreeBestMarkObj(qfalse);
        }

        pPoly            = cg_freeMarkPolys;
        cg_freeMarkPolys = pPoly->nextPoly;

        memset(pPoly, 0, sizeof(*pPoly));
        pPoly->nextPoly  = pMark->markPolys;
        pMark->markPolys = pPoly;
    }

    // link into the active list
    pMark->nextMark                      = cg_activeMarkObjs.nextMark;
    pMark->prevMark                      = &cg_activeMarkObjs;
    cg_activeMarkObjs.nextMark->prevMark = pMark;
    cg_activeMarkObjs.nextMark           = pMark;

    cg_iNumFreeMarkObjs--;
    assert(cg_iNumFreeMarkObjs >= 0);

    return pMark;
}

int CG_ImpactMark_GetLeafCallback(markFragment_t *mf, void *pCustom)
{
    return ((cg_impactmarkinfo_t *)pCustom)->leafnum;
}

qboolean CG_ImpactMark_PerPolyCallback(const vec3_t *markPoints, markFragment_t *mf, polyVert_t *verts, void *pCustom)
{
    int                  j;
    polyVert_t          *v;
    cg_impactmarkinfo_t *pInfo;

    pInfo = (cg_impactmarkinfo_t *)pCustom;

    if (mf->iIndex >= 0) {
        for (j = 0; j < mf->numPoints; j++) {
            vec3_t delta;

            v = &verts[j];
            VectorCopy(markPoints[j + mf->firstPoint], v->xyz);
            VectorSubtract(v->xyz, pInfo->origin, delta);
            v->st[0]       = pInfo->fSCenter + DotProduct(delta, pInfo->axis[1]) * pInfo->texCoordScaleS;
            v->st[1]       = pInfo->fTCenter + DotProduct(delta, pInfo->axis[2]) * pInfo->texCoordScaleT;
            v->modulate[0] = pInfo->colors[0];
            v->modulate[1] = pInfo->colors[1];
            v->modulate[2] = pInfo->colors[2];
            v->modulate[3] = pInfo->colors[3];
        }
    } else {
        if (!CG_GetMarkInlineModelOrientation(mf->iIndex)) {
            return qfalse;
        }

        for (j = 0; j < mf->numPoints; j++) {
            vec3_t vWorldPos;
            vec3_t delta;

            v = &verts[j];
            VectorCopy(markPoints[j + mf->firstPoint], v->xyz);

            CG_FragmentPosToWorldPos(v->xyz, vWorldPos);
            VectorSubtract(vWorldPos, pInfo->origin, delta);
            v->st[0]       = pInfo->fSCenter + DotProduct(delta, pInfo->axis[1]) * pInfo->texCoordScaleS;
            v->st[1]       = pInfo->fTCenter + DotProduct(delta, pInfo->axis[2]) * pInfo->texCoordScaleT;
            v->modulate[0] = pInfo->colors[0];
            v->modulate[1] = pInfo->colors[1];
            v->modulate[2] = pInfo->colors[2];
            v->modulate[3] = pInfo->colors[3];
        }
    }

    return qtrue;
}

/*
=================
CG_ImpactMark

origin should be a point within a unit of the plane
dir should be the plane normal

temporary marks will not be stored or randomly oriented, but immediately
passed to the renderer.
=================
*/

void CG_ImpactMark(
    qhandle_t    markShader,
    const vec3_t origin,
    const vec3_t dir,
    float        orientation,
    float        fSScale,
    float        fTScale,
    float        red,
    float        green,
    float        blue,
    float        alpha,
    qboolean     alphaFade,
    qboolean     temporary,
    qboolean     dolighting,
    qboolean     fadein,
    float        fSCenter,
    float        fTCenter
)
{
    int                 i;
    int                 numFragments;
    float               fSScale2, fTScale2;
    float               fSScale3, fTScale3;
    vec3_t              originalPoints[4];
    vec3_t              markPoints[MAX_MARK_POINTS];
    vec3_t              projection;
    markFragment_t      markFragments[MAX_MARK_FRAGMENTS], *mf;
    vec3_t              v;
    float               fRadiusSquared;
    cg_impactmarkinfo_t info;

    if (!cg_bMarksInitialized) {
        return;
    }

    if (!cg_addMarks->integer && markShader != cgs.media.shadowMarkShader
        && markShader != cgs.media.footShadowMarkShader) {
        return;
    }

    if (fSScale == 0.0) {
        fSScale = 1.0;
    }
    if (fTScale == 0.0) {
        fTScale = 1.0;
    }

    fRadiusSquared      = fSScale * fSScale + fTScale * fTScale;
    info.texCoordScaleS = 0.5 / fSScale;
    info.texCoordScaleT = 0.5 / fTScale;

    if (fSCenter < 0.0f || fSCenter > 1.0f) {
        fSCenter = 0.5f;
    }

    if (fTCenter < 0.0f || fTCenter > 1.0f) {
        fTCenter = 0.5f;
    }

    fSScale2 = (fSCenter + fSCenter) * fSScale;
    fTScale2 = (fTCenter + fTCenter) * fTScale;
    fSScale3 = (1.0 - fSCenter) * fSScale + (1.0 - fSCenter) * fSScale;
    fTScale3 = (1.0 - fTCenter) * fTScale + (1.0 - fTCenter) * fTScale;

    // create the texture axis

    if (orientation) {
        VectorNormalize2(dir, info.axis[0]);
        PerpendicularVector(info.axis[1], info.axis[0]);
        RotatePointAroundVector(info.axis[2], info.axis[0], info.axis[1], orientation);
        CrossProduct(info.axis[0], info.axis[2], info.axis[1]);
    } else {
        vec3_t angles;
        vec3_t tmp;

        VectorNormalize2(dir, info.axis[0]);
        VectorCopy(dir, tmp);
        vectoangles(tmp, angles);
        AnglesToAxis(angles, info.axis);
        VectorScale(info.axis[2], -1, info.axis[2]);
    }

    // create the full polygon
    for (i = 0; i < 3; i++) {
        originalPoints[0][i] = origin[i] - fSScale2 * info.axis[1][i] - fTScale2 * info.axis[2][i];
        originalPoints[1][i] = origin[i] + fSScale3 * info.axis[1][i] - fTScale2 * info.axis[2][i];
        originalPoints[2][i] = origin[i] + fSScale3 * info.axis[1][i] + fTScale3 * info.axis[2][i];
        originalPoints[3][i] = origin[i] - fSScale2 * info.axis[1][i] + fTScale3 * info.axis[2][i];
    }

    // get the fragments
    VectorScale(dir, -32, projection);

    numFragments = CG_GetMarkFragments(4, originalPoints, projection, markPoints, markFragments, fRadiusSquared);

    if (dolighting) {
        vec3_t vLight;
        cgi.R_GetLightingForDecal(vLight, dir, origin);

        info.colors[0] = (int)(red * vLight[0]);
        info.colors[1] = (int)(green * vLight[1]);
        info.colors[2] = (int)(blue * vLight[2]);
    } else {
        info.colors[0] = (int)(red * 255.0f);
        info.colors[1] = (int)(green * 255.0f);
        info.colors[2] = (int)(blue * 255.0f);
    }

    if (fadein) {
        info.colors[3] = 0;
    } else {
        info.colors[3] = (int)(alpha * 255.0);
    }

    info.fSCenter = fSCenter;
    info.fTCenter = fTCenter;
    VectorCopy(origin, info.origin);
    VectorAdd(origin, dir, v);
    info.leafnum = cgi.CM_PointLeafnum(v);

    if (temporary) {
        polyVert_t verts[8];

        for (i = 0, mf = markFragments; i < numFragments; i++, mf++) {
            if (mf->numPoints > 8) {
                mf->numPoints = 8;
            }

            if (CG_ImpactMark_PerPolyCallback(markPoints, mf, verts, (void *)&info)) {
                CG_AddFragmentToScene(mf->iIndex, markShader, mf->numPoints, verts);
            }
        }
    } else {
        CG_AssembleFinalMarks(
            markPoints,
            markFragments,
            numFragments,
            &CG_ImpactMark_PerPolyCallback,
            &CG_ImpactMark_GetLeafCallback,
            (void *)&info,
            info.origin,
            sqrt(fRadiusSquared),
            markShader,
            fadein,
            alphaFade
        );
    }
}

void CG_AssembleFinalMarks(
    vec3_t         *markPoints,
    markFragment_t *markFragments,
    int             numFragments,
    qboolean (*PerPolyCallback)(const vec3_t *markPoints, markFragment_t *mf, polyVert_t *verts, void *pCustom),
    int (*GetLeafCallback)(markFragment_t *mf, void *pCustom),
    void     *pCustom,
    vec3_t    pos,
    float     radius,
    qhandle_t markShader,
    qboolean  fadein,
    qboolean  alphaFade
)
{
    markObj_t      *pMark;
    markPoly_t     *pPoly;
    int             iGroup, iFirstNewGroup;
    int             numFragsForMark;
    int             i;
    markFragment_t *mf;

    iFirstNewGroup = 0;
    do {
        i               = iFirstNewGroup;
        mf              = &markFragments[iFirstNewGroup];
        iGroup          = mf->iIndex;
        numFragsForMark = 0;

        for (; i < numFragments; i++, mf++) {
            if (iGroup < 0 && mf->iIndex == iGroup) {
                numFragsForMark++;
            } else if (iGroup >= 0 && mf->iIndex >= 0) {
                numFragsForMark++;
            }
        }

        pMark = CG_AllocMark(numFragsForMark);
        if (!pMark) {
            break;
        }

        pMark->time       = cg.time;
        pMark->alphaFade  = alphaFade;
        pMark->markShader = markShader;
        pMark->fadein     = fadein;
        VectorCopy(pos, pMark->pos);
        pMark->radius = radius;

        i              = iFirstNewGroup;
        mf             = &markFragments[iFirstNewGroup];
        iFirstNewGroup = 0;

        if (mf->iIndex < 0) {
            centity_t *cent;
            vec3_t pos;

            cent = &cg_entities[-mf->iIndex];

            VectorSubtract(pMark->pos, cent->lerpOrigin, pos);

            if (cent->lerpAngles[0] || cent->lerpAngles[1] || cent->lerpAngles[2]) {
                vec3_t axis[3];

                // Fixed in OPM
                //  Make the position completely local to the entity
                AngleVectorsLeft(cent->lerpAngles, axis[0], axis[1], axis[2]);
                MatrixTransformVectorRight(axis, pos, pMark->pos);
            } else {
                VectorCopy(pos, pMark->pos);
            }
        }

        pPoly = pMark->markPolys;
        for (; i < numFragments; i++, mf++) {
            if (iGroup < 0 && mf->iIndex != iGroup) {
                if (!iFirstNewGroup) {
                    iFirstNewGroup = i;
                }
            } else if (iGroup >= 0 && mf->iIndex < 0) {
                if (!iFirstNewGroup) {
                    iFirstNewGroup = i;
                }
            } else {
                if (mf->numPoints > 8) {
                    mf->numPoints = 8;
                }

                if (PerPolyCallback(markPoints, mf, pPoly->verts, pCustom)) {
                    pPoly->numVerts = mf->numPoints;
                    pPoly->iIndex   = mf->iIndex;
                    pPoly           = pPoly->nextPoly;
                }
            }
        }

        pMark->leafnum = GetLeafCallback(mf, pCustom);
    } while (iFirstNewGroup);
}

void CG_ImpactMarkSimple(
    qhandle_t    markShader,
    const vec3_t origin,
    const vec3_t dir,
    float        orientation,
    float        fRadius,
    float        red,
    float        green,
    float        blue,
    float        alpha,
    qboolean     alphaFade,
    qboolean     temporary,
    qboolean     dolighting,
    qboolean     fadein
)
{
    if (cg_bMarksInitialized) {
        CG_ImpactMark(
            markShader,
            origin,
            dir,
            orientation,
            fRadius,
            fRadius,
            red,
            green,
            blue,
            alpha,
            alphaFade,
            temporary,
            dolighting,
            fadein,
            0.5f,
            0.5f
        );
    }
}

/*
===============
CG_AddMarks
===============
*/
#define MARK_TOTAL_TIME 10000
#define MARK_FADE_TIME  1000

void CG_AddMarks(void)
{
    int         j;
    int         t;
    int         fade;
    markObj_t  *pMark;
    markObj_t  *pNext;
    markPoly_t *pPoly;
    polyVert_t *pVert;
    polyVert_t  tmpVerts[8];
    int         viewleafnum;

    int         markTotalTime;
    int         markFadeOutTime;

    if (!cg_bMarksInitialized) {
        return;
    }

    if (!cg_addMarks->integer) {
        return;
    }

    if (cg_marks_lifetime && cg_marks_lifetime->value > 0.0f) {
        float lifeSec = (cg_marks_lifetime->value < 5.0f)
                            ? 45.0f
                            : Com_Clamp(5.0f, 300.0f, cg_marks_lifetime->value);
        markTotalTime   = (int)(lifeSec * 1000.0f);
        markFadeOutTime = (markTotalTime > 6000) ? 3000 : MARK_FADE_TIME;
    } else {
        markTotalTime   = MARK_TOTAL_TIME;
        markFadeOutTime = MARK_FADE_TIME;
    }

    viewleafnum = cgi.CM_PointLeafnum(cg.refdef.vieworg);
    for (pMark = cg_activeMarkObjs.nextMark; pMark != &cg_activeMarkObjs; pMark = pNext) {
        // grab next now, so if the local entity is freed we
        // still have it
        pNext = pMark->nextMark;

        // see if it is time to completely remove it
        if (pMark->alphaFade && cg.time > pMark->time + markTotalTime) {
            CG_FreeMarkObj(pMark);
            continue;
        }

        if (pMark->markPolys->iIndex < 0 && !CG_UpdateMarkPosition(pMark)) {
            if (pMark->lastVisTime < cg.time - 3000) {
                CG_FreeMarkObj(pMark);
            }
            continue;
        }

        if (!cgi.CM_LeafInPVS(viewleafnum, pMark->leafnum)) {
            continue;
        }

        if (pMark->markPolys->iIndex < 0) {
            vec3_t vWorldPos;
            CG_FragmentPosToWorldPos(pMark->pos, vWorldPos);
            if (CG_FrustumCullSphere(vWorldPos, pMark->radius)) {
                continue;
            }
        } else {
            if (CG_FrustumCullSphere(pMark->pos, pMark->radius)) {
                continue;
            }
        }

        pMark->lastVisTime = cg.time;
        if (pMark->fadein) {
            fade = 255 * (cg.time - pMark->time) / MARK_FADE_TIME;
            if (fade > 255) {
                fade          = 255;
                pMark->fadein = 0;
            }

            for (pPoly = pMark->markPolys; pPoly; pPoly = pPoly->nextPoly) {
                for (j = 0; j < pPoly->numVerts; j++) {
                    pPoly->verts[j].modulate[3] = fade;
                }
            }
        }

        if (pMark->alphaFade) {
            t = pMark->time + markTotalTime - cg.time;
            if (t < markFadeOutTime) {
                fade = 255 * t / markFadeOutTime;
                if (fade < 0) {
                    fade = 0;
                }
                for (pPoly = pMark->markPolys; pPoly; pPoly = pPoly->nextPoly) {
                    for (j = 0; j < pPoly->numVerts; j++) {
                        pPoly->verts[j].modulate[3] = fade;
                    }
                }
            }
        }

        for (pPoly = pMark->markPolys; pPoly; pPoly = pPoly->nextPoly) {
            if (pPoly->iIndex < 0) {
                memcpy(tmpVerts, pPoly->verts, 24 * pPoly->numVerts);
                pVert = tmpVerts;
            } else {
                pVert = pPoly->verts;
            }

            CG_AddFragmentToScene(pPoly->iIndex, pMark->markShader, pPoly->numVerts, pVert);
        }
    }

    CG_AddTreadMarks();
}

qboolean CG_CheckMakeMarkOnEntity(int iEntIndex)
{
    if (iEntIndex == ENTITYNUM_WORLD) {
        return qtrue;
    }

    if (iEntIndex == ENTITYNUM_NONE) {
        return qfalse;
    }

    if (cg_entities[iEntIndex].currentState.solid != SOLID_BMODEL) {
        return qfalse;
    }

    if (cg_entities[iEntIndex].currentState.modelindex < 0
        || cg_entities[iEntIndex].currentState.modelindex > cgi.CM_NumInlineModels()) {
        return qfalse;
    }

    return qtrue;
}

void CG_InitTestTreadMark()
{
    cg_treadmark_test = cgi.Cvar_Get("cg_treadmark_test", "0", 0);
}

int CG_StartTreadMark(int iReference, qhandle_t treadShader, const vec3_t vStartPos, float fWidth, float fAlpha)
{
    int          i;
    int          iTreadNum;
    treadMark_t *pTread;

    if (!cg_bMarksInitialized) {
        return -1;
    }

    if (!cg_addMarks->integer) {
        return -1;
    }

    iTreadNum = -1;
    for (i = 0; i < MAX_TREAD_MARKS; i++) {
        if (!cg_treadMarks[i].iState) {
            iTreadNum = i;
            pTread    = &cg_treadMarks[i];
            break;
        }
    }

    if (iTreadNum == -1) {
        return -1;
    }

    memset(pTread, 0, sizeof(*pTread));
    pTread->iState           = 1;
    pTread->iReferenceNumber = iReference;
    pTread->iLastTime        = cg.time;
    pTread->hTreadShader     = treadShader;
    pTread->fWidth           = fWidth * 0.5;
    VectorCopy(vStartPos, pTread->vMidPos);
    VectorCopy(vStartPos, pTread->vEndPos);

    if (fAlpha < 0.0) {
        pTread->fMidAlpha = 255.0;
    } else {
        pTread->fMidAlpha = fAlpha * 255.0;
    }

    pTread->fEndAlpha = pTread->fMidAlpha;
    return iTreadNum;
}

qboolean
CG_MakeTreadMarkDecal_PerPolyCallback(const vec3_t *markPoints, markFragment_t *mf, polyVert_t *verts, void *pCustom)
{
    int                 j;
    float               fDist;
    float               fSideDist;
    float               fSideAlpha;
    float               fCenterAlpha;
    float               fFrac;
    float               fTex, fAlpha;
    polyVert_t         *v;
    cg_treadmarkinfo_t *pInfo;

    pInfo = (cg_treadmarkinfo_t *)pCustom;

    if (mf->iIndex < 0) {
        vec3_t vWorldPos;

        if (!CG_GetMarkInlineModelOrientation(mf->iIndex)) {
            return qfalse;
        }

        for (j = 0; j < mf->numPoints; j++) {
            v = &verts[j];
            VectorCopy(markPoints[mf->firstPoint + j], v->xyz);
            v->modulate[0] = pInfo->colors[0];
            v->modulate[1] = pInfo->colors[1];
            v->modulate[2] = pInfo->colors[2];
            v->modulate[3] = pInfo->colors[3];

            CG_FragmentPosToWorldPos(v->xyz, vWorldPos);
            fSideDist = DotProduct(pInfo->vRight, vWorldPos) - pInfo->fRightCenterDist;
            v->st[0]  = (fSideDist + pInfo->pTread->fWidth) * pInfo->fOODoubleWidth;

            fSideAlpha = fSideDist * pInfo->fOOWidth;
            fDist      = DotProduct(vWorldPos, pInfo->vDirection);
            if (fSideAlpha < 0) {
                fSideAlpha = -fSideAlpha;
                v->st[1]   = pInfo->fStartTex + ((fDist - pInfo->fLeftStartDist) * pInfo->fLeftTexScale * fSideAlpha)
                         + ((fDist - pInfo->fStartDist) * pInfo->fCenterTexScale * (1.0 - fSideAlpha));
                v->modulate[3] = pInfo->fStartAlpha
                               + (1.0 - fSideAlpha)
                                     * ((fDist - pInfo->fStartDist) * pInfo->fCenterTexScale * pInfo->fCenterAlphaScale)
                               + (fDist - pInfo->fLeftStartDist) * pInfo->fLeftAlphaScale * fSideAlpha;
            } else {
                v->st[1] = pInfo->fStartTex + ((fDist - pInfo->fRightStartDist) * pInfo->fRightTexScale * fSideAlpha)
                         + ((fDist - pInfo->fStartDist) * pInfo->fCenterTexScale * (1.0 - fSideAlpha));
                v->modulate[3] = pInfo->fStartAlpha
                               + (1.0 - fSideAlpha)
                                     * ((fDist - pInfo->fStartDist) * pInfo->fCenterTexScale * pInfo->fCenterAlphaScale)
                               + (fDist - pInfo->fRightStartDist) * pInfo->fRightAlphaScale * fSideAlpha;
            }
        }
    } else {
        for (j = 0; j < mf->numPoints; j++) {
            v = &verts[j];
            VectorCopy(markPoints[mf->firstPoint + j], v->xyz);
            v->modulate[0] = pInfo->colors[0];
            v->modulate[1] = pInfo->colors[1];
            v->modulate[2] = pInfo->colors[2];
            v->modulate[3] = pInfo->colors[3];

            fSideDist = DotProduct(pInfo->vRight, v->xyz) - pInfo->fRightCenterDist;
            v->st[0]  = (fSideDist + pInfo->pTread->fWidth) * pInfo->fOODoubleWidth;

            fSideAlpha = fSideDist * pInfo->fOOWidth;
            fDist      = DotProduct(v->xyz, pInfo->vDirection);
            if (fSideAlpha < 0) {
                fSideAlpha = -fSideAlpha;
                fTex       = (fDist - pInfo->fLeftStartDist) * pInfo->fLeftTexScale;
                fAlpha     = (fDist - pInfo->fLeftStartDist) * pInfo->fLeftAlphaScale;
            } else {
                fTex   = (fDist - pInfo->fRightStartDist) * pInfo->fRightTexScale;
                fAlpha = (fDist - pInfo->fRightStartDist) * pInfo->fRightAlphaScale;
            }

            v->st[1] = pInfo->fStartTex + (fTex * fSideAlpha)
                     + (fDist - pInfo->fStartDist) * pInfo->fCenterTexScale * (1.0 - fSideAlpha);
            v->modulate[3] = pInfo->fStartAlpha
                           + ((1.0 - fSideAlpha) * ((fDist - pInfo->fStartDist) * pInfo->fCenterAlphaScale))
                           + (fAlpha * fSideAlpha);
        }
    }

    return qtrue;
}

int CG_MakeTreadMarkDecal_GetLeafCallback(markFragment_t *mf, void *pCustom)
{
    return ((cg_treadmarkinfo_t *)pCustom)->leafnum;
}

void CG_MakeTreadMarkDecal(treadMark_t *pTread, qboolean bStartSegment, qboolean bTemporary)
{
    int                i;
    int                numFragments;
    vec3_t             originalPoints[4];
    vec3_t             markPoints[MAX_MARK_POLYVERTS];
    vec3_t             projection;
    markFragment_t     markFragments[MAX_MARK_FRAGMENTS];
    markFragment_t    *mf;
    cg_treadmarkinfo_t info;
    float              fEndAlpha, fEndTex;
    float              fDist;
    vec3_t             vStartCenter;
    vec3_t             vEndCenter;
    vec3_t             vDelta;
    vec3_t             origin;
    float              fRadiusSquared;

    if (bStartSegment) {
        VectorCopy(pTread->vStartVerts[1], originalPoints[0]);
        VectorCopy(pTread->vStartVerts[0], originalPoints[1]);
        VectorCopy(pTread->vMidVerts[0], originalPoints[2]);
        VectorCopy(pTread->vMidVerts[1], originalPoints[3]);

        info.fStartAlpha = pTread->fStartAlpha;
        fEndAlpha        = pTread->fMidAlpha;
        info.fStartTex   = pTread->fStartTexCoord;
        fEndTex          = pTread->fMidTexCoord;

        // Calculate the start center
        VectorAdd(originalPoints[1], originalPoints[0], vStartCenter);
        VectorScale(vStartCenter, 0.5, vStartCenter);
        VectorCopy(pTread->vMidPos, vEndCenter);
        VectorCopy(pTread->vStartDir, info.vDirection);
    } else {
        VectorCopy(pTread->vMidVerts[1], originalPoints[0]);
        VectorCopy(pTread->vMidVerts[0], originalPoints[1]);
        VectorCopy(pTread->vEndVerts[0], originalPoints[2]);
        VectorCopy(pTread->vEndVerts[1], originalPoints[3]);

        info.fStartAlpha = pTread->fMidAlpha;
        fEndAlpha        = pTread->fEndAlpha;
        info.fStartTex   = pTread->fMidTexCoord;
        fEndTex          = pTread->fEndTexCoord;

        VectorCopy(pTread->vMidPos, vStartCenter);
        VectorCopy(pTread->vEndPos, vEndCenter);

        // Calculate the direction
        VectorSubtract(vEndCenter, vStartCenter, info.vDirection);
        VectorNormalizeFast(info.vDirection);
    }

    CrossProduct(vec_upwards, info.vDirection, info.vRight);
    info.fRightCenterDist = DotProduct(vEndCenter, info.vRight);
    info.fOODoubleWidth   = 0.5 / pTread->fWidth;
    info.fOOWidth         = info.fOODoubleWidth * 2;
    info.fStartDist       = DotProduct(info.vDirection, vStartCenter);
    info.fRightStartDist  = DotProduct(info.vDirection, originalPoints[1]);
    info.fLeftStartDist   = DotProduct(info.vDirection, originalPoints[0]);

    //
    // Center
    //

    VectorSubtract(vEndCenter, vStartCenter, vDelta);
    fDist = VectorLength(vDelta);

    fRadiusSquared = (Square(fDist) + Square(pTread->fWidth)) * 0.25;

    info.fCenterTexScale   = (fEndTex - info.fStartTex) / fDist;
    info.fCenterAlphaScale = (fEndAlpha - info.fStartAlpha) / fDist;

    //
    // Right
    //

    VectorSubtract(originalPoints[2], originalPoints[1], vDelta);
    fDist = VectorLength(vDelta);

    info.fRightTexScale   = (fEndTex - info.fStartTex) / fDist;
    info.fRightAlphaScale = (fEndAlpha - info.fStartAlpha) / fDist;

    //
    // Left
    //

    VectorSubtract(originalPoints[3], originalPoints[1], vDelta);
    fDist = VectorLength(vDelta);

    info.fLeftTexScale   = (fEndTex - info.fStartTex) / fDist;
    info.fLeftAlphaScale = (fEndAlpha - info.fStartAlpha) / fDist;

    VectorSet(projection, 0, 0, -32);
    info.colors[0] = info.colors[1] = info.colors[2] = info.colors[3] = -1;

    numFragments = CG_GetMarkFragments(4, originalPoints, projection, markPoints, markFragments, fRadiusSquared);
    VectorAdd(vStartCenter, vEndCenter, origin);
    VectorScale(origin, 0.5, origin);
    info.leafnum = cgi.CM_PointLeafnum(origin);
    info.pTread  = pTread;

    if (bTemporary) {
        for (i = 0; i < numFragments; i++) {
            polyVert_t verts[8];

            mf = &markFragments[i];
            if (mf->numPoints > 8) {
                mf->numPoints = 8;
            }
            if (CG_MakeTreadMarkDecal_PerPolyCallback(markPoints, mf, verts, &info)) {
                CG_AddFragmentToScene(mf->iIndex, pTread->hTreadShader, mf->numPoints, verts);
            }
        }
    } else {
        CG_AssembleFinalMarks(
            markPoints,
            markFragments,
            numFragments,
            &CG_MakeTreadMarkDecal_PerPolyCallback,
            &CG_MakeTreadMarkDecal_GetLeafCallback,
            &info,
            origin,
            sqrt(fRadiusSquared),
            pTread->hTreadShader,
            qfalse,
            qfalse
        );
    }
}

int CG_UpdateTreadMark(int iReference, const vec3_t vNewPos, float fAlpha)
{
    int          i;
    int          iTreadNum;
    float        fDist;
    float        fSplitLength;
    float        fNewLength;
    float        fTmp;
    qboolean     bDoSegmentation;
    vec3_t       vDelta, vDeltaNorm;
    vec3_t       vDir, vMidDir;
    vec3_t       vRight;
    treadMark_t *pTread;

    iTreadNum = -1;

    for (i = 0; i < MAX_TREAD_MARKS; i++) {
        if (cg_treadMarks[i].iReferenceNumber == iReference) {
            iTreadNum = i;
            pTread    = &cg_treadMarks[i];
        }
    }

    if (iTreadNum == -1 || !pTread->iState) {
        return -1;
    }

    pTread->iLastTime = cg.time;

    if (VectorCompare(pTread->vEndPos, vNewPos)) {
        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }
        return 0;
    }

    VectorSubtract(vNewPos, pTread->vMidPos, vDelta);
    fDist = VectorNormalize2(vDelta, vDeltaNorm);
    if (pTread->iState == 1) {
        fSplitLength    = 0;
        bDoSegmentation = qfalse;
    } else {
        VectorAdd(vDeltaNorm, pTread->vStartDir, vMidDir);
        VectorScale(vMidDir, 0.5, vMidDir);
        VectorNormalizeFast(vMidDir);

        fTmp         = DotProduct(vMidDir, vDeltaNorm);
        fSplitLength = pTread->fWidth / fTmp;

        if (fTmp < -0.5) {
            fTmp = fTmp
                 * (fTmp
                        * (fTmp
                               * (fTmp * (fTmp * (fTmp * -337.31875783205 + -1237.54375255107) + -1802.11467325687)
                                  + -1303.19904613494)
                           + -471.347871690988)
                    + -70.0883838161826);
        } else if (fTmp > 0.5) {
            fTmp = fTmp
                     * (fTmp
                            * (fTmp
                                   * (fTmp * (fTmp * (fTmp * -1507.55394345521 + 6580.58002318442) + -11860.0735285953)
                                      + 11290.7510782536)
                               + -5986.89654545347)
                        + 1675.66417006387)
                 + -192.426950291139;
        } else {
            fTmp = fTmp * (fTmp * -0.531387674508458 + -2.11e-14) + 1.00086138065435;
        }

        fNewLength = fSplitLength * fTmp;
        if (fNewLength + 24 > fDist) {
            bDoSegmentation = qfalse;
        } else if (fSplitLength < -1) {
            fSplitLength    = -fSplitLength;
            bDoSegmentation = qtrue;
        } else if (fDist > 256) {
            bDoSegmentation = qtrue;
        } else {
            CrossProduct(vec_upwards, pTread->vStartDir, vRight);
            VectorNormalizeFast(vRight);
            bDoSegmentation = fabs(DotProduct(vRight, vDelta)) > 16;
        }
    }

    if (pTread->iState == 2 && bDoSegmentation) {
        VectorCopy(pTread->vMidVerts[0], pTread->vStartVerts[0]);
        VectorCopy(pTread->vMidVerts[1], pTread->vStartVerts[1]);
        pTread->fMidTexCoord = pTread->fEndTexCoord;
        pTread->fMidAlpha    = pTread->fEndAlpha;
        VectorCopy(vNewPos, pTread->vEndPos);

        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }

        pTread->iState = 3;
    } else if (pTread->iState == 0 || pTread->iState == 1) {
        VectorCopy(vNewPos, pTread->vEndPos);

        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }

        CrossProduct(vec_upwards, vDeltaNorm, vDir);
        VectorMA(pTread->vMidPos, pTread->fWidth, vDir, pTread->vMidVerts[0]);
        VectorMA(pTread->vMidPos, 0.0 - pTread->fWidth, vDir, pTread->vMidVerts[1]);
        VectorMA(pTread->vEndPos, pTread->fWidth, vDir, pTread->vEndVerts[0]);
        VectorMA(pTread->vEndPos, 0.0 - pTread->fWidth, vDir, pTread->vEndVerts[1]);

        pTread->fEndTexCoord = fDist / 32.0;

        if (pTread->iState == 1 && fDist > 8) {
            VectorCopy(vDelta, pTread->vStartDir);
            pTread->iState = 2;
        }
        return 0;
    } else {
        if (bDoSegmentation) {
            CG_MakeTreadMarkDecal(pTread, qtrue, qfalse);

            VectorCopy(pTread->vMidVerts[0], pTread->vStartVerts[0]);
            VectorCopy(pTread->vMidVerts[1], pTread->vStartVerts[1]);
            VectorSubtract(pTread->vEndPos, pTread->vMidPos, pTread->vStartDir);
            VectorNormalizeFast(pTread->vStartDir);
            VectorCopy(pTread->vEndPos, pTread->vMidPos);
            pTread->fMidTexCoord = pTread->fEndTexCoord;
            pTread->fMidAlpha    = pTread->fEndAlpha;

            if (pTread->fStartTexCoord >= 1.0) {
                pTread->fMidTexCoord -= (int)pTread->fStartTexCoord;
                pTread->fStartTexCoord -= (int)pTread->fStartTexCoord;
            }
        }

        VectorCopy(vNewPos, pTread->vEndPos);
        pTread->fEndTexCoord = pTread->fMidTexCoord + fDist / 32.0;

        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }
    }

    CrossProduct(vec_upwards, vMidDir, vRight);
    VectorNormalizeFast(vRight);
    VectorMA(pTread->vMidPos, fSplitLength, vRight, pTread->vMidVerts[0]);
    VectorMA(pTread->vMidPos, 0.0 - fSplitLength, vRight, pTread->vMidVerts[1]);
    CrossProduct(vec_upwards, vDeltaNorm, vRight);
    VectorNormalizeFast(vRight);
    VectorMA(pTread->vEndPos, pTread->fWidth, vRight, pTread->vEndVerts[0]);
    VectorMA(pTread->vEndPos, 0.0 - pTread->fWidth, vRight, pTread->vEndVerts[1]);

    return 0;
}

void CG_AddTreadMarks()
{
    treadMark_t *pTread;
    trace_t      trace;
    vec3_t       vPos, vEnd;
    int          i;

    if (cg_treadmark_test->integer) {
        VectorCopy(cg.predicted_player_state.origin, vPos);
        VectorCopy(vPos, vEnd);
        vPos[2] += 32.0f;
        vEnd[2] -= 128.0f;

        CG_Trace(
            &trace,
            vPos,
            vec3_origin,
            vec3_origin,
            vEnd,
            cg.snap->ps.clientNum,
            MASK_SHOT,
            qfalse,
            qtrue,
            "CG_AddTreadMarks test"
        );

        if (trace.fraction < 1.0 && CG_UpdateTreadMark(1, trace.endpos, 1.0) == -1) {
            qhandle_t shader = cgi.R_RegisterShader("testtread");

            CG_StartTreadMark(1, shader, trace.endpos, cg_treadmark_test->integer, 1.0);
        }
    }

    for (i = 0; i < MAX_TREAD_MARKS; i++) {
        pTread = &cg_treadMarks[i];

        if (!pTread->iState) {
            continue;
        }

        if (cg.time - pTread->iLastTime > 500) {
            if (pTread->iState == 3) {
                CG_MakeTreadMarkDecal(pTread, qtrue, qfalse);
            }
            CG_MakeTreadMarkDecal(pTread, qfalse, qfalse);
            pTread->iState = 0;
        } else {
            if (pTread->iState == 3) {
                CG_MakeTreadMarkDecal(pTread, qtrue, qtrue);
            }
            CG_MakeTreadMarkDecal(pTread, qfalse, qtrue);
        }
    }
}

int CG_PermanentMark(
    vec3_t          origin,
    vec3_t          dir,
    float           orientation,
    float           fSScale,
    float           fTScale,
    float           red,
    float           green,
    float           blue,
    float           alpha,
    qboolean        dolighting,
    float           fSCenter,
    float           fTCenter,
    markFragment_t *pMarkFragments,
    void           *pVoidPolyVerts
)
{
    byte            colors[4];
    int             i, j;
    int             numFragments;
    float           fSScale2, fTScale2;
    float           fSScale3, fTScale3;
    float           texCoordScaleS, texCoordScaleT;
    vec3_t          originalPoints[4];
    vec3_t          markPoints[MAX_MARK_POLYVERTS];
    vec3_t          projection;
    vec3_t          vLight;
    vec3_t          vTmp;
    vec3_t          axis[3];
    markFragment_t *mf;
    polyVert_t     *pPolyVerts;
    polyVert_t     *v;
    clipHandle_t    cmodel;
    trace_t         trace;
    float           fRadiusSquared;

    pPolyVerts = (polyVert_t *)pVoidPolyVerts;

    VectorMA(origin, -2048, dir, vTmp);
    VectorAdd(origin, dir, origin);
    CG_Trace(
        &trace, origin, vec3_origin, vec3_origin, vTmp, ENTITYNUM_NONE, MASK_MARK, qfalse, qtrue, "CG_PermanentMark"
    );

    if (trace.fraction == 1) {
        return 0;
    }

    VectorCopy(trace.endpos, origin);
    VectorCopy(trace.plane.normal, dir);

    if (!fSScale) {
        fSScale = 1.0;
    }
    if (!fTScale) {
        fTScale = 1.0;
    }
    texCoordScaleT = 0.5 / fTScale;
    texCoordScaleS = 0.5 / fSScale;
    if (fSCenter < 0.0 || fSCenter > 1.0) {
        fSCenter = 0.5;
    }
    if (fTCenter < 0.0 || fTCenter > 1.0) {
        fTCenter = 0.5;
    }
    fRadiusSquared = fSScale * fSScale;
    fRadiusSquared = fTScale * fTScale + fRadiusSquared;
    fSScale2       = (fSScale * (1.0 - fSCenter)) * 2;
    fTScale2       = (fTScale * (1.0 - fTCenter)) * 2;
    fSScale3       = fSCenter * 2 * fSScale;
    fTScale3       = fTCenter * 2 * fTScale;

    VectorNormalize2(dir, axis[0]);
    PerpendicularVector(axis[1], axis[0]);
    RotatePointAroundVector(axis[2], axis[0], axis[1], orientation);
    CrossProduct(axis[0], axis[2], axis[1]);

    // create the full polygon
    for (i = 0; i < 3; i++) {
        originalPoints[0][i] = origin[i] - fSScale3 * axis[1][i] - fTScale3 * axis[2][i];
        originalPoints[1][i] = origin[i] + fSScale2 * axis[1][i] - fTScale3 * axis[2][i];
        originalPoints[2][i] = origin[i] + fSScale2 * axis[1][i] + fTScale2 * axis[2][i];
        originalPoints[3][i] = origin[i] - fSScale3 * axis[1][i] + fTScale2 * axis[2][i];
    }

    VectorScale(dir, -32, projection);
    numFragments = CG_GetMarkFragments(
        ARRAY_LEN(originalPoints), originalPoints, projection, markPoints, pMarkFragments, fRadiusSquared
    );

    if (!dolighting) {
        colors[0] = (int)(red * 255.0f);
        colors[1] = (int)(green * 255.0f);
        colors[2] = (int)(blue * 255.0f);
    }

    colors[3] = (int)(alpha * 255.0f);

    for (i = 0, mf = pMarkFragments; i < numFragments; i++, mf++) {
        vec3_t vWorldPos;
        vec3_t delta;

        if (mf->numPoints > 8) {
            mf->numPoints = 8;
        }

        if (mf->iIndex >= 0) {
            for (j = 0; j < mf->numPoints; j++) {
                v = &pPolyVerts[mf->firstPoint + j];
                VectorCopy(markPoints[mf->firstPoint + j], v->xyz);

                if (dolighting) {
                    cgi.R_GetLightingForDecal(vLight, dir, v->xyz);

                    colors[0] = (int)(red * vLight[0]);
                    colors[1] = (int)(green * vLight[1]);
                    colors[2] = (int)(blue * vLight[2]);
                }

                v->modulate[0] = colors[0];
                v->modulate[1] = colors[1];
                v->modulate[2] = colors[2];
                v->modulate[3] = colors[3];

                VectorSubtract(v->xyz, origin, delta);
                v->st[0] = fSCenter + DotProduct(delta, axis[1]) * texCoordScaleS;
                v->st[1] = fTCenter + DotProduct(delta, axis[2]) * texCoordScaleT;
            }
        } else if (CG_GetMarkInlineModelOrientation(mf->iIndex)) {
            for (j = 0; j < mf->numPoints; j++) {
                v = &pPolyVerts[mf->firstPoint + j];
                VectorCopy(markPoints[mf->firstPoint + j], v->xyz);

                if (dolighting) {
                    CG_FragmentPosToWorldPos(v->xyz, vWorldPos);
                    cgi.R_GetLightingForDecal(vLight, dir, v->xyz);

                    colors[0] = (int)(red * vLight[0]);
                    colors[1] = (int)(green * vLight[1]);
                    colors[2] = (int)(blue * vLight[2]);
                }

                v->modulate[0] = colors[0];
                v->modulate[1] = colors[1];
                v->modulate[2] = colors[2];
                v->modulate[3] = colors[3];

                VectorSubtract(v->xyz, origin, delta);
                v->st[0] = fSCenter + DotProduct(delta, axis[1]) * texCoordScaleS;
                v->st[1] = fTCenter + DotProduct(delta, axis[2]) * texCoordScaleT;
            }

            mf->iIndex = -cgi.CM_InlineModel(cg_entities[-mf->iIndex].currentState.modelindex);
        } else {
            mf->numPoints = 0;
        }
    }

    return numFragments;
}

int CG_PermanentTreadMarkDecal(
    treadMark_t    *pTread,
    qboolean        bStartSegment,
    qboolean        dolighting,
    markFragment_t *pMarkFragments,
    void           *pVoidPolyVerts
)
{
    byte            colors[4];
    int             i, j;
    int             numFragments;
    vec3_t          originalPoints[4];
    vec3_t          markPoints[MAX_MARK_POLYVERTS];
    vec3_t          projection;
    vec3_t          vLight;
    markFragment_t *mf;
    polyVert_t     *pPolyVerts;
    polyVert_t     *v;
    clipHandle_t    cmodel;
    float           fStartAlpha, fEndAlpha;
    float           fStartTex, fEndTex;
    float           fRightCenterDist;
    float           fOOWidth, fOODoubleWidth;
    float           fDist, fSideDist;
    float           fFrac;
    float           fStartDist, fRightStartDist, fLeftStartDist;
    float           fCenterTexScale, fRightTexScale, fLeftTexScale;
    float           fCenterAlphaScale, fRightAlphaScale, fLeftAlphaScale;
    float           fSideAlpha, fCenterAlpha;
    vec3_t          vStartCenter, vEndCenter;
    vec3_t          vDirection;
    vec3_t          vRight;
    vec3_t          vDelta;
    float           fRadiusSquared;

    pPolyVerts = (polyVert_t *)pVoidPolyVerts;

    if (bStartSegment) {
        VectorCopy(pTread->vStartVerts[1], originalPoints[0]);
        VectorCopy(pTread->vStartVerts[0], originalPoints[1]);
        VectorCopy(pTread->vMidVerts[0], originalPoints[2]);
        VectorCopy(pTread->vMidVerts[1], originalPoints[3]);

        fStartAlpha = pTread->fStartAlpha;
        fEndAlpha   = pTread->fMidAlpha;
        fStartTex   = pTread->fStartTexCoord;
        fEndTex     = pTread->fMidTexCoord;

        // Calculate the start center
        vStartCenter[0] = (originalPoints[1][0] + originalPoints[0][0]) * 0.5;
        vStartCenter[1] = (originalPoints[1][1] + originalPoints[0][1]) * 0.5;
        vStartCenter[2] = (originalPoints[1][2] + originalPoints[0][2]) * 0.5;
        VectorCopy(pTread->vMidPos, vEndCenter);
        VectorCopy(pTread->vStartDir, vDirection);
    } else {
        VectorCopy(pTread->vMidVerts[1], originalPoints[0]);
        VectorCopy(pTread->vMidVerts[0], originalPoints[1]);
        VectorCopy(pTread->vEndVerts[0], originalPoints[2]);
        VectorCopy(pTread->vEndVerts[1], originalPoints[3]);

        fStartAlpha = pTread->fMidAlpha;
        fEndAlpha   = pTread->fEndAlpha;
        fStartTex   = pTread->fMidTexCoord;
        fEndTex     = pTread->fEndTexCoord;

        VectorCopy(pTread->vMidPos, vStartCenter);
        VectorCopy(pTread->vEndPos, vEndCenter);

        // Calculate the direction
        VectorSubtract(vEndCenter, vStartCenter, vDirection);
        VectorNormalizeFast(vDirection);
    }

    CrossProduct(vec_upwards, vDirection, vRight);
    fRightCenterDist = DotProduct(vEndCenter, vRight);
    fOOWidth         = 1.0 / pTread->fWidth;
    fOODoubleWidth   = 1.0 / (pTread->fWidth * 2);
    fStartDist       = DotProduct(vDirection, vStartCenter);
    fRightStartDist  = DotProduct(vDirection, originalPoints[1]);
    fLeftStartDist   = DotProduct(vDirection, originalPoints[1]);

    //
    // Calculate center
    //
    VectorSubtract(vEndCenter, vStartCenter, vDelta);
    fDist             = VectorLength(vDelta);
    fCenterTexScale   = (fEndTex - fStartTex) / fDist;
    fCenterAlphaScale = (fEndAlpha - fStartAlpha) / fDist;
    fSideDist         = fDist;

    //
    // Calculate right
    //
    VectorSubtract(originalPoints[2], originalPoints[1], vDelta);
    fDist            = VectorLength(vDelta);
    fRightTexScale   = (fEndTex - fStartTex) / fDist;
    fRightAlphaScale = (fEndAlpha - fStartAlpha) / fDist;

    //
    // Calculate left
    //
    VectorSubtract(originalPoints[3], originalPoints[0], vDelta);
    fDist           = VectorLength(vDelta);
    fLeftTexScale   = (fEndTex - fStartTex) / fDist;
    fLeftAlphaScale = (fEndAlpha - fStartAlpha) / fDist;

    VectorSet(projection, 0, 0, -32);
    colors[0] = colors[1] = colors[2] = colors[3] = 0xff;

    numFragments = CG_GetMarkFragments(
        4, originalPoints, projection, markPoints, pMarkFragments, (Square(pTread->fWidth) + Square(fSideDist)) * 0.25
    );

    for (i = 0; i < numFragments; i++) {
        vec3_t vWorldPos;

        mf = &pMarkFragments[i];

        if (mf->numPoints > 8) {
            mf->numPoints = 8;
        }

        if (mf->iIndex >= 0) {
            for (j = 0; j < mf->numPoints; j++) {
                v = &pPolyVerts[mf->firstPoint + j];
                VectorCopy(markPoints[mf->firstPoint + j], v->xyz);

                if (dolighting) {
                    cgi.R_GetLightingForDecal(vLight, projection, v->xyz);

                    colors[0] = vLight[0];
                    colors[1] = vLight[1];
                    colors[2] = vLight[2];
                }

                fSideDist  = DotProduct(v->xyz, vRight) - fRightCenterDist;
                fSideAlpha = fSideDist * fOOWidth;
                v->st[0]   = (fSideDist + pTread->fWidth) * fOODoubleWidth;

                if (fSideAlpha < 0) {
                    fSideAlpha = -fSideAlpha;
                    v->st[1] =
                        fStartTex + ((DotProduct(v->xyz, vDirection) - fLeftStartDist) * fLeftTexScale * fSideAlpha);
                } else {
                    v->st[1] =
                        fStartTex + ((DotProduct(v->xyz, vDirection) - fRightStartDist) * fRightTexScale * fSideAlpha);
                }

                v->st[1] += (DotProduct(v->xyz, vDirection) - fStartDist) * fCenterTexScale * (1.0 - fSideAlpha);
            }
        } else if (CG_GetMarkInlineModelOrientation(mf->iIndex)) {
            for (j = 0; j < mf->numPoints; j++) {
                v = &pPolyVerts[mf->firstPoint + j];
                VectorCopy(markPoints[mf->firstPoint + j], v->xyz);

                if (dolighting) {
                    cgi.R_GetLightingForDecal(vLight, projection, v->xyz);

                    colors[0] = vLight[0];
                    colors[1] = vLight[1];
                    colors[2] = vLight[2];
                }

                v->modulate[0] = colors[0];
                v->modulate[1] = colors[1];
                v->modulate[2] = colors[2];
                v->modulate[3] = colors[3];

                CG_FragmentPosToWorldPos(v->xyz, vWorldPos);

                fSideDist  = DotProduct(vWorldPos, vRight) - fRightCenterDist;
                fSideAlpha = fSideDist * fOOWidth;
                v->st[0]   = (fSideDist + pTread->fWidth) * fOODoubleWidth;

                if (fSideAlpha < 0) {
                    fSideAlpha = -fSideAlpha;
                    v->st[1] =
                        fStartTex + ((DotProduct(vWorldPos, vDirection) - fLeftStartDist) * fLeftTexScale * fSideAlpha);
                } else {
                    v->st[1] = fStartTex
                             + ((DotProduct(vWorldPos, vDirection) - fRightStartDist) * fRightTexScale * fSideAlpha);
                }

                v->st[1] += (DotProduct(vWorldPos, vDirection) - fStartDist) * fCenterTexScale * (1.0 - fSideAlpha);
            }

            mf->iIndex = -cgi.CM_InlineModel(cg_entities[-mf->iIndex].currentState.modelindex);
        } else {
            mf->numPoints = 0;
        }
    }

    return numFragments;
}

int CG_PermanentUpdateTreadMark(
    treadMark_t *pTread, float fAlpha, float fMinSegment, float fMaxSegment, float fMaxOffset, float fTexScale
)
{
    trace_t  trace;
    float    fDist;
    float    fSplitLength;
    float    fNewLength;
    float    fTmp;
    qboolean bDoSegmentation;
    vec3_t   vPos;
    vec3_t   vEnd;
    vec3_t   vNewPos;
    vec3_t   vDelta, vDeltaNorm;
    vec3_t   vDir, vMidDir;
    vec3_t   vRight;

    VectorCopy(cg.predicted_player_state.origin, vPos);
    VectorCopy(cg.predicted_player_state.origin, vEnd);

    vPos[2] += 32;
    vEnd[2] -= 256;

    CG_Trace(
        &trace,
        vPos,
        vec3_origin,
        vec3_origin,
        vEnd,
        cg.snap->ps.clientNum,
        MASK_TREADMARK,
        qfalse,
        qtrue,
        "CG_PermanentUpdateTreadMark"
    );

    VectorCopy(trace.endpos, vNewPos);

    if (cg.time - pTread->iLastTime > 500) {
        VectorClear(pTread->vStartDir);
        VectorClear(pTread->vStartVerts[0]);
        VectorClear(pTread->vStartVerts[1]);
        pTread->fStartTexCoord = 0;
        pTread->fStartAlpha    = 0;

        VectorClear(pTread->vMidPos);
        VectorClear(pTread->vMidVerts[0]);
        VectorClear(pTread->vMidVerts[1]);
        pTread->fMidTexCoord = 0;
        pTread->fMidAlpha    = 0;

        VectorClear(pTread->vEndPos);
        VectorClear(pTread->vEndVerts[0]);
        VectorClear(pTread->vEndVerts[1]);
        pTread->fEndTexCoord = 0;
        pTread->fEndAlpha    = 0;

        pTread->iState           = 1;
        pTread->iReferenceNumber = 0;
        pTread->iLastTime        = cg.time;
        VectorCopy(vNewPos, pTread->vMidPos);
        VectorCopy(vNewPos, pTread->vEndPos);

        if (fAlpha < 0) {
            pTread->fMidAlpha = 255.0;
        } else {
            pTread->fMidAlpha = fAlpha * 255.0;
        }
        pTread->fEndAlpha = pTread->fMidAlpha;

        return 0;
    }

    if (VectorCompare(pTread->vEndPos, vNewPos)) {
        if (fAlpha < 0) {
            return -1;
        }

        fTmp              = pTread->fEndAlpha;
        pTread->fEndAlpha = fAlpha * 255;

        //if (fabs(fTmp - pTread->fEndAlpha > 0.05)) {
        // Fixed in OPM
        //  Looks like this was accidental
        if (fabs(fTmp - pTread->fEndAlpha) > 0.05) {
            return -1;
        }

        return 0;
    }

    VectorSubtract(vNewPos, pTread->vMidPos, vDelta);
    fDist = VectorNormalize2(vDelta, vDeltaNorm);

    if (pTread->iState == 1) {
        fSplitLength    = 0.0;
        bDoSegmentation = qfalse;
    } else {
        VectorAdd(vDeltaNorm, pTread->vStartDir, vMidDir);
        VectorScale(vMidDir, 0.5, vMidDir);
        VectorNormalizeFast(vMidDir);

        fTmp         = DotProduct(vMidDir, vDeltaNorm);
        fSplitLength = pTread->fWidth / fTmp;

        if (fTmp < -0.5) {
            fNewLength =
                fTmp
                * (fTmp
                       * (fTmp
                              * (fTmp * (fTmp * (fTmp * -337.31875783205 + -1237.54375255107) + -1802.11467325687)
                                 + -1303.19904613494)
                          + -471.347871690988)
                   + -70.0883838161826);
        } else if (fTmp > 0.5) {
            fNewLength =
                fTmp
                    * (fTmp
                           * (fTmp
                                  * (fTmp * (fTmp * (fTmp * -1507.55394345521 + 6580.58002318442) + -11860.0735285953)
                                     + 11290.7510782536)
                              + -5986.89654545347)
                       + 1675.66417006387)
                + -192.426950291139;
        } else {
            fNewLength = fTmp * (fTmp * -0.531387674508458 + -2.11e-14) + 1.00086138065435;
        }

        fNewLength *= fSplitLength;
        if (fNewLength + fMinSegment > fDist) {
            bDoSegmentation = qfalse;
        } else if (fSplitLength < -1) {
            fSplitLength    = -fSplitLength;
            bDoSegmentation = qtrue;
        } else if (fDist > fMaxSegment) {
            bDoSegmentation = qtrue;
        } else {
            CrossProduct(vec_upwards, pTread->vStartDir, vRight);
            VectorNormalizeFast(vRight);

            if (fabs(DotProduct(vRight, vDelta)) > fMaxOffset) {
                bDoSegmentation = qtrue;
            } else {
                bDoSegmentation = qfalse;
            }
        }
    }

    if (pTread->iState == 2 && bDoSegmentation) {
        VectorCopy(pTread->vStartVerts[0], pTread->vMidVerts[0]);
        VectorCopy(pTread->vStartVerts[1], pTread->vMidVerts[1]);
        pTread->fMidTexCoord = pTread->fEndTexCoord;
        pTread->fMidAlpha    = pTread->fEndAlpha;
        VectorCopy(vNewPos, pTread->vEndPos);

        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }

        pTread->iState = 3;

        return 0;
    } else if (pTread->iState == 1 || pTread->iState == 2) {
        VectorCopy(vNewPos, pTread->vEndPos);

        if (fAlpha >= 0.0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }

        CrossProduct(vec_upwards, vDeltaNorm, vDir);
        VectorMA(pTread->vMidPos, pTread->fWidth, vDir, pTread->vMidVerts[0]);
        VectorMA(pTread->vMidPos, -pTread->fWidth, vDir, pTread->vMidVerts[1]);
        VectorMA(pTread->vEndPos, pTread->fWidth, vDir, pTread->vEndVerts[0]);
        VectorMA(pTread->vEndPos, -pTread->fWidth, vDir, pTread->vEndVerts[1]);

        pTread->fEndTexCoord = fDist * fTexScale;

        if (pTread->iState == 1 && fDist > 8.0) {
            VectorCopy(vDelta, pTread->vStartDir);
            pTread->iState = 2;
        }

        return 0;
    } else if (bDoSegmentation) {
        if (!pTread->iReferenceNumber) {
            pTread->iReferenceNumber = 1;
            return 1;
        }

        pTread->iReferenceNumber = 0;
        VectorCopy(pTread->vStartVerts[0], pTread->vMidVerts[0]);
        VectorCopy(pTread->vStartVerts[1], pTread->vMidVerts[1]);
        VectorSubtract(pTread->vEndPos, pTread->vMidPos, pTread->vStartDir);
        VectorNormalizeFast(pTread->vStartDir);

        VectorCopy(pTread->vEndPos, pTread->vMidPos);
        pTread->fMidTexCoord = pTread->fEndTexCoord;
        pTread->fMidAlpha    = pTread->fEndAlpha;

        if (pTread->fStartTexCoord >= 1.0) {
            pTread->fStartTexCoord = pTread->fStartTexCoord - floor(pTread->fStartTexCoord);
            pTread->fMidTexCoord   = pTread->fMidTexCoord - floor(pTread->fStartTexCoord);
        }

        VectorCopy(vNewPos, pTread->vEndPos);
        pTread->fEndTexCoord = fDist * fTexScale + pTread->fMidTexCoord;
        if (fAlpha >= 0) {
            pTread->fEndAlpha = fAlpha * 255.0;
        }
    }

    CrossProduct(vec_upwards, vMidDir, vRight);
    VectorNormalizeFast(vRight);
    VectorMA(pTread->vMidPos, fSplitLength, vRight, pTread->vMidVerts[0]);
    VectorMA(pTread->vMidPos, -fSplitLength, vRight, pTread->vMidVerts[1]);
    CrossProduct(vec_upwards, vDeltaNorm, vRight);
    VectorNormalizeFast(vRight);
    VectorMA(pTread->vEndPos, pTread->fWidth, vRight, pTread->vEndVerts[0]);
    VectorMA(pTread->vEndPos, -pTread->fWidth, vRight, pTread->vEndVerts[1]);

    return 0;
}

/*
===============================================================================
REMASTER REALISTIC BLOOD & GORE SYSTEM (cg_gore 0 / 1 / 2)
Pure client-side procedural blood mist, gravity droplets, exit-wound wall
splatters, and seeping ground pools (zero external assets, savegame-safe).
===============================================================================
*/

#define MAX_GORE_PARTICLES 192

typedef struct {
    qboolean active;
    qboolean isMist;
    qboolean isWallDrip;
    qboolean isGunSmoke;
    qboolean isSpark;
    qboolean canSplat;
    int      startTime;
    int      lifeTime;
    vec3_t   pos;
    vec3_t   vel;
    float    startSize;
    float    endSize;
    float    alpha;
    float    rotCos;
    float    rotSin;
    byte     rgb[3];
} cgGoreParticle_t;

static cgGoreParticle_t s_goreParticles[MAX_GORE_PARTICLES];
static int              s_goreNextSlot   = 0;
static int              s_goreLastTime   = 0;
static unsigned int     s_goreRngState   = 0x5A17C391u;
static vec3_t           s_gorePoolPos[4];
static int              s_gorePoolTime[4];
static int              s_gorePoolNext   = 0;

static float CG_GoreRand01(void)
{
    s_goreRngState = s_goreRngState * 1664525u + 1013904223u;
    return (float)((s_goreRngState >> 8) & 0xFFFFu) / 65535.0f;
}

static float CG_GoreRandSigned(void)
{
    return CG_GoreRand01() * 2.0f - 1.0f;
}

static cgGoreParticle_t *CG_AllocGoreParticle(void)
{
    cgGoreParticle_t *p = &s_goreParticles[s_goreNextSlot];
    s_goreNextSlot = (s_goreNextSlot + 1) % MAX_GORE_PARTICLES;
    memset(p, 0, sizeof(*p));
    p->active = qtrue;
    return p;
}

#define MAX_BODY_WOUNDS            96
#define MAX_BODY_WOUNDS_PER_ENTITY 6

typedef struct {
    qboolean active;
    qboolean bound;
    qboolean fromPlayerRay;
    int      entityNum;
    dtiki_t *tiki;
    int      tagNum;
    vec3_t   localPos;      // bone-local once bound; world hit pos while pending
    vec3_t   localNorm;     // bone-local once bound; world hit normal while pending
    vec3_t   lastEntOrigin; // detects MP respawn/teleport on same entity slot
    float    rotAng;
    float    baseRadius;
    int      startTime;
    int      lifeTime;
    int      lastSeenTime;
    byte     rgb[3];
} cgBodyWound_t;

static cgBodyWound_t s_bodyWounds[MAX_BODY_WOUNDS];
static int           s_bodyWoundNext = 0;

static const char *s_bodyWoundBones[] = {
    "Bip01 Pelvis",
    "Bip01 Spine",
    "Bip01 Spine1",
    "Bip01 Spine2",
    "Bip01 Neck",
    "Bip01 Head",
    "Bip01 L Clavicle",
    "Bip01 R Clavicle",
    "Bip01 L UpperArm",
    "Bip01 R UpperArm",
    "Bip01 L Forearm",
    "Bip01 R Forearm",
    "Bip01 L Thigh",
    "Bip01 R Thigh",
    "Bip01 L Calf",
    "Bip01 R Calf"
};

static void CG_GetBoneWorldTransform(refEntity_t *pModel, int tagNum, vec3_t outOrigin, vec3_t outAxis[3])
{
    orientation_t oTag;
    int           k;

    oTag = cgi.TIKI_Orientation(pModel, tagNum);
    VectorCopy(pModel->origin, outOrigin);
    for (k = 0; k < 3; k++) {
        VectorMA(outOrigin, oTag.origin[k], pModel->axis[k], outOrigin);
    }
    R_ConcatRotations(oTag.axis, pModel->axis, outAxis);
}

static void CG_QueueBodyWound(const vec3_t pos, const vec3_t norm, qboolean large, const byte litRgb[3])
{
    cgBodyWound_t *w;

    if (!cgs.media.bloodWoundShader) {
        return;
    }

    w = &s_bodyWounds[s_bodyWoundNext];
    s_bodyWoundNext = (s_bodyWoundNext + 1) % MAX_BODY_WOUNDS;
    memset(w, 0, sizeof(*w));
    w->active        = qtrue;
    w->bound         = qfalse;
    w->fromPlayerRay = qfalse;
    w->entityNum     = -1;
    VectorCopy(pos, w->localPos);
    // Prefer exact bullet line-of-sight when player just fired (avoids boxy SOLID_BBOX +-X/+-Y/+-Z projection)
    if ((cg.time - cg.lastFireTime) >= 0 && (cg.time - cg.lastFireTime) <= 220) {
        vec3_t toHit;
        float  dist;
        VectorSubtract(pos, cg.refdef.vieworg, toHit);
        dist = VectorNormalize(toHit);
        if (dist > 24.0f && DotProduct(toHit, cg.refdef.viewaxis[0]) > 0.85f) {
            VectorNegate(toHit, w->localNorm);
            w->fromPlayerRay = qtrue;
        }
    }
    if (!w->fromPlayerRay) {
        // cg_parsemsg.cpp negates flesh_impact_norm for exit-spray; invert back to outward normal
        VectorNegate(norm, w->localNorm);
    }
    w->rotAng       = CG_GoreRand01() * 6.2831853f;
    w->baseRadius   = large ? (2.9f + CG_GoreRand01() * 0.7f) : (2.2f + CG_GoreRand01() * 0.6f);
    w->startTime    = cg.time;
    w->lifeTime     = 45000;
    w->lastSeenTime = cg.time;
    w->rgb[0]       = litRgb[0];
    w->rgb[1]       = litRgb[1];
    w->rgb[2]       = litRgb[2];
}

static qboolean CG_BindPendingBodyWound(cgBodyWound_t *w)
{
    int          bestEnt = -1;
    int          bestTag = -1;
    int          bestBoneIdx = -1;
    float        bestDistSq = 28.0f * 28.0f;
    vec3_t       bestBoneOrg;
    vec3_t       bestBoneAxis[3];
    refEntity_t *bestModel = NULL;
    int          i, b;
    int          clientNum;

    if (!cg.snap) {
        return qfalse;
    }
    clientNum = cg.snap->ps.clientNum;

    // Evaluate all visible snapshot entities + trace candidate for closest skeletal Bip01 bone
    for (i = -1; i < cg.snap->numEntities; i++) {
        int          entNum;
        refEntity_t *pModel;
        vec3_t       toEnt;

        if (i < 0) {
            trace_t tr;
            vec3_t  sPos, ePos;
            VectorMA(w->localPos, 14.0f, w->localNorm, sPos);
            VectorMA(w->localPos, -22.0f, w->localNorm, ePos);
            CG_Trace(&tr, sPos, vec3_origin, vec3_origin, ePos, clientNum, MASK_SHOT, qfalse, qfalse, "CG_BodyWoundBind");
            if (tr.fraction >= 1.0f || tr.entityNum == ENTITYNUM_WORLD || tr.entityNum < 0 || tr.entityNum >= MAX_GENTITIES) {
                continue;
            }
            entNum = tr.entityNum;
        } else {
            entNum = cg.snap->entities[i].number;
        }

        if (entNum == clientNum || entNum < 0 || entNum >= MAX_GENTITIES) {
            continue;
        }

        pModel = cgi.R_GetRenderEntity(entNum);
        if (!pModel || !pModel->tiki || (pModel->renderfx & (RF_FIRST_PERSON | RF_DONTDRAW | RF_INVISIBLE))) {
            continue;
        }

        VectorSubtract(w->localPos, pModel->origin, toEnt);
        if (toEnt[0] * toEnt[0] + toEnt[1] * toEnt[1] > 64.0f * 64.0f
            || toEnt[2] < -24.0f || toEnt[2] > 96.0f) {
            continue;
        }

        for (b = 0; b < (int)ARRAY_LEN(s_bodyWoundBones); b++) {
            int    tagNum = cgi.Tag_NumForName(pModel->tiki, s_bodyWoundBones[b]);
            vec3_t bOrg, bAxis[3], diff;
            float  d2;

            if (tagNum < 0) {
                continue;
            }
            CG_GetBoneWorldTransform(pModel, tagNum, bOrg, bAxis);
            VectorSubtract(w->localPos, bOrg, diff);
            d2 = VectorLengthSquared(diff);
            if (d2 < bestDistSq) {
                bestDistSq  = d2;
                bestEnt     = entNum;
                bestTag     = tagNum;
                bestBoneIdx = b;
                bestModel   = pModel;
                VectorCopy(bOrg, bestBoneOrg);
                AxisCopy(bAxis, bestBoneAxis);
            }
        }
    }

    if (bestEnt < 0 || bestTag < 0 || !bestModel) {
        return qfalse;
    }

    // Cap wounds per entity by retiring the oldest wound on this soldier
    {
        int count = 0;
        int oldestIdx = -1;
        int oldestTime = 0x7fffffff;
        for (i = 0; i < MAX_BODY_WOUNDS; i++) {
            if (s_bodyWounds[i].active && s_bodyWounds[i].bound && s_bodyWounds[i].entityNum == bestEnt && &s_bodyWounds[i] != w) {
                count++;
                if (s_bodyWounds[i].startTime < oldestTime) {
                    oldestTime = s_bodyWounds[i].startTime;
                    oldestIdx  = i;
                }
            }
        }
        if (count >= MAX_BODY_WOUNDS_PER_ENTITY && oldestIdx >= 0) {
            s_bodyWounds[oldestIdx].active = qfalse;
        }
    }

    // Store hit position and normal in bone-local space so wound tracks all animations
    {
        vec3_t delta, projNorm, lPos, lNorm;
        VectorSubtract(w->localPos, bestBoneOrg, delta);
        VectorCopy(w->localNorm, projNorm);
        // For AI/non-player shots, blend radial bone->hit direction to avoid boxy SOLID_BBOX axis normals
        if (!w->fromPlayerRay) {
            vec3_t radDir;
            VectorCopy(delta, radDir);
            if (VectorNormalize(radDir) > 0.5f) {
                projNorm[0] = radDir[0] * 0.65f + projNorm[0] * 0.35f;
                projNorm[1] = radDir[1] * 0.65f + projNorm[1] * 0.35f;
                projNorm[2] = radDir[2] * 0.65f + projNorm[2] * 0.35f;
                VectorNormalize(projNorm);
            }
        }
        lPos[0]  = DotProduct(delta, bestBoneAxis[0]);
        lPos[1]  = DotProduct(delta, bestBoneAxis[1]);
        lPos[2]  = DotProduct(delta, bestBoneAxis[2]);
        lNorm[0] = DotProduct(projNorm, bestBoneAxis[0]);
        lNorm[1] = DotProduct(projNorm, bestBoneAxis[1]);
        lNorm[2] = DotProduct(projNorm, bestBoneAxis[2]);
        if (VectorNormalize(lNorm) < 0.001f) {
            VectorSet(lNorm, 1.0f, 0.0f, 0.0f);
        }
        VectorCopy(lPos, w->localPos);
        VectorCopy(lNorm, w->localNorm);
    }

    // Scale wound radius strictly to anatomical body zone (soldier head is only ~8-9 units wide)
    if (bestBoneIdx == 4 || bestBoneIdx == 5) {
        w->baseRadius *= 0.45f; // Head / Neck: crisp bullet entry hole + small cheek/forehead stain
    } else if ((bestBoneIdx >= 6 && bestBoneIdx <= 11) || bestBoneIdx >= 14) {
        w->baseRadius *= 0.72f; // Arms / Forearms / Calves
    }

    w->entityNum    = bestEnt;
    w->tiki         = bestModel->tiki;
    w->tagNum       = bestTag;
    w->bound        = qtrue;
    w->lastSeenTime = cg.time;
    VectorCopy(bestModel->origin, w->lastEntOrigin);
    return qtrue;
}

static void CG_AddBodyWounds(void)
{
    int        i;
    polyVert_t verts[4];

    if (!cg_gore || cg_gore->integer <= 0 || !cgs.media.bloodWoundShader) {
        return;
    }

    for (i = 0; i < MAX_BODY_WOUNDS; i++) {
        cgBodyWound_t *w = &s_bodyWounds[i];
        refEntity_t   *pModel;
        vec3_t         bOrg, bAxis[3], wPos, wNorm, u0, v0, axU, axV, entDelta;
        int            age;
        float          seepT, radius, alpha, ca, sa;
        byte           aByte;

        if (!w->active) {
            continue;
        }
        if (cg.time < w->startTime || (cg.time - w->startTime) >= w->lifeTime) {
            w->active = qfalse;
            continue;
        }

        if (!w->bound) {
            if (!CG_BindPendingBodyWound(w)) {
                // Retry next frame briefly if entity snapshot arrived a tick later, else discard
                if (cg.time - w->startTime > 120) {
                    w->active = qfalse;
                }
                continue;
            }
        }

        pModel = cgi.R_GetRenderEntity(w->entityNum);
        if (!pModel || pModel->tiki != w->tiki || (pModel->renderfx & (RF_FIRST_PERSON | RF_DONTDRAW | RF_INVISIBLE))) {
            if (cg.time - w->lastSeenTime > 1200) {
                w->active = qfalse;
            }
            continue;
        }

        // Detect entity slot respawn/teleport (e.g. multiplayer respawn)
        VectorSubtract(pModel->origin, w->lastEntOrigin, entDelta);
        if (VectorLengthSquared(entDelta) > 128.0f * 128.0f) {
            w->active = qfalse;
            continue;
        }
        VectorCopy(pModel->origin, w->lastEntOrigin);
        w->lastSeenTime = cg.time;

        CG_GetBoneWorldTransform(pModel, w->tagNum, bOrg, bAxis);

        // Transform bone-local wound back to current animated world pose
        VectorCopy(bOrg, wPos);
        VectorMA(wPos, w->localPos[0], bAxis[0], wPos);
        VectorMA(wPos, w->localPos[1], bAxis[1], wPos);
        VectorMA(wPos, w->localPos[2], bAxis[2], wPos);

        VectorClear(wNorm);
        VectorMA(wNorm, w->localNorm[0], bAxis[0], wNorm);
        VectorMA(wNorm, w->localNorm[1], bAxis[1], wNorm);
        VectorMA(wNorm, w->localNorm[2], bAxis[2], wNorm);
        if (VectorNormalize(wNorm) < 0.001f) {
            VectorSet(wNorm, 0.0f, 0.0f, 1.0f);
        }

        // Bloodstain gradually soaks outward into uniform over first 1.6s, dries/fades in last 20%
        age = cg.time - w->startTime;
        seepT = (float)age / 1600.0f;
        if (seepT > 1.0f) {
            seepT = 1.0f;
        }
        radius = w->baseRadius * (0.68f + 0.32f * (seepT * (2.0f - seepT)));

        alpha = 0.94f;
        if (age < 120) {
            alpha *= (0.45f + 0.55f * ((float)age / 120.0f));
        } else {
            float lifeFrac = (float)age / (float)w->lifeTime;
            if (lifeFrac > 0.80f) {
                alpha *= (1.0f - lifeFrac) * 5.0f;
            }
        }
        if (alpha <= 0.01f) {
            continue;
        }
        aByte = (byte)(Com_Clamp(0.0f, 1.0f, alpha) * 255.0f);

        // Right-handed tangent basis: CrossProduct(axU, axV) == +wNorm
        PerpendicularVector(u0, wNorm);
        CrossProduct(wNorm, u0, v0);
        ca = cosf(w->rotAng);
        sa = sinf(w->rotAng);
        VectorScale(u0, ca * radius, axU);
        VectorMA(axU, sa * radius, v0, axU);
        VectorScale(u0, -sa * radius, axV);
        VectorMA(axV, ca * radius, v0, axV);

        verts[0].xyz[0] = wPos[0] - axU[0] - axV[0];
        verts[0].xyz[1] = wPos[1] - axU[1] - axV[1];
        verts[0].xyz[2] = wPos[2] - axU[2] - axV[2];
        verts[0].st[0]  = 0.0f;
        verts[0].st[1]  = 0.0f;

        verts[1].xyz[0] = wPos[0] + axU[0] - axV[0];
        verts[1].xyz[1] = wPos[1] + axU[1] - axV[1];
        verts[1].xyz[2] = wPos[2] + axU[2] - axV[2];
        verts[1].st[0]  = 1.0f;
        verts[1].st[1]  = 0.0f;

        verts[2].xyz[0] = wPos[0] + axU[0] + axV[0];
        verts[2].xyz[1] = wPos[1] + axU[1] + axV[1];
        verts[2].xyz[2] = wPos[2] + axU[2] + axV[2];
        verts[2].st[0]  = 1.0f;
        verts[2].st[1]  = 1.0f;

        verts[3].xyz[0] = wPos[0] - axU[0] + axV[0];
        verts[3].xyz[1] = wPos[1] - axU[1] + axV[1];
        verts[3].xyz[2] = wPos[2] - axU[2] + axV[2];
        verts[3].st[0]  = 0.0f;
        verts[3].st[1]  = 1.0f;

        // Premultiplied fade for GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA cloth-soak blend (inherits 100% live entity lighting)
        verts[0].modulate[0] = verts[1].modulate[0] = verts[2].modulate[0] = verts[3].modulate[0] = aByte;
        verts[0].modulate[1] = verts[1].modulate[1] = verts[2].modulate[1] = verts[3].modulate[1] = aByte;
        verts[0].modulate[2] = verts[1].modulate[2] = verts[2].modulate[2] = verts[3].modulate[2] = aByte;
        verts[0].modulate[3] = verts[1].modulate[3] = verts[2].modulate[3] = verts[3].modulate[3] = aByte;

        cgi.R_AddPolyToScene(cgs.media.bloodWoundShader, 4, verts, RF_BODY_WOUND | (w->entityNum & 0x3FF));
    }
}

typedef struct {
    int      deadSince;
    qboolean pooled;
    dtiki_t *tiki;
    vec3_t   lastOrigin;
} cgCorpsePool_t;

static cgCorpsePool_t s_corpsePools[MAX_GENTITIES];
static int            s_corpsePoolScanTime = 0;

static void CG_PlaceBloodDecal(
    qhandle_t    shader,
    const vec3_t origin,
    const vec3_t normal,
    float        radius,
    float        alpha,
    qboolean     fadein
);

static void CG_UpdateCorpseBloodPools(void)
{
    int i, goreLevel;

    if (!cg_gore || cg_gore->integer <= 0 || !cgs.media.bloodPoolShader || !cg.snap) {
        return;
    }

    if (s_corpsePoolScanTime > 0 && cg.time >= s_corpsePoolScanTime && (cg.time - s_corpsePoolScanTime) < 200) {
        return;
    }
    s_corpsePoolScanTime = cg.time;
    goreLevel            = (cg_gore->integer >= 2) ? 2 : 1;

    for (i = 0; i < cg.snap->numEntities; i++) {
        const entityState_t *s1 = &cg.snap->entities[i];
        int                  entNum = s1->number;
        cgCorpsePool_t      *cp;
        refEntity_t         *pModel;
        vec3_t               torsoPos, start, downEnd, delta;
        trace_t              tr;
        int                  tagNum, j;

        if (entNum < 0 || entNum >= MAX_GENTITIES || entNum == cg.snap->ps.clientNum) {
            continue;
        }

        cp = &s_corpsePools[entNum];
        if (!(s1->eFlags & EF_DEAD)) {
            if (cp->deadSince > 0) {
                memset(cp, 0, sizeof(*cp));
            }
            continue;
        }

        pModel = cgi.R_GetRenderEntity(entNum);
        if (!pModel || !pModel->tiki || (pModel->renderfx & (RF_FIRST_PERSON | RF_DONTDRAW | RF_INVISIBLE))) {
            continue;
        }

        if (cp->deadSince <= 0 || cg.time < cp->deadSince || cp->tiki != pModel->tiki) {
            cp->deadSince = cg.time;
            cp->pooled    = qfalse;
            cp->tiki      = pModel->tiki;
            VectorCopy(pModel->origin, cp->lastOrigin);
            continue;
        }

        VectorSubtract(pModel->origin, cp->lastOrigin, delta);
        if (VectorLengthSquared(delta) > 128.0f * 128.0f) {
            cp->deadSince = cg.time;
            cp->pooled    = qfalse;
            VectorCopy(pModel->origin, cp->lastOrigin);
            continue;
        }
        VectorCopy(pModel->origin, cp->lastOrigin);

        if (cp->pooled) {
            continue;
        }

        // Wait ~850ms so collapsing soldiers settle onto the ground before the blood pool seeps out beneath their torso
        if ((cg.time - cp->deadSince) < 850) {
            continue;
        }

        // Locate actual fallen torso bone (falls up to 2m away from standing entity origin during death anim)
        tagNum = cgi.Tag_NumForName(pModel->tiki, "Bip01 Spine1");
        if (tagNum < 0) {
            tagNum = cgi.Tag_NumForName(pModel->tiki, "Bip01 Pelvis");
        }
        if (tagNum >= 0) {
            vec3_t bAxis[3];
            CG_GetBoneWorldTransform(pModel, tagNum, torsoPos, bAxis);
        } else {
            VectorCopy(pModel->origin, torsoPos);
            torsoPos[2] += 10.0f;
        }

        // Skip only deep ocean water (>22u above fallen torso), allowing wet shoreline sand on Omaha Beach
        {
            vec3_t waterCheck;
            VectorCopy(torsoPos, waterCheck);
            waterCheck[2] += 22.0f;
            if (CG_PointContents(waterCheck, -1) & CONTENTS_WATER) {
                cp->pooled = qtrue;
                continue;
            }
        }

        VectorCopy(torsoPos, start);
        start[2] += 16.0f;
        VectorCopy(torsoPos, downEnd);
        downEnd[2] -= 48.0f;

        CG_Trace(&tr, start, vec3_origin, vec3_origin, downEnd, entNum, MASK_SOLID, qfalse, qfalse, "CG_CorpseBloodPool");
        if (tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal[2] > 0.58f && !(tr.surfaceFlags & SURF_SKY)) {
            qboolean tooClose = qfalse;
            for (j = 0; j < 4; j++) {
                if (s_gorePoolTime[j] > 0 && (cg.time - s_gorePoolTime[j]) < 5000) {
                    vec3_t d;
                    VectorSubtract(tr.endpos, s_gorePoolPos[j], d);
                    if (DotProduct(d, d) < (16.0f * 16.0f)) {
                        tooClose = qtrue;
                        break;
                    }
                }
            }

            if (!tooClose) {
                float poolRadius = (14.5f + CG_GoreRand01() * 6.5f) * (goreLevel >= 2 ? 1.25f : 1.0f);
                VectorCopy(tr.endpos, s_gorePoolPos[s_gorePoolNext]);
                s_gorePoolTime[s_gorePoolNext] = cg.time;
                s_gorePoolNext = (s_gorePoolNext + 1) & 3;
                CG_PlaceBloodDecal(cgs.media.bloodPoolShader, tr.endpos, tr.plane.normal, poolRadius, 0.88f, qtrue);
            }
        }

        cp->pooled = qtrue;
    }
}

void CG_InitGore(void)
{
    memset(s_goreParticles, 0, sizeof(s_goreParticles));
    memset(s_gorePoolPos, 0, sizeof(s_gorePoolPos));
    memset(s_gorePoolTime, 0, sizeof(s_gorePoolTime));
    memset(s_bodyWounds, 0, sizeof(s_bodyWounds));
    memset(s_corpsePools, 0, sizeof(s_corpsePools));
    s_goreNextSlot       = 0;
    s_goreLastTime       = 0;
    s_gorePoolNext       = 0;
    s_bodyWoundNext      = 0;
    s_corpsePoolScanTime = 0;
}

static void CG_PlaceBloodDecal(
    qhandle_t    shader,
    const vec3_t origin,
    const vec3_t normal,
    float        radius,
    float        alpha,
    qboolean     fadein
)
{
    float angle;

    if (!shader || radius <= 0.5f) {
        return;
    }

    angle = 1.0f + CG_GoreRand01() * 358.0f;
    CG_ImpactMark(
        shader,
        origin,
        normal,
        angle,
        radius,
        radius,
        0.92f,
        0.92f,
        0.92f,
        alpha,
        qtrue,
        qfalse,
        qtrue,
        fadein,
        0.5f,
        0.5f
    );
}

void CG_SpawnBloodImpact(const vec3_t pos, const vec3_t norm, qboolean large)
{
    int      goreLevel;
    float    intensity;
    vec3_t   n, right, up, toCam;
    vec3_t   vLight;
    byte     litRgb[3];
    float    camDist;
    int      numMist, numDrops, i;
    trace_t  tr;

    if (!cg_gore || cg_gore->integer <= 0) {
        return;
    }

    if (!cgs.media.bloodMistShader && !cgs.media.bloodSplatShaders[0]) {
        return;
    }

    goreLevel = (cg_gore->integer >= 2) ? 2 : 1;
    intensity = (goreLevel >= 2) ? 1.55f : 1.0f;
    if (large) {
        intensity *= 1.35f;
    }

    VectorCopy(norm, n);
    if (VectorNormalize(n) < 0.001f) {
        VectorSet(n, 0.0f, 0.0f, 1.0f);
    }
    PerpendicularVector(right, n);
    CrossProduct(n, right, up);

    // Sample local world lighting so blood stays grounded in shadows and rich in sunlight
    cgi.R_GetLightingForDecal(vLight, n, pos);
    for (i = 0; i < 3; i++) {
        float c = vLight[i];
        if (c < 72.0f) {
            c = 72.0f;
        } else if (c > 255.0f) {
            c = 255.0f;
        }
        litRgb[i] = (byte)c;
    }

    VectorSubtract(pos, cg.refdef.vieworg, toCam);
    camDist = VectorLength(toCam);

    // 0. Queue bone-tracked entry wound + spreading bloodstain on the struck soldier's body
    CG_QueueBodyWound(pos, n, large, litRgb);

    // 1. Expanding fine arterial blood mist puffs (skip directly inside camera eye)
    if (cgs.media.bloodMistShader && camDist > 28.0f) {
        numMist = large ? (goreLevel >= 2 ? 4 : 3) : (goreLevel >= 2 ? 3 : 2);
        for (i = 0; i < numMist; i++) {
            cgGoreParticle_t *p = CG_AllocGoreParticle();
            float ang = CG_GoreRand01() * 6.2831853f;
            float outSign = (i == 0) ? 1.0f : -1.0f; // entry + exit mist
            float spd = (18.0f + CG_GoreRand01() * 34.0f) * intensity;

            p->isMist    = qtrue;
            p->canSplat  = qfalse;
            p->startTime = cg.time;
            p->lifeTime  = (int)((310.0f + CG_GoreRand01() * 190.0f) * (large ? 1.15f : 1.0f));

            VectorMA(pos, outSign * (2.0f + CG_GoreRand01() * 4.0f), n, p->pos);
            VectorMA(p->pos, CG_GoreRandSigned() * 3.0f, right, p->pos);
            VectorMA(p->pos, CG_GoreRandSigned() * 3.0f, up, p->pos);

            VectorScale(n, outSign * spd, p->vel);
            VectorMA(p->vel, CG_GoreRandSigned() * 24.0f, right, p->vel);
            VectorMA(p->vel, CG_GoreRandSigned() * 24.0f, up, p->vel);
            p->vel[2] += 8.0f + CG_GoreRand01() * 18.0f;

            p->startSize = (5.5f + CG_GoreRand01() * 3.5f) * (large ? 1.25f : 1.0f);
            p->endSize   = (16.0f + CG_GoreRand01() * 10.0f) * intensity;
            p->alpha     = large ? 0.72f : 0.60f;
            p->rotCos    = cosf(ang);
            p->rotSin    = sinf(ang);
            p->rgb[0]    = litRgb[0];
            p->rgb[1]    = litRgb[1];
            p->rgb[2]    = litRgb[2];
        }
    }

    // 2. Ballistic gravity-driven blood droplets (entry splash + exit spray)
    if (cgs.media.bloodMistShader) {
        numDrops = large ? (goreLevel >= 2 ? 16 : 11) : (goreLevel >= 2 ? 11 : 7);
        for (i = 0; i < numDrops; i++) {
            cgGoreParticle_t *p = CG_AllocGoreParticle();
            float ang = CG_GoreRand01() * 6.2831853f;
            qboolean exitSpray = (i % 3 != 0) ? qtrue : qfalse;
            float dirSign = exitSpray ? -1.0f : 1.0f;
            float fwdSpd  = (exitSpray ? (95.0f + CG_GoreRand01() * 155.0f) : (55.0f + CG_GoreRand01() * 105.0f)) * (large ? 1.20f : 1.0f);
            float spread  = (28.0f + CG_GoreRand01() * 64.0f) * (large ? 1.15f : 1.0f);

            p->isMist    = qfalse;
            p->canSplat  = ((i & 1) == 0 && cgs.media.bloodSplatShaders[0] != 0) ? qtrue : qfalse;
            p->startTime = cg.time;
            p->lifeTime  = 450 + (int)(CG_GoreRand01() * 480.0f);

            VectorMA(pos, dirSign * 2.5f, n, p->pos);
            VectorScale(n, dirSign * fwdSpd, p->vel);
            VectorMA(p->vel, cosf(ang) * spread, right, p->vel);
            VectorMA(p->vel, sinf(ang) * spread, up, p->vel);
            p->vel[2] += 26.0f + CG_GoreRand01() * 72.0f;

            p->startSize = (2.1f + CG_GoreRand01() * 1.9f) * (large ? 1.20f : 1.0f);
            p->endSize   = p->startSize * 0.72f;
            p->alpha     = 0.90f;
            p->rotCos    = cosf(ang);
            p->rotSin    = sinf(ang);
            p->rgb[0]    = litRgb[0];
            p->rgb[1]    = litRgb[1];
            p->rgb[2]    = litRgb[2];
        }
    }

    // 3. Exit-wound wall/surface splatter behind struck soldier (world-only trace passes through victim)
    if (cgs.media.bloodSplatShaders[0]) {
        int numTraces = (large || goreLevel >= 2) ? 2 : 1;
        for (i = 0; i < numTraces; i++) {
            vec3_t start, end, rayDir;
            float  traceDist = (i == 0) ? 124.0f : 96.0f;

            VectorNegate(n, rayDir);
            if (i > 0) {
                VectorMA(rayDir, CG_GoreRandSigned() * 0.28f, right, rayDir);
                VectorMA(rayDir, CG_GoreRandSigned() * 0.24f - 0.08f, up, rayDir);
                VectorNormalize(rayDir);
            }

            VectorMA(pos, 2.0f, n, start);
            VectorMA(start, traceDist, rayDir, end);

            CG_Trace(&tr, start, vec3_origin, vec3_origin, end, 0, MASK_SOLID, qfalse, qfalse, "CG_GoreWallSplat");
            if (tr.fraction < 1.0f && tr.fraction > 0.01f && !tr.startsolid && !(tr.surfaceFlags & SURF_SKY)) {
                float proxBoost = 1.0f + (1.0f - tr.fraction) * 0.45f;
                float radius = ((i == 0) ? (10.5f + CG_GoreRand01() * 6.5f) : (6.5f + CG_GoreRand01() * 4.5f)) * proxBoost * (goreLevel >= 2 ? 1.28f : 1.0f);
                qhandle_t splatShader = cgs.media.bloodSplatShaders[(s_goreRngState >> 4) & 1];
                if (large) {
                    radius *= 1.22f;
                }
                CG_PlaceBloodDecal(splatShader, tr.endpos, tr.plane.normal, radius, 0.90f, qfalse);

                // Liquid gravity run-down on vertical walls: subtle downward drip streak + slow trickling bead
                if (fabsf(tr.plane.normal[2]) < 0.35f) {
                    vec3_t dripPos;
                    VectorCopy(tr.endpos, dripPos);
                    dripPos[2] -= radius * 0.58f;
                    CG_ImpactMark(
                        cgs.media.bloodPoolShader ? cgs.media.bloodPoolShader : splatShader,
                        dripPos,
                        tr.plane.normal,
                        0.0f,
                        radius * 0.24f,
                        radius * 1.15f,
                        0.90f,
                        0.90f,
                        0.90f,
                        0.84f,
                        qtrue,
                        qfalse,
                        qtrue,
                        qtrue,
                        0.5f,
                        0.5f
                    );

                    if (cgs.media.bloodMistShader) {
                        int d;
                        int numDrips = (large || goreLevel >= 2) ? 2 : 1;
                        for (d = 0; d < numDrips; d++) {
                            cgGoreParticle_t *dp = CG_AllocGoreParticle();
                            dp->isMist     = qfalse;
                            dp->isWallDrip = qtrue;
                            dp->canSplat   = qfalse;
                            dp->startTime  = cg.time;
                            dp->lifeTime   = 900 + (int)(CG_GoreRand01() * 650.0f);
                            VectorMA(tr.endpos, 0.8f, tr.plane.normal, dp->pos);
                            dp->pos[0]    += CG_GoreRandSigned() * (radius * 0.25f);
                            dp->pos[1]    += CG_GoreRandSigned() * (radius * 0.25f);
                            dp->pos[2]    -= CG_GoreRand01() * (radius * 0.35f);
                            VectorSet(dp->vel, 0.0f, 0.0f, -(11.0f + CG_GoreRand01() * 14.0f));
                            dp->startSize  = 1.6f + CG_GoreRand01() * 1.0f;
                            dp->endSize    = 0.9f;
                            dp->alpha      = 0.88f;
                            dp->rotCos     = 1.0f;
                            dp->rotSin     = 0.0f;
                            dp->rgb[0]     = litRgb[0];
                            dp->rgb[1]     = litRgb[1];
                            dp->rgb[2]     = litRgb[2];
                        }
                    }
                }
            }
        }
    }

    // 4. Seeping ground blood pool beneath struck/fallen soldier (rate-limited per spot)
    if (cgs.media.bloodPoolShader) {
        vec3_t downEnd;
        VectorCopy(pos, downEnd);
        downEnd[0] += CG_GoreRandSigned() * 8.0f;
        downEnd[1] += CG_GoreRandSigned() * 8.0f;
        downEnd[2] -= 86.0f;

        CG_Trace(&tr, pos, vec3_origin, vec3_origin, downEnd, 0, MASK_SOLID, qfalse, qfalse, "CG_GoreFloorPool");
        if (tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal[2] > 0.65f && !(tr.surfaceFlags & SURF_SKY)) {
            qboolean tooRecent = qfalse;
            for (i = 0; i < 4; i++) {
                if (s_gorePoolTime[i] > 0 && (cg.time - s_gorePoolTime[i]) < 550) {
                    vec3_t d;
                    VectorSubtract(tr.endpos, s_gorePoolPos[i], d);
                    if (DotProduct(d, d) < (18.0f * 18.0f)) {
                        tooRecent = qtrue;
                        break;
                    }
                }
            }

            if (!tooRecent) {
                float poolRadius = (large ? (15.0f + CG_GoreRand01() * 7.0f) : (10.5f + CG_GoreRand01() * 5.5f))
                                   * (goreLevel >= 2 ? 1.30f : 1.0f);
                VectorCopy(tr.endpos, s_gorePoolPos[s_gorePoolNext]);
                s_gorePoolTime[s_gorePoolNext] = cg.time;
                s_gorePoolNext = (s_gorePoolNext + 1) & 3;

                CG_PlaceBloodDecal(cgs.media.bloodPoolShader, tr.endpos, tr.plane.normal, poolRadius, 0.88f, qtrue);
            }
        }
    }
}

void CG_SpawnGunSmoke(const vec3_t pos, const vec3_t vel, float heatFrac)
{
    cgGoreParticle_t *p;
    float             ang;
    vec3_t            vLight;
    int               i;

    if (!cg_gunSmoke || cg_gunSmoke->integer <= 0 || !cgs.media.gunSmokeShader) {
        return;
    }

    p            = CG_AllocGoreParticle();
    ang          = CG_GoreRand01() * 6.2831853f;
    p->isMist    = qtrue;
    p->isGunSmoke = qtrue;
    p->canSplat  = qfalse;
    p->startTime = cg.time;
    p->lifeTime  = 520 + (int)(CG_GoreRand01() * 340.0f);

    VectorCopy(pos, p->pos);
    VectorCopy(vel, p->vel);
    p->vel[0] += CG_GoreRandSigned() * 5.0f;
    p->vel[1] += CG_GoreRandSigned() * 5.0f;
    p->vel[2] += 13.0f + CG_GoreRand01() * 9.0f;

    p->startSize = 1.6f + heatFrac * 0.9f;
    p->endSize   = 6.8f + heatFrac * 4.5f;
    p->alpha     = 0.14f + Com_Clamp(0.0f, 0.16f, heatFrac * 0.15f);
    p->rotCos    = cosf(ang);
    p->rotSin    = sinf(ang);

    cgi.R_GetLightingForDecal(vLight, vec_upwards, pos);
    for (i = 0; i < 3; i++) {
        float c = vLight[i];
        if (c < 95.0f) c = 95.0f;
        else if (c > 240.0f) c = 240.0f;
        p->rgb[i] = (byte)c;
    }
}

void CG_SpawnImpactSparks(const vec3_t pos, const vec3_t norm, qboolean metallic, qboolean large)
{
    vec3_t n, right, up;
    int    count, i;

    if (!cg_impactSparks || cg_impactSparks->integer <= 0 || !cgs.media.gunSmokeShader) {
        return;
    }

    VectorCopy(norm, n);
    if (VectorNormalize(n) < 0.001f) {
        VectorSet(n, 0.0f, 0.0f, 1.0f);
    }
    PerpendicularVector(right, n);
    CrossProduct(n, right, up);

    count = metallic ? (large ? 14 : 9) : (large ? 8 : 5);
    for (i = 0; i < count; i++) {
        cgGoreParticle_t *p = CG_AllocGoreParticle();
        float ang    = CG_GoreRand01() * 6.2831853f;
        float fwdSpd = (120.0f + CG_GoreRand01() * (metallic ? 240.0f : 150.0f)) * (large ? 1.2f : 1.0f);
        float spread = (45.0f + CG_GoreRand01() * 145.0f);

        p->isMist    = qfalse;
        p->isSpark   = qtrue;
        p->canSplat  = qfalse;
        p->startTime = cg.time;
        p->lifeTime  = 180 + (int)(CG_GoreRand01() * (metallic ? 360.0f : 230.0f));

        VectorMA(pos, 1.5f, n, p->pos);
        VectorScale(n, fwdSpd, p->vel);
        VectorMA(p->vel, cosf(ang) * spread, right, p->vel);
        VectorMA(p->vel, sinf(ang) * spread, up, p->vel);
        p->vel[2] += 35.0f + CG_GoreRand01() * 85.0f;

        p->startSize = metallic ? (1.4f + CG_GoreRand01() * 1.1f) : (1.1f + CG_GoreRand01() * 0.8f);
        p->endSize   = 0.45f;
        p->alpha     = 0.96f;
        p->rotCos    = 1.0f;
        p->rotSin    = 0.0f;
        p->rgb[0]    = 255;
        p->rgb[1]    = (byte)(metallic ? (185 + (int)(CG_GoreRand01() * 60.0f)) : (145 + (int)(CG_GoreRand01() * 55.0f)));
        p->rgb[2]    = (byte)(metallic ? (75 + (int)(CG_GoreRand01() * 65.0f)) : 40);
    }
}

void CG_AddGoreParticles(void)
{
    int        i;
    float      dt;
    vec3_t     camLeft, camUp;
    polyVert_t verts[4];

    if ((!cg_gore || cg_gore->integer <= 0)
        && (!cg_gunSmoke || cg_gunSmoke->integer <= 0)
        && (!cg_impactSparks || cg_impactSparks->integer <= 0)) {
        s_goreLastTime = cg.time;
        return;
    }

    CG_AddBodyWounds();
    CG_UpdateCorpseBloodPools();

    if (!cgs.media.bloodMistShader && !cgs.media.gunSmokeShader) {
        s_goreLastTime = cg.time;
        return;
    }

    if (s_goreLastTime <= 0 || cg.time < s_goreLastTime) {
        dt = 0.0f;
    } else {
        dt = (float)(cg.time - s_goreLastTime) * 0.001f;
        if (dt > 0.05f) {
            dt = 0.05f;
        }
    }
    s_goreLastTime = cg.time;

    VectorCopy(cg.refdef.viewaxis[1], camLeft);
    VectorCopy(cg.refdef.viewaxis[2], camUp);

    for (i = 0; i < MAX_GORE_PARTICLES; i++) {
        cgGoreParticle_t *p = &s_goreParticles[i];
        float             frac, size, alpha;
        vec3_t            axisR, axisU;
        byte              aByte;

        if (!p->active) {
            continue;
        }

        if (cg.time < p->startTime || (cg.time - p->startTime) >= p->lifeTime) {
            p->active = qfalse;
            continue;
        }

        frac = (float)(cg.time - p->startTime) / (float)p->lifeTime;

        if (dt > 0.0f) {
            if (p->isGunSmoke) {
                float drag = 1.0f - 2.4f * dt;
                if (drag < 0.4f) {
                    drag = 0.4f;
                }
                VectorScale(p->vel, drag, p->vel);
                p->vel[2] += 14.0f * dt;
                VectorMA(p->pos, dt, p->vel, p->pos);
            } else if (p->isMist) {
                float drag = 1.0f - 4.2f * dt;
                if (drag < 0.25f) {
                    drag = 0.25f;
                }
                VectorScale(p->vel, drag, p->vel);
                p->vel[2] -= 42.0f * dt;
                VectorMA(p->pos, dt, p->vel, p->pos);
            } else if (p->isSpark) {
                vec3_t prevPos;
                VectorCopy(p->pos, prevPos);
                p->vel[2] -= 740.0f * dt;
                VectorScale(p->vel, 1.0f - 0.35f * dt, p->vel);
                VectorMA(p->pos, dt, p->vel, p->pos);
                // Cool spark color from white-gold to glowing amber-orange
                if (p->rgb[1] > 70) {
                    p->rgb[1] = (byte)(p->rgb[1] > (int)(260.0f * dt) ? p->rgb[1] - (int)(260.0f * dt) : 70);
                }
                if (p->rgb[2] > 15) {
                    p->rgb[2] = (byte)(p->rgb[2] > (int)(240.0f * dt) ? p->rgb[2] - (int)(240.0f * dt) : 15);
                }
                if ((i & 1) == 0) {
                    trace_t tr;
                    CG_Trace(&tr, prevPos, vec3_origin, vec3_origin, p->pos, 0, MASK_SOLID, qfalse, qfalse, "CG_SparkBounce");
                    if (tr.fraction < 1.0f && !tr.startsolid) {
                        float vn = DotProduct(p->vel, tr.plane.normal);
                        VectorCopy(tr.endpos, p->pos);
                        VectorMA(p->vel, -1.55f * vn, tr.plane.normal, p->vel);
                        VectorScale(p->vel, 0.58f, p->vel);
                    }
                }
            } else if (p->isWallDrip) {
                // Slow viscous trickle straight down vertical wall
                p->vel[2] *= (1.0f - 0.55f * dt);
                VectorMA(p->pos, dt, p->vel, p->pos);
            } else {
                vec3_t prevPos;
                float  drag = 1.0f - 0.65f * dt;
                VectorCopy(p->pos, prevPos);
                if (drag < 0.5f) {
                    drag = 0.5f;
                }
                VectorScale(p->vel, drag, p->vel);
                p->vel[2] -= 660.0f * dt;
                VectorMA(p->pos, dt, p->vel, p->pos);

                if (p->canSplat) {
                    trace_t tr;
                    CG_Trace(&tr, prevPos, vec3_origin, vec3_origin, p->pos, 0, MASK_SOLID, qfalse, qfalse, "CG_GoreDropSplat");
                    if (tr.fraction < 1.0f) {
                        if (!tr.startsolid && !(tr.surfaceFlags & SURF_SKY) && cgs.media.bloodSplatShaders[0]) {
                            float dropRadius = (4.8f + CG_GoreRand01() * 4.2f) * ((cg_gore->integer >= 2) ? 1.25f : 1.0f);
                            CG_PlaceBloodDecal(
                                cgs.media.bloodSplatShaders[i & 1],
                                tr.endpos,
                                tr.plane.normal,
                                dropRadius,
                                0.86f,
                                qfalse
                            );
                        }
                        p->active = qfalse;
                        continue;
                    }
                }
            }
        }

        size = p->startSize + (p->endSize - p->startSize) * frac;
        if (p->isMist) {
            float fade = (1.0f - frac);
            alpha = p->alpha * fade * fade;
            // Rotated camera-facing billboard
            axisR[0] = (camLeft[0] * p->rotCos + camUp[0] * p->rotSin) * size;
            axisR[1] = (camLeft[1] * p->rotCos + camUp[1] * p->rotSin) * size;
            axisR[2] = (camLeft[2] * p->rotCos + camUp[2] * p->rotSin) * size;

            axisU[0] = (-camLeft[0] * p->rotSin + camUp[0] * p->rotCos) * size;
            axisU[1] = (-camLeft[1] * p->rotSin + camUp[1] * p->rotCos) * size;
            axisU[2] = (-camLeft[2] * p->rotSin + camUp[2] * p->rotCos) * size;
        } else {
            float vx = DotProduct(p->vel, camLeft);
            float vy = DotProduct(p->vel, camUp);
            float vLen = sqrtf(vx * vx + vy * vy);
            float c, s, stretch;
            alpha = p->alpha * (1.0f - frac * frac);

            if (vLen > 20.0f) {
                c = vx / vLen;
                s = vy / vLen;
                stretch = p->isSpark ? (1.6f + Com_Clamp(0.0f, 2.2f, vLen / 170.0f)) : (1.0f + Com_Clamp(0.0f, 0.85f, vLen / 260.0f));
            } else {
                c = p->rotCos;
                s = p->rotSin;
                stretch = 1.0f;
            }

            axisR[0] = (camLeft[0] * c + camUp[0] * s) * (size * stretch);
            axisR[1] = (camLeft[1] * c + camUp[1] * s) * (size * stretch);
            axisR[2] = (camLeft[2] * c + camUp[2] * s) * (size * stretch);

            axisU[0] = (-camLeft[0] * s + camUp[0] * c) * (size * (p->isSpark ? 0.42f : 0.78f));
            axisU[1] = (-camLeft[1] * s + camUp[1] * c) * (size * (p->isSpark ? 0.42f : 0.78f));
            axisU[2] = (-camLeft[2] * s + camUp[2] * c) * (size * (p->isSpark ? 0.42f : 0.78f));
        }

        if (alpha <= 0.01f) {
            continue;
        }

        aByte = (byte)(Com_Clamp(0.0f, 1.0f, alpha) * 255.0f);

        verts[0].xyz[0] = p->pos[0] - axisR[0] - axisU[0];
        verts[0].xyz[1] = p->pos[1] - axisR[1] - axisU[1];
        verts[0].xyz[2] = p->pos[2] - axisR[2] - axisU[2];
        verts[0].st[0]  = 0.0f;
        verts[0].st[1]  = 0.0f;

        verts[1].xyz[0] = p->pos[0] + axisR[0] - axisU[0];
        verts[1].xyz[1] = p->pos[1] + axisR[1] - axisU[1];
        verts[1].xyz[2] = p->pos[2] + axisR[2] - axisU[2];
        verts[1].st[0]  = 1.0f;
        verts[1].st[1]  = 0.0f;

        verts[2].xyz[0] = p->pos[0] + axisR[0] + axisU[0];
        verts[2].xyz[1] = p->pos[1] + axisR[1] + axisU[1];
        verts[2].xyz[2] = p->pos[2] + axisR[2] + axisU[2];
        verts[2].st[0]  = 1.0f;
        verts[2].st[1]  = 1.0f;

        verts[3].xyz[0] = p->pos[0] - axisR[0] + axisU[0];
        verts[3].xyz[1] = p->pos[1] - axisR[1] + axisU[1];
        verts[3].xyz[2] = p->pos[2] - axisR[2] + axisU[2];
        verts[3].st[0]  = 0.0f;
        verts[3].st[1]  = 1.0f;

        verts[0].modulate[0] = verts[1].modulate[0] = verts[2].modulate[0] = verts[3].modulate[0] = p->rgb[0];
        verts[0].modulate[1] = verts[1].modulate[1] = verts[2].modulate[1] = verts[3].modulate[1] = p->rgb[1];
        verts[0].modulate[2] = verts[1].modulate[2] = verts[2].modulate[2] = verts[3].modulate[2] = p->rgb[2];
        verts[0].modulate[3] = verts[1].modulate[3] = verts[2].modulate[3] = verts[3].modulate[3] = aByte;

        {
            qhandle_t hPartShader = (p->isGunSmoke || p->isSpark) ? cgs.media.gunSmokeShader : cgs.media.bloodMistShader;
            if (hPartShader) {
                cgi.R_AddPolyToScene(hPartShader, 4, verts, 0);
            }
        }
    }
}
