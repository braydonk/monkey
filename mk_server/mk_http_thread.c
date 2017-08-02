/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Server
 *  ==================
 *  Copyright 2001-2016 Monkey Software LLC <eduardo@monkey.io>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <monkey/mk_info.h>
#include <monkey/mk_plugin.h>
#include <monkey/mk_thread.h>
#include <monkey/mk_net.h>
#include <monkey/mk_vhost.h>
#include <monkey/mk_http_thread.h>

#include <stdlib.h>

static void cb_http_thread_destroy(void *data)
{
    struct mk_http_thread *mth;

    /* Before to destroy the thread context, unlink it */
    mth = (struct mk_http_thread *) data;
    mk_list_del(&mth->_head);
}

/*
 * libco do not support parameters in the entrypoint function due to the
 * complexity of implementation in terms of architecture and compiler, but
 * it provide a workaround using a global structure as a middle entry-point
 * that achieve the same stuff.
 */
struct mk_http_libco_params {
    int type;
    struct mk_vhost_handler *handler;
    struct mk_http_session *session;
    struct mk_http_request *request;
    int n_params;
    struct mk_list *params;
    struct mk_thread *th;
};

struct mk_http_libco_params libco_param;

static inline void thread_cb_init_vars()
{
    int type = libco_param.type;
    struct mk_vhost_handler *handler = libco_param.handler;
    struct mk_http_session *session = libco_param.session;
    struct mk_http_request *request = libco_param.request;
    int n_params = libco_param.n_params;
    struct mk_list *params = libco_param.params;
    struct mk_thread *th = libco_param.th;
    struct mk_plugin *plugin;

    /*
     * Until this point the th->callee already set the variables, so we
     * wait until the core wanted to resume so we really trigger the
     * output callback.
     */
    co_switch(th->caller);

    if (type == MK_HTTP_THREAD_LIB) {
        handler->cb(request, handler->data);
        mk_thread_yield(th);
    }
    else if (type == MK_HTTP_THREAD_PLUGIN) {
        /* FIXME: call plugin handler callback with params */
    }

    printf("init vars finishing\n");
}

static inline void thread_params_set(struct mk_thread *th,
                                     int type,
                                     struct mk_vhost_handler *handler,
                                     struct mk_http_session *session,
                                     struct mk_http_request *request,
                                     int n_params,
                                     struct mk_list *params)
{
    /* Callback parameters in order */
    libco_param.type     = type;
    libco_param.handler  = handler;
    libco_param.session  = session;
    libco_param.request  = request;
    libco_param.n_params = n_params;
    libco_param.params   = params;
    libco_param.th = th;

    co_switch(th->callee);
}

struct mk_http_thread *mk_http_thread_new(int type,
                                          struct mk_vhost_handler *handler,
                                          struct mk_http_session *session,
                                          struct mk_http_request *request,
                                          int n_params,
                                          struct mk_list *params)
{
    size_t stack_size;
    struct mk_thread *th;
    struct mk_http_thread *mth;
    struct mk_sched_worker *sched;

    sched = mk_sched_get_thread_conf();
    if (!sched) {
        return NULL;
    }

    th = mk_thread_new(sizeof(struct mk_http_thread), cb_http_thread_destroy);
    if (!th) {
        return NULL;
    }

    mth = (struct mk_http_thread *) MK_THREAD_DATA(th);
    if (!mth) {
        return NULL;
    }

    mth->session = session;
    mth->request = request;
    mth->parent  = th;
    request->thread = mth;
    mk_list_add(&mth->_head, &sched->threads);

    th->caller = co_active();
    th->callee = co_create(MK_THREAD_STACK_SIZE,
                           thread_cb_init_vars, &stack_size);

#ifdef MK_HAVE_VALGRIND
    th->valgrind_stack_id = VALGRIND_STACK_REGISTER(th->callee,
                                                    ((char *)th->callee) + stack_size);
#endif

    /* Workaround for makecontext() */
    thread_params_set(th, type, handler, session, request, n_params, params);

    return mth;
}

int mk_http_thread_event(struct mk_event *event)
{
    struct mk_sched_conn *conn = event;
    mk_thread_resume(conn->channel.thread);
    return 0;
}

/*
 * Start the co-routine: invoke coroutine callback and start processing
 * data flush requests.
 */
int mk_http_thread_start(struct mk_http_thread *mth)
{
    mk_http_thread_resume(mth);
    //mk_header_prepare(cs, sr, server);
}
