#ifndef PLATFORM_H_
#define PLATFORM_H_

/*
 * Structure packing
 */
/**
 * Some preprocessor magic to allow for a header file abstraction of
 * interrupt service routine declarations for the IAR compiler.  This
 * requires the use of the C99 _Pragma() directive (rather than the
 * old #pragma one that could not be used as a macro replacement), as
 * well as two different levels of preprocessor concetanations in
 * order to do both, assign the correct interrupt vector name, as well
 * as construct a unique function name for the ISR.
 *
 * Do *NOT* try to reorder the macros below, or you'll suddenly find
 * out about all kinds of IAR bugs...
 */
#define PRAGMA(x) _Pragma(#x)

// \cond
#if defined (__IAR_SYSTEMS_ICC__) || defined(__ICCAVR__) || defined(__ICCARM__) || defined(__ICCAVR32__)
#ifndef BEGIN_PACK
    #define BEGIN_PACK PRAGMA(pack(push, 1))
#endif
#ifndef END_PACK
    #define END_PACK   PRAGMA(pack(pop))
#endif
#ifndef PACK
    #define PACK
#endif
#ifndef INLINE
    #define INLINE static inline
#endif

#elif defined(__GNUC__)
#ifndef BEGIN_PACK
    #define BEGIN_PACK
#endif
#ifndef END_PACK
  #define END_PACK
#endif
#ifndef PACK
    #define PACK __attribute__ ((packed))
#endif
#ifndef INLINE
    #define INLINE inline
#endif
#else
#error unsupported compiler
#endif

/*
 * Atomic section handling
 */
#define atomic_enter()		{
#define atomic_leave()      }

/*
 * Helper macros
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)            (sizeof(x) / sizeof((x)[0]))
#endif
#ifndef MIN
#define MIN(v1,v2) ((v1) > (v2) ? (v2) : (v1))
#endif
#ifndef MAX
#define MAX(v1,v2) ((v1) < (v2) ? (v2) : (v1))
#endif
// define the access to the idle watchdog handler
#define SYSTEM_IDLE_WORKING()    main_set_idle_working();
void main_set_idle_working(void);

/*
 * Debug output handling
 */
#define DSTR(s)				s"\n"
//#define PSTR(s)            s
#define dbg_printf_p(x)		printf x

#define dbg_verbose(text) fprintf(stderr, \
        "%s:%d: %s\n", __FILE__, __LINE__, text)


#endif /* PLATFORM_H_ */
