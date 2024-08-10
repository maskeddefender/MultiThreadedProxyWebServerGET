# this is the makefile for the proxy server with cache to build the proxy server

# This defines a variable CC which stores the compiler to be used for the project. In this case, g++ (GNU C++ compiler) is used to compile the C++ source files.
CC=g++
# This defines another variable CFLAGS which contains the compiler flags.
# -g adds debugging information to the executable, which is useful when debugging with tools like gdb.
# -Wall enables all compiler's warning messages to help identify potential issues in the code.
CFLAGS= -g -Wall 

# This defines a target all, which depends on the target proxy. When you run make all or just make, the proxy target will be built.
all: proxy

# This defines the proxy target, which is dependent on the file proxy_server_with_cache.c. When you run make proxy, the commands under this target will be executed.
proxy: proxy_server_with_cache.c

# This command uses g++ (the value of CC) with the flags -g and -Wall (the value of CFLAGS) to compile proxy_parse.c into an object file named proxy_parse.o.
# -c tells the compiler to generate an object file (.o), not an executable.
# -o proxy_parse.o specifies the output file name.
# -lpthread links the pthread library, which is used for multi-threading support.
	$(CC) $(CFLAGS) -o proxy_parse.o -c proxy_parse.c -lpthread

# This is similar to the previous command, but it compiles proxy_server_with_cache.c into an object file named proxy.o.	
	$(CC) $(CFLAGS) -o proxy.o -c proxy_server_with_cache.c -lpthread

# This command links the object files proxy_parse.o and proxy.o together to create the final executable named proxy.	
	$(CC) $(CFLAGS) -o proxy proxy_parse.o proxy.o -lpthread


# This defines a clean target, which removes the generated executable proxy and any object files (*.o).
# rm -f forces removal without prompting for confirmation, even if the files do not exist.
clean:
	rm -f proxy *.o


# This defines a tar target, which creates a compressed tarball (ass1.tgz) containing the source files, README, Makefile, and header files.
# tar -cvzf is the command used to create a compressed .tgz archive.
# ass1.tgz is the name of the resulting tarball.
# The remaining arguments are the files to be included in the archive.
tar:
	tar -cvzf ass1.tgz proxy_server_with_cache.c README Makefile proxy_parse.c proxy_parse.h
