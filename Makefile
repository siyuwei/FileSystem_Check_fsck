
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
myfsck:  myfsck.o readwrite.o utility.o
	$(CC) $(CFLAGS) -o myfsck myfsck.o readwrite.o utility.o


myfsck.o:  myfsck.c myfsck.h readwrite.h utility.h
	$(CC) $(CFLAGS) -c myfsck.c

readwrite.o:  readwrite.c readwrite.h
	$(CC) $(CFLAGS) -c readwrite.c

utility.o: utility.c utility.h myfsck.h ext2_fs.h readwrite.h
	$(CC) $(CFLAGS) -c utility.c


# To start over from scratch, type 'make clean'.  This
# removes the executable file, as well as old .o object
# files and *~ backup files:
#
clean: 
	$(RM) count *.o *~