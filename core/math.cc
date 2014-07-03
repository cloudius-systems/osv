/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <math.h>
#include <osv/types.h>
#include <cmath>
#include <ctgmath>

extern "C"
int __isnan(double v)
{
    return std::isnan(v);
}

extern "C"
int __isnanl(long double v)
{
    return std::isnan(v);
}

extern "C"
int __isinf(double v)
{
    return std::isinf(v);
}

extern "C"
int __isinff(float v)
{
    return std::isinf(v);
}

extern "C"
int __isinfl(double v)
{
    return std::isinf(v);
}
