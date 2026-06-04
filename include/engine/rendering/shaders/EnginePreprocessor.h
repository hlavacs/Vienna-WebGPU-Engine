#pragma once

/**
 * @file EnginePreprocessor.h
 * @brief Variadic preprocessor helpers used by the engine struct/binding macros.
 *
 * Standard VA_ARGS counter + FOREACH-stringify. The `ENGINE_PP_EXPAND` wrapper
 * works around MSVC's older preprocessor treating `__VA_ARGS__` as a single
 * token in some contexts; new code can also opt into `/Zc:preprocessor` for
 * strict ISO behaviour, but the wrapper keeps us portable either way.
 *
 * Max arity matches `aggregate_reflect::kMaxFields`. If you bump one, bump
 * both.
 */

#define ENGINE_PP_EXPAND(x) x
#define ENGINE_PP_STRINGIFY_RAW(x) #x
#define ENGINE_PP_STRINGIFY(x) ENGINE_PP_STRINGIFY_RAW(x)
#define ENGINE_PP_CONCAT_RAW(a, b) a##b
#define ENGINE_PP_CONCAT(a, b) ENGINE_PP_CONCAT_RAW(a, b)

#define ENGINE_PP_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define ENGINE_PP_COUNT(...) ENGINE_PP_EXPAND(ENGINE_PP_NTH_ARG(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

// Stringify each variadic argument, comma-separated. Up to 16 arguments.
#define ENGINE_PP_S1(a)                                          #a
#define ENGINE_PP_S2(a, b)                                       #a, #b
#define ENGINE_PP_S3(a, b, c)                                    #a, #b, #c
#define ENGINE_PP_S4(a, b, c, d)                                 #a, #b, #c, #d
#define ENGINE_PP_S5(a, b, c, d, e)                              #a, #b, #c, #d, #e
#define ENGINE_PP_S6(a, b, c, d, e, f)                           #a, #b, #c, #d, #e, #f
#define ENGINE_PP_S7(a, b, c, d, e, f, g)                        #a, #b, #c, #d, #e, #f, #g
#define ENGINE_PP_S8(a, b, c, d, e, f, g, h)                     #a, #b, #c, #d, #e, #f, #g, #h
#define ENGINE_PP_S9(a, b, c, d, e, f, g, h, i)                  #a, #b, #c, #d, #e, #f, #g, #h, #i
#define ENGINE_PP_S10(a, b, c, d, e, f, g, h, i, j)              #a, #b, #c, #d, #e, #f, #g, #h, #i, #j
#define ENGINE_PP_S11(a, b, c, d, e, f, g, h, i, j, k)           #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k
#define ENGINE_PP_S12(a, b, c, d, e, f, g, h, i, j, k, l)        #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k, #l
#define ENGINE_PP_S13(a, b, c, d, e, f, g, h, i, j, k, l, m)     #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k, #l, #m
#define ENGINE_PP_S14(a, b, c, d, e, f, g, h, i, j, k, l, m, n)  #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k, #l, #m, #n
#define ENGINE_PP_S15(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k, #l, #m, #n, #o
#define ENGINE_PP_S16(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) #a, #b, #c, #d, #e, #f, #g, #h, #i, #j, #k, #l, #m, #n, #o, #p

#define ENGINE_PP_FOREACH_STRINGIFY_IMPL(N, ...) ENGINE_PP_EXPAND(ENGINE_PP_CONCAT(ENGINE_PP_S, N)(__VA_ARGS__))
#define ENGINE_PP_FOREACH_STRINGIFY(...) ENGINE_PP_FOREACH_STRINGIFY_IMPL(ENGINE_PP_COUNT(__VA_ARGS__), __VA_ARGS__)
