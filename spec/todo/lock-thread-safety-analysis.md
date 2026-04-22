# Non-thread-safe objects under 7 locks

Date: 2026-04-22

1. Lock at src/subscription_manager/subscription_orchestrator.cpp:169
- callback on_session_changed_

2. Lock at src/subscription_manager/subscription_orchestrator.cpp:257
- callback on_session_changed_

3. Lock at src/message_router/message_router.cpp:35
- callback is_online_
- callback deliver_

4. Lock at src/message_router/message_router.cpp:164
- callback deliver_
- callback on_offline_queue_changed_

5. Lock at src/message_router/message_router.cpp:214
- callback on_offline_queue_changed_

6. Lock at src/message_router/message_router.cpp:242
- callback is_online_
- callback deliver_

7. Lock at src/message_router/inbound_publish_processor.cpp:58
- callback on_retained_changed_
