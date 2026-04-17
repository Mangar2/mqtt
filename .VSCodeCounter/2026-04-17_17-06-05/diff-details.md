# Diff Details

Date : 2026-04-17 17:06:05

Directory c:\\Development\\mqtt\\src

Total : 42 files,  2405 codes, 771 comments, 658 blanks, all 3834 lines

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [src/SPEC.md](/src/SPEC.md) | Markdown | 34 | 0 | 8 | 42 |
| [src/auth/anonymous\_authenticator.cpp](/src/auth/anonymous_authenticator.cpp) | C++ | 5 | 0 | 0 | 5 |
| [src/auth/anonymous\_authenticator.h](/src/auth/anonymous_authenticator.h) | C++ | 0 | 0 | 1 | 1 |
| [src/auth/auth\_error.h](/src/auth/auth_error.h) | C++ | 0 | 1 | 0 | 1 |
| [src/auth/authenticator.cpp](/src/auth/authenticator.cpp) | C++ | 4 | 2 | 1 | 7 |
| [src/auth/authenticator.h](/src/auth/authenticator.h) | C++ | 0 | 0 | 1 | 1 |
| [src/auth/enhanced\_auth\_handler.cpp](/src/auth/enhanced_auth_handler.cpp) | C++ | -14 | 2 | -1 | -13 |
| [src/auth/enhanced\_auth\_handler.h](/src/auth/enhanced_auth_handler.h) | C++ | -3 | -4 | -1 | -8 |
| [src/auth/password\_authenticator.cpp](/src/auth/password_authenticator.cpp) | C++ | 3 | 0 | 0 | 3 |
| [src/auth/password\_authenticator.h](/src/auth/password_authenticator.h) | C++ | -1 | 0 | 1 | 0 |
| [src/auth/test/TEST\_SPEC.md](/src/auth/test/TEST_SPEC.md) | Markdown | 71 | 0 | 21 | 92 |
| [src/auth/test/auth\_test.cpp](/src/auth/test/auth_test.cpp) | C++ | 443 | 18 | 63 | 524 |
| [src/authz/SPEC.md](/src/authz/SPEC.md) | Markdown | 89 | 0 | 40 | 129 |
| [src/authz/acl\_engine.cpp](/src/authz/acl_engine.cpp) | C++ | 76 | 17 | 21 | 114 |
| [src/authz/acl\_engine.h](/src/authz/acl_engine.h) | C++ | 33 | 99 | 16 | 148 |
| [src/authz/acl\_loader.cpp](/src/authz/acl_loader.cpp) | C++ | 46 | 9 | 14 | 69 |
| [src/authz/acl\_loader.h](/src/authz/acl_loader.h) | C++ | 24 | 75 | 12 | 111 |
| [src/authz/acl\_rule.h](/src/authz/acl_rule.h) | C++ | 21 | 32 | 8 | 61 |
| [src/authz/authz\_error.h](/src/authz/authz_error.h) | C++ | 19 | 24 | 9 | 52 |
| [src/authz/test/TEST\_SPEC.md](/src/authz/test/TEST_SPEC.md) | Markdown | 61 | 0 | 23 | 84 |
| [src/authz/test/authz\_test.cpp](/src/authz/test/authz_test.cpp) | C++ | 234 | 25 | 56 | 315 |
| [src/session\_manager/SPEC.md](/src/session_manager/SPEC.md) | Markdown | 96 | 0 | 33 | 129 |
| [src/session\_manager/session\_expiry\_scheduler.cpp](/src/session_manager/session_expiry_scheduler.cpp) | C++ | 39 | 0 | 10 | 49 |
| [src/session\_manager/session\_expiry\_scheduler.h](/src/session_manager/session_expiry_scheduler.h) | C++ | 25 | 49 | 11 | 85 |
| [src/session\_manager/session\_manager.cpp](/src/session_manager/session_manager.cpp) | C++ | 104 | 17 | 28 | 149 |
| [src/session\_manager/session\_manager.h](/src/session_manager/session_manager.h) | C++ | 38 | 75 | 12 | 125 |
| [src/session\_manager/session\_manager\_error.h](/src/session_manager/session_manager_error.h) | C++ | 18 | 21 | 9 | 48 |
| [src/session\_manager/session\_open\_result.h](/src/session_manager/session_open_result.h) | C++ | 7 | 13 | 5 | 25 |
| [src/session\_manager/session\_takeover\_handler.cpp](/src/session_manager/session_takeover_handler.cpp) | C++ | 29 | 2 | 8 | 39 |
| [src/session\_manager/session\_takeover\_handler.h](/src/session_manager/session_takeover_handler.h) | C++ | 18 | 55 | 11 | 84 |
| [src/session\_manager/test/TEST\_SPEC.md](/src/session_manager/test/TEST_SPEC.md) | Markdown | 38 | 0 | 13 | 51 |
| [src/session\_manager/test/session\_manager\_test.cpp](/src/session_manager/test/session_manager_test.cpp) | C++ | 255 | 25 | 46 | 326 |
| [src/will\_manager/SPEC.md](/src/will_manager/SPEC.md) | Markdown | 96 | 0 | 39 | 135 |
| [src/will\_manager/test/TEST\_SPEC.md](/src/will_manager/test/TEST_SPEC.md) | Markdown | 42 | 0 | 11 | 53 |
| [src/will\_manager/test/will\_manager\_test.cpp](/src/will_manager/test/will_manager_test.cpp) | C++ | 255 | 14 | 54 | 323 |
| [src/will\_manager/will\_delay\_timer.cpp](/src/will_manager/will_delay_timer.cpp) | C++ | 27 | 0 | 8 | 35 |
| [src/will\_manager/will\_delay\_timer.h](/src/will_manager/will_delay_timer.h) | C++ | 28 | 48 | 11 | 87 |
| [src/will\_manager/will\_manager\_error.h](/src/will_manager/will_manager_error.h) | C++ | 17 | 21 | 9 | 47 |
| [src/will\_manager/will\_publisher.cpp](/src/will_manager/will_publisher.cpp) | C++ | 60 | 4 | 12 | 76 |
| [src/will\_manager/will\_publisher.h](/src/will_manager/will_publisher.h) | C++ | 30 | 89 | 16 | 135 |
| [src/will\_manager/will\_store.cpp](/src/will_manager/will_store.cpp) | C++ | 18 | 0 | 8 | 26 |
| [src/will\_manager/will\_store.h](/src/will_manager/will_store.h) | C++ | 20 | 38 | 11 | 69 |

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details