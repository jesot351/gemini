SRC_DIR=src
INC_DIR=src
VULKAN_SDK_PATH=/home/jesper/Documents/VulkanSDK/1.0.30.0/x86_64

EXEC=gemini
SRCS=$(SRC_DIR)/$(EXEC).cpp $(wildcard $(SRC_DIR)/systems/*.cpp) \
	$(wildcard $(SRC_DIR)/managers/*.cpp) \
	$(wildcard $(SRC_DIR)/data/*.cpp)
OBJS=$(SRCS:.cpp=.o)

CC=g++
CFLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -march=native -O2
LDFLAGS=-pthread -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan
INCLUDES=-I$(INC_DIR) -I$(VULKAN_SDK_PATH)/include

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(INCLUDES) $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(EXEC) $(OBJS) *~
