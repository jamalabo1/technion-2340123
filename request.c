#include "segel.h"
#include "request.h"
#include "formatter.h"


void requestError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg, time_stats tm_stats,
                  threads_stats t_stats) {
    char buf[MAXLINE], body[MAXBUF];

    // Safely build the body content
    sprintf(body, "<html><title>OS-HW3 Error</title>");
    sprintf(body + strlen(body), "<body bgcolor=\"fffff\">\r\n");
    sprintf(body + strlen(body), "%s: %s\r\n", errnum, shortmsg);
    sprintf(body + strlen(body), "<p>%s: %s\r\n", longmsg, cause);
    sprintf(body + strlen(body), "<hr>OS-HW3 Web Server\r\n");

    // Build the header
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf + strlen(buf), "Content-Type: text/html\r\n");
    sprintf(buf + strlen(buf), "Content-Length: %lu\r\n", strlen(body));

    // For error cases, we still append stats using the provided tm_stats
    int buf_len = append_stats(buf, t_stats, tm_stats);

    Rio_writen(fd, buf, buf_len);
    Rio_writen(fd, body, strlen(body));
}

void requestReadhdrs(rio_t *rp) {
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
    }
}

int requestParseURI(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    if (strstr(uri, "..")) {
        sprintf(filename, "./public/home.html");
        return 1;
    }
    if (!strstr(uri, "cgi")) {
        strcpy(cgiargs, "");

        //TODO: check if this is ok /%s since it's always / prefixed
        sprintf(filename, "./public/%s", uri);
        if (uri[strlen(uri) - 1] == '/') strcat(filename, "home.html");
        return 1;
    } else {
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        } else {
            strcpy(cgiargs, "");
        }
        sprintf(filename, "./public/%s", uri);
        return 0;
    }
}

void requestGetFiletype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else strcpy(filetype, "text/plain");
}

char *requestPrepareDynamic(char *filename, char *cgiargs, int *out_len) {
    char *emptylist[] = {NULL};
    int p[2];
    if (pipe(p) < 0) return NULL;

    pid_t pid = Fork();
    if (pid == 0) {
        /* Child */
        Close(p[0]);
        Setenv("QUERY_STRING", cgiargs, 1);
        Dup2(p[1], STDOUT_FILENO);
        Execve(filename, emptylist, environ);
        exit(0);
    }
    Close(p[1]);
    char *body = Malloc(MAXBUF);
    int total_read = 0, n;
    while ((n = Rio_readn(p[0], body + total_read, MAXBUF - total_read)) > 0) {
        total_read += n;
    }
    WaitPid(pid, NULL, 0);
    Close(p[0]);
    *out_len = total_read;
    return body;
}

void *requestPrepareStatic(char *filename, int filesize) {
    int srcfd = Open(filename, O_RDONLY, 0);
    char *body = Malloc(filesize);
    if (body) {
        Rio_readn(srcfd, body, filesize);
    }
    Close(srcfd);
    return (void *) body;
}

void requestHandle(int fd, time_stats tm_stats, threads_stats t_stats, server_log log) {
    int is_static = 0;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE], filetype[MAXLINE];
    rio_t rio;

    void *body_content = NULL;
    int body_len = 0;
    char resp_headers[MAXBUF];

    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    // all in all
    t_stats->total_req++;


    if (strcasecmp(method, "GET") == 0) {


        // we are about to enter the log
        add_to_log(log, tm_stats, t_stats);



        requestReadhdrs(&rio);
        is_static = requestParseURI(uri, filename, cgiargs);

        if (stat(filename, &sbuf) < 0) {
            requestError(fd, filename, "404", "Not found", "OS-HW3 Server could not find this file", tm_stats, t_stats);
            return;
        }

        if (is_static) {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
                requestError(fd, filename, "403", "Forbidden", "OS-HW3 Server could not read this file", tm_stats,
                             t_stats);
                return;
            }


            requestGetFiletype(filename, filetype);
            body_len = sbuf.st_size;
            body_content = requestPrepareStatic(filename, body_len);

            // thread safe, and valid here
            t_stats->stat_req++;

            // Fixed Content-Length format string and sprintf overlap
            sprintf(resp_headers, "HTTP/1.0 200 OK\r\n");
            sprintf(resp_headers + strlen(resp_headers), "Server: OS-HW3 Web Server\r\n");
            sprintf(resp_headers + strlen(resp_headers), "Content-Length: %d\r\n", body_len);
            sprintf(resp_headers + strlen(resp_headers), "Content-Type: %s\r\n", filetype);
        } else {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
                requestError(fd, filename, "403", "Forbidden", "OS-HW3 Server could not run this CGI program", tm_stats,
                             t_stats);
                return;
            }
            body_content = requestPrepareDynamic(filename, cgiargs, &body_len);

            // dynamic request
            t_stats->dynm_req++;

            sprintf(resp_headers, "HTTP/1.0 200 OK\r\n");
            sprintf(resp_headers + strlen(resp_headers), "Server: OS-HW3 Web Server\r\n");
        }
    } else if (strcasecmp(method, "POST") == 0) {
        // we are about to enter the log
        body_len = get_log(log, (char **) &body_content, tm_stats);

        // post request
        t_stats->post_req++;

        sprintf(resp_headers, "HTTP/1.0 200 OK\r\n");
        sprintf(resp_headers + strlen(resp_headers), "Server: OS-HW3 Web Server\r\n");
        sprintf(resp_headers + strlen(resp_headers), "Content-Length: %d\r\n", body_len);
        sprintf(resp_headers + strlen(resp_headers), "Content-Type: text/plain\r\n");
    } else {
        requestError(fd, method, "501", "Not Implemented", "OS-HW3 Server does not implement this method", tm_stats,
                     t_stats);
        return;
    }



    // --- SEND ---
    int total_header_len = append_stats(resp_headers, t_stats, tm_stats);
    Rio_writen(fd, resp_headers, total_header_len);
    Rio_writen(fd, body_content, body_len);

    if (body_content) {
        free(body_content);
    }
}
