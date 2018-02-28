/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUF_SIZE 100

void GET(char *path);

int clientfd;
pthread_mutex_t lock;
pthread_barrier_t barrier;

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char* host, char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info) {
  if (info == NULL) return -1;

  int clientfd;
  for (;info != NULL; info = info->ai_next) {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}

void * thread(void * argv2){
  // Send GET request > stdout
  char *argv = (char*)argv2;
  char buf[BUF_SIZE];
  printf("\t%s yo \t",argv);
  GET(argv);
  pthread_barrier_wait(&barrier);

  while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
    fputs(buf, stdout);
    memset(buf, 0, BUF_SIZE);
  }
  
  pthread_exit(NULL);//instead of NULL
}

// Send GET request
void GET(char *path) {
  char req[1000] = {0};
  pthread_mutex_lock(&lock);
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
  pthread_mutex_unlock(&lock);
  pthread_barrier_wait(&barrier);
  send(clientfd, req, strlen(req), 0);
}

int main(int argc, char **argv) {
  pthread_barrierattr_t attr;
  pthread_barrier_init(&barrier, &attr, (int)*argv[3]);

  if (argc != 6 && argc != 7) {
    fprintf(stderr, "USAGE: ./httpclient <hostname> <port> <threads> <schedalg> <request path> <request path2>\n");
    return 1;
  }  
  
  // Establish connection with <hostname>:<port>
  clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
 
  if (clientfd == -1) {
    if(argc == 6){
    	fprintf(stderr, "[main:73] Failed to connect to: %s:%s%s \n", argv[1], argv[2], argv[5]);
    }else{
    	fprintf(stderr, "[main:73] Failed to connect to: %s:%s%s%s \n", argv[1], argv[2], argv[5], argv[6]);
    }
    return 3;
  }

  pthread_t t [atoi(argv[3])];
  int i = 0;
  int j = 0;
  while(j<10){
  	//printf("yolo");
  	for(i = 0; i < atoi(argv[3]); i++){
  		//printf("here0");
  		if(pthread_create(&t[i], NULL, thread , (void*)argv[5])){
  			perror("could not create thread");
  		}
  		printf("here1");
  		//printf("here2");
  	}
  	//pthread_join(t[1],NULL);
  	j++;
  }
  close(clientfd);

  return 0;
}
