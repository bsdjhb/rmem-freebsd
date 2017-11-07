/*-
 * Copyright (c) 1998 Berkeley Software Design, Inc. All rights reserved.
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
 *	from BSDI $Id: mutex_witness.c,v 1.1.2.20 2000/04/27 03:10:27 cp Exp $
 *	and BSDI $Id: synch_machdep.c,v 2.3.2.39 2000/04/27 03:10:25 cp Exp $
 */

/*
 * Machine independent bits of mutex implementation.
 */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: head/sys/kern/kern_mutex.c 324613 2017-10-14 00:47:30Z mjg $");

#include "opt_adaptive_mutexes.h"
#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"
#include "opt_sched.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#if 0
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#endif
#include <sys/ktr.h>
#include <sys/lock.h>
#if 0
#include <sys/malloc.h>
#endif
#include <sys/mutex.h>
#if 0
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/turnstile.h>
#include <sys/vmmeter.h>
#endif
#include <sys/lock_profile.h>

#include <machine/atomic.h>
#if 0
#include <machine/bus.h>
#endif
#include <machine/cpu.h>

#if 0
#include <ddb/ddb.h>

#include <fs/devfs/devfs_int.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#endif

/*
 * Return the mutex address when the lock cookie address is provided.
 * This functionality assumes that struct mtx* have a member named mtx_lock.
 */
#define	mtxlock2mtx(c)	(__containerof(c, struct mtx, mtx_lock))

/*
 * Internal utility macros.
 */
#define mtx_unowned(m)	((m)->mtx_lock == MTX_UNOWNED)

#define	mtx_destroyed(m) ((m)->mtx_lock == MTX_DESTROYED)

#ifdef SMP
/*
 * _mtx_lock_spin_cookie: the tougher part of acquiring an MTX_SPIN lock.
 *
 * This is only called if we need to actually spin for the lock. Recursion
 * is handled inline.
 */
void
_mtx_lock_spin_cookie(volatile uintptr_t *c, uintptr_t v, uintptr_t tid,
    int opts, const char *file, int line)
{
	struct mtx *m;
#ifdef LOCK_PROFILING
	int contested = 0;
	uint64_t waittime = 0;
#endif
#ifdef KDTRACE_HOOKS
	int64_t spin_time = 0;
#endif
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	int doing_lockprof;
#endif

	m = mtxlock2mtx(c);

	if (__predict_false(v == MTX_UNOWNED))
		v = MTX_READ_VALUE(m);

	if (__predict_false(v == tid)) {
		m->mtx_recurse++;
		return;
	}

	if (LOCK_LOG_TEST(&m->lock_object, opts))
		CTR1(KTR_LOCK, "_mtx_lock_spin: %p spinning", m);
	KTR_STATE1(KTR_SCHED, "thread", sched_tdname((struct thread *)tid),
	    "spinning", "lockname:\"%s\"", m->lock_object.lo_name);

#ifdef HWPMC_HOOKS
	PMC_SOFT_CALL( , , lock, failed);
#endif
	lock_profile_obtain_lock_failed(&m->lock_object, &contested, &waittime);
#ifdef LOCK_PROFILING
	doing_lockprof = 1;
#elif defined(KDTRACE_HOOKS)
	doing_lockprof = lockstat_enabled;
	if (__predict_false(doing_lockprof))
		spin_time -= lockstat_nsecs(&m->lock_object);
#endif
	for (;;) {
		if (v == MTX_UNOWNED) {
			if (_mtx_obtain_lock_fetch(m, &v, tid))
				break;
			continue;
		}
		/* Give interrupts a chance while we spin. */
		spinlock_exit();
		do {
#if 0
			if (lda.spin_cnt < 10000000) {
				lock_delay(&lda);
			} else {
				lda.spin_cnt++;
				if (lda.spin_cnt < 60000000 || kdb_active ||
				    panicstr != NULL) {
					DELAY(1);

					/*
					 * Every so often try an
					 * atomic op instead of a
					 * simple read since that may
					 * "force" a pending write out
					 * of a store buffer.
					 */
					if (lda.spin_cnt % 1000 == 0)
						break;
				} else
					_mtx_lock_spin_failed(m);
				cpu_spinwait();
			}
#else
			cpu_spinwait();
#endif
			v = MTX_READ_VALUE(m);
		} while (v != MTX_UNOWNED);
		spinlock_enter();
	}

	if (LOCK_LOG_TEST(&m->lock_object, opts))
		CTR1(KTR_LOCK, "_mtx_lock_spin: %p spin done", m);
	KTR_STATE0(KTR_SCHED, "thread", sched_tdname((struct thread *)tid),
	    "running");

#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	if (__predict_true(!doing_lockprof))
		return;
#endif
#ifdef KDTRACE_HOOKS
	spin_time += lockstat_nsecs(&m->lock_object);
#endif
	LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(spin__acquire, m,
	    contested, waittime, file, line);
#ifdef KDTRACE_HOOKS
	if (lda.spin_cnt != 0)
		LOCKSTAT_RECORD1(spin__spin, m, spin_time);
#endif
}
#endif /* SMP */

/*
 * All the unlocking of MTX_SPIN locks is done inline.
 * See the __mtx_unlock_spin() macro for the details.
 */

/*
 * Mutex initialization routine; initialize lock `m' of type contained in
 * `opts' with options contained in `opts' and name `name.'  The optional
 * lock type `type' is used as a general lock category name for use with
 * witness.
 */
void
_mtx_init(volatile uintptr_t *c, const char *name, const char *type, int opts)
{
	struct mtx *m;
#if 0
	struct lock_class *class;
	int flags;
#endif

	m = mtxlock2mtx(c);

	MPASS((opts & ~(MTX_SPIN | MTX_QUIET | MTX_RECURSE |
	    MTX_NOWITNESS | MTX_DUPOK | MTX_NOPROFILE | MTX_NEW)) == 0);
	ASSERT_ATOMIC_LOAD_PTR(m->mtx_lock,
	    ("%s: mtx_lock not aligned for %s: %p", __func__, name,
	    &m->mtx_lock));

#if 0
	/* Determine lock class and lock flags. */
	if (opts & MTX_SPIN)
		class = &lock_class_mtx_spin;
	else
		class = &lock_class_mtx_sleep;
	flags = 0;
	if (opts & MTX_QUIET)
		flags |= LO_QUIET;
	if (opts & MTX_RECURSE)
		flags |= LO_RECURSABLE;
	if ((opts & MTX_NOWITNESS) == 0)
		flags |= LO_WITNESS;
	if (opts & MTX_DUPOK)
		flags |= LO_DUPOK;
	if (opts & MTX_NOPROFILE)
		flags |= LO_NOPROFILE;
	if (opts & MTX_NEW)
		flags |= LO_NEW;

	/* Initialize mutex. */
	lock_init(&m->lock_object, class, name, type, flags);
#endif

	m->mtx_lock = MTX_UNOWNED;
	m->mtx_recurse = 0;
}

#if 0
/*
 * Remove lock `m' from all_mtx queue.  We don't allow MTX_QUIET to be
 * passed in as a flag here because if the corresponding mtx_init() was
 * called with MTX_QUIET set, then it will already be set in the mutex's
 * flags.
 */
void
_mtx_destroy(volatile uintptr_t *c)
{
	struct mtx *m;

	m = mtxlock2mtx(c);

	if (!mtx_owned(m))
		MPASS(mtx_unowned(m));
	else {
		MPASS((m->mtx_lock & (MTX_RECURSED|MTX_CONTESTED)) == 0);

		/* Perform the non-mtx related part of mtx_unlock_spin(). */
		if (LOCK_CLASS(&m->lock_object) == &lock_class_mtx_spin)
			spinlock_exit();
		else
			TD_LOCKS_DEC(curthread);

		lock_profile_release_lock(&m->lock_object);
		/* Tell witness this isn't locked to make it happy. */
		WITNESS_UNLOCK(&m->lock_object, LOP_EXCLUSIVE, __FILE__,
		    __LINE__);
	}

	m->mtx_lock = MTX_DESTROYED;
	lock_destroy(&m->lock_object);
}
#endif
