/* Unit test that verifies limited netlink support in OSv
 *
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This test should run on Linux:
//   gcc tests/tst-netlink.c -o tst-netlink
//   ./tst-netlink

#include <stdio.h>            //printf, perror
#include <string.h>           //memset, strlen
#include <stdlib.h>           //exit
#include <unistd.h>           //close
#include <sys/socket.h>       //msghdr
#include <arpa/inet.h>        //inet_ntop
#include <linux/netlink.h>    //sockaddr_nl
#include <linux/rtnetlink.h>  //rtgenmsg,ifinfomsg
#include <net/if.h>
#include <assert.h>
#include <errno.h>

#define BUFSIZE 8192

void die(const char *s)
{
    perror(s);
    exit(1);
}

int called_response_handler = 0;

int test_netlink(struct nlmsghdr* req, pid_t pid, void (*handle_response)(struct nlmsghdr *))
{
    struct sockaddr_nl src_addr, dst_addr, src_addr2;
    int s, len, end = 0;
    struct msghdr msg;
    struct iovec iov[1];
    char buf[BUFSIZE];

    //create a netlink socket
    if ((s=socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) < 0)
    {
        die("socket FAILED");
    }

    //bind socket
    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = pid; // if 0 kernel will assign unique id
    src_addr.nl_groups = 0;  /* not in mcast groups */
    if (bind(s, (struct sockaddr*) &src_addr, sizeof(src_addr)))
    {
        die("bind FAILED");
    }

    //get sock name to check pid
    memset(&src_addr2, 0, sizeof(src_addr2));
    socklen_t addr_len = sizeof(src_addr2);
    if (getsockname(s, (struct sockaddr*)&src_addr2, &addr_len)) {
        die("getsockname FAILED");
    }
    if (src_addr.nl_pid != 0) {
        assert(src_addr.nl_pid == src_addr2.nl_pid);
    }

    //build destination - kernel netlink address
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.nl_family = AF_NETLINK;
    dst_addr.nl_pid = 0; // should be 0 if destination is kernel
    //dst_addr.nl_pid = 1; //TODO: check that non-0 errors with "sendmsg: Operation not permitted"
    dst_addr.nl_groups = 0;

    //build netlink message
    iov[0].iov_base = req;
    iov[0].iov_len = req->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_name = &dst_addr;
    msg.msg_namelen = sizeof(dst_addr);

    //send the message
    if (sendmsg(s, &msg, 0) < 0)
    {
        die("sendmsg FAILED");
    }

    called_response_handler = 0;
    //parse reply
    while (!end)
    {
        memset(&msg, 0, sizeof(msg)); //These and 2 lines below are needed to reset msg - otherwise weird page faults happen
        msg.msg_iov = iov;            //Check if we can improve things downstream with some asserts or even error handling
        msg.msg_iovlen = 1;

        memset(buf, 0, BUFSIZE);
        msg.msg_iov[0].iov_base = buf;
        msg.msg_iov[0].iov_len = BUFSIZE;
        printf("---> Receiving messages!\n");
        if ((len=recvmsg(s, &msg, 0)) < 0)
        {
            die("recvmsg FAILED");
        }

        for (struct nlmsghdr *rsp = (struct nlmsghdr *)buf;
             NLMSG_OK(rsp, len); rsp = NLMSG_NEXT(rsp, len))
        {
            printf("... received response: %d!\n", rsp->nlmsg_type);
            //Verify pid of the response matches pid of the socket
            assert(rsp->nlmsg_pid == src_addr2.nl_pid);
            //Verify sequence of the response matches sequence of the request
            assert(rsp->nlmsg_seq == req->nlmsg_seq);
            switch (rsp->nlmsg_type)
            {
                case NLMSG_DONE:
                    end++;
                    break;
                case NLMSG_ERROR:
                    called_response_handler = 1;
                    handle_response(rsp);
                    end++;
                    break;
                default:
                    called_response_handler = 1;
                    handle_response(rsp);
                    break;
            }
        }
    }

    if (close(s)) {
        die("close FAILED");
    };
    return 0;
}

/////////////////////////////
//Test RTM_GETLINK requests
/////////////////////////////
void test_getlink_response(struct nlmsghdr *rsp)
{
    struct ifinfomsg *iface;
    struct rtattr *attr;
    int len;

    assert(rsp->nlmsg_type == RTM_NEWLINK);

    iface = NLMSG_DATA(rsp);
    len = IFLA_PAYLOAD(rsp);

    printf("Interface %d: up=%d\n", iface->ifi_index, (iface->ifi_flags & IFF_UP) != 0);

    assert(iface->ifi_family == AF_UNSPEC);
    if (iface->ifi_flags & IFF_LOOPBACK) {
        assert(iface->ifi_index == 1);
        assert(iface->ifi_flags & IFF_UP);
    } else {
        assert(iface->ifi_index > 1);
    }
    //TODO: Verify ifi_type

    /* loop over all attributes for the NEWLINK message */
    for (attr = IFLA_RTA(iface); RTA_OK(attr, len); attr = RTA_NEXT(attr, len))
    {
        switch (attr->rta_type)
        {
            case IFLA_IFNAME:
                printf("\tname=%s\n", (char *)RTA_DATA(attr));
                break;
            case IFLA_ADDRESS:
            {
                unsigned char* ptr = (unsigned char*)RTA_DATA(attr);
                printf("\taddress=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
                break;
            }
            default:
                break;
        }
    }
}

struct nl_getlink_req {
  struct nlmsghdr hdr;
  struct rtgenmsg gen;
};

void test_getlink(pid_t pid)
{
    //build netlink request
    struct nl_getlink_req req;
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    req.hdr.nlmsg_type = RTM_GETLINK;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 321;
    req.hdr.nlmsg_pid = getpid();
    req.gen.rtgen_family = AF_INET;

    test_netlink(&req.hdr, pid, test_getlink_response);
    assert(called_response_handler);
}

/////////////////////////////
//Test RTM_GETADDR requests
/////////////////////////////
void print_ip_address(const char *type, int ip)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    printf("\t%s ip=%d.%d.%d.%d\n", type, bytes[0], bytes[1], bytes[2], bytes[3]);
}

void test_getaddr_response(struct nlmsghdr *rsp)
{
    struct ifaddrmsg *addr;
    struct rtattr *attr;
    int len;

    assert(rsp->nlmsg_type == RTM_NEWADDR);

    addr = NLMSG_DATA(rsp);
    len = IFA_PAYLOAD(rsp);

    printf("Address %d:\n", addr->ifa_index);
    assert(addr->ifa_family == AF_INET || addr->ifa_family == AF_INET6);

    /* loop over all attributes for the NEWLINK message */
    for (attr = IFA_RTA(addr); RTA_OK(attr, len); attr = RTA_NEXT(attr, len))
    {
        switch (attr->rta_type)
        {
            case IFA_LABEL:
                printf("\tlabel=%s\n", (char *)RTA_DATA(attr));
                break;
            case IFA_ADDRESS:
                print_ip_address("interface", *(int*)RTA_DATA(attr));
                break;
            case IFA_BROADCAST:
                print_ip_address("broadcast", *(int*)RTA_DATA(attr));
                break;
            case IFA_LOCAL:
                print_ip_address("local", *(int*)RTA_DATA(attr));
                break;
            default:
                break;
        }
    }
}

struct nl_getaddr_req {
  struct nlmsghdr hdr;
  struct rtgenmsg gen;
};

void test_getaddr(pid_t pid)
{
    //build netlink request
    struct nl_getaddr_req req;
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    req.hdr.nlmsg_type = RTM_GETADDR;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 654;
    req.hdr.nlmsg_pid = getpid();
    req.gen.rtgen_family = AF_INET;

    test_netlink(&req.hdr, pid, test_getaddr_response);
    assert(called_response_handler);
}

/////////////////////////////
//Test RTM_GETNEIGH requests
/////////////////////////////
#define ND_RTA(r) ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))

void test_getneigh_response(struct nlmsghdr *rsp)
{
    struct ndmsg *nd;
    struct rtattr *attr;
    int len;

    struct in_addr *inp;
    char ipv4string[INET_ADDRSTRLEN];

    assert(rsp->nlmsg_type == RTM_NEWNEIGH);

    nd = NLMSG_DATA(rsp);
    //len = NLMSG_PAYLOAD(rsp,sizeof(struct ndmsg));
    len = RTM_PAYLOAD(rsp);

    printf("Neighbour Table Entry %d:\n", nd->ndm_ifindex);
    assert(nd->ndm_family == AF_INET || nd->ndm_family == AF_INET6);

    printf("\tndm_state=");
    switch (nd->ndm_state) {
        case NUD_INCOMPLETE: printf("NUD_INCOMPLETE\n"); break;
        case NUD_REACHABLE:  printf("NUD_REACHABLE\n"); break;
        case NUD_STALE:      printf("NUD_STALE\n"); break;
        case NUD_DELAY:      printf("NUD_DELAY\n"); break;
        case NUD_PROBE:      printf("NUD_PROBE\n"); break;
        case NUD_FAILED:     printf("NUD_FAILED\n"); break;
        case NUD_NOARP:      printf("NUD_NOARP\n"); break;
        case NUD_PERMANENT:  printf("NUD_PERMANENT\n"); break;
        default: printf("NUD_???\n");
    }

    /* loop over all attributes for the NEWLINK message */
    for (attr = ND_RTA(nd); RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) //IFA_RTA
    {
        switch (attr->rta_type)
        {
            case NDA_DST:
            {
               inp = (struct in_addr *)RTA_DATA(attr);
               inet_ntop(AF_INET, inp, ipv4string, INET_ADDRSTRLEN);
               printf("\tIP address=%s\n",ipv4string);
            }
            break;

            case NDA_LLADDR:
            {
                unsigned char* ptr = (unsigned char*)RTA_DATA(attr);
                printf("\tL2 address=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
                break;
            }
            default:
                break;
        }
    }
}

struct nl_getneigh_req {
  struct nlmsghdr hdr;
  struct ndmsg r;
};

void test_getneigh(pid_t pid)
{
    //build netlink request
    struct nl_getneigh_req req;
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
    req.hdr.nlmsg_type = RTM_GETNEIGH;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 987;
    req.hdr.nlmsg_pid = getpid();
    req.r.ndm_family = AF_INET;
    req.r.ndm_state = NUD_REACHABLE;

    test_netlink(&req.hdr, pid, test_getneigh_response);
    assert(called_response_handler);
}

//////////////////////////////////////////
//Test unsupported netlink type operation
//////////////////////////////////////////
void test_invalid_type_response(struct nlmsghdr *rsp)
{
    struct nlmsgerr *err;
    assert(rsp->nlmsg_type == NLMSG_ERROR);
    err = (struct nlmsgerr *)NLMSG_DATA(rsp);
    assert(-(err->error) == EOPNOTSUPP);
}

void test_invalid_type_request()
{
    //build netlink request
    struct nl_getneigh_req req;
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(req));
    req.hdr.nlmsg_type = 9999;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    req.hdr.nlmsg_pid = getpid();
    req.r.ndm_family = AF_INET;
    req.r.ndm_state = NUD_REACHABLE;

    test_netlink(&req.hdr, 0, test_invalid_type_response);
    assert(called_response_handler);
}

#ifdef __OSV__
//////////////////////////////////////////
//Test handling of corrupt netlink request
//////////////////////////////////////////
void test_error_response(struct nlmsghdr *rsp)
{
    struct nlmsgerr *err;
    assert(rsp->nlmsg_type == NLMSG_ERROR);
    err = (struct nlmsgerr *)NLMSG_DATA(rsp);
    assert(-(err->error) == EINVAL);
}

void test_corrupt_request()
{
    //build netlink request
    struct nl_getneigh_req req;
    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(0); //This forces EINVAL error
    req.hdr.nlmsg_type = RTM_GETNEIGH;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    req.hdr.nlmsg_pid = getpid();
    req.r.ndm_family = AF_INET;
    req.r.ndm_state = NUD_REACHABLE;

    test_netlink(&req.hdr, 0, test_error_response);
    assert(called_response_handler);
}
#endif

int main()
{
    printf("--------- Interfaces (layer 2) ------\n");
    test_getlink(0);
    test_getlink(getpid());
    test_getlink(123);
    printf("--------- IP Addresses (layer 3) ----\n");
    test_getaddr(0);
    test_getaddr(getpid());
    test_getaddr(456);
    printf("--------- Neighbor Table Entries ----\n");
    test_getneigh(0);
    test_getneigh(getpid());
    test_getneigh(789);
    printf("--------- Testing invalid type request ---\n");
    test_invalid_type_request();
#ifdef __OSV__
    printf("--------- Testing corrupt request ---\n");
    test_corrupt_request();
#endif
}
