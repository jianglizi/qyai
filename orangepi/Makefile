
# 编译器和编译选项
CC = gcc
CFLAGS = -Wall -O2  # 开启警告并优化
DEBUG = -DUSE_DEBUG  # 调试选项
INCLUDES = -I./vad  # 头文件目录
LIB_NAMES = -lcurl -lwiringPi -ljson-c -lasound -lfvad # 库文件
LIB_PATH = -L./lib  # 库路径

# 源文件
SOURCES = $(wildcard ./*.c)

# 目标文件
OBJ = $(patsubst %.c, %.o, $(SOURCES))

# 目标执行文件
TARGET = main

# 目标规则：编译并链接生成可执行文件
$(TARGET): $(OBJ)
	@mkdir -p output
	$(CC) $(OBJ) $(LIB_PATH) $(LIB_NAMES) -o output/$(TARGET)
	@rm -f $(OBJ)  # 删除目标文件，清理临时文件

# 目标文件规则：编译源文件为目标文件
%.o: %.c
	$(CC) $(INCLUDES) $(DEBUG) $(CFLAGS) -c $< -o $@

# 清理规则
.PHONY: clean
clean:
	@echo "Removing linked and compiled files..."
	rm -rf $(OBJ) $(TARGET) output
	@echo "Clean completed."

