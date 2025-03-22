 #format is target-name: target dependencies 

 

#{-tab-}actions 



 # Tool invocations 
all: myshell 
	
myshell: myshell.o LineParser.o

	gcc -g -m32 -Wall -o myshell myshell.o LineParser.o 

 # Depends on the source and header files 

myshell.o: myshell.c 

	gcc -m32 -g -Wall -c -o myshell.o myshell.c 

LineParser.o: LineParser.c

	gcc -m32 -g -Wall -c -o LineParser.o LineParser.c 

 #tell make that "clean" is not a file name! 

.PHONY: clean 

 #Clean the build directory 

clean:

	rm -f *.o myshell 