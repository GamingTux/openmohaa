/*
===========================================================================
OpenMoHAA Remaster — HD Texture Logical Original Dimensions Table
Maps upscaled HD textures (2x/4x in zzz_hd_remaster.pk3 / zzz_hd_astc.pk3)
back to their original pre-upscale pixel dimensions (origW, origH) so UI
widgets (sliders, ammo bars, checkboxes, pulldowns, tiled frames) and 3D
sprites retain exact 1x layout proportions while rendering at full HD res.
===========================================================================
*/
#ifndef TR_HD_DIMS_H
#define TR_HD_DIMS_H

#define HD_DIMS_MAX_ENTRIES 8192
#define HD_DIMS_HASH_SIZE   4096

typedef struct hdDimEntry_s {
	char name[MAX_QPATH];
	short origW;
	short origH;
	int next;
} hdDimEntry_t;

static hdDimEntry_t s_hdDimsEntries[HD_DIMS_MAX_ENTRIES];
static int          s_hdDimsHash[HD_DIMS_HASH_SIZE];
static int          s_hdDimsCount  = 0;
static qboolean     s_hdDimsLoaded = qfalse;

static unsigned int R_HDDimsHashName(const char *s)
{
	unsigned int h = 2166136261u;
	while (*s) {
		h ^= (unsigned int)(byte)(*s++);
		h *= 16777619u;
	}
	return h & (HD_DIMS_HASH_SIZE - 1);
}

static void R_HDDimsNormalizeStem(const char *src, char *dst, size_t dstSize)
{
	char stripped[MAX_QPATH];
	size_t i;

	COM_StripExtension(src, stripped, sizeof(stripped));
	for (i = 0; stripped[i] && i + 1 < dstSize; i++) {
		char c = stripped[i];
		if (c == '\\') {
			c = '/';
		} else if (c >= 'A' && c <= 'Z') {
			c = (char)(c + ('a' - 'A'));
		}
		dst[i] = c;
	}
	dst[i] = '\0';
}

static void R_ResetHDOrigDims(void)
{
	s_hdDimsCount  = 0;
	s_hdDimsLoaded = qfalse;
}

static void R_LoadHDOrigDims(void)
{
	union {
		char *c;
		void *v;
	} buf;
	long len;
	int i;
	const char *p, *end;

	s_hdDimsLoaded = qtrue;
	s_hdDimsCount  = 0;
	for (i = 0; i < HD_DIMS_HASH_SIZE; i++) {
		s_hdDimsHash[i] = -1;
	}

	buf.v = NULL;
	len = ri.FS_ReadFileEx("hd_orig_dims.txt", &buf.v, qtrue);
	if (len <= 0 || !buf.c) {
		if (buf.v) {
			ri.FS_FreeFile(buf.v);
		}
		return;
	}

	p = buf.c;
	end = buf.c + len;
	while (p < end && s_hdDimsCount < HD_DIMS_MAX_ENTRIES) {
		char rawStem[MAX_QPATH];
		char normStem[MAX_QPATH];
		int w = 0, h = 0;
		size_t n = 0;

		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
			p++;
		}
		if (p >= end) {
			break;
		}
		if (*p == '#' || *p == '/') {
			while (p < end && *p != '\n') p++;
			continue;
		}

		// Read texture stem token
		while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
			if (n + 1 < sizeof(rawStem)) {
				rawStem[n++] = *p;
			}
			p++;
		}
		rawStem[n] = '\0';

		// Read origW
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		while (p < end && *p >= '0' && *p <= '9') {
			w = w * 10 + (*p - '0');
			p++;
		}

		// Read origH
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		while (p < end && *p >= '0' && *p <= '9') {
			h = h * 10 + (*p - '0');
			p++;
		}

		// Skip rest of line
		while (p < end && *p != '\n') p++;

		if (rawStem[0] && w > 0 && w <= 16384 && h > 0 && h <= 16384) {
			unsigned int bucket;
			hdDimEntry_t *e = &s_hdDimsEntries[s_hdDimsCount];

			R_HDDimsNormalizeStem(rawStem, normStem, sizeof(normStem));
			Q_strncpyz(e->name, normStem, sizeof(e->name));
			e->origW = (short)w;
			e->origH = (short)h;

			bucket = R_HDDimsHashName(normStem);
			e->next = s_hdDimsHash[bucket];
			s_hdDimsHash[bucket] = s_hdDimsCount;
			s_hdDimsCount++;
		}
	}

	ri.FS_FreeFile(buf.v);
}

static void R_LookupHDOrigDims(const char *imgName, int loadedW, int loadedH, int *outW, int *outH)
{
	char norm[MAX_QPATH];
	unsigned int bucket;
	int idx;

	*outW = loadedW;
	*outH = loadedH;

	if (!imgName || !imgName[0] || imgName[0] == '*') {
		return;
	}

	if (!s_hdDimsLoaded) {
		R_LoadHDOrigDims();
	}
	if (s_hdDimsCount <= 0) {
		return;
	}

	R_HDDimsNormalizeStem(imgName, norm, sizeof(norm));
	bucket = R_HDDimsHashName(norm);
	for (idx = s_hdDimsHash[bucket]; idx >= 0; idx = s_hdDimsEntries[idx].next) {
		if (!strcmp(s_hdDimsEntries[idx].name, norm)) {
			int ow = (int)s_hdDimsEntries[idx].origW;
			int oh = (int)s_hdDimsEntries[idx].origH;
			// Only apply logical original size when the loaded texture is actually upscaled
			if (loadedW > ow || loadedH > oh) {
				*outW = ow;
				*outH = oh;
			}
			return;
		}
	}
}

#endif /* TR_HD_DIMS_H */
