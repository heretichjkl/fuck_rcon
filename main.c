#ifdef WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#define BUFLEN 1024
#define MSG_HELP "rcon-cli [-I IP] [-P PORT] [-s (Use as shell)] -p PASSWORD\n" \
                            "\t[-D (Debug)] [-t TIMEOUT_IN_SECONDS]\0"
#define ARG_PREFIX '-'

char ip[] = "127.0.0.1";
uint16_t port = 27015;
char password[BUFLEN];
bool password_set = false;
float timeout = 1.0;
bool shell = false;
bool debug = false;


char cmd[BUFLEN];
int cmd_i = 0;

void f_strcpy(char *to, char *from) {
  while (*to++ = *from++)
    ;
  *to = '\0';
}

int f_strlen(char *s) {
  char *t = s;
  while(*s)
    ++s;
  return s - t;
}

void f_strcat(char *to, char *from) {
  int i = f_strlen(to);
  for (int j = 0;from[j];++j, ++i) {
    to[i] = from[j];
  }
  to[i] = '\0';
}

void print_help(void) {
  puts(MSG_HELP);
  exit(1);
}

void death(char *s, int code) {
  fprintf(stderr, "%s\n", s);
  if (code)
    fprintf(stderr, "Error code: %d\n", code);
#ifdef WIN32
  WSACleanup();
#endif
  exit(1);
}

int initSocket(int sock) {
#ifdef WIN32
  WSADATA wsa_data;
  int result;
  result = WSAStartup(MAKEWORD(2,2), &wsa_data);
  if (result != 0)
    death("WSAStartup failed", result);
#endif
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  return sock;
}

void closeSocket(int sock) {
#ifdef WIN32
  WSACleanup();
  closesocket(sock);
#else
  shutdown(sock, SHUT_RDWR);
#endif
}

void parseArgs(char **argv) {
  while (*(++argv)) {
    switch (**argv) {
      case ARG_PREFIX:
        switch (*(++(*argv))) {
          case 'I':
            if (*(++argv)) {
              f_strcpy(ip, *argv);
            }
            else {
              print_help();
              --argv;
            }
            break;
          case 'P':
            if (*(++argv)) {
              port = atoi(*argv);
            }
            else
              print_help();
            break;
          case 'p':
            if (*(++argv)) {
              f_strcpy(password, *argv);
              password_set = true;
            }
           else
             print_help();
           break;
           case 't':
             if (*(++argv)) {
               timeout = atof(*argv);
             }
             else
               print_help();
             break;
           case 's':
             shell = true;
             break;
           case 'D':
             debug = true;
             break;
         }
         break;
       default:
         if (cmd_i < BUFLEN) {
           cmd[cmd_i++] = ' ';
           for (int i = 0;(*argv)[i];++i) {
             cmd[cmd_i++] = (*argv)[i];
           }
           cmd[cmd_i] = '\0';
         }
         break;
  
     }
   }

}

int main(int argc, char **argv) {
  parseArgs(argv);

  if (!password_set)
    print_help();

  if (debug) {
    printf("IP: %s\nPort: %u\nPassword: %s\n", ip, port, password);
    printf("CMD:%s\n", cmd);
  }

  int sock;

  if ((sock = initSocket(sock)) == -1)
    death("Failed to create socket", sock);

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = inet_addr(ip);
  server.sin_port = htons(port);

  // Set timeout for recv
  struct timeval tv;
  tv.tv_usec = (timeout - (int)timeout) * 1000000;
  tv.tv_sec = timeout - (timeout - (int)timeout);
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
    death("setsockopt failed (SO_RCVTIMEO)", 0);

  char *msg = (char*)malloc(sizeof(char) * BUFLEN);
  f_strcpy(msg, "\xff\xff\xff\xffrcon \0");
  f_strcat(msg, password);
  f_strcat(msg, cmd);

  size_t sent = sendto(sock, msg, f_strlen(msg) + 1, 0,
      (const struct sockaddr*)&server, sizeof(server));
  if (sent < 0)
    death("Send failed", 0);

  char recv_buf[BUFLEN];
  int recv_n;
  do {
    recv_n = recv(sock, recv_buf, BUFLEN, 0);
    if (recv_n > 0) {
      int i = 0;
  
      for (;i < recv_n;++i) {
        if (recv_buf[i] == '\n') {
          ++i;
          break;
        }
      }
  
      for (;i < recv_n;++i)
        putc(recv_buf[i], stdout);
    }
    else if (recv_n == 0)
      printf("Connection closed\n");
    else {
      int err = errno;
      if (err == EWOULDBLOCK) // Timeout
        break;
      else
        printf("recv failed\nCode: %d\n", err);
    }
  } while (recv_n > 0);

  closeSocket(sock);

  return 0;
}
