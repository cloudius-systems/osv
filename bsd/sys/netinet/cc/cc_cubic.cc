/*-
 * Copyright (c) 2008-2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Lawrence Stewart while studying at the Centre
 * for Advanced Internet Architectures, Swinburne University of Technology, made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by David Hayes under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * An implementation of the CUBIC congestion control algorithm for FreeBSD,
 * based on the Internet Draft "draft-rhee-tcpm-cubic-02" by Rhee, Xu and Ha.
 * Originally released as part of the NewTCP research project at Swinburne
 * University of Technology's Centre for Advanced Internet Architectures,
 * Melbourne, Australia, which was made possible in part by a grant from the
 * Cisco University Research Program Fund at Community Foundation Silicon
 * Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#include <sys/cdefs.h>

#include <osv/initialize.hh>
#include <bsd/porting/netport.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>

#include <bsd/sys/net/vnet.h>

#include <bsd/sys/netinet/cc.h>
#include <bsd/sys/netinet/tcp_seq.h>
#include <bsd/sys/netinet/tcp_timer.h>
#include <bsd/sys/netinet/tcp_var.h>

#include <bsd/sys/netinet/cc/cc_cubic.h>
#include <bsd/sys/netinet/cc/cc_module.h>

static void	cubic_ack_received(struct cc_var *ccv, uint16_t type);
static void	cubic_cb_destroy(struct cc_var *ccv);
static int	cubic_cb_init(struct cc_var *ccv);
static void	cubic_cong_signal(struct cc_var *ccv, uint32_t type);
static void	cubic_conn_init(struct cc_var *ccv);
static int	cubic_mod_init(void);
static void	cubic_post_recovery(struct cc_var *ccv);
static void	cubic_record_rtt(struct cc_var *ccv);
static void	cubic_ssthresh_update(struct cc_var *ccv);

struct cubic {
	/* Cubic K in fixed point form with CUBIC_SHIFT worth of precision. */
	int64_t		K;
	/* Sum of RTT samples across an epoch in bsd_ticks. */
	int64_t		sum_rtt_ticks;
	/* cwnd at the most recent congestion event. */
	unsigned long	max_cwnd;
	/* cwnd at the previous congestion event. */
	unsigned long	prev_max_cwnd;
	/* Number of congestion events. */
	uint32_t	num_cong_events;
	/* Minimum observed rtt in bsd_ticks. */
	int		min_rtt_ticks;
	/* Mean observed rtt between congestion epochs. */
	int		mean_rtt_ticks;
	/* ACKs since last congestion event. */
	int		epoch_ack_count;
	/* Time of last congestion event in bsd_ticks. */
	int		t_last_cong;
};

MALLOC_DEFINE(M_CUBIC, "cubic data",
    "Per connection data required for the CUBIC congestion control algorithm");

struct cc_algo cubic_cc_algo = initialize_with([] (cc_algo& x) {
	strcpy(x.name, "cubic");
	x.ack_received = cubic_ack_received;
	x.cb_destroy = cubic_cb_destroy;
	x.cb_init = cubic_cb_init;
	x.cong_signal = cubic_cong_signal;
	x.conn_init = cubic_conn_init;
	x.mod_init = cubic_mod_init;
	x.post_recovery = cubic_post_recovery;
});

static void
cubic_ack_received(struct cc_var *ccv, uint16_t type)
{
	struct cubic *cubic_data;
	unsigned long w_tf, w_cubic_next;
	int ticks_since_cong;

	cubic_data = (cubic *)ccv->cc_data;
	cubic_record_rtt(ccv);

	/*
	 * Regular ACK and we're not in cong/fast recovery and we're cwnd
	 * limited and we're either not doing ABC or are slow starting or are
	 * doing ABC and we've sent a cwnd's worth of bytes.
	 */
	if (type == CC_ACK && !IN_RECOVERY(CCV(ccv, t_flags)) &&
	    (ccv->flags & CCF_CWND_LIMITED) && (!V_tcp_do_rfc3465 ||
	    CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh) ||
	    (V_tcp_do_rfc3465 && ccv->flags & CCF_ABC_SENTAWND))) {
		 /* Use the logic in NewReno ack_received() for slow start. */
		if (CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh) ||
		    cubic_data->min_rtt_ticks == TCPTV_SRTTBASE)
			newreno_cc_algo.ack_received(ccv, type);
		else {
			ticks_since_cong = bsd_ticks - cubic_data->t_last_cong;

			/*
			 * The mean RTT is used to best reflect the equations in
			 * the I-D. Using min_rtt in the tf_cwnd calculation
			 * causes w_tf to grow much faster than it should if the
			 * RTT is dominated by network buffering rather than
			 * propogation delay.
			 */
			w_tf = tf_cwnd(ticks_since_cong,
			    cubic_data->mean_rtt_ticks, cubic_data->max_cwnd,
			    CCV(ccv, t_maxseg));

			w_cubic_next = cubic_cwnd(ticks_since_cong +
			    cubic_data->mean_rtt_ticks, cubic_data->max_cwnd,
			    CCV(ccv, t_maxseg), cubic_data->K);

			ccv->flags &= ~CCF_ABC_SENTAWND;

			if (w_cubic_next < w_tf)
				/*
				 * TCP-friendly region, follow tf
				 * cwnd growth.
				 */
				CCV(ccv, snd_cwnd) = w_tf;

			else if (CCV(ccv, snd_cwnd) < w_cubic_next) {
				/*
				 * Concave or convex region, follow CUBIC
				 * cwnd growth.
				 */
				if (V_tcp_do_rfc3465)
					CCV(ccv, snd_cwnd) = w_cubic_next;
				else
					CCV(ccv, snd_cwnd) += ((w_cubic_next -
					    CCV(ccv, snd_cwnd)) *
					    CCV(ccv, t_maxseg)) /
					    CCV(ccv, snd_cwnd);
			}

			/*
			 * If we're not in slow start and we're probing for a
			 * new cwnd limit at the start of a connection
			 * (happens when hostcache has a relevant entry),
			 * keep updating our current estimate of the
			 * max_cwnd.
			 */
			if (cubic_data->num_cong_events == 0 &&
			    cubic_data->max_cwnd < CCV(ccv, snd_cwnd))
				cubic_data->max_cwnd = CCV(ccv, snd_cwnd);
		}
	}
}

static void
cubic_cb_destroy(struct cc_var *ccv)
{

	if (ccv->cc_data != NULL)
		free(ccv->cc_data);
}

static int
cubic_cb_init(struct cc_var *ccv)
{
	struct cubic *cubic_data;

	cubic_data = (cubic *)malloc(sizeof(struct cubic));

	if (cubic_data == NULL)
		return (ENOMEM);

	bzero(cubic_data, sizeof(struct cubic));

	/* Init some key variables with sensible defaults. */
	cubic_data->t_last_cong = bsd_ticks;
	cubic_data->min_rtt_ticks = TCPTV_SRTTBASE;
	cubic_data->mean_rtt_ticks = 1;

	ccv->cc_data = cubic_data;

	return (0);
}

/*
 * Perform any necessary tasks before we enter congestion recovery.
 */
static void
cubic_cong_signal(struct cc_var *ccv, uint32_t type)
{
	struct cubic *cubic_data;

	cubic_data = (cubic *)ccv->cc_data;

	switch (type) {
	case CC_NDUPACK:
		if (!IN_FASTRECOVERY(CCV(ccv, t_flags))) {
			if (!IN_CONGRECOVERY(CCV(ccv, t_flags))) {
				cubic_ssthresh_update(ccv);
				cubic_data->num_cong_events++;
				cubic_data->prev_max_cwnd = cubic_data->max_cwnd;
				cubic_data->max_cwnd = CCV(ccv, snd_cwnd);
			}
			ENTER_RECOVERY(CCV(ccv, t_flags));
		}
		break;

	case CC_ECN:
		if (!IN_CONGRECOVERY(CCV(ccv, t_flags))) {
			cubic_ssthresh_update(ccv);
			cubic_data->num_cong_events++;
			cubic_data->prev_max_cwnd = cubic_data->max_cwnd;
			cubic_data->max_cwnd = CCV(ccv, snd_cwnd);
			cubic_data->t_last_cong = bsd_ticks;
			CCV(ccv, snd_cwnd) = CCV(ccv, snd_ssthresh);
			ENTER_CONGRECOVERY(CCV(ccv, t_flags));
		}
		break;

	case CC_RTO:
		/*
		 * Grab the current time and record it so we know when the
		 * most recent congestion event was. Only record it when the
		 * timeout has fired more than once, as there is a reasonable
		 * chance the first one is a false alarm and may not indicate
		 * congestion.
		 */
		if (CCV(ccv, t_rxtshift) >= 2) {
			cubic_data->num_cong_events++;
			cubic_data->t_last_cong = bsd_ticks;
		}
		break;
	}
}

static void
cubic_conn_init(struct cc_var *ccv)
{
	struct cubic *cubic_data;

	cubic_data = (cubic *)ccv->cc_data;

	/*
	 * Ensure we have a sane initial value for max_cwnd recorded. Without
	 * this here bad things happen when entries from the TCP hostcache
	 * get used.
	 */
	cubic_data->max_cwnd = CCV(ccv, snd_cwnd);
}

static int
cubic_mod_init(void)
{

	cubic_cc_algo.after_idle = newreno_cc_algo.after_idle;

	return (0);
}

/*
 * Perform any necessary tasks before we exit congestion recovery.
 */
static void
cubic_post_recovery(struct cc_var *ccv)
{
	struct cubic *cubic_data;

	cubic_data = (cubic *)ccv->cc_data;

	/* Fast convergence heuristic. */
	if (cubic_data->max_cwnd < cubic_data->prev_max_cwnd)
		cubic_data->max_cwnd = (cubic_data->max_cwnd * CUBIC_FC_FACTOR)
		    >> CUBIC_SHIFT;

	if (IN_FASTRECOVERY(CCV(ccv, t_flags))) {
		/*
		 * If inflight data is less than ssthresh, set cwnd
		 * conservatively to avoid a burst of data, as suggested in
		 * the NewReno RFC. Otherwise, use the CUBIC method.
		 *
		 * XXXLAS: Find a way to do this without needing curack
		 */
		if (ccv->curack + CCV(ccv, snd_ssthresh) > CCV(ccv, snd_max))
			CCV(ccv, snd_cwnd) = CCV(ccv, snd_max) - ccv->curack +
			    CCV(ccv, t_maxseg);
		else
			/* Update cwnd based on beta and adjusted max_cwnd. */
			CCV(ccv, snd_cwnd) = bsd_max(1, ((CUBIC_BETA *
			    cubic_data->max_cwnd) >> CUBIC_SHIFT));
	}
	cubic_data->t_last_cong = bsd_ticks;

	/* Calculate the average RTT between congestion epochs. */
	if (cubic_data->epoch_ack_count > 0 &&
	    cubic_data->sum_rtt_ticks >= cubic_data->epoch_ack_count) {
		cubic_data->mean_rtt_ticks = (int)(cubic_data->sum_rtt_ticks /
		    cubic_data->epoch_ack_count);
	}

	cubic_data->epoch_ack_count = 0;
	cubic_data->sum_rtt_ticks = 0;
	cubic_data->K = cubic_k(cubic_data->max_cwnd / CCV(ccv, t_maxseg));
}

/*
 * Record the min RTT and sum samples for the epoch average RTT calculation.
 */
static void
cubic_record_rtt(struct cc_var *ccv)
{
	struct cubic *cubic_data;
	int t_srtt_ticks;

	/* Ignore srtt until a min number of samples have been taken. */
	if (CCV(ccv, t_rttupdated) >= CUBIC_MIN_RTT_SAMPLES) {
		cubic_data = (cubic *)ccv->cc_data;
		t_srtt_ticks = CCV(ccv, t_srtt) / TCP_RTT_SCALE;

		/*
		 * Record the current SRTT as our minrtt if it's the smallest
		 * we've seen or minrtt is currently equal to its initialised
		 * value.
		 *
		 * XXXLAS: Should there be some hysteresis for minrtt?
		 */
		if ((t_srtt_ticks < cubic_data->min_rtt_ticks ||
		    cubic_data->min_rtt_ticks == TCPTV_SRTTBASE)) {
			cubic_data->min_rtt_ticks = bsd_max(1, t_srtt_ticks);

			/*
			 * If the connection is within its first congestion
			 * epoch, ensure we prime mean_rtt_ticks with a
			 * reasonable value until the epoch average RTT is
			 * calculated in cubic_post_recovery().
			 */
			if (cubic_data->min_rtt_ticks >
			    cubic_data->mean_rtt_ticks)
				cubic_data->mean_rtt_ticks =
				    cubic_data->min_rtt_ticks;
		}

		/* Sum samples for epoch average RTT calculation. */
		cubic_data->sum_rtt_ticks += t_srtt_ticks;
		cubic_data->epoch_ack_count++;
	}
}

/*
 * Update the ssthresh in the event of congestion.
 */
static void
cubic_ssthresh_update(struct cc_var *ccv)
{
	struct cubic *cubic_data;

	cubic_data = (cubic *)ccv->cc_data;

	/*
	 * On the first congestion event, set ssthresh to cwnd * 0.5, on
	 * subsequent congestion events, set it to cwnd * beta.
	 */
	if (cubic_data->num_cong_events == 0)
		CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd) >> 1;
	else
		CCV(ccv, snd_ssthresh) = (CCV(ccv, snd_cwnd) * CUBIC_BETA)
		    >> CUBIC_SHIFT;
}
