#pragma once

#include "utils.h"
#include "Offset.h"

// An offset missing from the MCC build (src/offsets) makes INVOKE a no-op returning return_type{} and the
// pointers null.
#define DefNative(name) \
    namespace name::Native { \
        extern ThreadLocalStorage s_nativeInfo; \
        namespace Function {                                                      \
            template<typename return_type, typename... Args>                            \
            inline return_type INVOKE(const ::AlphaRing::Offset& offset, Args... args) { \
                typedef return_type (__fastcall* func_t)(...);                          \
                if (!offset.found()) return return_type();                              \
                return ((func_t)(Native::s_nativeInfo.m_hModule + offset))(args...);      \
            }                                                                           \
        }                                                                               \
    }                      \
    namespace name::Native

#define DefPtr(name, offset) \
        struct name##_t;      \
        inline name##_t* name() {return (offset).found() ? (name##_t*)(s_nativeInfo.m_hModule + offset) : nullptr;} \
        struct name##_t

#define DefPPtr(name, offset1, offset2) \
        struct name##_t;      \
        inline name##_t* name() {return (offset1).found() ? (name##_t*)(*(__int64*)(s_nativeInfo.m_hModule + offset1) + offset2) : nullptr;} \
        struct name##_t
