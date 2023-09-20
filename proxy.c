#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_t{
  char request[MAXLINE];
  int response_size;
  char response[MAX_OBJECT_SIZE];
  struct cache_t *prev, *next;
} cache_t;

cache_t *head;
cache_t *tail;

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *host_uri, char* server_port);
void *thread(void *vargp);
cache_t* find_cache(char str[]);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  int clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  head = (cache_t *)calloc(1, sizeof(cache_t));
  tail = (cache_t *)calloc(1, sizeof(cache_t));
  head->next = tail;
  tail->prev = head;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); 

  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); 
    printf("커넥션 연결 : %d\n", *connfdp);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
  return 0;
}

void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);  
  Close(connfd);
  return NULL;
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], host_uri[MAXLINE], host_port[MAXLINE];
  char request_buf[MAX_CACHE_SIZE], response_buf[MAX_CACHE_SIZE];

  rio_t request_rio, response_rio;

  Rio_readinitb(&request_rio, fd);

  Rio_readlineb(&request_rio, buf, MAXLINE);


  printf("===========%s\n", buf);
  cache_t* res = find_cache(buf);
  if (res) {
    printf("캐시된 경우!!!\n");
    Rio_writen(fd, res->response, res->response_size);
    return;
  }

  cache_t* new_node = (cache_t *) calloc(1, sizeof(cache_t));

  printf("Request headrs :\n");
  printf("%s", buf);
  strcpy(new_node->request, buf);
  printf("new_node의 url: %s\n", new_node->request);

  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  parse_uri(uri, hostname, host_uri, host_port);

  int clientfd = Open_clientfd(hostname, host_port);

  int content_length;

  sprintf(request_buf, "%s %s %s\r\n", method, host_uri, version);
  sprintf(request_buf, "%sUser-Agent:%s", request_buf, user_agent_hdr);
  sprintf(request_buf, "%sConnection: close\r\n", request_buf);
  sprintf(request_buf, "%sProxy-Connection: close\r\n\r\n", request_buf);
  printf("%s\n",request_buf);
  Rio_writen(clientfd, request_buf, strlen(request_buf));


  // // Rio_readlineb 사용
  // int n;
  // while ((n = Rio_readlineb(&response_rio, response_buf, MAXLINE)) > 0) {
  //   Rio_writen(fd, response_buf, n);
  // }

  int res_size;
  Rio_readinitb(&response_rio, clientfd);
  res_size = Rio_readnb(&response_rio, response_buf, MAX_CACHE_SIZE);

  response_buf[res_size] = '\0';
  Rio_writen(fd, response_buf, res_size);
  Close(clientfd);

  new_node->response_size = res_size;
  strcpy(new_node->response, response_buf);
  add_cache(new_node);
  print_list();
}

void parse_uri(char *uri, char *hostname, char *host_uri, char* server_port) {
  strcpy(server_port, "80");
  char* tmp[MAXLINE];
  sscanf(uri, "http://%s", tmp);

  char* p = index(tmp, '/');
  *p = '\0';
  
  strcpy(hostname, tmp);
  
  char* q = strchr(hostname, ':');
  if (q != NULL) {
    printf("포트 있음\n");
    *q = '\0';
    strcpy(server_port,q+1);
  }
  strcpy(host_uri, "/");
  strcat(host_uri, p+1);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}


cache_t* find_cache(char str[]) {
  cache_t* now = head;

  while (now->next != tail) { 
    if (!strcmp(now->request, str)){
      return now;
    } // 같으면 0반환
    now = now -> next;
  }
  return NULL;
}

void delete_last() {
  cache_t* last = tail->prev;
  last->prev->next = tail;
  tail->prev = last->prev;
  free(last);
}

void add_cache(cache_t* new) {
  new->prev = head;
  new->next = head->next;
  head->next->prev = new;
  head->next = new;
}

void print_list() {
  cache_t* now = head;
  while (now->next != tail) { 
    printf("now: %p, next: %p now의 url: %s\n", now, now->next, now->request);
    now = now -> next;
  }

}