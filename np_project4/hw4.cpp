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
#include <map>

using namespace std;


struct server_info{
	char ip[40];
	int port;
	char name_batch_file[50];
	FILE *fp;
	bool need_to_write;
    int lines;
    bool first_line;

    //proxy server
    char proxy_ip[40];
    int proxy_port;
	
	server_info(){
		fp = NULL;
		ip[0]='\0';
		port = 0;
        lines = 0;
        first_line = true;
		name_batch_file[0]='\0';
		need_to_write = false;
		proxy_ip[0] = '\0';
		proxy_port = 0;
	}

};

vector<server_info> form_inputs;
map<int,int> fd_td_index;

void print_start(void){
	cout << "<html>\
        <head>\
        <meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\
        <title>Network Programming Homework 3</title>\
        </head>\
        <body bgcolor=#336699>\
        <font face=\"Courier New\" size=2 color=#FFFF99>\
        <table width=\"800\" border=\"1\">\
        <tr>" <<endl;
    return;
}

int main(int argc, char* argv[], char* envp[]){

	//init
	form_inputs.resize(5);
	//try to get input
	print_start();
	char *query = getenv("QUERY_STRING");
	for(int i=0;i<5;++i){
		int shift_size = 0;
		char number[20];
		sprintf(number,"%d",i+1);
		string format("h"); format += string(number);
		format += string("=%[^&]&p"); format += string(number);
		format += string("=%d&f"); format += string(number);
		format += string("=%[^&]&sh"); format += string(number);
		format += string("=%[^&]&sp"); format += string(number);
		format += string( i == 4 ? "=%d" : "=%d&");
		format += string("%n");
		sscanf(query,format.c_str(),
			form_inputs[i].ip,			&form_inputs[i].port,		form_inputs[i].name_batch_file,
			form_inputs[i].proxy_ip,	&form_inputs[i].proxy_port, &shift_size);
		query += shift_size;
	}
	/*for(int i=0;i<5;++i)
		cout << form_inputs[i].ip << " " << form_inputs[i].port << " " << form_inputs[i].name_batch_file <<"<br>";
	*/

	//try to connect to server
	int socket_remain = 0;
	int sockfd[5];
	int number_batch = 0;
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
		//connect to proxy
		serv_addr[i].sin_family = AF_INET;
		serv_addr[i].sin_addr.s_addr = inet_addr( form_inputs[i].proxy_ip );
		serv_addr[i].sin_port = htons( form_inputs[i].proxy_port );
		if((sockfd[i] = socket(AF_INET,SOCK_STREAM,0) ) < 0 ){
			cout << "error : cannot open socket " << i << endl;
			exit(-1);
		}

		if(connect( sockfd[i], (sockaddr*)&serv_addr[i], sizeof(serv_addr[i]) ) < 0){
			cout << "error : cannot connect to server " << i <<endl;
			exit(-1);
		}
		
		//write request to proxy
		unsigned char reply[8];
		unsigned char request[50];
		unsigned char *tmp_request;
		request[0] = 4; // VN
		request[1] = 1; // CD
		request[2] = (unsigned char)(form_inputs[i].port / 256);
		request[3] = (unsigned char)(form_inputs[i].port % 256);
		sscanf( form_inputs[i].ip , "%u.%u.%u.%u" , &request[4], &request[5], &request[6], &request[7] );
		strcpy( (char*)request+8, "fuckit\0" );

		write( sockfd[i], request, 15 );
		read( sockfd[i], reply, 8);
		if( reply[0] != 0){
			cout << i <<" reply version " << (unsigned int)reply[0]<< " not currect<br>" ;
			exit(-1);
		}
		if( reply[1] != 90 ){
			cout << i <<" rejected<br>";
			exit(-1);
		}

		//set it to non_blocking
		fcntl_flags = fcntl(sockfd[i],F_GETFL,0);
		fcntl(sockfd[i],F_SETFL,fcntl_flags | O_NONBLOCK);

		//open the batch file
		form_inputs[i].fp = fopen(form_inputs[i].name_batch_file , "r");

		FD_SET(sockfd[i],&active_fds);
		cout << "<td>" << form_inputs[i].ip << "</td>";
		fd_td_index[i] = number_batch++;
	}
	cout << "</tr>"<<endl << "<tr>" <<endl;
	for(int i=0;i<number_batch;++i){
		cout << "<td valign=\"top\" id=\"m"<< i << "\"></td>";
	}
	cout << "</tr>" <<endl << "</table>" <<endl;

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

				if(n <= 0){//it's closed
					//cout << "hererc" <<endl;
					socket_remain--;
					close(sockfd[i]);
					FD_CLR(sockfd[i],&active_fds);
					socket_closed = true;
					continue;
				}
				//output result
				cout << "<script>document.all[\'m" << fd_td_index[i] << "\'].innerHTML += \"";
                if(form_inputs[i].first_line){
                    cout << "0 ";
                    form_inputs[i].first_line = false;
                }
				buffer[n]='\0';
				for(int j=0;buffer[j]!='\0';++j){
					if(buffer[j] == '\n'){
						cout <<"<br>"<<++form_inputs[i].lines<<" ";
                    }
					else if(buffer[j] == '%'){
						cout << '%';
						form_inputs[i].need_to_write = true;
					}
					else if(buffer[j] == '<'){
						cout << "&lt;";
					}
					else if(buffer[j] == '>'){
						cout << "&gt;";
					}
					else if(buffer[j] == '\''){
						cout << "\\\'";
					}
					else if(buffer[j] == '\"'){
						cout << "\\\"";
					}
					else if(buffer[j] == ' '){
						cout << "&nbsp;";
					}
					else if(buffer[j] == '\r'){
						cout << "";
					}
					else
						cout << buffer[j];
				}
				cout.flush();
				cout << "\";</script>"<<endl;
				//while(1);
				//cout << "not end" <<endl;
				
				//input a line from in_batch
				if(!socket_closed && form_inputs[i].need_to_write){
					char *line = NULL;
					size_t len = 0;
					//cout << " i shall not getline" <<endl;
					ssize_t size_getline = getline(&line,&len,form_inputs[i].fp);
					if(size_getline > 0){
						cout <<"<script>document.all[\'m" << fd_td_index[i] << "\'].innerHTML += \"<b>" ;
						for(int j=0;line[j] != '\0';++j){
							if(line[j] == '\n'){
							    cout <<"<br>";
                                cout << ++form_inputs[i].lines << " ";
                            }
							else if(line[j] == '%'){
								cout << '%';
								form_inputs[i].need_to_write = true;
							}
							else if(line[j] == '<'){
								cout << "&lt;";
							}
							else if(line[j] == '>'){
								cout << "&gt;";
							}
							else if(line[j] == '\''){
								cout << "\\\'";
							}
							else if(line[j] == '\"'){
								cout << "\\\"";
							}
							else if(line[j] == ' '){
								cout << "&nbsp;";
							}
							else if(line[j] == '\r'){
								cout << "";
							}
							else
								cout << line[j];
						}
						cout << "</b>\";</script>" <<endl;
						cout.flush();
						write(sockfd[i],line,strlen(line));
						line[0]='\0';
						free(line);
						line = NULL;
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

	cout << "</font>" <<endl << "</body>" <<endl << "</html>" <<endl;

	for(int i=0;i<5;++i)
		if(form_inputs[i].port != 0)
			fclose(form_inputs[i].fp);

	return 0;
}
