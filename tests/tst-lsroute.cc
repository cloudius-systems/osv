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

#include <sys/param.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/net/if.h>
#include <bsd/include/arpa/inet.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/netinet/in_systm.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet6/in6.h>
#include <bsd/porting/route.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <termios.h>
#include <unistd.h>

extern "C" {
# define NI_NUMERICHOST	1
extern int getnameinfo (const struct sockaddr *__restrict __sa,
			socklen_t __salen, char *__restrict __host,
			socklen_t __hostlen, char *__restrict __serv,
			socklen_t __servlen, int __flags);
}
    

#define ROUND_UP(n, m)                  ((((n) + (m - 1)) / (m)) * (m))


static int
mask42bits(struct in_addr mask)
{
  u_int32_t msk = ntohl(mask.s_addr);
  u_int32_t tst;
  int ret;

  for (ret = 32, tst = 1; tst; ret--, tst <<= 1)
    if (msk & tst)
      break;

  for (tst <<= 1; tst; tst <<= 1)
    if (!(msk & tst))
      break;
  return tst ? -1 : ret;
}

static int
mask62bits(const struct in6_addr *mask)
{
  const u_char masks[] = { 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe };
  const u_char *c, *p, *end;
  int masklen, m;

  p = (const u_char *)mask;
  for (masklen = 0, end = p + 16; p < end && *p == 0xff; p++)
    masklen += 8;

  if (p < end) {
    for (c = masks, m = 0; c < masks + sizeof masks; c++, m++)
      if (*c == *p) {
        masklen += m;
        break;
      }
  }

  return masklen;
}
#define	NCP_ASCIIBUFFERSIZE	52

struct ncpaddr {
  sa_family_t ncpaddr_family;
  union {
    struct in_addr ip4addr;
#ifndef NOINET6
    struct in6_addr ip6addr;
#endif
  } u;
};

struct ncprange{
  sa_family_t ncprange_family;
  union {
    struct {
      struct in_addr ipaddr;
      struct in_addr mask;
      int width;
    } ip4;
#ifndef NOINET6
    struct {
      struct in6_addr ipaddr;
      int width;
    } ip6;
#endif
  } u;
};

#define ncprange_ip4addr	u.ip4.ipaddr
#define ncprange_ip4mask	u.ip4.mask
#define ncprange_ip4width	u.ip4.width
#define ncpaddr_ip4addr		u.ip4addr
#ifndef NOINET6
#define ncprange_ip6addr	u.ip6.ipaddr
#define ncprange_ip6width	u.ip6.width
#define ncpaddr_ip6addr		u.ip6addr
#endif

int
ncprange_getaddr(const struct ncprange *range, struct ncpaddr *addr)
{
  switch (range->ncprange_family) {
  case AF_INET:
    addr->ncpaddr_family = AF_INET;
    addr->ncpaddr_ip4addr = range->ncprange_ip4addr;
    return 1;
#ifndef NOINET6
  case AF_INET6:
    addr->ncpaddr_family = AF_INET6;
    addr->ncpaddr_ip6addr =  range->ncprange_ip6addr;
    return 1;
#endif
  }

  return 0;
}

static char *
ncpaddr_ntowa(const struct ncpaddr *addr)
{
  static char res[NCP_ASCIIBUFFERSIZE];
#ifndef NOINET6
  struct sockaddr_in6 sin6;
#endif

  switch (addr->ncpaddr_family) {
  case AF_INET:
    snprintf(res, sizeof res, "%s", inet_ntoa(addr->ncpaddr_ip4addr));
    return res;

#ifndef NOINET6
  case AF_INET6:
    memset(&sin6, '\0', sizeof(sin6));
    sin6.sin6_len = sizeof(sin6);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = addr->ncpaddr_ip6addr;
#if 0
    adjust_linklocal(&sin6);
#endif
    if (getnameinfo((struct sockaddr *)&sin6, sizeof sin6, res, sizeof(res),
                    NULL, 0, NI_NUMERICHOST) != 0)
      break;

    return res;
#endif
  }

  snprintf(res, sizeof res, "<AF_UNSPEC>");
  return res;
}


void
ncprange_setsa(struct ncprange *range, const struct sockaddr *host,
               const struct sockaddr *mask)
{
  const struct sockaddr_in *host4 = (const struct sockaddr_in *)host;
  const struct sockaddr_in *mask4 = (const struct sockaddr_in *)mask;
#ifndef NOINET6
  const struct sockaddr_in6 *host6 = (const struct sockaddr_in6 *)host;
  const struct sockaddr_in6 *mask6 = (const struct sockaddr_in6 *)mask;
#endif

  switch (host->sa_family) {
  case AF_INET:
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = host4->sin_addr;
    if (host4->sin_addr.s_addr == INADDR_ANY) {
      range->ncprange_ip4mask.s_addr = INADDR_ANY;
      range->ncprange_ip4width = 0;
    } else if (mask4 && (mask4->sin_family == AF_INET ||
                         mask4->sin_family == 255)) {
      range->ncprange_ip4mask.s_addr = mask4->sin_addr.s_addr;
      range->ncprange_ip4width = mask42bits(mask4->sin_addr);
    } else {
      range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
      range->ncprange_ip4width = 32;
    }
    break;

#ifndef NOINET6
  case AF_INET6:
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = host6->sin6_addr;
    if (IN6_IS_ADDR_UNSPECIFIED(&host6->sin6_addr))
      range->ncprange_ip6width = 0;
    else
      range->ncprange_ip6width = mask6 ? mask62bits(&mask6->sin6_addr) : 128;
    break;
#endif

  default:
    range->ncprange_family = AF_UNSPEC;
  }
}

int
ncprange_isdefault(const struct ncprange *range)
{
  switch (range->ncprange_family) {
  case AF_INET:
    if (range->ncprange_ip4addr.s_addr == INADDR_ANY)
      return 1;
    break;

#ifndef NOINET6
  case AF_INET6:
    if (range->ncprange_ip6width == 0 &&
        IN6_IS_ADDR_UNSPECIFIED(&range->ncprange_ip6addr))
      return 1;
    break;
#endif
  }

  return 0;
}

const char *
ncprange_ntoa(const struct ncprange *range)
{
  char *res;
  struct ncpaddr addr;
  int len;

  if (!ncprange_getaddr(range, &addr))
    return "<AF_UNSPEC>";

  res = ncpaddr_ntowa(&addr);
  len = strlen(res);
  if (len >= NCP_ASCIIBUFFERSIZE - 1)
    return res;

  switch (range->ncprange_family) {
  case AF_INET:
    if (range->ncprange_ip4width == -1) {
      /* A non-contiguous mask */
      for (; len >= 3; res[len -= 2] = '\0')
        if (strcmp(res + len - 2, ".0"))
          break;
      snprintf(res + len, NCP_ASCIIBUFFERSIZE - len, "&0x%08lx",
               (unsigned long)ntohl(range->ncprange_ip4mask.s_addr));
    } else if (range->ncprange_ip4width < 32)
      snprintf(res + len, NCP_ASCIIBUFFERSIZE - len, "/%d",
               range->ncprange_ip4width);
    return res;

#ifndef NOINET6
  case AF_INET6:
    if (range->ncprange_ip6width != 128)
      snprintf(res + len, NCP_ASCIIBUFFERSIZE - len,
               "/%d", range->ncprange_ip6width);

    return res;
#endif
  }

  return "<AF_UNSPEC>";
}

const char *
NumStr(long val, char *buf, size_t sz)
{
  static char result[23];		/* handles 64 bit numbers */

  if (buf == NULL || sz == 0) {
    buf = result;
    sz = sizeof result;
  }
  snprintf(buf, sz, "<%ld>", val);
  return buf;
}

static struct bits {
  u_int32_t b_mask;
  char b_val;
} bits[] = {
  { RTF_UP, 'U' },
  { RTF_GATEWAY, 'G' },
  { RTF_HOST, 'H' },
  { RTF_REJECT, 'R' },
  { RTF_DYNAMIC, 'D' },
  { RTF_MODIFIED, 'M' },
  { RTF_DONE, 'd' },
#ifdef RTF_CLONING
  { RTF_CLONING, 'C' },
#endif
  { RTF_XRESOLVE, 'X' },
  { RTF_LLINFO, 'L' },
  { RTF_STATIC, 'S' },
  { RTF_PROTO1, '1' },
  { RTF_PROTO2, '2' },
  { RTF_BLACKHOLE, 'B' },
#ifdef RTF_WASCLONED
  { RTF_WASCLONED, 'W' },
#endif
#ifdef RTF_PRCLONING
  { RTF_PRCLONING, 'c' },
#endif
#ifdef RTF_PROTO3
  { RTF_PROTO3, '3' },
#endif
#ifdef RTF_BROADCAST
  { RTF_BROADCAST, 'b' },
#endif
  { 0, '\0' }
};

static void
p_flags(u_int32_t f, unsigned max)
{
  char name[33], *flags;
  register struct bits *p = bits;

  if (max > sizeof name - 1)
    max = sizeof name - 1;

  for (flags = name; p->b_mask && flags - name < (int)max; p++)
    if (p->b_mask & f)
      *flags++ = p->b_val;
  *flags = '\0';
  printf("%-*.*s", (int)max, (int)max, name);
}


static void
p_sockaddr(struct sockaddr *phost, struct sockaddr *pmask, int width)
{
    struct ncprange range;
    char buf[29];
    struct sockaddr_dl *dl = (struct sockaddr_dl *)phost;

    if (0)
    {
        char tmp[50];

        printf("Found the following sockaddr:\n");
        printf("  Family %d, len %d\n",
               (int)phost->sa_family, (int)phost->sa_len);
        inet_ntop(phost->sa_family, phost->sa_data, tmp, sizeof tmp);
        printf("  Addr %s\n", tmp);
        if (pmask)
        {
            inet_ntop(pmask->sa_family, pmask->sa_data, tmp, sizeof tmp);
            printf("  Mask %s\n", tmp);
        }
    }

    switch (phost->sa_family)
    {
    case AF_INET:
#ifndef NOINET6
    case AF_INET6:
#endif
        ncprange_setsa(&range, phost, pmask);
        if (ncprange_isdefault(&range))
            printf("%-*s ", width - 1, "default");
        else
            printf("%-*s ", width - 1, ncprange_ntoa(&range));
        return;

    case AF_LINK:
        if (dl->sdl_nlen)
            snprintf(buf, sizeof buf, "%.*s", dl->sdl_nlen, dl->sdl_data);
        else if (dl->sdl_alen)
        {
            if (dl->sdl_type == IFT_ETHER)
            {
                if (dl->sdl_alen < sizeof buf / 3)
                {
                    int f;
                    u_char *MAC;

                    MAC = (u_char *)dl->sdl_data + dl->sdl_nlen;
                    for (f = 0; f < dl->sdl_alen; f++)
                        sprintf(buf+f*3, "%02x:", MAC[f]);
                    buf[f*3-1] = '\0';
                }
                else
                    strcpy(buf, "??:??:??:??:??:??");
            }
            else
                sprintf(buf, "<IFT type %d>", dl->sdl_type);
        } else if (dl->sdl_slen)
            sprintf(buf, "<slen %d?>", dl->sdl_slen);
        else
            sprintf(buf, "link#%d", dl->sdl_index);
        break;

    default:
        sprintf(buf, "<AF type %d>", phost->sa_family);
        break;
    }

    printf("%-*s ", width-1, buf);
}


static void
route_ParseHdr(struct rt_msghdr *rtm, struct sockaddr *sa[RTAX_MAX])
{
    char *wp;
    int rtax;

    wp = (char *)(rtm + 1);

    for (rtax = 0; rtax < RTAX_MAX; rtax++)
    {
        if (rtm->rtm_addrs & (1 << rtax))
        {
            sa[rtax] = (struct sockaddr *)wp;
            wp += ROUND_UP(sa[rtax]->sa_len, sizeof(long));
           if (sa[rtax]->sa_family == 0)
                sa[rtax] = NULL;	/* ??? */
        }
        else
            sa[rtax] = NULL;
    }
}

static int route_nifs = -1;

const char *
Index2Nam(int idx)
{
  /*
   * XXX: Maybe we should select() on the routing socket so that we can
   *      notice interfaces that come & go (PCCARD support).
   *      Or we could even support a signal that resets these so that
   *      the PCCARD insert/remove events can signal ppp.
   */
  static char **ifs;		/* Figure these out once */
  static int debug_done;	/* Debug once */

  if (idx > route_nifs || (idx > 0 && ifs[idx-1] == NULL)) {
    int mib[6], have, had;
    size_t needed;
    char *buf, *ptr, *end;
    struct sockaddr_dl *dl;
    struct if_msghdr *ifm;

    if (ifs) {
      free(ifs);
      ifs = NULL;
      route_nifs = 0;
    }
    debug_done = 1;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (osv_sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
      fprintf(stderr, "Index2Nam: osv_sysctl: estimate: %s\n",
              strerror(errno));
      return NumStr(idx, NULL, 0);
    }
    if ((buf = new char[needed]) == NULL)
      return NumStr(idx, NULL, 0);
    if (osv_sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
      delete [] buf;
      return NumStr(idx, NULL, 0);
    }
    end = buf + needed;

    have = 0;
    for (ptr = buf; ptr < end; ptr += ifm->ifm_msglen) {
      ifm = (struct if_msghdr *)ptr;
      if (ifm->ifm_type != RTM_IFINFO)
        continue;
      dl = (struct sockaddr_dl *)(ifm + 1);
      if (ifm->ifm_index > 0) {
        if (ifm->ifm_index > have) {
          char **newifs;

          had = have;
          have = ifm->ifm_index + 5;
          if (had)
            newifs = (char **)realloc(ifs, sizeof(char *) * have);
          else
            newifs = (char **)malloc(sizeof(char *) * have);
          if (!newifs) {
            printf("Index2Nam: %s\n", strerror(errno));
            route_nifs = 0;
            if (ifs) {
              free(ifs);
              ifs = NULL;
            }
            delete [] buf;
            return NumStr(idx, NULL, 0);
          }
          ifs = newifs;
          memset(ifs + had, '\0', sizeof(char *) * (have - had));
        }
        if (ifs[ifm->ifm_index-1] == NULL) {
          ifs[ifm->ifm_index-1] = (char *)malloc(dl->sdl_nlen+1);
          if (ifs[ifm->ifm_index-1] == NULL)
	    printf("Skipping interface %s: Out of memory\n",
                  ifm->ifm_index);
	  else {
	    memcpy(ifs[ifm->ifm_index-1], dl->sdl_data, dl->sdl_nlen);
	    ifs[ifm->ifm_index-1][dl->sdl_nlen] = '\0';
	    if (route_nifs < ifm->ifm_index)
	      route_nifs = ifm->ifm_index;
	  }
        }
      } else
        printf("Skipping out-of-range interface %d!\n",
                  ifm->ifm_index);
    }
    delete [] buf ;
  }

  if (!debug_done) {
    int f;

    printf("Found the following interfaces:\n");
    for (f = 0; f < route_nifs; f++)
      if (ifs[f] != NULL)
        printf(" Index %d, name \"%s\"\n", f+1, ifs[f]);
    debug_done = 1;
  }

  if (idx < 1 || idx > route_nifs || ifs[idx-1] == NULL)
    return NumStr(idx, NULL, 0);

  return ifs[idx-1];
}

int
main(int argc, char *argv[])
{
    struct rt_msghdr *rtm;
    struct sockaddr *sa[RTAX_MAX];
    char *sp, *ep, *cp;
    size_t needed = -1;
    int mib[6];
    bool inet6 = false ;
    
    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_DUMP;
    mib[5] = 0;
    if (osv_sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
        fprintf(stderr, "lsroute: osv_sysctl: estimate: %s\n",
                strerror(errno));
        return (1);
    }
    
    sp = new  char [needed] ;
    if (sp == NULL)
        return (1);
    if (osv_sysctl(mib, 6, sp, &needed, NULL, 0) < 0) {
        fprintf(stderr, "lsroute: osv_sysctl: getroute: %s\n",
                strerror(errno));
        delete [] sp;
        return (1);
    }
    ep = sp + needed;

    printf("\n") ;
    printf("Internet\n") ;
    printf("%-20s%-20sFlags  Netif\n", "Destination", "Gateway");
    for (cp = sp; cp < ep; cp += rtm->rtm_msglen)
    {
        rtm = (struct rt_msghdr *)cp;

        route_ParseHdr(rtm, sa);

        if (sa[RTAX_DST] && sa[RTAX_GATEWAY] &&
            sa[RTAX_DST]->sa_family != AF_INET6)
        {
            p_sockaddr(sa[RTAX_DST], sa[RTAX_NETMASK], 20);
            p_sockaddr(sa[RTAX_GATEWAY], NULL, 20);

            p_flags(rtm->rtm_flags, 6);
            printf(" %s\n", Index2Nam(rtm->rtm_index));
        }
        else if (sa[RTAX_DST]->sa_family != AF_INET6)
            inet6 = true ;
    }

    if (inet6)
    {
        printf("\n") ;
        printf("Internet6\n") ;
        printf("%-20s%-20sFlags  Netif\n", "Destination", "Gateway");
        for (cp = sp; cp < ep; cp += rtm->rtm_msglen)
        {
            rtm = (struct rt_msghdr *)cp;

            route_ParseHdr(rtm, sa);

            if (sa[RTAX_DST] && sa[RTAX_GATEWAY] &&
                sa[RTAX_DST]->sa_family == AF_INET6) {
                p_sockaddr(sa[RTAX_DST], sa[RTAX_NETMASK], 20);
                p_sockaddr(sa[RTAX_GATEWAY], NULL, 20);

                p_flags(rtm->rtm_flags, 6);
                printf(" %s\n", Index2Nam(rtm->rtm_index));
            }
        }
    }
    delete [] sp;
    return 0;
}
