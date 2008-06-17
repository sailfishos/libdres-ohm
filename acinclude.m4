AC_DEFUN([AC_CHECK_CCOPT_VISIBILITY],
    [AC_CACHE_CHECK([whether ${CC-cc} supports -fvisibility=hidden],
         [ac_cv_cc_visibility],
         [echo 'void f(){}' > conftest.c
          if test -z "`${CC-cc} -c -fvisibility=hidden conftest.c 2>&1`"; then
            ac_cv_cc_visibility=yes
          else
            ac_cv_cc_visibility=no
          fi])
     if test $ac_cv_cc_visibility = yes; then
       AC_DEFINE([HAVE_VISIBILITY_SUPPORT], 1,
                 [Define to 1 if your compiler supports -fvisibility=hidden.])
       CCOPT_VISIBILITY_HIDDEN="-fvisibility=hidden"
     else
       AC_DEFINE([HAVE_VISIBILITY_SUPPORT], [],
                 [Define to 1 if your compiler supports -fvisibility=hidden.])
       CCOPT_VISIBILITY_HIDDEN=""
     fi
    ])
