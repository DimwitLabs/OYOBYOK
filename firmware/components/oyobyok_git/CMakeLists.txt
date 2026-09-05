idf_component_register(SRCS "oyobyok_git.c" INCLUDE_DIRS "../../shared" REQUIRES libgit2 fatfs)
# libgit2's *_OPTIONS_INIT macros leave trailing fields to the compiler.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
