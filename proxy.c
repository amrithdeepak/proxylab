/*
 * proxy - A low-level proxy servicing between the client
 * and the server. This proxy takes a port input and 
 * implements a cache to store recently called urls.
 *
 * Sunny Nahar
 * anahar
 *
 * Amrith Deepak
 * amrithd
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "csapp.h"
#include "cache.h"

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

/* footer strings */
static const char* http_ftr = " HTTP/1.0\r\n";

/* Opens a socket to the server */
int open_connection_to_server(char* server_name, int server_port)
{
	int server_socket_fd = open_clientfd_r(server_name, server_port);
	return server_socket_fd;
}

/* 
 * send_response_to_client:
 * Sends the server response back to the client 
 */
void send_response_to_client(char* servername, char* path, 
	           int client_socket_fd, int server_socket_fd)
{
	/* Setup vars */
	rio_t rp;
	char response[MAXLINE];
	char buffer[MAX_OBJECT_SIZE];
	int overflow = 0;
	size_t buf_size = 1; //Null terminator
	ssize_t nread;
	rio_readinitb(&rp, server_socket_fd);
	
	buffer[0] = 0;
	/* Read header of response */
	while (rio_readlineb(&rp, response, MAXLINE) > 0)
	{
		size_t resp_len = strlen(response);
		Rio_writen(client_socket_fd, response, resp_len);

		/* Add content to buffer */
		if(!overflow && resp_len + buf_size < MAX_OBJECT_SIZE)
		{
			buf_size += resp_len;
			strcat(buffer, response);
		}
		else /* Check if buffer exceeds max allowed buffer size */
			overflow = 1;
		if (strcmp(response, "\r\n")==0)
			  break;
	}
	
	/* Send the body of the response */
	while ((nread = rio_readnb(&rp, response, MAXLINE)) > 0)
	{
		size_t resp_len = strlen(response);
		/* Add to buffer */
		if(!overflow && resp_len + buf_size < MAX_OBJECT_SIZE)
		{
			buf_size += resp_len;
			strcat(buffer, response);
		}
		else
			overflow = 1;

		/* Write to client */
		Rio_writen(client_socket_fd, response, nread);
	}

	if(!overflow)
	{
		/* If the buffer fits with max buffer size, 
		   we add it to the cache */
		add_elem(servername, path, buffer);
	}
}

/* 
 * client_error: generates a HTML error response to send to client */
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

/* Parses the GET request */
int parse_get_request(int clientfd, char* request, char* request_prefix, 
	 char* server_name, int* server_port, char* server_path)
{
	const char* ptr = request + strlen(request_prefix);
	int offset = 0;
	char port[MAXLINE];

	int i = 0;
	int len = strlen(request);

	/* Get server name */
	while (i < len && (*ptr != ':' && *ptr != '/' && *ptr != ' '))
	{
		server_name[offset] = *ptr;
		ptr++;
		offset++;
		i++;
	}

	/* Server name is empty */
	if(offset == 0)
	{
		clienterror(clientfd,"Parser Error:","404","Server name is empty."
			        ,"");
		return -1;
	}
    server_name[offset] = 0;

    /* Missing http tag */
    if (i == len)
    {
    	clienterror(clientfd,"Parser Error","404",
    		    "Missing HTTP/1.x request.","");
    	return -1;
    }
    
    /* Get port number */
    if(*ptr == ':')
    {
    	i++;
	    ptr++;
	    offset = 0;
	    while (i < len && (*ptr != '/' && *ptr != ' '))
	    {
	    	i++;
	    	/* Make sure characters are numbers */
	    	if(*ptr < '0' || *ptr > '9')
	    	{
	    		clienterror(clientfd, "Parser Error", "404", 
	    			"Non-numeric character in port.", "");
	    		return -1;
	    	}
	  	    port[offset] = *ptr;
	  	    ptr++;
	  	    offset++;
	    }
	    /* Port is empty */
	    if(offset == 0)
	    {
	    	clienterror(clientfd, "Parser Error", "404", 
	    		     "No port specified after :", "");
	    	return -1;
	    }

	    /* Missing HTTP tag */
	    if (i == len)
	    {
	    	clienterror(clientfd, "Parser Error", "404", 
	    		              "Missing HTTP/1.x", "");
	    	return -1;
	    }
	    port[offset] = 0;
	    *server_port = atoi(port);
	}
	else
	{
		/* If no port specified, set to default port */
		*server_port = DEFAULT_HTTP_PORT;
	}

	/* Get the URI */
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
		/* No URI specified - send / */
		offset = 0;
		server_path[offset] = '/';
		server_path[offset+1] = 0;
	}
    return 0;
}

/* Check if request header is one of the predefined headers */
int is_default(char* request)
{
	/* Special check for host */
	if(strlen(request) >= strlen(host_tag) && 
		strncmp(request, host_tag, strlen(host_tag)) == 0)
		return 2;
	/* User agent header */
	else if(strlen(request) >= strlen(user_agent_tag) && 
		strncmp(request, user_agent_tag, strlen(user_agent_tag)) == 0)
		return 1;
	/* Accept tag header */
	else if(strlen(request) >= strlen(accept_tag) && 
		strncmp(request, accept_tag, strlen(accept_tag)) == 0)
		return 1;
	/* Accept encoding header */
	else if(strlen(request) >= strlen(accept_encoding_tag) && 
		strncmp(request, accept_encoding_tag, 
		strlen(accept_encoding_tag)) == 0)
		return 1;
	/* Connection header */
	else if(strlen(request) >= strlen(connection_tag) && 
		strncmp(request, connection_tag, strlen(connection_tag)) == 0)
		return 1;
	/* Proxy_connection header */
	else if(strlen(request) >= strlen(proxy_connection_tag) && 
		strncmp(request, proxy_connection_tag, 
		strlen(proxy_connection_tag)) == 0)
		return 1;
	return 0;
}

/* Handle the client connection through client socket */
void handle_client_connection(int client_socket_fd)
{
	/* Create local vars */
    char request[MAXLINE]; /* read buffer */
    char server_name[MAXLINE]; /* server name */
    char path[MAXLINE]; /* uri */
    char arg[MAXLINE]; /* temp buffer */
    
    int server_port;
    int server_socket_fd;
    
    char* request_prefix = "GET http://";
    char* request_prefix_s = "GET https://";
    const char* method = "GET ";
    rio_t rp;
    ssize_t n;

    /* read request from client */
    rio_readinitb(&rp, client_socket_fd);

    /* read the first line of the request */
    n = rio_readlineb(&rp, request, MAXLINE);

    /* If the http prefix is present */
    if (n > 0 && ((strncmp(request, request_prefix, 
    	strlen(request_prefix)) == 0) || (strncmp(request, request_prefix_s, 
    	strlen(request_prefix_s)) == 0)))
    {
  	    /* Extract the method, server name and server port from the 
  	       first line */

    	/* This handles http and https requests */
    	if(strncmp(request, request_prefix, strlen(request_prefix))==0)
    	{
	  	    if(parse_get_request(client_socket_fd, request, request_prefix, 
	  	    	server_name, &server_port, path) < 0)
	  	    {
	  	    	/* Some parse error has occurred 
	  	    	   This is sent as HTML back to client */
		  	    /* close connection to client*/
		  	    Close(client_socket_fd);
	  	  	    return;
	  	    }
  		}
  		else
  		{   /* HTTPS */
  			if(parse_get_request(client_socket_fd, request, request_prefix_s, 
	  	    	server_name, &server_port, path) < 0)
	  	    {
	  	    	/* Some parse error has occurred 
	  	    	   This is sent as HTML back to client */
		  	    /* close connection to client*/
		  	    Close(client_socket_fd);
	  	  	    return;
	  	    }
  		}
	    /* Check if request is in cache */
		struct cache_block* cb = find(server_name, path);
		if(cb != NULL)
		{
			/* Update the LRU index */
			update(cb);

			/* Write to client */
			Rio_writen(client_socket_fd, cb->data, strlen(cb->data));

			/* close connection to client*/
			Close(client_socket_fd);
			return;
		}

	    /* open a connection to end server */
	    server_socket_fd = open_connection_to_server(server_name, 
	    	                                         server_port);
	    if(server_socket_fd < 0)
	    {
	    	/* Socket error */
	    	clienterror(client_socket_fd, "Server Connection Error", 
	    		"404", "Error opening connection to server.", "");
	    	
	    	/* close connection to client*/
	    	Close(client_socket_fd);
	   		return; 	
	    }
	    
	    /* forward request to server */
	    
	    /* send the GET method */
	    Rio_writen(server_socket_fd, (void *)method, strlen(method));
	    /* send the url path */
	    Rio_writen(server_socket_fd, (void*)path, strlen(path));
	    Rio_writen(server_socket_fd, (void*)http_ftr, strlen(http_ftr));
	    
	    int hostseen = 0;
	    
	    /* Send header lines */
	    arg[0] = 0;
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
	    while (rio_readlineb(&rp, request, MAXLINE) > 0)
	    {
	    	/* If it isn't a default request we have already sent */
	    	if(is_default(request) != 1)
	    	{
	    		/* Special for host - we always return a host request
	    		   but if it is already there, we return that */
	    		if(is_default(request) == 2)
			 		hostseen = 1;

			 	if (strcmp(request, "\r\n")==0)
			 		break;

			 	/* Write to server */
			    Rio_writen(server_socket_fd, request, strlen(request));
		 	}
	    }

	    /* If host tag is not specified, write it */
	    if(!hostseen)
	    {
	    	strcat(arg, host_tag);
	    	strcat(arg, server_name);
	    	strcat(arg, "\r\n");
	    	Rio_writen(server_socket_fd, arg, strlen(arg));
	    }

	    /* Write newline */
	    Rio_writen(server_socket_fd, "\r\n", strlen("\r\n"));
	    
	    /* send server's response to client */
	    send_response_to_client(server_name, path, client_socket_fd, 
	    	server_socket_fd);

	    /* close connection to server */
	    Close(server_socket_fd);
	    /* close connection to client*/
	    Close(client_socket_fd);
    }
    else
    {
    	/* no HTTP prefix */
    	clienterror(client_socket_fd, "Parser Error" , 
    		"404", "Invalid command or malformed http://", "");

    	/* Close client connection */
	    Close(client_socket_fd);
    }
}

/* Thread routine */
void *thread(void *vargp)
{
	/* Get file descriptor */
	int connfd = *((int *)vargp);
	
	/* detach thread */
	Pthread_detach(pthread_self());
	Free(vargp);
	
	/* open up connection */
	handle_client_connection(connfd);
	return NULL;
}

/* Main function: parses input and starts proxy */
int main(int argc, char* argv[])
{
	int portnum;
	int listen_socket_fd;

	if (argc != 2)
	{
		printf("Invalid number of arguments. Usage:%s <port number>\n", 
			   argv[0]);
		exit(0);
	}

	/* Get portnum */
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
        printf("Could not open a listening socket at portnum: %d.\n", 
        	   portnum);
        exit(0);
	}

	/* init proxy cache */
	init_cache();

	/* Install SIGPIPE handler */
	Signal(SIGPIPE, SIG_IGN);  

	/* accept connections from clients by listening to listening socket */
	while (1)
	{
		pthread_t tid;
		int *client_socket_fd;
		struct sockaddr client_socket_addr;
		socklen_t client_socket_addr_len = sizeof(client_socket_addr);
		
		/* Get client socket file descriptor */
		client_socket_fd = Malloc(sizeof(int));
		*client_socket_fd = Accept(listen_socket_fd, &client_socket_addr, 
			                       &client_socket_addr_len);
		if (*client_socket_fd >= 0)
		{
			/* Create new thread for client */
			Pthread_create(&tid, NULL, thread, client_socket_fd);
		}
	}

	/* Free cache */
	while(get_total_size() > 0)
		remove_LRU();

	/* Destroy lock */
	if (pthread_rwlock_destroy(get_cache_lock()))
	{
	    printf("Failed to destroy rw lock.\n");
	    exit(0);
	}

    return 0;
}
