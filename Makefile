SRC_DIR=src
INC_DIR=src

EXEC=gemini
SRCS=$(SRC_DIR)/$(EXEC).cpp $(wildcard $(SRC_DIR)/systems/*.cpp) $(wildcard $(SRC_DIR)/managers/*.cpp)
OBJS=$(SRCS:.cpp=.o)

CC=g++
CFLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -march=native -O2
LDFLAGS=-pthread
INCLUDES=-I$(INC_DIR)

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(INCLUDES) $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.cpp %.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(EXEC) $(OBJS) *~
