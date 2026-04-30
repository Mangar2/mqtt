# Bug: sys-monitoring-config

## user report

ich habe jetzt message-store und broker länger laufen. message store sollte auch $SYS meldungen empfangen und speichern. Es werden aber keine $SYS meldungen empfangen oder gespeichert. bitte prüfe in der Reihenfolge: 1. sendet der broker überhaupt $SYS meldungen? 2. nur, wenn das mit ja beantwortet wird: werden die vom message store korrekt subscribed? wenn ja, warum tauchen die dann nicht auf (auch nicht in den dateien, die msgstore speichert)

## scope

### allow
- Broker runtime configuration for monitoring and $SYS periodic publication.
- Message store subscription configuration and startup diagnostics.
- Service startup command and active config file path checks.

### deny
- QoS flow analysis unrelated to $SYS publication.
- Persistence redesign and unrelated message-store internals.
- Performance/load tuning outside the missing $SYS symptom.

## confirmed facts

- Message store startup output confirms explicit subscriptions: [#=>qos1] and [$SYS/#=>qos1].
- Running broker service uses /home/pi/mqtt/broker/broker.ini via -c argument.
- In broker.ini, sys_topic_interval was set, but [monitoring] section header was commented out.
- Broker config loader applies sys_topic_interval only inside section monitoring.

## root cause

sys_topic_interval was configured outside an active [monitoring] section, so the setting was ignored and effective interval stayed disabled.

## fix

- Activate monitoring section in broker config:
  - [monitoring]
  - sys_topic_interval = 30
- Restart yahabroker service.

## resolution

Status: CLOSED

After enabling [monitoring] and restarting the broker, $SYS messages are received by message store.

## prevention

- Keep section header and key together when enabling optional blocks in shipped INI templates.
- Validate active broker config with service cmdline and startup behavior after config edits.
