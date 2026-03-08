# we put string.c into a separate library for two reasons:
#  1) so we can build it without LTO, to avoid link issues;
#  2) so we can make the string.h header available to
#     the lpc library

add_library(string STATIC string.c)
target_compile_options(string PRIVATE -Os -Wall -Wmissing-declarations -Werror -Wstack-usage=512 -fstack-usage -fsingle-precision-constant)
target_include_directories(string PUBLIC .)
set_target_properties(string PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)

