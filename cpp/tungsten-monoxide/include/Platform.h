#pragma once

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define MIM_PLATFORM_WIN32 1
#define MIM_PLATFORM_LINUX 2
#define MIM_PLATFORM_APPLE 3

#define MIM_COMPILER_MSVC 1
#define MIM_COMPILER_GNUC 2
#define MIM_COMPILER_BORL 3

// Find compiler information
#if defined( _MSC_VER )
#   define MIM_COMPILER MIM_COMPILER_MSVC
#   define MIM_COMP_VER _MSC_VER
#elif defined( __GNUC__ )
#   define MIM_COMPILER MIM_COMPILER_GNUC
#   define MIM_COMP_VER (((__GNUC__)*100) + \
	(__GNUC_MINOR__ * 10) + \
	__GNUC_PATCHLEVEL__)
#elif defined( __BORLANDC__ )
#   define MIM_COMPILER MIM_COMPILER_BORL
#   define MIM_COMP_VER __BCPLUSPLUS__
#else
#   pragma error "Unknown compiler."
#endif

// Set platform
#if defined( __WIN32__ ) || defined( _WIN32 )
#   define MIM_PLATFORM MIM_PLATFORM_WIN32
#elif defined( __APPLE_CC__)
#   define MIM_PLATFORM MIM_PLATFORM_APPLE
#else
#   define MIM_PLATFORM MIM_PLATFORM_LINUX
#endif

// DLL Export
#if MIM_PLATFORM == MIM_PLATFORM_WIN32
#	if defined(APPLICATION_DLL_EXPORT)
#		define APPLICATION_API __declspec( dllexport )
#	elif defined(APPLICATION_STATIC_LIB)
#		define APPLICATION_API
#	else
#		if defined(__MINGW32__)
#			define APPLICATION_API
#		else
#			define APPLICATION_API __declspec( dllimport )
#		endif
#	endif
#elif MIM_PLATFORM == MIM_PLATFORM_LINUX
#	if defined(APPLICATION_API)
#		define APPLICATION_API __attribute__((visibility("default")))
#	else
#		define APPLICATION_API
#	endif
#endif

// Ok, because only occurs on non-public STL members
#if MIM_PLATFORM == MIM_PLATFORM_WIN32
#	pragma warning(disable: 4251)
#endif

// Unused params
#define VAR_UNUSED(x) (void)(x)