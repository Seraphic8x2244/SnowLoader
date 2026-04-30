#pragma once

#include <Windows.h>

typedef enum MH_STATUS
{
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_HOOKED,
    MH_ERROR_NOT_HOOKED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND,
    MH_ERROR_UNKNOWN = -1
} MH_STATUS;

#ifndef MH_ALL_HOOKS
#define MH_ALL_HOOKS nullptr
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_MINHOOK
    MH_STATUS WINAPI MH_Initialize(void);
    MH_STATUS WINAPI MH_Uninitialize(void);
    MH_STATUS WINAPI MH_CreateHook(void* pTarget, void* pDetour, void** ppOriginal);
    MH_STATUS WINAPI MH_EnableHook(void* pTarget);
    MH_STATUS WINAPI MH_DisableHook(void* pTarget);
    MH_STATUS WINAPI MH_RemoveHook(void* pTarget);
#else
    static inline MH_STATUS WINAPI MH_Initialize(void)
    {
        return MH_ERROR_NOT_INITIALIZED;
    }

    static inline MH_STATUS WINAPI MH_Uninitialize(void)
    {
        return MH_OK;
    }

    static inline MH_STATUS WINAPI MH_CreateHook(void*, void*, void**)
    {
        return MH_ERROR_NOT_INITIALIZED;
    }

    static inline MH_STATUS WINAPI MH_EnableHook(void*)
    {
        return MH_ERROR_NOT_INITIALIZED;
    }

    static inline MH_STATUS WINAPI MH_DisableHook(void*)
    {
        return MH_OK;
    }

    static inline MH_STATUS WINAPI MH_RemoveHook(void*)
    {
        return MH_OK;
    }
#endif

#ifdef __cplusplus
}
#endif
