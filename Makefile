FLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -march=native -O2
DEBUG_FLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -march=native -O0 -g
LIBS=-pthread
CPPFILES=gemini.cpp TaskScheduling.cpp Platform.cpp
HEADERS=TaskScheduling.h Platform.h

gemini: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) $(CPPFILES) -o gemini $(LIBS)

asm: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) $(CPPFILES) $(LIBS) -S -fverbose-asm

debug: $(CPPFILES) $(HEADERS)
	g++ $(DEBUG_FLAGS) $(CPPFILES) -o gemini_debug $(LIBS)

clean:
	rm -f gemini *~ *.o
