#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    connect(s, (struct sockaddr*)&addr, sizeof(addr));
    
    send(s, "Hi", 2, 0);
    
    char buf[100];
    recv(s, buf, 100, 0);
    printf("%s\n", buf);
    
    close(s);
    return 0;
}