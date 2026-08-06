



#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>
#include "sha256_impl.h"

#include <time.h>

#define PORT        50410
#define SID         "1024"
#define MAX         4096
#define MAX_USER    100
#define MAX_ATTEMPT 3
#define BLOCK_TIME  60
#define RATE_LIMIT  5
#define REQ         20
#define TOKEN_LEN   32
#define TOKEN_EXPIRY 300


#define USER_FILE   "/srv/ie2102/IT24102410/users.dat"

#define LOG_FILE    "server_IT24102410.log"
#define DATA_DIR    "/srv/ie2102/IT24102410"


static int server_fd_global = -1;

typedef struct {
    char username[50];
    char salt[16 + 1];
    char password[100];
} User;

typedef struct {
    char  token[TOKEN_LEN + 1];
    char  username[50];
    time_t last_active;
    int   valid;
} Session;

User user[MAX_USER];
int  user_count = 0;

int    login_failures = 0;
int    login_attempts = 0;
time_t block_until    = 0;

int    request_count = 0;
time_t request_time  = 0;

// Signal Handlers (A2)                                                */
void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void sigint_handler(int sig) {
    (void)sig;
    printf("\n[server] Shutting down SID:%s cleanly.\n", SID);
    
    if (server_fd_global != -1) close(server_fd_global);
    exit(0);
}


// SHA-256 Helpers    
void sha256_has_pass(const char *input, char *outputbuffer) {
    unsigned char hash[32];           
    perform_hash((const char*)input, "some_salt",(char*)hash,32,0);

    
   for (int i = 0; i < 32; i++)
        sprintf(outputbuffer + (i * 2), "%02x", hash[i]);

    outputbuffer[64] = '\0';
    printf("[DEBUG] SHA256 hash: %s\n", outputbuffer);
}

void hash_pass(const char *pass, const char *salt, char *outputbuffer) {
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", salt, pass);
    printf("[DEBUG] Combined string: %s\n", combined);
    sha256_has_pass(combined, outputbuffer);
}

void gen_salt(char *salt, int len) {
    const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (int i = 0; i < len; i++)
        salt[i] = charset[rand() % (int)(sizeof(charset) - 1)];
    salt[len] = '\0';
}

void gen_token(char *token, int len) {
    gen_salt(token, len);
}

//for security= ignore the to trak anyone 


int check_block(void) {
    if (time(NULL) < block_until)   /* FIX 3: was "f(...)" – typo for if */
        return 1;
    return 0;
}
// pas the login faild cmd
void failed_login(void) {
    login_failures++;
    login_attempts++;
    if (login_failures >= MAX_ATTEMPT)
        block_until = time(NULL) + BLOCK_TIME;
}
// whne theclient gave wrong user name
int invalid_Uname(const char *uname) {
    if (strlen(uname) < 3 || strlen(uname) > 15) return 0;
    for (int i = 0; uname[i]; i++) {
        if (!isalnum((unsigned char)uname[i])) return 0;
    }
    return 1;
}
void load_users(void) {
    //LOG
    FILE *fp = fopen(USER_FILE, "rb");
    if (!fp) { user_count = 0; return; }
    fread(&user_count, sizeof(int), 1, fp);
    fread(user, sizeof(User), user_count, fp);
    fclose(fp);
}

void save_users(void) {
    FILE *fp = fopen(USER_FILE, "wb");
    if (!fp) return;
    fwrite(&user_count, sizeof(int), 1, fp);
    fwrite(user, sizeof(User), user_count, fp);
    fclose(fp);
}



// heare log event 

void Audit_logging(const char *ip, int port, pid_t pid,
                   const char *username, const char *cmd, const char *result) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) {
        perror("Log file open failed"); // This will tell you if it's a permission issue
        return;
    }

    time_t ticks = time(NULL);
    char *ts = ctime(&ticks);
    ts[strlen(ts) - 1] = '\0';

    fprintf(fp, "[%s] IP:%s:%d | PID:%d | USER:%s | CMD:%s | RES:%s\n",
            ts, ip, port, (int)pid,
            username ? username : "GUEST", cmd, result);

    fflush(fp);
   printf("[DEBUG CHILD %d] Audit log written to %s\n", getpid(), LOG_FILE);
   
    fclose(fp);
}

/* Reliable recv (A1 - partial recv handling)                          */
int recv_all(int sock, char *buf, int n) {
    int total = 0, r;
    while (total < n) {
        r = recv(sock, buf + total, n - total, 0);
        if (r <= 0) return r;
        total += r;   /*  total was never incremented in original */
    }
    return total;
}

/* Client Handler (all A1 + A3 + A4 + A5)                                 */
void handle_client(int client_sock, struct sockaddr_in client_addr) {
    char buffer[8192];
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int port = ntohs(client_addr.sin_port);

    printf("[DEBUG CHILD %d] Connected to IP: %s\n", getpid(), client_ip);


    Session session;
    memset(&session, 0, sizeof(session));

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        buffer[bytes] = '\0';

        /* A4: brute-force lockout */
        if (check_block()) {
            send(client_sock, "ERR 403 SID:1024 Account Blocked\n", 33, 0);
            Audit_logging(client_ip, port, getpid(),
                          session.valid ? session.username : NULL,
                          "BLOCKED", "LOCKOUT");
            continue;
        }

        /* A1: Protocol Parsing - expect "LEN:<n>\n<payload>" */
        char *len_ptr = strstr(buffer, "LEN:");
        if (!len_ptr) {
            send(client_sock, "ERR 400 SID:1024 Invalid Format\n", 32, 0);
            Audit_logging(client_ip, port, getpid(), NULL, "PARSE", "BAD_FORMAT");
            continue;
        }

        int msg_len;
        if (sscanf(len_ptr, "LEN:%d", &msg_len) != 1) {
            send(client_sock, "ERR 400 SID:1024 Invalid Length\n", 32, 0);
            continue;
        }

        /* A1: Oversized payload rejection (>4096) */
        if (msg_len > MAX) {
            send(client_sock, "ERR 413 SID:1024 Payload Too Large\n", 35, 0);
            Audit_logging(client_ip, port, getpid(), NULL, "RECV", "OVERSIZED");
            continue;
        }

        char *payload = strchr(buffer, '\n');
        if (!payload) {
            send(client_sock, "ERR 400 SID:1024 Missing Payload\n", 33, 0);
            continue;
        }
        payload++; /* advance past the newline */

        /* ---- Command Dispatch ---- */
        char cmd[32], arg1[64], arg2[64];
        int  n = sscanf(payload, "%31s %63s %63s", cmd, arg1, arg2);

        char response[512];

        /* REGISTER <user> <pass> */
        if (strcmp(cmd, "REGISTER") == 0 && n == 3) {
            if (!invalid_Uname(arg1)) {
                snprintf(response, sizeof(response),
                         "ERR 422 SID:%s Invalid Username\n", SID);
                send(client_sock, response, strlen(response), 0);
                Audit_logging(client_ip, port, getpid(), arg1, "REGISTER", "BAD_UNAME");
                continue;
            }
            if (user_count >= MAX_USER) {
                snprintf(response, sizeof(response),
                         "ERR 507 SID:%s User limit reached\n", SID);
                send(client_sock, response, strlen(response), 0);
                continue;
            }
            int dup = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(user[i].username, arg1) == 0) { dup = 1; break; }
            }
            if (dup) {
                snprintf(response, sizeof(response),
                         "ERR 409 SID:%s Username taken\n", SID);
                send(client_sock, response, strlen(response), 0);
                Audit_logging(client_ip, port, getpid(), arg1, "REGISTER", "DUPLICATE");
                continue;
            }
            User *u = &user[user_count++];
            strncpy(u->username, arg1, sizeof(u->username) - 1);
            gen_salt(u->salt, 16);
            hash_pass(arg2, u->salt, u->password);
            save_users();

            snprintf(response, sizeof(response),
                     "OK 201 SID:%s Registered %s\n", SID, arg1);
            send(client_sock, response, strlen(response), 0);
            Audit_logging(client_ip, port, getpid(), arg1, "REGISTER", "OK");

        /*Client must be enter name and password  LOGIN <user> <pass> */
        } else if (strcmp(cmd, "LOGIN") == 0 && n == 3) {
            int found = -1;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(user[i].username, arg1) == 0) { found = i; break; }
            }
            char hashed[65];
            if (found >= 0) hash_pass(arg2, user[found].salt, hashed);

            if (found < 0 || strcmp(hashed, user[found].password) != 0) {
                failed_login();
                snprintf(response, sizeof(response),
                         "ERR 401 SID:%s Bad credentials\n", SID);
                send(client_sock, response, strlen(response), 0);
                Audit_logging(client_ip, port, getpid(), arg1, "LOGIN", "FAIL");
                continue;
            }
            login_failures = 0;
            gen_token(session.token, TOKEN_LEN);
            strncpy(session.username, arg1, sizeof(session.username) - 1);
            session.last_active = time(NULL);
            session.valid = 1;

            snprintf(response, sizeof(response),
                     "OK 200 SID:%s TOKEN:%s\n", SID, session.token);
            send(client_sock, response, strlen(response), 0);
            Audit_logging(client_ip, port, getpid(), arg1, "LOGIN", "OK");

        /*The  LOGOUT */
        } else if (strcmp(cmd, "LOGOUT") == 0) {
            if (!session.valid) {
                snprintf(response, sizeof(response),
                         "ERR 401 SID:%s Not logged in\n", SID);
            } else {
                Audit_logging(client_ip, port, getpid(),
                              session.username, "LOGOUT", "OK");
                memset(&session, 0, sizeof(session));
                snprintf(response, sizeof(response),
                         "OK 200 SID:%s Logged out\n", SID);
            }
            send(client_sock, response, strlen(response), 0);

        /* Any other command */
        } else {
            /* Token expiry check */
            if (session.valid &&
                (time(NULL) - session.last_active) > TOKEN_EXPIRY) {
                session.valid = 0;
                snprintf(response, sizeof(response),
                         "ERR 401 SID:%s Session expired\n", SID);
                send(client_sock, response, strlen(response), 0);
                Audit_logging(client_ip, port, getpid(),
                              session.username, cmd, "TOKEN_EXPIRED");
                continue;
            }
            if (session.valid) session.last_active = time(NULL);

            snprintf(response, sizeof(response),
                     "OK 200 SID:%s Received: %s\n", SID, payload);
            send(client_sock, response, strlen(response), 0);
            Audit_logging(client_ip, port, getpid(),
                          session.valid ? session.username : "GUEST",
                          cmd, "OK");
        }
    }

    close(client_sock);
    exit(0);
}






time_t server_start;
int active_clients = 0;

server_start = time(NULL); 


if (strcmp(command, "STATUS") == 0) {
    long uptime = time(NULL) - server_start;
    char status_msg[128];
    sprintf(status_msg, "Clients: %d, Uptime: %ld seconds\n", active_clients, uptime);
    send(client_socket, status_msg, strlen(status_msg), 0);
}
// Update active_clients on connect/disconnect



/* main      */
int main(void) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT,  sigint_handler);

    load_users();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    /* sign to global so sigint_handler can close it */
    server_fd_global = server_fd;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    listen(server_fd, 128);
    printf("[SERVER] Listening on port %d | SID:%s\n", PORT, SID);

    while (1) {
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd < 0) continue;

        pid_t pid = fork();
        if (pid == 0) {       /* Child process */
            close(server_fd);
            handle_client(client_fd, client_addr);
        } else if (pid > 0) { /* Parent process */
            close(client_fd);
            printf("[PARENT] Spawned child PID = %d\n", (int)pid);
        } else {
            perror("fork");
            close(client_fd);
        }
    }

    return 0;
}
