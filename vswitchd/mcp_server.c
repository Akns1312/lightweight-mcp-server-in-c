#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "vswitch-idl.h"
#include "ovsdb-idl.h"
#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/util.h"
#include "ovs-thread.h"
#include "mcp_server.h"
#include "ovs-rcu.h"
#include "bridge.h"

#define PORT 8080

static int server_fd = -1;
//response helper
static void send_json(int client_fd, int code, const char *status, struct json *json_body)
{
    char *json_str = json_to_string(json_body, 0);

    char response[4096];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "%s",
        code, status, json_str);

    if (write(client_fd, response, strlen(response)) < 0) {
        perror("write");
    }

    free(json_str);
}

//error helper

static void
send_error(int client_fd, int code, const char *status, const char *message)
{
    struct json *err = json_object_create();
    json_object_put_string(err, "error", message);
    send_json(client_fd, code, status, err);
    json_destroy(err);
}

//handlers
static void handle_get_ports(int client_fd, struct ovsdb_idl *idl)
{
    if (!idl || !ovsdb_idl_has_ever_connected(idl)) {
        send_error(client_fd, 503, "Service Unavailable", "OVSDB not ready");
        return;
    }

    struct json *result = json_object_create();
    json_object_put_string(result, "action", "switch.get_ports");

    struct json *iface_array = json_array_create_empty();

    const struct ovsrec_bridge *br;

    OVSREC_BRIDGE_FOR_EACH (br, idl) {
        if (!br || !br->ports) continue;

        for (size_t i = 0; i < br->n_ports; i++) {
            struct ovsrec_port *port = br->ports[i];
            if (!port || !port->interfaces) continue;

            for (size_t j = 0; j < port->n_interfaces; j++) {
                struct ovsrec_interface *iface = port->interfaces[j];
                if (!iface) continue;

                struct json *entry = json_object_create();

                json_object_put_string(entry, "name",
                    iface->name ? iface->name : "unknown");

                json_object_put_string(entry, "type",
                    iface->type ? iface->type : "system");

                json_object_put_string(entry, "bridge",
                    br->name ? br->name : "unknown");

                json_array_add(iface_array, entry);
            }
        }
    }

    json_object_put(result, "data", iface_array);

    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void
handle_get_flows(int client_fd)
{
    char *flows_str = bridge_get_all_flows();

    struct json *result = json_object_create();
    json_object_put_string(result, "tool", "get_flows");
    json_object_put_string(result, "flows", flows_str ? flows_str : "");

    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
    free(flows_str);
}

static void
handle_get_port_stats(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "tool", "get_port_stats");
    json_object_put(result, "stats", bridge_get_port_stats());
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void
handle_set_vlan(int client_fd, struct json *arguments)
{
    struct json *port_item = shash_find_data(arguments->object, "port");
    if (!port_item || port_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing port argument");
        return;
    }

    struct json *vlan_item = shash_find_data(arguments->object, "vlan");
    if (!vlan_item || vlan_item->type != JSON_INTEGER) {
        send_error(client_fd, 400, "Bad Request", "missing vlan argument");
        return;
    }

    const char *port_name = json_string(port_item);
    int64_t vlan_id = vlan_item->integer;

    int ret = bridge_set_vlan(port_name, vlan_id);
    if (ret != 0) {
        send_error(client_fd, 404, "Not Found", "port not found");
        return;
    }

    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "set_vlan");
    json_object_put_string(result, "port",   port_name);
    json_object_put(result, "vlan",
                    json_integer_create(vlan_id));
    json_object_put_string(result, "status", "ok");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void
handle_set_port_state(int client_fd, struct json *arguments)
{

    struct json *port_item = shash_find_data(arguments->object, "port");
    if (!port_item || port_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing port argument");
        return;
    }

    struct json *state_item = shash_find_data(arguments->object, "state");
    if (!state_item || state_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing state argument");
        return;
    }

    const char *port_name = json_string(port_item);
    const char *state_str = json_string(state_item);

    bool up;
    if (strcmp(state_str, "up") == 0) {
        up = true;
    } else if (strcmp(state_str, "down") == 0) {
        up = false;
    } else {
        send_error(client_fd, 400, "Bad Request",
                   "state must be 'up' or 'down'");
        return;
    }

    int ret = bridge_set_port_state(port_name, up);
    if (ret != 0) {
        send_error(client_fd, 404, "Not Found", "port not found");
        return;
    }

    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "set_port_state");
    json_object_put_string(result, "port",   port_name);
    json_object_put_string(result, "state",  state_str);
    json_object_put_string(result, "status", "ok");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void
handle_get_tools(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "action", "switch.get_tools");

    struct json *tools_array = json_array_create_empty();

    // get_ports tool
    struct json *get_ports_tool = json_object_create();
    json_object_put_string(get_ports_tool, "name", "get_ports");
    json_object_put_string(get_ports_tool, "description", 
        "Get the list of all ports and interfaces on the OVS switch. "
        "Returns each interface's name, type, and which bridge it belongs to.");
    struct json *get_ports_args = json_object_create();
    json_object_put(get_ports_tool, "arguments", get_ports_args);
    json_array_add(tools_array, get_ports_tool);

    // get_flows tool
    struct json *get_flows_tool = json_object_create();
    json_object_put_string(get_flows_tool, "name", "get_flows");
    json_object_put_string(get_flows_tool, "description",
        "Get the current OpenFlow flow table from the OVS switch. "
        "Returns the full flow table including match rules, actions, "
        "packet counts, and byte counts per flow.");
    struct json *get_flows_args = json_object_create();
    json_object_put(get_flows_tool, "arguments", get_flows_args);
    json_array_add(tools_array, get_flows_tool);

    // get_port_stats tool
    struct json *get_port_stats_tool = json_object_create();
    json_object_put_string(get_port_stats_tool, "name", "get_port_stats");
    json_object_put_string(get_port_stats_tool, "description",
        "Get live traffic statistics for all ports on the OVS switch. "
        "Returns per-interface counters including rx_packets, tx_packets, "
        "rx_bytes, tx_bytes, rx_errors, tx_errors, rx_dropped, tx_dropped.");
    struct json *get_port_stats_args = json_object_create();
    json_object_put(get_port_stats_tool, "arguments", get_port_stats_args);
    json_array_add(tools_array, get_port_stats_tool);

    // set_vlan tool
    struct json *set_vlan_tool = json_object_create();
    json_object_put_string(set_vlan_tool, "name", "set_vlan");
    json_object_put_string(set_vlan_tool, "description",
        "Set the VLAN tag on a specific port. Writes the VLAN tag to OVSDB "
        "and immediately applies the configuration to the datapath.");
    struct json *set_vlan_args = json_object_create();
    json_object_put_string(set_vlan_args, "port", "string (required): The name of the port to configure (e.g. 'eth0', 'test-port')");
    json_object_put_string(set_vlan_args, "vlan", "integer (required): The VLAN ID to assign (1-4094)");
    json_object_put(set_vlan_tool, "arguments", set_vlan_args);
    json_array_add(tools_array, set_vlan_tool);

    // set_port_state tool
    struct json *set_port_state_tool = json_object_create();
    json_object_put_string(set_port_state_tool, "name", "set_port_state");
    json_object_put_string(set_port_state_tool, "description",
        "Enable or disable a port on the OVS switch. Sets the NETDEV_UP flag "
        "on the port's underlying network device, taking effect immediately.");
    struct json *set_port_state_args = json_object_create();
    json_object_put_string(set_port_state_args, "port", "string (required): The name of the port (e.g. 'eth0', 'test-port')");
    json_object_put_string(set_port_state_args, "state", "string (required): Either 'up' to enable or 'down' to disable");
    json_object_put(set_port_state_tool, "arguments", set_port_state_args);
    json_array_add(tools_array, set_port_state_tool);

    json_object_put(result, "tools", tools_array);

    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

//dispatcher

static void mcp_dispatch(int client_fd, const char *body,struct ovsdb_idl *idl)
{
    //parse the JSON body
    struct json *request = json_from_string(body);
    if (!request || request->type != JSON_OBJECT) {
        send_error(client_fd, 400, "Bad Request", "invalid JSON");
        json_destroy(request);
        return;
    }

    // extract "tool" field
    struct json *tool_item = shash_find_data(request->object, "tool");
    if (!tool_item || tool_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing tool field");
        json_destroy(request);
        return;
    }

    const char *tool = json_string(tool_item);
    printf("MCP tool: %s\n", tool);

    struct json *arguments = shash_find_data(request->object, "arguments");

    // route to handler
    if (strcmp(tool, "get_tools") == 0) {
        handle_get_tools(client_fd);
    } else if (strcmp(tool, "get_ports") == 0) {
        handle_get_ports(client_fd,idl);
    } else if (strcmp(tool, "get_flows") == 0) {
        handle_get_flows(client_fd);
    } else if (strcmp(tool, "get_port_stats") == 0) {
        handle_get_port_stats(client_fd);
    } else if (strcmp(tool, "set_vlan") == 0) {
        if (!arguments || arguments->type != JSON_OBJECT) {
            send_error(client_fd, 400, "Bad Request", "missing arguments");
        } else {
            handle_set_vlan(client_fd, arguments);
        }
    } else if (strcmp(tool, "set_port_state") == 0) {
        if (!arguments || arguments->type != JSON_OBJECT) {
            send_error(client_fd, 400, "Bad Request", "missing arguments");
        } else {
            handle_set_port_state(client_fd, arguments);
        }
    } else {
        send_error(client_fd, 404, "Not Found", "unknown tool");
    }

    json_destroy(request);
}



void mcp_server_init(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    printf("MCP server started on port %d\n", PORT);
}

void mcp_server_run(struct ovsdb_idl *idl)
{
    char buffer[4096];
    char method[16], path[256];

    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        return;
    }

    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[n] = '\0';

    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        close(client_fd);
        return;
    }

    if (strcmp(method, "POST") != 0 || strcmp(path, "/mcp") != 0) {
        send_error(client_fd, 404, "Not Found", "not found");
        close(client_fd);
        return;
    }

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        // Handle LF-only separators (Python requests library uses \n\n)
        body = strstr(buffer, "\n\n");
        if (body) {
            body += 2;
        } else {
            send_error(client_fd, 400, "Bad Request", "no body");
            close(client_fd);
            return;
        }
    }

    mcp_dispatch(client_fd, body, idl);
    close(client_fd);
}

void mcp_server_close(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        printf("MCP server stopped\n");
    }
}