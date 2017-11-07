/*-
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: mutex.h,v 2.7.2.35 2000/04/27 03:10:26 cp Exp $
 * $FreeBSD: head/sys/sys/mutex.h 324609 2017-10-13 20:31:56Z mjg $
 */

#ifndef _SYS_MUTEX_H_
#define _SYS_MUTEX_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

/*
 * Override 'curthread' for rmem to assume that the local function
 * contains 'tid' as an integer counter.  Shift this left by enough
 * bits to avoid a collision on the lock cookie.
 */
#define	curthread	((tid) << 16)

#ifdef _KERNEL
#include <sys/lock_profile.h>
#include <sys/lockstat.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

/*
 * Mutex types and options passed to mtx_init().  MTX_QUIET and MTX_DUPOK
 * can also be passed in.
 */
#define	MTX_DEF		0x00000000	/* DEFAULT (sleep) lock */ 
#define MTX_SPIN	0x00000001	/* Spin lock (disables interrupts) */
#define MTX_RECURSE	0x00000004	/* Option: lock allowed to recurse */
#define	MTX_NOWITNESS	0x00000008	/* Don't do any witness checking. */
#define MTX_NOPROFILE   0x00000020	/* Don't profile this lock */
#define	MTX_NEW		0x00000040	/* Don't check for double-init */

/*
 * Option flags passed to certain lock/unlock routines, through the use
 * of corresponding mtx_{lock,unlock}_flags() interface macros.
 */
#define	MTX_QUIET	LOP_QUIET	/* Don't log a mutex event */
#define	MTX_DUPOK	LOP_DUPOK	/* Don't log a duplicate acquire */

/*
 * State bits kept in mutex->mtx_lock, for the DEFAULT lock type. None of this,
 * with the exception of MTX_UNOWNED, applies to spin locks.
 */
#define	MTX_UNOWNED	0x00000000	/* Cookie for free mutex */
#define	MTX_RECURSED	0x00000001	/* lock recursed (for MTX_DEF only) */
#define	MTX_CONTESTED	0x00000002	/* lock contested (for MTX_DEF only) */
#define	MTX_DESTROYED	0x00000004	/* lock destroyed */
#define	MTX_FLAGMASK	(MTX_RECURSED | MTX_CONTESTED | MTX_DESTROYED)

/*
 * Prototypes
 *
 * NOTE: Functions prepended with `_' (underscore) are exported to other parts
 *	 of the kernel via macros, thus allowing us to use the cpp LOCK_FILE
 *	 and LOCK_LINE or for hiding the lock cookie crunching to the
 *	 consumers. These functions should not be called directly by any
 *	 code using the API. Their macros cover their functionality.
 *	 Functions with a `_' suffix are the entrypoint for the common
 *	 KPI covering both compat shims and fast path case.  These can be
 *	 used by consumers willing to pass options, file and line
 *	 informations, in an option-independent way.
 *
 * [See below for descriptions]
 *
 */
void	_mtx_init(volatile uintptr_t *c, const char *name, const char *type,
	    int opts);
void	_mtx_destroy(volatile uintptr_t *c);

#ifdef SMP
void	_mtx_lock_spin_cookie(volatile uintptr_t *c, uintptr_t v, uintptr_t tid,
	    int opts, const char *file, int line);
#endif

/*
 * Top-level macros to provide lock cookie once the actual mtx is passed.
 * They will also prevent passing a malformed object to the mtx KPI by
 * failing compilation as the mtx_lock reserved member will not be found.
 */
#define	mtx_init(m, n, t, o)						\
	_mtx_init(&(m)->mtx_lock, n, t, o)
#define	mtx_destroy(m)							\
	_mtx_destroy(&(m)->mtx_lock)
#ifdef SMP
#define	_mtx_lock_spin(m, v, t, o, f, l)				\
	_mtx_lock_spin_cookie(&(m)->mtx_lock, v, t, o, f, l)
#endif

#define	mtx_recurse	lock_object.lo_data

/* Very simple operations on mtx_lock. */

/* Try to obtain mtx_lock once. */
#define _mtx_obtain_lock(mp, tid)					\
	atomic_cmpset_acq_ptr(&(mp)->mtx_lock, MTX_UNOWNED, (tid))

#define _mtx_obtain_lock_fetch(mp, vp, tid)				\
	atomic_fcmpset_acq_ptr(&(mp)->mtx_lock, vp, (tid))

/* Try to release mtx_lock if it is unrecursed and uncontested. */
#define _mtx_release_lock(mp, tid)					\
	atomic_cmpset_rel_ptr(&(mp)->mtx_lock, (tid), MTX_UNOWNED)

/* Release mtx_lock quickly, assuming we own it. */
#define _mtx_release_lock_quick(mp)					\
	atomic_store_rel_ptr(&(mp)->mtx_lock, MTX_UNOWNED)

/*
 * Full lock operations that are suitable to be inlined in non-debug
 * kernels.  If the lock cannot be acquired or released trivially then
 * the work is deferred to another function.
 */

/*
 * Lock a spin mutex.  For spinlocks, we handle recursion inline (it
 * turns out that function calls can be significantly expensive on
 * some architectures).  Since spin locks are not _too_ common,
 * inlining this code is not too big a deal.
 */
#ifdef SMP
#define __mtx_lock_spin(mp, tid, opts, file, line) do {			\
	uintptr_t _tid = (uintptr_t)(tid);				\
	uintptr_t _v = MTX_UNOWNED;					\
									\
	spinlock_enter();						\
	if (!_mtx_obtain_lock_fetch((mp), &_v, _tid)) 			\
		_mtx_lock_spin((mp), _v, _tid, (opts), (file), (line)); \
	else 								\
		LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(spin__acquire,	\
		    mp, 0, 0, file, line);				\
} while (0)
#else /* SMP */
#define __mtx_lock_spin(mp, tid, opts, file, line) do {			\
	uintptr_t _tid = (uintptr_t)(tid);				\
									\
	spinlock_enter();						\
	if ((mp)->mtx_lock == _tid)					\
		(mp)->mtx_recurse++;					\
	else {								\
		KASSERT((mp)->mtx_lock == MTX_UNOWNED, ("corrupt spinlock")); \
		(mp)->mtx_lock = _tid;					\
	}								\
} while (0)
#endif /* SMP */

/*
 * Unlock a spin mutex.  For spinlocks, we can handle everything
 * inline, as it's pretty simple and a function call would be too
 * expensive (at least on some architectures).  Since spin locks are
 * not _too_ common, inlining this code is not too big a deal.
 *
 * Since we always perform a spinlock_enter() when attempting to acquire a
 * spin lock, we need to always perform a matching spinlock_exit() when
 * releasing a spin lock.  This includes the recursion cases.
 */
#ifdef SMP
#define __mtx_unlock_spin(mp) do {					\
	if (mtx_recursed((mp)))						\
		(mp)->mtx_recurse--;					\
	else {								\
		LOCKSTAT_PROFILE_RELEASE_LOCK(spin__release, mp);	\
		_mtx_release_lock_quick((mp));				\
	}								\
	spinlock_exit();						\
} while (0)
#else /* SMP */
#define __mtx_unlock_spin(mp) do {					\
	if (mtx_recursed((mp)))						\
		(mp)->mtx_recurse--;					\
	else {								\
		LOCKSTAT_PROFILE_RELEASE_LOCK(spin__release, mp);	\
		(mp)->mtx_lock = MTX_UNOWNED;				\
	}								\
	spinlock_exit();						\
} while (0)
#endif /* SMP */

/*
 * Exported lock manipulation interface.
 *
 * mtx_lock_spin(m) locks MTX_SPIN mutex `m'
 *
 * mtx_unlock_spin(m) unlocks MTX_SPIN mutex `m'
 *
 * mtx_lock_spin_flags(m, opts) and mtx_lock_flags(m, opts) locks mutex `m'
 *     and passes option flags `opts' to the "hard" function, if required.
 *     With these routines, it is possible to pass flags such as MTX_QUIET
 *     to the appropriate lock manipulation routines.
 *
 * mtx_initialized(m) returns non-zero if the lock `m' has been initialized.
 *
 * mtx_owned(m) returns non-zero if the current thread owns the lock `m'
 *
 * mtx_recursed(m) returns non-zero if the lock `m' is presently recursed.
 */ 
#define mtx_lock_spin(m)	mtx_lock_spin_flags((m), 0)
#define mtx_unlock_spin(m)	mtx_unlock_spin_flags((m), 0)

#if defined(MUTEX_NOINLINE)
#define	mtx_lock_spin_flags_(m, opts, file, line)			\
	_mtx_lock_spin_flags((m), (opts), (file), (line))
#define	mtx_unlock_spin_flags_(m, opts, file, line)			\
	_mtx_unlock_spin_flags((m), (opts), (file), (line))
#else	/* LOCK_DEBUG == 0 && !MUTEX_NOINLINE */
#define	mtx_lock_spin_flags_(m, opts, file, line)			\
	__mtx_lock_spin((m), curthread, (opts), (file), (line))
#define	mtx_unlock_spin_flags_(m, opts, file, line)			\
	__mtx_unlock_spin((m))
#endif	/* LOCK_DEBUG > 0 || MUTEX_NOINLINE */

#define	mtx_lock_spin_flags(m, opts)					\
	mtx_lock_spin_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define	mtx_unlock_spin_flags(m, opts)					\
	mtx_unlock_spin_flags_((m), (opts), LOCK_FILE, LOCK_LINE)

#define	MTX_READ_VALUE(m)	((m)->mtx_lock)

#define lv_mtx_owner(v)	((struct thread *)((v) & ~MTX_FLAGMASK))

#define mtx_owner(m)	lv_mtx_owner(MTX_READ_VALUE(m))

#define mtx_owned(m)	(mtx_owner(m) == curthread)

#define mtx_recursed(m)	((m)->mtx_recurse != 0)

#define mtx_name(m)	((m)->lock_object.lo_name)

#endif	/* _KERNEL */
#endif	/* _SYS_MUTEX_H_ */
