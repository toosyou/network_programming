#include <iostream>
#include <sstream>
#include "shell.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <unistd.h>
#include <string>
#include <stdio.h>

#define SERVER_PORT 30309

using namespace std;

int main(void){

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
    if(listen(fd_socket,31) == -1){
        cerr << "listen error" <<endl;
        close(fd_socket);
        exit(-1);
    }

    int nfds = getdtablesize();
    fd_set rfds;
    fd_set afds;

    FD_ZERO(&afds);
    FD_SET(fd_socket,&afds);
    
    vector<mysh> shells(31);
    map< int , int > fd_index_shell;
    timeval timeout = {0,0};

    while(1){
        memcpy(&rfds,&afds,sizeof(rfds));
        if(select(nfds,&rfds,(fd_set*)0,(fd_set*)0,(timeval*)NULL) < 0){
            close(fd_socket);
            cout << "select wrong : that's it, you are done..." <<endl;
            cout << strerror(errno) <<endl;
            exit(-1);
        }
        //new connect
        if(FD_ISSET(fd_socket,&rfds)){
            int client_length = sizeof(client_addr);
            //accept connection
            int fd_connection = accept(fd_socket,(sockaddr*)&client_addr,(socklen_t*)&client_length);
            if(fd_connection == -1){
                cerr << "accept error" <<endl;
                close(fd_socket);
                exit(-1);
            }
            //find one shell for it
            for(int i=1;i<31;++i){
                if(shells[i].id() == -1){
                    shells[i].set_id(i);
                    shells[i].set_fd(fd_connection);
                    fd_index_shell[fd_connection] = i;

                    int old_stdin = dup(STDIN_FILENO);
                    int old_stdout = dup(STDOUT_FILENO);
                    int old_stderr = dup(STDERR_FILENO);

                    dup2(fd_connection,STDIN_FILENO);
                    dup2(fd_connection,STDOUT_FILENO);
                    dup2(fd_connection,STDERR_FILENO);

                    shells[i].welcome();
                    string msg("*** User '(no name)' entered from ");
                    msg += shells[i].ip() + string("/");
                    msg += shells[i].port();
                    msg += string(". ***\n");
                    for(int j=0;j<shells.size();++j){
                        if(shells[j].id() != -1){
                            write( shells[j].fd() , msg.c_str() , msg.size());
                        }
                    }
                    shells[i].print_promt();
                    cout.flush();

                    dup2(old_stdin,STDIN_FILENO);
                    dup2(old_stdout,STDOUT_FILENO);
                    dup2(old_stderr,STDERR_FILENO);
                    close(old_stdin);
                    close(old_stdout);
                    close(old_stderr);

                    FD_SET(fd_connection,&afds);

                    break;
                }
            }
        }
        for(int fd = 0;fd < nfds;++fd){
            if(fd != fd_socket && FD_ISSET(fd,&rfds)){
                //do input thing
                char buffer[1000];
                int size_input = read(fd,buffer,999);
                if(size_input == 0){//it's closed
                    close(fd);
                    FD_CLR(fd,&afds);
                }
                string input(buffer,size_input);

                int index_shell = fd_index_shell[fd];
                if(input.find("exit") != string::npos){
                    string msg("*** User '");
                    if(shells[index_shell].nickname().empty())
                        msg += string("(no name)");
                    else
                        msg += shells[index_shell].nickname();
                    msg += string("' left. ***\n");
                    for(int i=0;i<shells.size();++i){
                        if(shells[i].id()!=-1){
                            write(shells[i].fd(),msg.c_str(),msg.size());
                        }
                    }

                    shells[index_shell].clear();
                    close(fd);
                    FD_CLR(fd,&afds);
                }
                else if(input.find("tell")!=string::npos){
                    stringstream iss(input);
                    string buffer;
                    int id_tell_to = 0;
                    iss >> buffer >> id_tell_to;
                    //generate the msg
                    bool first = true;
                    string msg("*** ");
                    if(shells[index_shell].nickname().empty()){
                        msg += string("(no name)");
                    }
                    else{
                        msg += shells[index_shell].nickname();
                    }
                    msg += string(" told you ***: ");
                    while(iss >> buffer){
                        if(!first)
                            msg += string(" ");
                        msg += buffer;
                        first = false;
                    }
                    msg += string("\n");

                    if( shells[id_tell_to].id() != -1 ){
                        write( shells[id_tell_to].fd() , msg.c_str() , msg.size() );
                    }
                    else{
                        char char_buffer[20];
                        sprintf(char_buffer,"%d",id_tell_to);
                        string err_msg("*** Error: user #");
                        err_msg += string(char_buffer);
                        err_msg += string(" does not exist yet. ***\n");
                        write( fd,err_msg.c_str(),err_msg.size() );
                    }
                    write(fd,"% ",2);
                }
                else if(input.find("yell") != string::npos){
                    stringstream iss(input);
                    string buffer;
                    bool first = true;
                    iss >> buffer; // yell
                    // make the msg
                    string msg("*** ");
                    if(shells[index_shell].nickname().empty()){
                        msg += string("(no name)");
                    }
                    else{
                        msg += shells[index_shell].nickname();
                    }
                    msg += string(" yelled ***: ");
                    while(iss >> buffer){
                        if(!first)
                            msg += string(" ");
                        msg += buffer;
                        first = false;
                    }
                    msg += string("\n");
                    for(int i=0;i<shells.size();++i){
                        if(shells[i].id() != -1){
                            write( shells[i].fd() , msg.c_str() , msg.size() );
                        }
                    }
                    write(fd,"% ",2);
                }
                else if(input.find("name") != string::npos){
                    stringstream iss(input);
                    string buffer;
                    string name;
                    iss >> buffer >> name;
                    //check if anyone has the same nae
                    bool someone_has_the_sam_name = false;
                    for(int i=0;i<shells.size();++i){
                        if(shells[i].id() != -1){
                            if(shells[i].nickname() == name){
                                someone_has_the_sam_name = true;
                                break;
                            }
                        }
                    }
                    if(someone_has_the_sam_name){
                        string msg("*** '");
                        msg += name;
                        msg += string("' already exists. ***\n");
                        write(fd, msg.c_str(),msg.size());
                    }
                    else{
                        string msg("*** User from ");
                        msg += shells[index_shell].ip() +string("//") +  shells[index_shell].port() + string(" is named '");
                        msg += name;
                        msg += string("'. ***\n");
                        for(int i=0;i<shells.size();++i){
                            if(shells[i].id() != -1){
                                write(shells[i].fd() , msg.c_str() , msg.size());
                            }
                        }
                        shells[index_shell].set_nickname(name);
                    }
                    write(fd,"% ",2);
                }
                else{
                    //find public pipe
                    /*int stdin_pubpipe = -1;
                    int stdout_pubpipe = -1;
                    stringstream iss(input);
                    string buffer;
                    while(iss >> buffer){
                        if(buffer[0]=='>' && buffer.size() != 1){//public stdout pipe
                            string number_string = buffer.substr(1);
                            stdout_pubpipe = atoi(number_string.c_str());
                        }
                        if(buffer[0]=='<' && buffer.size() != 1){
                            string number_string = buffer.substr(1);
                            stdin_pubpipe = atoi(number_string.c_str());
                        }
                    }

                    if(stdin_pubpipe != -1){
                        if(public_pipe.used[stdin_pubpipe] == true){
                            string msg("*** ");
                            char id_buffer[20];
                            sprintf(id_buffer,"%d",shells[index_shell].id());
                            if(shells[index_shell].nickname().empty())
                                msg += string("(no name)");
                            else
                                msg += shells[index_shell].nickname();
                            msg += string(" (#")+string(id_buffer)+string(") just received via '");
                            msg += input.substr(0,input.size()-2) + string("' ***\n");
                            for(int i=0;i<shells.size();++i){
                                if(shells[i].id() != -1){
                                    write(shells[i].fd(),msg.c_str(),msg.size());
                                }
                            }
                        }
                    }
                    if(stdout_pubpipe != -1){
                        if(public_pipe.used[stdout_pubpipe] == false || stdout_pubpipe == stdin_pubpipe){
                            string msg("*** ");
                            char id_buffer[20];
                            sprintf(id_buffer,"%d",shells[index_shell].id());
                            if(shells[index_shell].nickname().empty())
                                msg += string("(no name)");
                            else
                                msg += shells[index_shell].nickname();
                            msg += string(" (#")+string(id_buffer)+string(") just piped '");
                            msg += input.substr(0,input.size()-2) + string("' ***\n");
                            for(int i=0;i<shells.size();++i){
                                if(shells[i].id() != -1){
                                    write(shells[i].fd(),msg.c_str(),msg.size());
                                }
                            }
                        }
                    }*/

                    int old_stdin = dup(STDIN_FILENO);
                    int old_stdout = dup(STDOUT_FILENO);
                    int old_stderr = dup(STDERR_FILENO);
                    dup2(fd,STDIN_FILENO);
                    dup2(fd,STDOUT_FILENO);
                    dup2(fd,STDERR_FILENO);

                    if(input.find("who")!=string::npos){
                        cout << "<ID>\t<nickname>\t<IP/port>\t<indicate me>" <<endl;
                        for(int i=0;i<shells.size();++i){
                            if(shells[i].id() != -1){
                                cout << shells[i].id() << "\t";
                                string name = shells[i].nickname();
                                if(name.size() == 0)
                                    cout << "(no name)\t";
                                else
                                    cout << name << "\t";
                                cout << shells[i].ip() << "/";
                                cout << shells[i].port() << "\t";
                                if(i == index_shell)
                                    cout << "<-me" ;
                                cout << endl;
                            }
                        }
                        shells[index_shell].print_promt();
                    }
                    else{
                        shells[index_shell].input_command(input);
                        string msg = shells[index_shell].brocast_msg();
                        if(!msg.empty()){
                            for(int i=0;i<shells.size();++i){
                                if(shells[i].id()!=-1){
                                    write(shells[i].fd(),msg.c_str(),msg.size());
                                }
                            }
                        }
                        cout.flush();
                    }

                    dup2(old_stdin,STDIN_FILENO);
                    dup2(old_stdout,STDOUT_FILENO);
                    dup2(old_stderr,STDERR_FILENO);
                    close(old_stdin);
                    close(old_stdout);
                    close(old_stderr);
                }
            }
        }

    }

    close(fd_socket);
    return 0;
}
