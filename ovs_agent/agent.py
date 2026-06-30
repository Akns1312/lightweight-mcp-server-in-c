import requests
from google.adk.agents import Agent

OVS_MCP_URL = "http://localhost:8080/mcp"
AP_MCP_URL  = "http://localhost:8081/ap"
TIMEOUT = 10


def ovs_mcp(tool: str, arguments: dict = None) -> dict:
    """
    Single entry point to the OVS MCP server.
    Communicates with the Open vSwitch daemon.

    Args:
        tool: The name of the tool to invoke.
              Call "get_tools" first to discover all available tools.
        arguments: Optional dictionary of arguments required by the tool.

    Returns:
        JSON response from the OVS MCP server.

    Workflow:
    1. First call: ovs_mcp(tool="get_tools") → discover available tools
    2. Then call:  ovs_mcp(tool="<tool_name>", arguments={...})
    """
    payload = {"tool": tool}
    if arguments:
        payload["arguments"] = arguments
    try:
        r = requests.post(OVS_MCP_URL, json=payload, timeout=TIMEOUT)
        print(f"DEBUG OVS: status={r.status_code} body={r.text[:200]}")
        return r.json()
    except requests.exceptions.ConnectionError:
        return {"error": "Cannot reach OVS MCP server.",
                "hint": "Make sure ovs-vswitchd is running."}
    except requests.exceptions.Timeout:
        return {"error": "OVS MCP server timed out."}
    except Exception as e:
        return {"error": str(e)}


def ap_mcp(tool: str, arguments: dict = None) -> dict:
    """
    Single entry point to the AP MCP server.
    Communicates with the WiFi Access Point.

    Args:
        tool: The name of the tool to invoke.
              Call "get_tools" first to discover all available tools.
        arguments: Optional dictionary of arguments required by the tool.

    Returns:
        JSON response from the AP MCP server.

    Available tools (discover with get_tools):
    - get_ap_info      : SSID, channel, tx_power, client_count, health
    - set_channel      : Change WiFi channel (arguments: {"channel": int})
    - set_tx_power     : Change TX power in dBm (arguments: {"tx_power": int})
    - set_ssid         : Change WiFi name (arguments: {"ssid": string})
    - get_client_count : Number of connected clients

    Workflow:
    1. First call: ap_mcp(tool="get_tools") → discover available tools
    2. Then call:  ap_mcp(tool="<tool_name>", arguments={...})
    """
    payload = {"tool": tool}
    if arguments:
        payload["arguments"] = arguments
    try:
        r = requests.post(AP_MCP_URL, json=payload, timeout=TIMEOUT)
        print(f"DEBUG AP: status={r.status_code} body={r.text[:200]}")
        return r.json()
    except requests.exceptions.ConnectionError:
        return {"error": "Cannot reach AP MCP server.",
                "hint": "Make sure ovs-vswitchd is running on port 8081."}
    except requests.exceptions.Timeout:
        return {"error": "AP MCP server timed out."}
    except Exception as e:
        return {"error": str(e)}


root_agent = Agent(
    name="network_agent",
    model="gemini-3.1-flash-lite",
    description=(
        "An AI assistant for managing both an Open vSwitch (OVS) "
        "software switch and a WiFi Access Point in real time."
    ),
    instruction="""
    You are an expert network engineer assistant managing two systems:
    1. An Open vSwitch (OVS) software switch via ovs_mcp tool
    2. A WiFi Access Point (AP) via ap_mcp tool

    ## Your two tools

    **ovs_mcp(tool, arguments)** — controls the OVS wired switch
    **ap_mcp(tool, arguments)**  — controls the WiFi Access Point

    ## Two-stage workflow for EACH tool

    **Stage 1: Tool Discovery**
    - For OVS: call ovs_mcp(tool="get_tools")
    - For AP:  call ap_mcp(tool="get_tools")
    - This returns all available tools with descriptions and arguments

    **Stage 2: Tool Execution**
    - Call the appropriate tool with the required arguments

    ## Which tool to use

    - Questions about ports, flows, VLANs, port state → use ovs_mcp
    - Questions about WiFi, SSID, channel, signal, clients → use ap_mcp
    - Questions about overall network health → use BOTH

    ## Behavior guidelines

    0. Only use tools when necessary. General networking questions
       can be answered directly without calling any tools.

    1. Always discover tools first using get_tools before executing.

    2. After receiving results explain them clearly in plain English.

    3. For SET operations confirm what changed and what the result was.

    4. If a server returns an error tell the user clearly what went wrong.

    5. Keep responses concise. Use bullet points for multiple items.

    ## Example workflows

    User: "How is my network doing overall?"
    → Call ovs_mcp(tool="get_tools")
    → Call ovs_mcp(tool="get_ports")
    → Call ovs_mcp(tool="get_port_stats")
    → Call ap_mcp(tool="get_tools")
    → Call ap_mcp(tool="get_ap_info")
    → Summarize both wired and wireless status

    User: "Change the WiFi channel to 40"
    → Call ap_mcp(tool="get_tools")
    → Call ap_mcp(tool="set_channel", arguments={"channel": 40})
    → Confirm the change

    User: "What ports are on the switch?"
    → Call ovs_mcp(tool="get_tools")
    → Call ovs_mcp(tool="get_ports")
    → Present the port list
    """,
    tools=[
        ovs_mcp,
        ap_mcp,
    ],
)