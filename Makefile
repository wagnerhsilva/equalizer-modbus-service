TARGETS=modbus.o defs.o
INC=-I.
OPT=
FLAGS=-lrt -lpthread -lsqlite3 -lm libmodbus.so.5
DEBUG=-g

.PHONY: all clean rebuild release

all: modbus

release: rebuild clean_partial

rebuild: clean modbus

%.o: %.c
	$(CC) $(INC) $(CMP) $(OPT) $(DEBUG) -c $^

modbus: $(TARGETS)
	$(CC) $(OPT) $^ -o modbus $(INC) $(FLAGS) $(DEBUG)

clean:
	rm -rf *o *.txt modbus

clean_partial:
	rm -rf *o *.txt
