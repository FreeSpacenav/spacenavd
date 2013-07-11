#ifdef WIN32
#include <stdio.h>
#include <windows.h>

static void WINAPI svc_main(unsigned long argc, char **argv);
static unsigned long WINAPI svc_handler(unsigned long op, unsigned long evtype, void *evdata, void *cls);

static SERVICE_STATUS_HANDLE svc;

int main(int argc, char **argv)
{
	SERVICE_TABLE_ENTRY serv_tbl[] = {
		{"", svc_main},
		{0, 0}
	};

	if(!StartServiceCtrlDispatcher(serv_tbl)) {
		fprintf(stderr, "failed to start the spacenavd service\n");
		return 1;
	}
	return 0;
}


static void WINAPI svc_main(unsigned long argc, char **argv)
{
	SERVICE_STATUS status;

	svc = RegisterServiceCtrlHandlerEx("spacenavd", svc_handler, 0);

	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwCurrentState = SERVICE_START_PENDING;
	status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	status.dwWin32ExitCode = 0;
	status.dwCheckPoint = 0;
	status.dwWaitHint = 2000;
	SetServiceStatus(svc, &status);

	/* init */

	status.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(svc, &status);
}

static unsigned long WINAPI svc_handler(unsigned long op, unsigned long evtype, void *evdata, void *cls)
{
	printf("received service control message: %ld\n", op);
	switch(op) {
	case SERVICE_CONTROL_INTERROGATE:
		return 0;

	case SERVICE_CONTROL_STOP:
		exit(0);

	default:
		break;
	}
	return ERROR_CALL_NOT_IMPLEMENTED;
}
#else
int spnavd_win32_silcence_empty_file_warnings = 1;
#endif
