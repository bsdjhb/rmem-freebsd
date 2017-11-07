#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <thread_start_aarch64.h>

struct mtx m;
int x;

void
thread1(int tid)
{
	mtx_lock_spin(&m);
	x = x + 1;
	mtx_unlock_spin(&m);
}

void
thread2(int tid)
{
	mtx_lock_spin(&m);
	x = x - 1;
	mtx_unlock_spin(&m);
}

int
main()
{
	int tid1, tid2;

	mtx_init(&m, "test", NULL, MTX_SPIN);
	x = 4;
	tid1 = thread_start(thread1);
	tid2 = thread_start(thread2);
	return 1;
}
