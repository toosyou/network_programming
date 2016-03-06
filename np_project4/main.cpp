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

struct sock_request{
    int vn;
    int cd;
    unsigned int dstport;
    unsigned int dstip_rreverse;
    unsigned int dstip[4];
    unsigned char* user_id;
    unsigned char original[50];
    sock_request(){
        this->vn = -1;
        this->cd = -1;
        this->dstport = 0;
        this->dstip_rreverse = 0;
        this->user_id = NULL;
        this->original[0] = '\0';
        for(int i=0;i<4;++i)
            this->dstip[i] = 0;
    }
};

sock_request get_request(int fd){
    sock_request ret;
    read(fd,ret.original,50);
    ret.vn = (int)ret.original[0];
    ret.cd = (int)ret.original[1];
    ret.dstport = (unsigned int) (ret.original[2] << 8 | ret.original[3]);
    ret.dstip_rreverse = ret.original[7] << 24 | ret.original[6] << 16 | ret.original[5] << 8 | ret.original[4];
    for(int i=0;i<4;++i)
        ret.dstip[i] = (unsigned int)ret.original[4+i];

    ret.user_id = ret.original+8;
    return ret;
}

bool dst_pass(const sock_request& request){

    char rule[20] = {0};
    char type[2] = {0};
    char ip_firewall[30] = {0};
    FILE* socks_conf = fopen("socks.conf","r");
    if(socks_conf == NULL){
        cout << "socks.conf cannot open" <<endl;
        exit(-1);
    }
    while(!feof(socks_conf)){
        unsigned int address[4];
        fscanf(socks_conf,"%s %s %s",rule,type,ip_firewall);
        sscanf(ip_firewall,"%u.%u.%u.%u",&address[0],&address[1],&address[2],&address[3]);
        //printf("%u%u%u%u\n",address[0],address[1],address[2],address[3]);
        if( (type[0] == 'c' ?  1 : 2) == request.cd ){
            bool pass = true;
            for(int i=0;i<4;++i){
                if( address[i] != 0 && address[i] != request.dstip[0] ){
                    pass = false;
                    break;
                }
            }
            if(pass){
                fclose(socks_conf);
                return true;
                break;
            }
        }
    }
    fclose(socks_conf);
    return false;
}

void connect_to_server(int fd_client, int fd_server){

    //connect browser & server
    bool has_output = false;
    int nfds = getdtablesize();

    fd_set fd_readable;
    fd_set fd_avaliables;
    FD_ZERO( &fd_avaliables );
    FD_SET( fd_client , &fd_avaliables );
    FD_SET( fd_server , &fd_avaliables );
    cout << "----------------------------------------------------" <<endl;
    cout << "receiving data from server..." <<endl;
    while(1){
        memcpy( &fd_readable, &fd_avaliables, sizeof(fd_avaliables) );
        select( nfds, &fd_readable, (fd_set*)0, (fd_set*)0, (timeval*)0);
        if( FD_ISSET(fd_client, &fd_readable) ){//read from browser
            char buffer[500];
            int length = read( fd_client, buffer, 500 );
            if( length <= 0 ){//it's closed
                close(fd_server);
                close(fd_client);
                FD_CLR(fd_server, &fd_avaliables);
                FD_CLR(fd_client, &fd_avaliables);
                break;
            }
            else{
                write( fd_server, buffer, length);
            }
        }
        else if(FD_ISSET(fd_server, &fd_readable)){ // read from server
            char buffer[500];
            int length = read( fd_server, buffer, 500 );
            if( length <= 0 ){//it's closed
                close(fd_server);
                close(fd_client);
                FD_CLR(fd_server, &fd_avaliables);
                FD_CLR(fd_client, &fd_avaliables);
                break;
            }
            else{
                write( fd_client, buffer, length);
                if(has_output == false){
                    int length_to_check = min(length,50);
                    for(int i=0;i<length_to_check;++i){
                        if( isprint( buffer[i] ) )
                            cout << buffer[i];
                        }
                    has_output = true;
                }
            }
        }
    }
    cout << endl << "----------------------------------------------------" <<endl;
    return;        
}

void sock_handler(int fd_connection,sockaddr_in client_addr){
    //get request
    sock_request request = get_request(fd_connection);
    unsigned char reply[8];
    //version must be 4
    if(request.vn != 4){
        printf("----------------------------------------------------\n");
        printf("VN: %d\n",request.vn);
        printf("connection vn != 4 DROP\n");
        printf("----------------------------------------------------\n");
        return ;
    }
    
    //check firewall
    for(int i=0;i<8;++i)
        reply[i] = request.original[i];
    reply[0] = 0;

    if(dst_pass(request))
        reply[1] = 90;//accepted
    else
        reply[1] = 91;//rejected

    //output state
    printf("----------------------------------------------------\n");
    printf("VN: %d, CD: %d, USER_ID: %s\n",request.vn,request.cd,request.user_id);
    printf("Source Addr: %s(%u)\n",inet_ntoa(client_addr.sin_addr),client_addr.sin_port);
    printf("DSTIP: %u.%u.%u.%u(%u)\n",request.dstip[0],request.dstip[1],request.dstip[2],request.dstip[3],request.dstport);
    printf("%s\n",(reply[1] == 90 ? "accepted" : "rejected"));
    printf("----------------------------------------------------\n");


    //do the connection thing
    if(reply[1] == 91){//reject
        write(fd_connection, reply, 8);
        close(fd_connection);
    }
    else if(reply[1] == 90){

        if(request.cd == 1){//connect mode
            //reply
            write(fd_connection, reply, 8);

            //connect to web_server
            int fd_web = socket(AF_INET,SOCK_STREAM,0);
            sockaddr_in web_addr;
            bzero((char*)&web_addr,sizeof(web_addr));
            web_addr.sin_family = AF_INET;
            web_addr.sin_port = htons(request.dstport);
            web_addr.sin_addr.s_addr = request.dstip_rreverse;
            if(connect(fd_web,(sockaddr*)&web_addr,sizeof(web_addr)) == -1){
                cout << "web server connection error" <<endl;
                close(fd_web);
                close(fd_connection);
                exit(-1);
            }

            //connect browser & server
            connect_to_server(fd_connection, fd_web);
        }
        else{//bind mode

            //create a socket to wait for server to connect
            int fd_bind = -1;
            int fd_server = -1;
            sockaddr_in bind_addr;
            sockaddr_in server_addr;
            sockaddr_in real_sockaddr;
            int server_addr_length = sizeof(server_addr);
            int real_sockaddr_length = sizeof(real_sockaddr);

            if( fd_bind = socket(AF_INET, SOCK_STREAM, 0) < 0){
                cout << "bind_socket error" <<endl;
                exit(-1);
            }

            bind_addr.sin_family = AF_INET;
            bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            bind_addr.sin_port = htons(INADDR_ANY);

            if( bind(fd_bind, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0 ){
                cout << "bind error" <<endl;
                close(fd_connection);
                exit(-1);
            }
            if( getsockname( fd_bind, (sockaddr*)&bind_addr, (socklen_t*)&real_sockaddr_length ) < 0 ){
                cout << "cannot get socket name" <<endl;
                close(fd_connection);
                close(fd_bind);
                exit(-1);
            }
            if( listen(fd_bind,5) < 0 ){
                cout << "listen error" <<endl;
                close(fd_connection);
                close(fd_bind);
                exit(-1);
            }
            //reply to client
            reply[0] = 0;
            reply[2] = (unsigned char)(real_sockaddr.sin_port / 256);
            reply[3] = (unsigned char)(real_sockaddr.sin_port % 256);
            reply[4] = reply[5] = reply[6] = reply[7] = 0;
            write(fd_connection, reply, 8);

            //accept connectiong from server
            if( fd_server = accept(fd_bind, (sockaddr*)&server_addr, (socklen_t*)&server_addr_length) < 0){
                cout << "accept error" <<endl;
                close(fd_connection);
                close(fd_bind);
                exit(-1);
            }

            //send again
            write(fd_connection, reply, 8);

            //connect client & server
            connect_to_server(fd_connection,fd_server);

        }
        
    }

    return;
}

int main(){

    //ignore dead children
    signal( SIGCHLD, SIG_IGN);
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
            //handle the request
            sock_handler(fd_connection,client_addr);

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

