#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>



struct inputString
{
	
	char **args;                   // pointers to string args
	char inputStr[2048];                 // copy of input string (just in case)
	int  argCount;
	int  redirectOutputArg;         // index of arg '>'
	int  redirectInputArg ;         // index of arg '<'
	int  isComment;
	int  isDoubleMoney   ;         // has at least 1 instance of '$$' in arg list
	int  isExit;                   // first command is 'exit'
	int  isCd;                     //  ''      ''   ''  'cd'
	int  isStatus; 
	int  moneySymbolsArgs[512];        // e.g. moneySymbolsArgs[5]=4 --> 5th argument has '$$$$' in it
	int  isBackground;              // flag indicating last argument was '&'
};

typedef struct inputString InputString ;
int countMoney(char *argument);
void toggleFGonly(int signo);
void moneyChanger(char *argStr, int pid);
int status(InputString inString, int lastExitStatus, int lastKillSignal);
int changeDirectory(InputString inStr);

int  foregroundOnly = 0; 
         
int main()
{
	
	// for $$ expansion
	int pid = getpid();
	char pidStr[6];             
	sprintf(pidStr, "%d", pid);
	fflush(stdout);
	
	// outside prompt loop so status can refer to them
	int  lastExitStatus = 0;
	int  lastKillSignal = 0;
	
	           
	
	// CTRL-C SIGINT handler
	struct sigaction sa_sigint = {0};
	sa_sigint.sa_handler = SIG_IGN;
	sigfillset(&sa_sigint.sa_mask);
	sa_sigint.sa_flags = 0;
	sigaction(SIGINT, &sa_sigint, NULL);

	// CTRL-Z SITSTP handler
	struct sigaction sa_sigtstp = {0};
	sa_sigtstp.sa_handler = toggleFGonly;
	sigfillset(&sa_sigtstp.sa_mask);
	sa_sigtstp.sa_flags = 0;
	sigaction(SIGTSTP, &sa_sigtstp, NULL);
	
	// USER INPUT LOOP
	while (1)
	{
		// initialize inputString struc attributes:
		struct inputString in = {0};
		in.args = (char**) malloc(512 * sizeof(char*));   // 512 arg str references
		in.argCount = 0;
		int runFlag = 0;
		
		// print prompt
		printf(": ");
		fflush(stdout);
		
	    char tmp[2048] = {0}; // for strtok to have its way with...
		fgets(tmp, sizeof tmp, stdin);
		
		strcpy(in.inputStr, tmp);
		fflush(stdin);
		
		// empty input? write prompt again
		if (tmp[0] == '\0' || tmp[0] == '\n') 
			continue;
		
		// leading # ? write prompt again
		if (tmp[0] == '#')       // set in.isComment flag
		{
			in.isComment = 1;
			continue;
		}
		
		
		// parse input string, check for '$' '<' '>'
		char* arg = strtok(tmp, " \n");
		int i = 0;
		while (arg != NULL)
		{
			// mark args with '$$' to expand
			int numMoneySymbols = countMoney(arg);
			if (numMoneySymbols) 
			    in.moneySymbolsArgs[i] = numMoneySymbols;
			
			
			// check for '<', '>' and '&':
			if (*arg == '<') 					{ in.redirectInputArg  = i; }
			if (*arg == '>') 					{ in.redirectOutputArg = i; }
			if (*arg == '&' && strlen(arg)==1)  { in.isBackground      = 1; }
			in.argCount++;
			in.args[i] = (char*) malloc(strlen(arg) * sizeof(char) + 1);  // allocate a string for arg
			sprintf(in.args[i], "%s", arg);
			fflush(stdout);
			
			// expand '$$' to PID for arg:
			char changedStr[64];
			int changedStrLen;
			
			strcpy(changedStr, in.args[i]);
			if (numMoneySymbols) 					// if '$' in any of the args: expand them to PID
				moneyChanger(changedStr, pid);
			changedStrLen = strlen(changedStr);
			
			
			// copy '$$' expanded string back to args[i]
			sprintf(in.args[i], "%s", changedStr);
			fflush(stdout);
			
			arg = strtok(NULL, " \n");
			i++;
		}
		
				
		// check if first arg is a BUILTIN ('exit', 'cd', 'status')
		char *arg0 = in.args[0];
		if (strcmp(arg0, "exit")  ==0) { in.isExit   = 1;} 
		if (strcmp(arg0, "cd")    ==0) { in.isCd     = 1;}
		if (strcmp(arg0, "status")==0) { in.isStatus = 1;}	
		
		//////////////////////////////////////////////////////////////////////
		// dealing with 'builtins' 'cd' 'exit' 'status'
		//////////////////////////////////////////////////////////////////////
		// 'cd' command in first arg
		if      (in.isCd)     
		{ 
			int retVal = changeDirectory(in); 
			if   (retVal) { lastExitStatus = 1; }
			else { lastExitStatus = 0; }
			continue; 
		}
		
		else if (in.isStatus) { status(in, lastExitStatus, lastKillSignal); continue; }
		else if (in.isExit) 
		{
			/////////////////////////////////////////////////
			//////////////////////////////////////////////////
			// terminate children here///////////////////////
			///////////////////////////////////////////////////
			exit(0);
		}
		
		
		/////////////////////////////////////////////////////////////////
		// run child process, parse for input/output redirection, bg mode
		////////////////////////////////////////////////////////////////
		// set detach-on-fork mode off
		// set follow-fork-mode child --> these both to follow child process in gdb
		
		if (420==420)
		{
						
			
			int   spawnStatus;
			
			pid_t spawnPid = fork();
			
			// fork fail
			if (spawnPid == -1)
			{
				perror("fork() failed!\n");
				lastExitStatus = 1; lastKillSignal = 0;
				exit(1);
				
			} 
			// fork successfull:  spawn branch
			else if (spawnPid == 0)
			{
				// enable CTRL-C SIGINT on child process
				sa_sigint.sa_handler = SIG_DFL;
				sigaction(SIGINT, &sa_sigint, NULL);
				
		
				// if background process: remove '&' from args and set 
                // STDIN/STDOUT to /dev/null
				if (in.isBackground && foregroundOnly==0)
				{
					printf("background pid is %d\n", getpid()); 
					fflush(stdout);
					
					in.args[in.argCount-1] = '\0';      // remove "&" from args
					
					
					int devNull = open("/dev/null", O_RDWR);
					if (devNull == -1) 
					{
						printf("devNull did not open!"); 
						fflush(stdout); 
					}
					
					int isDupedin    = dup2(devNull, STDIN_FILENO);
					int isDupedout   = dup2(devNull, STDOUT_FILENO);

					if (isDupedin == -1 || isDupedout == -1) 
                         printf("dup2 failed to dup :( \n"); fflush(stdout);
					
				}
				
				// if '>' or '<' in args, set file descriptors accordingly
				if ((in.redirectOutputArg || in.redirectInputArg))
				{
					if (in.redirectOutputArg)
					{
						int newOutFd = open(in.args[in.redirectOutputArg + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
						if (newOutFd == -1) 
						{
							char* errorMsg = strerror(errno);
							printf("Error: %s\n", errorMsg);
							fflush(stdout);
							lastExitStatus = 1; lastKillSignal = 0; 
							continue;
						}
						
						int isDupedout = dup2(newOutFd, STDOUT_FILENO);   // set stdout to arg after ">"
						if (isDupedout == -1) 
						{ 
							char* errorMsg = strerror(errno);
							printf("Error: %s\n", errorMsg);
							fflush(stdout);
							lastExitStatus = 1; lastKillSignal = 0;
							continue;
						}
						in.args[in.redirectOutputArg] = '\0';       // remove ">" from argument list
					}
					
					if (in.redirectInputArg)
					{
						int newInFd   = open(in.args[in.redirectInputArg + 1], O_RDONLY);
						if (newInFd == -1) 
						{
							char* errorMsg = strerror(errno);
							printf("Error: %s\n", errorMsg);
							fflush(stdout);
							lastExitStatus = 1; lastKillSignal = 0;
							continue;
						}
						int isDupedin = dup2(newInFd, STDIN_FILENO);   // set stdout to arg after ">"
						if (isDupedin == -1) 
						{ 
							char* errorMsg = strerror(errno);
							printf("Error: %s\n", errorMsg);
							fflush(stdout);
							lastExitStatus = 1; lastKillSignal = 0;
							continue;
						}
						in.args[in.redirectInputArg] = '\0';       // remove "<" from argument list
					}
				}
				
				// if foreground only mode: take off '&' arg
				if (foregroundOnly==1)
					if (*in.args[in.argCount-1] == '&')
						in.args[in.argCount-1] = '\0';
						
				runFlag = execvp(in.args[0], in.args);
				// execvp fails ?
				if (runFlag == -1)
				{
					char* errorMsg = strerror(errno);
					printf("Error: %s\n", errorMsg);
					fflush(stdout);
					lastExitStatus = 1; lastKillSignal = 0;
					continue;
				}
				
			} 
			// fork successful: parent branch
			else
			{
				for (int w=0; w<4200; w++) { continue; } // slow down parent a couple microseconds
				
				// background/foreground process ?
				//pid_t spawnPid;
				if (in.isBackground && foregroundOnly==0) 
                    pid_t spawnPid = waitpid(spawnPid, &spawnStatus, spawnPid); 
				
				else { spawnPid = waitpid(spawnPid, &spawnStatus, 0); }
				
				// check normal termination:
				if (WIFEXITED(spawnStatus))
				{
					lastExitStatus = WEXITSTATUS(spawnStatus);
					lastKillSignal = 0;
					if   ( in.isBackground) { printf("background process %d finished. exit status: %d\n", spawnPid, lastExitStatus);}
					//else {                    printf("process %d finished. exit status: %d\n", spawnPid, lastExitStatus); }
					fflush(stdout);
					continue;
				}
				// terminated by signal
				else if (WIFSIGNALED(spawnStatus))
				{
					lastExitStatus = 0;
					lastKillSignal = WTERMSIG(spawnStatus);
					if   ( in.isBackground) { printf("background process %d terminated by signal %d\n", spawnPid, lastKillSignal);}
					else {                    printf("process %d terminated by signal %d\n", spawnPid , lastKillSignal); }
					fflush(stdout);
					continue;
				}
				
				// ZOMBIE KILLER
				// Check for terminated background processes!	
				while   (spawnPid = waitpid(-1, &spawnStatus, spawnPid) > 0) 
				{
				    if    ( in.isBackground) { printf("child %d terminated, exit status: %d\n", spawnPid, lastExitStatus);   }
					else  {                    printf("child %d terminated, exit status: %d\n", spawnPid, lastExitStatus); }
					fflush(stdout);
				}
				
				continue;
				
			}
			//printf("The process with pid %d is returning from main\n", getpid());
			//fflush(stdout);
			continue;
		}
		
			
		// free malloc'ed strings in struct
		for (int j=0; j < in.argCount; j++)
			free(in.args[j]);
		free(in.args);
		
		return 0;
	
		
	}			
}			


/** counts instances of '$' in input string */
int countMoney(char *argument)
{
	char* charPtr = argument;
    int moneySymbols = 0;              // number of '$' chars in 'argument'
	while ( *(charPtr) !=  '\0') 
	{ 
		if (*(charPtr) == '$') { moneySymbols++; }
		charPtr++;
	}
	return moneySymbols;
}

/** toggle 'foreground only mode' by CTRL-Z (SIGTSTP)' */
void toggleFGonly(int signo) {

	if (foregroundOnly == 0) 
	{
		char* msg = "Entering foreground-only mode (& is now ignored)\n";
		write(1, msg, 49);
		fflush(stdout);
		foregroundOnly = 1;
	}

	else 
	{
		char* msg = "Exiting foreground-only mode\n";
		write (1, msg, 29);
		fflush(stdout);
		foregroundOnly = 0;
	}
}

/** input:  string with a number of '$' chars in it
*   output: string with each instance of '$$' turned into PID of main()
*       e.g. if getPid()--> 420; moneyChanger('l337h4x0r$$') --> 'l337h4x0r420'
**/
void moneyChanger(char *argStr, int pid)
{
	int count = 0;            			  // number of occurrences of '$$'
	int lenArgStr = strlen(argStr);
	int indices[2048] = {0};		  // if indicies[5] == 1, then there's a '$$' at 6th char of argStr
	int index;
	char* ptr = argStr;
	while(ptr = strstr(ptr, "$$"))
	{
		// if we have two chars to compare
		if (*(ptr+1) != '\0')
		{
			index = (int)(ptr - argStr);  // current index of ptr.  AIN'T THAT FANCY !? THANKS 271!
			indices[index] = 1;
			ptr += 2;
		    count++;
		}
	}
	
	// get number of digits in PID to calculate replacement str length
	int temp = pid;
	int lengthPID = 0;
	while (temp)
	{
		temp = temp / 10;
		lengthPID++;
	}
	
	// build outString by changing '$$' into PID
	int j=0;   // counter for outString  
	int k=0;   // counter for argString
	char pidStr[lengthPID+1];
	pidStr[lengthPID] = '\0';
	sprintf(pidStr, "%d", pid);  // 420 --> "420"
	fflush(stdout);
	
	// output string length = original length - 2chars per count of '$$' + count * substitutions of PID
	int outStringLen = lenArgStr - 2 * count + count * lengthPID + 1;  // +1 null term.
	char outString[outStringLen];
	
	outString[outStringLen-1] = '\0';
	//printf("outString: %s", outString);
	
	ptr = outString;
	while (j < outStringLen-1)
	{
		// if '$$' at string index, copy PID there
		if (indices[k]) 
		{ 
			strncpy(outString + j, pidStr, lengthPID);
			ptr += lengthPID;
			j   += lengthPID;
			k   += 2;
		}
		// else copy the next char from argStr
		else
		{
			strncpy(outString + j, argStr + k, 1);
			j++;
			k++;
		}
	}

	
	strcpy(argStr, outString);
}

/** status
* if no foreground process has been run
* besides ('cd','exit' or 'status'
* status returns 0, else it returns
* the exit status or terminating signal
* of the last fg command run
**/
int status(InputString inString, int lastExitStatus, int lastKillSignal)
{
	if (lastExitStatus)
	{
		printf("exit value: %d\n", lastExitStatus);
		fflush(stdout);
		return lastExitStatus;
	}
	else if (lastKillSignal)
	{
		printf("terminated by signal: %d\n", lastKillSignal);
		fflush(stdout);
		return lastKillSignal;
	}
	else
	{
		printf("exit value 0\n");
		fflush(stdout);
	}
	
	return 0;
}

/** changeDirectory
* handles chdir() with or without an arg
* trying to make main() a little shorter
* RETURN:  0 for success, -1 for failure!
**/
int changeDirectory(InputString inStr)
{
	
	// just 'cd' no other arguments:
	if (inStr.argCount == 1)
	{
		
		
		//char *getenv(const char *name);   return= pointer to value specified in *name
		char homePath[420];
		//fprintf(stderr, "getenv(\"HOME\")=%s\n", getenv("HOME")); 
		
		sprintf(homePath, "%s", getenv("HOME"));
		fflush(stdout);
		
		
		// int chdir(const char *path);     return= 0 success, -1 FAIL
		int success;
		if (success = chdir(homePath) == -1)
		{
			perror("'cd' FAIL, your HOME variable is prolly set wrong\n");
			fflush(stderr);
			return 1;
		}
		
	}
	else    // change to specified directory
	{
		// user tries to pull a fast one and do 'cd arg1 arg2 ...'
		if (inStr.argCount > 2) 
		{ 
			printf("cd takes 0 or 1 argument\n"); 
			fflush(stdout);
			return 1; 
		}
		
		
		if (chdir(inStr.args[1]) == -1) 
		{ 
			perror("invalid directory\n"); 
			fflush(stderr); 
			return 1;
		}
		else { return 0; }
		
	}
}

