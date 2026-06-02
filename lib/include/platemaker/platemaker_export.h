
#ifndef PLATEMAKER_EXPORT_H
#define PLATEMAKER_EXPORT_H

#ifdef PLATEMAKER_STATIC_DEFINE
#  define PLATEMAKER_EXPORT
#  define PLATEMAKER_NO_EXPORT
#else
#  ifndef PLATEMAKER_EXPORT
#    ifdef platemaker_lib_EXPORTS
        /* We are building this library */
#      define PLATEMAKER_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define PLATEMAKER_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef PLATEMAKER_NO_EXPORT
#    define PLATEMAKER_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef PLATEMAKER_DEPRECATED
#  define PLATEMAKER_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef PLATEMAKER_DEPRECATED_EXPORT
#  define PLATEMAKER_DEPRECATED_EXPORT PLATEMAKER_EXPORT PLATEMAKER_DEPRECATED
#endif

#ifndef PLATEMAKER_DEPRECATED_NO_EXPORT
#  define PLATEMAKER_DEPRECATED_NO_EXPORT PLATEMAKER_NO_EXPORT PLATEMAKER_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef PLATEMAKER_NO_DEPRECATED
#    define PLATEMAKER_NO_DEPRECATED
#  endif
#endif

#endif /* PLATEMAKER_EXPORT_H */
