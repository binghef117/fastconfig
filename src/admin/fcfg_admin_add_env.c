#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common/fcfg_proto.h"
#include "fcfg_admin_func.h"
#include "fcfg_admin_add_env.h"

static bool show_usage = false;
FCFGAdminAddEnvGlobal g_fcfg_admin_add_env;
static void usage(char *program)
{
    fprintf(stderr, "Usage: %s options, the options as:\n"
            "\t -h help\n"
            "\t -c <config-filename>\n"
            "\t -e <config-env>\n"
            "\n", program);
}

static void parse_args(int argc, char **argv)
{
    int ch;
    int found = 0;

    while ((ch = getopt(argc, argv, "hc:e:")) != -1) {
        found = 1;
        switch (ch) {
            case 'c':
                g_fcfg_admin_add_env.config_file = optarg;
                break;
            case 'e':
                g_fcfg_admin_add_env.config_env = optarg;
                break;
            case 'h':
            default:
                show_usage = true;
                break;
        }
    }
    if (found == 0) {
        show_usage = true;
    }
}

void fcfg_set_admin_add_env(char *buff,
        int *body_len)
{
    unsigned char env_len = strlen(g_fcfg_admin_add_env.config_env);
    memcpy(buff, g_fcfg_admin_add_env.config_env,
           env_len);
    *body_len = env_len;
}

int fcfg_admin_add_env ()
{
    int ret;
    char buff[1024];
    int body_len;
    int size;
    FCFGResponseInfo resp_info;
    FCFGProtoHeader *fcfg_header_proto;

    fcfg_header_proto = (FCFGProtoHeader *)buff;
    fcfg_set_admin_add_env(buff + sizeof(FCFGProtoHeader), &body_len);
    fcfg_set_admin_header(fcfg_header_proto, FCFG_PROTO_ADD_ENV_REQ, body_len);
    size = sizeof(FCFGProtoHeader) + body_len;
    ret = send_and_recv_response_header(&g_fcfg_admin_vars.join_conn, buff, size, &resp_info,
            g_fcfg_admin_vars.network_timeout, g_fcfg_admin_vars.connect_timeout);
    if (ret) {
        fprintf(stderr, "send_and_recv_response_header fail. ret:%d, %s\n",
                ret, strerror(ret));
        return ret;
    }
    ret = fcfg_admin_check_response(&g_fcfg_admin_vars.join_conn, &resp_info, g_fcfg_admin_vars.network_timeout);
    if (ret) {
        fprintf(stderr, "add env fail.err info: %s\n",
                resp_info.error.message);
    } else {
        fprintf(stderr, "add env success !\n");
    }

    return ret;
}

int main (int argc, char **argv)
{
    int ret;

    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }
    parse_args(argc, argv);
    if (show_usage) {
        usage(argv[0]);
        return 0;
    }

    log_init2();

    ret = fcfg_admin_load_config(g_fcfg_admin_add_env.config_file);
    if (ret) {
        fprintf(stderr, "fcfg_admin_load_config fail:%s, ret:%d, %s\n",
                g_fcfg_admin_add_env.config_file, ret, strerror(ret));
        log_destroy();
        return ret;
    }


    if ((ret = conn_pool_connect_server(&g_fcfg_admin_vars.join_conn,
                    g_fcfg_admin_vars.connect_timeout)) != 0) {
        fprintf(stderr, "conn_pool_connect_server fail: %s:%d, ret:%d, %s\n",
                g_fcfg_admin_vars.join_conn.ip_addr,
                g_fcfg_admin_vars.join_conn.port,
                ret, strerror(ret));
        log_destroy();
        return ret;
    }

    if ((ret = fcfg_send_admin_join_request(&g_fcfg_admin_vars.join_conn,
            g_fcfg_admin_vars.network_timeout,
            g_fcfg_admin_vars.connect_timeout)) != 0) {
        log_destroy();
        return ret;
    }

    ret = fcfg_admin_add_env();
    if (g_fcfg_admin_vars.join_conn.sock >= 0) {
        conn_pool_disconnect_server(&g_fcfg_admin_vars.join_conn);
    }

    log_destroy();
    return 0;
}