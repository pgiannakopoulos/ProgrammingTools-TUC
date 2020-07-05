// ./remoteClient serverName serverPORT receivePort inputFileWithCommands
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
#include <stdbool.h>

int counter=0;
int results[4];
char packages[10000][512];
char queue[1000][512];
int packCount=0, queueCount=0;
	
void perror_exit ( char * message )	{
	perror ( message ) ;
	exit ( EXIT_FAILURE ) ;
}

void sig_usr(int signo){
    if(signo == SIGINT)
    return;
}

void time_to_stop(){
	while (waitpid(-1, NULL, 0) > 0);

	dup2(0, 2);
	fprintf(stderr, "Parent with pid: %d is closing\n", getpid());
	exit(0);
}

char* concat(char *s1, char *s2){
	char *result=malloc(strlen(s1)+strlen(s2)+1);
	strcpy(result,s1);
	strcat(result,s2);
	return result;
}

char* fileNameFromInt(int countLines, char *receivePort){//determining the name of the file to be created or updated
	char str[100];
	sprintf(str,"%d",countLines);
	return concat("output.", concat(concat(receivePort, "."), str));
}

void fileCreation(char *line, int countLines, char *receivePort){//creating a file for an answer
	char *fileName=fileNameFromInt(countLines, receivePort);
	FILE *myFile=fopen(fileName, "w");
	if(myFile==NULL)
		perror_exit("File not created.");
	if (strcmp(line, "Invalid command.")!=0)
		fputs(line, myFile);
	else
		fputs("", myFile);
	fclose(myFile);
}

void fileUpdate(char *text, char *fileName){//updating a file by adding another package t the end
	FILE *myFile2=fopen(fileName, "a");
	if(myFile2==NULL)
		perror_exit("File not created.");
	fputs(text, myFile2);
	fclose(myFile2);
}

int getId(char buf[512]){//extracting the id from a package
	char id_buf[10];
	int j = -1;
	do{
		j++;
		id_buf[j] = buf[j];
	}while(id_buf[j] != '#' && id_buf[j] != '!');
	id_buf[j] = '\0';
	return atoi(id_buf);
}

void findBufInfo(char buf[512]){//extracting the necessary information from a file like its id, index, 
	char id_buf[6];				//whether it is expecting more packages from the same answer and how many digits are the indicators
	int i = -1, id;
	//Read the id of the message
	do{
		i++;
		id_buf[i] = buf[i];
	}while(id_buf[i] != '#' && id_buf[i] != '!');

	bool final = id_buf[i] == '!';

	id_buf[i] = '\0';
	id = atoi(id_buf);

	char index_buf[200];
	int index, j=0;
	//Read the index of the package
	do{
		i++;
		index_buf[j] = buf[i];
		j++;
	}while(index_buf[j-1] != '#');
	index_buf[j] = '\0';
	index = atoi(index_buf);

	results[0]=id;
	results[1]=final;
	results[2]=index;
	results[3]=i;
}

bool allPacksReady(int id, int index){//checking whether all packages are received so the storing process begins
	int i, packId, packs=0;
	
	for (i=0; i<packCount; i++){
		if (packages[i][0]=='\0')
			continue;
		packId = getId(&packages[i][0]);
		if (id==packId)
			packs++;
	}
	if (index==packs)
		return true;
	else
		return false;
}

void timeToStore(int id, int index, char *receivePort, char buf[512]){//storing the packages on the correct order
	int i, j, k, l, packId, countIndex=0, packIndex, fileNumber;
	char firstIndicator='\0', *fileName, index_buf[200];

	fileNumber=counter;
	fileName=fileNameFromInt(fileNumber, receivePort);

	for (i=0; i<packCount; i++){
		if (packages[i][0]=='\0')
			continue;
		if (index==countIndex)
			break;
		packId=getId(&packages[i][0]);
		if (id==packId){
			j=0;
			k=0;
			firstIndicator='\0';
			while(firstIndicator!='#'){
				firstIndicator=packages[i][j];
				j++;
			}
			do{
				index_buf[k] = packages[i][j];
				k++;
				j++;
			}while(index_buf[k-1] != '#');
			index_buf[k] = '\0';
			packIndex = atoi(index_buf);
			if (packIndex==countIndex && countIndex==0){
				fileCreation(&packages[i][j], counter, receivePort);
				countIndex++;
				for (l=0; l<512; l++){
					packages[i][l]='\0';
				}
				i=0;
			}else if(packIndex==countIndex){
				fileUpdate(&packages[i][j], fileName);
				countIndex++;
				for (l=0; l<512; l++){
					packages[i][l]='\0';
				}
				i=0;
			}
		}
	}
	fileUpdate(&buf[j], fileName);
}

int getIndexById(int id, char buf[512]){//finding the index of a queue package based on its id
	int i, j, k, packId;
	char index_buf[200], firstIndicator;

	for (i=0; i<queueCount; i++){
		packId=getId(&queue[i][0]);
		if (id==packId){
			j=0;
			k=0;
			while(firstIndicator!='!'){
				firstIndicator=queue[i][j];
				j++;
			}
			do{
				j++;
				index_buf[k] = buf[j];
				k++;
			}while(index_buf[k-1] != '#');
			index_buf[k] = '\0';
			return atoi(index_buf);
		}
	}
	return -1;
}

void childProcess (int sock_udp, FILE *file, char *receivePort){//child receiving answers and storing them to files
	char buf[512];
	int i, j, packId;

	if ( recvfrom ( sock_udp , buf , 512 , 0 , NULL , NULL ) < 0) {
		perror ( " recvfrom " ) ; 
		exit (1) ; 
	}
	findBufInfo(buf);

	//printf ( "Package: %s\n" , buf);

	if (results[1]==1 && results[2]==0){
		counter++;
		fileCreation(&buf[results[3]+1], counter, receivePort);
	}else if(results[1]==0){
		strcpy(&packages[packCount][0], buf);
		packCount++;
		
		//check queue
		int index=getIndexById(results[0], buf);
		if (index!=-1 && allPacksReady(results[0],index)==true){
				counter++;
				for (i=0; i<queueCount; i++){
					packId=getId(&queue[i][0]);
					if (results[0]==packId)
						break;
				}
				timeToStore(results[0], index, receivePort, &queue[i][0]);
				for (j=0; j<512; j++){
					queue[i][j]='\0';
				}
		}
	}else{
		if(allPacksReady(results[0], results[2])==false){//checking if we have all packages needed for storing
			strcpy(&queue[queueCount][0],buf);
			queueCount++;
			//printf("Failure\n");
		}else{
			counter++;
			timeToStore(results[0], results[2], receivePort, buf);
			//printf("Success\n");
		}
	}
}

int main(int argc, char *argv[]){
	FILE *file, *fileForCount;
	char *line=NULL, *lineCount=NULL;
	size_t len=0, lenCount=0;
	ssize_t reading, readingCount;
	int countLines=0, numOfLines=0;
	int childpid;

	static struct sigaction act5;

	act5.sa_handler = time_to_stop;
	act5.sa_flags = SA_RESTART;
	sigfillset (&(act5.sa_mask)) ;
	sigaction ( SIGCHLD , & act5, NULL ) ;  

	if ( argc != 5) {
	printf ( " Please give serverName serverPORT receivePort inputFileWithCommands \n " ) ;
	exit (1) ;}

	file=fopen(argv[4], "r");
	fileForCount=fopen(argv[4], "r");
	//char *server_name="localhost";
	int server_port = atoi (argv[2]), rec_port = atoi (argv[3]), sock_tcp, sock_udp;

	struct sockaddr_in server;
	struct sockaddr_in client;
	struct sockaddr * serverptr = ( struct sockaddr *) & server ;
	struct sockaddr * clientptr = ( struct sockaddr *) & client ;
	struct hostent * rem ;

	/* Create socket */
	if ((sock_tcp = socket (AF_INET, SOCK_STREAM , 0) ) < 0){
		perror_exit ( " socket " ) ;
	}
	printf("Socket: %d\n", sock_tcp );
	//printf("%s\n", server_name );

	/* Find server address */
	if ( (rem = gethostbyname(argv[1])) == NULL ) {
		herror ( " gethostbyname " ) ; 
		exit (1) ;
	}
	printf("Server name: %s\n", rem->h_name );


	server.sin_family = AF_INET; /* Internet domain */
	memcpy(&(server.sin_addr), rem->h_addr , rem->h_length) ;
	server.sin_port = htons(server_port); /* Server port */

	/* Setup my address */
	client.sin_family = AF_INET ; /* Internet domain */
	client.sin_addr.s_addr = INADDR_ANY ; /* Any address */
	client.sin_port = htons(rec_port) ;

	//printf("my address %s and my port %d\n", ntohs(client.sin_port));

	if (( sock_udp = socket ( AF_INET , SOCK_DGRAM , 0) ) < 0) {
		perror ( " socket " ) ; exit (1) ; }

	// Make port resuable
	int reuse = 1;
	if ((setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))) <0){
		perror_exit("setsockopt(SO_REUSEADDR) failed");
	}

	if ((setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))) <0){
		perror_exit("setsockopt(SO_REUSEADDR) failed");
	}

	if ( bind ( sock_tcp , clientptr , sizeof ( client ) ) < 0) {
		perror ( " bind " ) ; exit (1) ; }
	
	if ( bind ( sock_udp , clientptr , sizeof ( client ) ) < 0) {
		perror ( " bind " ) ; exit (1) ; }
	
	/* Initiate connection */
	if ( connect ( sock_tcp , serverptr , sizeof ( server ) ) < 0)
		perror_exit ( " connect " ) ;
	printf ( "Connecting to %s port %d \n" , argv[1], server_port ) ;
	
	while((readingCount=getline(&lineCount,&lenCount,fileForCount))!=-1){//find the number of commands that exist in the input file
		if (lineCount[0]!='\n')
			numOfLines++;
	}
	fclose(fileForCount);
	childpid=fork();
	if (childpid==0){//child reading
		while(1){
			childProcess(sock_udp, file, argv[3]);
			if (counter==numOfLines){//child has received all answers expected
				close(sock_udp);
				exit(1);
			}
		}
	}else{//parent writing
		while((reading=getline(&line,&len,file))!=-1){
			if ( write ( sock_tcp , line , strlen(line)) < 0)
				perror_exit ( " write " );
			if (countLines!=0 && countLines%10==0)//every 10 commands,client sleeps for 5 seconds
				sleep(5);
			countLines++;
		}
		close(sock_tcp);
		fclose(file);
	}
}
