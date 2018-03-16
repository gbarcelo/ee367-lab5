# Make file

net367: host.o packet.o man.o main.o net.o switch.o
	gcc -o ./net367 ./obj/switch.o ./obj/host.o ./obj/man.o ./obj/main.o ./obj/net.o ./obj/packet.o

main.o: src/main.c
	gcc -o ./obj/main.o ./src/main.c -I./include/ -c

host.o: src/host.c
	gcc -o ./obj/host.o ./src/host.c -I./include/ -c

man.o: src/man.c
	gcc -o ./obj/man.o ./src/man.c -I./include/ -c

net.o:  src/net.c
	gcc -o ./obj/net.o ./src/net.c -I./include/ -c

packet.o:  src/packet.c
	gcc -o ./obj/packet.o ./src/packet.c -I./include/ -c

switch.o:  src/switch.c
		gcc -o ./obj/switch.o ./src/switch.c -I./include/ -c

run: net367
	./net367

clean:
	-rm ./obj/*.o
