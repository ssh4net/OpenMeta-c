#ifndef OMC_BASE_H
#define OMC_BASE_H

#ifdef __cplusplus
#define OMC_EXTERN_C_BEGIN extern "C" {
#define OMC_EXTERN_C_END }
#else
#define OMC_EXTERN_C_BEGIN
#define OMC_EXTERN_C_END
#endif

#if defined(_WIN32) && defined(OMC_BUILD_SHARED)
#if defined(OMC_BUILDING_SHARED)
#define OMC_API __declspec(dllexport)
#else
#define OMC_API __declspec(dllimport)
#endif
#else
#define OMC_API
#endif

#define OMC_VERSION_MAJOR 0
#define OMC_VERSION_MINOR 1
#define OMC_VERSION_PATCH 0

#endif
