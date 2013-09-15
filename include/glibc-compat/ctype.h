/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GLIBCCOMPAT_CTYPE_H_
#define GLIBCCOMPAT_CTYPE_H_

// FIXME: for big endian
#define __CTYPE_NTOHS(x) (((1 << x) >> 8) | (((1 << x) & 0xff) << 8))

enum {
  _ISupper = __CTYPE_NTOHS(0),
  _ISlower = __CTYPE_NTOHS(1),
  _ISalpha = __CTYPE_NTOHS(2),
  _ISdigit = __CTYPE_NTOHS(3),
  _ISxdigit = __CTYPE_NTOHS(4),
  _ISspace = __CTYPE_NTOHS(5),
  _ISprint = __CTYPE_NTOHS(6),
  _ISgraph = __CTYPE_NTOHS(7),
  _ISblank = __CTYPE_NTOHS(8),
  _IScntrl = __CTYPE_NTOHS(9),
  _ISpunct = __CTYPE_NTOHS(10),
  _ISalnum = __CTYPE_NTOHS(11),
};

#include_next <ctype.h>


#endif /* CTYPE_H_ */
