#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#define INPUTLENGTH 2048



void getInput(char*[], int*, char[], char[], int);
void executeCommand(char*[], int*, struct sigaction, int*, char[], char[]);
void toggleBackgroundMode(int);
void printExitStatus(int);

int allowBackground = 1;

/////////////////////////////////////////////////////////////
// - set handlers for SIGINT and SIGTERM
// - get input using function getInput()
// - handle BUILTIN functions ('cd', 'exit', 'status')
/////////////////////////////////////////////////////////////
int main() 
{

	int pid = getpid();
	int keepGoing = 1;
	int i;
	int exitStatus = 0;
	int background = 0;

	char inputFile[256] = "";
	char outputFile[256] = "";
	char* input[512];
        memset(input, '\0', 512);
	
	

	// Ignore SIGINT ^C
	struct sigaction sa_sigint = {0};
	sa_sigint.sa_handler = SIG_IGN;
	sigfillset(&sa_sigint.sa_mask);
	sa_sigint.sa_flags = 0;
	sigaction(SIGINT, &sa_sigint, NULL);

	// Redirect ^Z to toggleBackgroundMode()
	struct sigaction sa_sigtstp = {0};
	sa_sigtstp.sa_handler = toggleBackgroundMode;
	sigfillset(&sa_sigtstp.sa_mask);
	sa_sigtstp.sa_flags = 0;
	sigaction(SIGTSTP, &sa_sigtstp, NULL);

    //////////////////////////////////////////
    // get input loop
    //////////////////////////////////////////
    while (keepGoing) 
    {
	getInput(input, &background, inputFile, outputFile, pid);
        
        //////////////////////////////////////////////////
        // Check input for BUILTIN command
        //////////////////////////////////////////////////
		// '#' (comment)
		if (input[0][0] == '#' || 	input[0][0] == '\0') 
			continue;
		
		// EXIT
		else if (strcmp(input[0], "exit") == 0) 
			keepGoing = 0;
		
		// CD
		else if (strcmp(input[0], "cd") == 0)
                {
		    if (input[1]) 
                    {
			if (chdir(input[1]) == -1) 
                        {
			    printf("Directory not found.\n");
			    fflush(stdout);
			}
		    } 
                    else 
		        chdir(getenv("HOME"));
		}
		// STATUS
		else if (strcmp(input[0], "status") == 0) 
        	    printExitStatus(exitStatus);

		// normal command execution
		else 
		    executeCommand(input, &exitStatus, sa_sigint, &background, inputFile, outputFile);
		
	        // Reset variables
		memset(input, '\0', 512);
		background = 0;
		inputFile[0] = '\0';
		outputFile[0] = '\0';

        }
	
	return 0;
}

// prints if spawn process exited itself or was killed by a signal
void printExitStatus(int childExitMethod) 
{
	if (WIFEXITED(childExitMethod)) 
		printf("exit value %d\n", WEXITSTATUS(childExitMethod));
	else 
		printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
}

////////////////////////////////////////////////////////////////////////
//INPUT:
//		char*[512] arr		The output array
//		int* isBackground	Is this a isBackground process? boolean
//		char* inputName	    if redirection of STDIN , the input file name
//		char* outputName	if redirection of STDOUT, output filename
//		int pid				PID of smallsh
////////////////////////////////////////////////////////////////////////////
void getInput(char* arr[], int* isBackground, char inputName[], char outputName[], int pid) 
{
	
	char input[INPUTLENGTH];
	int i, j;

	// Get input
	printf(": ");
	fflush(stdout);
	fgets(input, INPUTLENGTH, stdin);

	// Remove newline
	int found = 0;
	for (i=0; !found && i<INPUTLENGTH; i++) 
        {
		if (input[i] == '\n') 
                {
			input[i] = '\0';
			found = 1;
		}
	}

	// If it's blank, return blank
	if (!strcmp(input, "")) 
        {
		arr[0] = strdup("");
		return;
	}

	// Translate rawInput into individual strings
	const char space[2] = " ";
	char *token = strtok(input, space);

	for (i=0; token; i++) 
        {
		// Check for & to be a background process
		if (!strcmp(token, "&")) 
			*isBackground = 1;
		
		// input redirection ? '<'
		else if (!strcmp(token, "<")) 
                {
			token = strtok(NULL, space);
			strcpy(inputName, token);
		}
		// output redirection ? '>'
		else if (!strcmp(token, ">")) 
                {
			token = strtok(NULL, space);
			strcpy(outputName, token);
		}
		
		else 
                {
			arr[i] = strdup(token);
			// Replace $$ with pid
			// Only occurs at end of string in testscirpt
			for (j=0; arr[i][j]; j++) 
                        {
				if (arr[i][j] == '$' && arr[i][j+1] == '$') 
				{
				    arr[i][j] = '\0';
				    snprintf(arr[i], 256, "%s%d", arr[i], pid);
				}
			}
		}
		
		token = strtok(NULL, space);
	}
}


/////////////////////////////////////////////////////////////////////////////////////
// INPUT:
// 	    char* arr[]			    The array with command information
//		int* spawnExitStatus	The success status of the command
//		struct sigaction sa	    The sigaction for SIGINT
//		int* isBackground		Is it a background process? boolean
//		char inputName[]		The name of the input file
//   	char outputName[]		The name of the output file
////////////////////////////////////////////////////////////////////////////////////
void executeCommand(char* arr[], int* spawnExitStatus, struct sigaction sa, int* isBackground, char inputName[], char outputName[]) 
{
	
	int input, output, result;
	pid_t spawnPid = -5;

	spawnPid = fork();
	switch (spawnPid) 
        {
		// error while forking
		case -1:	
			perror("Fork() didn't work !\n");
			exit(1);
			break;
		
                // SPAWN BRANCH
		case 0:	
			// set SIGKILL to default behavior
			sa.sa_handler = SIG_DFL;
			sigaction(SIGINT, &sa, NULL);

			// if input file:
			if (strcmp(inputName, "")) 
                        {
				input = open(inputName, O_RDONLY);
				if (input == -1) 
                                {
					perror("Unable to open input file\n");
					exit(1);
				}
				
                                // set STDIN to input
				result = dup2(input, 0);
				if (result == -1) 
                                {
					perror("Unable to assign input file\n");
					exit(2);
				}
				
                               // close input
				fcntl(input, F_SETFD, FD_CLOEXEC);
			}

			// if output file present in arguments:
			if (strcmp(outputName, "")) 
                        {	
				output = open(outputName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
				if (output == -1) 
                                {
					perror("Unable to open output file\n");
					exit(1);
				}
				
                               // set STDOUT to result
				result = dup2(output, 1);
				if (result == -1) 
                                {
					perror("Unable to assign output file\n");
					exit(2);
				}
				
                                // close output when done
				fcntl(output, F_SETFD, FD_CLOEXEC);
			}
			
			// EXECUTE HIM ! (execvp only returns if an error)
			if (execvp(arr[0], (char* const*)arr)) 
                        {
				printf("%s: no such file or directory\n", arr[0]);
				fflush(stdout);
				exit(2);
			}
			break;
		
		default:	
			
			if (*isBackground && allowBackground) 
                        {
				pid_t actualPid = waitpid(spawnPid, spawnExitStatus, WNOHANG);
				printf("background pid is %d\n", spawnPid);
				fflush(stdout);
			}
			else 
                        {
				pid_t actualPid = waitpid(spawnPid, spawnExitStatus, 0);
			}

		// KILL ALL ZOMBIES	
		while ((spawnPid = waitpid(-1, spawnExitStatus, WNOHANG)) > 0) 
                {
			printf("zombie process PID:%d terminated\n", spawnPid);
			fflush(stdout);
		}
	}
}

//////////////////////////////////////////////////////////////
// catches a SIGSTP (CTRL-Z) and toggles allowBackground flag
///////////////////////////////////////////////////////////////
void toggleBackgroundMode(int signo) 
{

	if (allowBackground == 1) 
        {
		char* message = "Entering foreground-only mode (& is now ignored)\n";
		write(1, message, 49);
		fflush(stdout);
		allowBackground = 0;
	}
	else 
        {
		char* message = "Exiting foreground-only mode\n";
		write (1, message, 29);
		fflush(stdout);
		allowBackground = 1;
	}
}


