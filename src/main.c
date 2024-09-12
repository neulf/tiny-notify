#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define CONFIG_FILE "notify.config"
#define LOG_DIR "./"  // 日志目录
#define MAX_WATCHES 1024  // 最大监听路径数

// 文件描述符和监视器描述符
int fd;
int wd[MAX_WATCHES];
int watch_count = 0;
int log_retention_days = 7;  // 默认日志保留天数

// 信号处理函数，捕获 SIGTERM 信号并清理资源
void handle_signal(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        printf("Caught signal %d, exiting gracefully...\n", signal);
        for (int i = 0; i < watch_count; i++) {
            if (wd[i] >= 0) inotify_rm_watch(fd, wd[i]);
        }
        if (fd >= 0) close(fd);
        exit(0);
    }
}

// 获取当前时间的字符串
void get_time_string(char *buffer, size_t size) {
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, size, "%Y-%m-%d_%H-%M-%S", timeinfo);
}

// 记录事件到日志文件，以 CSV 格式
void log_event(FILE *log_file, const char *path, const char *event_desc, int is_directory) {
    char time_str[64];
    get_time_string(time_str, sizeof(time_str));

    // 以CSV格式记录时间、事件类型、路径、是否为目录
    fprintf(log_file, "\"%s\",\"%s\",\"%s\",%d\n", time_str, event_desc, path, is_directory);
    fflush(log_file);
}

// 从配置文件读取多个要监控的目录路径和日志保留天数
int read_config(char directories[MAX_WATCHES][PATH_MAX]) {
    FILE *config_file = fopen(CONFIG_FILE, "r");
    if (!config_file) {
        perror("Error opening config file");
        return -1;
    }

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), config_file)) {
        // 去掉换行符
        line[strcspn(line, "\n")] = '\0';

        // 解析 "directory=" 开头的行
        if (strncmp(line, "directory=", 10) == 0) {
            strcpy(directories[watch_count], line + 10);  // 复制路径
            watch_count++;
            if (watch_count >= MAX_WATCHES) {
                fprintf(stderr, "Exceeded maximum number of directories to watch.\n");
                fclose(config_file);
                return -1;
            }
        }

        // 解析 "log_retention_days=" 配置
        if (strncmp(line, "log_retention_days=", 19) == 0) {
            log_retention_days = atoi(line + 19);  // 读取日志保留天数
        }
    }

    fclose(config_file);
    return 0;
}

// 删除超过指定天数的日志文件
void clean_old_logs() {
    DIR *dir;
    struct dirent *entry;
    char file_path[PATH_MAX];
    struct stat file_stat;
    time_t now = time(NULL);
    double seconds_in_days = log_retention_days * 24 * 3600;

    // 打开日志目录
    if ((dir = opendir(LOG_DIR)) == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        // 只处理以 "_log.csv" 结尾的文件
        if (strstr(entry->d_name, "_log.csv")) {
            snprintf(file_path, sizeof(file_path), "%s/%s", LOG_DIR, entry->d_name);
            
            // 获取文件的状态信息
            if (stat(file_path, &file_stat) == 0) {
                // 判断文件是否超过指定天数
                if (difftime(now, file_stat.st_mtime) > seconds_in_days) {
                    printf("Deleting old log file: %s\n", file_path);
                    if (remove(file_path) != 0) {
                        perror("Error deleting file");
                    }
                }
            } else {
                perror("stat");
            }
        }
    }

    closedir(dir);
}

int main() {
    char directories[MAX_WATCHES][PATH_MAX];
    char buffer[EVENT_BUF_LEN];
    FILE *log_file = NULL;

    // 从配置文件中读取要监控的多个目录和日志保留天数
    if (read_config(directories) != 0) {
        fprintf(stderr, "Failed to read configuration. Exiting...\n");
        return 1;
    }

    // 初始化 inotify
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return 1;
    }

    // 添加需要监听的多个目录
    for (int i = 0; i < watch_count; i++) {
        wd[i] = inotify_add_watch(fd, directories[i], IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
        if (wd[i] == -1) {
            perror("inotify_add_watch");
            close(fd);
            return 1;
        }
        printf("Monitoring directory: %s\n", directories[i]);
    }

    // 创建日志文件
    char log_filename[PATH_MAX];
    get_time_string(log_filename, sizeof(log_filename));
    strcat(log_filename, "_log.csv");
    log_file = fopen(log_filename, "w");

    if (!log_file) {
        perror("fopen");
        close(fd);
        return 1;
    }

    // 写入CSV文件的表头
    fprintf(log_file, "\"Time\",\"Event\",\"Path\",\"IsDirectory\"\n");

    // 注册信号处理程序
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    time_t start_time = time(NULL);
    const int log_interval = 300;  // 5分钟 = 300秒

    // 开始监听
    while (1) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }

        time_t current_time = time(NULL);
        // 每5分钟切换一个日志文件
        if (difftime(current_time, start_time) >= log_interval) {
            fclose(log_file);

            // 清理超过指定天数的日志文件
            clean_old_logs();

            get_time_string(log_filename, sizeof(log_filename));
            strcat(log_filename, "_log.csv");
            log_file = fopen(log_filename, "w");
            if (!log_file) {
                perror("fopen");
                break;
            }
            // 写入新日志文件的表头
            fprintf(log_file, "\"Time\",\"Event\",\"Path\",\"IsDirectory\"\n");
            start_time = current_time;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            char event_desc[256] = "";

            // 记录新增文件或目录
            if (event->mask & IN_CREATE) {
                snprintf(event_desc, sizeof(event_desc), "Created %s", (event->mask & IN_ISDIR) ? "directory" : "file");
            }
            // 记录修改的文件
            if (event->mask & IN_MODIFY) {
                snprintf(event_desc, sizeof(event_desc), "Modified file");
            }
            // 记录删除的文件或目录
            if (event->mask & IN_DELETE) {
                snprintf(event_desc, sizeof(event_desc), "Deleted %s", (event->mask & IN_ISDIR) ? "directory" : "file");
            }
            // 记录文件/目录的移动
            if (event->mask & IN_MOVED_FROM || event->mask & IN_MOVED_TO) {
                snprintf(event_desc, sizeof(event_desc), "Moved %s", (event->mask & IN_ISDIR) ? "directory" : "file");
            }

            if (strlen(event_desc) > 0) {
                log_event(log_file, event->name, event_desc, event->mask & IN_ISDIR);
            }

            i += EVENT_SIZE + event->len;
        }
    }

    // 清理
    fclose(log_file);
    for (int i = 0; i < watch_count; i++) {
        if (wd[i] >= 0) inotify_rm_watch(fd, wd[i]);
    }
    close(fd);

    return 0;
}
