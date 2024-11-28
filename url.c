/**
 *  Jiazi Yi
 *
 * LIX, Ecole Polytechnique
 * jiazi.yi@polytechnique.edu
 *
 * Updated by Pierre Pfister
 *
 * Cisco Systems
 * ppfister@cisco.com
 *
 */

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"url.h"

/**
 * parse a URL and store the information in info.
 * return 0 on success, or an integer on failure.
 *
 * Note that this function will heavily modify the 'url' string
 * by adding end-of-string delimiters '\0' into it, hence chunking it into smaller pieces.
 * In general this would be avoided, but it will be simpler this way.
 */

int parse_url(char* url, url_info *info) {
    char *column_slash_slash, *host_name_path, *protocol;
    char *original_url = strdup(url);  // Keep a copy of the original URL

    // Handle relative URLs
    if (url[0] == '/') {
        // Relative URL with absolute path
        info->path = strdup(url + 1);
        info->protocol = strdup("http");  // Default to http for relative URLs
        return 0;
    } else if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        // Relative URL with relative path
        info->path = strdup(url);
        info->protocol = strdup("http");  // Default to http for relative URLs
        return 0;
    }

    column_slash_slash = strstr(url, "://");
    if(column_slash_slash != NULL) {
        *column_slash_slash = '\0';
        host_name_path = column_slash_slash + 3;
        protocol = url;
    } else {
        host_name_path = url;
        protocol = "http";
    }

    info->protocol = strdup(protocol);

    // Accept both http and https
    if (strcmp(info->protocol, "http") != 0 && strcmp(info->protocol, "https") != 0) {
        free(original_url);
        return PARSE_URL_PROTOCOL_UNKNOWN;
    }

    char *slash = strchr(host_name_path, '/');
    if (slash == NULL) {
        info->path = strdup("");
    } else {
        *slash = '\0';
        info->path = strdup(slash + 1);
    }

    info->host = strdup(host_name_path);

    char *port = strchr(host_name_path, ':');
    if (port) {
        *port = '\0';
        port = port + 1;
        if (sscanf(port, "%d", &info->port) != 1) {
            free(original_url);
            return PARSE_URL_INVALID_PORT;
        }
    } else {
        // Set default port based on protocol
        info->port = (strcmp(info->protocol, "https") == 0) ? 443 : 80;
    }

    free(original_url);
    return 0;
}
/**
 * print the url info to std output
 */
void print_url_info(url_info *info){
	printf("The URL contains following information: \n");
	printf("Protocol:\t%s\n", info->protocol);
	printf("Host name:\t%s\n", info->host);
	printf("Port No.:\t%d\n", info->port);
	printf("Path:\t\t/%s\n", info->path);
}

int update_url(url_info *info, const char *new_url) {
    url_info new_info;
    int result = parse_url((char *)new_url, &new_info);
    if (result != 0) {
        return result;
    }

    // If the new URL is relative, update only the path
    if (new_info.host == NULL) {
        free(info->path);
        info->path = new_info.path;
    } else {
        free(info->protocol);
        free(info->host);
        free(info->path);
        *info = new_info;
    }

    return 0;
}

/**
 * Get the parsing error string to std output
 */
char* get_url_errstr(int err_code){
	return (char*) parse_url_errstr[err_code];
}