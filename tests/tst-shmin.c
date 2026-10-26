// Minimal MAP_SHARED-anon fork coherence probe + a MAP_PRIVATE-anon isolation
// probe.  Prints the exact physical-visibility outcome for each.
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main() {
    printf("=== tst-shmin ===\n");
    errno=0; pid_t probe=fork();
    if (probe==-1 && errno==ENOSYS){ printf("SKIP no fork\n"); return 0; }
    if (probe==0) _exit(0);
    waitpid(probe,0,0);

    volatile int *sh=(volatile int*)mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    volatile int *pv=(volatile int*)mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    sh[0]=0xAAAA; pv[0]=0x1111;
    __sync_synchronize();

    pid_t pid=fork();
    if (pid==0){
        sh[0]=0xBBBB;           // SHARED: must reach parent
        pv[0]=0x2222;           // PRIVATE: must NOT reach parent
        __sync_synchronize();
        int ok = (sh[0]==0xBBBB && pv[0]==0x2222);
        _exit(ok?0:1);
    }
    int st=0; waitpid(pid,&st,0);
    __sync_synchronize();
    printf("child exit=%d (want 0: child saw own writes)\n", WEXITSTATUS(st));
    printf("SHARED parent sees sh[0]=0x%X (want 0xBBBB)\n", sh[0]);
    printf("PRIVATE parent sees pv[0]=0x%X (want 0x1111)\n", pv[0]);
    int fail=0;
    if (sh[0]!=0xBBBB){ printf("FAIL shared not visible\n"); fail++; }
    else printf("PASS shared visible\n");
    if (pv[0]!=0x1111){ printf("FAIL private leaked\n"); fail++; }
    else printf("PASS private isolated\n");
    printf("=== tst-shmin done: %d fail ===\n", fail);
    return fail?1:0;
}
