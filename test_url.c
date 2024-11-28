#include<stdio.h>
#include<stdlib.h>
#include"url.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <URL>\n", argv[0]);
        return 1;
    }

    url_info info;
    int result = parse_url(argv[1], &info);

    if (result != 0) {
        printf("Error: %s\n", get_url_errstr(result));
        return 1;
    }

    print_url_info(&info);

    if (validate_url(&info)) {
        printf("VALID\n");
    } else {
        printf("INVALID\n");
    }

    return 0;
}
