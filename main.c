#ifdef _WIN32
  #include <ws2tcpip.h>
  #include <winsock2.h>
  #include <windows.h>
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netdb.h>
#endif

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

static int BUFLEN = 1024;

#define MSG_HELP "rcon-cli [-H HOST] [-P PORT] [-s (Use as shell)] -p PASSWORD\n" \
                            "\t[-D (Debug)] [-T TIMEOUT_IN_SECONDS] [-t (Use tcp)]\0"
#define MSG_ERR "[!]"
#define MSG_LOG "[*]"

#define ARG_PREFIX '-'

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 27015

#define TCP_AUTHENTICATE 3
#define TCP_EXEC 2
#define TCP_RESPONSE 0

#define RCON_ID 0xCAC1

typedef unsigned char byte;

char host[] = "127.0.0.1";
char port[] = "27015";
char *password;
bool password_set = false;
float timeout = 10.0;
bool tcp = false;
bool shell = false;
bool debug = false;

byte *data;
int data_i = 0;

bool rcon_auth(int sd);

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

size_t f_strncat(char *to, char *from, int n) {
  int l = f_strlen(to);
  size_t catted = 0;
  for (int i = 0;from[i] && i < n;++i, ++l) {
    to[l] = from[i];
    ++catted;
  }
  to[l] = '\0';

  return catted;
}

void print_help(void) {
  puts(MSG_HELP);
  exit(1);
}

int le_bytes_to_int(byte *s, int n, int offset) {
  int sum = 0;
  for (int i = 3, j = 3;i >= 0;--i, --j) {
    sum += s[i + offset] * (j > 0 ? (int) pow(256, j) : 1);
  }

  return sum;
}

void write_le_int_to_bytes(byte *s, int x, int offset) {
  for (int i = 3, j = 3;i >= 0;--i, --j) {
    s[i + offset] = (x >> j*8) & 0xff;
  }
  s[4 + offset] = '\0';
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
  hints.ai_socktype = (tcp ? SOCK_STREAM : SOCK_DGRAM);
  hints.ai_protocol = (tcp ? IPPROTO_TCP : IPPROTO_UDP);

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
            tcp = true;
            break;
          case 'T':
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
        if (data_i < BUFLEN) {
          data[data_i++] = ' ';
          for (int i = 0;(*argv)[i];++i) {
            data[data_i++] = (*argv)[i];
          }
          data[data_i] = '\0';
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

byte *pkg_build(int32_t cmd, char *data_str) {
  byte *pkg = (byte*) malloc(sizeof(byte) * BUFLEN);

  if (tcp) {
    int len = f_strlen(data_str);
    write_le_int_to_bytes(pkg, 10 + len, 0);
    write_le_int_to_bytes(pkg, RCON_ID, 4);
    write_le_int_to_bytes(pkg, cmd, 8);
    f_strncat(pkg + 12, data_str, len);
  } else {
    f_strcpy(pkg, "\xff\xff\xff\xffrcon \0");
    f_strncat(pkg, password, f_strlen(password));
    f_strncat(pkg, data_str, f_strlen(data_str));
  }

  return pkg;
}

int pkg_send(int sock, byte *pkg) {
  //byte *pkg = pkg_build(0);

  int sent = send(sock, (char*) pkg, 
      (tcp == true ? le_bytes_to_int(pkg, 4, 0) + 4 : f_strlen(pkg) + 1),
      0);
  if (sent < 0)
    death("Send failed", 0);
  free(pkg); // Won't be used anymore
  return sent;
}

void pkg_print(char *pkg) {
  int i;

  /*for(i = 0;pkg[i];++i) {
    if (pkg[i] == '\n') {
      ++i;
      break;
    }
  }*/

  if (tcp) {
    int pkg_size = le_bytes_to_int(pkg, 4, 0);
    if (pkg_size == 10)
      return;
    pkg_size += 4;
    for (int i = 12;i < pkg_size;++i)
      putchar(pkg[i]);
    if (pkg[i-1] != '\n')
      putchar('\n');
  } else {
    for(i = 10;pkg[i];++i) {
      if (pkg[i] == '\n') i += 10;
      putchar(pkg[i]);
    }
  }
}

byte *pkg_recv(int sock) {
  byte *pkg = (byte*) malloc(BUFLEN);

  int total = 0;
  int recv_n = 0;
  int32_t pkg_size;
  do {
    if (tcp) {
      recv_n = recv(sock, (char*) &pkg_size, 4, 0);
      total += recv_n;

      if (recv_n == 0) {
        printf("Connection lost\n");
        return NULL;
      } else if (recv_n != 4) {
        printf("recv: Invalid packet size!\n");
        return NULL;
      } else if (pkg_size < 10 || pkg_size > BUFLEN) {
        printf("Invalid size of received packet!\n");
        return NULL;
      }

      write_le_int_to_bytes(pkg, pkg_size, 0);

      while (pkg_size + 4 > total) {
        recv_n = recv(sock, (char*) pkg + total, BUFLEN - total, 0);
        total += recv_n;
        if (recv_n == 0) {
          printf("Connection lost\n");
          return NULL;
        }
      }
      return pkg;
    } else {
      if (recv_n == 10) { // No more to receive
        //pkg_print(pkg);
        break;
      }

      recv_n = recv(sock, (char*) pkg + total, BUFLEN - total, 0);
      total += recv_n;
      pkg[total] = '\0';

      if (recv_n > 0) continue;//pkg_print(pkg);
      else if (recv_n == 0)
        printf("Connection closed\n");
      else {
        if (get_timeout_error() == true) {// Timeout
          printf("Timeout! (%f)\n", timeout);
          break;
        }
        else
          printf("recv failed\n");
      }
    }
  } while (recv_n > 0);

  return pkg;
}

int get_line(char *buffer, int bufsize) {
  int c, len = 0;

  data[len++] = ' ';
  while ((c = getchar()) != '\n' && len < bufsize) {
    data[len++] = c;
  }
  data[len] = '\0';

  return len;
}

void run_shell(int sock) {
  if (tcp) {
    if (rcon_auth(sock) == false)
      death("Failed to authenticate!", 0);
  }
  int len = 0;
  byte *pkg;
  for (;;) {
    putchar('>');
    putchar(' ');
    len = get_line(data, BUFLEN);

    if (len > 0) {
      pkg = pkg_build(TCP_EXEC, data + (tcp ? 1 : 0));
      pkg_send(sock, pkg);
      pkg = pkg_recv(sock);
      pkg_print(pkg);
    }
  }
}

bool rcon_auth(int sd) {
  int ret;
  byte *pkg = pkg_build(TCP_AUTHENTICATE, password);
  if (pkg == NULL)
    death("Failed to create packet\n", 0);

  ret = pkg_send(sd, pkg);
  if (!ret)
    return false;

  pkg = pkg_recv(sd);
  if (pkg == NULL)
    return false;
  int id = le_bytes_to_int(pkg, 4, 4);
  return id == -1 ? false : true;
}

int main(int argc, char **argv) {
  password = (char*) malloc(BUFLEN);
  data = (char*) malloc(BUFLEN);
  parse_args(argv);

  if (tcp)
    BUFLEN = 4096;

  if (!password_set)
    print_help();

  if (debug) {
    printf("IP: %s\nPort: %s\nPassword: %s\n", host, port, password);
    printf("DATA:%s\nBUFLEN: %d\n", data, BUFLEN);
  }

  int sock = init_socket();
  if (sock == -1)
    death("Failed to create socket", sock);

  configure_socket(sock); // Set timeout for recv
  
  if (shell == true) {
    run_shell(sock);
  } else {
    byte *pkg = pkg_build(TCP_EXEC, data + (tcp ? 1 : 0)); // Doesn't matter if tcp isn't used
    if (tcp) {
      if (rcon_auth(sock) == false)
        return 1;
    }
    pkg_send(sock, pkg);
    pkg = pkg_recv(sock);
    pkg_print(pkg);
  }

  close_socket(sock);

  return 0;
}
