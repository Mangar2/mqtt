Skill for YAHA client architecture.
Caveman language. No format chars. No unneeded words.

Purpose
Define strict main file shape for YAHA clients.
Keep generic logic in generic modules.
Keep main thin.

Core rule
Main says only mach.
Main composes.
Main does not implement runtime policy.
Main does not implement reconnect policy.
Main does not implement signal policy.
Main does not implement generic io parsing policy.

Required architecture
One domain component implements IMqttComponent.
Generic mqtt client takes IMqttComponent only.
Generic runtime orchestrator takes mqtt client and IMqttComponent.
Main wires instances and calls one run entry.

Main file must do
Parse cli args for this app.
Load config source for this app.
Map config document to app internal config using app mapping function.
Create component instance from mapped config.
Create mqtt client instance from mapped mqtt config plus generic transport.
Create generic runtime orchestrator with mqtt client and component.
Call run once.
Return exit code.

Main file must not do
No signal handler code.
No reconnect loops.
No sleep poll loops.
No start stop lifecycle choreography code.
No generic transport diagnostics policy.
No generic benchmark or load mode logic.
No duplicated ini parser logic.

Generic module responsibilities
ini module: file io parse document typed helper.
mqtt_client module: session loop reconnect keepalive subscribe replay inbound dispatch.
mqtt_client_runtime module: signal handling wait loop shutdown orchestration.
message_store_client module: domain mapping from IniDocument to MessageStore config and runtime config.

Composition order
Create config values.
Create component.
Create mqtt client with component through IMqttComponent.
Create runtime.
Run runtime.

Shutdown order owned by generic runtime
Stop mqtt client.
Stop component via IMqttComponent close.

Required checks for new YAHA client main
Check main has no manual signal handlers.
Check main has no while sleep shutdown loop.
Check main has no duplicated generic parser code.
Check main uses IMqttComponent boundary only for runtime orchestration.
Check main file size stays small and orchestration only.

Anti patterns
Moving generic code from app class into main is forbidden.
Copying generic code between client mains is forbidden.
Adding app specific hooks for generic runtime concerns is forbidden when interface can cover it.

Output contract for reviews
State what is generic.
State what is app specific.
List violations by file path.
Give minimal refactor path to restore boundary.
