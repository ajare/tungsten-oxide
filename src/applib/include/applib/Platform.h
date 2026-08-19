#pragma once

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define APPLIB_PLATFORM_WIN32 1
#define APPLIB_PLATFORM_LINUX 2
#define APPLIB_PLATFORM_APPLE 3

#define APPLIB_COMPILER_MSVC 1
#define APPLIB_COMPILER_GNUC 2
#define APPLIB_COMPILER_BORL 3

// Find compiler information
#if defined( _MSC_VER )
#   define APPLIB_COMPILER APPLIB_COMPILER_MSVC
#   define APPLIB_COMP_VER _MSC_VER
#elif defined( __GNUC__ )
#   define APPLIB_COMPILER APPLIB_COMPILER_GNUC
#   define APPLIB_COMP_VER (((__GNUC__)*100) + \
	(__GNUC_MINOR__ * 10) + \
	__GNUC_PATCHLEVEL__)
#elif defined( __BORLANDC__ )
#   define APPLIB_COMPILER APPLIB_COMPILER_BORL
#   define APPLIB_COMP_VER __BCPLUSPLUS__
#else
#   pragma error "Unknown compiler."
#endif

// Set platform
#if defined( __WIN32__ ) || defined( _WIN32 )
#   define APPLIB_PLATFORM APPLIB_PLATFORM_WIN32
#elif defined( __APPLE_CC__)
#   define APPLIB_PLATFORM APPLIB_PLATFORM_APPLE
#else
#   define APPLIB_PLATFORM APPLIB_PLATFORM_LINUX
#endif

// DLL Export
#if APPLIB_PLATFORM == APPLIB_PLATFORM_WIN32
#	if defined(APPLIB_DLL_EXPORT)
#		define APPLIB_API __declspec( dllexport )
#	elif defined(APPLIB_STATIC_LIB)
#		define APPLIB_API
#	else
#		if defined(__MINGW32__)
#			define APPLIB_API
#		else
#			define APPLIB_API __declspec( dllimport )
#		endif
#	endif
#elif APPLIB_PLATFORM == APPLIB_PLATFORM_LINUX
#	if defined(APPLIB_DLL_EXPORT)
#		define APPLIB_API __attribute__((visibility("default")))
#	else
#		define APPLIB_API
#	endif
#endif

// Ok, because only occurs on non-public STL members
#if APPLIB_PLATFORM == APPLIB_PLATFORM_WIN32
#	pragma warning(disable: 4251)
#endif

// Memleak tracking
#ifdef APPLIB_USE_MEMLEAK_TRACKING
#	include "vld.h"
#endif

// Unused params
#define VAR_UNUSED(x) (void)(x)