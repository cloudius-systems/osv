#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <osv/debug.h>
#include <bsd/porting/callout.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>

#define tdbg(...) tprintf_d("tst-bsd-callout", __VA_ARGS__)

struct callout c1, c2;
int ctr;

void aaa(void *unused)
{
    callout_reset(&c1, hz/100, aaa, NULL);

    ctr++;
    tdbg("TICK %d\n", ctr);
    struct timeval t;
    getmicrotime(&t);
    tdbg("sec=%d msec=%d\n", t.tv_sec, t.tv_usec/1000);
}

void bbb(void *unused)
{
    // Stop aaa
    tdbg("SHUT-UP\n");
    callout_stop(&c1);
}

void test1(void)
{
    tdbg("BSD Callout Test1 - BEGIN\n");

    ctr = 0;

    callout_init(&c1, 1);
    callout_reset(&c1, hz/100, aaa, NULL);

    callout_init(&c2, 1);
    callout_reset(&c2, 10*hz, bbb, NULL);

    sleep(11);
    tdbg("BSD Callout Test1 - END\n");
}

/********************** Test 2 **********************/

/* Test a callout with mutex */
int test2_ctr_a=0;
int test2_ctr_b=0;
int test2_ctr_shared=0;
struct mtx t2_lock;
struct callout t2a, t2b;
struct callout t2a_mtx, t2b_mtx;

void t2_reset(void)
{
    test2_ctr_a=0;
    test2_ctr_b=0;
    test2_ctr_shared=0;
}

void t2_print(void)
{
    tdbg("test2_ctr_a=%d\n", test2_ctr_a);
    tdbg("test2_ctr_b=%d\n", test2_ctr_b);
    tdbg("test2_ctr_(a+b)=%d\n", test2_ctr_a + test2_ctr_b);
    tdbg("test2_ctr_shared=%d\n", test2_ctr_shared);
}

void t2_a(void *unused)
{
    test2_ctr_a++;
    test2_ctr_shared++;

    callout_reset(&t2a, hz/4000, t2_a, NULL);
}

void t2_b(void *unused)
{
    test2_ctr_b++;
    test2_ctr_shared++;

    callout_reset(&t2b, hz/4000, t2_b, NULL);
}

void t2_a2(void *unused)
{
    test2_ctr_a++;
    test2_ctr_shared++;

    callout_reset(&t2a_mtx, hz/4000, t2_a2, NULL);
}

void t2_b2(void *unused)
{
    test2_ctr_b++;
    test2_ctr_shared++;

    callout_reset(&t2b_mtx, hz/4000, t2_b2, NULL);
}

/*
 * Run 2 callouts that increases 2 counters each. one shared and one private.
 * If the accesses to the counter are synchronized the coutner will have a
 * coherent value with the sum of the private counters
 */
void test2(void)
{
    tdbg("BSD Callout Test2 - BEGIN\n");
    mtx_init(&t2_lock, NULL, NULL, 0);
    t2_reset();

    /************
     * No Mutex *
     ************/

    callout_init(&t2a, 1);
    callout_reset(&t2a, hz/4000, t2_a, NULL);

    callout_init(&t2b, 1);
    callout_reset(&t2b, hz/4000, t2_b, NULL);

    sleep(4);
    callout_drain(&t2a);
    callout_drain(&t2b);

    /* Run test without mutex (unsynchronized access) */
    tdbg("With No Mutex:\n");
    t2_print();

    /************
     * With Mutex *
     ************/

    t2_reset();
    callout_init_mtx(&t2a_mtx, &t2_lock, 1);
    callout_reset(&t2a_mtx, hz/4000, t2_a2, NULL);

    callout_init_mtx(&t2b_mtx, &t2_lock, 1);
    callout_reset(&t2b_mtx, hz/4000, t2_b2, NULL);

    sleep(4);
    callout_drain(&t2a_mtx);
    callout_drain(&t2b_mtx);

    /* Run test without mutex (unsynchronized access) */
    tdbg("With Mutex:\n");
    t2_print();

    tdbg("BSD Callout Test2 - END\n");
}

int main(int argc, char **argv)
{
    test1();
    test2();
    return 0;
}
