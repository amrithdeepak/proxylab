#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEFAULT_HTTP_PORT 80

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char* connection_hdr = "Connection: close\r\n";
static const char* proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* header strings */
static const char* host_tag = "Host: ";
static const char* accept_tag = "Accept: ";
static const char* accept_encoding_tag = "Accept-Encoding: ";
static const char* user_agent_tag = "User-Agent: ";
static const char* connection_tag = "Connection: ";
static const char* proxy_connection_tag = "Proxy-Connection: ";

/* cache */
static int total_size;
static int num;
static int pc;

struct cache_block
{
	uint32_t use_index;
	uint32_t size;
	char* hostname;
	char* uri;
	char* data;
	struct cache_block* prev;
	struct cache_block* next;
};

static struct cache_block* end;

static void init_cache()
{
	total_size = 0;
	num = 0;
	end = NULL;
	pc = 0;
}
static void remove_LRU()
{
	struct cache_block* curr = end;
	struct cache_block* min_p = curr;
	int min = curr->use_index;
	curr = curr->next;
	while(curr != end)
	{
		if(curr->use_index < min)
		{
			min_p = curr;
			min = curr->use_index;
		}
		curr = curr->next;
	}
	total_size -= min_p->size;
	num--;
	Free(min_p);
}
static void add_elem(char *hostname, char *uri, char *data)
{
	int size = 0;

	struct cache_block *cb = Malloc(sizeof(struct cache_block));

	size += strlen(hostname);
	char* hn = Malloc(strlen(hostname));
	strcpy(hn, hostname);
	
	size += strlen(uri);
	char* u = Malloc(strlen(uri));
	strcpy(u, uri);

	size += strlen(data);
	char* d = Malloc(strlen(data));
	strcpy(d, data);

	cb->hostname = hn;
	cb->uri = u;
	cb->data = d;
	while(total_size + size >= MAX_CACHE_SIZE)
		remove_LRU();
	if(num == 0)
	{	
		cb->prev = cb;
		cb->next = cb;
	}
	else
	{
		cb->prev = end;
		cb->next = end->next;
		end->next = cb;
	}
	end = cb;
	num++;
	cb->size = size;
	cb->use_index = pc++;
	total_size += size;
}
static struct cache_block* find(char* hostname, char* uri)
{
	if(end == NULL) return NULL;
	struct cache_block* curr = end;
	if(strcmp(hostname, curr->hostname) == 0 && strcmp(uri, curr->uri)==0)
		return curr;
	curr = curr -> next;
	while(curr != end)
	{
		if(strcmp(hostname, curr->hostname) == 0 && strcmp(uri, curr->uri)==0)
			return curr;
		curr = curr->next;
	}
	return NULL;
}

int open_connection_to_server(char* server_name, int server_port)
{
	int server_socket_fd = open_clientfd_r(server_name, server_port);
	return server_socket_fd;
}

void send_response_to_client(char* servername, char* path, int client_socket_fd, int server_socket_fd)
{
	rio_t rp;
	char response[MAXLINE];
	char buffer[MAX_OBJECT_SIZE];
	int overflow = 0;
	size_t buf_size = 0;
	ssize_t nread;
	rio_readinitb(&rp, server_socket_fd);
	
	buffer[0] = 0;
	while (rio_readlineb(&rp, response, MAXLINE))
	{
		size_t resp_len = strlen(response);
		Rio_writen(client_socket_fd, response, resp_len);
		if(!overflow && resp_len + buf_size < MAX_OBJECT_SIZE)
		{
			buf_size += resp_len;
			strcat(buffer, response);
		}
		else
			overflow = 1;
		if (strcmp(response, "\r\n")==0)
			  break;
	}
	
	// send the body of the response
	while ((nread = rio_readnb(&rp, response, MAXLINE)) != 0)
	{
		size_t resp_len = strlen(response);
		if(!overflow && resp_len + buf_size < MAX_OBJECT_SIZE)
		{
			buf_size += resp_len;
			strcat(buffer, response);
		}
		else
			overflow = 1;
		Rio_writen(client_socket_fd, response, nread);
	}
	if(!overflow)
	{
		add_elem(servername, path, buffer);
	}
	/* terminate the response with an empty line */
	//Rio_writen(client_socket_fd, "\r\n", strlen("\r\n"));
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

int parse_get_request(int clientfd, char* request, char* request_prefix, char* server_name, int* server_port, char* server_path)
{
	const char* ptr = request + strlen(request_prefix);
	int offset = 0;
	char port[MAXLINE];

	int i = 0;
	int len = strlen(request);
	while (i < len && (*ptr != ':' && *ptr != '/' && *ptr != ' '))
	{
		server_name[offset] = *ptr;
		ptr++;
		offset++;
		i++;
	}
	if(offset == 0)
	{
		clienterror(clientfd,"Empty hostname","404","","");
		return -1;
	}
    server_name[offset] = 0;

    if (i == len)
    {
    	clienterror(clientfd,"Missing HTTP/1.x request.","404","","");
    	return -1;
    }
    printf("Server_name: %s\n", server_name);
    if(*ptr == ':')
    {
    	i++;
	    ptr++;
	    offset = 0;
	    while (i < len && (*ptr != '/' && *ptr != ' '))
	    {
	    	i++;
	    	if(*ptr < '0' || *ptr > '9')
	    	{
	    		clienterror(clientfd, "Non-numeric character in port.", "404", "", "");
	    		return -1;
	    	}
	  	    port[offset] = *ptr;
	  	    ptr++;
	  	    offset++;
	    }
	    if(offset == 0)
	    {
	    	clienterror(clientfd, "No port specified after :", "404", "", "");
	    	return -1;
	    }
	    if (i == len)
	    {
	    	clienterror(clientfd, "Missing HTTP/1.x", "404", "", "");
	    	return -1;
	    }
	    port[offset] = 0;
	    *server_port = atoi(port);
	}
	else
	{
		*server_port = DEFAULT_HTTP_PORT;
	}
	if(*ptr == '/')
	{
	    offset = 0;
	    server_path[offset] = *ptr;
	    while (i < len && *ptr != ' ')
	    {
	  	    server_path[offset] = *ptr;
	  	    ptr++;
	  	    offset++;
	    }
	    server_path[offset] = 0;
	}
	else
	{
		//No /
		offset = 0;
		server_path[offset] = '/';
		server_path[offset+1] = 0;
	}
    return 0;
}
int is_default(char* request)
{
	if(strlen(request) >= strlen(host_tag) && strncmp(request, host_tag, strlen(host_tag)) == 0)
		return 1;
	else if(strlen(request) >= strlen(user_agent_tag) && strncmp(request, user_agent_tag, strlen(user_agent_tag)) == 0)
		return 1;
	else if(strlen(request) >= strlen(accept_tag) && strncmp(request, accept_tag, strlen(accept_tag)) == 0)
		return 1;
	else if(strlen(request) >= strlen(accept_encoding_tag) && strncmp(request, accept_encoding_tag, strlen(accept_encoding_tag)) == 0)
		return 1;
	else if(strlen(request) >= strlen(connection_tag) && strncmp(request, connection_tag, strlen(connection_tag)) == 0)
		return 1;
	else if(strlen(request) >= strlen(proxy_connection_tag) && strncmp(request, proxy_connection_tag, strlen(proxy_connection_tag)) == 0)
		return 1;
	return 0;
}
void handle_client_connection(int client_socket_fd)
{
    char request[MAXLINE];
    char server_name[MAXLINE];
    char path[MAXLINE];
    char arg[MAXLINE];
    int  server_port;
    int server_socket_fd;
    char* request_prefix = "GET http://";
    const char* method = "GET ";
    rio_t rp;
    ssize_t n;
    /* read request from client */
    rio_readinitb(&rp, client_socket_fd);

    /* read the first line of the request */
    n = rio_readlineb(&rp, request, MAXLINE);

    if (n && (strncmp(request, request_prefix, strlen(request_prefix)) == 0))
    {
  	    printf("Extracting from request\n");
  	    printf("request %s\n", request);
  	    /* Extract the method, server name and server port from the first line */
  	    if(parse_get_request(client_socket_fd, request, request_prefix, server_name, &server_port, path) < 0)
  	    {
	  	    /* close connection to client*/
	  	    Close(client_socket_fd);
  	  	    return;
  	    }
	    
  	    /* Check if in cache */
		struct cache_block* cb = find(server_name, path);
		if(cb != NULL)
		{
			Rio_writen(client_socket_fd, cb->data, strlen(cb->data));
			/* close connection to client*/
			Close(client_socket_fd);
			return;
		}

	    printf("Opening server connection fd %d\n", client_socket_fd);
	    /* open a connection to end server */
	    server_socket_fd = open_connection_to_server(server_name, server_port);	  
	    if(server_socket_fd < 0)
	    {
	    	printf("Serv name: %s port %d path %s\n", server_name, server_port, path);
	    	clienterror(client_socket_fd, "Error opening connection to server.", "404", "", "");
	    	/* close connection to client*/
	    	Close(client_socket_fd);
	   		return; 	
	    }
	    /* forward request to server */
	    /* send the GET method */
	    Rio_writen(server_socket_fd, (void *)method, strlen(method));
	    /* send the url path */
	    Rio_writen(server_socket_fd, (void*)path, strlen(path));
	    Rio_writen(server_socket_fd, (void*)" HTTP/1.0\r\n", strlen(" HTTP/1.0\r\n"));
	    
	    /* Send default lines */
	    arg[0] = 0;
	    strcat(arg, host_tag);
	    strcat(arg, server_name);
	    strcat(arg, "\r\n");
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    strcpy(arg, user_agent_hdr);
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    strcpy(arg, accept_hdr);
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    strcpy(arg, accept_encoding_hdr);
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    strcpy(arg, connection_hdr);
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    strcpy(arg, proxy_connection_hdr);
	    Rio_writen(server_socket_fd, arg, strlen(arg));
	    /* send the remainder lines of the request */
	    while (rio_readlineb(&rp, request, MAXLINE))
	    {
	    	if(!is_default(request))
	    	{
			    Rio_writen(server_socket_fd, request, strlen(request));
			    if (strcmp(request, "\r\n")==0)
			 		break;	
		 	}
	    }
	    printf("Writing to client connection\n");
	    //Rio_writen(server_socket_fd, (void*)"\r\n", strlen("\r\n"));
	    
	    /* send server's response to client */
	    send_response_to_client(server_name, path, client_socket_fd, server_socket_fd);
	    /* close connection to server */
	    Close(server_socket_fd);
	    /* close connection to client*/
	    Close(client_socket_fd);
    }
    else
    {
    	clienterror(client_socket_fd, "Invalid command or malformed http://", "404", "", "");
	    Close(client_socket_fd);
    }
}
/* Thread routine */
void *thread(void *vargp)
{
	int connfd = *((int *)vargp);
	printf("Entered new thread with connfd %d\n", connfd);
	
	/* detach thread */
	Pthread_detach(pthread_self());
	Free(vargp);
	
	/* open up connection */
	handle_client_connection(connfd);
	printf("Before close. fd: %d\n", connfd);
	//Close(connfd);
	return NULL;
}
int main(int argc, char* argv[])
{
	int portnum;
	int listen_socket_fd;

	if (argc != 2)
	{
		printf("Invalid number of arguments. Usage:%s <port number>\n", argv[0]);
		exit(0);
	}

	portnum = atoi(argv[1]);

	if (portnum == 0)
	{
		printf("Invalid port number:%s\n", argv[1]);
		exit(0);
	}

	if ((portnum <= 1024) || (portnum >= 65536)) {
        printf("Port Number is out of range (1024 < port number < 65536)\n");
        exit(0);
    }

	/* open a listening socket on the specified port */
	listen_socket_fd = open_listenfd(portnum);

	if (listen_socket_fd == -1)
	{
        printf("Could not open a listening socket at portnum: %d.\n", portnum);
        exit(0);
	}

	/* init proxy cache */
	init_cache();
	/* accept connections from clients by listening to listening socket */
	while (1)
	{
		pthread_t tid;
		int *client_socket_fd;
		struct sockaddr client_socket_addr;
		socklen_t client_socket_addr_len = sizeof(client_socket_addr);
		
		client_socket_fd = Malloc(sizeof(int));
		*client_socket_fd = Accept(listen_socket_fd, &client_socket_addr, &client_socket_addr_len);
		if (client_socket_fd >= 0)
		{
			printf("Handling client connection.\n");
			Pthread_create(&tid, NULL, thread, client_socket_fd);
		    //handle_client_connection(*client_socket_fd);
		    //Free(client_socket_fd);
		}
	}
    return 0;
}