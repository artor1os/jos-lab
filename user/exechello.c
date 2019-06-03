#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int r;
	cprintf("i am parent environment %08x\n", thisenv->env_id);
    if (fork() == 0) {
        if ((r = execl("hello", "hello", 0)) < 0)
            panic("exec(hello) failed: %e", r);
        assert(0); // no return
    }
    cprintf("hello again from %08x\n", thisenv->env_id);
}
