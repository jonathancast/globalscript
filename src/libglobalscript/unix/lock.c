/* §source.file Copy of Plan 9's §fn{lock.c}, here so we can use these routines on Unix too */

#include <u.h>
#define NO_IO_ROUTINES 1
#include <libglobalscript.h>

extern int	_tas(int*);

void
lock(Lock *lk)
{
	int i;

	/* once fast */
	if(!_tas(&lk->val))
		return;
	/* a thousand times pretty fast */
	for(i=0; i<1000; i++){
		if(!_tas(&lk->val))
			return;
		sleep(0);
	}
	/* now nice and slow */
	for(i=0; i<1000; i++){
		if(!_tas(&lk->val))
			return;
		sleep(100);
	}
	/* take your time */
	while(_tas(&lk->val))
		sleep(1000);
}

void
unlock(Lock *lk)
{
	lk->val = 0;
}
