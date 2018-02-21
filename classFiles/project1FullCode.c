#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

//Constants //TODO figure out how to really handle these values
const int NUM_THREADS = 4;
const int BUF_SIZE = 10;

//intiating mutex and conditions from the start so that it could be shared by both the consumer and producer method
//without having to make it a parameter
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_cons = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_prod = PTHREAD_COND_INITIALIZER;

/*
1) Struct to hold the jobs
2) list of threads performing work
*/

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

struct Request{
	int thread_id;
	int thread_count;
	int thread_html_count;
	int thread_image_count;
	int hit;
	int listenfd;
	int socketfd;
};

struct Buffer{//TODO: make function to initilize this
	struct Request *requests;

	//FOR STATS
	int number_of_requests_arrived;
	//time_since_server
	int number_of_requests_dispatched;
	//req_dispatch_time
	int number_of_requests_completed; //could be either in web() or main()
	//req_complete_time//this would be within the web() function

	//TO MAKE BUFFER A QUEUE
	int add;//places to add nexr
	int rem;//place to remove next element
	int num;//number of lements in buffer

};
//Creating buffer up here!
struct Buffer requestBuffer;

static int dummy; //keep compiler happy

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
		break;
	case FORBIDDEN:
		dummy = write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
		(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
		break;
	case NOTFOUND:
		dummy = write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
		(void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
		break;
	case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		dummy = write(fd,logbuffer,strlen(logbuffer));
		dummy = write(fd,"\n",1);
		(void)close(fd);
	}
	if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}


/* this is a child web server process, so we can exit on errors */
void web(int fd, int hit)
{
	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	static char buffer[BUFSIZE+1]; /* static so zero filled */

	ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
	if(ret == 0 || ret == -1) {	/* read failure stop now */
		logger(FORBIDDEN,"failed to read browser request","",fd);
	}
	if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
		buffer[ret]=0;		/* terminate the buffer */
	else buffer[0]=0;
	for(i=0;i<ret;i++)	/* remove CF and LF characters */
		if(buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i]='*';
	logger(LOG,"request",buffer,hit);
	if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
		logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
	}
	for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
		if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
		if(buffer[j] == '.' && buffer[j+1] == '.') {
			logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
		}
	if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
		(void)strcpy(buffer,"GET /index.html");

	/* work out the file type and check we support it */
	buflen=strlen(buffer);
	fstr = (char *)0;
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			break;
		}
	}
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	logger(LOG,"SEND",&buffer[5],hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	logger(LOG,"Header",buffer,hit);
	dummy = write(fd,buffer,strlen(buffer));

    /* Send the statistical headers described in the paper, example below

    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	dummy = write(fd,buffer,strlen(buffer));
    */

    /* send file in 8KB block - last block may be smaller */
	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
		dummy = write(fd,buffer,ret);
	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
	close(fd);
	exit(1);
}

void producer(int *listenfdAddress){
	int hit, socketfd;
	int listenfd = *listenfdAddress;
	socklen_t length;

	static struct sockaddr_in cli_addr; /* static = initialised to zeros */

	for(hit=1; ;hit++) {
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR,"system call","accept",0);


		pthread_mutex_lock(&m);
			if(requestBuffer.num > BUF_SIZE){
				exit(1);//buffer overflow
			}
			while(requestBuffer.num == BUF_SIZE) {
				pthread_cond_wait(&c_prod, &m);
			}
			//critical section: create request object
			struct Request newRequest;
			bzero(&newRequest, sizeof(newRequest));
			newRequest.thread_id = requestBuffer.number_of_requests_dispatched;
			newRequest.thread_count = 0;
			newRequest.thread_html_count = 0;
			newRequest.thread_image_count = 0;
			newRequest.hit = hit;//the hit in the for loop (which may have to be reconfigured?)
			newRequest.listenfd = listenfd;//from earlier in the code
			newRequest.socketfd = socketfd;////from earlier in the code
			//and add the reqeust object to the buffer
			requestBuffer.requests[requestBuffer.add] = newRequest;
			requestBuffer.add = (requestBuffer.add +1) % BUF_SIZE;
			requestBuffer.num++;

		pthread_mutex_unlock(&m);
		pthread_cond_signal(&c_cons);
		//printf("producer inserted %d\n", newRequest); fflush(stdout);//TODO: not sure what you wanted with this code but its not properly formated so commented it out

		printf("producer looping\n"); fflush (stdout);
	}
}

void consumer(void * args){
	struct Request currentRequest;
	while(1){
		pthread_mutex_lock(&m);
		if(requestBuffer.num < 0){
			exit(1);//meaningless error messsage
		}
		while(requestBuffer.num == 0){
			pthread_cond_wait (&c_cons, &m);
		}
		currentRequest = requestBuffer.requests[requestBuffer.rem];
		requestBuffer.rem = (requestBuffer.rem+1) % BUF_SIZE;
		requestBuffer.num--;
		/*
		switch (mode) {

        case ANY:
            printf("Running ANY scheduling as FIFO scheduling: ");
        case FIFO:
            printf("Running FIFO scheduling ");
            currentRequest = requestBuffer[requestBuffer.rem];
			requestBuffer.rem = (requestBuffer.rem+1) % BUF_SIZE;
			requestBuffer.num--;
            break;
        case HPIC:
            printf("Running HPIC scheduling:");
            break;
        case HPHC:
           printf("Running HPHC scheduling: ");
           break;
        default:
   		}
   		*/
		(void)close(currentRequest.listenfd);
	pthread_mutex_unlock(&m);
	web(currentRequest.socketfd, currentRequest.hit);//TODO: this is a big mistake, the mutex will not be unlocked until web returns, So i switched it. it will not cause errors when multiple threads try to read this variable
	pthread_cond_signal (&c_prod);
	printf("Consume value %d\n", 1);
	}
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd;


	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

		if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
	"\tnweb is a small and very safe mini web server\n"
	"\tnweb only servers out file/web pages with extensions named below\n"
	"\t and only from the named directory or its sub-directories.\n"
	"\tThere is no fancy features = safe and secure.\n\n"
	"\tExample: nweb 8181 /home/nwebdir &\n\n"
	"\tOnly Supports:", VERSION);
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
	"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
	"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
		exit(0);
	}
	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1){
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}
	/* Become deamon + unstopable and no zombies children (= no wait()) */

	if(fork() != 0)
		return 0; /* parent returns OK to shell */
	(void)signal(SIGCHLD, SIG_IGN); /* ignore child death */
	(void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
	for(i=0;i<32;i++)
		(void)close(i);		/* close open files */
	(void)setpgrp();		/* break away from process group */
	logger(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		logger(ERROR, "system call","socket",0);
	port = atoi(argv[1]); /*converts str to int*/
	if(port < 0 || port >60000)
		logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		logger(ERROR,"system call","bind",0);
	if( listen(listenfd,64) <0)
		logger(ERROR,"system call","listen",0);




	//Initialize our threads
	int j;
	pthread_t prod;
	pthread_t con_threads[NUM_THREADS];

	//Change attribute default from joinable to detachable
	pthread_attr_t attr;
	int pthread_attr_init(pthread_attr_t *attr); //TODO does this even make sense?
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	//initialize buffer (already created globally)
	requestBuffer.requests = (struct Request *) malloc(sizeof(struct Request) * BUF_SIZE);

	//initializing producer thread, will take in reqeusts, package them, and place them on a queue
	pthread_create(&prod, &attr, producer, NULL);

	//initializning the pool of threads, NUM_THREADS will be command-line input
	//attr was changed to make the threads detachable rather than joinable
	for(j = 0; j < NUM_THREADS; i++){
	pthread_create(&con_threads[j], &attr, consumer,NULL);
	}
}
