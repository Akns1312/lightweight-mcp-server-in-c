===========================
Lightweight MCP Server in C
===========================

Module 1 & 2 – Basic MCP Server Integration
============================================

Files Added
-----------

* ``vswitchd/mcp_server.c``
* ``vswitchd/mcp_server.h``

Files Modified
--------------

* ``vswitchd/ovs-vswitchd.c``
    * Called ``mcp_server_init()`` during startup.
    * Called ``mcp_server_run()`` inside the main loop.
    * Called ``mcp_server_close()`` during shutdown.

* ``vswitchd/automake.mk``
    * Added ``mcp_server.c`` so it gets compiled.

Functionality
-------------

* **Server Port:** ``8080``
* **Endpoint:** ``POST /mcp``
* **Response:** ``{"status": "ok"}``

Test Endpoints
--------------

**Basic MCP Endpoint**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp

Expected output:

.. code-block:: json

    {"status": "ok"}

---

Module 3 – MCP Dispatcher
==========================

Files Modified
--------------

* ``vswitchd/mcp_server.c``
    * Added JSON body parsing using OVS built-in JSON library
      (``openvswitch/json.h``).
    * Added ``mcp_dispatch()`` to route requests to handlers based
      on the ``tool`` field.
    * Added ``send_json()`` helper to build and send JSON responses.
    * Added ``send_error()`` helper for error responses.
    * Added three tool handler stubs.

Functionality
-------------

* **Server Port:** ``8080``
* **Endpoint:** ``POST /mcp``
* **Request Format:**

  .. code-block:: json

      {"tool": "get_ports"}

* **Tools Supported:**

  +--------------------+----------------------------------+
  | Tool               | Description                      |
  +====================+==================================+
  | ``get_ports``      | Returns list of switch ports     |
  +--------------------+----------------------------------+
  | ``get_flows``      | Returns current flow table       |
  +--------------------+----------------------------------+
  | ``get_port_stats`` | Returns counters for a port      |
  +--------------------+----------------------------------+

* **JSON Library:** OVS built-in ``openvswitch/json.h`` — no
  external dependencies.

Dispatcher Logic
----------------

.. code-block:: text

    POST /mcp
         │
         ▼
    parse HTTP body
         │
         ▼
    json_from_string() → struct json
         │
         ▼
    extract "tool" field via shash_find_data()
         │
         ▼
    strcmp(tool, "get_ports")      → handle_get_ports()
    strcmp(tool, "get_flows")      → handle_get_flows()
    strcmp(tool, "get_port_stats") → handle_get_port_stats()
    unknown tool                   → 404 error

Error Handling
--------------

+------------------------------+------+---------------------------+
| Condition                    | Code | Response                  |
+==============================+======+===========================+
| Invalid JSON body            | 400  | ``{"error":"invalid       |
|                              |      | JSON"}``                  |
+------------------------------+------+---------------------------+
| Missing ``tool`` field       | 400  | ``{"error":"missing tool  |
|                              |      | field"}``                 |
+------------------------------+------+---------------------------+
| Unknown tool name            | 404  | ``{"error":"unknown       |
|                              |      | tool"}``                  |
+------------------------------+------+---------------------------+
| Wrong method or path         | 404  | ``{"error":"not found"}`` |
+------------------------------+------+---------------------------+

Test Endpoints
--------------

**get_ports (stub)**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_ports"}'

Expected output:

.. code-block:: json

    {"tool":"get_ports","result":"stub"}

**get_flows (stub)**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_flows"}'

Expected output:

.. code-block:: json

    {"tool":"get_flows","result":"stub"}

**get_port_stats (stub)**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_port_stats"}'

Expected output:

.. code-block:: json

    {"tool":"get_port_stats","result":"stub"}

**Unknown tool**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "unknown"}'

Expected output:

.. code-block:: json

    {"error":"unknown tool"}

**Bad JSON**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d 'not json'

Expected output:

.. code-block:: json

    {"error":"invalid JSON"}

---

Module 4 – Connect MCP to OVS Internals
========================================

Files Modified
--------------

* ``vswitchd/mcp_server.c``
    * Implemented ``handle_get_ports()`` using OVSDB IDL —
      traverses bridge → port → interface hierarchy via
      ``OVSREC_BRIDGE_FOR_EACH``.
    * Implemented ``handle_get_flows()`` calling
      ``bridge_get_all_flows()``.
    * Implemented ``handle_get_port_stats()`` calling
      ``bridge_get_port_stats()``.

* ``vswitchd/bridge.c``
    * Added ``bridge_get_idl()`` to expose the internal IDL pointer.
    * Added ``bridge_get_all_flows()`` to walk all bridges and dump
      flow tables using ``ofproto_get_all_flows()``.
    * Added ``bridge_get_port_stats()`` to walk bridge → port →
      interface and call ``netdev_get_stats()`` per interface.

* ``vswitchd/bridge.h``
    * Declared ``bridge_get_idl()``.
    * Declared ``bridge_get_all_flows()``.
    * Declared ``bridge_get_port_stats()``.

* ``vswitchd/ovs-vswitchd.c``
    * Moved ``bridge_get_idl()`` call to after ``bridge_init()``
      so the IDL pointer is valid before use.

Functionality
-------------

All three tools now return real OVS data instead of stubs.

+--------------------+-----------------------------+------------------------+
| Tool               | Data Source                 | Access Method          |
+====================+=============================+========================+
| ``get_ports``      | OVSDB IDL (public)          | Direct from            |
|                    |                             | ``mcp_server.c``       |
+--------------------+-----------------------------+------------------------+
| ``get_flows``      | ``br->ofproto`` (private)   | Via                    |
|                    |                             | ``bridge_get_all_      |
|                    |                             | flows()``              |
+--------------------+-----------------------------+------------------------+
| ``get_port_stats`` | ``iface->netdev`` (private) | Via                    |
|                    |                             | ``bridge_get_port_     |
|                    |                             | stats()``              |
+--------------------+-----------------------------+------------------------+

Test Endpoints
--------------

**get_ports**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_ports"}'

Expected output:

.. code-block:: json

    {"action":"switch.get_ports","data":[{"name":"br0","bridge":"br0","type":"internal"}]}

**get_flows**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_flows"}'

Expected output:

.. code-block:: json

    {"flows":"bridge: br0\nduration=5s, n_packets=0, n_bytes=0, priority=0,actions=NORMAL\ntable_id=254, duration=5s, n_packets=0, n_bytes=0, priority=2,recirc_id=0,actions=drop\n","tool":"get_flows"}

**get_port_stats**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_port_stats"}'

Expected output:

.. code-block:: json

    {"stats":[{"name":"br0","tx_packets":0,"rx_errors":0,"tx_dropped":0,"bridge":"br0","rx_packets":0,"tx_bytes":0,"rx_dropped":0,"tx_errors":0,"rx_bytes":0}],"tool":"get_port_stats"}

---

Module 5 – SET Operations
==========================

Files Modified
--------------

* ``vswitchd/mcp_server.c``
    * Added ``handle_set_vlan()`` — extracts ``port`` and ``vlan``
      arguments, calls ``bridge_set_vlan()``, returns result.
    * Added ``handle_set_port_state()`` — extracts ``port`` and
      ``state`` arguments (``"up"`` or ``"down"``), calls
      ``bridge_set_port_state()``, returns result.
    * Added both tools to ``mcp_dispatch()``.

* ``vswitchd/bridge.c``
    * Added ``bridge_set_vlan()`` — finds port in OVSDB via
      ``OVSREC_PORT_FOR_EACH``, writes tag using
      ``ovsrec_port_set_tag()``, commits transaction, then calls
      ``port_configure()`` to push change into ofproto/datapath
      immediately.
    * Added ``bridge_set_port_state()`` — finds port via
      ``port_lookup()``, walks all interfaces on the port, calls
      ``netdev_turn_flags_on()`` or ``netdev_turn_flags_off()``
      with ``NETDEV_UP`` flag.
    * Registered ``ovsrec_port_col_tag`` with
      ``ovsdb_idl_add_column()`` in ``bridge_init()`` to allow
      writes under ``ovsdb_idl_verify_write_only()`` mode.

* ``vswitchd/bridge.h``
    * Declared ``bridge_set_vlan()``.
    * Declared ``bridge_set_port_state()``.

Functionality
-------------

* **Goal:** Allow MCP to modify live switch configuration.
* **Tools added:**

  +----------------------+------------------------------------------+
  | Tool                 | Description                              |
  +======================+==========================================+
  | ``set_vlan``         | Sets VLAN tag on a port and applies      |
  |                      | config to datapath immediately           |
  +----------------------+------------------------------------------+
  | ``set_port_state``   | Enables or disables a port by setting    |
  |                      | the ``NETDEV_UP`` flag on its netdev     |
  +----------------------+------------------------------------------+

Implementation Notes
--------------------

**set_vlan — Two Step Process:**

.. code-block:: text

    set_vlan (port, vlan_id)
         │
         ▼
    Step 1: ovsrec_port_set_tag() + ovsdb_idl_txn_commit_block()
            → writes tag value into OVSDB database
         │
         ▼
    Step 2: port_configure()
            → reads config from OVSDB and pushes into ofproto/datapath
            → change takes effect immediately without waiting for
              next reconfigure cycle

**set_port_state — netdev flags:**

.. code-block:: text

    set_port_state (port, up/down)
         │
         ▼
    port_lookup() → struct port *
         │
         ▼
    LIST_FOR_EACH iface in port->ifaces
         │
         ▼
    netdev_turn_flags_on(iface->netdev, NETDEV_UP)   ← if "up"
    netdev_turn_flags_off(iface->netdev, NETDEV_UP)  ← if "down"

**Bug Fixed — Port:tag column not writable:**

``ovsdb_idl_verify_write_only()`` is set in ``bridge_init()``,
which requires every writable column to be explicitly registered
via ``ovsdb_idl_add_column()``. The ``Port:tag`` column was only
registered with ``ovsdb_idl_omit_alert()`` but not
``ovsdb_idl_add_column()``, causing the error:

.. code-block:: text

    Bug: Attempt to write to a read/write column (Port:tag)
    when explicitly configured not to.

Fix: added ``ovsdb_idl_add_column(idl, &ovsrec_port_col_tag)``
in ``bridge_init()``.

Error Handling
--------------

+------------------------------+------+------------------------------+
| Condition                    | Code | Response                     |
+==============================+======+==============================+
| Missing ``port`` argument    | 400  | ``{"error":"missing port     |
|                              |      | argument"}``                 |
+------------------------------+------+------------------------------+
| Missing ``vlan`` argument    | 400  | ``{"error":"missing vlan     |
|                              |      | argument"}``                 |
+------------------------------+------+------------------------------+
| Missing ``state`` argument   | 400  | ``{"error":"missing state    |
|                              |      | argument"}``                 |
+------------------------------+------+------------------------------+
| Invalid state value          | 400  | ``{"error":"state must be    |
|                              |      | 'up' or 'down'"}``           |
+------------------------------+------+------------------------------+
| Port not found               | 404  | ``{"error":"port not         |
|                              |      | found"}``                    |
+------------------------------+------+------------------------------+
| Transaction failed           | 500  | ``{"error":"port not         |
|                              |      | found"}``                    |
+------------------------------+------+------------------------------+

Test Endpoints
--------------

**set_vlan**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "set_vlan", "arguments": {"port": "test-port", "vlan": 100}}'

Expected output:

.. code-block:: json

    {"port":"test-port","status":"ok","vlan":100,"tool":"set_vlan"}

Verify:

.. code-block:: bash

    sudo ovs-vsctl get port test-port tag

Expected output:

.. code-block:: text

    100

**set_port_state — disable**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "set_port_state", "arguments": {"port": "test-port", "state": "down"}}'

Expected output:

.. code-block:: json

    {"state":"down","port":"test-port","status":"ok","tool":"set_port_state"}

Verify:

.. code-block:: bash

    sudo ovs-vsctl get interface test-port admin_state

Expected output:

.. code-block:: text

    down

**set_port_state — enable**

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "set_port_state", "arguments": {"port": "test-port", "state": "up"}}'

Expected output:

.. code-block:: json

    {"state":"up","port":"test-port","status":"ok","tool":"set_port_state"}

Verify:

.. code-block:: bash

    sudo ovs-vsctl get interface test-port admin_state

Expected output:

.. code-block:: text

    up

---

Setup and Run
=============

Build OVS
---------

.. code-block:: bash

    ./boot.sh
    ./configure
    make -j4
    sudo make install

Start OVS
---------

Start database:

.. code-block:: bash

    sudo ovsdb-server \
    --remote=punix:/usr/local/var/run/openvswitch/db.sock \
    --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
    --pidfile --detach

Initialize DB:

.. code-block:: bash

    sudo ovs-vsctl --no-wait init

Start switch:

.. code-block:: bash

    sudo ovs-vswitchd --pidfile --detach

Create a test bridge:

.. code-block:: bash

    sudo ovs-vsctl add-br br0