#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h> 
#define BUFFER_SIZE 1024
const char *filename = "/var/tmp/aesdsocketdata";
int keep_running = 1;
void aesdsocket_signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Caught signal, exiting");
        keep_running = 0;
    }
}
int main(int argc, char *argv[])
{
printf("1");
  int daemon_mode = 0;
  if (argc > 1 && strcmp(argv[1], "-d") == 0)
  {
      daemon_mode = 1;
  }
  printf("1");
  int status, new_fd;
  struct sockaddr_storage their_addr;
  socklen_t addr_size;
  struct addrinfo hints;
  struct addrinfo *servinfo;  // will point to the results
  char client_ip[INET_ADDRSTRLEN]; 
  memset(&hints, 0, sizeof hints); // make sure the struct is empty
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

  if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) 
  {
      fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
      exit(1);
  }
  
  int sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if (sockfd < 0) 
  {
    printf("Error opening socket: %s\n", strerror(errno));
    exit(1);
  }
  
  int yes=1;
  
  // lose the pesky "Address already in use" error message
  if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes) == -1) 
  {
      perror("setsockopt");
      exit(1);
  } 
  
  int res = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
  if (res < 0) 
  {
    printf("Error binding to socket: %s\n", strerror(errno));
    exit(1);
  }
  
  // Start listening for connections
  if (listen(sockfd, 10) == -1) 
  {
        perror("listen failed");
        close(sockfd);
        exit(1);
  }
    // Fork if daemon mode is enabled
    if (daemon_mode)
    {
      pid_t pid = fork();
      if (pid < 0) 
      {
        perror("fork failed");
        close(sockfd);
        exit(1);
      }
      if (pid > 0) 
      {
        // Parent process
        exit(0);
      }
      // Child process becomes daemon

        setsid();
        chdir("/");
        // Open /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (fd == -1)
        {
          perror("open devnull failed");
          return -1;
        }
        // Redirect standard file descriptors to /dev/null
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
    }
  addr_size = sizeof their_addr;
  while(keep_running)
  {
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
    if(new_fd < 0)
    {
      printf("Error accepting the connection: %s\n", strerror(errno));
      exit(1);
    }

    if (getnameinfo((struct sockaddr*)&their_addr, addr_size, client_ip, sizeof(client_ip), NULL, 0, NI_NUMERICHOST) == 0) 
    {
      //printf("Accepted connection from %s\n", client_ip);
    } else {
      perror("getnameinfo failed");
    }
    
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);  // Open for writing, create if not exists
    if (fd == -1) 
    {
      perror("Error opening file");
      return 1;
    }
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(new_fd, buffer, sizeof(buffer))) > 0)
    {
      char *newline = memchr(buffer, '\n', bytes_read);
      size_t line_length = newline - buffer + 1;
      write(fd, newline, line_length);
      while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) 
      {
       send(new_fd, buffer, bytes_read, 0);
      }
    }  
    close(fd);
    printf("Closed connection from %s\n", client_ip);
    shutdown(new_fd, 2);
  }
  remove(filename);
  shutdown(sockfd,2);
  freeaddrinfo(servinfo);
  return 0;
}
