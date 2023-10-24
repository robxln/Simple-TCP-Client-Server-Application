#include "helper.h"
using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

int portno; //port number of server
int sockfd_udp, sockfd_tcp; //socketi pe care fie se primesc mesaje de la clientii UDP, fie se asteapta cereri de conexiuni pt clientii TCP
struct sockaddr_in serv_addr_udp, serv_addr_tcp; //adresele socketilor pt portul UDP si cel ce primeste conexiuni TCP


//buffer pentru parsare citire/mesaje
char buffer[BUFLEN];

struct Message {
    int len;
    char *payload;
};

map <string, int> clientId_socket_binding;
map <int, string> socket_clientId_binding;
map <string, set<string> > topic_clientIds_binding;

set < pair<string,string> > client_topic_activ_sf;
set <string> online_clients;
map <string, queue<Message> > client_messque;

void clearALL() {
    for (auto e : client_messque) {
        while (!e.second.empty())
            e.second.pop();
    }
    for (auto e : topic_clientIds_binding) {
        e.second.clear();
    }
    client_topic_activ_sf.clear();
    topic_clientIds_binding.clear();
    client_messque.clear();
    clientId_socket_binding.clear();
    socket_clientId_binding.clear();
    online_clients.clear();
}

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

Message assembly_message(char topic[], char data_type, char content[], char ip[], uint16_t port) {
    //cout << "intru in assembly_message\n";
    Message m;
    string result;
    result.clear();
    result.append(ip);
    result.append(":");
    result.append(to_string(port));
    result.append(" - ");
    result.append(topic);
    result.append(" - ");
    if (data_type == 0) { // INT
        //am un INT si primul octet din content e semnul urmat de 4 octeti cu valorea sa
        uint8_t sign;
        uint32_t value;
        memcpy(&sign, content, 1);
        memcpy(&value, content + 1, 4);
        value = ntohl(value);
        int number = (sign == 0) ? value : (0 - value);
        result.append("INT - ");
        result.append(to_string(number));

    } else if (data_type == 1) { // SHORT_REAL
        //am un SHORT_REAL care e valuarea actualua inmultita cu 100, si cu precizie de 2 zecimale
        uint16_t value;
        memcpy(&value, content, sizeof(uint16_t));
        value = ntohs(value);
        float number = (float)(value) / 100.0;
        result.append("SHORT_REAL - ");
        char float_buffer[20];
        memset(float_buffer, 0, sizeof(float_buffer));
        sprintf(float_buffer, "%.2f", number);
        string aux_str(float_buffer);
        result.append(aux_str);

    } else if (data_type == 2) { // FLOAT
        // am un FLOAT : 1 octet de semn, 4 octeti cu valorea in modul a {parte intreaga + parte fractionara}, 1 octet pt putere negativa a lui 10
        uint8_t sign, power;
        uint32_t value;
        memcpy(&sign, content, 1);
        memcpy(&value, content + 1, 4);
        memcpy(&power, content + 5, 1);
        value = ntohl(value);
        float number = (sign == 0) ? (1.0 * value * pow(10,  0 - power)) : (-1.0 * value * pow(10, 0 - power));
        result.append("FLOAT - ");
        result.append(to_string(number));
    } else if (data_type == 3) { // STRING
        result.append("STRING - ");
        result.append(content);
    } else {
        DIE(true, "No such data_type");
    }
    m.len = result.size();
    m.payload = NULL;
    m.payload = strdup(result.c_str());
    DIE(m.payload == NULL, "convert string to payload");
    return m;
}

int main(int argc, char *argv[]) {
    int ret;
    //dezactiveaza buffering-ul la afisare
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    //eroare folosire incorecta a servarului
    //serverul este oprit si sea fiseaza un mesaj pentru cum se utilizeaza
    if (argc < 2) {
        usage(argv[0]);
        return 0;
    }

    //extrag numarul portului serverului din linia de comanda
    portno = atoi(argv[1]);
    DIE(portno == 0, "Error parsing porno");


    //seturile de filedescriptori pt multiplaxarea I/O pe socheti
    fd_set read_fds, tmp_fds;
    //valorea maxima in setul de filedescriptori
    int fdmax = 0;

    //initializez fds-urile cu un set gol
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    //deschid socket pt clientii UDP
    sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(sockfd_udp < 0, "socket udp");

    //setez adresa pt socketul UDP
    memset(&serv_addr_udp, 0, sizeof(serv_addr_udp));
    serv_addr_udp.sin_family = AF_INET;
    serv_addr_udp.sin_port = htons(portno);
    serv_addr_udp.sin_addr.s_addr = INADDR_ANY;


    //deschid socket pt a permite conectarea la server pt clienti TCP
    sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd_tcp < 0, "socket tcp");

    //serez adresa pt socketul ce primeste conexiuni TCP
    memset(&serv_addr_tcp, 0, sizeof(serv_addr_tcp));
    serv_addr_tcp.sin_family = AF_INET;
    serv_addr_tcp.sin_port = htons(portno);
    serv_addr_tcp.sin_addr.s_addr = INADDR_ANY;

    //facem bind la socketul UDP
    ret = bind(sockfd_udp, (struct sockaddr *) &serv_addr_udp, sizeof(serv_addr_udp));
    DIE(ret < 0, "bind socket udp");

    //facem bind la socketul pt conexiuni TCP
    ret = bind(sockfd_tcp, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp));
    DIE(ret < 0, "bind socket tcp");
    
    //punem socketul tcp sa asculte si sa primeasca cereri de conexiuni de la clinetii TCP
    ret = listen(sockfd_tcp, MAX_CLIENTS);
    DIE(ret < 0, "listen");

    //adaug in setul de file descriptori pe ce TCP, UDP si STDIN
    FD_SET(sockfd_udp, &read_fds);
    FD_SET(sockfd_tcp, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    fdmax = max(sockfd_udp, max(sockfd_tcp, STDIN_FILENO) );

    while (true) {

        //vad care file desriptori de socketuri sunt disponibili
        tmp_fds = read_fds;
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select fds server");

        for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &tmp_fds))
                continue;
            if (i == STDIN_FILENO) {
                // serverul primeste comanda de la stdin
                //singura comanda pe care o accepta este cea de exit

                memset(buffer, 0, BUFLEN);
                DIE( fgets(buffer, BUFLEN, stdin) == NULL, "error fgets stdin server" );
                buffer[ strlen(buffer) - 1 ] = '\0';
                if ( strcmp(buffer, "exit") == 0 ) { //am in buffer comanda de exit
                    for (string clientId: online_clients) {
                        Message m;
                        m.len = 0;
                        m.payload = NULL;
                        m.payload = strdup("exit");
                        DIE (m.payload == NULL, "strdup exit");
                        m.len = strlen(m.payload);
                        int fd = clientId_socket_binding[clientId];
                        send_message(fd, m);
                        DIE(close(fd) < 0, "close TCP client socket");
                    }
                    DIE(close(sockfd_tcp) < 0, "close TCP listening for connect socket");
                    DIE(close(sockfd_udp) < 0, "close UDP socket");
                    clearALL();
                    exit(0);
                } else if ( strcmp(buffer, "printClients") == 0) {
                    //afisez clientii online la comanda
                    for (string clientId: online_clients)
                        printf("%s, ", clientId.c_str());
                    printf("\n");
                    //afisez toti clientii conectati pana acum
                    for (auto e: client_messque)
                        printf("%s, ", e.first.c_str());
                    printf("\n");
                } else
                    continue; //nu se intampla nimic

            } else if (i == sockfd_tcp) { //avem o cerere de conexiune TCP
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);

                //acceptam si cream un nou file descriptor socket pentru noul client
                int new_sockfd = accept(sockfd_tcp, (struct sockaddr *) &cli_addr, &cli_len);
                DIE(new_sockfd < 0, "accept new client TCP");

                //adaugam noul file descriptor in multimea descriptorilor
                FD_SET(new_sockfd, &read_fds);
                fdmax = max(fdmax, new_sockfd);

                //dezactivam Neagle pe noul fd socket
                int flag = 1;
                int ret = setsockopt(new_sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
                DIE(ret < 0, "TCP_NODELAY new socket");

                //primesc client_id pe socket
                Message m = receive_message(new_sockfd);
                string clientId(m.payload);

                //vad daca nu cumva deja exista cleintul
                if ( online_clients.find(clientId) != online_clients.end() ) {
                    //clientul e deja online
                    cout << "Client " << clientId << " already connected.\n";
                    //ii trimit mesaj de exit
                    Message m;
                    m.len = 0;
                    m.payload = NULL;
                    m.payload = strdup("exit");
                    DIE (m.payload == NULL, "strdup exit");
                    m.len = strlen(m.payload);
                    send_message(new_sockfd, m);

                    //inchid si scot socketul din multimea de fds
                    DIE(close(new_sockfd) < 0, "close already existing client socket");
                    FD_CLR(new_sockfd, &read_fds);
                } else {
                    //cleintul nu e online dar vad daca nu cumva se reconecteaza
                    //asta inseamna ca are o coada de mesaje fie ea si goala asociata cu id-ul
                    if (client_messque.find(clientId) != client_messque.end()) {
                        //am caz de reconectare al clientului
                        cout << "New client " << clientId << " connected from " << inet_ntoa(cli_addr.sin_addr) << ":" << ntohs(cli_addr.sin_port) << ".\n";
                        //trebuie sa trimit toate mesajele din coada
                        // am grija sa updatez informatia despre client
                        clientId_socket_binding[clientId] = new_sockfd;
                        socket_clientId_binding[new_sockfd] = clientId;
                        online_clients.insert(clientId);
                        queue<Message> que = client_messque[clientId];
                        while (!que.empty()) {
                            send_message(new_sockfd, que.front());
                            que.pop();
                        }
                    } else {
                        //clientul este nou si nu a mai fost pana acum
                        cout << "New client " << clientId << " connected from " << inet_ntoa(cli_addr.sin_addr) << ":" << ntohs(cli_addr.sin_port) << ".\n";
                        clientId_socket_binding[clientId] = new_sockfd;
                        socket_clientId_binding[new_sockfd] = clientId;
                        online_clients.insert(clientId);
                        queue<Message> aux_que;
                        //am grija ca coada sa fie goala
                        while (!aux_que.empty())
                            aux_que.pop();
                        client_messque[clientId] = aux_que;
                    }
                }

            } else if (i == sockfd_udp) {
                //read from the UDP socket a new message
                //cout << "intru pe udp\n";
                memset(buffer, 0, BUFLEN);
                socklen_t sock_udp_len = sizeof(serv_addr_udp);
                ret = recvfrom(sockfd_udp, buffer, BUFLEN, 0, (struct sockaddr *) &serv_addr_udp, &sock_udp_len);
                DIE(ret < 0, "recvfrom UDP");

                /* extrag datele din buffer
                | --- TOPIC --- | --- DATA_TYPE --- | --- CONTENT --- |
                      50 bytes        1 byte              1500 bytes
                */
                //cout << "am facut recv de pe UDP\n";
                char topic[52];
                memset(topic, 0, sizeof(topic));
                char data_type;
                char content[1505];
                memset(content, 0, sizeof(content));

                memcpy(topic, buffer, 50);
                memcpy(&data_type, buffer + 50, 1);
                memcpy(content, buffer + 51, 1500);

                //construiesc mesajul
                Message m = assembly_message(topic, data_type, content, inet_ntoa(serv_addr_udp.sin_addr), ntohs(serv_addr_udp.sin_port));
                //cout << "mesajul pe server de la udp este: " << m.payload << "\n";

                //vad daca am nu cumva nu exista instatioata vreo lista de clientii abonati la topic
                if (topic_clientIds_binding.find(topic) == topic_clientIds_binding.end()) {
                    //cout << "nu am topicul\n";
                    continue; //sar peste
                }

                //vad ce clienti sunt abonati la acest topic
                string _topic(topic);
                set<string> clients = topic_clientIds_binding[_topic];
                //cout << "clienti abonati la topic: \n";
                for (set<string>::iterator it = clients.begin(); it != clients.end(); it++) {
                    string clientId = (*it);
                    //cout << clientId << " ";
                    if (online_clients.find(clientId) != online_clients.end()) { //cleintul este online asa ca ii trimit direct mesajul
                        int sockfd = clientId_socket_binding[clientId]; // extragul socketul TCP al clientului
                        send_message(sockfd, m); // trimit mesajul clientului
                    } else {
                        //clientul nu e online dar vad daca nu cumva are sf activ si trebuie sa ii pun mesajul in coada de store
                        //verific daca nu cumva clientul se afla in multimea de clienti cu sf activ la acest topic
                        if ( client_topic_activ_sf.find(make_pair(clientId, _topic)) != client_topic_activ_sf.end() ) {
                            //cleintul are sf-ul activ asa ca ii pun mesajul in coada sa
                            client_messque[clientId].push(m);
                        }
                    }
                }
                //cout << endl;

            } else {
                //Avem o comanda de la un client de forma: subscribe, unsubscribe, exit
                // 1 - unsubscribe, 2 - subscribe, 3 - exit
                /*
                    | --- LEN --- | --- TYPE -- | --- TOPIC --- | --- SF --- |
                        4 octeti        1 octet    len - f(type)   1 octet
                        lungime         1/2/3       octeti
                        payload    <----            paylaod               --->
                */
                // daca type e 3 avem exit si nimic dupa
                // daca type e 1 avem unsubscribe si nu avem sf; f(type) = 1
                // daca type e 2 avem subscribe si avem sf; f(type) = 2
                Message m = receive_message(i);
                char type, sf = -1;
                char topic[52];
                memset(topic, 0, sizeof(topic));
                memcpy(&type, m.payload, 1);
                //cout << "type: " << (int)type << "\n";

                if (type == 3) {
                    //clientul s-a deconectat asa ca il scot din setul cu clientii online
                    string clientId = socket_clientId_binding[i];
                    online_clients.erase(clientId);
                    //cout << "exit " << clientId << "\n";
                    cout << "Client " << clientId << " disconnected.\n";
                    close(i);
                    FD_CLR(i, &read_fds);
                } else if (type == 1) {
                    //am comanda de unsubscribe
                    memcpy(topic, m.payload + 1, m.len - 1);
                    string clientId = socket_clientId_binding[i];
                    string _topic(topic);
                    topic_clientIds_binding[_topic].erase(clientId);
                    //Maybe i am wrong
                    client_topic_activ_sf.erase( make_pair(clientId, _topic) );
                } else if (type == 2) {
                    //am comanda de subscribe
                    //cout << m.len << "\n";
                    //cout << m.payload << " " << m.payload + 1 << "\n";
                    memcpy(topic, m.payload + 1, m.len - 2);
                    memcpy(&sf, m.payload + m.len - 1, 1);
                    
                    //sf l-am crescut cu 1 pentru a se trimite corect
                    sf--;

                    string clientId = socket_clientId_binding[i];
                    string _topic(topic);
                    //cout << "Am cerere de subscribe: " << clientId << " " << _topic << " " << (int)sf << "\n";
                    

                    //adaug la topic un nou clinet urmaritor si am grija sa instantiez setul gol inainte daca e cazul
                    if (topic_clientIds_binding.find(_topic) != topic_clientIds_binding.end()) {
                        //l-am gasit
                        //cout << "am gasit lista de clienti pt topic\n";
                        topic_clientIds_binding[_topic].insert(clientId);
                    } else {
                        //cout << "instantiez set clienti\n";
                        set<string> my_set;
                        my_set.clear();
                        topic_clientIds_binding[_topic] = my_set;
                        topic_clientIds_binding[_topic].insert(clientId);
                    }
                    
                   
                    if (sf == 1)
                        client_topic_activ_sf.insert( make_pair(clientId, _topic) );
                    else
                        client_topic_activ_sf.erase( make_pair(clientId, _topic) );
                }
            }
        }

    }
    close(sockfd_tcp);
    close(sockfd_udp);
    return 0;
}