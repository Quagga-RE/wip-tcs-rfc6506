/*
Copyright (c) 2007, 2008 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "babeld.h"
#include "util.h"
#include "net.h"
#include "sockopt.h"
#include "network.h"

int
babel_socket(int port)
{
    struct sockaddr_in6 sin6;
    int s, rc;
    int saved_errno;

    s = socket(PF_INET6, SOCK_DGRAM, 0);
    if(s < 0)
        return -1;

    if
    (
        setsockopt_ipv6_v6only         (s, 1) < 0 ||
        setsockopt_so_reuseaddr        (s, 1) < 0 ||
        setsockopt_ipv6_multicast_loop (s, 0) < 0 ||
        setsockopt_ipv6_unicast_hops   (s, 1) < 0 ||
        setsockopt_ipv6_multicast_hops (s, 1) < 0
    )
        goto fail;

    setsockopt_ipv6_tclass (s, IPTOS_PREC_INTERNETCONTROL);

    if (set_nonblocking (s) < 0)
        goto fail;

    rc = fcntl(s, F_GETFD, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFD, rc | FD_CLOEXEC);
    if(rc < 0)
        goto fail;

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));
    if(rc < 0)
        goto fail;

    return s;

 fail:
    saved_errno = errno;
    close(s);
    errno = saved_errno;
    return -1;
}

int
babel_recv(int s, void *buf, int buflen, struct sockaddr *sin, int slen)
{
    struct iovec iovec;
    struct msghdr msg;
    int rc;

    memset(&msg, 0, sizeof(msg));
    iovec.iov_base = buf;
    iovec.iov_len = buflen;
    msg.msg_name = sin;
    msg.msg_namelen = slen;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;

    rc = recvmsg(s, &msg, 0);
    return rc;
}

int
babel_send(int s,
           void *buf1, int buflen1, void *buf2, int buflen2,
           struct sockaddr *sin, int slen)
{
    struct iovec iovec[2];
    struct msghdr msg;
    int rc;

    iovec[0].iov_base = buf1;
    iovec[0].iov_len = buflen1;
    iovec[1].iov_base = buf2;
    iovec[1].iov_len = buflen2;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (struct sockaddr*)sin;
    msg.msg_namelen = slen;
    msg.msg_iov = iovec;
    msg.msg_iovlen = 2;

 again:
    rc = sendmsg(s, &msg, 0);
    if(rc < 0) {
        if(errno == EINTR)
            goto again;
        else if(errno == EAGAIN) {
            int rc2;
            rc2 = wait_for_fd(1, s, 5);
            if(rc2 > 0)
                goto again;
            errno = EAGAIN;
        }
    }
    return rc;
}

int
tcp_server_socket(int port, int local)
{
    struct sockaddr_in6 sin6;
    int s, rc, saved_errno;

    s = socket(PF_INET6, SOCK_STREAM, 0);
    if(s < 0)
        return -1;

    if (setsockopt_so_reuseaddr (s, 1) < 0)
        goto fail;

    if (set_nonblocking (s) < 0)
        goto fail;

    rc = fcntl(s, F_GETFD, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFD, rc | FD_CLOEXEC);
    if(rc < 0)
        goto fail;

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    if(local) {
        rc = inet_pton(AF_INET6, "::1", &sin6.sin6_addr);
        if(rc < 0)
            goto fail;
    }
    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));
    if(rc < 0)
        goto fail;

    rc = listen(s, 2);
    if(rc < 0)
        goto fail;

    return s;

 fail:
    saved_errno = errno;
    close(s);
    errno = saved_errno;
    return -1;
}
