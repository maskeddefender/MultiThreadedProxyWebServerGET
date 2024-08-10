#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>


#define MAX_BYTES 4096    //max allowed size of request/response
#define MAX_CLIENTS 400     //max number of client requests served at a time
#define MAX_SIZE 200*(1<<20)     //size of the cache - defines a cache size of 200 megabytes.
#define MAX_ELEMENT_SIZE 10*(1<<20)     //max size of an element in cache

typedef struct cache_element cache_element; //

// mein jo cache bna rha hun usme mujhe elements ko store krne k liye ek structure chahiye, or wo kon se elements honge define krna hoga
struct cache_element {
    char* data; // data is the response from the server, pahle kabhi request se aaya tha
    int len; // len is the length of the data in bytes
    char* url; // url is the key, jise data ko retrieve krne k liye use krenge, taki jab dobara request aaye to cache se retrieve kr ske
    time_t lru_time_track; // timestamp is the time when the data was stored in the cache, isse isliye use kr rhe taaki least recently used element k waqt ka note rakha jaa ske taake jarurat padne pr usse hatakr naya element add kr ske kynki mera cache full hai
    cache_element* next;    //pointer to next element, kynki iska hme linked list bnani hai 
};

// here we are just declaring the functions, we will define them later
cache_element* find(char* url); // ye jo ll banayenge to usme element find krne k liye iska use krenge
int add_cache_element(char* data,int size,char* url); // data ko cache me add krne k liye, uska size and url chahiye ki aaya kahan se hai
void remove_cache_element(); // cache full hone k baad kisi element ko remove krne k liye

int port_number = 8088; // Default Port for our proxy server

// jab bhi hm proxy k saath connect krna chahenge to ek socket communication krni padegi, isliye uske liye ek socket descriptor chahiye for the communication over the network 
int proxy_socketId;	     // socket descriptor declaration of proxy server on global level

// jitne clients mere saath connect honge utne threads bnaung and har ek thread me ek socket bnaunga jo mere proxy server k saath connected hoga
pthread_t tid[MAX_CLIENTS];         //array to store the thread ids of clients

// ek semaphore chahiye jo mujhe batayega ki kitne clients mere saath connected hain, agar max_clients se jyada clients connected hain to mujhe unhe block krna padega
sem_t seamaphore;	                //if client requests exceeds the max_clients this seamaphore puts the waiting threads to sleep and wakes them when traffic on queue decreases

//sem_t cache_lock;			       
// cache ek shared location hai, to avoid any race condition we need to lock the cache while accessing it 
pthread_mutex_t lock;               //lock is used for locking the cache

cache_element* head;                //pointer to the cache
int cache_size;             //cache_size denotes the current size of the cache


// Handling the ERROR code - The sendErrorMessage function is a utility function used in a web server to generate and send standardized HTTP error responses to clients. It handles various common HTTP error codes, constructs appropriate response headers and bodies, and sends them over the specified socket. The function also logs error information to the server console for most of the status codes. If an invalid status code is provided, the function returns -1.
// 400 Bad Request
// 403 Forbidden
// 404 Not Found
// 500 Internal Server Error
// 501 Not Implemented
// 505 HTTP Version Not Supported
// If the provided status_code does not match any of these, the function returns -1, indicating an error.
int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: greyhatbeastN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}




// connectRemoteServer function is used to connect to the remote server, host and port k saath connect krna hai
int connectRemoteServer(char* host_addr, int port_num)
{
	// Creating Socket for remote server ---------------------------
    // isse se aapka communication hoga remote server k saath
    // socket() function is used to create an endpoint for communication and returns a socket descriptor representing the endpoint.
    // AF_INET is the Internet address family for IPv4 that is used to designate the type of addresses that your socket can communicate with
    // SOCK_STREAM is used to create a stream socket in which characters are read in a continuous stream as if from a file or pipe
    // 0 is the protocol argument, which is the protocol to be used with the socket. It is usually set to 0 to automatically select the appropriate protocol.

	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    // yadi socket create nhi ho paya, nhi khol paye to error message print kr denge
	if( remoteSocket < 0)
	{
		printf("Error in Creating Socket.\n");
		return -1;
	}
	
	// Get host by the name or ip address provided
    // gethostbyname() function is used to resolve the hostname. It returns a pointer to a hostent structure that contains the details of the host.
    // aapke local me jayega and aapke pas jo host ki mapping hoti hai usko search kr k uska ip address lekr aata hai
	struct hostent *host = gethostbyname(host_addr);	

	if(host == NULL) // yadi host nhi milta to error message print kr denge
	{
		fprintf(stderr, "No such host exists.\n");	
		return -1;
	}

    // mil jata hai to uska ip address and port number store krna padega
	// inserts ip address and port number of host in struct `server_addr`
	struct sockaddr_in server_addr;

    // clears out the server_addr structure by setting all of its bytes to zero, ensuring that there are no uninitialized values.
	bzero((char*)&server_addr, sizeof(server_addr));
    // family of the address - AF_INET is the Internet address family for IPv4 that is used to designate the type of addresses that your socket can communicate with
	server_addr.sin_family = AF_INET;
    // port number of the server - htons() function converts the unsigned short integer hostshort from host byte order to network byte order, mtlb ki jo numbers internet ko samjh aate hai unme convert krta hai
	server_addr.sin_port = htons(port_num);


    // Copies the IP address from the host structure to the server_addr structure.
    // Copies the IP address from the host structure to the server_addr structure. The h_addr_list is an array of pointers to network addresses, where each pointer points to a structure containing an address. The first address in the array is the primary address of the host. 
	bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

	// Connect to Remote server ----------------------------------------------------
    // jo bhi bnaya hai usko connect krna hai, remote server k saath connect krna hai
	if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in connecting !\n"); // nhi ho paya to
		return -1;
	}

    // yadi connect ho jata hai to remote socket ka descriptor return kr denge
	// free(host_addr);
	return remoteSocket;
}





// handle_request function is used to send the request to the server and get the response from the server
// clientSocket is the socket descriptor of the client, request is the parsed request, tempReq is the temporary buffer where the request is stored
int handle_request(int clientSocket, ParsedRequest *request, char *tempReq)
{
	char *buf = (char*)malloc(sizeof(char)*MAX_BYTES); // buffer to store the request
	
    // creating a complete HTTP request to send to the server
    strcpy(buf, "GET "); // copying the GET method to the buffer
	strcat(buf, request->path); // concatenating the path to the buffer
	strcat(buf, " "); // concatenating the space to the buffer
	strcat(buf, request->version); // concatenating the version to the buffer
	strcat(buf, "\r\n"); // concatenating the new line to the buffer - \r is carriage return and \n is new line

	size_t len = strlen(buf); // length of the buffer


    // adding the headers to the request - Connection: close, Host: request->host
	if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key not work\n");
	}

    // if the host header is not present in the request then add the host header to the request
	if(ParsedHeader_get(request, "Host") == NULL)
	{
        // if the host header is not present in the request then add the host header to the request - yadi fir bhi nhi milta to error message print kr denge
		if(ParsedHeader_set(request, "Host", request->host) < 0){
			printf("Set \"Host\" header key not working\n");
		}
	}

    // unparse the headers of the request
    // request jo aayi hai uska header unparse krna hai, jo server ko bhejna hai, buffer me store krna hai, MAX_BYTES tak hi store krna hai
    // header pass krna hai, buffer me store krna hai, MAX_BYTES tak hi store krna hai
	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		printf("unparse failed\n"); // nhi hua to error message print kr denge
		//return -1;				// If this happens Still try to send request without header
	}


    // end server (not proxy server) ka port number 80 hai, isliye uska port number 80 hai (default port number)
	int server_port = 80;				// Default Remote Server Port

	if(request->port != NULL)
		server_port = atoi(request->port); // agar port number aaya hai to usko integer me convert kr denge

    // connect to the remote server - connectRemoteServer function is used to connect to the remote server
    // request->host is the host name of the server, server_port is the port number of the server


    // connectRemoteServer function is used to connect to the remote server, host and port k saath connect krna hai
	int remoteSocketID = connectRemoteServer(request->host, server_port);



    // yadi connectRemoteServer function successfully connect nhi kr payi to return -1
	if(remoteSocketID < 0)
		return -1;

    // sending the request to the server - remote server k liye request bhejna hai - bytes send krni hai fir receive krni hai
    // remoteSocketID is the socket descriptor of the remote server, buf is the buffer where the request is stored, len is the length of the request, 0 is the flag which is set to 0 for normal operation
	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

	bzero(buf, MAX_BYTES); // clearing the buffer

    // bytes_send is the number of bytes sent to the server, recv() function is used to receive the response from the server, remoteSocketID is the socket descriptor of the remote server, buf is the buffer where the response is stored, MAX_BYTES-1 is the maximum number of bytes that can be stored in the buffer, 0 is the flag which is set to 0 for normal operation
	bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES); //temp buffer
    // to store response from the server
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0; //index of temp buffer

    // yadi bytes_send > 0 hai to response aayi hai server se - i.e server se response jab tk aa rha hai tab tk loop chalega and receive krta rhega
	while(bytes_send > 0)
	{
        // sending the response to the client - client ko response bhejna hai - bytes_send krna hai and receive krna hai - clientSocket is the socket descriptor of the client, buf is the buffer where the response is stored, bytes_send is the number of bytes sent to the server, 0 is the flag which is set to 0 for normal operation
		bytes_send = send(clientSocket, buf, bytes_send, 0);
		
        // bytes_send is the total size - need to divide it by sizeof(char) to get the number of characters, temp_buffer_index is the index of the temp buffer where the response is stored
		for(int i=0;i<bytes_send/sizeof(char);i++){
            // copying the response from the buffer to the temp buffer - apne server se jo response aaya hai usko temp buffer me store kr rhe hai - cache me store krne k liye 
			temp_buffer[temp_buffer_index] = buf[i];
			// printf("%c",buf[i]); // Response Printing
			temp_buffer_index++;
		}

        // size of the temp buffer is increased by MAX_BYTES - response store krne k liye
		temp_buffer_size += MAX_BYTES;
        // temp buffer is reallocated to the new size - heap me eise dynamic memory allocation hoti hai, realloc() function use kr k
		temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size); 

        // yadi kuch send nhi ho paya to break kr denge
		if(bytes_send < 0)
		{
			perror("Error in sending data to client socket.\n");
			break;
		}
        // yadi receive hua to receive krte rahenge
		bzero(buf, MAX_BYTES); // pr pahle usko khali krna hai garbage values se

        // (response from server) - bytes_send is the number of bytes sent to the server, recv() function is used to receive the response from the server, remoteSocketID is the socket descriptor of the remote server, buf is the buffer where the response is stored, MAX_BYTES-1 is the maximum number of bytes that can be stored in the buffer, 0 is the flag which is set to 0 for normal operation
		bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0); // multi buffer me ek temp me store kr rhe hai taaki usko baad me hm temp me daal skein cache me store krne k liye - eisa hm issi liye kr rhe hai kynki hme cache me nhi mila tha request ka response tabhi to server se laakr usko de rhe hai client ko bhi vejne and aage k liye store rakhne k liye

	} 
    // jo bhi aaya temp buffer se usme \0 daal denge - string k end me \0 hota hai - string k end ko mark krne k liye
	temp_buffer[temp_buffer_index]='\0';
    // now freeing the buffer
	free(buf);
    // adding the response to the cache - cache me response add krna hai
	add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
	printf("Done\n");
	free(temp_buffer);
	
	// closing the remote socket
 	close(remoteSocketID);
	return 0;
}

// 
int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}


// creating a thread which doesn't returns anything
void* thread_fn(void* socketNew) // socketNew is the socket descriptor of the connected client
{
	sem_wait(&seamaphore); // waiting for the semaphore to be free, max_clients tak hi clients accept hongi
    // iff semaphore is negative then it will wait for the semaphore to be positive to continue the execution

	int p;
    // sem_getvalue returns the current value of the semaphore
	sem_getvalue(&seamaphore,&p);
    // printing the value of the semaphore
	printf("seamaphore value:%d\n",p);

    // typecasting the socket descriptor to int - jo uper se universal socket descriptor aaya hai usko int me convert kr rhe
    int* t= (int*)(socketNew);
	int socket=*t;           // Socket is socket descriptor of the connected Client - typecasting the void pointer to int pointer and then dereferencing it to get the socket descriptor
    // jb socket khula , cliest jo bhi http request vejega uski byte send krega, usko store krne k liye ek buffer chahiye and length of the buffer bhi chahiye
	int bytes_send_client,len;	  // Bytes Transferred and Length of the buffer

	// iss bufer me meri request aayegi, jo client ne vejegi
	char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));	// allocates 1*200 megabytes of memory for the buffer and initializes it to zero. The memory is ready to be used as a character array, likely for caching purposes.	
	
	bzero(buffer, MAX_BYTES);								// Making buffer zero - removing the garbage values

    // Receiving the Request from the client at the socket created for the client, kon si request aayi hai usko buffer me store krna hai, MAX_BYTES tak hi store krna hai, 0 is the flag - 0 means that no special options are used for this recv call(Common flags include MSG_DONTWAIT, MSG_PEEK, etc.)
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server
	

    // yadi hme bytes milti hai - i.e hme request aa rhi hai HTTP ki
	while(bytes_send_client > 0)
	{

		len = strlen(buffer); // length of the buffer
        //loop until u find "\r\n\r\n" in the buffer - i.e jab tak puri request nhi aati tab tak loop chalega
        // all HTTP requests are terminated by "\r\n\r\n" - so we are checking for this in the buffer
        // jab tk ye nhi milta tb tk bytes accept krte rahenge
		if(strstr(buffer, "\r\n\r\n") == NULL)
		{	
            // receiving the request from the client till the end of the request 
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
		}
		else{
			break;
		}
	}

	// printf("--------------------------------------------\n");
	// printf("%s\n",buffer);
	// printf("----------------------%d----------------------\n",strlen(buffer));
	
    // request ko store krne k liye ek temporary buffer chahiye, jisme request store hogi
	char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    //tempReq, buffer both store the http request sent by client
    // copying the request from buffer to tempReq
	for (int i = 0; i < strlen(buffer); i++)
	{
		tempReq[i] = buffer[i]; //
	}
	
	//checking for the request in cache 
    // if request is found in cache, then we will send the response from the cache to the client and return
	struct cache_element* temp = find(tempReq);

    //if request found in cache - 
	if( temp != NULL){
        //request found in cache, so sending the response to client from proxy's cache
		int size=temp->len/sizeof(char);  // size of the response element in cache
		int pos=0; // position of the response element in cache
        // sending the response from the cache to the client

        // hme cache se hi response mil gya hai to hm wahin se return kr denge
		char response[MAX_BYTES];

        // sending the response from the cache to the client
		while(pos<size){ // jab tk pura response nhi bhej dete
			bzero(response,MAX_BYTES); // clearing the response buffer - jo bhi response send hua usko clear kr dena hai
            // copying the response from the cache to the response buffer
			for(int i=0;i<MAX_BYTES;i++){
                // temp me hi data higa na jo server ne bheja tha, usko response me copy kr rhe
				response[i]=temp->data[pos]; 
                // ek ek byte nikaal rhe hai response se and response buffer me store kr rhe hai
				pos++;
			}
            // sending the response to the client to the socket created for the client - MAX_BYTES tak hi bhejna hai - response buffer me store hai jo hmne copy bnayi thi socket ki
			send(socket,response,MAX_BYTES,0);
		}
        // printing the response from the cache
		printf("Data retrived from the Cache\n\n");
		printf("%s\n\n",response);
		// close(socketNew);
		// sem_post(&seamaphore);
		// return NULL;
	}
	
	// if request not found in cache - mtlb aapke pas value hai hi nhi cache me
    // to sabse pahle dekhenge ki client se request successfully aayi hai ya nhi
	else if(bytes_send_client > 0) // yadi client se request aayi hai
	{
		len = strlen(buffer); 
		//Parsing the request
        // ParsedRequest is a structure that stores the parsed request. It contains the method, host, path, version, headers, and body of the request.
		ParsedRequest* request = ParsedRequest_create(); // creating a request object and this gives a structure which stores the parsed request
		
        //ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in the request
        // Parsing the request received from the client, buffer contains the request by the client, len is the length of the request
		if (ParsedRequest_parse(request, buffer, len) < 0) 
		{
		   	printf("Parsing failed\n"); // nhi hua to parsing failed
		}
		else
        // if parsing is successful then we will check the method, host, path and version of the request received 
		{	
			bzero(buffer, MAX_BYTES); // clearing the buffer
            // hm apne web server me abhi sirf GET request support kr rhe hai, isliye check krna padega ki GET request aayi hai ya nhi
            // POST, PUT, DELETE, HEAD, OPTIONS, CONNECT, TRACE, PATCH, COPY, LINK, UNLINK, PURGE, LOCK, UNLOCK, PROPFIND, VIEW, etc. are some of the other HTTP methods that can be used in a request.
			if(!strcmp(request->method,"GET"))		// if both are equal the strcmp() function returns 0					
			{
                // princeton university ki website hai jo ye HTTP version support krti hai
                // ye library bhi sirf HTTP/1.0 and HTTP/1.1 support krti hai - jo protocol hai uska version 1 ye support krti hai
				if( request->host && request->path && (checkHTTPversion(request->version) == 1) )
				{
                    // socket is the socket descriptor of the connected client, jahan pr hme request vejna hai, request is the parsed request, tempReq is the temporary buffer where the request is stored
					bytes_send_client = handle_request(socket, request, tempReq);		// Handle GET request
                    // yadi handle_request krne k baad bhi server se response nhi aata to error message send krna padega
					if(bytes_send_client == -1)
					{	
						sendErrorMessage(socket, 500);
					}

				}
				else
					sendErrorMessage(socket, 500);			// 500 Internal Error

			}
            else // yadi GET request nhi aayi hai to error message send krna padega kynki abhi hm sirf GET request support kr rhe hai
            {
                printf("This code doesn't support any method other than GET\n");
            }
    
		}
        //freeing up the request pointer - freeing the memory allocated to the request
		ParsedRequest_destroy(request);

	}

    // yadi client se request receive nhi ho payi to error message send krna padega
	else if( bytes_send_client < 0)
	{
		perror("Error in receiving from client.\n");
	}
    // yadi client nhi aayi hai to iska matlab hai client disconnected hai and  message print krna padega
	else if(bytes_send_client == 0)
	{
		printf("Client disconnected!\n");
	}

	shutdown(socket, SHUT_RDWR); // SHUT_RDWR is used to disable further send and receive operations on the socket
	close(socket); // closing the socket
	free(buffer); // freeing the buffer
	sem_post(&seamaphore);	// releasing the semaphore
    
	
	sem_getvalue(&seamaphore,&p); // getting the value of the semaphore
	printf("Semaphore post value:%d\n",p); // printing the value of the semaphore
	free(tempReq); // freeing the temporary buffer
	return NULL; // returning NULL
}



int main(int argc, char * argv[]) {

    

	int client_socketId, client_len; // client_socketId == to store the client socket id, client_len == to store the length of the client address
    // The struct sockaddr_in is a structure in C that is used to specify an address for the socket in the Internet domain, which is used for IPv4 addresses. It's commonly used in socket programming to hold information about the IP address and port number of a socket.
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    sem_init(&seamaphore,0,MAX_CLIENTS); // Initializing seamaphore and lock
    pthread_mutex_init(&lock,NULL); // Initializing lock for cache


    // kis port pr proxy server run krna hai, ye command line arguments se decide hoga
    if(argc == 2)        //checking whether two arguments are received or not
	{
		port_number = atoi(argv[1]);   // converting the port number to integer
	}
	else
	{
		printf("Too few arguments\n");
		exit(1);  // system call to terminate the whole program process
	}

    // band nhi hua to proxy server ko run krna hai
    printf("Setting Proxy Server Port : %d\n",port_number);

    // hmne proxy web server ka socket id bnaya tha uska bhi socket open krna padega na
    // proxy ka ek hi socket rhega jispr har ek client request krega whi hai proxy_socketId
    // jab ek socket se ek request accept ho jayegi tab hm ek naya thread spon krenge jo nye sockets bnayega dusre clients k lye and ab jab bhi uska respone aayega tab wo whi newly created socket se return hoga
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); 
    // AF_INET is the Internet address family for IPv4 that is used to designate the type of addresses that your socket can communicate with
    // SOCK_STREAM ka matlab hai ki jo bhi hoga TCP pr hoga

    // yadi socket create na kr paye to error message print kr denge
    if( proxy_socketId < 0)
	{
		perror("Failed to create socket.\n");
		exit(1);
	}

    // agar rsocket bn jata hai to us socket ko reuse krna chahiye, isliye uske liye socket me options set krna padega
    int reuse =1;
    // int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);
    // If setsockopt succeeds, it returns 0. If it fails, it returns -1, and errno is set to indicate the error.
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEADDR) failed\n");    
    // proxy_socketId is the socket descriptor created earlier with the socket() function.
    // SOL_SOCKET is used to indicate that the option is at the socket level, rather than at the protocol level
    // SO_REUSEADDR allows a socket to forcibly bind to a port in use by a socket in the TIME_WAIT state, i.e ek google.com aaya to dusre k liye ye nhi bolna hai ki socket is already in use, same socket use krna hai
    // (const char*)&reuse - When reuse is 1, it enables the SO_REUSEADDR option, allowing the socket to reuse the address
    // sizeof(reuse) specifies the size of the option_value. Since reuse is typically an int, sizeof(reuse) would be the size of an integer.

    // clears out the server_addr structure by setting all of its bytes to zero, ensuring that there are no uninitialized values.
    bzero((char*)&server_addr, sizeof(server_addr)); 

    server_addr.sin_family = AF_INET; // Assigning the address family to IPv4.
	server_addr.sin_port = htons(port_number); // Assigning port to the Proxy - htons() function converts the unsigned short integer hostshort from host byte order to network byte order, mtlb ki jo numbers internet ko samjh aate hai unme convert krta hai
	server_addr.sin_addr.s_addr = INADDR_ANY; // iss socket pr jo aap jis server se communicate krne wale ho at the end uspr koi bhi address define kr do 

    // Binding the socket
    // The bind() function is used in socket programming to associate a socket with a specific IP address and port number on the local machine. It essentially "binds" the socket to a specific address, so the operating system knows where to direct incoming data intended for that port.
	if( bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 ) // (struct sockaddr*)&server_addr typecasting for bind 
    // Binds the socket (represented by proxy_socketId) to the IP address and port number stored in server_addr. The third argument is the size of the server_addr structure. 
	{
		perror("Port is not free\n");
		exit(1);
	}
    // If bind() is successful, a message indicating the bound port number is printed.  
    // This part of the code is crucial in setting up a server to listen for incoming connections on a specific IP address and port number.
	printf("Binding on port: %d\n",port_number);


    // Proxy socket listening to the requests - proxy server is now ready to listen to the incoming requests
	int listen_status = listen(proxy_socketId, MAX_CLIENTS);

	if(listen_status < 0 )
	{
		perror("Error while Listening !\n");
		exit(1);
	}

    int i = 0; // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
	int Connected_socketId[MAX_CLIENTS];   // This array stores socket descriptors of connected clients - kitne sockets connect ho chuke hai server k saath

    // Infinite Loop for accepting connections
	while(1)
	{
		// suru me hamesha jo client ka address hai usko clear kr dena chahiye
		bzero((char*)&client_addr, sizeof(client_addr));			// Clears struct client_addr
		client_len = sizeof(client_addr);                           // intitalizing the length of client address

        // Accepting the connections - opening the socket for the client to proxy's socket 
        // aap jab bhi type cast krte ho jo function return kr rha hai usko usi type ka pointer bnana padega and yahan pr hme client ka address and length chahiye isliye uska typecast kiya hai - accept wale function k type ko dhyan me rakhte hue
		client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);	// Accepts connection
		if(client_socketId < 0) // socket nhi khul paya to
		{
			fprintf(stderr, "Error in Accepting connection !\n");
			exit(1);
		}
		else{ // socket khul gaya to - i.e iff client get connected successfully to the proxy server
			Connected_socketId[i] = client_socketId; // Storing accepted client into array
		}

		// Getting IP address and port number of client
		struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr; // ek copy pointer bna liya client ka address ka
		struct in_addr ip_addr = client_pt->sin_addr;                    // client ka ip address nikal liya jisse socket se connect kiya hai
		char str[INET_ADDRSTRLEN];										// INET_ADDRSTRLEN: Default ip address size - INET_ADDRSTRLEN is used to ensure that the array str is large enough to hold the string representation of an IPv4 address, including the null terminator. For IPv4 addresses, this size is typically 16 bytes

        // The inet_ntop function is used in C to convert an IP address from its binary form into a human-readable string format. Here’s a detailed explanation of the function call:
		inet_ntop( AF_INET, &ip_addr, str, INET_ADDRSTRLEN ); // Converts a binary IP address to a string format. The first argument specifies the address family, the second argument is the binary address, the third argument is the buffer to store the string, and the fourth argument is the size of the buffer. INET_ADDRSTRLEN ensures that the buffer is large enough for an IPv4 address.


		printf("Client is connected with port number: %d and ip address: %s \n",ntohs(client_addr.sin_port), str); // ntohs(client_addr.sin_port): Converts the port number from network byte order to host byte order so it can be correctly interpreted and displayed.


		//printf("Socket values of index %d in main function is %d\n",i, client_socketId);
		pthread_create(&tid[i],NULL,thread_fn, (void*)&Connected_socketId[i]); // Creating a thread for each client accepted
        // &tid[i]: Pointer to the pthread_t variable to store the thread ID.
        // NULL: Default thread attributes.
        // thread_fn: Function to be executed by the new thread, which must match the signature void *thread_fn(void *arg).
        // (void*)&Connected_socketId[i]: Pointer to the argument passed to the thread function, casting the socket ID to void *. 
		i++; 
	}
	close(proxy_socketId);									// Close socket to empty the memory of pointer and free the port
    // sem_destroy(&seamaphore);								// Destroy the semaphore
    // pthread_mutex_destroy(&lock);							// Destroy the lock
 	return 0;
}

// pahli baar bhi aaya ho ya pahle bhi aaya ho, usko fir bhi find krna to padega hi
cache_element* find(char* url){

// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site=NULL;
	//sem_wait(&cache_lock);

    // ye lock acquire krne k liye hai, kynki cache me search krna hai, cache me search krne k liye lock acquire krna padega
    int temp_lock_val = pthread_mutex_lock(&lock);
    // jab bhi chizen remove ya add krte hai cache me to lock acquire krte hai
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 

    // yadi cache khali nhi hai to search krna padega = isko hm ek linked list jaisa hi treat krte hai 
    if(head!=NULL){
        site = head;
        // jab tk site null nhi ho jata tab tk search krte rahenge
        while (site!=NULL)
        {
            // compare krte jao ki jo url aaya hai wo cache me hai ya nhi
            // site -> simple web server ka url hai, jo cache me store hua hai
            if(!strcmp(site->url,url)){
                // yadi url mil jata hai to uska time track update krna padega
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                
                printf("\nurl found\n");
				// Updating the time_track
                // time() function is used to get the current time of the system - latest time set?update krte rhna padega
				site->lru_time_track = time(NULL);
                // agli baar k liye jab cache full ho jayegi to sabse pahle ye element remove hoga kynki ye sabse purana element hai
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            // yadi url yahan pr nhi milta to next element me jao
            site=site->next;
        }       
    }
    // yadi head hi null hai url milna hi nhi
	else {
    printf("\nurl not found\n");
	}
	//sem_post(&cache_lock);
    // yadi url mil jata hai to lock ko release kr denge
    temp_lock_val = pthread_mutex_unlock(&lock);
    // lock relase krne k baad print kr denge
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}


// jab cache full ho jati hai to sabse pahle element remove krna padega - minimum itna jitna ki agla element add ho sake
// linked list me se sabse purana element remove krna padega
void remove_cache_element(){
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element * p ;  	// Cache_element Pointer (Prev. Pointer)
	cache_element * q ;		// Cache_element Pointer (Next Pointer)
	cache_element * temp;	// Cache element to remove
    //sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    // yadi cache khali nhi hai to search krna padega
	if( head != NULL) { // Cache != empty
    // initialize the pointers
		for (q = head, p = head, temp =head ; q -> next != NULL; 
            // Iterate through entire cache and search for oldest time track
			q = q -> next) { // Iterate through entire cache and search for oldest time track
            // yadi agla element ka time track pahle wale element se chota hai to usko update krna padega
            // time k hisaab se sabse purana element remove krna padega
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
                // update the time track of the element
				temp = q -> next;
                // update the pointers
				p = q;
			}
		}
        // yadi cache me ek hi element hai to usko remove kr denge
		if(temp == head) { 

			head = head -> next; /*Handle the base case*/

		} else { 

			p->next = temp->next;	

		}

        // cache size me element size subtract kr denge - jo abhi remove kiya hai uska size subtract kr denge 
		cache_size = cache_size - (temp -> len) - sizeof(cache_element) - strlen(temp -> url) - 1;     //updating the cache size
        // free the memory allocated to the removed element
		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
	//sem_post(&cache_lock);
    // lock release kr denge - remove ho gya hai element
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}


// yadi pahli baar kuch add krna hoga request k response ko cache me store krne k liye to add element krna hoga
// service to abhi available nhi hai, isliye cache me store krna hoga
// kya data lekr aayi, uska size kya hai, uska url kya hai jispr request gya tha
int add_cache_element(char* data,int size,char* url){
    // Adds element to the cache
	// sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);

    // jo element add krna hai uska size kitna hoga - data ka size, url ka size, cache_element ka size 
    int element_size=size+1+strlen(url)+sizeof(cache_element); // Size of the new element which will be added to the cache
    if(element_size>MAX_ELEMENT_SIZE){
		//sem_post(&cache_lock);
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        // yadi max element size se jyada hai to cache me add nhi krna hai - lock release kr denge ki bhai cache me add nhi kr skte, possible hi nhi hai - everything is unlocked and free - do whatever you want
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		// free(data);
		// printf("--\n");
		// free(url);
        return 0;
    }
    else // yadi element size cache me add krne k liye possible hai to
    {   
        // yadi cache size + element size > MAX_SIZE hai to cache se elements remove krna padega
        while(cache_size+element_size>MAX_SIZE){
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        // yadi cache size + element size < MAX_SIZE hai to cache me element add kr denge
        // typecasting the cache element to cache_element* - cache_element is a structure that stores the data, url, length of the data, and the time track of the data
        cache_element* element = (cache_element*) malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data= (char*)malloc(size+1); // Allocating memory for the response to be stored in the cache element
        // copying the data to the element - data is the response from the server
		strcpy(element->data,data); 
        // copying the url to the element - url is the url of the request
        element -> url = (char*)malloc(1+( strlen( url )*sizeof(char)  )); // Allocating memory for the request to be stored in the cache element (as a key)
        // copying the url to the element - url is the url of the request
		strcpy( element -> url, url );
        // updating the time track of the element - time() function is used to get the current time of the system
		element->lru_time_track=time(NULL);    // Updating the time_track
        // aaapne jo element add kiya hai uska next element head hoga - head is the first element of the cache - aage badhne k liye
        element->next=head; 
        // cache me jo element add kiya hai uska length kitna hoga - data ka length
        element->len=size;
        // jo bhi element add kiya hai usko head bna denge
        head=element;
        // cache size me element size add kr denge
        cache_size+=element_size;

        // lock release kr denge - cache me element add ho gya hai
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		//sem_post(&cache_lock);
		// free(data);
		// printf("--\n");
		// free(url);
        return 1;
    }
    return 0;
}


                                    