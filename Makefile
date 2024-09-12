# Makefile for cross-compiling on Windows for Linux

# 使用交叉编译工具链进行编译
CC = "C:/Program Files/x86_64-linux-musl/bin/x86_64-linux-musl-gcc.exe"
CFLAGS = -Wall -I.
SRC = src/main.c
OBJ = main.o
TARGET = main

# 默认目标：编译生成Linux可执行文件
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# 清理编译生成的文件
clean:
	rm -f $(TARGET) $(OBJ)

# 安装目标（可选，安装到指定目录）
#install:
#	mkdir -p /usr/local/bin/LinuxMonitorService
#	cp $(TARGET) /usr/local/bin/LinuxMonitorService/

# 运行目标：仅在Linux上运行，不适用于Windows
run:
	@echo "This binary is for Linux, please run it on a Linux system."

.PHONY: clean install run
