#include <unistd.h>
#include <include/osv/pid.h>

pid_t getppid(void)
{
	return OSV_PID;
}
