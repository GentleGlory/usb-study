
all: main.o
	gcc main.o -lusb-1.0 -o mouse_ctrl_test_async

main.o: main.c
	gcc -c main.c

clean:
	rm -f *.o
	rm -f mouse_ctrl_test_async
	 
