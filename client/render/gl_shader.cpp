/*
gl_shader.cpp - glsl shaders
Copyright (C) 2014 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "hud.h"
#include "utils.h"
#include "const.h"
#include "ref_params.h"
#include "gl_local.h"
#include <mathlib.h>
#include <stringlib.h>
#include "gl_shader.h"
#include "virtualfs.h"
#include "gl_world.h"
#include "gl_decals.h"
#include "studio.h"
#include "gl_grass.h"
#include "gl_cvars.h"
#include "basetypes.h"

//#define _DEBUG_UNIFORMS
#define SHADERS_HASH_SIZE	(MAX_GLSL_PROGRAMS >> 2)
#define MAX_FILE_STACK	64

static char	filenames_stack[MAX_FILE_STACK][256];
glsl_program_t	glsl_programs[MAX_GLSL_PROGRAMS];
glsl_program_t	*glsl_programsHashTable[SHADERS_HASH_SIZE];
int		num_glsl_programs;
static int	file_stack_pos;
static bool	cache_needs_update = false;

typedef struct
{
	const char	*name;
	uniformType_t	type;
	int		flags;	// hints
} uniformTable_t;

glsl_program_t *shader_t :: GetShader( void )
{
	return &glsl_programs[shadernum];
}

void shader_t :: SetShader( unsigned short hand )
{
	sequence = tr.glsl_valid_sequence;
	shadernum = hand;
}

bool shader_t :: IsValid( void )
{
	return (shadernum > 0) && (sequence == tr.glsl_valid_sequence);
}

int uniform_t :: GetSizeInBytes( void )
{
	switch( format )
	{
	case GL_SAMPLER_1D_ARB:
	case GL_SAMPLER_2D_ARB:
	case GL_SAMPLER_3D_ARB:
	case GL_SAMPLER_CUBE_ARB:
	case GL_SAMPLER_1D_SHADOW_ARB:
	case GL_SAMPLER_2D_SHADOW_ARB:
	case GL_SAMPLER_2D_RECT_ARB:
	case GL_SAMPLER_2D_RECT_SHADOW_ARB:
	case GL_SAMPLER_CUBE_SHADOW_EXT:
		return 4;
	case GL_FLOAT:
	case GL_INT:
	case GL_UNSIGNED_INT:
		return 4;
	case GL_FLOAT_VEC2_ARB:
	case GL_INT_VEC2_ARB:
		return 8;
	case GL_FLOAT_VEC3_ARB:
	case GL_INT_VEC3_ARB:
		return 12;
	case GL_FLOAT_VEC4_ARB:
	case GL_INT_VEC4_ARB:
		return 16;
	case GL_FLOAT_MAT2_ARB:
		return 16;
	case GL_FLOAT_MAT3_ARB:
		return 36;
	case GL_FLOAT_MAT4_ARB:
		return 64;
	}

	ALERT( at_error, "uniform %s has unspecified size\n", name );
	return 0; // assume error
}

void uniform_t :: SetValue( const void *pdata, int count )
{
	unicache_t *check = (unicache_t *)pdata;

	// set texture unit
	if( FBitSet( flags, UFL_TEXTURE_UNIT ) && unit >= 0 )
	{
		GL_Bind( unit, check->iValue[0] );
	}
	else if( size > 1 )
	{
		// handle arrays
		if( count == -1 )
			count = size;

		switch( format )
		{
		case GL_FLOAT:
			pglUniform1fvARB( location, count, (const float *)pdata );
			break;
		case GL_FLOAT_VEC2_ARB:
			pglUniform2fvARB( location, count, (const float *)pdata );
			break;
		case GL_FLOAT_VEC3_ARB:
			pglUniform3fvARB( location, count, (const float *)pdata );
			break;
		case GL_FLOAT_VEC4_ARB:
			pglUniform4fvARB( location, count, (const float *)pdata );
			break;
		case GL_FLOAT_MAT2_ARB:
			pglUniformMatrix2fvARB( location, count, GL_FALSE, (const float *)pdata );
			break;
		case GL_FLOAT_MAT3_ARB:
			pglUniformMatrix3fvARB( location, count, GL_FALSE, (const float *)pdata );
			break;
		case GL_FLOAT_MAT4_ARB:
			pglUniformMatrix4fvARB( location, count, GL_FALSE, (const float *)pdata );
			break;
		}
	}
	else
	{
		int testSize = GetSizeInBytes();

		// some values could be cached
		if( testSize <= 16 && !memcmp( &cache, check, testSize ))
			return;

		// single cached values
		switch( format )
		{
		case GL_FLOAT:
			pglUniform1fARB( location, check->fValue[0] );
			cache.fValue[0] = check->fValue[0];
			break;
		case GL_FLOAT_VEC2_ARB:
			pglUniform2fARB( location, check->fValue[0], check->fValue[1] );
			cache.fValue[0] = check->fValue[0];
			cache.fValue[1] = check->fValue[1];
			break;
		case GL_FLOAT_VEC3_ARB:
			pglUniform3fARB( location, check->fValue[0], check->fValue[1], check->fValue[2] );
			cache.fValue[0] = check->fValue[0];
			cache.fValue[1] = check->fValue[1];
			cache.fValue[2] = check->fValue[2];
			break;
		case GL_FLOAT_VEC4_ARB:
			pglUniform4fARB( location, check->fValue[0], check->fValue[1], check->fValue[2], check->fValue[3] );
			cache.fValue[0] = check->fValue[0];
			cache.fValue[1] = check->fValue[1];
			cache.fValue[2] = check->fValue[2];
			cache.fValue[3] = check->fValue[3];
			break;
		case GL_INT:
			pglUniform1iARB( location, check->iValue[0] );
			cache.iValue[0] = check->iValue[0];
			break;
		case GL_INT_VEC2_ARB:
			pglUniform2iARB( location, check->iValue[0], check->iValue[1] );
			cache.iValue[0] = check->iValue[0];
			cache.iValue[1] = check->iValue[1];
			break;
		case GL_INT_VEC3_ARB:
			pglUniform3iARB( location, check->iValue[0], check->iValue[1], check->iValue[2] );
			cache.iValue[0] = check->iValue[0];
			cache.iValue[1] = check->iValue[1];
			cache.iValue[2] = check->iValue[2];
			break;
		case GL_INT_VEC4_ARB:
			pglUniform4iARB( location, check->iValue[0], check->iValue[1], check->iValue[2], check->iValue[3] );
			cache.iValue[0] = check->iValue[0];
			cache.iValue[1] = check->iValue[1];
			cache.iValue[2] = check->iValue[2];
			cache.iValue[3] = check->iValue[3];
			break;
		case GL_FLOAT_MAT2_ARB:
			pglUniformMatrix2fvARB( location, 1, GL_FALSE, (const float *)pdata );
			cache.fValue[0] = check->fValue[0];
			cache.fValue[1] = check->fValue[1];
			cache.fValue[2] = check->fValue[2];
			cache.fValue[3] = check->fValue[3];
			break;
		case GL_FLOAT_MAT3_ARB:
			pglUniformMatrix3fvARB( location, 1, GL_FALSE, (const float *)pdata );
			break;
		case GL_FLOAT_MAT4_ARB:
			pglUniformMatrix4fvARB( location, 1, GL_FALSE, (const float *)pdata );
			break;
		}
	}
}

// all known engine uniforms
static uniformTable_t glsl_uniformTable[] =
{
{ "u_ColorMap",		UT_COLORMAP,		UFL_TEXTURE_UNIT },
{ "u_DepthMap",		UT_DEPTHMAP,		UFL_TEXTURE_UNIT },
{ "u_NormalMap",		UT_NORMALMAP,		UFL_TEXTURE_UNIT },
{ "u_GlossMap",		UT_GLOSSMAP,		UFL_TEXTURE_UNIT },
{ "u_DetailMap",		UT_DETAILMAP,		UFL_TEXTURE_UNIT },
{ "u_ProjectMap",		UT_PROJECTMAP,		UFL_TEXTURE_UNIT },
{ "u_ShadowMap0",		UT_SHADOWMAP0,		UFL_TEXTURE_UNIT },
{ "u_ShadowMap1",		UT_SHADOWMAP1,		UFL_TEXTURE_UNIT },
{ "u_ShadowMap2",		UT_SHADOWMAP2,		UFL_TEXTURE_UNIT },
{ "u_ShadowMap3",		UT_SHADOWMAP3,		UFL_TEXTURE_UNIT },
{ "u_ShadowMap",		UT_SHADOWMAP,		UFL_TEXTURE_UNIT },
{ "u_LightMap",		UT_LIGHTMAP,		UFL_TEXTURE_UNIT },
{ "u_DeluxeMap",		UT_DELUXEMAP,		UFL_TEXTURE_UNIT },
{ "u_DecalMap",		UT_DECALMAP,		UFL_TEXTURE_UNIT },
{ "u_ScreenMap",		UT_SCREENMAP,		UFL_TEXTURE_UNIT },
{ "u_VisLightMap0",		UT_VISLIGHTMAP0,		UFL_TEXTURE_UNIT },
{ "u_VisLightMap1",		UT_VISLIGHTMAP1,		UFL_TEXTURE_UNIT },
{ "u_EnvMap0",		UT_ENVMAP0,		UFL_TEXTURE_UNIT },
{ "u_EnvMap1",		UT_ENVMAP1,		UFL_TEXTURE_UNIT },
{ "u_EnvMap",		UT_ENVMAP,		UFL_TEXTURE_UNIT },
{ "u_GlowMap",		UT_GLOWMAP,		UFL_TEXTURE_UNIT },
{ "u_HeightMap",		UT_HEIGHTMAP,		UFL_TEXTURE_UNIT },
{ "u_LayerMap",		UT_LAYERMAP,		UFL_TEXTURE_UNIT },
{ "u_FragData0",		UT_FRAGDATA0,		UFL_TEXTURE_UNIT },
{ "u_FragData1",		UT_FRAGDATA1,		UFL_TEXTURE_UNIT },
{ "u_FragData2",		UT_FRAGDATA2,		UFL_TEXTURE_UNIT },
{ "u_BspPlanesMap",		UT_BSPPLANESMAP,		UFL_TEXTURE_UNIT },
{ "u_BspModelsMap",		UT_BSPMODELSMAP,		UFL_TEXTURE_UNIT },
{ "u_BspNodesMap",		UT_BSPNODESMAP,		UFL_TEXTURE_UNIT },
{ "u_BspLightsMap",		UT_BSPLIGHTSMAP,		UFL_TEXTURE_UNIT },
{ "u_FitNormalMap",		UT_FITNORMALMAP,		UFL_TEXTURE_UNIT },
{ "u_ModelMatrix",		UT_MODELMATRIX,		0 },
{ "u_ReflectMatrix",	UT_REFLECTMATRIX,		0 },
{ "u_BonesArray",		UT_BONESARRAY,		0 },	
{ "u_BoneQuaternion",	UT_BONEQUATERNION,		0 },
{ "u_BonePosition",		UT_BONEPOSITION,		0 },
{ "u_ScreenSizeInv",	UT_SCREENSIZEINV,		UFL_GLOBAL_PARM },
{ "u_zFar",		UT_ZFAR,			UFL_GLOBAL_PARM },
{ "u_LightStyleValues",	UT_LIGHTSTYLEVALUES,	UFL_GLOBAL_PARM },
{ "u_LightStyles",		UT_LIGHTSTYLES,		0 },
{ "u_RealTime",		UT_REALTIME,		UFL_GLOBAL_PARM },
{ "u_DetailScale",		UT_DETAILSCALE,		0 },
{ "u_FogParams",		UT_FOGPARAMS,		UFL_GLOBAL_PARM },
{ "u_ShadowParams",		UT_SHADOWPARMS,		0 },
{ "u_TexOffset",		UT_TEXOFFSET,		0 },
{ "u_ViewOrigin",		UT_VIEWORIGIN,		0 },	// not in a global because it's transformed into modelspace
{ "u_ViewRight",		UT_VIEWRIGHT,		0 },
{ "u_RenderColor",		UT_RENDERCOLOR,		0 },
{ "u_RenderAlpha",		UT_RENDERALPHA,		0 },
{ "u_Smoothness",		UT_SMOOTHNESS,		0 },
{ "u_ShadowMatrix",		UT_SHADOWMATRIX,		0 },
{ "u_ShadowSplitDist",	UT_SHADOWSPLITDIST,		UFL_GLOBAL_PARM },
{ "u_TexelSize",		UT_TEXELSIZE,		UFL_GLOBAL_PARM },
{ "u_GammaTable",		UT_GAMMATABLE,		UFL_GLOBAL_PARM },
{ "u_LightDir",		UT_LIGHTDIR,		0 },
{ "u_LightDiffuse",		UT_LIGHTDIFFUSE,		0 },
{ "u_LightShade",		UT_LIGHTSHADE,		0 },
{ "u_LightOrigin",		UT_LIGHTORIGIN,		0 },
{ "u_LightViewProjMatrix",	UT_LIGHTVIEWPROJMATRIX,	0 },
{ "u_DiffuseFactor",	UT_DIFFUSEFACTOR,		UFL_GLOBAL_PARM },
{ "u_AmbientFactor",	UT_AMBIENTFACTOR,		UFL_GLOBAL_PARM },
{ "u_AmbientCube",		UT_AMBIENTCUBE,		0 },
{ "u_SunRefract",		UT_SUNREFRACT,		UFL_GLOBAL_PARM },
{ "u_LerpFactor",		UT_LERPFACTOR,		0 },
{ "u_RefractScale",		UT_REFRACTSCALE,		0 },
{ "u_ReflectScale",		UT_REFLECTSCALE,		0 },
{ "u_AberrationScale",	UT_ABERRATIONSCALE,		0 },
{ "u_BoxMins",		UT_BOXMINS,		0 },
{ "u_BoxMaxs",		UT_BOXMAXS,		0 },
{ "u_CubeOrigin",		UT_CUBEORIGIN,		0 },
{ "u_CubeMipCount",		UT_CUBEMIPCOUNT,		0 },
{ "u_LightNums0",		UT_LIGHTNUMS0,		0 },
{ "u_LightNums1",		UT_LIGHTNUMS1,		0 },
{ "u_GrassParams",		UT_GRASSPARAMS,		0 },
{ "u_ReliefParams",		UT_RELIEFPARAMS,		0 },
{ "u_BlurFactor",		UT_BLURFACTOR,		0 },
{ "u_ScreenWidth",		UT_SCREENWIDTH,		0 },
{ "u_ScreenHeight",		UT_SCREENHEIGHT,		0 },
{ "u_FocalDepth",		UT_FOCALDEPTH,		0 },
{ "u_FocalLength",		UT_FOCALLENGTH,		0 },
{ "u_DofDebug",		UT_DOFDEBUG,		0 },
{ "u_FStop",		UT_FSTOP,			0 },
{ "u_GrayScale",		UT_GRAYSCALE,		0 },
{ "u_LightGamma",		UT_LIGHTGAMMA,		UFL_GLOBAL_PARM },
{ "u_LightScale",		UT_LIGHTSCALE,		UFL_GLOBAL_PARM },
{ "u_LightThreshold",	UT_LIGHTTHRESHOLD,		UFL_GLOBAL_PARM },
{ "u_NumVisibleModels",	UT_NUMVISIBLEMODELS,	UFL_GLOBAL_PARM },
{ "u_Undefined",		UT_UNDEFINED,		0 },
};

static char *GL_PrintInfoLog( GLhandleARB object )
{
	static char	msg[32768];
	int		maxLength = 0;

	pglGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		ALERT( at_warning, "GL_PrintInfoLog: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

	pglGetInfoLogARB( object, maxLength, &maxLength, msg );

	return msg;
}

static char *GL_PrintShaderSource( GLhandleARB object )
{
	static char	msg[8192];
	int		maxLength = 0;
	
	pglGetObjectParameterivARB( object, GL_OBJECT_SHADER_SOURCE_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		ALERT( at_warning, "GL_PrintShaderSource: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

	pglGetShaderSourceARB( object, maxLength, &maxLength, msg );

	return msg;
}

static bool GL_PushFileStack( const char *filename )
{
	if( file_stack_pos < MAX_FILE_STACK )
	{
		Q_strncpy( filenames_stack[file_stack_pos], filename, sizeof( filenames_stack[0] ));
		file_stack_pos++;
		return true;
	}

	ALERT( at_error, "GL_PushFileStack: stack overflow\n" );
	return false;
}

static bool GL_PopFileStack( void )
{
	file_stack_pos--;

	if( file_stack_pos < 0 )
	{
		ALERT( at_error, "GL_PushFileStack: stack underflow\n" );
		return false;
	}

	return true;
}

static bool GL_CheckFileStack( const char *filename )
{
	for( int i = 0; i < file_stack_pos; i++ )
	{
		if( !Q_stricmp( filenames_stack[i], filename ))
			return true;
	}
	return false;
}

static bool GL_TestSource( const char *szFilename, const char *szCacheName, CVirtualFS *file )
{
	int	iCompare;

	if( GL_CheckFileStack( szFilename ))
		return false;

	// check vertex shader source or include-file
	if( COMPARE_FILE_TIME( szFilename, szCacheName, &iCompare ))
	{
		// glsl file is newer.
		if( iCompare > 0 )
		{
			Msg( "%s was changed, %s will be updated\n", szFilename, szCacheName );
			cache_needs_update = true;
		}
	}
	else
	{
		Msg( "%s will be created\n", szCacheName );
		cache_needs_update = true;
	}

	int size;
	char *source = (char *)gEngfuncs.COM_LoadFile((char *)szFilename, 5, &size );
	if( !source ) return false;

	GL_PushFileStack( szFilename );
	file->Write( source, size );
	file->Seek( 0, SEEK_SET ); // rewind

	gEngfuncs.COM_FreeFile( source );

	return true;
}

static void GL_TestFile( const char *szFilename, const char *szCacheName, CVirtualFS *file )
{
	char	*pfile, token[256];
	char	line[2048];
	int	ret;

	do
	{
		ret = file->Gets( line, sizeof( line ));
		pfile = line;

		// NOTE: if first keyword it's not an '#include' just ignore it
		pfile = COM_ParseFile( pfile, token );
		if( !Q_strcmp( token, "#include" ))
		{
			CVirtualFS incfile;
			char incname[256];

			pfile = COM_ParseLine( pfile, token );
			Q_snprintf( incname, sizeof( incname ), "glsl/%s", token );
			if( !GL_TestSource( incname, szCacheName, &incfile ))
				continue;

			if( cache_needs_update )
				break; // no reason to seek more
			GL_TestFile( incname, szCacheName, &incfile );
		}
	} while( ret != EOF );

	GL_PopFileStack();
}

static bool GL_TestShader( const char *szFilename, const char *szCacheName )
{
	CVirtualFS file;

	cache_needs_update = false;
	file_stack_pos = 0;

	if( !GL_TestSource( szFilename, szCacheName, &file ))
		return false;
	GL_TestFile( szFilename, szCacheName, &file );

	return cache_needs_update;
}

static bool GL_LoadSource( const char *filename, CVirtualFS *file )
{
	if( GL_CheckFileStack( filename ))
	{
		ALERT( at_error, "recursive include for %s\n", filename );
		return false;
	}

	int size;
	char *source = (char *)gEngfuncs.COM_LoadFile((char *)filename, 5, &size );
	if( !source )
	{
		ALERT( at_error, "couldn't load %s\n", filename );
		return false;
	}

	GL_PushFileStack( filename );
	file->Write( source, size );
	file->Seek( 0, SEEK_SET ); // rewind

	gEngfuncs.COM_FreeFile( source );

	return true;
}

static void GL_ParseFile( const char *filename, CVirtualFS *file, CVirtualFS *out )
{
	char	*pfile, token[256];
	int	ret, fileline = 1;
	char	line[2048];

//	out->Printf( "#file %s\n", filename );	// OpenGL doesn't support #file :-(
	out->Printf( "#line 0\n" );

	do
	{
		ret = file->Gets( line, sizeof( line ));
		pfile = line;

		// NOTE: if first keyword it's not an '#include' just ignore it
		pfile = COM_ParseFile( pfile, token );
		if( !Q_strcmp( token, "#include" ))
		{
			CVirtualFS incfile;
			char incname[256];

			pfile = COM_ParseLine( pfile, token );
			Q_snprintf( incname, sizeof( incname ), "glsl/%s", token );
			if( !GL_LoadSource( incname, &incfile ))
			{
				fileline++;
				continue;
                              }
			GL_ParseFile( incname, &incfile, out );
//			out->Printf( "#file %s\n", filename );	// OpenGL doesn't support #file :-(
			out->Printf( "#line %i\n", fileline );
		}
		else out->Printf( "%s\n", line );
		fileline++;
	} while( ret != EOF );

	GL_PopFileStack();
}

static bool GL_ProcessShader( const char *filename, CVirtualFS *out, const char *defines = NULL )
{
	CVirtualFS file;

	file_stack_pos = 0;

	if( !GL_LoadSource( filename, &file ))
		return false;

	// add internal defines
	out->Printf( "#version 120\n" ); // OpenGL 2.1 required (because 'flat' modifier support only starts from this version)
	out->Printf( "#ifndef M_PI\n#define M_PI 3.14159265358979323846\n#endif\n" );
	out->Printf( "#ifndef M_PI2\n#define M_PI2 6.28318530717958647692\n#endif\n" );
	if( GL_Support( R_EXT_GPU_SHADER4 ))
	{
		out->Printf( "#extension GL_EXT_gpu_shader4 : require\n" ); // support bitwise ops
		out->Printf( "#define GLSL_gpu_shader4\n" );
		if( GL_Support( R_TEXTURE_ARRAY_EXT ))
			out->Printf( "#define GLSL_ALLOW_TEXTURE_ARRAY\n" );
	}
	else if( GL_Support( R_TEXTURE_ARRAY_EXT ))
	{
		out->Printf( "#extension GL_EXT_texture_array : require\n" ); // support texture arrays
		out->Printf( "#define GLSL_ALLOW_TEXTURE_ARRAY\n" );
	}

	if( GL_Support( R_TEXTURE_2D_RECT_EXT ))
	{
		out->Printf( "#extension GL_ARB_texture_rectangle : enable\n" ); // support texture rectangle
	}

	if( defines ) out->Print( defines );

	// user may override this constants
	out->Printf( "#ifndef MAXSTUDIOBONES\n#define MAXSTUDIOBONES %i\n#endif\n", glConfig.max_skinning_bones );
	out->Printf( "#ifndef MAX_SHADOWMAPS\n#define MAX_SHADOWMAPS %i\n#endif\n", MAX_SHADOWMAPS );
	out->Printf( "#ifndef NUM_SHADOW_SPLITS\n#define NUM_SHADOW_SPLITS %i\n#endif\n", NUM_SHADOW_SPLITS );
	out->Printf( "#ifndef LIGHT_SAMPLES\n#define LIGHT_SAMPLES %i\n#endif\n", LIGHT_SAMPLES );
	out->Printf( "#ifndef MAX_LIGHTSTYLES\n#define MAX_LIGHTSTYLES %i\n#endif\n", MAX_LIGHTSTYLES );
	out->Printf( "#ifndef MAXLIGHTMAPS\n#define MAXLIGHTMAPS %i\n#endif\n", MAXLIGHTMAPS );
	out->Printf( "#ifndef MAXDYNLIGHTS\n#define MAXDYNLIGHTS %i\n#endif\n", (int)cv_deferred_maxlights->value );
	out->Printf( "#ifndef GRASS_ANIM_DIST\n#define GRASS_ANIM_DIST %f\n#endif\n", GRASS_ANIM_DIST );

	GL_ParseFile( filename, &file, out );
	out->Write( "", 1 ); // terminator

	return true;
}

static void GL_LoadGPUShader( glsl_program_t *shader, const char *name, GLenum shaderType, const char *defines = NULL )
{
	char		filename[256];
	GLhandleARB	object;
	CVirtualFS	source;
	GLint		compiled;

	ASSERT( shader != NULL );

	switch( shaderType )
	{
	case GL_VERTEX_SHADER_ARB:
		Q_snprintf( filename, sizeof( filename ), "glsl/%s_vp.glsl", name );
		break;
	case GL_FRAGMENT_SHADER_ARB:
		Q_snprintf( filename, sizeof( filename ), "glsl/%s_fp.glsl", name );
		break;
	default:
		ALERT( at_error, "GL_LoadGPUShader: unknown shader type %p\n", shaderType );
		return;
	}

	// load includes, add some directives
	if( !GL_ProcessShader( filename, &source, defines ))
		return;

	GLcharARB *buffer = (GLcharARB *)source.GetBuffer();
	int bufferSize = source.GetSize();

	ALERT( at_aiconsole, "loading '%s'\n", filename );
	object = pglCreateShaderObjectARB( shaderType );
	pglShaderSourceARB( object, GL_TRUE, (const GLcharARB **)&buffer, &bufferSize );

	// compile shader
	pglCompileShaderARB( object );

	// check if shader compiled
	pglGetObjectParameterivARB( object, GL_OBJECT_COMPILE_STATUS_ARB, &compiled );

	if( !compiled )
	{
		if( developer_level ) Msg( "%s", GL_PrintInfoLog( object ));
		if( developer_level ) Msg( "Shader options:%s\n", GL_PretifyListOptions( defines ));
		ALERT( at_error, "Couldn't compile %s\n", filename ); 
		return;
	}

	if( shaderType == GL_VERTEX_SHADER_ARB )
		shader->status |= SHADER_VERTEX_COMPILED;
	else shader->status |= SHADER_FRAGMENT_COMPILED;

	// attach shader to program
	pglAttachObjectARB( shader->handle, object );

	// delete shader, no longer needed
	pglDeleteObjectARB( object );
}

static bool GL_LoadGPUBinaryShader( glsl_program_t *shader, const char *vpname, const char *fpname, uint checksum )
{
	char	szFilename[MAX_PATH];
	char	szVpSource[MAX_PATH];
	char	szFpSource[MAX_PATH];
	GLint	linked = 0;
	int	length;

	if( !GL_Support( R_BINARY_SHADER_EXT ))
		return false;

	Q_snprintf( szFilename, sizeof( szFilename ), "cache/glsl/%p.bin", checksum );
	Q_snprintf( szVpSource, sizeof( szVpSource ), "glsl/%s_vp.glsl", vpname );
	Q_snprintf( szFpSource, sizeof( szFpSource ), "glsl/%s_fp.glsl", fpname );

	// check vertex shader source
	if( GL_TestShader( szVpSource, szFilename ))
		return false;

	// check fragment shader source
	if( GL_TestShader( szFpSource, szFilename ))
		return false;

	byte *aMemFile = LOAD_FILE( szFilename, &length );
	if( !aMemFile ) return false;

	pglProgramBinary( shader->handle, glConfig.binary_formats, aMemFile, length );
	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_LINK_STATUS_ARB, &linked );
	SetBits( shader->status, SHADER_FRAGMENT_COMPILED|SHADER_VERTEX_COMPILED );
	FREE_FILE( aMemFile );

	if( linked )
	{
		ALERT( at_aiconsole, "loading %s\n", szFilename );
		SetBits( shader->status, SHADER_PROGRAM_LINKED );
		return true;
	}

	return false;
}

static bool GL_SaveGPUBinaryShader( glsl_program_t *shader, uint checksum )
{
	char	szFilename[MAX_PATH];
	int	length, result;
	GLenum	outFormat;
	byte	*binary;

	if( !GL_Support( R_BINARY_SHADER_EXT ))
		return false;

	Q_snprintf( szFilename, sizeof( szFilename ), "cache/glsl/%p.bin", checksum );
	pglGetObjectParameterivARB( shader->handle, GL_PROGRAM_BINARY_LENGTH, &length );
	if( length <= 0 ) return false;

	binary = (byte *)malloc( length );
	pglGetProgramBinary( shader->handle, length, &length, &outFormat, binary );

	result = SAVE_FILE( szFilename, binary, length );
	ALERT( at_aiconsole, "write glsl cache: %s\n", szFilename );
	free( binary );

	return (result != 0 );
}

static void GL_LinkProgram( glsl_program_t *shader )
{
	GLint	linked = 0;

	if( !shader ) return;

	if( GL_Support( R_BINARY_SHADER_EXT ))
		pglProgramParameteri( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );

	pglLinkProgramARB( shader->handle );

	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_LINK_STATUS_ARB, &linked );
	if( !linked )
	{
		ALERT( at_error, "%s\n%s shader failed to link\n", GL_PrintInfoLog( shader->handle ), shader->name );
		if( developer_level ) Msg( "Shader options:%s\n", GL_PretifyListOptions( shader->options ));
	}
	else shader->status |= SHADER_PROGRAM_LINKED;
}

static void GL_ValidateProgram( glsl_program_t *shader )
{
	GLint	validated = 0;

	if( !shader ) return;

	pglValidateProgramARB( shader->handle );

	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_VALIDATE_STATUS_ARB, &validated );
	if( !validated ) ALERT( at_error, "%s\n%s shader failed to validate\n", GL_PrintInfoLog( shader->handle ), shader->name );
}

int GL_UniformTypeToDwordCount( GLuint type, bool align = false )
{
	switch( type )
	{
	case GL_INT:
	case GL_UNSIGNED_INT:
	case GL_SAMPLER_1D_ARB:
	case GL_SAMPLER_2D_ARB:
	case GL_SAMPLER_3D_ARB:
	case GL_SAMPLER_CUBE_ARB:
	case GL_SAMPLER_1D_SHADOW_ARB:
	case GL_SAMPLER_2D_SHADOW_ARB:
	case GL_SAMPLER_2D_RECT_ARB:
	case GL_SAMPLER_2D_RECT_SHADOW_ARB:
	case GL_SAMPLER_CUBE_SHADOW_EXT:
		return align ? 4 : 1;	// int[1]
	case GL_FLOAT:
		return align ? 4 : 1;	// float[1]
	case GL_FLOAT_VEC2_ARB:
		return align ? 4 : 2;	// float[2]
	case GL_FLOAT_VEC3_ARB:
		return align ? 4 : 3;	// float[3]
	case GL_FLOAT_VEC4_ARB:
		return 4;	// float[4]
	case GL_FLOAT_MAT2_ARB:
		return 4;	// float[2][2]
	case GL_FLOAT_MAT3_ARB:
		return align ? 12 : 9;	// float[3][3]
	case GL_FLOAT_MAT4_ARB:
		return 16;// float[4][4]
	default: return align ? 4 : 1; // assume error
	}
}

const char *GL_UniformTypeToName( GLuint type )
{
	switch( type )
	{
	case GL_INT:
		return "int";
	case GL_UNSIGNED_INT:
		return "uint";
	case GL_FLOAT:
		return "float";
	case GL_FLOAT_VEC2_ARB:
		return "vec2";
	case GL_FLOAT_VEC3_ARB:
		return "vec3";
	case GL_FLOAT_VEC4_ARB:
		return "vec4";
	case GL_FLOAT_MAT2_ARB:
		return "mat2";
	case GL_FLOAT_MAT3_ARB:
		return "mat3";
	case GL_FLOAT_MAT4_ARB:
		return "mat4";
	case GL_SAMPLER_1D_ARB:
		return "sampler1D";
	case GL_SAMPLER_2D_ARB:
		return "sampler2D";
	case GL_SAMPLER_3D_ARB:
		return "sampler3D";
	case GL_SAMPLER_CUBE_ARB:
		return "samplerCube";
	case GL_SAMPLER_1D_ARRAY_EXT:
		return "sampler1DArray";
	case GL_SAMPLER_2D_ARRAY_EXT:
		return "sampler2DArray";
	case GL_SAMPLER_1D_SHADOW_ARB:
		return "sampler1DShadow";
	case GL_SAMPLER_2D_SHADOW_ARB:
		return "sampler2DShadow";
	case GL_SAMPLER_2D_RECT_ARB:
		return "sampler2DRect";
	case GL_SAMPLER_2D_RECT_SHADOW_ARB:
		return "sampler2DRectShadow";
	case GL_SAMPLER_CUBE_SHADOW_EXT:
		return "samplerCubeShadow";
	default:	return "???";
	}
}

void GL_ShowProgramUniforms( glsl_program_t *shader )
{
	int	count, size;
	char	uniformName[256];
	int	total_uniforms_used = 0;
	GLuint	type;

	if( !shader || developer_level < DEV_EXTENDED )
		return;
	
	// install the executables in the program object as part of current state.
	pglUseProgramObjectARB( shader->handle );

	// query the number of active uniforms
	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_ACTIVE_UNIFORMS_ARB, &count );

	// Loop over each of the active uniforms, and set their value
	for( int i = 0; i < count; i++ )
	{
		pglGetActiveUniformARB( shader->handle, i, sizeof( uniformName ), NULL, &size, &type, uniformName );
		if( developer_level >= DEV_EXTENDED )
		{
			if( size != 1 )
			{
				char *end = Q_strchr( uniformName, '[' );
				if( end ) *end = '\0'; // cutoff [0]
			}

			if( size == 1 ) ALERT( at_aiconsole, "uniform %s %s;\n", GL_UniformTypeToName( type ), uniformName );
			else ALERT( at_aiconsole, "uniform %s %s[%i];\n", GL_UniformTypeToName( type ), uniformName, size );
		}
		total_uniforms_used += GL_UniformTypeToDwordCount( type ) * size;
	}

	if( total_uniforms_used >= glConfig.max_vertex_uniforms )
		ALERT( at_error, "used uniforms %i is overflowed max count %i\n", total_uniforms_used, glConfig.max_vertex_uniforms );
	else ALERT( at_aiconsole, "used uniforms %i from %i\n", total_uniforms_used, glConfig.max_vertex_uniforms );

	int max_shader_uniforms = total_uniforms_used;

	if( max_shader_uniforms > ( glConfig.max_skinning_bones * 12 ))
		max_shader_uniforms -= ( glConfig.max_skinning_bones * 12 );

	ALERT( at_aiconsole, "%s used %i uniforms\n", shader->name, max_shader_uniforms );

	if( max_shader_uniforms > glConfig.peak_used_uniforms )
	{
		glConfig.peak_used_uniforms = max_shader_uniforms;
		tr.show_uniforms_peak = true;
	}

	pglUseProgramObjectARB( GL_NONE );
}

static void GL_ParseProgramUniforms( glsl_program_t *shader )
{
	int	count, size;
	char	uniformName[256];
	int	total_uniforms_used = 0;
	char	cleanName[256];
	int	enumUnits = 0;
	GLuint	format;

	// query the number of active uniforms
	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_ACTIVE_UNIFORMS_ARB, &count );
	pglUseProgramObjectARB( shader->handle );

	shader->uniforms = (uniform_t *)Mem_Alloc( count * sizeof( uniform_t ));
	shader->numUniforms = 0;
#ifdef _DEBUG_UNIFORMS
	Msg( "%c%s%s\n", 3, shader->name, GL_PretifyListOptions( shader->options ));
#endif
	// Loop over each of the active uniforms, and set their value
	for( int i = 0; i < count; i++ )
	{
		uniformTable_t *desc;
		uniform_t *uniform;
		int location;

		pglGetActiveUniformARB( shader->handle, i, sizeof( uniformName ), NULL, &size, &format, uniformName );

		if(( location = pglGetUniformLocationARB( shader->handle, uniformName )) == -1 )
			continue; // ignore built-in uniforms

		// remove array size from name
		Q_strncpy( cleanName, uniformName, sizeof( cleanName ));
		char *end = Q_strchr( cleanName, '[' );
		if( end ) *end = '\0'; // cutoff [0]

		// check for description
		for( int j = 0; j < ARRAYSIZE( glsl_uniformTable ); j++ )
		{
			desc = &glsl_uniformTable[j];
			if( !Q_strcmp( desc->name, cleanName ))
				break;
		}

		if( desc->type == UT_UNDEFINED )
		{
			ALERT( at_error, "%cUnhandled uniform %s. Ignoring\n", 3, uniformName );
			continue;
		}

		// fill next uniform
		uniform = &shader->uniforms[shader->numUniforms++];
		Q_strncpy( uniform->name, cleanName, sizeof( uniform->name ));
		uniform->location = location;
		uniform->flags = desc->flags;
		uniform->type = desc->type;
		uniform->format = format;
		uniform->size = size;

		if( FBitSet( uniform->flags, UFL_TEXTURE_UNIT ))
		{
			uniform->unit = enumUnits++;
			pglUniform1iARB( uniform->location, uniform->unit );
		}
		else uniform->unit = -1; // not a texture

		if( enumUnits >= glConfig.max_texture_units )
			ALERT( at_warning, "%c%s [%d] exceeded GL_MAX_IMAGE_UNITS\n", 3, cleanName, enumUnits );			
#ifdef _DEBUG_UNIFORMS
		if( uniform->unit != -1 ) Msg( "%c%s %s : %d;\n", 3, GL_UniformTypeToName( format ), cleanName, uniform->unit );
		else if( size == 1 ) Msg( "%cuniform %s %s;\n", 3, GL_UniformTypeToName( format ), cleanName );
		else Msg( "%cuniform %s %s[%i];\n", 3, GL_UniformTypeToName( format ), cleanName, size );
#endif
		total_uniforms_used += GL_UniformTypeToDwordCount( format ) * size;
	}

	if( total_uniforms_used >= glConfig.max_vertex_uniforms )
		ALERT( at_error, "%cused uniforms %i is overflowed max count %i\n", 3, total_uniforms_used, glConfig.max_vertex_uniforms );
#ifdef _DEBUG_UNIFORMS
	ALERT( at_aiconsole, "%c%s used %i uniforms\n", 3, shader->name, total_uniforms_used );
#endif
	if( total_uniforms_used > glConfig.peak_used_uniforms )
	{
		glConfig.peak_used_uniforms = total_uniforms_used;
		tr.show_uniforms_peak = true;
	}

	pglUseProgramObjectARB( GL_NONE );
}

static void GL_SetDefaultVertexAttribs( glsl_program_t *shader )
{
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_POSITION, "attr_Position" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD0, "attr_TexCoord0" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD1, "attr_TexCoord1" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD2, "attr_TexCoord2" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TANGENT, "attr_Tangent" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BINORMAL, "attr_Binormal" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_NORMAL, "attr_Normal" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_INDEXES, "attr_BoneIndexes" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_WEIGHTS, "attr_BoneWeights" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_STYLES, "attr_LightStyles" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_COLOR, "attr_LightColor" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_VECS, "attr_LightVecs" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_NUMS0, "attr_LightNums0" );
	pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_NUMS1, "attr_LightNums1" );
}

static void GL_ParseProgramVertexAttribs( glsl_program_t *shader )
{
	if( pglGetAttribLocationARB( shader->handle, "attr_Position" ) == ATTR_INDEX_POSITION )
		SetBits( shader->attribs, FATTR_POSITION );
	if( pglGetAttribLocationARB( shader->handle, "attr_TexCoord0" ) == ATTR_INDEX_TEXCOORD0 )
		SetBits( shader->attribs, FATTR_TEXCOORD0 );
	if( pglGetAttribLocationARB( shader->handle, "attr_TexCoord1" ) == ATTR_INDEX_TEXCOORD1 )
		SetBits( shader->attribs, FATTR_TEXCOORD1 );
	if( pglGetAttribLocationARB( shader->handle, "attr_TexCoord2" ) == ATTR_INDEX_TEXCOORD2 )
		SetBits( shader->attribs, FATTR_TEXCOORD2 );
	if( pglGetAttribLocationARB( shader->handle, "attr_Tangent" ) == ATTR_INDEX_TANGENT )
		SetBits( shader->attribs, FATTR_TANGENT );
	if( pglGetAttribLocationARB( shader->handle, "attr_Binormal" ) == ATTR_INDEX_BINORMAL )
		SetBits( shader->attribs, FATTR_BINORMAL );
	if( pglGetAttribLocationARB( shader->handle, "attr_Normal" ) == ATTR_INDEX_NORMAL )
		SetBits( shader->attribs, FATTR_NORMAL );
	if( pglGetAttribLocationARB( shader->handle, "attr_BoneIndexes" ) == ATTR_INDEX_BONE_INDEXES )
		SetBits( shader->attribs, FATTR_BONE_INDEXES );
	if( pglGetAttribLocationARB( shader->handle, "attr_BoneWeights" ) == ATTR_INDEX_BONE_WEIGHTS )
		SetBits( shader->attribs, FATTR_BONE_WEIGHTS );
	if( pglGetAttribLocationARB( shader->handle, "attr_LightStyles" ) == ATTR_INDEX_LIGHT_STYLES )
		SetBits( shader->attribs, FATTR_LIGHT_STYLES );
	if( pglGetAttribLocationARB( shader->handle, "attr_LightColor" ) == ATTR_INDEX_LIGHT_COLOR )
		SetBits( shader->attribs, FATTR_LIGHT_COLOR );
	if( pglGetAttribLocationARB( shader->handle, "attr_LightVecs" ) == ATTR_INDEX_LIGHT_VECS )
		SetBits( shader->attribs, FATTR_LIGHT_VECS );
	if( pglGetAttribLocationARB( shader->handle, "attr_LightNums0" ) == ATTR_INDEX_LIGHT_NUMS0 )
		SetBits( shader->attribs, FATTR_LIGHT_NUMS0 );
	if( pglGetAttribLocationARB( shader->handle, "attr_LightNums1" ) == ATTR_INDEX_LIGHT_NUMS1 )
		SetBits( shader->attribs, FATTR_LIGHT_NUMS1 );
}

void GL_BindShader( glsl_program_t *shader )
{
	if( !shader && RI->currentshader )
	{
		pglUseProgramObjectARB( GL_NONE );
		RI->currentshader = NULL;
	}
	else if( shader != RI->currentshader && shader != NULL && shader->handle )
	{
		pglUseProgramObjectARB( shader->handle );
		r_stats.num_shader_binds++;
		RI->currentshader = shader;
	}
}

static void GL_FreeGPUShader( glsl_program_t *shader )
{
	if( shader && shader->handle )
	{
		uint		hash;
		glsl_program_t	*cur;
		glsl_program_t	**prev;
		const char	*find;

		find = va( "%s %s", shader->name, shader->options );
		if( shader->uniforms != NULL )
		{
			Mem_Free( shader->uniforms );
			shader->uniforms = NULL;
		}

		// remove from hash table
		hash = COM_HashKey( find, SHADERS_HASH_SIZE );
		prev = &glsl_programsHashTable[hash];

		while( 1 )
		{
			cur = *prev;
			if( !cur ) break;

			if( cur == shader )
			{
				*prev = cur->nextHash;
				break;
			}
			prev = &cur->nextHash;
		}

		pglDeleteObjectARB( shader->handle );
		memset( shader, 0, sizeof( *shader ));
	}
}

static glsl_program_t *GL_CreateUberShader( GLint slot, const char *glname, const char *vpname, const char *fpname, const char *options, uint checksum )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return NULL;

	if( num_glsl_programs >= MAX_GLSL_PROGRAMS )
	{
		ALERT( at_error, "GL_CreateUberShader: GLSL shaders limit exceeded (%i max)\n", MAX_GLSL_PROGRAMS );
		return NULL;
	}

	// alloc new shader
	glsl_program_t *shader = &glsl_programs[slot];

	shader->handle = pglCreateProgramObjectARB();
	if( !shader->handle ) return NULL; // some bad happens

	Q_strncpy( shader->name, glname, sizeof( shader->name ));
	Q_strncpy( shader->options, options, sizeof( shader->options ));

	if( !GL_LoadGPUBinaryShader( shader, vpname, fpname, checksum ))
	{
		if( vpname ) GL_LoadGPUShader( shader, vpname, GL_VERTEX_SHADER_ARB, options );
		else SetBits( shader->status, SHADER_VERTEX_COMPILED );
		if( fpname ) GL_LoadGPUShader( shader, fpname, GL_FRAGMENT_SHADER_ARB, options );
		else SetBits( shader->status, SHADER_FRAGMENT_COMPILED );

		if( vpname && FBitSet( shader->status, SHADER_VERTEX_COMPILED ))
			GL_SetDefaultVertexAttribs( shader );

		GL_LinkProgram( shader );

		if( FBitSet( shader->status, SHADER_PROGRAM_LINKED ))
			GL_SaveGPUBinaryShader( shader, checksum );
	}

	// dynamic ubershaders has the identically name of fragment and vertex program
	if(( glname == vpname ) && ( glname == fpname ))
		SetBits( shader->status, SHADER_UBERSHADER ); // it's UberShader!

	if( FBitSet( shader->status, SHADER_PROGRAM_LINKED ))
	{
		// register shader uniforms
		GL_ParseProgramVertexAttribs( shader );
		GL_ParseProgramUniforms( shader );
		GL_ValidateProgram( shader );
	}

	// rewind generated errors if shader compile was failed
	// to avoid show them later e.g. on textures loading
	while( pglGetError() != GL_NO_ERROR );
#ifdef _DEBUG_UNIFORMS
	if( FBitSet( shader->status, SHADER_UBERSHADER ))
		ALERT( at_aiconsole, "CompileUberShader #%i: %s\n%s\n", slot, glname, options );
	else ALERT( at_aiconsole, "CompileShader #%i: %s\n%s\n", slot, glname, options );
#endif
	if( slot == num_glsl_programs )
	{
		if( num_glsl_programs == MAX_GLSL_PROGRAMS )
		{
			ALERT( at_error, "GL_CreateUberShader: GLSL shaders limit exceeded (%i max)\n", MAX_GLSL_PROGRAMS );
			GL_FreeGPUShader( shader );
			return NULL;
		}
		num_glsl_programs++;
	}

	// all done, program is loaded

	return shader;
}

word GL_FindUberShader( const char *glname, const char *options )
{
	glsl_program_t	*prog;

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return 0;

	ASSERT( glname != NULL );

	const char *find = va( "%s %s", glname, options );
	uint hash = COM_HashKey( find, SHADERS_HASH_SIZE );

	// check for coexist
	for( prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
	{
		if( !Q_strcmp( prog->name, glname ) && !Q_strcmp( prog->options, options ))
			return (word)(prog - glsl_programs);
	}

	int i;
	// find free spot
	for( i = 1; i < num_glsl_programs; i++ )
		if( !glsl_programs[i].name[0] )
			break;

	double start = Sys_DoubleTime();
	uint checksum = FILE_CRC32( find, Q_strlen( find ));
	prog = GL_CreateUberShader( i, glname, glname, glname, options, checksum );
	double end = Sys_DoubleTime();
	r_buildstats.compile_shader += (end - start);
	if( RENDER_GET_PARM( PARM_CLIENT_ACTIVE, 0 ))
		r_buildstats.total_buildtime += (end - start);

	if( prog != NULL )
	{
		// add to hash table
		prog->nextHash = glsl_programsHashTable[hash];
		glsl_programsHashTable[hash] = prog;
	}

	return (word)(prog - glsl_programs);
}

word GL_FindShader( const char *glname, const char *vpname, const char *fpname, const char *options )
{
	glsl_program_t	*prog;

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return 0;

	ASSERT( glname != NULL );

	const char *find = va( "%s %s", glname, options );
	uint hash = COM_HashKey( find, SHADERS_HASH_SIZE );

	// check for coexist
	for( prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
	{
		if( !Q_strcmp( prog->name, glname ) && !Q_strcmp( prog->options, options ))
			return (word)(prog - glsl_programs);
	}

	int i;
	// find free spot
	for( i = 1; i < num_glsl_programs; i++ )
		if( !glsl_programs[i].name[0] )
			break;

	double start = Sys_DoubleTime();
	uint checksum = FILE_CRC32( find, Q_strlen( find ));
	prog = GL_CreateUberShader( i, glname, vpname, fpname, options, checksum );
	double end = Sys_DoubleTime();
	r_buildstats.compile_shader += (end - start);
	if( RENDER_GET_PARM( PARM_CLIENT_ACTIVE, 0 ))
		r_buildstats.total_buildtime += (end - start);

	if( prog != NULL )
	{
		// add to hash table
		prog->nextHash = glsl_programsHashTable[hash];
		glsl_programsHashTable[hash] = prog;
	}

	return (word)(prog - glsl_programs);
}

void GL_AddShaderFeature( word shaderNum, int feature )
{
	if( shaderNum <= 0 || shaderNum >= MAX_GLSL_PROGRAMS )
		return;

	glsl_program_t *shader = &glsl_programs[shaderNum];
	SetBits( shader->status, feature );
}

void GL_SetShaderDirective( char *options, const char *directive )
{
	options[0] = '\0';
	Q_strncat( options, va( "#define %s\n", directive ), MAX_OPTIONS_LENGTH );
}

void GL_AddShaderDirective( char *options, const char *directive )
{
	Q_strncat( options, va( "#define %s\n", directive ), MAX_OPTIONS_LENGTH );
}

const char *GL_PretifyListOptions( const char *options, bool newlines )
{
	static char output[MAX_OPTIONS_LENGTH];
	const char *pstart = options;
	const char *pend = options + Q_strlen( options );
	char *pout = output;

	*pout = '\0';

	while( pstart < pend )
	{
		const char *pfind = Q_strstr( pstart, "#define" );
		if( !pfind ) break;

		pstart = pfind + Q_strlen( "#define" );

		for( ; *pstart != '\n'; pstart++, pout++ )
			*pout = *pstart;
		if( newlines )
			*pout++ = *pstart++;
		else pstart++; // skip '\n'
	}

	if( pout == output )
		return ""; // nothing found

	*pout++ = ' ';
	*pout = '\0';

	return output;
}

void GL_CheckTextureAlpha( char *options, int texturenum )
{
	if( RENDER_GET_PARM( PARM_TEX_ENCODE, texturenum ) == DXT_ENCODE_ALPHA_SDF )
		GL_AddShaderDirective( options, "SIGNED_DISTANCE_FIELD" );
}

void GL_EncodeNormal( char *options, int texturenum )
{
	if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_RED_GREEN_RGTC2_EXT )
	{
		GL_AddShaderDirective( options, "NORMAL_RG_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI )
	{
		GL_AddShaderDirective( options, "NORMAL_3DC_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_ENCODE, texturenum ) == DXT_ENCODE_NORMAL_AG_PARABOLOID )
	{
		GL_AddShaderDirective( options, "NORMAL_AG_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT )
	{
		// implicit DXT5NM format (Paranoia2 v 1.2 old stuff)
		if( FBitSet( RENDER_GET_PARM( PARM_TEX_FLAGS, texturenum ), TF_HAS_ALPHA ))
			GL_AddShaderDirective( options, "NORMAL_AG_PARABOLOID" );
	}
}

void GL_ListGPUShaders()
{
	int count = 0;

	for( uint i = 1; i < num_glsl_programs; i++ )
	{
		glsl_program_t *cur = &glsl_programs[i];
		if( !cur->name[0] ) continue;

		const char *options = GL_PretifyListOptions( cur->options );

		if( Q_stricmp( options, "" ))
			Msg( "#%i (%i) %s [%s]\n", i, cur->handle, cur->name, options );
		else 
			Msg( "#%i (%i) %s\n", i, cur->handle, cur->name );
		count++;
	}

	Msg( "Total %i shaders\n", count );
}

void GL_ReloadShaders()
{
	GL_FreeUberShaders();
	gEngfuncs.Con_Printf("GL_ReloadShaders: ubershaders reloading requested\n");
	tr.params_changed = true;
}

void GL_InitGPUShaders()
{
	char options[MAX_OPTIONS_LENGTH];

	memset( &glsl_programs, 0, sizeof( glsl_programs ));
	num_glsl_programs = 1; // entry #0 isn't used

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	ADD_COMMAND("shaderlist", GL_ListGPUShaders);
	ADD_COMMAND("r_reloadshaders", GL_ReloadShaders);

	// init sky shaders
	GL_SetShaderDirective( options, "SKYBOX_DAYTIME" );
	tr.skyboxEnv[0] = GL_FindShader( "forward/skybox", "forward/generic", "forward/skybox" );
	tr.skyboxEnv[1] = GL_FindShader( "forward/skybox", "forward/generic", "forward/skybox", options );
	tr.defSceneSky = GL_FindShader( "deferred/skybox", "deferred/generic", "deferred/sky_scene" );
	tr.defLightSky = GL_FindShader( "deferred/skybox", "deferred/generic", "deferred/sky_light" );
}

void GL_FreeUberShaders( void )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	for( uint i = 1; i < num_glsl_programs; i++ )
	{
		if( FBitSet( glsl_programs[i].status, SHADER_UBERSHADER )) 
			GL_FreeGPUShader( &glsl_programs[i] );
	}

	GL_BindShader( GL_NONE );
}

void GL_FreeGPUShaders( void )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	for( uint i = 1; i < num_glsl_programs; i++ )
		GL_FreeGPUShader( &glsl_programs[i] );

	GL_BindShader( GL_NONE );
}