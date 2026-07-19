/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 - 2020 Andy Green <andy@warmcat.com>
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

int
lws_plat_pipe_create(struct lws *wsi)
{
	struct lws_context_per_thread *pt =
			&wsi->a.context->pt[(int)wsi->tsi];
	int flags, n;

	pt->dummy_pipe_fds[0] = LWS_SOCK_INVALID;
	pt->dummy_pipe_fds[1] = LWS_SOCK_INVALID;

	n = pipe(pt->dummy_pipe_fds);
	if (n < 0)
		return n;

	flags = fcntl(pt->dummy_pipe_fds[0], F_GETFL, 0);
	if (flags < 0 ||
	    fcntl(pt->dummy_pipe_fds[0], F_SETFL, flags | O_NONBLOCK) < 0)
		goto bail;

	flags = fcntl(pt->dummy_pipe_fds[1], F_GETFL, 0);
	if (flags < 0 ||
	    fcntl(pt->dummy_pipe_fds[1], F_SETFL, flags | O_NONBLOCK) < 0)
		goto bail;

	return 0;

bail:
	close(pt->dummy_pipe_fds[0]);
	close(pt->dummy_pipe_fds[1]);
	pt->dummy_pipe_fds[0] = LWS_SOCK_INVALID;
	pt->dummy_pipe_fds[1] = LWS_SOCK_INVALID;

	return -1;
}

int
lws_plat_pipe_signal(struct lws_context *ctx, int tsi)
{
	struct lws_context_per_thread *pt = &ctx->pt[tsi];
	char buf = 0;
	int n;

	n = (int)write(pt->dummy_pipe_fds[1], &buf, 1);

	return n != 1;
}

void
lws_plat_pipe_close(struct lws *wsi)
{
	struct lws_context_per_thread *pt =
			&wsi->a.context->pt[(int)wsi->tsi];
	lws_sockfd_type read_fd = pt->dummy_pipe_fds[0];
	lws_sockfd_type write_fd = pt->dummy_pipe_fds[1];

	if (wsi->desc.sockfd == read_fd || wsi->desc.sockfd == write_fd)
		wsi->desc.sockfd = LWS_SOCK_INVALID;

	pt->dummy_pipe_fds[0] = LWS_SOCK_INVALID;
	pt->dummy_pipe_fds[1] = LWS_SOCK_INVALID;

	if (read_fd != LWS_SOCK_INVALID)
		close(read_fd);
	if (write_fd != LWS_SOCK_INVALID && write_fd != read_fd)
		close(write_fd);
}

int
lws_plat_pipe_is_fd_assocated(struct lws_context *cx, int tsi,
			      lws_sockfd_type fd)
{
	struct lws_context_per_thread *pt = &cx->pt[tsi];

	return fd == pt->dummy_pipe_fds[0] || fd == pt->dummy_pipe_fds[1];
}
