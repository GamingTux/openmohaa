/*
===========================================================================
OpenMoHAA Remaster - Procedural Blood & Gore Texture Generator
Synthesizes 4 high-detail 128x128 RGBA blood textures in memory at startup
(2 organic splatters, 1 ground pool, 1 fine mist/droplet puff) with zero
external asset dependencies for both OpenGL 1 and OpenGL 2 renderers.
===========================================================================
*/
#pragma once

#include <math.h>
#include <string.h>

#define REMASTER_BLOOD_TEX_SIZE 128

static float R_GoreHash2D(int ix, int iy, int seed)
{
	unsigned int n = (unsigned int)ix * 374761393u
	               + (unsigned int)iy * 668265263u
	               + (unsigned int)seed * 1442695041u;
	n = (n ^ (n >> 13)) * 1274126177u;
	n = n ^ (n >> 16);
	return (float)(n & 0xFFFFu) / 65535.0f;
}

static float R_GoreValueNoise(float x, float y, int seed)
{
	int ix = (int)floorf(x);
	int iy = (int)floorf(y);
	float fx = x - (float)ix;
	float fy = y - (float)iy;
	float ux = fx * fx * (3.0f - 2.0f * fx);
	float uy = fy * fy * (3.0f - 2.0f * fy);
	float a = R_GoreHash2D(ix,     iy,     seed);
	float b = R_GoreHash2D(ix + 1, iy,     seed);
	float c = R_GoreHash2D(ix,     iy + 1, seed);
	float d = R_GoreHash2D(ix + 1, iy + 1, seed);
	float ab = a + (b - a) * ux;
	float cd = c + (d - c) * ux;
	return ab + (cd - ab) * uy;
}

static float R_GoreFbm(float x, float y, int seed)
{
	float v = 0.0f;
	v += 0.56f * R_GoreValueNoise(x,        y,        seed);
	v += 0.29f * R_GoreValueNoise(x * 2.07f, y * 2.07f, seed + 19);
	v += 0.15f * R_GoreValueNoise(x * 4.21f, y * 4.21f, seed + 47);
	return v;
}

static float R_GoreSmoothstep(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

static void R_GenerateBloodTexture(int variant, unsigned char *rgba)
{
	const int S = REMASTER_BLOOD_TEX_SIZE;
	int x, y;
	int seed = 101 + variant * 313;

	// Deterministic satellite droplets for variants 0, 1 (splats) and 3 (mist)
	float dropX[28], dropY[28], dropRx[28], dropRy[28], dropCos[28], dropSin[28];
	int numDrops = (variant <= 1) ? 26 : ((variant == 3) ? 8 : 0);
	int i;

	for (i = 0; i < numDrops; i++)
	{
		float ang = R_GoreHash2D(i, 1, seed) * 6.2831853f;
		float rad = (variant == 3)
			? (0.14f + 0.52f * R_GoreHash2D(i, 2, seed))
			: (0.28f + 0.56f * R_GoreHash2D(i, 2, seed));
		float sz = (variant == 3)
			? (0.020f + 0.026f * R_GoreHash2D(i, 3, seed))
			: (0.014f + 0.036f * R_GoreHash2D(i, 3, seed));
		float stretch = (variant == 3)
			? (1.15f + 0.45f * R_GoreHash2D(i, 4, seed))
			: (1.20f + 1.55f * R_GoreHash2D(i, 4, seed));

		dropCos[i] = cosf(ang);
		dropSin[i] = sinf(ang);
		dropX[i]   = dropCos[i] * rad;
		dropY[i]   = dropSin[i] * rad;
		dropRx[i]  = sz * stretch;
		dropRy[i]  = sz;
	}

	for (y = 0; y < S; y++)
	{
		float ny = ((float)y + 0.5f) / (float)S * 2.0f - 1.0f;
		for (x = 0; x < S; x++)
		{
			float nx = ((float)x + 0.5f) / (float)S * 2.0f - 1.0f;
			float r = sqrtf(nx * nx + ny * ny);
			float a = atan2f(ny, nx);
			float borderFade = 1.0f - R_GoreSmoothstep(0.84f, 0.96f, r);
			float density = 0.0f;
			float alpha = 0.0f;
			float cr = 0.0f, cg = 0.0f, cb = 0.0f;
			unsigned char *out = rgba + (y * S + x) * 4;

			if (x < 2 || x >= S - 2 || y < 2 || y >= S - 2 || borderFade <= 0.001f)
			{
				out[0] = 64; out[1] = 4; out[2] = 4; out[3] = 0;
				continue;
			}

			if (variant == 0 || variant == 1)
			{
				// Photorealistic gritty arterial wall/floor splatter with frayed edges & fine micro-spatter
				float p1 = (variant == 0) ? 0.4f : 2.1f;
				float p2 = (variant == 0) ? 1.7f : 4.3f;
				float p3 = (variant == 0) ? 3.2f : 0.9f;
				float fbm  = R_GoreFbm(nx * 4.4f + 5.0f, ny * 4.4f + 5.0f, seed);
				float fine = R_GoreFbm(nx * 11.5f + 13.0f, ny * 11.5f + 13.0f, seed + 71);
				float angWarp = 0.13f * cosf(3.0f * a + p1)
				              + 0.09f * sinf(5.0f * a + p2)
				              + 0.06f * cosf(8.0f * a + p3)
				              + 0.04f * sinf(13.0f * a + p1 * 2.0f);
				float coreRad = 0.36f + angWarp + (fbm - 0.5f) * 0.36f + (fine - 0.5f) * 0.12f;
				float core = 1.0f - R_GoreSmoothstep(coreRad - 0.10f, coreRad + 0.06f, r);

				// Secondary off-center splash lobe for natural asymmetry
				float lx = nx - ((variant == 0) ? 0.15f : -0.17f);
				float ly = ny - ((variant == 0) ? -0.11f : 0.13f);
				float lr = sqrtf(lx * lx + ly * ly);
				float lobe = 1.0f - R_GoreSmoothstep(0.18f + (fbm - 0.5f) * 0.18f, 0.28f + (fbm - 0.5f) * 0.18f, lr);

				// Fine high-velocity micro-speckle spray field around core
				float sprayMask = (1.0f - R_GoreSmoothstep(0.20f, 0.86f, r)) * R_GoreSmoothstep(0.66f, 0.84f, fine);

				float drops = 0.0f;
				for (i = 0; i < numDrops; i++)
				{
					float dx = nx - dropX[i];
					float dy = ny - dropY[i];
					float ca = dropCos[i];
					float sa = dropSin[i];
					float u = (dx * ca + dy * sa) / dropRx[i];
					float v = (-dx * sa + dy * ca) / dropRy[i];
					float d2 = u * u + v * v;
					if (d2 < 1.55f)
					{
						float frayed = sqrtf(d2) + (fine - 0.5f) * 0.42f;
						float dd = 1.0f - R_GoreSmoothstep(0.45f, 1.08f, frayed);
						if (dd > drops) drops = dd;
					}
				}

				density = core;
				if (lobe * 0.88f > density) density = lobe * 0.88f;
				if (drops * 0.94f > density) density = drops * 0.94f;
				if (sprayMask * 0.78f > density) density = sprayMask * 0.78f;
				density *= borderFade;

				// Gritty deep crimson: darker pooled center, natural translucent feathered edge (no cartoon outline!)
				{
					float thick = R_GoreSmoothstep(0.18f, 0.92f, density) * (0.65f + 0.35f * fbm);
					cr = 98.0f - 30.0f * thick + (fine - 0.5f) * 20.0f;
					cg = 6.5f  - 3.0f  * thick;
					cb = 5.5f  - 2.0f  * thick;
					alpha = R_GoreSmoothstep(0.05f, 0.48f, density) * (0.76f + 0.16f * fbm);
				}
			}
			else if (variant == 2)
			{
				// Smooth multi-lobed dark venous blood puddle for ground beneath fallen soldiers
				float fbm = R_GoreFbm(nx * 2.6f + 9.0f, ny * 2.6f + 9.0f, seed);
				float angWarp = 0.12f * cosf(2.0f * a + 0.8f)
				              + 0.08f * sinf(3.0f * a + 2.4f)
				              + 0.05f * cosf(5.0f * a + 1.1f);
				float poolRad = 0.68f + angWarp + (fbm - 0.5f) * 0.20f;
				density = (1.0f - R_GoreSmoothstep(poolRad - 0.16f, poolRad + 0.06f, r)) * borderFade;

				{
					float thick = R_GoreSmoothstep(0.15f, 0.90f, density);
					// Darker deep venous center, slightly clearer burgundy toward thin perimeter
					cr = 92.0f - 30.0f * thick + (fbm - 0.5f) * 18.0f;
					cg = 7.0f  - 3.5f  * thick;
					cb = 6.0f  - 2.5f  * thick;
					alpha = R_GoreSmoothstep(0.04f, 0.42f, density) * 0.88f;
				}
			}
			else if (variant == 3)
			{
				// Fine turbulent 3D blood-spray mist puff + crisp micro-droplets
				float fbm = R_GoreFbm(nx * 3.4f + 2.0f, ny * 3.4f + 2.0f, seed);
				float radial = expf(-r * r * 3.8f);
				float mist = radial * (0.45f + 0.85f * fbm) * borderFade;

				float drops = 0.0f;
				for (i = 0; i < numDrops; i++)
				{
					float dx = nx - dropX[i];
					float dy = ny - dropY[i];
					float ca = dropCos[i];
					float sa = dropSin[i];
					float u = (dx * ca + dy * sa) / dropRx[i];
					float v = (-dx * sa + dy * ca) / dropRy[i];
					float d2 = u * u + v * v;
					if (d2 < 1.25f)
					{
						float dd = 1.0f - R_GoreSmoothstep(0.35f, 1.05f, sqrtf(d2));
						if (dd > drops) drops = dd;
					}
				}

				density = mist * 0.78f + drops * 0.75f;
				if (density > 1.0f) density = 1.0f;

				cr = 108.0f + 36.0f * fbm - 24.0f * drops;
				cg = 9.0f   + 5.0f  * fbm;
				cb = 8.0f   + 4.0f  * fbm;
				alpha = R_GoreSmoothstep(0.05f, 0.72f, density) * 0.86f;
			}
			else
			{
				// Variant 4: Soft wispy gun-barrel heat smoke puff (neutral pale gray-white)
				float fbm = R_GoreFbm(nx * 3.0f + 7.0f, ny * 3.0f + 7.0f, seed);
				float radial = expf(-r * r * 4.4f);
				density = radial * (0.35f + 0.90f * fbm) * borderFade;
				cr = 212.0f + 24.0f * fbm;
				cg = 214.0f + 24.0f * fbm;
				cb = 220.0f + 24.0f * fbm;
				alpha = R_GoreSmoothstep(0.04f, 0.85f, density) * 0.62f;
			}

			if (cr < 0.0f) cr = 0.0f; else if (cr > 255.0f) cr = 255.0f;
			if (cg < 0.0f) cg = 0.0f; else if (cg > 255.0f) cg = 255.0f;
			if (cb < 0.0f) cb = 0.0f; else if (cb > 255.0f) cb = 255.0f;
			if (alpha < 0.0f) alpha = 0.0f; else if (alpha > 1.0f) alpha = 1.0f;

			out[0] = (unsigned char)cr;
			out[1] = (unsigned char)cg;
			out[2] = (unsigned char)cb;
			out[3] = (unsigned char)(alpha * 255.0f);
		}
	}
}
