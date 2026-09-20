#pragma once

#include <utils.h>
#include <functional>

#include "./hook/Hook.h"
#include "./log/Log.h"
#include "./global/Global.h"
#include "./filesystem/Filesystem.h"

#undef NDEBUG
#include <assert.h>

#define assertm(exp, msg) \
    do { \
        if (!(exp)) { \
            AlphaRing::Log::AssertFailure(#exp, msg, __FILE__, __LINE__); \
        } \
        assert(((void)msg, (exp))); \
    } while (0)
