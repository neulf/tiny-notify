# tiny notify

A **Linux Service** that monitors the specified paths for changes to all files and folders within those paths. When a new file is created, modified, or deleted, the service logs these changes to a file. A new log file is created every 5 minutes, containing details such as the path and change information. Log files older than 7 days (configurable) are periodically deleted.

### Code Explanation:

1. **Configuration file reading `log_retention_days`**: The `read_config()` function now reads not only the directories to monitor but also parses the `log_retention_days` value from the configuration file to control the log retention period.

2. **Log Cleanup Mechanism**: Based on the number of days specified in the configuration file, the program will clean up log files older than this limit.

### Configuration File Example:

```
directory=/home/user/watch1
directory=/home/user/watch2
log_retention_days=7
```