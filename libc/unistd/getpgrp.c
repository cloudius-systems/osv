#include <unistd.h>
#include <osv/pid.h>

pid_t getpgrp(void)
{
	return OSV_PID;
}
