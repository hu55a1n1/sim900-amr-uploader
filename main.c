#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h> 
#include <stdlib.h>
#include "libserialport.h"

const char *desired_port = "/dev/ttyS2";
char AT_command[512];
struct sp_port *port;

void list_ports() {
    int i;
    struct sp_port **ports;

    enum sp_return error = sp_list_ports(&ports);
    if (error == SP_OK) {
        for (i = 0; ports[i]; i++) {
            printf("Found port: '%s'\n", sp_get_port_name(ports[i]));
        }
        sp_free_port_list(ports);
    } else {
        printf("No serial devices detected\n");
    }
    printf("\n");
}

void parse_serial(char *byte_buff, int byte_num) {
    printf("> Received: ");
    for (int i = 0; i < byte_num; i++) {
        printf("%c", byte_buff[i]);
    }
    printf("\n");
}

char *parseFind(const char *const theString, const char *objectOfDesire) {
    if (!theString) return 0;
    return strstr(theString, objectOfDesire);
}

int parseData(char *key, const char *const theString, const char *const start, const char *const end) {
    if (!theString) {
        return 0;
    }
    size_t startSize = strlen(start);
    char *startP = strstr(theString, start);
    if (!startP) {
        return 0;
    }
    startP += startSize;
    char *endP = strstr((startP), end);
    if (!endP) {
        return 0;
    }
    int dataPos = 0;
    while (startP != endP) {
        key[dataPos++] = *startP++;
    }
    key[dataPos] = '\0';
    return 1;
}

int read_serial(char *byte_buff, int size) {
    int bytes_waiting = sp_input_waiting(port);
    if (bytes_waiting > 0) {
        // printf("Bytes waiting %i\n", bytes_waiting);
        int byte_num = sp_nonblocking_read(port, byte_buff, size);
        byte_buff[byte_num] = '\0';
        parse_serial(byte_buff, byte_num);
        return 1;
    }
    fflush(stdout);
    return 0;
}

int wait_for_str(char *str, int waittime) {
    char byte_buff[512];
    int startTime = (int) time(NULL);
    while (((int) time(NULL) - startTime) < waittime) {
        if (read_serial(byte_buff, 512) && parseFind(byte_buff, str)) {
            return 1;
        }
        usleep(100000);
    }
    return 0;
}

int wait_for_str_ext(char *byte_buff, int size, char *str, int waittime) {
    int startTime = (int) time(NULL);
    while (((int) time(NULL) - startTime) < waittime) {
        if (read_serial(byte_buff, 512) && parseFind(byte_buff, str)) {
            return 1;
        }
        usleep(100000);
    }
    return 0;
}

char *read_file(const char *filename, int *size) {
    char *source = NULL;
    FILE *fp = fopen(filename, "rb");
    if (fp != NULL) {
        if (fseek(fp, 0L, SEEK_END) == 0) {
            long bufsize = ftell(fp);
            if (bufsize == -1) { return NULL; }

            source = malloc(sizeof(char) * (bufsize + 1));
            if (!source) return NULL;

            if (fseek(fp, 0L, SEEK_SET) != 0) { return NULL; }

            size_t newLen = fread(source, sizeof(char), bufsize, fp);
            if (ferror(fp) != 0) {
                fputs("Error reading file", stderr);
                free(source);
                return NULL;
            } else {
                source[newLen++] = 0;
            }

            *size = bufsize;
        }
        fclose(fp);
    }
    return source;
}

enum sp_return send_AT_command(struct sp_port *port, char *AT_command) {
    printf("> Sending: %s\n", AT_command);
    strcat(AT_command, "\r\n");
    return sp_blocking_write(port, AT_command, strlen(AT_command), 10000);
}

int check_amr(const char *filename, int filesize) {
    if (!filename) { return 0; }

    sprintf(AT_command, "AT+CFSGFIS=\"%s\"", filename);
    enum sp_return error = send_AT_command(port, AT_command);
    char resp[512] = {0};
    if (!wait_for_str_ext(resp, sizeof(resp), "CFSGFIS", 3)) {
        return 0;
    }

    char onflash_size[16] = {0};
    parseData(onflash_size, resp, "+CFSGFIS: ", "\r\n\r\nOK");
    if (atoi(onflash_size) == filesize) {
        return 1;
    }

    return 0;
}

int send_amr(const char *path, const char *filename, int filesize, int inputtime) {
    if (!filename) { return 0; }

    if (check_amr(filename, filesize)) {
        printf("===========================================\n");
        printf("\t>>> %s already uploaded", filename);
        printf("\n===========================================\n\n");
        return 1;
    }

    // Read file
    int size;
    char filename_abs[FILENAME_MAX] = {0};
    sprintf(filename_abs, "%s%s", path, filename);
    unsigned char *filebuf = read_file(filename_abs, &size);
    if (!filebuf) {
        printf("Error reading file!\n");
        return 0;
    }


    sprintf(AT_command, "AT+CFSWFILE=\"%s\",0,%d,%d", filename, filesize, inputtime);
    enum sp_return error = send_AT_command(port, AT_command);
    if (!wait_for_str("CONNECT", 5)) { return 0; }

    error = sp_blocking_write(port, filebuf, size, 10000);
    sleep(1);

    free(filebuf);
    return 1;
}

int main(int argc, char *argv[]) {
    int ret = 0;
    printf("\n\n===========================================\n");

    // list_ports();
    printf("Opening port '%s' \n", desired_port);
    enum sp_return error = sp_get_port_by_name(desired_port, &port);
    if (SP_OK != error) {
        printf("Error finding serial device\n");
        ret = -1;
        goto EXIT;
    }

    error = sp_open(port, SP_MODE_READ);
    if (SP_OK != error) {
        printf("Error opening serial device\n");
        ret = -2;
        goto EXIT;
    }

    sp_set_baudrate(port, 115200);

    // Check communication
    strcpy(AT_command, "AT");
    error = send_AT_command(port, AT_command);
    if (!wait_for_str("OK", 3)) {
        printf("Communication failure\n");
        ret = -3;
        goto CLOSE;
    }

    // Init flash
    strcpy(AT_command, "AT+CFSINIT");
    error = send_AT_command(port, AT_command);
    if (!wait_for_str("\r\n", 3)) { return 0; }

    if (!send_amr("./data/", "1.amr", 35910, 140000)) {
        printf("===========================================\n");
        printf("==== Failed to send one or more files! ====\n");
        printf("===========================================\n\n");

    } else {
        printf("\n\n===========================================\n");
        printf("================= SUCCESS =================\n");
        printf("===========================================\n\n");
    }

    CLOSE:
    sp_close(port);
    EXIT:
    return ret;
}
