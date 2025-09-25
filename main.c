#ifdef _WIN32
  #include <ws2tcpip.h>
  #include <winsock2.h>
  #include <windows.h>
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netdb.h>
#endif

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#define BUFLEN 1024
#define MSG_HELP "rcon-cli [-H HOST] [-P PORT] [-s (Use as shell)] -p PASSWORD\n" \
                            "\t[-D (Debug)] [-t TIMEOUT_IN_SECONDS]\0"
#define MSG_ERR "[!]"
#define MSG_LOG "[*]"

#define ARG_PREFIX '-'

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 27015

char host[] = "127.0.0.1";
char port[] = "27015";
char password[BUFLEN];
bool password_set = false;
float timeout = 10.0;
bool shell = false;
bool debug = false;

char cmd[BUFLEN];
int cmd_i = 0;

int f_strcpy(char *to, char *from) {
  int i = 0;
  while (*to++ = *from++)
    ++i;
  *to = '\0';
  return i;
}

int f_strlen(char *s) {
  char *t = s;
  while(*s)
    ++s;
  return s - t;
}

int f_strcat(char *to, char *from) {
  int i = f_strlen(to);
  int catted = 0;
  for (int j = 0;from[j];++j, ++i) {
    to[i] = from[j];
    ++catted;
  }
  to[i] = '\0';
  return catted;
}

void print_help(void) {
  puts(MSG_HELP);
  exit(1);
}

void death(char *s, int code) {
  fprintf(stderr, MSG_ERR " %s\n", s);
  if (code)
    fprintf(stderr, "Error code: %d\n", code);
#ifdef WIN32
  WSACleanup();
#endif
  exit(1);
}

void close_socket(int sock) {
#ifdef WIN32
  WSACleanup();
  closesocket(sock);
#else
  shutdown(sock, SHUT_RDWR);
#endif
}

int init_socket(void) {
#ifdef WIN32
  WSADATA wsa_data;
  int result_code;
  result_code = WSAStartup(MAKEWORD(2,2), &wsa_data);
  if (result_code != 0)
    death("WSAStartup failed", result_code);
#endif

  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  if ((s = getaddrinfo(host, port, &hints, &result)) != 0)
    death("getaddrinfo failed", 0);

  for (rp = result;rp != NULL;rp = rp->ai_next) {
    int sock, ret;
    char hostnum[NI_MAXHOST];

    if ((ret = getnameinfo(rp->ai_addr, rp->ai_addrlen, hostnum,
            sizeof(hostnum), NULL, 0, NI_NUMERICHOST)) != 0) {
      death("getnameinfo failed", 0);
    }

    if ((sock =
          socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) {
      if ((rp->ai_family == AF_INET6) && (errno = EAFNOSUPPORT))
        printf(MSG_LOG" socket: No IPv6 support on this host\n");
      else
        printf("socket: Error %s\n", strerror(errno));
      continue;
    }

    if (connect(sock, rp->ai_addr, rp->ai_addrlen) < 0) {
      printf(MSG_LOG" connect: %s\n", strerror(errno));
      close_socket(sock);
      continue;
    } else {
      return sock;
      break;
    }
  }

  death("Could not connect to host", 0);

  return -1;
}


void parse_args(char **argv) {
  while (*(++argv)) {
    switch (**argv) {
      case ARG_PREFIX:
        switch (*(++(*argv))) {
          case 'I': case 'H': // Keep compatible with first version
            if (*(++argv)) {
              f_strcpy(host, *argv);
            }
            else {
              print_help();
              --argv;
            }
            break;
          case 'P':
            if (*(++argv)) {
              f_strcpy(port, *argv);
            } else {
              print_help();
              --argv;
            }
            break;
          case 'p':
            if (*(++argv)) {
              f_strcpy(password, *argv);
              password_set = true;
            } else
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

void set_timeout(int sock) {
#ifdef WIN32
  int timeo = timeout * 1000;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
        (const char*)&timeo, sizeof(timeo)) == -1)
    death("setsockopt failed (SO_RCVTIMEO)", 0);
#else
  struct timeval tv;
  tv.tv_usec = (timeout - (int)timeout) * 1000000;
  tv.tv_sec = timeout - (timeout - (int) timeout);
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
    death("setsockopt failed (SO_RCVTIMEO)", 0);
#endif
}

void configure_socket(int sock) {
  set_timeout(sock);
}

bool get_timeout_error(void) {
  int err;
#ifdef WIN32
  err = WSAGetLastError();
  if (err == WSAETIMEDOUT)
    return true;
#else
  err = errno;
  if (err == EWOULDBLOCK)
    return true;
#endif
  return false;
}

size_t send_message(int sock) {
  char *msg = (char*)malloc(sizeof(char) * BUFLEN);
  f_strcpy(msg, "\xff\xff\xff\xffrcon \0");
  f_strcat(msg, password);
  f_strcat(msg, cmd);

  size_t sent = send(sock, msg, f_strlen(msg) + 1, 0);
  if (sent < 0)
    death("Send failed", 0);
  free(msg);
  return sent;
}

void recv_message(int sock) {
  char recv_buf[BUFLEN];
  int recv_n = 0;
  do {
    if (recv_n == 10) // No more to receive
      break;

    recv_n = recv(sock, recv_buf, BUFLEN, 0);
    recv_buf[recv_n] = '\0';

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
      if (get_timeout_error() == true) {// Timeout
        break;
      }
      else
        printf("recv failed\n");
    }
  } while (recv_n > 0);
}

int get_line(char *buffer, int bufsize) {
  int c, len = 0;

  cmd[len++] = ' ';
  while ((c = getchar()) != '\n' && len < bufsize) {
    cmd[len++] = c;
  }
  cmd[len] = '\0';

  return len;
}

void run_shell(int sock) {
  int len = 0;
  for (;;) {
    putchar('>');
    putchar(' ');
    len = get_line(cmd, BUFLEN);

    if (len > 0) {
      send_message(sock);
      recv_message(sock);
    }
  }
}

int main(int argc, char **argv) {
  parse_args(argv);

  if (!password_set)
    print_help();

  if (debug) {
    //printf("IP: %s\nPort: %u\nPassword: %s\n", host, port, password);
    printf("CMD:%s\n", cmd);
  }

  int sock = init_socket();

  if (sock == -1)
    death("Failed to create socket", sock);

  configure_socket(sock); // Set timeout for recv
  
  if (shell == true) {
    run_shell(sock);
  } else {
    send_message(sock);
    recv_message(sock);
  }

  close_socket(sock);

  return 0;
}
