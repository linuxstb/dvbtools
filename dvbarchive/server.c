/*
 * Connects to port 1234 on the local host.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>

#define NSTRS     3     /* no. of strings */

/*
 * Strings we send to the client.
 */
char *strs[NSTRS] = {
    "This is the first string from the server.\n",
    "This is the second string from the server.\n",
    "This is the third string from the server.\n"
};

extern int errno;

main()
{
  unsigned short int port=1234;
    char c;
    FILE *fp;
    int fromlen;
    char hostname[64];
    struct hostent *hp;
    struct sockaddr_in sin, fsin;
    register int i, s, ns;

    /*
     * Before we can do anything, we need
     * to know our hostname.
     */
    gethostname(hostname, sizeof(hostname));

    /*
     * Now we look up our host to get
     * its network number.
     */
    if ((hp = gethostbyname(hostname)) == NULL) {
        fprintf(stderr, "%s: host unknown.\n", hostname);
        exit(1);
    }

    /*
     * Get a socket to work with.  This socket will
     * be in the Internet domain, and will be a
     * stream socket.
     */
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("server: socket");
        exit(1);
    }

    /*
     * Create the address that we will be binding to.
     * We use port 1234 but put it into network
     * byte order.  Also, we use bcopy (see 
     * Chapter 14) to copy the network number.
     */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);

    /*
     * Try to bind the address to the socket.
     */
    if (bind(s, &sin, sizeof(sin)) < 0) {
        perror("server: bind");
        exit(1);
    }

    /*
     * Listen on the socket.
     */
    while (1) {
    if (listen(s, 5) < 0) {
        perror("server: listen");
        exit(1);
    }

    /*
     * Accept connections.  When we accept one, ns
     * will be connected to the client.  fsin will
     * contain the address of the client.
     */
    if ((ns = accept(s, &fsin, &fromlen)) < 0) {
        perror("server: accept");
        exit(1);
    }

    /*
     * We'll use stdio for reading the socket.
     */
    fp = fdopen(ns, "r");

    /*
     * First we send some strings to the client.
     */
    //    for (i = 0; i < NSTRS; i++)
    //        send(ns, strs[i], strlen(strs[i]), 0);

    /*
     * Then we read some strings from the client
     * and print them out.
     */
    while ((c = fgetc(fp)) != EOF) {
        if (c == '^')
            break;

        putchar(c);
        fflush(NULL);
    }

    /*
     * We can simply use close() to terminate the
     * connection, since we're done with both sides.
     */
}
    close(s);

    exit(0);
}

