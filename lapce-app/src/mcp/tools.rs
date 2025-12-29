//! MCP Tool Definitions
//!
//! UE5 Blueprint/Asset tools exposed via MCP.

use serde_json::json;
use super::types::McpTool;

/// Get all available MCP tools
pub fn get_all_tools() -> Vec<McpTool> {
    vec![
        // =================================================================
        // UE5 Asset Tools (via Bridge)
        // =================================================================
        McpTool {
            name: "create_asset".to_string(),
            description: "Create a new UE5 asset: Blueprint, Widget Blueprint, Animation Blueprint, Material, Behavior Tree, Blackboard, Struct, Enum, DataTable, or text file.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Asset name (e.g., 'BP_Enemy', 'WBP_MainMenu', 'ABP_Character')."
                    },
                    "parent": {
                        "type": "string",
                        "description": "Parent class or asset type. Blueprints: 'Actor', 'Character', 'Pawn', etc. Special: 'Widget', 'AnimInstance', 'Material', 'BehaviorTree', 'Struct', 'Enum', 'DataTable'."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Blueprints'). Default: /Game."
                    },
                    "skeleton": {
                        "type": "string",
                        "description": "For Animation Blueprints, the skeleton asset path."
                    },
                    "content": {
                        "type": "string",
                        "description": "For text files, the content to write."
                    },
                    "fields": {
                        "type": "array",
                        "description": "For Struct: field definitions.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "type": { "type": "string" },
                                "default_value": { "type": "string" },
                                "description": { "type": "string" }
                            },
                            "required": ["name", "type"]
                        }
                    },
                    "values": {
                        "type": "array",
                        "description": "For Enum: value definitions.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "display_name": { "type": "string" }
                            },
                            "required": ["name"]
                        }
                    },
                    "row_struct": {
                        "type": "string",
                        "description": "For DataTable: name or path of the row struct."
                    }
                },
                "required": ["name", "parent"]
            }),
        },
        McpTool {
            name: "read_asset".to_string(),
            description: "Read a UE5 asset's structure. Supports Blueprints, Widget Blueprints, Animation Blueprints, Behavior Trees, Blackboards, Structs, Enums, and DataTables.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Blueprints/BP_Player')."
                    },
                    "include": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "What to include: 'summary', 'components', 'variables', 'functions', 'graphs', 'interfaces'. Default: ['summary']."
                    },
                    "graph": {
                        "type": "string",
                        "description": "Specific graph to read nodes from (e.g., 'EventGraph')."
                    }
                },
                "required": ["name"]
            }),
        },
        McpTool {
            name: "edit_blueprint".to_string(),
            description: "Edit Blueprints, Widget Blueprints, and Animation Blueprints. Add/remove components, variables, functions, widgets, state machines, states, and transitions.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Blueprint name (e.g., 'BP_Player', 'WBP_MainMenu')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Blueprints'). Default: /Game."
                    },
                    "add_components": {
                        "type": "array",
                        "description": "Components to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "class": { "type": "string" },
                                "parent": { "type": "string" }
                            },
                            "required": ["name", "class"]
                        }
                    },
                    "remove_components": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "add_variables": {
                        "type": "array",
                        "description": "Variables to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "type": {
                                    "type": "object",
                                    "properties": {
                                        "base": { "type": "string" },
                                        "container": { "type": "string" },
                                        "subtype": { "type": "string" }
                                    },
                                    "required": ["base"]
                                },
                                "default": { "type": "string" },
                                "category": { "type": "string" },
                                "replicated": { "type": "boolean" },
                                "rep_notify": { "type": "boolean" },
                                "expose_on_spawn": { "type": "boolean" },
                                "private": { "type": "boolean" }
                            },
                            "required": ["name", "type"]
                        }
                    },
                    "remove_variables": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "add_functions": {
                        "type": "array",
                        "description": "Functions to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "inputs": { "type": "array" },
                                "outputs": { "type": "array" },
                                "pure": { "type": "boolean" }
                            },
                            "required": ["name"]
                        }
                    },
                    "remove_functions": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "add_widgets": {
                        "type": "array",
                        "description": "Widgets to add (Widget Blueprints only).",
                        "items": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string" },
                                "name": { "type": "string" },
                                "parent": { "type": "string" }
                            },
                            "required": ["type", "name"]
                        }
                    },
                    "remove_widgets": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "add_state_machines": {
                        "type": "array",
                        "description": "State machines to add (Animation Blueprints only).",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" }
                            },
                            "required": ["name"]
                        }
                    },
                    "add_states": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "state_machine": { "type": "string" }
                            },
                            "required": ["name", "state_machine"]
                        }
                    },
                    "add_transitions": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "state_machine": { "type": "string" },
                                "from_state": { "type": "string" },
                                "to_state": { "type": "string" }
                            },
                            "required": ["state_machine", "from_state", "to_state"]
                        }
                    },
                    "bind_event": {
                        "type": "object",
                        "properties": {
                            "source": { "type": "string" },
                            "event": { "type": "string" },
                            "handler": { "type": "string" }
                        },
                        "required": ["source", "event", "handler"]
                    }
                },
                "required": ["name"]
            }),
        },
        McpTool {
            name: "find_node".to_string(),
            description: "Search for available nodes in a Blueprint graph. Returns spawner IDs for use with edit_graph.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "asset": {
                        "type": "string",
                        "description": "Asset name (e.g., 'BP_Player')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Blueprints'). Default: /Game."
                    },
                    "query": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Search terms (e.g., ['print', 'string'])."
                    },
                    "graph_name": {
                        "type": "string",
                        "description": "Graph name. Default: 'EventGraph'."
                    },
                    "category": {
                        "type": "string",
                        "description": "Filter by category (e.g., 'Math', 'Flow Control')."
                    },
                    "input_type": {
                        "type": "string",
                        "description": "Filter by input pin type."
                    },
                    "output_type": {
                        "type": "string",
                        "description": "Filter by output pin type."
                    },
                    "limit": {
                        "type": "integer",
                        "description": "Maximum results. Default: 20."
                    }
                },
                "required": ["asset", "query"]
            }),
        },
        McpTool {
            name: "edit_graph".to_string(),
            description: "Edit a Blueprint graph: add nodes, create connections with auto-promotion/conversion, set pin values.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "asset": {
                        "type": "string",
                        "description": "Asset name (e.g., 'BP_Player')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Blueprints'). Default: /Game."
                    },
                    "graph_name": {
                        "type": "string",
                        "description": "Graph name. Default: 'EventGraph'."
                    },
                    "add_nodes": {
                        "type": "array",
                        "description": "Nodes to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "id": { "type": "string", "description": "Spawner ID from find_node." },
                                "name": { "type": "string", "description": "Node name for connections." },
                                "pins": { "type": "object", "description": "Pin values." }
                            },
                            "required": ["id"]
                        }
                    },
                    "connections": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Connections: 'NodeA:PinA -> NodeB:PinB'."
                    },
                    "disconnect": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Disconnect pins."
                    },
                    "set_pins": {
                        "type": "array",
                        "description": "Set values on existing nodes.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "node": { "type": "string" },
                                "values": { "type": "object" }
                            },
                            "required": ["node", "values"]
                        }
                    }
                },
                "required": ["asset"]
            }),
        },
    ]
}

/// Get a tool by name
pub fn get_tool_by_name(name: &str) -> Option<McpTool> {
    get_all_tools().into_iter().find(|t| t.name == name)
}
