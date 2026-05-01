## user report

es geht immer noch nicht, ich erhalte keine messages und der ping fehler kommt wie voher

## test case

command:
./build/release/yahabrokerconnectorclient test/connector.ini

expected bug signal:
- source side reports repeated ping failure (status 400)
- no source messages are forwarded to receiver

current status:
- reproduces in user environment (reported)
- local agent environment cannot fully validate external hosts yahapi/yaha2 reachability
