#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <bsd/porting/callout.h>
#include <bsd/porting/netport.h>

struct callout c1, c2;
int ctr;

void aaa(void *unused)
{
    callout_reset(&c1, hz/100, aaa, NULL);

    ctr++;
    printf("TICK %d\n", ctr);
    struct timeval t;
    getmicrotime(&t);
    printf("sec=%d msec=%d\n", t.tv_sec, t.tv_usec/1000);
}

void bbb(void *unused)
{
    // Stop aaa
    printf("SHUT-UP\n");
    callout_stop(&c1);
}

void test1(void)
{
    printf("BSD Callout Test\n");

    ctr = 0;

    callout_init(&c1, 1);
    callout_reset(&c1, hz/100, aaa, NULL);

    callout_init(&c2, 1);
    callout_reset(&c2, 10*hz, bbb, NULL);

    sleep(11);
    printf("BSD Callout Test Done\n");
}

int main(int argc, char **argv)
{
    test1();
    return 0;
}
