// ./remoteServer portNumber numChildren
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MSGSIZE 512 
#define COMMAND_SIZE_LIMIT 100
#define PACKAGE_SIZE 512
#define IP_SIZE 10
//#define COMAND_ID_SIZE 10 //timeToStop

//Parent global variables
int *child_weight;
int pipe_ready = 1, start_reading = 0;
int *pid, numChildren;

//Parent functions
void read_messages ( int sock, int client_port, char* client_ip, int p);
void pperror_exit ( char * message ) ;
void child_terminated (int sig, siginfo_t *siginfo, void *context);
pid_t select_child();
void removeSpaces(char * string);

//Parent signal handlers
void pipe_available();
void child_refresh(int sig, siginfo_t *siginfo, void *context);
void time_to_stop();

//Child main function
void child_server ( int pipe_read) ;


int main (int argc, char * argv []) {
	//Save the arguments
	int port;

	if ( argc != 3){
		printf( " Please give port number and number of children \n") ;
		exit(1);
	}

	if ( atoi ( argv [2]) <= 0)
	{
		printf("Please give positive number of children \n") ;
		exit(1);
	}
	
	port = atoi ( argv [1]) ;
	numChildren = atoi ( argv [2]) ;

	//Initialize children
	int p[2];

	pid = (int*)malloc(sizeof(int)*numChildren);
	if (pid == NULL) { 
		pperror_exit("malloc"); 
	}

	child_weight = (int*)malloc(sizeof(int)*numChildren);
	if (child_weight == NULL) { 
		pperror_exit("malloc"); 
	}

	if ( pipe(p) == -1) { 
		pperror_exit("pipe call"); 
	}

	for (int i = 0; i < numChildren; ++i)
	{
		if ( ( pid[i] = fork () ) < 0 ){
			pperror_exit("fork failed");
		}
			
		if ( pid[i] ==0) {
			//printf("%d Created child with pid: %d\n",i, getpid()); 
			close(p[1]); 
			child_server(p[0]);
		}else {
			child_weight[i] = 0;
		}
		
	}

	close(p[0]);

	//Configure the signals
	static struct sigaction act1, act2, act3, act4, act5;

	// the handler is set to IGNORE
	act1.sa_handler = SIG_IGN;
	act1.sa_flags = SA_RESTART;
	sigfillset (&(act1.sa_mask)) ;
	sigaction ( SIGPIPE , & act1 , NULL ) ;

	/* Reap dead children asynchronously*/
	act2.sa_flags = SA_SIGINFO | SA_RESTART;
	act2.sa_sigaction = child_terminated;
	sigfillset (&(act2.sa_mask)) ;
	sigaction ( SIGCHLD , & act2, NULL ) ;

	//Update pipe availability
	act3.sa_handler = pipe_available;
	act3.sa_flags = SA_RESTART;
	sigfillset (&(act3.sa_mask)) ;
	sigaction ( SIGUSR2 , & act3, NULL ) ;

	//Update child availability
	act4.sa_flags = SA_SIGINFO | SA_RESTART;
	act4.sa_sigaction = child_refresh;
	sigfillset (&(act4.sa_mask)) ;
	sigaction ( SIGUSR1 , & act4, NULL ) ;

	//Time to stop
	act5.sa_handler = time_to_stop;
	act5.sa_flags = SA_RESTART;
	sigfillset (&(act5.sa_mask)) ;
	sigaction ( SIGINT , & act5, NULL ) ;   //CTR-C
	

	//Start listening for connections
	int sock , newsock;
	struct sockaddr_in server , client ;
	socklen_t clientlen ;
	struct sockaddr * serverptr =( struct sockaddr *) & server ;
	struct sockaddr * clientptr =( struct sockaddr *) & client ;
	
	/* Create socket */
	if (( sock = socket ( AF_INET , SOCK_STREAM , 0) ) < 0){
		pperror_exit ( "socket" );
	}

	server.sin_family = AF_INET;    /* Internet domain */
	server.sin_addr . s_addr = htonl(INADDR_ANY) ;
	server.sin_port = htons(port) ;
	
	// Make port resuable
	int reuse = 1;
	if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))) <0){
		pperror_exit("setsockopt(SO_REUSEADDR) failed");
	}
	
	/* Bind socket to address */
	if ( bind ( sock , serverptr , sizeof ( server ) ) < 0){
		pperror_exit ( " bind " ) ;
	}
	
	/* Listen for connections */
	if ( listen ( sock , numChildren) < 0){ 
		pperror_exit ( " listen " ) ;
	}

	printf ( "Listening for connections to port %d \n " , port ) ;

	//Declare the FD set
	fd_set active_fd_set, read_fd_set;
	int client_port;
	char *client_ip;

	FD_ZERO(&active_fd_set);
	FD_SET(sock, &active_fd_set); //add listeting port

	//Request handling
	while (1) {
		read_fd_set = active_fd_set;

		//Check if some sends data
		if ((select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL)) < 0){
			pperror_exit("select");
		}

		for (int i = 0; i < FD_SETSIZE; ++i)
		{
			if(FD_ISSET(i, &read_fd_set)){
				if(i == sock){

					/* accept connection */
					clientlen = sizeof(client);

					if ((newsock = accept(sock, clientptr, &clientlen)) < 0){
						pperror_exit("accept");
					}
	
					/* Find client’s address */
					struct sockaddr_in* pV4Addr = (struct sockaddr_in*)clientptr;
					struct in_addr ipAddr = pV4Addr->sin_addr;

					client_ip = (char*)malloc(sizeof(char)*INET_ADDRSTRLEN);
					if (client_ip == NULL) { 
						pperror_exit("malloc"); 
					}

					inet_ntop( AF_INET, &ipAddr, client_ip, INET_ADDRSTRLEN );
					client_port = ntohs(client.sin_port);

					printf("connection established with IP : %s and PORT : %d\n", 
														client_ip, client_port); 
					FD_SET(newsock, &active_fd_set);
				}else{
					//New message from already connected client
					struct sockaddr_in addr;
				    socklen_t length = sizeof(struct sockaddr_in);

				    if (getpeername(i, (struct sockaddr *)&addr, &length) != 0){
				        pperror_exit("getpeername");
				    }

				    char * t = inet_ntoa(addr.sin_addr); 
				    strcpy(client_ip, t);
				    client_port = ntohs(addr.sin_port);
					
					read_messages(i, client_port, client_ip, p[1]);

					close(i);
					FD_CLR(i, &active_fd_set);
					printf("Connection with client with IP: %s and Port: %d CLOSED!\n", client_ip, client_port );
				}
			}
		}
	}
}

//Parent functions

//read messages and forward them to the childs
void read_messages(int sock, int client_port, char* client_ip, int p){
	char buf[MSGSIZE];
	int read_size;
	int child;

	char psize[103], psock[6], *message, delim[] = "#";
	int message_len;

	while ( (read_size = read(sock, buf, MSGSIZE)) > 0) {     //read client message
	    char* splited_buf;
	    splited_buf = strtok(buf,"\n");
	    while(splited_buf != NULL)  						  //split the message to lines
	    {	       
			if ((strlen(splited_buf)+1) > COMMAND_SIZE_LIMIT){  //check message lenght
				printf("Message skipped: %s\n", splited_buf);
			}else{

				sprintf(psize, "%d", read_size);
				sprintf(psock, "%d", client_port);

				//message format: size # IP # Port # command 
				message_len = strlen(psize) + strlen(client_ip)+ strlen(psock)+read_size+3*strlen(delim);
				message = (char *)malloc(message_len*sizeof(char)); 
				if (message == NULL) { 
					pperror_exit("malloc"); 
				}

				strcpy(message, psize);
				strcat(message, delim);
				strcat(message, client_ip);
				strcat(message, delim);
				strcat(message, psock);
				strcat(message, delim);
				strcat(message, splited_buf);
				
				int w = 0, wsize, pointer = 0;
				char writing_buffer[PIPE_BUF];
				
				//printf("Received Command: %s\n", splited_buf );
				while(pipe_ready == 0){						//check pipe availabilty
					sleep(1);
				}

				//put the message to pipe
				do{
					wsize = message_len > PIPE_BUF ? PIPE_BUF : message_len;
					memcpy(writing_buffer, &message[pointer], message_len);
					//printf("Message sent: %s with len: %d\n", writing_buffer, wsize );
					w = write(p,writing_buffer,wsize);
					if (w  < 0){
						pperror_exit ( " write " ) ;
					}
					pointer += w;
					message_len -= w;

				}while(w != wsize);
				
				//select a child
				do{
					child = select_child();
				}while(child<0);

				kill(pid[child], SIGUSR1);

				pipe_ready = 0;
				memset(writing_buffer, 0, sizeof(writing_buffer));
				free(message);
			}
		splited_buf = strtok(NULL,"\n");
	    }
	    memset(buf, 0, sizeof(buf));   //reset buffer
	}

}

//select a child to run the command
pid_t select_child(){
	int index = -1;
	 
	for (int i = 0; i < numChildren; i++){
	    if (child_weight[i] == 0 && pid[i] > 0){
	    	index = i;
	    	child_weight[index] = 1;
	    	break;
	    }
	}
	return index;
}

//exit with error
void pperror_exit(char * message){
	perror(message);
	time_to_stop();
}

//Parent Signal Handlers
void pipe_available(){
	pipe_ready = 1;
}

void child_refresh(int sig, siginfo_t *siginfo, void *context){
	for (int i = 0; i < numChildren; i++){
	    if (pid[i] ==  siginfo->si_pid){
	    	child_weight[i] = 0;
	    	break;
	    }
	}
}

void time_to_stop(){
	//kill all children with a signal
	for (int i = 0; i < numChildren; i++){
	    if (pid[i] > 0 ){
	    	kill(pid[i], SIGINT);
	    	child_weight[i] = 0;
	    }
	}
	while (waitpid(-1, NULL, 0) > 0);

	//close the parent
	free(pid);
	free(child_weight);

	dup2(0, 2);
	fprintf(stderr, "Parent with pid: %d is closing\n", getpid());
	exit(0);
}


void child_terminated(int sig, siginfo_t *siginfo, void *context) {
	//Update pid table with active children
	for (int i = 0; i < numChildren; i++){
	    if (pid[i] ==  siginfo->si_pid){
	    	while (waitpid(-1, NULL, WNOHANG) > 0);      //wait child
	    	pid[i] = -1;
	    	child_weight[i] = 0;
	    	break;
	    }
	}

	//Check if there available children
	for (int i = 0; i < numChildren; i++){
	    if (pid[i] > 0 ){
	    	return;
	    }
	}
	time_to_stop();
}


/* --- CHILD SECTION -- */

//Child global variables
int alive_child = 1;
char *id;

//Child functions
int readCommand(int pipe_read, char* buf, char *ip);
int recognizeCommand(char* string);
void runAndSendCommand(char* cmd, struct sockaddr* clientptr, socklen_t clientlen, int sock);
void cperror_exit();
void sendPackage(int p_index, char* response, int final, struct sockaddr* clientptr, socklen_t clientlen, int sock);

//Child signal handlers
void enable_reader();
void shut_down();


void child_server (int pipe_read ) {
	static struct sigaction act1, act2, act3;

	// the handler is set to IGNORE
	act1.sa_handler = SIG_IGN;
	sigfillset (&(act1.sa_mask)) ;
	sigaction(SIGPIPE , & act1 , NULL ) ;

	act2.sa_handler = enable_reader;
	sigfillset (&(act2.sa_mask)) ;
	sigaction(SIGUSR1 , &act2, NULL ) ;

	act3.sa_handler = shut_down;
	sigfillset (&(act3.sa_mask)) ;
	sigaction(SIGINT , &act3, NULL ) ;

	char buf[COMMAND_SIZE_LIMIT], ip[IP_SIZE], invalid_message[] = "Invalid command.";
	int client_port, sock;

	struct sockaddr_in client;
	struct sockaddr * clientptr = ( struct sockaddr *) & client ;
	socklen_t clientlen;
	
	/* Create socket */
	if (( sock = socket ( AF_INET , SOCK_DGRAM , 0) ) < 0){
		cperror_exit ( "socket" );
	}
	memset(buf, 0, sizeof(buf));   //initialize buffer - necessary for concat in readCommand

	//initialise the ID
	id = (char*)malloc(sizeof(char)*(ceil(numChildren/10) + 2));
	if (id == NULL) { 
		pperror_exit("malloc"); 
	}
	sprintf(id, "%d", getpid()%numChildren);

	int part_id = 0;
	char* ppart_id = &id[strlen(id)];


	while(alive_child == 1){
		if (start_reading == 1 ){
			client_port = readCommand(pipe_read, buf, ip);
			kill(getppid(),SIGUSR2);
			start_reading = 0;
		
			sprintf(ppart_id, "%d", part_id);

			/* Setup client's struct */
			client.sin_family = AF_INET; /* Internet domain */
			client.sin_addr.s_addr = inet_addr(ip); /* Client address */
			client.sin_port = htons(client_port) ;
			clientlen = sizeof(client);

			switch(recognizeCommand(buf)){
				case 0: //valid command od linux: ls, rm, cut, cat , grep of half-valid
					runAndSendCommand(buf, clientptr, clientlen, sock);
					kill(getppid(),SIGUSR1);      //tell parent that you are available for the next command
					break;
				case 1: //end command
					shut_down();
					break;
				case 2: //time to stop command
					kill(getppid(),SIGINT);
					break;
				case -1: //invalid command
					sendPackage(0, invalid_message, 1, clientptr, clientlen, sock);
					kill(getppid(),SIGUSR1);      //tell parent that you are available for the next command
					break;
			}
			memset(buf, 0, sizeof(buf));   //reset buffer

			part_id = (part_id < numChildren) ? part_id+1 : 0;
		}
	
	}
	//Closing
	close(sock);         //close the socket
	close(pipe_read);	 //close the pipe
	free(id);
	dup2(0, 2);
	fprintf(stderr, "Child with pid: %d is closing\n", getpid() );
	exit(0);    //exit sends SIGCHLD signal to parent
}

//Child functions

//get the command, IP and Port from pipe
int readCommand(int pipe_read, char* buf, char *ip){
	int i = -1, length, read_size, port;
	char size_buf[7], temp_buf[PIPE_BUF];
	char port_buf[5];

	//Read the size of the message
	do{
		i++;
		if (read (pipe_read, size_buf+i, 1) < 0){
			cperror_exit("read");
		}
	}while(size_buf[i] != '#');

	size_buf[i] = '\0';
	length = atoi(size_buf);
	//printf("Message length: %d\n", length);

	//Read IP
	i = -1;
	do{
		i++;
		if (read ( pipe_read , ip + i, 1) < 0){
			cperror_exit("read");
		}
	}while(ip[i] != '#');

	ip[i] = '\0';
	//printf("IP: %s\n", ip);

	//Read port
	i = -1;
	do{
		i++;
		if (read ( pipe_read , port_buf + i, 1) < 0){
			cperror_exit("read");
		}
	}while(port_buf[i] != '#');

	port_buf[i] = '\0';
	port = atoi(port_buf);
	//printf("Port: %d\n", port);
	
	//Read the message
	while(length > 0 ){
		if(length >= PIPE_BUF){
			if ((read_size = read (pipe_read, temp_buf , PIPE_BUF)) < 0){
				cperror_exit("read");
			}
		}else{
			if ((read_size = read ( pipe_read , temp_buf , length)) < 0){
				cperror_exit("read");
			}
		}
		strcat(buf, temp_buf);
		memset(temp_buf, 0, sizeof(temp_buf));   //reset buffer
		length = length - read_size;
	}
	//printf("Command from pipe: %s\n", buf);
	return port;	
}

//get the result of the command and send it
void runAndSendCommand(char* cmd, struct sockaddr* clientptr, socklen_t clientlen, int sock) {
    char response[MSGSIZE];
    int p_index = 0, c , i = 0;
    FILE* stream = popen(cmd, "r");

    while(1){
    	c = fgetc(stream);
    	response[i] = c;
    	char index[6];
    	sprintf(index, "%d", p_index);
    	if(c == EOF){
    		response[i] = '\0';
    		sendPackage(p_index, response, 1, clientptr, clientlen, sock);
    		break;
    	}else if (i == MSGSIZE-4-strlen(index)-strlen(id)){
    		response[i+1] = '\0'; // null terminate string		
    		/* Send message */
    		sendPackage(p_index, response, 0, clientptr, clientlen, sock);
    		p_index++;
    		i = -1;
    	}
    	i++;
    }
    pclose(stream); 
}

//Send the response to the client with the desired format
void sendPackage(int p_index, char* response, int final, struct sockaddr* clientptr, socklen_t clientlen, int sock){
	char delim[] = "#", end_delim[] = "!", *package, index[6], psize[4];
	int package_len;

	sprintf(index, "%d", p_index);
    sprintf(psize, "%ld", strlen(response));

	//message: size # socket # command #
	package_len = strlen(id) + strlen(response)+strlen(index)+2*strlen(delim)+1;
	package = (char *)malloc(package_len*sizeof(char));

	if (package == NULL) { 
		pperror_exit("malloc"); 
	} 

	strcpy(package, id);
	if(final == 1){
		strcat(package, end_delim);
	}else{
		strcat(package, delim);
	}
	strcat(package, index);
	strcat(package, delim);
	strcat(package, response);
	package[package_len-1] = '\0';

	//printf("Package Sent: %s with size: %ld\n", package, strlen(package));
    if (sendto(sock, package, package_len, 0, clientptr, clientlen ) < 0 ){
		cperror_exit("sendto");
	}
	free(package);
}

//Recognize the command and check if it is valid
int recognizeCommand(char* string){
	char tmt[] = "timeToStop", end[]= "end", quen[] = ";", dpipe[] = "|";
	char ls[] = "ls", cat[] = "cat", cut[] = "cut", grep[] = "grep", tr[] = "tr";
	char *command_list[5], *pointer;
	int i = 0, valid_command = 0;

	command_list[0] = ls;
	command_list[1] = cat;
	command_list[2] = cut;
	command_list[3] = grep;
	command_list[4] = tr;

	removeSpaces(string);

	if(strcmp(string, end) == 0){
    	return 1;
    }else if(strcmp(string, tmt) == 0){
    	return 2;
    }

	//check the first command
	for (i = 0; i < 5; i++){
		//check if starts with a valid command
		if (strncmp(string, command_list[i], strlen(command_list[i])) == 0){ 
			valid_command = 1;
			if ((pointer = strstr(string, quen)) != NULL){       //check for ';'  
				i = 0;
				while(*pointer != '\0'){
					*(pointer + i) = '\0';
					i++;
				}
			}
		}
	}

	char *temp_command = string;
	//check for pipes
	if(valid_command == 1){
		//while there is a pipe delimiter and valid command
		while(valid_command == 1 && (pointer = strstr(temp_command, dpipe)) != NULL){  
			temp_command = pointer + 1;   //get the command part after '|' delim
			removeSpaces(temp_command);
			valid_command = 0;    //reset validator
			
			for (i = 0; i < 5; i++){                            //check for valid command
				if (strncmp(temp_command, command_list[i], strlen(command_list[i])) == 0){
					valid_command = 1;
				}
			}
		}
	}else{
		printf("Invalid command: %s\n",string);
		return -1;
	}

	i = 0;
	if(valid_command == 0){
		while(*pointer != '\0'){
			*pointer = '\0';
			pointer++;
		}
	}
	//printf("Final Command: %s\n", string);
	return 0;  
}

//Remove space at the beginning of a string
void removeSpaces(char * string){
	char* temp_buf;
	int i = 0, j = 0;

	temp_buf = (char *)malloc(sizeof(char)*strlen(string));
	if (temp_buf == NULL) { 
		pperror_exit("malloc"); 
	}

	//find the first position without spaces
	while(string[i] == ' '){
		i++;
	}

	while(i < strlen(string)){
		temp_buf[j] = string[i];
		i++;
		j++;
	}

	temp_buf[j] = '\0';

	strcpy(string, temp_buf);
	free(temp_buf);
}


//Child signal handlers
void shut_down(){
	alive_child = 0;
}

void enable_reader(){
	start_reading = 1;
}

void cperror_exit(char * message){
	perror(message);
	kill(getppid(),SIGINT);
}
