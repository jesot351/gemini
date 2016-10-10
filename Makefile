FLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -march=native -O2
DEBUG_FLAGS=-std=c++11 -Wall -Wextra -Wno-unused-parameter -march=native -O2 -g
LIBS=-pthread
CPPFILES=gemini.cpp TaskScheduling.cpp Platform.cpp Rendering.cpp Memory.cpp Input.cpp Physics.cpp Animation.cpp AI.cpp
HEADERS=TaskScheduling.h Platform.h Rendering.h Memory.h Input.h Physics.h Animation.h AI.h

gemini: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) $(CPPFILES) -o gemini $(LIBS)

asm: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) $(CPPFILES) $(LIBS) -S -fverbose-asm

debug: $(CPPFILES) $(HEADERS)
	g++ $(DEBUG_FLAGS) $(CPPFILES) -o gemini_debug $(LIBS)

clean:
	rm -f gemini *~ *.o
