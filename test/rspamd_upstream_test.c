/*
 * Copyright (c) 2014, Vsevolod Stakhov
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "main.h"
#include "upstream.h"

const char *test_upstream_list = "microsoft.com:443:1,google.com:2,kernel.org:443:3";


static void
rspamd_upstream_test_method (struct upstream_list *ls,
		enum rspamd_upstream_rotation rot, const gchar *expected)
{
	struct upstream *up;

	up = rspamd_upstream_get (ls, rot);
	g_assert (up != NULL);
	g_assert (strcmp (rspamd_upstream_name (up), expected) == 0);
}

void
rspamd_upstream_test_func (void)
{
	struct upstream_list *ls;
	struct event_base *ev_base = event_init ();
	struct rspamd_dns_resolver *resolver;
	struct rspamd_config *cfg;

	cfg = (struct rspamd_config *)g_malloc (sizeof (struct rspamd_config));
	bzero (cfg, sizeof (struct rspamd_config));
	cfg->cfg_pool = rspamd_mempool_new (rspamd_mempool_suggest_size ());
	cfg->dns_retransmits = 2;
	cfg->dns_timeout = 0.5;

	resolver = dns_resolver_init (NULL, ev_base, cfg);

	rspamd_upstreams_library_init (resolver->r, ev_base);

	ls = rspamd_upstreams_create ();
	g_assert (rspamd_upstreams_parse_line (ls, test_upstream_list, 443, NULL));

	/* Test master-slave rotation */
	rspamd_upstream_test_method (ls, RSPAMD_UPSTREAM_MASTER_SLAVE, "kernel.org");
	rspamd_upstream_test_method (ls, RSPAMD_UPSTREAM_MASTER_SLAVE, "kernel.org");

	/* Test round-robin rotation */
	rspamd_upstream_test_method (ls, RSPAMD_UPSTREAM_ROUND_ROBIN, "kernel.org");
	rspamd_upstream_test_method (ls, RSPAMD_UPSTREAM_ROUND_ROBIN, "google.com");
	rspamd_upstream_test_method (ls, RSPAMD_UPSTREAM_ROUND_ROBIN, "microsoft.com");
}