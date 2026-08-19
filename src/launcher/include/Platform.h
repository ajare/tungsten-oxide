#pragma once

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define APP_PLATFORM_WINDOWS 1
#define APP_PLATFORM_LINUX 2
#define APP_PLATFORM_APPLE 3

#define APP_COMPILER_MSVC 1
#define APP_COMPILER_GNUC 2
#define APP_COMPILER_BORL 3

// Find compiler information
#if defined( _MSC_VER )
#   define APP_COMPILER APP_COMPILER_MSVC
#   define APP_COMP_VER _MSC_VER
#elif defined( __GNUC__ )
#   define APP_COMPILER APP_COMPILER_GNUC
#   define APP_COMP_VER (((__GNUC__)*100) + \
	(__GNUC_MINOR__ * 10) + \
	__GNUC_PATCHLEVEL__)
#elif defined( __BORLANDC__ )
#   define APP_COMPILER APP_COMPILER_BORL
#   define APP_COMP_VER __BCPLUSPLUS__
#else
#   pragma error "Unknown compiler."
#endif

// Set platform
#if defined( __WIN32__ ) || defined( _WIN32 )
#   define APP_PLATFORM APP_PLATFORM_WINDOWS
#elif defined( __APPLE_CC__)
#   define APP_PLATFORM APP_PLATFORM_APPLE
#else
#   define APP_PLATFORM APP_PLATFORM_LINUX
#endif

// Ok, because only occurs on non-public STL members
#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#	pragma warning(disable: 4251)
#endif

// Shared root namespace
#define APP_NAMESPACE wp
