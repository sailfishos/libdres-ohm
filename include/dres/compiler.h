/******************************************************************************/
/*  This file is part of dres the resource policy dependency resolver.        */
/*                                                                            */
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  This library is free software; you can redistribute                       */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

#ifndef __DRES_COMPILER_H__
#define __DRES_COMPILER_H__

/*
 * Notes:
 *
 *   This file defines compiler-specific macros for a few commonly used
 *   things that are not in the scope of the C99 and are thus not standardized.
 *   Currently these are
 *
 *   1) Visibility constraints: macros for controlling the scope of visibility
 *      of symbols with non-static scope
 *
 *      It is often comes handy, especially for libraries, to declare variables
 *      and functions with non-static scope (apart from the public API provided
 *      by the library) so that they can be accessed from all the modules of
 *      the library. However, these symbols should not appear in the global
 *      symbol table as they are not part of the public API and are not
 *      supposed to be accessed outside the library.
 *
 *      Here is the typical way of using these macros:
 *
 *        - Do not export global symbols by default. This is accomplished
 *          either by putting the NOEXPORT_BY_DEFAULT in the beginning
 *          of every module or a commonly included header or by instructing your
 *          compiler/linker to do so (eg. with -fvisibility=hidden for GCC)
 *
 *        - Mark symbols to be exported by the EXPORTED macro.
 *
 *      Depending on the ratio of exported vs. all non-static variables it
 *      might make more sense to do it the other way around, ie. export by
 *      default and mark the ones you do not want to expose with NOEXPORT.
 *
 *
 *   2) Branch-prediction hints: macros for asking the compiler to generate
 *      code with controlled branch-prediction characteristics.
 *
 *      You tell the compiler which side of the branch you want to optimize for
 *      by telling whether the branch condition is more likely to be true or
 *      false. These macros are very similar to the ones used by the linux
 *      kernel.
 */


#include <dres/config.h>                   /* HAVE_VISIBILITY_SUPPORT */


#ifdef __GNUC__

#  ifdef HAVE_VISIBILITY_SUPPORT
#    define NOEXPORT_BY_DEFAULT _Pragma("GCC visibility push(hidden)")
#    define EXPORTED_BY_DEFAULT _Pragma("GCC visibility push(default)")

#    define NOEXPORT __attribute__ ((visibility("hidden")))
#    define EXPORTED __attribute__ ((visibility("default")))
#  endif /* HAVE_VISIBILITY_SUPPORT */

#  ifndef unlikely
#    define unlikely(cond) __builtin_expect(cond, 0)
#  endif
#  ifndef likely
#    define likely(cond) __builtin_expect(cond, 1)
#  endif

#  ifndef UNUSED
#    define UNUSED __attribute__ ((unused))
#  endif

#else /* !__GNUC__ */

#  warning "Compiler specific macros are not defined for your compiler."

#  warning "Visibility constraint macros not defined for your compiler."
#  define NOEXPORT_BY_DEFAULT
#  define EXPORTED_BY_DEFAULT
#  define NOEXPORT
#  define EXPORTED

#  warning "Branch-prediction hint macros not defined for your compiler."
#  define unlikely(cond) (cond)
#  define likely(cond)   (cond)

#  define UNUSED
#endif /* !__GNUC__ */


#endif /* __DRES_COMPILER_H__ */
