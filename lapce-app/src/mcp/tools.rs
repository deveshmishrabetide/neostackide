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
        McpTool {
            name: "configure_asset".to_string(),
            description: "Read and configure properties on any UE5 asset using reflection. Supports Materials, Blueprints, Widgets, and their subobjects (components, child widgets).".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Asset name (e.g., 'M_BaseMaterial', 'BP_Enemy', 'WBP_MainMenu')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/Materials'). Default: /Game."
                    },
                    "subobject": {
                        "type": "string",
                        "description": "Target a specific subobject (widget name in Widget BP, component name in BP)."
                    },
                    "list_properties": {
                        "type": "boolean",
                        "description": "List all editable properties on the asset/subobject."
                    },
                    "get": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Property names to read values from."
                    },
                    "changes": {
                        "type": "array",
                        "description": "Property changes to apply.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "property": { "type": "string", "description": "Property name." },
                                "value": { "type": "string", "description": "New value (use UE format: 'BLEND_Translucent', 'True', '(X=1,Y=2,Z=3)')." }
                            },
                            "required": ["property", "value"]
                        }
                    }
                },
                "required": ["name"]
            }),
        },
        McpTool {
            name: "edit_behavior_tree".to_string(),
            description: "Edit Behavior Trees and Blackboards. Add/remove composite nodes, tasks, decorators, services, and blackboard keys.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Asset name (e.g., 'BT_EnemyAI', 'BB_EnemyData')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path (e.g., '/Game/AI'). Default: /Game."
                    },
                    "set_blackboard": {
                        "type": "string",
                        "description": "Set the blackboard asset for a behavior tree."
                    },
                    "add_composite": {
                        "type": "array",
                        "description": "Composite nodes to add (Selector, Sequence, Parallel).",
                        "items": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "description": "Selector, Sequence, Parallel, SimpleParallel." },
                                "name": { "type": "string" },
                                "parent": { "type": "string", "description": "Parent composite name (empty = root)." },
                                "index": { "type": "integer", "description": "Child index (-1 = append)." }
                            },
                            "required": ["type"]
                        }
                    },
                    "add_task": {
                        "type": "array",
                        "description": "Task nodes to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "description": "Task class (MoveTo, Wait, RunBehavior, etc.)." },
                                "name": { "type": "string" },
                                "parent": { "type": "string", "description": "Parent composite name." },
                                "index": { "type": "integer" }
                            },
                            "required": ["type", "parent"]
                        }
                    },
                    "add_decorator": {
                        "type": "array",
                        "description": "Decorators to add to nodes.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "description": "Decorator class (Blackboard, CoolDown, Loop, etc.)." },
                                "name": { "type": "string" },
                                "target": { "type": "string", "description": "Target node name to attach to." }
                            },
                            "required": ["type", "target"]
                        }
                    },
                    "add_service": {
                        "type": "array",
                        "description": "Services to add to composites.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "description": "Service class (DefaultFocus, RunEQS, etc.)." },
                                "name": { "type": "string" },
                                "target": { "type": "string", "description": "Target composite name." }
                            },
                            "required": ["type", "target"]
                        }
                    },
                    "remove_node": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Node names to remove."
                    },
                    "add_key": {
                        "type": "array",
                        "description": "Blackboard keys to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "type": { "type": "string", "description": "Bool, Int, Float, String, Name, Vector, Rotator, Object, Class, Enum." },
                                "base_class": { "type": "string", "description": "For Object/Class types." },
                                "category": { "type": "string" },
                                "instance_synced": { "type": "boolean" }
                            },
                            "required": ["name", "type"]
                        }
                    },
                    "remove_key": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Blackboard key names to remove."
                    }
                },
                "required": ["name"]
            }),
        },
        McpTool {
            name: "edit_data_structure".to_string(),
            description: "Edit User Defined Structs, Enums, and DataTables. Add/remove/modify fields, values, and rows.".to_string(),
            input_schema: json!({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Asset name (e.g., 'S_PlayerData', 'E_GameState', 'DT_Items')."
                    },
                    "path": {
                        "type": "string",
                        "description": "Asset path. Default: /Game."
                    },
                    "add_fields": {
                        "type": "array",
                        "description": "Struct fields to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "type": { "type": "string", "description": "Boolean, Integer, Float, String, Vector, Rotator, Transform, Object, etc." },
                                "default_value": { "type": "string" },
                                "description": { "type": "string" }
                            },
                            "required": ["name", "type"]
                        }
                    },
                    "remove_fields": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Struct field names to remove."
                    },
                    "modify_fields": {
                        "type": "array",
                        "description": "Struct fields to modify.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string", "description": "Current field name." },
                                "new_name": { "type": "string" },
                                "type": { "type": "string" },
                                "default_value": { "type": "string" },
                                "description": { "type": "string" }
                            },
                            "required": ["name"]
                        }
                    },
                    "add_values": {
                        "type": "array",
                        "description": "Enum values to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": { "type": "string" },
                                "display_name": { "type": "string" }
                            },
                            "required": ["name"]
                        }
                    },
                    "remove_values": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Enum value names to remove."
                    },
                    "modify_values": {
                        "type": "array",
                        "description": "Enum values to modify.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "index": { "type": "integer" },
                                "display_name": { "type": "string" }
                            },
                            "required": ["index"]
                        }
                    },
                    "add_rows": {
                        "type": "array",
                        "description": "DataTable rows to add.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "row_name": { "type": "string" },
                                "values": { "type": "object", "description": "Column name -> value mapping." }
                            },
                            "required": ["row_name", "values"]
                        }
                    },
                    "remove_rows": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "DataTable row names to remove."
                    },
                    "modify_rows": {
                        "type": "array",
                        "description": "DataTable rows to modify.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "row_name": { "type": "string" },
                                "values": { "type": "object" }
                            },
                            "required": ["row_name", "values"]
                        }
                    }
                },
                "required": ["name"]
            }),
        },
    ]
}

/// Get a tool by name
pub fn get_tool_by_name(name: &str) -> Option<McpTool> {
    get_all_tools().into_iter().find(|t| t.name == name)
}
