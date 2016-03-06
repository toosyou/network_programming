#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <vector>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>

using namespace std;

struct server_info{
	char ip[20];
	int port;
	char name_batch_file[50];
	FILE *fp;
	bool need_to_write;
	
	server_info(){
		fp = NULL;
		ip[0]='\0';
		port = 0;
		name_batch_file[0]='\0';
		need_to_write = false;
	}

};

vector<server_info> form_inputs;

int main(int argc, char* argv[], char* envp[]){

	//init
	form_inputs.resize(5);
	//try to get input
	cout << "Content-type: text/html" <<endl<<endl;
	//char *query = getenv("QUERY_STRING");
	char query[200] = "h1=140.113.168.194&p1=30309&f1=test&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=";
	sscanf(query,"h1=%[^&]&p1=%d&f1=%[^&]&h2=%[^&]&p2=%d&f2=%[^&]&h3=%[^&]&p3=%d&f3=%[^&]&h4=%[^&]&p4=%d&f4=%[^&]&h5=%[^&]&p5=%d&f5=%s",
		form_inputs[0].ip,&form_inputs[0].port,form_inputs[0].name_batch_file,
		form_inputs[1].ip,&form_inputs[1].port,form_inputs[1].name_batch_file,
		form_inputs[2].ip,&form_inputs[2].port,form_inputs[2].name_batch_file,
		form_inputs[3].ip,&form_inputs[3].port,form_inputs[3].name_batch_file,
		form_inputs[4].ip,&form_inputs[4].port,form_inputs[4].name_batch_file);
	for(int i=0;i<5;++i)
		cout << form_inputs[i].ip << " " << form_inputs[i].port << " " << form_inputs[i].name_batch_file <<"<br>";

	//try to connect to server
	int socket_remain = 0;
	int sockfd[5];
	sockaddr_in serv_addr[5];
	//using select thing
	fd_set read_fds;
	fd_set active_fds;
	int nfds = getdtablesize();
	FD_ZERO(&active_fds);

	for(int i=0;i<5;++i){
		int fcntl_flags = 0;
		if(form_inputs[i].port == 0)
			continue;
		socket_remain++;
		bzero((char*)&serv_addr[i],sizeof(serv_addr[i]));
		serv_addr[i].sin_family = AF_INET;
		serv_addr[i].sin_addr.s_addr = inet_addr( form_inputs[i].ip );
		serv_addr[i].sin_port = htons( form_inputs[i].port );
		if((sockfd[i] = socket(AF_INET,SOCK_STREAM,0) ) < 0 ){
			cout << "error : cannot open socket " << i << endl;
			exit(-1);
		}

		if(connect( sockfd[i], (sockaddr*)&serv_addr[i], sizeof(serv_addr[i]) ) < 0){
			cout << "error : cannot connect to server " << i <<endl;
			exit(-1);
		}
		fcntl_flags = fcntl(sockfd[i],F_GETFL,0);
		fcntl(sockfd[i],F_SETFL,fcntl_flags | O_NONBLOCK);
		//open the batch file
		form_inputs[i].fp = fopen(form_inputs[i].name_batch_file , "r");

		FD_SET(sockfd[i],&active_fds);
	}

	clock_t ct = clock();

	while(socket_remain > 0){
		if(  ((float)(clock() - ct) / (float)CLOCKS_PER_SEC) > 1.0 )
			exit(-1);
		

		memcpy(&read_fds,&active_fds,sizeof(read_fds));

		if(socket_remain > 0)
			select(nfds,&read_fds,(fd_set*)0,(fd_set*)0,(timeval*)0);

		for(int i=0;i<5;++i){
			if( form_inputs[i].port != 0 && FD_ISSET(sockfd[i],&read_fds)){

				char buffer[3000];
				int n = 0;
				bool socket_closed = false;
				n = read(sockfd[i],buffer,sizeof(buffer));
				if(  (float)(clock() - ct) / CLOCKS_PER_SEC > 1 )
					exit(-1);
				//cout << "herer" <<endl;

				if(n < 0){//it's closed
					//cout << "hererc" <<endl;
					socket_remain--;
					close(sockfd[i]);
					FD_CLR(sockfd[i],&active_fds);
					socket_closed = true;
					continue;
				}
				//output result
				buffer[n]='\0';
				for(int j=0;buffer[j]!='\0';++j){
					if(buffer[j] == '\n')
						cout <<"<br>" <<endl;
					else if(buffer[j] == '%'){
						cout << '%';
						form_inputs[i].need_to_write = true;
					}
					else
						cout << buffer[j];
				}
				cout.flush();
				//cout << "not end" <<endl;
				
				//input a line from in_batch
				if(!socket_closed && form_inputs[i].need_to_write){
					char *line = NULL;
					size_t len = 0;
					//cout << " i shall not getline" <<endl;
					ssize_t size_getline = getline(&line,&len,form_inputs[i].fp);
					cout << (int)size_getline <<endl;
					if(size_getline > 0){
						cout << line << "<br>" <<endl;
						cout.flush();
						write(sockfd[i],line,sizeof(line));
						free(line);
					}
					else{
						socket_remain--;
						close(sockfd[i]);
						FD_CLR(sockfd[i],&active_fds);
						socket_closed = true;
						continue;
					}

					form_inputs[i].need_to_write = false;
					if(  (float)(clock() - ct) / CLOCKS_PER_SEC > 1 )
						exit(-1);
				}
				
			}
		}
	}

	for(int i=0;i<5;++i)
		if(form_inputs[i].port != 0)
			fclose(form_inputs[i].fp);

	return 0;
}