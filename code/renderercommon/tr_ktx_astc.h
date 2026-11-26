/*
===========================================================================
OpenMoHAA Remaster — Hardware ASTC (KTX1) Texture Loader
Shared by renderergl1 and renderergl2 (zero external dependencies).
Used only when the GPU advertises GL_KHR_texture_compression_astc_ldr
(e.g. Raspberry Pi 5 VideoCore VII / V3D 7.1, mobile/ARM GPUs) and
r_ext_compressed_astc is enabled. Silently falls back to JPG/TGA/DDS
otherwise and for any texture without a .ktx counterpart.
===========================================================================
*/
#ifndef TR_KTX_ASTC_H
#define TR_KTX_ASTC_H

#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR           0x93B0
#define GL_COMPRESSED_RGBA_ASTC_5x4_KHR           0x93B1
#define GL_COMPRESSED_RGBA_ASTC_5x5_KHR           0x93B2
#define GL_COMPRESSED_RGBA_ASTC_6x5_KHR           0x93B3
#define GL_COMPRESSED_RGBA_ASTC_6x6_KHR           0x93B4
#define GL_COMPRESSED_RGBA_ASTC_8x5_KHR           0x93B5
#define GL_COMPRESSED_RGBA_ASTC_8x6_KHR           0x93B6
#define GL_COMPRESSED_RGBA_ASTC_8x8_KHR           0x93B7
#define GL_COMPRESSED_RGBA_ASTC_10x5_KHR          0x93B8
#define GL_COMPRESSED_RGBA_ASTC_10x6_KHR          0x93B9
#define GL_COMPRESSED_RGBA_ASTC_10x8_KHR          0x93BA
#define GL_COMPRESSED_RGBA_ASTC_10x10_KHR         0x93BB
#define GL_COMPRESSED_RGBA_ASTC_12x10_KHR         0x93BC
#define GL_COMPRESSED_RGBA_ASTC_12x12_KHR         0x93BD
#endif

#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR   0x93D0
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR   0x93D1
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR   0x93D2
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR   0x93D3
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR   0x93D4
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR   0x93D5
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR   0x93D6
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR   0x93D7
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR  0x93D8
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR  0x93D9
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR  0x93DA
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR 0x93DB
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR 0x93DC
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR 0x93DD
#endif

static qboolean R_ASTCBlockDims(GLenum fmt, int *bw, int *bh)
{
	int w = 0, h = 0;

	switch (fmt) {
	case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:   w = 4;  h = 4;  break;
	case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:   w = 5;  h = 4;  break;
	case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:   w = 5;  h = 5;  break;
	case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:   w = 6;  h = 5;  break;
	case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:   w = 6;  h = 6;  break;
	case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:   w = 8;  h = 5;  break;
	case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:   w = 8;  h = 6;  break;
	case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:   w = 8;  h = 8;  break;
	case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:  w = 10; h = 5;  break;
	case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:  w = 10; h = 6;  break;
	case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:  w = 10; h = 8;  break;
	case GL_COMPRESSED_RGBA_ASTC_10x10_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR: w = 10; h = 10; break;
	case GL_COMPRESSED_RGBA_ASTC_12x10_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR: w = 12; h = 10; break;
	case GL_COMPRESSED_RGBA_ASTC_12x12_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR: w = 12; h = 12; break;
	default:
		return qfalse;
	}

	if (bw) *bw = w;
	if (bh) *bh = h;
	return qtrue;
}

static int R_ASTCMipSize(int width, int height, GLenum fmt)
{
	int bw, bh;
	if (!R_ASTCBlockDims(fmt, &bw, &bh)) {
		return 0;
	}
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	return ((width + bw - 1) / bw) * ((height + bh - 1) / bh) * 16;
}

static ID_INLINE const char *R_ASTCFormatName(GLenum fmt)
{
	switch (fmt) {
	case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:   return "ASTC4x4";
	case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:   return "ASTC5x5";
	case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:   return "ASTC6x6";
	case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:
	case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:   return "ASTC8x8";
	default:                                        return "ASTC   ";
	}
}

static unsigned int R_KtxReadU32LE(const byte *p)
{
	return (unsigned int)p[0]
		| ((unsigned int)p[1] << 8)
		| ((unsigned int)p[2] << 16)
		| ((unsigned int)p[3] << 24);
}

static qboolean R_LoadKTX1_ASTC(
	const char *baseName,
	byte **outPic,
	int *outWidth,
	int *outHeight,
	GLenum *outFormat,
	int *outNumMips,
	qboolean *outHasAlpha)
{
	static const byte ktxMagic[12] = {
		0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
	};
	char ktxName[MAX_QPATH];
	union {
		byte *b;
		void *v;
	} buf;
	long len;
	unsigned int endianness, glType, glFormat, glInternalFormat;
	unsigned int pixelWidth, pixelHeight, pixelDepth;
	unsigned int numArrayElems, numFaces, numMips, bytesOfKV;
	unsigned int ofs, totalBytes, m;
	int mw, mh;
	qboolean hasAlpha = qtrue;
	byte *dst, *writePtr;

	if (!baseName || !baseName[0] || !outPic) {
		return qfalse;
	}

	COM_StripExtension(baseName, ktxName, sizeof(ktxName));
	if (!ktxName[0]) {
		return qfalse;
	}
	Q_strcat(ktxName, sizeof(ktxName), ".ktx");

	buf.v = NULL;
	len = ri.FS_ReadFileEx(ktxName, &buf.v, qtrue);
	if (len < 64 || !buf.b) {
		if (buf.v) {
			ri.FS_FreeFile(buf.v);
		}
		return qfalse;
	}

	if (memcmp(buf.b, ktxMagic, 12) != 0) {
		ri.FS_FreeFile(buf.v);
		return qfalse;
	}

	endianness       = R_KtxReadU32LE(buf.b + 12);
	glType           = R_KtxReadU32LE(buf.b + 16);
	glFormat         = R_KtxReadU32LE(buf.b + 24);
	glInternalFormat = R_KtxReadU32LE(buf.b + 28);
	pixelWidth       = R_KtxReadU32LE(buf.b + 36);
	pixelHeight      = R_KtxReadU32LE(buf.b + 40);
	pixelDepth       = R_KtxReadU32LE(buf.b + 44);
	numArrayElems    = R_KtxReadU32LE(buf.b + 48);
	numFaces         = R_KtxReadU32LE(buf.b + 52);
	numMips          = R_KtxReadU32LE(buf.b + 56);
	bytesOfKV        = R_KtxReadU32LE(buf.b + 60);

	if (endianness != 0x04030201u
		|| glType != 0 || glFormat != 0
		|| !R_ASTCBlockDims((GLenum)glInternalFormat, NULL, NULL)
		|| pixelWidth < 1 || pixelWidth > 16384
		|| pixelHeight < 1 || pixelHeight > 16384
		|| pixelDepth != 0
		|| numArrayElems > 1
		|| numFaces != 1
		|| numMips < 1 || numMips > 16
		|| 64u + bytesOfKV > (unsigned int)len) {
		ri.FS_FreeFile(buf.v);
		return qfalse;
	}

	// Optional key-value metadata scan (hasAlpha)
	ofs = 64u;
	while (ofs + 4u <= 64u + bytesOfKV) {
		unsigned int kvSize = R_KtxReadU32LE(buf.b + ofs);
		unsigned int padded = (kvSize + 3u) & ~3u;
		ofs += 4u;
		if (kvSize == 0 || ofs + kvSize > 64u + bytesOfKV) {
			break;
		}
		if (kvSize >= 10u && memcmp(buf.b + ofs, "hasAlpha\0", 9) == 0) {
			hasAlpha = (buf.b[ofs + 9] != '0') ? qtrue : qfalse;
		}
		ofs += padded;
	}

	// Pass 1: validate every mip level size and sum tightly-packed size
	ofs = 64u + bytesOfKV;
	totalBytes = 0u;
	mw = (int)pixelWidth;
	mh = (int)pixelHeight;
	for (m = 0; m < numMips; m++) {
		unsigned int imgSize, expected, pad;
		if (ofs + 4u > (unsigned int)len) {
			ri.FS_FreeFile(buf.v);
			return qfalse;
		}
		imgSize = R_KtxReadU32LE(buf.b + ofs);
		ofs += 4u;
		expected = (unsigned int)R_ASTCMipSize(mw, mh, (GLenum)glInternalFormat);
		pad = (3u - ((imgSize + 3u) & 3u));
		if (expected == 0 || imgSize != expected || ofs + imgSize > (unsigned int)len) {
			ri.FS_FreeFile(buf.v);
			return qfalse;
		}
		totalBytes += imgSize;
		ofs += imgSize + pad;
		mw = (mw > 1) ? (mw >> 1) : 1;
		mh = (mh > 1) ? (mh >> 1) : 1;
	}

	dst = (byte *)ri.Malloc((int)totalBytes);
	if (!dst) {
		ri.FS_FreeFile(buf.v);
		return qfalse;
	}

	// Pass 2: copy tightly-packed mip chain
	ofs = 64u + bytesOfKV;
	writePtr = dst;
	for (m = 0; m < numMips; m++) {
		unsigned int imgSize = R_KtxReadU32LE(buf.b + ofs);
		unsigned int pad = (3u - ((imgSize + 3u) & 3u));
		ofs += 4u;
		Com_Memcpy(writePtr, buf.b + ofs, imgSize);
		writePtr += imgSize;
		ofs += imgSize + pad;
	}

	ri.FS_FreeFile(buf.v);

	*outPic = dst;
	if (outWidth)    *outWidth    = (int)pixelWidth;
	if (outHeight)   *outHeight   = (int)pixelHeight;
	if (outFormat)   *outFormat   = (GLenum)glInternalFormat;
	if (outNumMips)  *outNumMips  = (int)numMips;
	if (outHasAlpha) *outHasAlpha = hasAlpha;
	return qtrue;
}

#endif /* TR_KTX_ASTC_H */
