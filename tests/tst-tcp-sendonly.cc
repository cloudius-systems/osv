//
// This test attempts to connect to the host (192.168.122.1) on port 9999
// and send a small data packet, this test will fail if this port will be
// not accessible, so before running this test make sure your firewall is
// disabled and netcat is listening.
//
// $ sudo service firewalld stop
// $ nc -l -p 9999
//
#include <debug.hh>
#include <string>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define dbg(...) tprintf_e("tst-tcp-sendonly", __VA_ARGS__)

#define CONNECT_PORT 9999

class test_tcp_sendonly {
public:
    bool run(size_t message_sz, int nchunks) {
        sleep(1);
        struct sockaddr_in raddr;

        // prepare message
        char* message = (char*)malloc(message_sz);
        for (size_t c=0; c < message_sz; c++) {
            message[c] = 'A';
        }

        memset(&raddr, 0, sizeof(raddr));
        raddr.sin_family = AF_INET;
        inet_aton("192.168.122.1", &raddr.sin_addr);
        raddr.sin_port = htons(CONNECT_PORT);

        dbg("Creating socket...");
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            dbg("socket() failed!");
            return false;
        }

        dbg("Connecting to 192.168.122.1:%d...", CONNECT_PORT);
        if ( connect(s, (struct sockaddr *)&raddr, sizeof(raddr)) < 0 ) {
            dbg("connect() failed, to run this test please start netcat");
            dbg("on the host, like this 'nc -l -p %d'", CONNECT_PORT);
            return false;
        }

        dbg("Writing message");
        for (int chunk=0; chunk < nchunks; chunk++) {
            int bytes = write(s, message, message_sz);
            if (bytes < 0) {
                dbg("write() failed!");
                return false;
            }
        }

        close(s);
        return true;
    }
private:

};

int main(int argc, char **argv)
{
    test_tcp_sendonly tso;
    return tso.run(1024, 10) ? 0 : 1;
}
