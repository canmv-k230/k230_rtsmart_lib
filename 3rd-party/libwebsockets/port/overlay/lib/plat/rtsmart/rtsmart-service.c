/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 - 2019 Andy Green <andy@warmcat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "private-lib-core.h"

/*
 * sys_poll() translates the input events to RT-Thread's private bit values,
 * then copies those translated values back to userspace when an fd is ready.
 * Preserve libwebsockets' POSIX event masks across the syscall.
 */
static int
lws_plat_poll(struct lws_pollfd *fds, unsigned int count, int timeout_ms)
{
	short saved_events[count ? count : 1];
	unsigned int i;
	int n;

	for (i = 0; i < count; i++)
		saved_events[i] = fds[i].events;

	n = poll(fds, count, timeout_ms);

	for (i = 0; i < count; i++)
		fds[i].events = saved_events[i];

	return n;
}

int
lws_poll_listen_fd(struct lws_pollfd *fd)
{
	return lws_plat_poll(fd, 1, 0);
}

int
_lws_plat_service_forced_tsi(struct lws_context *context, int tsi)
{
	struct lws_context_per_thread *pt = &context->pt[tsi];
	int m, n, r;

	r = lws_service_flag_pending(context, tsi);

	for (n = 0; n < (int)pt->fds_count; n++) {
		lws_sockfd_type fd = pt->fds[n].fd;

		if (!pt->fds[n].revents)
			continue;

		m = lws_service_fd_tsi(context, &pt->fds[n], tsi);
		if (m < 0) {
			lwsl_err("%s: lws_service_fd_tsi returned %d\n",
				 __func__, m);
			return -1;
		}

		/* Retry a slot replaced by the final fd when service closes it. */
		if (m && pt->fds[n].fd != fd)
			n--;
	}

	lws_service_do_ripe_rxflow(pt);

	return r;
}

#define LWS_POLL_WAIT_LIMIT 2000000000

int
_lws_plat_service_tsi(struct lws_context *context, int timeout_ms, int tsi)
{
	volatile struct lws_foreign_thread_pollfd *ftp, *next;
	volatile struct lws_context_per_thread *vpt;
	struct lws_context_per_thread *pt;
	lws_usec_t timeout_us, us;
#if defined(LWS_WITH_WAKE_LOGGING)
	unsigned int u;
	char hu[25];
	lws_usec_t a1;
#endif
#if defined(LWS_WITH_SYS_METRICS) || defined(LWS_WITH_WAKE_LOGGING)
	lws_usec_t a, b = 0;
#endif
	int n;
#if (defined(LWS_ROLE_WS) && !defined(LWS_WITHOUT_EXTENSIONS)) || \
	defined(LWS_WITH_TLS)
	int m;
#endif

	if (!context)
		return 1;

#if defined(LWS_WITH_SYS_METRICS)
	b =
#endif
			us = lws_now_usecs();

	pt = &context->pt[tsi];
	vpt = (volatile struct lws_context_per_thread *)pt;

	if (timeout_ms < 0)
		timeout_ms = 0;
	else
		/* Timers shorten the Unix ceiling; the event pipe wakes it. */
		timeout_ms = LWS_POLL_WAIT_LIMIT;
	timeout_us = ((lws_usec_t)timeout_ms) * LWS_US_PER_MS;

	if (context->event_loop_ops->run_pt)
		context->event_loop_ops->run_pt(context, tsi);

	if (!pt->service_tid_detected && context->vhost_list) {
		lws_fakewsi_def_plwsa(pt);

		lws_fakewsi_prep_plwsa_ctx(context);

		pt->service_tid = context->vhost_list->protocols[0].callback(
					(struct lws *)plwsa,
					LWS_CALLBACK_GET_THREAD_ID,
					context->vhost_list->protocols[0].user,
					NULL, 0);
		pt->service_tid_detected = 1;
	}

	lws_pt_lock(pt, __func__);
	/* Service scheduled events and limit the wait to the next due event. */
	us = __lws_sul_service_ripe(pt->pt_sul_owner,
				    LWS_COUNT_PT_SUL_OWNERS, us);
	if (us && us < timeout_us)
		timeout_us = us < context->us_wait_resolution ?
				context->us_wait_resolution : us;

	lws_pt_unlock(pt);

	if (!lws_service_adjust_timeout(context, 1, tsi))
		timeout_us = 0;

	/* Convert to milliseconds without allowing poll()'s signed int to wrap. */
	timeout_us /= LWS_US_PER_MS;

#if defined(LWS_WITH_SYS_METRICS) || defined(LWS_WITH_WAKE_LOGGING)
	a = lws_now_usecs() - b;
#endif
#if defined(LWS_WITH_WAKE_LOGGING)
	a1 = lws_now_usecs();
	lws_humanize(hu, sizeof(hu),
		     (uint64_t)(timeout_us * LWS_US_PER_MS),
		     humanize_schema_us);
	lwsl_cx_notice(context,
		       "event loop: entering sleep... scheduled wake after %s", hu);
#endif
	vpt->inside_poll = 1;
	lws_memory_barrier();
	n = lws_plat_poll(pt->fds, pt->fds_count, (int)timeout_us);
	vpt->inside_poll = 0;
	lws_memory_barrier();

#if defined(LWS_WITH_SYS_METRICS) || defined(LWS_WITH_WAKE_LOGGING)
	b = lws_now_usecs();
#endif
#if defined(LWS_WITH_WAKE_LOGGING)
	lws_humanize(hu, sizeof(hu), (uint64_t)(b - a1),
		     humanize_schema_us);
	lwsl_cx_notice(context, "event loop: WOKE after %s, %d fds ready",
		       hu, n);
	for (u = 0; u < pt->fds_count; u++) {
		struct lws *wsi;
		struct lws_pollfd *pfd = &vpt->fds[u];

		if (lws_socket_is_valid(pfd->fd) &&
		    (pfd->revents & (POLLIN | POLLOUT | POLLERR))) {
			wsi = wsi_from_fd(context, pfd->fd);
#if defined(LWS_WITH_SECURE_STREAMS)
			if (wsi->for_ss && wsi->a.opaque_user_data) {
				lws_ss_handle_t *fih =
					(lws_ss_handle_t *)wsi->a.opaque_user_data;

				lwsl_ss_notice(fih,
					"    ready fd %d, %s %s %s, SS policy %s",
					pfd->fd,
					pfd->revents & POLLIN ? "POLLIN" : "",
					pfd->revents & POLLOUT ? "POLLOUT" : "",
					pfd->revents & POLLERR ? "POLLERR" : "",
					fih->policy ? fih->policy->streamtype :
						      "(null)");
			} else
#endif
				lwsl_wsi_notice(wsi,
					"    ready fd %d, %s %s %s, protocol %s",
					pfd->fd,
					pfd->revents & POLLIN ? "POLLIN" : "",
					pfd->revents & POLLOUT ? "POLLOUT" : "",
					pfd->revents & POLLERR ? "POLLERR" : "",
					wsi->a.protocol ? wsi->a.protocol->name :
							  "(null)");
		}
	}
#endif

	/* Yield while a foreign pollfd update is in its short critical section. */
	while (vpt->foreign_spinlock)
		usleep(1000);

	lws_pt_lock(pt, __func__);

	ftp = vpt->foreign_pfd_list;
	while (ftp) {
		struct lws *wsi;
		struct lws_pollfd *pfd;

		next = ftp->next;
		pfd = &vpt->fds[ftp->fd_index];
		if (lws_socket_is_valid(pfd->fd)) {
			wsi = wsi_from_fd(context, pfd->fd);
			if (wsi)
				__lws_change_pollfd(wsi, ftp->_and, ftp->_or);
		}
#if defined(LWS_WITH_WAKE_LOGGING)
		else
			lwsl_cx_notice(context,
				       "*** WOKE on Invalid fd in foreign pfd list");
#endif
		lws_free((void *)ftp);
		ftp = next;
	}
	vpt->foreign_pfd_list = NULL;
	lws_memory_barrier();

	lws_pt_unlock(pt);

#if (defined(LWS_ROLE_WS) && !defined(LWS_WITHOUT_EXTENSIONS)) || \
	defined(LWS_WITH_TLS)
	m = 0;
#endif
#if defined(LWS_ROLE_WS) && !defined(LWS_WITHOUT_EXTENSIONS)
	m |= !!pt->ws.rx_draining_ext_list;
#endif

#if defined(LWS_WITH_TLS)
	if (pt->context->tls_ops &&
	    pt->context->tls_ops->fake_POLLIN_for_buffered)
		m |= pt->context->tls_ops->fake_POLLIN_for_buffered(pt);
#endif

	if (
#if (defined(LWS_ROLE_WS) && !defined(LWS_WITHOUT_EXTENSIONS)) || \
	defined(LWS_WITH_TLS)
		!m &&
#endif
		!n)
		lws_service_do_ripe_rxflow(pt);
	else if (_lws_plat_service_forced_tsi(context, tsi) < 0)
		return -1;

#if defined(LWS_WITH_SYS_METRICS)
	lws_metric_event(context->mt_service, METRES_GO,
			 (u_mt_t)(a + (lws_now_usecs() - b)));
#endif

	if (pt->destroy_self) {
		lws_context_destroy(pt->context);
		return -1;
	}

	return 0;
}

int
lws_plat_service(struct lws_context *context, int timeout_ms)
{
	return _lws_plat_service_tsi(context, timeout_ms, 0);
}
