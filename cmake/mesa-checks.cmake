# Compiler checks -> pre_args, mirroring meson.build sections.
# SPDX-License-Identifier: MIT

set(CMAKE_REQUIRED_FLAGS "")
set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

# GCC-style builtins
foreach(_b IN ITEMS bswap32 bswap64 clz clzll ctz expect ffs ffsll popcount popcountll unreachable)
  string(TOUPPER "${_b}" _bu)
  CHECK_SYMBOL_EXISTS(__builtin_${_b} "stdarg.h" __mesa_builtin_${_b})
  if(__mesa_builtin_${_b})
    add_compile_definitions(-DHAVE___BUILTIN_${_bu})
  endif()
endforeach()
CHECK_SYMBOL_EXISTS(__builtin_types_compatible_p "stddef.h" __mesa_has_types_compatible_p)
if(__mesa_has_types_compatible_p)
  add_compile_definitions(-DHAVE___BUILTIN_TYPES_COMPATIBLE_P)
endif()
foreach(_b IN ITEMS add_overflow add_overflow_p sub_overflow_p)
  string(TOUPPER "${_b}" _bu)
  CHECK_SYMBOL_EXISTS("__builtin_${_b}" "stdint.h" __mesa_has_${_bu})
  if(__mesa_has_${_bu})
    add_compile_definitions(-DHAVE___BUILTIN_${_bu})
  endif()
endforeach()
CHECK_SYMBOL_EXISTS(__builtin_ia32_clflushopt "stdint.h" __mesa_clflushopt)
if(__mesa_clflushopt)
  add_compile_definitions(-DHAVE___BUILTIN_IA32_CLFLUSHOPT)
endif()

# GCC function attributes
foreach(_a IN ITEMS const flatten malloc packed pure returns_nonnull format unused warn_unused_result alias noreturn optimize cold)
  string(TOUPPER "${_a}" _au)
  set(_code "int my_attr_${_a}(void) __attribute__((__${_a}__));")
  if(_a STREQUAL "const")
    set(_code "int my_attr_const(void) __attribute__((const));")
  elseif(_a STREQUAL "pure")
    set(_code "int my_attr_pure(void) __attribute__((pure));")
  elseif(_a STREQUAL "unused")
    set(_code "static int my_attr_unused __attribute__((unused));")
  elseif(_a STREQUAL "format")
    set(_code "int my_attr_format(const char *f, ...) __attribute__((format(printf, 1, 2)));")
  elseif(_a STREQUAL "packed")
    set(_code "struct s { char c; int i; } __attribute__((packed));")
  elseif(_a STREQUAL "alias")
    set(_code "extern int x; extern int y __attribute__((alias(\"x\"))); int x = 1;")
  elseif(_a STREQUAL "noreturn")
    set(_code "__attribute__((noreturn)) void hang(void);")
  elseif(_a STREQUAL "cold")
    set(_code "void cold_fn(void) __attribute__((cold));")
  elseif(_a STREQUAL "returns_nonnull")
    set(_code "void *f(void) __attribute__((returns_nonnull));")
  elseif(_a STREQUAL "flatten")
    set(_code "void f(void) __attribute__((flatten));")
  elseif(_a STREQUAL "malloc")
    set(_code "void *f(void) __attribute__((malloc));")
  elseif(_a STREQUAL "optimize")
    set(_code "void f(void) __attribute__((optimize(\"O3\")));")
  elseif(_a STREQUAL "warn_unused_result")
    set(_code "int f(void) __attribute__((warn_unused_result));")
  endif()
  check_c_source_compiles("${_code}
int main(void){return 0;}" __mesa_attr_${_a})
  if(__mesa_attr_${_a})
    add_compile_definitions(-DHAVE_FUNC_ATTRIBUTE_${_au})
  endif()
endforeach()
check_c_source_compiles("__attribute__((visibility(\"hidden\"))) void f(void);" HAVE_FUNC_ATTRIBUTE_VISIBILITY)
if(HAVE_FUNC_ATTRIBUTE_VISIBILITY)
  add_compile_definitions(-DHAVE_FUNC_ATTRIBUTE_VISIBILITY)
endif()

check_c_source_compiles("__uint128_t foo(void) { return 0; }" HAVE_UINT128)
if(HAVE_UINT128)
  add_compile_definitions(-DHAVE_UINT128)
endif()
CHECK_SYMBOL_EXISTS(reallocarray stdlib.h __mesa_reallocarray)
if(__mesa_reallocarray)
  add_compile_definitions(-DHAVE_REALLOCARRAY)
endif()
CHECK_SYMBOL_EXISTS(fmemopen stdio.h __mesa_fmemopen)
if(__mesa_fmemopen)
  add_compile_definitions(-DHAVE_FMEMOPEN)
endif()

# atomics
check_c_source_compiles("
#include <stdint.h>
int main() {
  struct { uint64_t *v; } x;
  return (int)__atomic_load_n(x.v, __ATOMIC_ACQUIRE) &
         (int)__atomic_add_fetch(x.v, (uint64_t)1, __ATOMIC_ACQ_REL);
}" USE_GCC_ATOMIC_BUILTINS)
if(USE_GCC_ATOMIC_BUILTINS)
  add_compile_definitions(-DUSE_GCC_ATOMIC_BUILTINS)
endif()
check_c_source_compiles("
#include <stdint.h>
uint64_t v;
int main() { return __sync_add_and_fetch(&v, (uint64_t)1); }" __mesa_sync64)
if(NOT __mesa_sync64)
  add_compile_definitions(-DMISSING_64BIT_ATOMICS)
endif()

# headers
set(_hdrs xlocale.h linux/futex.h endian.h dlfcn.h sys/shm.h cet.h pthread_np.h poll.h sys/inotify.h linux/udmabuf.h)
set(_hdrs_up XLOCALE_H LINUX_FUTEX_H ENDIAN_H DLFCN_H SYS_SHM_H CET_H PTHREAD_NP_H POLL_H SYS_INOTIFY_H LINUX_UDMABUF_H)
list(LENGTH _hdrs _n)
math(EXPR _last "${_n}-1")
foreach(_i RANGE 0 ${_last})
  list(GET _hdrs ${_i} _h)
  string(MAKE_C_IDENTIFIER "${_h}" _hid)
  list(GET _hdrs_up ${_i} _hu)
  check_include_file("${_h}" __mesa_hdr_${_i})
  if(__mesa_hdr_${_i})
    add_compile_definitions(-DHAVE_${_hu})
  endif()
endforeach()
check_include_file("sched.h" __mesa_sched_h)
if(__mesa_sched_h)
  add_compile_definitions(-DHAS_SCHED_H)
endif()
check_c_source_compiles("#include <sched.h>
int main(){ cpu_set_t c; return sched_getaffinity(0, sizeof(c), &c); }" __mesa_sched_affinity)
if(__mesa_sched_affinity)
  add_compile_definitions(-DHAS_SCHED_GETAFFINITY)
endif()

check_c_source_compiles("
#include <sys/sysmacros.h>
int main(){ return major(0)+minor(1)+makedev(2,3); }" MAJOR_IN_SYSMACROS)
if(MAJOR_IN_SYSMACROS)
  add_compile_definitions(-DMAJOR_IN_SYSMACROS)
endif()

# functions
foreach(_f IN ITEMS strtof mkostemp random_r flock strtok_r qsort_s posix_fallocate secure_getenv)
  string(TOUPPER "${_f}" _fu)
  CHECK_SYMBOL_EXISTS(${_f} "stdlib.h" __mesa_fn_${_fu})
  if(__mesa_fn_${_fu})
    add_compile_definitions(-DHAVE_${_fu})
  endif()
endforeach()
CHECK_SYMBOL_EXISTS(memfd_create "sys/mman.h" __mesa_memfd_create)
if(__mesa_memfd_create)
  add_compile_definitions(-DHAVE_MEMFD_CREATE)
endif()
CHECK_SYMBOL_EXISTS(getrandom "sys/random.h" __mesa_getrandom)
if(__mesa_getrandom)
  add_compile_definitions(-DHAVE_GETRANDOM)
endif()
CHECK_SYMBOL_EXISTS(sysconf "unistd.h" __mesa_sysconf)
if(__mesa_sysconf)
  add_compile_definitions(-DHAVE_SYSCONF)
endif()
CHECK_SYMBOL_EXISTS(thrd_create "threads.h" __mesa_thrd)
if(__mesa_thrd AND with_platform_android)
  add_compile_definitions(-DHAVE_THRD_CREATE)
endif()

# GNU qsort_r
check_cxx_source_compiles("
#define _GNU_SOURCE
#include <stdlib.h>
static int dcomp(const void *l, const void *r, void *t) { return 0; }
int main(int ac, char **av) {
  int arr[] = { 1 };
  void *t = NULL;
  qsort_r((void*)&arr[0], 1, 1, dcomp, t);
  return (0);
}" HAVE_GNU_QSORT_R)
if(HAVE_GNU_QSORT_R)
  add_compile_definitions(-DHAVE_GNU_QSORT_R)
else()
  check_cxx_source_compiles("
#include <stdlib.h>
static int dcomp(void *t, const void *l, const void *r) { return 0; }
int main(int ac, char **av) {
  int arr[] = { 1 };
  void *t = NULL;
  qsort_r((void*)&arr[0], 1, 1, t, dcomp);
  return (0);
}" HAVE_BSD_QSORT_R)
  if(HAVE_BSD_QSORT_R)
    add_compile_definitions(-DHAVE_BSD_QSORT_R)
  endif()
endif()

check_include_file("time.h"__time_h__)  # struct timespec check
check_struct_has_member("struct timespec" tv_sec "time.h" HAVE_STRUCT_TIMESPEC)
if(HAVE_STRUCT_TIMESPEC)
  add_compile_definitions(-DHAVE_STRUCT_TIMESPEC)
endif()

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_symbol_exists(program_invocation_name "errno.h" HAVE_PROGRAM_INVOCATION_NAME)
if(HAVE_PROGRAM_INVOCATION_NAME)
  add_compile_definitions(-DHAVE_PROGRAM_INVOCATION_NAME)
endif()
set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_symbol_exists(issignaling "math.h" HAVE_ISSIGNALING)
if(HAVE_ISSIGNALING)
  add_compile_definitions(-DHAVE_ISSIGNALING)
endif()

CHECK_SYMBOL_EXISTS(posix_memalign "stdlib.h" HAVE_POSIX_MEMALIGN)
if(HAVE_POSIX_MEMALIGN)
  add_compile_definitions(-DHAVE_POSIX_MEMALIGN)
endif()

check_struct_has_member("struct dirent" d_type "dirent.h" HAVE_DIRENT_D_TYPE)
if(HAVE_DIRENT_D_TYPE)
  add_compile_definitions(-DHAVE_DIRENT_D_TYPE)
endif()

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_c_source_compiles("
#define _GNU_SOURCE
#include <stdlib.h>
#include <locale.h>
#if defined(HAVE_XLOCALE_H)
#include <xlocale.h>
#endif
int main() {
  locale_t loc = newlocale(LC_CTYPE_MASK, \"C\", NULL);
  const char *s = \"1.0\";
  char *end;
  double d = strtod_l(s, &end, loc);
  float f = strtof_l(s, &end, loc);
  freelocale(loc);
  return 0;
}" HAVE_STRTOD_L)
if(HAVE_STRTOD_L)
  add_compile_definitions(-DHAVE_STRTOD_L)
endif()

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_c_source_compiles("
#include <errno.h>
#ifdef ETIME
int main(){ return 0; }
#else
#error no ETIME
#endif" __mesa_has_etime)
if(NOT __mesa_has_etime)
  add_compile_definitions(-DETIME=ETIMEDOUT)
endif()

# linker flags
set(CMAKE_REQUIRED_FLAGS "-Wl,--gc-sections")
check_c_source_compiles("int main() { return 0; }" HAVE_GC_SECTIONS)
set(CMAKE_REQUIRED_FLAGS "")
if(HAVE_GC_SECTIONS)
  add_compile_options(-ffunction-sections -fdata-sections)
  add_compile_definitions(-DHAVE_GC_SECTIONS)  # placeholder, not used
endif()
set(with_ld_version_script ON)
set(with_ld_dynamic_list ON)
set(ld_args_build_id -Wl,--build-id=sha1)
# ld_args_gc_sections / ld_args_bsymbolic (meson: cc.get_supported_link_arguments)
set(ld_args_gc_sections "")
if(HAVE_GC_SECTIONS)
  set(ld_args_gc_sections -Wl,--gc-sections)
endif()
set(ld_args_bsymbolic "")
set(CMAKE_REQUIRED_FLAGS "-Wl,-Bsymbolic")
check_c_source_compiles("int main() { return 0; }" HAVE_BSYMBOLIC)
set(CMAKE_REQUIRED_FLAGS "")
if(HAVE_BSYMBOLIC)
  set(ld_args_bsymbolic -Wl,-Bsymbolic)
endif()

# bool meson compile flags: no_override_init
set(CMAKE_REQUIRED_FLAGS "-Wno-override-init")
check_c_source_compiles("int main(){return 0;}" HAVE_NO_OVERRIDE_INIT)
set(CMAKE_REQUIRED_FLAGS "")
set(no_override_init_args "-Wno-override-init")

# dlopen/dladdr/dl_iterate_phdr
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  check_symbol_exists(dlopen "dlfcn.h" __mesa_dlopen)
  if(NOT __mesa_dlopen)
    set(MESA_DL_LIB "dl")
    link_libraries(dl)
  endif()
  check_symbol_exists(dladdr "dlfcn.h" HAVE_DLADDR)
  if(HAVE_DLADDR)
    add_compile_definitions(-DHAVE_DLADDR)
  endif()
endif()
check_symbol_exists(dl_iterate_phdr "link.h" HAVE_DL_ITERATE_PHDR)
if(HAVE_DL_ITERATE_PHDR)
  add_compile_definitions(-DHAVE_DL_ITERATE_PHDR)
endif()

# threads
find_package(Threads REQUIRED)
add_compile_definitions(-DHAVE_PTHREAD)
set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
check_c_source_compiles("
#define _GNU_SOURCE
#include <pthread.h>
int main(){ cpu_set_t c; return pthread_setaffinity_np(0, sizeof(c), &c); }" HAVE_PTHREAD_SETAFFINITY)
if(HAVE_PTHREAD_SETAFFINITY)
  add_compile_definitions(-DHAVE_PTHREAD_SETAFFINITY)
endif()
set(CMAKE_REQUIRED_LIBRARIES "")

# m, dl libs
find_library(M_LIB m)

# clock
check_symbol_exists(clock_gettime "time.h" __mesa_clock_gettime)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT __mesa_clock_gettime)
  find_library(RT_LIB rt)
endif()

message(STATUS "Mesa CMake: compiler checks complete")