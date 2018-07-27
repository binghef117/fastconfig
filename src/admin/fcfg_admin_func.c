#include "common/fcfg_proto.h"
#include "fcfg_admin_func.h"
#include "fastcommon/sockopt.h"

FCFGAdminGlobal g_fcfg_admin_vars;
void fcfg_set_admin_join_req(char *buff,
        int *body_len)
{
    FCFGProtoAdminJoinReq *join_req = (FCFGProtoAdminJoinReq *)buff;
    unsigned char username_len = strlen(g_fcfg_admin_vars.username);
    unsigned char secret_key_len = strlen(g_fcfg_admin_vars.secret_key);
    join_req->username_len = username_len;
    join_req->secret_key_len = secret_key_len;
    memcpy(join_req->username,
            g_fcfg_admin_vars.username,
            username_len);
    memcpy(join_req->username + username_len,
           g_fcfg_admin_vars.secret_key,
           secret_key_len);

    *body_len = sizeof(FCFGProtoAdminJoinReq) + secret_key_len + username_len;
}

int send_and_recv_response_header(ConnectionInfo *conn, char *data, int len,
        FCFGResponseInfo *resp_info, int network_timeout, int connect_timeout)
{
    int ret;
    FCFGProtoHeader fcfg_header_resp_pro;

    if ((ret = tcprecvdata_nb_ex(conn->sock, data,
            len, network_timeout, NULL)) != 0) {
        return ret;
    }
    if ((ret = tcprecvdata_nb_ex(conn->sock, &fcfg_header_resp_pro,
            sizeof(FCFGProtoHeader), network_timeout, NULL)) != 0) {
        return ret;
    }
    fcfg_proto_response_extract(&fcfg_header_resp_pro, resp_info);
    return 0;
}

static void _get_conn_config (ConnectionInfo *conn, const char *config_server)
{
    char *buff[2];
    char tmp_value[32];

    strncpy(tmp_value, config_server, sizeof(tmp_value) - 1);
    tmp_value[sizeof(tmp_value) - 1] = '\0';
    splitEx(tmp_value, ':', buff, 2);
    strncpy(conn->ip_addr, buff[0], sizeof(conn->ip_addr));
    conn->port = atoi(buff[1]);
    conn->sock = -1;
}

int fcfg_admin_load_config(const char *filename)
{
    IniContext ini_context;
    int result;
    char *pDataPath;
    char *config_server[FCFG_CONFIG_SERVER_COUNT_MAX];
    int server_count;
    int i;

    memset(&ini_context, 0, sizeof(IniContext));
    if ((result=iniLoadFromFile(filename, &ini_context)) != 0) {
        fprintf(stderr, "file: "__FILE__", line: %d, "
                "load conf file \"%s\" fail, ret code: %d",
                __LINE__, filename, result);
        return result;
    }

    server_count = iniGetValues(NULL, "config_server",
                    &ini_context, config_server, FCFG_CONFIG_SERVER_COUNT_MAX);
    if (server_count <= 0) {
        fprintf(stderr, "get config_server fail %d", server_count);
        return -1;
    }
    g_fcfg_admin_vars.server_count = server_count;
    g_fcfg_admin_vars.join_conn = (ConnectionInfo *)malloc(server_count *
            sizeof(ConnectionInfo));
    if (g_fcfg_admin_vars.join_conn == NULL) {
        fprintf(stderr, "malloc fail \n");
        return 1;
    }
    for (i = 0; i < server_count; i ++) {
        _get_conn_config(g_fcfg_admin_vars.join_conn + i, config_server[i]);
        fprintf(stderr, "config_server: %s\n", config_server[i]);
    }

    g_fcfg_admin_vars.network_timeout = iniGetIntValue(NULL, "network_timeout",
            &ini_context, FCFG_NETWORK_TIMEOUT_DEFAULT);

    g_fcfg_admin_vars.connect_timeout = iniGetIntValue(NULL, "connect_timeout",
            &ini_context, FCFG_CONNECT_TIMEOUT_DEFAULT);

    pDataPath = iniGetStrValue(NULL, "username", &ini_context);
    if (pDataPath == NULL || *pDataPath == '\0') {
        fprintf(stderr, "get username from file:%s", filename);
        return ENOENT;
    }
    snprintf(g_fcfg_admin_vars.username, sizeof(g_fcfg_admin_vars.username), "%s",
            pDataPath);

    pDataPath = iniGetStrValue(NULL, "secret_key", &ini_context);
    if (pDataPath == NULL || *pDataPath == '\0') {
        fprintf(stderr, "get secret_key from file:%s", filename);
        return ENOENT;
    }
    snprintf(g_fcfg_admin_vars.secret_key, sizeof(g_fcfg_admin_vars.secret_key), "%s",
            pDataPath);

    iniFreeContext(&ini_context);
    return 0;
}


int fcfg_admin_check_response(ConnectionInfo *join_conn,
        FCFGResponseInfo *resp_info, int network_timeout, unsigned char resp_cmd)
{
    if (resp_info->cmd == resp_cmd && resp_info->status == 0) {
        return 0;
    } else {
        if (resp_info->body_len) {
            tcprecvdata_nb_ex(join_conn->sock, resp_info->error.message,
                    resp_info->body_len, network_timeout, NULL);
        } else {
            resp_info->error.message[0] = '\0';
        }
        return 1;
    }

}
int fcfg_send_admin_join_request(ConnectionInfo *join_conn, int network_timeout,
        int connect_timeout)
{
    int ret;
    char buff[1024];
    int body_len;
    int size;
    FCFGResponseInfo resp_info;
    FCFGProtoHeader *fcfg_header_proto;

    fcfg_header_proto = (FCFGProtoHeader *)buff;
    fcfg_set_admin_join_req(buff + sizeof(FCFGProtoHeader), &body_len);
    fcfg_set_admin_header(fcfg_header_proto, FCFG_PROTO_ADMIN_JOIN_REQ, body_len);
    size = sizeof(FCFGProtoHeader) + body_len;
    ret = send_and_recv_response_header(join_conn, buff, size, &resp_info,
            network_timeout, connect_timeout);
    if (ret) {
        fprintf(stderr, "send_and_recv_response_header fail. ret:%d, %s\n",
                ret, strerror(ret));
        return ret;
    }
    ret = fcfg_admin_check_response (join_conn, &resp_info, network_timeout, FCFG_PROTO_ACK);
    if (ret) {
        fprintf(stderr, "join server fail. %s\n", resp_info.error.message);
    } else {
        fprintf(stderr, "join server success!\n");
    }

    return ret;
}

int fcfg_do_conn_config_server (ConnectionInfo **conn)
{
    int ret;
    int server_index;
    ConnectionInfo *join_conn;
    int index;

    index = 0;
    srand(time(NULL));
    server_index = rand() % g_fcfg_admin_vars.server_count;
    while (index < g_fcfg_admin_vars.server_count) {
        join_conn = g_fcfg_admin_vars.join_conn + server_index;
        if ((ret = conn_pool_connect_server(join_conn,
                        g_fcfg_admin_vars.connect_timeout)) != 0) {
            fprintf(stderr, "conn_pool_connect_server fail. server index[%d] %s:%d, ret:%d, %s\n",
                    server_index,
                    join_conn->ip_addr,
                    join_conn->port,
                    ret, strerror(ret));
            server_index = (server_index + 1) % g_fcfg_admin_vars.server_count;
            index ++;
        } else {
            /* connect success */
            *conn = join_conn;
            break;
        }
    }

    return ret;
}

void fcfg_disconn_config_server (ConnectionInfo *conn)
{
    if (conn->sock >= 0) {
        conn_pool_disconnect_server(conn);
    }
}
