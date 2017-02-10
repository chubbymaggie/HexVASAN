include_directories(..)

set(VASAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
#append_no_rtti_flag(VASAN_CFLAGS)

add_custom_target(vasan)
add_custom_target(vasan_weak)
#add_custom_target(vasancaller)

#add_compiler_rt_object_libraries(RTHexTypeCommon
#    OS ${SANITIZER_COMMON_SUPPORTED_OS}
#    ARCHS ${VASAN_COMMON_SUPPORTED_ARCH}
#    SOURCES ${VASAN_COMMON_SOURCES}
#    CFLAGS ${VASAN_CFLAGS})

add_compiler_rt_runtime(clang_rt.vasan
        STATIC
        ARCHS x86_64 
        SOURCES vasan.cc vasancaller.cc
#                $<TARGET_OBJECTS:RTInterception.x86_64>
#                $<TARGET_OBJECTS:RTSanitizerCommon.x86_64>
#                $<TARGET_OBJECTS:RTSanitizerCommonLibc.x86_64>
#                $<TARGET_OBJECTS:RTHexTypeCommon.x86_64>
        CFLAGS ${VASAN_CFLAGS}
 
        PARENT_TARGET vasan)

add_sanitizer_rt_symbols(clang_rt.vasan)

add_compiler_rt_runtime(clang_rt.vasan_weak
        STATIC 
        ARCHS x86_64 
        SOURCES vasan_weak.cc
#                $<TARGET_OBJECTS:RTInterception.x86_64>
#                $<TARGET_OBJECTS:RTSanitizerCommon.x86_64>
#                $<TARGET_OBJECTS:RTSanitizerCommonLibc.x86_64>
#                $<TARGET_OBJECTS:RTHexTypeCommon.x86_64>
        CFLAGS ${VASAN_CFLAGS}
 
        PARENT_TARGET vasan_weak)

add_sanitizer_rt_symbols(clang_rt.vasan_weak)

add_dependencies(compiler-rt vasan)
add_dependencies(compiler-rt vasan_weak)
#add_dependencies(compiler-rt vasancaller)

##add_sanitizer_rt_symbols(clang_rt.vachecker)

