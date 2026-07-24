#pragma once

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define WP_PLATFORM_WINDOWS 1
#define WP_PLATFORM_LINUX 2
#define WP_PLATFORM_APPLE 3

#define WP_COMPILER_MSVC 1
#define WP_COMPILER_GNUC 2
#define WP_COMPILER_BORL 3

#define WP_PLATFORM_BITS (sizeof(size_t) << 3)

// Find compiler information
#if defined( _MSC_VER )
#   define WP_COMPILER WP_COMPILER_MSVC
#   define WP_COMP_VER _MSC_VER
#elif defined( __GNUC__ )
#   define WP_COMPILER WP_COMPILER_GNUC
#   define WP_COMP_VER (((__GNUC__)*100) + \
	(__GNUC_MINOR__ * 10) + \
	__GNUC_PATCHLEVEL__)
#elif defined( __BORLANDC__ )
#   define WP_COMPILER WP_COMPILER_BORL
#   define WP_COMP_VER __BCPLUSPLUS__
#else
#   pragma error "Unknown compiler."
#endif

// Set platform
#if defined( __WIN32__ ) || defined( _WIN32 )
#   define WP_PLATFORM WP_PLATFORM_WINDOWS
#elif defined( __APPLE_CC__)
#   define WP_PLATFORM WP_PLATFORM_APPLE
#else
#   define WP_PLATFORM WP_PLATFORM_LINUX
#endif

// DLL Export
#if WP_PLATFORM == WP_PLATFORM_WINDOWS
#	if defined(WP_GEOMETRY_DLL_EXPORT)
#		define WP_GEOMETRY_API __declspec( dllexport )
#	elif defined(WP_GEOMETRY_STATIC_LIB)
#		define WP_GEOMETRY_API
#	else
#		if defined(__MINGW32__)
#			define WP_GEOMETRY_API
#		else
#			define WP_GEOMETRY_API __declspec( dllimport )
#		endif
#	endif
#elif WP_PLATFORM == WP_PLATFORM_LINUX
#	if defined(WP_GEOMETRY_DLL_EXPORT)
#		define WP_GEOMETRY_API __attribute__((visibility("default")))
#	else
#		define WP_GEOMETRY_API
#	endif
#endif

// Ok, because only occurs on non-public STL members
#if WP_PLATFORM == WP_PLATFORM_WINDOWS
#	pragma warning(disable: 4251)
#endif

// Shared root namespace
#define WP_NAMESPACE wp

// Memleak tracking
#ifdef WP_USE_MEMLEAK_TRACKING
#  if _MSC_VER < 1930
#    include <vld.h>
#  endif
#endif

// Unused params
#define WP_UNUSED(x) (void)(x)