/** main.cpp
	Copyright (C) 2008 Battelle Memorial Institute
	
	@file main.c
	@author David P. Chassin
 @{
 **/
#define _MAIN_C

#include "gldcore.h"

#include <poll.h>

SET_MYCONTEXT(DMC_MAIN)

/** Implements a pause on exit capability for Windows consoles
 **/
void GldMain::pause_at_exit(void) 
{
	if (global_pauseatexit)
	{
		int rc = 
#if defined WIN32
		system("pause");
#else
		system("read -p 'Press [RETURN] to end... ");
#endif
		if ( rc != 0 )
		{
			fprintf(stderr,"non-zero exit code (rc=%d) from system pause\n",rc);
		}
	}
}

/** The main entry point of GridLAB-D
    @returns Exit codes XC_SUCCESS, etc. (see gridlabd.h)
 **/
GldMain *my_instance = NULL; // TODO: move this to main to make main reentrant
#ifdef HAVE_PYTHON
extern "C" int main_python
#else
int main
#endif
(	int argc, /**< the number entries on command-line argument list \p argv */
	const char *argv[]) /**< a list of pointers to the command-line arguments */
{
	int return_code = XC_SUCCESS;
	try {
		my_instance = new GldMain(argc,argv);
		if ( my_instance == NULL )
		{
			output_error("unable to create new instance");
			return_code = XC_SHFAILED;	
		}
		else
		{
			return_code = my_instance->mainloop(argc,argv);
		}
	}
	catch (const char *msg)
	{
		output_error("%s", msg);
		return_code = XC_EXCEPTION;
	}
	catch (GldException *exc)
	{
		output_error("%s", exc->get_message());
		return_code = XC_EXCEPTION;
	}
	catch (...)
	{
		output_error("unknown exception");
		return_code = XC_EXCEPTION;
	}
	return_code = my_instance->run_on_exit(return_code);
	if ( my_instance != NULL )
	{
		delete my_instance;
		my_instance = NULL;
	}
	if ( global_server_keepalive )
	{
		int stat;
		wait(&stat);
	}
	return return_code;
}
unsigned int GldMain::next_id = 0;
GldMain::GldMain(int argc, const char *argv[])
: 	globals(this), 
	exec(this), 
	cmdarg(this),
	gui(this),
	loader(this)
{
	id = next_id++;
	// TODO: remove this when reetrant code is done
	my_instance = this;

	python_embed_init(argc,argv);

	set_global_browser();

	/* set the default timezone */
	timestamp_set_tz(NULL);

	exec.clock(); /* initialize the wall clock */
	realtime_starttime(); /* mark start */
	
	/* set the process info */
	global_process_id = getpid();
	atexit(pause_at_exit);

#ifdef WIN32
	kill_starthandler();
	atexit(kill_stophandler);
#endif

	/* capture the execdir */
	set_global_execname(argv[0]);
	set_global_execdir(argv[0]);

	/* determine current working directory */
	set_global_workdir();

	/* capture the command line */
	set_global_command_line(argc,argv);

	/* main initialization */
	if (!output_init(argc,argv) || !exec.init())
	{
		exec.mls_done();
		return;
	}		

	/* set thread count equal to processor count if not passed on command-line */
	if (global_threadcount == 0)
		global_threadcount = processor_count();
	IN_MYCONTEXT output_verbose("detected %d processor(s)", processor_count());
	IN_MYCONTEXT output_verbose("using %d helper thread(s)", global_threadcount);

	/* process command line arguments */
	if (cmdarg.load(argc,argv)==FAILED)
	{
		output_fatal("shutdown after command line rejected");
		/*	TROUBLESHOOT
			The command line is not valid and the system did not
			complete its startup procedure.  Correct the problem
			with the command line and try again.
		 */
		exec.mls_done();
		return;
	}

	/* stitch clock */
	global_clock = global_starttime;

	/* initialize scheduler */
	sched_init(0);

	/* recheck threadcount in case user set it 0 */
	if (global_threadcount == 0)
	{
		global_threadcount = processor_count();
		IN_MYCONTEXT output_verbose("using %d helper thread(s)", global_threadcount);
	}

	/* see if newer version is available */
	if ( global_check_version )
		check_version(1);

	/* setup the random number generator */
	random_init();

	/* pidfile */
	create_pidfile();

	/* do legal stuff */
#ifdef LEGAL_NOTICE
	if (strcmp(global_pidfile,"")==0 && legal_notice()==FAILED)
	{
		exec.mls_done();
		return;
	}
#endif
	
	return;
}

GldMain::~GldMain(void)
{
#ifndef HAVE_PYTHON
	python_embed_term();
#endif
	
	// TODO: add general destruction calls
	object_destroy_all();

	// TODO: remove this when reetrant code is done
	my_instance = NULL;

	return;
}

int GldMain::mainloop(int argc, const char *argv[])
{
	/* start the processing environment */
	IN_MYCONTEXT output_verbose("load time: %d sec", realtime_runtime());
	IN_MYCONTEXT output_verbose("starting up %s environment", global_environment);
	if (environment_start(argc,argv)==FAILED)
	{
		output_fatal("environment startup failed: %s", strerror(errno));
		/*	TROUBLESHOOT
			The requested environment could not be started.  This usually
			follows a more specific message regarding the startup problem.
			Follow the recommendation for the indicated problem.
		 */
		if ( exec.getexitcode()==XC_SUCCESS )
			exec.setexitcode(XC_ENVERR);
	}
	return exec.getexitcode();
}

void GldMain::set_global_browser(const char *path)
{
	const char *browser = path ? path : getenv("BROWSER");

	/* specify the default browser */
	if ( browser != NULL )
		strncpy(global_browser,browser,sizeof(global_browser)-1);

}

void GldMain::set_global_execname(const char *path)
{
	strcpy(global_execname,path);
}

void GldMain::set_global_execdir(const char *path)
{
	char *pd1, *pd2;
	strcpy(global_execdir,path);
	pd1 = strrchr(global_execdir,'/');
	pd2 = strrchr(global_execdir,'\\');
	if (pd1>pd2) *pd1='\0';
	else if (pd2>pd1) *pd2='\0';
	return;
}

void GldMain::set_global_command_line(int argc, const char *argv[])
{
	int i, pos=0;
	for (i=0; i<argc; i++)
	{
		if (pos < (int)(sizeof(global_command_line)-strlen(argv[i])))
			pos += sprintf(global_command_line+pos,"%s%s",pos>0?" ":"",argv[i]);
	}
	return;
}

void GldMain::set_global_workdir(const char *path)
{
	if ( path )
		strncpy(global_workdir,path,sizeof(global_workdir)-1);
	else if ( getcwd(global_workdir,sizeof(global_workdir)-1) == NULL )
		output_error("unable to read current working directory");
	return;
}

void GldMain::create_pidfile()
{
	if (strcmp(global_pidfile,"")!=0)
	{
		FILE *fp = fopen(global_pidfile,"w");
		if (fp==NULL)
		{
			output_fatal("unable to create pidfile '%s'", global_pidfile);
			/*	TROUBLESHOOT
				The system must allow creation of the process id file at
				the location indicated in the message.  Create and/or
				modify access rights to the path for that file and try again.
			 */
			exit(XC_PRCERR);
		}
#ifdef WIN32
#define getpid _getpid
#endif
		fprintf(fp,"%d\n",getpid());
		IN_MYCONTEXT output_verbose("process id %d written to %s", getpid(), global_pidfile);
		fclose(fp);
		atexit(delete_pidfile);
	}
	return;
}

void GldMain::delete_pidfile(void)
{
	unlink(global_pidfile);
}

int GldMain::add_on_exit(EXITCALL call)
{
	exitcalls.push_back(call);
	size_t n = exitcalls.size();
	IN_MYCONTEXT output_verbose("add_on_exit(%p) -> %d", call, n);
	return n;
}

int GldMain::add_on_exit(int xc, const char *cmd)
{
	try
	{
		onexitcommand *item = new onexitcommand(xc,cmd);
		exitcommands.push_back(*item);
		size_t n = exitcommands.size();
		IN_MYCONTEXT output_verbose("added on_exit(%d,'%s') -> %d", xc, cmd, n);
		return n;
	}
	catch (...)
	{
		output_error("unable to add on_exit %d '%s'", xc, cmd);
		return 0;
	}
}

int GldMain::run_on_exit(int return_code)
{
	int new_return_code = return_code;
	save_outputs();

	/* save the model */
	if (strcmp(global_savefile,"")!=0)
	{
		if (saveall(global_savefile)==FAILED)
			output_error("save to '%s' failed", global_savefile);
	}

	/* do module dumps */
	if (global_dumpall!=FALSE)
	{
		IN_MYCONTEXT output_verbose("dumping module data");
		module_dumpall();
	}

	/* KML output */
	if (strcmp(global_kmlfile,"")!=0)
		kml_dump(global_kmlfile);

	/* terminate */
	module_termall();

	/* wrap up */
	IN_MYCONTEXT output_verbose("shutdown complete");

	/* profile results */
	if (global_profiler)
	{
		class_profiles();
		module_profiles();
	}

#ifdef DUMP_SCHEDULES
	/* dump a copy of the schedules for reference */
	schedule_dumpall("schedules.txt");
#endif

	/* restore locale */
	locale_pop();

	/* if pause enabled */
#ifndef WIN32
	if (global_pauseatexit)
	{
		IN_MYCONTEXT output_verbose("pausing at exit");
		while (true) {
			sleep(5);
		}
	}
#endif

	/* compute elapsed runtime */
	IN_MYCONTEXT output_verbose("elapsed runtime %d seconds", realtime_runtime());
	IN_MYCONTEXT output_verbose("exit code %d", exec.getexitcode());

	for ( std::list<onexitcommand>::iterator cmd = exitcommands.begin() ; cmd != exitcommands.end() ; cmd++ )
	{
		if ( cmd->get_exitcode() == return_code 
			|| ( return_code != 0 && cmd->get_exitcode() == -1 ) 
			)
		{
			new_return_code = cmd->run() >> 8;
			if ( new_return_code != 0 )
			{
				output_error("on_exit %d '%s' command failed (return code %d)", cmd->get_exitcode(), cmd->get_command(), new_return_code);
				exec.setexitcode(EXITCODE(new_return_code));
				goto Done;
			}
			else
			{
				IN_MYCONTEXT output_verbose("running on_exit(%d,'%s') -> code %d", cmd->get_exitcode(), cmd->get_command(), new_return_code);
			}
		}
	}

	for ( std::list<EXITCALL>::iterator call = exitcalls.begin() ; call != exitcalls.end() ; call++ )
	{
		new_return_code = (*call)(return_code);
		if ( new_return_code != 0 )
		{
			output_error("on_exit call failed (return code %d)", new_return_code);
			exec.setexitcode(EXITCODE(new_return_code));
			goto Done;
		}
		else
		{
			IN_MYCONTEXT output_verbose("exitcall() -> code %d", new_return_code);
		}
	}

Done:
	/* compute elapsed runtime */
	IN_MYCONTEXT output_verbose("elapsed runtime %d seconds", realtime_runtime());
	IN_MYCONTEXT output_verbose("exit code %d", new_return_code);
	// simulation   handler   exitcode
	// ---------   ---------  --------- 
	//      0          0          0
	//      0          Y          Y
	//      X          0          0
	//      X          Y          Y       
	return new_return_code;
}

#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>

static struct pid {
	FILE *output;
	FILE *error;
	pid_t pid;
	struct pid *next;
} *pidlist;

extern char **environ;

/*	Function: popens
	
	Runs a program and connects its stdout and stderr to the FILEs.
	Returns:
		0	success
		-1	failed (see errno)
 */
int popens(const char *program, FILE **output, FILE **error)
{

	struct pid * volatile cur;
	int pdout[2],pderr[2];
	pid_t pid;
	if ( output == NULL && error == NULL )
	{
		errno = EINVAL;
		return -1;
	}
	cur = (struct pid*)malloc(sizeof(struct pid));
	if ( cur == NULL )
	{
		return -1;
	}
	if ( pipe(pdout) < 0 || pipe(pderr) < 0 ) 
	{
		free(cur);
		return -1;
	}
	pid = fork();
	if ( pid == -1 )
	{
		/* error */
		(void)close(pdout[0]);
		(void)close(pdout[1]);
		(void)close(pderr[0]);
		(void)close(pderr[1]);
		free(cur);
		return -1;
	}
	else if ( pid == 0 )
	{
		/* child writes on 1 */
		for ( struct pid *pcur = pidlist ; pcur ; pcur = pcur->next )
		{
			(void)close(fileno(pcur->output));
			(void)close(fileno(pcur->error));
		}
		if ( output ) 
		{
			(void) close(pdout[0]);
			if ( pdout[1] != STDOUT_FILENO ) 
			{
				(void)dup2(pdout[1], STDOUT_FILENO);
				(void)close(pdout[1]);
			}
		}	
		if ( error )
		{
			(void)close(pderr[0]);
			if ( pderr[1] != STDERR_FILENO ) 
			{
				(void)dup2(pderr[1], STDERR_FILENO);
				(void)close(pderr[1]);
			}
		}
		const char *argp[] = {getenv("SHELL"), "-c", program, NULL};
		exit ( execve(_PATH_BSHELL, (char *const*)argp, environ) ? 127 : 0 );
	}
	else
	{
		/* parent reads on 0 */
		if ( output ) 
		{
			*output = fdopen(pdout[0], "r");
			(void)close(pdout[1]);
		}
		if ( error ) 
		{
			*error = fdopen(pderr[0], "r");
			(void)close(pderr[1]);
		}
		/* Link into list of file descriptors. */
		cur->output = ( output ? *output : NULL );
		cur->error = ( error ? *error : NULL );
		cur->pid =  pid;
		cur->next = pidlist;
		pidlist = cur;
		return 0;
	}
}

/*	Function: pcloses
	Waits for the process associated with the stream to terminate and closes its pipes.
 
 	Returns:
 	-1  	if stream is not associated with a `popen3' command, if already closed, or waitpid returns an error.
 	status	if ok
 */
int pcloses(FILE *iop, bool wait=true)
{
	struct pid *cur, *last;
	int pstat;
	pid_t pid;
	/* Find the appropriate file pointer. */
	for ( last = NULL, cur = pidlist ; cur ; last = cur, cur = cur->next )
	{
		if ( cur->output == iop || cur->error == iop )
		{
			break;
		}
	}
	if ( cur == NULL )
	{
		return (-1);
	}
	if ( ! wait )
	{
		if ( cur->output ) (void)fclose(cur->output);
		if ( cur->error ) (void)fclose(cur->error);
		kill(cur->pid,SIGHUP);
		pid = 0;
	}
	else
	{
		do 
		{
			pid = waitpid(cur->pid, &pstat, 0);
		} while ( pid == -1 && errno == EINTR );
		if ( cur->output ) (void)fclose(cur->output);
		if ( cur->error ) (void)fclose(cur->error);
	}

	/* remove the entry from the linked list */
	if (last == NULL)
	{
		pidlist = cur->next;
	}
	else
	{
		last->next = cur->next;
	}
	free(cur);
	return ( pid == -1 ? errno : pstat>>8 );
}

int GldMain::subcommand(const char *format, ...)
{
	char *command;
	va_list ptr;
	va_start(ptr,format);
	if ( vasprintf(&command,format,ptr) < 0 )
	{
		output_error("GldMain::subcommand(format='%s',...): memory allocation failed",format);
		return -1;
	}
	va_end(ptr);

	FILE *output = NULL, *error = NULL;
	int rc = 0;
	if ( popens(command, &output, &error) < 0 ) 
	{
		output_error("GldMain::subcommand(format='%s',...): unable to run command '%s' (%s)",format,command,strerror(errno));
		rc = -1;
	}
	else
	{
		FILE *output_stream = output_get_stream("output");
		FILE *error_stream = output_get_stream("error");
		struct pollfd polldata[3];
		polldata[0].fd = 1;
		polldata[0].events = POLLOUT;
		polldata[1].fd = output ? fileno(output) : 0;
		polldata[1].events = POLLIN|POLLERR|POLLHUP;
		polldata[2].fd = error ? fileno(error) : 0;
		polldata[2].events = POLLIN|POLLERR|POLLHUP;
		char line[1024];
		while ( poll(polldata,sizeof(polldata)/sizeof(polldata[0]),-1) > 0 )
		{
			if ( polldata[1].revents&POLLHUP || polldata[2].revents&POLLHUP)
			{
				break;
			}
			if ( polldata[1].revents&POLLERR || polldata[2].revents&POLLERR)
			{
				output_error("GldMain::subcommand(command='%s'): pipe error", command);
				break;
			}
			// if ( polldata[0].revents&POLLOUT )
			// {
			// 	output_error("GldMain::subcommand(command='%s'): no input", command);
			// 	break;
			// }
			while ( output && polldata[1].revents&POLLIN && fgets(line, sizeof(line)-1, output) != NULL ) 
			{
				fprintf(output_stream,"%s",line);
			}
			while ( error && polldata[2].revents&POLLIN && fgets(line, sizeof(line)-1, error) != NULL ) 
			{
				fprintf(error_stream,"%s",line);
			}
		}
		rc = pcloses(output);
		if ( rc > 0 )
		{
			output_error("GldMain::subcommand(format='%s',...): command '%s' returns code %d",format,command,rc);
		}
	}
	return rc;
}

/** @} **/