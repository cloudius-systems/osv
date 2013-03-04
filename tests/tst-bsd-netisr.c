#include <stdio.h>
#include <unistd.h>

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/net/netisr.h>

static void
arpintr(struct mbuf* m)
{
    char *name = (char*)m;
    printf("arpintr() - %s\n", name);
}

static const struct netisr_handler arp_nh = {
    .nh_name = "arp",
    .nh_handler = arpintr,
    .nh_proto = NETISR_ARP,
    .nh_policy = NETISR_POLICY_SOURCE,
};

/*
 * Caution: Running this test will disable networking.
 * Disable this test after you're satisfied with the results.
 *
 * Netisrs can be dispatched directly, or they can be deferred.
 *
 * The default in freebsd is NETISR_DISPATCH_DIRECT.
 * Which means to basically call the interrupt directly (in OSv this is
 * always possible, as the per-cpu structured were trimmed and only a single
 * work_stream is supported for the time being.
 *
 * If you wish to test deferred dispatch, set NETISR_DISPATCH_POLICY_DEFAULT
 * to NETISR_DISPATCH_DEFERRED in netisr.c
 */
int main(void)
{
    int run_test=0;

    if (run_test) {
        printf("BSD NetISR Test\n");

        netisr_register(&arp_nh);
        netisr_dispatch(NETISR_ARP, (struct mbuf*)"A");
        netisr_dispatch(NETISR_ARP, (struct mbuf*)"B");
        netisr_dispatch(NETISR_ARP, (struct mbuf*)"C");

        sleep(1);

        printf("BSD NetISR Test Done.\n");
    }
    return (0);
}
