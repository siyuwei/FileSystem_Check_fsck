
CC = gcc
CFLAGS  = -g -Wall -I./inc

# typing 'make' will invoke the first target entry in the file 
# (in this case the default target entry)
# you can name this target entry anything, but "default" or "all"
# are the most commonly used names by convention
#
default: myfsck

# To create the executable file count we need the object files
# countwords.o, counter.o, and scanner.o:
#
myfsck:  myfsck.o readwrite.o
	$(CC) $(CFLAGS) -o myfsck myfsck.o readwrite.o

# To create the object file countwords.o, we need the source
# files countwords.c, scanner.h, and counter.h:
#
myfsck.o:  myfsck.c myfsck.h readwrite.h
	$(CC) $(CFLAGS) -c myfsck.c

# To create the object file counter.o, we need the source files
# counter.c and counter.h:
#
readwrite.o:  readwrite.c readwrite.h
	$(CC) $(CFLAGS) -c readwrite.c



# To start over from scratch, type 'make clean'.  This
# removes the executable file, as well as old .o object
# files and *~ backup files:
#
clean: 
	$(RM) count *.o *~