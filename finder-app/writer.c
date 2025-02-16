#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) { 
    openlog("writer app", LOG_PID|LOG_CONS, LOG_USER);
    
    if(argc != 3)
    {
        syslog( LOG_ERR , "This application expects 2 arguments.");
        return 1;
    }
    
    FILE* file;
    file = fopen(argv[1], "w");
    if(file == NULL)
    {
        syslog( LOG_ERR , "Unable to open file.");
        return 2;
    }
    fputs(argv[2], file);
    syslog( LOG_INFO , "Writing %s to %s", argv[2], argv[1]);
    fclose(file);
    closelog();
    return 0;
}
