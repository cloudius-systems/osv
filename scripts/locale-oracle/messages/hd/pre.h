/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Included first by hd/build.sh: OSv's gettext and libc-message sources built for Linux, their
 * public names prefixed osv_ (the harnesses call glibc's under the plain names). */
#define _GNU_SOURCE 1
#define OSV_CONF_LOCALE 1
#define gettext osv_gettext
#define dgettext osv_dgettext
#define dcgettext osv_dcgettext
#define ngettext osv_ngettext
#define dngettext osv_dngettext
#define dcngettext osv_dcngettext
#define textdomain osv_textdomain
#define bindtextdomain osv_bindtextdomain
#define bind_textdomain_codeset osv_bind_textdomain_codeset
#define strerror osv_strerror
#define strerror_l osv_strerror_l
#define __strerror_l osv___strerror_l
#define strerrordesc_np osv_strerrordesc_np
#define strsignal osv_strsignal
#define gai_strerror osv_gai_strerror
#define regerror osv_regerror
