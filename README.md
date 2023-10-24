Client-Server TCP Application

---

## Application Protocol

To facilitate communication between the server and TCP clients, we've implemented an application protocol. This protocol first sends a 4-byte message length (the number of bytes to expect), followed by the message itself. We've predefined a structure containing these two fields (length and payload) to simplify communication.

---

## Server

The server has been implemented to support the multiplexing of TCP socket I/O, allowing it to handle multiple connections (up to 1000). For each connection, it creates a file descriptor, saves it, and uses it for communication with the TCP client. The server has three initial socket connections: STDIN, a UDP client socket, and a TCP socket for accepting connections from TCP clients.

To manage commands, UDP and TCP clients, and store-and-forward policy, we've used the following data structures:
- `map <string, int> clientId_socket_binding`: Maps each client ID to the socket's file descriptor.
- `map <int, string> socket_clientId_binding`: Maps each socket file descriptor to the associated client ID.
- `map <string, set<string>> topic_clientIds_binding`: Maps each topic to an ordered set of clients subscribed to it.

We've also used sets to handle store-and-forward, online clients, and message queues for each client.

### Server Functionalities

The server performs several key functionalities:

1. **Accepting New TCP Connections:** The server listens for incoming TCP client connections and handles various scenarios, such as reconnections, new connections, and attempts to connect with a client ID that is already online.

2. **Processing UDP Messages:** The server receives UDP messages in the specified format, processes them, and forwards them to the relevant TCP clients based on their subscriptions and the store-and-forward policy.

3. **TCP Client Management:** The server manages the communication with TCP clients, allowing clients to send commands such as exit (disconnect), subscribe to topics (with or without store-and-forward), and unsubscribe from topics. It maintains the online/offline status of clients.

4. **Store and Forward:** The server maintains a queue of messages for each client with an active store-and-forward policy. When a client reconnects, it sends any stored messages that match the client's subscriptions.

5. **Command Processing:** The server handles commands received from clients, such as exit and subscribe/unsubscribe to topics. It ensures consistent communication with clients based on the application protocol.

### Commands Accepted by Server

- `exit`: Marks a client as offline and terminates their connection.
- `subscribe`: Allows clients to subscribe to topics, specifying their store-and-forward preference (active or inactive).
- `unsubscribe`: Enables clients to unsubscribe from topics.
- `printClients`: For debugging, displays online clients and all previously connected clients.

---

## TCP Client/Subscriber

The TCP client handles two sockets: STDIN and the one for communicating with the server.

### Client Functionalities

The client offers the following functionalities:

1. **Client Initialization:** The client establishes a connection to the server and handles incoming messages. It accepts commands from the user through STDIN.

2. **Command Processing:** The client accepts the following commands:
   - `exit`: Closes the client, disconnects from the server, and marks itself as offline.
   - `subscribe`: Subscribes to topics, specifying store-and-forward preferences.
   - `unsubscribe`: Unsubscribes from topics.

3. **Message Display:** The client receives messages from the server, including processed data and exit commands, and displays them to STDOUT.

### Commands Accepted by Client

- `unsubscribe`: Unsubscribes from topics.
- `subscribe`: Subscribes to topics, specifying store-and-forward preferences.
- `exit`: Closes the client, disconnects from the server, and marks itself as offline.

