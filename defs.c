#include "defs.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

#define OUTPUT_CONSOLE		0

int DEBUG = 1;
FILE *fp = NULL;

static int firstpass = 1;

int LOG(const char *format, ...) {
    int done = -1;
    char timestamp[80];
    struct tm * timeinfo;
    time_t rawtime;
    char buffer[256] = {0};
    char obuffer[256] = {0};
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    memset(timestamp,0,sizeof(timestamp));
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d-%H:%M:%S", timeinfo);

    if(DEBUG){
        if(!fp){
            fp = fopen(DEBUG_FILE, "a+");
        }
        if(firstpass == 1){
            
            strcat(buffer,"SOFTWARE BASICO V. " SOFTWARE_VERSION);
            strcat(buffer, "---[Invoked at: ");
            int s = strlen(buffer);
            strftime(&buffer[s], 100, "%Y-%m-%d %H:%M:%S", timeinfo);
            strcat(buffer, "]---\n");
            firstpass = 0;
            fwrite(buffer, strlen(buffer), 1, fp);
            if(OUTPUT_CONSOLE){
                printf("%s", buffer);
            }
        }
        /* String formatada */
        va_list arg;
        va_start(arg, format);
        done = vsnprintf(obuffer, 256, format, arg);
        va_end(arg);
        /* Separador */
        strcpy(buffer," - ");
        /* Salva arquivo */
        fwrite(timestamp, strlen(timestamp), 1, fp);
        fwrite(buffer, strlen(buffer), 1, fp);
        fwrite(obuffer, strlen(obuffer), 1, fp);
        fflush(fp);
        if(OUTPUT_CONSOLE){
            printf("%s%s%s", timestamp, buffer, obuffer);
            fflush(stdout);
        }
    }
    return done;
}
