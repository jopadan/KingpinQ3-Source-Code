/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_image.c
#include "tr_local.h"

static byte          s_intensitytable[ 256 ];
static unsigned char s_gammatable[ 256 ];

int                  gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int                  gl_filter_max = GL_LINEAR;

image_t              *r_imageHashTable[ IMAGE_FILE_HASH_SIZE ];

#define Tex_ByteToFloat(v) ( ( (int)(v) - 128 ) / 127.0f )
#define Tex_FloatToByte(v) ( 128 + (int) ( (v) * 127.0f + 0.5 ) )
//#define Tex_ByteToFloat(v) ( ( (float)(v) / 127.5f ) - 1.0f )
//#define Tex_FloatToByte(v) (byte)( roundf( ( (v) + 1.0f ) * 127.5f ) )

/*
** R_GammaCorrect
*/
void R_GammaCorrect( byte *buffer, int bufSize )
{
	int i;

	for ( i = 0; i < bufSize; i++ )
	{
		buffer[ i ] = s_gammatable[ buffer[ i ] ];
	}
}

typedef struct
{
	const char *name;
	int  minimize, maximize;
} textureMode_t;

static const textureMode_t modes[] =
{
	{ "GL_NEAREST",                GL_NEAREST,                GL_NEAREST },
	{ "GL_LINEAR",                 GL_LINEAR,                 GL_LINEAR  },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST",  GL_LINEAR_MIPMAP_NEAREST,  GL_LINEAR  },
	{ "GL_NEAREST_MIPMAP_LINEAR",  GL_NEAREST_MIPMAP_LINEAR,  GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR",   GL_LINEAR_MIPMAP_LINEAR,   GL_LINEAR  }
};

/*
================
return a hash value for the filename
================
*/
long GenerateImageHashValue( const char *fname )
{
	int  i;
	long hash;
	char letter;

//  ri.Printf(PRINT_ALL, "tr_image::GenerateImageHashValue: '%s'\n", fname);

	hash = 0;
	i = 0;

	while ( fname[ i ] != '\0' )
	{
		letter = tolower( fname[ i ] );

		//if(letter == '.')
		//  break;              // don't include extension

		if ( letter == '\\' )
		{
			letter = '/'; // damn path names
		}

		hash += ( long )( letter ) * ( i + 119 );
		i++;
	}

	hash &= ( IMAGE_FILE_HASH_SIZE - 1 );
	return hash;
}

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( const char *string )
{
	int     i;
	image_t *image;

	for ( i = 0; i < 6; i++ )
	{
		if ( !Q_stricmp( modes[ i ].name, string ) )
		{
			break;
		}
	}

	if ( i == 6 )
	{
		ri.Printf( PRINT_ALL, "bad filter name\n" );
		return;
	}

	gl_filter_min = modes[ i ].minimize;
	gl_filter_max = modes[ i ].maximize;

	// bound texture anisotropy
	if ( glConfig2.textureAnisotropyAvailable )
	{
		if ( r_ext_texture_filter_anisotropic->value > glConfig2.maxTextureAnisotropy )
		{
			ri.Cvar_Set( "r_ext_texture_filter_anisotropic", va( "%f", glConfig2.maxTextureAnisotropy ) );
		}
		else if ( r_ext_texture_filter_anisotropic->value < 1.0 )
		{
			ri.Cvar_Set( "r_ext_texture_filter_anisotropic", "1.0" );
		}
	}

	// change all the existing mipmap texture objects
	for ( i = 0; i < tr.images.currentElements; i++ )
	{
		image = (image_t*) Com_GrowListElement( &tr.images, i );

		if ( image->filterType == FT_DEFAULT)
		{
			GL_Bind( image );

			// set texture filter
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, gl_filter_min );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, gl_filter_max );

			// set texture anisotropy
			if ( glConfig2.textureAnisotropyAvailable )
			{
				glTexParameterf( image->type, GL_TEXTURE_MAX_ANISOTROPY_EXT, r_ext_texture_filter_anisotropic->value );
			}
		}
	}
}

/*
===============
R_SumOfUsedImages
===============
*/
int R_SumOfUsedImages( void )
{
	int     total;
	int     i;
	image_t *image;

	total = 0;

	for ( i = 0; i < tr.images.currentElements; i++ )
	{
		image = (image_t*) Com_GrowListElement( &tr.images, i );

		if ( image->frameUsed == tr.frameCount )
		{
			total += image->uploadWidth * image->uploadHeight;
		}
	}

	return total;
}

/*
===============
R_ImageList_f
===============
*/
void R_ImageList_f( void )
{
	int        i;
	image_t    *image;
	int        texels;
	int        dataSize;
	int        imageDataSize;
	const char *yesno[] =
	{
		"no ", "yes"
	};

	ri.Printf( PRINT_ALL, "\n      -w-- -h-- -mm- -type-   -if-- wrap --name-------\n" );

	texels = 0;
	dataSize = 0;

	for ( i = 0; i < tr.images.currentElements; i++ )
	{
		image = (image_t*) Com_GrowListElement( &tr.images, i );

		ri.Printf( PRINT_ALL, "%4i: %4i %4i  %s   ",
		           i, image->uploadWidth, image->uploadHeight, yesno[ image->filterType == FT_DEFAULT ] );

		switch ( image->type )
		{
			case GL_TEXTURE_2D:
				texels += image->uploadWidth * image->uploadHeight;
				imageDataSize = image->uploadWidth * image->uploadHeight;

				ri.Printf( PRINT_ALL, "2D   " );
				break;

			case GL_TEXTURE_CUBE_MAP:
				texels += image->uploadWidth * image->uploadHeight * 6;
				imageDataSize = image->uploadWidth * image->uploadHeight * 6;

				ri.Printf( PRINT_ALL, "CUBE " );
				break;

			default:
				ri.Printf( PRINT_ALL, "???? " );
				imageDataSize = image->uploadWidth * image->uploadHeight;
				break;
		}

		switch ( image->internalFormat )
		{
			case GL_RGB8:
				ri.Printf( PRINT_ALL, "RGB8     " );
				imageDataSize *= 3;
				break;

			case GL_RGBA8:
				ri.Printf( PRINT_ALL, "RGBA8    " );
				imageDataSize *= 4;
				break;

			case GL_RGB16:
				ri.Printf( PRINT_ALL, "RGB      " );
				imageDataSize *= 6;
				break;

			case GL_RGB16F:
				ri.Printf( PRINT_ALL, "RGB16F   " );
				imageDataSize *= 6;
				break;

			case GL_RGB32F:
				ri.Printf( PRINT_ALL, "RGB32F   " );
				imageDataSize *= 12;
				break;

			case GL_RGBA16F:
				ri.Printf( PRINT_ALL, "RGBA16F  " );
				imageDataSize *= 8;
				break;

			case GL_RGBA32F:
				ri.Printf( PRINT_ALL, "RGBA32F  " );
				imageDataSize *= 16;
				break;

			case GL_ALPHA16F_ARB:
				ri.Printf( PRINT_ALL, "A16F     " );
				imageDataSize *= 2;
				break;

			case GL_ALPHA32F_ARB:
				ri.Printf( PRINT_ALL, "A32F     " );
				imageDataSize *= 4;
				break;

			case GL_R16F:
				ri.Printf( PRINT_ALL, "R16F     " );
				imageDataSize *= 2;
				break;

			case GL_R32F:
				ri.Printf( PRINT_ALL, "R32F     " );
				imageDataSize *= 4;
				break;

			case GL_LUMINANCE_ALPHA16F_ARB:
				ri.Printf( PRINT_ALL, "LA16F    " );
				imageDataSize *= 4;
				break;

			case GL_LUMINANCE_ALPHA32F_ARB:
				ri.Printf( PRINT_ALL, "LA32F    " );
				imageDataSize *= 8;
				break;

			case GL_RG16F:
				ri.Printf( PRINT_ALL, "RG16F    " );
				imageDataSize *= 4;
				break;

			case GL_RG32F:
				ri.Printf( PRINT_ALL, "RG32F    " );
				imageDataSize *= 8;
				break;

			case GL_COMPRESSED_RGBA:
				ri.Printf( PRINT_ALL, "      " );
				imageDataSize *= 4; // FIXME
				break;

			case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
				ri.Printf( PRINT_ALL, "DXT1     " );
				imageDataSize *= 4 / 8;
				break;

			case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
				ri.Printf( PRINT_ALL, "DXT1a    " );
				imageDataSize *= 4 / 8;
				break;

			case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
				ri.Printf( PRINT_ALL, "DXT3     " );
				imageDataSize *= 4 / 4;
				break;

			case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
				ri.Printf( PRINT_ALL, "DXT5     " );
				imageDataSize *= 4 / 4;
				break;

			case GL_DEPTH_COMPONENT16:
				ri.Printf( PRINT_ALL, "D16      " );
				imageDataSize *= 2;
				break;

			case GL_DEPTH_COMPONENT24:
				ri.Printf( PRINT_ALL, "D24      " );
				imageDataSize *= 3;
				break;

			case GL_DEPTH_COMPONENT32:
				ri.Printf( PRINT_ALL, "D32      " );
				imageDataSize *= 4;
				break;

			default:
				ri.Printf( PRINT_ALL, "????     " );
				imageDataSize *= 4;
				break;
		}

		switch ( image->wrapType.s )
		{
			case WT_REPEAT:
				ri.Printf( PRINT_ALL, "s.rept  " );
				break;

			case WT_CLAMP:
				ri.Printf( PRINT_ALL, "s.clmp  " );
				break;

			case WT_EDGE_CLAMP:
				ri.Printf( PRINT_ALL, "s.eclmp " );
				break;

			case WT_ZERO_CLAMP:
				ri.Printf( PRINT_ALL, "s.zclmp " );
				break;

			case WT_ALPHA_ZERO_CLAMP:
				ri.Printf( PRINT_ALL, "s.azclmp" );
				break;

			default:
				ri.Printf( PRINT_ALL, "s.%4i  ", image->wrapType.s );
				break;
		}

		switch ( image->wrapType.t )
		{
			case WT_REPEAT:
				ri.Printf( PRINT_ALL, "t.rept  " );
				break;

			case WT_CLAMP:
				ri.Printf( PRINT_ALL, "t.clmp  " );
				break;

			case WT_EDGE_CLAMP:
				ri.Printf( PRINT_ALL, "t.eclmp " );
				break;

			case WT_ZERO_CLAMP:
				ri.Printf( PRINT_ALL, "t.zclmp " );
				break;

			case WT_ALPHA_ZERO_CLAMP:
				ri.Printf( PRINT_ALL, "t.azclmp" );
				break;

			default:
				ri.Printf( PRINT_ALL, "t.%4i  ", image->wrapType.t );
				break;
		}

		dataSize += imageDataSize;

		ri.Printf( PRINT_ALL, " %s\n", image->name );
	}

	ri.Printf( PRINT_ALL, " ---------\n" );
	ri.Printf( PRINT_ALL, " %i total texels (not including mipmaps)\n", texels );
	ri.Printf( PRINT_ALL, " %d.%02d MB total image memory\n", dataSize / ( 1024 * 1024 ),
	           ( dataSize % ( 1024 * 1024 ) ) * 100 / ( 1024 * 1024 ) );
	ri.Printf( PRINT_ALL, " %i total images\n\n", tr.images.currentElements );
}

//=======================================================================

/*
================
ResampleTexture

Used to resample images in a more general than quartering fashion.

This will only be filtered properly if the resampled size
is greater than half the original size.

If a larger shrinking is needed, use the mipmap function
before or after.
================
*/
static void ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight,
                             qboolean normalMap )
{
	int      x, y;
	unsigned *inrow, *inrow2;
	unsigned frac, fracstep;
	unsigned p1[ 2048 ], p2[ 2048 ];
	byte     *pix1, *pix2, *pix3, *pix4;
	vec3_t   n, n2, n3, n4;

	// NOTE: Tr3B - limitation not needed anymore
//  if(outwidth > 2048)
//      ri.Error(ERR_DROP, "ResampleTexture: max width");

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;

	for ( x = 0; x < outwidth; x++ )
	{
		p1[ x ] = 4 * ( frac >> 16 );
		frac += fracstep;
	}

	frac = 3 * ( fracstep >> 2 );

	for ( x = 0; x < outwidth; x++ )
	{
		p2[ x ] = 4 * ( frac >> 16 );
		frac += fracstep;
	}

	if ( normalMap )
	{
		for ( y = 0; y < outheight; y++ )
		{
			inrow = in + inwidth * ( int )( ( y + 0.25 ) * inheight / outheight );
			inrow2 = in + inwidth * ( int )( ( y + 0.75 ) * inheight / outheight );

			//frac = fracstep >> 1;

			for ( x = 0; x < outwidth; x++ )
			{
				pix1 = ( byte * ) inrow + p1[ x ];
				pix2 = ( byte * ) inrow + p2[ x ];
				pix3 = ( byte * ) inrow2 + p1[ x ];
				pix4 = ( byte * ) inrow2 + p2[ x ];

				n[ 0 ] = Tex_ByteToFloat( pix1[ 0 ] );
				n[ 1 ] = Tex_ByteToFloat( pix1[ 1 ] );
				n[ 2 ] = Tex_ByteToFloat( pix1[ 2 ] );

				n2[ 0 ] = Tex_ByteToFloat( pix2[ 0 ] );
				n2[ 1 ] = Tex_ByteToFloat( pix2[ 1 ] );
				n2[ 2 ] = Tex_ByteToFloat( pix2[ 2 ] );

				n3[ 0 ] = Tex_ByteToFloat( pix3[ 0 ] );
				n3[ 1 ] = Tex_ByteToFloat( pix3[ 1 ] );
				n3[ 2 ] = Tex_ByteToFloat( pix3[ 2 ] );

				n4[ 0 ] = Tex_ByteToFloat( pix4[ 0 ] );
				n4[ 1 ] = Tex_ByteToFloat( pix4[ 1 ] );
				n4[ 2 ] = Tex_ByteToFloat( pix4[ 2 ] );

				VectorAdd( n, n2, n );
				VectorAdd( n, n3, n );
				VectorAdd( n, n4, n );

				if ( !VectorNormalize( n ) )
				{
					VectorSet( n, 0, 0, 1 );
				}

				( ( byte * )( out ) ) [ 0 ] = Tex_FloatToByte( n[ 0 ] );
				( ( byte * )( out ) ) [ 1 ] = Tex_FloatToByte( n[ 1 ] );
				( ( byte * )( out ) ) [ 2 ] = Tex_FloatToByte( n[ 2 ] );
				( ( byte * )( out ) ) [ 3 ] = 255;

				++out;
			}
		}
	}
	else
	{
		for ( y = 0; y < outheight; y++ )
		{
			inrow = in + inwidth * ( int )( ( y + 0.25 ) * inheight / outheight );
			inrow2 = in + inwidth * ( int )( ( y + 0.75 ) * inheight / outheight );

			//frac = fracstep >> 1;

			for ( x = 0; x < outwidth; x++ )
			{
				pix1 = ( byte * ) inrow + p1[ x ];
				pix2 = ( byte * ) inrow + p2[ x ];
				pix3 = ( byte * ) inrow2 + p1[ x ];
				pix4 = ( byte * ) inrow2 + p2[ x ];

				( ( byte * )( out ) ) [ 0 ] = ( pix1[ 0 ] + pix2[ 0 ] + pix3[ 0 ] + pix4[ 0 ] ) >> 2;
				( ( byte * )( out ) ) [ 1 ] = ( pix1[ 1 ] + pix2[ 1 ] + pix3[ 1 ] + pix4[ 1 ] ) >> 2;
				( ( byte * )( out ) ) [ 2 ] = ( pix1[ 2 ] + pix2[ 2 ] + pix3[ 2 ] + pix4[ 2 ] ) >> 2;
				( ( byte * )( out ) ) [ 3 ] = ( pix1[ 3 ] + pix2[ 3 ] + pix3[ 3 ] + pix4[ 3 ] ) >> 2;

				++out;
			}
		}
	}
}

/*
================
R_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
void R_LightScaleTexture( unsigned *in, int inwidth, int inheight, qboolean onlyGamma )
{
	if ( onlyGamma )
	{
		if ( !glConfig.deviceSupportsGamma )
		{
			int  i, c;
			byte *p;

			p = ( byte * ) in;

			c = inwidth * inheight;

			for ( i = 0; i < c; i++, p += 4 )
			{
				p[ 0 ] = s_gammatable[ p[ 0 ] ];
				p[ 1 ] = s_gammatable[ p[ 1 ] ];
				p[ 2 ] = s_gammatable[ p[ 2 ] ];
			}
		}
	}
	else
	{
		int  i, c;
		byte *p;

		p = ( byte * ) in;

		c = inwidth * inheight;

		if ( glConfig.deviceSupportsGamma )
		{
			// raynorpat: small optimization
			if ( r_intensity->value != 1.0f )
			{
				for ( i = 0; i < c; i++, p += 4 )
				{
					p[ 0 ] = s_intensitytable[ p[ 0 ] ];
					p[ 1 ] = s_intensitytable[ p[ 1 ] ];
					p[ 2 ] = s_intensitytable[ p[ 2 ] ];
				}
			}
		}
		else
		{
			for ( i = 0; i < c; i++, p += 4 )
			{
				p[ 0 ] = s_gammatable[ s_intensitytable[ p[ 0 ] ] ];
				p[ 1 ] = s_gammatable[ s_intensitytable[ p[ 1 ] ] ];
				p[ 2 ] = s_gammatable[ s_intensitytable[ p[ 2 ] ] ];
			}
		}
	}
}

#ifdef COMPAT_KPQ3
// Compute Van der Corput radical inverse
// See: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverse_VdC(uint32_t bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Sample i-th point from Hammersley point set of NumSamples points total.
void sampleHammersley(uint32_t i, int numSamples, float out[2])
{	
	//calculate rand position 
	//hypov8 set Y as incremental
	out[0] = radicalInverse_VdC(i); //0-1
	out[1] = ((float)i*(1.0f / numSamples)); //0-1

}

void sampleWithRoughness(vec2_t randXY, int x, int y, float rough, int inWidth, int inHeight, uint32_t out[2])
{	
	float offsX, offsY;

	//centre sample
	float scaleX = (randXY[0] - 0.5)*0.5; //-0.5 to 0.5
	float scaleY = (randXY[1] - 0.5)*0.5; //-0.5 to 0.5

	scaleX *= rough*rough;
	scaleY *= rough*rough;

	offsX = (float)x + scaleX*inWidth;
	offsY = (float)y + scaleY*inHeight;

	//cap incriental Y value to boarder
	if (offsY < 0)
		offsY = 0;
	else if (offsY >= inHeight)
		offsY = inHeight;

	//calc spread from centre of pixel position
	out[0] = ((uint32_t)floor(offsX)) % inWidth; //wrap
	out[1] = ((uint32_t)floor(offsY)) /*% inHeight*/; //caped
}

void sampleWeight(float randXY[2], float *outWeight)
{
	//centre sample
	float scaleX = (randXY[0] - 0.5); //-0.5 to 0.5
	float scaleY = (randXY[1] - 0.5); //-0.5 to 0.5
	float t = 1.0f - (Q_fabs(scaleX) * Q_fabs(scaleY));
	*outWeight = (t*t);
}

/*
================
R_MipMapPBRSpec

================
*/
static void R_MipMapPBRSpec(const byte *data, byte *out, 
	int inWidth, int inHeight, int outWidth, int outHeight, int mipNum )
{
	int        x, y, s, midX, midY, offsX, offsY;
	double     curPixColor[3], weight;
	int        numSamples = 64; // 5 * (mipNum*mipNum);
	const byte *curSampleIn;
	byte       *curSampleOut;
	float      randPosXY[2];
	uint32_t   sampleXY[2];
	int        factor = 1 << mipNum;
	float      tmpWeight;
	float rough = (float)mipNum / 5.0f; //0.2, 0.4, 0.6, 0.8, 1.0	//generate 5 mipmaps

	//memset(out, 0xff, outWidth*outHeight * 4);

	for (y = 0; y < outHeight; y++)
	{
		offsX = y * outWidth * 4;
		for (x = 0; x < outWidth; x++)
		{
			offsY = x * 4;
			curSampleOut = &out[offsX+offsY];
			weight = 0;
			VectorSet(curPixColor, 0, 0, 0);
			midX = x*factor;
			midY = y*factor;
			for (s = 0; s < numSamples; s++)
			{
				sampleHammersley(s, numSamples, randPosXY); //rand sample
				sampleWithRoughness(randPosXY, midX, midY, rough, inWidth, inHeight, sampleXY ); //get x/y pos
				curSampleIn = &data[(sampleXY[1]*inWidth*4)+(sampleXY[0]*4)]; //move to pixel in orig image
				sampleWeight(randPosXY, &tmpWeight);
				weight += tmpWeight; //todo linear
				curPixColor[0] += (double)curSampleIn[0]* tmpWeight;
				curPixColor[1] += (double)curSampleIn[1]* tmpWeight;
				curPixColor[2] += (double)curSampleIn[2]* tmpWeight;
			}
			curPixColor[0] /= weight;
			curPixColor[1] /= weight;
			curPixColor[2] /= weight;
			curSampleOut[0] = (byte)curPixColor[0];
			curSampleOut[1] = (byte)curPixColor[1];
			curSampleOut[2] = (byte)curPixColor[2];
			curSampleOut[3] = 255;
		}
	}
}


#endif

/*
================
R_MipMap2

Operates in place, quartering the size of the texture
Proper linear filter
================
*/
static void R_MipMap2( unsigned *in, int inWidth, int inHeight )
{
	int      i, j, k;
	byte     *outpix;
	int      inWidthMask, inHeightMask;
	int      outWidth, outHeight;
	unsigned *temp;
	byte     *row[ 4 ];

	outWidth = inWidth >> 1;
	outHeight = inHeight >> 1;
	temp = (unsigned int*) ri.Hunk_AllocateTempMemory( outWidth * outHeight * 4 );
	outpix = (byte *) temp;

	inWidthMask = ( inWidth << 2 ) - 1; // applied to row indices
	inHeightMask = inHeight - 1; // applied to in indices

	row[ 1 ] = (byte *) &in[( -1 & inHeightMask ) * inWidth ];
	row[ 2 ] = (byte *) &in[   0                            ];
	row[ 3 ] = (byte *) &in[(  1 & inHeightMask ) * inWidth ];

	for ( i = 0; i < inHeight; i += 2 ) // count source, row pairs
	{
		row[ 0 ] = row[ 1 ];
		row[ 1 ] = row[ 2 ];
		row[ 2 ] = row[ 3 ];
		row[ 3 ] = (byte *) &in[ ( ( i + 2 ) & inHeightMask ) * inWidth ];

		for ( j = 0; j < inWidth * 4; j += 8 ) // count source, bytes comprising texel pairs
		{
			for ( k = j; k < j + 4; k++ )
			{
				const int km1 = ( k - 4 ) & inWidthMask;
				const int kp1 = ( k + 4 ) & inWidthMask;
				const int kp2 = ( k + 8 ) & inWidthMask;

				*outpix++ = ( 1 * row[ 0 ][ km1 ] + 2 * row[ 0 ][ k   ] + 2 * row[ 0 ][ kp1 ] + 1 * row[ 0 ][ kp2 ] +
						2 * row[ 1 ][ km1 ] + 4 * row[ 1 ][ k   ] + 4 * row[ 1 ][ kp1 ] + 2 * row[ 1 ][ kp2 ] +
						2 * row[ 2 ][ km1 ] + 4 * row[ 2 ][ k   ] + 4 * row[ 2 ][ kp1 ] + 2 * row[ 2 ][ kp2 ] +
						1 * row[ 3 ][ km1 ] + 2 * row[ 3 ][ k   ] + 2 * row[ 3 ][ kp1 ] + 1 * row[ 3 ][ kp2 ] ) / 36;
			}
		}
	}

	Com_Memcpy( in, temp, outWidth * outHeight * 4 );
	ri.Hunk_FreeTempMemory( temp );
}

/*
================
R_MipMap

Operates in place, quartering the size of the texture
================
*/
static void R_MipMap( byte *in, int width, int height )
{
	int  i, j;
	byte *out;
	int  row;

	if ( !r_simpleMipMaps->integer )
	{
		R_MipMap2( ( unsigned * ) in, width, height );
		return;
	}

	if ( width == 1 && height == 1 )
	{
		return;
	}

	row = width * 4;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 )
	{
		width += height; // get largest

		for ( i = 0; i < width; i++, out += 4, in += 8 )
		{
			out[ 0 ] = ( in[ 0 ] + in[ 4 ] ) >> 1;
			out[ 1 ] = ( in[ 1 ] + in[ 5 ] ) >> 1;
			out[ 2 ] = ( in[ 2 ] + in[ 6 ] ) >> 1;
			out[ 3 ] = ( in[ 3 ] + in[ 7 ] ) >> 1;
		}

		return;
	}

	for ( i = 0; i < height; i++, in += row )
	{
		for ( j = 0; j < width; j++, out += 4, in += 8 )
		{
			out[ 0 ] = ( in[ 0 ] + in[ 4 ] + in[ row + 0 ] + in[ row + 4 ] ) >> 2;
			out[ 1 ] = ( in[ 1 ] + in[ 5 ] + in[ row + 1 ] + in[ row + 5 ] ) >> 2;
			out[ 2 ] = ( in[ 2 ] + in[ 6 ] + in[ row + 2 ] + in[ row + 6 ] ) >> 2;
			out[ 3 ] = ( in[ 3 ] + in[ 7 ] + in[ row + 3 ] + in[ row + 7 ] ) >> 2;
		}
	}
}

/*
================
R_MipNormalMap

Operates in place, quartering the size of the texture
================
*/
// *INDENT-OFF*
static void R_MipNormalMap( byte *in, int width, int height )
{
	int    i, j;
	byte   *out;
	vec4_t n;
	vec_t  length;

	if ( width == 1 && height == 1 )
	{
		return;
	}

	out = in;
//	width >>= 1;
	width <<= 2;
	height >>= 1;

	for ( i = 0; i < height; i++, in += width )
	{
		for ( j = 0; j < width; j += 8, out += 4, in += 8 )
		{
			// these calculations were centred on 127.5: (p / 255.0 - 0.5) * 2.0
			n[ 0 ] = Tex_ByteToFloat( in[ 0 ] ) +
			         Tex_ByteToFloat( in[ 4 ] ) +
			         Tex_ByteToFloat( in[ width + 0 ] ) +
			         Tex_ByteToFloat( in[ width + 4 ] );

			n[ 1 ] = Tex_ByteToFloat( in[ 1 ] ) +
			         Tex_ByteToFloat( in[ 5 ] ) +
			         Tex_ByteToFloat( in[ width + 1 ] ) +
			         Tex_ByteToFloat( in[ width + 5 ] );

			n[ 2 ] = Tex_ByteToFloat( in[ 2 ] ) +
			         Tex_ByteToFloat( in[ 6 ] ) +
			         Tex_ByteToFloat( in[ width + 2 ] ) +
			         Tex_ByteToFloat( in[ width + 6 ] );

			n[ 3 ] = ( in[ 3 ] + in[ 7 ] + in[ width + 3 ] +  in[ width + 7 ] ) / 255.0f;

			length = VectorLength( n );

			if ( length )
			{
				n[ 0 ] /= length;
				n[ 1 ] /= length;
				n[ 2 ] /= length;
			}
			else
			{
				VectorSet( n, 0.0, 0.0, 1.0 );
			}

			out[ 0 ] = Tex_FloatToByte( n[ 0 ] );
			out[ 1 ] = Tex_FloatToByte( n[ 1 ] );
			out[ 2 ] = Tex_FloatToByte( n[ 2 ] );
			out[ 3 ] = ( byte )( n[ 3 ] * 255.0 / 4.0 );
			//out[3] = (in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
		}
	}
}

// *INDENT-ON*

static void R_HeightMapToNormalMap( byte *img, int width, int height, float scale )
{
	int    x, y;
	float  r, g, b;
	float  c, cx, cy;
	float  dcx, dcy;
	vec3_t n;

	const int row = 4 * width;

	for ( y = 0; y < height; y++ )
	{
		for ( x = 0; x < width; x++ )
		{
			// convert the pixel at x, y in the bump map to a normal (float)

			// expand [0,255] texel values to the [0,1] range
			r = img[ 0 ];
			g = img[ 1 ];
			b = img[ 2 ];

			c = ( r + g + b ) / 255.0f;

			// expand the texel to its right
			if ( x == width - 1 )
			{
				cx = c; // at edge, don't wrap
			}
			else
			{
				r = img[ 4 ];
				g = img[ 5 ];
				b = img[ 6 ];

				cx = ( r + g + b ) / 255.0f;
			}

			// expand the texel one up
			if ( y == height - 1 )
			{
				cy = c; // at edge, don't wrap
			}
			else
			{
				r = img[ row ];
				g = img[ row + 1 ];
				b = img[ row + 2 ];

				cy = ( r + g + b ) / 255.0f;
			}

			dcx = scale * ( c - cx );
			dcy = scale * ( c - cy );

			// normalize the vector
			VectorSet( n, dcx, dcy, 1.0 );  //scale);

			if ( !VectorNormalize( n ) )
			{
				VectorSet( n, 0, 0, 1 );
			}

			// repack the normalized vector into an RGB unsigned byte
			// vector in the normal map image
			*img++ = Tex_FloatToByte( n[ 0 ] );
			*img++ = Tex_FloatToByte( n[ 1 ] );
			*img++ = Tex_FloatToByte( n[ 2 ] );

			// put in no height as displacement map by default
			*img++ = ( byte ) 0; //Maths::clamp(c * 255.0 / 3.0, 0.0f, 255.0f));
		}
	}
}

static void R_DisplaceMap( byte *img, const byte *in2, int width, int height )
{
	int i;

	img += 3;

	for ( i = height * width; i; --i )
	{
		*img = ( in2[ 0 ] + in2[ 1 ] + in2[ 2 ] ) / 3;
		img += 4;
		in2 += 4;
	}
}


static void R_AddGloss( byte *img, byte *in2, int width, int height )
{
	int i;

	img += 3;

	for ( i = height * width; i; --i )
	{
		// seperate gloss maps should always be greyscale, but do the average anyway 
		*img = ( byte ) ( (int)in2[ 0 ] + (int)in2[ 1 ] + (int)in2[ 2 ] )/ 3;
		in2 += 4;
		img += 4;
	}
}

static void R_AddNormals( byte *img, byte *in2, int width, int height )
{
	int    i;
	vec3_t n;
	byte   a;
	byte   a2;

	for ( i = width * height; i; --i )
	{
		n[ 0 ] = Tex_ByteToFloat( img[ 0 ] ) + Tex_ByteToFloat( in2[ 0 ] );
		n[ 1 ] = Tex_ByteToFloat( img[ 1 ] ) + Tex_ByteToFloat( in2[ 1 ] );
		n[ 2 ] = Tex_ByteToFloat( img[ 2 ] ) + Tex_ByteToFloat( in2[ 2 ] );

		a = img[ 3 ];
		a2 = in2[ 3 ];

		if ( !VectorNormalize( n ) )
		{
			VectorSet( n, 0, 0, 1 );
		}

		img[ 0 ] = Tex_FloatToByte( n[ 0 ] );
		img[ 1 ] = Tex_FloatToByte( n[ 1 ] );
		img[ 2 ] = Tex_FloatToByte( n[ 2 ] );
		img[ 3 ] = ( byte )( Maths::clamp( a + a2, 0, 255 ) );

		img += 4;
		in2 += 4;
	}
}

static void R_InvertAlpha( byte *img, int width, int height )
{
	int i;

	img += 3;

	for ( i = width * height; i; --i )
	{
		*img = 255 - *img;
		img += 4;
	}
}

static void R_InvertColor( byte *img, int width, int height )
{
	int i;

	for ( i = width * height; i; --i )
	{
		img[ 0 ] = 255 - img[ 0 ];
		img[ 1 ] = 255 - img[ 1 ];
		img[ 2 ] = 255 - img[ 2 ];
		img += 4;
	}
}

static void R_MakeIntensity( byte *img, int width, int height )
{
	int i;

	for ( i = width * height; i; --i )
	{
		img[ 3 ] = img[ 2 ] = img[ 1 ] = img[ 0 ];
		img += 4;
	}
}

static void R_MakeAlpha( byte *img, int width, int height )
{
	int i;

	for ( i = width * height; i; --i )
	{
		img[ 3 ] = ( img[ 0 ] + img[ 1 ] + img[ 2 ] ) / 3;
		img[ 2 ] = 255;
		img[ 1 ] = 255;
		img[ 0 ] = 255;
		img += 4;
	}
}

/*
==================
R_BlendOverTexture

Apply a color blend over a set of pixels
==================
*/
static void R_BlendOverTexture( byte *data, int pixelCount, const byte blend[ 4 ] )
{
	int i;
	int inverseAlpha;
	int premult[ 3 ];

	inverseAlpha = 255 - blend[ 3 ];
	premult[ 0 ] = blend[ 0 ] * blend[ 3 ];
	premult[ 1 ] = blend[ 1 ] * blend[ 3 ];
	premult[ 2 ] = blend[ 2 ] * blend[ 3 ];

	for ( i = 0; i < pixelCount; i++, data += 4 )
	{
		data[ 0 ] = ( data[ 0 ] * inverseAlpha + premult[ 0 ] ) >> 9;
		data[ 1 ] = ( data[ 1 ] * inverseAlpha + premult[ 1 ] ) >> 9;
		data[ 2 ] = ( data[ 2 ] * inverseAlpha + premult[ 2 ] ) >> 9;
	}
}

static const byte mipBlendColors[ 16 ][ 4 ] =
{
	{ 0,   0,   0,   0   }
	,
	{ 255, 0,   0,   128 }
	,
	{ 0,   255, 0,   128 }
	,
	{ 0,   0,   255, 128 }
	,
	{ 255, 0,   0,   128 }
	,
	{ 0,   255, 0,   128 }
	,
	{ 0,   0,   255, 128 }
	,
	{ 255, 0,   0,   128 }
	,
	{ 0,   255, 0,   128 }
	,
	{ 0,   0,   255, 128 }
	,
	{ 255, 0,   0,   128 }
	,
	{ 0,   255, 0,   128 }
	,
	{ 0,   0,   255, 128 }
	,
	{ 255, 0,   0,   128 }
	,
	{ 0,   255, 0,   128 }
	,
	{ 0,   0,   255, 128 }
	,
};

/*
===============
R_UploadImage
===============
*/
void R_UploadImage( const byte **dataArray, int numData, image_t *image )
{
	const byte *data = dataArray[ 0 ];
	byte       *scaledBuffer = NULL;
	int        scaledWidth, scaledHeight;
	int        i, c;
	const byte *scan;
	GLenum     target;
	GLenum     format = GL_RGBA;
	GLenum     internalFormat = GL_RGB;

	static const vec4_t oneClampBorder = { 1, 1, 1, 1 };
	static const vec4_t zeroClampBorder = { 0, 0, 0, 1 };
	static const vec4_t alphaZeroClampBorder = { 0, 0, 0, 0 };

	GL_Bind( image );

	if ( glConfig2.textureNPOTAvailable )
	{
		scaledWidth = image->width;
		scaledHeight = image->height;
	}
	else
	{
		// convert to exact power of 2 sizes
		for ( scaledWidth = 1; scaledWidth < image->width; scaledWidth <<= 1 )
		{
			;
		}

		for ( scaledHeight = 1; scaledHeight < image->height; scaledHeight <<= 1 )
		{
			;
		}
	}

	if ( r_roundImagesDown->integer && scaledWidth > image->width )
	{
		scaledWidth >>= 1;
	}

	if ( r_roundImagesDown->integer && scaledHeight > image->height )
	{
		scaledHeight >>= 1;
	}

	// perform optional picmip operation
	if ( !( image->bits & IF_NOPICMIP ) )
	{
		int picmip = r_picmip->integer;//Deamon 5.2
		if( picmip < 0 )
			picmip = 0;
		else if (picmip > 2) //hypov8 fix cheat?
			picmip = 2;

		scaledWidth >>= r_picmip->integer;
		scaledHeight >>= r_picmip->integer;

		/*if( dataArray && numMips > picmip ) {
			dataArray += numData * picmip;
			numMips -= picmip;
		}*/
	}

	// clamp to minimum size
	if ( scaledWidth < 1 )
	{
		scaledWidth = 1;
	}

	if ( scaledHeight < 1 )
	{
		scaledHeight = 1;
	}

	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	if ( image->type == GL_TEXTURE_CUBE_MAP )
	{
		while ( scaledWidth > glConfig2.maxCubeMapTextureSize || scaledHeight > glConfig2.maxCubeMapTextureSize )
		{
			scaledWidth >>= 1;
			scaledHeight >>= 1;
		}
	}
	else
	{
		while ( scaledWidth > glConfig.maxTextureSize || scaledHeight > glConfig.maxTextureSize )
		{
			scaledWidth >>= 1;
			scaledHeight >>= 1;
		}
	}

	// set target
	switch ( image->type )
	{
		case GL_TEXTURE_3D:
			target = GL_TEXTURE_3D;
			break;

		case GL_TEXTURE_CUBE_MAP:
			target = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
			break;

		default:
			target = GL_TEXTURE_2D;
			break;
	}

	if ( image->bits & ( IF_DEPTH16 | IF_DEPTH24 | IF_DEPTH32 ) )
	{
		format = GL_DEPTH_COMPONENT;

		if ( image->bits & IF_DEPTH16 )
		{
			internalFormat = GL_DEPTH_COMPONENT16;
		}
		else if ( image->bits & IF_DEPTH24 )
		{
			internalFormat = GL_DEPTH_COMPONENT24;
		}
		else if ( image->bits & IF_DEPTH32 )
		{
			internalFormat = GL_DEPTH_COMPONENT32;
		}
	}
	else if ( image->bits & ( IF_PACKED_DEPTH24_STENCIL8 ) )
	{
		format = GL_DEPTH_STENCIL_EXT;
		internalFormat = GL_DEPTH24_STENCIL8_EXT;
	}
	else if ( glConfig2.textureFloatAvailable &&
	          ( image->bits & ( IF_RGBA16F | IF_RGBA32F | IF_RGBA16 | IF_TWOCOMP16F | IF_TWOCOMP32F | IF_ONECOMP16F | IF_ONECOMP32F ) ) )
	{
		if ( image->bits & IF_RGBA16F )
		{
			internalFormat = GL_RGBA16F;
		}
		else if ( image->bits & IF_RGBA32F )
		{
			internalFormat = GL_RGBA32F;
		}
		else if ( image->bits & IF_TWOCOMP16F )
		{
			internalFormat = glConfig2.textureRGAvailable ? GL_RG16F : GL_LUMINANCE_ALPHA16F_ARB;
#ifdef COMPAT_KPQ3
			format = GL_RG; // GL_RG16F;
#endif
		}
		else if ( image->bits & IF_TWOCOMP32F )
		{
			internalFormat = glConfig2.textureRGAvailable ? GL_RG32F : GL_LUMINANCE_ALPHA32F_ARB;
		}
		else if ( image->bits & IF_RGBA16 )
		{
			internalFormat = GL_RGBA16;
		}
		else if ( image->bits & IF_ONECOMP16F )
		{
			internalFormat = glConfig2.textureRGAvailable ? GL_R16F : GL_ALPHA16F_ARB;
		}
		else if ( image->bits & IF_ONECOMP32F )
		{
			internalFormat = glConfig2.textureRGAvailable ? GL_R32F : GL_ALPHA32F_ARB;
		}
	}
	else if ( image->bits & IF_RGBE )
	{
		internalFormat = GL_RGBA8;
	}
	else if ( !data ) 
	{
		internalFormat = GL_RGBA8;
	}
	else
	{
		int samples;

		// scan the texture for each channel's max values
		// and verify if the alpha channel is being used or not
		c = image->width * image->height;
		scan = data;

		samples = 3;

		// Tr3B: normalmaps have the displacement maps in the alpha channel
		// samples 3 would cause an opaque alpha channel and odd displacements!
		if ( image->bits & IF_NORMALMAP )
		{
			if ( image->bits & ( IF_DISPLACEMAP | IF_ALPHATEST ) )
			{
				samples = 4;
			}
			else
			{
				samples = 3;
			}
		}
		else if ( image->bits & IF_LIGHTMAP )
		{
			samples = 3;
		}
		else
		{
			for ( i = 0; i < c; i++ )
			{
				if ( scan[ i * 4 + 3 ] != 255 )
				{
					samples = 4;
					break;
				}
			}
		}

		// select proper internal format
		if ( samples == 3 )
		{
			if ( glConfig.textureCompression == TC_S3TC && !( image->bits & IF_NOCOMPRESSION ) )
			{
				internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
			}
			else
			{
				internalFormat = GL_RGB8;
			}
		}
		else if ( samples == 4 )
		{
			if ( image->bits & IF_ALPHA )
			{
				internalFormat = GL_ALPHA8;
			}
			else
			{
				if ( glConfig.textureCompression == TC_S3TC && !( image->bits & IF_NOCOMPRESSION ) )
				{
					if ( image->bits & IF_DISPLACEMAP )
					{
						internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
					}
					else if ( image->bits & IF_ALPHATEST )
					{
						internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
					}
					else
					{
						internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
					}
				}
				else
				{
					internalFormat = GL_RGBA8;
				}
			}
		}
	}

	// 3D textures are uploaded in slices via glTexSubImage3D,
	// so the storage has to be allocated before the loop
	if( image->type == GL_TEXTURE_3D ) {
		glTexImage3D( GL_TEXTURE_3D, 0, internalFormat,
			      scaledWidth, scaledHeight, numData,
			      0, format, GL_UNSIGNED_BYTE, NULL );
	}

	if( data )
		scaledBuffer = (byte*) ri.Hunk_AllocateTempMemory( sizeof( byte ) * scaledWidth * scaledHeight * 4 );
	else
		scaledBuffer = NULL;

	for ( i = 0; i < numData; i++ )
	{
		data = dataArray[ i ];

		if( scaledBuffer )
		{
			// copy or resample data as appropriate for first MIP level
			if ( ( scaledWidth == image->width ) && ( scaledHeight == image->height ) )
			{
				Com_Memcpy( scaledBuffer, data, scaledWidth * scaledHeight * 4 );
			}
			else
			{
				ResampleTexture( ( unsigned * ) data, image->width, image->height, ( unsigned * ) scaledBuffer, scaledWidth, scaledHeight,
						 ( image->bits & IF_NORMALMAP ) );
			}

			if ( !( image->bits & ( IF_NORMALMAP | IF_RGBA16F | IF_RGBA32F | IF_TWOCOMP16F | IF_TWOCOMP32F | IF_NOLIGHTSCALE ) ) )
			{
#if 0 //def COMPAT_KPQ3
				if (image->filterType == FT_CUBEMIP)
					R_LightScaleTexture( ( unsigned * ) scaledBuffer, scaledWidth, scaledHeight, image->filterType == FT_CUBEMIP );
				else
#endif
					R_LightScaleTexture( ( unsigned * ) scaledBuffer, scaledWidth, scaledHeight, image->filterType == FT_DEFAULT );
			}
		}

		image->uploadWidth = scaledWidth;
		image->uploadHeight = scaledHeight;
		image->internalFormat = internalFormat;

		switch ( image->type )
		{
			case GL_TEXTURE_3D:
				glTexSubImage3D( GL_TEXTURE_3D, 0, 0, 0, i,
						 scaledWidth, scaledHeight, 1,
						 format, GL_UNSIGNED_BYTE,
						 scaledBuffer );
				break;
			case GL_TEXTURE_CUBE_MAP:
				glTexImage2D( target + i, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_BYTE,
				              scaledBuffer );
				break;

			default:
				if ( image->bits & IF_PACKED_DEPTH24_STENCIL8 )
				{
					glTexImage2D( target, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_INT_24_8_EXT, NULL );
				}
#ifdef COMPAT_KPQ3
				else if (image->bits & (IF_TWOCOMP16F | IF_TWOCOMP32F))
				{			//GL_UNSIGNED_SHORT //GL_2_BYTES //GL_FLOAT format = GL_RG

					glTexImage2D( target, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_SHORT, scaledBuffer );
				}
#endif
				else
				{ // 2D (GLenum target, level, internalformat, width, height,  border,  format,  type, const GLvoid *pixels);
					glTexImage2D( target, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_BYTE, scaledBuffer );
				}

				break;
		}

		if ( (image->filterType == FT_DEFAULT  
#ifdef COMPAT_KPQ3
			|| image->filterType == FT_CUBEMIP
#endif
			) && (image->type != GL_TEXTURE_CUBE_MAP || i == 5)) //Daemon 5.2 (note: cubemaps crashing fix)
		{
			if ( glConfig.driverType == GLDRV_OPENGL3 || glConfig2.framebufferObjectAvailable )
			{
#ifdef COMPAT_KPQ3
				if (image->filterType == FT_CUBEMIP && image->type == GL_TEXTURE_CUBE_MAP )
					glTexParameteri(image->type, GL_TEXTURE_CUBE_MAP_SEAMLESS, GL_TRUE); //GL version is 3.2 or greater
#endif
				glGenerateMipmapEXT( image->type );
				glTexParameteri( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );  // default to trilinear
			}
			else if ( glConfig2.generateMipmapAvailable )
			{
				// raynorpat: if hardware mipmap generation is available, use it
				//glHint(GL_GENERATE_MIPMAP_HINT_SGIS, GL_NICEST);  // make sure it's nice
				glTexParameteri( image->type, GL_GENERATE_MIPMAP_SGIS, GL_TRUE );
				glTexParameteri( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );  // default to trilinear
 			}
		}

		// generate mipmap
		if ( glConfig.driverType != GLDRV_OPENGL3 && !glConfig2.framebufferObjectAvailable && !glConfig2.generateMipmapAvailable )
		{
			if ( (image->filterType == FT_DEFAULT 
#ifdef COMPAT_KPQ3
				|| image->filterType == FT_CUBEMIP
#endif
				) && !( image->bits & ( IF_DEPTH16 | IF_DEPTH24 | IF_DEPTH32 | IF_PACKED_DEPTH24_STENCIL8 ) ) )
			{
				int mipLevel;
				int mipWidth, mipHeight;

				mipLevel = 0;
				mipWidth = scaledWidth;
				mipHeight = scaledHeight;

				while ( mipWidth > 1 || mipHeight > 1 )
				{
					if ( image->bits & IF_NORMALMAP )
					{
						R_MipNormalMap( scaledBuffer, mipWidth, mipHeight );
					}
					else
					{
						R_MipMap( scaledBuffer, mipWidth, mipHeight );
					}

					mipWidth >>= 1;
					mipHeight >>= 1;

					if ( mipWidth < 1 )
					{
						mipWidth = 1;
					}

					if ( mipHeight < 1 )
					{
						mipHeight = 1;
					}

					mipLevel++;

					if ( r_colorMipLevels->integer && !( image->bits & IF_NORMALMAP ) )
					{
						R_BlendOverTexture( scaledBuffer, mipWidth * mipHeight, mipBlendColors[ mipLevel ] );
					}

					switch ( image->type )
					{
						case GL_TEXTURE_CUBE_MAP:
							glTexImage2D( target + i, mipLevel, internalFormat, mipWidth, mipHeight, 0, format, GL_UNSIGNED_BYTE,
							              scaledBuffer );
							break;

						default:
							glTexImage2D( target, mipLevel, internalFormat, mipWidth, mipHeight, 0, format, GL_UNSIGNED_BYTE,
							              scaledBuffer );
							break;
					}
				}
			}
		}
	}

	GL_CheckErrors();

	
#ifdef COMPAT_KPQ3
	//pbr compute spec LOD (using mipmaps)
	if (image->filterType == FT_CUBEMIP)
	{
		int mipLevel;
		int mipWidth, mipHeight;

		mipLevel = 0;
		mipWidth = scaledWidth;
		mipHeight = scaledHeight;

		while (mipWidth > 2 || mipHeight > 2 || mipLevel <= 5)
		{
			mipWidth >>= 1;
			mipHeight >>= 1;
			mipLevel++;

			if (!(image->bits & (IF_DEPTH16 | IF_DEPTH24 | IF_DEPTH32 | IF_PACKED_DEPTH24_STENCIL8)))
			{
				R_MipMapPBRSpec(data, scaledBuffer, scaledWidth, scaledHeight, mipWidth, mipHeight, mipLevel);
			}
			//else //16 bit?
			
			if ( image->type == GL_TEXTURE_CUBE_MAP)
				glTexImage2D( target + i, mipLevel, internalFormat, mipWidth, mipHeight, 0, format, GL_UNSIGNED_BYTE, scaledBuffer );
			else
				glTexImage2D( target, mipLevel, internalFormat, mipWidth, mipHeight, 0, format, GL_UNSIGNED_BYTE, scaledBuffer );
		}
	}
	GL_CheckErrors();
#endif	


	// set filter type
	switch ( image->filterType )
	{
		case FT_DEFAULT:
			// set texture anisotropy
			if ( glConfig2.textureAnisotropyAvailable )
			{
				glTexParameterf( image->type, GL_TEXTURE_MAX_ANISOTROPY_EXT, r_ext_texture_filter_anisotropic->value );
			}
			glTexParameterf(image->type, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameterf(image->type, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			break;

#ifdef COMPAT_KPQ3
		case FT_CUBEMIP: //mip cubemaps used in pbr reflections
			glTexParameterf(image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameterf(image->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			break;
#endif	
		case FT_LINEAR:
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;

		case FT_NEAREST:
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			break;

		default:
			ri.Printf( PRINT_WARNING, "WARNING: unknown filter type for image '%s'\n", image->name );
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;
	}

	GL_CheckErrors();

	// set wrap type
	if (image->wrapType.s == image->wrapType.t)
	{
		switch (image->wrapType.s)
		{
			case WT_REPEAT:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_REPEAT);
				break;

			case WT_CLAMP:
			case WT_EDGE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				break;
			case WT_ONE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder);
				break;
			case WT_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder);
				break;

			case WT_ALPHA_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder);
				break;

			default:
				ri.Printf(PRINT_WARNING, "WARNING: unknown wrap type for image '%s'\n", image->name);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_REPEAT);
				break;
		}
	}
	else 
	{
		// warn about mismatched clamp types if both require a border colour to be set
		if ((image->wrapType.s == WT_ZERO_CLAMP || image->wrapType.s == WT_ONE_CLAMP || image->wrapType.s == WT_ALPHA_ZERO_CLAMP) &&
			(image->wrapType.t == WT_ZERO_CLAMP || image->wrapType.t == WT_ONE_CLAMP || image->wrapType.t == WT_ALPHA_ZERO_CLAMP))
		{
			ri.Printf(PRINT_WARNING, "WARNING: mismatched wrap types for image '%s'\n", image->name);
		}

		switch (image->wrapType.s)
		{
			case WT_REPEAT:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_REPEAT);
				break;

			case WT_CLAMP:
			case WT_EDGE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				break;

			case WT_ONE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder);
				break;

			case WT_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder);
				break;

			case WT_ALPHA_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder);
				break;

			default:
				ri.Printf(PRINT_WARNING, "WARNING: unknown wrap type for image '%s' axis S\n", image->name);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_S, GL_REPEAT);
				break;
		}

		switch (image->wrapType.t)
		{
			case WT_REPEAT:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_REPEAT);
				break;

			case WT_CLAMP:
			case WT_EDGE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				break;

			case WT_ONE_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder);
				break;

			case WT_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder);
				break;

			case WT_ALPHA_ZERO_CLAMP:
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				glTexParameterfv(image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder);
				break;

			default:
				ri.Printf(PRINT_WARNING, "WARNING: unknown wrap type for image '%s' axis T\n", image->name);
				glTexParameterf(image->type, GL_TEXTURE_WRAP_T, GL_REPEAT);
				break;
		}
	}

	GL_CheckErrors();

	if ( scaledBuffer != 0 )
	{
		ri.Hunk_FreeTempMemory( scaledBuffer );
	}

	GL_Unbind( image );
}

/*
================
R_AllocImage
================
*/
image_t        *R_AllocImage( const char *name, qboolean linkIntoHashTable )
{
	image_t *image;
	long    hash;
	char    buffer[ 1024 ];

//  if(strlen(name) >= MAX_QPATH)
	if ( strlen( name ) >= 1024 )
	{
		ri.Error( ERR_DROP, "R_AllocImage: \"%s\" image name is too long", name );
		return NULL;
	}

	image = (image_t*) ri.Hunk_Alloc( sizeof( image_t ), h_low );
	Com_Memset( image, 0, sizeof( image_t ) );

	glGenTextures( 1, &image->texnum );

	Com_AddToGrowList( &tr.images, image );

	Q_strncpyz( image->name, name, sizeof( image->name ) );

	if ( linkIntoHashTable )
	{
		Q_strncpyz( buffer, name, sizeof( buffer ) );
		hash = GenerateImageHashValue( buffer );
		image->next = r_imageHashTable[ hash ];
		r_imageHashTable[ hash ] = image;
	}

	return image;
}

/*
================
R_CreateImage
================
*/
image_t        *R_CreateImage( const char *name,
                               const byte *pic, int width, int height, int bits, filterType_t filterType, wrapType_t wrapType )
{
	image_t *image;

	image = R_AllocImage( name, qtrue );

	if ( !image )
	{
		return NULL;
	}

	image->type = GL_TEXTURE_2D;

	image->width = width;
	image->height = height;

	image->bits = bits;
	image->filterType = filterType;
	image->wrapType = wrapType;

	R_UploadImage( &pic, 1, image );

	return image;
}

/*
================
R_CreateGlyph
================
*/
image_t *R_CreateGlyph( const char *name, const byte *pic, int width, int height )
{
	image_t *image = R_AllocImage( name, qtrue );

	if ( !image )
	{
		return NULL;
	}

	image->type = GL_TEXTURE_2D;
	image->width = width;
	image->height = height;
	image->bits = IF_NOPICMIP;
	image->filterType = FT_LINEAR;
	image->wrapType = WT_CLAMP;

	GL_Bind( image );

	image->uploadWidth = width;
	image->uploadHeight = height;
	image->internalFormat = GL_RGBA;

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pic );

	GL_CheckErrors();

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	GL_CheckErrors();

	GL_Unbind( image );

	return image;
}

/*
================
R_CreateCubeImage
================
*/
image_t        *R_CreateCubeImage( const char *name,
                                   const byte *pic[ 6 ],
                                   int width, int height, int bits, filterType_t filterType, wrapType_t wrapType )
{
	image_t *image;

	image = R_AllocImage( name, qtrue );

	if ( !image )
	{
		return NULL;
	}

	image->type = GL_TEXTURE_CUBE_MAP;

	image->width = width;
	image->height = height;

	image->bits = bits;
	image->filterType = filterType;
	image->wrapType = wrapType;

	R_UploadImage( pic, 6, image );

	return image;
}

/*
================
R_Create3DImage
================
*/
image_t        *R_Create3DImage( const char *name,
				 const byte *pic,
				 int width, int height, int depth,
				 int bits, filterType_t filterType,
				 wrapType_t wrapType )
{
	image_t *image;
	const byte **pics;
	int i;

	image = R_AllocImage( name, qtrue );

	if ( !image )
	{
		return NULL;
	}

	image->type = GL_TEXTURE_3D;

	image->width = width;
	image->height = height;

	pics = (const byte**) ri.Hunk_AllocateTempMemory( depth * sizeof(const byte *) );
	for( i = 0; i < depth; i++ ) {
		pics[i] = pic + i * width * height * sizeof(color4ub_t);
	}

	image->bits = bits;
	image->filterType = filterType;
	image->wrapType = wrapType;

	R_UploadImage( pics, depth, image );

	ri.Hunk_FreeTempMemory( pics );

	return image;
}

static void R_LoadImage( char **buffer, byte **pic, int *width, int *height, int *bits, const char *materialName );
image_t     *R_LoadDDSImage( const char *name, int bits, filterType_t filterType, wrapType_t wrapType );

static qboolean ParseHeightMap( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char  *token;
	float scale;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for heightMap\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of image for heightMap\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ',' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: no matching ',' found for heightMap\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );
	scale = atof( token );

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for heightMap\n", token );
		return qfalse;
	}

	R_HeightMapToNormalMap( *pic, *width, *height, scale );

	*bits &= ~IF_ALPHA;
	*bits |= IF_NORMALMAP;

	return qtrue;
}

static qboolean ParseDisplaceMap( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;
	byte *pic2;
	int  width2, height2;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for displaceMap\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of first image for displaceMap\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ',' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: no matching ',' found for displaceMap\n" );
		return qfalse;
	}

	R_LoadImage( text, &pic2, &width2, &height2, bits, materialName );

	if ( !pic2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of second image for displaceMap\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for displaceMap\n", token );
	}

	if ( *width != width2 || *height != height2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: images for displaceMap have different dimensions (%i x %i != %i x %i)\n",
		           *width, *height, width2, height2 );

		//ri.Free(*pic);
		//*pic = NULL;

		ri.Free( pic2 );
		return qfalse;
	}

	R_DisplaceMap( *pic, pic2, *width, *height );

	ri.Free( pic2 );

	*bits &= ~IF_ALPHA;
	*bits |= IF_NORMALMAP;
	*bits |= IF_DISPLACEMAP;

	return qtrue;
}

static qboolean ParseAddGloss( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;
	byte *pic2;
	int  width2, height2;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for addGloss\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of first image ( specular ) for addGloss\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ',' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: no matching ',' found for addGloss\n" );
		return qfalse;
	}

	R_LoadImage( text, &pic2, &width2, &height2, bits, materialName );

	if ( !pic2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of second image ( gloss ) for addGloss\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for addGloss\n", token );
	}

	if ( *width != width2 || *height != height2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: images for specularMap and glossMap have different dimensions (%i x %i != %i x %i)\n",
		           *width, *height, width2, height2 );

		//ri.Free(*pic);
		//*pic = NULL;

		ri.Free( pic2 );
		return qfalse;
	}

	R_AddGloss( *pic, pic2, *width, *height );

	ri.Free( pic2 );

	*bits &= ~IF_ALPHA;

	return qtrue;
}

static qboolean ParseAddNormals( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;
	byte *pic2;
	int  width2, height2;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for addNormals\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of first image for addNormals\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ',' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: no matching ',' found for addNormals\n" );
		return qfalse;
	}

	R_LoadImage( text, &pic2, &width2, &height2, bits, materialName );

	if ( !pic2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of second image for addNormals\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for addNormals\n", token );
	}

	if ( *width != width2 || *height != height2 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: images for addNormals have different dimensions (%i x %i != %i x %i)\n",
		           *width, *height, width2, height2 );

		//ri.Free(*pic);
		//*pic = NULL;

		ri.Free( pic2 );
		return qfalse;
	}

	R_AddNormals( *pic, pic2, *width, *height );

	ri.Free( pic2 );

	*bits &= ~IF_ALPHA;
	*bits |= IF_NORMALMAP;

	return qtrue;
}

static qboolean ParseInvertAlpha( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for invertAlpha\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of image for invertAlpha\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for invertAlpha\n", token );
		return qfalse;
	}

	R_InvertAlpha( *pic, *width, *height );

	return qtrue;
}

static qboolean ParseInvertColor( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for invertColor\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of image for invertColor\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for invertColor\n", token );
		return qfalse;
	}

	R_InvertColor( *pic, *width, *height );

	return qtrue;
}

static qboolean ParseMakeIntensity( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for makeIntensity\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of image for makeIntensity\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for makeIntensity\n", token );
		return qfalse;
	}

	R_MakeIntensity( *pic, *width, *height );

	*bits &= ~IF_ALPHA;
	*bits &= ~IF_NORMALMAP;

	return qtrue;
}

static qboolean ParseMakeAlpha( char **text, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != '(' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '(', found '%s' for makeAlpha\n", token );
		return qfalse;
	}

	R_LoadImage( text, pic, width, height, bits, materialName );

	if ( !pic )
	{
		ri.Printf( PRINT_WARNING, "WARNING: failed loading of image for makeAlpha\n" );
		return qfalse;
	}

	token = Com_ParseExt( text, qfalse );

	if ( token[ 0 ] != ')' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting ')', found '%s' for makeAlpha\n", token );
		return qfalse;
	}

	R_MakeAlpha( *pic, *width, *height );

//	*bits |= IF_ALPHA;
	*bits &= IF_NORMALMAP; //hypov8 note: is this is correct? (&=~). and should be alpha?

	return qtrue;
}

typedef struct
{
	char *ext;
	void ( *ImageLoader )( const char *, unsigned char **, int *, int *, byte );
} imageExtToLoaderMap_t;

// Note that the ordering indicates the order of preference used
// when there are multiple images of different formats available
static const imageExtToLoaderMap_t imageLoaders[] =
{
#ifdef USE_IMAGE_WEBP
	{ "webp", LoadWEBP },
#endif
	{ "png",  LoadPNG  },
	{ "tga",  LoadTGA  },
	{ "jpg",  LoadJPG  },
	{ "jpeg", LoadJPG  },
//	{"dds", LoadDDS},  // need to write some direct uploader routines first
//	{"hdr", LoadRGBE}  // RGBE just sucks
};

static int                   numImageLoaders = ARRAY_LEN( imageLoaders );

#if defined(HYPODEBUG_MAP_PRINT)
//int load_start; // = Sys_Milliseconds(), end;

/*
=================
R_Print_ImageLoad

print map textures on load with adding string xcopy command
use to generate pk3 files for map
com_printmap xcopy -t -x
=================
*/
static void R_Print_ImageLoad(char * name)
{
	cvar_t * var = Cvar_Get("com_printmap", "", 0);

	if (var->string[0])
	{
		int l = strlen(name);
		//int end = Sys_Milliseconds();
		//skip any known KingpinQ3 files
		if (Q_strnicmp(name, "textures/strombine", 18) && 			
			Q_strnicmp(name, "textures/decals/", 16) && 
			Q_strnicmp(name, "textures/method/", 16) && 
			Q_strnicmp(name, "textures/color", 14) && 
			Q_strnicmp(name, "textures/kpq3_", 14) && 
			Q_strnicmp(name, "textures/misc_", 14) &&
			Q_strnicmp(name, "lights/kpq3/", 12) && 
			//Q_strnicmp(name, "cubemaps/", 9) && //hypov8 todo: move default?
			Q_strnicmp(name, "sprites/", 8) && 
			Q_strnicmp(name, "gfx/", 4) &&
			Q_strnicmp(name, "ui/", 3)
			)
		{
			//Com_Printf("load image time=: %i msec. %s\n", end - start, buffer); //add hypo degbug
			Com_Printf("%s \"%s\"\n", var->string, name); //add hypov8
			//write file or save to zip?
		}
	}
}
#endif

/*
=================
R_LoadImage

Loads any of the supported image types into a canonical
32 bit format.
=================
*/
static void R_LoadImage( char **buffer, byte **pic, int *width, int *height, int *bits, const char *materialName )
{
	char *token;

	*pic = NULL; //hypov8 note: posible mem leak when cubemap fails
	*width = 0;
	*height = 0;

	token = Com_ParseExt( buffer, qfalse );

	if ( !token[ 0 ] )
	{
		ri.Printf( PRINT_WARNING, "WARNING: NULL parameter for R_LoadImage\n" );
		return;
	}

	//ri.Printf(PRINT_ALL, "R_LoadImage: token '%s'\n", token);

	// heightMap(<map>, <float>)  Turns a grayscale height map into a normal map. <float> varies the bumpiness
	if ( !Q_stricmp( token, "heightMap" ) )
	{
		if ( !ParseHeightMap( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse heightMap(<map>, <float>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// displaceMap(<map>, <map>)  Sets the alpha channel to an average of the second image's RGB channels.
	else if ( !Q_stricmp( token, "displaceMap" ) )
	{
		if ( !ParseDisplaceMap( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse displaceMap(<map>, <map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// addNormals(<map>, <map>)  Adds two normal maps together. Result is normalized.
	else if ( !Q_stricmp( token, "addNormals" ) )
	{
		if ( !ParseAddNormals( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse addNormals(<map>, <map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// smoothNormals(<map>)  Does a box filter on the normal map, and normalizes the result.
	else if ( !Q_stricmp( token, "smoothNormals" ) )
	{
		ri.Printf( PRINT_WARNING, "WARNING: smoothNormals(<map>) keyword not supported\n" );
	}
	// add(<map>, <map>)  Adds two images without normalizing the result
	else if ( !Q_stricmp( token, "add" ) )
	{
		ri.Printf( PRINT_WARNING, "WARNING: add(<map>, <map>) keyword not supported\n" );
	}
	// scale(<map>, <float> [,float] [,float] [,float])  Scales the RGBA by the specified factors. Defaults to 0.
	else if ( !Q_stricmp( token, "scale" ) )
	{
		ri.Printf( PRINT_WARNING, "WARNING: scale(<map>, <float> [,float] [,float] [,float]) keyword not supported\n" );
	}
	// invertAlpha(<map>)  Inverts the alpha channel (0 becomes 1, 1 becomes 0)
	else if ( !Q_stricmp( token, "invertAlpha" ) )
	{
		if ( !ParseInvertAlpha( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse invertAlpha(<map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// invertColor(<map>)  Inverts the R, G, and B channels
	else if ( !Q_stricmp( token, "invertColor" ) )
	{
		if ( !ParseInvertColor( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse invertColor(<map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// makeIntensity(<map>)  Copies the red channel to the G, B, and A channels
	else if ( !Q_stricmp( token, "makeIntensity" ) )
	{
		if ( !ParseMakeIntensity( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse makeIntensity(<map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	// makeAlpha(<map>)  Sets the alpha channel to an average of the RGB channels. Sets the RGB channels to white.
	else if ( !Q_stricmp( token, "makeAlpha" ) )
	{
		if ( !ParseMakeAlpha( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse makeAlpha(<map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	else if ( !Q_stricmp( token, "addGloss" ) )
	{
		if ( !ParseAddGloss( buffer, pic, width, height, bits, materialName ) )
		{
			if ( materialName && materialName[ 0 ] != '\0' )
			{
				ri.Printf( PRINT_WARNING, "WARNING: failed to parse addGloss(<map>, <map>) expression for shader '%s'\n", materialName );
			}
		}
	}
	else
	{
		qboolean   orgNameFailed = qfalse;
		int        i;
		const char *ext;
		char       filename[ MAX_QPATH ];
		byte       alphaByte;

		// Tr3B: clear alpha of normalmaps for displacement mapping
		if ( *bits & IF_NORMALMAP )
		{
			alphaByte = 0x00;
		}
		else
		{
			alphaByte = 0xFF;
		}

		Q_strncpyz( filename, token, sizeof( filename ) );

		ext = COM_GetExtension( filename );

		if ( *ext )
		{
			// look for the correct loader and use it
			for ( i = 0; i < numImageLoaders; i++ )
			{
				if ( !Q_stricmp( ext, imageLoaders[ i ].ext ) )
				{
					// load
					imageLoaders[ i ].ImageLoader( filename, pic, width, height, alphaByte );
					break;
				}
			}

			// a loader was found
			if ( i < numImageLoaders )
			{
				if ( *pic == NULL )
				{
					// loader failed, most likely because the file isn't there;
					// try again without the extension
					orgNameFailed = qtrue;
					COM_StripExtension3( token, filename, MAX_QPATH );
				}
				else
				{
#if defined(HYPODEBUG_MAP_PRINT)
					R_Print_ImageLoad(filename);
#endif
					// something loaded
					return;
				}
			}
		}

		// try and find a suitable match using all the image formats supported
		for ( i = 0; i < numImageLoaders; i++ )
		{
			char *altName = va( "%s.%s", filename, imageLoaders[ i ].ext );

			// load
			imageLoaders[ i ].ImageLoader( altName, pic, width, height, alphaByte );

			if ( *pic )
			{
				if ( orgNameFailed )
				{
					//ri.Printf(PRINT_DEVELOPER, "WARNING: %s not present, using %s instead\n", token, altName);
				}
#if defined(HYPODEBUG_MAP_PRINT)
				R_Print_ImageLoad(altName);
#endif
				break;
			}
		}
	}
}

/*
===============
R_FindImageFile

Finds or loads the given image.
Returns NULL if it fails, not a default image.
==============
*/
image_t        *R_FindImageFile( const char *imageName, int bits, filterType_t filterType, wrapType_t wrapType, const char *materialName )
{
	image_t       *image = NULL;
	int           width = 0, height = 0;
	byte          *pic = NULL;
	long          hash;
	char          buffer[ 1024 ];
	//char          ddsName[ 1024 ];
	char          *buffer_p;
	unsigned long diff;
#if defined(HYPODEBUG_IMG_TIME)
	int start;
	start = Sys_Milliseconds();
#endif

	if ( !imageName )
	{
		return NULL;
	}

	Q_strncpyz( buffer, imageName, sizeof( buffer ) );
	hash = GenerateImageHashValue( buffer );

//  ri.Printf(PRINT_ALL, "R_FindImageFile: buffer '%s'\n", buffer);

	// see if the image is already loaded
	for ( image = r_imageHashTable[ hash ]; image; image = image->next )
	{
		if (!Q_stricmpn(buffer, image->name, sizeof(image->name)))
		{
			// the white image can be used with any set of parms, but other mismatches are errors
			if ( Q_stricmp( buffer, "_white" ) )
			{
				diff = bits ^ image->bits;

				/*
				   if(diff & IF_NOMIPMAPS)
				   {
				   ri.Printf(PRINT_DEVELOPER, "WARNING: reused image %s with mixed mipmap parm\n", name);
				   }
				 */

				if ( diff & IF_NOPICMIP )
				{
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image '%s' with mixed allowPicmip parm for shader '%s\n", imageName, materialName );
				}

				if ( image->wrapType != wrapType )
				{
					ri.Printf( PRINT_WARNING, "WARNING: reused image '%s' with mixed glWrapType parm for shader '%s'\n", imageName, materialName );
				}
			}
			return image;
		}
	}

#if 0
	if ( glConfig.textureCompression == TC_S3TC && !( bits & IF_NOCOMPRESSION ) && Q_strnicmp( imageName, "fonts", 5 ) )
	{
		Q_strncpyz( ddsName, imageName, sizeof( ddsName ) );
		COM_StripExtension3( ddsName, ddsName, sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), ".dds" );
		// try to load a customized .dds texture

		image = R_LoadDDSImage( ddsName, bits, filterType, wrapType );
		if ( image != NULL )
		{
			ri.Printf( PRINT_ALL, "found custom .dds '%s'\n", ddsName );
			return image;
		}
	}
#endif

#if 0
	else if ( r_tryCachedDDSImages->integer && !( bits & IF_NOCOMPRESSION ) && Q_strnicmp( name, "fonts", 5 ) )
	{
		Q_strncpyz( ddsName, "dds/", sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), name );
		COM_StripExtension3( ddsName, ddsName, sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), ".dds" );

		// try to load a cached .dds texture from the XreaL/<mod>/dds/ folder
		image = R_LoadDDSImage( ddsName, bits, filterType, wrapType );

		if ( image != NULL )
		{
			ri.Printf( PRINT_ALL, "found cached .dds '%s'\n", ddsName );
			return image;
		}
	}
#endif

	// load the pic from disk
	buffer_p = &buffer[ 0 ];
	R_LoadImage( &buffer_p, &pic, &width, &height, &bits, materialName );

	if ( pic == NULL )
	{
		return NULL;
	}

#if /*defined( COMPAT_KPQ3 ) ||*/ defined( COMPAT_ET )
	if ( bits & IF_LIGHTMAP )
	{
		R_ProcessLightmap( pic, 4, width, height, pic );

		bits |= IF_NOCOMPRESSION;
	}
#endif
#if 0
	//if(r_tryCachedDDSImages->integer && !(bits & IF_NOCOMPRESSION) && Q_strnicmp(name, "fonts", 5))
	{
		// try to cache a .dds texture to the XreaL/<mod>/dds/ folder
		SavePNG( ddsName, pic, width, height, 4, qtrue );
	}
#endif

	image = R_CreateImage( ( char * ) buffer, pic, width, height, bits, filterType, wrapType );
	ri.Free( pic );

#if defined(HYPODEBUG_IMG_TIME)
	Com_Printf("Image load time: %3i msec. (%s)\n", Sys_Milliseconds() - start, buffer);
#endif

	return image;
}

static void R_Flip( byte *in, int width, int height )
{
	int32_t *data = (int32_t *) in;
	int x, y;

	for ( y = 0; y < height; y++ )
	{
		for ( x = 0; x < width / 2; x++ )
		{
			int32_t texel = data[ x ];
			data[ x ] = data[ width - 1 - x ];
			data[ width - 1 - x ] = texel;
		}
		data += width;
	}
}

static void R_Flop( byte *in, int width, int height )
{
	int32_t *upper = (int32_t *) in;
	int32_t *lower = (int32_t *) in + ( height - 1 ) * width;
	int     x, y;

	for ( y = 0; y < height / 2; y++ )
	{
		for ( x = 0; x < width; x++ )
		{
			int32_t texel = upper[ x ];
			upper[ x ] = lower[ x ];
			lower[ x ] = texel;
		}

		upper += width;
		lower -= width;
	}
}

static void R_Rotate( byte *in, int width, int height, int degrees )
{
	byte color[ 4 ];
	int  x, y, x2, y2;
	byte *out, *tmp;

	tmp = (byte*) ri.Hunk_AllocateTempMemory( width * height * 4 );

	// rotate into tmp buffer
	for ( y = 0; y < height; y++ )
	{
		for ( x = 0; x < width; x++ )
		{
			color[ 0 ] = in[ 4 * ( y * width + x ) + 0 ];
			color[ 1 ] = in[ 4 * ( y * width + x ) + 1 ];
			color[ 2 ] = in[ 4 * ( y * width + x ) + 2 ];
			color[ 3 ] = in[ 4 * ( y * width + x ) + 3 ];

			if ( degrees == 90 )
			{
				x2 = y;
				y2 = ( height - ( 1 + x ) );

				tmp[ 4 * ( y2 * width + x2 ) + 0 ] = color[ 0 ];
				tmp[ 4 * ( y2 * width + x2 ) + 1 ] = color[ 1 ];
				tmp[ 4 * ( y2 * width + x2 ) + 2 ] = color[ 2 ];
				tmp[ 4 * ( y2 * width + x2 ) + 3 ] = color[ 3 ];
			}
			else if ( degrees == -90 )
			{
				x2 = ( width - ( 1 + y ) );
				y2 = x;
 
				tmp[ 4 * ( y2 * width + x2 ) + 0 ] = color[ 0 ];
				tmp[ 4 * ( y2 * width + x2 ) + 1 ] = color[ 1 ];
				tmp[ 4 * ( y2 * width + x2 ) + 2 ] = color[ 2 ];
				tmp[ 4 * ( y2 * width + x2 ) + 3 ] = color[ 3 ];
			}
			else
			{
				tmp[ 4 * ( y * width + x ) + 0 ] = color[ 0 ];
				tmp[ 4 * ( y * width + x ) + 1 ] = color[ 1 ];
				tmp[ 4 * ( y * width + x ) + 2 ] = color[ 2 ];
				tmp[ 4 * ( y * width + x ) + 3 ] = color[ 3 ];
			}
		}
	}

	// copy back to input
	out = in;

	for ( y = 0; y < height; y++ )
	{
		for ( x = 0; x < width; x++ )
		{
			out[ 4 * ( y * width + x ) + 0 ] = tmp[ 4 * ( y * width + x ) + 0 ];
			out[ 4 * ( y * width + x ) + 1 ] = tmp[ 4 * ( y * width + x ) + 1 ];
			out[ 4 * ( y * width + x ) + 2 ] = tmp[ 4 * ( y * width + x ) + 2 ];
			out[ 4 * ( y * width + x ) + 3 ] = tmp[ 4 * ( y * width + x ) + 3 ];
		}
	}

	ri.Hunk_FreeTempMemory( tmp );
}

/*
===============
R_SubImageCpy

Copies between a smaller image and a larger image.
Last flag controls is copy in or out of larger image.

e.g.
dest = malloc(4*4*channels);
[________]
[________]
[________]
[________]
src = malloc(2*2*channels);
[____]
[____]
R_SubImageCpy(dest, 0, 0, 4, 4, src, 2, 2, channels);
[____]___]
[____]___]
[________]
[________]
===============
*/
void R_SubImageCpy( byte *dest, size_t destx, size_t desty, size_t destw, size_t desth, byte *src, size_t srcw, size_t srch, size_t bytes )
{
	size_t s_rowBytes = srcw * bytes;
	size_t d_rowBytes = destw * bytes;
	byte   *d = dest + ( ( destx * bytes ) + ( desty * d_rowBytes ) );
	byte   *d_max = dest + ( destw * desth * bytes ) - s_rowBytes;
	byte   *s = src;
	byte   *s_max = src + ( srcw * srch * bytes ) - s_rowBytes;

	while ( ( s <= s_max ) && ( d <= d_max ) )
	{
		memcpy( d, s, s_rowBytes );
		d += d_rowBytes;
		s += s_rowBytes;
	}
}

/*
===============
R_FindCubeImage_Free

fix for posible mem leak
clear on failed images
===============
*/
void R_FindCubeImage_Free(byte **pic)
{
	int i;
	for ( i = 0; i < 6; i++ )
	{
		if ( pic[ i ] )
		{
			ri.Free( pic[ i ] ); //(void*)
		}
	}
}

/*
===============
R_FindCubeImage

Finds or loads the given image.
Returns NULL if it fails, not a default image.

Tr3B: fear the use of goto
==============
*/
image_t        *R_FindCubeImage( const char *imageName, int bits, filterType_t filterType, wrapType_t wrapType, const char *materialName )
{
	int         i;
	image_t     *image = NULL;
	int         width = 0, height = 0;
	byte        *pic[ 6 ];
	long        hash;

	static char *openglSuffices[ 6 ] = { "px", "nx", "py", "ny", "pz", "nz" };

	static char     *doom3Suffices[ 6 ] = { "forward", "back", "left", "right", "up", "down" };
	static qboolean doom3FlipX[ 6 ] = { qtrue,         qtrue,  qfalse, qtrue,  qtrue,  qfalse };
	static qboolean doom3FlipY[ 6 ] = { qfalse,        qfalse, qtrue,  qfalse, qfalse, qtrue };
	static int      doom3Rot[ 6 ]   = { 90,            -90,    0,      0,      90,     -90 };

	static char     *quakeSuffices[ 6 ] = { "rt", "lf",  "bk",    "ft",   "up",   "dn" };
	static qboolean quakeFlipX[ 6 ] = { qtrue,    qtrue,  qfalse, qtrue,  qtrue,  qfalse };
	static qboolean quakeFlipY[ 6 ] = { qfalse,   qfalse, qtrue,  qfalse, qfalse, qtrue };
	static int      quakeRot[ 6 ]   = { 90,       -90,    0,      0,      90,     -90 };

	int             bitsIgnore;
	char            buffer[ 1024 ], filename[ 1024 ];
	char            ddsName[ 1024 ];
	char            *filename_p;
#if defined(HYPODEBUG_IMG_TIME)
	int start;
	start = Sys_Milliseconds();
#endif

	if ( !imageName )
	{
		return NULL;
	}

	Q_strncpyz( buffer, imageName, sizeof( buffer ) );
	hash = GenerateImageHashValue( buffer );

	// see if the image is already loaded
	for ( image = r_imageHashTable[ hash ]; image; image = image->next )
	{
		if ( !Q_stricmp( buffer, image->name ) )
		{
			return image;
		}
	}

	if ( glConfig.textureCompression == TC_S3TC && !( bits & IF_NOCOMPRESSION ) && Q_strnicmp( imageName, "fonts", 5 ) )
	{
		Q_strncpyz( ddsName, imageName, sizeof( ddsName ) );
		COM_StripExtension3( ddsName, ddsName, sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), ".dds" );

		// try to load a customized .dds texture
		image = R_LoadDDSImage( ddsName, bits, filterType, wrapType );

		if ( image != NULL )
		{
			ri.Printf( PRINT_ALL, "found custom .dds '%s'\n", ddsName );
			return image;
		}
	}

#if 0
	else if ( r_tryCachedDDSImages->integer && !( bits & IF_NOCOMPRESSION ) && Q_strnicmp( name, "fonts", 5 ) )
	{
		Q_strncpyz( ddsName, "dds/", sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), name );
		COM_StripExtension3( ddsName, ddsName, sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), ".dds" );

		// try to load a cached .dds texture from the XreaL/<mod>/dds/ folder
		image = R_LoadDDSImage( ddsName, bits, filterType, wrapType );

		if ( image != NULL )
		{
			ri.Printf( PRINT_ALL, "found cached .dds '%s'\n", ddsName );
			return image;
		}
	}

#endif

	for ( i = 0; i < 6; i++ )
	{
		pic[ i ] = NULL;
	}

	for ( i = 0; i < 6; i++ )
	{
		Com_sprintf( filename, sizeof( filename ), "%s_%s", buffer, openglSuffices[ i ] );

		filename_p = &filename[ 0 ];
		R_LoadImage( &filename_p, &pic[ i ], &width, &height, &bitsIgnore, materialName );

		if ( !pic[ i ] || width != height )
		{
			image = NULL;
			R_FindCubeImage_Free(pic);
			goto tryDoom3Suffices;
		}
	}

	goto createCubeImage;

tryDoom3Suffices:

	for ( i = 0; i < 6; i++ )
	{
		Com_sprintf( filename, sizeof( filename ), "%s_%s", buffer, doom3Suffices[ i ] );

		filename_p = &filename[ 0 ];
		R_LoadImage( &filename_p, &pic[ i ], &width, &height, &bitsIgnore, materialName );

		if ( !pic[ i ] || width != height )
		{
			image = NULL;
			R_FindCubeImage_Free(pic);
			goto tryQuakeSuffices;
		}

		if ( doom3FlipX[ i ] )
		{
			R_Flip( pic[ i ], width, height );
		}

		if ( doom3FlipY[ i ] )
		{
			R_Flop( pic[ i ], width, height );
		}

		R_Rotate( pic[ i ], width, height, doom3Rot[ i ] );
	}

	goto createCubeImage;

tryQuakeSuffices:

	for ( i = 0; i < 6; i++ )
	{
		Com_sprintf( filename, sizeof( filename ), "%s_%s", buffer, quakeSuffices[ i ] );

		filename_p = &filename[ 0 ];
		R_LoadImage( &filename_p, &pic[ i ], &width, &height, &bitsIgnore, materialName );

		if ( !pic[ i ] || width != height )
		{
			image = NULL;
			R_FindCubeImage_Free(pic);
			goto skipCubeImage;
		}

		if ( quakeFlipX[ i ] )
		{
			R_Flip( pic[ i ], width, height );
		}

		if ( quakeFlipY[ i ] )
		{
			R_Flop( pic[ i ], width, height );
		}

		R_Rotate( pic[ i ], width, height, quakeRot[ i ] );
	}

createCubeImage:
	image = R_CreateCubeImage( ( char * ) buffer, ( const byte ** ) pic, width, height, bits, filterType, wrapType );

skipCubeImage:

	R_FindCubeImage_Free(pic);

#if defined(HYPODEBUG_IMG_TIME)
	Com_Printf("Cubeimage  time: %3i msec. (%s)\n", Sys_Milliseconds() - start, buffer);
#endif
	return image;
}

/*
=================
R_InitFogTable
=================
*/
void R_InitFogTable( void )
{
	int   i;
	float d;
	float exp;

	exp = 0.5;

	for ( i = 0; i < FOG_TABLE_SIZE; i++ )
	{
		d = pow( ( float ) i / ( FOG_TABLE_SIZE - 1 ), exp );

		tr.fogTable[ i ] = d;
	}
}

/*
================
R_FogFactor

Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
================
*/
float R_FogFactor( float s, float t )
{
	float d;

	s -= 1.0 / 512;

	if ( s < 0 )
	{
		return 0;
	}

	if ( t < 1.0 / 32 )
	{
		return 0;
	}

	if ( t < 31.0 / 32 )
	{
		s *= ( t - 1.0f / 32.0f ) / ( 30.0f / 32.0f );
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0 )
	{
		s = 1.0;
	}

	d = tr.fogTable[( int )( s * ( FOG_TABLE_SIZE - 1 ) ) ];

	return d;
}

/*
================
R_CreateFogImage
================
*/
#define FOG_S 256
#define FOG_T 32
static void R_CreateFogImage( void )
{
	int   x, y;
	byte  *data, *ptr;
	//float           g;
	float d;
	float borderColor[ 4 ];

	ptr = data = (byte*) ri.Hunk_AllocateTempMemory( FOG_S * FOG_T * 4 );

	//g = 2.0;

	// S is distance, T is depth
	for ( y = 0; y < FOG_T; y++ )
	{
		for ( x = 0; x < FOG_S; x++ )
		{
			d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );

			ptr[ 0 ] = ptr[ 1 ] = ptr[ 2 ] = 255;
			ptr[ 3 ] = 255 * d;
			ptr += 4;
		}
	}

	// standard openGL clamping doesn't really do what we want -- it includes
	// the border color at the edges.  OpenGL 1.2 has clamp-to-edge, which does
	// what we want.
	tr.fogImage = R_CreateImage( "_fog", ( byte * ) data, FOG_S, FOG_T, IF_NOPICMIP, FT_LINEAR, WT_CLAMP );
	ri.Hunk_FreeTempMemory( data );

	borderColor[ 0 ] = 1.0;
	borderColor[ 1 ] = 1.0;
	borderColor[ 2 ] = 1.0;
	borderColor[ 3 ] = 1;

	glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
}

/*
==================
R_CreateDefaultImage
==================
*/
#define DEFAULT_SIZE 128
#define MINIMAGE_SIZE 8
static void R_CreateDefaultImage( void )
{
	int  x;
	byte data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset( data, 32, sizeof( data ) );

	for ( x = 0; x < DEFAULT_SIZE; x++ )
	{
		data[ 0 ][ x ][ 0 ] = data[ 0 ][ x ][ 1 ] = data[ 0 ][ x ][ 2 ] = data[ 0 ][ x ][ 3 ] = 255;
		data[ x ][ 0 ][ 0 ] = data[ x ][ 0 ][ 1 ] = data[ x ][ 0 ][ 2 ] = data[ x ][ 0 ][ 3 ] = 255;

		data[ DEFAULT_SIZE - 1 ][ x ][ 0 ] =
		  data[ DEFAULT_SIZE - 1 ][ x ][ 1 ] = data[ DEFAULT_SIZE - 1 ][ x ][ 2 ] = data[ DEFAULT_SIZE - 1 ][ x ][ 3 ] = 255;

		data[ x ][ DEFAULT_SIZE - 1 ][ 0 ] =
		  data[ x ][ DEFAULT_SIZE - 1 ][ 1 ] = data[ x ][ DEFAULT_SIZE - 1 ][ 2 ] = data[ x ][ DEFAULT_SIZE - 1 ][ 3 ] = 255;
	}

	tr.defaultImage = R_CreateImage( "_default", ( byte * ) data, DEFAULT_SIZE, DEFAULT_SIZE, IF_NOPICMIP, FT_DEFAULT, WT_REPEAT );
}

static void R_CreateRandomNormalsImage( void )
{
	int  x, y;
	byte data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];
	byte *ptr = &data[0][0][0];

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset( data, 32, sizeof( data ) );

	for ( y = 0; y < DEFAULT_SIZE; y++ )
	{
		for ( x = 0; x < DEFAULT_SIZE; x++ )
		{
			vec3_t n;
			float  r, angle;

			r = random();
			angle = 2.0 * M_PI * r; // / 360.0;

			VectorSet( n, cos( angle ), sin( angle ), r );
			VectorNormalize( n );

			//VectorSet(n, crandom(), crandom(), crandom());

			ptr[ 0 ] = ( byte )( 128 + 127 * n[ 0 ] );
			ptr[ 1 ] = ( byte )( 128 + 127 * n[ 1 ] );
			ptr[ 2 ] = ( byte )( 128 + 127 * n[ 2 ] );
			ptr[ 3 ] = 255;
			ptr += 4;
		}
	}

	tr.randomNormalsImage = R_CreateImage( "_randomNormals", ( byte * ) data, DEFAULT_SIZE, DEFAULT_SIZE, IF_NOPICMIP, FT_DEFAULT, WT_REPEAT );
}

static void R_CreateNoFalloffImage( void )
{
	byte data[ MINIMAGE_SIZE ][ MINIMAGE_SIZE ][ 4 ];

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, sizeof( data ) );
	tr.noFalloffImage = R_CreateImage( "_noFalloff", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP );
}

#define ATTENUATION_XY_SIZE 128
static void R_CreateAttenuationXYImage( void )
{
	int  x, y;
	byte data[ ATTENUATION_XY_SIZE ][ ATTENUATION_XY_SIZE ][ 4 ];
	byte *ptr = &data[0][0][0];
	int  b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for ( y = 0; y < ATTENUATION_XY_SIZE; y++ )
	{
		for ( x = 0; x < ATTENUATION_XY_SIZE; x++ )
		{
			float d;

			d = ( ATTENUATION_XY_SIZE / 2 - 0.5f - x ) * ( ATTENUATION_XY_SIZE / 2 - 0.5f - x ) +
			    ( ATTENUATION_XY_SIZE / 2 - 0.5f - y ) * ( ATTENUATION_XY_SIZE / 2 - 0.5f - y );
			b = 4000 / d;

			if ( b > 255 )
			{
				b = 255;
			}
			else if ( b < 75 )
			{
				b = 0;
			}

			ptr[ 0 ] = ptr[ 1 ] = ptr[ 2 ] = b;
			ptr[ 3 ] = 255;
			ptr += 4;
		}
	}

	tr.attenuationXYImage =
	  R_CreateImage( "_attenuationXY", ( byte * ) data, ATTENUATION_XY_SIZE, ATTENUATION_XY_SIZE, IF_NOPICMIP, FT_LINEAR,
	                 WT_CLAMP );
}

static void R_CreateContrastRenderFBOImage( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth * 0.25f;
		height = glConfig.vidHeight * 0.25f;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth ) * 0.25f;
		height = NearestPowerOfTwo( glConfig.vidHeight ) * 0.25f;
	}

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.contrastRenderFBOImage = R_CreateImage( "_contrastRenderFBO", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION | IF_RGBA16F, FT_LINEAR, WT_CLAMP );
	}
	else
	{
		tr.contrastRenderFBOImage = R_CreateImage( "_contrastRenderFBO", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_LINEAR, WT_CLAMP );
	}
}

static void R_CreateBloomRenderFBOImage( void )
{
	int  i;
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth * 0.25f;
		height = glConfig.vidHeight * 0.25f;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth ) * 0.25f;
		height = NearestPowerOfTwo( glConfig.vidHeight ) * 0.25f;
	}

	for ( i = 0; i < 2; i++ )
	{
		if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
		{
			tr.bloomRenderFBOImage[ i ] = R_CreateImage( va( "_bloomRenderFBO%d", i ), NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION | IF_RGBA16F, FT_LINEAR, WT_CLAMP );
		}
		else
		{
			tr.bloomRenderFBOImage[ i ] = R_CreateImage( va( "_bloomRenderFBO%d", i ), NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_LINEAR, WT_CLAMP );
		}
	}
}

static void R_CreateCurrentRenderImage( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

	tr.currentRenderImage = R_CreateImage( "_currentRender", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
}

static void R_CreateDepthRenderImage( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

#if 0

	if ( glConfig2.framebufferPackedDepthStencilAvailable )
	{
		tr.depthRenderImage = R_CreateImage( "_depthRender", NULL, width, height, IF_NOPICMIP | IF_PACKED_DEPTH24_STENCIL8, FT_NEAREST, WT_CLAMP );
	}
	else if ( glConfig.hardwareType == GLHW_ATI || glConfig.hardwareType == GLHW_ATI_DX10 ) // || glConfig.hardwareType == GLHW_NV_DX10)
	{
		tr.depthRenderImage = R_CreateImage( "_depthRender", NULL, width, height, IF_NOPICMIP | IF_DEPTH16, FT_NEAREST, WT_CLAMP );
	}
	else
#endif
	{
		tr.depthRenderImage = R_CreateImage( "_depthRender", NULL, width, height, IF_NOPICMIP | IF_DEPTH24, FT_NEAREST, WT_CLAMP );
	}
}

static void R_CreatePortalRenderImage( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.portalRenderImage = R_CreateImage( "_portalRender", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.portalRenderImage = R_CreateImage( "_portalRender", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}
}

static void R_CreateOcclusionRenderFBOImage( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

	//
#if 0

	if ( glConfig.hardwareType == GLHW_ATI_DX10 || glConfig.hardwareType == GLHW_NV_DX10 )
	{
		tr.occlusionRenderFBOImage = R_CreateImage( "_occlusionFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA16F, FT_NEAREST, WT_CLAMP );
	}
	else if ( glConfig2.framebufferPackedDepthStencilAvailable )
	{
		tr.occlusionRenderFBOImage = R_CreateImage( "_occlusionFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA32F, FT_NEAREST, WT_CLAMP );
	}
	else
#endif
	{
		tr.occlusionRenderFBOImage = R_CreateImage( "_occlusionFBORender", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}
}

static void R_CreateDepthToColorFBOImages( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

#if 0

	if ( glConfig.hardwareType == GLHW_ATI_DX10 )
	{
		tr.depthToColorBackFacesFBOImage = R_CreateImage( "_depthToColorBackFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA16F, FT_NEAREST, WT_CLAMP );
		tr.depthToColorFrontFacesFBOImage = R_CreateImage( "_depthToColorFrontFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA16F, FT_NEAREST, WT_CLAMP );
	}
	else if ( glConfig.hardwareType == GLHW_NV_DX10 )
	{
		tr.depthToColorBackFacesFBOImage = R_CreateImage( "_depthToColorBackFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA32F, FT_NEAREST, WT_CLAMP );
		tr.depthToColorFrontFacesFBOImage = R_CreateImage( "_depthToColorFrontFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA32F, FT_NEAREST, WT_CLAMP );
	}
	else if ( glConfig2.framebufferPackedDepthStencilAvailable )
	{
		tr.depthToColorBackFacesFBOImage = R_CreateImage( "_depthToColorBackFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA32F, FT_NEAREST, WT_CLAMP );
		tr.depthToColorFrontFacesFBOImage = R_CreateImage( "_depthToColorFrontFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_ALPHA32F, FT_NEAREST, WT_CLAMP );
	}
	else
#endif
	{
		tr.depthToColorBackFacesFBOImage = R_CreateImage( "_depthToColorBackFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
		tr.depthToColorFrontFacesFBOImage = R_CreateImage( "_depthToColorFrontFacesFBORender", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}
}

// Tr3B: clean up this mess some day ...
static void R_CreateDownScaleFBOImages( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth * 0.25f;
		height = glConfig.vidHeight * 0.25f;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth * 0.25f );
		height = NearestPowerOfTwo( glConfig.vidHeight * 0.25f );
	}

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.downScaleFBOImage_quarter = R_CreateImage( "_downScaleFBOImage_quarter", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.downScaleFBOImage_quarter = R_CreateImage( "_downScaleFBOImage_quarter", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}

	width = height = 64;

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.downScaleFBOImage_64x64 = R_CreateImage( "_downScaleFBOImage_64x64", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.downScaleFBOImage_64x64 = R_CreateImage( "_downScaleFBOImage_64x64", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}

#if 0
	width = height = 16;

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.downScaleFBOImage_16x16 = R_CreateImage( "_downScaleFBOImage_16x16", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.downScaleFBOImage_16x16 = R_CreateImage( "_downScaleFBOImage_16x16", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}

	width = height = 4;

	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.downScaleFBOImage_4x4 = R_CreateImage( "_downScaleFBOImage_4x4", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.downScaleFBOImage_4x4 = R_CreateImage( "_downScaleFBOImage_4x4", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}

	width = height = 1;
	if ( r_hdrRendering->integer && glConfig2.textureFloatAvailable )
	{
		tr.downScaleFBOImage_1x1 = R_CreateImage( "_downScaleFBOImage_1x1", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.downScaleFBOImage_1x1 = R_CreateImage( "_downScaleFBOImage_1x1", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}
#endif
}

static void R_CreateDeferredRenderFBOImages( void )
{
	int  width, height;

	if ( glConfig2.textureNPOTAvailable )
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	else
	{
		width = NearestPowerOfTwo( glConfig.vidWidth );
		height = NearestPowerOfTwo( glConfig.vidHeight );
	}

	if ( HDR_ENABLED() )
	{
		tr.deferredRenderFBOImage = R_CreateImage( "_deferredRenderFBO", NULL, width, height, IF_NOPICMIP | IF_RGBA16F, FT_NEAREST, WT_CLAMP );
	}
	else
	{
		tr.deferredRenderFBOImage = R_CreateImage( "_deferredRenderFBO", NULL, width, height, IF_NOPICMIP | IF_NOCOMPRESSION, FT_NEAREST, WT_CLAMP );
	}
}

// *INDENT-OFF*
static void R_CreateShadowMapFBOImage( void )
{
	int  i;
	int  width, height;
	int numShadowMaps = ( r_softShadowsPP->integer && r_shadows->integer >= SHADOWING_VSM16 ) ? MAX_SHADOWMAPS * 2 : MAX_SHADOWMAPS;
	int format;
	filterType_t filter;

	if ( !glConfig2.textureFloatAvailable || r_shadows->integer < SHADOWING_ESM16 )
	{
		return;
	}

	if ( r_shadows->integer == SHADOWING_ESM32 )
	{
		format = IF_NOPICMIP | IF_ONECOMP32F;
	}
	else if ( r_shadows->integer == SHADOWING_VSM32 )
	{
		format = IF_NOPICMIP | IF_TWOCOMP32F;
	}
	else if ( r_shadows->integer == SHADOWING_EVSM32 )
	{
		if ( r_evsmPostProcess->integer )
		{
			format = IF_NOPICMIP | IF_ONECOMP32F;
		}
		else
		{
			format = IF_NOPICMIP | IF_RGBA32F;
		}
	}
	else if ( r_shadows->integer == SHADOWING_ESM16 )
	{
		format = IF_NOPICMIP | IF_ONECOMP16F;
	}
	else if ( r_shadows->integer == SHADOWING_VSM16 )
	{
		format = IF_NOPICMIP | IF_TWOCOMP16F;
	}
	else
	{
		format = IF_NOPICMIP | IF_RGBA16F;
	}
	if( r_shadowMapLinearFilter->integer )
	{
		filter = FT_LINEAR;
	}
	else
	{
		filter = FT_NEAREST;
	}

	for ( i = 0; i < numShadowMaps; i++ )
	{
		width = height = shadowMapResolutions[ i % MAX_SHADOWMAPS ];

		tr.shadowMapFBOImage[ i ] = R_CreateImage( va( "_shadowMapFBO%d", i ), NULL, width, height, format, filter, WT_ONE_CLAMP );
		tr.shadowClipMapFBOImage[ i ] = R_CreateImage( va( "_shadowClipMapFBO%d", i ), NULL, width, height, format, filter, WT_ONE_CLAMP );
	}

	// sun shadow maps
	for ( i = 0; i < numShadowMaps; i++ )
	{
		width = height = sunShadowMapResolutions[ i % MAX_SHADOWMAPS ];

		tr.sunShadowMapFBOImage[ i ] = R_CreateImage( va( "_sunShadowMapFBO%d", i ), NULL, width, height, format, filter, WT_ONE_CLAMP );
		tr.sunShadowClipMapFBOImage[ i ] = R_CreateImage( va( "_sunShadowClipMapFBO%d", i ), NULL, width, height, format, filter, WT_ONE_CLAMP );
	}
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateShadowCubeFBOImage( void )
{
	int  i, j;
	int  width, height;
	byte *data[ 6 ];
	int format;
	filterType_t filter;

	if ( !glConfig2.textureFloatAvailable || r_shadows->integer < SHADOWING_ESM16 )
	{
		return;
	}

	if ( r_shadows->integer == SHADOWING_ESM32 )
	{
		format = IF_NOPICMIP | IF_ONECOMP32F;
	}
	else if ( r_shadows->integer == SHADOWING_VSM32 )
	{
		format = IF_NOPICMIP | IF_TWOCOMP32F;
	}
	else if ( r_shadows->integer == SHADOWING_EVSM32 )
	{
		if ( r_evsmPostProcess->integer )
		{
			format = IF_NOPICMIP | IF_ONECOMP32F;
		}
		else
		{
			format = IF_NOPICMIP | IF_RGBA32F;
		}
	}
	else if ( r_shadows->integer == SHADOWING_ESM16 )
	{
		format = IF_NOPICMIP | IF_ONECOMP16F;
	}
	else if ( r_shadows->integer == SHADOWING_VSM16 )
	{
		format = IF_NOPICMIP | IF_TWOCOMP16F;
	}
	else
	{
		format = IF_NOPICMIP | IF_RGBA16F;
	}
	if( r_shadowMapLinearFilter->integer )
	{
		filter = FT_LINEAR;
	}
	else
	{
		filter = FT_NEAREST;
	}

	for ( j = 0; j < 5; j++ )
	{
		width = height = shadowMapResolutions[ j ];

		for ( i = 0; i < 6; i++ )
		{
			data[ i ] = NULL;
		}

		tr.shadowCubeFBOImage[ j ] = R_CreateCubeImage( va( "_shadowCubeFBO%d", j ), ( const byte ** ) data, width, height, format, filter, WT_EDGE_CLAMP );
		tr.shadowClipCubeFBOImage[ j ] = R_CreateCubeImage( va( "_shadowClipCubeFBO%d", j ), ( const byte ** ) data, width, height, format, filter, WT_EDGE_CLAMP );
	}
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateBlackCubeImage( void )
{
	int  i;
	int  width, height;
	byte *data[ 6 ];

	width = REF_CUBEMAP_SIZE;
	height = REF_CUBEMAP_SIZE;

	for ( i = 0; i < 6; i++ )
	{
		data[ i ] = (byte*) ri.Hunk_AllocateTempMemory( width * height * 4 );
		Com_Memset( data[ i ], 0, width * height * 4 );
	}

	tr.blackCubeImage = R_CreateCubeImage( "_blackCube", ( const byte ** ) data, width, height, IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP );
	tr.autoCubeImage = R_CreateCubeImage( "_autoCube", ( const byte ** ) data, width, height, IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP );

	for ( i = 5; i >= 0; i-- )
	{
		ri.Hunk_FreeTempMemory( data[ i ] );
	}
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateWhiteCubeImage( void )
{
	int  i;
	int  width, height;
	byte *data[ 6 ];

	width = REF_CUBEMAP_SIZE;
	height = REF_CUBEMAP_SIZE;

	for ( i = 0; i < 6; i++ )
	{
		data[ i ] = (byte*) ri.Hunk_AllocateTempMemory( width * height * 4 );
		Com_Memset( data[ i ], 0xFF, width * height * 4 );
	}

	tr.whiteCubeImage = R_CreateCubeImage( "_whiteCube", ( const byte ** ) data, width, height, IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP );

	for ( i = 5; i >= 0; i-- )
	{
		ri.Hunk_FreeTempMemory( data[ i ] );
	}
}

// *INDENT-ON*

static void R_CreateColorGradeImage( void )
{
	byte *data, *ptr;
	int i, r, g, b;

	data = (byte*) ri.Hunk_AllocateTempMemory( 4 * REF_COLORGRADEMAP_STORE_SIZE * sizeof(color4ub_t) );

	// 255 is 15 * 17, so the colors range from 0 to 255
	for( ptr = data, i = 0; i < 4; i++ ) {
		for( b = 0; b < REF_COLORGRADEMAP_SIZE; b++ ) {
			for( g = 0; g < REF_COLORGRADEMAP_SIZE; g++ ) {
				for( r = 0; r < REF_COLORGRADEMAP_SIZE; r++ ) {
					*ptr++ = (byte) r * 17;
					*ptr++ = (byte) g * 17;
					*ptr++ = (byte) b * 17;
					*ptr++ = 255;
				}
			}
		}
	}

	tr.colorGradeImage = R_Create3DImage( "_colorGrade", data,
					      REF_COLORGRADEMAP_SIZE,
					      REF_COLORGRADEMAP_SIZE,
					      4 * REF_COLORGRADEMAP_SIZE,
					      IF_NOPICMIP | IF_NOCOMPRESSION | IF_NOLIGHTSCALE,
					      FT_LINEAR,
					      WT_EDGE_CLAMP );

	ri.Hunk_FreeTempMemory( data );
}

#if 0 //def COMPAT_KPQ3 //generate LUT. using image instead
#define LUT_IMAGE_SIZE 512
/* A Program to generate high quality BRDF lookup tables for the split-sum approximation in UE4s Physically based rendering
 * LUTs are stored in 16 bit or 32 bit floating point textures in either KTX or DDS format. 
 * Written by: Hector Medina-Fetterman
 */

//hypov8 note: converted from https://github.com/HectorMF/BRDFGenerator

const float PI = 3.14159265358979323846264338327950288;

float RadicalInverse_VdC(unsigned int bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

void Hammersley(unsigned int i, unsigned int N, vec2_t out)
{
	unsigned int tmp = i;
	//vec2_t ret = {float(i) / float(N), RadicalInverse_VdC(i)};
	out[0] = float(i) / float(N);
	out[1] = RadicalInverse_VdC(tmp);
}

void ImportanceSampleGGX(vec2_t Xi, float roughness, vec3_t N, vec3_t out)
{
	float a = roughness*roughness;

	float phi = 2.0 * PI * Xi[0];
	float cosTheta = sqrt((1.0 - Xi[1]) / (1.0 + (a*a - 1.0) * Xi[1]));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	vec3_t H, up, cTmp, tangent, bitangent;

	// from spherical coordinates to cartesian coordinates
	H[0] = cos(phi) * sinTheta;
	H[1] = sin(phi) * sinTheta;
	H[2] = cosTheta;

	// from tangent-space vector to world-space sample vector
	//vec3_t up = Q_fabs(N[2]) < 0.999 ? vec3_t(0.0, 0.0, 1.0) : vec3_t(1.0, 0.0, 0.0);
	if (Q_fabs(N[2]) < 0.999)
		VectorSet(up, 0.0f, 0.0f, 1.0f);
	else
		VectorSet(up, 1.0f, 0.0f, 0.0f);

	//vec3_t tangent = normalize(cross(up, N));
	CrossProduct(up, N, cTmp);
	VectorNormalize2(cTmp, tangent);

	//vec3_t bitangent = cross(N, tangent);
	CrossProduct(N, tangent, bitangent);

	//vec3_t sampleVec = tangent * H[0] + bitangent * H[1] + N * H[2];
	VectorScale(tangent, H[0], tangent);
	VectorScale(bitangent, H[1], bitangent);
	VectorScale(N, H[2], cTmp);
	VectorAdd(tangent, bitangent, tangent); 
	VectorAdd(tangent, cTmp, tangent); 

	//return  normalize(sampleVec);
	VectorNormalize2(tangent, out);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float a = roughness;
	float k = (a * a) / 2.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(float roughness, float NoV, float NoL)
{
	float ggx2 = GeometrySchlickGGX(NoV, roughness);
	float ggx1 = GeometrySchlickGGX(NoL, roughness);

	return ggx1 * ggx2;
}

void IntegrateBRDF(float NdotV, float roughness, unsigned int samples, vec2_t out)
{
	vec3_t V;
	unsigned int i;
	float A = 0.0;
	float B = 0.0;
	vec3_t N = {0.0, 0.0, 1.0};
	vec2_t Xi;
	vec3_t H, L, tmp;
	float NoL, NoH, VoH, NoV, G, G_Vis, Fc;


	V[0] = sqrt(1.0 - NdotV * NdotV);
	V[1] = 0.0;
	V[2] = NdotV;

	for (i = 0u; i < samples; ++i)
	{
		Hammersley(i, samples, Xi);
		ImportanceSampleGGX(Xi, roughness, N, H);
		//L = normalize(2.0f * DotProduct(V, H) * H - V);
		VectorScale(H, 2.0f * DotProduct(V, H), tmp);
		VectorSubtract(tmp, V, tmp);
		VectorNormalize2(tmp, L);

		NoL = Q_max(L[2], 0.0f);
		NoH = Q_max(H[2], 0.0f);
		VoH = Q_max(DotProduct(V, H), 0.0f);
		NoV = Q_max(DotProduct(N, V), 0.0f);

		if (NoL > 0.0)
		{
			G = GeometrySmith(roughness, NoV, NoL);

			G_Vis = (G * VoH) / (NoH * NoV);
			Fc = pow(1.0 - VoH, 5.0);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	Xi[0] = (A / float(samples));
	Xi[1] = (B / float(samples));

	//return vec2_t(A / float(samples), B / float(samples));
	out[0] = (A / float(samples)) * (256*256-1);
	out[1] = (B / float(samples)) * (256*256-1);
}

void R_CreatePBR_LUT(void)
{
	//Here we set up the default parameters
	int samples = 10; //was 1024.. this is sooooooooooooo slow
	int size = LUT_IMAGE_SIZE;
	int x, y, bits = 16;
	float NoV, roughness; 
	uint16_t data[ LUT_IMAGE_SIZE ][ LUT_IMAGE_SIZE ][ 2 ];
	vec2_t out;


	//for (y = 0; y < size; y++)
	for (y = size-1; y >= 0; y--)
	{
		for (x = 0; x < size; x++)
		{
			NoV = (y + 0.5f) * (1.0f / size);
			roughness = 1-((x + 0.5f) * (1.0f / size)); //invert write order
			IntegrateBRDF(NoV, roughness, samples, out);
			//v1 = (uint16_t)(int)__out__[0];
			//v2 = (uint16_t)(int)__out__[1];
			//data[x][y][0] = out[0];
			//data[x][y][1] = out[1];
			data[x][y][0] = (uint16_t)out[0];
			data[x][y][1] = (uint16_t)out[1];
		}
	}

	/*std::cout << bits << " bit, [" << size << " x " << size << "] BRDF LUT generated using " << samples << " samples.\n";
	std::cout << "Saved LUT to " << filename << ".\n";*/

	//upload texture
	tr.pbrLutImage =
	  R_CreateImage( "_pbrLUT", ( byte * ) data, LUT_IMAGE_SIZE, LUT_IMAGE_SIZE, 
	  IF_NOCOMPRESSION | IF_NOPICMIP | IF_TWOCOMP16F, FT_LINEAR, WT_EDGE_CLAMP); // WT_EDGE_CLAMP );
}


static void R_CreatePBR_LUT1( void )
{
	int  x, y;
	byte data[ LUT_IMAGE_SIZE ][ LUT_IMAGE_SIZE ][ 4 ];
	byte *ptr = &data[0][0][0];
	int  b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for ( y = 0; y < LUT_IMAGE_SIZE; y++ )
	{
		for ( x = 0; x < LUT_IMAGE_SIZE; x++ )
		{
			float d;

			d = ( LUT_IMAGE_SIZE / 2 - 0.5f - x ) * ( LUT_IMAGE_SIZE / 2 - 0.5f - x ) +
			    ( LUT_IMAGE_SIZE / 2 - 0.5f - y ) * ( LUT_IMAGE_SIZE / 2 - 0.5f - y );
			b = 4000 / d;

			if ( b > 255 )
			{
				b = 255;
			}
			else if ( b < 75 )
			{
				b = 0;
			}

			ptr[ 0 ] = ptr[ 1 ] = ptr[ 2 ] = b;
			ptr[ 3 ] = 255;
			ptr += 4;
		}
	}

	tr.attenuationXYImage =
	  R_CreateImage( "_attenuationXY", ( byte * ) data, LUT_IMAGE_SIZE, LUT_IMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP );
}
#endif

/*
==================
R_CreateBuiltinImages
==================
*/
void R_CreateBuiltinImages( void )
{
	int   x, y;
	byte  data[ DEFAULT_SIZE * DEFAULT_SIZE * 4 ];
	byte  *out;
	float s, value;
	byte  intensity;

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, sizeof( data ) );
	tr.whiteImage = R_CreateImage( "_white", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT );

	// we use a solid black image instead of disabling texturing
	for (x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4)	{
		out[0] = out[1] = out[2] = 0;
	}
	tr.blackImage = R_CreateImage( "_black", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT );

	// grey //add hypov8
	for (x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4)	{
		out[0] = out[1] = out[2] = 128;
	}
	tr.greyImage = R_CreateImage("_grey", (byte *)data,	MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT);

	// red
	for ( x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4 )	{
		out[ 1 ] = out[ 2 ] = 0;
		out[ 0 ] = 255;
	}
	tr.redImage = R_CreateImage( "_red", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT );

	// green
	for ( x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4 )	{
		out[ 0 ] = out[ 2 ] = 0;
		out[ 1 ] = 255;
	}
	tr.greenImage = R_CreateImage( "_green", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT );

	// blue
	for ( x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4 )	{
		out[ 0 ] = out[ 1 ] = 0;
		out[ 2 ] = 255;
	}
	tr.blueImage = R_CreateImage( "_blue", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP, FT_LINEAR, WT_REPEAT );

	// generate a default normalmap with a zero heightmap
	for ( x = (MINIMAGE_SIZE * MINIMAGE_SIZE), out = &data[0]; x; --x, out += 4 )	{
		out[ 0 ] = out[ 1 ] = 128;
		out[ 2 ] = 255;
	}
	tr.flatImage = R_CreateImage( "_flat", ( byte * ) data, MINIMAGE_SIZE, MINIMAGE_SIZE, IF_NOPICMIP | IF_NORMALMAP, FT_LINEAR, WT_REPEAT );

	// _scratch
	for ( x = 0; x < 32; x++ )	{
		// scratchimage is usually used for cinematic drawing
		tr.scratchImage[ x ] = R_CreateImage( "_scratch", ( byte * ) data, DEFAULT_SIZE, DEFAULT_SIZE, IF_NONE, FT_LINEAR, WT_CLAMP );
	}

	//_quadratic
	out = &data[ 0 ];
	for ( y = 0; y < DEFAULT_SIZE; y++ )
	{
		for ( x = 0; x < DEFAULT_SIZE; x++, out += 4 )
		{
			s = ( ( ( float ) x + 0.5f ) * ( 2.0f / DEFAULT_SIZE ) - 1.0f );

			s = Q_fabs( s ) - ( 1.0f / DEFAULT_SIZE );

			value = 1.0f - ( s * 2.0f ) + ( s * s );

			intensity = ClampByte( Q_ftol( value * 255.0f ) );

			out[ 0 ] = intensity;
			out[ 1 ] = intensity;
			out[ 2 ] = intensity;
			out[ 3 ] = intensity;
		}
	}
	tr.quadraticImage =
	  R_CreateImage( "_quadratic", ( byte * ) data, DEFAULT_SIZE, DEFAULT_SIZE, IF_NOPICMIP | IF_NOCOMPRESSION, FT_LINEAR,
	                 WT_CLAMP );

	R_CreateRandomNormalsImage();
	R_CreateFogImage();
	R_CreateNoFalloffImage();
	R_CreateAttenuationXYImage();
	R_CreateContrastRenderFBOImage();
	R_CreateBloomRenderFBOImage();
	R_CreateCurrentRenderImage();
	R_CreateDepthRenderImage();
	R_CreatePortalRenderImage();
	R_CreateOcclusionRenderFBOImage();
	R_CreateDepthToColorFBOImages();
	R_CreateDownScaleFBOImages();
	R_CreateDeferredRenderFBOImages();
	R_CreateShadowMapFBOImage();
	R_CreateShadowCubeFBOImage();
	R_CreateBlackCubeImage();
	R_CreateWhiteCubeImage();
	R_CreateColorGradeImage();
}

/*
===============
R_SetColorMappings
===============
*/
void R_SetColorMappings( void )
{
	int   i, j;
	float g;
	int   inf;
	int   shift;

	tr.mapOverBrightBits = r_mapOverBrightBits->integer;

	// setup the overbright lighting
	tr.overbrightBits = r_overBrightBits->integer;

	if ( !glConfig.deviceSupportsGamma )
	{
		tr.overbrightBits = 0; // need hardware gamma for overbright
	}

	// never overbright in windowed mode
	if ( !glConfig.isFullscreen )
	{
		tr.overbrightBits = 0;
	}

	// allow 2 overbright bits in 24 bit, but only 1 in 16 bit
	if ( glConfig.colorBits > 16 )
	{
		if ( tr.overbrightBits > 2 )
		{
			tr.overbrightBits = 2;
		}
	}
	else
	{
		if ( tr.overbrightBits > 1 )
		{
			tr.overbrightBits = 1;
		}
	}

	if ( tr.overbrightBits < 0 )
	{
		tr.overbrightBits = 0;
	}

	tr.identityLight = 1.0f / ( 1 << tr.overbrightBits );

	if ( r_intensity->value <= 1 )
	{
		ri.Cvar_Set( "r_intensity", "1" );
	}

	if ( r_gamma->value < 0.5f )
	{
		ri.Cvar_Set( "r_gamma", "0.5" );
	}
	else if ( r_gamma->value > 3.0f )
	{
		ri.Cvar_Set( "r_gamma", "3.0" );
	}

	g = r_gamma->value;

	shift = tr.overbrightBits;

	for ( i = 0; i < 256; i++ )
	{
		if ( g == 1 )
		{
			inf = i;
		}
		else
		{
			inf = 255 * pow( i / 255.0f, 1.0f / g ) + 0.5f;
		}

		inf <<= shift;

		if ( inf < 0 )
		{
			inf = 0;
		}

		if ( inf > 255 )
		{
			inf = 255;
		}

		s_gammatable[ i ] = inf;
	}

	for ( i = 0; i < 256; i++ )
	{
		j = i * r_intensity->value;

		if ( j > 255 )
		{
			j = 255;
		}

		s_intensitytable[ i ] = j;
	}

	GLimp_SetGamma( s_gammatable, s_gammatable, s_gammatable );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void )
{
	const char *charsetImage = "gfx/2d/charset-bezerk-plain-rc2.png";
	const char *grainImage = "gfx/2d/camera/grain.png";
	const char *vignetteImage = "gfx/2d/camera/vignette.png";

#ifdef COMPAT_KPQ3 //pbr images
	const char *pbrSpecCubeImage = "gfx/pbr/hipshot/miramar"; // for standard reflections when cube probe fails
	//const char *pbrLutImage = "gfx/pbr/ibl_brdf_lut.png";
	const char *pbrLutImage = "gfx/pbr/lut3.png";
	const char *pbrEnvImage = "gfx/pbr/miramar_hdri5.png";
#endif
	ri.Printf( PRINT_DEVELOPER, "------- R_InitImages -------\n" );

	Com_Memset( r_imageHashTable, 0, sizeof( r_imageHashTable ) );
	Com_InitGrowList( &tr.images, 4096 );
	Com_InitGrowList( &tr.lightmaps, 128 );
	Com_InitGrowList( &tr.deluxemaps, 128 );

	// build brightness translation tables
	R_SetColorMappings();

	// create default texture and white texture
	R_CreateBuiltinImages();

	tr.charsetImage = R_FindImageFile( charsetImage, IF_NOCOMPRESSION | IF_NOPICMIP, FT_DEFAULT, WT_CLAMP, NULL );

	if ( !tr.charsetImage )
	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", charsetImage );
	}

	tr.grainImage = R_FindImageFile( grainImage, IF_NOCOMPRESSION | IF_NOPICMIP, FT_DEFAULT, WT_REPEAT, NULL );

	if ( !tr.grainImage )
	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", grainImage );
	}

	tr.vignetteImage = R_FindImageFile( vignetteImage, IF_NOCOMPRESSION | IF_NOPICMIP, FT_DEFAULT, WT_CLAMP, NULL );

	if ( !tr.vignetteImage )
	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", vignetteImage );
	}

#ifdef COMPAT_KPQ3 //note: these are constantly reloaded :/

	//default reflection cubemap
	tr.skyCubeMapDefault = R_FindCubeImage(pbrSpecCubeImage, IF_NONE, FT_LINEAR, WT_EDGE_CLAMP, NULL );
	if ( !tr.skyCubeMapDefault )	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", pbrSpecCubeImage);
	}

	//PBR specuar BRDF
	tr.pbrSpecHdriImage_default = R_FindImageFile( pbrEnvImage, IF_NOCOMPRESSION | IF_NOPICMIP, FT_CUBEMIP, WT_CLAMP, NULL );
	if ( !tr.pbrSpecHdriImage_default )	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", pbrEnvImage );
	}

	//PBR LUT
	//R_CreatePBR_LUT();
	tr.pbrLutImage = R_FindImageFile( pbrLutImage, IF_NOCOMPRESSION | IF_NOPICMIP, FT_LINEAR, WT_EDGE_CLAMP, NULL );
	if ( !tr.pbrLutImage )	{
		ri.Error( ERR_FATAL, "R_InitImages: could not load '%s'", pbrLutImage );
	}

#endif
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void )
{
	int     i;
	image_t *image;

	ri.Printf( PRINT_DEVELOPER, "------- R_ShutdownImages -------\n" );

	for ( i = 0; i < tr.images.currentElements; i++ )
	{
		image = (image_t*) Com_GrowListElement( &tr.images, i );

		glDeleteTextures( 1, &image->texnum );
	}

	Com_Memset( glState.currenttextures, 0, sizeof( glState.currenttextures ) );

	/*
	if(glBindTexture)
	{
	        if(glActiveTexture)
	        {
	                for(i = 8 - 1; i >= 0; i--)
	                {
	                        GL_SelectTexture(i);
	                        glBindTexture(GL_TEXTURE_2D, 0);
	                }
	        }
	        else
	        {
	                glBindTexture(GL_TEXTURE_2D, 0);
	        }
	}
	*/

	Com_DestroyGrowList( &tr.images );
	Com_DestroyGrowList( &tr.lightmaps );
	Com_DestroyGrowList( &tr.deluxemaps );
	Com_DestroyGrowList( &tr.cubeProbes );

	FreeVertexHashTable( tr.cubeHashTable );
}

int RE_GetTextureId( const char *name )
{
	int     i;
	image_t *image;

	ri.Printf( PRINT_DEVELOPER, S_COLOR_YELLOW "RE_GetTextureId [%s].\n", name );

	for ( i = 0; i < tr.images.currentElements; i++ )
	{
		image = (image_t*) Com_GrowListElement( &tr.images, i );

		if ( !strcmp( name, image->name ) )
		{
//          ri.Printf(PRINT_ALL, "Found textureid %d\n", i);
			return i;
		}
	}

//  ri.Printf(PRINT_ALL, "Image not found.\n");
	return -1;
}
