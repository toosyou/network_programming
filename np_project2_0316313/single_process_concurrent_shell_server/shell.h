#ifndef SHELL_H
#define SHELL_H

#include <iostream>
#include <vector>
#include <string>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sstream>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <map>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>

using namespace std;

#define PATH_BUFSIZE 1024
#define COMMAND_BUFSIZE 1024
#define TOKEN_BUFSIZE 64
#define TOKEN_DELIMITERS " \t\r\n\a"
#define BACKGROUND_EXECUTION 0
#define FOREGROUND_EXECUTION 1
#define PIPELINE_EXECUTION 2

#define PIPE_INIT 0
#define PIPE_STDOUT 1
#define PIPE_STDERR 2

struct pipes{
    bool used[101];
    int pipe[101][2];
    pipes(){
        for(int i=0;i<101;++i)
            used[i] = false;
    }
};

pipes public_pipe;

struct pgid_with_size{
    pid_t pgid;
    int size;
    pgid_with_size(){
        pgid = 0;
        size = 0;
    }
};

struct pipe_to_go{
    int fd[2];
    int rest;
    pipe_to_go(){
        fd[0] = -1;
        fd[1] = -1;
        rest = -1;
    }
};

bool pipe2go_cmp(pipe_to_go a , pipe_to_go b){
    return a.rest < b.rest;
}

struct command_segment{
    vector<string> args;
    pid_t pid;
    pid_t pgid;
    bool is_file;
    bool is_first;
    int pubpipe_to;
    int pubpipe_from;
    command_segment(){
        args.clear();
        pid = 0;
        pgid = 0;
        is_file = false;
        is_first = false;
        pubpipe_to = -1;
        pubpipe_from = -1;
    }
    void clear(void){
        args.clear();
        pid = 0;
        pgid = 0;
        is_file = false;
        is_first = false;
        pubpipe_to = -1;
        pubpipe_from = -1;
    }
};

struct command{
    list<command_segment> segment;
    int mode;
    int stdin_rest;
    int stderr_rest;
    command(){
        segment.clear();
        mode = FOREGROUND_EXECUTION;
        stdin_rest = 0;
        stderr_rest = 0;
    }
    void clear(void){
        segment.clear();;
        mode = FOREGROUND_EXECUTION;
        stdin_rest = 0;
        stderr_rest = 0;
    }
    int size(void){
        return this->segment.size();
    }
};

class mysh{
    
    int id_;
    int fd_;
    string nickname_;
    string ip_;
    string port_;
    string brocast_msg_;
    string input_;
    
    
    vector<pipe_to_go> pipe_queue;
    map<pid_t, vector<pgid_with_size>::iterator > pid_to_it;
    vector<pgid_with_size> back_pgids;

    int cmd_cd(string path){
        int rtn_cd = chdir(path.c_str());
        if(rtn_cd == -1){
            cout << strerror(errno) <<endl;
            return -1;
        }
        return 0;
    }

    int cmd_exit(void){
        //clean up
        //close all the pipe remend
        for(int i=0;i<pipe_queue.size();++i){
            close(pipe_queue[i].fd[1]);
        }
        //say goodbye!
        //cout << "Goodbye!" <<endl;
        pipe_queue.clear();
    }

    int cmd_kill(string pid_s){
        kill(atoi(pid_s.c_str()),SIGINT);
        return 0;
    }

    int cmd_fg(int pgid){
        int this_pgid = getpgid(0);
        int index_pgid = -1;
        int wait_status = 0;

        kill(pgid,SIGCONT);

        //find it in background_pigds
        for(int i=0;i<back_pgids.size();++i){
            if(back_pgids[i].pgid == pgid)
                index_pgid = i;
        }
        if(index_pgid == -1){
            cout << "pid/pgid not found" << endl;
            return -1;
        }

        //make it foreground
        if(tcsetpgrp(STDIN_FILENO,pgid) == -1){
            cerr << "tcsetpgrp error 0" <<endl;
            cerr << strerror(errno) <<endl;
        }
        //wait for pgid
        for(int i=0;i<back_pgids[index_pgid].size;++i){
            //cout << "i am waiting " << i <<endl;
            int rtn_waitpid = waitpid(-pgid,&wait_status,WUNTRACED) ;
            if( rtn_waitpid == -1 && errno != ECHILD){
                cerr << "waitpid wrong" <<endl;
                cerr << strerror(errno) <<endl;
            }
            if(rtn_waitpid != -1 && WIFEXITED(wait_status) == 1){
                back_pgids[index_pgid].size--;
            }
            //cout << "finished " << i <<endl;
        }

        if(tcsetpgrp(STDIN_FILENO,this_pgid) == -1){
            cerr << "tcsetpgrp error 1" <<endl;
            cerr << strerror(errno) <<endl;
        }

        return 0;
    }

    int cmd_bg(int pgid){
        kill(pgid,SIGCONT);
        return 0;
    }

    int cmd_bg(string pgid_s){
        int pgid = atoi(pgid_s.c_str());
        return cmd_bg(pgid);
    }

    int cmd_fg(string pgid_s){
        int pgid = atoi(pgid_s.c_str());
        return cmd_fg(pgid);
    }

    int cmd_setenv(string name,string value){
        if(setenv(name.c_str(),value.c_str(),1) == -1){
            cerr << "setenv error" <<endl;
            return -1;
        }
        return 0;
    }

    int cmd_printenv(string name){
        char *env = getenv(name.c_str());
        if(env == NULL){
            cerr << "printenv error" <<endl;
            return -1;
        }
        cout << name << "=" << env <<endl;
        return 0;
    }

    int shell_command_parser(string &input,command &command_line){
        istringstream iss(input);
        string buffer;
        bool must_end = false;

        command_segment tmp_seg;
        tmp_seg.is_first = true;
        while(iss >> buffer){
            //there's argument that contains '/' which is not allowed
            if(buffer.find("/") != string::npos){
                cerr << "argument is not allowed to contain \'/\' " <<endl;
                return -1;
            }
            if(buffer == "|"){
                if(tmp_seg.args.size() == 0)
                    return -1;
                command_line.segment.push_back(tmp_seg);
                tmp_seg.clear();
            }
            else if(buffer == ">"){
                if(tmp_seg.args.size() == 0)
                    return -1;
                command_line.segment.push_back(tmp_seg);
                tmp_seg.clear();
                tmp_seg.is_file = true;
            }
            else if(buffer[0] == '>'){//pubpipe to
                string number_string = buffer.substr(1);
                int number = atoi(number_string.c_str());
                tmp_seg.pubpipe_to = number;
            }
            else if(buffer[0] == '<'){ // pubpipe from
                string number_string = buffer.substr(1);
                int number = atoi(number_string.c_str());
                tmp_seg.pubpipe_from = number;
            }
            else if(buffer[0] == '|'){ //numbered pipe
                string number_string = buffer.substr(1);
                if(number_string.size() == 0)
                    return -1;
                int number = atoi(number_string.c_str());
                if(number <= 0 || number > 1000)
                    return -1;
                command_line.stdin_rest = number;
                must_end = true;
            }
            else if(buffer[0] == '!'){ //numbered pipe with stderr
                if(buffer.size() == 1)
                    return -1;
                string number_string = buffer.substr(1);
                int number = atoi(number_string.c_str());
                if(number<= 0 || number > 1000)
                    return -1;
                command_line.stderr_rest = number;
                must_end = true;
            }
            else{
                if(must_end == true)
                    return -1;
                if(tmp_seg.is_file == true)
                    must_end = true;
                tmp_seg.args.push_back(buffer);
            }
        }
        if(tmp_seg.args.size() != 0)
            command_line.segment.push_back(tmp_seg);
        return 0;
    }

    int shell_exec_builtin(command_segment &segment){
        string &arg0 = segment.args[0];
        if(arg0 == "cd")
            cmd_cd(segment.args[1]);
        else if(arg0 == "exit")
            cmd_exit();
        else if(arg0 == "printenv")
            cmd_printenv(segment.args[1]);
        else if(arg0 == "setenv")
            cmd_setenv(segment.args[1],segment.args[2]);
        else
            return 0; // it's not a builtin-function
        return 1;
    }

    bool is_buildin(command_segment &segment){
        string &arg0 = segment.args[0];
        if(arg0 == "cd");
        else if(arg0 == "exit");
        else if(arg0 == "kill");
        else if(arg0 == "fg");
        else if(arg0 == "bg");
        else
            return false; // it's not a builtin-function
        return true;
    }

    int shell_exec_segment(command_segment &segment,vector<int>&in_fd, int out_fd,int err_fd, vector<vector<int> > &fds,pipe_to_go &stdout_to_go,pipe_to_go &stderr_to_go){

        if(shell_exec_builtin(segment))
            return 0;

        char** args = new char*[segment.args.size()+1];
        for(int i=0;i<segment.args.size();++i){
            args[i] = new char[segment.args[i].size()+1];
            strcpy(args[i],segment.args[i].c_str());
        }
        args[ segment.args.size() ] = NULL;

        //check public pipe
        if(segment.pubpipe_from != -1){
            if( public_pipe.used[segment.pubpipe_from] == false ){
                cout << "*** Error: the pipe #" << segment.pubpipe_from << " does not exist yet. ***" <<endl;
                return -2;
            }
            else{
                string name = nickname_.empty() ? string("(no name)") : nickname_;
                char number_buffer[20];
                sprintf(number_buffer,"%d",this->id_);
                string number_string(number_buffer);
                this->brocast_msg_ += string("*** ") + name + string(" (#") + number_string;
                this->brocast_msg_ += string(") just received via '") + this->input_ + string("' ***\n");

                in_fd.push_back( public_pipe.pipe[segment.pubpipe_from][STDIN_FILENO] );
                public_pipe.used[ segment.pubpipe_from ] = false;
            }
        }
        if(segment.pubpipe_to != -1){
            if( public_pipe.used[segment.pubpipe_to] == true ){
                cout << "*** Error: the pipe #" << segment.pubpipe_to << " already exists. ***" <<endl;
                return -2;
            }
            else{
                string name = nickname_.empty() ? string("(no name)") : nickname_;
                char number_buffer[20];
                sprintf(number_buffer,"%d",this->id_);
                string number_string(number_buffer);
                this->brocast_msg_ += string("*** ") + name + string(" (#") + number_string;
                this->brocast_msg_ += string(") just piped '") + this->input_ + string("' ***\n");

                if(pipe(public_pipe.pipe[segment.pubpipe_to]) == -1){
                    cerr << "pubpipe to error" <<endl;
                    exit(-1);
                }
                out_fd = public_pipe.pipe[segment.pubpipe_to][STDOUT_FILENO];
                public_pipe.used[segment.pubpipe_to] = true;
            }
        }

        pid_t childpid = fork();
        if(childpid == -1){
            cout << "wrong at fork" <<endl;
            exit(0);
        }

        if(childpid == 0){//child
            //cout << "in_fd : " << in_fd << "\tout_fd : " << out_fd <<endl;
            char buffer[100000];
            int total_size = 0;
            int tmp_fd[2];
            pipe(tmp_fd);
            for(int i=0;i<in_fd.size();++i){
                int size_read = read(in_fd[i],buffer+total_size,99999-total_size);
                total_size += size_read;
            }
            buffer[total_size] = '\0';
            write(tmp_fd[1],buffer,total_size);

            dup2(tmp_fd[0],STDIN_FILENO);
            close(tmp_fd[0]);
            close(tmp_fd[1]);

            if(out_fd != STDOUT_FILENO && out_fd != -1){
                if(dup2(out_fd,STDOUT_FILENO) == -1){
                    cerr << strerror(errno) <<endl;
                    exit(errno);
                }
            }

            if(err_fd != STDERR_FILENO){
                if(dup2(err_fd,STDERR_FILENO) == -1){
                    cerr << strerror(errno) <<endl;
                    exit(errno);
                }
                close(err_fd);
            }

            //close all the fds
            for(int i=0;i<fds.size();++i){
                close(fds[i][0]);
                close(fds[i][1]);
            }
            if(stdout_to_go.rest > 0){
                close(stdout_to_go.fd[0]);
                close(stdout_to_go.fd[1]);
            }
            if(stderr_to_go.rest > 0){
                close(stderr_to_go.fd[0]);
                close(stderr_to_go.fd[1]);
            }
            for(int i=0;i<pipe_queue.size();++i){
                close(pipe_queue[i].fd[0]);
                //close(pipe_queue[i].fd[1]);
            }

            if(execvp(segment.args[0].c_str(),args) == -1){
                //cerr << strerror(errno) <<endl;
            }

            exit(errno);
        }
        else{//parent
            int wait_status = -1;

            if(segment.is_first == false){
                for(int i=0;i<in_fd.size();++i){
                    close(in_fd[i]);
                }
            }

            if(out_fd != STDOUT_FILENO){
                close(out_fd);
            }
            if(err_fd != STDERR_FILENO){
                close(err_fd);
            }

            if(waitpid(childpid,&wait_status,WUNTRACED) == -1){
                return -1;
            }
            if(WEXITSTATUS(wait_status) == ENOENT){
                cout << "Unknown command: [" << segment.args[0] << "]." << endl;
                return -1;
            }
            for(int i=0;i<segment.args.size();++i)
                delete [] args[i];
            delete [] args;
            return 0;
        }

        return 0;
    }

    int shell_exec_command(command &command_line){
        int rtn_exec = 0;
        bool something_done = false;
        vector< vector<int> > fds;
        list<command_segment>::iterator it = command_line.segment.begin();

        //pipe from numbered pipe
        vector<int> first_fd;
        vector< vector<pipe_to_go>::iterator > its_first_fd;
        for(int i=0;i<pipe_queue.size();++i){
            pipe_to_go &this_pipe2go = pipe_queue[i];
            if(this_pipe2go.rest == 1){
                first_fd.push_back(this_pipe2go.fd[0]);
                its_first_fd.push_back( pipe_queue.begin()+i );
            }
        }


        //check if it need pipe_to_go
        pipe_to_go stdout_to_go;
        pipe_to_go stderr_to_go;
        if(command_line.stdin_rest > 0){
            int tmp_fd[2];
            if(pipe(tmp_fd) == -1){
                cerr << "pipe tmp_fd error" <<endl;
                exit(-1);
            }
            stdout_to_go.fd[0] = tmp_fd[0];
            stdout_to_go.fd[1] = tmp_fd[1];
            stdout_to_go.rest = command_line.stdin_rest;
        }
        if(command_line.stderr_rest > 0){
            int tmp_fd[2];
            if(pipe(tmp_fd) == -1){
                cerr << "pipe err_fd error" <<endl;
                exit(-1);
            }
            stderr_to_go.fd[0] = tmp_fd[0];
            stderr_to_go.fd[1] = tmp_fd[1];
            stderr_to_go.rest = command_line.stderr_rest;
        }

        //pass pipe and segment to exec_seg
        for(int i=0;i<command_line.size();++i){
            //open a pipe for this segment
            vector<int> tmp_pipe(2,0);
            int tmp_fd[2];
            if(pipe(tmp_fd) == -1){
                cerr << "wrong at pipe()" <<endl;
                return -1;
            }
            tmp_pipe[0] = tmp_fd[0];
            tmp_pipe[1] = tmp_fd[1];
            fds.push_back(tmp_pipe);

            //do the normal thing
            vector<int> in_fd;
            int out_fd = STDOUT_FILENO;
            int err_fd = STDERR_FILENO;
            if(i == 0)
                in_fd = first_fd;
            else
                in_fd.push_back( fds[i-1][0] );
            if(i != command_line.size()-1)
                out_fd = fds[i][1];
            else{//last segment. connect pipe_to_go to it
                if(stdout_to_go.rest > 0)
                    out_fd = stdout_to_go.fd[1];
                if(stderr_to_go.rest > 0)
                    err_fd = stderr_to_go.fd[1];
            }
            list<command_segment>::iterator tmp_it = it;
            tmp_it++;
            if(tmp_it!=command_line.segment.end()){
                if(tmp_it->is_file == true){//next segment is a file, do the IO Redirection
                    close(out_fd);
                    out_fd = creat(tmp_it->args[0].c_str(),0644);
                    i++; //dont do the last segment
                }
            }

            rtn_exec = shell_exec_segment(*it,in_fd,out_fd,err_fd,fds,stdout_to_go,stderr_to_go);
            if(rtn_exec != 0){
                //cerr << "rtn_exec = " << rtn_exec <<endl;
                break;
            }
            else{
                it++;
                something_done = true;
            }
        }
        //close all pipe in parent
        for(int i=0;i<fds.size();++i){
            close(fds[i][0]);
            close(fds[i][1]);
        }
        if(rtn_exec == 0 || (rtn_exec == -2 && something_done ) ){//all things are execed fine or error on pubpipe
            //close things
            for(int i=0;i<first_fd.size();++i){
                close(first_fd[i]);
                //cout << i <<endl;
            }
            first_fd.clear();
            for(int i=0;i<pipe_queue.size();++i){
                if(pipe_queue[i].rest == 1){
                    pipe_queue.erase(pipe_queue.begin()+i);
                    --i;
                }
            }
            its_first_fd.clear();
            //decrease all the rest
            for(int i=0;i<pipe_queue.size();++i){
                pipe_queue[i].rest--;
            }
            //close new pipe to go & push it in
            if(stdout_to_go.rest > 0){
                //close(stdout_to_go.fd[1]);
                pipe_queue.push_back(stdout_to_go);
            }
            if(stderr_to_go.rest > 0){
                //close(stderr_to_go.fd[1]);
                pipe_queue.push_back(stderr_to_go);
            }
        }
        else{//something wrong
            //no need to close/erase first_fd & decrease pipe_queue
            //close all pipe to go
            if(stdout_to_go.rest > 0){
                close(stdout_to_go.fd[0]);
                close(stdout_to_go.fd[1]);
            }
            if(stderr_to_go.rest > 0){
                close(stderr_to_go.fd[0]);
                close(stderr_to_go.fd[1]);
            }
        }
        command_line.clear();
        fds.clear();
        return 0;
    }

    void shell_print_promt(){
        cout << "% " ;
        return;
    }


    int shell_clean_up(void){

        //merge pipes with same rest
        sort(pipe_queue.begin(),pipe_queue.end(),&pipe2go_cmp);

        for(int i=1;i<pipe_queue.size();++i){
            if(pipe_queue[i-1].rest == pipe_queue[i].rest){
                char buffer[100000];
                int tmp_fd[2];
                if(pipe(tmp_fd) == -1){
                    cerr << "clean up pipe error" <<endl;
                    exit(-1);
                }
                int size_read = read(pipe_queue[i-1].fd[0],buffer,99999);
                size_read += read(pipe_queue[i].fd[0],buffer+size_read,99999-size_read);
                buffer[size_read]='\0';
                write(tmp_fd[1],buffer,size_read);
                //close all
                close(tmp_fd[1]);
                close(pipe_queue[i-1].fd[0]);
                close(pipe_queue[i].fd[0]);

                pipe_queue[i-1].fd[0] = tmp_fd[0];
                pipe_queue[i-1].fd[1] = -1;
                pipe_queue.erase(pipe_queue.begin()+i);
                i--;
            }
        }

        return 0;
    }

    void shell_loop(){
        string input_command;
        command command_line;

        int status = 1;

        while(status >= 0){
            shell_clean_up();
            shell_print_promt();
            getline(cin,input_command);
            if(input_command.size() == 0)
                continue;
            if(shell_command_parser(input_command,command_line) == -1){
                input_command.clear();
                command_line.clear();
                continue;
            }
            status = shell_exec_command(command_line);
            input_command.clear();
            command_line.clear();
        }
        return;
    }

    void shell_welcome(){
        cout << "****************************************" <<endl;
        cout << "** Welcome to the information server. **" <<endl;
        cout << "****************************************" <<endl;
        return;
    }

    void kill_foreground(int signum){
        fflush(stdin);
        shell_print_promt();
        return;
    }

    void make_shell_forground(int signum){
        cout << "what are you doing" <<endl;
        int this_pgid = getpgid(0);
        //set parent to foreground
        if(tcsetpgrp(STDIN_FILENO,this_pgid) == -1){
            cerr << "tcsetpgrp error" <<endl;
            cerr << strerror(errno) <<endl;
        }
        return;
    }

    void deal_with_zombie(int signum){
        //cout << "SIGCHILD handling" <<endl;
        int status = 0;
        int zombie_pid = waitpid(-1,&status,WNOHANG);
        if(zombie_pid == -1 ){
            cout << "error occures when dealing with zombie" <<endl;
            cout << strerror(errno) <<endl;
            return ;
        }
        if(zombie_pid == 0)
            return;
        pid_to_it[zombie_pid]->size--;
        //pid_to_it.erase(zombie_pid);
        //cout << "finished" <<endl;
        return;
    }

    void shell_init(){
        //cmd_cd(string("/u/cs/103/0316313/ras/"));
        //cmd_setenv(string("PATH"),string("bin:."));
        //set this pgid
        setpgid(0,0);
        //set SIGCHLD for zombie
        //signal(SIGCHLD,&deal_with_zombie);

        signal(SIGTTIN,SIG_IGN);
        signal(SIGTTOU,SIG_IGN);

        return;
    }


public:
    mysh(int id = -1,string ip = string("CGILAB"),string port = string("511")){
        this->id_ = id;
        this->ip_ = ip;
        this->port_ = port;

        //shell_welcome();
        //shell_print_promt();
    }
    void welcome(void){
        this->shell_welcome();
    }
    void print_promt(void){
        this->shell_print_promt();
        cout.flush();
    }

    int input_command(string input){
        command command_line;

        this->input_ = input.substr(0,input.size()-2);
        shell_clean_up();
        if(input.size() == 0){
            shell_print_promt();
            return 0;
        }
        if(shell_command_parser(input,command_line) == -1){
            command_line.clear();
            return -1;
        }
        int status = shell_exec_command(command_line);
        command_line.clear();
        print_promt();
        return status;
    }
    ~mysh(){
        this->cmd_exit();
    }
    void clear(void){
        this->cmd_exit();
        this->id_ = -1;
        this->nickname_.clear();
        this->brocast_msg_.clear();
    }
    int id(void){
        return this->id_;
    }
    string nickname(void){
        return this->nickname_;
    }
    string ip(void){
        return this->ip_;
    }
    string port(void){
        return this->port_;
    }
    int fd(void){
        return this->fd_;
    }
    string brocast_msg(void){
        string return_msg = brocast_msg_;
        brocast_msg_.clear();
        return return_msg;
    }

    void set_fd(int fd){
        this->fd_ = fd;
    }

    void set_id(int id){
        this->id_ = id;
    }
    void set_nickname(string name){
        this->nickname_ = name;
    }

};


#endif // SHELL_H
