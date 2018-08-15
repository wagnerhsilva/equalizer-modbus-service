TARGETS=main.o cmdatabase.o
INC=-I . -lsqlite3 -lm libmodbus.so.5 -lpthread
OPT=-O0 -std=c++11
DEBUG=-g

.PHONY: all clean rebuild release

all: modbus

release: rebuild clean_partial

rebuild: clean modbus

%.o: %.cpp
	$(CXX) $(INC) $(CMP) $(OPT) $(DEBUG) -c $^

modbus: $(TARGETS)
	$(CXX) $(OPT) $^ -o modbus $(INC) $(DEBUG)

clean:
	rm -rf *o *.txt modbus

clean_partial:
	rm -rf *o *.txt
