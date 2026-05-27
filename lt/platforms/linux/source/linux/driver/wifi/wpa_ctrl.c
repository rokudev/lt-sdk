/*
 * wpa_supplicant/hostapd control interface library
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * Derived from wpa_ctrl.c.
 */

//#include "includes.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "wpa_ctrl.h"

typedef long os_time_t;

struct os_time {
    os_time_t sec;
    os_time_t usec;
};

void sleep2(os_time_t sec, os_time_t usec)
{
    if (sec)
        sleep(sec);
    if (usec)
        usleep(usec);
}

/**
 * struct wpa_ctrl - Internal structure for control interface library
 *
 * This structure is used by the wpa_supplicant/hostapd control interface
 * library to store internal data. Programs using the library should not touch
 * this data directly. They can only use the pointer to the data structure as
 * an identifier for the control interface connection and use this as one of
 * the arguments for most of the control interface library functions.
 */
struct wpa_ctrl {
    int s;
    struct sockaddr_un local;
    struct sockaddr_un dest;
};


#ifndef CONFIG_CTRL_IFACE_CLIENT_DIR
#define CONFIG_CTRL_IFACE_CLIENT_DIR "/tmp"
#endif /* CONFIG_CTRL_IFACE_CLIENT_DIR */
#ifndef CONFIG_CTRL_IFACE_CLIENT_PREFIX
#define CONFIG_CTRL_IFACE_CLIENT_PREFIX "wpa_ctrl_"
#endif /* CONFIG_CTRL_IFACE_CLIENT_PREFIX */

size_t strlcpy(char *dest, const char *src, size_t siz)
{
    const char *s = src;
    size_t left = siz;

    if (left) {
        /* Copy string up to the maximum size of the dest buffer */
        while (--left != 0) {
            if ((*dest++ = *s++) == '\0')
                break;
        }
    }

    if (left == 0) {
        /* Not enough room for the string; force NUL-termination */
        if (siz != 0)
            *dest = '\0';
        while (*s++)
            ; /* determine total src string length */
    }

    return s - src - 1;
}

int get_time(struct os_time *t)
{
    int res;
    struct timeval tv;
    res = gettimeofday(&tv, NULL);
    t->sec = tv.tv_sec;
    t->usec = tv.tv_usec;
    return res;
}

struct wpa_ctrl * wpa_ctrl_open(const char *ctrl_path)
{
    struct wpa_ctrl *ctrl;
    static int counter = 0;
    int ret;
    size_t res;
    int tries = 0;
    int flags;

    if (ctrl_path == NULL)
        return NULL;

    ctrl = (struct wpa_ctrl *)malloc(sizeof(*ctrl));
    if (ctrl == NULL)
        return NULL;
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->s = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (ctrl->s < 0) {
        free(ctrl);
        return NULL;
    }

    ctrl->local.sun_family = AF_UNIX;

    {
        __sync_fetch_and_add(&counter, 1);
    }
try_again:
    ret = snprintf(ctrl->local.sun_path, sizeof(ctrl->local.sun_path),
              CONFIG_CTRL_IFACE_CLIENT_DIR "/"
              CONFIG_CTRL_IFACE_CLIENT_PREFIX "%d-%d",
              (int) getpid(), counter);
    if (ret < 0 || (size_t) ret >= sizeof(ctrl->local.sun_path)) {
        close(ctrl->s);
        free(ctrl);
        return NULL;
    }
    tries++;
    //printf("----- sun_path %s\n", ctrl->local.sun_path);
    if (bind(ctrl->s, (struct sockaddr *) &ctrl->local,
            sizeof(ctrl->local)) < 0) {
        if (errno == EADDRINUSE && tries < 2) {
            /*
             * getpid() returns unique identifier for this instance
             * of wpa_ctrl, so the existing socket file must have
             * been left by unclean termination of an earlier run.
             * Remove the file and try again.
             */
            unlink(ctrl->local.sun_path);
            goto try_again;
        }
        close(ctrl->s);
        free(ctrl);
        //printf("WPASupplicant: bind error %d: %s\n", errno, ctrl->local.sun_path);
        return NULL;
    }

    ctrl->dest.sun_family = AF_UNIX;
    if (strncmp(ctrl_path, "@abstract:", 10) == 0) {
        ctrl->dest.sun_path[0] = '\0';
        strlcpy(ctrl->dest.sun_path + 1, ctrl_path + 10,
               sizeof(ctrl->dest.sun_path) - 1);
    } else {
        res = strlcpy(ctrl->dest.sun_path, ctrl_path,
                 sizeof(ctrl->dest.sun_path));
        if (res >= sizeof(ctrl->dest.sun_path)) {
            close(ctrl->s);
            free(ctrl);
            return NULL;
        }
    }
    //printf("----- dest path %s\n", ctrl->dest.sun_path);
    if (connect(ctrl->s, (struct sockaddr *) &ctrl->dest,
            sizeof(ctrl->dest)) < 0) {
        close(ctrl->s);
        unlink(ctrl->local.sun_path);
        free(ctrl);
        //printf("WPASupplicant: connect error %d: %s\n", errno, ctrl->dest.sun_path);
        return NULL;
    }

    /*
     * Make socket non-blocking so that we don't hang forever if
     * target dies unexpectedly.
     */
    flags = fcntl(ctrl->s, F_GETFL);
    if (flags >= 0) {
        flags |= O_NONBLOCK;
        if (fcntl(ctrl->s, F_SETFL, flags) < 0) {
            perror("fcntl(ctrl->s, O_NONBLOCK)");
            /* Not fatal, continue on.*/
        }
    }

    return ctrl;
}

void wpa_ctrl_close(struct wpa_ctrl *ctrl)
{
    if (ctrl == NULL)
        return;
    unlink(ctrl->local.sun_path);
    if (ctrl->s >= 0)
        close(ctrl->s);
    free(ctrl);
}

int unsolicited_msg(char *msg, size_t msg_len, int len,
                   void (*msg_cb)(char *msg, size_t len))
{
    int unsolicited = 0;
    
    if (len > 0 && msg[0] == '<') {
        /* This is an unsolicited message from
         * wpa_supplicant, not the reply to the
         * request. Use msg_cb to report this to the
         * caller. */
        unsolicited = 1;
        if (msg_cb) {
            /* Make sure the message is nul
             * terminated. */
            if ((size_t) len == msg_len)
                len = (msg_len) - 1;
            msg[len] = '\0';
            msg_cb(msg, len);
        }
    }
    return unsolicited;
}

int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
             char *reply, size_t *reply_len,
             void (*msg_cb)(char *msg, size_t len))
{
    struct timeval tv;
    struct os_time started_at;
    int res;
    fd_set rfds;
    const char *_cmd;
    char *cmd_buf = NULL;
    size_t _cmd_len;

    {
        _cmd = cmd;
        _cmd_len = cmd_len;
    }

    /*
     * Flush receive buffer:
     * - Stale replies are silently dropped
     * - Unsolicited messages are sent to the callback function(may be null)
     */
    while ((res = recv(ctrl->s, reply, *reply_len, 0)) > 0) {
        unsolicited_msg(reply, *reply_len, res, msg_cb);
    }

    errno = 0;
    started_at.sec = 0;
    started_at.usec = 0;
retry_send:
    if (send(ctrl->s, _cmd, _cmd_len, 0) < 0) {
        if (errno == EAGAIN || errno == EBUSY || errno == EWOULDBLOCK)
        {
            /*
             * Must be a non-blocking socket... Try for a bit
             * longer before giving up.
             */
            if (started_at.sec == 0)
                get_time(&started_at);
            else {
                struct os_time n;
                get_time(&n);
                /* Try for a few seconds. */
                if (n.sec > started_at.sec + 5)
                    goto send_err;
            }
            sleep2(1, 0);
            goto retry_send;
        }
    send_err:
        free(cmd_buf);
        return -1;
    }
    free(cmd_buf);

    for (;;) {
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        FD_ZERO(&rfds);
        FD_SET(ctrl->s, &rfds);
        res = select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
        if (res < 0)
            return res;
        if (FD_ISSET(ctrl->s, &rfds)) {
            res = recv(ctrl->s, reply, *reply_len, 0);
            if (res < 0)
                return res;
            if (unsolicited_msg(reply, *reply_len, res, msg_cb)) {
                continue;
            }
            *reply_len = res;
            break;
        } else {
            return -2;
        }
    }
    return 0;
}


static int wpa_ctrl_attach_helper(struct wpa_ctrl *ctrl, int attach)
{
    char buf[10];
    int ret;
    size_t len = 10;

    ret = wpa_ctrl_request(ctrl, attach ? "ATTACH" : "DETACH", 6,
                   buf, &len, NULL);
    if (ret < 0)
        return ret;
    if (len == 3 && memcmp(buf, "OK\n", 3) == 0)
        return 0;
    return -1;
}


int wpa_ctrl_attach(struct wpa_ctrl *ctrl)
{
    return wpa_ctrl_attach_helper(ctrl, 1);
}


int wpa_ctrl_detach(struct wpa_ctrl *ctrl)
{
    return wpa_ctrl_attach_helper(ctrl, 0);
}


int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len)
{
    int res;

    res = recv(ctrl->s, reply, *reply_len, 0);
    if (res < 0)
        return res;
    *reply_len = res;
    return 0;
}

// monitor both wpa_ctrl and p2p_ipc socket
// return a bitmap, bit is set if the socket has data
// bit 0: wpa_ctrl socket
// bit 1: p2p_ipc_socket 
int wpa_ctrl_and_p2p_ipc_socket_pending(struct wpa_ctrl *ctrl, int p2p_ipc_sock)
{
    int mask = 0;
    struct pollfd pfds[2] = {{.fd = ctrl->s, .events = POLLIN, .revents = 0},
                             {.fd = p2p_ipc_sock, .events = POLLIN, .revents = 0}};

    if (poll(pfds, 2, 0) > 0) {
        for (int i = 0; i < 2; i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                continue;
            } else if (pfds[i].revents & POLLIN) {
                mask |= (1 << i);
            }
        }
    }
    return mask;
}


int wpa_ctrl_pending(struct wpa_ctrl *ctrl)
{
    struct timeval tv;
    fd_set rfds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(ctrl->s, &rfds);
    select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
    return FD_ISSET(ctrl->s, &rfds);
}


int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl)
{
    return ctrl->s;
}
