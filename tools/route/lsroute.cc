/*
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "route_info.hh"
#include <errno.h>
#include "stdio.h"
#include <string.h>

int
main(int argc, char *argv[])
{
    bool inet6 = false;

    printf("\n");
    printf("Internet\n");
    printf("%-20s%-20sFlags  Netif\n", "Destination", "Gateway");
    int width = 20;
    bool ipv4 = true;
    osv::route_fun print_fun = [&inet6, ipv4, width](const osv::route_info& route) {
        if (ipv4 ^  route.ipv6) {
            printf("%-*s ", width - 1, route.destination.c_str());
            printf("%-*s ", width - 1, route.gateway.c_str());
            printf(" %s",route.flags.c_str());
            printf(" %s\n",route.netif.c_str());
        }

        if (route.ipv6) {
            inet6 = true;
        }
        return true;
    };

    int res = foreach_route(print_fun);

    if (res != 0) {
        fprintf(stderr, "lsroute: osv_sysctl: estimate: %s\n",
                strerror(res));
        return (1);
    }
    ipv4 = false;
    if (inet6)
    {
        printf("\n");
        printf("Internet6\n");
        printf("%-20s%-20sFlags  Netif\n", "Destination", "Gateway");
        foreach_route(print_fun);
    }
    return 0;
}
