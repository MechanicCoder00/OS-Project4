CC	= gcc
CFLAGS = -lm #-DDEBUG
TARGET1	= oss
TARGET2 = process
OBJ1	= oss.o
OBJ2	= process.o

ALL:	$(TARGET1) $(TARGET2)

$(TARGET1): $(OBJ1)
	$(CC) ${CFLAGS} -o $@ $(OBJ1)
	
$(TARGET2): $(OBJ2)
	$(CC) ${CFLAGS} -o $@ $(OBJ2)

$(OBJ1): oss.c
	$(CC) ${CFLAGS} -c oss.c

$(OBJ2): process.c
	$(CC) ${CFLAGS} -c process.c

.PHONY: clean
clean:
	/bin/rm -f *.o *.log $(TARGET1) $(TARGET2)
