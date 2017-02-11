include_directories(..)

set(VASAN_CFLAGS ${SANITIZER_COMMON_CFLAGS} -U_FORTIFY_SOURCE)

add_custom_target(vasan)

add_compiler_rt_runtime(clang_rt.vasan
        STATIC
        ARCHS x86_64 
        SOURCES vasan.c stack.c hashmap.c
        CFLAGS ${VASAN_CFLAGS} -fvisibility=default -DNO_BACKTRACE
        PARENT_TARGET vasan)

add_sanitizer_rt_symbols(clang_rt.vasan)

add_dependencies(compiler-rt vasan)
