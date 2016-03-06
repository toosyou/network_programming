#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#define SERVER_PORT 30309

using namespace std;

//this is a server of shell

void kill_the_zombie(int signum){
    int wait_status = -1;
    int childpid = waitpid(-1,&wait_status,WNOHANG);
    if(childpid == -1){
        cerr << "waitpid error" <<endl;
        cerr << strerror(errno) <<endl;
        exit(-1);
    }
    return;
}

int main()
{
    //handle signals
    signal(SIGCHLD,&kill_the_zombie);

    //open the server
    sockaddr_in server_addr,client_addr;
    int fd_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd_socket == -1){
        cerr << "fd error" <<endl;
        exit(-1);
    }
    memset(&server_addr,0,sizeof(sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr=INADDR_ANY;

    //bind
    if(::bind(fd_socket,(const sockaddr*)&server_addr,sizeof(server_addr)) != 0){
        cerr << "bind error" <<endl;
        cerr << strerror(errno) <<endl;
        close(fd_socket);
        exit(-1);
    }
    //listen
    if(listen(fd_socket,5) == -1){
        cerr << "listen error" <<endl;
        close(fd_socket);
        exit(-1);
    }
    //wait for connect
    while(1){
        int client_length = sizeof(client_addr);
        //accept connection
        int fd_connection = accept(fd_socket,(sockaddr*)&client_addr,(socklen_t*)&client_length);
        if(fd_connection == -1){
            cerr << "accept error" <<endl;
            close(fd_socket);
            exit(-1);
        }
        //fork a child to process
        int childpid = fork();
        if(childpid == 0){//child process
            //simply make connection to stdin/stdout
            dup2(fd_connection,STDIN_FILENO);
            dup2(fd_connection,STDOUT_FILENO);
            dup2(fd_connection,STDERR_FILENO);
            close(fd_connection);
            close(fd_socket);
            //do the real thing
            //cout << "accept" <<endl;
            execl("../np_shell/np_shell",NULL);

            exit(0);
        }
        else{//parent
            close(fd_connection);
            setpgid(childpid,childpid);
        }
    }

    close(fd_socket);
    return 0;
}
