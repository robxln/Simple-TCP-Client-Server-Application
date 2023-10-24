server: server.cpp
	g++ server.cpp -o server

subscriber: subscriber.cpp
	g++ subscriber.cpp -o subscriber
	
clean: subscriber server
	rm -f subscriber server