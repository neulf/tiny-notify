// src/monitor.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )
#define LOG_DIR "logs/"
#define CONFIG_FILE "config/config.txt"
#define DEFAULT_LOG_RETENTION_DAYS 7

// Function to read the config file
int read_config(char *directories[], int *log_retention_days) {
    FILE *config = fopen(CONFIG_FILE, "r");
    if (config == NULL) {
        printf("Error: Unable to read config file.\n");
        return -1;
    }
    
    char line[256];
    int dir_count = 0;
    
    while (fgets(line, sizeof(line), config)) {
        if (strstr(line, "directory=")) {
            strtok(line, "=");
            directories[dir_count++] = strdup(strtok(NULL, "\n"));
        } else if (strstr(line, "log_retention_days=")) {
            strtok(line, "=");
            *log_retention_days = atoi(strtok(NULL, "\n"));
        }
    }
    fclose(config);
    return dir_count;
}

// Function to clean logs older than retention days
void clean_logs(int log_retention_days) {
    DIR *d;
    struct dirent *dir;
    d = opendir(LOG_DIR);
    if (d) {
        time_t now = time(NULL);
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) { // Only consider files
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s%s", LOG_DIR, dir->d_name);
                
                struct stat st;
                stat(filepath, &st);
                double diff = difftime(now, st.st_mtime) / (60 * 60 * 24);
                if (diff > log_retention_days) {
                    remove(filepath);
                    printf("Log file %s deleted.\n", filepath);
                }
            }
        }
        closedir(d);
    }
}

// Main function to monitor directories
int main() {
    int log_retention_days = DEFAULT_LOG_RETENTION_DAYS;
    char *directories[10];
    
    int dir_count = read_config(directories, &log_retention_days);
    if (dir_count < 1) {
        printf("No directories to watch. Exiting.\n");
        return 1;
    }

    // Initialize inotify
    int fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    // Add watch on directories
    for (int i = 0; i < dir_count; i++) {
        inotify_add_watch(fd, directories[i], IN_CREATE | IN_MODIFY | IN_DELETE);
        printf("Monitoring directory: %s\n", directories[i]);
    }

    // Monitor changes
    char buffer[EVENT_BUF_LEN];
    while (1) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            perror("read");
        }
        
        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            if (event->len) {
                char action[16];
                if (event->mask & IN_CREATE) {
                    strcpy(action, "created");
                } else if (event->mask & IN_MODIFY) {
                    strcpy(action, "modified");
                } else if (event->mask & IN_DELETE) {
                    strcpy(action, "deleted");
                }
                char log_file[64];
                time_t t = time(NULL);
                struct tm tm = *localtime(&t);
                snprintf(log_file, sizeof(log_file), LOG_DIR"log_%d-%02d-%02d_%02d:%02d.txt",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);

                FILE *log = fopen(log_file, "a");
                fprintf(log, "File %s was %s in directory %s.\n", event->name, action, directories[0]);
                fclose(log);
            }
            i += EVENT_SIZE + event->len;
        }

        // Clean old logs
        clean_logs(log_retention_days);
    }

    close(fd);
    return 0;
}
