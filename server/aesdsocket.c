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
#include <pthread.h>
#include <sys/queue.h>

#define BUFFER_SIZE 1024

#define SLIST_FOREACH_SAFE(var, head, field, tvar)                           \
    for ((var) = SLIST_FIRST((head));                                        \
            (var) && ((tvar) = SLIST_NEXT((var), field), 1);                 \
            (var) = (tvar))

// Structure for a singly linked list node (to track threads)
typedef struct Node {
    pthread_t thread_id;
    int done;
    int clientfd;
    SLIST_ENTRY(Node) next;
} Node;

// Define the singly linked list head
SLIST_HEAD(LinkedList, Node); // Creates a struct { struct Node *slh_first; }
#ifdef USE_AESD_CHAR_DEVICE
const char *filename = "/dev/aesdchar";
#else
const char *filename = "/var/tmp/aesdsocketdata";
#endif
volatile sig_atomic_t keep_running = 1;
int clientfd, sockfd;
pthread_mutex_t file_mutex;  // Mutex for file access

void aesdsocket_signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Caught signal, exiting\n");
        if (clientfd > 0)
    	{
            close(clientfd);
    	}
    	if (sockfd > 0)
    	{
            close(sockfd);
    	}
        keep_running = 0;
    }
}
void * writeTimestamp(void *arg)
{
  while(1)
  {
    sleep(10);
    // Get the current system time
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M%S %z\n", timeinfo); 
    
    pthread_mutex_lock(&file_mutex);
    FILE *file = fopen(filename, "a");
    if (file == NULL) {
      perror("Error opening file");
      pthread_mutex_unlock(&file_mutex);
      continue;
    }   
    fputs(timestamp, file);
    fclose(file);
    pthread_mutex_unlock(&file_mutex);

  }

  return NULL;
}

void * sendrecv(void *arg)
{
  Node* param = (Node *)arg;
  
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  while ((bytes_read = read(param->clientfd, buffer, sizeof(buffer))) > 0) 
  {
    char *newline = memchr(buffer, '\n', bytes_read);
      if (newline)
      {
          size_t line_length = newline - buffer + 1;
          pthread_mutex_lock(&file_mutex);
          int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
          if (fd == -1) {
              perror("Error opening file");
              pthread_mutex_unlock(&file_mutex);
              param->done =1;
              break;
          }
          write(fd, buffer, line_length);
          close(fd);
          pthread_mutex_unlock(&file_mutex);
          pthread_mutex_lock(&file_mutex);
          int fd_read = open(filename, O_RDONLY);
          if (fd_read == -1) {
              perror("Error opening file");
              pthread_mutex_unlock(&file_mutex);
              param->done =1;
              break;
          }
          char buffer2[BUFFER_SIZE];
          while ((bytes_read = read(fd_read, buffer2, sizeof(buffer2))) > 0) 
          {
              send(clientfd, buffer2, bytes_read, 0);
          }
          if (bytes_read ==-1)
          {
            printf("Error reading from file");
          }
          close(fd_read);
          pthread_mutex_unlock(&file_mutex);
      }
      else
      {
          pthread_mutex_lock(&file_mutex);
          int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
          if (fd == -1) {
              perror("Error opening file");
              pthread_mutex_unlock(&file_mutex);
              param->done =1;
              break;
          }
          write(fd, buffer, bytes_read);
          close(fd);
          pthread_mutex_unlock(&file_mutex);
      }
  }
  param->done = 1;
  shutdown(clientfd, SHUT_RDWR);
  close(clientfd);
  return NULL;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, aesdsocket_signal_handler);
    signal(SIGTERM, aesdsocket_signal_handler);
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status;
    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd < 0) {
        perror("Error opening socket");
        freeaddrinfo(servinfo);
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        perror("setsockopt");
        freeaddrinfo(servinfo);
        exit(1);
    }

    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error binding to socket");
        freeaddrinfo(servinfo);
        exit(1);
    }

    if (listen(sockfd, 10) == -1) {
        perror("listen failed");
        exit(1);
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        if (pid > 0) {
            exit(0);
        }

        setsid();
        chdir("/");
        
        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof their_addr;
    char client_ip[INET_ADDRSTRLEN];
    struct LinkedList head = SLIST_HEAD_INITIALIZER(head); // Initialize the list
    SLIST_INIT(&head); // Ensure it's initialized

#ifndef USE_AESD_CHAR_DEVICE
    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, writeTimestamp, NULL);
#endif
    
    while (keep_running) {
        clientfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (clientfd < 0) {
            perror("Error accepting connection");
            continue;
        }

        if (getnameinfo((struct sockaddr*)&their_addr, addr_size, client_ip, sizeof(client_ip), NULL, 0, NI_NUMERICHOST) == 0) {
            printf("Accepted connection from %s\n", client_ip);
        } else {
            perror("getnameinfo failed");
        }
        
        // Create thread for socket listener 
        Node * new_node = (Node *)malloc(sizeof(Node));
        new_node->done = 0;
        new_node->clientfd = clientfd;

        pthread_create(&(new_node->thread_id), NULL, sendrecv, (void*)new_node);
        SLIST_INSERT_HEAD(&head, new_node, next);
        
        //Check if any thread completed its execution and join it
        Node *node, *temp_safe;
        SLIST_FOREACH_SAFE(node, &head, next, temp_safe) {
            if (node->done == 1) { // Condition to delete
                pthread_join(node->thread_id, NULL); 
                SLIST_REMOVE(&head, node, Node, next);
                free(node);
            }
        }   
    }
    
    // Upon reciving a sigkill stop all threads and free the linked list
    Node *node, *temp_safe;
    SLIST_FOREACH_SAFE(node, &head, next, temp_safe) {
      pthread_join(node->thread_id, NULL); 
      SLIST_REMOVE(&head, node, Node, next);
      free(node);
    }
#ifndef USE_AESD_CHAR_DEVICE    
    pthread_cancel(timestamp_thread);
    pthread_join(timestamp_thread, NULL);
    
    remove(filename);
#endif    
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    freeaddrinfo(servinfo);
    pthread_mutex_destroy(&file_mutex);  // Destroy mutex
    return 0;
}

