#include <sys/socket.h>
#include <sys/epoll.h>
#include <cstring>
#include <zconf.h>
#include <fcntl.h>
#include <iostream>
#include <deque>
#include <functional>
#include <netinet/in.h>
#include <termios.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <wait.h>

using namespace std;

struct Listener {
    int sockfd;
    sockaddr_in addr;

    Listener(uint16_t port) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);

        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

        bind(sockfd, (const sockaddr *) &addr, sizeof(addr));
        listen(sockfd, 42);
    }

    int acceptClient() {
        socklen_t t = sizeof(addr);
        int client = accept(sockfd, (sockaddr *) &addr, &t);
        return client;
    }

    ~Listener() {
        close(sockfd);
    }
};

int getmaster() {
    cout << "get" << endl;
    int master = posix_openpt(O_RDWR);
    grantpt(master);
    unlockpt(master);

    return master;
};


struct DequeBuffer {
    deque<string> deq;
    size_t pos = 0;

    void push(string &str) {
        deq.push_back(str);
    }

    const char *from() {
        return deq.front().data() + pos;
    }

    size_t getlen() {
        return deq.front().size() - pos;
    }

    void setpos(size_t res) {
        pos += res;
    }

    void refresh() {
        if (pos == deq.front().size()) {
            deq.pop_front();
            pos = 0;
        }
    }

    bool isEmpty() {
        return deq.empty();
    }
};

struct Context {
    function<int(int)> action;
    int fd;
};

void make_nonblocking(int fd) {
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) < 0) {
        perror("Non bocking set failed");
    }
}

struct Session {
    int masterfd;
    int epollfd;
    int sockfd;
    int listener;
    int sh_pid;

    bool stopped = false;

    DequeBuffer pt_buffer;
    DequeBuffer net_buffer;
    const int LEN = 1000000;
    char *buffer = new char[LEN];

    Context master_cont, sock_cont;

    void stop() {
        if (!stopped) {
            epoll_event event;
            epoll_ctl(epollfd, EPOLL_CTL_DEL, masterfd, &event);
            epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, &event);

            close(masterfd);
            close(sockfd);
            cout << "Killing" << endl;
            kill(sh_pid, SIGKILL);
            int info;
            cout << "Waiting" << endl;

            waitpid(sh_pid, &info, NULL);
            cout << "Done with pid: " << sh_pid << endl;

        }
        stopped = true;
    }

    ~Session() {
        stop();
        delete[](buffer);
    }

    Session(int _epollfd, int _sockfd, int _master, int _listenre) {
        masterfd = _master;
        epollfd = _epollfd;
        sockfd = _sockfd;
        listener = _listenre;

        make_nonblocking(masterfd);
        make_nonblocking(sockfd);

        master_cont.fd = masterfd;
        sock_cont.fd = sockfd;

        sock_cont.action = [this](int flags) {
            if (flags & EPOLLIN) {

                ssize_t count = read(sockfd, buffer, LEN);
                if (count == 0){
                    stop();
                }
                string s(buffer, count);
//                cout << "count on sock cont in: " << count << "S: " << s << endl;

                pt_buffer.push(s);

                add_fd_write(&master_cont);
                return 0;
            } else {
                if (flags & EPOLLOUT) {

                    ssize_t writed = write(sockfd, net_buffer.from(), net_buffer.getlen());

//                    cout << "pos" << net_buffer.pos << " buffer: " << net_buffer.from() <<
//                    " writed on master cont out: " << writed << " len: " << net_buffer.getlen() << endl;

                    net_buffer.setpos(writed);
                    net_buffer.refresh();

                    if (net_buffer.isEmpty()) {
//                        cout << "sock_cont deleted" << endl;
                        del_fd_write(&sock_cont);
                    }
                    return 0;
                } else {
                    stop();
                    return -1;
                }
            }
        };

        master_cont.action = [this](int flags) {
            if (flags & EPOLLIN) {
                ssize_t count = read(masterfd, buffer, LEN);
                if (count == 0){
                    stop();
                }
                string s(buffer, count);
                net_buffer.push(s);

//                cout << "count on master cont in: " << count << " S: " << s << endl;

                add_fd_write(&sock_cont);
                return 0;
            } else {
                if (flags & EPOLLOUT) {
                    ssize_t writed = write(masterfd, pt_buffer.from(), pt_buffer.getlen());

//                    cout << "pos" << pt_buffer.pos << " buffer: " << pt_buffer.from() <<
//                    " writed on master cont out: " << writed << " len: " << endl;

                    pt_buffer.setpos(writed);
                    pt_buffer.refresh();

                    if (pt_buffer.isEmpty()) {
//                        cout << "master not writing now" << endl;
                        del_fd_write(&master_cont);
                    }
                    return 0;
                } else {
                    stop();
                    return -1;
                }
            }
        };

    }


    void reg() {
        epoll_event event;
        event.events = EPOLLIN | EPOLLHUP | EPOLLERR| EPOLLRDHUP;
        event.data.ptr = &sock_cont;
        cout << epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event) << endl;

        epoll_event event1;
        event1.events = EPOLLIN | EPOLLHUP | EPOLLERR| EPOLLRDHUP;
        event1.data.ptr = &master_cont;
        cout << epoll_ctl(epollfd, EPOLL_CTL_ADD, masterfd, &event1) << endl;

        sh_pid = fork();
        if (sh_pid > 0) {
        } else {
            setsid();

            int slave = open(ptsname(masterfd), O_RDWR);

            struct termios slave_orig_term_settings; // Saved terminal settings
            struct termios new_term_settings; // Current terminal settings
            tcgetattr(slave, &slave_orig_term_settings);
            new_term_settings = slave_orig_term_settings;
            new_term_settings.c_lflag &= ~(ECHO | ECHONL | ICANON);

            tcsetattr(slave, TCSANOW, &new_term_settings);

            dup2(slave, STDIN_FILENO);
            dup2(slave, STDOUT_FILENO);
            dup2(slave, STDERR_FILENO);

            close(slave);
            close(masterfd);
            close(epollfd);
            close(sockfd);
            close(listener);

            _exit(execlp("sh", "sh", NULL));
        }
    }

    int add_fd_write(Context *context) {
        epoll_event event1;
        event1.events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT | EPOLLRDHUP;
        event1.data.ptr = context;
        epoll_ctl(epollfd, EPOLL_CTL_MOD, context->fd, &event1);
        return 0;
    }

    int del_fd_write(Context *context) {
        epoll_event event1;
        event1.events = EPOLLIN | EPOLLHUP | EPOLLERR| EPOLLRDHUP;
        event1.data.ptr = context;
        epoll_ctl(epollfd, EPOLL_CTL_MOD, context->fd, &event1);
        return 0;
    }
};

struct Epoll {
    int epollfd;
    Context accept_cont;
    Listener *listener;
    const int MAXEVENTS = 1000;

    Epoll(uint16_t port) {
        listener = new Listener(port);

        epollfd = epoll_create(O_CLOEXEC);
        accept_cont.fd = listener->sockfd;
        accept_cont.action = [this](int flags) {
            if (flags & EPOLLIN) {
                int sockfd = listener->acceptClient();
                cout << "sockfd" << endl;
                if (sockfd < 0) {
                    //todo
                }
                add_new_user(sockfd);
            }
            return 0;
        };
        epoll_event event;
        event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        event.data.ptr = &accept_cont;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, listener->sockfd, &event);
    }

    void add_new_user(int user_sock_fd) {
        int master = getmaster();
        Session *session = new Session(epollfd, user_sock_fd, master, listener->sockfd);
        session->reg();
    }




    void process() {
        epoll_event *events = new epoll_event[MAXEVENTS];
        while (true) {
            int size = epoll_wait(epollfd, events, MAXEVENTS, -1);
            if (size < 0) {
                //todo
            }
            for (int i = 0; i < size; i++) {
                Context *ev = (Context *) events[i].data.ptr;
                ev->action(events[i].events);
            }
        }
    }
};

void daemonize() {
    int pid = fork();
    if (pid < 0) {
        //todo
        _exit(-1);
    }
    if (pid > 0) {
        //parent not need anymore
        _exit(0);
    }
    //child
    setsid();
}

Epoll *epoll;
bool sig = false;

void handler(int sig, siginfo_t *siginfo, void *ign) {
    if (!sig) {
        delete (epoll);
        epoll = NULL;
    }
    sig = true;
}

int main(int argc, char **argv) {
    daemonize();

    struct sigaction act;
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = handler;
//    sigaction(SIGINT, &act, NULL);

    epoll = new Epoll(atoi(argv[1]));
    epoll->process();
    return 0;
}