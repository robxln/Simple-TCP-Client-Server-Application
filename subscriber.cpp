#include "helper.h"
using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s id_client ip_server port_server\n", file);
	exit(0);
}

//buffer pentru parsare citire/mesaje
char buffer[BUFLEN];
struct Message {
    int len;
    char *payload;
};

void send_message(int socketfd, Message m) {
    //fmai inatai trimitem lungimea mesajului
    int remaining_bytes = sizeof(m.len);
    int send_bytes = 0;
    memset(buffer, 0, BUFLEN);
    memcpy(buffer, &m.len, sizeof(int));
    while (remaining_bytes != 0) {
        int n = send(socketfd, buffer + send_bytes, remaining_bytes, 0);
        DIE(n < 0, "send len payload");
        remaining_bytes -= n;
        send_bytes += n;
    }

    //trimitem mesajul propriu-zis
    remaining_bytes = m.len;
    send_bytes = 0;
    memset(buffer, 0, BUFLEN);
    memcpy(buffer, m.payload, m.len);
    while (remaining_bytes != 0) {
        int n = send(socketfd, buffer + send_bytes, remaining_bytes, 0);
        DIE(n < 0, "send paylaod");
        remaining_bytes -= n;
        send_bytes += n;
    }
}

Message receive_message(int socketfd) {
    //primesc lungimea mesajului care trebuie sa fie de 4 octeti
    Message m;
    int remaining_bytes = sizeof(int);
    int recv_bytes = 0;
    memset(buffer, 0, BUFLEN);
    while (remaining_bytes != 0) {
        int n = recv(socketfd, buffer + recv_bytes, remaining_bytes, 0);
        DIE(n < 0, "recv len payload");
        remaining_bytes -= n;
        recv_bytes += n;
    }
    int len = 0;
    memcpy(&len, buffer, sizeof(int));
    m.len = len;

    //primesc len bytes de calup de date (payload)
    remaining_bytes = len;
    recv_bytes = 0;
    memset(buffer, 0, BUFLEN);
    while (remaining_bytes != 0) {
        int n = recv(socketfd, buffer + recv_bytes, remaining_bytes, 0);
        DIE(n < 0, "recv payload");
        remaining_bytes -= n;
        recv_bytes += n;
    }
    m.payload = (char *)calloc(m.len + 1, sizeof(char));
    DIE(m.payload == NULL, "error calloc payload for recv mess in server");
    memcpy(m.payload, buffer, m.len);
    return m;
}

int portno, sockfd;
struct sockaddr_in serv_addr;

int main(int argc, char *argv[]) {
	int ret;
    //dezactiveaza buffering-ul la afisare
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	//eroare folosire incorecta a servarului
    //serverul este oprit si se afiseaza un mesaj pentru cum se utilizeaza
    if (argc < 4) {
        usage(argv[0]);
        return 0;
    }

	//deschid socket TCP catre server
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket client");
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton clinet");

	//dezactivam Neagle pe socket
	int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

	//conectare la server pe socketul creat
	ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect client to server");

	//trimit un mesaj cu client_id-ul meu
	Message clientId_mess;
	clientId_mess.len = strlen(argv[1]);
	clientId_mess.payload = NULL;
	clientId_mess.payload = strdup(argv[1]);
	DIE(clientId_mess.payload == NULL, "strdup clinetId");
	send_message(sockfd, clientId_mess);

    //seturile de filedescriptori pt multiplaxarea I/O pe socheti
    fd_set read_fds, tmp_fds;
    //valorea maxima in setul de filedescriptori
    int fdmax = 0;

    //initializez fds-urile cu un set gol
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    //adaug in setul de file descriptori pe ce STDIN si socketul TCP catre server
    FD_SET(sockfd, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    fdmax = max(sockfd, STDIN_FILENO);

	while (true) {
		//vad care file desriptori de socketuri sunt disponibili
        tmp_fds = read_fds;
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select fds client");
		for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &tmp_fds))
                continue;
			if (i == STDIN_FILENO) {
				//citesc in buffer linia cu comanda de la stdin
				char read_buffer[BUFLEN];
				memset(read_buffer, 0, BUFLEN);
				DIE( fgets(read_buffer, BUFLEN, stdin) == NULL, "error fgets stdin client" );
                read_buffer[ strlen(read_buffer) - 1 ] = '\0';
				//Avem o comanda de la un client de forma: subscribe, unsubscribe, exit
                // 1 - unsubscribe, 2 - subscribe, 3 - exit
                /*
                    | --- LEN --- | --- TYPE -- | --- TOPIC --- | --- SF --- |
                        4 octeti        1 octet    len - f(type)   1 octet
                        lungime         3/1/2       octeti
                        payload    <----            paylaod               --->
                */
			    // daca type e 3 avem exit si nimic dupa
			   	// daca type e 1 avem unsubscribe si nu avem sf; f(type) = 1
                // daca type e 2 avem subscribe si avem sf; f(type) = 2
			   	if ( strcmp(read_buffer, "exit") == 0 ) {
					//anunt serverul ca am dat exit ca sa nu ma mai vada ca fiind online
					char type = 3;
					Message m;
					m.payload = NULL;
					m.payload = (char *)calloc(1, sizeof(char));
					DIE(m.payload == NULL, "calloc exit client");
					memcpy(m.payload, &type, 1);
					m.len = 1;
					send_message(sockfd, m);
					close(sockfd);
    				return 0;
				}

				if ( strncmp(read_buffer, "subscribe", 9) == 0 ) {
					char *comanda = strtok(read_buffer, " ");
                	char *topic = strtok(NULL, " ");
                	char *sf = strtok(NULL, " ");

					if (topic == NULL || sf == NULL) {
						fprintf(stderr, "Comanda gresita subscribe.\n");
						continue;
					}

					//cout << topic << " " << sf << "\n";

					char sf_val = (char)atoi(sf) + 1;
					char type = 2;
					Message m;
					m.payload = NULL;
					m.payload = (char *)calloc(strlen(topic) + 2, sizeof(char));
					DIE(m.payload == NULL, "calloc subscribe client");
					memcpy(m.payload, &type, 1);
					memcpy(m.payload + 1, topic, strlen(topic));
					memcpy(m.payload + 1 + strlen(topic), &sf_val, sizeof(char));
					m.len = 2 + strlen(topic);
					//cout << m.len << "\n";
					send_message(sockfd, m);
					printf("Subscribed to topic.\n");
					continue;
				}

				if ( strncmp(read_buffer, "unsubscribe", 11) == 0 ) {
					char *comanda = strtok(read_buffer, " ");
                	char *topic = strtok(NULL, " ");

					if (topic == NULL) {
						fprintf(stderr, "Comanda gresita unsubscribe.\n");
						continue;
					}
					char type = 1;
					Message m;
					m.payload = NULL;
					m.payload = (char *)calloc(strlen(topic) + 1, sizeof(char));
					DIE(m.payload == NULL, "calloc unsubscribe client");
					memcpy(m.payload, &type, 1);
					memcpy(m.payload + 1, topic, strlen(topic));
					m.len = 1 + strlen(topic);
					send_message(sockfd, m);
					printf("Unubscribed from topic.\n");
					continue;
				}

			} else if (i == sockfd) {
				//primesc mesaj de la server si il afisez
				Message m = receive_message(sockfd);

				if (strcmp(m.payload, "exit") == 0) {
					//serverul s-a inchis asa ca inchid si eu clientul
					close(sockfd);
    				return 0;
				}

				//am mesajul deja procesat de la server eu doar il afisez
				printf("%s\n", m.payload);
			}
		}
	} 
	close(sockfd);
    return 0;
}