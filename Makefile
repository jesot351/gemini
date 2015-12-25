FLAGS=-std=c++11 -Wall -Wextra -march=native -O2
LIBS=-pthread
CPPFILES=gemini.cpp
HEADERS=

gemini: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) $(CPPFILES) -o gemini $(LIBS)

linker-fix: $(CPPFILES) $(HEADERS)
	g++ $(FLAGS) -W1,--no-as-needed $(CPPFILES) -o gemini $(LIBS)

clean:
	rm -f gemini *~ *.o
