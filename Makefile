
build:
	g++ qjreditor.cpp -o qjreditor -lncurses

install:
	mv qjreditor /usr/bin/qjreditor

clean:
	rm -f ./qjreditor
