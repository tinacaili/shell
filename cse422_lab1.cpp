// cse422_project1.cpp : Defines the entry point for the console application.
//Authors:
//Tina Li
//Megan Bacani

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"
#include <stdio.h>
#include <dirent.h>

#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
* Jobs states: FG (foreground), BG (background), ST (stopped)
* Job state transitions and enabling actions:
*     FG -> ST  : ctrl-z
*     ST -> FG  : fg command
*     ST -> BG  : bg command
*     BG -> FG  : fg command
* At most 1 job can be in the FG state.
*/

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "xsh >> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
	pid_t pid;              /* job PID */
	int jid;                /* job ID [1, 2, ...] */
	int state;              /* UNDEF, BG, FG, or ST */
	char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

void eval(char * cmdline);
int parseline(string cmdline, vector<string> & argv, char ** argv_a);
int builtin_cmd(vector<string> & argv);
int waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void sigcont_handler(int sig);

bool is_number(const string& s);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

int spawn_proc(int in, int out, struct command *cmd);
int fork_pipes(int n, struct command *cmd);



void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

vector<string> cmdHistory;
map<string, string> local_variables;

//debugging mode, set to true just while we're debugging, otherwise start at false
bool x_flag;
vector<string> cmds_pipe;

//global variables to look for stdout redirection
string file;
bool isOut;

struct command
{
	char **argv;
};

int last_pid = 0;
vector<pid_t> bg_job_pids;

int main(int argc, char * argv[])
{
	char c;
	char cmdline[MAXLINE] = { 0 };
	int emit_prompt = 1; /* emit prompt (default) */
	x_flag = false;

	/* Redirect stderr to stdout (so that driver will get all output
	* on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "xdf:")) != EOF) {
		switch (c) {
		case 'x':   /* print help message */
		{
			x_flag = true;
			break;
		}
		case 'd':    /* emit additional diagnostic info -- debugging flag */
		{
			verbose = 1;
			break;
		}
		case 'f':	/* flag to read from a file */
		{
			vector<string> cmds;
			ifstream ifs(optarg);
			if (ifs.is_open())
			{
				string nextcmd;

				//reads file and evals each line 
				while (getline(ifs, nextcmd))
				{
						cout << "xsh >> " << nextcmd << endl;

					//parsing command for a ! to indicate bg job
					char * argv2[MAXARGS] = { 0 };
					vector<string> argv_v2;
					int bg = parseline(nextcmd, argv_v2, argv2);


					//removing the < 
					nextcmd.erase(std::remove(nextcmd.begin(), nextcmd.end(), '<'), nextcmd.end());

					cmds.push_back(nextcmd);

					//save command into a char * array to be put through eval, look for stdout redirection
					char* arr = new char[nextcmd.length() + 1];
					for (int i = 0; i < nextcmd.length(); ++i)
					{
						arr[i] = nextcmd[i];
						if (nextcmd[i] == '>')
						{
							isOut = true;
						}
					}
					arr[nextcmd.length()] = '\0';

					//if found stdout redirection, grab file name to write out to and send rest to eval
					string a;
					if (isOut)
					{
						if (bg)
						{
							//remove the ! if it is a bg to parse for std out redirection
							nextcmd.erase(std::remove(nextcmd.begin(), nextcmd.end(), '!'), nextcmd.end());
						}

						bool isFirst = true;
						stringstream ss2(nextcmd);
						string arg;
						vector<string> args;

						while (getline(ss2, arg, '>'))
						{
							args.push_back(arg);

							//remove the stdin char
							if (isFirst)
							{
								a = arg;
								a.erase(std::remove(a.begin(), a.end(), '<'), a.end());
								isFirst = false;
							}
						}

						//erase all extra spaces and EOF chars from filename
						file = args.back();
						file.erase(std::remove(file.begin(), file.end(), ' '), file.end());
						stringstream ss3(file);
						string f;
						bool first = true;
						while (getline(ss3, f))
						{
							f.erase(std::remove(f.begin(), f.end(), ' '), f.end());

							if (first)
							{
								file = f;
								first = false;
							}
						}
						args.pop_back();
						char* arr = new char[a.length() + 1];
						for (int i = 0; i < a.length(); ++i)
						{
							arr[i] = a[i];
						}
						arr[a.length()] = '\0';

						//remove end of line char
						file.pop_back();

						//adds ! to the end of the command if it was a bg job
						if (bg)
						{
							a += " !";
						}

						//convert to a char* array to be passed into eval
						char* arr1 = new char[a.length() + 1];
						for (int i = 0; i < a.length(); ++i)
						{
							arr1[i] = a[i];
						}
						arr1[a.length()] = '\0';

						//evaluate each line as a separate command 
						eval(arr1);
						delete[] arr1;
					}
					//if it does not have a stdout redirection, eval cmd as it was
					else
					{
						eval(arr);
						delete[] arr;
					}

					//allows the commands in the file be processed after the last command's job finishes
					for (int i = 0; i < 20000000; i++);
				}


			}
			else
			{
				cout << "File can't be opened" << endl;
			}
			ifs.close();
			break;
		}
		default:
		{
			cout << "Default" << endl;
		}
		}
	}



	/* Install the signal handlers */


	/* These are the ones you will need to implement */
	Signal(SIGINT, sigint_handler);   /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	Signal(SIGCONT, sigcont_handler);  /* ctrl-q */
	Signal(SIGCHLD, sigchld_handler); /* reaps child processes properly*/
	Signal(SIGQUIT, sigquit_handler); /* ctrl-s */

	/* Initialize the job list */
	initjobs(jobs);

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
			cout << "error " << endl;
		}
		if (feof(stdin)) { /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		isOut = false;

		//parses commandline to look for bg job
		char * argv3[MAXARGS] = { 0 };
		vector<string> argv_v3;
		int bg = parseline(cmdline, argv_v3, argv3);
		string temp = cmdline;


		//remove < for stdin redirection
		temp.erase(std::remove(temp.begin(), temp.end(), '<'), temp.end());

		//move to a char* and look for a stdout redirection
		char* arr = new char[temp.length() + 1];
		for (int i = 0; i < temp.length(); ++i)
		{
			arr[i] = temp[i];
			if (temp[i] == '>')
			{
				isOut = true;
			}
		}
		arr[temp.length()] = '\0';

		string a;
		//if found a stdout redirection, split string by file name and command
		if (isOut)
		{
			if (bg)
			{
				temp.erase(std::remove(temp.begin(), temp.end(), '!'), temp.end());
			}
			bool isFirst = true;
			stringstream ss3(temp);
			string arg;
			vector<string> args;

			while (getline(ss3, arg, '>'))
			{
				args.push_back(arg);

				//save first arg to go thru eval
				if (isFirst)
				{
					a = arg;
					isFirst = false;
				}
			}

			//remove all spaces and extra chars from file name
			file = args.back();
			file.erase(std::remove(file.begin(), file.end(), ' '), file.end());
			stringstream ss4(file);
			string f;
			bool first = true;
			while (getline(ss4, f))
			{
				f.erase(std::remove(f.begin(), f.end(), ' '), f.end());

				if (first)
				{
					file = f;
					first = false;
				}
			}
			args.pop_back();

			//convert into char* array
			char* arr = new char[a.length() + 1];
			for (int i = 0; i < a.length(); ++i)
			{
				arr[i] = a[i];
			}
			arr[a.length()] = '\0';

			//add the ! back into the end of the command if bg
			if (bg)
			{
				a += " !";
			}

			char* arr1 = new char[a.length() + 1];
			for (int i = 0; i < a.length(); ++i)
			{
				arr1[i] = a[i];
			}
			arr1[a.length()] = '\0';

			//evaluate each line as a separate command 
			eval(arr1);
			delete[] arr1;
		}
		else
		{
			eval(arr);
		}

		fflush(stdout);
		fflush(stdout);
	}


	exit(0); /* control never reaches here */
	return 0;

}

int spawn_proc(int in, int out, struct command *cmd)
{
	pid_t pid;
	if ((pid = fork()) == 0){
		if (in != 0){
			dup2(in, 0);
			close(in);
		}
		if (out != 1){
			dup2(out, 1);
			close(out);
		}
		return execvp(cmd->argv[0], (char * const *)cmd->argv);
	}
	return pid;
}

int fork_pipes(int n, struct command *cmd)
{
	int i;
	pid_t pid;
	int in, fd[2];

	/* The first process should get its input from the original file descriptor 0.  */
	in = 0;
	/* Note the loop bound, we spawn here all, but the last stage of the pipeline.  */
	for (i = 0; i < n - 1; ++i)
	{
		pipe(fd);
		/* f [1] is the write end of the pipe, we carry `in` from the prev iteration.  */
		spawn_proc(in, fd[1], cmd + i);
		/* No need for the write end of the pipe, the child will write here.  */
		close(fd[1]);
		/* Keep the read end of the pipe, the next child will read from there.  */
		in = fd[0];
	}

	/* Last stage of the pipeline - set stdin be the read end of the previous pipe
	and output to the original file descriptor 1. */
	if (in != 0)
		dup2(in, 0);

	//moves file string into a char* array 
	char* arr3 = new char[file.length() + 1];
	for (int i = 0; i < file.length(); ++i)
	{
		arr3[i] = file[i];
	}
	arr3[file.length()] = '\0';

	//opesn file and redirects stdout into there if isOut
	int defout;
	int fd1;
	if (isOut)
	{
		defout = dup(1);
		//creates new file with all permissions
		fd1 = open(arr3, O_RDWR | O_CREAT | O_TRUNC, 0777);
		dup2(fd1, 1);
	}

	/* Execute the last stage with the current process. */
	int return_level = execvp(cmd[i].argv[0], (char * const *)cmd[i].argv);

	//redirect back to normal stdout
	if (isOut){
		dup2(defout, 1); // redirect output back to stdout
		close(fd1);
		close(defout);
	}
	return return_level;
}


void eval(char * cmdline){
	/* masks following signals so they are ignored*/
	sigset_t mask_rest;
	sigemptyset(&mask_rest);
	sigaddset(&mask_rest, SIGABRT);
	sigaddset(&mask_rest, SIGALRM);
	sigaddset(&mask_rest, SIGHUP);
	sigaddset(&mask_rest, SIGTERM);
	sigaddset(&mask_rest, SIGUSR1);
	sigaddset(&mask_rest, SIGUSR2);
	if (sigprocmask(SIG_BLOCK, &mask_rest, NULL) < 0) cout << "Error with sigprocmask" << endl;

	if (verbose)
	{
		cout << "Evaluating: " << cmdline << endl;
	}

	cmdHistory.push_back(cmdline);
	//sends cmdline through parseline
	char * argv[MAXARGS] = { 0 };
	vector<string> argv_v;
	int bg = parseline(cmdline, argv_v, argv);
	pid_t pid;

	//looking for pipes
	bool isPipe = false;
	int num_pipes = 0;
	for (int i = 0; cmdline[i] != '\0'; ++i){
		if (cmdline[i] == '|'){
			isPipe = true;
			num_pipes++;
		}
	}

	num_pipes++;

	//create string stream to separate by pipes 
	string cmdlineString = cmdline;
	stringstream ss(cmdlineString);
	string cmd;
	vector<string>::iterator it;

	string last;

	//while loop to separate commands by pipes and run eval on each 
	//close output and reset to normal standard out 
	while (getline(ss, cmd, '|')){
		cmds_pipe.push_back(cmd);
	}

	//pushes back each piped string into a vector
	if (!builtin_cmd(argv_v)){
		//if there's pipes, formats the char array correctly to be piped
		vector<string> pipes_strings;
		string substring = "";
		for (int i = 0; i < argv_v.size(); i++){
			if (argv_v[i] == "|"){
				pipes_strings.push_back(substring);
				substring = "";
			}
			else if (argv_v[i] != "<"){
				substring = substring + " " + argv[i];
			}
		}
		pipes_strings.push_back(substring);

		struct command * cmd_pipes = new command[num_pipes];
		int count_pipes = 0;
		for (string str_ls : pipes_strings){
			string tmp_cmd;
			stringstream ss_cmd(str_ls);
			vector<string> broken_strings;
			int num_words_string = 0;
			while (ss_cmd >> tmp_cmd){
				broken_strings.push_back(tmp_cmd);
				num_words_string++;
			}

			char ** char_ls = new char *[num_words_string + 1];

			int char_array_counter = 0;
			for (string bs : broken_strings)
			{
				char* arr = new char[bs.length() + 1];
				for (int i = 0; i < bs.length(); ++i)
				{
					arr[i] = bs[i];
				}
				arr[bs.length()] = '\0';

				char_ls[char_array_counter] = arr;
				char_array_counter++;
			}
			char_ls[char_array_counter] = 0;

			cmd_pipes[count_pipes] = { char_ls };
			count_pipes++;
		}

		/* If is foreground job */
		if (!bg)
		{
			//if is pipe
			if (isPipe){
				if ((pid = fork()) == 0)
				{
					setpgid(0, 0);
					/* Executes the job in the child's process. execvpe allows for the environment
					* to be passed in and checks the paths in the environ if the path isn't found
					* in the immediate directory.
					*/
					fork_pipes(num_pipes, cmd_pipes);
				}
				/* In parent process */
				if (pid > 0)
				{
					/* Adds the child job into list of jobs to make it easier to find foreground jobs */
					addjob(jobs, pid, FG, cmdline); //Doing in parent for now
					string fg_return_int = to_string(waitfg(pid));
					string fg_return = "fg_return";
					char * buffer = getenv(fg_return.c_str());
					if (buffer == NULL)
					{
						setenv(fg_return.c_str(), fg_return_int.c_str(), 0);
					}
					else {
						setenv(fg_return.c_str(), fg_return_int.c_str(), 1);
					}
					return;
				}
			}
			else{ //if not pipe
				/* Forks a child */
				if ((pid = fork()) == 0)
				{
					setpgid(0, 0);

					/* Executes the job in the child's process. execvpe allows for the environment
					* to be passed in and checks the paths in the environ if the path isn't found
					* in the immediate directory.
					*/

					//redirects stdout to a file
					int defout;
					int fd3;
					if (isOut)
					{
						//convert file string into a char* array 
						char* arr5 = new char[file.length() + 1];
						for (int i = 0; i < file.length(); ++i)
						{
							arr5[i] = file[i];
						}
						arr5[file.length()] = '\0';
						defout = dup(1);

						//opens new file with all permissions
						fd3 = open(arr5, O_RDWR | O_CREAT | O_TRUNC, 0777);
						dup2(fd3, 1);
					}

					if (execvpe(argv[0], argv, environ) < 0){
						cout << argv[0] << ": command not found" << endl;
						exit(0);
					}
					// redirect output back to stdout
					if (isOut){
						dup2(defout, 1);
						close(fd3);
						close(defout);
					}
				}
				/* In parent process */
				if (pid > 0)
				{
					/* Adds the child job into list of jobs to make it easier to find foreground jobs */
					addjob(jobs, pid, FG, cmdline); //Doing in parent for now
					string fg_return_int = to_string(waitfg(pid));
					string fg_return = "fg_return";
					char * buffer = getenv(fg_return.c_str());
					if (buffer == NULL)
					{
						setenv(fg_return.c_str(), fg_return_int.c_str(), 0);
					}
					else {
						setenv(fg_return.c_str(), fg_return_int.c_str(), 1);
					}
					return;
				}
			}
		}
		else{ // background process
			if (isPipe){
				if ((pid = fork()) == 0)
				{
					setpgid(0, 0);
					/* Executes the job in the child's process. execvpe allows for the environment
					* to be passed in and checks the paths in the environ if the path isn't found
					* in the immediate directory.
					*/
					fork_pipes(num_pipes, cmd_pipes);
				}
				/* In parent process */
				if (pid > 0)
				{
					//sets environment variable
					string bg_pid = "bg_pid";
					char * buffer = getenv(bg_pid.c_str());
					string bg_pid_int = to_string(pid);
					if (buffer == NULL)
					{
						setenv(bg_pid.c_str(), bg_pid_int.c_str(), 0);
					}
					else {
						setenv(bg_pid.c_str(), bg_pid_int.c_str(), 1);
					}
					/* Adds the child job into list of jobs to make it easier to find foreground jobs */
					bg_job_pids.push_back(pid);
					addjob(jobs, pid, BG, cmdline); //Doing in parent for now
					return;
				}
			}
			else{
				/* Forks a child */
				if ((pid = fork()) == 0)
				{
					setpgid(0, 0);

					/* Executes the job in the child's process. execvpe allows for the environment
					* to be passed in and checks the paths in the environ if the path isn't found
					* in the immediate directory.
					*/

					//redirects stdout to file if > was found
					int defout;
					int fd2;
					if (isOut)
					{
						//converts file string to char* array 
						char* arr4 = new char[file.length() + 1];
						for (int i = 0; i < file.length(); ++i)
						{
							arr4[i] = file[i];
						}
						arr4[file.length()] = '\0';
						defout = dup(1);

						//opens new file with all permissions
						fd2 = open(arr4, O_RDWR | O_CREAT | O_TRUNC, 0777);
						dup2(fd2, 1);
					}

					if (execvpe(argv[0], argv, environ) < 0){
						cout << argv[0] << ": command not found" << endl;
						exit(0);
					}

					if (isOut){
						dup2(defout, 1); // redirect output back to stdout
						close(fd2);
						close(defout);
					}

				}
				/* In parent process */
				if (pid > 0)
				{
					//sets bg environment variable
					string bg_pid = "bg_pid";
					char * buffer = getenv(bg_pid.c_str());
					string bg_pid_int = to_string(pid);
					if (buffer == NULL)
					{
						setenv(bg_pid.c_str(), bg_pid_int.c_str(), 0);
					}
					else {
						setenv(bg_pid.c_str(), bg_pid_int.c_str(), 1);
					}
					/* Adds the child job into list of jobs to make it easier to find foreground jobs */
					bg_job_pids.push_back(pid);
					addjob(jobs, pid, BG, cmdline); //Doing in parent for now
					return;
				}
			}
		}
	}

	return;
}

/* Parses the command line, removes unnessary characters, removes whitespace, determines if background job
* and stores it in a vector<string> to pass into the builtin_cmd to make life easier and
* char** to pass into the exec methods in eval
*/
int parseline(string cmdline, vector<string> & argv, char ** argv_a){
	int bg;

	/* find and replace all \ with spaces */
	replace(cmdline.begin(), cmdline.end(), '\'', ' ');

	/* pushing arguments into argv */
	istringstream iss(cmdline);
	string cmdstring;

	//stops getting the string after # is seen
	getline(iss, cmdstring, '#');
	istringstream c_iss(cmdstring);
	string temp;
	while (c_iss >> temp){
		argv.push_back(temp);
	}

	if (argv.size() == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background?*/
	if ((bg = (argv[argv.size() - 1] == "!")) != 0) {
		argv.pop_back();
	}

	/* removing leading and trailing spaces everywhere */
	for (string & str : argv){
		size_t first = str.find_first_not_of(' ');
		size_t last = str.find_last_not_of(' ');
		str = str.substr(first, (last - first + 1));

		//if the command is not echo, replace variables
		if (argv[0].compare("echo")){
			if (str[0] == '$'){
				string temp = str.substr(1, str.length() - 1);
				bool is_found = false;
				string path = (getenv(&temp[0]) != NULL) ? getenv(&temp[0]) : "";
				if (!path.empty()){
					str = path;
					is_found = true;
				}
				if (!is_found && local_variables.find(temp) != local_variables.end()){
					is_found = true;
					str = local_variables[temp];
				}
				if (temp == "$"){
					str = to_string(getpid());
				}
				if (temp == "?"){
					string fg = "fg_return";
					str = (getenv(&fg[0]) != NULL) ? getenv(&fg[0]) : "$?";
				}
				if (temp == "!"){
					string bg = "bg_pid";
					str = (getenv(&bg[0]) != NULL) ? getenv(&bg[0]) : "$!";
				}
			}
		}
		//remove the " in echo command
		else {
			string final_str = "";
			for (char c : str){
				if (c != '\"'){
					final_str += c;
				}
			}
			str = final_str;
		}
		//prints out command with replaced variables if need be
		if (x_flag){
			cout << str << " ";
		}
	}
	if (x_flag){
		if (bg != 0) cout << "!";
		cout << endl;
	}


	/* converts vector<string> to char**
	* are passed into the exec methods
	*/
	for (int i = 0; i < argv.size(); i++){
		char * a = new char[argv[i].length() + 1];
		for (int j = 0; j < argv[i].length(); j++)
		{
			a[j] = argv[i][j];
		}
		a[argv[i].length()] = '\0';
		argv_a[i] = a;
	}

	return bg;
}


/* Determines if the string passed in is a number */
bool is_number(const string& s)
{
	if (s[0] == '-')
		return !s.empty() && find_if(s.begin() + 1, s.end(), [](char c) { return !isdigit(c); }) == s.end();
	else
		return !s.empty() && find_if(s.begin(), s.end(), [](char c) { return !isdigit(c); }) == s.end();
}

/* Evaluates the commandline first internally to make sure it's not an internal command */
int builtin_cmd(vector<string> & argv){
	if (argv.empty()){
		return 1;
	}

	string cmd = argv[0];

	/* exit the shell and returns status in second cmd. If non is given, exit with status code of 0 */
	if (!cmd.compare("exit")){
		for (pid_t pid : bg_job_pids){
			kill(pid, SIGKILL);
		}
		if (argv.size() == 2){
			string str_code = argv[1];

			for (char c : str_code){
				if (!isdigit(c)){
					cout << "Not a valid exit code" << endl;
					exit(-1);
				}
			}
			int exit_code = stoi(argv[1]);
			cout << "Exiting " << exit_code << endl;
			exit(exit_code);
		}

		cout << "Exiting normally" << endl;
		exit(0);
	}

	/* pause the operation of the shell until 'Enter' key is pressed */
	else if (cmd == "pause")
	{
		cout << "should be ignoring" << endl;
		//FIXME Control C still goes through 
		cin.ignore();
		return 1;
	}

	/* clear the screen and display a new command line prompt at the top */
	else if (!cmd.compare("clr")){
		printf("\033c");
		return 1;
	}

	/* display <comment> on the stdout followed by a new line */
	else if (!cmd.compare("echo")){
		bool notEcho = false;
		for (string s : argv){
			if (!notEcho) notEcho = true;
			else cout << s << " ";
		}
		cout << endl;
		return 1;
	}

	/* display W1 W2... on the stdout followed by a new line and replaces substitutions */
	else if (!cmd.compare("show")){
		bool notShow = false;
		for (string s : argv){
			if (!notShow) notShow = true;
			else cout << s << " ";
		}
		cout << endl;
		return 1;
	}

	/* list the contents of the current directory */
	else if (!cmd.compare("dir")){
		DIR *dir;
		struct dirent *entries;

		dir = opendir("./");
		if (dir != NULL)
		{
			//reads each entry in current directory
			while (entries = readdir(dir))
			{
				cout << entries->d_name << endl;
			}
		}
		return 1;
	}

	/* list the contents of the current directory */
	else if (!cmd.compare("chdir")){

		cout << "Currently viewing:\n" << getcwd(NULL, 0) << endl;
		//if given chdir and file name, change to filename
		if (argv.size() == 2){
			string temp = argv[1];

			//call chdir method on the string desired
			int result = chdir(temp.c_str());

			if (result == 0){
				cout << "directory changed\n" << endl;
				cout << "Currently viewing:\n" << getcwd(NULL, 0) << endl;
			}
			else
				cout << "error in chdir" << endl;
		}

		//if no directory file specified, change to home directory
		else if (argv.size() == 1) {
			cout << "argc is 1\n" << endl;
			char * home;
			string temp = "HOME";
			home = getenv(temp.c_str());
			int i = chdir(home);

			if (i < 0)
				cout << "directory couldn't be changed\n" << endl;


			else{
				cout << "directory changed\n" << endl;
				cout << "home = " << home << endl;
			}


		}

		//if there are more than 2 args, throw error
		else if (argv.size() > 2) {
			cout << "too many args: " << argv[0] << endl;
			exit(1);
		}
		return 1;


	}

	/* environ, expor, unexport, set, unset */
	/* display to the terminal a listing of all environment strings that are currently defined*/
	else if (!cmd.compare("environ")){
		char ** original = environ;
		while (environ != NULL && *environ != NULL && *environ != "\0"){
			cout << *environ << endl;
			environ++;
		}
		environ = original;
		return 1;

	}

	/* export the global variable argv[1] with the value argv[2] to the environment*/
	else if (!cmd.compare("export")){
		if (argv.size() == 3){
			char * buffer;
			string argv1 = argv[1];
			string argv2 = argv[2];
			buffer = getenv(argv1.c_str());
			if (buffer == NULL)
			{
				setenv(argv1.c_str(), argv2.c_str(), 0);
			}
			else {
				setenv(argv1.c_str(), argv2.c_str(), 1);
			}

			cout << endl;
			for (int i = 0; environ[i] != NULL; i++){
				cout << environ[i] << endl;
			}
		}
		return 1;

	}

	/* unexport the global variable in argv[1] from the environment */
	else if (!cmd.compare("unexport")){
		if (argv.size() == 2){
			char * buffer;
			string temp = argv[1];
			buffer = getenv(temp.c_str());
			if (buffer != NULL){
				string toChar = argv[1];
				char * set = &toChar[0];
				putenv(set);
				cout << "Environment variable successfully unset" << endl;
			}
			else cout << "Environment variable does not exist" << endl;
		}
		return 1;

	}

	/* set the value of the local variable W1 to the value W2 */
	else if (!cmd.compare("set")){
		if (argv.size() == 3){
			if (local_variables.find(argv[1]) == local_variables.end())
				local_variables.insert(pair<string, string>(argv[1], argv[2]));
			else local_variables[argv[1]] = argv[2];
			for (map<string, string>::iterator it = local_variables.begin(); it != local_variables.end(); it++){
				cout << it->first << " = " << it->second << endl;
			}
		}
		return 1;

	}

	/* unset a previously set local variable */
	else if (!cmd.compare("unset")){
		if (argv.size() == 2){
			if (local_variables.find(argv[1]) != local_variables.end()){
				local_variables.erase(argv[1]);
			}
			for (map<string, string>::iterator it = local_variables.begin(); it != local_variables.end(); it++){
				cout << it->first << " = " << it->second << endl;
			}
		}
		return 1;

	}

	/* displays past commands */
	else if (!cmd.compare("history")){
		int size = cmdHistory.size() - 1;
		int n;
		try{
			n = (argv.size() > 1) ? stoi(argv[1]) : size;
		}
		catch (...) {
			n = size;
		}
		if (n > size) n = size;
		for (int i = size - n; i < size; ++i)
		{
			cout << i + 1 << "  " << cmdHistory[i] << endl;
		}
		return 1;

	}

	/* prints to the screen, then executes the command in the history list which corresponds to the nth line
	* if n not specified, repeats the previous command
	*/
	else if (!cmd.compare("repeat")){
		int size = cmdHistory.size() - 1;
		int n = size;
		//take argv[1] if number is given, size otherwise
		if (argv.size() >= 2)
		{
			try{
				n = stoi(argv[1]);
			}
			catch (...){
				cout << "Error in repeat" << endl;
			}
		}
		else if (argv.size() == 1)
			n = size;
		else
		{
			cout << "Invalid repeat command" << endl;
			exit(1);
		}

		if (n > size)
			n = size;

		string tmp = cmdHistory[n - 1];
		char* arr = new char[tmp.length() + 1];
		for (int i = 0; i < tmp.length(); ++i)
		{
			arr[i] = tmp[i];
		}
		arr[tmp.length()] = '\0';
		eval(arr);
		delete[] arr;
		return 1;

	}

	/* sends a signal to the specified pid. If no signal specified, SIGTERM signal is default*/
	else if (!cmd.compare("kill")){
		if (argv.size() == 3){
			cout << "here" << endl;
			string sig_str = argv[1].substr(1, argv[1].length());
			if (is_number(sig_str) && is_number(argv[2])){
				int pid = stoi(argv[2]);
				int sig_num = stoi(sig_str);
				if (kill(pid, sig_num) < 0) cout << "Did not kill" << endl;
				int * status;
				waitpid(pid, status, NULL);
			}
		}
		if (argv.size() == 2){
			if (is_number(argv[1])){
				int pid = stoi(argv[1]);
				//FIXME THIS IS SUPPOSED TO BE SIGTERM
				if (kill(pid, SIGINT) < 0) cout << "Did not kill" << endl;
				int * status;
				waitpid(pid, status, NULL);
			}
		}
		return 1;
	}

	/* waits for the pid specified, if -1, waits for any children to complete. Essentially brings a bg job to fg */
	else if (!cmd.compare("wait")){
		if (argv.size() == 2){
			int pid;
			if (is_number(argv[1]) && (pid = stoi(argv[1])) >= -1){
				if (pid == -1){
					wait(NULL);
				}
				else{
					int status;
					waitpid(pid, &status, NULL);
				}
			}
		}
		return 1;
	}

	//command that displays the man page 
	else if (!cmd.compare("help")){

		ifstream ifs_help("man.txt");
		if (ifs_help.is_open())
		{
			string man_line;
			int line_num = 0;
			while (getline(ifs_help, man_line) && line_num < 25)
			{
				cout << man_line << endl;
				line_num++;
			}

			string more;
			char ch = fgetc(stdin);
			while (!ifs_help.eof() && ch != 'q'){
				if (ch == 10)
					getline(ifs_help, man_line);
				cout << man_line;
				ch = fgetc(stdin);

			}
		}
		ifs_help.close();
		return 1;
	}


	else return 0;
}



/*
* waitfg - Block until process pid is no longer the foreground process
*/
int waitfg(pid_t pid)
{
	int status = 0;
	//int jid = pid2jid(pid);	

	if ((pid = waitpid(-1, &status, WUNTRACED)) > 0)
	{
		int jid = pid2jid(pid);

		//printf("wait in while loop \n");
		if (WIFSTOPPED(status))
		{
			sio_puts("[");
			sio_putl(pid);
			sio_puts("] stopped by signal 20\n");

			jobs[jid - 1].state = ST;
			//printf("job stopped");
		}
		//printf("out of loop");
		if (WIFEXITED(status) || WIFSIGNALED(status))
		{
			int termsig = WTERMSIG(status);
			//printf ("termsig %i \n", termsig);
			if (termsig == 2){
				sio_puts("[");
				sio_putl(pid);
				sio_puts("] terminated by signal 2\n");

			}
			deletejob(jobs, pid);
		}
		//printf("done");
	}
	return WEXITSTATUS(status);
}

/*****************
* Signal handlers
*****************/
//void sigsegv_handler(int sig)
//{
//	printf("THIS");
//	exit(0);
//	return;
//}

/*
* sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
*     a child job terminates (becomes a zombie), or stops because it
*     received a SIGSTOP or SIGTSTP signal. The handler reaps all
*     available zombie children, but doesn't wait for any other
*     currently running children to terminate.
*/
void sigchld_handler(int sig)
{
	//printf("child handler \n");

	pid_t pid;
	int status;

	//listjobs(jobs);
	while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
	{
		int jid = pid2jid(pid);

		//printf("WTFin while loop \n");
		if (WIFSTOPPED(status))
		{
			sio_puts("[");
			sio_putl(pid);
			sio_puts("] stopped by signal 20\n");


			jobs[jid - 1].state = ST;
		}
		if (WIFEXITED(status) || WIFSIGNALED(status))
		{
			int termsig = WTERMSIG(status);
			//printf ("termsig %i \n", termsig);
			if (termsig == 2){

				sio_puts("[");
				sio_putl(pid);
				sio_puts("] stopped by signal 2\n");

			}
			deletejob(jobs, pid);
		}
	}
	return;
}

/*
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
*    user types ctrl-c at the keyboard.  Catch it and send it along
*    to the foreground job.
*/
void sigint_handler(int sig)
{
	int fpid;
	if ((fpid = fgpid(jobs)) > 0)
	{
		kill(-fgpid(jobs), SIGINT);
	}
	return;
}

/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
*     the user types ctrl-z at the keyboard. Suspends the
*     foreground job by sending it a SIGTSTP. This is what ctrl-s does in
*	the normal linux shell
*/
void sigtstp_handler(int sig)
{
	int fpid;
	if ((fpid = fgpid(jobs)) > 0)
	{
		kill(-fgpid(jobs), SIGTSTP);
	}
	return;
}

/*
* sigcont_handler - The kernel sends a SIGCONT to the shell whenever the
*	the user types ctrl-q at the keyboard. Continues a stopped job.
*/
void sigcont_handler(int sig)
{
	int fpid;
	if ((fpid = fgpid(jobs)) > 0)
	{
		kill(-fgpid(jobs), SIGCONT);
	}
}

/*
* sigquit_handler - The kernel sends a SIGQUIT to the shell whenever the
*	the user types ctrl-s at the keyboard. Stops all output to the terminal
*	whilst still running it.
*/
void sigquit_handler(int sig)
{
	int fpid;
	if ((fpid = fgpid(jobs)) > 0)
	{
		kill(-fgpid(jobs), SIGQUIT);
	}
}
/*********************
* End signal handlers
*********************/

/***********************************************
* Helper routines that manipulate the job list
**********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
	int i, max = 0;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if (verbose){
				printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs) + 1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return &jobs[i];
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state) {
			case BG:
				printf("Running ");
				break;
			case FG:
				printf("Foreground ");
				break;
			case ST:
				printf("Stopped ");
				break;
			default:
				printf("listjobs: Internal error: job[%d].state=%d ",
					i, jobs[i].state);
			}
			printf("%s", jobs[i].cmdline);
		}
	}
}
/******************************
* end job list helper routines
******************************/


/***********************
* Other helper routines
***********************/

/*
* usage - print a help message
*/
//FIXME THIS IS WRONG
void usage(void)
{
	printf("Usage: shell [-xdf]\n");
	printf("   -x   prints after varaibel substitution\n");
	printf("   -d   print debug messages\n");
	printf("   -f   input is from a file\n");
	exit(1);
}

/*
* unix_error - unix-style error routine
*/
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
* app_error - application-style error routine
*/
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
* Signal - wrapper for the sigaction function
*/
handler_t *Signal(int signum, handler_t *handler)
{
	struct sigaction action, old_action;

	action.sa_handler = handler;
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}
