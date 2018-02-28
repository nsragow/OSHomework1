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
#include <sys/time.h>
#include <stdbool.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404
#define TEXT        0
#define IMAGE	    1
#define FIFO	    2
#define ANY         3
#define HPIC	    4
#define HPHC	    5
#define IMAGE_TYPE  0 
#define HTML_TYPE 1


//Constants
int NUM_THREADS;
int BUF_SIZE;
int mode;



//intiating mutex and conditions from the start so that it could be shared by both the consumer and producer method
//without having to make it a parameter
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_cons = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_prod = PTHREAD_COND_INITIALIZER;
struct timeval startTime;
struct timezone startZone;

int amount_of_requests_read;
int amount_of_requests_passed;


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
	char buf[BUFSIZE+1];
	char * fstr;
	int thread_id;
	int thread_count;
	int thread_html_count;
	int thread_image_count;
	int hit;
	int listenfd;
	int socketfd;
	int type;
	int read_before;
	int passed_at_arrival;
	int passed_at_passed;
	struct timeval * firstSaw;
	struct timeval * passedToConsumer;
	struct timeval * finishedReading;
};

struct Buffer{
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

void timeSubtract(struct timeval * original,struct timeval * now, struct timeval * to_return){
  long original_sec = original->tv_sec;
  long original_micro = original->tv_usec;
  long now_sec = now->tv_sec;
  long now_micro = now->tv_usec;

  to_return->tv_sec = now_sec - original_sec;

  if(now_micro < original_micro){
    long diffirence =  original_micro - now_micro;
    to_return->tv_usec = 1000000 - diffirence;
    to_return->tv_sec--;
  }
  else{
    to_return->tv_usec = now_micro - original_micro;
  }
}

/* this is a child web server process, so we can exit on errors */
void web(int fd, int hit, struct Request * requestFromWeb, int thread_id, int amount_of_requests_passed)
{

	int file_fd, buflen;
	char * fstr;
	long i,len;



	/* work out the file type and check we support it */
	buflen=strlen(requestFromWeb->buf);
	fstr = (char *)0;
	
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if( !strncmp(&requestFromWeb->buf[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			break;
		}
	}
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",requestFromWeb->buf,fd);

	if(fstr == extensions[0].filetype){
		requestFromWeb->thread_image_count++;
	}
	else if(fstr == extensions[1].filetype){
		requestFromWeb->thread_image_count++;
	}
	else if(fstr == extensions[2].filetype){
		requestFromWeb->thread_image_count++;
	}
	else if(fstr == extensions[3].filetype){
		requestFromWeb->thread_image_count++;
	}
	else if(fstr == extensions[4].filetype){
		requestFromWeb->thread_image_count++;
	}
	else{
		requestFromWeb->thread_html_count++;
		logger(LOG, "faie","report",requestFromWeb->thread_html_count);
	}


	if(( file_fd = open(&requestFromWeb->buf[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&requestFromWeb->buf[5],fd);
	}
	struct timeval       arrivalHere;
	struct timeval arrivalSinceStart;
	struct timezone     arrivalHereZ;
	gettimeofday(&arrivalHere,&arrivalHereZ);
	timeSubtract(&startTime,&arrivalHere,&arrivalSinceStart);
	requestFromWeb->finishedReading = &arrivalSinceStart;
	requestFromWeb->read_before = amount_of_requests_read;
	amount_of_requests_read++;
	logger(LOG,"SEND",&requestFromWeb->buf[5],hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(requestFromWeb->buf,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\nX-stat-req-arrival-count: %d\nX-stat-req-arrival-time: %ld seconds and %d microseconds\nX-stat-req-dispatch-count: %d\nX-stat-req-dispatch-time: %ld seconds and %d microseconds\nX-stat-req-complete-count: %d\nX-stat-req-complete-time: %ld seconds and %d microseconds\nX-stat-req-age: %d\nX-stat-thread-id: %d\nX-stat-thread-count: %d\nX-stat-thread-html: %d\nX-stat-thread-image: %d\n\n", VERSION, len, fstr,requestFromWeb->hit-1,(long)requestFromWeb->firstSaw->tv_sec,((int)requestFromWeb->firstSaw->tv_usec)/1000,requestFromWeb->passed_at_arrival,(long)requestFromWeb->passedToConsumer->tv_sec,((int)requestFromWeb->passedToConsumer->tv_usec)/1000,requestFromWeb->read_before,(long)requestFromWeb->finishedReading->tv_sec,((int)requestFromWeb->finishedReading->tv_usec)/1000,requestFromWeb->passed_at_arrival,thread_id,amount_of_requests_passed - (requestFromWeb->hit-1),requestFromWeb->thread_html_count,requestFromWeb->thread_image_count); /* Header + a blank line */
	logger(LOG,"Header",requestFromWeb->buf,hit);

	dummy = write(fd,requestFromWeb->buf,strlen(requestFromWeb->buf));


    /* Send the statistical headers described in the paper, example below

    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	dummy = write(fd,buffer,strlen(buffer));
    */
	int ret;
    /* send file in 8KB block - last block may be smaller */
	//logger(LOG,"web",buffer,0);
	while (	(ret = read(file_fd, requestFromWeb->buf, BUFSIZE)) > 0 ) {
		//logger(LOG,"web",buffer,0);
		dummy = write(fd,requestFromWeb->buf,ret);
		//logger(LOG,"web",buffer,0);
	}
	char eof = 0;
	dummy = write(fd,&eof,sizeof(eof));

	sleep(1);	/* allow socket to drain before signalling the socket is closed */


}

void * producer(void *listenfdAddress){
	int hit, socketfd;

	int listenfd = *(int *)listenfdAddress;

	socklen_t length;



	logger(LOG,"producer","producer begins", 0);

	for(hit=1; ;hit++) {
		struct sockaddr_in cli_addr; /* static = initialised to zeros */
		cli_addr.sin_family = 0;
		cli_addr.sin_port = 0;
		cli_addr.sin_addr.s_addr = 0;
		for(int numb = 0; numb < 8; numb ++){
			cli_addr.sin_zero[numb] = 0;
		}

		length = sizeof(cli_addr);
		logger(LOG,"producer","about to accept",0);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR,"system call","accept",0);

		struct timeval       arrivalHere;
		struct timeval arrivalSinceStart;
		struct timezone     arrivalHereZ;
		gettimeofday(&arrivalHere,&arrivalHereZ);
		timeSubtract(&startTime,&arrivalHere,&arrivalSinceStart);

		struct Request newRequest;
		bzero(&newRequest, sizeof(newRequest));
		newRequest.thread_id = requestBuffer.number_of_requests_dispatched;
		newRequest.thread_count = 0;
		newRequest.thread_html_count = 0;
		newRequest.thread_image_count = 0;
		newRequest.hit = hit;//the hit in the for loop (which may have to be reconfigured?)
		newRequest.listenfd = listenfd;//from earlier in the code
		newRequest.socketfd = socketfd;////from earlier in the code
	  newRequest.firstSaw = &arrivalSinceStart;
		pthread_mutex_lock(&m);
		newRequest.passed_at_arrival = amount_of_requests_passed;
		long ret, len, i;
		int j, buflen;
		//char * fstr;
		ret = read(socketfd,newRequest.buf,BUFSIZE);

			if(ret == 0 || ret == -1) {
				logger(LOG,"failed to read browser request: ret: ","",errno);
			}
			if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
				newRequest.buf[ret]=0;		/* terminate the buffer */
			else newRequest.buf[0]=0;
		for(i=0;i<ret;i++)	/* remove CF and LF characters */
			if(newRequest.buf[i] == '\r' || newRequest.buf[i] == '\n')
			newRequest.buf[i]='*';
		logger(LOG,"request",newRequest.buf,hit);
		if(strncmp(newRequest.buf,"GET ",4) && strncmp(newRequest.buf,"get ",4) ) {
		logger(FORBIDDEN,"Only simple GET operation supported",newRequest.buf,socketfd);
		}
		for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
			if(newRequest.buf[i] == ' ') { /* string is "GET URL " +lots of other stuff */
				newRequest.buf[i] = 0;
				break;
			}
		}
		for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
			if(newRequest.buf[j] == '.' && newRequest.buf[j+1] == '.') {
			logger(FORBIDDEN,"Parent directory (..) path names not supported",newRequest.buf,socketfd);
			}
		if( !strncmp(&newRequest.buf[0],"GET /\0",6) || !strncmp(&newRequest.buf[0],"get /\0",6) ) /* convert no filename to index file */
			(void)strcpy(newRequest.buf,"GET /index.html");


		buflen=strlen(newRequest.buf);
		newRequest.fstr = (char *)0;
		for(i=0;extensions[i].ext != 0;i++) {
			len = strlen(extensions[i].ext);
			if( !strncmp(&newRequest.buf[buflen-len], extensions[i].ext, len)) {
				if(!strcmp(extensions[i].ext, "html") || !strcmp(extensions[i].ext, "htm")){
					newRequest.type = TEXT;
				}
				else{
					newRequest.type = IMAGE;
				}
				newRequest.fstr =extensions[i].filetype;
				logger(LOG,"producer","TYPE",newRequest.type);
				break;

			}

		}



		//logger(LOG,"producer","got mutex lock\n",0);
			if(requestBuffer.num > BUF_SIZE){
				logger(LOG,"producer","fatal error num bigger than bufsize\n",0);
				exit(1);//buffer overflow
			}
			while(requestBuffer.num == BUF_SIZE) {
				logger(LOG,"producer","buf full, going to wait\n",0);
				pthread_cond_wait(&c_prod, &m);
			}
			//logger(LOG,"producer","have mutex after bufsizecheck\n",0);
			//critical section: create request object
			/*
			//logic to determine if something is an image
			static char buf[BUFSIZE+1];
			long ret;
			ret = read(socketfd,buf,BUFSIZE);
			for(i=0;i<ret;i++){
				if(buffer[i] == '.'){
					if( !strncmp(buffer,"gif",4) || !strncmp(buffer,"jpg",4) ||
					    !strncmp(buffer,"jpeg",)5 || !strncmp(buffer,"png",4) ||
					    !strncmp(buffer,"ico",)4 || !strncmp(buffer,"zip",4) ||
					    !strncmp(buffer,"gz",)3 || !strncmp(buffer,"tar",4)) {
						//item is an image
						newRequest.type = IMAGE;
					}
					else if	(!strncmp(buffer,"htm",4) || !strncmp(buffer,"html",4){
						//item is text
						newRequest.type = TEXT;
					}

			}
			*/


			//and add the reqeust object to the buffer
			requestBuffer.requests[requestBuffer.add] = newRequest;
			requestBuffer.add = (requestBuffer.add +1) % BUF_SIZE;
			requestBuffer.num++;
			logger(LOG,"producer","num is",requestBuffer.num);

		pthread_mutex_unlock(&m);
		//logger(LOG,"producer","mutex unlocked\n",0);
		pthread_cond_signal(&c_cons);

	}
}

void * consumer(void * args){
	int thread_id = *(int *)args;
	int numOfProcessedRequests = 0;
	int numOfHTML = 0;
	int numOfImage = 0;
	
	logger(LOG,"consumer","born", 0);

	struct Request currentRequest;
	while(1){

		pthread_mutex_lock(&m);

		if(requestBuffer.num < 0){
			logger(LOG,"consumer","fatal error num less than sero\n",0);
			exit(1);//meaningless error messsage
		}

		while(requestBuffer.num == 0){
			pthread_cond_wait (&c_cons, &m);
		}

		struct timeval       arrivalHere;
		struct timeval arrivalSinceStart;
		struct timezone     arrivalHereZ;
		gettimeofday(&arrivalHere,&arrivalHereZ);
		timeSubtract(&startTime,&arrivalHere,&arrivalSinceStart);

		int count;
		int inCount;
		struct Request tempRequest;
		switch (mode) {

				case ANY:
		printf("Running ANY scheduling as FIFO scheduling: ");
				case FIFO:
					printf("Running FIFO scheduling ");
					currentRequest = requestBuffer.requests[requestBuffer.rem];
		requestBuffer.rem = (requestBuffer.rem+1) % BUF_SIZE;
		logger(LOG,"consumer","changing rem",requestBuffer.rem);
		requestBuffer.num--;
						break;
				case HPIC:
		printf("Running HPIC scheduling:");
		//check if its a an Image
		currentRequest = requestBuffer.requests[requestBuffer.rem];
		bool isImage = false;
		for(count = 0; count < requestBuffer.num; count++){
			tempRequest = requestBuffer.requests[requestBuffer.rem + count];
			if(tempRequest.type == IMAGE){
				isImage = true;
				//logger(LOG,"producer","TYPE 2:",tempRequest.type);
				//logger(LOG,"consumer","THis item is an image!",0);
				currentRequest = tempRequest;//move the rest back one, decrease add spot and number, remove stays same
				for(inCount = count; inCount < requestBuffer.num; inCount++){
					requestBuffer.requests[(requestBuffer.rem + inCount)  % BUF_SIZE] = requestBuffer.requests[(requestBuffer.rem + inCount) % BUF_SIZE + 1];
					logger(LOG,"consumer","changing rem",requestBuffer.rem);
				}
				requestBuffer.add--;
				break;
			}
		}
		if(!isImage){
			requestBuffer.rem = (requestBuffer.rem+1) % BUF_SIZE;
			logger(LOG,"consumer","changing rem",requestBuffer.rem);
		}
		requestBuffer.num--;

						break;
				case HPHC:
					 printf("Running HPHC scheduling: ");
						printf("Running HPIC scheduling:");
		//check if its a an text
		currentRequest = requestBuffer.requests[requestBuffer.rem];

		bool isText = false;
		for(count = 0; count < requestBuffer.num; count++){
			tempRequest = requestBuffer.requests[requestBuffer.rem + count];
			if(tempRequest.type == TEXT){
				isText = true;
				currentRequest = tempRequest;//move the rest back one, decrease add spot and number, remove stays same
				for(inCount = count; inCount < requestBuffer.num; inCount++){
					requestBuffer.requests[(requestBuffer.rem + inCount)  % BUF_SIZE] = requestBuffer.requests[(requestBuffer.rem + inCount) % BUF_SIZE + 1];
					logger(LOG,"consumer","changing rem",requestBuffer.rem);
				}
				requestBuffer.add--;
				break;
			}
		}
		if(!isText){
			requestBuffer.rem = (requestBuffer.rem+1) % BUF_SIZE;
			logger(LOG,"consumer","changing rem",requestBuffer.rem);
		}
		requestBuffer.num--;

					 break;
				default: logger(LOG,"consumer","NO mode arg",0);
			}
		currentRequest.thread_html_count = numOfHTML;
		currentRequest.thread_image_count = numOfImage;

		currentRequest.passedToConsumer = &arrivalSinceStart;
		currentRequest.passed_at_passed = amount_of_requests_passed;
		web(currentRequest.socketfd, currentRequest.hit, &currentRequest, thread_id, amount_of_requests_passed);
		numOfHTML = currentRequest.thread_html_count;
		numOfImage = currentRequest.thread_image_count;
		amount_of_requests_passed++;
		pthread_mutex_unlock(&m);



	numOfProcessedRequests++;

	pthread_cond_signal (&c_prod);

	(void)close(currentRequest.socketfd);


	}
}

int main(int argc, char **argv)
{
	logger(LOG,"main","starting",0);
	int i, port, /*pid, TODO: commented out because it was not in use*/ listenfd;


	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

		if( argc < 6  || argc > 6 || !strcmp(argv[1], "-?") ) {
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

	/*
	if( !strncmp(argv[5],"FIFO"   ,5 ) || !strncmp(argv[5],"ANY", 4 ) ||
	    !strncmp(argv[5],"HPIC",5 ) || !strncmp(argv[5],"HPHC", 5 ) ){
		logger(ERROR, "main","illegal mode argument",0);
		exit(3);
	}
	mode = argv[5];
	*/
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


	NUM_THREADS = atoi(argv[3]);
	logger(LOG, "main", "NUM_THREADS: ",NUM_THREADS);

	BUF_SIZE = atoi(argv[4]);
	logger(LOG,"main", "BUF_SIZE",BUF_SIZE);

	if (!strncmp(argv[5],"FIFO",5)){
		mode = FIFO;
	}
	else if (!strncmp(argv[5],"ANY",4)){
		mode = ANY;
	}
	else if (!strncmp(argv[5],"HPIC",5)){
		mode = HPIC;
	}
	else if (!strncmp(argv[5],"HPHC",4)){
		mode = HPHC;
	}
	//logger(LOG,"main","mode: ", mode);

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
	int k;
	pthread_t prod;
	pthread_t con_threads[NUM_THREADS];

	//Change attribute default from joinable to detachable
	/*pthread_attr_t attr;
	int pthread_attr_init(pthread_attr_t *attr); //TODO does this even make sense?
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);*/
	//initialize buffer (already created globally)

	requestBuffer.requests = (struct Request *) malloc(sizeof(struct Request) * BUF_SIZE);

	//initializing producer thread, will take in reqeusts, package them, and place them on a queue
	logger(LOG,"main","created producer", listenfd);
	k = pthread_create(&prod, NULL, producer, &listenfd);
	logger(LOG,"main","producer create result:", k);

	//initializning the pool of threads, NUM_THREADS will be command-line input
	//attr was changed to make the threads detachable rather than joinable
	//logger(LOG,"main","created consumers", 0);
	for(j = 0; j < NUM_THREADS; j++){
		int id = j;
		k = pthread_create(&(con_threads[j]), NULL, consumer,(void *)&id);;
		logger(LOG,"main","consumer create result:", k);
	}

	//while(1){}
	int returnOfMainThread = 1;
	pthread_exit((void *)&returnOfMainThread);
}
