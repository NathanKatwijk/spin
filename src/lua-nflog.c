#define LUA_LIB
#include "lua.h"
#include "lauxlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <libnetfilter_log/linux_nfnetlink_log.h>

#include <libnfnetlink/libnfnetlink.h>
#include <libnetfilter_log/libnetfilter_log.h>


#include <math.h>
#define LUA_NFLOG_NAME "nflog"
#define LUA_NFLOG_HANDLER_NAME "nflog.Handler"
#define LUA_NFLOG_EVENT_NAME "nflog.Event"
#define PI 3.1415729

static int math_sin (lua_State *L) {
    //char* a = malloc(24000);
    //(void)a;
    //lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
    lua_pushnumber(L, 32);
    return 1;
}

#define BUFSZ 10000

void stackdump_g(lua_State* l)
{
    int i;
    int top = lua_gettop(l);
    printf("--------------stack--------------\n");
    printf("total in stack %d\n",top);

    for (i = 1; i <= top; i++)
    {  /* repeat for each level */
        int t = lua_type(l, i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
                printf("string: '%s'\n", lua_tostring(l, i));
                break;
            case LUA_TBOOLEAN:  /* booleans */
                printf("boolean %s\n",lua_toboolean(l, i) ? "true" : "false");
                break;
            case LUA_TNUMBER:  /* numbers */
                printf("number: %g\n", lua_tonumber(l, i));
                break;
            default:  /* other values */
                printf("%s\n", lua_typename(l, t));
                break;
        }
        printf("  ");  /* put a separator */
    }
    printf("\n");  /* end the listing */
    printf("----------end  stack-------------\n");
}

typedef struct {
    // callback function passed by the user, stored in the lua registry
    int lua_callback_regid;
    // optional data passed by the user, stored in the lua registry
    int lua_callback_data_regid;
    // file descriptor of the netfilter listener
    int fd;
    // netlogger handle
    struct nflog_handle *handle;
    // netlogger group handle
    struct nflog_g_handle *ghandle;
    // lua state stack, used when calling the callback
    lua_State* L;
} netlogger_info;

typedef struct {
    struct nfulnl_msg_packet_hdr* header;
    struct nflog_data *data;
} event_info;

static void print_nli(netlogger_info* nli) {
    printf("[Netlogger info]\n");
    printf("callback id: %d\n", nli->lua_callback_regid);
    printf("callback data id: %d\n", nli->lua_callback_data_regid);
    printf("callback fd: %d\n", nli->fd);
    printf("callback handle ptr: %p\n", nli->handle);
    printf("callback ghandle ptr: %p\n", nli->ghandle);
    printf("callback state ptr: %p\n", nli->L);
}


int callback_handler(struct nflog_g_handle *handle,
                     struct nfgenmsg *msg,
                     struct nflog_data *nfldata,
                     void *netlogger_info_ptr) {
    netlogger_info* nli = (netlogger_info*) netlogger_info_ptr;

    lua_rawgeti(nli->L, LUA_REGISTRYINDEX, nli->lua_callback_regid);
    lua_rawgeti(nli->L, LUA_REGISTRYINDEX, nli->lua_callback_data_regid);

    // create the event object and add it to the stack
    event_info* event = (event_info*) lua_newuserdata(nli->L, sizeof(event_info));
    // Make that into an actual object
    luaL_getmetatable(nli->L, LUA_NFLOG_EVENT_NAME);
    lua_setmetatable(nli->L, -2);

    event->data = nfldata;
    event->header = nflog_get_msg_packet_hdr(nfldata);

    int result = lua_pcall(nli->L, 2, 0, 0);
    if (result != 0) {
        lua_error(nli->L);
    }
    //stackdump_g(nli->L);
}

//
// Sets up a loop
// arguments:
// group_number (int)
// callback_function (function)
// optional callback function extra argument
//
// The callback function will be passed a pointer to the event and the optional extra user argument
//
static int setup_netlogger_loop(lua_State *L) {
    int fd = -1;

    struct nflog_handle *handle = NULL;
    struct nflog_g_handle *group = NULL;

    // handle the lua arugments to this function
    int groupnum = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // TODO: make setter functions for the options below
    // e.g. handler:settimeout(secs, millisecs), etc.

    /* This opens the relevent netlink socket of the relevent type */
    if ((handle = nflog_open()) == NULL){
        fprintf(stderr, "Could not get netlink handle\n");
        exit(1);
    }

    if (nflog_bind_pf(handle, AF_INET) < 0) {
        fprintf(stderr, "Could not bind netlink handle\n");
        exit(1);
    }

    if ((group = nflog_bind_group(handle, groupnum)) == NULL) {
        fprintf(stderr, "Could not bind to group\n");
        exit(1);
    }

    if (nflog_set_mode(group, NFULNL_COPY_PACKET, 0xffff) < 0) {
        fprintf(stderr, "Could not set group mode\n");
        exit(1);
    }
    if (nflog_set_nlbufsiz(group, BUFSZ) < 0) {
        fprintf(stderr, "Could not set group buffer size\n");
        exit(1);
    }
    if (nflog_set_timeout(group, 1) < 0) {
        fprintf(stderr, "Could not set the group timeout\n");
    }

    /* Get the actual FD for the netlogger entry */
    fd = nflog_fd(handle);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    stackdump_g(L);

    int cb_d = luaL_ref(L, LUA_REGISTRYINDEX);
    int cb_f = luaL_ref(L, LUA_REGISTRYINDEX);

    // Create the result and put it on the stack to return
    netlogger_info* nli = (netlogger_info*) lua_newuserdata(L, sizeof(netlogger_info));

    // Make that into an actual object
    luaL_getmetatable(L, LUA_NFLOG_HANDLER_NAME);
    lua_setmetatable(L, -2);

    nli->fd = fd;
    nli->handle = handle;
    nli->L = L;
    nli->lua_callback_data_regid = cb_d;
    nli->lua_callback_regid = cb_f;

    nflog_callback_register(group, &callback_handler, nli);

    return 1;
}


static int handler_loop_once(lua_State *L) {
    int sz;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    sz = recv(nli->fd, buf, BUFSZ, 0);
    if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
        //printf("[XX] EINTR or EAGAIN\n", nli);
        return 0;
    } else if (sz < 0) {
        printf("Error reading from nflog socket\n");
        return 0;
    }
    nflog_handle_packet(nli->handle, buf, sz);
    return 0;
}

static int handler_loop_forever(lua_State *L) {
    int sz;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    for (;;) {
        sz = recv(nli->fd, buf, BUFSZ, 0);
        if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else if (sz < 0) {
            printf("Error reading from nflog socket\n");
            break;
        }
        nflog_handle_packet(nli->handle, buf, sz);
    }
    return 0;
}

static int handler_close(lua_State *L) {
    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);

    nflog_unbind_group(nli->ghandle);
    nflog_close(nli->handle);

    return 0;
}

static int event_get_from_addr(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);

    char* data;
    size_t datalen;
    char ip_frm[INET6_ADDRSTRLEN];

    data = NULL;
    datalen = nflog_get_payload(event->data, &data);

    if (ntohs(event->header->hw_protocol) == 0x86dd) {
        inet_ntop(AF_INET6, &data[8], ip_frm, INET6_ADDRSTRLEN);
    } else {
        inet_ntop(AF_INET, &data[12], ip_frm, INET6_ADDRSTRLEN);
    }

    lua_pushstring(L, ip_frm);
    return 1;
}

static int event_get_to_addr(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);

    char* data;
    size_t datalen;
    char ip_to[INET6_ADDRSTRLEN];

    data = NULL;
    datalen = nflog_get_payload(event->data, &data);

    if (ntohs(event->header->hw_protocol) == 0x86dd) {
        inet_ntop(AF_INET6, &data[24], ip_to, INET6_ADDRSTRLEN);
    } else {
        inet_ntop(AF_INET, &data[16], ip_to, INET6_ADDRSTRLEN);
    }

    lua_pushstring(L, ip_to);
    return 1;
}

static int event_get_payload_size(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    lua_pushnumber(L, datalen);
    return 1;
}

static int event_get_timestamp(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    return 0;
}

// Library function mapping
static const luaL_Reg nflog_lib[] = {
    {"sin", math_sin},
    {"setup_netlogger_loop", setup_netlogger_loop},
    {NULL, NULL}
};

// Handler function mapping
static const luaL_Reg handler_mapping[] = {
    {"loop_forever", handler_loop_forever},
    {"loop_once", handler_loop_once},
    {"close", handler_close},
    {NULL, NULL}
};

// Event function mapping
static const luaL_Reg event_mapping[] = {
    {"get_from_addr", event_get_from_addr},
    {"get_to_addr", event_get_to_addr},
    {"get_payload_size", event_get_payload_size},
    {"get_timestamp", event_get_timestamp},
    {NULL, NULL}
};

/*
** Netfilter-log library initialization
*/
LUALIB_API int luaopen_lnflog (lua_State *L) {

    // register the handler class
    luaL_newmetatable(L, LUA_NFLOG_HANDLER_NAME); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, handler_mapping); // register functions in the metatable

    // register the event class
    luaL_newmetatable(L, LUA_NFLOG_EVENT_NAME); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, event_mapping); // register functions in the metatable

    // register the library
    luaL_register(L, LUA_NFLOG_NAME, nflog_lib);

    lua_pushnumber(L, PI);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, HUGE_VAL);
    lua_setfield(L, -2, "huge");

    // any additional initialization code goes here

    return 1;
}
