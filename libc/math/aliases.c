#include "libm.h"

double __exp_finite(double x)
{
	return exp(x);
}

double __exp2_finite(double x)
{
	return exp2(x);
}

double __log10_finite(double x)
{
	return log10(x);
}

double __log2_finite(double x)
{
	return log2(x);
}

double __log2f_finite(double x)
{
	return log2f(x);
}

double __log_finite(double x)
{
	return log(x);
}

double __pow_finite(double x, double y)
{
	return pow(x, y);
}
