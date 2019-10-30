#include <cstdio>
#include <time.h>
#include "kbhit.h"
#include "CConnectionMonitoringThread.h"
#include <mcheck.h>

#define ESC						( 27 )


int main()
{
	mtrace();

	CConnectionMonitoringThread* pcConnectionMonitoringThread = (CConnectionMonitoringThread*)new CConnectionMonitoringThread();
	timespec		tTimeSpec;
	tTimeSpec.tv_sec = 1;
	tTimeSpec.tv_nsec = 0;


	printf("-----[ CConnectionMonitoringThread Demo ]-----\n");
	printf(" [Enter] key : Demo End\n");

	pcConnectionMonitoringThread->Start();

	while (1)
	{
		if (kbhit())
		{
			break;
		}
		nanosleep(&tTimeSpec, NULL);

	}

	pcConnectionMonitoringThread->Stop();

	delete pcConnectionMonitoringThread;

	muntrace();

	return 0;
}