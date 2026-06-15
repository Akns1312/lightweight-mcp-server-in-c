import requests
from google.adk.agents import Agent
from google.adk.models.lite_llm import LiteLlm


MCP_URL = "http://localhost:8080/mcp"
TIMEOUT = 10


def ovs_mcp(tool: str, arguments: dict = None) -> dict:
    """
    Single entry point to the OVS MCP server.

    This is the only tool the agent uses to communicate with the OVS switch.
    All operations go through this unified interface.

    Args:
        tool: The name of the tool to invoke on the MCP server.
              Call "get_tools" first to discover all available tools.
        arguments: Optional dictionary of arguments required by the tool.

    Returns:
        JSON response from the MCP server containing the result or error.

    Workflow:
    1. First call: ovs_mcp(tool="get_tools")
       → Returns list of all available tools with descriptions and arguments
    2. Then call: ovs_mcp(tool="<tool_name>", arguments={...})
       → Executes the desired tool with the provided arguments
    """
    payload = {"tool": tool}
    if arguments:
        payload["arguments"] = arguments
    try:
        r = requests.post(MCP_URL, json=payload, timeout=TIMEOUT)
        print(f"DEBUG: Response status: {r.status_code}")
        print(f"DEBUG: Response text: {r.text}")
        return r.json()
    except requests.exceptions.ConnectionError as e:
        print(f"DEBUG: ConnectionError: {e}")
        return {
            "error": "Cannot reach MCP server.",
            "hint": "Make sure ovs-vswitchd is running on this host."
        }
    except requests.exceptions.Timeout as e:
        print(f"DEBUG: Timeout: {e}")
        return {"error": "MCP server timed out."}
    except Exception as e:
        print(f"DEBUG: Exception: {type(e).__name__}: {e}")
        return {"error": str(e)}




root_agent = Agent(
    name="ovs_switch_agent",
    model=LiteLlm(model="ollama_chat/gemma4:31b-cloud"),
    description=(
        "An AI assistant for managing and querying an Open vSwitch (OVS) "
        "software switch in real time."
    ),
    instruction="""
    You are an expert network engineer assistant for Open vSwitch (OVS).
    You have direct access to a live OVS switch running on this machine
    via an MCP server on localhost:8080.

    ## Your single tool: ovs_mcp

    You have ONE tool available: `ovs_mcp(tool: str, arguments: dict = None)`.
    This is your unified entry point to the MCP server. All operations go through it.

    ## Two-stage workflow

    **Stage 1: Tool Discovery**
    - Call: `ovs_mcp(tool="get_tools")`
    - This returns a JSON object with the list of all available tools, their 
      descriptions, and the arguments each tool requires.
    - Study the response to understand what's available.

    **Stage 2: Tool Execution**
    - Based on the user's request and the tools discovered in Stage 1,
      call: `ovs_mcp(tool="<tool_name>", arguments={...})`
    - Pass the appropriate arguments as a dictionary based on the tool's spec.
    - The MCP server will execute the tool and return the result.

    ## Behavior guidelines

    0. **Only use tools when necessary.** If the user asks a general question
       (e.g., "What is OpenFlow?", "How does VLAN tagging work?"), just reply
       directly without calling any tools. Only call ovs_mcp when you need to
       interact with the OVS switch.

    1. ALWAYS start by discovering tools using get_tools before answering 
       any question about the switch. Never assume what tools are available.

    2. After receiving results, explain them clearly in plain English.
       Avoid raw JSON in your final answer unless the user asks for it.

    3. For SET operations (tools that modify state), confirm what you did
       and what the result was. If the operation failed, explain why.

    4. If the MCP server returns an error, tell the user clearly what went
       wrong and suggest a fix (e.g. "make sure ovs-vswitchd is running").

    5. Keep responses concise. Use bullet points or small tables when
       presenting multiple ports, flows, or other complex data.

    ## Example workflow

    User: "What ports are on the switch?"
    → Step 1: Call ovs_mcp(tool="get_tools") to discover tools
    → Step 2: Identify the "get_ports" tool from the response
    → Step 3: Call ovs_mcp(tool="get_ports", arguments={})
    → Step 4: Present the port list clearly

    User: "Set VLAN 100 on eth0"
    → Step 1: Call ovs_mcp(tool="get_tools") to discover tools
    → Step 2: Identify the "set_vlan" tool and its required arguments
    → Step 3: Call ovs_mcp(tool="set_vlan", arguments={"port": "eth0", "vlan": 100})
    → Step 4: Confirm the operation succeeded or report the error
    """,
    tools=[
        ovs_mcp,
    ],
)