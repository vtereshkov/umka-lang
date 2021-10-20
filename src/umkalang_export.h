
#ifndef UMKALANG_EXPORT_H
#define UMKALANG_EXPORT_H

#ifdef UMKALANG_STATIC_DEFINE
#  define UMKALANG_EXPORT
#  define UMKALANG_NO_EXPORT
#else
#  ifndef UMKALANG_EXPORT
#    ifdef umkalang_EXPORTS
        /* We are building this library */
#      define UMKALANG_EXPORT 
#    else
        /* We are using this library */
#      define UMKALANG_EXPORT 
#    endif
#  endif

#  ifndef UMKALANG_NO_EXPORT
#    define UMKALANG_NO_EXPORT 
#  endif
#endif

#ifndef UMKALANG_DEPRECATED
#  define UMKALANG_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef UMKALANG_DEPRECATED_EXPORT
#  define UMKALANG_DEPRECATED_EXPORT UMKALANG_EXPORT UMKALANG_DEPRECATED
#endif

#ifndef UMKALANG_DEPRECATED_NO_EXPORT
#  define UMKALANG_DEPRECATED_NO_EXPORT UMKALANG_NO_EXPORT UMKALANG_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef UMKALANG_NO_DEPRECATED
#    define UMKALANG_NO_DEPRECATED
#  endif
#endif

#endif /* UMKALANG_EXPORT_H */
