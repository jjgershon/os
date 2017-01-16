CC=g++
CFLAGS= -c -g3 

all:Tester

Tester : HW4Test.o test_utils.o
	$(CC) -g3 -lpthread -DHW4_DEBUG HW4Test.o test_utils.o -o Tester

#ExampleTest.o : ExampleTest.cpp cutest.h test_utils.o
#	$(CC) $(CFLAGS) ExampleTest.cpp 

HW4Test.o : HW4Test.cpp cutest.h test_utils.o
	$(CC) $(CFLAGS) HW4Test.cpp 

test_utils.o : test_utils.cpp test_utils.hpp
	$(CC) $(CFLAGS) test_utils.cpp

clean:
	rm Tester *.o
