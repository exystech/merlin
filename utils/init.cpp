#include <stdio.h>
#include <sys/wait.h>
#include <libmaxsi/platform.h>
#include <libmaxsi/process.h>
#include <libmaxsi/thread.h>

using namespace Maxsi;

int parent(pid_t childid)
{
	int status;
	waitpid(childid, &status, 0);
	return status;
}

int child()
{
	const char* programname = "sh";
	const char* newargv[] = { programname };

	Process::Execute(programname, 1, newargv);

	return 2;
}

int main(int argc, char* argv[])
{
	// Reset the terminal's color and the rest of it.
	printf("\r\e[m\e[J");

	pid_t childpid = Process::Fork();
	
	if ( childpid < 0 )
	{
		printf("init: unable to fork a child\n");
		return 2;
	}

	return ( childpid == 0 ) ? child() : parent(childpid);
}
