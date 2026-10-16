//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#define	USED

#ifdef _WIN32
#include <windows.h>
#else
// POSIX / Linux threading support
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>

// These priority constants normally come from windows.h; define them
// here so the priority-mapping code below stays platform-agnostic.
#define THREAD_PRIORITY_IDLE		19
#define THREAD_PRIORITY_LOWEST		10
#define THREAD_PRIORITY_NORMAL		0
#endif

#include "cmdlib.h"
#define NO_THREAD_NAMES
#include "threads.h"
#include "pacifier.h"

#define	MAX_THREADS	16


class CRunThreadsData
{
public:
	int m_iThread;
	void *m_pUserData;
	RunThreadsFn m_Fn;
};

CRunThreadsData g_RunThreadsData[MAX_THREADS];


int		dispatch;
int		workcount;
qboolean		pacifier;

qboolean	threaded;
bool g_bLowPriorityThreads = false;

#ifdef _WIN32
HANDLE g_ThreadHandles[MAX_THREADS];
#else
pthread_t g_ThreadHandles[MAX_THREADS];
#endif



/*
=============
GetThreadWork

=============
*/
int	GetThreadWork (void)
{
	int	r;

	ThreadLock ();

	if (dispatch == workcount)
	{
		ThreadUnlock ();
		return -1;
	}

	UpdatePacifier( (float)dispatch / workcount );

	r = dispatch;
	dispatch++;
	ThreadUnlock ();

	return r;
}


ThreadWorkerFn workfunction;

void ThreadWorkerFunction( int iThread, void *pUserData )
{
	int		work;

	while (1)
	{
		work = GetThreadWork ();
		if (work == -1)
			break;
		 
		workfunction( iThread, work );
	}
}

void RunThreadsOnIndividual (int workcnt, qboolean showpacifier, ThreadWorkerFn func)
{
	if (numthreads == -1)
		ThreadSetDefault ();
	
	workfunction = func;
	RunThreadsOn (workcnt, showpacifier, ThreadWorkerFunction);
}


/*
===================================================================

WIN32

===================================================================
*/

int		numthreads = -1;
#ifdef _WIN32
CRITICAL_SECTION		crit;
#else
pthread_mutex_t		crit;
#endif
static int enter;


#ifdef _WIN32
class CCritInit
{
public:
	CCritInit()
	{
		InitializeCriticalSection (&crit);
	}
} g_CritInit;
#else
// On Linux we use a recursive mutex so the existing recursive-lock
// detection logic in ThreadLock/ThreadUnlock keeps working.
class CCritInit
{
public:
	CCritInit()
	{
		pthread_mutexattr_t attr;
		pthread_mutexattr_init( &attr );
		pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
		pthread_mutex_init( &crit, &attr );
		pthread_mutexattr_destroy( &attr );
	}
} g_CritInit;
#endif


void SetLowPriority()
{
#ifdef _WIN32
	SetPriorityClass( GetCurrentProcess(), IDLE_PRIORITY_CLASS );
#else
	// Put the whole process at the lowest nice level so it doesn't hog the UI.
	setpriority( PRIO_PROCESS, 0, 19 );
#endif
}


void ThreadSetDefault (void)
{
	if (numthreads == -1)	// not set manually
	{
#ifdef _WIN32
		SYSTEM_INFO info;
		GetSystemInfo (&info);
		numthreads = info.dwNumberOfProcessors;
#else
		long nProcs = sysconf( _SC_NPROCESSORS_ONLN );
		numthreads = (int)nProcs;
#endif
		if (numthreads < 1 || numthreads > 32)
			numthreads = 1;
	}

	Msg ("%i threads\n", numthreads);
}


void ThreadLock (void)
{
	if (!threaded)
		return;
#ifdef _WIN32
	EnterCriticalSection (&crit);
#else
	pthread_mutex_lock( &crit );
#endif
	if (enter)
		Error ("Recursive ThreadLock\n");
	enter = 1;
}

void ThreadUnlock (void)
{
	if (!threaded)
		return;
	if (!enter)
		Error ("ThreadUnlock without lock\n");
	enter = 0;
#ifdef _WIN32
	LeaveCriticalSection (&crit);
#else
	pthread_mutex_unlock( &crit );
#endif
}


// This runs in the thread and dispatches a RunThreadsFn call.
#ifdef _WIN32
DWORD WINAPI InternalRunThreadsFn( LPVOID pParameter )
#else
void *InternalRunThreadsFn( void *pParameter )
#endif
{
	CRunThreadsData *pData = (CRunThreadsData*)pParameter;
	pData->m_Fn( pData->m_iThread, pData->m_pUserData );
#ifdef _WIN32
	return 0;
#else
	return NULL;
#endif
}

#ifdef _WIN32
// Windows priority mapping is handled inline below via SetThreadPriority.
#else
// Map the engine's priority levels onto Linux nice values for the
// calling thread (requires the thread's kernel tid).
static void SetThreadPriorityPOSIX( int iPriority )
{
	pid_t tid = (pid_t)syscall( SYS_gettid );
	switch ( iPriority )
	{
	case THREAD_PRIORITY_IDLE:
		setpriority( PRIO_PROCESS, tid, 19 );
		break;
	case THREAD_PRIORITY_LOWEST:
		setpriority( PRIO_PROCESS, tid, 10 );
		break;
	default:
		setpriority( PRIO_PROCESS, tid, 0 );
		break;
	}
}
#endif


void RunThreads_Start( RunThreadsFn fn, void *pUserData, ERunThreadsPriority ePriority )
{
	Assert( numthreads > 0 );
	threaded = true;

	if ( numthreads > MAX_TOOL_THREADS )
		numthreads = MAX_TOOL_THREADS;

	for ( int i=0; i < numthreads ;i++ )
	{
		g_RunThreadsData[i].m_iThread = i;
		g_RunThreadsData[i].m_pUserData = pUserData;
		g_RunThreadsData[i].m_Fn = fn;

#ifdef _WIN32
		DWORD dwDummy;
		g_ThreadHandles[i] = CreateThread(
		   NULL,	// LPSECURITY_ATTRIBUTES lpsa,
		   0,		// DWORD cbStack,
		   InternalRunThreadsFn,	// LPTHREAD_START_ROUTINE lpStartAddr,
		   &g_RunThreadsData[i],	// LPVOID lpvThreadParm,
		   0,			// DWORD fdwCreate,
		   &dwDummy );

		if ( ePriority == k_eRunThreadsPriority_UseGlobalState )
		{
			if( g_bLowPriorityThreads )
				SetThreadPriority( g_ThreadHandles[i], THREAD_PRIORITY_LOWEST );
		}
		else if ( ePriority == k_eRunThreadsPriority_Idle )
		{
			SetThreadPriority( g_ThreadHandles[i], THREAD_PRIORITY_IDLE );
		}
#else
		pthread_create(
			&g_ThreadHandles[i],	// pthread_t *
			NULL,					// pthread_attr_t *
			InternalRunThreadsFn,	// void *(*)(void *)
			&g_RunThreadsData[i] );	// void * argument

		if ( ePriority == k_eRunThreadsPriority_UseGlobalState )
		{
			if( g_bLowPriorityThreads )
				SetThreadPriorityPOSIX( THREAD_PRIORITY_LOWEST );
		}
		else if ( ePriority == k_eRunThreadsPriority_Idle )
		{
			SetThreadPriorityPOSIX( THREAD_PRIORITY_IDLE );
		}
#endif
	}
}


void RunThreads_End()
{
#ifdef _WIN32
	WaitForMultipleObjects( numthreads, g_ThreadHandles, TRUE, INFINITE );
	for ( int i=0; i < numthreads; i++ )
		CloseHandle( g_ThreadHandles[i] );
#else
	for ( int i=0; i < numthreads; i++ )
		pthread_join( g_ThreadHandles[i], NULL );
#endif

	threaded = false;
}


/*
=============
RunThreadsOn
=============
*/
void RunThreadsOn( int workcnt, qboolean showpacifier, RunThreadsFn fn, void *pUserData )
{
	int		start, end;

	start = Plat_FloatTime();
	dispatch = 0;
	workcount = workcnt;
	StartPacifier("");
	pacifier = showpacifier;

#ifdef _PROFILE
	threaded = false;
	(*fn)( 0 );
	return;
#endif

	
	RunThreads_Start( fn, pUserData );
	RunThreads_End();


	end = Plat_FloatTime();
	if (pacifier)
	{
		EndPacifier(false);
		printf (" (%i)\n", end-start);
	}
}
