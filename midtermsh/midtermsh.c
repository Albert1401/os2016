#include <unistd.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <fcntl.h>
#include <wait.h>
#include <iostream>
#include <errno.h>
#include <cstdio>

using namespace std;

const size_t buff_size = 1024;
char *buff = new char[1024];
vector<pid_t> childs;

struct command {
    const char *name;
    vector<const char *> args;
};

string trim(string &str) {
    size_t first = str.find_first_not_of(' ');
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

//TODO empty string
/**
 * checks for valid command, returns commands to execute
 * vectors size can be 0
 */
vector<command> parse_expr(string &str_comm) {
    vector<command> commands;

    string token;
    str_comm = trim(str_comm);

    stringstream ss(str_comm);
    while (getline(ss, token, '|')) {
        token = trim(token);

        stringstream ss1(token);
        string token1;
        command comm;
        if (getline(ss1, token1, ' ')) {
            char *name = new char[token1.size() + 1];
            strcpy(name, token1.c_str());
            comm.name = name;
            comm.args.push_back(name);
        } else {
            throw 0;
        };

        while (getline(ss1, token1, ' ')) {
            char *arg = new char[token1.size() + 1];
            strcpy(arg, token1.c_str());
            comm.args.push_back(arg);
        }
        comm.args.push_back(NULL);
        commands.push_back(comm);
    }
    return commands;
}


void execute(vector<command> commands) {
    int in, new_in = STDIN_FILENO;
    int out;
    for (int i = 0; i < commands.size(); i++) {
        in = new_in;
        if (i != commands.size() - 1) {
            int pipes[2];
            if (pipe2(pipes, O_CLOEXEC)) {
                //TODO
            };
            new_in = pipes[0];
            out = pipes[1];
        } else {
            out = STDOUT_FILENO;
        }
        pid_t pid = fork();
        if (pid == -1) {
            //todo
        }
        if (pid > 0) {
            if (in != STDIN_FILENO) close(in);
            if (out != STDOUT_FILENO) close(out);
            childs.push_back(pid);
        } else {
            dup2(in, STDIN_FILENO);
            dup2(out, STDOUT_FILENO);
            _exit(execvp(commands[i].name, (char *const *) commands[i].args.data()));
        }
    }
    int status;
    for (int i = 0; i < childs.size(); i++) waitpid(childs[i], &status, 0);
}

void line() {
    write(STDOUT_FILENO, "$ ", 2);
}

void handler(int sig, siginfo_t *, void *) {
    cerr << "1";
    for (int i = 0; i < childs.size(); i++) {
        kill(childs[i], sig);
    }
    write(STDOUT_FILENO, "\n", 1);
}

int main() {
    struct sigaction action;
    action.sa_sigaction = handler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);

    sigaction(SIGINT, &action, NULL);
//    if (sigaction(SIGINT, &action, NULL) < 0) {
//        return errno;
//    }

    ssize_t count = 0;
    while (1) {
        string str_command;
        line();
        while ((count = read(STDIN_FILENO, buff, buff_size)) > 0) {
            str_command.append(buff, 0, count);
            for (int i = 0; i < count; i++) {
                if (buff[i] == '\n') {
                    if (count - 1 > i) {
                        write(STDIN_FILENO, buff + i + 1, count - i - 1);
                    }
                    str_command.erase(i);
                    if (str_command != "") {

                        execute(parse_expr(str_command));
                    }
                    line();
                    str_command.clear();
                    childs.clear();
                    break;
                }
            }

        }
    }
}
